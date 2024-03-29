// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_point.h"

#include <limits>
#include <utility>

#include <base/test/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/mock_platform.h"
#include "cros-disks/platform.h"

namespace cros_disks {

namespace {

using testing::ElementsAre;
using testing::Return;
using testing::StrictMock;

constexpr char kMountPath[] = "/mount/path";
constexpr char kSource[] = "source";
constexpr char kFSType[] = "fstype";
constexpr char kOptions[] = "foo=bar";

class MountPointTest : public testing::Test {
 protected:
  StrictMock<MockPlatform> platform_;
  const MountPointData data_ = {.mount_path = base::FilePath(kMountPath),
                                .source = kSource,
                                .filesystem_type = kFSType,
                                .flags = MS_DIRSYNC | MS_NODEV,
                                .data = kOptions};
};

}  // namespace

TEST_F(MountPointTest, Unmount) {
  auto mount_point = std::make_unique<MountPoint>(data_, &platform_);

  EXPECT_CALL(platform_, Unmount(base::FilePath(kMountPath)))
      .WillOnce(Return(MOUNT_ERROR_INVALID_ARCHIVE));
  EXPECT_EQ(MOUNT_ERROR_INVALID_ARCHIVE, mount_point->Unmount());

  EXPECT_CALL(platform_, Unmount(base::FilePath(kMountPath)))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(kMountPath))
      .WillOnce(Return(true));
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Unmount());

  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Unmount());
}

TEST_F(MountPointTest, UnmountOnDestroy) {
  const std::unique_ptr<MountPoint> mount_point =
      std::make_unique<MountPoint>(data_, &platform_);
  EXPECT_TRUE(mount_point->is_mounted());

  EXPECT_CALL(platform_, Unmount).WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(kMountPath))
      .WillOnce(Return(false));
}

TEST_F(MountPointTest, UnmountError) {
  const std::unique_ptr<MountPoint> mount_point =
      std::make_unique<MountPoint>(data_, &platform_);
  EXPECT_TRUE(mount_point->is_mounted());

  EXPECT_CALL(platform_, Unmount(base::FilePath(kMountPath)))
      .WillOnce(Return(MOUNT_ERROR_PATH_NOT_MOUNTED));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(kMountPath))
      .WillOnce(Return(true));
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Unmount());

  EXPECT_FALSE(mount_point->is_mounted());
}

TEST_F(MountPointTest, Remount) {
  const std::unique_ptr<MountPoint> mount_point =
      std::make_unique<MountPoint>(data_, &platform_);
  EXPECT_TRUE(mount_point->is_mounted());
  EXPECT_FALSE(mount_point->is_read_only());

  EXPECT_CALL(platform_,
              Mount(kSource, kMountPath, kFSType,
                    MS_DIRSYNC | MS_NODEV | MS_RDONLY | MS_REMOUNT, kOptions))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Remount(true));
  EXPECT_TRUE(mount_point->is_read_only());

  EXPECT_CALL(platform_, Mount(kSource, kMountPath, kFSType,
                               MS_DIRSYNC | MS_NODEV | MS_REMOUNT, kOptions))
      .WillOnce(Return(MOUNT_ERROR_INTERNAL));
  EXPECT_EQ(MOUNT_ERROR_INTERNAL, mount_point->Remount(false));
  EXPECT_TRUE(mount_point->is_read_only());

  EXPECT_CALL(platform_, Unmount(base::FilePath(kMountPath)))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(kMountPath))
      .WillOnce(Return(true));
}

TEST_F(MountPointTest, RemountUnmounted) {
  const std::unique_ptr<MountPoint> mount_point =
      MountPoint::CreateUnmounted(data_);
  EXPECT_FALSE(mount_point->is_mounted());
  EXPECT_FALSE(mount_point->is_read_only());

  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Remount(true));
  EXPECT_FALSE(mount_point->is_read_only());
}

TEST_F(MountPointTest, MountError) {
  EXPECT_CALL(platform_, Mount(kSource, kMountPath, kFSType,
                               MS_DIRSYNC | MS_NODEV, kOptions))
      .WillOnce(Return(MOUNT_ERROR_INVALID_ARGUMENT));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  const std::unique_ptr<MountPoint> mount_point =
      MountPoint::Mount(data_, &platform_, &error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_INVALID_ARGUMENT, error);
}

TEST_F(MountPointTest, MountSucceeds) {
  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  EXPECT_CALL(platform_, Mount(kSource, kMountPath, kFSType,
                               MS_DIRSYNC | MS_NODEV, kOptions))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  const std::unique_ptr<MountPoint> mount_point =
      MountPoint::Mount(data_, &platform_, &error);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);
  EXPECT_TRUE(mount_point);
  EXPECT_TRUE(mount_point->is_mounted());
  EXPECT_EQ(data_.mount_path, mount_point->path());
  EXPECT_EQ(data_.source, mount_point->source());
  EXPECT_EQ(data_.filesystem_type, mount_point->fstype());
  EXPECT_EQ(data_.flags, mount_point->flags());

  EXPECT_CALL(platform_, Unmount).WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(kMountPath))
      .WillOnce(Return(true));
}

TEST_F(MountPointTest, CreateUnmounted) {
  const std::unique_ptr<MountPoint> mount_point =
      MountPoint::CreateUnmounted(data_);
  EXPECT_TRUE(mount_point);
  EXPECT_FALSE(mount_point->is_mounted());
  EXPECT_EQ(data_.mount_path, mount_point->path());
  EXPECT_EQ(data_.source, mount_point->source());
  EXPECT_EQ(data_.filesystem_type, mount_point->fstype());
  EXPECT_EQ(data_.flags, mount_point->flags());
}

TEST_F(MountPointTest, ParseProgressMessage) {
  int percent = -1;
  EXPECT_FALSE(MountPoint::ParseProgressMessage("", &percent));
  EXPECT_FALSE(MountPoint::ParseProgressMessage("x", &percent));
  EXPECT_EQ(percent, -1);

  EXPECT_FALSE(MountPoint::ParseProgressMessage("%", &percent));
  EXPECT_FALSE(MountPoint::ParseProgressMessage(" %", &percent));
  EXPECT_FALSE(MountPoint::ParseProgressMessage("x%", &percent));

  percent = -1;
  EXPECT_TRUE(MountPoint::ParseProgressMessage("0%", &percent));
  EXPECT_EQ(percent, 0);

  percent = -1;
  EXPECT_TRUE(MountPoint::ParseProgressMessage("Whatever 1%", &percent));
  EXPECT_EQ(percent, 1);

  percent = -1;
  EXPECT_TRUE(MountPoint::ParseProgressMessage("Whatever 99%", &percent));
  EXPECT_EQ(percent, 99);

  percent = -1;
  EXPECT_TRUE(MountPoint::ParseProgressMessage("Whatever 100%", &percent));
  EXPECT_EQ(percent, 100);

  percent = -1;
  EXPECT_FALSE(MountPoint::ParseProgressMessage("Whatever 101%", &percent));
  EXPECT_EQ(percent, 101);

  percent = -1;
  EXPECT_FALSE(MountPoint::ParseProgressMessage("Whatever 9999999999999999999%",
                                                &percent));
  EXPECT_EQ(percent, std::numeric_limits<int>::max());
}

TEST_F(MountPointTest, ProgressCallback) {
  const std::unique_ptr<MountPoint> mount_point =
      MountPoint::CreateUnmounted(data_);
  mount_point->OnProgress("Loading 1%");
  mount_point->OnProgress("Loading 2%");
  std::vector<int> progress_percents;
  mount_point->SetProgressCallback(base::BindLambdaForTesting(
      [&progress_percents, want_mount_point = mount_point.get()](
          const MountPoint* const got_mount_point) {
        EXPECT_EQ(want_mount_point, got_mount_point);
        progress_percents.push_back(got_mount_point->progress_percent());
      }));
  mount_point->OnProgress("Loading 3%");
  mount_point->OnProgress("Loading 99%");
  mount_point->OnProgress("");
  mount_point->OnProgress("%");
  mount_point->OnProgress("xx%");
  mount_point->OnProgress("Ignored 1");
  mount_point->OnProgress("Loading 100%");
  EXPECT_THAT(progress_percents, ElementsAre(3, 99, 100));
}

}  // namespace cros_disks
