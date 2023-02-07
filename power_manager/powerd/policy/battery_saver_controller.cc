// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/battery_saver_controller.h"

#include <base/logging.h>
#include <dbus/power_manager/dbus-constants.h>
#include <power_manager/proto_bindings/battery_saver.pb.h>

namespace power_manager::policy {

BatterySaverController::BatterySaverController() : weak_ptr_factory_(this) {}

void BatterySaverController::Init(system::DBusWrapperInterface& dbus_wrapper) {
  dbus_wrapper_ = &dbus_wrapper;

  dbus_wrapper.ExportMethod(
      kGetBatterySaverModeState,
      base::BindRepeating(&BatterySaverController::OnGetStateCall,
                          weak_ptr_factory_.GetWeakPtr()));
  dbus_wrapper.ExportMethod(
      kSetBatterySaverModeState,
      base::BindRepeating(&BatterySaverController::OnSetStateCall,
                          weak_ptr_factory_.GetWeakPtr()));
}

void BatterySaverController::OnGetStateCall(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  // Get the current state.
  BatterySaverModeState state = GetState();

  // Serialize our reply to the sender.
  std::unique_ptr<dbus::Response> response(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageWriter writer(response.get());
  writer.AppendProtoAsArrayOfBytes(state);
  std::move(response_sender).Run(std::move(response));
}

void BatterySaverController::OnSetStateCall(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  // Deserialize the message.
  dbus::MessageReader reader(method_call);
  SetBatterySaverModeStateRequest request;
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Invalid args to D-Bus call " << kSetBatterySaverModeState;
    std::move(response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(
            method_call, DBUS_ERROR_INVALID_ARGS,
            "Expected SetBatterySaverModeStateRequest protobuf"));
    return;
  }

  // Update the state.
  SetState(request);

  // Reply to the caller.
  std::move(response_sender).Run(dbus::Response::FromMethodCall(method_call));
}

BatterySaverModeState BatterySaverController::GetState() const {
  BatterySaverModeState result;
  result.set_enabled(enabled_);
  return result;
}

void BatterySaverController::SetState(
    const SetBatterySaverModeStateRequest& request) {
  // If we are already in the desired state, ignore the request.
  if (request.enabled() == enabled_) {
    return;
  }

  // Otherwise, transition to the desired state.
  LOG(INFO) << "Battery saver mode "
            << (request.enabled() ? "enabled" : "disabled");
  enabled_ = request.enabled();
  SendStateChangeSignal(request.enabled()
                            ? BatterySaverModeState::CAUSE_USER_ENABLED
                            : BatterySaverModeState::CAUSE_USER_DISABLED);
}

void BatterySaverController::SendStateChangeSignal(
    BatterySaverModeState::Cause cause) {
  BatterySaverModeState state = GetState();
  state.set_cause(cause);
  dbus_wrapper_->EmitSignalWithProtocolBuffer(kBatterySaverModeStateChanged,
                                              state);
}

}  // namespace power_manager::policy