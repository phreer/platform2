// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_MIDDLEWARE_OWNER_H_
#define LIBHWSEC_MIDDLEWARE_MIDDLEWARE_OWNER_H_

#include <atomic>
#include <memory>
#include <utility>

#include <absl/base/attributes.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/task_runner.h>
#include <base/threading/thread.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/error/tpm_retry_handler.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/middleware/middleware_derivative.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"

#ifndef BUILD_LIBHWSEC
#error "Don't include this file outside libhwsec!"
#endif

namespace hwsec {

class Middleware;

class HWSEC_EXPORT MiddlewareOwner {
 public:
  // A tag to indicate the backend would be run on the current thread.
  // All CallSync would run immediately in this mode.
  // If the current thread doesn't have any task runner, the CallAsync would
  // check failed.
  struct OnCurrentTaskRunner {};

  friend class Middleware;

  // Constructor for an isolated thread.
  MiddlewareOwner();

  // Constructor for no isolated thread.
  explicit MiddlewareOwner(OnCurrentTaskRunner);

  // Constructor for custom task runner, the task runner cannot be the current
  // thread.
  explicit MiddlewareOwner(scoped_refptr<base::TaskRunner> task_runner);

  // Constructor for custom backend and no isolated thread.
  MiddlewareOwner(std::unique_ptr<Backend> custom_backend, OnCurrentTaskRunner);

  // Constructor for custom backend and task runner, the task runner cannot be
  // the current thread.
  MiddlewareOwner(std::unique_ptr<Backend> custom_backend,
                  scoped_refptr<base::TaskRunner> task_runner);

  virtual ~MiddlewareOwner();

  MiddlewareDerivative Derive();

 private:
  void InitBackend(std::unique_ptr<Backend> custom_backend);
  void FiniBackend();

  std::unique_ptr<base::Thread> background_thread_;

  scoped_refptr<base::TaskRunner> task_runner_;
  std::atomic<base::PlatformThreadId> thread_id_;

  // Use thread_local to ensure the proxy and backend could only be accessed on
  // a thread.
  ABSL_CONST_INIT static inline thread_local std::unique_ptr<Proxy> proxy_;
  ABSL_CONST_INIT static inline thread_local std::unique_ptr<Backend> backend_;

  // Member variables should appear before the WeakPtrFactory, to ensure
  // that any WeakPtrs to Controller are invalidated before its members
  // variable's destructors are executed, rendering them invalid.
  base::WeakPtrFactory<MiddlewareOwner> weak_factory_{this};
};

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_MIDDLEWARE_OWNER_H_
