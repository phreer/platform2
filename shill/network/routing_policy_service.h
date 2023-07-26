// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_ROUTING_POLICY_SERVICE_H_
#define SHILL_NETWORK_ROUTING_POLICY_SERVICE_H_

// Add for fib_rule_uid_range definition.
#include <linux/fib_rules.h>

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/no_destructor.h>

#include "shill/net/rtnl_message.h"

bool operator==(const fib_rule_uid_range& a, const fib_rule_uid_range& b);

namespace shill {

// Represents a single policy routing rule.
// ctor will initialize |dst| and |src| to be of the same family of |family|,
// but caller needs to ensure the family still matches when they set |dst| or
// |src| afterwards.
struct RoutingPolicyEntry {
  struct FwMark {
    uint32_t value = 0;
    uint32_t mask = 0xFFFFFFFF;

    bool operator==(const FwMark& b) const {
      return (value == b.value) && (mask == b.mask);
    }
  };

  explicit RoutingPolicyEntry(net_base::IPFamily family);

  bool operator==(const RoutingPolicyEntry& b) const;

  net_base::IPFamily family;
  uint32_t priority = 1;
  uint32_t table = RT_TABLE_MAIN;

  net_base::IPCIDR dst;
  net_base::IPCIDR src;

  std::optional<FwMark> fw_mark;
  std::optional<fib_rule_uid_range> uid_range;
  std::optional<std::string> iif_name;
  std::optional<std::string> oif_name;

  bool invert_rule = false;
};

// Print out an entry in a format similar to that of ip rule.
std::ostream& operator<<(std::ostream& os, const RoutingPolicyEntry& entry);

class RTNLHandler;
class RTNLListener;

// A singleton maintains an in-process copy of the kernel routing policy data
// base (RPDB). Offers the ability for other modules to modify RPDB, adding and
// removing routing policy entries (often referred as routing rules).
class RoutingPolicyService {
 public:
  // Priority of the rule sending all traffic to the local routing table.
  static constexpr uint32_t kRulePriorityLocal = 0;
  // Priority of the rule sending all traffic to the main routing table.
  static constexpr uint32_t kRulePriorityMain = 32766;

  virtual ~RoutingPolicyService();

  static RoutingPolicyService* GetInstance();

  virtual void Start();
  virtual void Stop();

  // Add an entry to the routing rule table.
  virtual bool AddRule(int interface_index, const RoutingPolicyEntry& entry);

  // Flush all routing rules for |interface_index|.
  virtual void FlushRules(int interface_index);

  // Returns the user traffic uids.
  virtual const std::vector<uint32_t>& GetUserTrafficUids();

  // Returns shill uid used for critical system traffic.
  virtual uint32_t GetShillUid();

 protected:
  RoutingPolicyService();
  RoutingPolicyService(const RoutingPolicyService&) = delete;
  RoutingPolicyService& operator=(const RoutingPolicyService&) = delete;

 private:
  friend class base::NoDestructor<RoutingPolicyService>;
  friend class RoutingPolicyServiceTest;
  using PolicyTableEntryVector = std::vector<RoutingPolicyEntry>;
  using PolicyTables = std::unordered_map<int, PolicyTableEntryVector>;

  void RuleMsgHandler(const RTNLMessage& message);

  bool ApplyRule(uint32_t interface_index,
                 const RoutingPolicyEntry& entry,
                 RTNLMessage::Mode mode,
                 unsigned int flags);
  std::optional<RoutingPolicyEntry> ParseRoutingPolicyMessage(
      const RTNLMessage& message);

  // Maps from interface ids to the routing policy entries associated with the
  // interface.
  PolicyTables policy_tables_;

  std::unique_ptr<RTNLListener> rule_listener_;

  // "User traffic" refers to traffic from processes that run under one of the
  // unix users enumered in |kUserTrafficUsernames| constant in
  // shill/routing_table.cc.
  std::vector<uint32_t> user_traffic_uids_;

  // Cache singleton pointer for performance and test purposes.
  RTNLHandler* rtnl_handler_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_ROUTING_POLICY_SERVICE_H_