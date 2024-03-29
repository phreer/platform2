// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOD_CONFIG_H_
#define BIOD_BIOD_CONFIG_H_

#include <optional>
#include <string>

#include <cros_config/cros_config_interface.h>

namespace biod {

extern const char kCrosConfigFPPath[];
extern const char kCrosConfigFPBoard[];
extern const char kCrosConfigFPLocation[];

constexpr char kFpBoardDartmonkey[] = "dartmonkey";
constexpr char kFpBoardNami[] = "nami_fp";
constexpr char kFpBoardNocturne[] = "nocturne_fp";
constexpr char kFpBoardBloonchipper[] = "bloonchipper";

/**
 * @brief Deduce if fingerprint is explicitly not supported.
 *
 * This will only register as unsupported if cros_config explicitly
 * indicates that fingerprint is not supported on the model.
 *
 * @return true if fingerprint is not supported on this platform,
 *         false if fingerprint may be supported on this platform
 */
bool FingerprintUnsupported(brillo::CrosConfigInterface* cros_config);

/**
 * @brief Fetch the fingerprint board name (dartmonkey, bloonchipper, etc).
 *
 * @return no value if cros_config does not report the fingerprint board,
 *         else the fingerprint board as a string
 */
std::optional<std::string> FingerprintBoard(
    brillo::CrosConfigInterface* cros_config);

}  // namespace biod

#endif  // BIOD_BIOD_CONFIG_H_
