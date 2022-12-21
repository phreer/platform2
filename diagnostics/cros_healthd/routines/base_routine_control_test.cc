// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/json/json_writer.h>
#include <base/test/task_environment.h>
#include <base/values.h>
#include <gmock/gmock-cardinalities.h>
#include <gmock/gmock.h>
#include <gtest/gtest-death-test.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/common/mojo_test_utils.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}

namespace diagnostics {

class RoutineControlImplPeer final : public BaseRoutineControl {
 public:
  explicit RoutineControlImplPeer(
      base::OnceCallback<void(uint32_t error, const std::string& reason)>
          on_exception_)
      : BaseRoutineControl(std::move(on_exception_)) {}
  RoutineControlImplPeer(const RoutineControlImplPeer&) = delete;
  RoutineControlImplPeer& operator=(const RoutineControlImplPeer&) = delete;
  ~RoutineControlImplPeer() override = default;

  int GetObserverSize() { return observers_.size(); }

  void OnStart() override { return; }

  void SetRunningImpl() {
    SetRunningState();
    // Flush observes_ to make sure all commands on observer side has been
    // executed.
    observers_.FlushForTesting();
  }

  void SetWaitingImpl(mojom::RoutineStateWaiting::Reason reason,
                      const std::string& message) {
    SetWaitingState(reason, message);
    // Flush observes_ to make sure all commands on observer side has been
    // executed.
    observers_.FlushForTesting();
  }

  void SetFinishedImpl(bool passed, mojom::RoutineDetailPtr state) {
    SetFinishedState(passed, std::move(state));
    // Flush observes_ to make sure all commands on observer side has been
    // executed.
    observers_.FlushForTesting();
  }

  void SetPercentageImpl(uint8_t percentage) {
    SetPercentage(percentage);
    // Flush observes_ to make sure all commands on observer side has been
    // executed.
    observers_.FlushForTesting();
  }
};

namespace {

using ::testing::_;
using ::testing::AtLeast;
using ::testing::StrictMock;

class BaseRoutineControlTest : public testing::Test {
 public:
  BaseRoutineControlTest() {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  }

 protected:
  base::OnceCallback<void(uint32_t, const std::string&)> ExpectNoException() {
    return base::BindOnce([](uint32_t error, const std::string& reason) {
      EXPECT_TRUE(false) << "No Exception should occur: " << reason;
    });
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
};

class MockObserver : public mojom::RoutineObserver {
 public:
  explicit MockObserver(
      mojo::PendingReceiver<ash::cros_healthd::mojom::RoutineObserver> receiver)
      : receiver_{this /* impl */, std::move(receiver)} {}
  MOCK_METHOD(void,
              OnRoutineStateChange,
              (ash::cros_healthd::mojom::RoutineStatePtr),
              (override));

 private:
  const mojo::Receiver<ash::cros_healthd::mojom::RoutineObserver> receiver_;
};

void ExpectOutput(int8_t expect_percentage,
                  mojom::RoutineStateUnionPtr expect_state,
                  mojom::RoutineStatePtr got_state) {
  EXPECT_EQ(got_state->percentage, expect_percentage);
  EXPECT_EQ(got_state->state_union, expect_state);
  return;
}

// Test that we can successfully call getState.
TEST_F(BaseRoutineControlTest, GetState) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.GetState(base::BindOnce(&ExpectOutput, 0,
                             mojom::RoutineStateUnion::NewInitialized(
                                 mojom::RoutineStateInitialized::New())));
}

// Test that state can successfully set percentage.
TEST_F(BaseRoutineControlTest, SetPercentage) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.SetPercentageImpl(50);
  rc.GetState(base::BindOnce(
      &ExpectOutput, 50,
      mojom::RoutineStateUnion::NewRunning(mojom::RoutineStateRunning::New())));
}

// Test that state will return exception for setting percentage over 100.
TEST_F(BaseRoutineControlTest, SetOver100Percentage) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  EXPECT_DEATH_IF_SUPPORTED(rc.SetPercentageImpl(101), "");
}

// Test that state will return exception for setting percentage that decreased.
TEST_F(BaseRoutineControlTest, SetDecreasingPercentage) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.SetPercentageImpl(50);
  EXPECT_DEATH_IF_SUPPORTED(rc.SetPercentageImpl(40), "");
}

// Test that state will return exception for setting percentage that decreased.
TEST_F(BaseRoutineControlTest, SetPercentageWithoutStart) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  EXPECT_DEATH_IF_SUPPORTED(rc.SetPercentageImpl(50), "");
}

// Test that state can successfully enter running state from start.
TEST_F(BaseRoutineControlTest, EnterRunningState) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.GetState(base::BindOnce(
      &ExpectOutput, 0,
      mojom::RoutineStateUnion::NewRunning(mojom::RoutineStateRunning::New())));
}

// Test that state can enter running from waiting.
TEST_F(BaseRoutineControlTest, EnterRunningStateFromWaiting) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.SetWaitingImpl(ash::cros_healthd::mojom::RoutineStateWaiting::Reason::
                        kWaitingToBeScheduled,
                    "");
  rc.SetRunningImpl();
  rc.GetState(base::BindOnce(
      &ExpectOutput, 0,
      mojom::RoutineStateUnion::NewRunning(mojom::RoutineStateRunning::New())));
}

// Test that state cannot enter running from initialized.
TEST_F(BaseRoutineControlTest, CannotEnterRunningStateWithoutStarting) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  EXPECT_DEATH(rc.SetRunningImpl(), "state");
}

// Test that state cannot enter running from finished.
TEST_F(BaseRoutineControlTest, CannotEnterRunningStateFromFinished) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.SetFinishedImpl(true, nullptr);
  EXPECT_DEATH_IF_SUPPORTED(rc.SetRunningImpl(), "");
}

// Test that state can enter running from running.
TEST_F(BaseRoutineControlTest, EnterRunningStateFromRunning) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.SetRunningImpl();
  rc.SetRunningImpl();
  rc.GetState(base::BindOnce(
      &ExpectOutput, 0,
      mojom::RoutineStateUnion::NewRunning(mojom::RoutineStateRunning::New())));
}

// Test that state can successfully enter waiting from running.
TEST_F(BaseRoutineControlTest, EnterWaitingStateFromRunning) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();

  rc.SetWaitingImpl(ash::cros_healthd::mojom::RoutineStateWaiting::Reason::
                        kWaitingToBeScheduled,
                    "");
  rc.GetState(base::BindOnce(
      &ExpectOutput, 0,
      mojom::RoutineStateUnion::NewWaiting(mojom::RoutineStateWaiting::New(
          mojom::RoutineStateWaiting::Reason::kWaitingToBeScheduled, ""))));
}

// Test that state cannot enter waiting from initialized.
TEST_F(BaseRoutineControlTest, CannotEnterWaitingStateFromInitialized) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  EXPECT_DEATH_IF_SUPPORTED(
      rc.SetWaitingImpl(ash::cros_healthd::mojom::RoutineStateWaiting::Reason::
                            kWaitingToBeScheduled,
                        ""),
      "");
}

// Test that state cannot enter waiting from finished.
TEST_F(BaseRoutineControlTest, CannotEnterWaitingStateFromFinished) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.SetFinishedImpl(true, nullptr);
  EXPECT_DEATH_IF_SUPPORTED(
      rc.SetWaitingImpl(ash::cros_healthd::mojom::RoutineStateWaiting::Reason::
                            kWaitingToBeScheduled,
                        ""),
      "");
}

// Test that state cannot enter waiting from waiting.
TEST_F(BaseRoutineControlTest, CannotEnterWaitingStateFromWaiting) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();

  rc.SetWaitingImpl(ash::cros_healthd::mojom::RoutineStateWaiting::Reason::
                        kWaitingToBeScheduled,
                    "");
  EXPECT_DEATH_IF_SUPPORTED(
      rc.SetWaitingImpl(ash::cros_healthd::mojom::RoutineStateWaiting::Reason::
                            kWaitingToBeScheduled,
                        ""),
      "");
}

// Test that state can successfully enter finished from running.
TEST_F(BaseRoutineControlTest, EnterFinishedStateFromRunning) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.SetRunningImpl();
  rc.SetFinishedImpl(true, nullptr);
  rc.GetState(
      base::BindOnce(&ExpectOutput, 100,
                     mojom::RoutineStateUnion::NewFinished(
                         mojom::RoutineStateFinished::New(true, nullptr))));
}

// Test that state cannot enter finished from initialized.
TEST_F(BaseRoutineControlTest, CannotEnterFinishedStateFromInitialized) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  EXPECT_DEATH_IF_SUPPORTED(rc.SetFinishedImpl(true, nullptr), "");
}

// Test that state cannot enter finished from waiting.
TEST_F(BaseRoutineControlTest, CannotEnterFinishedStateFromWaiting) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();

  rc.SetWaitingImpl(ash::cros_healthd::mojom::RoutineStateWaiting::Reason::
                        kWaitingToBeScheduled,
                    "");
  EXPECT_DEATH_IF_SUPPORTED(rc.SetFinishedImpl(true, nullptr), "");
}

// Test that state cannot enter finished from finished.
TEST_F(BaseRoutineControlTest, CannotEnterFinishedStateFromFinished) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  rc.Start();
  rc.SetFinishedImpl(true, nullptr);
  EXPECT_DEATH_IF_SUPPORTED(rc.SetFinishedImpl(true, nullptr), "");
}

// Test that we can successfully notify one observer.
TEST_F(BaseRoutineControlTest, NotifyOneObserver) {
  auto rc = RoutineControlImplPeer(ExpectNoException());
  mojo::PendingRemote<mojom::RoutineObserver> observer_remote_1;
  auto observer_1 = std::make_unique<StrictMock<MockObserver>>(
      observer_remote_1.InitWithNewPipeAndPassReceiver());
  rc.AddObserver(std::move(observer_remote_1));
  EXPECT_CALL(*observer_1.get(), OnRoutineStateChange(_))
      .Times(AtLeast(1))
      .WillOnce(testing::WithArg<0>(
          testing::Invoke([=](ash::cros_healthd::mojom::RoutineStatePtr state) {
            EXPECT_TRUE(state->state_union->is_running());
          })))
      .WillOnce(testing::WithArg<0>(
          testing::Invoke([=](ash::cros_healthd::mojom::RoutineStatePtr state) {
            EXPECT_TRUE(state->state_union->is_waiting());
          })))
      .WillOnce(testing::WithArg<0>(
          testing::Invoke([=](ash::cros_healthd::mojom::RoutineStatePtr state) {
            EXPECT_TRUE(state->state_union->is_running());
          })))
      .WillOnce(testing::WithArg<0>(
          testing::Invoke([=](ash::cros_healthd::mojom::RoutineStatePtr state) {
            EXPECT_TRUE(state->state_union->is_finished());
          })));
  rc.Start();
  rc.SetWaitingImpl(ash::cros_healthd::mojom::RoutineStateWaiting::Reason::
                        kWaitingToBeScheduled,
                    "");
  rc.SetRunningImpl();
  rc.SetFinishedImpl(true, nullptr);
}

// Test that we can successfully notify multiple observers.
TEST_F(BaseRoutineControlTest, NotifyMultipleObservers) {
  auto rc = RoutineControlImplPeer(ExpectNoException());

  mojo::PendingRemote<mojom::RoutineObserver> observer_remote_1;
  auto observer_1 = std::make_unique<StrictMock<MockObserver>>(
      observer_remote_1.InitWithNewPipeAndPassReceiver());
  rc.AddObserver(std::move(observer_remote_1));

  mojo::PendingRemote<mojom::RoutineObserver> observer_remote_2;
  auto observer_2 = std::make_unique<StrictMock<MockObserver>>(
      observer_remote_2.InitWithNewPipeAndPassReceiver());
  rc.AddObserver(std::move(observer_remote_2));

  mojo::PendingRemote<mojom::RoutineObserver> observer_remote_3;
  auto observer_3 = std::make_unique<StrictMock<MockObserver>>(
      observer_remote_3.InitWithNewPipeAndPassReceiver());
  rc.AddObserver(std::move(observer_remote_3));
  EXPECT_CALL(*observer_1.get(), OnRoutineStateChange(_)).Times(AtLeast(1));
  EXPECT_CALL(*observer_2.get(), OnRoutineStateChange(_)).Times(AtLeast(1));
  EXPECT_CALL(*observer_3.get(), OnRoutineStateChange(_)).Times(AtLeast(1));

  rc.Start();
  rc.SetFinishedImpl(true, nullptr);
}

// Test that we can successfully notify other observers after an observer has
// disconnected.
TEST_F(BaseRoutineControlTest, DisconnectedObserver) {
  auto rc = RoutineControlImplPeer(ExpectNoException());

  mojo::PendingRemote<mojom::RoutineObserver> observer_remote_1;
  auto observer_1 = std::make_unique<StrictMock<MockObserver>>(
      observer_remote_1.InitWithNewPipeAndPassReceiver());
  rc.AddObserver(std::move(observer_remote_1));

  mojo::PendingRemote<mojom::RoutineObserver> observer_remote_2;
  auto observer_2 = std::make_unique<StrictMock<MockObserver>>(
      observer_remote_2.InitWithNewPipeAndPassReceiver());
  rc.AddObserver(std::move(observer_remote_2));

  mojo::PendingRemote<mojom::RoutineObserver> observer_remote_3;
  auto observer_3 = std::make_unique<StrictMock<MockObserver>>(
      observer_remote_3.InitWithNewPipeAndPassReceiver());
  rc.AddObserver(std::move(observer_remote_3));

  EXPECT_CALL(*observer_1.get(), OnRoutineStateChange(_)).Times(AtLeast(1));
  EXPECT_CALL(*observer_2.get(), OnRoutineStateChange(_)).Times(AtLeast(1));
  EXPECT_CALL(*observer_3.get(), OnRoutineStateChange(_)).Times(0);

  // observer disconnected, Remote set should now only notify two observers.
  observer_3.reset();
  rc.Start();
  rc.SetFinishedImpl(true, nullptr);
}

}  // namespace
}  // namespace diagnostics