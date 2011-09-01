// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_
#define SHILL_WIFI_

#include <map>
#include <string>
#include <vector>

#include <dbus-c++/dbus.h>

#include "shill/device.h"
#include "shill/refptr_types.h"
#include "shill/shill_event.h"

namespace shill {

class SupplicantInterfaceProxyInterface;
class SupplicantProcessProxyInterface;
class WiFiService;

// WiFi class. Specialization of Device for WiFi.
class WiFi : public Device {
 public:
  WiFi(ControlInterface *control_interface,
       EventDispatcher *dispatcher,
       Manager *manager,
       const std::string &link,
       const std::string &address,
       int interface_index);
  virtual ~WiFi();
  virtual void Start();
  virtual void Stop();
  virtual void Scan();
  virtual bool TechnologyIs(const Technology type) const;
  virtual void LinkEvent(unsigned int flags, unsigned int change);

  // called by SupplicantInterfaceProxy, in response to events from
  // wpa_supplicant.
  void BSSAdded(const ::DBus::Path &BSS,
                const std::map<std::string, ::DBus::Variant> &properties);
  void ScanDone();

  // called by WiFiService
  void ConnectTo(WiFiService *service);

 private:
  typedef std::map<const std::string, WiFiEndpointRefPtr> EndpointMap;
  typedef std::map<const std::string, WiFiServiceRefPtr> ServiceMap;

  static const char kSupplicantPath[];
  static const char kSupplicantDBusAddr[];
  static const char kSupplicantWiFiDriver[];
  static const char kSupplicantErrorInterfaceExists[];
  static const char kSupplicantPropertySSID[];
  static const char kSupplicantPropertyNetworkMode[];
  static const char kSupplicantPropertyKeyMode[];
  static const char kSupplicantPropertyScanType[];
  static const char kSupplicantKeyModeNone[];
  static const char kSupplicantScanTypeActive[];

  void ScanDoneTask();
  void ScanTask();

  ScopedRunnableMethodFactory<WiFi> task_factory_;
  scoped_ptr<SupplicantProcessProxyInterface> supplicant_process_proxy_;
  scoped_ptr<SupplicantInterfaceProxyInterface> supplicant_interface_proxy_;
  EndpointMap endpoint_by_bssid_;
  ServiceMap service_by_private_id_;

  // Properties
  std::string bgscan_method_;
  uint16 bgscan_short_interval_;
  int32 bgscan_signal_threshold_;
  bool scan_pending_;
  uint16 scan_interval_;
  bool link_up_;

  friend class WiFiMainTest;  // access to supplicant_*_proxy_, link_up_
  DISALLOW_COPY_AND_ASSIGN(WiFi);
};

}  // namespace shill

#endif  // SHILL_WIFI_
