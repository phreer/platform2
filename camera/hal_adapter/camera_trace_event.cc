/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_trace_event.h"

#include <iostream>

#include <base/notreached.h>

namespace cros {

perfetto::Track GetTraceTrack(HalAdapterTraceEvent event,
                              int primary_id,
                              int secondary_id) {
  auto uuid = (static_cast<uint64_t>(primary_id) << 32) +
              (static_cast<uint64_t>(secondary_id & 0xFFFF) << 16) +
              static_cast<uint64_t>(event);
  return perfetto::Track(uuid);
}

perfetto::StaticString ToString(HalAdapterTraceEvent event) {
  switch (event) {
    case HalAdapterTraceEvent::kCapture:
      return "capture";
    default:
      NOTREACHED() << "Unexpected camera trace event: "
                   << static_cast<int>(event);
  }
}

}  // namespace cros
