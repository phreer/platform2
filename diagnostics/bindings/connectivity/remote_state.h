// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BINDINGS_CONNECTIVITY_REMOTE_STATE_H_
#define DIAGNOSTICS_BINDINGS_CONNECTIVITY_REMOTE_STATE_H_

#include <memory>

#include <base/callback.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/bindings/connectivity/mojom/state.mojom.h"

namespace chromeos {
namespace cros_healthd {
namespace connectivity {

// RemoteState provides interface to get the remote internal state of
// connectivity test between two context object in each processes.
class RemoteState {
 public:
  RemoteState(const RemoteState&) = delete;
  RemoteState& operator=(const RemoteState&) = delete;
  virtual ~RemoteState() = default;

  static std::unique_ptr<RemoteState> Create(
      mojo::PendingRemote<mojom::State> remote);

 public:
  // Used by TestConsumer to get the LastCallHasNext state. Returns
  // a callback for async call.
  virtual base::OnceCallback<void(base::OnceCallback<void(bool)>)>
  GetLastCallHasNextClosure() = 0;
  // Used by TestConsumer to wait the last function call finished.
  // This is used before each call to a function without callback (no response
  // parameters). For recursive interface checking, the callback will be
  // stacked.
  virtual void WaitLastCall(base::OnceClosure callback) = 0;
  // Used by TestConsumer as the disconnect handler to fulfill the callback of
  // |WaitRemoteLastCall|. When connection error ocurrs (e.g. Interfaces
  // mismatch), the connection will be reset. In this case, the callback of
  // |WaitRemoteLastCall| won't be called. We cannot drop the callback because
  // the |State| interface is still connected. Instead, this function is used to
  // call the remote callback from this side.
  virtual base::OnceClosure GetFulfillLastCallCallbackClosure() = 0;

 protected:
  RemoteState() = default;
};

}  // namespace connectivity
}  // namespace cros_healthd
}  // namespace chromeos

#endif  // DIAGNOSTICS_BINDINGS_CONNECTIVITY_REMOTE_STATE_H_
