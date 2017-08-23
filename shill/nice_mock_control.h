//
// Copyright (C) 2012 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef SHILL_NICE_MOCK_CONTROL_H_
#define SHILL_NICE_MOCK_CONTROL_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/control_interface.h"
#include "shill/dhcp/dhcp_proxy_interface.h"
#include "shill/dhcp/dhcpcd_listener_interface.h"
#include "shill/firewall_proxy_interface.h"
#include "shill/power_manager_proxy_interface.h"
#include "shill/upstart/upstart_proxy_interface.h"

#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
#include "shill/supplicant/supplicant_process_proxy_interface.h"
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

#if !defined(DISABLE_WIMAX)
#include "shill/wimax/wimax_device_proxy_interface.h"
#include "shill/wimax/wimax_manager_proxy_interface.h"
#endif  // DISABLE_WIMAX

namespace shill {
// An implementation of the Shill RPC-channel-interface-factory interface that
// returns nice mocks.
class NiceMockControl : public ControlInterface {
 public:
  NiceMockControl();
  ~NiceMockControl() override;

  void RegisterManagerObject(
      Manager* manager,
      const base::Closure& registration_done_callback) override {};

  // Each of these can be called once.  Ownership of the appropriate
  // interface pointer is given up upon call.
  std::unique_ptr<DeviceAdaptorInterface> CreateDeviceAdaptor(
      Device* device) override;
  std::unique_ptr<IPConfigAdaptorInterface> CreateIPConfigAdaptor(
      IPConfig* config) override;
  std::unique_ptr<ManagerAdaptorInterface> CreateManagerAdaptor(
      Manager* manager) override;
  std::unique_ptr<ProfileAdaptorInterface> CreateProfileAdaptor(
      Profile* profile) override;
  std::unique_ptr<RPCTaskAdaptorInterface> CreateRPCTaskAdaptor(
      RPCTask* task) override;
  std::unique_ptr<ServiceAdaptorInterface> CreateServiceAdaptor(
      Service* service) override;
#ifndef DISABLE_VPN
  std::unique_ptr<ThirdPartyVpnAdaptorInterface> CreateThirdPartyVpnAdaptor(
      ThirdPartyVpnDriver* driver) override;
#endif
  const std::string& NullRPCIdentifier() override;

  MOCK_METHOD2(CreateRPCServiceWatcher,
               RPCServiceWatcherInterface*(
                   const std::string& connection_name,
                   const base::Closure& on_connection_vanished));

  MOCK_METHOD3(CreatePowerManagerProxy,
               std::unique_ptr<PowerManagerProxyInterface>(
                   PowerManagerProxyDelegate* delegate,
                   const base::Closure& service_appeared_callback,
                   const base::Closure& service_vanished_callback));
#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  MOCK_METHOD2(CreateSupplicantProcessProxy,
               std::unique_ptr<SupplicantProcessProxyInterface>(
                   const base::Closure& service_appeared_callback,
                   const base::Closure& service_vanished_callback));
  MOCK_METHOD2(CreateSupplicantInterfaceProxy,
               SupplicantInterfaceProxyInterface*(
                   SupplicantEventDelegateInterface* delegate,
                   const std::string& object_path));
  MOCK_METHOD1(CreateSupplicantNetworkProxy,
               SupplicantNetworkProxyInterface*(
                   const std::string& object_path));
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X
#if !defined(DISABLE_WIFI)
  MOCK_METHOD2(CreateSupplicantBSSProxy,
               SupplicantBSSProxyInterface*(WiFiEndpoint* wifi_endpoint,
                                            const std::string& object_path));
#endif  // DISABLE_WIFI
  MOCK_METHOD1(
      CreateDHCPCDListener,
      std::unique_ptr<DHCPCDListenerInterface>(DHCPProvider* provider));
  MOCK_METHOD1(CreateDHCPProxy,
               std::unique_ptr<DHCPProxyInterface>(const std::string& service));

  MOCK_METHOD0(CreateUpstartProxy, std::unique_ptr<UpstartProxyInterface>());

  MOCK_METHOD0(CreateFirewallProxy, std::unique_ptr<FirewallProxyInterface>());

#if !defined(DISABLE_CELLULAR)
  MOCK_METHOD2(CreateDBusPropertiesProxy,
               DBusPropertiesProxyInterface*(const std::string& path,
                                             const std::string& service));

  MOCK_METHOD4(CreateDBusObjectManagerProxy,
               DBusObjectManagerProxyInterface*(
                   const std::string& path,
                   const std::string& service,
                   const base::Closure& service_appeared_callback,
                   const base::Closure& service_vanished_callback));
  MOCK_METHOD5(CreateModemManagerProxy,
               ModemManagerProxyInterface*(
                   ModemManagerClassic* manager,
                   const std::string& path,
                   const std::string& service,
                   const base::Closure& service_appeared_callback,
                   const base::Closure& service_vanished_callback));
  MOCK_METHOD2(CreateModemProxy,
               ModemProxyInterface*(const std::string& path,
                                    const std::string& service));
  MOCK_METHOD2(CreateModemSimpleProxy,
               ModemSimpleProxyInterface*(const std::string& path,
                                          const std::string& service));

  MOCK_METHOD2(CreateModemCDMAProxy,
               ModemCDMAProxyInterface*(const std::string& path,
                                        const std::string& service));
  MOCK_METHOD2(CreateModemGSMCardProxy,
               ModemGSMCardProxyInterface*(const std::string& path,
                                           const std::string& service));
  MOCK_METHOD2(CreateModemGSMNetworkProxy,
               ModemGSMNetworkProxyInterface*(const std::string& path,
                                              const std::string& service));
  MOCK_METHOD2(CreateModemGobiProxy,
               ModemGobiProxyInterface*(const std::string& path,
                                        const std::string& service));
  MOCK_METHOD2(CreateMM1ModemLocationProxy,
               mm1::ModemLocationProxyInterface*(const std::string& path,
                                                  const std::string& service));
  MOCK_METHOD2(CreateMM1ModemModem3gppProxy,
               mm1::ModemModem3gppProxyInterface*(const std::string& path,
                                                  const std::string& service));
  MOCK_METHOD2(CreateMM1ModemModemCdmaProxy,
               mm1::ModemModemCdmaProxyInterface*(const std::string& path,
                                                  const std::string& service));
  MOCK_METHOD2(CreateMM1ModemProxy,
               mm1::ModemProxyInterface*(const std::string& path,
                                         const std::string& service));
  MOCK_METHOD2(CreateMM1ModemSimpleProxy,
               mm1::ModemSimpleProxyInterface*(const std::string& path,
                                               const std::string& service));
  MOCK_METHOD2(CreateSimProxy,
               mm1::SimProxyInterface*(const std::string& path,
                                       const std::string& service));
#endif  // DISABLE_CELLULAR

#if !defined(DISABLE_WIMAX)
  MOCK_METHOD1(
      CreateWiMaxDeviceProxy,
      std::unique_ptr<WiMaxDeviceProxyInterface>(const std::string& path));
  MOCK_METHOD2(CreateWiMaxManagerProxy,
               std::unique_ptr<WiMaxManagerProxyInterface>(
                   const base::Closure& service_appeared_callback,
                   const base::Closure& service_vanished_callback));
  MOCK_METHOD1(CreateWiMaxNetworkProxy,
               WiMaxNetworkProxyInterface*(const std::string& path));
#endif  // DISABLE_WIMAX

 private:
  std::string null_identifier_;

  DISALLOW_COPY_AND_ASSIGN(NiceMockControl);
};

}  // namespace shill

#endif  // SHILL_NICE_MOCK_CONTROL_H_
