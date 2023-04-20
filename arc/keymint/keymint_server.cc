// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/keymint_server.h"

#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/platform_thread.h>
#include <keymaster/android_keymaster_messages.h>
#include <mojo/keymint.mojom.h>

// The implementations of |arc::mojom::KeyMintServer| methods below have the
// following overall pattern:
//
// * Generate an std::unique_ptr to a KeyMint request data structure from the
//   arguments received from Mojo, usually through the helpers in conversion.h.
//
// * Execute the operation in |backend->keymint()|, posting this task to a
//   background thread. This produces a KeyMint response data structure.
//
// * Post the response to a callback that runs on the original thread (in this
//   case, the Mojo thread where the request started).
//
// * Convert the KeyMint response to the Mojo return values, and run the
//   result callback.
//
namespace arc::keymint {

namespace {

constexpr size_t kOperationTableSize = 16;
// TODO(b/278968783): Add version negotiation for KeyMint.
// KeyMint Message versions are drawn from Android
// Keymaster Messages.
constexpr int32_t kKeyMintMessageVersion = 4;

}  // namespace

KeyMintServer::Backend::Backend()
    : context_(new context::ArcKeyMintContext()),
      keymint_(context_, kOperationTableSize, kKeyMintMessageVersion) {}

KeyMintServer::Backend::~Backend() = default;

KeyMintServer::KeyMintServer()
    : backend_thread_("BackendKeyMintThread"), weak_ptr_factory_(this) {
  CHECK(backend_thread_.Start()) << "Failed to start keymint thread";
}

KeyMintServer::~KeyMintServer() = default;

void KeyMintServer::SetSystemVersion(uint32_t android_version,
                                     uint32_t android_patchlevel) {
  auto task_lambda = [](context::ArcKeyMintContext* context,
                        uint32_t android_version, uint32_t android_patchlevel) {
    // |context| is guaranteed valid here because it's owned
    // by |backend_|, which outlives the |backend_thread_|
    // this runs on.
    context->SetSystemVersion(android_version, android_patchlevel);
  };
  backend_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(task_lambda, backend_.context(),
                                android_version, android_patchlevel));
}

template <typename KmMember, typename KmRequest, typename KmResponse>
void KeyMintServer::RunKeyMintRequest(
    const base::Location& location,
    KmMember member,
    std::unique_ptr<KmRequest> request,
    base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
  auto task_lambda =
      [](const base::Location& location,
         scoped_refptr<base::TaskRunner> original_task_runner,
         ::keymaster::AndroidKeymaster* keymaster, KmMember member,
         std::unique_ptr<KmRequest> request,
         base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
        // Prepare a KeyMint response data structure.
        auto response =
            std::make_unique<KmResponse>(keymaster->message_version());
        // Execute the operation.
        (*keymaster.*member)(*request, response.get());
        // Post |callback| to the |original_task_runner| given |response|.
        original_task_runner->PostTask(
            location, base::BindOnce(std::move(callback), std::move(response)));
      };
  // Post the KeyMint operation to a background thread while capturing the
  // current task runner.
  backend_thread_.task_runner()->PostTask(
      location,
      base::BindOnce(task_lambda, location,
                     base::SingleThreadTaskRunner::GetCurrentDefault(),
                     backend_.keymint(), member, std::move(request),
                     std::move(callback)));
}

void KeyMintServer::DeleteKey(const std::vector<uint8_t>& key_blob,
                              DeleteKeyCallback callback) {
  // Convert input |key_blob| into |km_request|. All data is deep copied to
  // avoid use-after-free.
  auto km_request = std::make_unique<::keymaster::DeleteKeyRequest>(
      backend_.keymint()->message_version());
  km_request->SetKeyMaterial(key_blob.data(), key_blob.size());

  // Call keymint.
  RunKeyMintRequest(
      FROM_HERE, &::keymaster::AndroidKeymaster::DeleteKey,
      std::move(km_request),
      base::BindOnce(
          [](DeleteKeyCallback callback,
             std::unique_ptr<::keymaster::DeleteKeyResponse> km_response) {
            // Run callback.
            std::move(callback).Run(km_response->error);
          },
          std::move(callback)));
}

void KeyMintServer::DeleteAllKeys(DeleteAllKeysCallback callback) {
  // Prepare keymint request.
  auto km_request = std::make_unique<::keymaster::DeleteAllKeysRequest>(
      backend_.keymint()->message_version());

  // Call keymint.
  RunKeyMintRequest(
      FROM_HERE, &::keymaster::AndroidKeymaster::DeleteAllKeys,
      std::move(km_request),
      base::BindOnce(
          [](DeleteAllKeysCallback callback,
             std::unique_ptr<::keymaster::DeleteAllKeysResponse> km_response) {
            // Run callback.
            std::move(callback).Run(km_response->error);
          },
          std::move(callback)));
}

}  // namespace arc::keymint
