// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_module.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/containers/span.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>

#include "missive/compression/compression_module.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

StorageModule::StorageModule() = default;

StorageModule::~StorageModule() = default;

void StorageModule::AddRecord(Priority priority,
                              Record record,
                              EnqueueCallback callback) {
  storage_->Write(priority, std::move(record), std::move(callback));
}

void StorageModule::Flush(Priority priority, FlushCallback callback) {
  std::move(callback).Run(storage_->Flush(priority));
}

void StorageModule::ReportSuccess(SequenceInformation sequence_information,
                                  bool force) {
  storage_->Confirm(std::move(sequence_information), force,
                    base::BindOnce([](Status status) {
                      LOG_IF(ERROR, !status.ok())
                          << "Unable to confirm record deletion: " << status;
                    }));
}

void StorageModule::UpdateEncryptionKey(
    SignedEncryptionInfo signed_encryption_key) {
  storage_->UpdateEncryptionKey(std::move(signed_encryption_key));
}

// static
void StorageModule::Create(
    const StorageOptions& options,
    UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
    scoped_refptr<EncryptionModuleInterface> encryption_module,
    scoped_refptr<CompressionModule> compression_module,
    base::OnceCallback<void(StatusOr<scoped_refptr<StorageModuleInterface>>)>
        callback) {
  scoped_refptr<StorageModule> instance =
      // Cannot base::MakeRefCounted, since constructor is protected.
      base::WrapRefCounted(new StorageModule());
  Storage::Create(
      options, async_start_upload_cb, encryption_module, compression_module,
      base::BindOnce(
          [](scoped_refptr<StorageModule> instance,
             base::OnceCallback<void(
                 StatusOr<scoped_refptr<StorageModuleInterface>>)> callback,
             StatusOr<scoped_refptr<Storage>> storage) {
            if (!storage.ok()) {
              std::move(callback).Run(storage.status());
              return;
            }
            instance->storage_ = std::move(storage.ValueOrDie());
            std::move(callback).Run(std::move(instance));
          },
          std::move(instance), std::move(callback)));
}

}  // namespace reporting
