// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dhcp_server_controller.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/posix/eintr_wrapper.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <net-base/ipv4_address.h>
#include <shill/net/mock_process_manager.h>

#include "patchpanel/metrics.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::StrictMock;
using testing::WithArg;

namespace patchpanel {
namespace {

using Config = DHCPServerController::Config;
using net_base::IPv4Address;
using net_base::IPv4CIDR;

class MockCallback {
 public:
  MOCK_METHOD(void, OnProcessExited, (int));
};

ACTION_P(SetStderrFd, stderr_fd) {
  *arg6.stderr_fd = stderr_fd;
}

}  // namespace

class DHCPServerControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(0, pipe(pipe_fds_));

    dhcp_server_controller_ = std::make_unique<DHCPServerController>(
        &metrics_, kTetheringDHCPServerUmaEventMetrics, "wlan0");
    dhcp_server_controller_->set_process_manager_for_testing(&process_manager_);
  }

  Config CreateValidConfig() {
    const auto host_ip = *IPv4CIDR::CreateFromCIDRString("192.168.1.1/24");
    const auto start_ip = *IPv4Address::CreateFromString("192.168.1.50");
    const auto end_ip = *IPv4Address::CreateFromString("192.168.1.100");
    const auto dns_servers = {
        *IPv4Address::CreateFromString("1.2.3.4"),
        *IPv4Address::CreateFromString("5.6.7.8"),
    };
    const std::vector<std::string> domain_searches = {
        "domain.local0",
        "domain.local1",
    };
    const std::optional<int> mtu = 2000;
    const Config::DHCPOptions dhcp_options = {{43, "ANDROID_METERED"}};
    return Config::Create(host_ip, start_ip, end_ip, dns_servers,
                          domain_searches, mtu, dhcp_options)
        .value();
  }

  void ExpectMetrics(TetheringDHCPServerUmaEvent event, int times) {
    EXPECT_CALL(
        metrics_,
        SendEnumToUMA(
            kTetheringDHCPServerUmaEventMetrics, static_cast<int>(event),
            static_cast<int>(TetheringDHCPServerUmaEvent::kMaxValue) + 1))
        .Times(times)
        .WillRepeatedly(Return(true));
  }

  int read_fd() const { return pipe_fds_[0]; }
  int write_fd() const { return pipe_fds_[1]; }

  // The environment instances which are required for using
  // base::FileDescriptorWatcher::WatchReadable. Declared them first to ensure
  // they are the last things to be cleaned up.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};

  StrictMock<MetricsLibraryMock> metrics_;
  StrictMock<shill::MockProcessManager> process_manager_;
  std::unique_ptr<DHCPServerController> dhcp_server_controller_;

  int pipe_fds_[2];
};

TEST_F(DHCPServerControllerTest, ConfigWithWrongSubnet) {
  // start_ip and end_ip are not in 192.168.1.0 subnet.
  const auto host_ip = *IPv4CIDR::CreateFromCIDRString("192.168.1.1/24");
  const auto start_ip = *IPv4Address::CreateFromString("192.168.5.50");
  const auto end_ip = *IPv4Address::CreateFromString("192.168.5.100");

  EXPECT_EQ(Config::Create(host_ip, start_ip, end_ip, {}, {}, std::nullopt, {}),
            std::nullopt);
}

TEST_F(DHCPServerControllerTest, ConfigWithWrongRange) {
  // end_ip is smaller than start_ip.
  const auto host_ip = *IPv4CIDR::CreateFromCIDRString("192.168.1.1/24");
  const auto start_ip = *IPv4Address::CreateFromString("192.168.1.100");
  const auto end_ip = *IPv4Address::CreateFromString("192.168.1.50");

  EXPECT_EQ(Config::Create(host_ip, start_ip, end_ip, {}, {}, std::nullopt, {}),
            std::nullopt);
}

TEST_F(DHCPServerControllerTest, ValidConfig) {
  const auto host_ip = *IPv4CIDR::CreateFromCIDRString("192.168.1.1/24");
  const auto start_ip = *IPv4Address::CreateFromString("192.168.1.50");
  const auto end_ip = *IPv4Address::CreateFromString("192.168.1.100");
  const auto dns_servers = {
      *IPv4Address::CreateFromString("1.2.3.4"),
      *IPv4Address::CreateFromString("5.6.7.8"),
  };
  const std::vector<std::string> domain_searches = {
      "domain.local0",
      "domain.local1",
  };
  const std::optional<int> mtu = 2000;
  const Config::DHCPOptions dhcp_options = {{43, "ANDROID_METERED"}};
  const auto config = Config::Create(host_ip, start_ip, end_ip, dns_servers,
                                     domain_searches, mtu, dhcp_options);

  ASSERT_NE(config, std::nullopt);
  EXPECT_EQ(config->host_ip(), "192.168.1.1");
  EXPECT_EQ(config->netmask(), "255.255.255.0");
  EXPECT_EQ(config->start_ip(), "192.168.1.50");
  EXPECT_EQ(config->end_ip(), "192.168.1.100");
  EXPECT_EQ(config->dns_servers(), "1.2.3.4,5.6.7.8");
  EXPECT_EQ(config->mtu(), "2000");
  EXPECT_EQ(config->dhcp_options(), dhcp_options);
}

TEST_F(DHCPServerControllerTest, ValidConfigWithoutOptionalArgument) {
  const auto host_ip = *IPv4CIDR::CreateFromCIDRString("192.168.1.1/24");
  const auto start_ip = *IPv4Address::CreateFromString("192.168.1.50");
  const auto end_ip = *IPv4Address::CreateFromString("192.168.1.100");
  const auto config =
      Config::Create(host_ip, start_ip, end_ip, {}, {}, std::nullopt, {});

  ASSERT_NE(config, std::nullopt);
  EXPECT_EQ(config->dns_servers(), "");
  EXPECT_EQ(config->domain_searches(), "");
  EXPECT_EQ(config->mtu(), "");
}

TEST_F(DHCPServerControllerTest, StartSuccessfulAtFirstTime) {
  const auto config = CreateValidConfig();
  const auto cmd_path = base::FilePath("/usr/sbin/dnsmasq");
  const std::vector<std::string> cmd_args = {
      "--log-facility=-",
      "--dhcp-authoritative",
      "--keep-in-foreground",
      "--log-dhcp",
      "--no-ping",
      "--port=0",
      "--leasefile-ro",
      "--interface=wlan0",
      "--dhcp-range=192.168.1.50,192.168.1.100,255.255.255.0,12h",
      "--dhcp-option=option:netmask,255.255.255.0",
      "--dhcp-option=option:router,192.168.1.1",
      "--dhcp-option=option:dns-server,1.2.3.4,5.6.7.8",
      "--dhcp-option=option:domain-search,domain.local0,domain.local1",
      "--dhcp-option=option:mtu,2000",
      "--dhcp-option-force=43,ANDROID_METERED",
  };
  constexpr pid_t pid = 5;

  // Start() is called twice, but only the first time is successful.
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStart, 2);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStartSuccess, 1);
  // Stop() is called once at destructor, and it is successful.
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStop, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStopSuccess, 1);

  EXPECT_CALL(process_manager_, StartProcessInMinijailWithPipes(
                                    _, cmd_path, cmd_args, _, _, _, _))
      .WillOnce(DoAll(SetStderrFd(read_fd()), Return(pid)));
  EXPECT_CALL(process_manager_, StopProcess(pid)).WillOnce(Return(true));

  // Start() should be successful at the first time.
  EXPECT_EQ(dhcp_server_controller_->IsRunning(), false);
  EXPECT_TRUE(dhcp_server_controller_->Start(config, base::DoNothing()));
  EXPECT_TRUE(dhcp_server_controller_->IsRunning());

  // Start() should fail and do nothing when the previous one is still running.
  EXPECT_FALSE(dhcp_server_controller_->Start(config, base::DoNothing()));
}

TEST_F(DHCPServerControllerTest, StartFailed) {
  const auto config = CreateValidConfig();
  constexpr pid_t invalid_pid = shill::ProcessManager::kInvalidPID;

  ExpectMetrics(TetheringDHCPServerUmaEvent::kStart, 1);
  EXPECT_CALL(process_manager_, StartProcessInMinijailWithPipes)
      .WillOnce(Return(invalid_pid));

  // Start() should fail if receiving invalid pid.
  EXPECT_FALSE(dhcp_server_controller_->Start(config, base::DoNothing()));
  EXPECT_FALSE(dhcp_server_controller_->IsRunning());
}

TEST_F(DHCPServerControllerTest, StartAndStop) {
  const auto config = CreateValidConfig();
  constexpr pid_t pid = 5;

  ExpectMetrics(TetheringDHCPServerUmaEvent::kStart, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStartSuccess, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStop, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStopSuccess, 1);

  EXPECT_CALL(process_manager_, StartProcessInMinijailWithPipes)
      .WillOnce(DoAll(SetStderrFd(read_fd()), Return(pid)));
  EXPECT_CALL(process_manager_, StopProcess(pid)).WillOnce(Return(true));

  EXPECT_TRUE(dhcp_server_controller_->Start(config, base::DoNothing()));
  EXPECT_TRUE(dhcp_server_controller_->IsRunning());

  // After the server is running, Stop() should make the server not running.
  dhcp_server_controller_->Stop();
  EXPECT_FALSE(dhcp_server_controller_->IsRunning());
}

TEST_F(DHCPServerControllerTest, OnProcessExited) {
  const auto config = CreateValidConfig();
  constexpr pid_t pid = 5;
  constexpr int exit_status = 7;

  // Store the exit callback at |exit_cb_at_process_manager|.
  base::OnceCallback<void(int)> exit_cb_at_process_manager;
  EXPECT_CALL(process_manager_, StartProcessInMinijailWithPipes)
      .WillOnce(
          DoAll(SetStderrFd(read_fd()),
                WithArg<5>([&exit_cb_at_process_manager](
                               base::OnceCallback<void(int)> exit_callback) {
                  exit_cb_at_process_manager = std::move(exit_callback);
                  return pid;
                })));

  ExpectMetrics(TetheringDHCPServerUmaEvent::kStart, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStartSuccess, 1);

  // The callback from caller should be called with exit status when the process
  // is exited unexpectedly.
  MockCallback exit_cb_from_caller;
  EXPECT_CALL(exit_cb_from_caller, OnProcessExited(exit_status));
  EXPECT_TRUE(dhcp_server_controller_->Start(
      config, base::BindOnce(&MockCallback::OnProcessExited,
                             base::Unretained(&exit_cb_from_caller))));

  // Call the |exit_cb_at_process_manager| to simulate the process being exited
  // unexpectedly.
  std::move(exit_cb_at_process_manager).Run(exit_status);
}

TEST_F(DHCPServerControllerTest, Metric) {
  const auto config = CreateValidConfig();
  constexpr pid_t pid = 5;

  ExpectMetrics(TetheringDHCPServerUmaEvent::kStart, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStartSuccess, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStop, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kStopSuccess, 1);

  ExpectMetrics(TetheringDHCPServerUmaEvent::kDHCPMessageRequest, 2);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kDHCPMessageAck, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kDHCPMessageNak, 1);
  ExpectMetrics(TetheringDHCPServerUmaEvent::kDHCPMessageDecline, 1);

  EXPECT_CALL(process_manager_, StartProcessInMinijailWithPipes)
      .WillOnce(DoAll(SetStderrFd(read_fd()), Return(pid)));
  EXPECT_CALL(process_manager_, StopProcess(pid)).WillOnce(Return(true));
  EXPECT_TRUE(dhcp_server_controller_->Start(config, base::DoNothing()));

  base::WriteFileDescriptor(write_fd(), "DHCPREQUEST(ap0)\n");
  base::WriteFileDescriptor(write_fd(), "DHCPNAK(ap0)\n");
  base::WriteFileDescriptor(write_fd(), "DHCPDISCOVER(ap0)\n");
  base::WriteFileDescriptor(write_fd(), "DHCPOFFER(ap0)\n");
  base::WriteFileDescriptor(write_fd(), "DHCPREQUEST(ap0)\n");
  base::WriteFileDescriptor(write_fd(), "DHCPACK(ap0)\n");
  base::WriteFileDescriptor(write_fd(), "DHCPDECLINE(ap0)\n");
  base::RunLoop().RunUntilIdle();
}

}  // namespace patchpanel
