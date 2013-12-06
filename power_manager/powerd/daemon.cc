// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/daemon.h"

#include <inttypes.h>
#include <libudev.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "chromeos/dbus/service_constants.h"
#include "metrics/metrics_library.h"
#include "power_manager/common/dbus_sender.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/common/util_dbus.h"
#include "power_manager/policy.pb.h"
#include "power_manager/power_supply_properties.pb.h"
#include "power_manager/powerd/metrics_constants.h"
#include "power_manager/powerd/metrics_reporter.h"
#include "power_manager/powerd/policy/external_backlight_controller.h"
#include "power_manager/powerd/policy/internal_backlight_controller.h"
#include "power_manager/powerd/policy/keyboard_backlight_controller.h"
#include "power_manager/powerd/policy/state_controller.h"
#include "power_manager/powerd/system/ambient_light_sensor.h"
#include "power_manager/powerd/system/audio_client.h"
#include "power_manager/powerd/system/display_power_setter.h"
#include "power_manager/powerd/system/input.h"
#include "power_manager/powerd/system/internal_backlight.h"

namespace power_manager {

namespace {

// Path to file that's touched before the system suspends and unlinked
// after it resumes. Used by crash-reporter to avoid reporting unclean
// shutdowns that occur while the system is suspended (i.e. probably due to
// the battery charge reaching zero).
const char kSuspendedStatePath[] = "/var/lib/power_manager/powerd_suspended";

// Power supply subsystem for udev events.
const char kPowerSupplyUdevSubsystem[] = "power_supply";

// Strings for states that powerd cares about from the session manager's
// SessionStateChanged signal.
const char kSessionStarted[] = "started";

// Path containing the number of wakeup events.
const char kWakeupCountPath[] = "/sys/power/wakeup_count";

// Copies fields from |status| into |proto|.
void CopyPowerStatusToProtocolBuffer(const system::PowerStatus& status,
                                     PowerSupplyProperties* proto) {
  DCHECK(proto);
  proto->Clear();
  proto->set_external_power(status.external_power);
  proto->set_battery_state(status.battery_state);
  proto->set_battery_percent(status.display_battery_percentage);
  // Show the user the time until powerd will shut down the system automatically
  // rather than the time until the battery is completely empty.
  proto->set_battery_time_to_empty_sec(
      status.battery_time_to_shutdown.InSeconds());
  proto->set_battery_time_to_full_sec(
      status.battery_time_to_full.InSeconds());
  proto->set_is_calculating_battery_time(status.is_calculating_battery_time);
}

// Returns a string describing the battery status from |status|.
std::string GetPowerStatusBatteryDebugString(
    const system::PowerStatus& status) {
  if (!status.battery_is_present)
    return std::string();

  std::string output;
  switch (status.external_power) {
    case PowerSupplyProperties_ExternalPower_AC:
    case PowerSupplyProperties_ExternalPower_USB:
    case PowerSupplyProperties_ExternalPower_ORIGINAL_SPRING_CHARGER: {
      const char* type =
          (status.external_power == PowerSupplyProperties_ExternalPower_AC ||
           status.external_power ==
           PowerSupplyProperties_ExternalPower_ORIGINAL_SPRING_CHARGER) ? "AC" :
          "USB";

      std::string kernel_type = status.line_power_type;
      if (status.external_power ==
          PowerSupplyProperties_ExternalPower_ORIGINAL_SPRING_CHARGER)
        kernel_type += "-orig-spring";

      output = StringPrintf("On %s (%s", type, kernel_type.c_str());
      if (status.line_power_current || status.line_power_voltage) {
        output += StringPrintf(", %.3fA at %.1fV",
            status.line_power_current, status.line_power_voltage);
      }
      output += ") with battery at ";
      break;
    }
    case PowerSupplyProperties_ExternalPower_DISCONNECTED:
      output = "On battery at ";
      break;
  }

  long rounded_actual = lround(status.battery_percentage);
  long rounded_display = lround(status.display_battery_percentage);
  output += StringPrintf("%ld%%", rounded_actual);
  if (rounded_actual != rounded_display)
    output += StringPrintf(" (displayed as %ld%%)", rounded_display);
  output += StringPrintf(", %.3f/%.3fAh at %.3fA", status.battery_charge,
      status.battery_charge_full, status.battery_current);

  switch (status.battery_state) {
    case PowerSupplyProperties_BatteryState_FULL:
      output += ", full";
      break;
    case PowerSupplyProperties_BatteryState_CHARGING:
      if (status.battery_time_to_full >= base::TimeDelta()) {
        output += ", " + util::TimeDeltaToString(status.battery_time_to_full) +
            " until full";
        if (status.is_calculating_battery_time)
          output += " (calculating)";
      }
      break;
    case PowerSupplyProperties_BatteryState_DISCHARGING:
      if (status.battery_time_to_empty >= base::TimeDelta()) {
        output += ", " + util::TimeDeltaToString(status.battery_time_to_empty) +
            " until empty";
        if (status.is_calculating_battery_time) {
          output += " (calculating)";
        } else if (status.battery_time_to_shutdown !=
                   status.battery_time_to_empty) {
          output += StringPrintf(" (%s until shutdown)",
              util::TimeDeltaToString(status.battery_time_to_shutdown).c_str());
        }
      }
      break;
    case PowerSupplyProperties_BatteryState_NOT_PRESENT:
      break;
  }

  return output;
}

}  // namespace

// Performs actions requested by |state_controller_|.  The reason that
// this is a nested class of Daemon rather than just being implented as
// part of Daemon is to avoid method naming conflicts.
class Daemon::StateControllerDelegate
    : public policy::StateController::Delegate {
 public:
  explicit StateControllerDelegate(Daemon* daemon) : daemon_(daemon) {}
  virtual ~StateControllerDelegate() {
    daemon_ = NULL;
  }

  // Overridden from policy::StateController::Delegate:
  virtual bool IsUsbInputDeviceConnected() OVERRIDE {
    return daemon_->input_->IsUSBInputDeviceConnected();
  }

  virtual bool IsOobeCompleted() OVERRIDE {
    return util::OOBECompleted();
  }

  virtual bool IsHdmiAudioActive() OVERRIDE {
    return daemon_->audio_client_->hdmi_active();
  }

  virtual bool IsHeadphoneJackPlugged() OVERRIDE {
    return daemon_->audio_client_->headphone_jack_plugged();
  }

  virtual LidState QueryLidState() OVERRIDE {
    return daemon_->input_->QueryLidState();
  }

  virtual void DimScreen() OVERRIDE {
    daemon_->SetBacklightsDimmedForInactivity(true);
  }

  virtual void UndimScreen() OVERRIDE {
    daemon_->SetBacklightsDimmedForInactivity(false);
  }

  virtual void TurnScreenOff() OVERRIDE {
    daemon_->SetBacklightsOffForInactivity(true);
  }

  virtual void TurnScreenOn() OVERRIDE {
    daemon_->SetBacklightsOffForInactivity(false);
  }

  virtual void LockScreen() OVERRIDE {
    util::CallSessionManagerMethod(
        login_manager::kSessionManagerLockScreen, NULL);
  }

  virtual void Suspend() OVERRIDE {
    daemon_->Suspend(false, 0);
  }

  virtual void StopSession() OVERRIDE {
    // This session manager method takes a string argument, although it
    // doesn't currently do anything with it.
    util::CallSessionManagerMethod(
        login_manager::kSessionManagerStopSession, "");
  }

  virtual void ShutDown() OVERRIDE {
    // TODO(derat): Maybe pass the shutdown reason (idle vs. lid-closed)
    // and pass it here.  This isn't necessary at the moment, since nothing
    // special is done for any reason besides |kShutdownReasonLowBattery|.
    daemon_->ShutDown(SHUTDOWN_POWER_OFF, kShutdownReasonUnknown);
  }

  virtual void UpdatePanelForDockedMode(bool docked) OVERRIDE {
    daemon_->SetBacklightsDocked(docked);
  }

  virtual void EmitIdleActionImminent() OVERRIDE {
    daemon_->dbus_sender_->EmitBareSignal(kIdleActionImminentSignal);
  }

  virtual void EmitIdleActionDeferred() OVERRIDE {
    daemon_->dbus_sender_->EmitBareSignal(kIdleActionDeferredSignal);
  }

  virtual void ReportUserActivityMetrics() OVERRIDE {
    daemon_->metrics_reporter_->GenerateUserActivityMetrics();
  }

 private:
  Daemon* daemon_;  // not owned

  DISALLOW_COPY_AND_ASSIGN(StateControllerDelegate);
};

// Performs actions on behalf of Suspender.
class Daemon::SuspenderDelegate : public policy::Suspender::Delegate {
 public:
  SuspenderDelegate(Daemon* daemon) : daemon_(daemon) {}
  virtual ~SuspenderDelegate() {}

  // Delegate implementation:
  virtual bool IsLidClosed() OVERRIDE {
    return daemon_->input_->QueryLidState() == LID_CLOSED;
  }

  virtual bool GetWakeupCount(uint64* wakeup_count) OVERRIDE {
    DCHECK(wakeup_count);
    base::FilePath path(kWakeupCountPath);
    std::string buf;
    if (file_util::ReadFileToString(path, &buf)) {
      TrimWhitespaceASCII(buf, TRIM_TRAILING, &buf);
      if (base::StringToUint64(buf, wakeup_count))
        return true;

      LOG(ERROR) << "Could not parse wakeup count from \"" << buf << "\"";
    } else {
      LOG(ERROR) << "Could not read " << kWakeupCountPath;
    }
    return false;
  }

  virtual void PrepareForSuspendAnnouncement() OVERRIDE {
    daemon_->PrepareForSuspendAnnouncement();
  }

  virtual void HandleCanceledSuspendAnnouncement() OVERRIDE {
    daemon_->HandleCanceledSuspendAnnouncement();
    SendPowerStateChangedSignal("on");
  }

  virtual void PrepareForSuspend() {
    daemon_->PrepareForSuspend();
    SendPowerStateChangedSignal("mem");
  }

  virtual SuspendResult Suspend(uint64 wakeup_count,
                                bool wakeup_count_valid,
                                base::TimeDelta duration) OVERRIDE {
    std::string args;
    if (wakeup_count_valid) {
      args += StringPrintf(" --suspend_wakeup_count_valid"
                           " --suspend_wakeup_count %" PRIu64, wakeup_count);
    }
    if (duration != base::TimeDelta()) {
      args += StringPrintf(" --suspend_duration %" PRId64,
                           duration.InSeconds());
    }

    // These exit codes are defined in scripts/powerd_suspend.
    switch (util::RunSetuidHelper("suspend", args, true)) {
      case 0:
        return SUSPEND_SUCCESSFUL;
      case 1:
        return SUSPEND_FAILED;
      case 2:
        return SUSPEND_CANCELED;
      default:
        LOG(ERROR) << "Treating unexpected exit code as suspend failure";
        return SUSPEND_FAILED;
    }
  }

  virtual void HandleResume(bool suspend_was_successful,
                            int num_suspend_retries,
                            int max_suspend_retries) OVERRIDE {
    SendPowerStateChangedSignal("on");
    daemon_->HandleResume(suspend_was_successful,
                          num_suspend_retries,
                          max_suspend_retries);
  }

  virtual void ShutDownForFailedSuspend() OVERRIDE {
    daemon_->ShutDown(SHUTDOWN_POWER_OFF, kShutdownReasonSuspendFailed);
  }

  virtual void ShutDownForDarkResume() OVERRIDE {
    daemon_->ShutDown(SHUTDOWN_POWER_OFF, kShutdownReasonDarkResume);
  }

 private:
  void SendPowerStateChangedSignal(const std::string& power_state) {
    DBusMessage* signal = dbus_message_new_signal(kPowerManagerServicePath,
                                                  kPowerManagerInterface,
                                                  kPowerStateChanged);
    const char* state = power_state.c_str();
    dbus_message_append_args(signal,
                             DBUS_TYPE_STRING, &state,
                             DBUS_TYPE_INVALID);
    dbus_connection_send(util::GetSystemDBusConnection(), signal, NULL);
    dbus_message_unref(signal);
  }

  Daemon* daemon_;  // not owned

  DISALLOW_COPY_AND_ASSIGN(SuspenderDelegate);
};

Daemon::Daemon(const base::FilePath& read_write_prefs_dir,
               const base::FilePath& read_only_prefs_dir,
               const base::FilePath& run_dir)
    : prefs_(new Prefs),
      state_controller_delegate_(new StateControllerDelegate(this)),
      dbus_sender_(
          new DBusSender(kPowerManagerServicePath, kPowerManagerInterface)),
      input_(new system::Input),
      state_controller_(new policy::StateController(
          state_controller_delegate_.get(), prefs_.get())),
      input_controller_(new policy::InputController(
          input_.get(), this, dbus_sender_.get())),
      audio_client_(new system::AudioClient),
      peripheral_battery_watcher_(new system::PeripheralBatteryWatcher(
          dbus_sender_.get())),
      shutting_down_(false),
      power_supply_(new system::PowerSupply(
          base::FilePath(kPowerStatusPath), prefs_.get())),
      dark_resume_policy_(new policy::DarkResumePolicy(
          power_supply_.get(), prefs_.get())),
      suspender_delegate_(new SuspenderDelegate(this)),
      suspender_(suspender_delegate_.get(), dbus_sender_.get(),
                 dark_resume_policy_.get()),
      run_dir_(run_dir),
      session_state_(SESSION_STOPPED),
      udev_(NULL),
      state_controller_initialized_(false),
      created_suspended_state_file_(false),
      lock_vt_before_suspend_(false),
      log_suspend_with_mosys_eventlog_(false) {
  CHECK(prefs_->Init(util::GetPrefPaths(
      base::FilePath(read_write_prefs_dir),
      base::FilePath(read_only_prefs_dir))));
  power_supply_->AddObserver(this);
  audio_client_->AddObserver(this);
}

Daemon::~Daemon() {
  audio_client_->RemoveObserver(this);
  if (display_backlight_controller_)
    display_backlight_controller_->RemoveObserver(this);
  power_supply_->RemoveObserver(this);

  if (udev_)
    udev_unref(udev_);
}

void Daemon::Init() {
  RegisterUdevEventHandler();
  RegisterDBusMessageHandler();

  if (BoolPrefIsTrue(kHasAmbientLightSensorPref)) {
    light_sensor_.reset(new system::AmbientLightSensor);
    light_sensor_->Init();
  }

  display_power_setter_.reset(new system::DisplayPowerSetter);
  if (BoolPrefIsTrue(kExternalDisplayOnlyPref)) {
    display_backlight_controller_.reset(
        new policy::ExternalBacklightController(display_power_setter_.get()));
    static_cast<policy::ExternalBacklightController*>(
        display_backlight_controller_.get())->Init();
  } else {
    display_backlight_.reset(new system::InternalBacklight);
    if (!display_backlight_->Init(base::FilePath(kInternalBacklightPath),
                                  kInternalBacklightPattern)) {
      LOG(ERROR) << "Cannot initialize display backlight";
      display_backlight_.reset();
    } else {
      display_backlight_controller_.reset(
          new policy::InternalBacklightController(
              display_backlight_.get(), prefs_.get(), light_sensor_.get(),
              display_power_setter_.get()));
      if (!static_cast<policy::InternalBacklightController*>(
              display_backlight_controller_.get())->Init()) {
        LOG(ERROR) << "Cannot initialize display backlight controller";
        display_backlight_controller_.reset();
        display_backlight_.reset();
      }
    }
  }
  if (display_backlight_controller_)
    display_backlight_controller_->AddObserver(this);

  if (BoolPrefIsTrue(kHasKeyboardBacklightPref)) {
    if (!light_sensor_.get()) {
      LOG(ERROR) << "Keyboard backlight requires ambient light sensor";
    } else {
      keyboard_backlight_.reset(new system::InternalBacklight);
      if (!keyboard_backlight_->Init(base::FilePath(kKeyboardBacklightPath),
                                     kKeyboardBacklightPattern)) {
        LOG(ERROR) << "Cannot initialize keyboard backlight";
        keyboard_backlight_.reset();
      } else {
        keyboard_backlight_controller_.reset(
            new policy::KeyboardBacklightController(
                keyboard_backlight_.get(), prefs_.get(), light_sensor_.get(),
                display_backlight_controller_.get()));
        if (!keyboard_backlight_controller_->Init()) {
          LOG(ERROR) << "Cannot initialize keyboard backlight controller";
          keyboard_backlight_controller_.reset();
          keyboard_backlight_.reset();
        }
      }
    }
  }

  prefs_->GetBool(kLockVTBeforeSuspendPref, &lock_vt_before_suspend_);
  prefs_->GetBool(kMosysEventlogPref, &log_suspend_with_mosys_eventlog_);

  power_supply_->Init();
  power_supply_->RefreshImmediately();

  metrics_library_.reset(new MetricsLibrary);
  metrics_library_->Init();

  metrics_reporter_.reset(new MetricsReporter(
      prefs_.get(), metrics_library_.get(), display_backlight_controller_.get(),
      keyboard_backlight_controller_.get())),
  metrics_reporter_->Init(power_supply_->power_status());

  std::string session_state;
  if (util::IsDBusServiceConnected(login_manager::kSessionManagerServiceName,
                                   login_manager::kSessionManagerServicePath,
                                   login_manager::kSessionManagerInterface,
                                   NULL) &&
      util::GetSessionState(&session_state)) {
    OnSessionStateChange(session_state);
  }

  OnPowerStatusUpdate();

  dark_resume_policy_->Init();
  suspender_.Init(prefs_.get());

  CHECK(input_->Init(prefs_.get()));
  input_controller_->Init(prefs_.get());

  // Initialize |state_controller_| before |audio_client_| to ensure that the
  // former is ready to receive the initial notification if audio is already
  // playing.
  const PowerSource power_source =
      power_supply_->power_status().line_power_on ? POWER_AC : POWER_BATTERY;
  state_controller_->Init(power_source, input_->QueryLidState(),
                          session_state_);
  state_controller_initialized_ = true;

  if (util::IsDBusServiceConnected(cras::kCrasServiceName,
                                   cras::kCrasServicePath,
                                   cras::kCrasControlInterface, NULL)) {
    audio_client_->LoadInitialState();
  }

  peripheral_battery_watcher_->Init();
}

bool Daemon::BoolPrefIsTrue(const std::string& name) const {
  bool value = false;
  return prefs_->GetBool(name, &value) && value;
}

void Daemon::AdjustKeyboardBrightness(int direction) {
  if (!keyboard_backlight_controller_)
    return;

  if (direction > 0)
    keyboard_backlight_controller_->IncreaseUserBrightness();
  else if (direction < 0)
    keyboard_backlight_controller_->DecreaseUserBrightness(
        true /* allow_off */);
}

void Daemon::SendBrightnessChangedSignal(
    double brightness_percent,
    policy::BacklightController::BrightnessChangeCause cause,
    const std::string& signal_name) {
  dbus_int32_t brightness_percent_int =
      static_cast<dbus_int32_t>(round(brightness_percent));

  dbus_bool_t user_initiated = FALSE;
  switch (cause) {
    case policy::BacklightController::BRIGHTNESS_CHANGE_AUTOMATED:
      user_initiated = FALSE;
      break;
    case policy::BacklightController::BRIGHTNESS_CHANGE_USER_INITIATED:
      user_initiated = TRUE;
      break;
    default:
      NOTREACHED() << "Unhandled brightness change cause " << cause;
  }

  DBusMessage* signal = dbus_message_new_signal(kPowerManagerServicePath,
                                                kPowerManagerInterface,
                                                signal_name.c_str());
  dbus_message_append_args(signal,
                           DBUS_TYPE_INT32, &brightness_percent_int,
                           DBUS_TYPE_BOOLEAN, &user_initiated,
                           DBUS_TYPE_INVALID);
  dbus_connection_send(util::GetSystemDBusConnection(), signal, NULL);
  dbus_message_unref(signal);
}

void Daemon::OnBrightnessChanged(
    double brightness_percent,
    policy::BacklightController::BrightnessChangeCause cause,
    policy::BacklightController* source) {
  if (source == display_backlight_controller_ &&
      display_backlight_controller_) {
    SendBrightnessChangedSignal(brightness_percent, cause,
                                kBrightnessChangedSignal);
  } else if (source == keyboard_backlight_controller_.get() &&
             keyboard_backlight_controller_) {
    SendBrightnessChangedSignal(brightness_percent, cause,
                                kKeyboardBrightnessChangedSignal);
  } else {
    NOTREACHED() << "Received a brightness change callback from an unknown "
                 << "backlight controller";
  }
}

void Daemon::PrepareForSuspendAnnouncement() {
  // When going to suspend, notify the backlight controller so it can turn
  // the backlight off and tell the kernel to resume the current level
  // after resuming.  This must occur before Chrome is told that the system
  // is going to suspend (Chrome turns the display back on while leaving
  // the backlight off).
  SetBacklightsSuspended(true);
}

void Daemon::HandleCanceledSuspendAnnouncement() {
  // Undo the earlier BACKLIGHT_SUSPENDED call.
  SetBacklightsSuspended(false);
}

void Daemon::PrepareForSuspend() {
  // This command is run synchronously to ensure that it finishes before the
  // system is suspended.
  // TODO(derat): Remove this once it's logged by the kernel:
  // http://crosbug.com/p/16132
  if (log_suspend_with_mosys_eventlog_)
    util::RunSetuidHelper("mosys_eventlog", "--mosys_eventlog_code=0xa7", true);

  // Do not let suspend change the console terminal.
  if (lock_vt_before_suspend_)
    util::RunSetuidHelper("lock_vt", "", true);

  // Touch a file that crash-reporter can inspect later to determine
  // whether the system was suspended while an unclean shutdown occurred.
  // If the file already exists, assume that crash-reporter hasn't seen it
  // yet and avoid unlinking it after resume.
  created_suspended_state_file_ = false;
  const base::FilePath kStatePath(kSuspendedStatePath);
  if (!file_util::PathExists(kStatePath)) {
    if (file_util::WriteFile(kStatePath, NULL, 0) == 0)
      created_suspended_state_file_ = true;
    else
      LOG(WARNING) << "Unable to create " << kSuspendedStatePath;
  }

  power_supply_->SetSuspended(true);
  audio_client_->MuteSystem();
  metrics_reporter_->PrepareForSuspend();
}

void Daemon::HandleResume(bool suspend_was_successful,
                          int num_suspend_retries,
                          int max_suspend_retries) {
  SetBacklightsSuspended(false);
  audio_client_->RestoreMutedState();
  power_supply_->SetSuspended(false);
  state_controller_->HandleResume();

  // Allow virtual terminal switching again.
  if (lock_vt_before_suspend_)
    util::RunSetuidHelper("unlock_vt", "", true);

  if (created_suspended_state_file_) {
    if (!file_util::Delete(base::FilePath(kSuspendedStatePath), false))
      LOG(ERROR) << "Failed to delete " << kSuspendedStatePath;
  }

  if (suspend_was_successful) {
    metrics_reporter_->GenerateRetrySuspendMetric(
        num_suspend_retries, max_suspend_retries);
    metrics_reporter_->HandleResume();
  }

  // TODO(derat): Remove this once it's logged by the kernel:
  // http://crosbug.com/p/16132
  if (log_suspend_with_mosys_eventlog_) {
    util::RunSetuidHelper(
        "mosys_eventlog", "--mosys_eventlog_code=0xa8", false);
  }
}

void Daemon::HandleLidClosed() {
  if (state_controller_initialized_)
    state_controller_->HandleLidStateChange(LID_CLOSED);
}

void Daemon::HandleLidOpened() {
  suspender_.HandleLidOpened();
  if (state_controller_initialized_)
    state_controller_->HandleLidStateChange(LID_OPEN);
}

void Daemon::HandlePowerButtonEvent(ButtonState state) {
  metrics_reporter_->HandlePowerButtonEvent(state);
  if (state == BUTTON_DOWN && display_backlight_controller_)
    display_backlight_controller_->HandlePowerButtonPress();
}

void Daemon::DeferInactivityTimeoutForVT2() {
  state_controller_->HandleUserActivity();
}

void Daemon::ShutDownForPowerButtonWithNoDisplay() {
  LOG(INFO) << "Shutting down due to power button press while no display is "
            << "connected";
  metrics_reporter_->HandlePowerButtonEvent(BUTTON_DOWN);
  ShutDown(SHUTDOWN_POWER_OFF, kShutdownReasonUserRequest);
}

void Daemon::HandleMissingPowerButtonAcknowledgment() {
  LOG(INFO) << "Didn't receive power button acknowledgment from Chrome";
  util::Launch("sync");
}

void Daemon::OnAudioStateChange(bool active) {
  // |state_controller_| needs to be ready at this point -- since notifications
  // only arrive when the audio state changes, skipping any is unsafe.
  CHECK(state_controller_initialized_);
  state_controller_->HandleAudioStateChange(active);
}

void Daemon::OnPowerStatusUpdate() {
  const system::PowerStatus& status = power_supply_->power_status();
  if (status.battery_is_present)
    LOG(INFO) << GetPowerStatusBatteryDebugString(status);

  metrics_reporter_->HandlePowerStatusUpdate(status);

  const PowerSource power_source =
      status.line_power_on ? POWER_AC : POWER_BATTERY;
  if (display_backlight_controller_)
    display_backlight_controller_->HandlePowerSourceChange(power_source);
  if (keyboard_backlight_controller_)
    keyboard_backlight_controller_->HandlePowerSourceChange(power_source);
  if (state_controller_initialized_)
    state_controller_->HandlePowerSourceChange(power_source);

  if (status.battery_is_present && status.battery_below_shutdown_threshold) {
    LOG(INFO) << "Shutting down due to low battery ("
              << StringPrintf("%0.2f", status.battery_percentage) << "%, "
              << util::TimeDeltaToString(status.battery_time_to_empty)
              << " until empty, "
              << StringPrintf("%0.3f", status.observed_battery_charge_rate)
              << "A observed charge rate)";
    ShutDown(SHUTDOWN_POWER_OFF, kShutdownReasonLowBattery);
  }

  PowerSupplyProperties protobuf;
  CopyPowerStatusToProtocolBuffer(status, &protobuf);
  dbus_sender_->EmitSignalWithProtocolBuffer(kPowerSupplyPollSignal, protobuf);
}

gboolean Daemon::UdevEventHandler(GIOChannel* /* source */,
                                  GIOCondition /* condition */,
                                  gpointer data) {
  Daemon* daemon = static_cast<Daemon*>(data);

  struct udev_device* dev = udev_monitor_receive_device(daemon->udev_monitor_);
  if (dev) {
    LOG(INFO) << "Event on (" << udev_device_get_subsystem(dev) << ") Action "
              << udev_device_get_action(dev);
    CHECK(std::string(udev_device_get_subsystem(dev)) ==
          kPowerSupplyUdevSubsystem);
    udev_device_unref(dev);
    daemon->power_supply_->HandleUdevEvent();
  } else {
    LOG(ERROR) << "Can't get receive_device()";
    return FALSE;
  }
  return TRUE;
}

void Daemon::RegisterUdevEventHandler() {
  // Create the udev object.
  udev_ = udev_new();
  if (!udev_)
    LOG(ERROR) << "Can't create udev object.";

  // Create the udev monitor structure.
  udev_monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_monitor_) {
    LOG(ERROR) << "Can't create udev monitor.";
    udev_unref(udev_);
  }
  udev_monitor_filter_add_match_subsystem_devtype(udev_monitor_,
                                                  kPowerSupplyUdevSubsystem,
                                                  NULL);
  udev_monitor_enable_receiving(udev_monitor_);

  int fd = udev_monitor_get_fd(udev_monitor_);

  GIOChannel* channel = g_io_channel_unix_new(fd);
  g_io_add_watch(channel, G_IO_IN, &(Daemon::UdevEventHandler), this);

  LOG(INFO) << "Udev controller waiting for events on subsystem "
            << kPowerSupplyUdevSubsystem;
}

void Daemon::RegisterDBusMessageHandler() {
  util::RequestDBusServiceName(kPowerManagerServiceName);

  dbus_handler_.SetNameOwnerChangedHandler(
      base::Bind(&Daemon::HandleDBusNameOwnerChanged, base::Unretained(this)));

  dbus_handler_.AddSignalHandler(
      login_manager::kSessionManagerInterface,
      login_manager::kSessionStateChangedSignal,
      base::Bind(&Daemon::HandleSessionStateChangedSignal,
                 base::Unretained(this)));
  dbus_handler_.AddSignalHandler(
      update_engine::kUpdateEngineInterface,
      update_engine::kStatusUpdate,
      base::Bind(&Daemon::HandleUpdateEngineStatusUpdateSignal,
                 base::Unretained(this)));
  dbus_handler_.AddSignalHandler(
      cras::kCrasControlInterface,
      cras::kNodesChanged,
      base::Bind(&Daemon::HandleCrasNodesChangedSignal,
                 base::Unretained(this)));
  dbus_handler_.AddSignalHandler(
      cras::kCrasControlInterface,
      cras::kActiveOutputNodeChanged,
      base::Bind(&Daemon::HandleCrasActiveOutputNodeChangedSignal,
                 base::Unretained(this)));
  dbus_handler_.AddSignalHandler(
      cras::kCrasControlInterface,
      cras::kNumberOfActiveStreamsChanged,
      base::Bind(&Daemon::HandleCrasNumberOfActiveStreamsChanged,
                 base::Unretained(this)));

  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kRequestShutdownMethod,
      base::Bind(&Daemon::HandleRequestShutdownMethod, base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kRequestRestartMethod,
      base::Bind(&Daemon::HandleRequestRestartMethod, base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kRequestSuspendMethod,
      base::Bind(&Daemon::HandleRequestSuspendMethod, base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kDecreaseScreenBrightness,
      base::Bind(&Daemon::HandleDecreaseScreenBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kIncreaseScreenBrightness,
      base::Bind(&Daemon::HandleIncreaseScreenBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kGetScreenBrightnessPercent,
      base::Bind(&Daemon::HandleGetScreenBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kSetScreenBrightnessPercent,
      base::Bind(&Daemon::HandleSetScreenBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kDecreaseKeyboardBrightness,
      base::Bind(&Daemon::HandleDecreaseKeyboardBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kIncreaseKeyboardBrightness,
      base::Bind(&Daemon::HandleIncreaseKeyboardBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kGetPowerSupplyPropertiesMethod,
      base::Bind(&Daemon::HandleGetPowerSupplyPropertiesMethod,
                 base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kHandleVideoActivityMethod,
      base::Bind(&Daemon::HandleVideoActivityMethod, base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kHandleUserActivityMethod,
      base::Bind(&Daemon::HandleUserActivityMethod, base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kSetIsProjectingMethod,
      base::Bind(&Daemon::HandleSetIsProjectingMethod, base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kSetPolicyMethod,
      base::Bind(&Daemon::HandleSetPolicyMethod, base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kHandlePowerButtonAcknowledgmentMethod,
      base::Bind(&Daemon::HandlePowerButtonAcknowledgment,
                 base::Unretained(this)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kRegisterSuspendDelayMethod,
      base::Bind(&policy::Suspender::RegisterSuspendDelay,
                 base::Unretained(&suspender_)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kUnregisterSuspendDelayMethod,
      base::Bind(&policy::Suspender::UnregisterSuspendDelay,
                 base::Unretained(&suspender_)));
  dbus_handler_.AddMethodHandler(
      kPowerManagerInterface,
      kHandleSuspendReadinessMethod,
      base::Bind(&policy::Suspender::HandleSuspendReadiness,
                 base::Unretained(&suspender_)));

  dbus_handler_.Start();
}

void Daemon::HandleDBusNameOwnerChanged(const std::string& name,
                                        const std::string& old_owner,
                                        const std::string& new_owner) {
  if (name == login_manager::kSessionManagerServiceName && !new_owner.empty()) {
    LOG(INFO) << "D-Bus " << name << " ownership changed to " << new_owner;
    std::string session_state;
    if (util::GetSessionState(&session_state))
      OnSessionStateChange(session_state);
  } else if (name == cras::kCrasServiceName && !new_owner.empty()) {
    LOG(INFO) << "D-Bus " << name << " ownership changed to " << new_owner;
    audio_client_->LoadInitialState();
  } else if (name == chromeos::kLibCrosServiceName && !new_owner.empty()) {
    LOG(INFO) << "D-Bus " << name << " ownership changed to " << new_owner;
    if (display_backlight_controller_)
      display_backlight_controller_->HandleChromeStart();
  }
  suspender_.HandleDBusNameOwnerChanged(name, old_owner, new_owner);
}

bool Daemon::HandleSessionStateChangedSignal(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  const char* state = NULL;
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_STRING, &state,
                            DBUS_TYPE_INVALID)) {
    OnSessionStateChange(state ? state : "");
  } else {
    LOG(WARNING) << "Unable to read "
                 << login_manager::kSessionStateChangedSignal << " args";
    dbus_error_free(&error);
  }
  return false;
}

bool Daemon::HandleUpdateEngineStatusUpdateSignal(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);

  dbus_int64_t last_checked_time = 0;
  double progress = 0.0;
  const char* current_operation = NULL;
  const char* new_version = NULL;
  dbus_int64_t new_size = 0;

  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_INT64, &last_checked_time,
                             DBUS_TYPE_DOUBLE, &progress,
                             DBUS_TYPE_STRING, &current_operation,
                             DBUS_TYPE_STRING, &new_version,
                             DBUS_TYPE_INT64, &new_size,
                             DBUS_TYPE_INVALID)) {
    LOG(WARNING) << "Unable to read args from " << update_engine::kStatusUpdate
                 << " signal";
    dbus_error_free(&error);
    return false;
  }

  UpdaterState state = UPDATER_IDLE;
  std::string operation = current_operation;
  if (operation == update_engine::kUpdateStatusDownloading ||
      operation == update_engine::kUpdateStatusVerifying ||
      operation == update_engine::kUpdateStatusFinalizing) {
    state = UPDATER_UPDATING;
  } else if (operation == update_engine::kUpdateStatusUpdatedNeedReboot) {
    state = UPDATER_UPDATED;
  }
  state_controller_->HandleUpdaterStateChange(state);

  return false;
}

bool Daemon::HandleCrasNodesChangedSignal(DBusMessage* message) {
  audio_client_->UpdateDevices();
  return false;
}

bool Daemon::HandleCrasActiveOutputNodeChangedSignal(DBusMessage* message) {
  audio_client_->UpdateDevices();
  return false;
}

bool Daemon::HandleCrasNumberOfActiveStreamsChanged(DBusMessage* message) {
  audio_client_->UpdateNumActiveStreams();
  return false;
}

DBusMessage* Daemon::HandleRequestShutdownMethod(DBusMessage* message) {
  LOG(INFO) << "Got " << kRequestShutdownMethod << " message";
  ShutDown(SHUTDOWN_POWER_OFF, kShutdownReasonUserRequest);
  return NULL;
}

DBusMessage* Daemon::HandleRequestRestartMethod(DBusMessage* message) {
  LOG(INFO) << "Got " << kRequestRestartMethod << " message";
  ShutDown(SHUTDOWN_REBOOT, kShutdownReasonUserRequest);
  return NULL;
}

DBusMessage* Daemon::HandleRequestSuspendMethod(DBusMessage* message) {
  // Read an optional uint64 argument specifying the wakeup count that is
  // expected.
  dbus_uint64_t external_wakeup_count = 0;
  DBusError error;
  dbus_error_init(&error);
  const bool got_external_wakeup_count = dbus_message_get_args(
      message, &error, DBUS_TYPE_UINT64, &external_wakeup_count,
      DBUS_TYPE_INVALID);
  dbus_error_free(&error);

  LOG(INFO) << "Got " << kRequestSuspendMethod << " message"
            << (got_external_wakeup_count ?
                StringPrintf(" with external wakeup count %" PRIu64,
                             external_wakeup_count).c_str() : "");
  Suspend(got_external_wakeup_count, external_wakeup_count);
  return NULL;
}

DBusMessage* Daemon::HandleDecreaseScreenBrightnessMethod(
    DBusMessage* message) {
  if (!display_backlight_controller_)
    return util::CreateDBusErrorReply(message, "Backlight uninitialized");

  dbus_bool_t allow_off = false;
  DBusError error;
  dbus_error_init(&error);
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_BOOLEAN, &allow_off,
                            DBUS_TYPE_INVALID) == FALSE) {
    LOG(WARNING) << "Unable to read " << kDecreaseScreenBrightness << " args";
    dbus_error_free(&error);
  }
  bool changed =
      display_backlight_controller_->DecreaseUserBrightness( allow_off);
  double percent = 0.0;
  if (!changed &&
      display_backlight_controller_->GetBrightnessPercent(&percent)) {
    SendBrightnessChangedSignal(
        percent, policy::BacklightController::BRIGHTNESS_CHANGE_USER_INITIATED,
        kBrightnessChangedSignal);
  }
  return NULL;
}

DBusMessage* Daemon::HandleIncreaseScreenBrightnessMethod(
    DBusMessage* message) {
  if (!display_backlight_controller_)
    return util::CreateDBusErrorReply(message, "Backlight uninitialized");

  bool changed = display_backlight_controller_->IncreaseUserBrightness();
  double percent = 0.0;
  if (!changed &&
      display_backlight_controller_->GetBrightnessPercent(&percent)) {
    SendBrightnessChangedSignal(
        percent, policy::BacklightController::BRIGHTNESS_CHANGE_USER_INITIATED,
        kBrightnessChangedSignal);
  }
  return NULL;
}

DBusMessage* Daemon::HandleSetScreenBrightnessMethod(DBusMessage* message) {
  double percent;
  int dbus_style;
  DBusError error;
  dbus_error_init(&error);
  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_DOUBLE, &percent,
                             DBUS_TYPE_INT32, &dbus_style,
                             DBUS_TYPE_INVALID)) {
    LOG(WARNING) << kSetScreenBrightnessPercent
                << ": Error reading args: " << error.message;
    dbus_error_free(&error);
    return util::CreateDBusInvalidArgsErrorReply(message);
  }
  policy::BacklightController::TransitionStyle style =
      policy::BacklightController::TRANSITION_FAST;
  switch (dbus_style) {
    case kBrightnessTransitionGradual:
      style = policy::BacklightController::TRANSITION_FAST;
      break;
    case kBrightnessTransitionInstant:
      style = policy::BacklightController::TRANSITION_INSTANT;
      break;
    default:
      LOG(WARNING) << "Invalid transition style passed ( " << dbus_style
                  << " ).  Using default fast transition";
  }
  display_backlight_controller_->SetUserBrightnessPercent(percent, style);
  return NULL;
}

DBusMessage* Daemon::HandleGetScreenBrightnessMethod(DBusMessage* message) {
  if (!display_backlight_controller_)
    return util::CreateDBusErrorReply(message, "Backlight uninitialized");

  double percent = 0.0;
  if (!display_backlight_controller_->GetBrightnessPercent(&percent)) {
    return util::CreateDBusErrorReply(
        message, "Could not fetch Screen Brightness");
  }
  DBusMessage* reply = util::CreateEmptyDBusReply(message);
  CHECK(reply);
  dbus_message_append_args(reply,
                           DBUS_TYPE_DOUBLE, &percent,
                           DBUS_TYPE_INVALID);
  return reply;
}

DBusMessage* Daemon::HandleDecreaseKeyboardBrightnessMethod(
    DBusMessage* message) {
  AdjustKeyboardBrightness(-1);
  return NULL;
}

DBusMessage* Daemon::HandleIncreaseKeyboardBrightnessMethod(
    DBusMessage* message) {
  AdjustKeyboardBrightness(1);
  return NULL;
}

DBusMessage* Daemon::HandleGetPowerSupplyPropertiesMethod(
    DBusMessage* message) {
  PowerSupplyProperties protobuf;
  CopyPowerStatusToProtocolBuffer(power_supply_->power_status(), &protobuf);
  return util::CreateDBusProtocolBufferReply(message, protobuf);
}

DBusMessage* Daemon::HandleVideoActivityMethod(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  gboolean is_fullscreen = false;
  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_BOOLEAN, &is_fullscreen,
                             DBUS_TYPE_INVALID)) {
    LOG(WARNING) << "Unable to read " << kHandleVideoActivityMethod << "args";
    dbus_error_free(&error);
  }

  if (keyboard_backlight_controller_)
    keyboard_backlight_controller_->HandleVideoActivity(is_fullscreen);
  state_controller_->HandleVideoActivity();
  return NULL;
}

DBusMessage* Daemon::HandleUserActivityMethod(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  dbus_int32_t type_int = USER_ACTIVITY_OTHER;
  dbus_message_get_args(
      message, &error, DBUS_TYPE_INT32, &type_int, DBUS_TYPE_INVALID);
  dbus_error_free(&error);
  UserActivityType type = static_cast<UserActivityType>(type_int);

  suspender_.HandleUserActivity();
  state_controller_->HandleUserActivity();
  if (display_backlight_controller_)
    display_backlight_controller_->HandleUserActivity(type);
  if (keyboard_backlight_controller_)
    keyboard_backlight_controller_->HandleUserActivity(type);
  return NULL;
}

DBusMessage* Daemon::HandleSetIsProjectingMethod(DBusMessage* message) {
  DBusMessage* reply = NULL;
  DBusError error;
  dbus_error_init(&error);
  bool is_projecting = false;
  dbus_bool_t args_ok =
      dbus_message_get_args(message, &error,
                            DBUS_TYPE_BOOLEAN, &is_projecting,
                            DBUS_TYPE_INVALID);
  if (args_ok) {
    DisplayMode mode = is_projecting ? DISPLAY_PRESENTATION : DISPLAY_NORMAL;
    state_controller_->HandleDisplayModeChange(mode);
    if (display_backlight_controller_)
      display_backlight_controller_->HandleDisplayModeChange(mode);
  } else {
    // The message was malformed so log this and return an error.
    LOG(WARNING) << kSetIsProjectingMethod << ": Error reading args: "
                 << error.message;
    reply = util::CreateDBusInvalidArgsErrorReply(message);
  }
  return reply;
}

DBusMessage* Daemon::HandleSetPolicyMethod(DBusMessage* message) {
  PowerManagementPolicy policy;
  if (!util::ParseProtocolBufferFromDBusMessage(message, &policy)) {
    LOG(WARNING) << "Unable to parse " << kSetPolicyMethod << " request";
    return util::CreateDBusInvalidArgsErrorReply(message);
  }
  state_controller_->HandlePolicyChange(policy);
  if (display_backlight_controller_)
    display_backlight_controller_->HandlePolicyChange(policy);
  return NULL;
}

DBusMessage* Daemon::HandlePowerButtonAcknowledgment(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  dbus_int64_t timestamp_internal = 0;
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_INT64, &timestamp_internal,
                            DBUS_TYPE_INVALID)) {
    input_controller_->HandlePowerButtonAcknowledgment(
        base::TimeTicks::FromInternalValue(timestamp_internal));
  } else {
    dbus_error_free(&error);
    LOG(WARNING) << "Unable to parse "
                 << kHandlePowerButtonAcknowledgmentMethod << " request";
  }
  return NULL;
}

void Daemon::OnSessionStateChange(const std::string& state_str) {
  DLOG(INFO) << "Session " << state_str;
  SessionState state = (state_str == kSessionStarted) ?
      SESSION_STARTED : SESSION_STOPPED;
  if (state == session_state_)
    return;

  session_state_ = state;
  metrics_reporter_->HandleSessionStateChange(state);
  if (state_controller_initialized_)
    state_controller_->HandleSessionStateChange(state);
  if (display_backlight_controller_)
    display_backlight_controller_->HandleSessionStateChange(state);
  if (keyboard_backlight_controller_)
    keyboard_backlight_controller_->HandleSessionStateChange(state);
}

void Daemon::ShutDown(ShutdownMode mode, const std::string& reason) {
  if (shutting_down_) {
    LOG(WARNING) << "Shutdown already initiated";
    return;
  }

  shutting_down_ = true;
  suspender_.HandleShutdown();

  // If we want to display a low-battery alert while shutting down, don't turn
  // the screen off immediately.
  if (reason != kShutdownReasonLowBattery) {
    if (display_backlight_controller_)
      display_backlight_controller_->SetShuttingDown(true);
    if (keyboard_backlight_controller_)
      keyboard_backlight_controller_->SetShuttingDown(true);
  }

  switch (mode) {
    case SHUTDOWN_POWER_OFF:
      LOG(INFO) << "Shutting down, reason: " << reason;
      util::RunSetuidHelper("shut_down", "--shutdown_reason=" + reason, false);
      break;
    case SHUTDOWN_REBOOT:
      LOG(INFO) << "Restarting";
      util::RunSetuidHelper("reboot", "", false);
      break;
  }
}

void Daemon::Suspend(bool use_external_wakeup_count,
                     uint64 external_wakeup_count) {
  if (shutting_down_) {
    LOG(INFO) << "Ignoring request for suspend with outstanding shutdown";
    return;
  }

  if (use_external_wakeup_count)
    suspender_.RequestSuspendWithExternalWakeupCount(external_wakeup_count);
  else
    suspender_.RequestSuspend();
}

void Daemon::SetBacklightsDimmedForInactivity(bool dimmed) {
  if (display_backlight_controller_)
    display_backlight_controller_->SetDimmedForInactivity(dimmed);
  if (keyboard_backlight_controller_)
    keyboard_backlight_controller_->SetDimmedForInactivity(dimmed);
  metrics_reporter_->HandleScreenDimmedChange(
      dimmed, state_controller_->last_user_activity_time());
}

void Daemon::SetBacklightsOffForInactivity(bool off) {
  if (display_backlight_controller_)
    display_backlight_controller_->SetOffForInactivity(off);
  if (keyboard_backlight_controller_)
    keyboard_backlight_controller_->SetOffForInactivity(off);
  metrics_reporter_->HandleScreenOffChange(
      off, state_controller_->last_user_activity_time());
}

void Daemon::SetBacklightsSuspended(bool suspended) {
  if (display_backlight_controller_)
    display_backlight_controller_->SetSuspended(suspended);
  if (keyboard_backlight_controller_)
    keyboard_backlight_controller_->SetSuspended(suspended);
}

void Daemon::SetBacklightsDocked(bool docked) {
  if (display_backlight_controller_)
    display_backlight_controller_->SetDocked(docked);
  if (keyboard_backlight_controller_)
    keyboard_backlight_controller_->SetDocked(docked);
}

}  // namespace power_manager
