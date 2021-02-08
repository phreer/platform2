// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arcvm_native_collector.h"

#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>

#include "crash-reporter/arc_util.h"
#include "crash-reporter/constants.h"
#include "crash-reporter/util.h"

namespace {

constexpr char kArcvmNativeCollectorName[] = "ARCVM_native";
constexpr char kArcvmNativeCrashType[] = "native_crash";

}  // namespace

ArcvmNativeCollector::ArcvmNativeCollector()
    : CrashCollector(kArcvmNativeCollectorName,
                     kAlwaysUseUserCrashDirectory,
                     kNormalCrashSendMode) {}

ArcvmNativeCollector::~ArcvmNativeCollector() = default;

bool ArcvmNativeCollector::HandleCrash(
    const arc_util::BuildProperty& build_property,
    const CrashInfo& crash_info) {
  return HandleCrashWithMinidumpFD(build_property, crash_info,
                                   base::ScopedFD(STDIN_FILENO));
}

bool ArcvmNativeCollector::HandleCrashWithMinidumpFD(
    const arc_util::BuildProperty& build_property,
    const CrashInfo& crash_info,
    base::ScopedFD minidump_fd) {
  const std::string message =
      "Received crash notification for " + crash_info.exec_name;
  LogCrash(message, "handling");

  bool out_of_capacity = false;
  base::FilePath crash_dir;
  if (!GetCreatedCrashDirectoryByEuid(geteuid(), &crash_dir,
                                      &out_of_capacity)) {
    LOG(ERROR) << "Failed to create or find crash directory";
    if (!out_of_capacity)
      EnqueueCollectionErrorLog(kErrorSystemIssue, crash_info.exec_name);
    return false;
  }

  AddArcMetadata(build_property, crash_info);

  const std::string basename_without_ext =
      FormatDumpBasename(crash_info.exec_name, crash_info.time, crash_info.pid);
  const base::FilePath minidump_path = GetCrashPath(
      crash_dir, basename_without_ext, constants::kMinidumpExtension);
  if (!DumpFdToFile(std::move(minidump_fd), minidump_path)) {
    LOG(ERROR) << "Failed to write minidump file";
    return false;
  }

  const base::FilePath metadata_path =
      GetCrashPath(crash_dir, basename_without_ext, "meta");
  FinishCrash(metadata_path, crash_info.exec_name,
              minidump_path.BaseName().value());

  return true;
}

void ArcvmNativeCollector::AddArcMetadata(
    const arc_util::BuildProperty& build_property,
    const CrashInfo& crash_info) {
  AddCrashMetaUploadData(arc_util::kProductField, arc_util::kArcProduct);
  AddCrashMetaUploadData(arc_util::kProcessField, crash_info.exec_name);
  AddCrashMetaUploadData(arc_util::kCrashTypeField, kArcvmNativeCrashType);
  AddCrashMetaUploadData(arc_util::kChromeOsVersionField, GetOsVersion());
  for (auto metadata : arc_util::ListMetadataForBuildProperty(build_property)) {
    AddCrashMetaUploadData(metadata.first, metadata.second);
  }
}

bool ArcvmNativeCollector::DumpFdToFile(base::ScopedFD src_fd,
                                        const base::FilePath& dst_path) {
  base::ScopedFD dst_fd = GetNewFileHandle(dst_path);
  if (!dst_fd.is_valid())
    return false;

  base::File src_file(src_fd.release());
  constexpr size_t kBufferSize = 4096;
  char buffer[kBufferSize];

  while (true) {
    ssize_t bytes_written =
        src_file.ReadAtCurrentPosNoBestEffort(buffer, kBufferSize);
    if (bytes_written < 0)
      return false;
    if (bytes_written == 0)
      return true;
    if (!base::WriteFileDescriptor(dst_fd.get(), buffer, bytes_written))
      return false;
  }
}

std::string ArcvmNativeCollector::GetProductVersion() const {
  std::string version;
  return arc_util::GetChromeVersion(&version) ? version : kUnknownValue;
}
