// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_utility.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::error::testing::IsOkAndHolds;
using hwsec_foundation::error::testing::NotOk;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using tpm_manager::NvramResult;
using tpm_manager::NvramSpaceAttribute;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_SUCCESS;

namespace hwsec {

namespace {

std::string GenerateFakeQuotedData(const std::string& fake_nv_data) {
  trunks::TPMS_ATTEST fake_attestation_data;
  fake_attestation_data.qualified_signer.size = trunks::UINT16(0);
  fake_attestation_data.extra_data.size = trunks::UINT16(0);
  fake_attestation_data.type = trunks::TPM_ST_ATTEST_NV;
  fake_attestation_data.attested.nv.index_name.size = 0;
  fake_attestation_data.attested.nv.nv_contents.size = fake_nv_data.size();
  memcpy(fake_attestation_data.attested.nv.nv_contents.buffer,
         fake_nv_data.data(), fake_nv_data.size());
  std::string fake_quoted_data;
  trunks::Serialize_TPMS_ATTEST(fake_attestation_data, &fake_quoted_data);

  return fake_quoted_data;
}

}  // namespace

using BackendRoDataTpm2Test = BackendTpm2TestBase;

TEST_F(BackendRoDataTpm2Test, IsReady) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(315);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(false);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PERSISTENT_WRITE_LOCK);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_READ_AUTHORIZATION);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm2().IsReady(RoSpace::kG2fCert),
              IsOkAndHolds(true));
}

TEST_F(BackendRoDataTpm2Test, IsReadyNotAvailable) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(315);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(false);
  // Missing required attributes.
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm2().IsReady(RoSpace::kG2fCert),
              IsOkAndHolds(false));
}

TEST_F(BackendRoDataTpm2Test, IsReadySpaceNotExist) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SPACE_DOES_NOT_EXIST);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm2().IsReady(RoSpace::kG2fCert),
              IsOkAndHolds(false));
}

TEST_F(BackendRoDataTpm2Test, IsReadyOtherError) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_DEVICE_ERROR);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm2().IsReady(RoSpace::kG2fCert), NotOk());
}

TEST_F(BackendRoDataTpm2Test, Read) {
  const std::string kFakeData = "fake_data";

  tpm_manager::ReadSpaceReply read_reply;
  read_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  read_reply.set_data(kFakeData);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ReadSpace(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(read_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm2().Read(RoSpace::kG2fCert),
              IsOkAndHolds(brillo::BlobFromString(kFakeData)));
}

TEST_F(BackendRoDataTpm2Test, Certify) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kFakeKeyName = "fake_key_name";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
  };
  const RoSpace kFakeSpace = RoSpace::kBoardId;
  const std::string kFakeQuotedData =
      GenerateFakeQuotedData("fake_quoted_data");
  const trunks::TPM2B_ATTEST kFakeQuotedStruct =
      trunks::Make_TPM2B_ATTEST(kFakeQuotedData);
  const trunks::TPMT_SIGNATURE kFakeSignature = {
      .sig_alg = trunks::TPM_ALG_RSASSA,
      .signature.rsassa.sig = trunks::Make_TPM2B_PUBLIC_KEY_RSA("fake_quote"),
  };

  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(12);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(true);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PERSISTENT_WRITE_LOCK);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_READ_AUTHORIZATION);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(kFakeKeyHandle), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePublic), Return(TPM_RC_SUCCESS)));

  auto load_key_result = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();
  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeKeyName), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpm(),
              NV_CertifySyncShort(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<7>(kFakeQuotedStruct),
                      SetArgPointee<8>(kFakeSignature),
                      Return(TPM_RC_SUCCESS)));

  auto result =
      backend_->GetRoDataTpm2().Certify(kFakeSpace, fake_key.GetKey());
  ASSERT_OK(result);
  ASSERT_TRUE(result->has_quoted_data());
  EXPECT_EQ(result->quoted_data(), kFakeQuotedData);
  ASSERT_TRUE(result->has_quote());
  EXPECT_NE(result->quote().find("fake_quote"), std::string::npos);
}

}  // namespace hwsec
