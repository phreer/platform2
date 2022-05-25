// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_GPU_H_
#define RUNTIME_PROBE_FUNCTIONS_GPU_H_

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Probe gpu components.
class GpuFunction final : public PrivilegedProbeFunction {
  using PrivilegedProbeFunction::PrivilegedProbeFunction;

 public:
  NAME_PROBE_FUNCTION("gpu");

 private:
  // PrivilegedProbeFunction overrides.
  DataType EvalImpl() const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_GPU_H_
