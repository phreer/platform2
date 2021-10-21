// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for AuthSession.

#include "cryptohome/auth_session.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/task_environment.h>
#include <base/timer/mock_timer.h>
#include <brillo/cryptohome.h>
#include <gtest/gtest.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/mock_keyset_management.h"

using ::testing::_;
using ::testing::ByMove;
using ::testing::NiceMock;
using ::testing::Return;

namespace {
// Fake label to be in used in this test suite.
constexpr char kFakeLabel[] = "test_label";
// Fake password to be in used in this test suite.
constexpr char kFakePass[] = "test_pass";
// Fake username to be used in this test suite.
constexpr char kFakeUsername[] = "test_username";

}  // namespace

namespace cryptohome {

class AuthSessionTest : public ::testing::Test {
 public:
  AuthSessionTest() = default;
  AuthSessionTest(const AuthSessionTest&) = delete;
  AuthSessionTest& operator=(const AuthSessionTest&) = delete;
  ~AuthSessionTest() override = default;

  void SetUp() override {
    // Setup salt for brillo functions.
    brillo::SecureBlob fake_salt(CRYPTOHOME_DEFAULT_SALT_LENGTH, 'S');
    // Lifetime of this pointer is determined by this class.
    brillo_salt_ = std::make_unique<std::string>(
        reinterpret_cast<const char*>(fake_salt.data()), fake_salt.size());
    brillo::cryptohome::home::SetSystemSalt(brillo_salt_.get());
  }

  void TearDown() override {
    // tearing down salt set.
    brillo::cryptohome::home::SetSystemSalt(NULL);
  }

 protected:
  // Mock KeysetManagent object, will be passed to AuthSession for its internal
  // use.
  NiceMock<MockKeysetManagement> keyset_management_;
  std::unique_ptr<std::string> brillo_salt_;
};

TEST_F(AuthSessionTest, TimeoutTest) {
  base::test::SingleThreadTaskEnvironment task_environment;
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool* called, const base::UnguessableToken&) { *called = true; },
      base::Unretained(&called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &keyset_management_);
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  ASSERT_TRUE(auth_session.timer_.IsRunning());
  auth_session.timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_TRUE(called);
}

TEST_F(AuthSessionTest, SerializedStringFromNullToken) {
  base::UnguessableToken token = base::UnguessableToken::Null();
  base::Optional<std::string> serialized_token =
      AuthSession::GetSerializedStringFromToken(token);
  EXPECT_FALSE(serialized_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromEmptyString) {
  std::string serialized_string = "";
  base::Optional<base::UnguessableToken> unguessable_token =
      AuthSession::GetTokenFromSerializedString(serialized_string);
  EXPECT_FALSE(unguessable_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromUnexpectedSize) {
  std::string serialized_string = "unexpected_sized_string";
  base::Optional<base::UnguessableToken> unguessable_token =
      AuthSession::GetTokenFromSerializedString(serialized_string);
  EXPECT_FALSE(unguessable_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromString) {
  base::UnguessableToken original_token = base::UnguessableToken::Create();
  base::Optional<std::string> serialized_token =
      AuthSession::GetSerializedStringFromToken(original_token);
  EXPECT_TRUE(serialized_token.has_value());
  base::Optional<base::UnguessableToken> deserialized_token =
      AuthSession::GetTokenFromSerializedString(serialized_token.value());
  EXPECT_TRUE(deserialized_token.has_value());
  EXPECT_EQ(deserialized_token.value(), original_token);
}

// This test check AuthSession::GetCredential function for a regular user and
// ensures that the fields are set as they should be.
TEST_F(AuthSessionTest, GetCredentialRegularUser) {
  // SETUP
  base::test::SingleThreadTaskEnvironment task_environment;
  MountError error;
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool* called, const base::UnguessableToken&) { *called = true; },
      base::Unretained(&called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &keyset_management_);
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);

  // TEST
  ASSERT_TRUE(auth_session.timer_.IsRunning());
  auth_session.timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_TRUE(called);
  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);
  std::unique_ptr<Credentials> test_creds =
      auth_session.GetCredentials(authorization_request, &error);

  // VERIFY
  // DebugString is used in the absence of a comparator for KeyData protobuf.
  EXPECT_EQ(test_creds->key_data().DebugString(),
            authorization_request.mutable_key()->data().DebugString());
}

// This test check AuthSession::GetCredential function for a kiosk user and
// ensures that the fields are set as they should be.
TEST_F(AuthSessionTest, GetCredentialKioskUser) {
  // SETUP
  base::test::SingleThreadTaskEnvironment task_environment;
  MountError error;
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool* called, const base::UnguessableToken&) { *called = true; },
      base::Unretained(&called));
  // SecureBlob for kFakePass above
  const brillo::SecureBlob fake_pass_blob(
      brillo::BlobFromString(kFakeUsername));

  AuthSession auth_session(kFakeUsername, 0, std::move(on_timeout),
                           &keyset_management_);
  EXPECT_CALL(keyset_management_, GetPublicMountPassKey(_))
      .WillOnce(Return(ByMove(fake_pass_blob)));
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);

  // TEST
  ASSERT_TRUE(auth_session.timer_.IsRunning());
  auth_session.timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_TRUE(called);
  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);
  authorization_request.mutable_key()->mutable_data()->set_type(
      KeyData::KEY_TYPE_KIOSK);
  std::unique_ptr<Credentials> test_creds =
      auth_session.GetCredentials(authorization_request, &error);

  // VERIFY
  // DebugString is used in the absence of a comparator for KeyData protobuf.
  EXPECT_EQ(test_creds->key_data().DebugString(),
            authorization_request.mutable_key()->data().DebugString());
  EXPECT_EQ(test_creds->passkey(), fake_pass_blob);
}

// Test if AuthSession correctly adds new credentials for a new user.
TEST_F(AuthSessionTest, AddCredentialNewUser) {
  // Setup.
  base::test::SingleThreadTaskEnvironment task_environment;
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &keyset_management_);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_FALSE(auth_session.user_exists());
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(keyset_management_, AddInitialKeyset(_)).WillOnce(Return(true));

  // Verify.
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.AddCredentials(add_cred_request));
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
}

// Test if AuthSession correctly authenticates existing credentials for a
// user.
TEST_F(AuthSessionTest, AuthenticateExistingUser) {
  // Setup.
  base::test::SingleThreadTaskEnvironment task_environment;
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &keyset_management_);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);

  auto vk = std::make_unique<VaultKeyset>();
  EXPECT_CALL(keyset_management_, LoadUnwrappedKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));

  // Verify.
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.Authenticate(authorization_request));
  EXPECT_EQ(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_TRUE(auth_session.TakeCredentialVerifier()->Verify(
      brillo::SecureBlob(kFakePass)));

  // Cleanup.
  auth_session.timer_.FireNow();
  EXPECT_THAT(AuthStatus::kAuthStatusTimedOut, auth_session.GetStatus());
  EXPECT_TRUE(called);
}

}  // namespace cryptohome
