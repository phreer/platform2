// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_STORAGE_H_
#define MISSIVE_STORAGE_STORAGE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/containers/flat_map.h>
#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_piece.h>

#include "missive/compression/compression_module.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_queue.h"
#include "missive/storage/storage_uploader_interface.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

// Storage represents the data to be collected, stored persistently and uploaded
// according to the priority.
class Storage : public base::RefCountedThreadSafe<Storage> {
 public:
  // Creates Storage instance, and returns it with the completion callback.
  static void Create(
      const StorageOptions& options,
      UploaderInterface::AsyncStartUploaderCb async_start_upload_cb,
      scoped_refptr<EncryptionModuleInterface> encryption_module,
      scoped_refptr<CompressionModule> compression_module,
      base::OnceCallback<void(StatusOr<scoped_refptr<Storage>>)> completion_cb);

  Storage(const Storage& other) = delete;
  Storage& operator=(const Storage& other) = delete;

  // Wraps and serializes Record (taking ownership of it), encrypts and writes
  // the resulting blob into the Storage (the last file of it) according to the
  // priority with the next sequencing id assigned. If file is going to
  // become too large, it is closed and new file is created.
  void Write(Priority priority,
             Record record,
             base::OnceCallback<void(Status)> completion_cb);

  // Confirms acceptance of the records according to the
  // |sequence_information.priority()| up to
  // |sequence_information.sequencing_id()| (inclusively), if the
  // |sequence_information.generation_id()| matches. All records with sequencing
  // ids <= this one can be removed from the Storage, and can no longer be
  // uploaded. In order to reset to the very first record (seq_id=0)
  // |sequence_information.sequencing_id()| should be set to -1.
  // If |force| is false (which is used in most cases),
  // |sequence_information.sequencing_id()| is only accepted if no higher ids
  // were confirmed before; otherwise it is accepted unconditionally.
  void Confirm(SequenceInformation sequence_information,
               bool force,
               base::OnceCallback<void(Status)> completion_cb);

  // Initiates upload of collected records according to the priority.
  // Called usually for a queue with an infinite or very large upload period.
  // Multiple |Flush| calls can safely run in parallel.
  // Returns error if cannot start upload.
  Status Flush(Priority priority);

  // If the server attached signed encryption key to the response, it needs to
  // be paased here.
  void UpdateEncryptionKey(SignedEncryptionInfo signed_encryption_key);

  // Stores the given |pipeline_id|. Overwrites any data from previous calls.
  // Returns "ok" |Status| if success. Otherwise returns error Status.
  Status StorePipelineId(base::StringPiece pipeline_id);

  // Returns the pipeline ID if possible. Otherwise, returns error Status.
  StatusOr<std::string> GetPipelineId();

 protected:
  virtual ~Storage();

 private:
  friend class base::RefCountedThreadSafe<Storage>;

  // Private bridge class.
  class QueueUploaderInterface;

  // Private helper class for key upload/download to the file system.
  class KeyInStorage;

  // Private helper class for initial key delivery from the server.
  // It can be invoked multiple times in parallel, but will only do
  // one server roundtrip and notify all requestors upon its completion.
  class KeyDelivery;

  // Private helper class for pipeline ID upload/download to the file system.
  class PipelineIdInStorage;

  // Private constructor, to be called by Create factory method only.
  // Queues need to be added afterwards.
  Storage(const StorageOptions& options,
          scoped_refptr<EncryptionModuleInterface> encryption_module,
          scoped_refptr<CompressionModule> compression_module,
          UploaderInterface::AsyncStartUploaderCb async_start_upload_cb);

  // Initializes the object by adding all queues for all priorities.
  // Must be called once and only once after construction.
  // Returns OK or error status, if anything failed to initialize.
  Status Init();

  // Helper method that selects queue by priority. Returns error
  // if priority does not match any queue.
  // Note: queues_ never change after initialization is finished, so there is no
  // need to protect or serialize access to it.
  StatusOr<scoped_refptr<StorageQueue>> GetQueue(Priority priority) const;

  // Immutable options, stored at the time of creation.
  const StorageOptions options_;

  // Encryption module.
  scoped_refptr<EncryptionModuleInterface> encryption_module_;

  // Internal module for initiail key delivery from server.
  std::unique_ptr<KeyDelivery> key_delivery_;

  // Compression module.
  scoped_refptr<CompressionModule> compression_module_;

  // Internal key management module.
  std::unique_ptr<KeyInStorage> key_in_storage_;

  // Map priority->StorageQueue.
  base::flat_map<Priority, scoped_refptr<StorageQueue>> queues_;

  // Upload provider callback.
  const UploaderInterface::AsyncStartUploaderCb async_start_upload_cb_;

  // Internal pipeline ID management module.
  std::unique_ptr<PipelineIdInStorage> pipeline_id_in_storage_;
};

}  // namespace reporting

#endif  // MISSIVE_STORAGE_STORAGE_H_
