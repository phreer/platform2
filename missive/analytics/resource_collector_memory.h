// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_MEMORY_H_
#define MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_MEMORY_H_

#include <ctime>

#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>

#include "missive/analytics/resource_collector.h"
#include "missive/resources/resource_interface.h"

namespace reporting::analytics {

class ResourceCollectorMemory : public ResourceCollector {
 public:
  // |interval| means the same as in |ResourceCollector::ResourceCollector|.
  // |memory_resource| should point to the same |ResourceInterface| object
  // as the one in the |StorageOption| object that is passed to the
  // |Storage| instance.
  ResourceCollectorMemory(base::TimeDelta interval,
                          scoped_refptr<ResourceInterface> memory_resource);
  virtual ~ResourceCollectorMemory();

 private:
  friend class ResourceCollectorMemoryTest;
  FRIEND_TEST(ResourceCollectorMemoryTest, SuccessfullySend);

  // UMA name
  static constexpr char kUmaName[] = "Platform.Missive.MemoryUsage";
  // The max of the storage usage in 0.1MiB that we are collecting: 5MiB (50 *
  // 0.1MiB). This is slightly higher than our limit so we should have a chance
  // to observe abnormal behaviors.
  static constexpr int kUmaMax = 50;

  // Converts bytes to the nearest 0.1MiBs.
  static int ConvertBytesTo0_1Mibs(int bytes);

  // Collects memory usage given by the memory resource management in Missive.
  void Collect() override;
  // Sends used memory size to UMA.
  bool SendMemorySizeToUma(int memory_size);

  // Memory resource interface.
  scoped_refptr<ResourceInterface> memory_resource_;
};

}  // namespace reporting::analytics

#endif  // MISSIVE_ANALYTICS_RESOURCE_COLLECTOR_MEMORY_H_
