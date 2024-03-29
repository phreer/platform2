// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/iio_ec_sensor_utils_impl.h"

#include <numeric>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

namespace {

constexpr int kMaxNumEntries = 1024;
constexpr int kTimeoutOverheadInMS = 1000;
constexpr double kSecond2Millisecond = 1000.0;
constexpr int kNumberFirstReadsDiscarded = 10;

constexpr char kIioDevicePathPrefix[] = "/sys/bus/iio/devices/iio:device";
constexpr char kIioDeviceEntryName[] = "name";
constexpr char kIioDeviceEntryLocation[] = "location";
constexpr char kIioDeviceEntryFrequencyAvailable[] =
    "sampling_frequency_available";
constexpr char kIioDeviceEntryScale[] = "scale";

constexpr char kIioServiceClientCmdPath[] = "/usr/sbin/iioservice_simpleclient";
constexpr char kIioParameterChannelsPrefix[] = "--channels=";
constexpr char kIioParameterFrequencyPrefix[] = "--frequency=";
constexpr char kIioParameterDeviceIdPrefix[] = "--device_id=";
constexpr char kIioParameterSamplesPrefix[] = "--samples=";
constexpr char kIioParameterTimeoutPrefix[] = "--timeout=";

}  // namespace

namespace rmad {

IioEcSensorUtilsImpl::IioEcSensorUtilsImpl(const std::string& location,
                                           const std::string& name)
    : IioEcSensorUtils(location, name), initialized_(false) {
  Initialize();
}

bool IioEcSensorUtilsImpl::GetAvgData(const std::vector<std::string>& channels,
                                      int samples,
                                      std::vector<double>* avg_data,
                                      std::vector<double>* variance) {
  CHECK_GT(channels.size(), 0);
  CHECK_GT(samples, 0);
  CHECK(avg_data);

  if (samples == 1 && variance) {
    LOG(ERROR) << "We can only estimate variance when |samples| > 1.";
    return false;
  }

  if (!initialized_) {
    LOG(ERROR) << location_ << ":" << name_ << " is not initialized.";
    return false;
  }

  std::string parameter_channels = kIioParameterChannelsPrefix;
  for (auto channel : channels) {
    parameter_channels += channel + " ";
  }

  std::vector<std::string> argv{
      kIioServiceClientCmdPath,
      parameter_channels,
      kIioParameterFrequencyPrefix + base::NumberToString(frequency_),
      kIioParameterDeviceIdPrefix + base::NumberToString(id_),
      kIioParameterSamplesPrefix +
          base::NumberToString(samples + kNumberFirstReadsDiscarded),
      kIioParameterTimeoutPrefix +
          base::NumberToString(ceil(kSecond2Millisecond / frequency_) +
                               kTimeoutOverheadInMS),
  };

  std::string value;
  if (!base::GetAppOutputAndError(argv, &value)) {
    std::string whole_cmd;
    for (auto arg : argv) {
      whole_cmd += " " + arg;
    }
    LOG(ERROR) << location_ << ":" << name_ << ": Failed to get data by"
               << whole_cmd;
    return false;
  }

  std::vector<std::vector<double>> data(channels.size());
  for (int i = 0; i < channels.size(); i++) {
    re2::StringPiece str_piece(value);
    re2::RE2 reg(channels[i] + R"(: ([-+]?\d+))");
    std::string match;
    double raw_data;

    while (RE2::FindAndConsume(&str_piece, reg, &match)) {
      if (!base::StringToDouble(match, &raw_data)) {
        LOG(WARNING) << location_ << ":" << name_ << ": Failed to parse data ["
                     << match << "]";
        continue;
      }

      data.at(i).push_back(raw_data * scale_);
    }

    if (data.at(i).size() != samples + kNumberFirstReadsDiscarded) {
      LOG(ERROR) << location_ << ":" << name_ << ":" << channels[i]
                 << ": We received " << data.at(i).size() << " instead of "
                 << samples << " + " << kNumberFirstReadsDiscarded
                 << " (dropped few invalid reads at the beginning) samples.";
      return false;
    }

    // Remove first few invalid reads as a workaround of crrev/c/1423123.
    // This would be fixed by FW update later.
    // TODO(genechang): Remove this workaround when new firmware is released.
    data.at(i).erase(data.at(i).begin(),
                     data.at(i).begin() + kNumberFirstReadsDiscarded);
    CHECK_EQ(data.at(i).size(), samples);
  }

  avg_data->resize(channels.size());
  for (int i = 0; i < channels.size(); i++) {
    avg_data->at(i) =
        std::accumulate(data.at(i).begin(), data.at(i).end(), 0.0) / samples;
  }

  if (!variance) {
    return true;
  }

  CHECK_GT(samples, 1);
  variance->resize(channels.size(), 0.0);
  for (int i = 0; i < channels.size(); i++) {
    const double avg = avg_data->at(i);
    for (double value : data.at(i)) {
      variance->at(i) += (value - avg) * (value - avg);
    }
    variance->at(i) /= data.at(i).size() - 1;
  }

  return true;
}

bool IioEcSensorUtilsImpl::GetSysValues(const std::vector<std::string>& entries,
                                        std::vector<double>* values) {
  if (!initialized_) {
    LOG(ERROR) << location_ << ":" << name_ << " is not initialized.";
    return false;
  }

  std::vector<double> buffer_values;
  for (int i = 0; i < entries.size(); i++) {
    auto entry = sysfs_path_.Append(entries[i]);
    double val = 0.0;
    if (std::string str_val;
        !base::PathExists(entry) || !base::ReadFileToString(entry, &str_val) ||
        !base::StringToDouble(
            base::TrimWhitespaceASCII(str_val, base::TRIM_ALL), &val)) {
      LOG(ERROR) << "Failed to read sys value at " << entry;
      return false;
    }
    buffer_values.push_back(val);
  }

  *values = buffer_values;
  return true;
}

void IioEcSensorUtilsImpl::Initialize() {
  for (int i = 0; i < kMaxNumEntries; i++) {
    base::FilePath sysfs_path(kIioDevicePathPrefix + base::NumberToString(i));
    if (!base::PathExists(sysfs_path)) {
      break;
    }

    if (InitializeFromSysfsPath(sysfs_path)) {
      id_ = i;
      sysfs_path_ = sysfs_path;
      initialized_ = true;
      break;
    }
  }
}

bool IioEcSensorUtilsImpl::InitializeFromSysfsPath(
    const base::FilePath& sysfs_path) {
  CHECK(base::PathExists(sysfs_path));

  base::FilePath entry_name = sysfs_path.Append(kIioDeviceEntryName);
  if (std::string buf;
      !base::PathExists(entry_name) ||
      !base::ReadFileToString(entry_name, &buf) ||
      name_ != base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING)) {
    return false;
  }

  base::FilePath entry_location = sysfs_path.Append(kIioDeviceEntryLocation);
  if (std::string buf;
      !base::PathExists(entry_location) ||
      !base::ReadFileToString(entry_location, &buf) ||
      location_ != base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING)) {
    return false;
  }

  // For the sensor to work properly, we should set it according to one of its
  // available sampling frequencies. Since all available frequencies should
  // work, we will use the fastest frequency for calibration to save time.
  base::FilePath entry_frequency_available =
      sysfs_path.Append(kIioDeviceEntryFrequencyAvailable);
  std::string frequency_available;
  if (!base::PathExists(entry_frequency_available) ||
      !base::ReadFileToString(entry_frequency_available,
                              &frequency_available)) {
    return false;
  }
  // The value from sysfs could be one of:
  // 1. A set of small discrete values, such as "0 2 4 6 8".
  // 2. A range "[min min_step max]", where steps are not linear but power of 2.
  frequency_ = 0;
  re2::StringPiece str_piece(frequency_available);
  // Currently, we only used the highest frequency.
  re2::RE2 reg(R"((\d+(\.\d+)?)\s*$)");
  std::string match;
  if (double freq; RE2::FindAndConsume(&str_piece, reg, &match) &&
                   base::StringToDouble(match, &freq) && freq > frequency_) {
    frequency_ = freq;
  } else {
    return false;
  }

  base::FilePath entry_scale = sysfs_path.Append(kIioDeviceEntryScale);
  if (std::string buf;
      !base::PathExists(entry_scale) ||
      !base::ReadFileToString(entry_scale, &buf) ||
      !base::StringToDouble(base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING),
                            &scale_)) {
    return false;
  }

  return true;
}

}  // namespace rmad
