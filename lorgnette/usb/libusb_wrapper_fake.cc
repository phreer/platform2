// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/usb/libusb_wrapper_fake.h"

#include <base/logging.h>

#include "lorgnette/usb/usb_device.h"

namespace lorgnette {

std::vector<std::unique_ptr<UsbDevice>> LibusbWrapperFake::GetDevices() {
  return {};
}

}  // namespace lorgnette
