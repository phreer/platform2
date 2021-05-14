// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/time_metrics.h"

#include <metrics/metrics_library.h>

namespace ml {

namespace {
// UMA histogram range for wall time metrics.
constexpr int kWallTimeMinMicrosec = 1;           // 1 μs
constexpr int kWallTimeMaxMicrosec = 1800000000;  // 30 min
constexpr int kWallTimeBuckets = 100;
}  // namespace

WallTimeMetric::WallTimeMetric(const std::string& name)
    : metric_name_(name), start_time_(base::Time::Now()) {}

WallTimeMetric::~WallTimeMetric() {
  MetricsLibrary().SendToUMA(
      metric_name_, (base::Time::Now() - start_time_).InMicroseconds(),
      kWallTimeMinMicrosec, kWallTimeMaxMicrosec, kWallTimeBuckets);
}

}  // namespace ml
