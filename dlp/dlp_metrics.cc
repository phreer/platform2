// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_metrics.h"

namespace dlp {

DlpMetrics::DlpMetrics() : metrics_lib_(std::make_unique<MetricsLibrary>()) {}
DlpMetrics::~DlpMetrics() = default;

void DlpMetrics::SendBooleanHistogram(const std::string& name,
                                      bool value) const {
  metrics_lib_->SendBoolToUMA(name, value);
}

void DlpMetrics::SendFanotifyError(FanotifyError error) const {
  metrics_lib_->SendEnumToUMA(kDlpFanotifyErrorHistogram, error);
}

};  // namespace dlp
