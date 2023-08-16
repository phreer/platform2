// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/encrypted.h"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::kAesGcmIVSize;
using ::hwsec_foundation::kAesGcmTagSize;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

// Converts the wrapped key block information from serializable structs into the
// constainer struct wrapped key block map.
decltype(EncryptedUss::Container::wrapped_key_blocks)
GetKeyBlocksFromSerializableStructs(
    const std::vector<UserSecretStashWrappedKeyBlock>& serializable_blocks) {
  decltype(EncryptedUss::Container::wrapped_key_blocks) key_blocks;

  for (const UserSecretStashWrappedKeyBlock& serializable_block :
       serializable_blocks) {
    if (serializable_block.wrapping_id.empty()) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with an empty ID.";
      continue;
    }
    if (key_blocks.count(serializable_block.wrapping_id)) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with duplicate ID "
          << serializable_block.wrapping_id << ".";
      continue;
    }

    if (!serializable_block.encryption_algorithm) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "unset algorithm";
      continue;
    }
    if (*serializable_block.encryption_algorithm !=
        UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "unknown algorithm: "
                   << static_cast<int>(
                          *serializable_block.encryption_algorithm);
      continue;
    }

    if (serializable_block.encrypted_key.empty()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "empty encrypted key.";
      continue;
    }

    if (serializable_block.iv.empty()) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with an empty IV.";
      continue;
    }

    if (serializable_block.gcm_tag.empty()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "empty AES-GCM tag.";
      continue;
    }

    EncryptedUss::WrappedKeyBlock key_block = {
        .encryption_algorithm = *serializable_block.encryption_algorithm,
        .encrypted_key = serializable_block.encrypted_key,
        .iv = serializable_block.iv,
        .gcm_tag = serializable_block.gcm_tag,
    };
    key_blocks.emplace(serializable_block.wrapping_id, std::move(key_block));
  }

  return key_blocks;
}

}  // namespace

CryptohomeStatusOr<EncryptedUss::Container> EncryptedUss::Container::FromBlob(
    const brillo::Blob& flatbuffer) {
  Container container;

  // This check is redundant to the flatbuffer parsing below, but we check it
  // here in order to distinguish "empty file" from "corrupted file" in metrics
  // and logs.
  if (flatbuffer.empty()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSEmptySerializedInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDeleteVault, PossibleAction::kAuth,
                        PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  std::optional<UserSecretStashContainer> deserialized =
      UserSecretStashContainer::Deserialize(flatbuffer);
  if (!deserialized) {
    LOG(ERROR) << "Failed to deserialize UserSecretStashContainer";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSDeserializeFailedInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  if (!deserialized->encryption_algorithm) {
    LOG(ERROR) << "UserSecretStashContainer has no algorithm set";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoAlgInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (*deserialized->encryption_algorithm !=
      UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
    LOG(ERROR) << "UserSecretStashContainer uses unknown algorithm: "
               << static_cast<int>(*deserialized->encryption_algorithm);
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSUnknownAlgInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  if (deserialized->ciphertext.empty()) {
    LOG(ERROR) << "UserSecretStash has empty ciphertext";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoCiphertextInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  container.ciphertext = deserialized->ciphertext;

  if (deserialized->iv.empty()) {
    LOG(ERROR) << "UserSecretStash has empty IV";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoIVInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (deserialized->iv.size() != kAesGcmIVSize) {
    LOG(ERROR) << "UserSecretStash has IV of wrong length: "
               << deserialized->iv.size() << ", expected: " << kAesGcmIVSize;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSIVWrongSizeInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  container.iv = deserialized->iv;

  if (deserialized->gcm_tag.empty()) {
    LOG(ERROR) << "UserSecretStash has empty AES-GCM tag";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSNoGCMTagInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (deserialized->gcm_tag.size() != kAesGcmTagSize) {
    LOG(ERROR) << "UserSecretStash has AES-GCM tag of wrong length: "
               << deserialized->gcm_tag.size()
               << ", expected: " << kAesGcmTagSize;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSTagWrongSizeInGetContainerFromFB),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  container.gcm_tag = deserialized->gcm_tag;

  container.wrapped_key_blocks =
      GetKeyBlocksFromSerializableStructs(deserialized->wrapped_key_blocks);

  container.created_on_os_version = deserialized->created_on_os_version;

  container.user_metadata = deserialized->user_metadata;

  return std::move(container);
}

CryptohomeStatusOr<EncryptedUss> EncryptedUss::FromBlob(
    const brillo::Blob& flatbuffer) {
  ASSIGN_OR_RETURN(Container container, Container::FromBlob(flatbuffer));
  return EncryptedUss(std::move(container));
}

CryptohomeStatusOr<EncryptedUss> EncryptedUss::FromStorage(
    const ObfuscatedUsername& username, const UssStorage& storage) {
  ASSIGN_OR_RETURN(brillo::Blob flatbuffer, storage.LoadPersisted(username));
  return FromBlob(flatbuffer);
}

EncryptedUss::EncryptedUss(Container container)
    : container_(std::move(container)) {}

std::set<std::string_view> EncryptedUss::WrappedMainKeyIds() const {
  std::set<std::string_view> ids;
  for (const auto& [wrapping_id, unused] : container_.wrapped_key_blocks) {
    ids.insert(wrapping_id);
  }
  return ids;
}

}  // namespace cryptohome