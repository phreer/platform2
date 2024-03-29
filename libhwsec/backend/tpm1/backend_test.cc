// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"

using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::Args;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;

namespace hwsec {

class BackendTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendTpm1Test, GetScopedTssContext) {
  TSS_HCONTEXT kFakeContext = 0x5566;

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  auto result = backend_->GetScopedTssContext();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->value(), kFakeContext);
}

TEST_F(BackendTpm1Test, GetTssContext) {
  TSS_HCONTEXT kFakeContext = 0x1234;

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  auto result = backend_->GetTssContext();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeContext);

  // Run again to check the cache works correctly.
  result = backend_->GetTssContext();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeContext);
}

TEST_F(BackendTpm1Test, GetUserTpmHandle) {
  TSS_HCONTEXT kFakeContext = 0x1234;
  TSS_HTPM kFakeTpm = 0x5678;

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_GetTpmObject(kFakeContext, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeTpm), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  auto result = backend_->GetUserTpmHandle();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeTpm);

  // Run again to check the cache works correctly.
  result = backend_->GetUserTpmHandle();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeTpm);
}

TEST_F(BackendTpm1Test, GetDelegateTpmHandle) {
  TSS_HCONTEXT kFakeContext = 0x1234;
  TSS_HTPM kFakeTpm1 = 0x5678;
  TSS_HTPM kFakeTpm2 = 0x8765;
  TSS_HPOLICY kPolicy1 = 0x9901;
  TSS_HPOLICY kPolicy2 = 0x9902;

  std::string fake_delegate_blob = "fake_deleagte_blob";
  std::string fake_delegate_secret = "fake_deleagte_secret";

  tpm_manager::GetTpmStatusReply reply;
  *reply.mutable_local_data()->mutable_owner_delegate()->mutable_blob() =
      fake_delegate_blob;
  *reply.mutable_local_data()->mutable_owner_delegate()->mutable_secret() =
      fake_delegate_secret;
  EXPECT_CALL(proxy_->GetMock().tpm_manager, GetTpmStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_GetTpmObject(kFakeContext, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeTpm1), Return(TPM_SUCCESS)))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeTpm2), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_GetPolicyObject(kFakeTpm1, TSS_POLICY_USAGE, _))
      .WillOnce(DoAll(SetArgPointee<2>(kPolicy1), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_GetPolicyObject(kFakeTpm2, TSS_POLICY_USAGE, _))
      .WillOnce(DoAll(SetArgPointee<2>(kPolicy2), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Policy_SetSecret(kPolicy1, TSS_SECRET_MODE_PLAIN, _, _))
      .With(Args<3, 2>(ElementsAreArray(fake_delegate_secret)))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Policy_SetSecret(kPolicy2, TSS_SECRET_MODE_PLAIN, _, _))
      .With(Args<3, 2>(ElementsAreArray(fake_delegate_secret)))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_SetAttribData(kPolicy1, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                                 TSS_TSPATTRIB_POLDEL_OWNERBLOB, _, _))
      .With(Args<4, 3>(ElementsAreArray(fake_delegate_blob)))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_SetAttribData(kPolicy2, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                                 TSS_TSPATTRIB_POLDEL_OWNERBLOB, _, _))
      .With(Args<4, 3>(ElementsAreArray(fake_delegate_blob)))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_CloseObject(kFakeContext, kFakeTpm1))
      .WillOnce(Return(TPM_SUCCESS));
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_CloseObject(kFakeContext, kFakeTpm2))
      .WillOnce(Return(TPM_SUCCESS));
  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  auto result1 = backend_->GetDelegateTpmHandle();
  ASSERT_TRUE(result1.ok());
  EXPECT_EQ(result1->value(), kFakeTpm1);

  auto result2 = backend_->GetDelegateTpmHandle();
  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(result2->value(), kFakeTpm2);
}

}  // namespace hwsec
