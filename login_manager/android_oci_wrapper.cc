// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/android_oci_wrapper.h"

#include <signal.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "login_manager/system_utils.h"

namespace login_manager {

constexpr char AndroidOciWrapper::kContainerPath[];
constexpr char AndroidOciWrapper::kContainerId[];
constexpr char AndroidOciWrapper::kRootFsPath[];
constexpr char AndroidOciWrapper::kContainerPidName[];
constexpr char AndroidOciWrapper::kRunOciPath[];
constexpr char AndroidOciWrapper::kRunOciKillCommand[];
constexpr char AndroidOciWrapper::kRunOciKillSignal[];
constexpr char AndroidOciWrapper::kRunOciDestroyCommand[];
constexpr char AndroidOciWrapper::kRunAndroidScriptPath[];
constexpr char AndroidOciWrapper::kProcFdPath[];

AndroidOciWrapper::AndroidOciWrapper(SystemUtils* system_utils,
                                     const base::FilePath& containers_directory)
    : container_pid_(0),
      system_utils_(system_utils),
      containers_directory_(containers_directory),
      clean_exit_(false),
      stateful_mode_(StatefulMode::STATEFUL) {
  DCHECK(system_utils_);
  DCHECK(!containers_directory_.empty());
}

AndroidOciWrapper::~AndroidOciWrapper() = default;

bool AndroidOciWrapper::IsManagedJob(pid_t pid) {
  return container_pid_ == pid;
}

void AndroidOciWrapper::HandleExit(const siginfo_t& status) {
  if (!container_pid_)
    return;

  CleanUpContainer();
}

void AndroidOciWrapper::RequestJobExit() {
  if (!container_pid_)
    return;

  clean_exit_ = true;

  if (stateful_mode_ != StatefulMode::STATELESS && RequestTermination())
    return;

  std::vector<std::string> argv = {kRunOciPath, kRunOciKillSignal,
                                   kRunOciKillCommand, kContainerId};

  int exit_code = -1;
  if (!system_utils_->LaunchAndWait(argv, &exit_code)) {
    PLOG(ERROR) << "Failed to run run_oci";
    return;
  }

  if (exit_code) {
    PLOG(ERROR) << "run_oci failed to forcefully shut down container \""
                << kContainerId << "\"";
  }
}

void AndroidOciWrapper::EnsureJobExit(base::TimeDelta timeout) {
  pid_t pid;
  if (GetContainerPID(&pid) && !system_utils_->ProcessIsGone(pid, timeout)) {
    if (system_utils_->kill(pid, -1, SIGKILL))
      PLOG(ERROR) << "Failed to kill container " << pid;
  }

  CleanUpContainer();
}

bool AndroidOciWrapper::StartContainer(const ExitCallback& exit_callback) {
  pid_t pid = system_utils_->fork();

  if (pid < 0) {
    PLOG(ERROR) << "Failed to fork a new process for run_oci";
    return false;
  }

  // This is the child process.
  if (pid == 0) {
    ExecuteRunOciToStartContainer();

    // Child process should never come down to this point, but we can't mimic
    // this behavior in unit tests, so add a return here to make unit tests
    // pass.
    return false;
  }

  // This is the parent process. The child process can't reach this point.
  LOG(INFO) << "run_oci PID: " << pid;

  int status = -1;
  pid_t result =
      system_utils_->Wait(pid, base::TimeDelta::FromSeconds(10), &status);
  if (result != pid) {
    if (result)
      PLOG(ERROR) << "Failed to wait on run_oci exit";
    else
      LOG(ERROR) << "Timed out to wait on run_oci exit";

    // We assume libcontainer won't create a new process group for init process,
    // so we can use run_oci's PID as the PGID to kill all processes in the
    // container because we created a session in run_oci process.
    KillProcessGroup(pid);
    return false;
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status)) {
    LOG(ERROR) << "run_oci failed to launch Android container. WIFEXITED: "
               << WIFEXITED(status) << " WEXITSTATUS: " << WEXITSTATUS(status);
    return false;
  }

  base::FilePath container_pid_path =
      base::FilePath(ContainerManagerInterface::kContainerRunPath)
          .Append(kContainerId)
          .Append(kContainerPidName);

  std::string pid_str;
  if (!system_utils_->ReadFileToString(container_pid_path, &pid_str)) {
    PLOG(ERROR) << "Failed to read container pid file";
    KillProcessGroup(pid);
    return false;
  }
  if (!base::StringToInt(base::TrimWhitespaceASCII(pid_str, base::TRIM_ALL),
                         &container_pid_)) {
    LOG(ERROR) << "Failed to convert \"" << pid_str << "\" to pid";
    KillProcessGroup(pid);
    return false;
  }

  LOG(INFO) << "Container PID: " << container_pid_;

  exit_callback_ = exit_callback;
  clean_exit_ = false;

  return true;
}

bool AndroidOciWrapper::GetRootFsPath(base::FilePath* path_out) const {
  pid_t pid;
  if (!GetContainerPID(&pid))
    return false;

  *path_out = base::FilePath(ContainerManagerInterface::kContainerRunPath)
                  .Append(kContainerId)
                  .Append(kRootFsPath);
  return true;
}

bool AndroidOciWrapper::GetContainerPID(pid_t* pid_out) const {
  if (!container_pid_)
    return false;

  *pid_out = container_pid_;
  return true;
}

void AndroidOciWrapper::SetStatefulMode(StatefulMode mode) {
  stateful_mode_ = mode;
}

void AndroidOciWrapper::ExecuteRunOciToStartContainer() {
  // Clear signal mask.
  if (!system_utils_->ChangeBlockedSignals(SIG_SETMASK, std::vector<int>()))
    PLOG(FATAL) << "Failed to clear blocked signals";

  base::FilePath container_absolute_path =
      containers_directory_.Append(kContainerPath);
  if (system_utils_->chdir(container_absolute_path))
    PLOG(FATAL) << "Failed to change directory";

  // Close all FDs inherited from session manager.
  if (!CloseOpenedFiles())
    PLOG(FATAL) << "Failed to close all fds";

  if (system_utils_->setsid() < 0)
    PLOG(FATAL) << "Failed to create a new session";

  base::FilePath run_android_script_absolute_path =
      containers_directory_.Append(kRunAndroidScriptPath);
  constexpr const char* const args[] = {"run_oci", kContainerId, nullptr};

  // This path is needed by run_android script to run. Container fails to launch
  // by taking out any one of them. Once we get rid of run_android script we
  // should be able to remove this PATH requirement.
  constexpr static const char kPath[] = "PATH=/usr/bin:/bin";
  constexpr const char* const envs[] = {kPath, nullptr};
  if (system_utils_->execve(run_android_script_absolute_path, args, envs))
    PLOG(FATAL) << "Failed to run run_oci";
}

bool AndroidOciWrapper::RequestTermination() {
  // Use run_oci to perform graceful shutdown.
  std::vector<std::string> argv = {kRunOciPath, kRunOciKillCommand,
                                   kContainerId};

  int exit_code = -1;
  if (!system_utils_->LaunchAndWait(argv, &exit_code)) {
    PLOG(ERROR) << "Failed to run run_oci";
    return false;
  } else if (exit_code) {
    LOG(ERROR) << "run_oci failed to gracefully shut down container \""
               << kContainerId << "\"";
    return false;
  }

  return true;
}

void AndroidOciWrapper::CleanUpContainer() {
  pid_t pid;
  if (!GetContainerPID(&pid))
    return;

  // Save temporary values until everything is cleaned up.
  ExitCallback old_callback;

  std::vector<std::string> argv = {kRunOciPath, kRunOciDestroyCommand,
                                   kContainerId};

  int exit_code = -1;
  if (!system_utils_->LaunchAndWait(argv, &exit_code)) {
    PLOG(ERROR) << "Failed to run run_oci";
  } else if (exit_code) {
    LOG(ERROR) << "run_oci failed to clean up resources for \"" << kContainerId
               << "\"";
  }

  std::swap(old_callback, exit_callback_);

  container_pid_ = 0;

  if (!old_callback.is_null())
    old_callback.Run(pid, clean_exit_);
}

bool AndroidOciWrapper::CloseOpenedFiles() {
  std::vector<base::FilePath> files;
  if (!system_utils_->EnumerateFiles(base::FilePath(kProcFdPath),
                                     base::FileEnumerator::FILES, &files)) {
    LOG(ERROR) << "Failed to enumerate files in " << kProcFdPath;
    return false;
  }

  for (const base::FilePath& file : files) {
    std::string name = file.BaseName().MaybeAsASCII();

    int fd;
    if (!base::StringToInt(name, &fd)) {
      LOG(WARNING) << "Skipped unparsable FD \"" << name << "\"";
      continue;
    }

    if (fd <= STDERR_FILENO)
      continue;

    if (system_utils_->close(fd)) {
      PLOG(ERROR) << "Failed to close FD " << fd;
      return false;
    }
  }

  return true;
}

void AndroidOciWrapper::KillProcessGroup(pid_t pgid) {
  CHECK_GT(pgid, 1);

  if (!system_utils_->ProcessGroupIsGone(pgid, base::TimeDelta()) &&
      system_utils_->kill(-pgid, -1, SIGKILL))
    PLOG(ERROR) << "Failed to kill run_oci pgroup";
}

}  // namespace login_manager
