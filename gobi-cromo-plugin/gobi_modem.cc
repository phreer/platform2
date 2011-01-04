// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gobi_modem.h"
#include "gobi_modem_handler.h"
#include <sstream>

#include <dbus/dbus.h>

#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>

extern "C" {
#include <libudev.h>
#include <time.h>
};

#include <base/stringprintf.h>
#include <cromo/carrier.h>
#include <cromo/cromo_server.h>
#include <cromo/utilities.h>

#include <glog/logging.h>
#include <mm/mm-modem.h>

// This ought to be in a header file somewhere, but if it is, I can't find it.
#ifndef NDEBUG
static const int DEBUG = 1;
#else
static const int DEBUG = 0;
#endif


#define DEFINE_ERROR(name) \
  const char* k##name##Error = "org.chromium.ModemManager.Error." #name;
#define DEFINE_MM_ERROR(name) \
  const char* k##name##Error = \
    "org.freedesktop.ModemManager.Modem.Gsm." #name;

#include "gobi_modem_errors.h"
#undef DEFINE_ERROR
#undef DEFINE_MM_ERROR

static const size_t kDefaultBufferSize = 128;
static const char *kNetworkDriver = "QCUSBNet2k";
static const char *kFifoName = "/tmp/gobi-nmea";

using utilities::DBusPropertyMap;

// The following constants define the granularity with which signal
// strength is reported, i.e., the number of bars.
//
// The values given here are used to compute an array of thresholds
// consisting of the values [-113, -98, -83, -68, -53], which results
// in the following mapping of signal strength as reported in dBm to
// bars displayed:
//
// <  -113             0 bars
// >= -113 and < -98   1 bar
// >=  -98 and < -83   2 bars
// >=  -83 and < -68   3 bars
// >=  -68 and < -53   4 bars
// >=  -53             5 bars

// Any reported signal strength larger than or equal to kMaxSignalStrengthDbm
// is considered full strength.
static const int kMaxSignalStrengthDbm = -53;
// Any reported signal strength smaller than kMaxSignalStrengthDbm is
// considered zero strength.
static const int kMinSignalStrengthDbm = -113;
// The number of signal strength levels we want to report, ranging from
// 0 bars to kSignalStrengthNumLevels-1 bars.
static const int kSignalStrengthNumLevels = 6;

// Returns the current time, in milliseconds, from an unspecified epoch.
unsigned long long GobiModem::GetTimeMs(void) {
  struct timespec ts;
  unsigned long long rv;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  rv = ts.tv_sec;
  rv *= 1000;
  rv += ts.tv_nsec / 1000000;
  return rv;
}

unsigned long GobiModem::MapDbmToPercent(INT8 signal_strength_dbm) {
  unsigned long signal_strength_percent;

  if (signal_strength_dbm < kMinSignalStrengthDbm)
    signal_strength_percent = 0;
  else if (signal_strength_dbm >= kMaxSignalStrengthDbm)
    signal_strength_percent = 100;
  else
    signal_strength_percent =
        ((signal_strength_dbm - kMinSignalStrengthDbm) * 100 /
         (kMaxSignalStrengthDbm - kMinSignalStrengthDbm));
  return signal_strength_percent;
}

ULONG GobiModem::MapDataBearerToRfi(ULONG data_bearer_technology) {
  switch (data_bearer_technology) {
    case gobi::kDataBearerCdma1xRtt:
      return gobi::kRfiCdma1xRtt;
    case gobi::kDataBearerCdmaEvdo:
    case gobi::kDataBearerCdmaEvdoRevA:
      return gobi::kRfiCdmaEvdo;
    case gobi::kDataBearerGprs:
      return gobi::kRfiGsm;
    case gobi::kDataBearerWcdma:
    case gobi::kDataBearerEdge:
    case gobi::kDataBearerHsdpaDlWcdmaUl:
    case gobi::kDataBearerWcdmaDlUsupaUl:
    case gobi::kDataBearerHsdpaDlHsupaUl:
      return gobi::kRfiUmts;
    default:
      return gobi::kRfiCdmaEvdo;
  }
}

static struct udev_enumerate *enumerate_net_devices(struct udev *udev)
{
  int rc;
  struct udev_enumerate *udev_enumerate = udev_enumerate_new(udev);

  if (udev_enumerate == NULL) {
    return NULL;
  }

  rc = udev_enumerate_add_match_subsystem(udev_enumerate, "net");
  if (rc != 0) {
    udev_enumerate_unref(udev_enumerate);
    return NULL;
  }

  rc = udev_enumerate_scan_devices(udev_enumerate);
  if (rc != 0) {
    udev_enumerate_unref(udev_enumerate);
    return NULL;
  }
  return udev_enumerate;
}

GobiModem* GobiModem::connected_modem_(NULL);
GobiModemHandler* GobiModem::handler_;
GobiModem::mutex_wrapper_ GobiModem::modem_mutex_;

bool StartExitTrampoline(void *arg) {
  GobiModem *modem = static_cast<GobiModem*>(arg);
  return modem->StartExit();
}

bool ExitOkTrampoline(void *arg) {
  GobiModem *modem = static_cast<GobiModem*>(arg);
  return modem->is_disconnected();
}

bool SuspendOkTrampoline(void *arg) {
  GobiModem *modem = static_cast<GobiModem*>(arg);
  return modem->is_disconnected();
}

GobiModem::GobiModem(DBus::Connection& connection,
                     const DBus::Path& path,
                     const DEVICE_ELEMENT& device,
                     gobi::Sdk* sdk)
    : DBus::ObjectAdaptor(connection, path),
      sdk_(sdk),
      last_seen_(-1),
      nmea_fd_(-1),
      session_state_(gobi::kDisconnected),
      session_id_(0),
      suspending_(false),
      exiting_(false),
      device_resetting_(false) {
  memcpy(&device_, &device, sizeof(device_));
}

void GobiModem::Init() {
  sdk_->set_current_modem_path(path());
  metrics_lib_.reset(new MetricsLibrary());
  metrics_lib_->Init();

  // Initialize DBus object properties
  SetDeviceProperties();
  SetModemProperties();
  Enabled = false;
  IpMethod = MM_MODEM_IP_METHOD_DHCP;
  UnlockRequired = "";
  UnlockRetries = 999;

  carrier_ = handler_->server().FindCarrierNoOp();

  for (int i = 0; i < GOBI_EVENT_MAX; i++) {
    event_enabled[i] = false;
  }

  char name[64];
  void *thisp = static_cast<void*>(this);
  snprintf(name, sizeof(name), "gobi-%p", thisp);
  hooks_name_ = name;

  handler_->server().start_exit_hooks().Add(hooks_name_, StartExitTrampoline,
                                            this);
  handler_->server().exit_ok_hooks().Add(hooks_name_, ExitOkTrampoline, this);
  RegisterStartSuspend(hooks_name_);
  handler_->server().suspend_ok_hooks().Add(hooks_name_, SuspendOkTrampoline,
                                            this);
}

GobiModem::~GobiModem() {
  handler_->server().start_exit_hooks().Del(hooks_name_);
  handler_->server().exit_ok_hooks().Del(hooks_name_);
  handler_->server().UnregisterStartSuspend(hooks_name_);
  handler_->server().suspend_ok_hooks().Del(hooks_name_);

  ApiDisconnect();
}

enum {
  METRIC_INDEX_NONE = 0,
  METRIC_INDEX_QMI_HARDWARE_RESTRICTED,
  METRIC_INDEX_MAX
};

static unsigned int QCErrorToMetricIndex(unsigned int error) {
  if (error == gobi::kQMIHardwareRestricted)
    return METRIC_INDEX_QMI_HARDWARE_RESTRICTED;
  return METRIC_INDEX_NONE;
}

// DBUS Methods: Modem
void GobiModem::Enable(const bool& enable, DBus::Error& error) {
  LOG(INFO) << "Enable: " << Enabled() << " => " << enable;
  if (enable && !Enabled()) {
    ApiConnect(error);
    if (error.is_set())
      return;
    LogGobiInformation();
    ULONG firmware_id;
    ULONG technology;
    ULONG carrier_id;
    ULONG region;
    ULONG gps_capability;

    ULONG rc = sdk_->SetPower(gobi::kOnline);
    metrics_lib_->SendEnumToUMA(METRIC_BASE_NAME "SetPower",
                                QCErrorToMetricIndex(rc),
                                METRIC_INDEX_MAX);
    ENSURE_SDK_SUCCESS(SetPower, rc, kSdkError);
    rc = sdk_->GetFirmwareInfo(&firmware_id,
                               &technology,
                               &carrier_id,
                               &region,
                               &gps_capability);
    ENSURE_SDK_SUCCESS(GetFirmwareInfo, rc, kSdkError);

    const Carrier *carrier =
        handler_->server().FindCarrierByCarrierId(carrier_id);

    if (carrier != NULL) {
      LOG(INFO) << "Current carrier is " << carrier->name()
                << " (" << carrier->carrier_id() << ")";
      carrier_ = carrier;
    } else {
      LOG(WARNING) << "Unknown carrier, id=" << carrier_id;
    }
    Enabled = true;
    registration_start_time_ = GetTimeMs();
    // TODO(ers) disabling NMEA thread creation
    // until issues are addressed, e.g., thread
    // leakage and possible invalid pointer references.
    //    StartNMEAThread();
  } else if (!enable && Enabled()) {
    ULONG rc = sdk_->SetPower(gobi::kPersistentLowPower);
    if (rc != 0) {
      error.set(kSdkError, "SetPower");
      LOG(WARNING) << "SetPower failed : " << rc;
      return;
    }
    ApiDisconnect();
    Enabled = false;
  }
}

void GobiModem::Connect(const std::string& unused_number, DBus::Error& error) {
  if (!Enabled()) {
    LOG(WARNING) << "Connect on disabled modem";
    error.set(kConnectError, "Modem is disabled");
    return;
  }
  if (exiting_) {
    LOG(WARNING) << "Connect when exiting";
    error.set(kConnectError, "Cromo is exiting");
    return;
  }
  ULONG failure_reason;
  ULONG rc;

  connect_start_time_ = GetTimeMs();
  rc = sdk_->StartDataSession(NULL,  // technology
                              NULL,  // APN Name
                              NULL,  // Authentication
                              NULL,  // Username
                              NULL,  // Password
                              &session_id_,  // OUT: session ID
                              &failure_reason  // OUT: failure reason
                              );
  if (rc == 0) {
    LOG(INFO) << "Session ID " <<  session_id_;
    return;
  }

  LOG(WARNING) << "StartDataSession failed: " << rc;
  if (rc == gobi::kCallFailed) {
    LOG(WARNING) << "Failure Reason " <<  failure_reason;
  }
  error.set(kConnectError, "StartDataSession");
}


void GobiModem::Disconnect(DBus::Error& error) {
  LOG(INFO) << "Disconnect(" << session_id_ << ")";
  if (session_state_ != gobi::kConnected) {
    LOG(WARNING) << "Disconnect called when not connected";
    error.set(kDisconnectError, "Not connected");
    return;
  }

  disconnect_start_time_ = GetTimeMs();
  ULONG rc = sdk_->StopDataSession(session_id_);
  if (rc != 0)
    disconnect_start_time_ = 0;
  ENSURE_SDK_SUCCESS(StopDataSession, rc, kDisconnectError);
  error.set(kOperationInitiatedError, "");
  // session_state_ will be set in SessionStateCallback
}

std::string GobiModem::GetUSBAddress() {
  return sysfs_path_.substr(sysfs_path_.find_last_of('/') + 1);
}

DBus::Struct<uint32_t, uint32_t, uint32_t, uint32_t> GobiModem::GetIP4Config(
    DBus::Error& error) {
  DBus::Struct<uint32_t, uint32_t, uint32_t, uint32_t> result;

  LOG(INFO) << "GetIP4Config (unimplemented)";

  return result;
}

void GobiModem::FactoryReset(const std::string& code, DBus::Error& error) {
  LOG(INFO) << "ResetToFactoryDefaults";
  ULONG rc = sdk_->ResetToFactoryDefaults(const_cast<CHAR *>(code.c_str()));
  ENSURE_SDK_SUCCESS(ResetToFactoryDefaults, rc, kSdkError);
  LOG(INFO) << "FactoryReset succeeded. Device should disappear and reappear.";
}

DBus::Struct<std::string, std::string, std::string> GobiModem::GetInfo(
    DBus::Error& error) {
  // (manufacturer, modem, version).
  DBus::Struct<std::string, std::string, std::string> result;

  char buffer[kDefaultBufferSize];

  ULONG rc = sdk_->GetManufacturer(sizeof(buffer), buffer);
  ENSURE_SDK_SUCCESS_WITH_RESULT(GetManufacturer, rc, kSdkError, result);
  result._1 = buffer;
  rc = sdk_->GetModelID(sizeof(buffer), buffer);
  ENSURE_SDK_SUCCESS_WITH_RESULT(GetModelID, rc, kSdkError, result);
  result._2 = buffer;
  rc = sdk_->GetHardwareRevision(sizeof(buffer), buffer);
  ENSURE_SDK_SUCCESS_WITH_RESULT(GetHardwareRevision, rc, kSdkError, result);
  result._3 = buffer;

  LOG(INFO) << "Manufacturer: " << result._1;
  LOG(INFO) << "Modem: " << result._2;
  LOG(INFO) << "Version: " << result._3;
  return result;
}

// DBUS Methods: ModemSimple
void GobiModem::Connect(const DBusPropertyMap& properties, DBus::Error& error) {
  LOG(INFO) << "Simple.Connect";
  Connect("unused_number", error);
}

void GobiModem::GetSerialNumbers(SerialNumbers *out, DBus::Error &error) {
  char esn[kDefaultBufferSize];
  char imei[kDefaultBufferSize];
  char meid[kDefaultBufferSize];

  ULONG rc = sdk_->GetSerialNumbers(kDefaultBufferSize, esn,
                              kDefaultBufferSize, imei,
                              kDefaultBufferSize, meid);
  ENSURE_SDK_SUCCESS(GetSerialNumbers, rc, kSdkError);
  out->esn = esn;
  out->imei = imei;
  out->meid = meid;
}

int GobiModem::GetMmActivationState() {
  ULONG device_activation_state;
  ULONG rc;
  rc = sdk_->GetActivationState(&device_activation_state);
  if (rc != 0) {
    LOG(ERROR) << "GetActivationState: " << rc;
    return -1;
  }
  LOG(INFO) << "device activation state: " << device_activation_state;
  if (device_activation_state == 1) {
    return MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED;
  }

  // Is the modem de-activated, or is there an activation in flight?
  switch (carrier_->activation_method()) {
    case Carrier::kOmadm: {
        ULONG session_state;
        ULONG session_type;
        ULONG failure_reason;
        BYTE retry_count;
        WORD session_pause;
        WORD time_remaining;  // For session pause
        rc = sdk_->OMADMGetSessionInfo(
            &session_state, &session_type, &failure_reason, &retry_count,
            &session_pause, & time_remaining);
        if (rc != 0) {
          // kNoTrackingSessionHasBeenStarted -> modem has never tried
          // to run OMADM; this is not an error condition.
          if (rc != gobi::kNoTrackingSessionHasBeenStarted) {
            LOG(ERROR) << "Could not get omadm state: " << rc;
          }
          return MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED;
        }
        return (session_state <= gobi::kOmadmMaxFinal) ?
            MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED :
            MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING;
      }
      break;
    case Carrier::kOtasp:
      return (device_activation_state == gobi::kNotActivated) ?
          MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED :
          MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING;
      break;
    default:  // This is a UMTS carrier; we count it as activated
      return MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED;
  }
}

// if we get SDK errors while trying to retrieve information,
// we ignore them, and just don't set the corresponding properties
DBusPropertyMap GobiModem::GetStatus(DBus::Error& error_ignored) {
  DBusPropertyMap result;

  int32_t rssi;
  DBus::Error signal_strength_error;

  StrengthMap interface_to_dbm;
  GetSignalStrengthDbm(rssi, &interface_to_dbm, signal_strength_error);
  if (signal_strength_error.is_set()) {
    // Activate() looks for this key and does not even try if present
    result["no_signal"].writer().append_bool(true);
  } else {
    result["signal_strength_dbm"].writer().append_int32(rssi);
    for (StrengthMap::iterator i = interface_to_dbm.begin();
         i != interface_to_dbm.end();
         ++i) {
      char buf[30];
      snprintf(buf,
               sizeof(buf),
               "interface_%u_dbm",
               static_cast<unsigned int>(i->first));
      result[buf].writer().append_int32(i->second);
    }
  }

  SerialNumbers serials;
  DBus::Error serial_numbers_error;
  this->GetSerialNumbers(&serials, serial_numbers_error);
  if (!serial_numbers_error.is_set()) {
    if (serials.esn.length()) {
      result["esn"].writer().append_string(serials.esn.c_str());
    }
    if (serials.meid.length()) {
      result["meid"].writer().append_string(serials.meid.c_str());
    }
    if (serials.imei.length()) {
      result["imei"].writer().append_string(serials.imei.c_str());
    }
  }
  char imsi[kDefaultBufferSize];
  ULONG rc = sdk_->GetIMSI(kDefaultBufferSize, imsi);
  if (rc == 0 && strlen(imsi) != 0)
    result["imsi"].writer().append_string(imsi);

  ULONG firmware_id;
  ULONG technology_id;
  ULONG carrier_id;
  ULONG region;
  ULONG gps_capability;
  rc = sdk_->GetFirmwareInfo(&firmware_id,
                             &technology_id,
                             &carrier_id,
                             &region,
                             &gps_capability);
  if (rc == 0) {
    if (carrier_id != carrier_->carrier_id()) {
      LOG(ERROR) << "Inconsistent carrier: id = " << carrier_id;
      std::ostringstream s;
      s << "inconsistent: " << carrier_id << " vs. " << carrier_->carrier_id();
      result["carrier"].writer().append_string(s.str().c_str());
    } else {
      result["carrier"].writer().append_string(carrier_->name());
    }

    // TODO(ers) we'd like to return "operator_name", but the
    // SDK provides no apparent means of determining it.

    const char* technology;
    if (technology_id == 0)
      technology = "CDMA";
    else if (technology_id == 1)
      technology = "UMTS";
    else
      technology = "unknown";
    result["technology"].writer().append_string(technology);
  }


  ULONG session_state;
  ULONG mm_modem_state;
  rc = sdk_->GetSessionState(&session_state);
  if (rc == 0) {
    // TODO(ers) if not registered, should return enabled state
    switch (session_state) {
      case gobi::kConnected:
        mm_modem_state = MM_MODEM_STATE_CONNECTED;
        break;
      case gobi::kAuthenticating:
        mm_modem_state = MM_MODEM_STATE_CONNECTING;
        break;
      case gobi::kDisconnected:
      default:
        ULONG reg_state;
        ULONG l1, l2;      // don't care
        WORD w1, w2;
        BYTE b3[10];
        BYTE b2 = sizeof(b3)/sizeof(BYTE);
        rc = sdk_->GetServingNetwork(&reg_state, &l1, &b2, b3, &l2, &w1, &w2);
        if (rc == 0) {
          if (reg_state == gobi::kRegistered) {
            mm_modem_state = MM_MODEM_STATE_REGISTERED;
            break;
          } else if (reg_state == gobi::kSearching) {
            mm_modem_state = MM_MODEM_STATE_SEARCHING;
            break;
          }
        }
        mm_modem_state = MM_MODEM_STATE_UNKNOWN;
    }
    result["state"].writer().append_uint32(mm_modem_state);
  }

  char mdn[kDefaultBufferSize], min[kDefaultBufferSize];
  rc = sdk_->GetVoiceNumber(kDefaultBufferSize, mdn,
                            kDefaultBufferSize, min);
  if (rc == 0) {
    result["mdn"].writer().append_string(mdn);
    result["min"].writer().append_string(min);
  } else if (rc != gobi::kNotProvisioned) {
    LOG(WARNING) << "GetVoiceNumber failed: " << rc;
  }

  char firmware_revision[kDefaultBufferSize];
  rc = sdk_->GetFirmwareRevision(sizeof(firmware_revision), firmware_revision);
  if (rc == 0 && strlen(firmware_revision) != 0) {
    result["firmware_revision"].writer().append_string(firmware_revision);
  }

  WORD prl_version;
  rc = sdk_->GetPRLVersion(&prl_version);
  if (rc == 0) {
    result["prl_version"].writer().append_uint16(prl_version);
  }

  int activation_state = GetMmActivationState();
  if (activation_state >= 0) {
    result["activation_state"].writer().append_uint32(activation_state);
  }
  carrier_-> ModifyModemStatusReturn(&result);

  return result;
}


void GobiModem::GetGobiRegistrationState(ULONG* cdma_1x_state,
                                         ULONG* cdma_evdo_state,
                                         ULONG* roaming_state,
                                         DBus::Error& error) {
  ULONG reg_state;
  ULONG l1;
  WORD w1, w2;
  BYTE radio_interfaces[10];
  BYTE num_radio_interfaces = sizeof(radio_interfaces)/sizeof(BYTE);

  ULONG rc = sdk_->GetServingNetwork(&reg_state, &l1, &num_radio_interfaces,
                                     radio_interfaces, roaming_state,
                                     &w1, &w2);
  ENSURE_SDK_SUCCESS(GetServingNetwork, rc, kSdkError);

  for (int i = 0; i < num_radio_interfaces; i++) {
    DLOG(INFO) << "Registration state " << reg_state
               << " for RFI " << (int)radio_interfaces[i];
    if (radio_interfaces[i] == gobi::kRfiCdma1xRtt)
      *cdma_1x_state = reg_state;
    else if (radio_interfaces[i] == gobi::kRfiCdmaEvdo)
      *cdma_evdo_state = reg_state;
  }
}

// This is only in debug builds; if you add actual code here, see
// RegisterCallbacks().
static void ByteTotalsCallback(ULONGLONG tx, ULONGLONG rx) {
  LOG(INFO) << "ByteTotalsCallback: tx " << tx << " rx " << rx;
}

// This is only in debug builds; if you add actual code here, see
// RegisterCallbacks().
static void DataCapabilitiesCallback(BYTE size, BYTE* caps) {
  ULONG *ucaps = reinterpret_cast<ULONG*>(caps);
  LOG(INFO) << "DataCapabilitiesHandler:";
  for (int i = 0; i < size; i++) {
    LOG(INFO) << "  Cap: " << static_cast<int>(ucaps[i]);
  }
}

// This is only in debug builds; if you add actual code here, see
// RegisterCallbacks().
gboolean GobiModem::DormancyStatusCallback(gpointer data) {
  DormancyStatusArgs *args = reinterpret_cast<DormancyStatusArgs*>(data);
  GobiModem *modem = handler_->LookupByPath(*args->path);
  LOG(INFO) << "DormancyStatusCallback: " << args->status;
  if (modem->event_enabled[GOBI_EVENT_DORMANCY]) {
    modem->DormancyStatus(args->status == gobi::kDormant);
  }
  return FALSE;
}

static void MobileIPStatusCallback(ULONG status) {
  LOG(INFO) << "MobileIPStatusCallback: " << status;
}

static void PowerCallback(ULONG mode) {
  LOG(INFO) << "PowerCallback: " << mode;
}

static void LURejectCallback(ULONG domain, ULONG cause) {
  LOG(INFO) << "LURejectCallback: domain " << domain << " cause " << cause;
}

static void NewSMSCallback(ULONG type, ULONG index) {
  LOG(INFO) << "NewSMSCallback: type " << type << " index " << index;
}

static void NMEACallback(LPCSTR nmea) {
  LOG(INFO) << "NMEACallback: " << nmea;
}

static void PDSStateCallback(ULONG enabled, ULONG tracking) {
  LOG(INFO) << "PDSStateCallback: enabled " << enabled
            << " tracking " << tracking;
}

void GobiModem::RegisterCallbacks() {
  sdk_->SetMobileIPStatusCallback(MobileIPStatusCallback);
  sdk_->SetPowerCallback(PowerCallback);
  sdk_->SetRFInfoCallback(RFInfoCallbackTrampoline);
  sdk_->SetLURejectCallback(LURejectCallback);
  sdk_->SetNewSMSCallback(NewSMSCallback);
  sdk_->SetNMEACallback(NMEACallback);
  sdk_->SetPDSStateCallback(PDSStateCallback);

  sdk_->SetNMEAPlusCallback(NmeaPlusCallbackTrampoline);
  sdk_->SetSessionStateCallback(SessionStateCallbackTrampoline);
  sdk_->SetDataBearerCallback(DataBearerCallbackTrampoline);
  sdk_->SetRoamingIndicatorCallback(RoamingIndicatorCallbackTrampoline);

  if (DEBUG) {
    // These are only used for logging. If you make one of these a non-debug
    // callback, see EventKeyToIndex() below, which will need to be updated.
    sdk_->SetByteTotalsCallback(ByteTotalsCallback, 60);
    sdk_->SetDataCapabilitiesCallback(DataCapabilitiesCallback);
    sdk_->SetDormancyStatusCallback(DormancyStatusCallbackTrampoline);
  }

  static int num_thresholds = kSignalStrengthNumLevels - 1;
  int interval =
      (kMaxSignalStrengthDbm - kMinSignalStrengthDbm) /
      (kSignalStrengthNumLevels - 1);
  INT8 thresholds[num_thresholds];
  for (int i = 0; i < num_thresholds; i++) {
    thresholds[i] = kMinSignalStrengthDbm + interval * i;
  }
  sdk_->SetSignalStrengthCallback(SignalStrengthCallbackTrampoline,
                                  num_thresholds,
                                  thresholds);
}

bool GobiModem::CanMakeMethodCalls(void) {
  // The Gobi has been observed (at least once - see chromium-os:7172) to get
  // into a state where we can set up a QMI channel to it (so QCWWANConnect()
  // succeeds) but not actually invoke any functions. We'll force the issue here
  // by calling GetSerialNumbers so we can detect this failure mode early.
  ULONG rc;
  char esn[kDefaultBufferSize];
  char imei[kDefaultBufferSize];
  char meid[kDefaultBufferSize];
  rc = sdk_->GetSerialNumbers(sizeof(esn), esn, sizeof(imei), imei,
                              sizeof(meid), meid);
  if (rc != 0) {
    LOG(ERROR) << "GetSerialNumbers() failed: " << rc;
  }

  return rc == 0;
}

void GobiModem::ApiConnect(DBus::Error& error) {

  // It is safe to test for NULL outside of a lock because ApiConnect
  // is only called by the main thread, and only the main thread can
  // modify connected_modem_.
  if (connected_modem_ != NULL) {
    LOG(INFO) << "ApiAlready connected: connected_modem_=0x" << connected_modem_
              << "this=0x" << this;
    error.set(kOperationNotAllowedError,
              "Only one modem can be connected via Api");
    return;
  }
  ULONG rc = sdk_->QCWWANConnect(device_.deviceNode, device_.deviceKey);
  if (rc != 0 || !CanMakeMethodCalls()) {
    LOG(ERROR) << "QCWWANConnect() failed: " << rc;
    // Reset and try again.
    ResetDevice(rc);
    rc = sdk_->QCWWANConnect(device_.deviceNode, device_.deviceKey);
    if (rc != 0 || !CanMakeMethodCalls()) {
      LOG(ERROR) << "QCWWANConnect() failed again: " << rc;
      // Totally stuck. Oh well.
      abort();
    }
  }

  pthread_mutex_lock(&modem_mutex_.mutex_);
  connected_modem_ = this;
  pthread_mutex_unlock(&modem_mutex_.mutex_);
  RegisterCallbacks();
}


ULONG GobiModem::ApiDisconnect(void) {

  ULONG rc = 0;

  pthread_mutex_lock(&modem_mutex_.mutex_);
  if (connected_modem_ == this) {
    LOG(INFO) << "Disconnecting from QCWWAN.  this_=0x" << this;
    connected_modem_ = NULL;
    pthread_mutex_unlock(&modem_mutex_.mutex_);
    rc = sdk_->QCWWANDisconnect();
  } else {
    LOG(INFO) << "Not connected.  this_=0x" << this;
    pthread_mutex_unlock(&modem_mutex_.mutex_);
  }
  return rc;
}

void GobiModem::LogGobiInformation() {
  ULONG rc;

  char buffer[kDefaultBufferSize];
  rc = sdk_->GetManufacturer(sizeof(buffer), buffer);
  if (rc == 0) {
    LOG(INFO) << "Manufacturer: " << buffer;
  }

  ULONG firmware_id;
  ULONG technology;
  ULONG carrier;
  ULONG region;
  ULONG gps_capability;
  rc = sdk_->GetFirmwareInfo(&firmware_id,
                             &technology,
                             &carrier,
                             &region,
                             &gps_capability);
  if (rc == 0) {
    LOG(INFO) << "Firmware info: "
              << "firmware_id: " << firmware_id
              << " technology: " << technology
              << " carrier: " << carrier
              << " region: " << region
              << " gps_capability: " << gps_capability;
  } else {
    LOG(WARNING) << "Cannot get firmware info: " << rc;
  }

  char amss[kDefaultBufferSize], boot[kDefaultBufferSize];
  char pri[kDefaultBufferSize];

  rc = sdk_->GetFirmwareRevisions(kDefaultBufferSize, amss,
                                  kDefaultBufferSize, boot,
                                  kDefaultBufferSize, pri);
  if (rc == 0) {
    LOG(INFO) << "Firmware Revisions: AMSS: " << amss
              << " boot: " << boot
              << " pri: " << pri;
  } else {
    LOG(WARNING) << "Cannot get firmware revision info: " << rc;
  }

  rc = sdk_->GetImageStore(sizeof(buffer), buffer);
  if (rc == 0) {
    LOG(INFO) << "ImageStore: " << buffer;
  } else {
    LOG(WARNING) << "Cannot get image store info: " << rc;
  }

  SerialNumbers serials;
  DBus::Error error;
  GetSerialNumbers(&serials, error);
  if (!error.is_set()) {
    DLOG(INFO) << "ESN " << serials.esn
               << " IMEI " << serials.imei
               << " MEID " << serials.meid;
  } else {
    LOG(WARNING) << "Cannot get serial numbers: " << error;
  }

  char number[kDefaultBufferSize], min_array[kDefaultBufferSize];
  rc = sdk_->GetVoiceNumber(kDefaultBufferSize, number,
                            kDefaultBufferSize, min_array);
  if (rc == 0) {
    char masked_min[kDefaultBufferSize + 1];
    strncpy(masked_min, min_array, sizeof(masked_min));
    if (strlen(masked_min) >= 4)
      strcpy(masked_min + strlen(masked_min) - 4, "XXXX");
    char masked_voice[kDefaultBufferSize + 1];
    strncpy(masked_voice, number, sizeof(masked_voice));
    if (strlen(masked_voice) >= 4)
      strcpy(masked_voice + strlen(masked_voice) - 4, "XXXX");
    LOG(INFO) << "Voice: " << masked_voice
              << " MIN: " << masked_min;
  } else if (rc != gobi::kNotProvisioned) {
    LOG(WARNING) << "GetVoiceNumber failed: " << rc;
  }

  BYTE index;
  rc = sdk_->GetActiveMobileIPProfile(&index);
  if (rc != 0 && rc != gobi::kNotSupportedByDevice) {
    LOG(WARNING) << "GetAMIPP: " << rc;
  } else {
    LOG(INFO) << "Mobile IP profile: " << (int)index;
  }
}

void GobiModem::SoftReset(DBus::Error& error) {
  ResetModem(error);
}

void GobiModem::PowerCycle(DBus::Error &error) {
  LOG(INFO) << "Initiating modem powercycle";
  ULONG rc = sdk_->SetPower(gobi::kPowerOff);
  ENSURE_SDK_SUCCESS(SetPower, rc, kSdkError);
}


void GobiModem::ResetModem(DBus::Error& error) {
  Enabled = false;
  LOG(INFO) << "Offline";

  // Resetting the modem will cause it to disappear and reappear.
  ULONG rc = sdk_->SetPower(gobi::kOffline);
  ENSURE_SDK_SUCCESS(SetPower, rc, kSdkError);

  LOG(INFO) << "Reset";
  rc = sdk_->SetPower(gobi::kReset);
  ENSURE_SDK_SUCCESS(SetPower, rc, kSdkError);

  rc = ApiDisconnect();
  ENSURE_SDK_SUCCESS(QCWWanDisconnect, rc, kSdkError);
}

void GobiModem::SetCarrier(const std::string& carrier_name,
                           DBus::Error& error) {
  const Carrier *carrier = handler_->server().FindCarrierByName(carrier_name);
  if (carrier == NULL) {
    // TODO(rochberg):  Do we need to sanitize this string?
    LOG(WARNING) << "Could not parse carrier: " << carrier_name;
    error.set(kFirmwareLoadError, "Unknown carrier name");
    return;
  }

  LOG(INFO) << "Carrier image selection starting: " << carrier_name;
  ULONG firmware_id;
  ULONG technology;
  ULONG modem_carrier_id;
  ULONG region;
  ULONG gps_capability;
  ULONG rc = sdk_->GetFirmwareInfo(&firmware_id,
                                   &technology,
                                   &modem_carrier_id,
                                   &region,
                                   &gps_capability);
  ENSURE_SDK_SUCCESS(GetFirmwareInfo, rc, kFirmwareLoadError);

  if (modem_carrier_id != carrier->carrier_id()) {

    // UpgradeFirmware doesn't pay attention to anything before the
    // last /, so we don't supply it
    std::string image_path = std::string("/") + carrier->firmware_directory();

    LOG(INFO) << "Current Gobi carrier: " << modem_carrier_id
              << ".  Carrier image selection "
              << image_path;
    rc = sdk_->UpgradeFirmware(const_cast<CHAR *>(image_path.c_str()));
    if (rc != 0) {
      LOG(WARNING) << "Carrier image selection from: "
                   << image_path << " failed: " << rc;
      error.set(kFirmwareLoadError, "UpgradeFirmware");
    } else {
      ApiDisconnect();
    }
  }
}

uint32_t GobiModem::CommonGetSignalQuality(DBus::Error& error) {
  if (!Enabled()) {
    LOG(WARNING) << "GetSignalQuality on disabled modem";
    error.set(kModeError, "Modem is disabled");
  } else {
    int32_t signal_strength_dbm;
    GetSignalStrengthDbm(signal_strength_dbm, NULL, error);
    if (!error.is_set()) {
      uint32_t result = MapDbmToPercent(signal_strength_dbm);
      LOG(INFO) << "GetSignalQuality => " << result << "%";
      return result;
    }
  }
  // for the error cases, return an impossible value
  return 999;
}

void GobiModem::GetSignalStrengthDbm(int& output,
                                     StrengthMap *interface_to_dbm,
                                     DBus::Error& error) {
  ULONG kSignals = 10;
  ULONG signals = kSignals;
  INT8 strengths[kSignals];
  ULONG interfaces[kSignals];

  ULONG rc = sdk_->GetSignalStrengths(&signals, strengths, interfaces);
  ENSURE_SDK_SUCCESS(GetSignalStrengths, rc, kSdkError);

  signals = std::min(kSignals, signals);

  if (interface_to_dbm) {
    for (ULONG i = 0; i < signals; ++i) {
      (*interface_to_dbm)[interfaces[i]] = strengths[i];
    }
  }

  INT8 max_strength = -127;
  for (ULONG i = 0; i < signals; ++i) {
    DLOG(INFO) << "Interface " << i << ": " << static_cast<int>(strengths[i])
               << " dBM technology: " << interfaces[i];
    // TODO(ers) mark radio interface technology as registered?
    if (strengths[i] > max_strength) {
      max_strength = strengths[i];
    }
  }

  // If we're in the connected state, pick the signal strength for the radio
  // interface that's being used. Otherwise, pick the strongest signal.
  ULONG session_state;
  rc = sdk_->GetSessionState(&session_state);
  ENSURE_SDK_SUCCESS(GetSessionState, rc, kSdkError);

  if (session_state == gobi::kConnected) {
    ULONG db_technology;
    rc =  sdk_->GetDataBearerTechnology(&db_technology);
    if (rc != 0) {
      LOG(WARNING) << "GetDataBearerTechnology failed: " << rc;
      error.set(kSdkError, "GetDataBearerTechnology");
      return;
    }
    DLOG(INFO) << "data bearer technology " << db_technology;
    ULONG rfi_technology = MapDataBearerToRfi(db_technology);
    for (ULONG i = 0; i < signals; ++i) {
      if (interfaces[i] == rfi_technology) {
        output = strengths[i];
        return;
      }
    }
  }
  output = max_strength;
}

// Set properties for which a connection to the SDK is required
// to obtain the needed information. Since this is called before
// the modem is enabled, we connect to the SDK, get the properties
// we need, and then disconnect from the SDK.
// pre-condition: Enabled == false
void GobiModem::SetModemProperties() {
  DBus::Error connect_error;

  ApiConnect(connect_error);
  if (connect_error.is_set()) {
    // Use a default identifier assuming a single GOBI is connected
    EquipmentIdentifier = "GOBI";
    Type = MM_MODEM_TYPE_CDMA;
    return;
  }

  SerialNumbers serials;
  DBus::Error getserial_error;
  GetSerialNumbers(&serials, getserial_error);
  DBus::Error getdev_error;
  ULONG u1, u2, u3, u4;
  BYTE radioInterfaces[10];
  ULONG numRadioInterfaces = sizeof(radioInterfaces)/sizeof(BYTE);
  ULONG rc = sdk_->GetDeviceCapabilities(&u1, &u2, &u3, &u4,
                                         &numRadioInterfaces,
                                         radioInterfaces);
  if (rc == 0) {
    if (numRadioInterfaces != 0) {
      if (radioInterfaces[0] == gobi::kRfiGsm ||
          radioInterfaces[0] == gobi::kRfiUmts) {
        Type = MM_MODEM_TYPE_GSM;
      } else {
        Type = MM_MODEM_TYPE_CDMA;
      }
    }
  }
  ApiDisconnect();
  if (!getserial_error.is_set()) {
    // TODO(jglasgow): if GSM return serials.imei
    EquipmentIdentifier = serials.meid;
    SetDeviceSpecificIdentifier(serials);
  } else {
    // Use a default identifier assuming a single GOBI is connected
    EquipmentIdentifier = "GOBI";
  }
}

void GobiModem::SetDeviceSpecificIdentifier(const SerialNumbers& ignored) {
  // Overridden in CDMA, GSM
}

void *GobiModem::NMEAThread(void) {
  int fd;
  ULONG gps_mask, cell_mask;

  unlink(kFifoName);
  mkfifo(kFifoName, 0700);

  // This will wait for a reader to open before continuing
  fd = open(kFifoName, O_WRONLY);

  LOG(INFO) << "NMEA fifo running, GPS enabled";

  // Enable GPS tracking
  sdk_->SetServiceAutomaticTracking(1);

  // Reset all GPS/Cell positioning fields at startup
  gps_mask = 0x1fff;
  cell_mask = 0x3ff;
  sdk_->ResetPDSData(&gps_mask, &cell_mask);

  nmea_fd_ = fd;

  return NULL;
}

void GobiModem::StartNMEAThread() {
  // Create thread to wait for fifo reader
  pthread_create(&nmea_thread, NULL, NMEAThreadTrampoline, NULL);
}

// Event callbacks run in the context of the main thread

gboolean GobiModem::NmeaPlusCallback(gpointer data) {
  NmeaPlusArgs *args = static_cast<NmeaPlusArgs*>(data);
  LOG(INFO) << "NMEA Plus State Callback: "
            << args->nmea << " " << args->mode;
  GobiModem* modem = handler_->LookupByPath(*args->path);
  if (modem != NULL && modem->nmea_fd_ != -1) {
    int ret = write(modem->nmea_fd_, args->nmea.c_str(), args->nmea.length());
    if (ret < 0) {
      // Failed write means fifo reader went away
      LOG(INFO) << "NMEA fifo stopped, GPS disabled";
      unlink(kFifoName);
      close(modem->nmea_fd_);
      modem->nmea_fd_ = -1;

      // Disable GPS tracking until we have a listener again
      modem->sdk_->SetServiceAutomaticTracking(0);

      // Re-start the fifo listener thread
      modem->StartNMEAThread();
    }
  }
  delete args;
  return FALSE;
}


void GobiModem::SinkSdkError(const std::string& modem_path,
                             const std::string& sdk_function,
                             ULONG error) {
  LOG(ERROR) << sdk_function << ": unrecoverable error " << error
             << " on modem " << modem_path;
  PostCallbackRequest(GobiModem::SdkErrorHandler,
                      new SdkErrorArgs(error));
}

// Callbacks:  Run in the context of the main thread
gboolean GobiModem::SdkErrorHandler(gpointer data) {
  SdkErrorArgs *args = static_cast<SdkErrorArgs *>(data);
  GobiModem* modem = handler_->LookupByPath(*args->path);
  if (modem != NULL) {
    modem->ResetDevice(args->error);
  } else {
    LOG(INFO) << "Reset received for obsolete path "
              << args->path;
  }
  return FALSE;
}

gboolean GobiModem::SignalStrengthCallback(gpointer data) {
  SignalStrengthArgs* args = static_cast<SignalStrengthArgs*>(data);
  GobiModem* modem = handler_->LookupByPath(*args->path);
  if (modem != NULL)
    modem->SignalStrengthHandler(args->signal_strength, args->radio_interface);
  delete args;
  return FALSE;
}

gboolean GobiModem::SessionStateCallback(gpointer data) {
  SessionStateArgs* args = static_cast<SessionStateArgs*>(data);
  GobiModem* modem = handler_->LookupByPath(*args->path);
  if (modem != NULL)
    modem->SessionStateHandler(args->state, args->session_end_reason);
  delete args;
  return FALSE;
}

gboolean GobiModem::RegistrationStateCallback(gpointer data) {
  CallbackArgs* args = static_cast<CallbackArgs*>(data);
  GobiModem* modem = handler_->LookupByPath(*args->path);
  delete args;
  if (modem != NULL)
    modem->RegistrationStateHandler();
  return FALSE;
}

void GobiModem::SignalStrengthHandler(INT8 signal_strength,
                                      ULONG radio_interface) {
  // Overridden by per-technology modem handlers
  // TODO(ers) mark radio interface technology as registered?
}

void GobiModem::SessionStateHandler(ULONG state, ULONG session_end_reason) {
  LOG(INFO) << "SessionStateHandler state: " << state
            << " reason: "
            << (state == gobi::kConnected?0:session_end_reason);
  if (state == gobi::kConnected) {
    ULONG data_bearer_technology;
    sdk_->GetDataBearerTechnology(&data_bearer_technology);
    // TODO(ers) send a signal or change a property to notify
    // listeners about the change in data bearer technology
  }

  session_state_ = state;
  if (state == gobi::kDisconnected) {
    if (disconnect_start_time_) {
      ULONG duration = GetTimeMs() - disconnect_start_time_;
      metrics_lib_->SendToUMA(METRIC_BASE_NAME "Disconnect", duration, 0, 150000,
                              20);
      disconnect_start_time_ = 0;
    }
    session_id_ = 0;
    unsigned int reason = QMIReasonToMMReason(session_end_reason);
    if (reason == MM_MODEM_CONNECTION_STATE_CHANGE_REASON_REQUESTED &&
        suspending_)
      reason = MM_MODEM_CONNECTION_STATE_CHANGE_REASON_SUSPEND;
    ConnectionStateChanged(false, reason);
  } else if (state == gobi::kConnected) {
    ULONG duration = GetTimeMs() - connect_start_time_;
    metrics_lib_->SendToUMA(METRIC_BASE_NAME "Connect", duration, 0, 150000, 20);
    suspending_ = false;
    ConnectionStateChanged(true, 0);
  }
}

void GobiModem::RegistrationStateHandler() {
  // TODO(cros-connectivity):  Factor out commonality from CDMA, GSM
}

// Set DBus properties that pertain to the modem hardware device.
// The properties set here are Device, MasterDevice, and Driver.
void GobiModem::SetDeviceProperties()
{
  struct udev *udev = udev_new();

  if (udev == NULL) {
    LOG(WARNING) << "udev == NULL";
    return;
  }

  struct udev_list_entry *entry;
  struct udev_enumerate *udev_enumerate = enumerate_net_devices(udev);
  if (udev_enumerate == NULL) {
    LOG(WARNING) << "udev_enumerate == NULL";
    udev_unref(udev);
    return;
  }

  for (entry = udev_enumerate_get_list_entry(udev_enumerate);
      entry != NULL;
      entry = udev_list_entry_get_next(entry)) {

    std::string syspath(udev_list_entry_get_name(entry));

    struct udev_device *udev_device =
        udev_device_new_from_syspath(udev, syspath.c_str());
    if (udev_device == NULL)
      continue;

    std::string driver;
    struct udev_device *parent = udev_device_get_parent(udev_device);
    if (parent != NULL)
      driver = udev_device_get_driver(parent);

    if (driver.compare(kNetworkDriver) == 0) {
      // Extract last portion of syspath...
      size_t found = syspath.find_last_of('/');
      if (found != std::string::npos) {
        Device = syspath.substr(found + 1);
        struct udev_device *grandparent;
        if (parent != NULL) {
          grandparent = udev_device_get_parent(parent);
          if (grandparent != NULL) {
            sysfs_path_ = udev_device_get_syspath(grandparent);
            LOG(INFO) << "sysfs path: " << sysfs_path_;
            MasterDevice = sysfs_path_;
          }
        }
        Driver = driver;
        udev_device_unref(udev_device);

        // TODO(jglasgow): Support multiple devices.
        // This functions returns the first network device whose
        // driver is a qualcomm network device driver.  This will not
        // work properly if a machine has multiple devices that use the
        // Qualcomm network device driver.
        break;
      }
    }
    udev_device_unref(udev_device);
  }
  udev_enumerate_unref(udev_enumerate);
  udev_unref(udev);
}

bool GobiModem::StartExit() {
  exiting_ = true;
  if (session_id_)
    sdk_->StopDataSession(session_id_);

  LOG(INFO) << "StartExit: session id " << session_id_;
  return true;
}

unsigned int GobiModem::QMIReasonToMMReason(unsigned int qmireason) {
  switch (qmireason) {
    case gobi::kClientEndedCall:
      return MM_MODEM_CONNECTION_STATE_CHANGE_REASON_REQUESTED;
    default:
      return MM_MODEM_CONNECTION_STATE_CHANGE_REASON_UNKNOWN;
  }
}

bool GobiModem::StartSuspend() {
  LOG(INFO) << "StartSuspend";
  suspending_ = true;
  if (session_id_)
    sdk_->StopDataSession(session_id_);
  return true;
}

bool StartSuspendTrampoline(void *arg) {
  GobiModem *modem = static_cast<GobiModem*>(arg);
  return modem->StartSuspend();
}

void GobiModem::RegisterStartSuspend(const std::string &name) {
  // TODO(ellyjones): Get maxdelay_ms from the carrier plugin
  static const int maxdelay_ms = 10000;
  handler_->server().RegisterStartSuspend(name, StartSuspendTrampoline, this,
                                          maxdelay_ms);
}

// Tokenizes a string of the form (<[+-]ident>)* into a list of strings of the
// form [+-]ident.
static std::vector<std::string> TokenizeRequest(const std::string& req) {
  std::vector<std::string> tokens;
  std::string token;
  size_t start, end;

  start = req.find_first_of("+-");
  while (start != req.npos) {
    end = req.find_first_of("+-", start + 1);
    if (end == req.npos)
      token = req.substr(start);
    else
      token = req.substr(start, end - start);
    tokens.push_back(token);
    start = end;
  }

  return tokens;
}

int GobiModem::EventKeyToIndex(const char *key) {
  if (DEBUG && !strcmp(key, "dormancy"))
    return GOBI_EVENT_DORMANCY;
  return -1;
}

void GobiModem::RequestEvent(const std::string request, DBus::Error& error) {
  const char *req = request.c_str();
  const char *key = req + 1;

  if (!strcmp(key, "*")) {
    for (int i = 0; i < GOBI_EVENT_MAX; i++) {
      event_enabled[i] = (req[0] == '+');
    }
    return;
  }

  int idx = EventKeyToIndex(key);
  if (idx < 0) {
    error.set(kInvalidArgumentError, "Unknown event requested.");
    return;
  }

  event_enabled[idx] = (req[0] == '+');
}

void GobiModem::RequestEvents(const std::string& events, DBus::Error& error) {
  std::vector<std::string> requests = TokenizeRequest(events);
  std::vector<std::string>::iterator it;
  for (it = requests.begin(); it != requests.end(); it++) {
    RequestEvent(*it, error);
  }
}

void GobiModem::RecordResetReason(ULONG reason) {
  static const ULONG distinguished_errors[] = {
    gobi::kErrorSendingQmiRequest,    // 0
    gobi::kErrorReceivingQmiRequest,  // 1
    gobi::kErrorNeedsReset,           // 2
  };
  // Leave some room for other errors
  const int kMaxError = 10;
  int bucket = kMaxError;
  for (size_t i = 0; i < arraysize(distinguished_errors); ++i) {
    if (reason == distinguished_errors[i]) {
      bucket = i;
    }
  }
  metrics_lib_->SendEnumToUMA(
      METRIC_BASE_NAME "ResetReason", bucket, kMaxError + 1);
}

void GobiModem::ResetDevice(ULONG reason) {
  if (device_resetting_) {
    LOG(ERROR) << "Already resetting " << static_cast<std::string>(path());
    return;
  }
  if (suspending_) {
    LOG(ERROR) << "Already suspending " << static_cast<std::string>(path());
    return;
  }
  device_resetting_ = true;
  RecordResetReason(reason);
  const char *addr = GetUSBAddress().c_str();
  LOG(ERROR) << "Resetting modem device: "
             << static_cast<std::string>(path())
             << " USB device: " << addr;
  ApiDisconnect();

  int pid = fork();
  if (pid < 0) {
    PLOG(ERROR) << "fork()";
    return;
  }
  if (pid == 0) {
    int rc = daemon(0, 0);
    if (rc == 0) {
      const char kReset[] = "/usr/bin/gobi-modem-reset";
      execl(kReset,
            kReset,
            addr,
            static_cast<char *>(NULL));
      PLOG(ERROR) << "execl()";
    } else {
      PLOG(ERROR) << "daemon()";
    }
    _exit(rc);
  } else {
    int status;
    waitpid(pid, &status, 0);
    if (status != 0) {
      LOG(ERROR) << "Child process failed: " << status;
    }
  }
}
