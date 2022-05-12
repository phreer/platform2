// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash_storage.h"

#include <optional>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_platform.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using testing::_;
using testing::Return;

namespace cryptohome {

namespace {
constexpr char kUssContainer[] = "fake_uss_container";
constexpr char kObfuscatedUsername[] = "foo@gmail.com";
}  // namespace

class UserSecretStashStorageTest : public ::testing::Test {
 protected:
  MockPlatform platform_;
  UserSecretStashStorage uss_storage_{&platform_};
};

// Test the successful scenario of the USS persisting and loading.
TEST_F(UserSecretStashStorageTest, PersistThenLoad) {
  // Write the USS.
  EXPECT_TRUE(
      uss_storage_.Persist(BlobFromString(kUssContainer), kObfuscatedUsername)
          .ok());
  EXPECT_TRUE(platform_.FileExists(UserSecretStashPath(kObfuscatedUsername)));

  // Load the USS and check it didn't change.
  CryptohomeStatusOr<Blob> loaded_uss_container =
      uss_storage_.LoadPersisted(kObfuscatedUsername);
  ASSERT_TRUE(loaded_uss_container.ok());
  EXPECT_EQ(BlobToString(loaded_uss_container.value()), kUssContainer);
}

// Test that the persisting fails when the USS file writing fails.
TEST_F(UserSecretStashStorageTest, PersistFailure) {
  EXPECT_CALL(platform_, WriteFileAtomicDurable(
                             UserSecretStashPath(kObfuscatedUsername), _, _))
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(
      uss_storage_.Persist(BlobFromString(kUssContainer), kObfuscatedUsername)
          .ok());
}

// Test that the loading fails when the USS file doesn't exist.
TEST_F(UserSecretStashStorageTest, LoadFailureNonExisting) {
  EXPECT_FALSE(uss_storage_.LoadPersisted(kObfuscatedUsername).ok());
}

}  // namespace cryptohome
