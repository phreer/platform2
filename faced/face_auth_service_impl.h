// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_FACE_AUTH_SERVICE_IMPL_H_
#define FACED_FACE_AUTH_SERVICE_IMPL_H_

#include <memory>

#include <absl/random/random.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "faced/mojom/faceauth.mojom.h"
#include "faced/session.h"

namespace faced {

// Face Authentication Service implementation.
//
// Creates and manages enrollment and authentication sessions.
class FaceAuthServiceImpl
    : public chromeos::faceauth::mojom::FaceAuthenticationService {
 public:
  using DisconnectionCallback = base::OnceCallback<void()>;

  // FaceAuthServiceImpl constructor.
  //
  // `receiver` is the pending receiver of `FaceAuthenticationService`.
  // `disconnect_handler` is the callback invoked when the receiver is
  // disconnected.
  FaceAuthServiceImpl(mojo::PendingReceiver<FaceAuthenticationService> receiver,
                      DisconnectionCallback disconnect_handler);

  bool has_active_session() { return session_.get() != nullptr; }

  // `FaceAuthenticationService` implementation.
  void CreateEnrollmentSession(
      chromeos::faceauth::mojom::EnrollmentSessionConfigPtr config,
      mojo::PendingReceiver<chromeos::faceauth::mojom::FaceEnrollmentSession>
          receiver,
      mojo::PendingRemote<
          chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate> delegate,
      CreateEnrollmentSessionCallback callback) override;

  void CreateAuthenticationSession(
      chromeos::faceauth::mojom::AuthenticationSessionConfigPtr config,
      mojo::PendingReceiver<
          chromeos::faceauth::mojom::FaceAuthenticationSession> receiver,
      mojo::PendingRemote<
          chromeos::faceauth::mojom::FaceAuthenticationSessionDelegate>
          delegate,
      CreateAuthenticationSessionCallback callback) override;

 private:
  void ClearSession() { session_.reset(); }

  // Handle the disconnection of the receiver.
  void HandleDisconnect(base::OnceClosure callback);

  mojo::Receiver<chromeos::faceauth::mojom::FaceAuthenticationService>
      receiver_;

  absl::BitGen bitgen_;

  std::unique_ptr<SessionInterface> session_;
};

}  // namespace faced

#endif  // FACED_FACE_AUTH_SERVICE_IMPL_H_
