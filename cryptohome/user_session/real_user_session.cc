// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session/real_user_session.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/notreached.h>
#include <base/values.h>
#include <brillo/cryptohome.h>
#include <cryptohome/scrypt_verifier.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/sha.h>

#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/error/cryptohome_mount_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/pkcs11_token.h"
#include "cryptohome/pkcs11/pkcs11_token_factory.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/error.h"
#include "cryptohome/storage/mount.h"

using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeMountError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::HmacSha256;
using hwsec_foundation::Sha256;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

// Message to use when generating a secret for WebAuthn.
constexpr char kWebAuthnSecretHmacMessage[] = "AuthTimeWebAuthnSecret";

// Message to use when generating a secret for hibernate.
constexpr char kHibernateSecretHmacMessage[] = "AuthTimeHibernateSecret";

RealUserSession::RealUserSession(
    const std::string& username,
    HomeDirs* homedirs,
    KeysetManagement* keyset_management,
    UserOldestActivityTimestampManager* user_activity_timestamp_manager,
    Pkcs11TokenFactory* pkcs11_token_factory,
    const scoped_refptr<Mount> mount)
    : username_(username),
      obfuscated_username_(SanitizeUserName(username_)),
      homedirs_(homedirs),
      keyset_management_(keyset_management),
      user_activity_timestamp_manager_(user_activity_timestamp_manager),
      pkcs11_token_factory_(pkcs11_token_factory),
      mount_(mount) {}

MountStatus RealUserSession::MountVault(
    const std::string& username,
    const FileSystemKeyset& fs_keyset,
    const CryptohomeVault::Options& vault_options) {
  if (username_ != username) {
    NOTREACHED() << "MountVault username mismatch.";
  }

  StorageStatus status =
      mount_->MountCryptohome(username, fs_keyset, vault_options);
  if (!status.ok()) {
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(kLocUserSessionMountFailedInMountVault),
        ErrorActionSet({ErrorAction::kRetry, ErrorAction::kAuth,
                        ErrorAction::kDeleteVault, ErrorAction::kPowerwash}),
        status->error());
  }

  user_activity_timestamp_manager_->UpdateTimestamp(obfuscated_username_,
                                                    base::TimeDelta());
  pkcs11_token_ = pkcs11_token_factory_->New(
      username, homedirs_->GetChapsTokenDir(username), fs_keyset.chaps_key());

  // u2fd only needs to fetch the secret hash and not the secret itself when
  // mounting.
  PrepareWebAuthnSecretHash(fs_keyset.Key().fek, fs_keyset.Key().fnek);
  PrepareHibernateSecret(fs_keyset.Key().fek, fs_keyset.Key().fnek);

  return OkStatus<CryptohomeMountError>();
}

MountStatus RealUserSession::MountEphemeral(const std::string& username) {
  if (username_ != username) {
    NOTREACHED() << "MountEphemeral username mismatch.";
  }

  if (homedirs_->IsOrWillBeOwner(username)) {
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(kLocUserSessionOwnerNotSupportedInMountEphemeral),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER);
  }

  StorageStatus status = mount_->MountEphemeralCryptohome(username);
  if (status.ok()) {
    pkcs11_token_ = pkcs11_token_factory_->New(
        username_, homedirs_->GetChapsTokenDir(username_),
        brillo::SecureBlob());
    return OkStatus<CryptohomeMountError>();
  }

  return MakeStatus<CryptohomeMountError>(
      CRYPTOHOME_ERR_LOC(kLocUserSessionMountFailedInMountEphemeral),
      ErrorActionSet(
          {ErrorAction::kRetry, ErrorAction::kReboot, ErrorAction::kPowerwash}),
      status->error());
}

MountStatus RealUserSession::MountGuest() {
  if (username_ != kGuestUserName) {
    NOTREACHED() << "MountGuest username mismatch.";
  }

  StorageStatus status = mount_->MountEphemeralCryptohome(kGuestUserName);
  if (status.ok()) {
    return OkStatus<CryptohomeMountError>();
  }
  return MakeStatus<CryptohomeMountError>(
      CRYPTOHOME_ERR_LOC(kLocUserSessionMountEphemeralFailed),
      ErrorActionSet(
          {ErrorAction::kRetry, ErrorAction::kReboot, ErrorAction::kPowerwash}),
      status->error(), std::nullopt);
}

bool RealUserSession::Unmount() {
  if (pkcs11_token_) {
    pkcs11_token_->Remove();
    pkcs11_token_.reset();
  }
  if (mount_->IsNonEphemeralMounted()) {
    user_activity_timestamp_manager_->UpdateTimestamp(obfuscated_username_,
                                                      base::TimeDelta());
  }
  return mount_->UnmountCryptohome();
}

base::Value RealUserSession::GetStatus() const {
  base::Value dv(base::Value::Type::DICTIONARY);
  std::string user = SanitizeUserName(username_);
  base::Value keysets(base::Value::Type::LIST);
  std::vector<int> key_indices;
  if (user.length() &&
      keyset_management_->GetVaultKeysets(user, &key_indices)) {
    for (auto key_index : key_indices) {
      base::Value keyset_dict(base::Value::Type::DICTIONARY);
      std::unique_ptr<VaultKeyset> keyset(
          keyset_management_->LoadVaultKeysetForUser(user, key_index));
      if (keyset.get()) {
        bool tpm = keyset->GetFlags() & SerializedVaultKeyset::TPM_WRAPPED;
        bool scrypt =
            keyset->GetFlags() & SerializedVaultKeyset::SCRYPT_WRAPPED;
        keyset_dict.SetBoolKey("tpm", tpm);
        keyset_dict.SetBoolKey("scrypt", scrypt);
        keyset_dict.SetBoolKey("ok", true);
        if (keyset->HasKeyData()) {
          keyset_dict.SetStringKey("label", keyset->GetKeyData().label());
        }
      } else {
        keyset_dict.SetBoolKey("ok", false);
      }
      keyset_dict.SetIntKey("index", key_index);
      keysets.Append(std::move(keyset_dict));
    }
  }
  dv.SetKey("keysets", std::move(keysets));
  dv.SetBoolKey("mounted", mount_->IsMounted());
  std::string obfuscated_owner;
  homedirs_->GetOwner(&obfuscated_owner);
  dv.SetStringKey("owner", obfuscated_owner);
  dv.SetBoolKey("enterprise", homedirs_->enterprise_owned());

  dv.SetStringKey("type", mount_->GetMountTypeString());

  return dv;
}

void RealUserSession::PrepareWebAuthnSecret(const brillo::SecureBlob& fek,
                                            const brillo::SecureBlob& fnek) {
  // This WebAuthn secret can be rederived upon in-session user auth success
  // since they will unlock the vault keyset.
  const std::string message(kWebAuthnSecretHmacMessage);

  webauthn_secret_ = std::make_unique<brillo::SecureBlob>(
      HmacSha256(brillo::SecureBlob::Combine(fnek, fek),
                 brillo::Blob(message.cbegin(), message.cend())));
  webauthn_secret_hash_ = Sha256(*webauthn_secret_);

  clear_webauthn_secret_timer_.Start(
      FROM_HERE, base::Seconds(10),
      base::BindOnce(&RealUserSession::ClearWebAuthnSecret,
                     base::Unretained(this)));
}

void RealUserSession::PrepareWebAuthnSecretHash(
    const brillo::SecureBlob& fek, const brillo::SecureBlob& fnek) {
  // This WebAuthn secret can be rederived upon in-session user auth success
  // since they will unlock the vault keyset.
  const std::string message(kWebAuthnSecretHmacMessage);

  // Only need to store the hash in this method, don't need the secret itself.
  auto webauthn_secret = std::make_unique<brillo::SecureBlob>(
      HmacSha256(brillo::SecureBlob::Combine(fnek, fek),
                 brillo::Blob(message.cbegin(), message.cend())));
  webauthn_secret_hash_ = Sha256(*webauthn_secret);
}

void RealUserSession::ClearWebAuthnSecret() {
  webauthn_secret_.reset();
}

std::unique_ptr<brillo::SecureBlob> RealUserSession::GetWebAuthnSecret() {
  return std::move(webauthn_secret_);
}

const brillo::SecureBlob& RealUserSession::GetWebAuthnSecretHash() const {
  return webauthn_secret_hash_;
}

void RealUserSession::PrepareHibernateSecret(const brillo::SecureBlob& fek,
                                             const brillo::SecureBlob& fnek) {
  // This hibernate secret can be rederived upon in-session user auth success
  // since they will unlock the vault keyset.
  const std::string message(kHibernateSecretHmacMessage);

  hibernate_secret_ = std::make_unique<brillo::SecureBlob>(
      HmacSha256(brillo::SecureBlob::Combine(fnek, fek),
                 brillo::Blob(message.cbegin(), message.cend())));

  clear_hibernate_secret_timer_.Start(
      FROM_HERE, base::Seconds(600),
      base::BindOnce(&RealUserSession::ClearHibernateSecret,
                     base::Unretained(this)));
}

void RealUserSession::ClearHibernateSecret() {
  hibernate_secret_.reset();
}

std::unique_ptr<brillo::SecureBlob> RealUserSession::GetHibernateSecret() {
  return std::move(hibernate_secret_);
}

void RealUserSession::SetCredentials(const Credentials& credentials) {
  if (obfuscated_username_ != credentials.GetObfuscatedUsername()) {
    NOTREACHED() << "SetCredentials username mismatch.";
    return;
  }

  key_data_ = credentials.key_data();

  credential_verifier_.reset(new ScryptVerifier(key_data_.label()));
  if (!credential_verifier_->Set(credentials.passkey())) {
    LOG(WARNING) << "CredentialVerifier could not be set";
  }
}

void RealUserSession::SetCredentials(AuthSession* auth_session) {
  if (obfuscated_username_ != auth_session->obfuscated_username()) {
    NOTREACHED() << "SetCredentials auth session username mismatch.";
    return;
  }

  key_data_ = auth_session->current_key_data();
  credential_verifier_ = auth_session->TakeCredentialVerifier();
}

bool RealUserSession::VerifyUser(const std::string& obfuscated_username) const {
  return obfuscated_username_ == obfuscated_username;
}

// TODO(betuls): Move credential verification to AuthBlocks once AuthBlock
// refactor is completed.
bool RealUserSession::VerifyCredentials(const Credentials& credentials) const {
  ReportTimerStart(kSessionUnlockTimer);

  if (!credential_verifier_) {
    LOG(ERROR) << "Attempt to verify credentials with no verifier set";
    return false;
  }
  if (!VerifyUser(credentials.GetObfuscatedUsername())) {
    return false;
  }
  // If the incoming credentials have no label, then just test the secret. If it
  // is labeled, then the label must match.
  if (!credentials.key_data().label().empty() &&
      credentials.key_data().label() !=
          credential_verifier_->auth_factor_label()) {
    return false;
  }

  bool status = credential_verifier_->Verify(credentials.passkey());

  ReportTimerStop(kSessionUnlockTimer);

  return status;
}

void RealUserSession::RemoveCredentialVerifierForKeyLabel(
    const std::string& key_label) {
  if (!credential_verifier_) {
    return;
  }

  // If the credential to remove has the same label as the current credential
  // verifier, then we reset the credential verifier and remove KeyData.
  if (key_label == credential_verifier_->auth_factor_label()) {
    credential_verifier_.reset();
    key_data_ = KeyData();
  }
}

bool RealUserSession::ResetApplicationContainer(
    const std::string& application) {
  if (!mount_->IsNonEphemeralMounted()) {
    return false;
  }

  return mount_->ResetApplicationContainer(application);
}

}  // namespace cryptohome
