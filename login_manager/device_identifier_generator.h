// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_DEVICE_IDENTIFIER_GENERATOR_H_
#define LOGIN_MANAGER_DEVICE_IDENTIFIER_GENERATOR_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>

namespace login_manager {

class ChildOutputCollector;
class LoginMetrics;
class SystemUtils;

// Generates time-based opaque device identifiers used as keys in server-side
// device state storage, and generates derived device active secret for Private
// Computing membership checking in a privacy compliant manner. This allows
// persisting device-specific data on the server that survives device recovery
// and can be restored afterwards.
class DeviceIdentifierGenerator {
 public:
  // Callback type for state key generation requests.
  typedef base::Callback<void(const std::vector<std::vector<uint8_t>>&)>
      StateKeyCallback;

  // Callback type for PSM derived device active secret generation request.
  typedef base::Callback<void(const std::string&)>
      PsmDeviceActiveSecretCallback;

  // The power of two determining the size of the time quanta for device state
  // keys, i.e. the quanta will be of size 2^kDeviceStateKeyTimeQuantumPower
  // seconds. 2^23 seconds corresponds to 97.09 days.
  static const int kDeviceStateKeyTimeQuantumPower = 23;

  // The number of future time quanta to generate device state identifiers for.
  // This determines the interval after which a device will no longer receive
  // server-backed state information and thus corresponds to the delay until a
  // device becomes anonymous to the server again.
  //
  // The goal here is to guarantee state key validity for 1 year into the
  // future. 4 quanta are needed to cover a year, but the current quantum is
  // clipped short in the general case. Hence, one buffer quantum is needed to
  // make up for the clipping, yielding a total of 5 quanta.
  static const int kDeviceStateKeyFutureQuanta = 5;

  DeviceIdentifierGenerator(SystemUtils* system_utils, LoginMetrics* metrics);
  DeviceIdentifierGenerator(const DeviceIdentifierGenerator&) = delete;
  DeviceIdentifierGenerator& operator=(const DeviceIdentifierGenerator&) =
      delete;

  virtual ~DeviceIdentifierGenerator();

  // Parses a machine information string containing newline-separated key=value
  // pairs. Removes quoting from names and values. Returns true if parsing is
  // successful.
  static bool ParseMachineInfo(const std::string& data,
                               std::map<std::string, std::string>* params);

  // Sets a machine information parameters. Returns false if a parameter is
  // missing.
  bool InitMachineInfo(const std::map<std::string, std::string>& params);

  // Requests the currently-valid state keys to be computed based on system
  // time. Identifiers are generated by running a number of stable hardware
  // identifiers along with a quantized time stamp through a cryptographic hash
  // function. The resulting identifiers shouldn't be predictable by servers, so
  // it's important to include hardware identifiers that are only known to the
  // device.
  //
  // Quantized time is included in order to avoid sharing stable device
  // identifiers with the server. This improves privacy, since the server will
  // only be able to track the device within the validity time of the
  // identifiers it has collected. Once the device stops sharing identifiers, it
  // will eventually regain anonymity.
  //
  // It is the responsibility of the caller to ensure that the system time
  // is accurate before starting the request! The state keys (sorted by time
  // quantum in ascending order) are returned via |callback|, potentially after
  // waiting for machine info to become available. If the state keys can't be
  // computed due to missing machine identifiers, |callback| will be invoked
  // with an empty vector.
  virtual void RequestStateKeys(const StateKeyCallback& callback);

  // Request the derived device secret. Using hmac to hash the device
  // stable secret and the derived device secret is returned via |callback|.
  virtual void RequestPsmDeviceActiveSecret(
      const PsmDeviceActiveSecretCallback& callback);

 private:
  // Computes the keys and stores them in |state_keys|. In case of error,
  // |state_keys| will be cleared.
  void ComputeKeys(std::vector<std::vector<uint8_t>>* state_keys);

  // Derive the device active secret by hmac stable_device_secret_DO_NOT_SHARE.
  void DerivePsmDeviceActiveSecret(std::string* derived_secret);

  SystemUtils* system_utils_;
  LoginMetrics* metrics_;

  // Pending state key generation callbacks.
  std::vector<StateKeyCallback> pending_callbacks_;

  // Pending PSM device secret generation callbacks.
  std::vector<PsmDeviceActiveSecretCallback>
      pending_psm_device_secret_callbacks_;

  // The data required to generate state keys.
  bool machine_info_available_ = false;
  std::string stable_device_secret_;

  // TODO(mnissler): Remove these and the corresponding code once the old way
  // of generating state keys is no longer used in the wild, i.e. all devices
  // can be assumed to have a stable device secret.
  std::string machine_serial_number_;
  std::string disk_serial_number_;
  std::string group_code_key_;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_DEVICE_IDENTIFIER_GENERATOR_H_
