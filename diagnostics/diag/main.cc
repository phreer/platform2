// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <iostream>
#include <iterator>
#include <map>
#include <string>

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>

#include "diagnostics/diag/diag_actions.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

// Poll interval while waiting for a routine to finish.
constexpr base::TimeDelta kRoutinePollIntervalTimeDelta =
    base::TimeDelta::FromMilliseconds(100);
// Maximum time we're willing to wait for a routine to finish.
constexpr base::TimeDelta kMaximumRoutineExecutionTimeDelta =
    base::TimeDelta::FromSeconds(600);

const struct {
  const char* switch_name;
  mojo_ipc::DiagnosticRoutineEnum routine;
} kDiagnosticRoutineSwitches[] = {
    {"battery_capacity", mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity},
    {"battery_health", mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth},
    {"urandom", mojo_ipc::DiagnosticRoutineEnum::kUrandom},
    {"smartctl_check", mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck},
    {"ac_power", mojo_ipc::DiagnosticRoutineEnum::kAcPower},
    {"cpu_cache", mojo_ipc::DiagnosticRoutineEnum::kCpuCache},
    {"cpu_stress", mojo_ipc::DiagnosticRoutineEnum::kCpuStress},
    {"floating_point_accuracy",
     mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy},
    {"nvme_wear_level", mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel},
    {"nvme_self_test", mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest}};

}  // namespace

// 'diag' command-line tool:
//
// Test driver for libdiag. Only supports running a single diagnostic routine
// at a time.
int main(int argc, char** argv) {
  DEFINE_string(action, "",
                "Action to perform. Options are:\n\tget_routines - retrieve "
                "available routines.\n\trun_routine - run specified routine.");
  DEFINE_string(routine, "",
                "Diagnostic routine to run. For a list of available routines, "
                "run 'diag --action=get_routines'.");
  DEFINE_uint32(low_mah, 1000, "Lower bound for the battery routine, in mAh.");
  DEFINE_uint32(high_mah, 10000,
                "Upper bound for the battery routine, in mAh.");
  DEFINE_uint32(
      maximum_cycle_count, 0,
      "Maximum cycle count allowed for the battery_sysfs routine to pass.");
  DEFINE_uint32(percent_battery_wear_allowed, 100,
                "Maximum percent battery wear allowed for the battery_sysfs "
                "routine to pass.");
  DEFINE_uint32(length_seconds, 10,
                "Number of seconds to run the routine for.");
  DEFINE_bool(ac_power_is_connected, true,
              "Whether or not the AC power routine expects the power supply to "
              "be connected.");
  DEFINE_string(
      expected_power_type, "",
      "Optional type of power supply expected for the AC power routine.");
  DEFINE_uint32(wear_level_threshold, 50,
                "Threshold number in percentage which routine examines "
                "wear level of NVMe against.");
  DEFINE_bool(nvme_self_test_long, false,
              "Long-time period self-test of NVMe would be performed with "
              "this flag being set.");
  brillo::FlagHelper::Init(argc, argv, "diag - Device diagnostic tool.");

  logging::InitLogging(logging::LoggingSettings());

  base::AtExitManager at_exit_manager;

  base::MessageLoopForIO message_loop;

  if (FLAGS_action == "") {
    std::cout << "--action must be specified. Use --help for help on usage."
              << std::endl;
    return EXIT_FAILURE;
  }

  diagnostics::DiagActions actions{kRoutinePollIntervalTimeDelta,
                                   kMaximumRoutineExecutionTimeDelta};

  if (FLAGS_action == "get_routines")
    return actions.ActionGetRoutines() ? EXIT_SUCCESS : EXIT_FAILURE;

  if (FLAGS_action == "run_routine") {
    std::map<std::string, mojo_ipc::DiagnosticRoutineEnum>
        switch_to_diagnostic_routine;
    for (const auto& item : kDiagnosticRoutineSwitches)
      switch_to_diagnostic_routine[item.switch_name] = item.routine;
    auto itr = switch_to_diagnostic_routine.find(FLAGS_routine);
    if (itr == switch_to_diagnostic_routine.end()) {
      std::cout << "Unknown routine: " << FLAGS_routine << std::endl;
      return EXIT_FAILURE;
    }

    bool routine_result;
    switch (itr->second) {
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity:
        routine_result = actions.ActionRunBatteryCapacityRoutine(
            FLAGS_low_mah, FLAGS_high_mah);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth:
        routine_result = actions.ActionRunBatteryHealthRoutine(
            FLAGS_maximum_cycle_count, FLAGS_percent_battery_wear_allowed);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kUrandom:
        routine_result = actions.ActionRunUrandomRoutine(FLAGS_length_seconds);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck:
        routine_result = actions.ActionRunSmartctlCheckRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kAcPower:
        routine_result = actions.ActionRunAcPowerRoutine(
            FLAGS_ac_power_is_connected, FLAGS_expected_power_type);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCpuCache:
        routine_result = actions.ActionRunCpuCacheRoutine(
            base::TimeDelta().FromSeconds(FLAGS_length_seconds));
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCpuStress:
        routine_result = actions.ActionRunCpuStressRoutine(
            base::TimeDelta().FromSeconds(FLAGS_length_seconds));
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy:
        routine_result = actions.ActionRunFloatingPointAccuracyRoutine(
            base::TimeDelta::FromSeconds(FLAGS_length_seconds));
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel:
        routine_result =
            actions.ActionRunNvmeWearLevelRoutine(FLAGS_wear_level_threshold);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest:
        routine_result =
            actions.ActionRunNvmeSelfTestRoutine(FLAGS_nvme_self_test_long);
        break;
      default:
        std::cout << "Unsupported routine: " << FLAGS_routine << std::endl;
        return EXIT_FAILURE;
    }

    return routine_result ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  std::cout << "Unknown action: " << FLAGS_action << std::endl;
  return EXIT_FAILURE;
}
