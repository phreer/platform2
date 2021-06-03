// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/modem_qrtr.h"

#include <algorithm>
#include <array>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <libqrtr.h>

#include "hermes/apdu.h"
#include "hermes/dms_cmd.h"
#include "hermes/euicc_manager_interface.h"
#include "hermes/sgp_22.h"
#include "hermes/socket_qrtr.h"
#include "hermes/type_traits.h"
#include "hermes/uim_cmd.h"

namespace {

// This represents the default logical slot that we want our eSIM to be
// assigned. For dual sim - single standby modems, this will always work. For
// other multi-sim modems, get the first active slot and store it as a ModemQrtr
// field.
constexpr uint8_t kDefaultLogicalSlot = 0x01;
constexpr uint8_t kInvalidChannel = -1;

constexpr int kEidLen = 16;
constexpr char bcd_chars[] = "0123456789\0\0\0\0\0\0";

// A profile enable/disable results in an automatic refresh.
// Block QMI messages during this refresh. If the refresh
// takes any longer, Hermes will retry channel acquisition
// after kInitRetryDelay
constexpr auto kSimRefreshDelay = base::TimeDelta::FromSeconds(3);

constexpr auto kInitRetryDelay = base::TimeDelta::FromSeconds(10);
constexpr int kMaxRetries = 5;

constexpr int kQmiSuccess = 0;
// This error will be returned when a received qmi message cannot be parsed
// or when it is received in an unexpected state.
constexpr int kQmiMessageProcessingError = -1;

bool CheckMessageSuccess(UimCmd cmd, const uim_qmi_result& qmi_result) {
  if (qmi_result.result == 0) {
    return true;
  }

  LOG(ERROR) << cmd.ToString()
             << " response contained error: " << qmi_result.error;
  return false;
}
constexpr uint16_t kErrorNoEffect = 26;

void PrintQmiProcessingResult(int err) {
  VLOG(2) << "Rx QMI message processed with code:" << err;
}

void RunNextStep(
    base::OnceCallback<void(base::OnceCallback<void(int)>)> next_step,
    base::OnceCallback<void(int)> cb,
    int err) {
  VLOG(2) << "QMI message processed with code:" << err;
  if (err) {
    std::move(cb).Run(err);
    return;
  }
  std::move(next_step).Run(std::move(cb));
}

}  // namespace

namespace hermes {

struct ApduTxInfo : public ModemQrtr::TxInfo {
  explicit ApduTxInfo(CommandApdu apdu) : apdu_(std::move(apdu)) {}
  CommandApdu apdu_;
};

struct SwitchSlotTxInfo : public ModemQrtr::TxInfo {
  explicit SwitchSlotTxInfo(const uint32_t physical_slot,
                            const uint8_t logical_slot)
      : physical_slot_(physical_slot), logical_slot_(logical_slot) {}
  const uint32_t physical_slot_;
  const uint8_t logical_slot_;
};

std::unique_ptr<ModemQrtr> ModemQrtr::Create(
    std::unique_ptr<SocketInterface> socket,
    Logger* logger,
    Executor* executor) {
  // Open the socket prior to passing to ModemQrtr, such that it always has a
  // valid socket to write to.
  if (!socket || !socket->Open()) {
    LOG(ERROR) << "Failed to open socket";
    return nullptr;
  }
  return std::unique_ptr<ModemQrtr>(
      new ModemQrtr(std::move(socket), logger, executor));
}

ModemQrtr::ModemQrtr(std::unique_ptr<SocketInterface> socket,
                     Logger* logger,
                     Executor* executor)
    : qmi_disabled_(false),
      retry_count_(0),
      extended_apdu_supported_(false),
      current_transaction_id_(static_cast<uint16_t>(-1)),
      channel_(kInvalidChannel),
      logical_slot_(kDefaultLogicalSlot),
      procedure_bytes_mode_(ProcedureBytesMode::EnableIntermediateBytes),
      socket_(std::move(socket)),
      buffer_(4096),
      euicc_manager_(nullptr),
      logger_(logger),
      executor_(executor),
      weak_factory_(this) {
  CHECK(socket_);
  CHECK(socket_->IsValid());
  socket_->SetDataAvailableCallback(
      base::Bind(&ModemQrtr::OnDataAvailable, weak_factory_.GetWeakPtr()));

  // Set SGP.22 specification version supported by this implementation (this is
  // not currently constrained by the eUICC we use).
  spec_version_.set_major(2);
  spec_version_.set_minor(2);
  spec_version_.set_revision(0);

  // DMS callbacks
  qmi_rx_callbacks_[{QmiCmdInterface::Service::kDms,
                     DmsCmd::QmiType::kGetDeviceSerialNumbers}] =
      base::Bind(&ModemQrtr::ReceiveQmiGetSerialNumbers,
                 base::Unretained(this));

  // UIM callbacks
  qmi_rx_callbacks_[{QmiCmdInterface::Service::kUim, UimCmd::QmiType::kReset}] =
      base::Bind(&ModemQrtr::ReceiveQmiReset, base::Unretained(this));
  qmi_rx_callbacks_[{QmiCmdInterface::Service::kUim,
                     UimCmd::QmiType::kSendApdu}] =
      base::Bind(&ModemQrtr::ReceiveQmiSendApdu, base::Unretained(this));
  qmi_rx_callbacks_[{QmiCmdInterface::Service::kUim,
                     UimCmd::QmiType::kSwitchSlot}] =
      base::Bind(&ModemQrtr::ReceiveQmiSwitchSlot, base::Unretained(this));
  qmi_rx_callbacks_[{QmiCmdInterface::Service::kUim,
                     UimCmd::QmiType::kGetSlots}] =
      base::Bind(&ModemQrtr::ReceiveQmiGetSlots, base::Unretained(this));
  qmi_rx_callbacks_[{QmiCmdInterface::Service::kUim,
                     UimCmd::QmiType::kOpenLogicalChannel}] =
      base::Bind(&ModemQrtr::ReceiveQmiOpenLogicalChannel,
                 base::Unretained(this));
}

ModemQrtr::~ModemQrtr() {
  Shutdown();
  socket_->Close();
}

void ModemQrtr::SetActiveSlot(const uint32_t physical_slot, ResultCallback cb) {
  LOG(INFO) << __func__ << " physical_slot:" << physical_slot;
  if (stored_active_slot_ && stored_active_slot_.value() == physical_slot) {
    LOG(INFO) << "Requested slot is already active";
    AcquireChannel(std::move(cb));
    return;
  }
  auto acquire_channel =
      base::BindOnce(&ModemQrtr::AcquireChannel, weak_factory_.GetWeakPtr());
  tx_queue_.push_front(
      {std::make_unique<SwitchSlotTxInfo>(physical_slot, logical_slot_),
       AllocateId(), std::make_unique<UimCmd>(UimCmd::QmiType::kSwitchSlot),
       base::BindOnce(&RunNextStep, std::move(acquire_channel),
                      std::move(cb))});
  TransmitFromQueue();
}

void ModemQrtr::StoreAndSetActiveSlot(uint32_t physical_slot,
                                      ResultCallback cb) {
  LOG(INFO) << __func__ << " physical_slot:" << physical_slot;
  auto set_active_slot = base::BindOnce(
      &ModemQrtr::SetActiveSlot, weak_factory_.GetWeakPtr(), physical_slot);
  tx_queue_.push_back({std::make_unique<TxInfo>(), AllocateId(),
                       std::make_unique<UimCmd>(UimCmd::QmiType::kGetSlots),
                       base::BindOnce(&RunNextStep, std::move(set_active_slot),
                                      std::move(cb))});
  TransmitFromQueue();
}

void ModemQrtr::RestoreActiveSlot(ResultCallback cb) {
  LOG(INFO) << __func__;
  if (!stored_active_slot_) {
    LOG(ERROR) << "Attempted to restore active slot when none was stored";
    return;
  }
  tx_queue_.push_back({std::make_unique<SwitchSlotTxInfo>(
                           stored_active_slot_.value(), logical_slot_),
                       AllocateId(),
                       std::make_unique<UimCmd>(UimCmd::QmiType::kSwitchSlot),
                       std::move(cb)});
  stored_active_slot_.reset();
  TransmitFromQueue();
}

void ModemQrtr::SendApdusResponse(ResponseCallback callback, int err) {
  callback(responses_, err);
  // ResponseCallback interface does not indicate a change in ownership of
  // |responses_|, but all callbacks should transfer ownership.
  CHECK(responses_.empty());
}

void ModemQrtr::SendApdus(std::vector<lpa::card::Apdu> apdus,
                          ResponseCallback cb) {
  base::OnceCallback<void(int)> callback;
  callback = base::BindOnce(&ModemQrtr::SendApdusResponse,
                            weak_factory_.GetWeakPtr(), std::move(cb));
  for (size_t i = 0; i < apdus.size(); ++i) {
    CommandApdu apdu(static_cast<ApduClass>(apdus[i].cla()),
                     static_cast<ApduInstruction>(apdus[i].ins()),
                     extended_apdu_supported_);
    apdu.AddData(apdus[i].data());
    tx_queue_.push_back(
        {std::make_unique<ApduTxInfo>(std::move(apdu)), AllocateId(),
         std::make_unique<UimCmd>(UimCmd::QmiType::kSendApdu),
         i == apdus.size() - 1 ? std::move(callback)
                               : base::Bind(&PrintQmiProcessingResult)});
  }
  // Begin transmitting if we are not already processing a transaction.
  if (!pending_response_type_) {
    TransmitFromQueue();
  }
}

bool ModemQrtr::IsSimValidAfterEnable() {
  // This function is called by the lpa after profile enable.
  return true;
}

bool ModemQrtr::IsSimValidAfterDisable() {
  // This function is called by the lpa after profile disable.
  return true;
}

void ModemQrtr::Initialize(EuiccManagerInterface* euicc_manager,
                           ResultCallback cb) {
  LOG(INFO) << __func__;
  CHECK(current_state_ == State::kUninitialized);
  retry_initialization_callback_.Reset();
  euicc_manager_ = euicc_manager;
  if (!socket_->StartService(QmiCmdInterface::Service::kDms, 1, 0)) {
    LOG(ERROR) << "Failed starting DMS service during ModemQrtr initialization";
    RetryInitialization(std::move(cb));
    return;
  }
  init_done_cb_ = std::move(cb);
  current_state_.Transition(State::kInitializeStarted);
}

void ModemQrtr::InitializeUim() {
  LOG(INFO) << __func__;
  // StartService should result in a received QRTR_TYPE_NEW_SERVER
  // packet. Don't send other packets until that occurs.
  if (!socket_->StartService(QmiCmdInterface::Service::kUim, 1, 0)) {
    LOG(ERROR) << "Failed starting UIM service during ModemQrtr initialization";
    RetryInitialization(std::move(init_done_cb_));
    return;
  }
}

void ModemQrtr::AcquireChannel(base::OnceCallback<void(int)> cb) {
  LOG(INFO) << "Acquiring Channel";
  if (current_state_ != State::kUimStarted) {
    LOG(ERROR) << "Cannot acquire channel before initialization. Retrying "
                  "initialization...";
    auto acquire_channel =
        base::BindOnce(&ModemQrtr::AcquireChannel, weak_factory_.GetWeakPtr());
    // We need to acquire a channel immediately after initialization.
    RetryInitialization(base::BindOnce(&RunNextStep, std::move(acquire_channel),
                                       std::move(cb)));
    return;
  }

  channel_ = kInvalidChannel;
  auto send_open_logical_channel = base::BindOnce(
      &ModemQrtr::SendOpenLogicalChannel, weak_factory_.GetWeakPtr());
  tx_queue_.push_front(
      {std::unique_ptr<TxInfo>(), AllocateId(),
       std::make_unique<UimCmd>(UimCmd::QmiType::kReset),
       base::BindOnce(&RunNextStep, std::move(send_open_logical_channel),
                      std::move(cb))});
  TransmitFromQueue();
}

void ModemQrtr::SendOpenLogicalChannel(base::OnceCallback<void(int)> cb) {
  tx_queue_.push_front(
      {std::unique_ptr<TxInfo>(), AllocateId(),
       std::make_unique<UimCmd>(UimCmd::QmiType::kOpenLogicalChannel),
       std::move(cb)});
  TransmitFromQueue();
}

void ModemQrtr::RetryInitialization(ResultCallback cb) {
  if (retry_count_ > kMaxRetries) {
    LOG(INFO) << __func__ << ": Max retry count(" << kMaxRetries
              << ") exceeded, will not retry initialization.";
    retry_count_ = 0;
    while (!tx_queue_.empty()) {
      std::move(tx_queue_[0].cb_).Run(kQmiMessageProcessingError);
      tx_queue_.pop_front();
    }
    std::move(cb).Run(kQmiMessageProcessingError);
    return;
  }
  LOG(INFO) << "Reprobing for eSIM in " << kInitRetryDelay.InSeconds()
            << " seconds";
  Shutdown();
  retry_initialization_callback_.Reset();
  retry_initialization_callback_ =
      base::BindOnce(&ModemQrtr::Initialize, weak_factory_.GetWeakPtr(),
                     euicc_manager_, std::move(cb));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, std::move(retry_initialization_callback_), kInitRetryDelay);
  retry_count_++;
}

void ModemQrtr::Shutdown() {
  LOG(INFO) << __func__;
  if (current_state_ != State::kUninitialized &&
      current_state_ != State::kInitializeStarted) {
    socket_->StopService(to_underlying(QmiCmdInterface::Service::kUim), 1, 0);
    socket_->StopService(to_underlying(QmiCmdInterface::Service::kDms), 1, 0);
  }
  qrtr_table_.clear();
  current_state_.Transition(State::kUninitialized);
}

uint16_t ModemQrtr::AllocateId() {
  // transaction id cannot be 0, but when incrementing by 1, an overflow will
  // result in this method at some point returning 0. Incrementing by 2 when
  // transaction_id is initialized as an odd number guarantees us that this
  // method will never return 0 without special-casing the overflow.
  current_transaction_id_ += 2;
  return current_transaction_id_;
}

/////////////////////////////////////
// Transmit method implementations //
/////////////////////////////////////

void ModemQrtr::TransmitFromQueue() {
  if (tx_queue_.empty() || pending_response_type_ || qmi_disabled_ ||
      retry_initialization_callback_) {
    return;
  }

  switch (tx_queue_[0].qmi_msg_->service()) {
    case QmiCmdInterface::Service::kUim:
      TransmitUimCmdFromQueue();
      break;
    case QmiCmdInterface::Service::kDms:
      TransmitDmsCmdFromQueue();
      break;
  }
}

void ModemQrtr::TransmitDmsCmdFromQueue() {
  auto qmi_cmd = tx_queue_[0].qmi_msg_.get();
  CHECK(qmi_cmd->service() == QmiCmdInterface::Service::kDms)
      << "Attempted to send non-DMS command in " << __func__;
  switch (qmi_cmd->qmi_type()) {
    case DmsCmd::QmiType::kGetDeviceSerialNumbers:
      dms_get_device_serial_numbers_req imei_request;
      SendCommand(tx_queue_[0].qmi_msg_.get(), tx_queue_[0].id_, &imei_request,
                  dms_get_device_serial_numbers_req_ei);
      break;
    default:
      LOG(ERROR) << "Unexpected QMI DMS type in ModemQrtr tx queue";
  }
}

void ModemQrtr::TransmitUimCmdFromQueue() {
  auto qmi_cmd = tx_queue_[0].qmi_msg_.get();
  CHECK(qmi_cmd->service() == QmiCmdInterface::Service::kUim)
      << "Attempted to send non-UIM command in " << __func__;
  switch (qmi_cmd->qmi_type()) {
    case UimCmd::QmiType::kReset:
      uim_reset_req reset_request;
      SendCommand(tx_queue_[0].qmi_msg_.get(), tx_queue_[0].id_, &reset_request,
                  uim_reset_req_ei);
      break;
    case UimCmd::QmiType::kSwitchSlot:
      // Don't pop since we need to update the inactive euicc if SwitchSlot
      // succeeds
      TransmitQmiSwitchSlot(&tx_queue_[0]);
      break;
    case UimCmd::QmiType::kGetSlots:
      uim_get_slots_req slots_request;
      SendCommand(tx_queue_[0].qmi_msg_.get(), tx_queue_[0].id_, &slots_request,
                  uim_get_slots_req_ei);
      break;
    case UimCmd::QmiType::kOpenLogicalChannel:
      TransmitQmiOpenLogicalChannel(&tx_queue_[0]);
      break;
    case UimCmd::QmiType::kSendApdu:
      TransmitQmiSendApdu(&tx_queue_[0]);
      break;
    default:
      LOG(ERROR) << "Unexpected QMI UIM type in ModemQrtr tx queue";
  }
}

void ModemQrtr::TransmitQmiSwitchSlot(TxElement* tx_element) {
  auto switch_slot_tx_info =
      dynamic_cast<SwitchSlotTxInfo*>(tx_queue_[0].info_.get());
  // Slot switching takes time, thus switch slots only when absolutely necessary
  if (!stored_active_slot_ ||
      stored_active_slot_.value() != switch_slot_tx_info->physical_slot_) {
    uim_switch_slot_req switch_slot_request;
    switch_slot_request.physical_slot = switch_slot_tx_info->physical_slot_;
    switch_slot_request.logical_slot = switch_slot_tx_info->logical_slot_;
    SendCommand(tx_queue_[0].qmi_msg_.get(), tx_queue_[0].id_,
                &switch_slot_request, uim_switch_slot_req_ei);
  } else {
    LOG(INFO) << "Requested slot is already active";
    tx_queue_.pop_front();
    TransmitFromQueue();
  }
}

void ModemQrtr::TransmitQmiOpenLogicalChannel(TxElement* tx_element) {
  DCHECK(tx_element);
  DCHECK(tx_element->qmi_msg_->qmi_type() ==
         UimCmd::QmiType::kOpenLogicalChannel);

  uim_open_logical_channel_req request;
  request.slot = logical_slot_;
  request.aid_valid = true;
  request.aid_len = kAidIsdr.size();
  std::copy(kAidIsdr.begin(), kAidIsdr.end(), request.aid);

  SendCommand(tx_element->qmi_msg_.get(), tx_element->id_, &request,
              uim_open_logical_channel_req_ei);
}

void ModemQrtr::TransmitQmiSendApdu(TxElement* tx_element) {
  DCHECK(tx_element);
  DCHECK(tx_element->qmi_msg_->qmi_type() == UimCmd::QmiType::kSendApdu);

  uim_send_apdu_req request;
  request.slot = logical_slot_;
  request.channel_id_valid = true;
  request.channel_id = channel_;
  request.procedure_bytes_valid = true;
  request.procedure_bytes = to_underlying(procedure_bytes_mode_);

  uint8_t* fragment;
  ApduTxInfo* apdu = static_cast<ApduTxInfo*>(tx_element->info_.get());
  size_t fragment_size = apdu->apdu_.GetNextFragment(&fragment);
  request.apdu_len = fragment_size;
  std::copy(fragment, fragment + fragment_size, request.apdu);

  SendCommand(tx_element->qmi_msg_.get(), tx_element->id_, &request,
              uim_send_apdu_req_ei);
}

bool ModemQrtr::SendCommand(QmiCmdInterface* qmi_command,
                            uint16_t id,
                            void* c_struct,
                            qmi_elem_info* ei) {
  if (!socket_->IsValid()) {
    LOG(ERROR) << "ModemQrtr socket is invalid!";
    return false;
  }
  if (pending_response_type_) {
    LOG(ERROR) << "QRTR tried to send buffer while awaiting a qmi response";
    return false;
  }
  if (qmi_command->service() == QmiCmdInterface::Service::kUim &&
      current_state_ != State::kUimStarted) {
    LOG(ERROR) << "QRTR tried to send UIM message in state: " << current_state_;
    return false;
  }
  if (qmi_command->service() == QmiCmdInterface::Service::kDms &&
      current_state_ != State::kDmsStarted) {
    LOG(ERROR) << "QRTR tried to send DMS message in state: " << current_state_;
    return false;
  }
  if (!qrtr_table_.ContainsService(qmi_command->service())) {
    LOG(ERROR) << "Tried sending to unknown service:" << qmi_command->service();
    return false;
  }
  if (qmi_command->service() == QmiCmdInterface::Service::kUim &&
      (qmi_command->qmi_type() == UimCmd::QmiType::kSendApdu) &&
      channel_ == kInvalidChannel) {
    LOG(ERROR) << "QRTR tried to send apdu when channel is invalid";
    return false;
  }

  std::vector<uint8_t> encoded_buffer(kBufferDataSize * 2, 0);
  qrtr_packet packet;
  packet.data = encoded_buffer.data();
  packet.data_len = encoded_buffer.size();

  size_t len = qmi_encode_message(&packet, QMI_REQUEST, qmi_command->qmi_type(),
                                  id, c_struct, ei);
  if (len < 0) {
    LOG(ERROR) << "Failed to encode QMI UIM request: "
               << qmi_command->qmi_type();
    return false;
  }

  LOG(INFO) << "ModemQrtr sending transaction type " << qmi_command->qmi_type()
            << " with data (size : " << packet.data_len
            << ") : " << base::HexEncode(packet.data, packet.data_len);

  int success = -1;
  success =
      socket_->Send(packet.data, packet.data_len,
                    reinterpret_cast<const void*>(
                        &qrtr_table_.GetMetadata(qmi_command->service())));
  if (success < 0) {
    LOG(ERROR) << "qrtr_sendto failed";
    return false;
  }

  switch (qmi_command->service()) {
    case QmiCmdInterface::Service::kDms:
      pending_response_type_ = std::make_unique<DmsCmd>(
          static_cast<DmsCmd::QmiType>(qmi_command->qmi_type()));
      break;
    case QmiCmdInterface::Service::kUim:
      pending_response_type_ = std::make_unique<UimCmd>(
          static_cast<UimCmd::QmiType>(qmi_command->qmi_type()));
      break;
    default:
      CHECK(false) << "Unknown service: " << qmi_command->service();
      return false;
  }
  return true;
}

////////////////////////////////////
// Receive method implementations //
////////////////////////////////////

void ModemQrtr::ProcessQrtrPacket(uint32_t node, uint32_t port, int size) {
  sockaddr_qrtr qrtr_sock;
  qrtr_sock.sq_family = AF_QIPCRTR;
  qrtr_sock.sq_node = node;
  qrtr_sock.sq_port = port;

  qrtr_packet pkt;
  int ret = qrtr_decode(&pkt, buffer_.data(), size, &qrtr_sock);
  if (ret < 0) {
    LOG(ERROR) << "qrtr_decode failed";
    return;
  }

  switch (pkt.type) {
    case QRTR_TYPE_NEW_SERVER:
      LOG(INFO) << "Received NEW_SERVER QRTR packet";
      if (pkt.service == QmiCmdInterface::Service::kUim) {
        current_state_.Transition(State::kUimStarted);
        qrtr_table_.Insert(QmiCmdInterface::Service::kUim,
                           {pkt.port, pkt.node});
        VLOG(2) << "Stored UIM metadata";
        // Request initial info about SIM slots.
        // TODO(crbug.com/1085825) Add support for getting indications so that
        // this info can get updated.
        auto send_reset =
            base::BindOnce(&ModemQrtr::SendReset, weak_factory_.GetWeakPtr());
        tx_queue_.push_front(
            {std::make_unique<TxInfo>(), AllocateId(),
             std::make_unique<UimCmd>(UimCmd::QmiType::kGetSlots),
             base::BindOnce(&ModemQrtr::RunNextStepOrRetry,
                            weak_factory_.GetWeakPtr(), std::move(send_reset),
                            std::move(init_done_cb_))});
      }
      if (pkt.service == QmiCmdInterface::Service::kDms) {
        current_state_.Transition(State::kDmsStarted);
        qrtr_table_.Insert(QmiCmdInterface::Service::kDms,
                           {pkt.port, pkt.node});
        VLOG(2) << "Stored DMS metadata";

        // We get imei on a best effort basis, we will initialize uim even if it
        // does not succeed.
        base::OnceClosure initialize_uim = base::BindOnce(
            &ModemQrtr::InitializeUim, weak_factory_.GetWeakPtr());
        tx_queue_.push_front(
            {std::make_unique<TxInfo>(), AllocateId(),
             std::make_unique<DmsCmd>(DmsCmd::QmiType::kGetDeviceSerialNumbers),
             base::BindOnce(&IgnoreErrorRunClosure,
                            std::move(initialize_uim))});
      }
      break;
    case QRTR_TYPE_DATA:
      VLOG(1) << "Received data QRTR packet";
      ProcessQmiPacket(pkt);
      break;
    case QRTR_TYPE_DEL_SERVER:
    case QRTR_TYPE_HELLO:
    case QRTR_TYPE_BYE:
    case QRTR_TYPE_DEL_CLIENT:
    case QRTR_TYPE_RESUME_TX:
    case QRTR_TYPE_EXIT:
    case QRTR_TYPE_PING:
    case QRTR_TYPE_NEW_LOOKUP:
    case QRTR_TYPE_DEL_LOOKUP:
      LOG(INFO) << "Received QRTR packet of type " << pkt.type << ". Ignoring.";
      break;
    default:
      LOG(WARNING) << "Received QRTR packet but did not recognize packet type "
                   << pkt.type << ".";
  }
  // If we cannot yet send another request, it is because we are waiting for a
  // response. After the response is received and processed, the next request
  // will be sent.
  if (!pending_response_type_) {
    TransmitFromQueue();
  }
}

void ModemQrtr::RunNextStepOrRetry(
    base::OnceCallback<void(base::OnceCallback<void(int)>)> next_step,
    base::OnceCallback<void(int)> cb,
    int err) {
  if (err) {
    RetryInitialization(std::move(cb));
    return;
  }
  RunNextStep(std::move(next_step), std::move(cb), err);
}

void ModemQrtr::SendReset(ResultCallback cb) {
  tx_queue_.push_front({std::unique_ptr<TxInfo>(), AllocateId(),
                        std::make_unique<UimCmd>(UimCmd::QmiType::kReset),
                        std::move(cb)});
  TransmitFromQueue();
}

void ModemQrtr::ProcessQmiPacket(const qrtr_packet& packet) {
  uint32_t qmi_type;
  if (qmi_decode_header(&packet, &qmi_type) < 0) {
    LOG(ERROR) << "QRTR received invalid QMI packet";
    return;
  }
  QmiCmdInterface::Service service =
      qrtr_table_.GetService({packet.port, packet.node});
  VLOG(2) << "Received QMI message of type: " << qmi_type
          << " from service: " << service;

  if (!pending_response_type_) {
    LOG(ERROR) << "Received unexpected QMI response. No pending response.";
    return;
  }

  if (qmi_rx_callbacks_.find({service, qmi_type}) == qmi_rx_callbacks_.end()) {
    LOG(WARNING) << "Unknown QMI message of type: " << qmi_type
                 << " from service: " << service;
    return;
  }

  int err = qmi_rx_callbacks_[{service, qmi_type}].Run(packet);

  if (pending_response_type_->service() != service)
    LOG(ERROR) << "Received unexpected QMI response. Expected service: "
               << pending_response_type_->service()
               << " Actual service: " << service;
  if (pending_response_type_->qmi_type() != qmi_type)
    LOG(ERROR) << "Received unexpected QMI response. Expected type: "
               << pending_response_type_->qmi_type()
               << " Actual type:" << qmi_type;
  pending_response_type_.reset();

  // Most elements in the queue are simple qmi messages, which means they can
  // popped immediately after a response. Apdu's may be sent in fragments, which
  // means we pop them only after responses to all fragments have been received
  // in ReceiveQmiSendApdu
  if (service == QmiCmdInterface::kUim &&
      qmi_type == UimCmd::QmiType::kSendApdu)
    return;
  // pop before running the callback since the callback might change the state
  // of the queue.
  auto cb_ = std::move(tx_queue_[0].cb_);
  tx_queue_.pop_front();
  if (!cb_.is_null())
    std::move(cb_).Run(err);
}

int ModemQrtr::ReceiveQmiGetSlots(const qrtr_packet& packet) {
  UimCmd cmd(UimCmd::QmiType::kGetSlots);
  uim_get_slots_resp resp;
  unsigned int id;
  if (qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd.qmi_type(),
                         uim_get_slots_resp_ei) < 0) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
    return kQmiMessageProcessingError;
  } else if (!CheckMessageSuccess(cmd, resp.result)) {
    return resp.result.error;
  }

  if (!resp.status_valid || !resp.slot_info_valid) {
    LOG(ERROR) << "QMI UIM response for " << cmd.ToString()
               << " contained invalid slot info";
    return kQmiMessageProcessingError;
  }

  CHECK(euicc_manager_);
  bool logical_slot_found = false;
  uint8_t min_len = std::min(std::min(resp.status_len, resp.slot_info_len),
                             resp.eid_info_len);
  if (resp.status_len != resp.slot_info_len ||
      resp.status_len != resp.eid_info_len) {
    LOG(ERROR) << "Lengths of status, slot_info and eid_info differ,"
               << " slot_info_len:" << resp.slot_info_len
               << " status_len:" << resp.status_len
               << " eid_info_len:" << resp.eid_info_len;
  }

  bool euicc_found = false;
  for (uint8_t i = 0; i < min_len; ++i) {
    bool is_present = (resp.status[i].physical_card_status ==
                       uim_physical_slot_status::kCardPresent);
    bool is_euicc = resp.slot_info[i].is_euicc;

    bool is_active = (resp.status[i].physical_slot_state ==
                      uim_physical_slot_status::kSlotActive);

    LOG(INFO) << "Slot:" << i + 1 << " is_present:" << is_present
              << " is_euicc:" << is_euicc << " is_active:" << is_active;
    if (is_active) {
      stored_active_slot_ = i + 1;
      if (!logical_slot_found) {
        // This is the logical slot we grab when we perform a switch slot
        logical_slot_ = resp.status[i].logical_slot;
        logical_slot_found = true;
      }
    }
    if (!is_present || !is_euicc) {
      // TODO(b/184541133): Call euicc_manager_->OnEuiccRemoved(i + 1); once
      // firmware gives correct status.
      continue;
    }

    euicc_found = true;
    std::string eid;
    if (resp.eid_info[i].eid_len != kEidLen)
      LOG(ERROR) << "Expected eid_len=" << kEidLen << ", eid_len is "
                 << resp.eid_info[i].eid_len;
    for (int j = 0; j < resp.eid_info[i].eid_len; j++) {
      eid += bcd_chars[(resp.eid_info[i].eid[j] >> 4) & 0xF];
      eid += bcd_chars[resp.eid_info[i].eid[j] & 0xF];
      if (j == 0) {
        CHECK(eid == "89") << "Expected eid to begin with 89, eid begins with "
                           << eid;
      }
    }

    VLOG(2) << "EID for slot " << i + 1 << " is " << eid;
    euicc_manager_->OnEuiccUpdated(
        i + 1, is_active
                   ? EuiccSlotInfo(resp.status[i].logical_slot, std::move(eid))
                   : EuiccSlotInfo(std::move(eid)));
  }

  if (!euicc_found) {
    LOG(ERROR) << "Expected to find an eSIM ...";
    return kQmiMessageProcessingError;
  }
  return kQmiSuccess;
}

int ModemQrtr::ReceiveQmiSwitchSlot(const qrtr_packet& packet) {
  UimCmd cmd(UimCmd::QmiType::kSwitchSlot);
  uim_switch_slot_resp resp;
  unsigned int id;

  if (qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd.qmi_type(),
                         uim_switch_slot_resp_ei) < 0) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
    return kQmiMessageProcessingError;
  }

  if (!CheckMessageSuccess(cmd, resp.result) &&
      resp.result.error != kErrorNoEffect) {
    return resp.result.error;
  }

  auto switch_slot_tx_info =
      dynamic_cast<SwitchSlotTxInfo*>(tx_queue_.front().info_.get());
  euicc_manager_->OnLogicalSlotUpdated(switch_slot_tx_info->physical_slot_,
                                       switch_slot_tx_info->logical_slot_);
  if (stored_active_slot_)
    euicc_manager_->OnLogicalSlotUpdated(stored_active_slot_.value(),
                                         base::nullopt);

  // Sending QMI messages immediately after switch slot leads to QMI errors
  // since slot switching takes time. If channel reacquisition fails despite
  // this delay, we retry after kInitRetryDelay.
  DisableQmi(kSwitchSlotDelay);
  return kQmiSuccess;
}

int ModemQrtr::ReceiveQmiGetSerialNumbers(const qrtr_packet& packet) {
  DmsCmd cmd(DmsCmd::QmiType::kGetDeviceSerialNumbers);
  if (current_state_ != State::kDmsStarted) {
    LOG(ERROR) << "Received unexpected QMI DMS response: " << cmd.ToString()
               << " in state " << current_state_;
    return kQmiMessageProcessingError;
  }

  dms_get_device_serial_numbers_resp resp;
  unsigned int id;
  if (qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd.qmi_type(),
                         dms_get_device_serial_numbers_resp_ei) < 0) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
  }

  if (resp.result.result != 0) {
    LOG(ERROR) << cmd.ToString() << " Could not decode imei"
               << resp.result.error;
  }

  if (!resp.imei_valid) {
    LOG(ERROR) << "QMI UIM response for " << cmd.ToString()
               << " contained an invalid imei";
  }

  imei_ = resp.imei;
  VLOG(2) << "IMEI: " << imei_;
  // We always return success since imei_ is not used by most SMDPs.
  return kQmiSuccess;
}

int ModemQrtr::ReceiveQmiReset(const qrtr_packet& packet) {
  current_state_.Transition(State::kUimStarted);
  VLOG(1) << "Ignoring received RESET packet";
  return kQmiSuccess;
}

int ModemQrtr::ReceiveQmiOpenLogicalChannel(const qrtr_packet& packet) {
  LOG(INFO) << __func__;
  int err = ParseQmiOpenLogicalChannel(packet);
  if (err) {
    LOG(ERROR) << "Logical channel could not be opened, retrying...";
    Shutdown();
    auto retry_acquire_channel =
        base::BindOnce(&ModemQrtr::AcquireChannel, weak_factory_.GetWeakPtr(),
                       std::move(tx_queue_[0].cb_));
    tx_queue_[0].cb_ = base::BindOnce(&IgnoreErrorRunClosure,
                                      std::move(retry_acquire_channel));
  }
  return err;
}

int ModemQrtr::ParseQmiOpenLogicalChannel(const qrtr_packet& packet) {
  LOG(INFO) << __func__;
  UimCmd cmd(UimCmd::QmiType::kOpenLogicalChannel);
  if (current_state_ != State::kUimStarted) {
    LOG(ERROR) << "Received unexpected QMI UIM response: " << cmd.ToString()
               << " in state " << current_state_;
    return kQmiMessageProcessingError;
  }

  uim_open_logical_channel_resp resp;
  unsigned int id;
  if (qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd.qmi_type(),
                         uim_open_logical_channel_resp_ei) < 0) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
    return kQmiMessageProcessingError;
  }

  if (resp.result.result != 0) {
    LOG(ERROR)
        << cmd.ToString()
        << " Could not open channel to eSIM. QMI response contained error: "
        << resp.result.error;
    return resp.result.error;
  }

  if (!resp.channel_id_valid) {
    LOG(ERROR) << "QMI UIM response for " << cmd.ToString()
               << " contained an invalid channel id";
    return kQmiMessageProcessingError;
  }

  channel_ = resp.channel_id;
  return kQmiSuccess;
}

int ModemQrtr::ReceiveQmiSendApdu(const qrtr_packet& packet) {
  UimCmd cmd(UimCmd::QmiType::kSendApdu);
  CHECK(tx_queue_.size());
  // Ensure that the queued element is for a kSendApdu command
  TxInfo* base_info = tx_queue_[0].info_.get();
  CHECK(base_info);
  CHECK(dynamic_cast<ApduTxInfo*>(base_info));

  static ResponseApdu payload;
  uim_send_apdu_resp resp;
  unsigned int id;
  ApduTxInfo* info = static_cast<ApduTxInfo*>(base_info);
  if (!qmi_decode_message(&resp, &id, &packet, QMI_RESPONSE, cmd.qmi_type(),
                          uim_send_apdu_resp_ei)) {
    LOG(ERROR) << "Failed to decode QMI UIM response: " << cmd.ToString();
    return kQmiMessageProcessingError;
  }

  if (!CheckMessageSuccess(cmd, resp.result)) {
    std::move(tx_queue_[0].cb_).Run(lpa::card::EuiccCard::kSendApduError);
    // Pop the apdu that caused the error.
    tx_queue_.pop_front();
    AcquireChannel(base::OnceCallback<void(int)>());
    return resp.result.error;
  }

  VLOG(2) << "Adding to payload from APDU response ("
          << resp.apdu_response_len - 2 << " bytes): "
          << base::HexEncode(resp.apdu_response, resp.apdu_response_len - 2);
  payload.AddData(resp.apdu_response, resp.apdu_response_len);
  if (payload.MorePayloadIncoming()) {
    // Make the next transmit operation be a request for more APDU data
    info->apdu_ = payload.CreateGetMoreCommand(false);
    return kQmiSuccess;
  } else if (info->apdu_.HasMoreFragments()) {
    // Send next fragment of APDU
    VLOG(1) << "Sending next APDU fragment...";
    TransmitFromQueue();
    return kQmiSuccess;
  }

  if (tx_queue_.empty() || static_cast<uint16_t>(id) != tx_queue_[0].id_) {
    LOG(ERROR) << "ModemQrtr received APDU from modem with unrecognized "
               << "transaction ID";
    return kQmiMessageProcessingError;
  }

  VLOG(1) << "Finished transaction " << tx_queue_[0].id_ / 2
          << " (id: " << tx_queue_[0].id_ << ")";
  responses_.push_back(payload.Release());
  std::move(tx_queue_[0].cb_).Run(lpa::card::EuiccCard::kNoError);
  tx_queue_.pop_front();
  return kQmiSuccess;
}

void ModemQrtr::OnDataAvailable(SocketInterface* socket) {
  CHECK(socket == socket_.get());

  void* metadata = nullptr;
  SocketQrtr::PacketMetadata data = {0, 0};
  if (socket->GetType() == SocketInterface::Type::kQrtr) {
    metadata = reinterpret_cast<void*>(&data);
  }

  int bytes_received = socket->Recv(buffer_.data(), buffer_.size(), metadata);
  if (bytes_received < 0) {
    LOG(ERROR) << "Socket recv failed";
    return;
  }
  LOG(INFO) << "ModemQrtr recevied raw data (" << bytes_received
            << " bytes): " << base::HexEncode(buffer_.data(), bytes_received);
  ProcessQrtrPacket(data.node, data.port, bytes_received);
}

const lpa::proto::EuiccSpecVersion& ModemQrtr::GetCardVersion() {
  return spec_version_;
}

void ModemQrtr::SetProcedureBytes(
    const ProcedureBytesMode procedure_bytes_mode) {
  procedure_bytes_mode_ = procedure_bytes_mode;
}

bool ModemQrtr::State::Transition(ModemQrtr::State::Value value) {
  bool valid_transition = false;
  switch (value) {
    case kUninitialized:
      valid_transition = true;
      break;
    case kUimStarted:
      // We transition to kUimStarted just before acquiring a channel
      valid_transition = (value_ == kDmsStarted || value_ == kUimStarted);
      break;
    default:
      // Most states can only transition from the previous state.
      valid_transition = (value == value_ + 1);
  }

  if (valid_transition) {
    LOG(INFO) << "Transitioning from state " << *this << " to state "
              << State(value);
    value_ = value;
  } else {
    LOG(ERROR) << "Cannot transition from state " << *this << " to state "
               << State(value);
  }
  return valid_transition;
}

void ModemQrtr::DisableQmi(base::TimeDelta duration) {
  LOG(INFO) << __func__ << " for " << duration << "seconds";
  qmi_disabled_ = true;
  executor_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ModemQrtr::EnableQmi, weak_factory_.GetWeakPtr()),
      duration);
}

void ModemQrtr::EnableQmi() {
  LOG(INFO) << __func__;
  qmi_disabled_ = false;
  TransmitFromQueue();
}

void ModemQrtr::StartProfileOp(uint32_t physical_slot, ResultCallback cb) {
  LOG(INFO) << __func__ << " physical_slot:" << physical_slot;
  // The card triggers a refresh after profile enable. This refresh can cause
  // response apdu's with intermediate bytes to be flushed during a qmi
  // transaction. Since, we don't use these intermediate bytes, disable
  // them to avoid qmi errors as per QC's recommendation. b/169954635
  SetProcedureBytes(ProcedureBytesMode::DisableIntermediateBytes);
  StoreAndSetActiveSlot(physical_slot, std::move(cb));
}

void ModemQrtr::FinishProfileOp(ResultCallback cb) {
  LOG(INFO) << __func__;
  DisableQmi(kSimRefreshDelay);
  SetProcedureBytes(ProcedureBytesMode::EnableIntermediateBytes);
  AcquireChannel(std::move(cb));
}

void ModemQrtr::QrtrTable::Insert(QmiCmdInterface::Service service,
                                  SocketQrtr::PacketMetadata metadata) {
  qrtr_metadata_[service] = metadata;
  service_from_metadata_[metadata] = service;
}

void ModemQrtr::QrtrTable::clear() {
  qrtr_metadata_.clear();
  service_from_metadata_.clear();
}

const SocketQrtr::PacketMetadata& ModemQrtr::QrtrTable::GetMetadata(
    QmiCmdInterface::Service service) {
  return qrtr_metadata_[service];
}

const QmiCmdInterface::Service& ModemQrtr::QrtrTable::GetService(
    SocketQrtr::PacketMetadata metadata) {
  auto it = service_from_metadata_.find(metadata);
  CHECK(it != service_from_metadata_.end())
      << "Metadata not found in qrtr_table";
  return it->second;
}

bool ModemQrtr::QrtrTable::ContainsService(QmiCmdInterface::Service service) {
  return (qrtr_metadata_.find(service) != qrtr_metadata_.end());
}

}  // namespace hermes
