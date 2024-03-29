// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Mount.

#include "cryptohome/storage/mount.h"

#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/hash/sha1.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <brillo/cryptohome.h>
#include <brillo/process/process.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/error.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

using base::FilePath;
using base::StringPrintf;
using brillo::BlobToString;
using brillo::SecureBlob;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::IsSanitizedUserName;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::dircrypto_data_migrator::MigrationHelper;
using hwsec_foundation::SecureBlobToHex;

namespace {
constexpr bool __attribute__((unused)) MountUserSessionOOP() {
  return USE_MOUNT_OOP;
}

}  // namespace

namespace cryptohome {

void StartUserFileAttrsCleanerService(cryptohome::Platform* platform,
                                      const std::string& username) {
  std::unique_ptr<brillo::Process> file_attrs =
      platform->CreateProcessInstance();

  file_attrs->AddArg("/sbin/initctl");
  file_attrs->AddArg("start");
  file_attrs->AddArg("--no-wait");
  file_attrs->AddArg("file_attrs_cleaner_tool");
  file_attrs->AddArg(
      base::StringPrintf("OBFUSCATED_USERNAME=%s", username.c_str()));

  if (file_attrs->Run() != 0)
    PLOG(WARNING) << "Error while running file_attrs_cleaner_tool";
}

Mount::Mount(Platform* platform,
             HomeDirs* homedirs,
             bool legacy_mount,
             bool bind_mount_downloads,
             bool use_local_mounter)
    : platform_(platform),
      homedirs_(homedirs),
      legacy_mount_(legacy_mount),
      bind_mount_downloads_(bind_mount_downloads),
      dircrypto_migration_stopped_condition_(&active_dircrypto_migrator_lock_) {
  if (use_local_mounter) {
    active_mounter_.reset(
        new MountHelper(legacy_mount_, bind_mount_downloads_, platform_));
  } else {
    active_mounter_.reset(new OutOfProcessMountHelper(
        legacy_mount_, bind_mount_downloads_, platform_));
  }
}

Mount::Mount() : Mount(nullptr, nullptr) {}

Mount::~Mount() {
  if (IsMounted())
    UnmountCryptohome();
}

StorageStatus Mount::MountEphemeralCryptohome(const std::string& username) {
  username_ = username;
  std::string obfuscated_username = SanitizeUserName(username_);

  base::ScopedClosureRunner cleanup_runner(base::BindOnce(
      base::IgnoreResult(&Mount::UnmountCryptohome), base::Unretained(this)));

  // Ephemeral cryptohome can't be mounted twice.
  CHECK(active_mounter_->CanPerformEphemeralMount());

  user_cryptohome_vault_ = homedirs_->GetVaultFactory()->Generate(
      obfuscated_username, FileSystemKeyReference(),
      EncryptedContainerType::kEphemeral);

  if (!user_cryptohome_vault_) {
    return StorageStatus::Make(FROM_HERE, "Failed to generate ephemeral vault.",
                               MOUNT_ERROR_FATAL);
  }

  RETURN_IF_ERROR(user_cryptohome_vault_->Setup(FileSystemKey())).LogError()
      << "Failed to setup ephemeral vault";

  RETURN_IF_ERROR(
      active_mounter_->PerformEphemeralMount(
          username, user_cryptohome_vault_->GetContainerBackingLocation()))
          .LogError()
      << "PerformEphemeralMount() failed, aborting ephemeral mount";

  cleanup_runner.ReplaceClosure(base::DoNothing());

  return StorageStatus::Ok();
}

StorageStatus Mount::MountCryptohome(
    const std::string& username,
    const FileSystemKeyset& file_system_keyset,
    const CryptohomeVault::Options& vault_options) {
  username_ = username;
  std::string obfuscated_username = SanitizeUserName(username_);

  ASSIGN_OR_RETURN(EncryptedContainerType vault_type,
                   homedirs_->PickVaultType(obfuscated_username, vault_options),
                   (_.LogError() << "Could't pick vault type"));

  user_cryptohome_vault_ = homedirs_->GetVaultFactory()->Generate(
      obfuscated_username, file_system_keyset.KeyReference(), vault_type,
      homedirs_->KeylockerForStorageEncryptionEnabled());

  if (GetMountType() == MountType::NONE) {
    // TODO(dlunev): there should be a more proper error code set. CREATE_FAILED
    // is a temporary returned error to keep the behaviour unchanged while
    // refactoring.
    return StorageStatus::Make(FROM_HERE, "Could't generate vault",
                               MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED);
  }

  // Ensure we don't leave any mounts hanging on intermediate errors.
  // The closure won't outlive the class so |this| will always be valid.
  // |active_mounter_| will always be valid since this callback runs in the
  // destructor at the latest.
  base::ScopedClosureRunner cleanup_runner(base::BindOnce(
      base::IgnoreResult(&Mount::UnmountCryptohome), base::Unretained(this)));

  // Set up the cryptohome vault for mount.
  RETURN_IF_ERROR(user_cryptohome_vault_->Setup(file_system_keyset.Key()))
          .LogError()
      << "Failed to setup persisten vault";

  std::string key_signature =
      SecureBlobToHex(file_system_keyset.KeyReference().fek_sig);
  std::string fnek_signature =
      SecureBlobToHex(file_system_keyset.KeyReference().fnek_sig);

  cryptohome::ReportTimerStart(cryptohome::kPerformMountTimer);
  RETURN_IF_ERROR(active_mounter_->PerformMount(GetMountType(), username_,
                                                key_signature, fnek_signature))
          .LogError()
      << "MountHelper::PerformMount failed";

  cryptohome::ReportTimerStop(cryptohome::kPerformMountTimer);

  // Once mount is complete, do a deferred teardown for on the vault.
  // The teardown occurs when the vault's containers has no references ie. no
  // mount holds the containers open.
  // This is useful if cryptohome crashes: on recovery, if cryptohome decides to
  // cleanup mounts, the underlying devices (in case of dm-crypt cryptohome)
  // will be automatically torn down.

  // TODO(sarthakkukreti): remove this in favor of using the session-manager
  // as the source-of-truth during crash recovery. That would allow us to
  // reconstruct the run-time state of cryptohome vault(s) at the time of crash.
  std::ignore = user_cryptohome_vault_->SetLazyTeardownWhenUnused();

  // At this point we're done mounting.
  cleanup_runner.ReplaceClosure(base::DoNothing());

  user_cryptohome_vault_->ReportVaultEncryptionType();

  // Start file attribute cleaner service.
  StartUserFileAttrsCleanerService(platform_, obfuscated_username);

  // TODO(fqj,b/116072767) Ignore errors since unlabeled files are currently
  // still okay during current development progress.
  // Report the success rate of the restore SELinux context operation for user
  // directory to decide on the action on failure when we  move on to the next
  // phase in the cryptohome SELinux development, i.e. making cryptohome
  // enforcing.
  if (platform_->RestoreSELinuxContexts(UserPath(obfuscated_username),
                                        true /*recursive*/)) {
    ReportRestoreSELinuxContextResultForHomeDir(true);
  } else {
    ReportRestoreSELinuxContextResultForHomeDir(false);
    LOG(ERROR) << "RestoreSELinuxContexts(" << UserPath(obfuscated_username)
               << ") failed.";
  }

  return StorageStatus::Ok();
}

bool Mount::UnmountCryptohome() {
  // There should be no file access when unmounting.
  // Stop dircrypto migration if in progress.
  MaybeCancelMigrateEncryptionAndWait();

  active_mounter_->UnmountAll();

  // Resetting the vault teardowns the enclosed containers if setup succeeded.
  user_cryptohome_vault_.reset();

  return true;
}

void Mount::UnmountCryptohomeFromMigration() {
  active_mounter_->UnmountAll();

  // Resetting the vault teardowns the enclosed containers if setup succeeded.
  user_cryptohome_vault_.reset();
}

bool Mount::IsMounted() const {
  return active_mounter_ && active_mounter_->MountPerformed();
}

bool Mount::IsEphemeral() const {
  return GetMountType() == MountType::EPHEMERAL;
}

bool Mount::IsNonEphemeralMounted() const {
  return IsMounted() && !IsEphemeral();
}

bool Mount::OwnsMountPoint(const FilePath& path) const {
  return active_mounter_ && active_mounter_->IsPathMounted(path);
}

MountType Mount::GetMountType() const {
  if (!user_cryptohome_vault_) {
    return MountType::NONE;
  }
  return user_cryptohome_vault_->GetMountType();
}

std::string Mount::GetMountTypeString() const {
  switch (GetMountType()) {
    case MountType::NONE:
      return "none";
    case MountType::ECRYPTFS:
      return "ecryptfs";
    case MountType::DIR_CRYPTO:
      return "dircrypto";
    case MountType::ECRYPTFS_TO_DIR_CRYPTO:
      return "ecryptfs-to-dircrypto";
    case MountType::ECRYPTFS_TO_DMCRYPT:
      return "ecryptfs-to-dmcrypt";
    case MountType::DIR_CRYPTO_TO_DMCRYPT:
      return "dircrypto-to-dmcrypt";
    case MountType::EPHEMERAL:
      return "ephemeral";
    case MountType::DMCRYPT:
      return "dmcrypt";
  }
  return "";
}

bool Mount::MigrateEncryption(const MigrationHelper::ProgressCallback& callback,
                              MigrationType migration_type) {
  if (!IsMounted()) {
    LOG(ERROR) << "Not mounted.";
    return false;
  }
  auto mount_type = GetMountType();
  if (mount_type == MountType::ECRYPTFS_TO_DIR_CRYPTO ||
      mount_type == MountType::ECRYPTFS_TO_DMCRYPT) {
    return MigrateFromEcryptfs(callback, migration_type);
  }
  if (mount_type == MountType::DIR_CRYPTO_TO_DMCRYPT) {
    return MigrateFromDircrypto(callback, migration_type);
  }
  LOG(ERROR) << "Not mounted for migration.";
  return false;
}

bool Mount::MigrateFromEcryptfs(
    const MigrationHelper::ProgressCallback& callback,
    MigrationType migration_type) {
  std::string obfuscated_username = SanitizeUserName(username_);
  FilePath source = GetUserTemporaryMountDirectory(obfuscated_username);
  FilePath destination = GetUserMountDirectory(obfuscated_username);

  if (!platform_->DirectoryExists(source) || !OwnsMountPoint(source)) {
    LOG(ERROR) << "Unexpected ecryptfs source state.";
    return false;
  }
  // Do migration.
  constexpr uint64_t kMaxChunkSize = 128 * 1024 * 1024;
  if (!PerformMigration(callback, std::make_unique<MigrationHelper>(
                                      platform_, source, destination,
                                      UserPath(obfuscated_username),
                                      kMaxChunkSize, migration_type))) {
    return false;
  }

  // Clean up.
  FilePath vault_path = GetEcryptfsUserVaultPath(obfuscated_username);
  if (!platform_->DeletePathRecursively(source) ||
      !platform_->DeletePathRecursively(vault_path)) {
    LOG(ERROR) << "Failed to delete the old vault.";
    return false;
  }

  return true;
}

bool Mount::MigrateFromDircrypto(
    const MigrationHelper::ProgressCallback& callback,
    MigrationType migration_type) {
  std::string obfuscated_username = SanitizeUserName(username_);
  FilePath source = GetUserMountDirectory(obfuscated_username);
  FilePath destination = GetUserTemporaryMountDirectory(obfuscated_username);

  if (!platform_->DirectoryExists(destination) ||
      !OwnsMountPoint(destination)) {
    LOG(ERROR) << "Unexpected dmcrypt destination state.";
    return false;
  }
  // Do migration.
  constexpr uint64_t kMaxChunkSize = 128 * 1024 * 1024;
  if (!PerformMigration(callback, std::make_unique<MigrationHelper>(
                                      platform_, source, destination,
                                      UserPath(obfuscated_username),
                                      kMaxChunkSize, migration_type))) {
    return false;
  }

  // Clean up, in case of dircrypto source is the vault path, and destination
  // is a temp mount point.
  FilePath vault_path = GetEcryptfsUserVaultPath(obfuscated_username);
  if (!platform_->DeletePathRecursively(source) ||
      !platform_->DeletePathRecursively(destination)) {
    LOG(ERROR) << "Failed to delete the old vault.";
    return false;
  }

  return true;
}

bool Mount::PerformMigration(const MigrationHelper::ProgressCallback& callback,
                             std::unique_ptr<MigrationHelper> migrator) {
  {  // Abort if already cancelled.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    if (is_dircrypto_migration_cancelled_)
      return false;
    CHECK(!active_dircrypto_migrator_);
    active_dircrypto_migrator_ = migrator.get();
  }
  bool success = migrator->Migrate(callback);

  UnmountCryptohomeFromMigration();
  {  // Signal the waiting thread.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    active_dircrypto_migrator_ = nullptr;
    dircrypto_migration_stopped_condition_.Signal();
  }
  if (!success) {
    LOG(ERROR) << "Failed to migrate.";
    return false;
  }
  return true;
}

bool Mount::ResetApplicationContainer(const std::string& application) {
  if (!user_cryptohome_vault_) {
    LOG(ERROR) << "No active vault containers to reset.";
    return false;
  }

  return user_cryptohome_vault_->ResetApplicationContainer(application);
}

void Mount::MaybeCancelMigrateEncryptionAndWait() {
  base::AutoLock lock(active_dircrypto_migrator_lock_);
  is_dircrypto_migration_cancelled_ = true;
  while (active_dircrypto_migrator_) {
    active_dircrypto_migrator_->Cancel();
    LOG(INFO) << "Waiting for dircrypto migration to stop.";
    dircrypto_migration_stopped_condition_.Wait();
    LOG(INFO) << "Dircrypto migration stopped.";
  }
}

}  // namespace cryptohome
