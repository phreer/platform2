// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIDIS_TEST_HELPER_H_
#define MIDIS_TEST_HELPER_H_

#include "gmock/gmock.h"

#include <string>

#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>

#include "midis/device_tracker.h"

namespace midis {

base::FilePath CreateFakeDevSndDir(base::FilePath temp_path) {
  // Create the fake dev node file to which we write.
  temp_path = temp_path.Append("dev/snd");
  base::File::Error error;

  if (!CreateDirectoryAndGetError(temp_path, &error)) {
    LOG(ERROR) << "Failed to create dir: " << temp_path.value() << ": "
               << base::File::ErrorToString(error);
    return base::FilePath();
  }

  return temp_path;
}

MATCHER_P2(DeviceMatcher, id, name, "") {
  return (id == UdevHandler::GenerateDeviceId(arg->GetCard(),
                                              arg->GetDeviceNum()) &&
          base::EqualsCaseInsensitiveASCII(arg->GetName(), name));
}

base::FilePath CreateDevNodeFileName(base::FilePath dev_path_base,
                                     uint32_t sys_num, uint32_t dev_num) {
  // Create a fake devnode file
  std::string node_name = base::StringPrintf("midiC%uD%u", sys_num, dev_num);
  return dev_path_base.Append(node_name);
}

}  // namespace midis

#endif  // MIDIS_TEST_HELPER_H_
