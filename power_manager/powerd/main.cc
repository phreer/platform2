// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gflags/gflags.h>

#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/daemon.h"

#ifndef VCSID
#define VCSID "<not set>"
#endif

DEFINE_string(prefs_dir, power_manager::kReadWritePrefsDir,
              "Directory holding read/write preferences.");
DEFINE_string(default_prefs_dir, power_manager::kReadOnlyPrefsDir,
              "Directory holding read-only default settings.");
DEFINE_string(log_dir, "", "Directory where logs are written.");
DEFINE_string(run_dir, "", "Directory where stateful data is written.");
// This flag is handled by libbase/libchrome's logging library instead of
// directly by powerd, but it is defined here so gflags won't abort after
// seeing an unexpected flag.
DEFINE_string(vmodule, "",
              "Per-module verbose logging levels, e.g. \"foo=1,bar=2\"");

namespace {

// Moves |latest_log_symlink| to |previous_log_symlink| and creates a relative
// symlink at |latest_log_symlink| pointing to |log_file|. All files must be in
// the same directory.
void UpdateLogSymlinks(const base::FilePath& latest_log_symlink,
                       const base::FilePath& previous_log_symlink,
                       const base::FilePath& log_file) {
  CHECK_EQ(latest_log_symlink.DirName().value(), log_file.DirName().value());
  base::DeleteFile(previous_log_symlink, false);
  base::Move(latest_log_symlink, previous_log_symlink);
  if (!base::CreateSymbolicLink(log_file.BaseName(), latest_log_symlink)) {
    LOG(ERROR) << "Unable to create symbolic link from "
               << latest_log_symlink.value() << " to " << log_file.value();
  }
}

std::string GetTimeAsString(time_t utime) {
  struct tm tm;
  CHECK_EQ(localtime_r(&utime, &tm), &tm);
  char str[16];
  CHECK_EQ(strftime(str, sizeof(str), "%Y%m%d-%H%M%S", &tm), 15UL);
  return std::string(str);
}

}  // namespace

int main(int argc, char* argv[]) {
  // gflags rewrites argv; give base::CommandLine first crack at it.
  CommandLine::Init(argc, argv);
  google::SetUsageMessage("powerd, the Chromium OS userspace power manager.");
  google::ParseCommandLineFlags(&argc, &argv, true);
  CHECK(!FLAGS_prefs_dir.empty()) << "--prefs_dir is required";
  CHECK(!FLAGS_log_dir.empty()) << "--log_dir is required";
  CHECK(!FLAGS_run_dir.empty()) << "--run_dir is required";
  CHECK_EQ(argc, 1) << "Unexpected arguments. Try --help";

  const base::FilePath log_file = base::FilePath(FLAGS_log_dir).Append(
      base::StringPrintf("powerd.%s", GetTimeAsString(::time(NULL)).c_str()));
  UpdateLogSymlinks(base::FilePath(FLAGS_log_dir).Append("powerd.LATEST"),
                    base::FilePath(FLAGS_log_dir).Append("powerd.PREVIOUS"),
                    log_file);

  logging::LoggingSettings logging_settings;
  logging_settings.logging_dest = logging::LOG_TO_FILE;
  logging_settings.log_file = log_file.value().c_str();
  logging_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  logging::InitLogging(logging_settings);
  LOG(INFO) << "vcsid " << VCSID;

  base::AtExitManager at_exit_manager;
  base::MessageLoopForIO message_loop;

  // Extra parens to avoid http://en.wikipedia.org/wiki/Most_vexing_parse.
  power_manager::Daemon daemon((base::FilePath(FLAGS_prefs_dir)),
                               (base::FilePath(FLAGS_default_prefs_dir)),
                               (base::FilePath(FLAGS_run_dir)));
  daemon.Init();

  message_loop.Run();
  return 0;
}
