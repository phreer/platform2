// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_utils.h"
#include "diagnostics/cros_healthd/routines/https_latency/https_latency.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;
using testing::Values;
using testing::WithArg;
using testing::WithParamInterface;

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

// POD struct for HttpsLatencyProblemTest.
struct HttpsLatencyProblemTestParams {
  network_diagnostics_ipc::HttpsLatencyProblem problem_enum;
  std::string failure_message;
};

class HttpsLatencyRoutineTest : public testing::Test {
 protected:
  HttpsLatencyRoutineTest() = default;
  HttpsLatencyRoutineTest(const HttpsLatencyRoutineTest&) = delete;
  HttpsLatencyRoutineTest& operator=(const HttpsLatencyRoutineTest&) = delete;

  void SetUp() override {
    routine_ = CreateHttpsLatencyRoutine(network_diagnostics_adapter());
  }

  mojo_ipc::RoutineUpdatePtr RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnionPtr()};
    routine_->Start();
    routine_->PopulateStatusUpdate(&update, true);
    return chromeos::cros_healthd::mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union));
  }

  MockNetworkDiagnosticsAdapter* network_diagnostics_adapter() {
    return mock_context_.network_diagnostics_adapter();
  }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
};

// Test that the HttpsLatency routine can be run successfully.
TEST_F(HttpsLatencyRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpsLatencyRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunHttpsLatencyCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kNoProblem,
            network_diagnostics_ipc::RoutineProblems::NewHttpsLatencyProblems(
                {}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kHttpsLatencyRoutineNoProblemMessage);
}

// Test that the HttpsLatency routine returns an error when it is not
// run.
TEST_F(HttpsLatencyRoutineTest, RoutineError) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpsLatencyRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunHttpsLatencyCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kNotRun,
            network_diagnostics_ipc::RoutineProblems::NewHttpsLatencyProblems(
                {}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun,
                             kHttpsLatencyRoutineNotRunMessage);
}

// Tests that the HttpsLatency routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the HttpsLatencyProblemTestParams POD struct):
// * |problem_enum| - The type of HttpsLatency problem.
// * |failure_message| - Failure message for a problem.
class HttpsLatencyProblemTest
    : public HttpsLatencyRoutineTest,
      public WithParamInterface<HttpsLatencyProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  HttpsLatencyProblemTestParams params() const { return GetParam(); }
};

// Test that the HttpsLatency routine handles the given HTTPS latency problem.
TEST_P(HttpsLatencyProblemTest, HandleHttpsLatencyProblem) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpsLatencyRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunHttpsLatencyCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kProblem,
            network_diagnostics_ipc::RoutineProblems::NewHttpsLatencyProblems(
                {params().problem_enum}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    HttpsLatencyProblemTest,
    Values(
        HttpsLatencyProblemTestParams{
            network_diagnostics_ipc::HttpsLatencyProblem::kFailedDnsResolutions,
            kHttpsLatencyRoutineFailedDnsResolutionsProblemMessage},
        HttpsLatencyProblemTestParams{
            network_diagnostics_ipc::HttpsLatencyProblem::kFailedHttpsRequests,
            kHttpsLatencyRoutineFailedHttpsRequestsProblemMessage},
        HttpsLatencyProblemTestParams{
            network_diagnostics_ipc::HttpsLatencyProblem::kHighLatency,
            kHttpsLatencyRoutineHighLatencyProblemMessage},
        HttpsLatencyProblemTestParams{
            network_diagnostics_ipc::HttpsLatencyProblem::kVeryHighLatency,
            kHttpsLatencyRoutineVeryHighLatencyProblemMessage}));

}  // namespace
}  // namespace diagnostics
