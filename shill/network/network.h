// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_H_
#define SHILL_NETWORK_NETWORK_H_

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/time/time.h>

#include "shill/connection.h"
#include "shill/ipconfig.h"
#include "shill/mockable.h"
#include "shill/net/ip_address.h"
#include "shill/net/rtnl_handler.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcp_provider.h"
#include "shill/network/slaac_controller.h"
#include "shill/technology.h"

namespace shill {

class EventDispatcher;
class Metrics;
class RoutingTable;
class Service;

// An object of Network class represents a network interface in the kernel, and
// maintains the layer 3 configuration on this interface.
// TODO(b/232177767): Currently this class is mainly a wrapper of the Connection
// class.
class Network {
 public:
  // The EventHandler is passed in in the constructor of Network to listen to
  // the events generated by Network. The object implements this interface must
  // have a longer life time that the Network object, e.g., that object can be
  // the owner of this Network object.
  class EventHandler {
   public:
    // Called every time when the network config on the connection is updated.
    // When this callback is called, the Network must be in a connected state,
    // but this signal does not always indicate a change from a non-connected
    // state to a connected state.
    // TODO(b/232177767): Currently this function will not be called if there is
    // an IPv6 update when IPv4 is working.
    virtual void OnConnectionUpdated() = 0;

    // Called when the Network becomes idle from a non-idle state (configuring
    // or connected), no matter if this state change is caused by a failure
    // (e.g., DHCP failure) or a user-initiate disconnect. |is_failure|
    // indicates this failure is triggered by a DHCP failure. Note that
    // currently this is the only failure type generated inside the Network
    // class.
    virtual void OnNetworkStopped(bool is_failure) = 0;

    // The IPConfig object lists held by this Network has changed.
    virtual void OnIPConfigsPropertyUpdated() = 0;

    // Called when a new DHCPv4 lease is obtained for this device. This is
    // called before OnConnectionUpdated() is called as a result of the lease
    // acquisition.
    virtual void OnGetDHCPLease() = 0;
    // Called when DHCPv4 fails to acquire a lease.
    virtual void OnGetDHCPFailure() = 0;
    // Called on when an IPv6 address is obtained from SLAAC. SLAAC is initiated
    // by the kernel when the link is connected and is currently not monitored
    // by shill. Derived class should implement this function to listen to this
    // event. Base class does nothing. This is called before
    // OnConnectionUpdated() is called and before captive portal detection is
    // started if IPv4 is not configured.
    virtual void OnGetSLAACAddress() = 0;

    // Called after IPv4 has been configured as a result of acquiring a new DHCP
    // lease. This is called after OnGetDHCPLease, OnIPConfigsPropertyUpdated,
    // and OnConnectionUpdated.
    virtual void OnIPv4ConfiguredWithDHCPLease() = 0;
    // Called after IPv6 has been configured as a result of acquiring an IPv6
    // address from the kernel when SLAAC completes. This is called after
    // OnGetSLAACAddress, OnIPConfigsPropertyUpdated, and OnConnectionUpdated
    // (if IPv4 is not yet configured).
    virtual void OnIPv6ConfiguredWithSLAACAddress() = 0;
  };

  // Options for starting a network.
  struct StartOptions {
    // Start DHCP client on this interface if |dhcp| is not empty.
    std::optional<DHCPProvider::Options> dhcp;
    // Accept router advertisements for IPv6.
    bool accept_ra = false;
  };

  // State for tracking the L3 connectivity (e.g., portal state is not
  // included).
  enum class State {
    // The Network is not started.
    kIdle,
    // The Network has been started. Waiting for IP configuration provisioned.
    kConfiguring,
    // The layer 3 connectivity has been established. At least one of IPv4 and
    // IPv6 configuration has been provisioned, and the other one can still be
    // in the configuring state.
    kConnected,
  };

  // Note that |event_handler| should live longer than the created Network
  // object, so usually it should be the owner of this object.
  explicit Network(int interface_index,
                   const std::string& interface_name,
                   Technology technology,
                   bool fixed_ip_params,
                   EventHandler* event_handler,
                   ControlInterface* control_interface,
                   EventDispatcher* dispatcher,
                   Metrics* metrics);
  Network(const Network&) = delete;
  Network& operator=(const Network&) = delete;
  virtual ~Network();

  // Starts the network with the given |options|.
  mockable void Start(const StartOptions& options);
  // Stops the network connection. OnNetworkStopped() will be called when
  // cleaning up the network state is finished.
  mockable void Stop();

  State state() const { return state_; }

  mockable bool IsConnected() const { return state_ == State::kConnected; }

  // Sets IPv4 properties specific to technology. Currently this is used by
  // cellular and VPN.
  mockable void set_link_protocol_ipv4_properties(
      std::unique_ptr<IPConfig::Properties> props) {
    link_protocol_ipv4_properties_ = std::move(props);
  }

  void set_link_protocol_ipv6_properties(
      std::unique_ptr<IPConfig::Properties> props) {
    link_protocol_ipv6_properties_ = std::move(props);
  }

  int interface_index() const { return interface_index_; }
  std::string interface_name() const { return interface_name_; }

  // Interfaces between Service and Network.
  // Callback invoked when the static IP properties configured on the selected
  // service changed.
  mockable void OnStaticIPConfigChanged(const NetworkConfig& config);
  // Register a callback that gets called when the |current_ipconfig_| changed.
  // This should only be used by Service.
  mockable void RegisterCurrentIPConfigChangeHandler(
      base::RepeatingClosure handler);
  // Returns the IPConfig object which is used to setup the Connection of this
  // Network. Returns nullptr if there is no such IPConfig.
  mockable IPConfig* GetCurrentIPConfig() const;
  // The NetworkConfig before applying the static one. Only needed by Service.
  const NetworkConfig& saved_network_config() const {
    return saved_network_config_;
  }

  // Functions for DHCP.
  // Initiate renewal of existing DHCP lease. Return false if the renewal failed
  // immediately, or we don't have active lease now.
  mockable bool RenewDHCPLease();
  // Destroy the lease, if any, with this |name|.
  // Called by the service during Unload() as part of the cleanup sequence.
  mockable void DestroyDHCPLease(const std::string& name);
  // Calculates the duration till a DHCP lease is due for renewal, and stores
  // this value in |result|. Returns std::nullopt if there is no upcoming DHCP
  // lease renewal, base::TimeDelta wrapped in std::optional otherwise.
  mockable std::optional<base::TimeDelta> TimeToNextDHCPLeaseRenewal();

  // Functions for IPv6.
  // TODO(b/232177767): Move StartIPv6() and StopIPv6() into private section.
  // Currently these two functions are only used in cellular.cc, and those
  // usages will not affect the logical state of Network.
  void StopIPv6();
  void StartIPv6();
  // Invalidate the IPv6 config kept in shill and wait for the new config from
  // the kernel.
  mockable void InvalidateIPv6Config();
  void set_ipv6_static_properties(std::unique_ptr<IPConfig::Properties> props) {
    ipv6_static_properties_ = std::move(props);
  }
  // Called by DeviceInfo.
  void EnableIPv6Privacy();

  // Returns a WeakPtr of the Network.
  base::WeakPtr<Network> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Routing policy rules have priorities, which establishes the order in which
  // policy rules will be matched against the current traffic. The higher the
  // priority value, the lower the priority of the rule. 0 is the highest rule
  // priority and is generally reserved for the kernel.
  //
  // Updates the kernel's routing policy rule database such that policy rules
  // corresponding to this Connection will use |priority| as the "base
  // priority". This call also updates the systemwide DNS configuration if
  // necessary, and triggers captive portal detection if the connection has
  // transitioned from non-default to default.
  //
  // This function should only be called when the Network is connected,
  // otherwise the call is a no-op.
  mockable void SetPriority(uint32_t priority, bool is_primary_physical);

  // Returns true if this Network is currently the systemwide default.
  mockable bool IsDefault() const;

  // Determines whether this Network controls the system DNS settings. This
  // should only be true for one Network at a time. This function should only be
  // called when the Network is connected, otherwise the call is a no-op.
  mockable void SetUseDNS(bool enable);

  // Returns all known (global) addresses of the Network. That includes IPv4
  // address from link protocol, or from DHCPv4, or from static IPv4
  // configuration; and IPv6 address from SLAAC and/or from link protocol.
  std::vector<IPAddress> GetAddresses() const;

  // Properties of the current IP config. Returns IPv4 properties if the Network
  // is dual-stack, and default (empty) values if the Network is not connected.
  mockable std::vector<std::string> dns_servers() const;
  mockable IPAddress local() const;
  IPAddress gateway() const;

  // TODO(b/232177767): This group of getters and setters are only exposed for
  // the purpose of refactor. New code outside Device should not use these.
  IPConfig* ipconfig() const { return ipconfig_.get(); }
  IPConfig* ip6config() const { return ip6config_.get(); }
  void set_ipconfig(std::unique_ptr<IPConfig> config) {
    ipconfig_ = std::move(config);
  }
  void set_ip6config(std::unique_ptr<IPConfig> config) {
    ip6config_ = std::move(config);
  }
  bool fixed_ip_params() const { return fixed_ip_params_; }

  // Only used in tests.
  void set_connection_for_testing(std::unique_ptr<Connection> connection) {
    connection_ = std::move(connection);
  }
  void set_fixed_ip_params_for_testing(bool val) { fixed_ip_params_ = val; }
  void set_dhcp_provider_for_testing(DHCPProvider* provider) {
    dhcp_provider_ = provider;
  }
  void set_routing_table_for_testing(RoutingTable* routing_table) {
    routing_table_ = routing_table;
  }
  void set_state_for_testing(State state) { state_ = state; }

 private:
  // TODO(b/232177767): Refactor DeviceTest to remove this dependency.
  friend class DeviceTest;
  // TODO(b/232177767): Refactor StaticIPParametersTest to remove this
  // dependency
  friend class StaticIPParametersTest;

  // Configures (or reconfigures) the associated Connection object with the
  // given IPConfig. When configuration is from SLAAC, set
  // |ipconfig->properties.method| to kTypeIPv6so that Connection skips address
  // configuration and only does routing policy setup.
  void SetupConnection(IPConfig* ipconfig);

  // Creates a Connection object can be used in this Network object. Isolate
  // this function only for unit tests, so that we can inject a mock Connection
  // object easily.
  mockable std::unique_ptr<Connection> CreateConnection() const;

  // Creates a SLAACController object. Isolated for unit test mock injection.
  mockable std::unique_ptr<SLAACController> CreateSLAACController();

  // Shuts down and clears all the running state of this network. If
  // |trigger_callback| is true and the Network is started, OnNetworkStopped()
  // will be invoked with |is_failure|.
  void StopInternal(bool is_failure, bool trigger_callback);

  // Functions for IPv4.
  // Triggers a reconfiguration on connection for an IPv4 config change.
  void OnIPv4ConfigUpdated();
  // Callback registered with DHCPController. Also see the comment for
  // DHCPController::UpdateCallback.
  void OnIPConfigUpdatedFromDHCP(const IPConfig::Properties& properties,
                                 bool new_lease_acquired);
  // Callback invoked on DHCP failures.
  void OnDHCPFailure();

  // Functions for IPv6.
  // Configures static IP address received from cellular bearer.
  void ConfigureStaticIPv6Address();
  // Called when IPv6 configuration changes.
  void OnIPv6ConfigUpdated();

  // Callback registered with SLAACController. |update_type| indicates the
  // update type (see comment in SLAACController declaration for detail).
  void OnUpdateFromSLAAC(SLAACController::UpdateType update_type);
  void OnIPv6AddressChanged();
  void OnIPv6DnsServerAddressesChanged();

  // Enable ARP filtering on the interface. Incoming ARP requests are responded
  // to only by the interface(s) owning the address. Outgoing ARP requests will
  // contain the best local address for the target.
  void EnableARPFiltering();

  // Set an IP configuration flag on the device. |family| should be "ipv6" or
  // "ipv4". |flag| should be the name of the flag to be set and |value| is
  // what this flag should be set to. Overridden by unit tests to pretend
  // writing to procfs.
  mockable bool SetIPFlag(IPAddress::Family family,
                          const std::string& flag,
                          const std::string& value);

  const int interface_index_;
  const std::string interface_name_;
  const Technology technology_;

  // If true, IP parameters should not be modified. This should not be changed
  // after a Network object is created. Make it modifiable just for unit tests.
  bool fixed_ip_params_;

  State state_ = State::kIdle;

  std::unique_ptr<Connection> connection_;

  std::unique_ptr<DHCPController> dhcp_controller_;
  std::unique_ptr<SLAACController> slaac_controller_;
  std::unique_ptr<IPConfig> ipconfig_;
  std::unique_ptr<IPConfig> ip6config_;

  base::RepeatingClosure current_ipconfig_change_handler_;
  // If not empty, |current_ipconfig_| should points to either |ipconfig_| or
  // |ip6config_| which is used to setup the connection. GetCurrentIPConfig()
  // should be used to get this property so that its validity can be checked.
  IPConfig* current_ipconfig_ = nullptr;

  // The technology-specific IPv4 config properties. Currently only used by
  // cellular and VPN. Assume that when this field is not empty, it must have
  // valid values to set up the connection (e.g., at least address and prefix
  // len).
  std::unique_ptr<IPConfig::Properties> link_protocol_ipv4_properties_;

  // The technology-specific IPv6 config properties.
  std::unique_ptr<IPConfig::Properties> link_protocol_ipv6_properties_;

  // TODO(b/227563210): We currently use ip6config() for IPv6 network properties
  // from SLAAC and this separated |ipv6_static_properties_| for static
  // configurations from cellular. This is temporary and only works because we
  // always expect a SLAAC config to be available (which will not be true for
  // VPN). Will come back to rework after the Device-Network refactor. Note that
  // in the current implementation this variable will not be reset by Network
  // class itself.
  std::unique_ptr<IPConfig::Properties> ipv6_static_properties_;

  // The static NetworkConfig from the associated Service.
  NetworkConfig static_network_config_;
  // The NetworkConfig before applying a static one. This will be used for 1)
  // able to restore the config to the previous state and 2) being exposed as a
  // Service property via D-Bus.
  NetworkConfig saved_network_config_;

  // Track the current same-net multi-home state.
  bool is_multi_homed_ = false;

  // Remember which flag files were previously successfully written. Only used
  // in SetIPFlag().
  std::set<std::string> written_flags_;

  EventHandler* event_handler_;

  // Other dependencies.
  ControlInterface* control_interface_;
  EventDispatcher* dispatcher_;
  Metrics* metrics_;

  // Cache singleton pointers for performance and test purposes.
  DHCPProvider* dhcp_provider_;
  RoutingTable* routing_table_;
  RTNLHandler* rtnl_handler_;

  base::WeakPtrFactory<Network> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_H_
