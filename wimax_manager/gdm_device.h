// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WIMAX_MANAGER_GDM_DEVICE_H_
#define WIMAX_MANAGER_GDM_DEVICE_H_

extern "C" {
#include <gct/gctapi.h>
}  // extern "C"

#include <string>

#include <base/basictypes.h>
#include <base/memory/weak_ptr.h>
#include <base/timer/timer.h>
#include <gtest/gtest_prod.h>

#include "wimax_manager/device.h"

namespace wimax_manager {

class EAPParameters;
class GdmDriver;

class GdmDevice : public Device {
 public:
  GdmDevice(Manager *manager, uint8 index, const std::string &name,
            const base::WeakPtr<GdmDriver> &driver);
  virtual ~GdmDevice();

  virtual bool Enable();
  virtual bool Disable();
  virtual bool ScanNetworks();
  virtual bool UpdateStatus();
  virtual bool Connect(const Network &network,
                       const base::DictionaryValue &parameters);
  virtual bool Disconnect();

  void OnNetworkScan();
  void OnStatusUpdate();
  void OnDBusAdaptorStatusUpdate();
  void CancelConnectOnTimeout();
  void RestoreStatusUpdateInterval();

 protected:
  virtual void UpdateNetworkScanInterval(uint32 network_scan_interval);
  virtual void UpdateStatusUpdateInterval(uint32 status_update_interval);

 private:
  friend class GdmDriver;
  FRIEND_TEST(GdmDeviceTest, ConstructEAPParametersUsingConnectParameters);
  FRIEND_TEST(GdmDeviceTest, ConstructEAPParametersUsingOperatorEAPParameters);
  FRIEND_TEST(GdmDeviceTest,
              ConstructEAPParametersWithAnonymousIdentityUpdated);
  FRIEND_TEST(GdmDeviceTest, ConstructEAPParametersWithInvalidEAPParameters);
  FRIEND_TEST(GdmDeviceTest, ConstructEAPParametersWithoutEAPParameters);

  bool Open();
  bool Close();
  void ClearCurrentConnectionProfile();

  static bool ConstructEAPParameters(
      const base::DictionaryValue &connect_parameters,
      const EAPParameters &operator_eap_parameters,
      GCT_API_EAP_PARAM *eap_parameters);

  EAPParameters GetNetworkOperatorEAPParameters(const Network &network) const;

  void set_connection_progress(
      WIMAX_API_CONNECTION_PROGRESS_INFO connection_progress) {
    connection_progress_ = connection_progress;
  }

  base::WeakPtr<GdmDriver> driver_;
  bool open_;
  WIMAX_API_CONNECTION_PROGRESS_INFO connection_progress_;
  base::OneShotTimer<GdmDevice> connect_timeout_timer_;
  base::OneShotTimer<GdmDevice> initial_network_scan_timer_;
  base::RepeatingTimer<GdmDevice> network_scan_timer_;
  base::RepeatingTimer<GdmDevice> status_update_timer_;
  base::OneShotTimer<GdmDevice> dbus_adaptor_status_update_timer_;
  base::OneShotTimer<GdmDevice> restore_status_update_interval_timer_;
  bool restore_status_update_interval_;
  Network::Identifier current_network_identifier_;
  std::string current_user_identity_;

  DISALLOW_COPY_AND_ASSIGN(GdmDevice);
};

}  // namespace wimax_manager

#endif  // WIMAX_MANAGER_GDM_DEVICE_H_
