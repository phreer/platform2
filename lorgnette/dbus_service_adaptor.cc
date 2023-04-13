// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "lorgnette/dbus_service_adaptor.h"

#include <signal.h>
#include <utility>

#include <chromeos/dbus/service_constants.h>

namespace lorgnette {

namespace {
class ScopeLogger {
 public:
  explicit ScopeLogger(std::string name) : name_(std::move(name)) {
    LOG(INFO) << name_ << ": Enter";
  }

  ~ScopeLogger() { LOG(INFO) << name_ << ": Exit"; }

 private:
  std::string name_;
};

}  // namespace

DBusServiceAdaptor::DBusServiceAdaptor(std::unique_ptr<Manager> manager)
    : org::chromium::lorgnette::ManagerAdaptor(this),
      manager_(std::move(manager)) {
  // Set signal sender to be the real D-Bus call by default.
  manager_->SetScanStatusChangedSignalSender(base::BindRepeating(
      [](base::WeakPtr<DBusServiceAdaptor> adaptor,
         const ScanStatusChangedSignal& signal) {
        if (adaptor) {
          adaptor->SendScanStatusChangedSignal(signal);
        }
      },
      weak_factory_.GetWeakPtr()));
}

DBusServiceAdaptor::~DBusServiceAdaptor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void DBusServiceAdaptor::RegisterAsync(
    brillo::dbus_utils::ExportedObjectManager* object_manager,
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(!dbus_object_) << "Already registered";
  scoped_refptr<dbus::Bus> bus =
      object_manager ? object_manager->GetBus() : nullptr;
  dbus_object_.reset(new brillo::dbus_utils::DBusObject(
      object_manager, bus, dbus::ObjectPath(kManagerServicePath)));
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "DBusServiceAdaptor.RegisterAsync() failed.", true));
  manager_->ConnectDBusObjects(bus);
}

bool DBusServiceAdaptor::ListScanners(brillo::ErrorPtr* error,
                                      ListScannersResponse* scanner_list_out) {
  ScopeLogger scope("DBusServiceAdaptor::ListScanners");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return manager_->ListScanners(error, scanner_list_out);
}

bool DBusServiceAdaptor::GetScannerCapabilities(
    brillo::ErrorPtr* error,
    const std::string& device_name,
    ScannerCapabilities* capabilities) {
  ScopeLogger scope("DBusServiceAdaptor::GetScannerCapabilities");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return manager_->GetScannerCapabilities(error, device_name, capabilities);
}

StartScanResponse DBusServiceAdaptor::StartScan(
    const StartScanRequest& request) {
  ScopeLogger scope("DBusServiceAdaptor::StartScan");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return manager_->StartScan(request);
}

void DBusServiceAdaptor::GetNextImage(
    std::unique_ptr<DBusMethodResponse<GetNextImageResponse>> response,
    const GetNextImageRequest& request,
    const base::ScopedFD& out_fd) {
  ScopeLogger scope("DBusServiceAdaptor::GetNextImage");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return manager_->GetNextImage(std::move(response), request, out_fd);
}

CancelScanResponse DBusServiceAdaptor::CancelScan(
    const CancelScanRequest& request) {
  ScopeLogger scope("DBusServiceAdaptor::CancelScan");
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return manager_->CancelScan(request);
}

}  // namespace lorgnette