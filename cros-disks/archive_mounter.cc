// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_mounter.h"

#include <utility>

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <brillo/scoped_mount_namespace.h>
#include <dbus/cros-disks/dbus-constants.h>

#include "cros-disks/quote.h"

namespace cros_disks {

ArchiveMounter::ArchiveMounter(
    const Platform* platform,
    brillo::ProcessReaper* process_reaper,
    std::string archive_type,
    Metrics* metrics,
    std::string metrics_name,
    std::vector<int> password_needed_exit_codes,
    std::unique_ptr<SandboxedProcessFactory> sandbox_factory,
    std::vector<std::string> extra_command_line_options)
    : FUSEMounter(
          platform,
          process_reaper,
          archive_type + "fs",
          {.metrics = metrics,
           .metrics_name = std::move(metrics_name),
           .password_needed_exit_codes = std::move(password_needed_exit_codes),
           .read_only = true}),
      extension_("." + archive_type),
      metrics_(metrics),
      sandbox_factory_(std::move(sandbox_factory)),
      extra_command_line_options_(std::move(extra_command_line_options)) {}

ArchiveMounter::~ArchiveMounter() = default;

bool ArchiveMounter::CanMount(const std::string& source,
                              const std::vector<std::string>& /*params*/,
                              base::FilePath* suggested_dir_name) const {
  base::FilePath path(source);
  if (path.IsAbsolute() && base::CompareCaseInsensitiveASCII(
                               path.FinalExtension(), extension_) == 0) {
    *suggested_dir_name = path.BaseName();
    return true;
  }
  return false;
}

std::unique_ptr<SandboxedProcess> ArchiveMounter::PrepareSandbox(
    const std::string& source,
    const base::FilePath& /*target_path*/,
    std::vector<std::string> params,
    MountErrorType* error) const {
  base::FilePath path(source);

  if (!path.IsAbsolute() || path.ReferencesParent()) {
    LOG(ERROR) << "Invalid archive path " << redact(path);
    *error = MOUNT_ERROR_INVALID_ARGUMENT;
    return nullptr;
  }

  if (metrics_)
    metrics_->RecordArchiveType(path);

  auto sandbox = sandbox_factory_->CreateSandboxedProcess();

  std::unique_ptr<brillo::ScopedMountNamespace> mount_ns;
  if (!platform()->PathExists(path.value())) {
    // Try to locate the file in Chrome's mount namespace.
    mount_ns = brillo::ScopedMountNamespace::CreateFromPath(
        base::FilePath(kChromeNamespace));
    if (!mount_ns) {
      PLOG(ERROR) << "Cannot enter mount namespace " << quote(kChromeNamespace);
      *error = MOUNT_ERROR_INVALID_PATH;
      return nullptr;
    }

    if (!platform()->PathExists(path.value())) {
      PLOG(ERROR) << "Cannot find archive " << redact(path);
      *error = MOUNT_ERROR_INVALID_PATH;
      return nullptr;
    }
  }

  if (base::StartsWith(path.BaseName().value(), "b1238564.")) {
    LOG(INFO) << "Simulating progress for " << quote(path);
    sandbox->SimulateProgressForTesting();
  }

  // Archives are typically under /home, /media or /run. To bind-mount the
  // source those directories must be writable, but by default only /run is.
  for (const char* const dir : {"/home", "/media"}) {
    if (!sandbox->Mount("tmpfs", dir, "tmpfs", "mode=0755,size=1M")) {
      LOG(ERROR) << "Cannot mount " << quote(dir);
      *error = MOUNT_ERROR_INTERNAL;
      return nullptr;
    }
  }

  // Is the process "password-aware"?
  if (AcceptsPassword()) {
    if (std::string password; GetParamValue(params, "password", &password)) {
      sandbox->SetStdIn(password);
    }
  }

  // Bind-mount parts of a multipart archive if any.
  for (const std::string& part : GetBindPaths(path.value())) {
    if (!sandbox->BindMount(part, part, /* writeable= */ false,
                            /* recursive= */ false)) {
      PLOG(ERROR) << "Cannot bind-mount archive " << redact(part);
      *error = MOUNT_ERROR_INTERNAL;
      return nullptr;
    }
  }

  // Prepare command line arguments.
  sandbox->AddArgument("-o");
  sandbox->AddArgument(base::StringPrintf("ro,umask=0222,uid=%d,gid=%d",
                                          kChronosUID, kChronosAccessGID));

  if (std::string encoding; GetParamValue(params, "encoding", &encoding)) {
    sandbox->AddArgument("-o");
    sandbox->AddArgument("encoding=" + encoding);
  }

  for (const auto& opt : extra_command_line_options_)
    sandbox->AddArgument(opt);

  sandbox->AddArgument(path.value());

  if (mount_ns) {
    // Sandbox will need to enter Chrome's namespace too to access files.
    mount_ns.reset();
    sandbox->EnterExistingMountNamespace(kChromeNamespace);
  }

  *error = MOUNT_ERROR_NONE;
  return sandbox;
}

}  // namespace cros_disks
