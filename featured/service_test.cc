// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/dcheck_is_on.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>

#include "featured/service.h"
#include "featured/store_impl.h"
#include "featured/store_impl_mock.h"
#include "featured/store_interface.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

namespace featured {

TEST(FeatureCommand, FileExistsTest) {
  base::FilePath file;
  ASSERT_TRUE(base::CreateTemporaryFile(&file));

  FileExistsCommand c(file.MaybeAsASCII());
  ASSERT_TRUE(c.Execute());

  FileNotExistsCommand c2(file.MaybeAsASCII());
  ASSERT_FALSE(c2.Execute());
}

TEST(FeatureCommand, FileNotExistsTest) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());

  base::FilePath file(dir.GetPath().Append("non-existent"));

  FileNotExistsCommand c(file.MaybeAsASCII());
  ASSERT_TRUE(c.Execute());

  FileExistsCommand c2(file.MaybeAsASCII());
  ASSERT_FALSE(c2.Execute());
}

TEST(FeatureCommand, MkdirTest) {
  if (base::PathExists(base::FilePath("/sys/kernel/tracing/instances/"))) {
    const std::string sys_path = "/sys/kernel/tracing/instances/unittest";
    EXPECT_FALSE(base::PathExists(base::FilePath(sys_path)));
    EXPECT_TRUE(featured::MkdirCommand(sys_path).Execute());
    EXPECT_TRUE(base::PathExists(base::FilePath(sys_path)));
    EXPECT_TRUE(base::DeleteFile(base::FilePath(sys_path)));
    EXPECT_FALSE(base::PathExists(base::FilePath(sys_path)));
  }

  if (base::PathExists(base::FilePath("/mnt"))) {
    const std::string mnt_path = "/mnt/notallowed";
    EXPECT_FALSE(base::PathExists(base::FilePath(mnt_path)));
    EXPECT_FALSE(featured::MkdirCommand(mnt_path).Execute());
    EXPECT_FALSE(base::PathExists(base::FilePath(mnt_path)));
  }
}

class DbusFeaturedServiceTest : public testing::Test {
 public:
  DbusFeaturedServiceTest()
      : mock_store_impl_(std::make_unique<StrictMock<MockStoreImpl>>()),
        mock_bus_(base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options{})),
        path_(chromeos::kChromeFeaturesServicePath),
        mock_proxy_(base::MakeRefCounted<dbus::MockObjectProxy>(
            mock_bus_.get(), chromeos::kChromeFeaturesServiceName, path_)),
        mock_exported_object_(
            base::MakeRefCounted<StrictMock<dbus::MockExportedObject>>(
                mock_bus_.get(), path_)) {
    ON_CALL(*mock_bus_, GetExportedObject(_))
        .WillByDefault(Return(mock_exported_object_.get()));
    ON_CALL(*mock_bus_, Connect()).WillByDefault(Return(true));
    ON_CALL(*mock_bus_, GetObjectProxy(_, _))
        .WillByDefault(Return(mock_proxy_.get()));
    ON_CALL(*mock_bus_, RequestOwnershipAndBlock(_, _))
        .WillByDefault(Return(true));
  }

 protected:
  std::unique_ptr<MockStoreImpl> mock_store_impl_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  dbus::ObjectPath path_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
};

// Checks that service start successfully increments the boot attempts counter
// on boot.
TEST_F(DbusFeaturedServiceTest, IncrementBootAttemptsOnStartup_Success) {
  EXPECT_CALL(*mock_store_impl_, IncrementBootAttemptsSinceLastUpdate())
      .WillOnce(Return(true));

  std::shared_ptr<DbusFeaturedService> service =
      std::make_shared<DbusFeaturedService>(std::move(mock_store_impl_));
  ASSERT_NE(service, nullptr);

  EXPECT_TRUE(service->Start(mock_bus_.get(), service));
}

// Checks that service start fails when incrementing the boot attempts counter
// on boot fails.
TEST_F(DbusFeaturedServiceTest, IncrementBootAttemptsOnStartup_Failure) {
  EXPECT_CALL(*mock_store_impl_, IncrementBootAttemptsSinceLastUpdate())
      .WillOnce(Return(false));

  std::shared_ptr<DbusFeaturedService> service =
      std::make_shared<DbusFeaturedService>(std::move(mock_store_impl_));
  ASSERT_NE(service, nullptr);

  EXPECT_FALSE(service->Start(mock_bus_.get(), service));
}
}  // namespace featured
