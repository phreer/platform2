// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/observer_list.h>
#include <base/observer_list_types.h>
#include <base/strings/string_split.h>
#include <base/test/task_environment.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_monitor.h>
#include <brillo/unittest_utils.h>
#include <gtest/gtest.h>
#include <gtest/gtest_prod.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/common/mojo_type_utils.h"
#include "diagnostics/cros_healthd/events/udev_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/usb_utils_constants.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

constexpr const char kUdevActionAdd[] = "add";
constexpr const char kUdevActionRemove[] = "remove";
constexpr const char kUdevThunderboltSubSystem[] = "thunderbolt";
constexpr char kFakeThunderboltDevicePath[] =
    "sys/bus/thunderbolt/devices/domain0/";
constexpr const char kUdevActionChange[] = "change";
constexpr char kFakeThunderboltFullPath[] =
    "sys/bus/thunderbolt/devices/domain0/0-0:1-0";
constexpr char kFileThunderboltAuthorized[] = "authorized";
constexpr char kThunderboltAuthorized[] = "1";
constexpr char kThunderboltUnAuthorized[] = "0";

constexpr const char kUdevUsbSubSystem[] = "usb";
constexpr const char kUdevUsbDeviceType[] = "usb_device";
constexpr const char kFakeUsbSysPath[] = "sys/fake/dev/path";
constexpr const char kFakeUsbVendor[] = "fake_usb_vendor";
constexpr const char kFakeUsbName[] = "fake_usb_name";
constexpr const char kFakeUsbProduct[] = "47f/430c/1093";
constexpr uint16_t kFakeUsbVid = 0x47f;
constexpr uint16_t kFakeUsbPid = 0x430c;

class MockCrosHealthdThunderboltObserver
    : public mojo_ipc::CrosHealthdThunderboltObserver {
 public:
  explicit MockCrosHealthdThunderboltObserver(
      mojo::PendingReceiver<mojo_ipc::CrosHealthdThunderboltObserver> receiver)
      : receiver_{this /* impl */, std::move(receiver)} {
    DCHECK(receiver_.is_bound());
  }
  MockCrosHealthdThunderboltObserver(
      const MockCrosHealthdThunderboltObserver&) = delete;
  MockCrosHealthdThunderboltObserver& operator=(
      const MockCrosHealthdThunderboltObserver&) = delete;

  MOCK_METHOD(void, OnAdd, (), (override));
  MOCK_METHOD(void, OnRemove, (), (override));
  MOCK_METHOD(void, OnAuthorized, (), (override));
  MOCK_METHOD(void, OnUnAuthorized, (), (override));

 private:
  mojo::Receiver<mojo_ipc::CrosHealthdThunderboltObserver> receiver_;
};

class MockCrosHealthdUsbObserver : public mojo_ipc::CrosHealthdUsbObserver {
 public:
  explicit MockCrosHealthdUsbObserver(
      mojo::PendingReceiver<mojo_ipc::CrosHealthdUsbObserver> receiver)
      : receiver_{this /* impl */, std::move(receiver)} {
    DCHECK(receiver_.is_bound());
  }
  MockCrosHealthdUsbObserver(const MockCrosHealthdUsbObserver&) = delete;
  MockCrosHealthdUsbObserver& operator=(const MockCrosHealthdUsbObserver&) =
      delete;

  MOCK_METHOD(void, OnAdd, (mojo_ipc::UsbEventInfoPtr), (override));
  MOCK_METHOD(void, OnRemove, (mojo_ipc::UsbEventInfoPtr), (override));

 private:
  mojo::Receiver<mojo_ipc::CrosHealthdUsbObserver> receiver_;
};

class UdevEventsImplTest : public BaseFileTest {
 public:
  UdevEventsImpl* udev_events_impl() { return &udev_events_impl_; }

 protected:
  MockContext mock_context_;
  UdevEventsImpl udev_events_impl_{&mock_context_};
};

class ThunderboltEventTest : public UdevEventsImplTest {
 public:
  ThunderboltEventTest()
      : task_environment_(
            base::test::TaskEnvironment::MainThreadType::IO,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC) {}

  void SetUp() override {
    mojo::PendingRemote<mojo_ipc::CrosHealthdThunderboltObserver> observer;
    mojo::PendingReceiver<mojo_ipc::CrosHealthdThunderboltObserver>
        observer_receiver(observer.InitWithNewPipeAndPassReceiver());
    observer_ =
        std::make_unique<StrictMock<MockCrosHealthdThunderboltObserver>>(
            std::move(observer_receiver));
    udev_events_impl_.AddThunderboltObserver(std::move(observer));
    SetTestRoot(mock_context_.root_dir());
  }

  MockCrosHealthdThunderboltObserver* mock_observer() {
    return observer_.get();
  }

  void DestroyMojoObserver() {
    observer_.reset();
    task_environment_.RunUntilIdle();
  }

  void SetUpSysfsFile(const std::string& val) {
    const auto dir = kFakeThunderboltDevicePath;
    const auto dev_file = "0-0:1-0";
    SetFile({dir, dev_file, kFileThunderboltAuthorized}, val);
  }

  void TriggerUdevEvent(const char* action, const char* authorized) {
    const auto& root = mock_context_.root_dir();
    auto path = root.Append(kFakeThunderboltFullPath);
    auto monitor = mock_context_.mock_udev_monitor();
    auto device = std::make_unique<brillo::MockUdevDevice>();
    EXPECT_CALL(*device, GetAction()).WillOnce(Return(action));
    EXPECT_CALL(*device, GetSubsystem())
        .WillOnce(Return(kUdevThunderboltSubSystem));
    if (authorized) {
      SetUpSysfsFile(std::string(authorized));
      EXPECT_CALL(*device, GetSysPath()).WillOnce(Return(path.value().c_str()));
    }
    EXPECT_CALL(*monitor, ReceiveDevice())
        .WillOnce(Return(ByMove(std::move(device))));
    udev_events_impl()->OnUdevEvent();
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<brillo::MockUdevDevice> device_;
  std::unique_ptr<brillo::UdevMonitor> monitor_;
  std::unique_ptr<StrictMock<MockCrosHealthdThunderboltObserver>> observer_;
};

class UsbEventTest : public UdevEventsImplTest {
 public:
  UsbEventTest()
      : task_environment_(
            base::test::TaskEnvironment::MainThreadType::IO,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC) {}

  void SetUp() override {
    mojo::PendingRemote<mojo_ipc::CrosHealthdUsbObserver> observer;
    mojo::PendingReceiver<mojo_ipc::CrosHealthdUsbObserver> observer_receiver(
        observer.InitWithNewPipeAndPassReceiver());
    observer_ = std::make_unique<StrictMock<MockCrosHealthdUsbObserver>>(
        std::move(observer_receiver));
    udev_events_impl_.AddUsbObserver(std::move(observer));
    SetTestRoot(mock_context_.root_dir());
  }

  MockCrosHealthdUsbObserver* mock_observer() { return observer_.get(); }

  void DestroyMojoObserver() {
    observer_.reset();
    task_environment_.RunUntilIdle();
  }

  void SetInterfacesType() {
    // Human Interface Device.
    SetFile({kFakeUsbSysPath, "1-1.2:1.0", "bInterfaceClass"}, "03");
    // Video.
    SetFile({kFakeUsbSysPath, "1-1.2:1.1", "bInterfaceClass"}, "0E");
    // Wireless.
    SetFile({kFakeUsbSysPath, "1-1.2:1.2", "bInterfaceClass"}, "E0");
  }

  void SetSysfsFiles() {
    auto product_tokens =
        base::SplitString(std::string(kFakeUsbProduct), "/",
                          base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    EXPECT_EQ(product_tokens.size(), 3);
    SetFile({kFakeUsbSysPath, kFileUsbVendor}, product_tokens[0]);
    SetFile({kFakeUsbSysPath, kFileUsbProduct}, product_tokens[1]);
  }

  void TriggerUdevEvent(const char* action) {
    const auto& root = mock_context_.root_dir();
    auto path = root.Append(kFakeUsbSysPath);
    auto monitor = mock_context_.mock_udev_monitor();
    auto device = std::make_unique<brillo::MockUdevDevice>();
    EXPECT_CALL(*device, GetAction()).WillOnce(Return(action));
    EXPECT_CALL(*device, GetSubsystem()).WillOnce(Return(kUdevUsbSubSystem));
    EXPECT_CALL(*device, GetDeviceType()).WillOnce(Return(kUdevUsbDeviceType));
    EXPECT_CALL(*device, GetPropertyValue(kPropertieVendorFromDB))
        .WillOnce(Return(kFakeUsbVendor));
    EXPECT_CALL(*device, GetPropertyValue(kPropertieModelFromDB))
        .WillOnce(Return(kFakeUsbName));
    EXPECT_CALL(*device, GetPropertyValue(kPropertieProduct))
        .WillOnce(Return(kFakeUsbProduct));
    EXPECT_CALL(*device, GetSysPath())
        .WillRepeatedly(Return(path.value().c_str()));
    EXPECT_CALL(*monitor, ReceiveDevice())
        .WillOnce(Return(ByMove(std::move(device))));
    SetInterfacesType();
    SetSysfsFiles();

    udev_events_impl()->OnUdevEvent();
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<brillo::MockUdevDevice> device_;
  std::unique_ptr<brillo::UdevMonitor> monitor_;
  std::unique_ptr<StrictMock<MockCrosHealthdUsbObserver>> observer_;
};

TEST_F(ThunderboltEventTest, TestThunderboltAddEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdd()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  TriggerUdevEvent(kUdevActionAdd, nullptr);

  run_loop.Run();
}

TEST_F(ThunderboltEventTest, TestThunderboltRemoveEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnRemove()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  TriggerUdevEvent(kUdevActionRemove, nullptr);

  run_loop.Run();
}

TEST_F(ThunderboltEventTest, TestThunderboltAuthorizedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAuthorized()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  TriggerUdevEvent(kUdevActionChange, kThunderboltAuthorized);

  run_loop.Run();
}

TEST_F(ThunderboltEventTest, TestThunderboltUnAuthorizedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnUnAuthorized()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  TriggerUdevEvent(kUdevActionChange, kThunderboltUnAuthorized);

  run_loop.Run();
}

TEST_F(UsbEventTest, TestUsbAddEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdd(_))
      .WillOnce([&](mojo_ipc::UsbEventInfoPtr info) {
        EXPECT_EQ(info->vendor, kFakeUsbVendor);
        EXPECT_EQ(info->name, kFakeUsbName);
        EXPECT_EQ(info->vid, kFakeUsbVid);
        EXPECT_EQ(info->pid, kFakeUsbPid);
        EXPECT_THAT(info->categories,
                    testing::UnorderedElementsAreArray(
                        {"Wireless", "Human Interface Device", "Video"}));
        run_loop.Quit();
      });

  TriggerUdevEvent(kUdevActionAdd);

  run_loop.Run();
}

TEST_F(UsbEventTest, TestUsbRemoveEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnRemove(_))
      .WillOnce([&](mojo_ipc::UsbEventInfoPtr info) {
        EXPECT_EQ(info->vendor, kFakeUsbVendor);
        EXPECT_EQ(info->name, kFakeUsbName);
        EXPECT_EQ(info->vid, kFakeUsbVid);
        EXPECT_EQ(info->pid, kFakeUsbPid);
        EXPECT_THAT(info->categories,
                    testing::UnorderedElementsAreArray(
                        {"Wireless", "Human Interface Device", "Video"}));
        run_loop.Quit();
      });

  TriggerUdevEvent(kUdevActionRemove);

  run_loop.Run();
}

}  // namespace
}  // namespace diagnostics
