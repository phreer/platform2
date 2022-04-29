// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biometrics_daemon.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/dbus/async_event_sequencer.h>

#include "biod/cros_fp_biometrics_manager.h"
#include "biod/power_button_filter.h"
#include "biod/utils.h"

namespace biod {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusInterface;
using brillo::dbus_utils::DBusObject;
using brillo::dbus_utils::ExportedObjectManager;
using brillo::dbus_utils::ExportedProperty;
using brillo::dbus_utils::ExportedPropertySet;
using dbus::ObjectPath;

namespace errors {
const char kDomain[] = "biod";
const char kInternalError[] = "internal_error";
const char kInvalidArguments[] = "invalid_arguments";
}  // namespace errors

BiometricsManagerWrapper::BiometricsManagerWrapper(
    std::unique_ptr<BiometricsManager> biometrics_manager,
    ExportedObjectManager* object_manager,
    ObjectPath object_path,
    const AsyncEventSequencer::CompletionAction& completion_callback)
    : biometrics_manager_(std::move(biometrics_manager)),
      dbus_object_(object_manager, object_manager->GetBus(), object_path),
      object_path_(std::move(object_path)),
      enroll_session_object_path_(object_path_.value() + "/EnrollSession"),
      auth_session_object_path_(object_path_.value() + "/AuthSession") {
  CHECK(biometrics_manager_);

  biometrics_manager_->SetEnrollScanDoneHandler(base::BindRepeating(
      &BiometricsManagerWrapper::OnEnrollScanDone, base::Unretained(this)));
  biometrics_manager_->SetAuthScanDoneHandler(base::BindRepeating(
      &BiometricsManagerWrapper::OnAuthScanDone, base::Unretained(this)));
  biometrics_manager_->SetSessionFailedHandler(base::BindRepeating(
      &BiometricsManagerWrapper::OnSessionFailed, base::Unretained(this)));

  dbus::ObjectProxy* bus_proxy = object_manager->GetBus()->GetObjectProxy(
      dbus::kDBusServiceName, dbus::ObjectPath(dbus::kDBusServicePath));
  bus_proxy->ConnectToSignal(
      dbus::kDBusInterface, "NameOwnerChanged",
      base::BindRepeating(&BiometricsManagerWrapper::OnNameOwnerChanged,
                          base::Unretained(this)),
      base::BindOnce(&LogOnSignalConnected));

  DBusInterface* bio_interface =
      dbus_object_.AddOrGetInterface(kBiometricsManagerInterface);
  property_type_.SetValue(
      static_cast<uint32_t>(biometrics_manager_->GetType()));
  bio_interface->AddProperty(kBiometricsManagerBiometricTypeProperty,
                             &property_type_);
  bio_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      kBiometricsManagerStartEnrollSessionMethod,
      base::BindRepeating(&BiometricsManagerWrapper::StartEnrollSession,
                          base::Unretained(this)));
  bio_interface->AddSimpleMethodHandlerWithError(
      kBiometricsManagerGetRecordsForUserMethod,
      base::BindRepeating(&BiometricsManagerWrapper::GetRecordsForUser,
                          base::Unretained(this)));
  bio_interface->AddSimpleMethodHandlerWithError(
      kBiometricsManagerDestroyAllRecordsMethod,
      base::BindRepeating(&BiometricsManagerWrapper::DestroyAllRecords,
                          base::Unretained(this)));
  bio_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      kBiometricsManagerStartAuthSessionMethod,
      base::BindRepeating(&BiometricsManagerWrapper::StartAuthSession,
                          base::Unretained(this)));
  dbus_object_.RegisterAsync(completion_callback);

  RefreshRecordObjects();
}

BiometricsManagerWrapper::RecordWrapper::RecordWrapper(
    BiometricsManagerWrapper* biometrics_manager,
    std::unique_ptr<BiometricsManagerRecord> record,
    ExportedObjectManager* object_manager,
    const ObjectPath& object_path)
    : biometrics_manager_(biometrics_manager),
      record_(std::move(record)),
      dbus_object_(object_manager, object_manager->GetBus(), object_path),
      object_path_(object_path) {
  DBusInterface* record_interface =
      dbus_object_.AddOrGetInterface(kRecordInterface);
  property_label_.SetValue(record_->GetLabel());
  record_interface->AddProperty(kRecordLabelProperty, &property_label_);
  record_interface->AddSimpleMethodHandlerWithError(
      kRecordSetLabelMethod,
      base::BindRepeating(&RecordWrapper::SetLabel, base::Unretained(this)));
  record_interface->AddSimpleMethodHandlerWithError(
      kRecordRemoveMethod,
      base::BindRepeating(&RecordWrapper::Remove, base::Unretained(this)));
  dbus_object_.RegisterAndBlock();
}

BiometricsManagerWrapper::RecordWrapper::~RecordWrapper() {
  dbus_object_.UnregisterAsync();
}

bool BiometricsManagerWrapper::RecordWrapper::SetLabel(
    brillo::ErrorPtr* error, const std::string& new_label) {
  if (!record_->SetLabel(new_label)) {
    *error =
        brillo::Error::Create(FROM_HERE, errors::kDomain,
                              errors::kInternalError, "Failed to set label");
    return false;
  }
  property_label_.SetValue(new_label);
  return true;
}

bool BiometricsManagerWrapper::RecordWrapper::Remove(brillo::ErrorPtr* error) {
  if (!record_->Remove()) {
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to remove record");
    return false;
  }
  biometrics_manager_->RefreshRecordObjects();
  return true;
}

void BiometricsManagerWrapper::FinalizeEnrollSessionObject() {
  enroll_session_owner_.clear();
  enroll_session_dbus_object_->UnregisterAsync();
  enroll_session_dbus_object_.reset();
}

void BiometricsManagerWrapper::FinalizeAuthSessionObject() {
  auth_session_owner_.clear();
  auth_session_dbus_object_->UnregisterAsync();
  auth_session_dbus_object_.reset();
}

void BiometricsManagerWrapper::OnNameOwnerChanged(dbus::Signal* sig) {
  dbus::MessageReader reader(sig);
  std::string name, old_owner, new_owner;
  if (!reader.PopString(&name) || !reader.PopString(&old_owner) ||
      !reader.PopString(&new_owner)) {
    LOG(ERROR) << "Received invalid NameOwnerChanged signal";
    return;
  }

  // We are only interested in cases where a name gets dropped from D-Bus.
  if (name.empty() || !new_owner.empty())
    return;

  // If one of the session was owned by the dropped name, the session should
  // also be dropped, as there is nobody left to end it explicitly.

  if (name == enroll_session_owner_) {
    LOG(INFO) << "EnrollSession object owner " << enroll_session_owner_
              << " has died. EnrollSession is canceled automatically.";
    if (enroll_session_)
      enroll_session_.End();

    if (enroll_session_dbus_object_)
      FinalizeEnrollSessionObject();
  }

  if (name == auth_session_owner_) {
    LOG(INFO) << "AuthSession object owner " << auth_session_owner_
              << " has died. AuthSession is ended automatically.";
    if (auth_session_)
      auth_session_.End();

    if (auth_session_dbus_object_)
      FinalizeAuthSessionObject();
  }
}

void BiometricsManagerWrapper::OnEnrollScanDone(
    ScanResult scan_result,
    const BiometricsManager::EnrollStatus& enroll_status) {
  if (!enroll_session_dbus_object_)
    return;

  dbus::Signal enroll_scan_done_signal(kBiometricsManagerInterface,
                                       kBiometricsManagerEnrollScanDoneSignal);
  dbus::MessageWriter writer(&enroll_scan_done_signal);
  EnrollScanDone proto;
  proto.set_scan_result(scan_result);
  proto.set_done(enroll_status.done);
  if (enroll_status.percent_complete >= 0) {
    proto.set_percent_complete(enroll_status.percent_complete);
  }
  writer.AppendProtoAsArrayOfBytes(proto);
  dbus_object_.SendSignal(&enroll_scan_done_signal);
  if (enroll_status.done) {
    enroll_session_.End();
    FinalizeEnrollSessionObject();
    RefreshRecordObjects();
  }
}

void BiometricsManagerWrapper::OnAuthScanDone(
    FingerprintMessage result, BiometricsManager::AttemptMatches matches) {
  if (!auth_session_dbus_object_)
    return;

  dbus::Signal auth_scan_done_signal(kBiometricsManagerInterface,
                                     kBiometricsManagerAuthScanDoneSignal);
  dbus::MessageWriter writer(&auth_scan_done_signal);
  writer.AppendProtoAsArrayOfBytes(result);
  dbus::MessageWriter matches_writer(nullptr);
  writer.OpenArray("{sao}", &matches_writer);
  for (const auto& match : matches) {
    dbus::MessageWriter entry_writer(nullptr);
    matches_writer.OpenDictEntry(&entry_writer);
    entry_writer.AppendString(match.first);
    std::vector<ObjectPath> record_object_paths;
    record_object_paths.resize(match.second.size());
    std::transform(match.second.begin(), match.second.end(),
                   record_object_paths.begin(),
                   [this](const std::string& record_id) {
                     return ObjectPath(object_path_.value() +
                                       std::string("/Record") + record_id);
                   });
    entry_writer.AppendArrayOfObjectPaths(record_object_paths);
    matches_writer.CloseContainer(&entry_writer);
  }
  writer.CloseContainer(&matches_writer);
  dbus_object_.SendSignal(&auth_scan_done_signal);
}

void BiometricsManagerWrapper::OnSessionFailed() {
  if (enroll_session_dbus_object_) {
    dbus::Signal session_failed_signal(kBiometricsManagerInterface,
                                       kBiometricsManagerSessionFailedSignal);
    dbus_object_.SendSignal(&session_failed_signal);
    FinalizeEnrollSessionObject();
  }

  if (enroll_session_)
    enroll_session_.End();

  if (auth_session_dbus_object_) {
    dbus::Signal session_failed_signal(kBiometricsManagerInterface,
                                       kBiometricsManagerSessionFailedSignal);
    dbus_object_.SendSignal(&session_failed_signal);
    FinalizeAuthSessionObject();
  }

  if (auth_session_)
    auth_session_.End();
}

bool BiometricsManagerWrapper::StartEnrollSession(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const std::string& user_id,
    const std::string& label,
    ObjectPath* enroll_session_path) {
  BiometricsManager::EnrollSession enroll_session =
      biometrics_manager_->StartEnrollSession(user_id, label);
  if (!enroll_session) {
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to start EnrollSession");
    return false;
  }
  enroll_session_ = std::move(enroll_session);

  enroll_session_dbus_object_ = std::make_unique<DBusObject>(
      nullptr, dbus_object_.GetBus(), enroll_session_object_path_);
  DBusInterface* enroll_session_interface =
      enroll_session_dbus_object_->AddOrGetInterface(kEnrollSessionInterface);
  enroll_session_interface->AddSimpleMethodHandlerWithError(
      kEnrollSessionCancelMethod,
      base::BindRepeating(&BiometricsManagerWrapper::EnrollSessionCancel,
                          base::Unretained(this)));
  enroll_session_dbus_object_->RegisterAndBlock();
  *enroll_session_path = enroll_session_object_path_;
  enroll_session_owner_ = message->GetSender();

  return true;
}

bool BiometricsManagerWrapper::GetRecordsForUser(brillo::ErrorPtr* error,
                                                 const std::string& user_id,
                                                 std::vector<ObjectPath>* out) {
  for (const auto& record : records_) {
    if (record->GetUserId() == user_id)
      out->emplace_back(record->path());
  }
  return true;
}

bool BiometricsManagerWrapper::DestroyAllRecords(brillo::ErrorPtr* error) {
  if (!biometrics_manager_->DestroyAllRecords()) {
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to destroy all records");
    return false;
  }
  RefreshRecordObjects();
  return true;
}

bool BiometricsManagerWrapper::StartAuthSession(brillo::ErrorPtr* error,
                                                dbus::Message* message,
                                                ObjectPath* auth_session_path) {
  BiometricsManager::AuthSession auth_session =
      biometrics_manager_->StartAuthSession();
  if (!auth_session) {
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to start AuthSession");
    return false;
  }
  auth_session_ = std::move(auth_session);

  auth_session_dbus_object_ = std::make_unique<DBusObject>(
      nullptr, dbus_object_.GetBus(), auth_session_object_path_);
  DBusInterface* auth_session_interface =
      auth_session_dbus_object_->AddOrGetInterface(kAuthSessionInterface);
  auth_session_interface->AddSimpleMethodHandlerWithError(
      kAuthSessionEndMethod,
      base::BindRepeating(&BiometricsManagerWrapper::AuthSessionEnd,
                          base::Unretained(this)));
  auth_session_dbus_object_->RegisterAndBlock();
  *auth_session_path = auth_session_object_path_;
  auth_session_owner_ = message->GetSender();

  return true;
}

bool BiometricsManagerWrapper::EnrollSessionCancel(brillo::ErrorPtr* error) {
  if (!enroll_session_) {
    LOG(WARNING) << "DBus client attempted to cancel null EnrollSession";
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInvalidArguments,
                                   "EnrollSession object was null");
    return false;
  }
  enroll_session_.End();
  // TODO(crbug.com/715302): FpcBiometricsManager need to wait here for
  // EnrollSession to end completely before any other session could start. Wait
  // time is ~200 milliseconds.

  if (enroll_session_dbus_object_) {
    FinalizeEnrollSessionObject();
  }
  return true;
}

bool BiometricsManagerWrapper::AuthSessionEnd(brillo::ErrorPtr* error) {
  if (!auth_session_) {
    LOG(WARNING) << "DBus client attempted to cancel null AuthSession";
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInvalidArguments,
                                   "AuthSession object was null");
    return false;
  }
  auth_session_.End();
  // TODO(crbug.com/715302): FpcBiometricsManager need to wait here for
  // AuthSession to end completely before any other session could start. Wait
  // time is ~200 milliseconds.

  if (auth_session_dbus_object_) {
    FinalizeAuthSessionObject();
  }
  return true;
}

void BiometricsManagerWrapper::RefreshRecordObjects() {
  records_.clear();
  std::vector<std::unique_ptr<BiometricsManagerRecord>> records =
      biometrics_manager_->GetLoadedRecords();

  ExportedObjectManager* object_manager = dbus_object_.GetObjectManager().get();
  std::string records_root_path = object_path_.value() + std::string("/Record");

  for (std::unique_ptr<BiometricsManagerRecord>& record : records) {
    ObjectPath record_path(records_root_path + record->GetId());
    records_.emplace_back(std::make_unique<RecordWrapper>(
        this, std::move(record), object_manager, record_path));
  }
}

BiometricsDaemon::BiometricsDaemon() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = base::MakeRefCounted<dbus::Bus>(options);
  CHECK(bus_->Connect()) << "Failed to connect to system D-Bus";

  object_manager_ = std::make_unique<ExportedObjectManager>(
      bus_, ObjectPath(kBiodServicePath));

  auto sequencer = base::MakeRefCounted<AsyncEventSequencer>();
  object_manager_->RegisterAsync(
      sequencer->GetHandler("Manager.RegisterAsync() failed.", true));

  ObjectPath cros_fp_bio_path = ObjectPath(base::StringPrintf(
      "%s/%s", kBiodServicePath, kCrosFpBiometricsManagerName));

  auto biod_metrics = std::make_unique<BiodMetrics>();
  auto cros_fp_device = CrosFpDevice::Create(
      biod_metrics.get(), std::make_unique<ec::EcCommandFactory>());
  CHECK(cros_fp_device) << "Failed to initialize CrosFpDevice.";
  auto power_button_filter = PowerButtonFilter::Create(bus_);
  CHECK(power_button_filter) << "Failed to initialize PowerButtonFilter.";
  auto biod_storage =
      std::make_unique<BiodStorage>(biod::kCrosFpBiometricsManagerName);

  session_state_manager_ = std::make_unique<SessionStateManager>(bus_.get());

  auto cros_fp_bio = std::make_unique<CrosFpBiometricsManager>(
      std::move(power_button_filter), std::move(cros_fp_device),
      std::move(biod_metrics),
      std::make_unique<CrosFpRecordManager>(std::move(biod_storage)));
  if (cros_fp_bio) {
    biometrics_managers_.emplace_back(
        std::make_unique<BiometricsManagerWrapper>(
            std::move(cros_fp_bio), object_manager_.get(), cros_fp_bio_path,
            sequencer->GetHandler(
                "Failed to register CrosFpBiometricsManager object", true)));
  } else {
    LOG(INFO) << "No CrosFpBiometricsManager detected.";
  }

  session_state_manager_->AddObserver(this);
  session_state_manager_->RefreshPrimaryUser();

  CHECK(bus_->RequestOwnershipAndBlock(kBiodServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY));
}

void BiometricsDaemon::OnUserLoggedIn(const std::string& sanitized_username,
                                      bool is_new_login) {
  for (const auto& biometrics_manager_wrapper : biometrics_managers_) {
    if (!biometrics_manager_wrapper->get().ResetSensor()) {
      LOG(ERROR) << "Failed to reset biometric sensor type: "
                 << biometrics_manager_wrapper->get().GetType();
    }
    biometrics_manager_wrapper->get().SetDiskAccesses(true);
    biometrics_manager_wrapper->get().ReadRecordsForSingleUser(
        sanitized_username);
    biometrics_manager_wrapper->RefreshRecordObjects();
    if (is_new_login) {
      biometrics_manager_wrapper->get().SendStatsOnLogin();
    }
  }
}

void BiometricsDaemon::OnUserLoggedOut() {
  // Assuming that log out will always log out all users at the same time.
  for (const auto& biometrics_manager_wrapper : biometrics_managers_) {
    biometrics_manager_wrapper->get().SetDiskAccesses(false);
    biometrics_manager_wrapper->get().RemoveRecordsFromMemory();
    biometrics_manager_wrapper->RefreshRecordObjects();
  }
}
}  // namespace biod
