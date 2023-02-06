// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_AUTH_BLOCK_SERVICE_H_
#define CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_AUTH_BLOCK_SERVICE_H_

#include <memory>
#include <optional>
#include <string>

#include <base/callback.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_blocks/biometrics_command_processor.h"
#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

// BiometricsAuthBlockService is in charge of managing biometrics sessions
// and handling biometrics commands.
class BiometricsAuthBlockService {
 public:
  using OperationInput = BiometricsCommandProcessor::OperationInput;
  using OperationOutput = BiometricsCommandProcessor::OperationOutput;
  using OperationCallback = BiometricsCommandProcessor::OperationCallback;

  BiometricsAuthBlockService(
      std::unique_ptr<BiometricsCommandProcessor> processor,
      base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress)>
          enroll_signal_sender,
      base::RepeatingCallback<void(user_data_auth::AuthScanDone)>
          auth_signal_sender);
  BiometricsAuthBlockService(const BiometricsAuthBlockService&) = delete;
  BiometricsAuthBlockService& operator=(const BiometricsAuthBlockService&) =
      delete;
  ~BiometricsAuthBlockService() = default;

  // StartEnrollSession initiates a biometrics enrollment session. If
  // successful, enroll_signal_sender will be triggered with upcoming enrollment
  // progress signals.
  void StartEnrollSession(AuthFactorType auth_factor_type,
                          std::string obfuscated_username,
                          PreparedAuthFactorToken::Consumer on_done);

  // CreateCredential returns the necessary data for cryptohome to
  // create an AuthFactor for the newly created biometrics credential.
  void CreateCredential(OperationInput payload, OperationCallback on_done);

  // EndEnrollSession ends the biometrics enrollment session.
  void EndEnrollSession();

  // StartAuthenticateSession initiates a biometrics authentication session. If
  // successful, auth_signal_sender_ will be triggered with upcoming
  // authentication scan signals.
  void StartAuthenticateSession(AuthFactorType auth_factor_type,
                                std::string obfuscated_username,
                                PreparedAuthFactorToken::Consumer on_done);

  // MatchCredential returns the necessary data for cryptohome to
  // authenticate the AuthFactor for the matched biometrics credential.
  void MatchCredential(OperationInput payload, OperationCallback on_done);

  // EndAuthenticateSession ends the biometrics authentication session.
  void EndAuthenticateSession();

  // TakeNonce retrieves the nonce from the latest-completed enrollment or auth
  // scan event. The nonce is needed for the caller to interact with PinWeaver
  // and construct the OperationInput. The nonce is erased after calling this
  // function such that a nonce is never retrieved twice.
  std::optional<brillo::Blob> TakeNonce();

 private:
  class Token : public PreparedAuthFactorToken {
   public:
    enum class TokenType {
      kEnroll,
      kAuthenticate,
    };

    Token(AuthFactorType auth_factor_type,
          TokenType token_type,
          std::string user_id);

    // Attaches the token to the underlying service. Ideally we'd do this in the
    // constructor but the token is constructed when we initiate the request to
    // start the session, not after the session is (successfully) started. We
    // don't want the token to be able to terminate the session until it
    // starts, so we wait until that point to attach it.
    void AttachToService(BiometricsAuthBlockService* service);

    TokenType type() const { return token_type_; }

    std::string user_id() const { return user_id_; }

   private:
    CryptohomeStatus TerminateAuthFactor() override;

    TokenType token_type_;
    std::string user_id_;
    BiometricsAuthBlockService* service_ = nullptr;
    TerminateOnDestruction terminate_;
  };

  std::unique_ptr<BiometricsCommandProcessor> processor_;
  // The most recent auth nonce received.
  std::optional<brillo::Blob> auth_nonce_;
  // A callback to send cryptohome AuthEnrollmentProgress signal.
  base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress)>
      enroll_signal_sender_;
  // A callback to send cryptohome AuthScanDone signal.
  base::RepeatingCallback<void(user_data_auth::AuthScanDone)>
      auth_signal_sender_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_AUTH_BLOCK_SERVICE_H_
