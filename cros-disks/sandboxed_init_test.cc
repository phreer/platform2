// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/sandboxed_init.h"

#include <utility>

#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/test/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cros_disks {
namespace {

// Writes a string to a file descriptor.
void Write(int fd, base::StringPiece s) {
  if (!base::WriteFileDescriptor(fd, s))
    PLOG(FATAL) << "Cannot write '" << s << "' to file descriptor " << fd;
}

// Reads a string from a file descriptor.
std::string Read(int fd) {
  char buffer[PIPE_BUF];
  const ssize_t n = HANDLE_EINTR(read(fd, buffer, PIPE_BUF));
  if (n < 0)
    PLOG(FATAL) << "Cannot read from file descriptor " << fd;
  DCHECK_GE(n, 0);
  DCHECK_LE(n, PIPE_BUF);
  return std::string(buffer, n);
}

// Reads a string from a file descriptor.
std::string Read(const base::ScopedFD& fd) {
  return Read(fd.get());
}

class SandboxedInitTest : public testing::Test {
 protected:
  template <typename F>
  void RunUnderInit(F launcher) {
    SandboxedInit init(
        SubprocessPipe::Open(SubprocessPipe::kParentToChild, &in_),
        SubprocessPipe::Open(SubprocessPipe::kChildToParent, &out_),
        base::ScopedFD(dup(STDERR_FILENO)),
        SubprocessPipe::Open(SubprocessPipe::kChildToParent, &ctrl_));

    CHECK(base::SetNonBlocking(ctrl_.get()));

    const pid_t pid = fork();
    if (pid < 0)
      PLOG(FATAL) << "Cannot fork";

    if (pid > 0) {
      // Parent process.
      pid_ = pid;
      return;
    }

    // In 'init' process.
    DCHECK_EQ(0, pid);
    LOG(INFO) << "The 'init' process started";

    // Make the 'init' process a child subreaper, so that it adopts the orphaned
    // 'daemon' process.
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0)
      PLOG(FATAL) << "Cannot make the 'init' process a child subreaper";

    // Avoid leaking pipe file descriptors into the 'init' process.
    in_.reset();
    out_.reset();
    ctrl_.reset();

    // Sets a signal handler for SIGUSR1. This signal handler doesn't do
    // anything, but it is put in place so that SIGUSR1 doesn't terminate the
    // 'init' process.
    CHECK_NE(signal(SIGUSR1,
                    [](int sig) {
                      RAW_LOG(INFO, "The 'init' process received SIGUSR1");
                      RAW_CHECK(sig == SIGUSR1);
                    }),
             SIG_ERR);

    // Run the main 'init' process loop.
    init.RunInsideSandboxNoReturn(
        base::BindLambdaForTesting(std::move(launcher)));
    NOTREACHED();
  }

  // Waits for the 'init' process to terminate if |no_hang == false|. Returns
  // the process's exit code, or -1 if the process is still running and |no_hang
  // == true|.
  int WaitForInitExit(bool no_hang = false) {
    CHECK_LT(0, pid_);
    if (no_hang) {
      LOG(INFO) << "Checking if 'init' is still running...";
    } else {
      LOG(INFO) << "Waiting for 'init' process to finish...";
    }

    int wstatus;
    const int ret =
        HANDLE_EINTR(waitpid(pid_, &wstatus, no_hang ? WNOHANG : 0));
    if (ret < 0)
      PLOG(FATAL) << "Cannot wait for the 'init' process PID " << pid_;

    if (ret == 0) {
      CHECK(no_hang);
      LOG(INFO) << "The 'init' process is still running";
      return -1;
    }

    const int exit_code = SandboxedInit::WStatusToStatus(wstatus);
    LOG(INFO) << "The 'init' process finished with exit code " << exit_code;
    pid_ = -1;
    return exit_code;
  }

  // Waits for the 'launcher' process to finish and returns its exit code.
  int WaitForLauncherExit() {
    EXPECT_TRUE(ctrl_.is_valid());
    LOG(INFO) << "Waiting for the 'launcher' process to finish...";
    const int exit_code = SandboxedInit::WaitForLauncherStatus(&ctrl_);
    LOG(INFO) << "The 'launcher' process finished with exit code " << exit_code;
    EXPECT_FALSE(ctrl_.is_valid());
    return exit_code;
  }

  // Polls the 'launcher' process. Returns its nonnegative exit code if it has
  // already finished, or -1 if it is still running.
  int PollLauncher() {
    EXPECT_TRUE(ctrl_.is_valid());
    LOG(INFO) << "Checking if the 'launcher' process is still running...";
    const int exit_code = SandboxedInit::PollLauncherStatus(&ctrl_);

    if (exit_code < 0) {
      LOG(INFO) << "The 'launcher' process is still running";
      EXPECT_TRUE(ctrl_.is_valid());
    } else {
      LOG(INFO) << "The 'launcher' process finished with exit code "
                << exit_code;
      EXPECT_FALSE(ctrl_.is_valid());
    }

    return exit_code;
  }

  pid_t pid_ = -1;
  base::ScopedFD in_, out_, ctrl_;
};

}  // namespace

TEST_F(SandboxedInitTest, LauncherTerminatesSuccessfully) {
  RunUnderInit([]() { return 0; });
  EXPECT_EQ(0, WaitForLauncherExit());
  EXPECT_EQ(0, WaitForInitExit());
}

TEST_F(SandboxedInitTest, LauncherTerminatesWithError) {
  RunUnderInit([]() { return 12; });
  EXPECT_EQ(12, WaitForLauncherExit());
  EXPECT_EQ(12, WaitForInitExit());
}

TEST_F(SandboxedInitTest, LauncherCrashes) {
  RunUnderInit([]() {
    raise(SIGALRM);
    pause();
    return 35;
  });
  EXPECT_EQ(128 + SIGALRM, WaitForLauncherExit());
  EXPECT_EQ(128 + SIGALRM, WaitForInitExit());
}

TEST_F(SandboxedInitTest, CtrlPipeIsClosed) {
  RunUnderInit([]() {
    // Signal that the 'launcher' process started
    Write(STDOUT_FILENO, "Begin");

    // Wait to be unblocked
    const std::string s = Read(STDIN_FILENO);

    // Signal that the 'launcher' process was unblocked
    Write(STDOUT_FILENO, "Received: " + s);
    return 12;
  });

  // Wait for the 'launcher' process to start.
  EXPECT_EQ("Begin", Read(out_));
  EXPECT_EQ(-1, PollLauncher());

  // Close reading end of control pipe.
  EXPECT_TRUE(ctrl_.is_valid());
  ctrl_.reset();
  EXPECT_FALSE(ctrl_.is_valid());

  // Unblock the 'launcher' process.
  Write(in_.get(), "Continue");

  // Wait for the 'launcher' process to continue.
  EXPECT_EQ("Received: Continue", Read(out_));
  EXPECT_EQ("", Read(out_));

  // Wait for the 'init' process to finish.
  EXPECT_EQ(128 + SIGABRT, WaitForInitExit());
}

TEST_F(SandboxedInitTest, LauncherWritesToStdOut) {
  RunUnderInit([]() {
    Write(STDOUT_FILENO, "Sent to stdout");
    Write(STDERR_FILENO, "This message is written to stderr\n");
    LOG(INFO) << "This is a LOG(INFO) message";
    LOG(WARNING) << "This is a LOG(WARNING) message";
    LOG(ERROR) << "This is a LOG(ERROR) message";
    return 12;
  });

  EXPECT_EQ("Sent to stdout", Read(out_));

  EXPECT_EQ(12, WaitForLauncherExit());
  EXPECT_EQ(12, WaitForInitExit());
  EXPECT_EQ("", Read(out_));
}

TEST_F(SandboxedInitTest, InitUndisturbedBySignal) {
  RunUnderInit([]() {
    // Signal that the 'launcher' process started.
    Write(STDOUT_FILENO, "Begin");

    // Wait to be unblocked.
    const std::string s = Read(STDIN_FILENO);

    // Signal that the 'launcher' process was unblocked.
    Write(STDOUT_FILENO, "Received: " + s);
    return 12;
  });

  // Wait for the 'launcher' process to start.
  EXPECT_EQ("Begin", Read(out_));
  EXPECT_EQ(-1, PollLauncher());

  for (int i = 0; i < 5; ++i) {
    // Send SIGUSR1 to the 'init' process. Because of the signal handler set in
    // RunUnderInit(), this signal shouldn't disturb or crash the 'init'
    // process.
    EXPECT_EQ(0, kill(pid_, SIGUSR1));
    // Send SIGPIPE to the 'init' process. This signal should be ignored, and it
    // shouldn't disturb or crash the 'init' process.
    EXPECT_EQ(0, kill(pid_, SIGPIPE));
    usleep(100'000);
  }

  // Unblock the 'launcher' process.
  Write(in_.get(), "Continue");

  // Wait for the 'launcher' process to continue.
  EXPECT_EQ("Received: Continue", Read(out_));
  EXPECT_EQ(12, WaitForLauncherExit());

  // Wait for the 'init' process to finish.
  EXPECT_EQ(12, WaitForInitExit());
  EXPECT_EQ("", Read(out_));
}

TEST_F(SandboxedInitTest, InitCrashesWhileLauncherIsRunning) {
  RunUnderInit([]() {
    // Signal that the 'launcher' process started.
    Write(STDOUT_FILENO, "Begin");

    // Wait to be unblocked.
    const std::string s = Read(STDIN_FILENO);

    // Signal that the 'launcher' process was unblocked.
    Write(STDOUT_FILENO, "Received: " + s);
    return 12;
  });

  // Wait for the 'launcher' process to start.
  EXPECT_EQ("Begin", Read(out_));
  EXPECT_EQ(-1, PollLauncher());

  // Send SIGALRM to crash the 'init' process.
  EXPECT_EQ(kill(pid_, SIGALRM), 0);

  // Wait for the 'init' process to finish.
  EXPECT_EQ(128 + SIGALRM, WaitForInitExit());

  // Since the 'init' process is not monitoring the 'launcher' process anymore,
  // it reports the 'launcher' process as having been terminated by SIGKILL
  // (which would have happened if this 'init' process was running in a PID
  // namespace).
  EXPECT_EQ(128 + SIGKILL, WaitForLauncherExit());

  // Actually, in this test, the 'launcher' process has been orphaned, but it is
  // still alive. Unblock the 'launcher' process and wait for it to finish.
  Write(in_.get(), "Continue");
  EXPECT_EQ("Received: Continue", Read(out_));
  EXPECT_EQ("", Read(out_));
}

TEST_F(SandboxedInitTest, InitCrashesWhileDaemonIsRunning) {
  RunUnderInit([]() {
    // Launcher process starts a 'daemon' process.
    if (daemon(0, 1) < 0)
      PLOG(FATAL) << "Cannot daemon";

    // Signal that the 'daemon' process started.
    Write(STDOUT_FILENO, "Begin");

    // Wait to be unblocked.
    const std::string s = Read(STDIN_FILENO);

    // Signal that the 'daemon' process was unblocked.
    Write(STDOUT_FILENO, "Received: " + s);
    return 12;
  });

  // The 'launcher' process should terminate first.
  EXPECT_EQ(0, WaitForLauncherExit());

  // Wait for 'daemon' process to start.
  EXPECT_EQ("Begin", Read(out_));

  // The 'init' process should still be there, having adopted the 'daemon'
  // process.
  EXPECT_EQ(-1, WaitForInitExit(true));

  // Send SIGALRM to crash the 'init' process.
  EXPECT_EQ(kill(pid_, SIGALRM), 0);

  // Wait for the 'init' process to finish.
  EXPECT_EQ(128 + SIGALRM, WaitForInitExit());

  // If the 'init' process was in a PID namespace, the 'daemon' process would
  // have been killed by a SIGKILL sent by the kernel. But, in this test, the
  // 'daemon' process has simply been orphaned, and it is still alive. Unblock
  // the 'daemon' process and wait for it to finish.
  Write(in_.get(), "Continue");
  EXPECT_EQ("Received: Continue", Read(out_));
  EXPECT_EQ("", Read(out_));
}

TEST_F(SandboxedInitTest, DaemonBlocksAndTerminates) {
  RunUnderInit([]() {
    // Launcher process starts a 'daemon' process.
    if (daemon(0, 1) < 0)
      PLOG(FATAL) << "Cannot daemon";

    // Signal that the 'daemon' process started.
    Write(STDOUT_FILENO, "Begin");

    // Wait to be unblocked.
    const std::string s = Read(STDIN_FILENO);

    // Signal that the 'daemon' process was unblocked.
    Write(STDOUT_FILENO, "Received: " + s);
    return 42;
  });

  // The 'launcher' process should terminate first.
  EXPECT_EQ(0, WaitForLauncherExit());

  // Wait for 'daemon' process to start.
  EXPECT_EQ("Begin", Read(out_));

  // The 'init' process should still be there, having adopted the 'daemon'
  // process.
  EXPECT_EQ(-1, WaitForInitExit(true));

  // Unblock the 'daemon' process.
  Write(in_.get(), "Continue");

  // Wait for 'daemon' process to continue and finish.
  EXPECT_EQ("Received: Continue", Read(out_));

  // Wait for the 'init' process to finish and relay the 'daemon' process exit
  // code.
  EXPECT_EQ(42, WaitForInitExit());
  EXPECT_EQ("", Read(out_));
}

TEST_F(SandboxedInitTest, DaemonCrashes) {
  RunUnderInit([]() {
    // Launcher process starts a 'daemon' process.
    if (daemon(0, 1) < 0)
      PLOG(FATAL) << "Cannot daemon";

    // Signal that the 'daemon' process started.
    Write(STDOUT_FILENO, "Begin");

    // Raise a signal that should terminate this 'daemon' process.
    raise(SIGALRM);

    // Wait to be unblocked.
    const std::string s = Read(STDIN_FILENO);

    // Signal that the 'daemon' process was unblocked.
    Write(STDOUT_FILENO, "Received: " + s);
    return 42;
  });

  // The 'launcher' process should terminate first.
  EXPECT_EQ(0, WaitForLauncherExit());

  // Wait for the 'init' process to finish and relay the 'daemon' process exit
  // code.
  EXPECT_EQ(128 + SIGALRM, WaitForInitExit());

  // The 'daemon' process should have only written these lines to its stdout.
  EXPECT_EQ("Begin", Read(out_));
  EXPECT_EQ("", Read(out_));
}

TEST_F(SandboxedInitTest, DISABLED_InitRelaysSigTerm) {
  RunUnderInit([]() {
    // Launcher process starts a 'daemon' process.
    if (daemon(0, 1) < 0)
      PLOG(FATAL) << "Cannot daemon";

    // Signal that the 'daemon' process started.
    Write(STDOUT_FILENO, "Begin");

    // Set SIGTERM handler.
    static bool terminate = false;
    const auto term_handler = [](int sig) { terminate = true; };
    CHECK_NE(SIG_ERR, signal(SIGTERM, term_handler));

    while (!terminate) {
      LOG(INFO) << "Daemon is waiting for a signal...";
      pause();
      LOG(INFO) << "Daemon received a signal";
    }

    // Signal that the 'daemon' process was unblocked.
    LOG(INFO) << "Daemon is finishing...";
    Write(STDOUT_FILENO, "End");
    return 43;
  });

  // The 'launcher' process should terminate first.
  EXPECT_EQ(0, WaitForLauncherExit());

  // Wait for 'daemon' process to start.
  EXPECT_EQ("Begin", Read(out_));

  // The 'init' process should still be there, having adopted the 'daemon'
  // process.
  EXPECT_EQ(-1, WaitForInitExit(true));

  // Send SIGTERM to the 'init' process.
  if (kill(pid_, SIGTERM) < 0)
    PLOG(FATAL) << "Cannot send SIGKILL to 'init' process PID " << pid_;

  // The SIGTERM signal should be relayed by 'init' to the 'daemon' process, and
  // that should gracefully terminate the daemon, and the 'init' process.
  EXPECT_EQ("End", Read(out_));

  // Wait for the 'init' process to finish and relay the 'daemon' process exit
  // code.
  EXPECT_EQ(43, WaitForInitExit());
  EXPECT_EQ("", Read(out_));
}

}  // namespace cros_disks
