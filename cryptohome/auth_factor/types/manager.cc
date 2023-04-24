// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/manager.h"

#include <memory>
#include <utility>

#include <base/check_op.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/cryptohome_recovery.h"
#include "cryptohome/auth_factor/types/fingerprint.h"
#include "cryptohome/auth_factor/types/kiosk.h"
#include "cryptohome/auth_factor/types/legacy_fingerprint.h"
#include "cryptohome/auth_factor/types/null.h"
#include "cryptohome/auth_factor/types/password.h"
#include "cryptohome/auth_factor/types/pin.h"
#include "cryptohome/auth_factor/types/smart_card.h"

namespace cryptohome {
namespace {

// Construct a new driver instance for the given type.
std::unique_ptr<AuthFactorDriver> CreateDriver(
    AuthFactorType auth_factor_type) {
  // This is written using a switch to force full enum coverage.
  switch (auth_factor_type) {
    case AuthFactorType::kPassword:
      return std::make_unique<PasswordAuthFactorDriver>();
    case AuthFactorType::kPin:
      return std::make_unique<PinAuthFactorDriver>();
    case AuthFactorType::kCryptohomeRecovery:
      return std::make_unique<CryptohomeRecoveryAuthFactorDriver>();
    case AuthFactorType::kKiosk:
      return std::make_unique<KioskAuthFactorDriver>();
    case AuthFactorType::kSmartCard:
      return std::make_unique<SmartCardAuthFactorDriver>();
    case AuthFactorType::kLegacyFingerprint:
      return std::make_unique<LegacyFingerprintAuthFactorDriver>();
    case AuthFactorType::kFingerprint:
      return std::make_unique<FingerprintAuthFactorDriver>();
    case AuthFactorType::kUnspecified:
      return nullptr;
  }
}

// Construct a map of drivers for all types.
std::unordered_map<AuthFactorType, std::unique_ptr<AuthFactorDriver>>
CreateDriverMap() {
  std::unordered_map<AuthFactorType, std::unique_ptr<AuthFactorDriver>>
      driver_map;
  for (AuthFactorType auth_factor_type : {
           AuthFactorType::kPassword,
           AuthFactorType::kPin,
           AuthFactorType::kCryptohomeRecovery,
           AuthFactorType::kKiosk,
           AuthFactorType::kSmartCard,
           AuthFactorType::kLegacyFingerprint,
           AuthFactorType::kFingerprint,
       }) {
    auto driver = CreateDriver(auth_factor_type);
    CHECK_NE(driver.get(), nullptr);
    driver_map[auth_factor_type] = std::move(driver);
  }
  return driver_map;
}

}  // namespace

AuthFactorDriverManager::AuthFactorDriverManager()
    : null_driver_(std::make_unique<NullAuthFactorDriver>()),
      driver_map_(CreateDriverMap()) {}

const AuthFactorDriver& AuthFactorDriverManager::GetDriver(
    AuthFactorType auth_factor_type) const {
  auto iter = driver_map_.find(auth_factor_type);
  if (iter != driver_map_.end()) {
    return *iter->second;
  }
  return *null_driver_;
}

}  // namespace cryptohome