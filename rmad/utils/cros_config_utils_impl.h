// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CROS_CONFIG_UTILS_IMPL_H_
#define RMAD_UTILS_CROS_CONFIG_UTILS_IMPL_H_

#include "rmad/utils/cros_config_utils.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <chromeos-config/libcros_config/cros_config_interface.h>

namespace rmad {

class CrosConfigUtilsImpl : public CrosConfigUtils {
 public:
  CrosConfigUtilsImpl();
  explicit CrosConfigUtilsImpl(
      const std::string& config_file_path,
      std::unique_ptr<brillo::CrosConfigInterface> cros_config);
  ~CrosConfigUtilsImpl() override = default;

  bool GetModelName(std::string* model_name) const override;
  bool GetCurrentSkuId(int* sku) const override;
  bool GetCurrentCustomLabelTag(std::string* custom_label_tag) const override;
  bool GetSkuIdList(std::vector<int>* sku_list) const override;
  bool GetCustomLabelTagList(
      std::vector<std::string>* custom_label_tag_list) const override;

 private:
  bool GetMatchedItemsFromIdentity(const std::string& key,
                                   std::vector<base::Value>* list) const;

  std::string config_file_path_;
  std::unique_ptr<brillo::CrosConfigInterface> cros_config_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CROS_CONFIG_UTILS_IMPL_H_
