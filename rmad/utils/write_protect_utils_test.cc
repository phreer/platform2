// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/write_protect_utils_impl.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/utils/mock_crossystem_utils.h"
#include "rmad/utils/mock_ec_utils.h"
#include "rmad/utils/mock_flashrom_utils.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

class WriteProtectUtilsTest : public testing::Test {
 public:
  WriteProtectUtilsTest() = default;
  ~WriteProtectUtilsTest() override = default;

  std::unique_ptr<WriteProtectUtilsImpl> CreateWriteProtectUtils(
      bool hwwp_success,
      bool hwwp_enabled,
      bool apwp_success,
      bool apwp_enabled,
      bool ecwp_success,
      bool ecwp_enabled) {
    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();
    ON_CALL(*mock_crossystem_utils,
            GetInt(Eq(CrosSystemUtils::kHwwpStatusProperty), _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(hwwp_enabled), Return(hwwp_success)));

    // Mock |EcUtils|.
    auto mock_ec_utils = std::make_unique<NiceMock<MockEcUtils>>();

    // Mock |FlashromUtils|.
    auto mock_flashrom_utils = std::make_unique<NiceMock<MockFlashromUtils>>();
    ON_CALL(*mock_flashrom_utils, GetApWriteProtectionStatus(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(apwp_enabled), Return(apwp_success)));
    ON_CALL(*mock_flashrom_utils, GetEcWriteProtectionStatus(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(ecwp_enabled), Return(ecwp_success)));

    return std::make_unique<WriteProtectUtilsImpl>(
        std::move(mock_crossystem_utils), std::move(mock_ec_utils),
        std::move(mock_flashrom_utils));
  }
};

TEST_F(WriteProtectUtilsTest, GetHwwp_Enabled_Success) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ true, /*hwwp_enabled*/ true,
                              /*apwp_success*/ true, /*apwp_enabled*/ true,
                              /*ecwp_success*/ true, /*ecwp_enabled*/ true);
  bool wp_status;
  ASSERT_TRUE(utils->GetHardwareWriteProtectionStatus(&wp_status));
  ASSERT_TRUE(wp_status);
}

TEST_F(WriteProtectUtilsTest, GetHwwp_Disabled_Success) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ true, /*hwwp_enabled*/ false,
                              /*apwp_success*/ true, /*apwp_enabled*/ true,
                              /*ecwp_success*/ true, /*ecwp_enabled*/ true);
  bool wp_status;
  ASSERT_TRUE(utils->GetHardwareWriteProtectionStatus(&wp_status));
  ASSERT_FALSE(wp_status);
}

TEST_F(WriteProtectUtilsTest, GetHwwp_Fail) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ false, /*hwwp_enabled*/ false,
                              /*apwp_success*/ true, /*apwp_enabled*/ true,
                              /*ecwp_success*/ true, /*ecwp_enabled*/ true);
  bool wp_status;
  ASSERT_FALSE(utils->GetHardwareWriteProtectionStatus(&wp_status));
}

TEST_F(WriteProtectUtilsTest, GetApwp_Enabled_Success) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ true, /*hwwp_enabled*/ true,
                              /*apwp_success*/ true, /*apwp_enabled*/ true,
                              /*ecwp_success*/ true, /*ecwp_enabled*/ true);
  bool wp_status;
  ASSERT_TRUE(utils->GetApWriteProtectionStatus(&wp_status));
  ASSERT_TRUE(wp_status);
}

TEST_F(WriteProtectUtilsTest, GetApwp_Disabled_Success) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ true, /*hwwp_enabled*/ true,
                              /*apwp_success*/ true, /*apwp_enabled*/ false,
                              /*ecwp_success*/ true, /*ecwp_enabled*/ true);
  bool wp_status;
  ASSERT_TRUE(utils->GetApWriteProtectionStatus(&wp_status));
  ASSERT_FALSE(wp_status);
}

TEST_F(WriteProtectUtilsTest, GetApwp_Fail) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ true, /*hwwp_enabled*/ true,
                              /*apwp_success*/ false, /*apwp_enabled*/ true,
                              /*ecwp_success*/ true, /*ecwp_enabled*/ true);
  bool wp_status;
  ASSERT_FALSE(utils->GetApWriteProtectionStatus(&wp_status));
}

TEST_F(WriteProtectUtilsTest, GetEcwp_Enabled_Success) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ true, /*hwwp_enabled*/ true,
                              /*apwp_success*/ true, /*apwp_enabled*/ true,
                              /*ecwp_success*/ true, /*ecwp_enabled*/ true);
  bool wp_status;
  ASSERT_TRUE(utils->GetEcWriteProtectionStatus(&wp_status));
  ASSERT_TRUE(wp_status);
}

TEST_F(WriteProtectUtilsTest, GetEcwp_Disabled_Success) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ true, /*hwwp_enabled*/ true,
                              /*apwp_success*/ true, /*apwp_enabled*/ true,
                              /*ecwp_success*/ true, /*ecwp_enabled*/ false);
  bool wp_status;
  ASSERT_TRUE(utils->GetEcWriteProtectionStatus(&wp_status));
  ASSERT_FALSE(wp_status);
}

TEST_F(WriteProtectUtilsTest, GetEcwp_Fail) {
  auto utils =
      CreateWriteProtectUtils(/*hwwp_success*/ true, /*hwwp_enabled*/ true,
                              /*apwp_success*/ true, /*apwp_enabled*/ true,
                              /*ecwp_success*/ false, /*ecwp_enabled*/ true);
  bool wp_status;
  ASSERT_FALSE(utils->GetEcWriteProtectionStatus(&wp_status));
}

}  // namespace rmad