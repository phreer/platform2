// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/daemon_samples_observer.h"

#include <utility>

#include <base/bind.h>

#include "iioservice/iioservice_simpleclient/samples_observer.h"

namespace iioservice {

DaemonSamplesObserver::DaemonSamplesObserver(
    int device_id,
    cros::mojom::DeviceType device_type,
    std::vector<std::string> channel_ids,
    double frequency,
    int timeout,
    int samples,
    int mojo_broker_disconnect_tolerance)
    : Daemon(mojo_broker_disconnect_tolerance),
      device_id_(device_id),
      device_type_(device_type),
      channel_ids_(std::move(channel_ids)),
      frequency_(frequency),
      timeout_(timeout),
      samples_(samples),
      weak_ptr_factory_(this) {}

DaemonSamplesObserver::~DaemonSamplesObserver() = default;

void DaemonSamplesObserver::SetSensorClient() {
  sensor_client_ = SamplesObserver::Create(
      base::ThreadTaskRunnerHandle::Get(), device_id_, device_type_,
      std::move(channel_ids_), frequency_, timeout_, samples_,
      base::BindRepeating(&DaemonSamplesObserver::OnMojoDisconnect,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&DaemonSamplesObserver::Quit,
                     weak_ptr_factory_.GetWeakPtr()));
}

}  // namespace iioservice
