// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount-info.h"

#include <base/file_util.h>
#include <base/stringprintf.h>
#include <gtest/gtest.h>

using std::string;
using std::vector;

namespace cros_disks {

class MountInfoTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    string content =
      "14 12 0:3 / /proc rw,noexec - proc none rw\n"
      "15 12 0:12 / /sys rw,noexec - sysfs none rw\n"
      "16 12 0:13 / /tmp rw,nodev - tmpfs /tmp rw\n"
      "18 13 0:14 / /dev/shm rw,noexec - tmpfs shmfs rw\n"
      "21 12 8:1 /var /var rw,noexec - ext3 /dev/sda1 rw\n"
      "22 12 8:1 /home /home rw,nodev - ext3 /dev/sda1 rw\n"
      "30 26 11:0 / /media/Test\\0401 ro - vfat /dev/sdb1 ro\n";

    FilePath mount_file;
    ASSERT_TRUE(file_util::CreateTemporaryFile(&mount_file));
    ASSERT_EQ(content.size(),
        file_util::WriteFile(mount_file, content.c_str(), content.size()));
    mount_file_ = mount_file.value();
  }

  virtual void TearDown() {
    ASSERT_TRUE(file_util::Delete(FilePath(mount_file_), false));
  }

 protected:
  string mount_file_;
  MountInfo manager_;
};

TEST_F(MountInfoTest, ConvertOctalStringToInt) {
  EXPECT_EQ(-1, manager_.ConvertOctalStringToInt(""));
  EXPECT_EQ(-1, manager_.ConvertOctalStringToInt("0"));
  EXPECT_EQ(-1, manager_.ConvertOctalStringToInt("00"));
  EXPECT_EQ(-1, manager_.ConvertOctalStringToInt("0000"));
  EXPECT_EQ(-1, manager_.ConvertOctalStringToInt("800"));
  EXPECT_EQ(-1, manager_.ConvertOctalStringToInt("080"));
  EXPECT_EQ(-1, manager_.ConvertOctalStringToInt("008"));

  for (int decimal = 0; decimal < 256; ++decimal) {
    string octal = base::StringPrintf("%03o", decimal);
    EXPECT_EQ(decimal, manager_.ConvertOctalStringToInt(octal));
  }
}

TEST_F(MountInfoTest, DecodePath) {
  EXPECT_EQ("Test", manager_.DecodePath("Test"));
  EXPECT_EQ("Test ", manager_.DecodePath("Test\\040"));
  EXPECT_EQ("Test 1", manager_.DecodePath("Test\\0401"));
  EXPECT_EQ(" Test 1", manager_.DecodePath("\\040Test\\0401"));
  EXPECT_EQ("Test\\040", manager_.DecodePath("Test\\134040"));
  EXPECT_EQ("Test\\04", manager_.DecodePath("Test\\04"));
  EXPECT_EQ("Test\\800", manager_.DecodePath("Test\\800"));
}

TEST_F(MountInfoTest, GetMountPaths) {
  EXPECT_TRUE(manager_.RetrieveFromFile(mount_file_));

  vector<string> expected_paths;
  expected_paths.push_back("/var");
  expected_paths.push_back("/home");
  EXPECT_TRUE(expected_paths == manager_.GetMountPaths("/dev/sda1"));

  expected_paths.clear();
  expected_paths.push_back("/media/Test 1");
  EXPECT_TRUE(expected_paths == manager_.GetMountPaths("/dev/sdb1"));

  expected_paths.clear();
  EXPECT_TRUE(expected_paths == manager_.GetMountPaths("/dev/sdc1"));
}

TEST_F(MountInfoTest, HasMountPath) {
  EXPECT_TRUE(manager_.RetrieveFromFile(mount_file_));
  EXPECT_FALSE(manager_.HasMountPath(""));
  EXPECT_FALSE(manager_.HasMountPath("/"));
  EXPECT_FALSE(manager_.HasMountPath("/dev/sda1"));
  EXPECT_FALSE(manager_.HasMountPath("/dev/sdb1"));
  EXPECT_FALSE(manager_.HasMountPath("/nonexistent-path"));
  EXPECT_TRUE(manager_.HasMountPath("/dev/shm"));
  EXPECT_TRUE(manager_.HasMountPath("/home"));
  EXPECT_TRUE(manager_.HasMountPath("/media/Test 1"));
  EXPECT_TRUE(manager_.HasMountPath("/proc"));
  EXPECT_TRUE(manager_.HasMountPath("/sys"));
  EXPECT_TRUE(manager_.HasMountPath("/tmp"));
  EXPECT_TRUE(manager_.HasMountPath("/var"));
}

TEST_F(MountInfoTest, RetrieveFromFile) {
  EXPECT_TRUE(manager_.RetrieveFromFile(mount_file_));
}

TEST_F(MountInfoTest, RetrieveFromCurrentProcess) {
  EXPECT_TRUE(manager_.RetrieveFromCurrentProcess());
  EXPECT_TRUE(manager_.HasMountPath("/proc"));
}

}  // namespace cros_disks
