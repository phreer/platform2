// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_PATCHPANEL_ADAPTOR_H_
#define PATCHPANEL_PATCHPANEL_ADAPTOR_H_

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <metrics/metrics_library.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/dbus_adaptors/org.chromium.patchpanel.h"
#include "patchpanel/manager.h"
#include "patchpanel/metrics.h"
#include "patchpanel/system.h"

namespace shill {
class ProcessManager;
}  // namespace shill

namespace patchpanel {

// Delegates the D-Bus binding, which is generated by chromeos-dbus-binding, to
// the core implementation of the patchpanel service.
class PatchpanelAdaptor : public org::chromium::PatchPanelInterface,
                          public org::chromium::PatchPanelAdaptor,
                          public Manager::ClientNotifier {
 public:
  PatchpanelAdaptor(const base::FilePath& cmd_path,
                    scoped_refptr<::dbus::Bus> bus,
                    System* system,
                    shill::ProcessManager* process_manager,
                    MetricsLibraryInterface* metrics,
                    std::unique_ptr<RTNLClient> rtnl_client);

  PatchpanelAdaptor(const PatchpanelAdaptor&) = delete;
  PatchpanelAdaptor& operator=(const PatchpanelAdaptor&) = delete;

  // Register the D-Bus methods to the D-Bus daemon.
  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  // Implements org::chromium::PatchPanelInterface, which are mapping to the
  // exported D-Bus methods.
  ArcShutdownResponse ArcShutdown(const ArcShutdownRequest& request) override;
  ArcStartupResponse ArcStartup(const ArcStartupRequest& request) override;
  ArcVmShutdownResponse ArcVmShutdown(
      const ArcVmShutdownRequest& request) override;
  ArcVmStartupResponse ArcVmStartup(
      const ArcVmStartupRequest& request) override;
  ConnectNamespaceResponse ConnectNamespace(
      const ConnectNamespaceRequest& request,
      const base::ScopedFD& client_fd) override;
  LocalOnlyNetworkResponse CreateLocalOnlyNetwork(
      const LocalOnlyNetworkRequest& request,
      const base::ScopedFD& client_fd) override;
  TetheredNetworkResponse CreateTetheredNetwork(
      const TetheredNetworkRequest& request,
      const base::ScopedFD& client_fd) override;
  GetDevicesResponse GetDevices(
      const GetDevicesRequest& request) const override;
  GetDownstreamNetworkInfoResponse GetDownstreamNetworkInfo(
      const GetDownstreamNetworkInfoRequest& request) const override;
  TrafficCountersResponse GetTrafficCounters(
      const TrafficCountersRequest& request) const override;
  ModifyPortRuleResponse ModifyPortRule(
      const ModifyPortRuleRequest& request) override;
  ParallelsVmShutdownResponse ParallelsVmShutdown(
      const ParallelsVmShutdownRequest& request) override;
  ParallelsVmStartupResponse ParallelsVmStartup(
      const ParallelsVmStartupRequest& request) override;
  SetDnsRedirectionRuleResponse SetDnsRedirectionRule(
      const SetDnsRedirectionRuleRequest& request,
      const base::ScopedFD& client_fd) override;
  SetVpnIntentResponse SetVpnIntent(const SetVpnIntentRequest& request,
                                    const base::ScopedFD& socket_fd) override;
  SetVpnLockdownResponse SetVpnLockdown(
      const SetVpnLockdownRequest& request) override;
  TerminaVmShutdownResponse TerminaVmShutdown(
      const TerminaVmShutdownRequest& request) override;
  TerminaVmStartupResponse TerminaVmStartup(
      const TerminaVmStartupRequest& request) override;

  NotifyAndroidWifiMulticastLockChangeResponse
  NotifyAndroidWifiMulticastLockChange(
      const NotifyAndroidWifiMulticastLockChangeRequest& request) override;

  NotifyAndroidInteractiveStateResponse NotifyAndroidInteractiveState(
      const NotifyAndroidInteractiveStateRequest& request) override;

  // Implements Manager::ClientNotifier, which are mapping to the exported D-Bus
  // signals.
  void OnNetworkDeviceChanged(const Device& virtual_device,
                              Device::ChangeEvent event) override;
  void OnNetworkConfigurationChanged() override;
  void OnNeighborReachabilityEvent(
      int ifindex,
      const net_base::IPAddress& ip_addr,
      NeighborLinkMonitor::NeighborRole role,
      NeighborReachabilityEventSignal::EventType event_type) override;

 private:
  void RecordDbusEvent(DbusUmaEvent event) const;

  brillo::dbus_utils::DBusObject dbus_object_;

  // UMA metrics client. The caller should guarantee it outlives this
  // PatchpanelAdaptor instance.
  MetricsLibraryInterface* metrics_;

  // The core logic of patchpanel.
  std::unique_ptr<Manager> manager_;
};

}  // namespace patchpanel
#endif  // PATCHPANEL_PATCHPANEL_ADAPTOR_H_
