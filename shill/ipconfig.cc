// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ipconfig.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/network/network_config.h"
#include "shill/static_ip_parameters.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kInet;
static std::string ObjectID(const IPConfig* i) {
  return i->GetRpcIdentifier().value();
}
}  // namespace Logging

namespace {

constexpr char kTypeIP[] = "ip";

template <class T>
void ApplyOptional(const std::optional<T>& src, T* dst) {
  if (src.has_value()) {
    *dst = src.value();
  }
}

}  // namespace

bool IPConfig::Properties::HasIPAddressAndDNS() const {
  return !address.empty() && !dns_servers.empty();
}

NetworkConfig IPConfig::Properties::ToNetworkConfig() const {
  NetworkConfig ret;

  if (mtu != IPConfig::kUndefinedMTU) {
    ret.mtu = mtu;
  }
  ret.dns_search_domains = domain_search;
  ret.dns_servers = dns_servers;

  if (address_family != IPAddress::kFamilyIPv4 &&
      address_family != IPAddress::kFamilyIPv6) {
    LOG(INFO) << "The input IPConfig::Properties does not have an valid "
                 "family. Skip setting family-specific fields.";
    return ret;
  }

  NetworkConfig::RouteProperties* route_props = nullptr;
  switch (address_family) {
    case IPAddress::kFamilyIPv4:
      if (!address.empty()) {
        ret.ipv4_address_cidr =
            base::StrCat({address, "/", base::NumberToString(subnet_prefix)});
      }
      ret.ipv4_default_route = default_route;
      route_props = &ret.ipv4_route;
      break;
    case IPAddress::kFamilyIPv6:
      if (!address.empty()) {
        ret.ipv6_address_cidrs = std::vector<std::string>{};
        ret.ipv6_address_cidrs->push_back(
            base::StrCat({address, "/", base::NumberToString(subnet_prefix)}));
      }
      route_props = &ret.ipv6_route;
      break;
    default:
      LOG(INFO) << "The input IPConfig::Properties does not have an valid "
                   "family. Skip setting family-specific fields.";
      return ret;
  }

  route_props->gateway = gateway;
  route_props->included_route_prefixes = inclusion_list;
  route_props->excluded_route_prefixes = exclusion_list;

  return ret;
}

void IPConfig::Properties::UpdateFromNetworkConfig(
    const NetworkConfig& network_config) {
  if (address_family != IPAddress::kFamilyIPv4) {
    LOG(DFATAL) << "The IPConfig object is not for IPv4, but for "
                << address_family;
    return;
  }

  if (network_config.ipv4_address_cidr.has_value()) {
    IPAddress addr(IPAddress::kFamilyIPv4);
    if (addr.SetAddressAndPrefixFromString(
            network_config.ipv4_address_cidr.value())) {
      address = addr.ToString();
      subnet_prefix = addr.prefix();
    } else {
      LOG(ERROR) << "ipv4_address_cidr does not have a valid value "
                 << network_config.ipv4_address_cidr.value();
    }
  }
  ApplyOptional(network_config.ipv4_default_route, &default_route);
  ApplyOptional(network_config.ipv4_route.gateway, &gateway);
  ApplyOptional(network_config.ipv4_route.included_route_prefixes,
                &inclusion_list);
  ApplyOptional(network_config.ipv4_route.excluded_route_prefixes,
                &exclusion_list);
  ApplyOptional(network_config.mtu, &mtu);
  ApplyOptional(network_config.dns_servers, &dns_servers);
  ApplyOptional(network_config.dns_search_domains, &domain_search);
}

// static
uint32_t IPConfig::global_serial_ = 0;

IPConfig::IPConfig(ControlInterface* control_interface,
                   const std::string& device_name)
    : IPConfig(control_interface, device_name, kTypeIP) {}

IPConfig::IPConfig(ControlInterface* control_interface,
                   const std::string& device_name,
                   const std::string& type)
    : device_name_(device_name),
      type_(type),
      serial_(global_serial_++),
      adaptor_(control_interface->CreateIPConfigAdaptor(this)) {
  store_.RegisterConstString(kAddressProperty, &properties_.address);
  store_.RegisterConstString(kBroadcastProperty,
                             &properties_.broadcast_address);
  store_.RegisterConstString(kDomainNameProperty, &properties_.domain_name);
  store_.RegisterConstString(kGatewayProperty, &properties_.gateway);
  store_.RegisterConstString(kMethodProperty, &properties_.method);
  store_.RegisterConstInt32(kMtuProperty, &properties_.mtu);
  store_.RegisterConstStrings(kNameServersProperty, &properties_.dns_servers);
  store_.RegisterConstString(kPeerAddressProperty, &properties_.peer_address);
  store_.RegisterConstInt32(kPrefixlenProperty, &properties_.subnet_prefix);
  store_.RegisterConstStrings(kSearchDomainsProperty,
                              &properties_.domain_search);
  store_.RegisterConstByteArray(kVendorEncapsulatedOptionsProperty,
                                &properties_.vendor_encapsulated_options);
  store_.RegisterConstString(kWebProxyAutoDiscoveryUrlProperty,
                             &properties_.web_proxy_auto_discovery);
  store_.RegisterConstUint32(kLeaseDurationSecondsProperty,
                             &properties_.lease_duration_seconds);
  store_.RegisterConstByteArray(kiSNSOptionDataProperty,
                                &properties_.isns_option_data);
  SLOG(this, 2) << __func__ << " device: " << device_name_;
}

IPConfig::~IPConfig() {
  SLOG(this, 2) << __func__ << " device: " << device_name();
}

const RpcIdentifier& IPConfig::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

void IPConfig::ApplyStaticIPParameters(
    StaticIPParameters* static_ip_parameters) {
  static_ip_parameters->ApplyTo(&properties_);
  EmitChanges();
}

void IPConfig::RestoreSavedIPParameters(
    StaticIPParameters* static_ip_parameters) {
  static_ip_parameters->RestoreTo(&properties_);
  EmitChanges();
}

bool IPConfig::SetBlackholedUids(const std::vector<uint32_t>& uids) {
  if (properties_.blackholed_uids == uids) {
    return false;
  }
  properties_.blackholed_uids = uids;
  return true;
}

bool IPConfig::ClearBlackholedUids() {
  return SetBlackholedUids(std::vector<uint32_t>());
}

void IPConfig::UpdateProperties(const Properties& properties) {
  properties_ = properties;
  EmitChanges();
}

void IPConfig::UpdateDNSServers(std::vector<std::string> dns_servers) {
  properties_.dns_servers = std::move(dns_servers);
  EmitChanges();
}

void IPConfig::ResetProperties() {
  properties_ = Properties();
  EmitChanges();
}

void IPConfig::EmitChanges() {
  adaptor_->EmitStringChanged(kAddressProperty, properties_.address);
  adaptor_->EmitStringsChanged(kNameServersProperty, properties_.dns_servers);
}

}  // namespace shill
