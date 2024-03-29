// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_REAL_USER_SESSION_FACTORY_H_
#define CRYPTOHOME_USER_SESSION_REAL_USER_SESSION_FACTORY_H_

#include <memory>
#include <string>
#include <utility>

#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/pkcs11_token_factory.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_factory.h"
#include "cryptohome/user_session/real_user_session.h"
#include "cryptohome/user_session/user_session_factory.h"

namespace cryptohome {

class RealUserSessionFactory : public UserSessionFactory {
 public:
  RealUserSessionFactory(
      std::unique_ptr<MountFactory> mount_factory,
      Platform* platform,
      HomeDirs* homedirs,
      KeysetManagement* keyset_management,
      UserOldestActivityTimestampManager* user_activity_timestamp_manager,
      Pkcs11TokenFactory* pkcs11_token_factory)
      : UserSessionFactory(),
        mount_factory_(std::move(mount_factory)),
        platform_(platform),
        homedirs_(homedirs),
        keyset_management_(keyset_management),
        user_activity_timestamp_manager_(user_activity_timestamp_manager),
        pkcs11_token_factory_(pkcs11_token_factory) {}

  ~RealUserSessionFactory() override = default;

  std::unique_ptr<UserSession> New(const std::string& username,
                                   bool legacy_mount,
                                   bool bind_mount_downloads) override {
    return std::make_unique<RealUserSession>(
        username, homedirs_, keyset_management_,
        user_activity_timestamp_manager_, pkcs11_token_factory_,
        mount_factory_->New(platform_, homedirs_, legacy_mount,
                            bind_mount_downloads, /*use_local_mounter=*/false));
  }

 private:
  std::unique_ptr<MountFactory> mount_factory_;
  Platform* platform_;
  HomeDirs* homedirs_;
  KeysetManagement* keyset_management_;
  UserOldestActivityTimestampManager* user_activity_timestamp_manager_;
  Pkcs11TokenFactory* pkcs11_token_factory_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_REAL_USER_SESSION_FACTORY_H_
