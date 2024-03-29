// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/run_calibration_state_handler.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/mock_sensor_calibration_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;

namespace {

constexpr char kBaseInstructionName[] =
    "RMAD_CALIBRATION_INSTRUCTION_PLACE_BASE_ON_FLAT_SURFACE";
constexpr char kLidInstructionName[] =
    "RMAD_CALIBRATION_INSTRUCTION_PLACE_LID_ON_FLAT_SURFACE";

constexpr char kBaseAccName[] = "RMAD_COMPONENT_BASE_ACCELEROMETER";
constexpr char kLidAccName[] = "RMAD_COMPONENT_LID_ACCELEROMETER";
constexpr char kBaseGyroName[] = "RMAD_COMPONENT_BASE_GYROSCOPE";
constexpr char kLidGyroName[] = "RMAD_COMPONENT_LID_GYROSCOPE";

constexpr char kStatusWaitingName[] = "RMAD_CALIBRATION_WAITING";
constexpr char kStatusCompleteName[] = "RMAD_CALIBRATION_COMPLETE";
constexpr char kStatusInProgressName[] = "RMAD_CALIBRATION_IN_PROGRESS";
constexpr char kStatusSkipName[] = "RMAD_CALIBRATION_SKIP";
constexpr char kStatusFailedName[] = "RMAD_CALIBRATION_FAILED";
constexpr char kStatusUnknownName[] = "RMAD_CALIBRATION_UNKNOWN";

}  // namespace

namespace rmad {

class RunCalibrationStateHandlerTest : public StateHandlerTest {
 public:
  // Helper classto mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(void,
                SendCalibrationOverallSignal,
                (CalibrationOverallStatus),
                (const));

    MOCK_METHOD(void,
                SendCalibrationProgressSignal,
                (CalibrationComponentStatus),
                (const));
  };

  scoped_refptr<RunCalibrationStateHandler> CreateStateHandler(
      bool base_acc_calibration,
      const std::vector<double>& base_acc_progress,
      bool lid_acc_calibration,
      const std::vector<double>& lid_acc_progress,
      bool base_gyro_calibration,
      const std::vector<double>& base_gyro_progress,
      bool lid_gyro_calibration,
      const std::vector<double>& lid_gyro_progress) {
    // Mock |SensorCalibrationUtils|.
    std::unique_ptr<StrictMock<MockSensorCalibrationUtils>> base_acc_utils =
        std::make_unique<StrictMock<MockSensorCalibrationUtils>>("base", "acc");
    std::unique_ptr<StrictMock<MockSensorCalibrationUtils>> lid_acc_utils =
        std::make_unique<StrictMock<MockSensorCalibrationUtils>>("lid", "acc");
    std::unique_ptr<StrictMock<MockSensorCalibrationUtils>> base_gyro_utils =
        std::make_unique<StrictMock<MockSensorCalibrationUtils>>("base",
                                                                 "gyro");
    std::unique_ptr<StrictMock<MockSensorCalibrationUtils>> lid_gyro_utils =
        std::make_unique<StrictMock<MockSensorCalibrationUtils>>("lid", "gyro");

    {
      InSequence seq;
      EXPECT_CALL(*base_acc_utils, Calibrate())
          .WillRepeatedly(Return(base_acc_calibration));
      for (auto progress : base_acc_progress) {
        EXPECT_CALL(*base_acc_utils, GetProgress(_))
            .WillOnce(DoAll(SetArgPointee<0>(progress), Return(true)));
      }
    }
    {
      InSequence seq;
      EXPECT_CALL(*lid_acc_utils, Calibrate())
          .WillRepeatedly(Return(lid_acc_calibration));
      for (auto progress : lid_acc_progress) {
        EXPECT_CALL(*lid_acc_utils, GetProgress(_))
            .WillOnce(DoAll(SetArgPointee<0>(progress), Return(true)));
      }
    }
    {
      InSequence seq;
      EXPECT_CALL(*base_gyro_utils, Calibrate())
          .WillRepeatedly(Return(base_gyro_calibration));
      for (auto progress : base_gyro_progress) {
        EXPECT_CALL(*base_gyro_utils, GetProgress(_))
            .WillOnce(DoAll(SetArgPointee<0>(progress), Return(true)));
      }
    }
    {
      InSequence seq;
      EXPECT_CALL(*lid_gyro_utils, Calibrate())
          .WillRepeatedly(Return(lid_gyro_calibration));
      for (auto progress : lid_gyro_progress) {
        EXPECT_CALL(*lid_gyro_utils, GetProgress(_))
            .WillOnce(DoAll(SetArgPointee<0>(progress), Return(true)));
      }
    }

    // To PostTask into TaskRunner, we ignore the return value of Calibrate.
    // However, it will cause mock leaks in unittest. We use StrictMock and
    // EXPEXT_CALL to ensure the results, and then we Use AllowLeak to prevent
    // warnings.
    testing::Mock::AllowLeak(base_acc_utils.get());
    testing::Mock::AllowLeak(lid_acc_utils.get());
    testing::Mock::AllowLeak(base_gyro_utils.get());
    testing::Mock::AllowLeak(lid_gyro_utils.get());

    // Register signal callbacks.
    daemon_callback_->SetCalibrationOverallSignalCallback(base::BindRepeating(
        &SignalSender::SendCalibrationOverallSignal,
        base::Unretained(&signal_calibration_overall_sender_)));
    daemon_callback_->SetCalibrationComponentSignalCallback(base::BindRepeating(
        &SignalSender::SendCalibrationProgressSignal,
        base::Unretained(&signal_calibration_component_sender_)));

    return base::MakeRefCounted<RunCalibrationStateHandler>(
        json_store_, daemon_callback_, std::move(base_acc_utils),
        std::move(lid_acc_utils), std::move(base_gyro_utils),
        std::move(lid_gyro_utils));
  }

  void QueueProgress(CalibrationComponentStatus progress) {
    progress_history_.push_back(progress);
  }

  void QueueOverallStatus(CalibrationOverallStatus overall_status) {
    overall_status_history_.push_back(overall_status);
  }

 protected:
  StrictMock<SignalSender> signal_calibration_component_sender_;
  StrictMock<SignalSender> signal_calibration_overall_sender_;

  // Variablesfor TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  void SetUp() override {
    StateHandlerTest::SetUp();
    EXPECT_CALL(signal_calibration_overall_sender_,
                SendCalibrationOverallSignal(_))
        .WillRepeatedly(WithArg<0>(
            Invoke(this, &RunCalibrationStateHandlerTest::QueueOverallStatus)));

    EXPECT_CALL(signal_calibration_component_sender_,
                SendCalibrationProgressSignal(_))
        .WillRepeatedly(WithArg<0>(
            Invoke(this, &RunCalibrationStateHandlerTest::QueueProgress)));
  }

  std::vector<CalibrationComponentStatus> progress_history_;
  std::vector<CalibrationOverallStatus> overall_status_history_;
};

TEST_F(RunCalibrationStateHandlerTest, Cleanup_Success) {
  auto handler = CreateStateHandler(false, {}, false, {}, false, {}, false, {});
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest, InitializeState_Success) {
  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {kBaseInstructionName,
           {{kBaseAccName, kStatusSkipName}, {kBaseGyroName, kStatusSkipName}}},
          {kLidInstructionName,
           {{kLidAccName, kStatusSkipName}, {kLidGyroName, kStatusSkipName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(RunCalibrationStateHandlerTest, InitializeState_NoCalibrationMap) {
  auto handler = CreateStateHandler(false, {}, false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_Success) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kLidInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, true, {0.5, 1.0}, false, {},
                                    true, {0.5, 1.0});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(), RMAD_COMPONENT_LID_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 0.5);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_LID_GYROSCOPE);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {kBaseInstructionName,
           {{kBaseAccName, kStatusCompleteName},
            {kBaseGyroName, kStatusCompleteName}}},
          {kLidInstructionName,
           {{kLidAccName, kStatusInProgressName},
            {kLidGyroName, kStatusInProgressName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 4);
  EXPECT_EQ(progress_history_[2].progress(), 1.0);
  EXPECT_EQ(progress_history_[2].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[2].component(), RMAD_COMPONENT_LID_ACCELEROMETER);
  EXPECT_EQ(progress_history_[3].progress(), 1.0);
  EXPECT_EQ(progress_history_[3].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[3].component(), RMAD_COMPONENT_LID_GYROSCOPE);
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusCompleteName},
                                  {kBaseGyroName, kStatusCompleteName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusCompleteName},
                                  {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0], RMAD_CALIBRATION_OVERALL_COMPLETE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_Success_NoWipeDevice) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, false));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kLidInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, true, {0.5, 1.0}, false, {},
                                    true, {0.5, 1.0});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(), RMAD_COMPONENT_LID_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 0.5);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_LID_GYROSCOPE);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {kBaseInstructionName,
           {{kBaseAccName, kStatusCompleteName},
            {kBaseGyroName, kStatusCompleteName}}},
          {kLidInstructionName,
           {{kLidAccName, kStatusInProgressName},
            {kLidGyroName, kStatusInProgressName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 4);
  EXPECT_EQ(progress_history_[2].progress(), 1.0);
  EXPECT_EQ(progress_history_[2].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[2].component(), RMAD_COMPONENT_LID_ACCELEROMETER);
  EXPECT_EQ(progress_history_[3].progress(), 1.0);
  EXPECT_EQ(progress_history_[3].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[3].component(), RMAD_COMPONENT_LID_GYROSCOPE);
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusCompleteName},
                                  {kBaseGyroName, kStatusCompleteName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusCompleteName},
                                  {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0], RMAD_CALIBRATION_OVERALL_COMPLETE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpEnablePhysical);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_SuccessNeedAnotherRound) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kBaseInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusWaitingName},
                                      {kBaseGyroName, kStatusWaitingName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(true, {0.5, 1.0}, false, {}, true,
                                    {0.5, 1.0}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(),
            RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 0.5);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {kBaseInstructionName,
           {{kBaseAccName, kStatusInProgressName},
            {kBaseGyroName, kStatusInProgressName}}},
          {kLidInstructionName,
           {{kLidAccName, kStatusWaitingName},
            {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 4);
  EXPECT_EQ(progress_history_[2].progress(), 1.0);
  EXPECT_EQ(progress_history_[2].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[2].component(),
            RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[3].progress(), 1.0);
  EXPECT_EQ(progress_history_[3].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[3].component(), RMAD_COMPONENT_BASE_GYROSCOPE);
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));

  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusCompleteName},
                                  {kBaseGyroName, kStatusCompleteName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusWaitingName},
                                  {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NeedCheck_SomethingFailed) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusFailedName},
                                      {kBaseGyroName, kStatusSkipName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusCompleteName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_INITIALIZATION_FAILED);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusFailedName},
                                  {kBaseGyroName, kStatusSkipName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusCompleteName},
                                  {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_NoNeedCalibration) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusSkipName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusCompleteName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(progress_history_.size(), 0);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusCompleteName},
                                  {kBaseGyroName, kStatusSkipName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusCompleteName},
                                  {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0], RMAD_CALIBRATION_OVERALL_COMPLETE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_NoNeedCalibration_NoWipeDevice) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, false));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusSkipName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusCompleteName},
                                      {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(progress_history_.size(), 0);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map = {{kBaseInstructionName,
                                 {{kBaseAccName, kStatusCompleteName},
                                  {kBaseGyroName, kStatusSkipName}}},
                                {kLidInstructionName,
                                 {{kLidAccName, kStatusCompleteName},
                                  {kLidGyroName, kStatusCompleteName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map);

  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0], RMAD_CALIBRATION_OVERALL_COMPLETE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpEnablePhysical);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_MissingState) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kBaseInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusWaitingName},
                                      {kBaseGyroName, kStatusWaitingName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(true, {1.0}, false, {}, true, {1.0}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 1.0);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[0].component(),
            RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 1.0);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  // No RunCalibrationState.
  RmadState state;
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_MissingWipeDeviceVar) {
  // No kWipeDevice set.
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kBaseInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusWaitingName},
                                      {kBaseGyroName, kStatusWaitingName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(true, {1.0}, false, {}, true, {1.0}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 1.0);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[0].component(),
            RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 1.0);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_UnexpectedReboot) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(json_store_->SetValue(kCalibrationInstruction, kLidAccName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusInProgressName},
                                      {kLidGyroName, kStatusInProgressName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
  handler->CleanUpState();

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {kBaseInstructionName,
           {{kBaseAccName, kStatusCompleteName},
            {kBaseGyroName, kStatusCompleteName}}},
          {kLidInstructionName,
           {{kLidAccName, kStatusFailedName},
            {kLidGyroName, kStatusFailedName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_NotFinished) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kBaseInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusWaitingName},
                                      {kBaseGyroName, kStatusWaitingName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(true, {0.5}, false, {}, true, {0.5}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(),
            RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), 0.5);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  std::map<std::string, std::map<std::string, std::string>>
      current_calibration_map;
  EXPECT_TRUE(json_store_->GetValue(kCalibrationMap, &current_calibration_map));
  const std::map<std::string, std::map<std::string, std::string>>
      target_calibration_map_one_interval = {
          {kBaseInstructionName,
           {{kBaseAccName, kStatusInProgressName},
            {kBaseGyroName, kStatusInProgressName}}},
          {kLidInstructionName,
           {{kLidAccName, kStatusWaitingName},
            {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_EQ(current_calibration_map, target_calibration_map_one_interval);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kRunCalibration);
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_SuccessUnknownComponent) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kBaseInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {kBaseInstructionName,
           {{RmadComponent_Name(RMAD_COMPONENT_UNKNOWN), kStatusWaitingName},
            {kBaseGyroName, kStatusWaitingName}}},
          {kLidInstructionName,
           {{kLidAccName, kStatusWaitingName},
            {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(false, {}, false, {}, true, {0.5, 1.0}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 1);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[1].progress(), 1.0);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_SuccessInvalidComponent) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kBaseInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {
          {kBaseInstructionName,
           {{RmadComponent_Name(RMAD_COMPONENT_DRAM), kStatusWaitingName},
            {kBaseGyroName, kStatusWaitingName}}},
          {kLidInstructionName,
           {{kLidAccName, kStatusWaitingName},
            {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(false, {}, false, {}, true, {0.5, 1.0}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 1);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[1].progress(), 1.0);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest, GetNextStateCase_SuccessUnknownStatus) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kBaseInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusUnknownName},
                                      {kBaseGyroName, kStatusWaitingName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(false, {}, false, {}, true, {0.5, 1.0}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 1);
  EXPECT_EQ(progress_history_[0].progress(), 0.5);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_IN_PROGRESS);
  EXPECT_EQ(progress_history_[0].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[1].progress(), 1.0);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_COMPLETE);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_SuccessCalibrationFailed) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kBaseInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusWaitingName},
                                      {kBaseGyroName, kStatusWaitingName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(true, {1.0}, false, {}, false, {-1.0}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 1.0);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[0].component(),
            RMAD_COMPONENT_BASE_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), -1.0);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_FAILED);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_BASE_GYROSCOPE);

  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_FAILED);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kSetupCalibration);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest,
       GetNextStateCase_SuccessCalibrationFailedNoMoreSensors) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kLidInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusWaitingName},
                                      {kLidGyroName, kStatusWaitingName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler =
      CreateStateHandler(true, {}, false, {1.0}, false, {}, false, {-1.0});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  task_environment_.FastForwardBy(RunCalibrationStateHandler::kPollInterval);
  EXPECT_EQ(progress_history_.size(), 2);
  EXPECT_EQ(progress_history_[0].progress(), 1.0);
  EXPECT_EQ(progress_history_[0].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE);
  EXPECT_EQ(progress_history_[0].component(), RMAD_COMPONENT_LID_ACCELEROMETER);
  EXPECT_EQ(progress_history_[1].progress(), -1.0);
  EXPECT_EQ(progress_history_[1].status(),
            CalibrationComponentStatus::RMAD_CALIBRATION_FAILED);
  EXPECT_EQ(progress_history_[1].component(), RMAD_COMPONENT_LID_GYROSCOPE);

  EXPECT_EQ(overall_status_history_.size(), 1);
  EXPECT_EQ(overall_status_history_[0],
            RMAD_CALIBRATION_OVERALL_CURRENT_ROUND_FAILED);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RmadState state = handler->GetState();
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
  handler->CleanUpState();
}

TEST_F(RunCalibrationStateHandlerTest, TryGetNextStateCaseAtBoot_Success) {
  EXPECT_TRUE(json_store_->SetValue(kWipeDevice, true));
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationInstruction, kLidInstructionName));

  const std::map<std::string, std::map<std::string, std::string>>
      predefined_calibration_map = {{kBaseInstructionName,
                                     {{kBaseAccName, kStatusCompleteName},
                                      {kBaseGyroName, kStatusCompleteName}}},
                                    {kLidInstructionName,
                                     {{kLidAccName, kStatusInProgressName},
                                      {kLidGyroName, kStatusInProgressName}}}};
  EXPECT_TRUE(
      json_store_->SetValue(kCalibrationMap, predefined_calibration_map));

  auto handler = CreateStateHandler(false, {}, false, {}, false, {}, false, {});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);
  handler->CleanUpState();
}

}  // namespace rmad
