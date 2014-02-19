// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/hardware.h"

#include <base/file_util.h>
#include <base/logging.h>
#include <base/string_util.h>
#include <rootdev/rootdev.h>
#include <vboot/crossystem.h>

extern "C" {
#include "vboot/vboot_host.h"
}

// We don't use these variables, but libcgpt needs them defined to link.
// TODO(dgarrett) chromium:318536
const char* progname = "";
const char* command = "";
void (*uuid_generator)(uint8_t* buffer) = NULL;

#include "update_engine/subprocess.h"
#include "update_engine/utils.h"

using std::string;
using std::vector;

namespace chromeos_update_engine {

const string Hardware::BootKernelDevice() {
  return utils::KernelDeviceOfBootDevice(Hardware::BootDevice());
}

const string Hardware::BootDevice() {
  char boot_path[PATH_MAX];
  // Resolve the boot device path fully, including dereferencing
  // through dm-verity.
  int ret = rootdev(boot_path, sizeof(boot_path), true, false);

  if (ret < 0) {
    LOG(ERROR) << "rootdev failed to find the root device";
    return "";
  }
  LOG_IF(WARNING, ret > 0) << "rootdev found a device name with no device node";

  // This local variable is used to construct the return string and is not
  // passed around after use.
  return boot_path;
}

bool Hardware::IsKernelBootable(const std::string& kernel_device,
                                bool* bootable) {
  CgptAddParams params;
  memset(&params, '\0', sizeof(params));

  string root_dev = utils::RootDevice(kernel_device);
  string partition_number_str = utils::PartitionNumber(kernel_device);
  uint32_t partition_number = atoi(partition_number_str.c_str());

  params.drive_name = const_cast<char *>(root_dev.c_str());
  params.partition = partition_number;

  int retval = CgptGetPartitionDetails(&params);
  if (retval != CGPT_OK)
    return false;

  *bootable = params.successful || (params.tries > 0);
  return true;
}

bool Hardware::MarkKernelUnbootable(const std::string& kernel_device) {
  LOG(INFO) << "MarkPartitionUnbootable: " << kernel_device;

  if (kernel_device == BootKernelDevice()) {
    LOG(ERROR) << "Refusing to mark current kernel as unbootable.";
    return false;
  }

  string root_dev = utils::RootDevice(kernel_device);
  string partition_number_str = utils::PartitionNumber(kernel_device);
  uint32_t partition_number = atoi(partition_number_str.c_str());

  CgptAddParams params;
  memset(&params, 0, sizeof(params));

  params.drive_name = const_cast<char *>(root_dev.c_str());
  params.partition = partition_number;

  params.successful = false;
  params.set_successful = true;

  params.tries = 0;
  params.set_tries = true;

  int retval = CgptSetAttributes(&params);
  if (retval != CGPT_OK) {
    LOG(ERROR) << "Marking kernel unbootable failed.";
    return false;
  }

  return true;
}

bool Hardware::IsOfficialBuild() {
  return VbGetSystemPropertyInt("debug_build") == 0;
}

bool Hardware::IsNormalBootMode() {
  bool dev_mode = VbGetSystemPropertyInt("devsw_boot") != 0;
  LOG_IF(INFO, dev_mode) << "Booted in dev mode.";
  return !dev_mode;
}

static string ReadValueFromCrosSystem(const string& key) {
  char value_buffer[VB_MAX_STRING_PROPERTY];

  const char *rv = VbGetSystemPropertyString(key.c_str(), value_buffer,
                                             sizeof(value_buffer));
  if (rv != NULL) {
    string return_value(value_buffer);
    TrimWhitespaceASCII(return_value, TRIM_ALL, &return_value);
    return return_value;
  }

  LOG(ERROR) << "Unable to read crossystem key " << key;
  return "";
}

string Hardware::GetHardwareClass() {
  return ReadValueFromCrosSystem("hwid");
}

string Hardware::GetFirmwareVersion() {
  return ReadValueFromCrosSystem("fwid");
}

string Hardware::GetECVersion() {
  string input_line;
  int exit_code = 0;
  vector<string> cmd(1, "/usr/sbin/mosys");
  cmd.push_back("-k");
  cmd.push_back("ec");
  cmd.push_back("info");

  bool success = Subprocess::SynchronousExec(cmd, &exit_code, &input_line);
  if (!success || exit_code) {
    LOG(ERROR) << "Unable to read ec info from mosys (" << exit_code << ")";
    return "";
  }

  return utils::ParseECVersion(input_line);
}

}  // namespace chromeos_update_engine
