// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/ipv4_address.h"

#include <arpa/inet.h>

#include <array>

#include <base/logging.h>
#include <gtest/gtest.h>

namespace shill {
namespace {

TEST(IPv4AddressTest, DefaultConstructor) {
  const IPv4Address default_addr;
  IPv4Address::DataType data{0, 0, 0, 0};

  EXPECT_EQ(default_addr.data(), data);
  EXPECT_EQ(default_addr, IPv4Address(0, 0, 0, 0));
}

TEST(IPv4AddressTest, Constructor) {
  const std::array<uint8_t, 4> data{192, 168, 10, 1};
  // Constructed from raw number.
  const IPv4Address address1(192, 168, 10, 1);
  // Constructed from std::array.
  const IPv4Address address2(data);
  // Constructed from other instance.
  const IPv4Address address3(address1);

  EXPECT_EQ(address1.data(), data);
  EXPECT_EQ(address1, address2);
  EXPECT_EQ(address1, address3);
}

TEST(IPv4AddressTest, CreateFromString_Success) {
  const auto address = IPv4Address::CreateFromString("192.168.10.1");
  ASSERT_TRUE(address);
  EXPECT_EQ(*address, IPv4Address(192, 168, 10, 1));
}

TEST(IPv4AddressTest, ToString) {
  const IPv4Address address(192, 168, 10, 1);
  EXPECT_EQ(address.ToString(), "192.168.10.1");
  // Make sure std::ostream operator<<() works.
  LOG(INFO) << "address = " << address;
}

TEST(IPv4AddressTest, CreateFromString_Fail) {
  EXPECT_FALSE(IPv4Address::CreateFromString(""));
  EXPECT_FALSE(IPv4Address::CreateFromString("192.168.10.1/24"));
  EXPECT_FALSE(IPv4Address::CreateFromString("fe80::1aa9:5ff:7ebf:14c5"));
}

TEST(IPv4AddressTest, IsZero) {
  const IPv4Address default_addr;
  EXPECT_TRUE(default_addr.IsZero());

  const IPv4Address address(0, 0, 0, 1);
  EXPECT_FALSE(address.IsZero());
}

TEST(IPv4AddressTest, Order) {
  const IPv4Address kOrderedAddresses[] = {
      {127, 0, 0, 1},   {192, 168, 1, 1},  {192, 168, 1, 32},
      {192, 168, 2, 1}, {192, 168, 2, 32}, {255, 255, 255, 255}};

  for (size_t i = 0; i < std::size(kOrderedAddresses); ++i) {
    for (size_t j = 0; j < std::size(kOrderedAddresses); ++j) {
      if (i < j) {
        EXPECT_TRUE(kOrderedAddresses[i] < kOrderedAddresses[j]);
      } else {
        EXPECT_FALSE(kOrderedAddresses[i] < kOrderedAddresses[j]);
      }
    }
  }
}

TEST(IPv4CIDR, CreateFromCIDRString) {
  const auto cidr1 = IPv4CIDR::CreateFromCIDRString("192.168.10.1/0");
  ASSERT_TRUE(cidr1);
  EXPECT_EQ(cidr1->address(), IPv4Address(192, 168, 10, 1));
  EXPECT_EQ(cidr1->prefix_length(), 0);

  const auto cidr2 = IPv4CIDR::CreateFromCIDRString("192.168.10.1/25");
  ASSERT_TRUE(cidr2);
  EXPECT_EQ(cidr2->address(), IPv4Address(192, 168, 10, 1));
  EXPECT_EQ(cidr2->prefix_length(), 25);

  const auto cidr3 = IPv4CIDR::CreateFromCIDRString("192.168.10.1/32");
  ASSERT_TRUE(cidr3);
  EXPECT_EQ(cidr3->address(), IPv4Address(192, 168, 10, 1));
  EXPECT_EQ(cidr3->prefix_length(), 32);
}

TEST(IPv4CIDR, CreateFromCIDRString_Fail) {
  EXPECT_FALSE(IPv4CIDR::CreateFromCIDRString("192.168.10.1"));
  EXPECT_FALSE(IPv4CIDR::CreateFromCIDRString("192.168.10.1/-1"));
  EXPECT_FALSE(IPv4CIDR::CreateFromCIDRString("192.168.10.1/33"));
  EXPECT_FALSE(IPv4CIDR::CreateFromCIDRString("192.168.10/24"));
  EXPECT_FALSE(IPv4CIDR::CreateFromCIDRString("::1"));
  EXPECT_FALSE(IPv4CIDR::CreateFromCIDRString("::1/24"));
}

TEST(IPv4CIDR, CreateFromStringAndPrefix) {
  const auto cidr1 = IPv4CIDR::CreateFromStringAndPrefix("192.168.10.1", 0);
  ASSERT_TRUE(cidr1);
  EXPECT_EQ(cidr1->address(), IPv4Address(192, 168, 10, 1));
  EXPECT_EQ(cidr1->prefix_length(), 0);

  const auto cidr2 = IPv4CIDR::CreateFromStringAndPrefix("192.168.10.1", 25);
  ASSERT_TRUE(cidr2);
  EXPECT_EQ(cidr2->address(), IPv4Address(192, 168, 10, 1));
  EXPECT_EQ(cidr2->prefix_length(), 25);

  const auto cidr3 = IPv4CIDR::CreateFromStringAndPrefix("192.168.10.1", 32);
  ASSERT_TRUE(cidr3);
  EXPECT_EQ(cidr3->address(), IPv4Address(192, 168, 10, 1));
  EXPECT_EQ(cidr3->prefix_length(), 32);
}

TEST(IPv4CIDR, CreateFromAddressAndPrefix) {
  const IPv4Address address(192, 168, 10, 1);
  ASSERT_TRUE(IPv4CIDR::CreateFromAddressAndPrefix(address, 0));
  ASSERT_TRUE(IPv4CIDR::CreateFromAddressAndPrefix(address, 25));
  ASSERT_TRUE(IPv4CIDR::CreateFromAddressAndPrefix(address, 32));

  ASSERT_FALSE(IPv4CIDR::CreateFromAddressAndPrefix(address, 33));
  ASSERT_FALSE(IPv4CIDR::CreateFromAddressAndPrefix(address, -1));
}

TEST(IPv4CIDR, GetPrefixAddress) {
  const auto cidr1 = *IPv4CIDR::CreateFromCIDRString("192.168.10.123/24");
  EXPECT_EQ(cidr1.GetPrefixAddress().ToString(), "192.168.10.0");

  const auto cidr2 = *IPv4CIDR::CreateFromCIDRString("192.168.255.123/20");
  EXPECT_EQ(cidr2.GetPrefixAddress().ToString(), "192.168.240.0");
}

TEST(IPv4CIDR, GetBroadcast) {
  const auto cidr1 = *IPv4CIDR::CreateFromCIDRString("192.168.10.123/24");
  EXPECT_EQ(cidr1.GetBroadcast().ToString(), "192.168.10.255");

  const auto cidr2 = *IPv4CIDR::CreateFromCIDRString("192.168.1.123/20");
  EXPECT_EQ(cidr2.GetBroadcast().ToString(), "192.168.15.255");
}

TEST(IPv4CIDR, ContainsAddress) {
  const auto cidr = *IPv4CIDR::CreateFromCIDRString("192.168.10.123/24");
  EXPECT_TRUE(cidr.ContainsAddress(IPv4Address(192, 168, 10, 1)));
  EXPECT_TRUE(cidr.ContainsAddress(IPv4Address(192, 168, 10, 123)));
  EXPECT_TRUE(cidr.ContainsAddress(IPv4Address(192, 168, 10, 255)));
  EXPECT_FALSE(cidr.ContainsAddress(IPv4Address(192, 168, 11, 123)));
  EXPECT_FALSE(cidr.ContainsAddress(IPv4Address(193, 168, 10, 123)));
}

TEST(IPv4CIDR, ToString) {
  const std::string cidr_string = "192.168.10.123/24";
  const auto cidr = *IPv4CIDR::CreateFromCIDRString(cidr_string);
  EXPECT_EQ(cidr.ToString(), cidr_string);
  // Make sure std::ostream operator<<() works.
  LOG(INFO) << "cidr = " << cidr;
}

TEST(IPv4CIDR, GetNetmask) {
  EXPECT_EQ(*IPv4CIDR::GetNetmask(0), IPv4Address(0, 0, 0, 0));
  EXPECT_EQ(*IPv4CIDR::GetNetmask(1), IPv4Address(128, 0, 0, 0));
  EXPECT_EQ(*IPv4CIDR::GetNetmask(4), IPv4Address(240, 0, 0, 0));
  EXPECT_EQ(*IPv4CIDR::GetNetmask(8), IPv4Address(255, 0, 0, 0));
  EXPECT_EQ(*IPv4CIDR::GetNetmask(16), IPv4Address(255, 255, 0, 0));
  EXPECT_EQ(*IPv4CIDR::GetNetmask(24), IPv4Address(255, 255, 255, 0));
  EXPECT_EQ(*IPv4CIDR::GetNetmask(31), IPv4Address(255, 255, 255, 254));
  EXPECT_EQ(*IPv4CIDR::GetNetmask(32), IPv4Address(255, 255, 255, 255));

  EXPECT_FALSE(IPv4CIDR::GetNetmask(-1));
  EXPECT_FALSE(IPv4CIDR::GetNetmask(33));
}

TEST(IPv4CIDR, ToNetmask) {
  const auto cidr1 = *IPv4CIDR::CreateFromCIDRString("192.168.2.1/0");
  EXPECT_EQ(cidr1.ToNetmask(), IPv4Address(0, 0, 0, 0));

  const auto cidr2 = *IPv4CIDR::CreateFromCIDRString("192.168.2.1/8");
  EXPECT_EQ(cidr2.ToNetmask(), IPv4Address(255, 0, 0, 0));

  const auto cidr3 = *IPv4CIDR::CreateFromCIDRString("192.168.2.1/24");
  EXPECT_EQ(cidr3.ToNetmask(), IPv4Address(255, 255, 255, 0));

  const auto cidr4 = *IPv4CIDR::CreateFromCIDRString("192.168.2.1/32");
  EXPECT_EQ(cidr4.ToNetmask(), IPv4Address(255, 255, 255, 255));
}

}  // namespace
}  // namespace shill
