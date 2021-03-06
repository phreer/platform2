// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include "missive/storage/resources/resource_interface.h"

#include "base/task/post_task.h"
#include "base/task_runner.h"
#include "base/test/task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::Eq;

namespace reporting {
namespace {

class TestCallbackWaiter {
 public:
  TestCallbackWaiter() = default;
  TestCallbackWaiter(const TestCallbackWaiter& other) = delete;
  TestCallbackWaiter& operator=(const TestCallbackWaiter& other) = delete;

  void Attach() {
    const size_t old_counter = counter_.fetch_add(1);
    DCHECK_GT(old_counter, 0u) << "Cannot attach when already being released";
  }

  void Signal() {
    const size_t old_counter = counter_.fetch_sub(1);
    DCHECK_GT(old_counter, 0u) << "Already being released";
    if (old_counter == 1u) {
      // Dropped last owner.
      run_loop_.Quit();
    }
  }

  void Wait() {
    Signal();  // Rid of the constructor's ownership.
    run_loop_.Run();
  }

 private:
  std::atomic<size_t> counter_{1};  // Owned by constructor.
  base::RunLoop run_loop_;
};

class ResourceInterfaceTest
    : public ::testing::TestWithParam<ResourceInterface*> {
 protected:
  ResourceInterface* resource_interface() const { return GetParam(); }

 private:
  base::test::TaskEnvironment task_environment_;
};

TEST_P(ResourceInterfaceTest, NestedReservationTest) {
  uint64_t size = resource_interface()->GetTotal();
  while ((size / 2) > 0u) {
    size /= 2;
    EXPECT_TRUE(resource_interface()->Reserve(size));
  }

  for (; size < resource_interface()->GetTotal(); size *= 2) {
    resource_interface()->Discard(size);
  }

  EXPECT_THAT(resource_interface()->GetUsed(), Eq(0u));
}

TEST_P(ResourceInterfaceTest, SimultaneousReservationTest) {
  uint64_t size = resource_interface()->GetTotal();

  // Schedule reservations.
  TestCallbackWaiter reserve_waiter;
  while ((size / 2) > 0u) {
    size /= 2;
    reserve_waiter.Attach();
    base::ThreadPool::PostTask(
        FROM_HERE, {base::TaskPriority::BEST_EFFORT},
        base::BindOnce(
            [](size_t size, ResourceInterface* resource_interface,
               TestCallbackWaiter* waiter) {
              EXPECT_TRUE(resource_interface->Reserve(size));
              waiter->Signal();
            },
            size, resource_interface(), &reserve_waiter));
  }
  reserve_waiter.Wait();

  // Schedule discards.
  TestCallbackWaiter discard_waiter;
  for (; size < resource_interface()->GetTotal(); size *= 2) {
    discard_waiter.Attach();
    base::ThreadPool::PostTask(
        FROM_HERE, {base::TaskPriority::BEST_EFFORT},
        base::BindOnce(
            [](size_t size, ResourceInterface* resource_interface,
               TestCallbackWaiter* waiter) {
              resource_interface->Discard(size);
              waiter->Signal();
            },
            size, resource_interface(), &discard_waiter));
  }
  discard_waiter.Wait();

  EXPECT_THAT(resource_interface()->GetUsed(), Eq(0u));
}

TEST_P(ResourceInterfaceTest, SimultaneousScopedReservationTest) {
  uint64_t size = resource_interface()->GetTotal();
  TestCallbackWaiter waiter;
  while ((size / 2) > 0u) {
    size /= 2;
    waiter.Attach();
    base::ThreadPool::PostTask(
        FROM_HERE, {base::TaskPriority::BEST_EFFORT},
        base::BindOnce(
            [](size_t size, ResourceInterface* resource_interface,
               TestCallbackWaiter* waiter) {
              { ScopedReservation(size, resource_interface); }
              waiter->Signal();
            },
            size, resource_interface(), &waiter));
  }
  waiter.Wait();
  EXPECT_THAT(resource_interface()->GetUsed(), Eq(0u));
}

TEST_P(ResourceInterfaceTest, ReservationOverMaxTest) {
  EXPECT_FALSE(
      resource_interface()->Reserve(resource_interface()->GetTotal() + 1));
  EXPECT_TRUE(resource_interface()->Reserve(resource_interface()->GetTotal()));
  resource_interface()->Discard(resource_interface()->GetTotal());
  EXPECT_THAT(resource_interface()->GetUsed(), Eq(0u));
}

INSTANTIATE_TEST_SUITE_P(VariousResources,
                         ResourceInterfaceTest,
                         testing::Values(GetMemoryResource(),
                                         GetDiskResource()));

}  // namespace
}  // namespace reporting
