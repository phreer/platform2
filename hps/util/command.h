// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_UTIL_COMMAND_H_
#define HPS_UTIL_COMMAND_H_

/*
 * Class to automatically register commands.
 */

#include <iostream>
#include <memory>
#include <utility>

#include <string.h>

#include <base/command_line.h>

#include "hps/hps.h"

class Command {
 public:
  Command(const char* name,
          const char* help,
          int (*func)(std::unique_ptr<hps::HPS>,
                      const base::CommandLine::StringVector& args))
      : name_(name), help_(help), func_(func), next_(nullptr) {
    // Add myself to the list of commands.
    this->next_ = list_;
    list_ = this;
  }
  /*
   * Match command and run.
   * Returns exit value.
   */
  static int Execute(const char* cmd,
                     std::unique_ptr<hps::HPS> hps,
                     const base::CommandLine::StringVector& args) {
    for (auto el = list_; el != nullptr; el = el->next_) {
      if (strcmp(el->name_, cmd) == 0) {
        return el->func_(std::move(hps), args);
      }
    }
    ShowHelp();
    return 1;
  }
  static void ShowHelp() {
    std::cerr << "Error - commands are:" << std::endl;
    for (auto el = list_; el != nullptr; el = el->next_) {
      std::cerr << el->help_ << std::endl;
    }
  }

 private:
  const char* name_;
  const char* help_;
  int (*func_)(std::unique_ptr<hps::HPS>,
               const base::CommandLine::StringVector&);
  Command* next_;
  static Command* list_;  // Global head of command list.
};

#endif  // HPS_UTIL_COMMAND_H_
