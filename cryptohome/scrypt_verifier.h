// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SCRYPT_VERIFIER_H_
#define CRYPTOHOME_SCRYPT_VERIFIER_H_

#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credential_verifier.h"

namespace cryptohome {

class ScryptVerifier final : public CredentialVerifier {
 public:
  explicit ScryptVerifier(const std::string& auth_factor_label);
  ~ScryptVerifier() override = default;

  // Prohibit copy/move/assignment.
  ScryptVerifier(const ScryptVerifier&) = delete;
  ScryptVerifier(const ScryptVerifier&&) = delete;
  ScryptVerifier& operator=(const ScryptVerifier&) = delete;
  ScryptVerifier& operator=(const ScryptVerifier&&) = delete;

  bool Set(const brillo::SecureBlob& secret) override;
  bool Verify(const brillo::SecureBlob& secret) override;

 private:
  brillo::SecureBlob scrypt_salt_;
  brillo::SecureBlob verifier_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SCRYPT_VERIFIER_H_
