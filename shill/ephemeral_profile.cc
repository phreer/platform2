// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ephemeral_profile.h"

#include <string>
#include <map>

#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/manager.h"

using std::map;
using std::string;

namespace shill {

EphemeralProfile::EphemeralProfile(ControlInterface *control_interface,
                                   Manager *manager)
    : Profile(control_interface, manager, Identifier(), "", false) {
}

EphemeralProfile::~EphemeralProfile() {}

void EphemeralProfile::Finalize() {
  services()->clear();
}

bool EphemeralProfile::Save() {
  NOTREACHED();
  return false;
}

}  // namespace shill
