// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/proto_utils.h"

namespace patchpanel {

void FillDeviceProto(const Device& virtual_device,
                     patchpanel::NetworkDevice* output) {
  // TODO(hugobenichi) Consolidate guest_type in Device class and set
  // guest_type.
  output->set_ifname(virtual_device.host_ifname());
  output->set_phys_ifname(virtual_device.phys_ifname());
  output->set_guest_ifname(virtual_device.guest_ifname());
  output->set_ipv4_addr(virtual_device.config().guest_ipv4_addr());
  output->set_host_ipv4_addr(virtual_device.config().host_ipv4_addr());
}

// TODO(b/239559602) Introduce better types for IPv4 addr and IPv4 CIDR.
void FillSubnetProto(const uint32_t base_addr,
                     int prefix_length,
                     patchpanel::IPv4Subnet* output) {
  output->set_addr(&base_addr, sizeof(base_addr));
  output->set_base_addr(base_addr);
  output->set_prefix_len(static_cast<uint32_t>(prefix_length));
}

void FillSubnetProto(const Subnet& virtual_subnet,
                     patchpanel::IPv4Subnet* output) {
  FillSubnetProto(virtual_subnet.BaseAddress(), virtual_subnet.PrefixLength(),
                  output);
}

void FillDeviceDnsProxyProto(
    const Device& virtual_device,
    patchpanel::NetworkDevice* output,
    const std::map<std::string, std::string>& ipv4_addrs,
    const std::map<std::string, std::string>& ipv6_addrs) {
  const auto& ipv4_it = ipv4_addrs.find(virtual_device.host_ifname());
  if (ipv4_it != ipv4_addrs.end()) {
    in_addr ipv4_addr = StringToIPv4Address(ipv4_it->second);
    output->set_dns_proxy_ipv4_addr(reinterpret_cast<const char*>(&ipv4_addr),
                                    sizeof(in_addr));
  }
  const auto& ipv6_it = ipv6_addrs.find(virtual_device.host_ifname());
  if (ipv6_it != ipv6_addrs.end()) {
    in6_addr ipv6_addr = StringToIPv6Address(ipv6_it->second);
    output->set_dns_proxy_ipv6_addr(reinterpret_cast<const char*>(&ipv6_addr),
                                    sizeof(in6_addr));
  }
}

void FillDownstreamNetworkProto(
    const DownstreamNetworkInfo& downstream_network_info,
    patchpanel::DownstreamNetwork* output) {
  output->set_downstream_ifname(downstream_network_info.downstream_ifname);
  output->set_ipv4_gateway_addr(&downstream_network_info.ipv4_addr,
                                sizeof(downstream_network_info.ipv4_addr));
  FillSubnetProto(downstream_network_info.ipv4_base_addr,
                  downstream_network_info.ipv4_prefix_length,
                  output->mutable_ipv4_subnet());
}

}  // namespace patchpanel