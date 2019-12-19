// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_mount_manager.h"

#include <sys/mount.h>

#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_util.h>
#include <brillo/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/fuse_helper.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/uri.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::WithArg;

namespace cros_disks {

namespace {

const char kMountRoot[] = "/mntroot";
const char kWorkingDirRoot[] = "/wkdir";
const char kNoType[] = "";
const char kSomeMountpoint[] = "/mnt";
const Uri kSomeSource("fuse", "something");

// Mock Platform implementation for testing.
class MockPlatform : public Platform {
 public:
  MockPlatform() = default;

  MOCK_METHOD(MountErrorType,
              Unmount,
              (const std::string&, int),
              (const, override));
  MOCK_METHOD(bool, DirectoryExists, (const std::string&), (const, override));
  MOCK_METHOD(bool, CreateDirectory, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              SetPermissions,
              (const std::string&, mode_t),
              (const, override));
  MOCK_METHOD(bool,
              CreateTemporaryDirInDir,
              (const std::string&, const std::string&, std::string*),
              (const, override));
};

// Mock implementation of FUSEHelper.
class MockHelper : public FUSEHelper {
 public:
  MockHelper(const std::string& tag,
             const Platform* platform,
             brillo::ProcessReaper* process_reaper)
      : FUSEHelper(tag,
                   platform,
                   process_reaper,
                   base::FilePath("/sbin/" + tag),
                   "fuse-" + tag) {}

  MOCK_METHOD(bool, CanMount, (const Uri&), (const, override));
  MOCK_METHOD(std::string, GetTargetSuffix, (const Uri&), (const, override));
  MOCK_METHOD(std::unique_ptr<FUSEMounter>,
              CreateMounter,
              (const base::FilePath&,
               const Uri&,
               const base::FilePath&,
               const std::vector<std::string>&),
              (const, override));
};

class MockMounter : public FUSEMounter {
 public:
  MockMounter(const Platform* platform, brillo::ProcessReaper* process_reaper)
      : FUSEMounter("foobar",
                    "/mnt",
                    "fuse",
                    MountOptions(),
                    platform,
                    process_reaper,
                    "/bin/sh",
                    "root",
                    "",
                    {},
                    false) {}
  MOCK_METHOD(std::unique_ptr<MountPoint>,
              Mount,
              (const std::string&,
               const base::FilePath&,
               std::vector<std::string>,
               MountErrorType*),
              (const, override));
};

}  // namespace

class FUSEMountManagerTest : public ::testing::Test {
 public:
  FUSEMountManagerTest()
      : manager_(kMountRoot,
                 kWorkingDirRoot,
                 &platform_,
                 &metrics_,
                 &process_reaper_),
        foo_(new MockHelper("foo", &platform_, &process_reaper_)),
        bar_(new MockHelper("bar", &platform_, &process_reaper_)),
        baz_(new MockHelper("baz", &platform_, &process_reaper_)) {
    ON_CALL(platform_, Unmount(_, _))
        .WillByDefault(Return(MOUNT_ERROR_INVALID_ARGUMENT));
    ON_CALL(platform_, DirectoryExists(_)).WillByDefault(Return(true));
  }

 protected:
  void RegisterHelper(std::unique_ptr<FUSEHelper> helper) {
    manager_.RegisterHelper(std::move(helper));
  }

  MountErrorType DoMount(const std::string& type, const std::string& src) {
    MountOptions mount_options;
    return manager_.DoMount(src, type, {}, kSomeMountpoint, &mount_options);
  }

  Metrics metrics_;
  MockPlatform platform_;
  brillo::ProcessReaper process_reaper_;
  FUSEMountManager manager_;
  std::unique_ptr<MockHelper> foo_;
  std::unique_ptr<MockHelper> bar_;
  std::unique_ptr<MockHelper> baz_;
};

// Verifies that CanMount returns false when there are no handlers registered.
TEST_F(FUSEMountManagerTest, CanMount_NoHandlers) {
  EXPECT_FALSE(manager_.CanMount(kSomeSource.value()));
}

// Verifies that CanMount returns false when known helpers can't handle that.
TEST_F(FUSEMountManagerTest, CanMount_NotHandled) {
  EXPECT_CALL(*foo_, CanMount(_)).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount(_)).WillOnce(Return(false));
  EXPECT_CALL(*baz_, CanMount(_)).WillOnce(Return(false));
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  EXPECT_FALSE(manager_.CanMount(kSomeSource.value()));
}

// Verify that CanMount returns true when there is a helper that can handle
// this source.
TEST_F(FUSEMountManagerTest, CanMount) {
  EXPECT_CALL(*foo_, CanMount(_)).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount(_)).WillOnce(Return(true));
  EXPECT_CALL(*baz_, CanMount(_)).Times(0);
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  EXPECT_TRUE(manager_.CanMount(kSomeSource.value()));
}

// Verify that SuggestMountPath dispatches query for name to the correct helper.
TEST_F(FUSEMountManagerTest, SuggestMountPath) {
  EXPECT_CALL(*foo_, CanMount(_)).WillOnce(Return(false));
  EXPECT_CALL(*foo_, GetTargetSuffix(_)).Times(0);
  EXPECT_CALL(*bar_, CanMount(_)).WillOnce(Return(true));
  EXPECT_CALL(*bar_, GetTargetSuffix(kSomeSource)).WillOnce(Return("suffix"));
  EXPECT_CALL(*baz_, CanMount(_)).Times(0);
  EXPECT_CALL(*baz_, GetTargetSuffix(_)).Times(0);
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  EXPECT_EQ("/mntroot/suffix", manager_.SuggestMountPath(kSomeSource.value()));
}

// Verify that DoUnmount delegates unmount directly to platform.
TEST_F(FUSEMountManagerTest, DoUnmount) {
  EXPECT_CALL(platform_, Unmount(kSomeSource.value(), 0))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, Unmount("foobar", 0))
      .WillOnce(Return(MOUNT_ERROR_PATH_NOT_MOUNTED));
  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.DoUnmount(kSomeSource.value()));
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, manager_.DoUnmount("foobar"));
}

// Verify that DoUnmount forces unmount when the filesystem is busy.
TEST_F(FUSEMountManagerTest, DoUnmount_Busy) {
  EXPECT_CALL(platform_, Unmount("foobar", 0))
      .WillOnce(Return(MOUNT_ERROR_PATH_ALREADY_MOUNTED));
  EXPECT_CALL(platform_, Unmount("foobar", MNT_FORCE | MNT_DETACH))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.DoUnmount("foobar"));
}

// Verify that DoMount fails when there are not helpers.
TEST_F(FUSEMountManagerTest, DoMount_NoHandlers) {
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            DoMount(kNoType, kSomeSource.value()));
}

// Verify that DoMount fails when helpers don't handle this source.
TEST_F(FUSEMountManagerTest, DoMount_NotHandled) {
  EXPECT_CALL(*foo_, CanMount(_)).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount(_)).WillOnce(Return(false));
  EXPECT_CALL(*baz_, CanMount(_)).WillOnce(Return(false));
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            DoMount(kNoType, kSomeSource.value()));
}

// Verify that DoMount delegates mounting to the correct helpers when
// dispatching by source description.
TEST_F(FUSEMountManagerTest, DoMount_BySource) {
  EXPECT_CALL(*foo_, CanMount(_)).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*baz_, CanMount(_)).Times(0);
  EXPECT_CALL(*foo_, CreateMounter(_, _, _, _)).Times(0);
  EXPECT_CALL(platform_, CreateTemporaryDirInDir(kWorkingDirRoot, _, _))
      .WillOnce(DoAll(SetArgPointee<2>("/blah"), Return(true)));
  EXPECT_CALL(platform_, SetPermissions("/blah", 0755)).WillOnce(Return(true));
  MockMounter* mounter = new MockMounter(&platform_, &process_reaper_);
  EXPECT_CALL(*mounter, Mount("foobar", base::FilePath(kSomeMountpoint), _, _))
      .WillOnce(WithArg<3>([](MountErrorType* error) {
        *error = MOUNT_ERROR_NONE;
        return std::make_unique<MountPoint>(base::FilePath(kSomeMountpoint),
                                            nullptr);
      }));
  std::unique_ptr<FUSEMounter> ptr(mounter);
  EXPECT_CALL(*bar_, CreateMounter(base::FilePath("/blah"), kSomeSource,
                                   base::FilePath(kSomeMountpoint), _))
      .WillOnce(Return(ByMove(std::move(ptr))));
  EXPECT_CALL(*baz_, CreateMounter(_, _, _, _)).Times(0);
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  EXPECT_EQ(MOUNT_ERROR_NONE, DoMount(kNoType, kSomeSource.value()));
}

}  // namespace cros_disks
