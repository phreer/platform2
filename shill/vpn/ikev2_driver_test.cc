// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ikev2_driver.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_process_manager.h"
#include "shill/test_event_dispatcher.h"

#include "shill/vpn/ipsec_connection.h"
#include "shill/vpn/mock_vpn_driver.h"
#include "shill/vpn/vpn_connection_under_test.h"

namespace shill {

class IKEv2DriverUnderTest : public IKEv2Driver {
 public:
  IKEv2DriverUnderTest(Manager* manager, ProcessManager* process_manager)
      : IKEv2Driver(manager, process_manager) {}

  IKEv2DriverUnderTest(const IKEv2DriverUnderTest&) = delete;
  IKEv2DriverUnderTest& operator=(const IKEv2DriverUnderTest&) = delete;

  VPNConnectionUnderTest* ipsec_connection() const {
    return dynamic_cast<VPNConnectionUnderTest*>(ipsec_connection_.get());
  }

  IPsecConnection::Config* ipsec_config() const { return ipsec_config_.get(); }

 private:
  std::unique_ptr<VPNConnection> CreateIPsecConnection(
      std::unique_ptr<IPsecConnection::Config> config,
      std::unique_ptr<VPNConnection::Callbacks> callbacks,
      DeviceInfo* device_info,
      EventDispatcher* dispatcher,
      ProcessManager* process_manager) override {
    ipsec_config_ = std::move(config);
    auto ipsec_connection = std::make_unique<VPNConnectionUnderTest>(
        std::move(callbacks), dispatcher);
    EXPECT_CALL(*ipsec_connection, OnConnect());
    return ipsec_connection;
  }

  std::unique_ptr<IPsecConnection::Config> ipsec_config_;
};

namespace {

using testing::_;

class IKEv2DriverTest : public testing::Test {
 public:
  IKEv2DriverTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        driver_(new IKEv2DriverUnderTest(&manager_, &process_manager_)) {}

 protected:
  void InvokeAndVerifyConnectAsync() {
    const auto timeout = driver_->ConnectAsync(&event_handler_);
    EXPECT_NE(timeout, VPNDriver::kTimeoutNone);

    dispatcher_.task_environment().RunUntilIdle();
    EXPECT_NE(driver_->ipsec_connection(), nullptr);
    EXPECT_NE(driver_->ipsec_config(), nullptr);
  }

  // Dependencies used by |driver_|.
  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  MockProcessManager process_manager_;

  // Other objects used in the tests.
  MockVPNDriverEventHandler event_handler_;
  std::unique_ptr<PropertyStore> store_;

  std::unique_ptr<IKEv2DriverUnderTest> driver_;
};

TEST_F(IKEv2DriverTest, ConnectAndDisconnect) {
  InvokeAndVerifyConnectAsync();

  // Connected.
  const std::string kIfName = "xfrm0";
  constexpr int kIfIndex = 123;
  driver_->ipsec_connection()->TriggerConnected(kIfName, kIfIndex, {});
  EXPECT_CALL(event_handler_, OnDriverConnected(kIfName, kIfIndex));
  dispatcher_.DispatchPendingEvents();

  // Triggers disconnect.
  driver_->Disconnect();
  EXPECT_CALL(*driver_->ipsec_connection(), OnDisconnect());
  dispatcher_.DispatchPendingEvents();

  // Stopped.
  driver_->ipsec_connection()->TriggerStopped();
  dispatcher_.DispatchPendingEvents();
  EXPECT_EQ(driver_->ipsec_connection(), nullptr);
}

TEST_F(IKEv2DriverTest, ConnectTimeout) {
  InvokeAndVerifyConnectAsync();

  EXPECT_CALL(event_handler_, OnDriverFailure(Service::kFailureConnect, _));
  EXPECT_CALL(*driver_->ipsec_connection(), OnDisconnect());
  driver_->OnConnectTimeout();
  dispatcher_.DispatchPendingEvents();

  driver_->ipsec_connection()->TriggerStopped();
  dispatcher_.DispatchPendingEvents();
  EXPECT_EQ(driver_->ipsec_connection(), nullptr);
}

TEST_F(IKEv2DriverTest, ConnectingFailure) {
  InvokeAndVerifyConnectAsync();

  EXPECT_CALL(event_handler_, OnDriverFailure(Service::kFailureInternal, _));
  driver_->ipsec_connection()->TriggerFailure(Service::kFailureInternal, "");
  dispatcher_.DispatchPendingEvents();

  driver_->ipsec_connection()->TriggerStopped();
  dispatcher_.DispatchPendingEvents();
  EXPECT_EQ(driver_->ipsec_connection(), nullptr);
}

TEST_F(IKEv2DriverTest, ConnectedFailure) {
  InvokeAndVerifyConnectAsync();

  // Makes it connected.
  driver_->ipsec_connection()->TriggerConnected("ifname", 123, {});
  dispatcher_.DispatchPendingEvents();

  EXPECT_CALL(event_handler_, OnDriverFailure(Service::kFailureInternal, _));
  driver_->ipsec_connection()->TriggerFailure(Service::kFailureInternal, "");
  dispatcher_.DispatchPendingEvents();

  driver_->ipsec_connection()->TriggerStopped();
  dispatcher_.DispatchPendingEvents();
  EXPECT_EQ(driver_->ipsec_connection(), nullptr);
}

// TODO(b/210064468): Add tests for default service change and suspend events.

}  // namespace
}  // namespace shill
