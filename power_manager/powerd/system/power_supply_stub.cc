// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/power_supply_stub.h"

#include <base/check.h>

namespace power_manager {
namespace system {

PowerSupplyStub::PowerSupplyStub() : refresh_result_(true) {}

PowerSupplyStub::~PowerSupplyStub() {}

void PowerSupplyStub::NotifyObservers() {
  for (PowerSupplyObserver& observer : observers_)
    observer.OnPowerStatusUpdate();
}

void PowerSupplyStub::AddObserver(PowerSupplyObserver* observer) {
  CHECK(observer);
  observers_.AddObserver(observer);
}

void PowerSupplyStub::RemoveObserver(PowerSupplyObserver* observer) {
  CHECK(observer);
  observers_.RemoveObserver(observer);
}

PowerStatus PowerSupplyStub::GetPowerStatus() const {
  return status_;
}

bool PowerSupplyStub::RefreshImmediately() {
  return refresh_result_;
}

void PowerSupplyStub::SetSuspended(bool suspended) {}

void PowerSupplyStub::SetAdaptiveChargingSupported(bool supported) {}

void PowerSupplyStub::SetAdaptiveChargingHeuristicEnabled(bool enabled) {}

void PowerSupplyStub::SetAdaptiveCharging(const base::TimeTicks& target_time,
                                          double hold_percent) {}

void PowerSupplyStub::ClearAdaptiveCharging() {}

}  // namespace system
}  // namespace power_manager
