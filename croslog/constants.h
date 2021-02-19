// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_CONSTANTS_H_
#define CROSLOG_CONSTANTS_H_

#include <array>
#include <string_view>

namespace croslog {

static const char* const kLogSources[] = {
    // Log files from rsyslog:
    // clang-format off
    "/var/log/arc.log",
    "/var/log/boot.log",
    "/var/log/hammerd.log",
    "/var/log/messages",
    "/var/log/net.log",
    "/var/log/secure",
    "/var/log/upstart.log",
    // clang-format on
};

static const char kAuditLogSources[] = "/var/log/audit/audit.log";

static constexpr std::array kLogsToRotate{
    // clang-format off
    std::string_view("/var/log/messages"),
    std::string_view("/var/log/secure"),
    std::string_view("/var/log/net.log"),
    std::string_view("/var/log/faillog"),
    std::string_view("/var/log/session_manager"),
    std::string_view("/var/log/atrus.log"),
    std::string_view("/var/log/tlsdate.log"),
    std::string_view("/var/log/authpolicy.log"),
    std::string_view("/var/log/tpm-firmware-updater.log"),
    std::string_view("/var/log/arc.log"),
    std::string_view("/var/log/recover_duts/recover_duts.log"),
    std::string_view("/var/log/hammerd.log"),
    std::string_view("/var/log/upstart.log"),
    std::string_view("/var/log/typecd.log"),
    std::string_view("/var/log/bluetooth.log"),
    // clang-format on
};

}  // namespace croslog

#endif  // CROSLOG_CONSTANTS_H_
