// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>

#include "diagnostics/cros_healthd/cros_healthd_routine_factory.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/utils/mojo_service_provider.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// Production implementation of the CrosHealthdDiagnosticsService interface.
class CrosHealthdRoutineService final
    : public chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService {
 public:
  CrosHealthdRoutineService(Context* context,
                            CrosHealthdRoutineFactory* routine_factory);
  CrosHealthdRoutineService(const CrosHealthdRoutineService&) = delete;
  CrosHealthdRoutineService& operator=(const CrosHealthdRoutineService&) =
      delete;
  ~CrosHealthdRoutineService() override;

  // Registers |callback| to run when the service is ready. If the service is
  // already ready, |callback| will be run immediately.
  void RegisterServiceReadyCallback(base::OnceClosure callback);

  // chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService overrides:
  void GetAvailableRoutines(GetAvailableRoutinesCallback callback) override;
  void GetRoutineUpdate(
      int32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output,
      GetRoutineUpdateCallback callback) override;
  void RunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const std::optional<std::string>& expected_power_type,
      RunAcPowerRoutineCallback callback) override;
  void RunBatteryCapacityRoutine(
      RunBatteryCapacityRoutineCallback callback) override;
  void RunBatteryChargeRoutine(
      uint32_t length_seconds,
      uint32_t minimum_charge_percent_required,
      RunBatteryChargeRoutineCallback callback) override;
  void RunBatteryDischargeRoutine(
      uint32_t length_seconds,
      uint32_t maximum_discharge_percent_allowed,
      RunBatteryDischargeRoutineCallback callback) override;
  void RunBatteryHealthRoutine(
      RunBatteryHealthRoutineCallback callback) override;
  void RunCaptivePortalRoutine(
      RunCaptivePortalRoutineCallback callback) override;
  void RunCpuCacheRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunCpuCacheRoutineCallback callback) override;
  void RunCpuStressRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunCpuStressRoutineCallback callback) override;
  void RunDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      uint32_t length_seconds,
      uint32_t file_size_mb,
      RunDiskReadRoutineCallback callback) override;
  void RunDnsLatencyRoutine(RunDnsLatencyRoutineCallback callback) override;
  void RunDnsResolutionRoutine(
      RunDnsResolutionRoutineCallback callback) override;
  void RunDnsResolverPresentRoutine(
      RunDnsResolverPresentRoutineCallback callback) override;
  void RunFloatingPointAccuracyRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunFloatingPointAccuracyRoutineCallback callback) override;
  void RunGatewayCanBePingedRoutine(
      RunGatewayCanBePingedRoutineCallback callback) override;
  void RunHasSecureWiFiConnectionRoutine(
      RunHasSecureWiFiConnectionRoutineCallback callback) override;
  void RunHttpFirewallRoutine(RunHttpFirewallRoutineCallback callback) override;
  void RunHttpsFirewallRoutine(
      RunHttpsFirewallRoutineCallback callback) override;
  void RunHttpsLatencyRoutine(RunHttpsLatencyRoutineCallback callback) override;
  void RunLanConnectivityRoutine(
      RunLanConnectivityRoutineCallback callback) override;
  void RunMemoryRoutine(RunMemoryRoutineCallback callback) override;
  void RunNvmeSelfTestRoutine(
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type,
      RunNvmeSelfTestRoutineCallback callback) override;
  void RunNvmeWearLevelRoutine(
      uint32_t wear_level_threshold,
      RunNvmeWearLevelRoutineCallback callback) override;
  void RunPrimeSearchRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunPrimeSearchRoutineCallback callback) override;
  void RunSignalStrengthRoutine(
      RunSignalStrengthRoutineCallback callback) override;
  void RunSmartctlCheckRoutine(
      RunSmartctlCheckRoutineCallback callback) override;
  void RunUrandomRoutine(
      chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
      RunUrandomRoutineCallback callback) override;
  void RunVideoConferencingRoutine(
      const std::optional<std::string>& stun_server_hostname,
      RunVideoConferencingRoutineCallback callback) override;
  void RunArcHttpRoutine(RunArcHttpRoutineCallback callback) override;
  void RunArcPingRoutine(RunArcPingRoutineCallback callback) override;
  void RunArcDnsResolutionRoutine(
      RunArcDnsResolutionRoutineCallback callback) override;

 private:
  void RunRoutine(
      std::unique_ptr<DiagnosticRoutine> routine,
      chromeos::cros_healthd::mojom::DiagnosticRoutineEnum routine_enum,
      base::OnceCallback<
          void(chromeos::cros_healthd::mojom::RunRoutineResponsePtr)> callback);

  // Callback for checking whether nvme-self-test is supported.
  void HandleNvmeSelfTestSupportedResponse(bool supported);

  // Updates |ready_| and runs the elements of |service_ready_callbacks_|.
  // Called when this service is ready to handle CrosHealthdDiagnosticsService
  // method calls.
  void OnServiceReady();

  // Checks what routines are supported on the device and populates the member
  // available_routines_. Run |completion_callback| when all the checks are
  // done.
  void PopulateAvailableRoutines(base::OnceClosure completion_callback);

  // Whether this service is ready to handle CrosHealthdDiagnosticsService
  // method calls.
  bool ready_;
  // Map from IDs to instances of diagnostics routines that have
  // been started.
  std::map<int32_t, std::unique_ptr<DiagnosticRoutine>> active_routines_;
  // Generator for IDs - currently, when we need a new ID we just return
  // next_id_, then increment next_id_.
  int32_t next_id_ = 1;
  // Each of the supported diagnostic routines. Must be kept in sync with the
  // enums in diagnostics/mojo/cros_health_diagnostics.mojom.
  std::set<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
      available_routines_;
  // Unowned pointer that should outlive this instance.
  Context* const context_ = nullptr;
  // Responsible for making the routines. Unowned pointer that should outlive
  // this instance.
  CrosHealthdRoutineFactory* routine_factory_ = nullptr;
  // The callbacks to run when the service become ready.
  std::vector<base::OnceClosure> service_ready_callbacks_;
  // Mojo service provider to provide service to mojo service manager.
  MojoServiceProvider<
      chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService>
      provider_;

  // Must be the last class member.
  base::WeakPtrFactory<CrosHealthdRoutineService> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_H_
