// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/syslog_logging.h>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/threading/thread.h>

#include "arc/vm/vsock_proxy/server_proxy.h"

int main(int argc, char** argv) {
  // Initialize CommandLine for VLOG before InitLog.
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader |
                  brillo::kLogToStderrIfTty);

  if (argc < 2) {
    LOG(ERROR) << "Mount path is not specified.";
    return 1;
  }
  base::MessageLoopForIO message_loop_;
  base::FileDescriptorWatcher watcher_{&message_loop_};

  base::Thread proxy_file_system_thread{"ProxyFileSystem"};
  base::Thread::Options options;
  options.message_loop_type = base::MessageLoop::TYPE_IO;
  if (!proxy_file_system_thread.StartWithOptions(options)) {
    LOG(ERROR) << "Failed to start ProxyFileSystem thread.";
    return 1;
  }

  base::RunLoop run_loop;
  arc::ServerProxy server_proxy(proxy_file_system_thread.task_runner(),
                                base::FilePath(argv[1]),
                                run_loop.QuitClosure());
  if (!server_proxy.Initialize()) {
    LOG(ERROR) << "Failed to initialize ServerProxy.";
    return 1;
  }
  run_loop.Run();
  return 0;
}
