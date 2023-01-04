// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_IMPL_H_

#include <memory>

#include <base/check.h>
#include <brillo/dbus/dbus_connection.h>
#include <libcrossystem/crossystem.h>
#include <shill/dbus-proxies.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

class ContextImpl : public Context {
 public:
  ~ContextImpl() override;

  crossystem::Crossystem* crossystem() override { return &crossystem_; }

  org::chromium::debugdProxyInterface* debugd_proxy() override {
    CHECK(debugd_proxy_);
    return debugd_proxy_.get();
  };

  HelperInvoker* helper_invoker() override {
    CHECK(helper_invoker_);
    return helper_invoker_.get();
  }

  org::chromium::flimflam::ManagerProxyInterface* shill_manager_proxy()
      override {
    CHECK(shill_manager_proxy_);
    return shill_manager_proxy_.get();
  }

  std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface>
  CreateShillDeviceProxy(const dbus::ObjectPath& path) override {
    CHECK(dbus_bus_);
    return std::make_unique<org::chromium::flimflam::DeviceProxy>(dbus_bus_,
                                                                  path);
  }

 protected:
  // This interface should be used through its derived classes.
  ContextImpl();

  // Setups the dbus connection and the dbus services.
  bool SetupDBusServices();

  // The object to hold the dbus connection.
  brillo::DBusConnection connection_;
  // The object to access crossystem system properties.
  crossystem::CrossystemImpl crossystem_;
  // The reference of the dbus connection.
  scoped_refptr<dbus::Bus> dbus_bus_;
  // The proxy object for dbugd dbus service.
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
  // The object for invoking helper.
  std::unique_ptr<HelperInvoker> helper_invoker_;
  // The proxy object for shill manager.
  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      shill_manager_proxy_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_IMPL_H_
