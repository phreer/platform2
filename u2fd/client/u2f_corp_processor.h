// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_CLIENT_U2F_CORP_PROCESSOR_H_
#define U2FD_CLIENT_U2F_CORP_PROCESSOR_H_

#include <functional>

#include <metrics/metrics_library.h>
#include <session_manager/dbus-proxies.h>

#include "u2fd/client/tpm_vendor_cmd.h"
#include "u2fd/client/u2f_apdu.h"
#include "u2fd/client/u2f_corp_firmware_version.h"

namespace u2f {

class U2fCorpProcessor {
 public:
  U2fCorpProcessor() = default;
  virtual ~U2fCorpProcessor() = default;

  virtual void Initialize(U2fCorpFirmwareVersion fw_version,
                          org::chromium::SessionManagerInterfaceProxy* sm_proxy,
                          TpmVendorCommandProxy* tpm_proxy,
                          MetricsLibraryInterface* metrics,
                          std::function<void()> request_presence) = 0;

  virtual U2fResponseApdu ProcessApdu(const U2fCommandApdu& apdu) = 0;
  virtual void Reset() = 0;
};

}  // namespace u2f

#endif  // U2FD_CLIENT_U2F_CORP_PROCESSOR_H_
