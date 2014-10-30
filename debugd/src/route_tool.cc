// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/route_tool.h"

#include <base/files/file_util.h>
#include <chromeos/process.h>

#include "debugd/src/process_with_output.h"

namespace debugd {

const char* kIpTool = "/bin/ip";

std::vector<std::string> RouteTool::GetRoutes(
    const std::map<std::string, DBus::Variant>& options, DBus::Error* error) {
  std::vector<std::string> result;
  ProcessWithOutput p;
  if (!p.Init())
    return result;
  p.AddArg(kIpTool);
  if (options.count("v6") == 1)
    p.AddArg("-6");
  p.AddArg("r");  // route
  p.AddArg("s");  // show
  if (p.Run())
    return result;
  p.GetOutputLines(&result);
  return result;
}

}  // namespace debugd
