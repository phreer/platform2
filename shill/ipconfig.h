// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_IPCONFIG_H_
#define SHILL_IPCONFIG_H_

#include <memory>
#include <string>
#include <vector>

#include "shill/mockable.h"
#include "shill/net/ip_address.h"
#include "shill/network/network_config.h"
#include "shill/routing_policy_entry.h"
#include "shill/store/property_store.h"

namespace shill {
class ControlInterface;
class Error;
class IPConfigAdaptorInterface;

class IPConfig {
 public:
  struct Route {
    Route() {}
    Route(const std::string& host_in,
          int prefix_in,
          const std::string& gateway_in)
        : host(host_in), prefix(prefix_in), gateway(gateway_in) {}
    std::string host;
    int prefix = 0;
    std::string gateway;
  };

  struct Properties {
    // Whether this struct contains both IP address and DNS, and thus is ready
    // to be used for network connection.
    bool HasIPAddressAndDNS() const;

    NetworkConfig ToNetworkConfig() const;

    // Applies all non-empty properties in |network_config| to this object. This
    // function assumes that |this| is an IPv4 config.
    void UpdateFromNetworkConfig(const NetworkConfig& network_config);

    IPAddress::Family address_family = IPAddress::kFamilyUnknown;
    std::string address;
    int32_t subnet_prefix = 0;
    std::string broadcast_address;
    std::vector<std::string> dns_servers;
    std::string domain_name;
    std::vector<std::string> domain_search;
    std::string gateway;
    std::string method;
    // The address of the remote endpoint for pointopoint interfaces.
    // Note that presense of this field indicates that this is a p2p interface,
    // and a gateway won't be needed in creating routes on this interface.
    std::string peer_address;
    // List of uids that have their traffic blocked.
    std::vector<uint32_t> blackholed_uids;
    // Set the flag to true when the interface should be set as the default
    // route. This flag only affects IPv4.
    bool default_route = true;
    // A list of IP blocks in CIDR format that should be included on this
    // network.
    std::vector<std::string> inclusion_list;
    // A list of IP blocks in CIDR format that should be excluded from VPN.
    std::vector<std::string> exclusion_list;
    // Block IPv6 traffic.  Used if connected to an IPv4-only VPN.
    bool blackhole_ipv6 = false;
    // Should traffic whose source address matches one of this interface's
    // addresses be sent to the interface's per-device table. This field is only
    // used for non-physical interfaces--physical interfaces will always act as
    // if this were true.
    bool use_if_addrs = false;
    // MTU to set on the interface.  If unset, defaults to |kUndefinedMTU|.
    int32_t mtu = kUndefinedMTU;
    // Routes configured by the classless static routes option in DHCP. Traffic
    // sent to prefixes in this list will be routed through this connection,
    // even if it is not the default connection.
    std::vector<Route> dhcp_classless_static_routes;
    // Vendor encapsulated option string gained from DHCP.
    ByteArray vendor_encapsulated_options;
    // iSNS option data gained from DHCP.
    ByteArray isns_option_data;
    // Web Proxy Auto Discovery (WPAD) URL gained from DHCP.
    std::string web_proxy_auto_discovery;
    // Length of time the lease was granted.
    uint32_t lease_duration_seconds = 0;
  };

  enum Method { kMethodUnknown, kMethodPPP, kMethodStatic, kMethodDHCP };

  // Define a default and a minimum viable MTU value.
  static constexpr int kDefaultMTU = 1500;
  static constexpr int kMinIPv4MTU = 576;
  static constexpr int kMinIPv6MTU = 1280;
  static constexpr int kUndefinedMTU = 0;

  static constexpr char kTypeDHCP[] = "dhcp";

  IPConfig(ControlInterface* control_interface, const std::string& device_name);
  IPConfig(ControlInterface* control_interface,
           const std::string& device_name,
           const std::string& type);
  IPConfig(const IPConfig&) = delete;
  IPConfig& operator=(const IPConfig&) = delete;

  virtual ~IPConfig();

  const std::string& device_name() const { return device_name_; }
  const std::string& type() const { return type_; }
  uint32_t serial() const { return serial_; }

  const RpcIdentifier& GetRpcIdentifier() const;

  void set_properties(const Properties& props) { properties_ = props; }
  mockable const Properties& properties() const { return properties_; }

  // Update DNS servers setting for this ipconfig, this allows Chrome
  // to retrieve the new DNS servers.
  mockable void UpdateDNSServers(std::vector<std::string> dns_servers);

  // Reset the IPConfig properties to their default values.
  mockable void ResetProperties();

  // Updates the IP configuration properties and notifies listeners on D-Bus.
  void UpdateProperties(const Properties& properties);

  PropertyStore* mutable_store() { return &store_; }
  const PropertyStore& store() const { return store_; }

  // Applies |config| to this object and inform D-Bus listeners of the change.
  // Returns the current config before applying the incoming one.
  NetworkConfig ApplyNetworkConfig(const NetworkConfig& config);

  // Returns whether the function call changed the configuration.
  bool SetBlackholedUids(const std::vector<uint32_t>& uids);
  bool ClearBlackholedUids();

 private:
  friend class IPConfigTest;

  // Inform RPC listeners of changes to our properties. MAY emit
  // changes even on unchanged properties.
  mockable void EmitChanges();

  static uint32_t global_serial_;
  PropertyStore store_;
  const std::string device_name_;
  const std::string type_;
  const uint32_t serial_;
  std::unique_ptr<IPConfigAdaptorInterface> adaptor_;
  Properties properties_;
};

}  // namespace shill

#endif  // SHILL_IPCONFIG_H_
