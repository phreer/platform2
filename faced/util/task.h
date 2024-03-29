// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_UTIL_TASK_H_
#define FACED_UTIL_TASK_H_

#include <base/location.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include <utility>

#include "base/task/sequenced_task_runner.h"

namespace faced {

// Get the current task's SequencedTaskRunner.
//
// A shorter way of writing `base::SequencedTaskRunnerHandle::Get()`.
inline const scoped_refptr<base::SequencedTaskRunner>& CurrentSequence() {
  return base::SequencedTaskRunnerHandle::Get();
}

// Post a task to the current thread's SequencedTaskRunner.
//
// Invalid to call if there is no current sequence.
inline bool PostToCurrentSequence(
    base::OnceClosure task,
    const base::Location& from_here = base::Location::Current()) {
  return CurrentSequence()->PostTask(from_here, std::move(task));
}

}  // namespace faced

#endif  // FACED_UTIL_TASK_H_
