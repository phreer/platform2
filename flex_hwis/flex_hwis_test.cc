// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"

#include <memory>
#include <optional>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <policy/mock_device_policy.h>

using ::testing::_;
using ::testing::AtMost;
using ::testing::Return;

namespace flex_hwis {

ACTION_P(SetEnabled, enabled) {
  *arg0 = enabled;
  return true;
}

ACTION_P(SetEnterpriseEnrolled, managed) {
  return managed;
}

class FlexHwisTest : public ::testing::Test {
 protected:
  void SetUp() override {
    constexpr char kUuid[] = "reven-uuid";

    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();

    // The default setting is for the device to be managed and
    // all device policies to be enabled.
    device_policy_ = new policy::MockDevicePolicy();
    EXPECT_CALL(*device_policy_, LoadPolicy(false))
        .Times(AtMost(1))
        .WillOnce(Return(true));
    EXPECT_CALL(*device_policy_, GetHwDataUsageEnabled(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportSystemInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportCpuInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportGraphicsStatus(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportMemoryInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportVersionInfo(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, GetReportNetworkConfig(_))
        .Times(AtMost(1))
        .WillOnce(SetEnabled(true));
    EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled())
        .Times(AtMost(1))
        .WillOnce(SetEnterpriseEnrolled(true));
    flex_hwis_sender_ = flex_hwis::FlexHwisSender(
        test_path_,
        std::make_unique<policy::PolicyProvider>(
            std::unique_ptr<policy::MockDevicePolicy>(device_policy_)));

    CreateUuid(kUuid);
  }

  void CreateTimeStamp(const std::string& timestamp) {
    base::FilePath time_path = test_path_.Append("var/lib/flex_hwis_tool");
    CHECK(base::CreateDirectory(time_path));
    CHECK(base::WriteFile(time_path.Append("time"), timestamp));
  }

  void CreateUuid(const std::string& uuid) {
    base::FilePath uuid_path = test_path_.Append("proc/sys/kernel/random");
    CHECK(base::CreateDirectory(uuid_path));
    CHECK(base::WriteFile(uuid_path.Append("uuid"), uuid));
  }

  std::optional<flex_hwis::FlexHwisSender> flex_hwis_sender_;
  policy::MockDevicePolicy* device_policy_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
};

TEST_F(FlexHwisTest, HasRunRecently) {
  CreateTimeStamp(base::TimeFormatHTTP(base::Time::Now()));
  EXPECT_EQ(flex_hwis_sender_->CollectAndSend(), Result::HasRunRecently);
}

TEST_F(FlexHwisTest, ManagedWithoutPermission) {
  EXPECT_CALL(*device_policy_, GetReportSystemInfo(_))
      .WillOnce(SetEnabled(false));
  EXPECT_EQ(flex_hwis_sender_->CollectAndSend(), Result::NotAuthorized);
}

TEST_F(FlexHwisTest, UnManagedWithoutPermission) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled())
      .WillOnce(SetEnterpriseEnrolled(false));
  EXPECT_CALL(*device_policy_, GetHwDataUsageEnabled(_))
      .WillOnce(SetEnabled(false));
  EXPECT_EQ(flex_hwis_sender_->CollectAndSend(), Result::NotAuthorized);
}

TEST_F(FlexHwisTest, ManagedWithPermission) {
  EXPECT_EQ(flex_hwis_sender_->CollectAndSend(), Result::Sent);
}

TEST_F(FlexHwisTest, UnManagedWithPermission) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled())
      .WillOnce(SetEnterpriseEnrolled(false));
  EXPECT_EQ(flex_hwis_sender_->CollectAndSend(), Result::Sent);
}

}  // namespace flex_hwis
