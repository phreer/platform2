// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/connection.h"

#include <arpa/inet.h>
#include <linux/rtnetlink.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "shill/ipconfig.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_resolver.h"
#include "shill/mock_routing_table.h"
#include "shill/net/mock_rtnl_handler.h"
#include "shill/routing_policy_entry.h"
#include "shill/routing_table_entry.h"

using testing::_;
using testing::AnyNumber;
using testing::Mock;
using testing::Return;
using testing::StrictMock;
using testing::Test;
using testing::WithArg;

namespace shill {

namespace {
const int kDeviceInterfaceIndexBase = 100;

const char kIPAddress0[] = "192.168.1.1";
const char kGatewayAddress0[] = "192.168.1.254";
const char kBroadcastAddress0[] = "192.168.1.255";
const char kNameServer0[] = "8.8.8.8";
const char kNameServer1[] = "8.8.9.9";
const int32_t kPrefix0 = 24;
const int32_t kPrefix1 = 31;
const char kSearchDomain0[] = "chromium.org";
const char kSearchDomain1[] = "google.com";
const char kIPv6Address[] = "2001:db8::1";
const char kIPv6NameServer0[] = "2001:db9::1";
const char kIPv6NameServer1[] = "2001:db9::2";

MATCHER_P2(IsIPAddress, address, prefix, "") {
  IPAddress match_address(address);
  match_address.set_prefix(prefix);
  return match_address.Equals(arg);
}

MATCHER_P(IsIPv6Address, address, "") {
  IPAddress match_address(address);
  return match_address.Equals(arg);
}

MATCHER(IsDefaultAddress, "") {
  IPAddress match_address(arg);
  return match_address.IsDefault();
}

MATCHER_P(IsValidRoutingTableEntry, dst, "") {
  return dst.Equals(arg.dst);
}

MATCHER_P(IsValidThrowRoute, dst, "") {
  return dst.Equals(arg.dst) && arg.type == RTN_THROW;
}

MATCHER_P2(IsValidRoutingRule, family, priority, "") {
  return arg.family == family && arg.priority == priority;
}

MATCHER_P3(IsValidUidRule, family, priority, uid, "") {
  return arg.family == family && arg.priority == priority &&
         arg.uid_range == fib_rule_uid_range{uid, uid};
}

MATCHER_P3(IsValidIifRule, family, priority, iif, "") {
  return arg.family == family && arg.priority == priority &&
         arg.iif_name == iif;
}

MATCHER_P3(IsValidOifRule, family, priority, oif, "") {
  return arg.family == family && arg.priority == priority &&
         arg.oif_name == oif;
}

MATCHER_P(IsLinkRouteTo, dst, "") {
  return dst.HasSameAddressAs(arg.dst) &&
         arg.dst.prefix() ==
             IPAddress::GetMaxPrefixLength(IPAddress::kFamilyIPv4) &&
         !arg.src.IsValid() && !arg.gateway.IsValid() &&
         arg.scope == RT_SCOPE_LINK;
}

}  // namespace

class ConnectionTest : public Test {
 public:
  ConnectionTest()
      : manager_(&control_, nullptr, nullptr),
        device_info_(new StrictMock<MockDeviceInfo>(&manager_)),
        connection_(nullptr),
        ipconfig_(new IPConfig(&control_, "")),
        ip6config_(new IPConfig(&control_, "")),
        local_address_(IPAddress::kFamilyIPv4),
        broadcast_address_(IPAddress::kFamilyIPv4),
        gateway_address_(IPAddress::kFamilyIPv4),
        default_address_(IPAddress::kFamilyIPv4),
        local_ipv6_address_(IPAddress::kFamilyIPv6) {}

  void SetUp() override {
    properties_.address = kIPAddress0;
    properties_.subnet_prefix = kPrefix0;
    properties_.gateway = kGatewayAddress0;
    properties_.broadcast_address = kBroadcastAddress0;
    properties_.dns_servers = {kNameServer0, kNameServer1};
    properties_.domain_search = {kSearchDomain0, kSearchDomain1};
    properties_.address_family = IPAddress::kFamilyIPv4;
    UpdateProperties();

    ipv6_properties_.address = kIPv6Address;
    ipv6_properties_.dns_servers = {kIPv6NameServer0, kIPv6NameServer1};
    ipv6_properties_.address_family = IPAddress::kFamilyIPv6;
    UpdateIPv6Properties();

    EXPECT_TRUE(local_address_.SetAddressFromString(kIPAddress0));
    EXPECT_TRUE(broadcast_address_.SetAddressFromString(kBroadcastAddress0));
    EXPECT_TRUE(gateway_address_.SetAddressFromString(kGatewayAddress0));
    EXPECT_TRUE(local_ipv6_address_.SetAddressFromString(kIPv6Address));
  }

  void TearDown() override {
    if (connection_) {
      AddDestructorExpectations();
      connection_ = nullptr;
    }
  }

  void ReplaceSingletons(ConnectionRefPtr connection) {
    connection->resolver_ = &resolver_;
    connection->routing_table_ = &routing_table_;
    connection->rtnl_handler_ = &rtnl_handler_;
  }

  void UpdateProperties() { ipconfig_->UpdateProperties(properties_, true); }

  void UpdateIPv6Properties() {
    ip6config_->UpdateProperties(ipv6_properties_, true);
  }

  const IPAddress& GetLocalAddress(ConnectionRefPtr connection) {
    return connection->local_;
  }

  const IPAddress& GetGatewayAddress(ConnectionRefPtr connection) {
    return connection->gateway_;
  }

  bool GetHasBroadcastDomain(ConnectionRefPtr connection) {
    return connection->has_broadcast_domain_;
  }

  void SetLocal(const IPAddress& local) { connection_->local_ = local; }

  scoped_refptr<MockDevice> CreateDevice(Technology technology) {
    scoped_refptr<MockDevice> device = new StrictMock<MockDevice>(
        &manager_, "test_" + technology.GetName(), std::string(),
        kDeviceInterfaceIndexBase + static_cast<int>(technology));
    EXPECT_CALL(*device, technology()).WillRepeatedly(Return(technology));
    EXPECT_CALL(*device_info_, GetDevice(device->interface_index()))
        .WillRepeatedly(Return(device));
    ON_CALL(*device_info_, GetAddresses(device->interface_index(), _))
        .WillByDefault(WithArg<1>(testing::Invoke(
            [](std::vector<DeviceInfo::AddressData>* addresses) {
              *addresses = {
                  DeviceInfo::AddressData(IPAddress(kIPAddress0), 0, 0),
              };
              return true;
            })));
    return device;
  }

 protected:
  class DisconnectCallbackTarget {
   public:
    DisconnectCallbackTarget()
        : callback_(base::Bind(&DisconnectCallbackTarget::CallTarget,
                               base::Unretained(this))) {}

    MOCK_METHOD0(CallTarget, void());
    const base::Closure& callback() const { return callback_; }

   private:
    base::Closure callback_;
  };

  void AddDestructorExpectations() {
    ASSERT_NE(connection_, nullptr);
    EXPECT_CALL(routing_table_, FlushRoutes(connection_->interface_index_));
    EXPECT_CALL(routing_table_,
                FlushRoutesWithTag(connection_->interface_index_));
    EXPECT_CALL(routing_table_, FlushRules(connection_->interface_index_));
    EXPECT_CALL(routing_table_, FreeTableId(connection_->table_id_));
    if (connection_->fixed_ip_params_) {
      EXPECT_CALL(*device_info_, FlushAddresses(connection_->interface_index_))
          .Times(0);
    } else {
      EXPECT_CALL(*device_info_, FlushAddresses(connection_->interface_index_));
    }
  }

  void AddNonPhysicalRoutingPolicyExpectations(DeviceRefPtr device,
                                               uint32_t priority) {
    EXPECT_CALL(routing_table_, FlushRules(device->interface_index()));

    EXPECT_CALL(routing_table_,
                AddRule(device->interface_index(),
                        IsValidOifRule(IPAddress::kFamilyIPv4, priority,
                                       device->link_name())))
        .WillOnce(Return(true));
    EXPECT_CALL(routing_table_,
                AddRule(device->interface_index(),
                        IsValidOifRule(IPAddress::kFamilyIPv6, priority,
                                       device->link_name())))
        .WillOnce(Return(true));
  }

  void AddPhysicalRoutingPolicyExpectations(DeviceRefPtr device,
                                            uint32_t priority,
                                            bool is_primary_physical) {
    EXPECT_CALL(*device_info_, GetAddresses(device->interface_index(), _))
        .Times(testing::AnyNumber());

    EXPECT_CALL(routing_table_, FlushRules(device->interface_index()));

    // Primary physical interface will create catch-all for IPv4 and v6.
    // It will also add a main routing table rule above its other rules for both
    // IPv4 and v6.
    if (is_primary_physical) {
      EXPECT_CALL(
          routing_table_,
          AddRule(device->interface_index(),
                  IsValidRoutingRule(IPAddress::kFamilyIPv4, priority - 1)))
          .WillOnce(Return(true));
      EXPECT_CALL(
          routing_table_,
          AddRule(device->interface_index(),
                  IsValidRoutingRule(IPAddress::kFamilyIPv6, priority - 1)))
          .WillOnce(Return(true));

      EXPECT_CALL(
          routing_table_,
          AddRule(device->interface_index(),
                  IsValidRoutingRule(IPAddress::kFamilyIPv4,
                                     RoutingTable::kRulePriorityMain - 1)))
          .WillOnce(Return(true));
      EXPECT_CALL(
          routing_table_,
          AddRule(device->interface_index(),
                  IsValidRoutingRule(IPAddress::kFamilyIPv6,
                                     RoutingTable::kRulePriorityMain - 1)))
          .WillOnce(Return(true));
    }

    std::vector<DeviceInfo::AddressData> addr_data;
    EXPECT_TRUE(
        device_info_->GetAddresses(device->interface_index(), &addr_data));
    for (const auto& data : addr_data) {
      EXPECT_CALL(routing_table_,
                  AddRule(device->interface_index(),
                          IsValidRoutingRule(data.address.family(), priority)))
          .WillOnce(Return(true));
    }
    // Physical interfaces will have both iif and oif rules to send to the
    // per-interface table if the interface name matches.
    EXPECT_CALL(routing_table_,
                AddRule(device->interface_index(),
                        IsValidIifRule(IPAddress::kFamilyIPv4, priority,
                                       device->link_name())))
        .WillOnce(Return(true));
    EXPECT_CALL(routing_table_,
                AddRule(device->interface_index(),
                        IsValidIifRule(IPAddress::kFamilyIPv6, priority,
                                       device->link_name())))
        .WillOnce(Return(true));
    EXPECT_CALL(routing_table_,
                AddRule(device->interface_index(),
                        IsValidOifRule(IPAddress::kFamilyIPv4, priority,
                                       device->link_name())))
        .WillOnce(Return(true));
    EXPECT_CALL(routing_table_,
                AddRule(device->interface_index(),
                        IsValidOifRule(IPAddress::kFamilyIPv6, priority,
                                       device->link_name())))
        .WillOnce(Return(true));
  }

  ConnectionRefPtr CreateConnection(DeviceRefPtr device,
                                    bool fixed_ip_params = false) {
    ConnectionRefPtr connection(new Connection(
        device->interface_index(), device->link_name(), fixed_ip_params,
        device->technology(), device_info_.get(), &control_));
    ReplaceSingletons(connection);
    return connection;
  }

  MockControl control_;
  MockManager manager_;
  std::unique_ptr<StrictMock<MockDeviceInfo>> device_info_;
  ConnectionRefPtr connection_;
  IPConfigRefPtr ipconfig_;
  IPConfigRefPtr ip6config_;
  IPConfig::Properties properties_;
  IPConfig::Properties ipv6_properties_;
  IPAddress local_address_;
  IPAddress broadcast_address_;
  IPAddress gateway_address_;
  IPAddress default_address_;
  IPAddress local_ipv6_address_;
  StrictMock<MockResolver> resolver_;
  StrictMock<MockRoutingTable> routing_table_;
  StrictMock<MockRTNLHandler> rtnl_handler_;
};

TEST_F(ConnectionTest, InitState) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  EXPECT_EQ(device->interface_index(), connection_->interface_index_);
  EXPECT_EQ(device->link_name(), connection_->interface_name_);
  EXPECT_FALSE(connection_->IsDefault());
  EXPECT_FALSE(connection_->routing_request_count_);
}

TEST_F(ConnectionTest, AddNonPhysicalDeviceConfig) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  // UpdateFromIPConfig creates a per-device table for all devices.
  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);

  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPAddress(local_address_, kPrefix0)))
      .WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPAddress(local_address_, kPrefix0),
                                  IsIPAddress(broadcast_address_, 0),
                                  IsIPAddress(default_address_, 0)));
  EXPECT_CALL(routing_table_,
              SetDefaultRoute(device->interface_index(),
                              IsIPAddress(gateway_address_, 0), _, table_id));
  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ipconfig_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->UpdateFromIPConfig(ipconfig_);

  IPAddress test_local_address(local_address_);
  test_local_address.set_prefix(kPrefix0);
  EXPECT_TRUE(test_local_address.Equals(GetLocalAddress(connection_)));
  EXPECT_TRUE(gateway_address_.Equals(GetGatewayAddress(connection_)));
  EXPECT_TRUE(GetHasBroadcastDomain(connection_));
  EXPECT_FALSE(connection_->IsIPv6());

  // Set default metric and use DNS.
  connection_->SetUseDNS(true);
  AddNonPhysicalRoutingPolicyExpectations(device, Connection::kDefaultMetric);
  EXPECT_CALL(resolver_,
              SetDNSFromLists(ipconfig_->properties().dns_servers,
                              ipconfig_->properties().domain_search));
  EXPECT_CALL(*device, RequestPortalDetection()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_, SetDefaultMetric(_, Connection::kDefaultMetric));
  connection_->SetMetric(Connection::kDefaultMetric, false);
  EXPECT_TRUE(connection_->IsDefault());
  Mock::VerifyAndClearExpectations(&routing_table_);

  // Set non-default metric and do not use DNS.
  connection_->SetUseDNS(false);
  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_,
              SetDefaultMetric(_, Connection::kLowestPriorityMetric));
  connection_->SetMetric(Connection::kLowestPriorityMetric, false);
  EXPECT_FALSE(connection_->IsDefault());
}

TEST_F(ConnectionTest, AddPhysicalDeviceConfig) {
  auto device = CreateDevice(Technology::kEthernet);
  connection_ = CreateConnection(device);

  // UpdateFromIPConfig creates a per-device table for all devices.
  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);

  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPAddress(local_address_, kPrefix0)))
      .WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPAddress(local_address_, kPrefix0),
                                  IsIPAddress(broadcast_address_, 0),
                                  IsIPAddress(default_address_, 0)));
  EXPECT_CALL(routing_table_,
              SetDefaultRoute(device->interface_index(),
                              IsIPAddress(gateway_address_, 0), _, table_id));
  AddPhysicalRoutingPolicyExpectations(
      device, Connection::kLowestPriorityMetric, false);
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ipconfig_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->UpdateFromIPConfig(ipconfig_);

  IPAddress test_local_address(local_address_);
  test_local_address.set_prefix(kPrefix0);
  EXPECT_TRUE(test_local_address.Equals(GetLocalAddress(connection_)));
  EXPECT_TRUE(gateway_address_.Equals(GetGatewayAddress(connection_)));
  EXPECT_TRUE(GetHasBroadcastDomain(connection_));
  EXPECT_FALSE(connection_->IsIPv6());

  // Set default metric and use DNS.
  connection_->SetUseDNS(true);
  AddPhysicalRoutingPolicyExpectations(device, Connection::kDefaultMetric,
                                       true);
  EXPECT_CALL(resolver_,
              SetDNSFromLists(ipconfig_->properties().dns_servers,
                              ipconfig_->properties().domain_search));
  EXPECT_CALL(*device, RequestPortalDetection()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_, SetDefaultMetric(_, Connection::kDefaultMetric));
  connection_->SetMetric(Connection::kDefaultMetric, true);
  EXPECT_TRUE(connection_->IsDefault());
  Mock::VerifyAndClearExpectations(&routing_table_);

  // Set non-default metric and do not use DNS.
  connection_->SetUseDNS(false);
  AddPhysicalRoutingPolicyExpectations(
      device, Connection::kLowestPriorityMetric, false);
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_,
              SetDefaultMetric(_, Connection::kLowestPriorityMetric));
  connection_->SetMetric(Connection::kLowestPriorityMetric, false);
  EXPECT_FALSE(connection_->IsDefault());
}

TEST_F(ConnectionTest, AddNonPhysicalDeviceConfigUserTrafficOnly) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  const std::string kExcludeAddress1 = "192.0.1.0/24";
  const std::string kExcludeAddress2 = "192.0.2.0/24";
  const uint32_t uid = 1000;
  IPAddress address1(IPAddress::kFamilyIPv4);
  IPAddress address2(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(address1.SetAddressAndPrefixFromString(kExcludeAddress1));
  EXPECT_TRUE(address2.SetAddressAndPrefixFromString(kExcludeAddress2));

  properties_.allowed_uids = {uid};
  properties_.default_route = false;
  properties_.exclusion_list = {kExcludeAddress1, kExcludeAddress2};
  UpdateProperties();

  // UpdateFromIPConfig creates a per-device table for all devices.
  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);

  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPAddress(local_address_, kPrefix0)))
      .WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPAddress(local_address_, kPrefix0),
                                  IsIPAddress(broadcast_address_, 0),
                                  IsIPAddress(default_address_, 0)));
  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ipconfig_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));

  // SetupExcludedRoutes should create RTN_THROW entries for both networks.
  EXPECT_CALL(routing_table_,
              AddRoute(device->interface_index(), IsValidThrowRoute(address1)))
      .WillOnce(Return(true));
  EXPECT_CALL(routing_table_,
              AddRoute(device->interface_index(), IsValidThrowRoute(address2)))
      .WillOnce(Return(true));

  // UpdateRoutingPolicy should create rules for IPv4 and IPv6
  EXPECT_CALL(routing_table_,
              AddRule(device->interface_index(),
                      IsValidUidRule(IPAddress::kFamilyIPv4,
                                     Connection::kLowestPriorityMetric, uid)))
      .WillOnce(Return(true));
  EXPECT_CALL(routing_table_,
              AddRule(device->interface_index(),
                      IsValidUidRule(IPAddress::kFamilyIPv6,
                                     Connection::kLowestPriorityMetric, uid)))
      .WillOnce(Return(true));

  connection_->UpdateFromIPConfig(ipconfig_);

  IPAddress test_local_address(local_address_);
  test_local_address.set_prefix(kPrefix0);
  EXPECT_TRUE(test_local_address.Equals(GetLocalAddress(connection_)));
  EXPECT_TRUE(gateway_address_.Equals(GetGatewayAddress(connection_)));
  EXPECT_TRUE(GetHasBroadcastDomain(connection_));
  EXPECT_FALSE(connection_->IsIPv6());

  AddNonPhysicalRoutingPolicyExpectations(device, Connection::kDefaultMetric);
  EXPECT_CALL(routing_table_,
              AddRule(device->interface_index(),
                      IsValidUidRule(IPAddress::kFamilyIPv4,
                                     Connection::kDefaultMetric, uid)))
      .WillOnce(Return(true));
  EXPECT_CALL(routing_table_,
              AddRule(device->interface_index(),
                      IsValidUidRule(IPAddress::kFamilyIPv6,
                                     Connection::kDefaultMetric, uid)))
      .WillOnce(Return(true));
  EXPECT_CALL(resolver_,
              SetDNSFromLists(ipconfig_->properties().dns_servers,
                              ipconfig_->properties().domain_search));
  EXPECT_CALL(*device, RequestPortalDetection()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  connection_->SetUseDNS(true);
  EXPECT_CALL(routing_table_, SetDefaultMetric(_, Connection::kDefaultMetric));
  connection_->SetMetric(Connection::kDefaultMetric, true);
  Mock::VerifyAndClearExpectations(&routing_table_);
  EXPECT_TRUE(connection_->IsDefault());

  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_,
              AddRule(device->interface_index(),
                      IsValidUidRule(IPAddress::kFamilyIPv4,
                                     Connection::kLowestPriorityMetric, uid)))
      .WillOnce(Return(true));
  EXPECT_CALL(routing_table_,
              AddRule(device->interface_index(),
                      IsValidUidRule(IPAddress::kFamilyIPv6,
                                     Connection::kLowestPriorityMetric, uid)))
      .WillOnce(Return(true));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  connection_->SetUseDNS(false);
  EXPECT_CALL(routing_table_,
              SetDefaultMetric(_, Connection::kLowestPriorityMetric));
  connection_->SetMetric(Connection::kLowestPriorityMetric, false);
  EXPECT_FALSE(connection_->IsDefault());
}

TEST_F(ConnectionTest, AddNonPhysicalDeviceConfigIPv6) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  // UpdateFromIPConfig creates a per-device table for all devices.
  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);

  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPv6Address(local_ipv6_address_)))
      .WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPv6Address(local_ipv6_address_),
                                  IsDefaultAddress(), _));
  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ip6config_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->UpdateFromIPConfig(ip6config_);

  IPAddress test_local_address(local_ipv6_address_);
  EXPECT_TRUE(test_local_address.Equals(GetLocalAddress(connection_)));
  EXPECT_TRUE(connection_->IsIPv6());
}

TEST_F(ConnectionTest, AddPhysicalDeviceConfigIPv6) {
  auto device = CreateDevice(Technology::kEthernet);
  connection_ = CreateConnection(device);

  // UpdateFromIPConfig creates a per-device table for all devices.
  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);

  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPv6Address(local_ipv6_address_)))
      .WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPv6Address(local_ipv6_address_),
                                  IsDefaultAddress(), _));
  AddPhysicalRoutingPolicyExpectations(
      device, Connection::kLowestPriorityMetric, false);
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ip6config_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->UpdateFromIPConfig(ip6config_);

  IPAddress test_local_address(local_ipv6_address_);
  EXPECT_TRUE(test_local_address.Equals(GetLocalAddress(connection_)));
  EXPECT_TRUE(connection_->IsIPv6());
}

TEST_F(ConnectionTest, AddConfigWithPeer) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  const std::string kPeerAddress("192.168.1.222");
  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);
  IPAddress peer_address(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(peer_address.SetAddressFromString(kPeerAddress));
  properties_.peer_address = kPeerAddress;
  properties_.gateway = std::string();
  UpdateProperties();
  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPAddress(local_address_, kPrefix0)))
      .WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPAddress(local_address_, kPrefix0),
                                  IsIPAddress(broadcast_address_, 0), _));
  EXPECT_CALL(routing_table_, SetDefaultRoute(_, _, _, _)).Times(1);
  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ipconfig_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->UpdateFromIPConfig(ipconfig_);
  EXPECT_FALSE(GetHasBroadcastDomain(connection_));
}

TEST_F(ConnectionTest, AddConfigWithBrokenNetmask) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  // Assign a prefix that makes the gateway unreachable.
  properties_.subnet_prefix = kPrefix1;
  UpdateProperties();

  // UpdateFromIPConfig creates a per-device table for all devices.
  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);

  // Connection should add a link route which will allow the
  // gateway to be reachable.
  IPAddress gateway_address(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(gateway_address.SetAddressFromString(kGatewayAddress0));
  EXPECT_CALL(routing_table_, AddRoute(device->interface_index(),
                                       IsLinkRouteTo(gateway_address)))
      .WillOnce(Return(true));
  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPAddress(local_address_, kPrefix1)))
      .WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPAddress(local_address_, kPrefix1),
                                  IsIPAddress(broadcast_address_, 0),
                                  IsIPAddress(default_address_, 0)));
  EXPECT_CALL(routing_table_,
              SetDefaultRoute(device->interface_index(),
                              IsIPAddress(gateway_address_, 0), _, table_id));

  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ipconfig_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->UpdateFromIPConfig(ipconfig_);
}

TEST_F(ConnectionTest, AddConfigReverse) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  AddNonPhysicalRoutingPolicyExpectations(device, Connection::kDefaultMetric);

  std::vector<std::string> empty_list;
  EXPECT_CALL(resolver_, SetDNSFromLists(empty_list, empty_list));
  EXPECT_CALL(*device, RequestPortalDetection()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  connection_->SetUseDNS(true);
  EXPECT_CALL(routing_table_, SetDefaultMetric(_, Connection::kDefaultMetric));
  connection_->SetMetric(Connection::kDefaultMetric, true);
  Mock::VerifyAndClearExpectations(&routing_table_);

  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);

  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPAddress(local_address_, kPrefix0)))
      .WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPAddress(local_address_, kPrefix0),
                                  IsIPAddress(broadcast_address_, 0),
                                  IsIPAddress(default_address_, 0)));
  EXPECT_CALL(routing_table_,
              SetDefaultRoute(device->interface_index(),
                              IsIPAddress(gateway_address_, 0),
                              Connection::kDefaultMetric, table_id));
  AddNonPhysicalRoutingPolicyExpectations(device, Connection::kDefaultMetric);
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ipconfig_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(resolver_,
              SetDNSFromLists(ipconfig_->properties().dns_servers,
                              ipconfig_->properties().domain_search));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->UpdateFromIPConfig(ipconfig_);
}

TEST_F(ConnectionTest, AddConfigWithDNSDomain) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).Times(1);

  const std::string kDomainName("chromium.org");
  properties_.domain_search.clear();
  properties_.domain_name = kDomainName;
  UpdateProperties();
  EXPECT_CALL(*device_info_, HasOtherAddress(_, _)).WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_, AddInterfaceAddress(_, _, _, _));
  EXPECT_CALL(routing_table_, SetDefaultRoute(_, _, _, _));
  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_, ConfigureRoutes(_, _, _, _));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(_, _));
  connection_->UpdateFromIPConfig(ipconfig_);

  AddNonPhysicalRoutingPolicyExpectations(device, Connection::kDefaultMetric);
  std::vector<std::string> domain_search_list = {kDomainName + "."};
  EXPECT_CALL(resolver_, SetDNSFromLists(_, domain_search_list));
  EXPECT_CALL(*device, RequestPortalDetection()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  connection_->SetUseDNS(true);
  EXPECT_CALL(routing_table_, SetDefaultMetric(_, Connection::kDefaultMetric));
  connection_->SetMetric(Connection::kDefaultMetric, true);
}

TEST_F(ConnectionTest, AddConfigWithFixedIpParams) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device, true);

  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));

  // Initial setup: routes but no IP configuration.
  EXPECT_CALL(*device_info_, HasOtherAddress(_, _)).Times(0);
  EXPECT_CALL(rtnl_handler_, AddInterfaceAddress(_, _, _, _)).Times(0);
  EXPECT_CALL(routing_table_, SetDefaultRoute(_, _, _, _));
  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);
  EXPECT_CALL(routing_table_, ConfigureRoutes(_, _, _, _));
  EXPECT_CALL(*device, RequestPortalDetection()).WillOnce(Return(true));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(_, _)).Times(0);
  connection_->UpdateFromIPConfig(ipconfig_);
  Mock::VerifyAndClearExpectations(&routing_table_);
  Mock::VerifyAndClearExpectations(&rtnl_handler_);

  // Change metric to make this the default service.
  AddNonPhysicalRoutingPolicyExpectations(device, Connection::kDefaultMetric);
  EXPECT_CALL(resolver_, SetDNSFromLists(_, _));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  connection_->SetUseDNS(true);
  EXPECT_CALL(routing_table_, SetDefaultMetric(_, Connection::kDefaultMetric));
  connection_->SetMetric(Connection::kDefaultMetric, false);
}

TEST_F(ConnectionTest, HasOtherAddress) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  const uint32_t table_id = 8;
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  EXPECT_CALL(*device_info_,
              HasOtherAddress(device->interface_index(),
                              IsIPAddress(local_address_, kPrefix0)))
      .WillOnce(Return(true));
  EXPECT_CALL(routing_table_, FlushRoutes(device->interface_index()));
  EXPECT_CALL(*device_info_, FlushAddresses(device->interface_index()));
  EXPECT_CALL(rtnl_handler_,
              AddInterfaceAddress(device->interface_index(),
                                  IsIPAddress(local_address_, kPrefix0),
                                  IsIPAddress(broadcast_address_, 0),
                                  IsIPAddress(default_address_, 0)));
  EXPECT_CALL(routing_table_,
              SetDefaultRoute(device->interface_index(),
                              IsIPAddress(gateway_address_, 0), _, table_id));
  EXPECT_CALL(routing_table_,
              ConfigureRoutes(device->interface_index(), ipconfig_,
                              Connection::kDefaultMetric, table_id));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));

  AddNonPhysicalRoutingPolicyExpectations(device,
                                          Connection::kLowestPriorityMetric);

  connection_->UpdateFromIPConfig(ipconfig_);
}

TEST_F(ConnectionTest, UpdateDNSServers) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  static const char* const kDnsServers[] = {"1.1.1.1", "1.1.1.2"};
  std::vector<std::string> dns_servers(kDnsServers, std::end(kDnsServers));

  // Non-default connection.
  connection_->metric_ = Connection::kLowestPriorityMetric;
  EXPECT_CALL(resolver_, SetDNSFromLists(_, _)).Times(0);
  connection_->UpdateDNSServers(dns_servers);
  Mock::VerifyAndClearExpectations(&resolver_);

  // Default connection.
  connection_->use_dns_ = true;
  connection_->metric_ = Connection::kDefaultMetric;
  EXPECT_CALL(resolver_, SetDNSFromLists(dns_servers, _));
  connection_->UpdateDNSServers(dns_servers);
  Mock::VerifyAndClearExpectations(&resolver_);
}

TEST_F(ConnectionTest, RouteRequest) {
  auto device = CreateDevice(Technology::kEthernet);
  connection_ = CreateConnection(device);

  EXPECT_CALL(*device, SetLooseRouting(true)).Times(1);
  connection_->RequestRouting();
  connection_->RequestRouting();

  // The first release should only decrement the reference counter.
  connection_->ReleaseRouting();

  // Another release will re-enable reverse-path filter.
  EXPECT_CALL(*device, SetLooseRouting(false));
  EXPECT_CALL(routing_table_, FlushCache());
  connection_->ReleaseRouting();
}

TEST_F(ConnectionTest, BlackholeIPv6) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  const uint32_t table_id = 9;
  properties_.blackhole_ipv6 = true;
  UpdateProperties();
  EXPECT_CALL(*device_info_, HasOtherAddress(_, _)).WillOnce(Return(false));
  EXPECT_CALL(rtnl_handler_, AddInterfaceAddress(_, _, _, _));
  EXPECT_CALL(routing_table_, SetDefaultRoute(_, _, _, _));
  EXPECT_CALL(routing_table_, FreeTableId(RT_TABLE_MAIN)).Times(AnyNumber());
  EXPECT_CALL(routing_table_, AllocTableId()).WillOnce(Return(table_id));
  EXPECT_CALL(routing_table_, FlushRules(_));
  EXPECT_CALL(routing_table_, AddRule(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(routing_table_, ConfigureRoutes(_, _, _, _));
  EXPECT_CALL(routing_table_, FlushCache()).WillOnce(Return(true));
  EXPECT_CALL(routing_table_,
              CreateBlackholeRoute(device->interface_index(),
                                   IPAddress::kFamilyIPv6, 0, table_id))
      .WillOnce(Return(true));
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->UpdateFromIPConfig(ipconfig_);
}

TEST_F(ConnectionTest, FixGatewayReachability) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  static const char kLocal[] = "10.242.2.13";
  IPAddress local(IPAddress::kFamilyIPv4);
  ASSERT_TRUE(local.SetAddressFromString(kLocal));
  const int kPrefix = 24;
  local.set_prefix(kPrefix);
  IPAddress gateway(IPAddress::kFamilyIPv4);
  IPAddress peer(IPAddress::kFamilyIPv4);

  // Should fail because no gateway is set and peer address is invalid.
  EXPECT_FALSE(connection_->FixGatewayReachability(local, &peer, &gateway));
  EXPECT_EQ(kPrefix, local.prefix());
  EXPECT_FALSE(peer.IsValid());
  EXPECT_FALSE(gateway.IsValid());

  // Should succeed because with the given prefix, this gateway is reachable.
  static const char kReachableGateway[] = "10.242.2.14";
  ASSERT_TRUE(gateway.SetAddressFromString(kReachableGateway));
  IPAddress gateway_backup(gateway);
  peer = IPAddress(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(connection_->FixGatewayReachability(local, &peer, &gateway));
  // Prefix should remain unchanged.
  EXPECT_EQ(kPrefix, local.prefix());
  // Peer should remain unchanged.
  EXPECT_FALSE(peer.IsValid());
  // Gateway should remain unchanged.
  EXPECT_TRUE(gateway_backup.Equals(gateway));

  // Should succeed because we created a link route to the gateway.
  static const char kRemoteGateway[] = "10.242.3.14";
  ASSERT_TRUE(gateway.SetAddressFromString(kRemoteGateway));
  gateway_backup = gateway;
  gateway_backup.SetAddressToDefault();
  peer = IPAddress(IPAddress::kFamilyIPv4);
  EXPECT_CALL(routing_table_,
              AddRoute(device->interface_index(), IsLinkRouteTo(gateway)))
      .WillOnce(Return(true));
  EXPECT_TRUE(connection_->FixGatewayReachability(local, &peer, &gateway));

  // Invalid peer should not be modified.
  EXPECT_FALSE(peer.IsValid());
  // Gateway should not be set to default.
  EXPECT_FALSE(gateway_backup.Equals(gateway));

  // Should fail if AddRoute() fails.
  EXPECT_CALL(routing_table_,
              AddRoute(device->interface_index(), IsLinkRouteTo(gateway)))
      .WillOnce(Return(false));
  EXPECT_FALSE(connection_->FixGatewayReachability(local, &peer, &gateway));

  // Even if there is a peer specified and it does not match the gateway, we
  // should not fail.
  local.set_prefix(kPrefix);
  ASSERT_TRUE(gateway.SetAddressFromString(kReachableGateway));
  EXPECT_TRUE(connection_->FixGatewayReachability(local, &peer, &gateway));
  EXPECT_EQ(kPrefix, local.prefix());
  EXPECT_FALSE(peer.Equals(gateway));

  // If this is a peer-to-peer interface and the peer matches the gateway, the
  // gateway and peer address should be modified to allow routing to work
  // correctly.
  static const char kUnreachableGateway[] = "11.242.2.14";
  ASSERT_TRUE(gateway.SetAddressFromString(kUnreachableGateway));
  ASSERT_TRUE(peer.SetAddressFromString(kUnreachableGateway));
  EXPECT_TRUE(connection_->FixGatewayReachability(local, &peer, &gateway));
  EXPECT_TRUE(peer.IsDefault());
  EXPECT_TRUE(gateway.IsDefault());
}

TEST_F(ConnectionTest, GetSubnetName) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  EXPECT_EQ("", connection_->GetSubnetName());
  IPAddress local("1.2.3.4");
  local.set_prefix(24);
  SetLocal(local);
  EXPECT_EQ("1.2.3.0/24", connection_->GetSubnetName());
}

TEST_F(ConnectionTest, SetMTU) {
  auto device = CreateDevice(Technology::kUnknown);
  connection_ = CreateConnection(device);

  testing::InSequence seq;
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->SetMTU(0);

  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kDefaultMTU));
  connection_->SetMTU(IPConfig::kUndefinedMTU);

  // Test IPv4 minimum MTU.
  SetLocal(local_address_);
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kMinIPv4MTU));
  connection_->SetMTU(1);

  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kMinIPv4MTU));
  connection_->SetMTU(IPConfig::kMinIPv4MTU - 1);

  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kMinIPv4MTU));
  connection_->SetMTU(IPConfig::kMinIPv4MTU);

  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kMinIPv4MTU + 1));
  connection_->SetMTU(IPConfig::kMinIPv4MTU + 1);

  // Test IPv6 minimum MTU.
  SetLocal(local_ipv6_address_);
  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kMinIPv6MTU));
  connection_->SetMTU(1);

  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kMinIPv6MTU));
  connection_->SetMTU(IPConfig::kMinIPv6MTU - 1);

  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kMinIPv6MTU));
  connection_->SetMTU(IPConfig::kMinIPv6MTU);

  EXPECT_CALL(rtnl_handler_, SetInterfaceMTU(device->interface_index(),
                                             IPConfig::kMinIPv6MTU + 1));
  connection_->SetMTU(IPConfig::kMinIPv6MTU + 1);
}

}  // namespace shill
