// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_

#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

#include "diagnostics/mojom/private/cros_healthd_executor.mojom.h"

#include <string>

namespace diagnostics {

// Provides a convenient way to access the root-level executor.
class ExecutorAdapter {
 public:
  using Executor = chromeos::cros_healthd_executor::mojom::Executor;

  virtual ~ExecutorAdapter() = default;

  // Establishes a Mojo connection with the executor.
  virtual void Connect(mojo::PlatformChannelEndpoint endpoint) = 0;

  // Returns the speed of each of the device's fans.
  virtual void GetFanSpeed(Executor::GetFanSpeedCallback callback) = 0;

  // Returns the wireless interfaces.
  virtual void GetInterfaces(Executor::GetInterfacesCallback callback) = 0;

  // Returns the wireless link information.
  virtual void GetLink(const std::string& interface_name,
                       Executor::GetLinkCallback callback) = 0;

  // Returns the wireless device information.
  virtual void GetInfo(const std::string& interface_name,
                       Executor::GetInfoCallback callback) = 0;

  // Returns the wireless device information.
  virtual void GetScanDump(const std::string& interface_name,
                           Executor::GetScanDumpCallback callback) = 0;

  // Instructs the executor to run the memtester executable.
  virtual void RunMemtester(Executor::RunMemtesterCallback callback) = 0;

  // Asks the executor to kill a memtester executable started by a call to
  // RunMemtester().
  virtual void KillMemtester() = 0;

  // Retrieves the contents of a process-specific I/O file.
  virtual void GetProcessIOContents(
      const pid_t pid, Executor::GetProcessIOContentsCallback callback) = 0;

  // Read MSR register.
  virtual void ReadMsr(const uint32_t msr_reg,
                       Executor::ReadMsrCallback callback) = 0;

  // Get UEFI Secure Boot variable file content
  virtual void GetUEFISecureBootContent(
      Executor::GetUEFISecureBootContentCallback callback) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_ADAPTER_H_
