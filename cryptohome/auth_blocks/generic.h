// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_GENERIC_H_
#define CRYPTOHOME_AUTH_BLOCKS_GENERIC_H_

#include <optional>
#include <tuple>

#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/auth_blocks/async_challenge_credential_auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"
#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"
#include "cryptohome/auth_blocks/fingerprint_auth_block.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

namespace cryptohome {

// To be supported by this generic API, an AuthBlock class must implement a
// specific static API. This is the GenericAuthBlock concept.
//
// TODO(b/272098290): Make this an actual concept when C++20 is available.
// The generic auth block type must:
//   - Have a static constexpr member kType of AuthBlockType
//   - Have a StateType alias specifying the AuthBlockState type
//   - Have a static function IsSupported() that returns CryptoStatus

// Provide a collection of functions that delegates the actual operations to the
// appropriate auth block implementation, based on an AuthBlockType parameter.
//
// These operations are generally implemented by going through the list of all
// AuthBlock classes, finding one with a matching kType and then calling the
// static operation for that type. Thus for each public function there are
// usually two private functions, one handling the generic case and one handling
// the base case when no such class is found.
//
// The generic function option does not hold any internal state of its own but
// it does have pointers to all the standard "global" interfaces that the
// various AuthBlock static functions take as parameters.
class GenericAuthBlockFunctions {
 private:
  // This special template type is a way for us to "pass" a set of types to a
  // function. The actual type itself is empty and doesn't have any particular
  // value besides having a parameter pack of types attached to it.
  template <typename... Types>
  struct TypeContainer {};

  // A type container with all of the auth block types that support generic
  // functions. Usually used as the initial TypeContainer parameter for all of
  // the variadic functions.
  using AllBlockTypes = TypeContainer<PinWeaverAuthBlock,
                                      AsyncChallengeCredentialAuthBlock,
                                      DoubleWrappedCompatAuthBlock,
                                      TpmBoundToPcrAuthBlock,
                                      TpmNotBoundToPcrAuthBlock,
                                      ScryptAuthBlock,
                                      CryptohomeRecoveryAuthBlock,
                                      TpmEccAuthBlock,
                                      FingerprintAuthBlock>;

  // Call the given function pointer with all of the parameters in |parameters_|
  // but stripped down to the subset of parameters it accepts.
  template <typename Ret, typename... Args>
  Ret CallWithParameters(Ret (*func)(Args...)) {
    return std::apply(func, std::tie(std::get<Args>(parameters_)...));
  }

  // Generic thunk from generic to type-specific IsSupported.
  template <typename T, typename... Rest>
  CryptoStatus IsSupportedImpl(AuthBlockType auth_block_type,
                               TypeContainer<T, Rest...>) {
    if (T::kType == auth_block_type) {
      return CallWithParameters(&T::IsSupported);
    }
    return IsSupportedImpl(auth_block_type, TypeContainer<Rest...>());
  }
  CryptoStatus IsSupportedImpl(AuthBlockType auth_block_type, TypeContainer<>) {
    return hwsec_foundation::status::MakeStatus<error::CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocGenericAuthBlockIsSupportedNotFound),
        error::ErrorActionSet({error::ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Underlying implementation of GetAuthBlockTypeFromState. Looks for an auth
  // block implementation whose StateType matches the state stored in the auth
  // block state variant.
  template <typename T, typename... Rest>
  std::optional<AuthBlockType> GetAuthBlockTypeFromStateImpl(
      const AuthBlockState& auth_block_state, TypeContainer<T, Rest...>) {
    if (std::holds_alternative<typename T::StateType>(auth_block_state.state)) {
      return T::kType;
    }
    return GetAuthBlockTypeFromStateImpl(auth_block_state,
                                         TypeContainer<Rest...>());
  }
  std::optional<AuthBlockType> GetAuthBlockTypeFromStateImpl(
      const AuthBlockState& auth_block_state, TypeContainer<>) {
    return std::nullopt;
  }

 public:
  GenericAuthBlockFunctions(
      Crypto* crypto,
      base::RepeatingCallback<BiometricsAuthBlockService*()> bio_service_getter)
      : crypto_(crypto),
        bio_service_getter_(bio_service_getter),
        parameters_(std::tie(*crypto_, bio_service_getter_)) {}

  // Returns success if this auth block type is supported on the current
  // hardware and software environment.
  CryptoStatus IsSupported(AuthBlockType auth_block_type) {
    return IsSupportedImpl(auth_block_type, AllBlockTypes());
  }

  // Generic implementation of AuthBlockUtility::GetAuthBlockTypeFromState.
  std::optional<AuthBlockType> GetAuthBlockTypeFromState(
      const AuthBlockState& auth_block_state) {
    return GetAuthBlockTypeFromStateImpl(auth_block_state, AllBlockTypes());
  }

 private:
  // Global interfaces used as parameters by the various auth block functions.
  Crypto* crypto_;
  base::RepeatingCallback<BiometricsAuthBlockService*()> bio_service_getter_;

  // References to all of the parameters values that we support, stored as a
  // tuple. We keep this in a tuple because it makes it much simpler to
  // automatically reduce to the subset of arguments that the underlying
  // AuthBlock functions require.
  std::tuple<Crypto&, base::RepeatingCallback<BiometricsAuthBlockService*()>&>
      parameters_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_GENERIC_H_