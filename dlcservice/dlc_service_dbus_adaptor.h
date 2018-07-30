// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_SERVICE_DBUS_ADAPTOR_H_
#define DLCSERVICE_DLC_SERVICE_DBUS_ADAPTOR_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <imageloader/dbus-proxies.h>
#include <update_engine/dbus-proxies.h>

#include "dlcservice/dbus_adaptors/org.chromium.DlcServiceInterface.h"

namespace dlcservice {

// DlcServiceDBusAdaptor is a D-Bus adaptor that manages life-cycles of DLCs
// (Downloadable Content) and provides an API for the rest of the system to
// install/uninstall DLCs.
class DlcServiceDBusAdaptor
    : public org::chromium::DlcServiceInterfaceInterface,
      public org::chromium::DlcServiceInterfaceAdaptor {
 public:
  DlcServiceDBusAdaptor(
      std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
          image_loader_proxy,
      std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
          update_engine_proxy,
      const base::FilePath& manifest_dir,
      const base::FilePath& content_dir);
  ~DlcServiceDBusAdaptor();

  // org::chromium::DlServiceInterfaceInterface overrides:
  bool Install(brillo::ErrorPtr* err,
               const std::string& id_in,
               std::string* mount_point_out) override;
  bool Uninstall(brillo::ErrorPtr* err, const std::string& id_in) override;

 private:
  // Load installed DLC images.
  void LoadDlcImages();

  std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
      image_loader_proxy_;
  std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
      update_engine_proxy_;

  base::FilePath manifest_dir_;
  base::FilePath content_dir_;

  DISALLOW_COPY_AND_ASSIGN(DlcServiceDBusAdaptor);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_SERVICE_DBUS_ADAPTOR_H_
