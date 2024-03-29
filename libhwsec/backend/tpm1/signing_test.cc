// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"

using hwsec_foundation::error::testing::IsOk;
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
using tpm_manager::TpmManagerStatus;
namespace hwsec {

class BackendSigningTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendSigningTpm1Test, Sign) {
  const OperationPolicy kFakePolicy{};
  const brillo::Blob kFakeKeyBlob = brillo::BlobFromString("fake_key_blob");
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const uint32_t kFakeKeyHandle = 0x1337;
  const uint32_t kFakeHashHandle = 0x7331;
  const brillo::Blob kFakeData = brillo::BlobFromString("fake_data");
  const brillo::Blob kFakeSignature = brillo::BlobFromString("fake_signature");

  SetupSrk();

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Context_LoadKeyByBlob(kDefaultContext, kDefaultSrkHandle, _, _, _))
      .With(Args<3, 2>(ElementsAreArray(kFakeKeyBlob)))
      .WillOnce(DoAll(SetArgPointee<4>(kFakeKeyHandle), Return(TPM_SUCCESS)));

  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_GetPubKey(kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  auto key = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, kFakeKeyBlob);

  ASSERT_THAT(key, IsOk());

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_CreateObject(kDefaultContext, TSS_OBJECT_TYPE_HASH,
                                        TSS_HASH_OTHER, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakeHashHandle), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Hash_SetHashValue(kFakeHashHandle, _, _))
      .WillOnce(Return(TPM_SUCCESS));

  brillo::Blob signature = kFakeSignature;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Hash_Sign(kFakeHashHandle, kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(signature.size()),
                      SetArgPointee<3>(signature.data()), Return(TPM_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Signing::Sign>(
      kFakePolicy, key->GetKey(), kFakeData);

  ASSERT_THAT(result, IsOk());
  EXPECT_EQ(*result, signature);
}

TEST_F(BackendSigningTpm1Test, SignWithUnsupportedPolicy) {
  const OperationPolicy kFakePolicy{
      .permission =
          Permission{
              .auth_value = brillo::SecureBlob("auth"),
          },
  };
  const brillo::Blob kFakeKeyBlob = brillo::BlobFromString("fake_key_blob");
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const uint32_t kFakeKeyHandle = 0x1337;
  const brillo::Blob kFakeData = brillo::BlobFromString("fake_data");

  SetupSrk();

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Context_LoadKeyByBlob(kDefaultContext, kDefaultSrkHandle, _, _, _))
      .With(Args<3, 2>(ElementsAreArray(kFakeKeyBlob)))
      .WillOnce(DoAll(SetArgPointee<4>(kFakeKeyHandle), Return(TPM_SUCCESS)));

  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_GetPubKey(kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  auto key = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, kFakeKeyBlob);

  ASSERT_THAT(key, IsOk());

  auto result = middleware_->CallSync<&Backend::Signing::Sign>(
      kFakePolicy, key->GetKey(), kFakeData);

  EXPECT_FALSE(result.ok());
}

}  // namespace hwsec
