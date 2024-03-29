// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/sealing.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/callback_helpers.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/tpm_utility.h>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

StatusOr<bool> SealingTpm2::IsSupported() {
  return true;
}

StatusOr<brillo::Blob> SealingTpm2::Seal(
    const OperationPolicySetting& policy,
    const brillo::SecureBlob& unsealed_data) {
  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::string policy_digest;
  bool use_only_policy_authorization = false;

  ASSIGN_OR_RETURN(
      const ConfigTpm2::PcrMap& settings,
      backend_.GetConfigTpm2().ToSettingsPcrMap(policy.device_config_settings),
      _.WithStatus<TPMError>("Failed to convert setting to PCR map"));

  if (!policy.permission.auth_value.has_value() && settings.empty()) {
    return MakeStatus<TPMError>("Seal without any useful policy",
                                TPMRetryAction::kNoRetry);
  }

  if (settings.size()) {
    RETURN_IF_ERROR(
        MakeStatus<TPM2Error>(context.tpm_utility->GetPolicyDigestForPcrValues(
            settings, policy.permission.auth_value.has_value(),
            &policy_digest)))
        .WithStatus<TPMError>("Failed to get policy digest");

    // We should not allow using the key without policy when the policy had been
    // set.
    use_only_policy_authorization = true;
  }

  std::unique_ptr<trunks::HmacSession> session =
      context.factory.GetHmacSession();
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(session->StartUnboundSession(true, true)))
      .WithStatus<TPMError>("Failed to start hmac session");

  std::string auth_value;
  if (policy.permission.auth_value.has_value()) {
    auth_value = policy.permission.auth_value.value().to_string();
  }

  std::string plaintext = unsealed_data.to_string();

  // Cleanup the data from secure blob.
  base::ScopedClosureRunner cleanup_auth_value(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(auth_value)));
  base::ScopedClosureRunner cleanup_plaintext(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(plaintext)));

  std::string sealed_str;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->SealData(
          plaintext, policy_digest, auth_value, use_only_policy_authorization,
          session->GetDelegate(), &sealed_str)))
      .WithStatus<TPMError>("Failed to seal data to PCR with authorization");

  return BlobFromString(sealed_str);
}

StatusOr<std::optional<ScopedKey>> SealingTpm2::PreloadSealedData(
    const OperationPolicy& policy, const brillo::Blob& sealed_data) {
  ASSIGN_OR_RETURN(ScopedKey key,
                   backend_.GetKeyManagementTpm2().LoadKey(policy, sealed_data),
                   _.WithStatus<TPMError>("Failed to load sealed data"));
  return std::move(key);
}

StatusOr<brillo::SecureBlob> SealingTpm2::Unseal(
    const OperationPolicy& policy,
    const brillo::Blob& sealed_data,
    UnsealOptions options) {
  // Use unsalted session here, to unseal faster.
  ASSIGN_OR_RETURN(
      ConfigTpm2::TrunksSession session,
      backend_.GetConfigTpm2().GetTrunksSession(policy, false, false),
      _.WithStatus<TPMError>("Failed to get session for policy"));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::string unsealed_data;
  // Cleanup the unsealed_data.
  base::ScopedClosureRunner cleanup_unsealed_data(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(unsealed_data)));

  if (options.preload_data.has_value()) {
    ASSIGN_OR_RETURN(const KeyTpm2& key_data,
                     backend_.GetKeyManagementTpm2().GetKeyData(
                         options.preload_data.value()),
                     _.WithStatus<TPMError>("Failed to get preload data"));

    RETURN_IF_ERROR(
        MakeStatus<TPM2Error>(context.tpm_utility->UnsealDataWithHandle(
            key_data.key_handle, session.delegate, &unsealed_data)))
        .WithStatus<TPMError>("Failed to unseal data with handle");
  } else {
    RETURN_IF_ERROR(
        MakeStatus<TPM2Error>(context.tpm_utility->UnsealData(
            BlobToString(sealed_data), session.delegate, &unsealed_data)))
        .WithStatus<TPMError>("Error to unseal data");
  }

  brillo::SecureBlob result(unsealed_data.begin(), unsealed_data.end());
  return result;
}

}  // namespace hwsec
