// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos-config/libcros_config/cros_config.h>

#include "rmad/utils/json_store.h"

namespace rmad {

namespace {

// TODO(genechang): We should build the configuration ourselves to
// prevent possible changes to the configuration file in the future.
const std::string kChromeosConfigPath(
    "/usr/share/chromeos-config/yaml/config.yaml");

constexpr char kChromeos[] = "chromeos";
constexpr char kChromeosConfigs[] = "configs";

// cros_config root path.
constexpr char kCrosRootPath[] = "/";
constexpr char kCrosModelNameKey[] = "name";

// cros_config identity path.
constexpr char kCrosIdentityPath[] = "identity";
constexpr char kCrosIdentitySkuKey[] = "sku-id";
constexpr char kCrosIdentityCustomLabelTagKey[] = "custom-label-tag";

// cros_config rmad path.
constexpr char kCrosRmadPath[] = "/rmad";
constexpr char kCrosRmadEnabledKey[] = "enabled";
constexpr char kCrosRmadHasCbiKey[] = "has-cbi";

// cros_config rmad/ssfc path.
constexpr char kCrosRmadSsfcPath[] = "/rmad/ssfc";
constexpr char kCrosRmadSsfcMaskKey[] = "mask";
constexpr char kCrosRmadSsfcComponentsPath[] = "/rmad/ssfc/components";
constexpr char kCrosRmadSsfcComponentsIdentifierKey[] = "identifier";
constexpr char kCrosRmadSsfcComponentsValueKey[] = "value";
constexpr int kMaxSsfcComponentsNum = 1024;

constexpr char kTrueStr[] = "true";

}  // namespace

CrosConfigUtilsImpl::CrosConfigUtilsImpl()
    : config_file_path_(kChromeosConfigPath) {
  cros_config_ = std::make_unique<brillo::CrosConfig>();
}

CrosConfigUtilsImpl::CrosConfigUtilsImpl(
    const std::string& config_file_path,
    std::unique_ptr<brillo::CrosConfigInterface> cros_config)
    : config_file_path_(config_file_path),
      cros_config_(std::move(cros_config)) {}

bool CrosConfigUtilsImpl::GetRmadConfig(RmadConfig* config) const {
  DCHECK(config);

  config->enabled = GetBooleanWithDefault(std::string(kCrosRmadPath),
                                          kCrosRmadEnabledKey, false);
  config->has_cbi = GetBooleanWithDefault(std::string(kCrosRmadPath),
                                          kCrosRmadHasCbiKey, false);
  config->ssfc = GetSsfc();

  return true;
}

bool CrosConfigUtilsImpl::GetModelName(std::string* model_name) const {
  DCHECK(model_name);

  return cros_config_->GetString(kCrosRootPath, kCrosModelNameKey, model_name);
}

bool CrosConfigUtilsImpl::GetSkuId(int* sku_id) const {
  DCHECK(sku_id);

  std::string sku_id_str;
  if (!cros_config_->GetString(
          std::string(kCrosRootPath) + std::string(kCrosIdentityPath),
          kCrosIdentitySkuKey, &sku_id_str)) {
    return false;
  }

  return base::StringToInt(sku_id_str, sku_id);
}

bool CrosConfigUtilsImpl::GetCustomLabelTag(
    std::string* custom_label_tag) const {
  DCHECK(custom_label_tag);

  return cros_config_->GetString(
      std::string(kCrosRootPath) + std::string(kCrosIdentityPath),
      kCrosIdentityCustomLabelTagKey, custom_label_tag);
}

bool CrosConfigUtilsImpl::GetSkuIdList(std::vector<int>* sku_id_list) const {
  DCHECK(sku_id_list);

  std::vector<base::Value> values;
  if (!GetMatchedItemsFromIdentity(kCrosIdentitySkuKey, &values)) {
    return false;
  }

  sku_id_list->clear();
  for (auto& value : values) {
    if (value.is_int()) {
      sku_id_list->push_back(value.GetInt());
    }
  }

  sort(sku_id_list->begin(), sku_id_list->end());
  return true;
}

bool CrosConfigUtilsImpl::GetCustomLabelTagList(
    std::vector<std::string>* custom_label_tag_list) const {
  DCHECK(custom_label_tag_list);

  std::vector<base::Value> values;
  if (!GetMatchedItemsFromIdentity(kCrosIdentityCustomLabelTagKey, &values)) {
    return false;
  }

  custom_label_tag_list->clear();
  for (auto& value : values) {
    if (value.is_string()) {
      custom_label_tag_list->push_back(value.GetString());
    }
  }

  sort(custom_label_tag_list->begin(), custom_label_tag_list->end());
  return true;
}

bool CrosConfigUtilsImpl::GetMatchedItemsFromIdentity(
    const std::string& key, std::vector<base::Value>* list) const {
  DCHECK(list);

  list->clear();
  std::set<base::Value> items;

  std::string model_name;
  if (!GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name for comparison";
    return false;
  }

  scoped_refptr<JsonStore> json_store =
      base::MakeRefCounted<JsonStore>(base::FilePath(config_file_path_));

  if (auto error = json_store->GetReadError();
      error != JsonStore::READ_ERROR_NONE) {
    LOG_STREAM(ERROR) << "Failed to parse file due to error code #" << error;
    return false;
  }

  base::Value cros;
  if (!json_store->GetValue(kChromeos, &cros)) {
    LOG(ERROR) << "Failed to get the chromeos section from the file";
    return false;
  }
  DCHECK(cros.is_dict());

  base::Value::List* cros_configs = cros.GetDict().FindList(kChromeosConfigs);
  if (!cros_configs) {
    LOG(ERROR) << "Failed to get the configs section from the file";
    return false;
  }

  for (const auto& config : *cros_configs) {
    DCHECK(config.is_dict());
    const std::string* name = config.GetDict().FindString(kCrosModelNameKey);
    if (!name || *name != model_name) {
      continue;
    }

    const base::Value::Dict* identity =
        config.GetDict().FindDict(kCrosIdentityPath);
    const base::Value* item = identity->Find(key);
    if (item) {
      items.insert(item->Clone());
    }
  }

  for (auto& item : items) {
    list->push_back(item.Clone());
  }
  return true;
}

bool CrosConfigUtilsImpl::GetBooleanWithDefault(const std::string& path,
                                                const std::string& key,
                                                bool default_value) const {
  bool ret = default_value;
  if (std::string value_str; cros_config_->GetString(path, key, &value_str)) {
    ret = (value_str == kTrueStr);
  }
  return ret;
}

SsfcConfig CrosConfigUtilsImpl::GetSsfc() const {
  SsfcConfig ssfc;
  ssfc.mask = GetSsfcMask();
  ssfc.components = GetSsfcComponents();
  // SSFC config integrity check. No component should set the bits in the mask.
  for (const auto& [identifier, value] : ssfc.components) {
    if (value & ssfc.mask) {
      LOG(WARNING) << "Component " << identifier << " has SSFC value " << value
                   << "which conflicts with SSFC mask " << ssfc.mask;
    }
  }
  return ssfc;
}

uint32_t CrosConfigUtilsImpl::GetSsfcMask() const {
  std::string mask_str;
  uint32_t mask;
  if (cros_config_->GetString(kCrosRmadSsfcPath, kCrosRmadSsfcMaskKey,
                              &mask_str) &&
      base::StringToUint(mask_str, &mask)) {
    return mask;
  }
  return 0;
}

std::map<std::string, uint32_t> CrosConfigUtilsImpl::GetSsfcComponents() const {
  std::map<std::string, uint32_t> components;
  for (int i = 0; i < kMaxSsfcComponentsNum; ++i) {
    const std::string path =
        base::StringPrintf("%s/%d", kCrosRmadSsfcComponentsPath, i);
    std::string identifier, value_str;
    uint32_t value;
    if (cros_config_->GetString(path, kCrosRmadSsfcComponentsIdentifierKey,
                                &identifier) &&
        cros_config_->GetString(path, kCrosRmadSsfcComponentsValueKey,
                                &value_str) &&
        base::StringToUint(value_str, &value)) {
      components[identifier] = value;
    } else {
      break;
    }
  }
  return components;
}

}  // namespace rmad
