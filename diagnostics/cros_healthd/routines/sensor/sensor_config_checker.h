// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_CONFIG_CHECKER_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_CONFIG_CHECKER_H_

#include <set>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <iioservice/mojo/sensor.mojom.h>

#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/cros_healthd/system/system_config_interface.h"

namespace diagnostics {

// Check if the sensor info from iioservice is consistent with static config.
class SensorConfigChecker {
 public:
  explicit SensorConfigChecker(MojoService* const mojo_service,
                               SystemConfigInterface* const system_config);
  SensorConfigChecker(const SensorConfigChecker&) = delete;
  SensorConfigChecker& operator=(const SensorConfigChecker&) = delete;
  ~SensorConfigChecker();

  void VerifySensorInfo(
      const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
          ids_types,
      base::OnceCallback<void(bool)> on_finish);

 private:
  // Handle the response of sensor attributes from the sensor device.
  void HandleSensorLocationResponse(
      const std::vector<cros::mojom::DeviceType>& sensor_types,
      const std::vector<std::optional<std::string>>& attributes);

  // Verification completion function.
  void CheckSystemConfig(base::OnceCallback<void(bool)> on_finish,
                         bool all_callbacks_called);

  // Unowned. Should outlive this instance.
  MojoService* const mojo_service_;
  SystemConfigInterface* const system_config_;

  // Used to check if the target sensor is present.
  std::set<SensorConfig> iio_sensors_{};

  // Must be the last class member.
  base::WeakPtrFactory<SensorConfigChecker> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSOR_CONFIG_CHECKER_H_