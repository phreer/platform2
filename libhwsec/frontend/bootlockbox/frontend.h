// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_BOOTLOCKBOX_FRONTEND_H_
#define LIBHWSEC_FRONTEND_BOOTLOCKBOX_FRONTEND_H_

#include <optional>
#include <vector>

#include <base/callback.h>
#include <brillo/secure_blob.h>

#include "libhwsec/backend/storage.h"
#include "libhwsec/frontend/frontend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BootLockboxFrontend : public Frontend {
 public:
  using StorageState = Storage::ReadyState;

  ~BootLockboxFrontend() override = default;

  // Add a callback to wait until the space related functions are ready to use.
  virtual void WaitUntilReady(base::OnceCallback<void(Status)> callback) = 0;

  // Gets the state of bootlockbox space.
  virtual StatusOr<StorageState> GetSpaceState() = 0;

  // Prepares the bootlockbox space.
  virtual Status PrepareSpace(uint32_t size) = 0;

  // Reads the data of bootlockbox space.
  virtual StatusOr<brillo::Blob> LoadSpace() = 0;

  // Writes the data to bootlockbox space.
  virtual Status StoreSpace(const brillo::Blob& blob) = 0;

  // Locks the bootlockbox space.
  virtual Status LockSpace() = 0;

  // Is the bootlockbox space write locked or not.
  virtual StatusOr<bool> IsSpaceWriteLocked() = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_BOOTLOCKBOX_FRONTEND_H_