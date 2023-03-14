// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/subprocess_controller.h"

#include <fcntl.h>

#include <memory>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>
#include <shill/net/ip_address.h>
#include <shill/net/mock_process_manager.h>

#include "patchpanel/fake_system.h"

using testing::_;
using testing::Return;
using testing::WithArg;

namespace patchpanel {
namespace {

const base::FilePath kCmdPath("/usr/bin/patchpaneld");

class SubprocessControllerTest : public ::testing::Test {
 protected:
  // Generate a fake FD for the variables that will be closed during the tests.
  int GenerateFakeFd() { return open("/dev/null", O_RDONLY); }

  // Should be the first member to be initialized first and destroyed last.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  FakeSystem system_;
  shill::MockProcessManager process_manager_;
};

TEST_F(SubprocessControllerTest, Start) {
  // Generate the fake fds as the result of socketpair().
  const int fake_fds[2] = {GenerateFakeFd(), GenerateFakeFd()};
  EXPECT_CALL(system_, SocketPair(AF_UNIX, SOCK_SEQPACKET, 0, _))
      .WillOnce(WithArg<3>([&](int sv[2]) {
        sv[0] = fake_fds[0];
        sv[1] = fake_fds[1];
        return 0;
      }));

  const std::string fd_arg =
      base::StringPrintf("--adb_proxy_fd=%d", fake_fds[1]);
  const std::vector<std::string> args = {"--arg1", "--arg2", fd_arg};
  const std::vector<std::pair<int, int>> fds_to_bind = {
      {fake_fds[1], fake_fds[1]}};
  constexpr pid_t pid = 9;
  EXPECT_CALL(process_manager_,
              StartProcess(_, kCmdPath, args, _, fds_to_bind, true, _))
      .WillOnce(Return(pid));

  SubprocessController subprocess_controller(
      &system_, &process_manager_, kCmdPath,
      std::vector<std::string>{"--arg1", "--arg2"}, "--adb_proxy_fd");
  subprocess_controller.Start();
}

TEST_F(SubprocessControllerTest, Restart) {
  ON_CALL(system_, SocketPair(_, _, _, _))
      .WillByDefault(WithArg<3>([&](int sv[2]) {
        sv[0] = GenerateFakeFd();
        sv[1] = GenerateFakeFd();
        return 0;
      }));

  // Store the exit callback at |exit_cb_at_process_manager|.
  base::OnceCallback<void(int)> exit_cb_at_process_manager;
  constexpr pid_t pid = 9;
  EXPECT_CALL(process_manager_, StartProcess(_, _, _, _, _, _, _))
      .WillOnce(WithArg<6>([&exit_cb_at_process_manager](
                               base::OnceCallback<void(int)> exit_callback) {
        exit_cb_at_process_manager = std::move(exit_callback);
        return pid;
      }));

  SubprocessController subprocess_controller(&system_, &process_manager_,
                                             kCmdPath, {}, "--adb_proxy_fd");
  subprocess_controller.Start();

  // The start logic should be called again when the process is exited
  // unexpectedly.
  EXPECT_CALL(process_manager_, StartProcess(_, _, _, _, _, _, _))
      .WillOnce(Return(pid));

  // Call the |exit_cb_at_process_manager| to simulate the process being exited
  // unexpectedly.
  constexpr int exit_status = 1;
  std::move(exit_cb_at_process_manager).Run(exit_status);

  // The restart callback should be called in 1 second.
  task_environment_.FastForwardBy(base::Milliseconds(1000));
}

}  // namespace
}  // namespace patchpanel
