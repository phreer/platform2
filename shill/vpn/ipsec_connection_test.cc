// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ipsec_connection.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_process_manager.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/fake_vpn_util.h"

namespace shill {

class IPsecConnectionUnderTest : public IPsecConnection {
 public:
  IPsecConnectionUnderTest(std::unique_ptr<Config> config,
                           std::unique_ptr<Callbacks> callbacks,
                           EventDispatcher* dispatcher,
                           ProcessManager* process_manager)
      : IPsecConnection(std::move(config),
                        std::move(callbacks),
                        dispatcher,
                        process_manager) {
    vpn_util_ = std::make_unique<FakeVPNUtil>();
  }

  IPsecConnectionUnderTest(const IPsecConnectionUnderTest&) = delete;
  IPsecConnectionUnderTest& operator=(const IPsecConnectionUnderTest&) = delete;

  base::FilePath SetTempDir() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    return temp_dir_.GetPath();
  }

  void InvokeScheduleConnectTask(ConnectStep step) {
    IPsecConnection::ScheduleConnectTask(step);
  }

  void set_strongswan_conf_path(const base::FilePath& path) {
    strongswan_conf_path_ = path;
  }

  void set_swanctl_conf_path(const base::FilePath& path) {
    swanctl_conf_path_ = path;
  }

  void set_vici_socket_path(const base::FilePath& path) {
    vici_socket_path_ = path;
  }

  void set_state(State state) { state_ = state; }

  MOCK_METHOD(void, ScheduleConnectTask, (ConnectStep), (override));
  MOCK_METHOD(void,
              NotifyFailure,
              (Service::ConnectFailure, const std::string&),
              (override));
};

namespace {

using ConnectStep = IPsecConnection::ConnectStep;

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SaveArg;

// Note that there is a MACRO in this string so we cannot use raw string literal
// here.
constexpr char kExpectedStrongSwanConf[] =
    "charon {\n"
    "  accept_unencrypted_mainmode_messages = yes\n"
    "  ignore_routing_tables = 0\n"
    "  install_routes = no\n"
    "  routing_table = 0\n"
    "  syslog {\n"
    "    daemon {\n"
    "      ike = 2\n"
    "      cfg = 2\n"
    "      knl = 2\n"
    "    }\n"
    "  }\n"
    "  plugins {\n"
    "    pkcs11 {\n"
    "      modules {\n"
    "        crypto_module {\n"
    "          path = " PKCS11_LIB
    "\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "}";

class MockCallbacks {
 public:
  MOCK_METHOD(void,
              OnConnected,
              (const std::string& link_name,
               int interface_index,
               const IPConfig::Properties& ip_properties));
  MOCK_METHOD(void, OnFailure, (Service::ConnectFailure));
  MOCK_METHOD(void, OnStopped, ());
};

class IPsecConnectionTest : public testing::Test {
 public:
  IPsecConnectionTest() {
    auto callbacks = std::make_unique<VPNConnection::Callbacks>(
        base::BindRepeating(&MockCallbacks::OnConnected,
                            base::Unretained(&callbacks_)),
        base::BindOnce(&MockCallbacks::OnFailure,
                       base::Unretained(&callbacks_)),
        base::BindOnce(&MockCallbacks::OnStopped,
                       base::Unretained(&callbacks_)));
    ipsec_connection_ = std::make_unique<IPsecConnectionUnderTest>(
        std::make_unique<IPsecConnection::Config>(), std::move(callbacks),
        &dispatcher_, &process_manager_);
  }

 protected:
  EventDispatcherForTest dispatcher_;
  MockCallbacks callbacks_;
  MockProcessManager process_manager_;

  std::unique_ptr<IPsecConnectionUnderTest> ipsec_connection_;
};

TEST_F(IPsecConnectionTest, WriteStrongSwanConfig) {
  base::FilePath temp_dir = ipsec_connection_->SetTempDir();

  // Signal should be send out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kStrongSwanConfigWritten));

  ipsec_connection_->InvokeScheduleConnectTask(ConnectStep::kStart);

  // IPsecConnection should write the config to the `strongswan.conf` file under
  // the temp dir it created.
  base::FilePath expected_path = temp_dir.Append("strongswan.conf");
  ASSERT_TRUE(base::PathExists(expected_path));
  std::string actual_content;
  ASSERT_TRUE(base::ReadFileToString(expected_path, &actual_content));
  EXPECT_EQ(actual_content, kExpectedStrongSwanConf);

  // The file should be deleted after destroying the IPsecConnection object.
  ipsec_connection_ = nullptr;
  ASSERT_FALSE(base::PathExists(expected_path));
}

TEST_F(IPsecConnectionTest, StartCharon) {
  ipsec_connection_->set_state(VPNConnection::State::kConnecting);

  const base::FilePath kStrongSwanConfPath("/tmp/strongswan.conf");
  ipsec_connection_->set_strongswan_conf_path(kStrongSwanConfPath);

  // Prepares the file path under the scoped temp dir. The actual file will be
  // created later to simulate the case that it is created by the charon
  // process.
  const auto tmp_dir = ipsec_connection_->SetTempDir();
  const base::FilePath kViciSocketPath = tmp_dir.Append("charon.vici");
  ipsec_connection_->set_vici_socket_path(kViciSocketPath);

  // Expects call for starting charon process.
  const base::FilePath kExpectedProgramPath("/usr/libexec/ipsec/charon");
  const std::vector<std::string> kExpectedArgs = {};
  const std::map<std::string, std::string> kExpectedEnv = {
      {"STRONGSWAN_CONF", kStrongSwanConfPath.value()}};
  constexpr uint64_t kExpectedCapMask =
      CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_BIND_SERVICE) |
      CAP_TO_MASK(CAP_NET_RAW) | CAP_TO_MASK(CAP_SETGID);
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, kExpectedProgramPath, kExpectedArgs,
                                     kExpectedEnv, "vpn", "vpn",
                                     kExpectedCapMask, true, true, _))
      .WillOnce(Return(123));

  // Triggers the task.
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kStrongSwanConfigWritten);

  // Creates the socket file, and then IPsecConnection should be notified and
  // forward the step. We use a RunLoop here instead of RunUtilIdle() since it
  // cannot be guaranteed that FilePathWatcher posted the task before
  // RunUtilIdle() is called.
  CHECK(base::WriteFile(kViciSocketPath, ""));
  base::RunLoop run_loop;
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kCharonStarted))
      .WillOnce([&](ConnectStep) { run_loop.Quit(); });
  run_loop.Run();
}

TEST_F(IPsecConnectionTest, StartCharonFailWithStartProcess) {
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, "vpn", "vpn",
                                                       _, true, true, _))
      .WillOnce(Return(-1));
  EXPECT_CALL(*ipsec_connection_, NotifyFailure(_, _));
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kStrongSwanConfigWritten);
}

TEST_F(IPsecConnectionTest, StartCharonFailWithCharonExited) {
  base::OnceCallback<void(int)> exit_cb;
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, "vpn", "vpn",
                                                       _, true, true, _))
      .WillOnce(DoAll(SaveArg<9>(&exit_cb), Return(123)));
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kStrongSwanConfigWritten);

  EXPECT_CALL(*ipsec_connection_, NotifyFailure(_, _));
  std::move(exit_cb).Run(1);
}

TEST_F(IPsecConnectionTest, WriteSwanctlConfig) {
  base::FilePath temp_dir = ipsec_connection_->SetTempDir();

  // Signal should be sent out at the end of the execution.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kSwanctlConfigWritten));

  ipsec_connection_->InvokeScheduleConnectTask(ConnectStep::kCharonStarted);

  // IPsecConnection should write the config to the `swanctl.conf` file under
  // the temp dir it created.
  base::FilePath expected_path = temp_dir.Append("swanctl.conf");
  ASSERT_TRUE(base::PathExists(expected_path));
  std::string actual_content;
  ASSERT_TRUE(base::ReadFileToString(expected_path, &actual_content));

  // TODO(b/165170125): Check file contents.

  // The file should be deleted after destroying the IPsecConnection object.
  ipsec_connection_ = nullptr;
  ASSERT_FALSE(base::PathExists(expected_path));
}

TEST_F(IPsecConnectionTest, SwanctlLoadConfig) {
  const base::FilePath kStrongSwanConfPath("/tmp/strongswan.conf");
  ipsec_connection_->set_strongswan_conf_path(kStrongSwanConfPath);

  const base::FilePath kSwanctlConfPath("/tmp/swanctl.conf");
  ipsec_connection_->set_swanctl_conf_path(kSwanctlConfPath);

  // Expects call for starting swanctl process.
  base::OnceCallback<void(int)> exit_cb;
  const base::FilePath kExpectedProgramPath("/usr/sbin/swanctl");
  const std::vector<std::string> kExpectedArgs = {"--load-all", "--file",
                                                  kSwanctlConfPath.value()};
  const std::map<std::string, std::string> kExpectedEnv = {
      {"STRONGSWAN_CONF", kStrongSwanConfPath.value()}};
  constexpr uint64_t kExpectedCapMask = 0;
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, kExpectedProgramPath, kExpectedArgs,
                                     kExpectedEnv, "vpn", "vpn",
                                     kExpectedCapMask, true, true, _))
      .WillOnce(DoAll(SaveArg<9>(&exit_cb), Return(123)));

  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigWritten);

  // Signal should be sent out if swanctl exits with 0.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kSwanctlConfigLoaded));
  std::move(exit_cb).Run(0);
}

TEST_F(IPsecConnectionTest, SwanctlLoadConfigFailExecution) {
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, "vpn", "vpn",
                                                       _, true, true, _))
      .WillOnce(Return(-1));
  EXPECT_CALL(*ipsec_connection_, NotifyFailure(_, _));
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigWritten);
}

TEST_F(IPsecConnectionTest, SwanctlLoadConfigFailExitCodeNonZero) {
  base::OnceCallback<void(int)> exit_cb;
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, "vpn", "vpn",
                                                       _, true, true, _))
      .WillOnce(DoAll(SaveArg<9>(&exit_cb), Return(123)));
  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigWritten);

  EXPECT_CALL(*ipsec_connection_, NotifyFailure(_, _));
  std::move(exit_cb).Run(1);
}

TEST_F(IPsecConnectionTest, SwanctlInitiateConnection) {
  const base::FilePath kStrongSwanConfPath("/tmp/strongswan.conf");
  ipsec_connection_->set_strongswan_conf_path(kStrongSwanConfPath);

  const base::FilePath kSwanctlConfPath("/tmp/swanctl.conf");
  ipsec_connection_->set_swanctl_conf_path(kSwanctlConfPath);

  // Expects call for starting swanctl process.
  base::OnceCallback<void(int)> exit_cb;
  const base::FilePath kExpectedProgramPath("/usr/sbin/swanctl");
  const std::vector<std::string> kExpectedArgs = {"--initiate", "-c",
                                                  "managed"};
  const std::map<std::string, std::string> kExpectedEnv = {
      {"STRONGSWAN_CONF", kStrongSwanConfPath.value()}};
  constexpr uint64_t kExpectedCapMask = 0;
  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, kExpectedProgramPath, kExpectedArgs,
                                     kExpectedEnv, "vpn", "vpn",
                                     kExpectedCapMask, true, true, _))
      .WillOnce(DoAll(SaveArg<9>(&exit_cb), Return(123)));

  ipsec_connection_->InvokeScheduleConnectTask(
      ConnectStep::kSwanctlConfigLoaded);

  // Signal should be sent out if swanctl exits with 0.
  EXPECT_CALL(*ipsec_connection_,
              ScheduleConnectTask(ConnectStep::kIPsecConnected));
  std::move(exit_cb).Run(0);
}

}  // namespace
}  // namespace shill
