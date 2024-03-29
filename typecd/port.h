// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PORT_H_
#define TYPECD_PORT_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/cancelable_callback.h>
#include <gtest/gtest_prod.h>

#include "typecd/cable.h"
#include "typecd/ec_util.h"
#include "typecd/partner.h"

namespace typecd {

// Possible return values for the various CanEnter*() Mode entry checks.
enum class ModeEntryResult {
  kSuccess = 0,
  kCableError = 1,
  kPartnerError = 2,
  kPortError = 3,
  kMaxValue = kPortError,
};

// Possible data roles for the port.
enum class DataRole {
  kNone = 0,
  kDevice = 1,
  kHost = 2,
  kMaxValue = kHost,
};

// Possible power roles for the port.
enum class PowerRole {
  kNone = 0,
  kSink = 1,
  kSource = 2,
  kMaxValue = kSource,
};

// This class is used to represent a Type C Port. It can be used to access PD
// state associated with the port, and will also contain handles to the object
// representing a peripheral (i.e "Partner") if one is connected to the port.
class Port {
 public:
  static std::unique_ptr<Port> CreatePort(const base::FilePath& syspath);
  Port(const base::FilePath& syspath, int port_num);
  virtual ~Port() = default;

  void AddPartner(const base::FilePath& path);
  void RemovePartner();

  void AddCable(const base::FilePath& path);
  void RemoveCable();
  void AddCablePlug(const base::FilePath& path);

  // Add/remove an alternate mode for the partner.
  void AddRemovePartnerAltMode(const base::FilePath& path, bool added);

  void AddCableAltMode(const base::FilePath& path);

  void PartnerChanged();

  void PortChanged();

  void SetCurrentMode(TypeCMode mode) { current_mode_ = mode; }

  TypeCMode GetCurrentMode() { return current_mode_; }

  void SetActiveStateOnModeEntry(bool state) {
    user_active_on_mode_entry_ = state;
  }
  bool GetActiveStateOnModeEntry() { return user_active_on_mode_entry_; }

  // Returns the current data role for the port.
  virtual DataRole GetDataRole();

  // Returns the current power role for the port.
  virtual PowerRole GetPowerRole();

  // Configure whether the port supports USB4 (and by extension, TBT Compat)
  // mode.
  void SetSupportsUSB4(bool enable) { supports_usb4_ = enable; }

  // Check whether we can enter DP Alt Mode. This should check for the presence
  // of required attributes on the Partner and (if applicable) Cable.
  virtual bool CanEnterDPAltMode(bool* invalid_cable_flag);

  // Check whether we can enter Thunderbolt Compatibility Alt Mode. This should
  // check for the presence of required attributes on the Partner and
  // (if applicable) Cable.
  virtual ModeEntryResult CanEnterTBTCompatibilityMode();

  // Returns whether the partner can enter USB4. This should check the following
  // attributes for USB4 support:
  // - Partner(SOP) PD identity.
  // - Cable speed.
  // - Cable type.
  virtual ModeEntryResult CanEnterUSB4();

  // Returns true when all PD discovery information (PD Identity VDOs, all
  // Discover Mode data) for a partner has been processed.
  //
  // NOTE: Any mode entry decision logic should only run if this function
  // returns true.
  virtual bool IsPartnerDiscoveryComplete();

  // Returns true when the partner reports PD support, and false otherwise.
  virtual bool PartnerSupportsPD();

  // Return true when all PD discovery information (PD Identity VDOs, all
  // Discover Mode data) for a cable has been processed.
  //
  // NOTE: Any mode entry decision logic should only run if this function
  // returns true.
  virtual bool IsCableDiscoveryComplete();

  // Returns true if the port's partner supports a higher USB gen than the
  // cable.
  virtual bool CableLimitingUSBSpeed();

  // Enqueue metrics reporting task with a delay to give time for PD
  // negotiation.
  void EnqueueMetricsTask(Metrics* metrics, bool mode_entry_supported);

  // Cancel enqueued metrics task.
  void CancelMetricsTask();

 private:
  friend class PortTest;
  FRIEND_TEST(PortTest, BasicAdd);
  FRIEND_TEST(PortTest, DPAltModeEntryCheckTrue);
  FRIEND_TEST(PortTest, DPAltModeEntryCheckFalseWithDPSID);
  FRIEND_TEST(PortTest, DPAltModeEntryCheckFalse);
  FRIEND_TEST(PortTest, DPAltModeEntryCalDigitTBT4ToDisplay);
  FRIEND_TEST(PortTest, DPAltModeEntryAnkerUsb3Gen2ToDisplay);
  FRIEND_TEST(PortTest, DPAltModeEntryHPUsb3Gen1ToDisplay);
  FRIEND_TEST(PortTest, DPAltModeEntryAppleTBT3ToDisplay);
  FRIEND_TEST(PortTest, DPAltModeEntryUnbrandedUSB2ToDisplay);
  FRIEND_TEST(PortTest, DPAltModeEntryNekteckUSB2ToDisplay);
  FRIEND_TEST(PortTest, DPAltModeEntryTBT3ToDock);
  FRIEND_TEST(PortTest, DPAltModeEntryUnbrandedUSB2ToDock);
  FRIEND_TEST(PortTest, DPAltModeEntryNekteckUSB2ToDock);
  FRIEND_TEST(PortTest, DPAltModeEntryCableMattersDock);
  FRIEND_TEST(PortTest, TBTCompatibilityModeEntryCheckTrueStartech);
  FRIEND_TEST(PortTest, TBTCompatibilityModeEntryCheckFalseStartech);
  FRIEND_TEST(PortTest, TBTCompatibilityModeEntryCheckTrueWD19TB);
  FRIEND_TEST(PortTest, USB4EntryTrueGatkexPassiveTBT3Cable);
  FRIEND_TEST(PortTest, USB4EntryTrueGatkexPassiveNonTBT3Cable);
  FRIEND_TEST(PortTest, USB4EntryFalseGatkexPassiveNonTBT3Cable);
  FRIEND_TEST(PortTest, USB4EntryFalseGatkexActiveTBT3Cable);
  FRIEND_TEST(PortTest, USB4EntryTrueGatkexAppleTBT3ProCable);
  FRIEND_TEST(PortTest, USB4ToTBT);
  FRIEND_TEST(PortTest, USB4ToDPAltMode);
  FRIEND_TEST(PortTest, USB4LimitedByCableFalse);
  FRIEND_TEST(PortTest, USB4LimitedByCableTrue);
  FRIEND_TEST(PortTest, USB4LimitedByTBT3PassiveCableFalse);
  FRIEND_TEST(PortTest, USB4LimitedByTBT4PassiveLRDCableFalse);
  FRIEND_TEST(PortTest, BillboardOnlyDisplayNotLimitedByCable);
  FRIEND_TEST(PortTest, CableLimitingSpeedTBT4DockAppleTBT3ProCable);

  // Helper functions from test_util for defining devices used in unit tests.
  friend void AddUnbrandedUSB2Cable(Port& port);
  friend void AddNekteckUSB2PassiveCable(Port& port);
  friend void AddHongjuUSB3p1Gen1Cable(Port& port);
  friend void AddHPUSB3p2Gen1Cable(Port& port);
  friend void AddAnkerUSB3p2Gen2Cable(Port& port);
  friend void AddCableMatters20GbpsCable(Port& port);
  friend void AddUnbrandedTBT3Cable(Port& port);
  friend void AddBelkinTBT3PassiveCable(Port& port);
  friend void AddBelkinTBT3ActiveCable(Port& port);
  friend void AddAppleTBT3ProCable(Port& port);
  friend void AddCalDigitTBT4Cable(Port& port);
  friend void AddCableMattersTBT4LRDCable(Port& port);
  friend void AddStartech40GbpsCable(Port& port);
  friend void AddCableMattersDock(Port& port);
  friend void AddDellWD19TBDock(Port& port);
  friend void AddStartechDock(Port& port);
  friend void AddStartechTB3DK2DPWDock(Port& port);
  friend void AddThinkpadTBT3Dock(Port& port);
  friend void AddIntelUSB4GatkexCreekDock(Port& port);
  friend void AddOWCTBT4Dock(Port& port);
  friend void AddWimaxitDisplay(Port& port);

  bool IsPartnerAltModePresent(uint16_t altmode_sid);

  bool IsCableAltModePresent(uint16_t altmode_sid);

  // Reads the current port data role from sysfs and stores it in |data_role_|.
  void ParseDataRole();
  // Reads the current port power role from sysfs and stores it in
  // |power_role_|.
  void ParsePowerRole();

  // Calls the |partner_|'s metrics reporting function, if a |partner_| is
  // registered.
  void ReportPartnerMetrics(Metrics* metrics);

  // Calls the |cable_|'s metrics reporting function, if a |cable_| is
  // registered.
  void ReportCableMetrics(Metrics* metrics);

  // Reports port level metrics.
  void ReportPortMetrics(Metrics* metrics);

  // Reports all metrics.
  virtual void ReportMetrics(Metrics* metrics, bool mode_entry_supported);

  // Sysfs path used to access partner PD information.
  base::FilePath syspath_;
  // Port number as described by the Type C connector class framework.
  int port_num_;
  std::unique_ptr<Cable> cable_;
  std::unique_ptr<Partner> partner_;
  // Tracks the user active state when a mode was last entered.
  bool user_active_on_mode_entry_;
  TypeCMode current_mode_;
  // Field which tracks whether port metrics have been reported. This
  // prevents duplicate reporting.
  bool metrics_reported_;
  // Indicates whether the port supports USB4 entry.
  bool supports_usb4_;
  DataRole data_role_;
  PowerRole power_role_;

  // Cancelable callback for metrics reporting.
  base::CancelableOnceClosure report_metrics_callback_;
};

}  // namespace typecd

#endif  // TYPECD_PORT_H_
