// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/cros_key.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <base/logging.h>
#include <base/notreached.h>
#include <keymaster/authorization_set.h>
#include <keymaster/keymaster_tags.h>

namespace arc::keymint::context {

CrosKeyFactory::CrosKeyFactory(base::WeakPtr<ContextAdaptor> context_adaptor,
                               keymaster_algorithm_t algorithm)
    : context_adaptor_(context_adaptor),
      sign_factory_(
          std::make_unique<CrosOperationFactory>(algorithm, KM_PURPOSE_SIGN)) {}

keymaster_error_t CrosKeyFactory::LoadKey(
    KeyData&& key_data,
    ::keymaster::AuthorizationSet&& hw_enforced,
    ::keymaster::AuthorizationSet&& sw_enforced,
    ::keymaster::UniquePtr<::keymaster::Key>* key) const {
  // TODO(b/274723555): Implement LoadKey function for KeyMint Context.
  return KM_ERROR_UNIMPLEMENTED;
}

keymaster_error_t CrosKeyFactory::LoadKey(
    ::keymaster::KeymasterKeyBlob&& key_material,
    const ::keymaster::AuthorizationSet& additional_params,
    ::keymaster::AuthorizationSet&& hw_enforced,
    ::keymaster::AuthorizationSet&& sw_enforced,
    ::keymaster::UniquePtr<::keymaster::Key>* key) const {
  NOTREACHED() << __func__ << " should never be called";
  return KM_ERROR_UNIMPLEMENTED;
}

::keymaster::OperationFactory* CrosKeyFactory::GetOperationFactory(
    keymaster_purpose_t purpose) const {
  // TODO(b/274723555): Implement this for KeyMint Context.
  return nullptr;
}

keymaster_error_t CrosKeyFactory::GenerateKey(
    const ::keymaster::AuthorizationSet& key_description,
    ::keymaster::UniquePtr<::keymaster::Key> attestation_signing_key,
    const ::keymaster::KeymasterBlob& issuer_subject,
    ::keymaster::KeymasterKeyBlob* key_blob,
    ::keymaster::AuthorizationSet* hw_enforced,
    ::keymaster::AuthorizationSet* sw_enforced,
    ::keymaster::CertificateChain* cert_chain) const {
  NOTREACHED() << __func__ << " should never be called";
  return KM_ERROR_UNIMPLEMENTED;
}

keymaster_error_t CrosKeyFactory::ImportKey(
    const ::keymaster::AuthorizationSet& key_description,
    keymaster_key_format_t input_key_material_format,
    const ::keymaster::KeymasterKeyBlob& input_key_material,
    ::keymaster::UniquePtr<::keymaster::Key> attestation_signing_key,
    const ::keymaster::KeymasterBlob& issuer_subject,
    ::keymaster::KeymasterKeyBlob* output_key_blob,
    ::keymaster::AuthorizationSet* hw_enforced,
    ::keymaster::AuthorizationSet* sw_enforced,
    ::keymaster::CertificateChain* cert_chain) const {
  NOTREACHED() << __func__ << " should never be called";
  return KM_ERROR_UNIMPLEMENTED;
}

const keymaster_key_format_t* CrosKeyFactory::SupportedImportFormats(
    size_t* format_count) const {
  NOTREACHED() << __func__ << " should never be called";
  *format_count = 0;
  return nullptr;
}

const keymaster_key_format_t* CrosKeyFactory::SupportedExportFormats(
    size_t* format_count) const {
  NOTREACHED() << __func__ << " should never be called";
  *format_count = 0;
  return nullptr;
}

CrosKey::CrosKey(::keymaster::AuthorizationSet&& hw_enforced,
                 ::keymaster::AuthorizationSet&& sw_enforced,
                 const CrosKeyFactory* key_factory,
                 KeyData&& key_data)
    : ::keymaster::Key(
          std::move(hw_enforced), std::move(sw_enforced), key_factory),
      key_data_(std::move(key_data)) {}

CrosKey::~CrosKey() = default;

ChapsKey::ChapsKey(::keymaster::AuthorizationSet&& hw_enforced,
                   ::keymaster::AuthorizationSet&& sw_enforced,
                   const CrosKeyFactory* key_factory,
                   KeyData&& key_data)
    : CrosKey(std::move(hw_enforced),
              std::move(sw_enforced),
              key_factory,
              std::move(key_data)) {}

ChapsKey::ChapsKey(ChapsKey&& chaps_key)
    : ChapsKey(chaps_key.hw_enforced_move(),
               chaps_key.sw_enforced_move(),
               chaps_key.cros_key_factory(),
               std::move(chaps_key.key_data_)) {}

ChapsKey::~ChapsKey() = default;

ChapsKey& ChapsKey::operator=(ChapsKey&& other) {
  hw_enforced_ = other.hw_enforced_move();
  sw_enforced_ = other.sw_enforced_move();
  key_factory_ = other.cros_key_factory();
  key_data_ = std::move(other.key_data_);
  return *this;
}

keymaster_error_t ChapsKey::formatted_key_material(
    keymaster_key_format_t format,
    ::keymaster::UniquePtr<uint8_t[]>* out_material,
    size_t* out_size) const {
  // TODO(b/274723555): Implement this function for KeyMint Context.
  return KM_ERROR_UNKNOWN_ERROR;
}

CrosOperationFactory::CrosOperationFactory(keymaster_algorithm_t algorithm,
                                           keymaster_purpose_t purpose)
    : algorithm_(algorithm), purpose_(purpose) {}

CrosOperationFactory::~CrosOperationFactory() = default;

::keymaster::OperationFactory::KeyType CrosOperationFactory::registry_key()
    const {
  return ::keymaster::OperationFactory::KeyType(algorithm_, purpose_);
}

::keymaster::OperationPtr CrosOperationFactory::CreateOperation(
    ::keymaster::Key&& key,
    const ::keymaster::AuthorizationSet& begin_params,
    keymaster_error_t* error) {
  // TODO(b/274723555) : Implement this for KeyMint Context.
  return nullptr;
}

}  // namespace arc::keymint::context
