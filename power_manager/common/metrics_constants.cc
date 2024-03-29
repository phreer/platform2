// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/common/metrics_constants.h"

#include <base/time/time.h>

namespace power_manager {
namespace metrics {

const char kAcSuffix[] = "OnAC";
const char kBatterySuffix[] = "OnBattery";

extern const char kPrivacyScreenDisabled[] = "PrivacyScreenDisabled";
extern const char kPrivacyScreenEnabled[] = "PrivacyScreenEnabled";

const int kMaxPercent = 101;
const int kDefaultBuckets = 50;
const int kDefaultDischargeBuckets = 100;

const char kSuspendAttemptsBeforeSuccessName[] =
    "Power.SuspendAttemptsBeforeSuccess";
const char kHibernateAttemptsBeforeSuccessName[] =
    "Power.HibernateAttemptsBeforeSuccess";
const char kSuspendAttemptsBeforeCancelName[] =
    "Power.SuspendAttemptsBeforeCancel";
const char kHibernateAttemptsBeforeCancelName[] =
    "Power.HibernateAttemptsBeforeCancel";
const int kSuspendAttemptsMin = 1;
const int kSuspendAttemptsMax = 20;
const int kSuspendAttemptsBuckets =
    kSuspendAttemptsMax - kSuspendAttemptsMin + 1;

const char kShutdownReasonName[] = "Power.ShutdownReason";
const int kShutdownReasonMax = 10;

const char kBacklightLevelName[] = "Power.BacklightLevel";
const char kKeyboardBacklightLevelName[] = "Power.KeyboardBacklightLevel";
const base::TimeDelta kBacklightLevelInterval = base::Seconds(30);

const char kIdleAfterScreenOffName[] = "Power.IdleTimeAfterScreenOff";
const int kIdleAfterScreenOffMin = 100;
const int kIdleAfterScreenOffMax = 10 * 60 * 1000;

const char kIdleName[] = "Power.IdleTime";
const int kIdleMin = 60 * 1000;
const int kIdleMax = 60 * 60 * 1000;

const char kIdleAfterDimName[] = "Power.IdleTimeAfterDim";
const int kIdleAfterDimMin = 100;
const int kIdleAfterDimMax = 10 * 60 * 1000;

const char kBatteryChargeHealthName[] = "Power.BatteryChargeHealth";  // %
// >100% to account for new batteries which often charge above full
const int kBatteryChargeHealthMax = 111;

const char kBatteryDischargeRateName[] = "Power.BatteryDischargeRate";  // mW
const int kBatteryDischargeRateMin = 1;
const int kBatteryDischargeRateMax = 20000;
const base::TimeDelta kBatteryDischargeRateInterval = base::Seconds(30);

const char kBatteryDischargeRateWhileSuspendedName[] =
    "Power.BatteryDischargeRateWhileSuspended";  // mW
const char kBatteryDischargeRateWhileHibernatedName[] =
    "Power.BatteryDischargeRateWhileHibernated";  // mW
const int kBatteryDischargeRateWhileSuspendedMin = 1;
const int kBatteryDischargeRateWhileSuspendedMax = 5000;
const base::TimeDelta kBatteryDischargeRateWhileSuspendedMinSuspend =
    base::Minutes(10);

const char kBatteryRemainingWhenChargeStartsName[] =
    "Power.BatteryRemainingWhenChargeStarts";  // %
const char kBatteryRemainingAtEndOfSessionName[] =
    "Power.BatteryRemainingAtEndOfSession";  // %
const char kBatteryRemainingAtStartOfSessionName[] =
    "Power.BatteryRemainingAtStartOfSession";                               // %
const char kBatteryRemainingAtBootName[] = "Power.BatteryRemainingAtBoot";  // %

const char kAdaptiveChargingMinutesDeltaActiveName[] =
    "Power.AdaptiveChargingMinutesDelta.Active";
const char kAdaptiveChargingMinutesDeltaHeuristicDisabledName[] =
    "Power.AdaptiveChargingMinutesDelta.HeuristicDisabled";
const char kAdaptiveChargingMinutesDeltaUserCanceledName[] =
    "Power.AdaptiveChargingMinutesDelta.UserCanceled";
const char kAdaptiveChargingMinutesDeltaUserDisabledName[] =
    "Power.AdaptiveChargingMinutesDelta.UserDisabled";
const char kAdaptiveChargingMinutesDeltaShutdownName[] =
    "Power.AdaptiveChargingMinutesDelta.Shutdown";
const char kAdaptiveChargingMinutesDeltaNotSupportedName[] =
    "Power.AdaptiveChargingMinutesDelta.NotSupported";

const char kAdaptiveChargingMinutesDeltaLateSuffix[] = ".Late";
const char kAdaptiveChargingMinutesDeltaEarlySuffix[] = ".Early";

// Sets the range to 0 to 12 hours. If our prediction is too late, the
// `kAdaptiveChargingMinutesDeltaLateSuffix` is appended to the name. For
// accurate and too early predictions, we append the
// `kAdaptiveChargingMinutesDeltaEarlySuffix`. We use the suffixes since UMA
// doesn't support negative numbers. If too many reports hit the 12 hour limit,
// the max should be increased.
const int kAdaptiveChargingMinutesDeltaMin = 0;
const int kAdaptiveChargingMinutesDeltaMax = 12 * 60;

const char kAdaptiveChargingBatteryPercentageOnUnplugName[] =
    "Power.AdaptiveChargingBatteryPercentageOnUnplug";  // %

// For tracking how long it takes to charge from the hold percent to max.
// Anything longer than 4 hours would be extremely abnormal, especially since we
// don't report this metric when charging with a low power USB charger.
const char kAdaptiveChargingMinutesToFullName[] =
    "Power.AdaptiveChargingMinutesToFull";
const int kAdaptiveChargingMinutesToFullMin = 0;
const int kAdaptiveChargingMinutesToFullMax = 4 * 60;

// Set the max limit for the delay time to 3 days. If we start hitting that
// often, we can consider increasing it.
const int kAdaptiveChargingMinutesBuckets = 100;
const char kAdaptiveChargingMinutesDelayName[] =
    "Power.AdaptiveChargingMinutes.Delay";
const char kAdaptiveChargingMinutesAvailableName[] =
    "Power.AdaptiveChargingMinutes.Available";
const int kAdaptiveChargingMinutesMin = 0;
const int kAdaptiveChargingMinutesMax = 3 * 24 * 60;

const char kNumberOfAlsAdjustmentsPerSessionName[] =
    "Power.NumberOfAlsAdjustmentsPerSession";
const int kNumberOfAlsAdjustmentsPerSessionMin = 1;
const int kNumberOfAlsAdjustmentsPerSessionMax = 10000;

const char kUserBrightnessAdjustmentsPerSessionName[] =
    "Power.UserBrightnessAdjustmentsPerSession";
const int kUserBrightnessAdjustmentsPerSessionMin = 1;
const int kUserBrightnessAdjustmentsPerSessionMax = 10000;

const char kLengthOfSessionName[] = "Power.LengthOfSession";
const int kLengthOfSessionMin = 1;
const int kLengthOfSessionMax = 60 * 60 * 12;

const char kNumOfSessionsPerChargeName[] = "Power.NumberOfSessionsPerCharge";
const int kNumOfSessionsPerChargeMin = 1;
const int kNumOfSessionsPerChargeMax = 10000;

const char kPowerButtonDownTimeName[] = "Power.PowerButtonDownTime";  // ms
const int kPowerButtonDownTimeMin = 1;
const int kPowerButtonDownTimeMax = 8 * 1000;

const char kPowerButtonAcknowledgmentDelayName[] =
    "Power.PowerButtonAcknowledgmentDelay";  // ms
const int kPowerButtonAcknowledgmentDelayMin = 1;
const int kPowerButtonAcknowledgmentDelayMax = 8 * 1000;

const char kBatteryInfoSampleName[] = "Power.BatteryInfoSample";

const char kPowerSupplyMaxVoltageName[] = "Power.PowerSupplyMaxVoltage";
const int kPowerSupplyMaxVoltageMax = 21;

const char kPowerSupplyMaxPowerName[] = "Power.PowerSupplyMaxPower";
const int kPowerSupplyMaxPowerMax = 101;

const char kPowerSupplyTypeName[] = "Power.PowerSupplyType";

const char kConnectedChargingPortsName[] = "Power.ConnectedChargingPorts";

const char kExternalBrightnessRequestResultName[] =
    "Power.ExternalBrightnessRequestResult";
const char kExternalBrightnessReadResultName[] =
    "Power.ExternalBrightnessReadResult";
const char kExternalBrightnessWriteResultName[] =
    "Power.ExternalBrightnessWriteResult";
const char kExternalDisplayOpenResultName[] = "Power.ExternalDisplayOpenResult";
const int kExternalDisplayResultMax = 10;

const char kDarkResumeWakeupsPerHourName[] = "Power.DarkResumeWakeupsPerHour";
const int kDarkResumeWakeupsPerHourMin = 0;
const int kDarkResumeWakeupsPerHourMax = 60 * 60;

const char kDarkResumeWakeDurationMsName[] = "Power.DarkResumeWakeDurationMs";
const int kDarkResumeWakeDurationMsMin = 0;
const int kDarkResumeWakeDurationMsMax = 10 * 60 * 1000;

const char kS0ixResidencyRateName[] = "Power.S0ixResidencyRate";  // %

const char kDimEvent[] = "Power.DimEvent";
const int kHpsEventDurationMin = 1;        // One second.
const int kHpsEventDurationMax = 60 * 60;  // One Hour.
const char kQuickDimDurationBeforeRevertedByHpsSec[] =
    "Power.QuickDimRevertedByHps.DurationSeconds";
const char kQuickDimDurationBeforeRevertedByUserSec[] =
    "Power.QuickDimRevertedByUser.DurationSeconds";
const char kStandardDimDurationBeforeRevertedByUserSec[] =
    "Power.StandardDimRevertedByUser.DurationSeconds";
const char kStandardDimDeferredByHpsSec[] =
    "Power.StandardDimDeferredByHps.DurationSeconds";

const char kLockEvent[] = "Power.LockEvent";

const char kAmbientLightOnResumeName[] = "Power.AmbientLightOnResume";
const int kAmbientLightOnResumeMin = 0;
const int kAmbientLightOnResumeMax = 100000;

}  // namespace metrics
}  // namespace power_manager
