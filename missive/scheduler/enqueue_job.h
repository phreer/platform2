// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_SCHEDULER_ENQUEUE_JOB_H_
#define MISSIVE_SCHEDULER_ENQUEUE_JOB_H_

#include <memory>

#include <brillo/dbus/dbus_method_response.h>

#include "missive/proto/interface.pb.h"
#include "missive/scheduler/scheduler.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/util/status.h"

namespace reporting {

class EnqueueJob : public Scheduler::Job {
 public:
  class EnqueueResponseDelegate : public Job::JobResponseDelegate {
   public:
    EnqueueResponseDelegate(
        std::unique_ptr<
            brillo::dbus_utils::DBusMethodResponse<EnqueueRecordResponse>>
            response);

   private:
    friend Scheduler::Job;

    Status Complete() override;
    Status Cancel(Status status) override;

    Status SendResponse(Status status);

    // response_ can only be used once - the logic in Scheduler::Job ensures
    // that only Complete or Cancel are every called once.
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<EnqueueRecordResponse>>
        response_;
  };

  EnqueueJob(scoped_refptr<StorageModuleInterface> storage_module,
             EnqueueRecordRequest request,
             std::unique_ptr<EnqueueResponseDelegate> delegate);

 protected:
  // EnqueueJob::StartImpl expects EnqueueRecordRequest to include a valid file
  // descriptor and the pid of the owner. Permissions of the file descriptor
  // must be set by the owner such that the Missive Daemon can open it.
  // Utilizing a file descriptor allows us to avoid a copy from DBus and then
  // another copy to Missive.
  // The file descriptor **must** point to a memory mapped file and not an
  // actual file, as device and user data cannot be copied to disk without
  // encryption.
  void StartImpl() override;

 private:
  scoped_refptr<StorageModuleInterface> storage_module_;
  const EnqueueRecordRequest request_;
};

}  // namespace reporting

#endif  // MISSIVE_SCHEDULER_ENQUEUE_JOB_H_
