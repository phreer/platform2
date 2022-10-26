// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/error_logger.h"

#include <type_traits>

namespace cros_disks {

#define CROS_DISKS_PRINT(X) \
  case X:                   \
    return out << #X;

std::ostream& operator<<(std::ostream& out, const FormatErrorType error) {
  switch (error) {
    CROS_DISKS_PRINT(FORMAT_ERROR_NONE)
    CROS_DISKS_PRINT(FORMAT_ERROR_UNKNOWN)
    CROS_DISKS_PRINT(FORMAT_ERROR_INTERNAL)
    CROS_DISKS_PRINT(FORMAT_ERROR_INVALID_DEVICE_PATH)
    CROS_DISKS_PRINT(FORMAT_ERROR_DEVICE_BEING_FORMATTED)
    CROS_DISKS_PRINT(FORMAT_ERROR_UNSUPPORTED_FILESYSTEM)
    CROS_DISKS_PRINT(FORMAT_ERROR_FORMAT_PROGRAM_NOT_FOUND)
    CROS_DISKS_PRINT(FORMAT_ERROR_FORMAT_PROGRAM_FAILED)
    CROS_DISKS_PRINT(FORMAT_ERROR_DEVICE_NOT_ALLOWED)
    CROS_DISKS_PRINT(FORMAT_ERROR_INVALID_OPTIONS)
    CROS_DISKS_PRINT(FORMAT_ERROR_LONG_NAME)
    CROS_DISKS_PRINT(FORMAT_ERROR_INVALID_CHARACTER)
  }
  return out << "FORMAT_ERROR_"
             << static_cast<std::underlying_type_t<FormatErrorType>>(error);
}

std::ostream& operator<<(std::ostream& out, const MountErrorType error) {
  switch (error) {
    CROS_DISKS_PRINT(MOUNT_ERROR_NONE)
    CROS_DISKS_PRINT(MOUNT_ERROR_UNKNOWN)
    CROS_DISKS_PRINT(MOUNT_ERROR_INTERNAL)
    CROS_DISKS_PRINT(MOUNT_ERROR_INVALID_ARGUMENT)
    CROS_DISKS_PRINT(MOUNT_ERROR_INVALID_PATH)
    CROS_DISKS_PRINT(MOUNT_ERROR_PATH_ALREADY_MOUNTED)
    CROS_DISKS_PRINT(MOUNT_ERROR_PATH_NOT_MOUNTED)
    CROS_DISKS_PRINT(MOUNT_ERROR_DIRECTORY_CREATION_FAILED)
    CROS_DISKS_PRINT(MOUNT_ERROR_INVALID_MOUNT_OPTIONS)
    CROS_DISKS_PRINT(MOUNT_ERROR_INVALID_UNMOUNT_OPTIONS)
    CROS_DISKS_PRINT(MOUNT_ERROR_INSUFFICIENT_PERMISSIONS)
    CROS_DISKS_PRINT(MOUNT_ERROR_MOUNT_PROGRAM_NOT_FOUND)
    CROS_DISKS_PRINT(MOUNT_ERROR_MOUNT_PROGRAM_FAILED)
    CROS_DISKS_PRINT(MOUNT_ERROR_NEED_PASSWORD)
    CROS_DISKS_PRINT(MOUNT_ERROR_IN_PROGRESS)
    CROS_DISKS_PRINT(MOUNT_ERROR_CANCELLED)
    CROS_DISKS_PRINT(MOUNT_ERROR_BUSY)
    CROS_DISKS_PRINT(MOUNT_ERROR_INVALID_DEVICE_PATH)
    CROS_DISKS_PRINT(MOUNT_ERROR_UNKNOWN_FILESYSTEM)
    CROS_DISKS_PRINT(MOUNT_ERROR_UNSUPPORTED_FILESYSTEM)
    CROS_DISKS_PRINT(MOUNT_ERROR_INVALID_ARCHIVE)
  }
  return out << "MOUNT_ERROR_"
             << static_cast<std::underlying_type_t<MountErrorType>>(error);
}

std::ostream& operator<<(std::ostream& out, const PartitionErrorType error) {
  switch (error) {
    CROS_DISKS_PRINT(PARTITION_ERROR_NONE)
    CROS_DISKS_PRINT(PARTITION_ERROR_UNKNOWN)
    CROS_DISKS_PRINT(PARTITION_ERROR_INTERNAL)
    CROS_DISKS_PRINT(PARTITION_ERROR_INVALID_DEVICE_PATH)
    CROS_DISKS_PRINT(PARTITION_ERROR_DEVICE_BEING_PARTITIONED)
    CROS_DISKS_PRINT(PARTITION_ERROR_PROGRAM_NOT_FOUND)
    CROS_DISKS_PRINT(PARTITION_ERROR_PROGRAM_FAILED)
    CROS_DISKS_PRINT(PARTITION_ERROR_DEVICE_NOT_ALLOWED)
  }
  return out << "PARTITION_ERROR_"
             << static_cast<std::underlying_type_t<PartitionErrorType>>(error);
}

std::ostream& operator<<(std::ostream& out, const RenameErrorType error) {
  switch (error) {
    CROS_DISKS_PRINT(RENAME_ERROR_NONE)
    CROS_DISKS_PRINT(RENAME_ERROR_UNKNOWN)
    CROS_DISKS_PRINT(RENAME_ERROR_INTERNAL)
    CROS_DISKS_PRINT(RENAME_ERROR_INVALID_DEVICE_PATH)
    CROS_DISKS_PRINT(RENAME_ERROR_DEVICE_BEING_RENAMED)
    CROS_DISKS_PRINT(RENAME_ERROR_UNSUPPORTED_FILESYSTEM)
    CROS_DISKS_PRINT(RENAME_ERROR_RENAME_PROGRAM_NOT_FOUND)
    CROS_DISKS_PRINT(RENAME_ERROR_RENAME_PROGRAM_FAILED)
    CROS_DISKS_PRINT(RENAME_ERROR_DEVICE_NOT_ALLOWED)
    CROS_DISKS_PRINT(RENAME_ERROR_LONG_NAME)
    CROS_DISKS_PRINT(RENAME_ERROR_INVALID_CHARACTER)
  }
  return out << "RENAME_ERROR_"
             << static_cast<std::underlying_type_t<RenameErrorType>>(error);
}

}  // namespace cros_disks
