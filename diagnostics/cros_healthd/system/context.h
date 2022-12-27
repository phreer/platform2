// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/tick_clock.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/udev/udev_monitor.h>
#include <chromeos/chromeos-config/libcros_config/cros_config_interface.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

#include "diagnostics/common/system/powerd_adapter.h"
#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/network/network_health_adapter.h"
#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter.h"
#include "diagnostics/cros_healthd/system/libdrm_util.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/cros_healthd/system/pci_util.h"
#include "diagnostics/cros_healthd/system/system_config_interface.h"
#include "diagnostics/cros_healthd/system/system_utilities.h"

namespace brillo {
class Udev;
};

namespace org {
namespace chromium {
class AttestationProxyInterface;
class debugdProxyInterface;
class PowerManagerProxyInterface;
class TpmManagerProxyInterface;

namespace cras {
class ControlProxyInterface;
}  // namespace cras
}  // namespace chromium

namespace freedesktop {
class fwupdProxyInterface;
}  // namespace freedesktop

class bluezProxy;
}  // namespace org

namespace diagnostics {
class BluetoothEventHub;
class BluetoothInfoManager;

// A context class for holding the helper objects used in cros_healthd, which
// simplifies the passing of the helper objects to other objects. For instance,
// instead of passing various helper objects to an object via its constructor,
// the context object is passed.
class Context {
 public:
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  virtual ~Context();

  static std::unique_ptr<Context> Create(
      mojo::PlatformChannelEndpoint executor_endpoint,
      std::unique_ptr<brillo::UdevMonitor>&& udev_monitor,
      base::OnceClosure shutdown_callback);

  // Creates an object for accessing |LibdrmUtil| interface.
  virtual std::unique_ptr<LibdrmUtil> CreateLibdrmUtil();

  // Creates an object for accessing |PciUtil| interface.
  virtual std::unique_ptr<PciUtil> CreatePciUtil();

  // Return current time.
  virtual const base::Time time() const;

  // Accessors for the various helper objects:
  // Use the object returned by attestation_proxy() to get the attestation
  // information from attestation service.
  org::chromium::AttestationProxyInterface* attestation_proxy() const {
    return attestation_proxy_.get();
  }
  // Use the object returned by cros_config() to query the device's
  // configuration file.
  brillo::CrosConfigInterface* cros_config() const {
    return cros_config_.get();
  }
  // Use the object returned by debugd_proxy() to make calls to debugd. Example:
  // cros_healthd calls out to debugd when it needs to collect smart battery
  // metrics like manufacture_date_smart and temperature_smart.
  org::chromium::debugdProxyInterface* debugd_proxy() const {
    return debugd_proxy_.get();
  }
  // Use the object returned by power_manager_proxy() to communicate with power
  // manager daemon through dbus.
  org::chromium::PowerManagerProxyInterface* power_manager_proxy() const {
    return power_manager_proxy_.get();
  }
  // Use the object returned by cras_proxy() to communicate with cras daemon
  // through dbus.
  org::chromium::cras::ControlProxyInterface* cras_proxy() const {
    return cras_proxy_.get();
  }
  // Use the object returned by fwupd_proxy() to communicate with fwupd through
  // dbus.
  org::freedesktop::fwupdProxyInterface* fwupd_proxy() const {
    return fwupd_proxy_.get();
  }
  // Use the object returned by network_health_adapter() to make requests to the
  // NetworkHealthService. Example: cros_healthd calls out to the
  // NetworkHealthService to get network telemetry data.
  NetworkHealthAdapter* network_health_adapter() const {
    return network_health_adapter_.get();
  }
  // Use the object returned by network_diagnostics_adapter() to make calls to
  // the NetworkDiagnosticsRoutines interface implemented by the browser.
  // Example: cros_healthd calls out to the NetworkDiagnosticsRoutines interface
  // with async callbacks when it needs to run network diagnostics.
  NetworkDiagnosticsAdapter* network_diagnostics_adapter() const {
    return network_diagnostics_adapter_.get();
  }
  // Use the object returned by powerd_adapter() to subscribe to notifications
  // from powerd.
  PowerdAdapter* powerd_adapter() const { return powerd_adapter_.get(); }
  // Use the object returned by root_dir() to determine the root directory of
  // the system.
  const base::FilePath& root_dir() const { return root_dir_; }
  // Use the object returned by udev_monitor() to receive udev events.
  const std::unique_ptr<brillo::UdevMonitor>& udev_monitor() const {
    return udev_monitor_;
  }
  // Use the object returned by system_config() to determine which conditional
  // features a device supports.
  SystemConfigInterface* system_config() const { return system_config_.get(); }
  // Use the interface returned by executor() to make calls to the root-level
  // executor.
  virtual ash::cros_healthd::mojom::Executor* executor() {
    return executor_.get();
  }
  // Use the object returned by system_utils() to access system utilities.
  SystemUtilities* system_utils() const { return system_utils_.get(); }
  // Use the object returned by bluetooth_info_manager() to access Bluetooth
  // adapter and device information from the Bluetooth proxy.
  BluetoothInfoManager* bluetooth_info_manager() const {
    return bluetooth_info_manager_.get();
  }
  // Use the object returned by bluetooth_event_hub() to subscribe Bluetooth
  // events.
  BluetoothEventHub* bluetooth_event_hub() const {
    return bluetooth_event_hub_.get();
  }
  // Use the object returned by tick_clock() to track the passage of time.
  base::TickClock* tick_clock() const { return tick_clock_.get(); }
  // Use the object returned by tpm_manager_proxy() to get the tpm information
  // from tpm manager.
  org::chromium::TpmManagerProxyInterface* tpm_manager_proxy() const {
    return tpm_manager_proxy_.get();
  }
  // Use the object returned by udev() to access udev related interfaces.
  brillo::Udev* udev() const { return udev_.get(); }
  // Get MojoService to access external mojo services.
  MojoService* mojo_service() const { return mojo_service_.get(); }

 private:
  Context();

 private:
  // Allows MockContext to override the default helper objects.
  friend class MockContext;

  // This should be the only connection to D-Bus. Use |connection_| to get the
  // |dbus_bus|.
  brillo::DBusConnection connection_;

  // Used by this object to initialize the SystemConfig. Used for reading
  // cros_config properties to determine device feature support.
  std::unique_ptr<brillo::CrosConfigInterface> cros_config_;

  // Used to watch udev events.
  std::unique_ptr<brillo::UdevMonitor> udev_monitor_;

  // Used to access Bluetooth info and watch Bluetooth events.
  std::unique_ptr<org::bluezProxy> bluez_proxy_;

  // Members accessed via the accessor functions defined above.
  std::unique_ptr<org::chromium::AttestationProxyInterface> attestation_proxy_;
  std::unique_ptr<org::chromium::cras::ControlProxyInterface> cras_proxy_;
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
  std::unique_ptr<org::freedesktop::fwupdProxyInterface> fwupd_proxy_;
  std::unique_ptr<MojoService> mojo_service_;
  std::unique_ptr<NetworkHealthAdapter> network_health_adapter_;
  std::unique_ptr<NetworkDiagnosticsAdapter> network_diagnostics_adapter_;
  std::unique_ptr<org::chromium::PowerManagerProxyInterface>
      power_manager_proxy_;
  std::unique_ptr<PowerdAdapter> powerd_adapter_;
  std::unique_ptr<SystemConfigInterface> system_config_;
  mojo::Remote<ash::cros_healthd::mojom::Executor> executor_;
  std::unique_ptr<SystemUtilities> system_utils_;
  std::unique_ptr<BluetoothEventHub> bluetooth_event_hub_;
  std::unique_ptr<BluetoothInfoManager> bluetooth_info_manager_;
  std::unique_ptr<base::TickClock> tick_clock_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy_;
  std::unique_ptr<brillo::Udev> udev_;
  base::FilePath root_dir_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_
