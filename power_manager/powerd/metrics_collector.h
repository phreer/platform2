// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_METRICS_COLLECTOR_H_
#define POWER_MANAGER_POWERD_METRICS_COLLECTOR_H_

#include <stdint.h>

#include <string>
#include <vector>

#include <base/compiler_specific.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <gtest/gtest_prod.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/metrics_constants.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/suspender.h"
#include "power_manager/powerd/system/power_supply.h"
#include "privacy_screen/proto_bindings/privacy_screen.pb.h"

namespace power_manager {

class PrefsInterface;

namespace policy {
class BacklightController;
}

namespace metrics {

// Used by Daemon to report metrics by way of Chrome.
//
// This class handles the reporting of complex metrics (e.g. tracking the
// session start time and reporting related metrics after the session stops).
//
// Classes that just need to report simple metrics in response to an event
// should use the convenience functions declared in common/metrics_sender.h to
// send metrics directly.
class MetricsCollector {
 public:
  // Path to S0ix residency counter for big-core CPU. This counter indicates the
  // time spent by the cpus in S0ix (in microseconds).
  static constexpr char kBigCoreS0ixResidencyPath[] =
      "/sys/kernel/debug/pmc_core/slp_s0_residency_usec";
  // Path to S0ix residency counter for small-core CPU. This counter indicates
  // the time spent by the cpus in S0ix (in microseconds).
  static constexpr char kSmallCoreS0ixResidencyPath[] =
      "/sys/kernel/debug/telemetry/s0ix_residency_usec";
  // Expected overhead time to enter/exit S0ix after suspending. This is just an
  // approximation to prevent aggressive warnings.
  static constexpr base::TimeDelta KS0ixOverheadTime = base::Seconds(15);

  // Returns a copy of |enum_name| with a suffix describing |power_source|
  // appended to it. Public so it can be called by tests.
  static std::string AppendPowerSourceToEnumName(const std::string& enum_name,
                                                 PowerSource power_source);

  // Returns a copy of |enum_name| with a suffix describing privacy screen state
  // |state| appended to it. Public so it can be called by tests.
  static std::string AppendPrivacyScreenStateToEnumName(
      const std::string& enum_name,
      const privacy_screen::PrivacyScreenSetting_PrivacyScreenState& state);

  // Calculates the S0ix residency percentage that should be reported
  // as part of UMA metrics by MetricsCollector.
  static int GetExpectedS0ixResidencyPercent(
      const base::TimeDelta& suspend_time,
      const base::TimeDelta& actual_residency);

  MetricsCollector();
  MetricsCollector(const MetricsCollector&) = delete;
  MetricsCollector& operator=(const MetricsCollector&) = delete;

  ~MetricsCollector();

  // Initializes the object and starts |generate_backlight_metrics_timer_|.
  // Ownership of pointers remains with the caller.
  void Init(PrefsInterface* prefs,
            policy::BacklightController* display_backlight_controller,
            policy::BacklightController* keyboard_backlight_controller,
            const system::PowerStatus& power_status,
            bool first_run_after_boot);

  // Records changes to system state.
  void HandleScreenDimmedChange(bool dimmed,
                                base::TimeTicks last_user_activity_time);
  void HandleScreenOffChange(bool off, base::TimeTicks last_user_activity_time);
  void HandleSessionStateChange(SessionState state);
  void HandlePowerStatusUpdate(const system::PowerStatus& status);
  void HandleShutdown(ShutdownReason reason);
  void HandlePrivacyScreenStateChange(
      const privacy_screen::PrivacyScreenSetting_PrivacyScreenState& state);

  // Called at the beginning of a suspend request (which may consist of multiple
  // suspend attempts).
  void PrepareForSuspend();

  // Called at the end of a successful suspend request. |num_suspend_attempts|
  // contains the number of attempts up to and including the one in which the
  // system successfully suspended. |hibernated| indicates whether or not the
  // system suspended to disk (true) or RAM (false).
  void HandleResume(int num_suspend_attempts, bool hibernated);

  // Called after a suspend request (that is, a series of one or more suspend
  // attempts performed in response to e.g. the lid being closed) is canceled.
  void HandleCanceledSuspendRequest(int num_suspend_attempts, bool hibernate);

  // Called after a suspend request has completed (successfully or not).
  // Generates UMA metrics for dark resume.  The size of |wake_durations| is the
  // number of times the system woke up in dark resume during the suspend
  // request and the value of each element is the time spent in dark resume for
  // the corresponding wake.  |suspend_duration| is the total time the system
  // spent in user-visible suspend (including the time spent in dark resume).
  void GenerateDarkResumeMetrics(
      const std::vector<policy::Suspender::DarkResumeInfo>& wake_durations,
      base::TimeDelta suspend_duration);

  // Generates UMA metrics on when leaving the idle state.
  void GenerateUserActivityMetrics();

  // Generates UMA metrics about the current backlight level.
  void GenerateBacklightLevelMetrics();

  // Generates UMA metrics about dimming events.
  void GenerateDimEventMetrics(DimEvent sample);

  // Generates UMA metrics about locking events.
  void GenerateLockEventMetrics(LockEvent sample);

  // Generates UMA metrics about Hps events (dimming, locking, deferring by Hps)
  // durations.
  void GenerateHpsEventDurationMetrics(const std::string& event_name,
                                       base::TimeDelta duration);

  // Generates UMA metric on number of Adaptive Charging Actives.
  void GenerateAdaptiveChargingActiveMetrics(bool enabled);

  // Generates UMA metrics about Adaptive Charging accuracy on AC unplug.
  void GenerateAdaptiveChargingUnplugMetrics(
      const AdaptiveChargingState state,
      const base::TimeTicks& target_time,
      const base::TimeTicks& hold_start_time,
      const base::TimeTicks& hold_end_time,
      const base::TimeTicks& charge_finished_time,
      double display_battery_percent);

  // Handles the power button being pressed or released.
  void HandlePowerButtonEvent(ButtonState state);

  // Sends a metric reporting the amount of time that Chrome took to acknowledge
  // a power button event.
  void SendPowerButtonAcknowledgmentDelayMetric(base::TimeDelta delay);

  // Sets a prefix path which is used as file system root when testing.
  // Setting to an empty path removes the prefix.
  void set_prefix_path_for_testing(const base::FilePath& file) {
    prefix_path_for_testing_ = file;
  }

  // Generates UMA metrics about Ambient Light level on Resume.
  void GenerateAmbientLightResumeMetrics(int lux);

 private:
  friend class MetricsCollectorTest;
  FRIEND_TEST(MetricsCollectorTest, BacklightLevel);
  FRIEND_TEST(MetricsCollectorTest, SendMetricWithPowerSource);
  FRIEND_TEST(MetricsCollectorTest, WakeReasonToHistogramName);
  FRIEND_TEST(MetricsCollectorTest, GatherDarkResumeMetrics);

  // These methods append the current power source to |name|.
  bool SendMetricWithPowerSource(
      const std::string& name, int sample, int min, int max, int num_buckets);
  bool SendEnumMetricWithPowerSource(const std::string& name,
                                     int sample,
                                     int max);
  // This method appends the current privacy screen state and the current power
  // source to |name|. Metrics are only sent if privacy screen is supported. If
  // privacy screen is not supported, this method returns true but does not send
  // metrics.
  bool SendEnumMetricWithPrivacyScreenStatePowerSource(const std::string& name,
                                                       int sample,
                                                       int max);

  // Generates a battery discharge rate UMA metric sample. Returns
  // true if a sample was sent to UMA, false otherwise.
  void GenerateBatteryDischargeRateMetric();

  // Sends a histogram sample containing the rate at which the battery
  // discharged while the system was suspended if the system was on battery
  // power both before suspending and after resuming.  Called by
  // GenerateMetricsOnPowerEvent().  Returns true if the sample was sent.
  void GenerateBatteryDischargeRateWhileSuspendedMetric();

  // Increments the number of user sessions that have been active on the
  // current battery charge.
  void IncrementNumOfSessionsPerChargeMetric();

  // Generates number of sessions per charge UMA metric sample if the current
  // stored value is greater then 0.
  void GenerateNumOfSessionsPerChargeMetric();

  // On devices that suspend to idle (S0ix), the power rail that supplies power
  // to the CPU is left on. Ideally CPUs enter the lowest power state (S0ix)
  // during suspend. But a malfunctioning driver/peripheral can keep the CPUs
  // busy, draining the battery. This function parses the counter that keeps
  // track of number of usecs the cpu spends in the lowest power state before
  // and after suspend. It then generates UMA metrics for percentage of suspend
  // time the system spends in the lowest power state.

  // This function parses the counter that keeps track of number of usecs the
  // CPU spends in the lowest power state. When |pre_suspend| is true, it
  // records the residency in |s0ix_residency_usecs_before_suspend_|. When it's
  // false, it reports the S0ix residency rate (%) in comparision to suspend
  // time.
  void TrackS0ixResidency(bool pre_suspend);

  // Returns new FilePath after prepending |prefix_path_for_testing_| to
  // given file path.
  base::FilePath GetPrefixedFilePath(const base::FilePath& file_path) const;

  PrefsInterface* prefs_ = nullptr;
  policy::BacklightController* display_backlight_controller_ = nullptr;
  policy::BacklightController* keyboard_backlight_controller_ = nullptr;

  Clock clock_;

  // Last power status passed to HandlePowerStatusUpdate().
  system::PowerStatus last_power_status_;

  // Current session state.
  SessionState session_state_ = SessionState::STOPPED;

  // Time at which the current session (if any) started.
  base::TimeTicks session_start_time_;

  // Runs GenerateBacklightLevelMetric().
  base::RepeatingTimer generate_backlight_metrics_timer_;

  // Last privacy screen state that we have been informed of.
  privacy_screen::PrivacyScreenSetting_PrivacyScreenState
      privacy_screen_state_ =
          privacy_screen::PrivacyScreenSetting_PrivacyScreenState_NOT_SUPPORTED;

  // Timestamp of the last generated battery discharge rate metric.
  base::TimeTicks last_battery_discharge_rate_metric_timestamp_;

  // Timestamp of the last time the power button was down.
  base::TimeTicks last_power_button_down_timestamp_;

  // Timestamp of the last idle event (that is, either
  // |screen_dim_timestamp_| or |screen_off_timestamp_|).
  base::TimeTicks last_idle_event_timestamp_;

  // Idle duration as of the last idle event.
  base::TimeDelta last_idle_timedelta_;

  // Notes if the most recent suspend attempt was a hibernation or not.
  bool last_suspend_was_hibernate_ = false;

  // Timestamps of the last idle-triggered power state transitions.
  base::TimeTicks screen_dim_timestamp_;
  base::TimeTicks screen_off_timestamp_;

  // Information recorded by PrepareForSuspend() just before the system
  // suspends. |time_before_suspend_| is initialized using CLOCK_BOOTTIME,
  // which is identical to CLOCK_MONOTONIC, but includes any time spent in
  // suspend.
  double battery_energy_before_suspend_ = 0.0;
  bool on_line_power_before_suspend_ = false;
  base::TimeTicks time_before_suspend_;
  uint64_t s0ix_residency_usecs_before_suspend_ = 0;
  bool pre_suspend_s0ix_read_successful_ = false;

  // Set by HandleResume() to indicate that
  // GenerateBatteryDischargeRateWhileSuspendedMetric() should send a
  // sample when it is next called.
  bool report_battery_discharge_rate_while_suspended_ = false;

  // Path to S0ix residency file on current device.
  base::FilePath s0ix_residency_path_;
  // Max residency that |s0ix_residency_path_| can report. On big-core
  // platforms the default value is set to 100*UINT32_MAX in the Init().
  base::TimeDelta max_s0ix_residency_ = base::TimeDelta::Max();

  // True if suspend to idle (S0ix) is enabled.
  bool suspend_to_idle_ = false;

  // If non-empty, contains a temp dir that will be prepended to paths.
  base::FilePath prefix_path_for_testing_;
};

}  // namespace metrics
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_METRICS_COLLECTOR_H_
