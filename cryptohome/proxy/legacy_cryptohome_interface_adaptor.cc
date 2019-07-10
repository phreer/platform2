// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>

#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/proxy/legacy_cryptohome_interface_adaptor.h"

namespace cryptohome {

void LegacyCryptohomeInterfaceAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  // completion_callback is a callback that will be run when all method
  // registration have finished. We don't have anything to run after completion
  // so we'll just pass this along to libbrillo. This callback is typically used
  // to signal to the DBus Daemon that method registration is complete
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(completion_callback);

  // Register the dbus signal handlers
  userdataauth_proxy_->RegisterDircryptoMigrationProgressSignalHandler(
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::OnDircryptoMigrationProgressSignal,
          base::Unretained(this)),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::OnSignalConnectedHandler,
                 base::Unretained(this)));
}

void LegacyCryptohomeInterfaceAdaptor::IsMounted(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::IsMountedRequest request;
  userdataauth_proxy_->IsMountedAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::IsMountedOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::IsMountedOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::IsMountedReply& reply) {
  response->Return(reply.is_mounted());
}

void LegacyCryptohomeInterfaceAdaptor::IsMountedForUser(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool, bool>>
        response,
    const std::string& in_username) {
  auto response_shared = std::make_shared<SharedDBusMethodResponse<bool, bool>>(
      std::move(response));

  user_data_auth::IsMountedRequest request;
  request.set_username(in_username);
  userdataauth_proxy_->IsMountedAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::IsMountedForUserOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool, bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::IsMountedForUserOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool, bool>> response,
    const user_data_auth::IsMountedReply& reply) {
  response->Return(reply.is_mounted(), reply.is_ephemeral_mount());
}

void LegacyCryptohomeInterfaceAdaptor::ListKeysEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::ListKeysRequest& /*in_list_keys_request*/) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::ListKeysRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  // Note that in_list_keys_request is empty
  userdataauth_proxy_->ListKeysAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ListKeysExOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::ListKeysExOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::ListKeysReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(
      static_cast<cryptohome::CryptohomeErrorCode>(result.error()));
  cryptohome::ListKeysReply* result_extension =
      result.MutableExtension(cryptohome::ListKeysReply::reply);
  result_extension->mutable_labels()->CopyFrom(reply.labels());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::CheckKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::CheckKeyRequest& in_check_key_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::CheckKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  userdataauth_proxy_->CheckKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::CheckKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::RemoveKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::RemoveKeyRequest& in_remove_key_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::RemoveKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_key()->CopyFrom(in_remove_key_request.key());
  userdataauth_proxy_->RemoveKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::RemoveKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetKeyDataEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::GetKeyDataRequest& in_get_key_data_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetKeyDataRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_key()->CopyFrom(in_get_key_data_request.key());
  userdataauth_proxy_->GetKeyDataAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetKeyDataOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetKeyDataOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetKeyDataReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(
      static_cast<cryptohome::CryptohomeErrorCode>(result.error()));
  cryptohome::GetKeyDataReply* result_extension =
      result.MutableExtension(cryptohome::GetKeyDataReply::reply);
  result_extension->mutable_key_data()->CopyFrom(reply.key_data());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::MigrateKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::MigrateKeyRequest& in_migrate_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::MigrateKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.set_secret(in_migrate_request.secret());
  userdataauth_proxy_->MigrateKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::MigrateKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::AddKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::AddKeyRequest& in_add_key_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::AddKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_key()->CopyFrom(in_add_key_request.key());
  request.set_clobber_if_exists(in_add_key_request.clobber_if_exists());
  userdataauth_proxy_->AddKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::AddKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}
void LegacyCryptohomeInterfaceAdaptor::AddDataRestoreKey(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::UpdateKeyEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::UpdateKeyRequest& in_update_key_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::UpdateKeyRequest request;
  request.mutable_account_id()->CopyFrom(in_account_id);
  request.mutable_authorization_request()->CopyFrom(in_authorization_request);
  request.mutable_changes()->CopyFrom(in_update_key_request.changes());
  request.set_authorization_signature(
      in_update_key_request.authorization_signature());
  userdataauth_proxy_->UpdateKeyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::UpdateKeyReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::RemoveEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::RemoveRequest request;
  request.mutable_identifier()->CopyFrom(in_account);
  userdataauth_proxy_->RemoveAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::RemoveReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetSystemSalt(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>>
        response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>>>(
          std::move(response));

  user_data_auth::GetSystemSaltRequest request;
  misc_proxy_->GetSystemSaltAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::GetSystemSaltOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>>,
          base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetSystemSaltOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>> response,
    const user_data_auth::GetSystemSaltReply& reply) {
  std::vector<uint8_t> salt(reply.salt().begin(), reply.salt().end());
  response->Return(salt);
}

void LegacyCryptohomeInterfaceAdaptor::GetSanitizedUsername(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
        response,
    const std::string& in_username) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::string>>(
          std::move(response));

  user_data_auth::GetSanitizedUsernameRequest request;
  misc_proxy_->GetSanitizedUsernameAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetSanitizedUsernameOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetSanitizedUsernameOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::string>> response,
    const user_data_auth::GetSanitizedUsernameReply& reply) {
  response->Return(reply.sanitized_username());
}

void LegacyCryptohomeInterfaceAdaptor::MountEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::AuthorizationRequest& in_authorization_request,
    const cryptohome::MountRequest& in_mount_request) {
  std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>>
      response_shared =
          std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
              std::move(response));

  user_data_auth::MountRequest request;
  *request.mutable_account() = in_account_id;
  request.mutable_authorization()->CopyFrom(in_authorization_request);
  request.set_require_ephemeral(in_mount_request.require_ephemeral());
  if (in_mount_request.has_create()) {
    request.mutable_create()->mutable_keys()->CopyFrom(
        in_mount_request.create().keys());
    request.mutable_create()->set_copy_authorization_key(
        in_mount_request.create().copy_authorization_key());
    request.mutable_create()->set_force_ecryptfs(
        in_mount_request.create().force_ecryptfs());
  }
  request.set_force_dircrypto_if_available(
      in_mount_request.force_dircrypto_if_available());
  request.set_to_migrate_from_ecryptfs(
      in_mount_request.to_migrate_from_ecryptfs());
  request.set_public_mount(in_mount_request.public_mount());
  request.set_hidden_mount(in_mount_request.hidden_mount());
  request.set_guest_mount(false);
  // There's another MountGuestEx to handle guest mount. This method only
  // deal with non-guest mount so guest_mount is false here.

  userdataauth_proxy_->MountAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::MountExOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::MountExOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::MountReply& reply) {
  if (reply.error() ==
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT) {
    constexpr char error_msg[] =
        "Invalid argument on MountEx(), see logs for more details.";
    LOG(WARNING) << error_msg;
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_INVALID_ARGS, error_msg);
    return;
  }
  cryptohome::BaseReply result;
  result.set_error(static_cast<cryptohome::CryptohomeErrorCode>(reply.error()));
  MountReply* result_extension =
      result.MutableExtension(cryptohome::MountReply::reply);
  result_extension->set_recreated(reply.recreated());
  result_extension->set_sanitized_username(reply.sanitized_username());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::MountGuestEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::MountGuestRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::MountRequest request;
  request.set_guest_mount(true);

  userdataauth_proxy_->MountAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::MountReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::RenameCryptohome(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_cryptohome_id_from,
    const cryptohome::AccountIdentifier& in_cryptohome_id_to) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::RenameRequest request;
  *request.mutable_id_from() = in_cryptohome_id_from;
  *request.mutable_id_to() = in_cryptohome_id_to;
  userdataauth_proxy_->RenameAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::RenameReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetAccountDiskUsage(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::AccountIdentifier& in_account_id) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetAccountDiskUsageRequest request;
  *request.mutable_identifier() = in_account_id;
  userdataauth_proxy_->GetAccountDiskUsageAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetAccountDiskUsageOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetAccountDiskUsageOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetAccountDiskUsageReply& reply) {
  cryptohome::BaseReply result;
  result.set_error(
      static_cast<cryptohome::CryptohomeErrorCode>(result.error()));
  cryptohome::GetAccountDiskUsageReply* result_extension =
      result.MutableExtension(cryptohome::GetAccountDiskUsageReply::reply);
  result_extension->set_size(reply.size());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::UnmountEx(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::UnmountRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::UnmountRequest request;
  userdataauth_proxy_->UnmountAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardBaseReplyErrorCode<
                     user_data_auth::UnmountReply>,
                 response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::UpdateCurrentUserActivityTimestamp(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    int32_t in_time_shift_sec) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<>>(std::move(response));

  user_data_auth::UpdateCurrentUserActivityTimestampRequest request;
  request.set_time_shift_sec(in_time_shift_sec);
  misc_proxy_->UpdateCurrentUserActivityTimestampAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     UpdateCurrentUserActivityTimestampOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::
    UpdateCurrentUserActivityTimestampOnSuccess(
        std::shared_ptr<SharedDBusMethodResponse<>> response,
        const user_data_auth::UpdateCurrentUserActivityTimestampReply& reply) {
  if (reply.error() != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(WARNING) << "UpdateCurrentUserActivityTimestamp() failure received by "
                    "cryptohome-proxy, error "
                 << static_cast<int>(reply.error());
  }
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmIsReadyOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsReadyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const tpm_manager::GetTpmStatusReply& reply) {
  response->Return(reply.enabled() && reply.owned());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsEnabled(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmIsEnabledOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsEnabledOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const tpm_manager::GetTpmStatusReply& reply) {
  response->Return(reply.enabled());
}

void LegacyCryptohomeInterfaceAdaptor::TpmGetPassword(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
        response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::string>>(
          std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmGetPasswordOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmGetPasswordOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::string>> response,
    const tpm_manager::GetTpmStatusReply& reply) {
  response->Return(reply.local_data().owner_password());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsOwned(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  tpm_manager::GetTpmStatusRequest request;
  tpm_ownership_proxy_->GetTpmStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmIsOwnedOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsOwnedOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const tpm_manager::GetTpmStatusReply& reply) {
  response->Return(reply.owned());
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsBeingOwned(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  LOG(WARNING) << "Deprecated TpmIsBeingOwned is called.";
  response->Return(false);
}

void LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnership(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  tpm_manager::TakeOwnershipRequest request;
  tpm_ownership_proxy_->TakeOwnershipAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnershipOnSuccess,
          base::Unretained(this)),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnershipOnFailure,
          base::Unretained(this)));

  // Note that this method is special in the sense that this call will return
  // immediately as soon as the target method is called on the UserDataAuth
  // side. The result from the target method on UserDataAuth side is not passed
  // back to the caller of this method, but instead is logged if there's any
  // failure.
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnershipOnSuccess(
    const tpm_manager::TakeOwnershipReply& reply) {
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(WARNING) << "TakeOwnership failure observed in "
                    "TpmCanAttemptOwnership() of cryptohome-proxy. Status: "
                 << static_cast<int>(reply.status());
  }
}

void LegacyCryptohomeInterfaceAdaptor::TpmCanAttemptOwnershipOnFailure(
    brillo::Error* err) {
  // Note that creation of Error object already logs the error.
  LOG(WARNING) << "TakeOwnership encountered an error, observed in "
                  "TpmCanAttemptOwnership() of cryptohome-proxy.";
}

void LegacyCryptohomeInterfaceAdaptor::TpmClearStoredPassword(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<>>(std::move(response));

  tpm_manager::ClearStoredOwnerPasswordRequest request;
  tpm_ownership_proxy_->ClearStoredOwnerPasswordAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmClearStoredPasswordOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmClearStoredPasswordOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<>> response,
    const tpm_manager::ClearStoredOwnerPasswordReply& reply) {
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationPrepared(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  attestation::GetEnrollmentPreparationsRequest request;

  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation_proxy_->GetEnrollmentPreparationsAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationPreparedOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationPreparedOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::GetEnrollmentPreparationsReply& reply) {
  bool prepared = false;
  for (const auto& preparation : reply.enrollment_preparations()) {
    if (preparation.second) {
      prepared = true;
      break;
    }
  }

  response->Return(prepared);
}

void LegacyCryptohomeInterfaceAdaptor::
    TpmAttestationGetEnrollmentPreparationsEx(
        std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
            cryptohome::BaseReply>> response,
        const cryptohome::AttestationGetEnrollmentPreparationsRequest&
            in_request) {
  base::Optional<attestation::ACAType> aca_type;
  const int in_pca_type = in_request.pca_type();
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg =
        "Requested ACA type " + std::to_string(in_pca_type) +
        " is not supported in TpmAttestationGetEnrollmentPreparationsEx()";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }

  std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>>
      response_shared(new SharedDBusMethodResponse<cryptohome::BaseReply>(
          std::move(response)));

  attestation::GetEnrollmentPreparationsRequest request;
  request.set_aca_type(aca_type.value());

  attestation_proxy_->GetEnrollmentPreparationsAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationGetEnrollmentPreparationsExOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::
    TpmAttestationGetEnrollmentPreparationsExOnSuccess(
        std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>>
            response,
        const attestation::GetEnrollmentPreparationsReply& reply) {
  cryptohome::BaseReply result;
  AttestationGetEnrollmentPreparationsReply* extension =
      result.MutableExtension(AttestationGetEnrollmentPreparationsReply::reply);

  if (reply.status() != attestation::STATUS_SUCCESS) {
    // Failure.
    result.set_error(CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
  } else {
    auto map = reply.enrollment_preparations();
    for (auto it = map.cbegin(), end = map.cend(); it != end; ++it) {
      (*extension->mutable_enrollment_preparations())[it->first] = it->second;
    }
  }

  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::TpmVerifyAttestationData(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_cros_core) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::VerifyRequest request;
  request.set_cros_core(in_is_cros_core);
  request.set_ek_only(false);

  attestation_proxy_->VerifyAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::TpmVerifyAttestationDataOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmVerifyAttestationDataOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::VerifyReply& reply) {
  if (reply.status() != attestation::STATUS_SUCCESS) {
    std::string error_msg =
        "TpmVerifyAttestationData(): Attestation daemon returned status " +
        std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  response->Return(reply.verified());
}

void LegacyCryptohomeInterfaceAdaptor::TpmVerifyEK(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_cros_core) {
  std::shared_ptr<SharedDBusMethodResponse<bool>> response_shared(
      new SharedDBusMethodResponse<bool>(std::move(response)));

  attestation::VerifyRequest request;
  request.set_cros_core(in_is_cros_core);
  request.set_ek_only(true);

  attestation_proxy_->VerifyAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmVerifyEKOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmVerifyEKOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::VerifyReply& reply) {
  if (reply.status() != attestation::STATUS_SUCCESS) {
    std::string error_msg =
        "TpmVerifyEK(): Attestation daemon returned status " +
        std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  response->Return(reply.verified());
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationCreateEnrollRequest(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    int32_t in_pca_type) {
  attestation::CreateEnrollRequestRequest request;
  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg = "Requested ACA type " +
                            std::to_string(in_pca_type) + " is not supported";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }
  request.set_aca_type(aca_type.value());

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>>>(
          std::move(response));

  attestation_proxy_->CreateEnrollRequestAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationCreateEnrollRequestOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>>,
          base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::
    TpmAttestationCreateEnrollRequestOnSuccess(
        std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>>
            response,
        const attestation::CreateEnrollRequestReply& reply) {
  if (reply.status() != attestation::STATUS_SUCCESS) {
    std::string error_msg = "Attestation daemon returned status " +
                            std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  std::vector<uint8_t> result(reply.pca_request().begin(),
                              reply.pca_request().end());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationCreateEnrollRequest(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_pca_type) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationEnroll(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    int32_t in_pca_type,
    const std::vector<uint8_t>& in_pca_response) {
  attestation::FinishEnrollRequest request;
  request.set_pca_response(in_pca_response.data(), in_pca_response.size());
  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg = "Requested ACA type " +
                            std::to_string(in_pca_type) + " is not supported";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }
  request.set_aca_type(aca_type.value());

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));
  attestation_proxy_->FinishEnrollAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::TpmAttestationEnrollSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationEnrollSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const attestation::FinishEnrollReply& reply) {
  response->Return(reply.status() ==
                   attestation::AttestationStatus::STATUS_SUCCESS);
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationEnroll(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_pca_type,
    const std::vector<uint8_t>& in_pca_response) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationCreateCertRequest(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>>> response,
    int32_t in_pca_type,
    int32_t in_certificate_profile,
    const std::string& in_username,
    const std::string& in_request_origin) {
  attestation::CreateCertificateRequestRequest request;
  request.set_certificate_profile(
      IntegerToCertificateProfile(in_certificate_profile));
  request.set_username(in_username);
  request.set_request_origin(in_request_origin);
  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    std::string error_msg = "Requested ACA type " +
                            std::to_string(in_pca_type) + " is not supported";
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED, error_msg);
    return;
  }
  request.set_aca_type(aca_type.value());

  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>>>(
          std::move(response));
  attestation_proxy_->CreateCertificateRequestAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     TpmAttestationCreateCertRequestOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>>,
          base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationCreateCertRequestOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>>> response,
    const attestation::CreateCertificateRequestReply& reply) {
  if (reply.status() != attestation::STATUS_SUCCESS) {
    std::string error_msg = "Attestation daemon returned status " +
                            std::to_string(static_cast<int>(reply.status()));
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED, error_msg);
    return;
  }
  std::vector<uint8_t> result(reply.pca_request().begin(),
                              reply.pca_request().end());
  response->Return(result);
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationCreateCertRequest(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_pca_type,
    int32_t in_certificate_profile,
    const std::string& in_username,
    const std::string& in_request_origin) {
  attestation::CreateCertificateRequestRequest request;

  base::Optional<attestation::ACAType> aca_type;
  aca_type = IntegerToACAType(in_pca_type);
  if (!aca_type.has_value()) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_NOT_SUPPORTED,
                             "Requested ACA type is not supported");
    return;
  }

  request.set_aca_type(aca_type.value());
  request.set_certificate_profile(
      IntegerToCertificateProfile(in_certificate_profile));
  request.set_username(in_username);
  request.set_request_origin(in_request_origin);
  int async_id = HandleAsyncData<attestation::CreateCertificateRequestRequest,
                                 attestation::CreateCertificateRequestReply>(
      &attestation::CreateCertificateRequestReply::pca_request, request,
      base::BindOnce(&org::chromium::AttestationProxyInterface::
                         CreateCertificateRequestAsync,
                     base::Unretained(attestation_proxy_)));
  response->Return(async_id);
  return;
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationFinishCertRequest(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    const std::vector<uint8_t>& in_pca_response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::AsyncTpmAttestationFinishCertRequest(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    const std::vector<uint8_t>& in_pca_response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmIsAttestationEnrolled(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationDoesKeyExist(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetCertificate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetPublicKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetEnrollmentId(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    bool in_ignore_cache) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationRegisterKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSignEnterpriseChallenge(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name,
    const std::string& in_domain,
    const std::vector<uint8_t>& in_device_id,
    bool in_include_signed_public_key,
    const std::vector<uint8_t>& in_challenge) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSignEnterpriseVaChallenge(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    int32_t in_va_type,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name,
    const std::string& in_domain,
    const std::vector<uint8_t>& in_device_id,
    bool in_include_signed_public_key,
    const std::vector<uint8_t>& in_challenge) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSignSimpleChallenge(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name,
    const std::vector<uint8_t>& in_challenge) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetKeyPayload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationSetKeyPayload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_name,
    const std::vector<uint8_t>& in_payload) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationDeleteKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool in_is_user_specific,
    const std::string& in_username,
    const std::string& in_key_prefix) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationGetEK(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string, bool>>
        response) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmAttestationResetIdentity(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    const std::string& in_reset_token) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::TpmGetVersionStructured(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<uint32_t,
                                                           uint64_t,
                                                           uint32_t,
                                                           uint32_t,
                                                           uint64_t,
                                                           std::string>>
        response) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11IsTpmTokenReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::Pkcs11IsTpmTokenReadyRequest request;
  pkcs11_proxy_->Pkcs11IsTpmTokenReadyAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::Pkcs11IsTpmTokenReadyOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11IsTpmTokenReadyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::Pkcs11IsTpmTokenReadyReply& reply) {
  response->Return(reply.ready());
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfo(
    std::unique_ptr<brillo::dbus_utils::
                        DBusMethodResponse<std::string, std::string, int32_t>>
        response) {
  auto response_shared = std::make_shared<
      SharedDBusMethodResponse<std::string, std::string, int32_t>>(
      std::move(response));

  user_data_auth::Pkcs11GetTpmTokeInfoRequest request;
  pkcs11_proxy_->Pkcs11GetTpmTokeInfoAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfoOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string,
                                                          std::string, int32_t>,
          base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfoForUser(
    std::unique_ptr<brillo::dbus_utils::
                        DBusMethodResponse<std::string, std::string, int32_t>>
        response,
    const std::string& in_username) {
  auto response_shared = std::make_shared<
      SharedDBusMethodResponse<std::string, std::string, int32_t>>(
      std::move(response));

  user_data_auth::Pkcs11GetTpmTokeInfoRequest request;
  request.set_username(in_username);
  // Note that the response needed for Pkcs11GetTpmTokenInfo and
  // Pkcs11GetTpmTokenInfoForUser are the same, so we'll use the
  // Pkcs11GetTpmTokenInfo version here.
  pkcs11_proxy_->Pkcs11GetTpmTokeInfoAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfoOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::string,
                                                          std::string, int32_t>,
          base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11GetTpmTokenInfoOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::string, std::string, int32_t>>
        response,
    const user_data_auth::Pkcs11GetTpmTokeInfoReply& reply) {
  response->Return(reply.token_info().label(), reply.token_info().user_pin(),
                   reply.token_info().slot());
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11Terminate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    const std::string& in_username) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<>>(std::move(response));

  user_data_auth::Pkcs11TerminateRequest request;
  request.set_username(in_username);
  pkcs11_proxy_->Pkcs11TerminateAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::Pkcs11TerminateOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::Pkcs11TerminateOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<>> response,
    const user_data_auth::Pkcs11TerminateReply& reply) {
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::GetStatusString(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::string>>
        response) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesGet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<std::vector<uint8_t>,
                                                           bool>> response,
    const std::string& in_name) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>(
          std::move(response));

  user_data_auth::InstallAttributesGetRequest request;
  request.set_name(in_name);
  install_attributes_proxy_->InstallAttributesGetAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesGetOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::ForwardError<std::vector<uint8_t>,
                                                          bool>,
          base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesGetOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<std::vector<uint8_t>, bool>>
        response,
    const user_data_auth::InstallAttributesGetReply& reply) {
  std::vector<uint8_t> result(reply.value().begin(), reply.value().end());
  bool success = (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  response->Return(result, success);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesSet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    const std::string& in_name,
    const std::vector<uint8_t>& in_value) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesSetRequest request;
  request.set_name(in_name);
  *request.mutable_value() = {in_value.begin(), in_value.end()};
  install_attributes_proxy_->InstallAttributesSetAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesSetOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesSetOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesSetReply& reply) {
  bool success = (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  response->Return(success);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesCount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<int32_t>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesCountOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<int32_t>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesCountOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<int32_t>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  response->Return(reply.count());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesFinalize(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesFinalizeRequest request;
  install_attributes_proxy_->InstallAttributesFinalizeAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesFinalizeOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesFinalizeOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesFinalizeReply& reply) {
  bool success = (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  response->Return(success);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsReadyOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsReadyOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  bool ready =
      (reply.state() != user_data_auth::InstallAttributesState::UNKNOWN);
  response->Return(ready);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsSecure(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsSecureOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsSecureOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  response->Return(reply.is_secure());
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsInvalid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     InstallAttributesIsInvalidOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsInvalidOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  bool is_invalid =
      (reply.state() == user_data_auth::InstallAttributesState::INVALID);
  response->Return(is_invalid);
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsFirstInstall(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::InstallAttributesGetStatusRequest request;
  install_attributes_proxy_->InstallAttributesGetStatusAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::
                     InstallAttributesIsFirstInstallOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::InstallAttributesIsFirstInstallOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::InstallAttributesGetStatusReply& reply) {
  bool is_first_install =
      (reply.state() == user_data_auth::InstallAttributesState::FIRST_INSTALL);
  response->Return(is_first_install);
}

void LegacyCryptohomeInterfaceAdaptor::SignBootLockbox(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::SignBootLockboxRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::VerifyBootLockbox(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::VerifyBootLockboxRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::FinalizeBootLockbox(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::FinalizeBootLockboxRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::GetBootAttribute(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetBootAttributeRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::SetBootAttribute(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::SetBootAttributeRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::FlushAndSignBootAttributes(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::FlushAndSignBootAttributesRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::GetLoginStatus(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetLoginStatusRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::GetTpmStatus(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetTpmStatusRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::GetEndorsementInfo(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetEndorsementInfoRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::InitializeCastKey(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::InitializeCastKeyRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::GetFirmwareManagementParameters(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetFirmwareManagementParametersRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::SetFirmwareManagementParameters(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::SetFirmwareManagementParametersRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::RemoveFirmwareManagementParameters(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::RemoveFirmwareManagementParametersRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::MigrateToDircrypto(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    const cryptohome::AccountIdentifier& in_account_id,
    const cryptohome::MigrateToDircryptoRequest& in_migrate_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<>>(std::move(response));

  user_data_auth::StartMigrateToDircryptoRequest request;
  *request.mutable_account_id() = in_account_id;
  request.set_minimal_migration(in_migrate_request.minimal_migration());
  userdataauth_proxy_->StartMigrateToDircryptoAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::MigrateToDircryptoOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::MigrateToDircryptoOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<>> response,
    const user_data_auth::StartMigrateToDircryptoReply& reply) {
  if (reply.error() != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(WARNING) << "StartMigrateToDircryptoAsync() failed with error code "
                 << static_cast<int>(reply.error());
  }
  response->Return();
}

void LegacyCryptohomeInterfaceAdaptor::NeedsDircryptoMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    const cryptohome::AccountIdentifier& in_account_id) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::NeedsDircryptoMigrationRequest request;
  *request.mutable_account_id() = in_account_id;
  userdataauth_proxy_->NeedsDircryptoMigrationAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::NeedsDircryptoMigrationOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::NeedsDircryptoMigrationOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::NeedsDircryptoMigrationReply& reply) {
  if (reply.error() != user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    // There's an error, we should return an error.
    LOG(ERROR) << "NeedsDircryptoMigration returned "
               << static_cast<int>(reply.error());
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED,
                             "An error occurred on the UserDataAuth side when "
                             "proxying NeedsDircryptoMigration.");
    return;
  }
  response->Return(reply.needs_dircrypto_migration());
}

void LegacyCryptohomeInterfaceAdaptor::GetSupportedKeyPolicies(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetSupportedKeyPoliciesRequest& in_request) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<cryptohome::BaseReply>>(
          std::move(response));

  user_data_auth::GetSupportedKeyPoliciesRequest request;
  userdataauth_proxy_->GetSupportedKeyPoliciesAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetSupportedKeyPoliciesOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<
                     cryptohome::BaseReply>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetSupportedKeyPoliciesOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<cryptohome::BaseReply>> response,
    const user_data_auth::GetSupportedKeyPoliciesReply& reply) {
  cryptohome::BaseReply base_reply;
  cryptohome::GetSupportedKeyPoliciesReply* extension =
      base_reply.MutableExtension(
          cryptohome::GetSupportedKeyPoliciesReply::reply);

  extension->set_low_entropy_credentials(
      reply.low_entropy_credentials_supported());
  response->Return(base_reply);
}

void LegacyCryptohomeInterfaceAdaptor::IsQuotaSupported(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<bool>>(std::move(response));

  user_data_auth::GetArcDiskFeaturesRequest request;
  arc_quota_proxy_->GetArcDiskFeaturesAsync(
      request,
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::IsQuotaSupportedOnSuccess,
                 base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<bool>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::IsQuotaSupportedOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<bool>> response,
    const user_data_auth::GetArcDiskFeaturesReply& reply) {
  response->Return(reply.quota_supported());
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForUid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int64_t>> response,
    uint32_t in_uid) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<int64_t>>(std::move(response));

  user_data_auth::GetCurrentSpaceForArcUidRequest request;
  request.set_uid(in_uid);
  arc_quota_proxy_->GetCurrentSpaceForArcUidAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForUidOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<int64_t>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForUidOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<int64_t>> response,
    const user_data_auth::GetCurrentSpaceForArcUidReply& reply) {
  response->Return(reply.cur_space());
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForGid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int64_t>> response,
    uint32_t in_gid) {
  auto response_shared =
      std::make_shared<SharedDBusMethodResponse<int64_t>>(std::move(response));

  user_data_auth::GetCurrentSpaceForArcGidRequest request;
  request.set_gid(in_gid);
  arc_quota_proxy_->GetCurrentSpaceForArcGidAsync(
      request,
      base::Bind(
          &LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForGidOnSuccess,
          base::Unretained(this), response_shared),
      base::Bind(&LegacyCryptohomeInterfaceAdaptor::ForwardError<int64_t>,
                 base::Unretained(this), response_shared));
}

void LegacyCryptohomeInterfaceAdaptor::GetCurrentSpaceForGidOnSuccess(
    std::shared_ptr<SharedDBusMethodResponse<int64_t>> response,
    const user_data_auth::GetCurrentSpaceForArcGidReply& reply) {
  response->Return(reply.cur_space());
}

void LegacyCryptohomeInterfaceAdaptor::LockToSingleUserMountUntilReboot(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::LockToSingleUserMountUntilRebootRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::GetRsuDeviceId(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<cryptohome::BaseReply>> response,
    const cryptohome::GetRsuDeviceIdRequest& in_request) {
  // Not implemented yet
  response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                           DBUS_ERROR_NOT_SUPPORTED,
                           "Method unimplemented yet");
}

void LegacyCryptohomeInterfaceAdaptor::OnDircryptoMigrationProgressSignal(
    const user_data_auth::DircryptoMigrationProgress& progress) {
  VirtualSendDircryptoMigrationProgressSignal(
      dircrypto_data_migrator::MigrationHelper::ConvertDircryptoMigrationStatus(
          progress.status()),
      progress.current_bytes(), progress.total_bytes());
}

void LegacyCryptohomeInterfaceAdaptor::OnSignalConnectedHandler(
    const std::string& interface, const std::string& signal, bool success) {
  if (!success) {
    LOG(ERROR)
        << "Failure to connect DBus signal in cryptohome-proxy, interface="
        << interface << ", signal=" << signal;
  }
}

// A helper function which maps an integer to a valid CertificateProfile.
attestation::CertificateProfile
LegacyCryptohomeInterfaceAdaptor::IntegerToCertificateProfile(
    int profile_value) {
  // The protobuf compiler generates the _IsValid function.
  if (!attestation::CertificateProfile_IsValid(profile_value)) {
    return attestation::CertificateProfile::ENTERPRISE_USER_CERTIFICATE;
  }
  return static_cast<attestation::CertificateProfile>(profile_value);
}

// A helper function which maps an integer to a valid ACAType.
base::Optional<attestation::ACAType>
LegacyCryptohomeInterfaceAdaptor::IntegerToACAType(int type) {
  if (!attestation::ACAType_IsValid(type)) {
    return base::nullopt;
  }
  return static_cast<attestation::ACAType>(type);
}

}  // namespace cryptohome
