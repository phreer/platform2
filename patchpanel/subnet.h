// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SUBNET_H_
#define PATCHPANEL_SUBNET_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <brillo/brillo_export.h>
#include <net-base/ipv4_address.h>

namespace patchpanel {

// Represents an allocated address inside an IPv4 subnet.  The address is freed
// when this object is destroyed.
class BRILLO_EXPORT SubnetAddress {
 public:
  // Creates a new SubnetAddress with the given |cidr|. |release_cb| runs in the
  // destructor of this class and can be used to free other resources associated
  // with the subnet address.
  SubnetAddress(const net_base::IPv4CIDR& cidr, base::OnceClosure release_cb);
  ~SubnetAddress();

  SubnetAddress(const SubnetAddress&) = delete;
  SubnetAddress& operator=(const SubnetAddress&) = delete;

  const net_base::IPv4CIDR& cidr() const { return cidr_; }

 private:
  net_base::IPv4CIDR cidr_;
  base::ScopedClosureRunner release_cb_;
};

// Represents an allocated IPv4 subnet.
class BRILLO_EXPORT Subnet {
 public:
  // Creates a new Subnet with the given base address and prefix length.
  // |base_addr| must be in network-byte order. |release_cb| runs in the
  // destructor of this class and can be used to free other resources associated
  // with the subnet.
  Subnet(uint32_t base_addr, int prefix_length, base::OnceClosure release_cb);
  Subnet(const Subnet&) = delete;
  Subnet& operator=(const Subnet&) = delete;

  ~Subnet();

  // Allocates the address at |offset|. Returns nullptr if |offset| is invalid
  // or the address is already allocated. Note: |offset| is relative to the base
  // address.
  std::unique_ptr<SubnetAddress> AllocateAtOffset(uint32_t offset);

  // Returns the address at the given |offset| in network-byte order. Returns
  // INADDR_ANY if the offset exceeds the available IPs in the subnet.
  // Available IPs do not include the network id or the broadcast address.
  // |offset| is relative to the base address.
  uint32_t AddressAtOffset(uint32_t offset) const;

  // Returns the number of available IPs in this subnet.
  uint32_t AvailableCount() const;

  // Returns the base address in network-byte order.
  uint32_t BaseAddress() const;

  // Returns the netmask in network-byte order.
  uint32_t Netmask() const;

  // Returns the prefix in network-byte order.
  uint32_t Prefix() const;

  // Returns the prefix length.
  int PrefixLength() const;

  // Returns the CIDR representation of this subnet, for instance
  // 192.168.0.0/24.
  std::string ToCidrString() const;

 private:
  // Returns true if the address that is relative to the base address by
  // |offset| is in the subnet.
  // Note: the base address and the broadcast address are considered invalid.
  // So IsValidOffset(0) and IsValidOffset(addrs_.size() - 1) are false.
  bool IsValidOffset(uint32_t offset) const;

  // Marks the address at |offset| as free.
  void Free(uint32_t offset);

  // Base address of the subnet, in network-byte order.
  uint32_t base_addr_;

  // Prefix length.
  int prefix_length_;

  // Keeps track of allocated addresses.
  std::vector<bool> addrs_;

  // Callback to run when this object is deleted.
  base::OnceClosure release_cb_;

  base::WeakPtrFactory<Subnet> weak_factory_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_SUBNET_H_
