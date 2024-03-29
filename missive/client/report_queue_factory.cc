// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/report_queue_factory.h"

#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/strings/string_piece.h>
#include <base/task/bind_post_task.h>
#include <base/task/thread_pool.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/backoff_entry.h>

#include "missive/client/report_queue_configuration.h"
#include "missive/client/report_queue_provider.h"
#include "missive/util/backoff_settings.h"

#define LOG_WITH_STATUS(LEVEL, MESSAGE, STATUS) \
  VLOG(LEVEL) << MESSAGE << " status=" << STATUS.status();

namespace reporting {

// static
void ReportQueueFactory::Create(EventType event_type,
                                Destination destination,
                                SuccessCallback success_cb) {
  DCHECK(base::ThreadTaskRunnerHandle::IsSet());

  auto config_result = ReportQueueConfiguration::Create(
      event_type, destination,
      base::BindRepeating([]() { return Status::StatusOK(); }));
  if (!config_result.ok()) {
    LOG_WITH_STATUS(1, "ReportQueueConfiguration is invalid.", config_result);
    return;
  }

  // Asynchronously create and try to set ReportQueue.
  auto try_set_cb = CreateTrySetCallback(destination, std::move(success_cb),
                                         GetBackoffEntry());
  base::ThreadPool::PostTask(
      FROM_HERE, base::BindOnce(ReportQueueProvider::CreateQueue,
                                std::move(config_result.ValueOrDie()),
                                std::move(try_set_cb)));
}

// static
std::unique_ptr<ReportQueue, base::OnTaskRunnerDeleter>
ReportQueueFactory::CreateSpeculativeReportQueue(EventType event_type,
                                                 Destination destination) {
  DCHECK(base::SequencedTaskRunnerHandle::IsSet());

  auto config_result = ReportQueueConfiguration::Create(
      event_type, destination,
      base::BindRepeating([]() { return Status::StatusOK(); }));

  if (!config_result.ok()) {
    DVLOG(1)
        << "Cannot initialize report queue. Invalid ReportQueueConfiguration: "
        << config_result.status();
    return std::unique_ptr<ReportQueue, base::OnTaskRunnerDeleter>(
        nullptr,
        base::OnTaskRunnerDeleter(base::SequencedTaskRunnerHandle::Get()));
  }

  auto speculative_queue_result = ReportQueueProvider::CreateSpeculativeQueue(
      std::move(config_result.ValueOrDie()));
  if (!speculative_queue_result.ok()) {
    DVLOG(1) << "Failed to create speculative queue: "
             << speculative_queue_result.status();
    return std::unique_ptr<ReportQueue, base::OnTaskRunnerDeleter>(
        nullptr,
        base::OnTaskRunnerDeleter(base::SequencedTaskRunnerHandle::Get()));
  }

  return std::move(speculative_queue_result.ValueOrDie());
}

ReportQueueFactory::TrySetReportQueueCallback
ReportQueueFactory::CreateTrySetCallback(
    Destination destination,
    SuccessCallback success_cb,
    std::unique_ptr<::brillo::BackoffEntry> backoff_entry) {
  return base::BindPostTask(
      base::ThreadTaskRunnerHandle::Get(),
      base::BindOnce(&ReportQueueFactory::TrySetReportQueue,
                     std::move(success_cb)));
}

// static
void ReportQueueFactory::TrySetReportQueue(
    SuccessCallback success_cb,
    StatusOr<std::unique_ptr<ReportQueue>> report_queue_result) {
  if (!report_queue_result.ok()) {
    LOG_WITH_STATUS(1, "ReportQueue could not be created.",
                    report_queue_result);
    return;
  }
  std::move(success_cb).Run(std::move(report_queue_result.ValueOrDie()));
}
}  // namespace reporting
