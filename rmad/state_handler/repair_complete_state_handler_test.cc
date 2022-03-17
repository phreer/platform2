// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/metrics/mock_metrics_utils.h"
#include "rmad/state_handler/repair_complete_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_power_manager_client.h"
#include "rmad/utils/mock_sys_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;

namespace rmad {

class RepairCompleteStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(void, SendPowerCableStateSignal, (bool), (const));
  };

  scoped_refptr<RepairCompleteStateHandler> CreateStateHandler(
      bool* reboot_called,
      bool* shutdown_called,
      bool* metrics_called,
      bool record_metrics_success = true) {
    auto mock_power_manager_client =
        std::make_unique<NiceMock<MockPowerManagerClient>>();
    if (reboot_called) {
      ON_CALL(*mock_power_manager_client, Restart())
          .WillByDefault(DoAll(Assign(reboot_called, true), Return(true)));
    } else {
      ON_CALL(*mock_power_manager_client, Restart())
          .WillByDefault(Return(true));
    }
    if (shutdown_called) {
      ON_CALL(*mock_power_manager_client, Shutdown())
          .WillByDefault(DoAll(Assign(shutdown_called, true), Return(true)));
    } else {
      ON_CALL(*mock_power_manager_client, Shutdown())
          .WillByDefault(Return(true));
    }
    auto mock_sys_utils = std::make_unique<NiceMock<MockSysUtils>>();
    ON_CALL(*mock_sys_utils, IsPowerSourcePresent())
        .WillByDefault(Return(true));
    auto mock_metrics_utils = std::make_unique<NiceMock<MockMetricsUtils>>();
    ON_CALL(*mock_metrics_utils, Record(_, _))
        .WillByDefault(DoAll(Assign(metrics_called, true),
                             Return(record_metrics_success)));
    auto handler = base::MakeRefCounted<RepairCompleteStateHandler>(
        json_store_, GetTempDirPath(), std::move(mock_power_manager_client),
        std::move(mock_sys_utils), std::move(mock_metrics_utils));
    auto callback =
        base::BindRepeating(&SignalSender::SendPowerCableStateSignal,
                            base::Unretained(&signal_sender_));
    handler->RegisterSignalSender(std::move(callback));

    ON_CALL(signal_sender_, SendPowerCableStateSignal(_))
        .WillByDefault(Return());
    return handler;
  }

  base::FilePath GetPowerwashRequestFilePath() const {
    return GetTempDirPath().AppendASCII(kPowerwashRequestFilePath);
  }

  base::FilePath GetCutoffRequestFilePath() const {
    return GetTempDirPath().AppendASCII(kCutoffRequestFilePath);
  }

  base::FilePath GetDisablePowerwashFilePath() const {
    return GetTempDirPath().AppendASCII(kDisablePowerwashFilePath);
  }

  base::FilePath GetTestDirPath() const {
    return GetTempDirPath().AppendASCII(kTestDirPath);
  }

 protected:
  NiceMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(RepairCompleteStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler(nullptr, nullptr, nullptr);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Override signal sender mock.
  EXPECT_CALL(signal_sender_, SendPowerCableStateSignal(_))
      .WillOnce([](bool is_connected) {
        EXPECT_TRUE(is_connected);
      });
  task_environment_.FastForwardBy(
      RepairCompleteStateHandler::kReportPowerCableInterval);
}

TEST_F(RepairCompleteStateHandlerTest,
       GetNextStateCase_DifferentOwner_Powerwash) {
  bool reboot_called = false;
  auto handler = CreateStateHandler(&reboot_called, nullptr, nullptr);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to different owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, false));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_BATTERY_CUTOFF);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);

  // Check that powerwash is requested.
  bool powerwash_request;
  EXPECT_TRUE(json_store_->GetValue(kPowerwashRequest, &powerwash_request));
  EXPECT_TRUE(powerwash_request);
  EXPECT_TRUE(base::PathExists(GetPowerwashRequestFilePath()));

  // Reboot is called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_TRUE(reboot_called);
}

TEST_F(RepairCompleteStateHandlerTest,
       GetNextStateCase_DifferentOwner_PowerwashDisabledManually) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler =
      CreateStateHandler(&reboot_called, &shutdown_called, &metrics_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to different owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, false));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));

  // Powerwash disabled manually.
  brillo::TouchFile(GetDisablePowerwashFilePath());

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_REBOOT);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Reboot is called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest,
       GetNextStateCase_DifferentOwner_PowerwashDisabledInTestMode) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler =
      CreateStateHandler(&reboot_called, &shutdown_called, &metrics_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to different owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, false));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));

  // Powerwash disabled in test mode.
  brillo::TouchFile(GetTestDirPath());

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_REBOOT);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Reboot is called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_DifferentOwner_Reboot) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler =
      CreateStateHandler(&reboot_called, &shutdown_called, &metrics_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to different owner, and powerwash is done.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, false));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, true));

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_REBOOT);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Reboot is called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest,
       GetNextStateCase_DifferentOwner_Shutdown) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler =
      CreateStateHandler(&reboot_called, &shutdown_called, &metrics_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to different owner, and powerwash is done.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, false));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, true));

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_SHUTDOWN);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Shutdown is called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_FALSE(reboot_called);
  EXPECT_TRUE(shutdown_called);
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_SameOwner_Cutoff) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler =
      CreateStateHandler(&reboot_called, &shutdown_called, &metrics_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to same owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, true));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_BATTERY_CUTOFF);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Cutoff and reboot are called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_SameOwner_Reboot) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler =
      CreateStateHandler(&reboot_called, &shutdown_called, &metrics_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to same owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, true));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_REBOOT);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_REBOOT);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Reboot is called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_SameOwner_Shutdown) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler =
      CreateStateHandler(&reboot_called, &shutdown_called, &metrics_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to same owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, true));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_SHUTDOWN);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Shutdown is called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_FALSE(reboot_called);
  EXPECT_TRUE(shutdown_called);
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_Success_Cutoff) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler =
      CreateStateHandler(&reboot_called, &shutdown_called, &metrics_called);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to same owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, true));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_BATTERY_CUTOFF);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Cutoff and reboot are called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(nullptr, nullptr, nullptr);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No RepairCompleteState.
  RmadState state;

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);

  // Check that the state file still exists.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler(nullptr, nullptr, nullptr);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_UNKNOWN);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);

  // Check that the state file still exists.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_MetricsFailed) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler = CreateStateHandler(&reboot_called, &shutdown_called,
                                    &metrics_called, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to same owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, true));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_BATTERY_CUTOFF);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  // Structured metrics recording is expected to fail as current library does
  // not support recording locally without user consent. We shouldn't let it
  // block the flow until the library actually supports it.
  EXPECT_EQ(error, RMAD_ERROR_EXPECT_SHUTDOWN);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Check that the state file is cleared.
  EXPECT_FALSE(base::PathExists(GetStateFilePath()));

  // Cutoff and reboot are called after a delay.
  task_environment_.FastForwardBy(RepairCompleteStateHandler::kShutdownDelay);
  EXPECT_TRUE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(base::PathExists(GetCutoffRequestFilePath()));
}

TEST_F(RepairCompleteStateHandlerTest, GetNextStateCase_JsonFailed) {
  bool reboot_called = false, shutdown_called = false, metrics_called = false;
  auto handler = CreateStateHandler(&reboot_called, &shutdown_called,
                                    &metrics_called, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Returning to same owner, and powerwash is not done yet.
  EXPECT_TRUE(json_store_->SetValue(kSameOwner, true));
  EXPECT_TRUE(json_store_->SetValue(kPowerwashRequest, false));

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(GetStateFilePath()));

  auto repair_complete = std::make_unique<RepairCompleteState>();
  repair_complete->set_shutdown(
      RepairCompleteState::RMAD_REPAIR_COMPLETE_BATTERY_CUTOFF);
  RmadState state;
  state.set_allocated_repair_complete(repair_complete.release());

  base::SetPosixFilePermissions(GetStateFilePath(), 0444);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kRepairComplete);
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_TRUE(metrics_called);
  EXPECT_FALSE(base::PathExists(GetPowerwashRequestFilePath()));
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));

  // Stop the polling timer.
  handler->CleanUpState();

  // Check that the power cycle won't be called if the state file cannot be
  // cleared.
  task_environment_.FastForwardUntilNoTasksRemain();
  EXPECT_FALSE(reboot_called);
  EXPECT_FALSE(shutdown_called);
  EXPECT_FALSE(base::PathExists(GetCutoffRequestFilePath()));
}

}  // namespace rmad
