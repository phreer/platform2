// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_UTIL_DISCONNECTABLE_CLIENT_H_
#define MISSIVE_UTIL_DISCONNECTABLE_CLIENT_H_

#include <memory>

#include <base/callback.h>
#include <base/containers/fixed_flat_map.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>
#include <base/thread_annotations.h>

#include "missive/util/status.h"

namespace reporting {

// Client connection class that handles possible disconnections.
// When the service is not available, calls are immediately responded with
// failure, including those started beforewhen the connection was available.
class DisconnectableClient {
 public:
  // Interface to actual connection call.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Makes actual call to the service.
    virtual void DoCall(base::OnceClosure cb) = 0;
    // Processes response received from the service (OK is successful).
    virtual void Respond(Status status) = 0;
  };

  explicit DisconnectableClient(
      scoped_refptr<base::SequencedTaskRunner> task_runner);
  DisconnectableClient(const DisconnectableClient& other) = delete;
  DisconnectableClient& operator=(const DisconnectableClient& other) = delete;
  ~DisconnectableClient();

  // Makes a call. Must be executed on task_runner_.
  void MaybeMakeCall(std::unique_ptr<Delegate> delegate);

  // Sets availability flag of the service.
  void SetAvailability(bool is_available);

  // Returns sequenced task runner for all operations.
  scoped_refptr<base::SequencedTaskRunner> task_runner() const;

 private:
  void CallResponded(uint64_t id);

  // Sequenced task runner - must be first member of the class.
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  // Availability flag.
  bool is_available_ GUARDED_BY_CONTEXT(sequence_checker_){false};

  // Map of delegates indexed by unique ids (all delegates will fail with error
  // Status if service disconnects).
  base::flat_map<uint64_t, std::unique_ptr<Delegate>> outstanding_delegates_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Weak pointer factory - must be last member of the class.
  base::WeakPtrFactory<DisconnectableClient> weak_ptr_factory_{this};
};

}  // namespace reporting

#endif  // MISSIVE_UTIL_DISCONNECTABLE_CLIENT_H_
