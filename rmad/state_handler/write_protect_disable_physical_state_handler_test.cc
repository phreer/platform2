// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/state_handler/write_protect_disable_physical_state_handler.h"
#include "rmad/utils/mock_crossystem_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::InSequence;
using testing::IsFalse;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class WriteProtectDisablePhysicalStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(bool, SendHardwareWriteProtectSignal, (bool), (const));
  };

  scoped_refptr<WriteProtectDisablePhysicalStateHandler> CreateStateHandler(
      const std::vector<int> wp_status_list) {
    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<StrictMock<MockCrosSystemUtils>>();
    {
      InSequence seq;
      for (int i = 0; i < wp_status_list.size(); ++i) {
        EXPECT_CALL(*mock_crossystem_utils, GetInt(_, _))
            .WillOnce(DoAll(SetArgPointee<1>(wp_status_list[i]), Return(true)));
      }
    }

    auto handler =
        base::MakeRefCounted<WriteProtectDisablePhysicalStateHandler>(
            json_store_, std::move(mock_crossystem_utils));
    auto callback = std::make_unique<base::RepeatingCallback<bool(bool)>>(
        base::BindRepeating(&SignalSender::SendHardwareWriteProtectSignal,
                            base::Unretained(&signal_sender_)));
    handler->RegisterSignalSender(std::move(callback));
    return handler;
  }

 protected:
  StrictMock<SignalSender> signal_sender_;

  // Variables for TaskRunner.
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(WriteProtectDisablePhysicalStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler({0});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_wp_disable_physical(new WriteProtectDisablePhysicalState);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableComplete);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest,
       GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WriteProtectDisablePhysicalState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);
}

TEST_F(WriteProtectDisablePhysicalStateHandlerTest, GetNextStateCase_Wait) {
  auto handler = CreateStateHandler({1, 1, 1, 0});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.set_allocated_wp_disable_physical(new WriteProtectDisablePhysicalState);

  // First call to |mock_crossystem_utils_|, get 1.
  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  bool signal_sent = false;
  EXPECT_CALL(signal_sender_, SendHardwareWriteProtectSignal(IsFalse()))
      .WillOnce(DoAll(Assign(&signal_sent, true), Return(true)));

  // Second call to |mock_crossystem_utils_| during polling, get 1.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_FALSE(signal_sent);
  // Third call to |mock_crossystem_utils_| during polling, get 1.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_FALSE(signal_sent);
  // Forth call to |mock_crossystem_utils_| during polling, get 0.
  task_environment_.FastForwardBy(
      WriteProtectDisablePhysicalStateHandler::kPollInterval);
  EXPECT_TRUE(signal_sent);
}

}  // namespace rmad
