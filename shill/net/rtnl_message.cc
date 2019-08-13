// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/rtnl_message.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "shill/net/ndisc.h"

namespace shill {

namespace {
const char* TypeToString(RTNLMessage::Type type) {
  switch (type) {
    case RTNLMessage::kTypeLink:
      return "Link";
    case RTNLMessage::kTypeAddress:
      return "Address";
    case RTNLMessage::kTypeRoute:
      return "Route";
    case RTNLMessage::kTypeRule:
      return "Rule";
    case RTNLMessage::kTypeRdnss:
      return "Rdnss";
    case RTNLMessage::kTypeDnssl:
      return "Dnssl";
    case RTNLMessage::kTypeNeighbor:
      return "Neighbor";
    default:
      return "UnknownType";
  }
}
}  // namespace

struct RTNLHeader {
  RTNLHeader() {
    memset(this, 0, sizeof(*this));
  }
  struct nlmsghdr hdr;
  union {
    struct ifinfomsg ifi;
    struct ifaddrmsg ifa;
    struct rtmsg rtm;
    struct nduseroptmsg nd_user_opt;
    struct ndmsg ndm;
  };
};

std::string RTNLMessage::LinkStatus::ToString() const {
  return base::StringPrintf("LinkStatus type %d flags %X change %X",
                            type, flags, change);
}

std::string RTNLMessage::AddressStatus::ToString() const {
  return base::StringPrintf("AddressStatus prefix_len %d flags %X scope %d",
                            prefix_len, flags, scope);
}

std::string RTNLMessage::RouteStatus::ToString() const {
  return base::StringPrintf("RouteStatus dst_prefix %d src_prefix %d table %d"
                            " protocol %d scope %d type %d flags %X",
                            dst_prefix, src_prefix, table,
                            protocol, scope, type,
                            flags);
}

std::string RTNLMessage::NeighborStatus::ToString() const {
  return base::StringPrintf("NeighborStatus state %d flags %X type %d",
                            state, flags, type);
}

std::string RTNLMessage::RdnssOption::ToString() const {
  return base::StringPrintf("RdnssOption lifetime %d", lifetime);
}

RTNLMessage::RTNLMessage()
    : type_(kTypeUnknown),
      mode_(kModeUnknown),
      flags_(0),
      seq_(0),
      pid_(0),
      interface_index_(0),
      family_(IPAddress::kFamilyUnknown) {}

RTNLMessage::RTNLMessage(Type type,
                         Mode mode,
                         uint16_t flags,
                         uint32_t seq,
                         uint32_t pid,
                         int32_t interface_index,
                         IPAddress::Family family)
    : type_(type),
      mode_(mode),
      flags_(flags),
      seq_(seq),
      pid_(pid),
      interface_index_(interface_index),
      family_(family) {}

bool RTNLMessage::Decode(const ByteString& msg) {
  bool ret = DecodeInternal(msg);
  if (!ret) {
    Reset();
  }
  return ret;
}

bool RTNLMessage::DecodeInternal(const ByteString& msg) {
  const RTNLHeader* hdr =
      reinterpret_cast<const RTNLHeader*>(msg.GetConstData());

  if (msg.GetLength() < sizeof(hdr->hdr) ||
      msg.GetLength() < hdr->hdr.nlmsg_len)
    return false;

  Mode mode = kModeUnknown;
  switch (hdr->hdr.nlmsg_type) {
  case RTM_NEWLINK:
  case RTM_NEWADDR:
  case RTM_NEWROUTE:
  case RTM_NEWRULE:
  case RTM_NEWNDUSEROPT:
  case RTM_NEWNEIGH:
    mode = kModeAdd;
    break;

  case RTM_DELLINK:
  case RTM_DELADDR:
  case RTM_DELROUTE:
  case RTM_DELRULE:
  case RTM_DELNEIGH:
    mode = kModeDelete;
    break;

  default:
    return false;
  }

  rtattr* attr_data = nullptr;
  int attr_length = 0;

  switch (hdr->hdr.nlmsg_type) {
  case RTM_NEWLINK:
  case RTM_DELLINK:
    if (!DecodeLink(hdr, mode, &attr_data, &attr_length))
      return false;
    break;

  case RTM_NEWADDR:
  case RTM_DELADDR:
    if (!DecodeAddress(hdr, mode, &attr_data, &attr_length))
      return false;
    break;

  case RTM_NEWROUTE:
  case RTM_DELROUTE:
    if (!DecodeRoute(hdr, mode, &attr_data, &attr_length))
      return false;
    break;

  case RTM_NEWRULE:
  case RTM_DELRULE:
    if (!DecodeRule(hdr, mode, &attr_data, &attr_length))
      return false;
    break;

  case RTM_NEWNDUSEROPT:
    if (!DecodeNdUserOption(hdr, mode, &attr_data, &attr_length))
      return false;
    break;

  case RTM_NEWNEIGH:
  case RTM_DELNEIGH:
    if (!DecodeNeighbor(hdr, mode, &attr_data, &attr_length))
      return false;
    break;

  default:
    NOTREACHED();
  }

  flags_ = hdr->hdr.nlmsg_flags;
  seq_ = hdr->hdr.nlmsg_seq;
  pid_ = hdr->hdr.nlmsg_pid;

  while (attr_data && RTA_OK(attr_data, attr_length)) {
    SetAttribute(
        attr_data->rta_type,
        ByteString(reinterpret_cast<unsigned char*>(RTA_DATA(attr_data)),
                   RTA_PAYLOAD(attr_data)));
    attr_data = RTA_NEXT(attr_data, attr_length);
  }

  if (attr_length) {
    // We hit a parse error while going through the attributes
    attributes_.clear();
    return false;
  }

  return true;
}

bool RTNLMessage::DecodeLink(const RTNLHeader* hdr,
                             Mode mode,
                             rtattr** attr_data,
                             int* attr_length) {
  if (hdr->hdr.nlmsg_len < NLMSG_LENGTH(sizeof(hdr->ifi))) {
    return false;
  }

  mode_ = mode;
  *attr_data = IFLA_RTA(NLMSG_DATA(&hdr->hdr));
  *attr_length = IFLA_PAYLOAD(&hdr->hdr);

  type_ = kTypeLink;
  family_ = hdr->ifi.ifi_family;
  interface_index_ = hdr->ifi.ifi_index;
  set_link_status(LinkStatus(hdr->ifi.ifi_type,
                             hdr->ifi.ifi_flags,
                             hdr->ifi.ifi_change));
  return true;
}

bool RTNLMessage::DecodeAddress(const RTNLHeader* hdr,
                                Mode mode,
                                rtattr** attr_data,
                                int* attr_length) {
  if (hdr->hdr.nlmsg_len < NLMSG_LENGTH(sizeof(hdr->ifa))) {
    return false;
  }
  mode_ = mode;
  *attr_data = IFA_RTA(NLMSG_DATA(&hdr->hdr));
  *attr_length = IFA_PAYLOAD(&hdr->hdr);

  type_ = kTypeAddress;
  family_ = hdr->ifa.ifa_family;
  interface_index_ = hdr->ifa.ifa_index;
  set_address_status(AddressStatus(hdr->ifa.ifa_prefixlen,
                                   hdr->ifa.ifa_flags,
                                   hdr->ifa.ifa_scope));
  return true;
}

bool RTNLMessage::DecodeRoute(const RTNLHeader* hdr,
                              Mode mode,
                              rtattr** attr_data,
                              int* attr_length) {
  if (hdr->hdr.nlmsg_len < NLMSG_LENGTH(sizeof(hdr->rtm))) {
    return false;
  }
  mode_ = mode;
  *attr_data = RTM_RTA(NLMSG_DATA(&hdr->hdr));
  *attr_length = RTM_PAYLOAD(&hdr->hdr);

  type_ = kTypeRoute;
  family_ = hdr->rtm.rtm_family;
  set_route_status(RouteStatus(hdr->rtm.rtm_dst_len,
                               hdr->rtm.rtm_src_len,
                               hdr->rtm.rtm_table,
                               hdr->rtm.rtm_protocol,
                               hdr->rtm.rtm_scope,
                               hdr->rtm.rtm_type,
                               hdr->rtm.rtm_flags));
  return true;
}

bool RTNLMessage::DecodeRule(const RTNLHeader* hdr,
                             Mode mode,
                             rtattr** attr_data,
                             int* attr_length) {
  if (hdr->hdr.nlmsg_len < NLMSG_LENGTH(sizeof(hdr->rtm))) {
    return false;
  }
  mode_ = mode;
  *attr_data = RTM_RTA(NLMSG_DATA(&hdr->hdr));
  *attr_length = RTM_PAYLOAD(&hdr->hdr);

  type_ = kTypeRule;
  family_ = hdr->rtm.rtm_family;
  set_route_status(RouteStatus(hdr->rtm.rtm_dst_len,
                               hdr->rtm.rtm_src_len,
                               hdr->rtm.rtm_table,
                               hdr->rtm.rtm_protocol,
                               hdr->rtm.rtm_scope,
                               hdr->rtm.rtm_type,
                               hdr->rtm.rtm_flags));
  return true;
}

bool RTNLMessage::DecodeNdUserOption(const RTNLHeader* hdr,
                                     Mode mode,
                                     rtattr** attr_data,
                                     int* attr_length) {
  size_t min_payload_len = sizeof(hdr->nd_user_opt) +
                           sizeof(struct nduseroptmsg) +
                           sizeof(NDUserOptionHeader);
  if (hdr->hdr.nlmsg_len < NLMSG_LENGTH(min_payload_len)) {
    return false;
  }

  mode_ = mode;
  interface_index_ = hdr->nd_user_opt.nduseropt_ifindex;
  family_ = hdr->nd_user_opt.nduseropt_family;

  // Verify IP family.
  if (family_ != IPAddress::kFamilyIPv6) {
    return false;
  }
  // Verify message must at-least contain the option header.
  if (hdr->nd_user_opt.nduseropt_opts_len < sizeof(NDUserOptionHeader)) {
    return false;
  }

  // Parse the option header.
  const NDUserOptionHeader* nd_user_option_header =
      reinterpret_cast<const NDUserOptionHeader*>(
          reinterpret_cast<const uint8_t*>(&hdr->nd_user_opt) +
          sizeof(struct nduseroptmsg));
  uint32_t lifetime = ntohl(nd_user_option_header->lifetime);

  // Verify option length.
  // The length field in the header is in units of 8 octets.
  int opt_len = static_cast<int>(nd_user_option_header->length) * 8;
  if (opt_len != hdr->nd_user_opt.nduseropt_opts_len) {
    return false;
  }

  // Determine option data pointer and data length.
  const uint8_t* option_data =
      reinterpret_cast<const uint8_t*>(nd_user_option_header + 1);
  int data_len = opt_len - sizeof(NDUserOptionHeader);
  if (hdr->hdr.nlmsg_len < NLMSG_LENGTH(min_payload_len + data_len)) {
    return false;
  }

  if (nd_user_option_header->type == ND_OPT_DNSSL) {
    // TODO(zqiu): Parse DNSSL (DNS Search List) option.
    type_ = kTypeDnssl;
    return true;
  } else if (nd_user_option_header->type == ND_OPT_RDNSS) {
    // Parse RNDSS (Recursive DNS Server) option.
    type_ = kTypeRdnss;
    return ParseRdnssOption(option_data, data_len, lifetime);
  }

  return false;
}

bool RTNLMessage::ParseRdnssOption(const uint8_t* data,
                                   int length,
                                   uint32_t lifetime) {
  const int addr_length = IPAddress::GetAddressLength(IPAddress::kFamilyIPv6);

  // Verify data size are multiple of individual address size.
  if (length % addr_length != 0) {
    return false;
  }

  // Parse the DNS server addresses.
  std::vector<IPAddress> dns_server_addresses;
  while (length > 0) {
    dns_server_addresses.push_back(
        IPAddress(IPAddress::kFamilyIPv6,
                  ByteString(data, addr_length)));
    length -= addr_length;
    data += addr_length;
  }
  set_rdnss_option(RdnssOption(lifetime, dns_server_addresses));
  return true;
}

bool RTNLMessage::DecodeNeighbor(const RTNLHeader* hdr,
                                 Mode mode,
                                 rtattr** attr_data,
                                 int* attr_length) {
  if (hdr->hdr.nlmsg_len < NLMSG_LENGTH(sizeof(hdr->ndm))) {
    return false;
  }

  mode_ = mode;
  interface_index_ = hdr->ndm.ndm_ifindex;
  family_ = hdr->ndm.ndm_family;
  type_ = kTypeNeighbor;

  *attr_data = RTM_RTA(NLMSG_DATA(&hdr->hdr));
  *attr_length = RTM_PAYLOAD(&hdr->hdr);

  set_neighbor_status(NeighborStatus(hdr->ndm.ndm_state,
                                     hdr->ndm.ndm_flags,
                                     hdr->ndm.ndm_type));
  return true;
}

ByteString RTNLMessage::Encode() const {
  if (type_ != kTypeLink &&
      type_ != kTypeAddress &&
      type_ != kTypeRoute &&
      type_ != kTypeRule &&
      type_ != kTypeNeighbor) {
    return ByteString();
  }

  RTNLHeader hdr;
  hdr.hdr.nlmsg_flags = flags_;
  hdr.hdr.nlmsg_seq = seq_;
  hdr.hdr.nlmsg_pid = pid_;

  if (mode_ == kModeGet) {
    if (type_ == kTypeLink) {
      hdr.hdr.nlmsg_type = RTM_GETLINK;
    } else if (type_ == kTypeAddress) {
      hdr.hdr.nlmsg_type = RTM_GETADDR;
    } else if (type_ == kTypeRoute) {
      hdr.hdr.nlmsg_type = RTM_GETROUTE;
    } else if (type_ == kTypeRule) {
      hdr.hdr.nlmsg_type = RTM_GETRULE;
    } else if (type_ == kTypeNeighbor) {
      hdr.hdr.nlmsg_type = RTM_GETNEIGH;
    } else {
      NOTIMPLEMENTED();
      return ByteString();
    }
    hdr.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr.ifi));
    hdr.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    hdr.ifi.ifi_family = family_;
  } else {
    switch (type_) {
    case kTypeLink:
      if (!EncodeLink(&hdr)) {
        return ByteString();
      }
      break;

    case kTypeAddress:
      if (!EncodeAddress(&hdr)) {
        return ByteString();
      }
      break;

    case kTypeRoute:
    case kTypeRule:
      if (!EncodeRoute(&hdr)) {
        return ByteString();
      }
      break;

    case kTypeNeighbor:
      if (!EncodeNeighbor(&hdr)) {
        return ByteString();
      }
      break;

    default:
      NOTREACHED();
    }
  }

  size_t header_length = hdr.hdr.nlmsg_len;
  ByteString attributes;

  for (const auto& id_attribute_pair : attributes_) {
    size_t len = RTA_LENGTH(id_attribute_pair.second.GetLength());
    hdr.hdr.nlmsg_len = NLMSG_ALIGN(hdr.hdr.nlmsg_len) + RTA_ALIGN(len);

    struct rtattr rt_attr = {
      static_cast<unsigned short>(len),  // NOLINT(runtime/int)
      id_attribute_pair.first,
    };
    ByteString attr_header(reinterpret_cast<unsigned char*>(&rt_attr),
                           sizeof(rt_attr));
    attr_header.Resize(RTA_ALIGN(attr_header.GetLength()));
    attributes.Append(attr_header);

    ByteString attr_data(id_attribute_pair.second);
    attr_data.Resize(RTA_ALIGN(attr_data.GetLength()));
    attributes.Append(attr_data);
  }

  ByteString packet(reinterpret_cast<unsigned char*>(&hdr), header_length);
  packet.Append(attributes);

  return packet;
}

bool RTNLMessage::EncodeLink(RTNLHeader* hdr) const {
  switch (mode_) {
    case kModeAdd:
      hdr->hdr.nlmsg_type = RTM_NEWLINK;
      break;
    case kModeDelete:
      hdr->hdr.nlmsg_type = RTM_DELLINK;
      break;
    case kModeQuery:
      hdr->hdr.nlmsg_type = RTM_GETLINK;
      break;
    default:
      NOTIMPLEMENTED();
      return false;
  }
  hdr->hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr->ifi));
  hdr->ifi.ifi_family = family_;
  hdr->ifi.ifi_index = interface_index_;
  hdr->ifi.ifi_type = link_status_.type;
  hdr->ifi.ifi_flags = link_status_.flags;
  hdr->ifi.ifi_change = link_status_.change;
  return true;
}

bool RTNLMessage::EncodeAddress(RTNLHeader* hdr) const {
  switch (mode_) {
    case kModeAdd:
      hdr->hdr.nlmsg_type = RTM_NEWADDR;
      break;
    case kModeDelete:
      hdr->hdr.nlmsg_type = RTM_DELADDR;
      break;
    case kModeQuery:
      hdr->hdr.nlmsg_type = RTM_GETADDR;
      break;
    default:
      NOTIMPLEMENTED();
      return false;
  }
  hdr->hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr->ifa));
  hdr->ifa.ifa_family = family_;
  hdr->ifa.ifa_prefixlen = address_status_.prefix_len;
  hdr->ifa.ifa_flags = address_status_.flags;
  hdr->ifa.ifa_scope = address_status_.scope;
  hdr->ifa.ifa_index = interface_index_;
  return true;
}

bool RTNLMessage::EncodeRoute(RTNLHeader* hdr) const {
  // Routes and routing rules are both based on struct rtm
  switch (mode_) {
    case kModeAdd:
      hdr->hdr.nlmsg_type = (type_ == kTypeRoute) ? RTM_NEWROUTE : RTM_NEWRULE;
      break;
    case kModeDelete:
      hdr->hdr.nlmsg_type = (type_ == kTypeRoute) ? RTM_DELROUTE : RTM_DELRULE;
      break;
    case kModeQuery:
      hdr->hdr.nlmsg_type = (type_ == kTypeRoute) ? RTM_GETROUTE : RTM_GETRULE;
      break;
    default:
      NOTIMPLEMENTED();
      return false;
  }
  hdr->hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr->rtm));
  hdr->rtm.rtm_family = family_;
  hdr->rtm.rtm_dst_len = route_status_.dst_prefix;
  hdr->rtm.rtm_src_len = route_status_.src_prefix;
  hdr->rtm.rtm_table = route_status_.table;
  hdr->rtm.rtm_protocol = route_status_.protocol;
  hdr->rtm.rtm_scope = route_status_.scope;
  hdr->rtm.rtm_type = route_status_.type;
  hdr->rtm.rtm_flags = route_status_.flags;
  return true;
}

bool RTNLMessage::EncodeNeighbor(RTNLHeader* hdr) const {
  switch (mode_) {
    case kModeAdd:
      hdr->hdr.nlmsg_type = RTM_NEWNEIGH;
      break;
    case kModeDelete:
      hdr->hdr.nlmsg_type = RTM_DELNEIGH;
      break;
    case kModeQuery:
      hdr->hdr.nlmsg_type = RTM_GETNEIGH;
      break;
    default:
      NOTIMPLEMENTED();
      return false;
  }
  hdr->hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr->ndm));
  hdr->ndm.ndm_family = family_;
  hdr->ndm.ndm_ifindex = interface_index_;
  hdr->ndm.ndm_state = neighbor_status_.state;
  hdr->ndm.ndm_flags = neighbor_status_.flags;
  hdr->ndm.ndm_type = neighbor_status_.type;
  return true;
}

void RTNLMessage::Reset() {
  type_ = kTypeUnknown;
  mode_ = kModeUnknown;
  flags_ = 0;
  seq_ = 0;
  pid_ = 0;
  interface_index_ = 0;
  family_ = IPAddress::kFamilyUnknown;
  link_status_ = LinkStatus();
  address_status_ = AddressStatus();
  route_status_ = RouteStatus();
  neighbor_status_ = NeighborStatus();
  rdnss_option_ = RdnssOption();
  attributes_.clear();
}

// static
std::string RTNLMessage::ModeToString(RTNLMessage::Mode mode) {
  switch (mode) {
    case RTNLMessage::kModeGet:
      return "Get";
    case RTNLMessage::kModeAdd:
      return "Add";
    case RTNLMessage::kModeDelete:
      return "Delete";
    case RTNLMessage::kModeQuery:
      return "Query";
    default:
      return "UnknownMode";
  }
}

std::string RTNLMessage::ToString() const {
  std::string str = base::StringPrintf("%s %s: ",
                                       ModeToString(mode()).c_str(),
                                       TypeToString(type()));
  switch (type()) {
    case RTNLMessage::kTypeLink:
      str += link_status_.ToString();
      break;
    case RTNLMessage::kTypeAddress:
      str += address_status_.ToString();
      break;
    case RTNLMessage::kTypeRoute:
    case RTNLMessage::kTypeRule:
      str += route_status_.ToString();
      break;
    case RTNLMessage::kTypeRdnss:
    case RTNLMessage::kTypeDnssl:
      str += rdnss_option_.ToString();
      break;
    case RTNLMessage::kTypeNeighbor:
      str += neighbor_status_.ToString();
      break;
    default:
      break;
  }
  return str;
}

}  // namespace shill
