// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Command-line utility to access to the Chrome OS master configuration from the
// build system. This is not to be used on a Chrome OS device.

#include <iostream>
#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "brillo/flag_helper.h"
#include "chromeos-config/libcros_config/cros_config.h"

int main(int argc, char* argv[]) {
  DEFINE_bool(list_models, false, "Lists all models in the config file.");
  DEFINE_string(model, "", "Optionally specifies which model name to use.")

  std::string usage = "Chrome OS Model Configuration for Host\n\nUsage: " +
      std::string(argv[0]) + " [flags] config_filepath [path] [key]";
  brillo::FlagHelper::Init(argc, argv, usage);

  brillo::CrosConfig cros_config;

  base::CommandLine::StringVector args =
      base::CommandLine::ForCurrentProcess()->GetArgs();

  CHECK_EQ(args.size() >= 3 && FLAGS_model.empty(), false)
      << "Must pass in --model to use [path + key] args.";

  CHECK_EQ(args.size() <= 2 && !FLAGS_list_models, false)
      << "Must pass either --list_models or [path + args].";

  CHECK_EQ(args.size() > 1 && FLAGS_list_models, false)
      << "Cannot pass --list_models and [path + key] at the same time.";

  if (args.size() < 1) {
    std::cerr << usage << "\nPass --help for more information." << std::endl;
    return 1;
  }

  base::FilePath config_filepath = base::FilePath(args[0]);
  if (!cros_config.InitForHost(base::FilePath(config_filepath), FLAGS_model)) {
    return 1;
  }

  if (args.size() == 3) {
    std::string path = args[1];
    std::string property = args[2];

    std::string value;
    if (!cros_config.GetString(path, property, &value)) {
      return 1;
    }
    std::cout << value;
  }

  if (FLAGS_list_models) {
    for (const auto& model : cros_config.GetModelNames()) {
      std::cout << model << std::endl;
    }
  }

  return 0;
}
