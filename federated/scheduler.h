// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_SCHEDULER_H_
#define FEDERATED_SCHEDULER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
// NOLINTNEXTLINE(build/include_alpha) "dbus-proxies.h" needs "dlcservice.pb.h"
#include <dlcservice/dbus-proxies.h>

#include "federated/federated_client.h"

namespace federated {
class StorageManager;
class DeviceStatusMonitor;

class Scheduler {
 public:
  Scheduler(StorageManager* storage_manager,
            std::unique_ptr<DeviceStatusMonitor> device_status_monitor,
            dbus::Bus* bus);
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
  // TODO(alanlxl): create a destructor or finalize method that deletes examples
  //                from the database.
  ~Scheduler() = default;

  // Tries to schedule tasks if the library dlc is already installed, otherwise
  // triggers dlc install and schedules tasks when it receives a DlcStateChanged
  // signal indicating the library dlc is installed.
  void Schedule();

 private:
  // Loads federated library from the given `dlc_root_path`, then for each
  // client, creates a FederatedClient instance and schedules recurring jobs.
  void ScheduleInternal(const std::string& dlc_root_path);

  // Handles DlcStateChanged signals.
  void OnDlcStateChanged(const dlcservice::DlcState& dlc_state);

  // Posts the TryToStartJobForClient task for the given client.
  void KeepSchedulingJobForClient(FederatedClient* const federated_client);

  // Tries to check-in the server and starts a federated task if training
  // conditions are satisfied, updates the FederatedClient object if receiving
  // response from server and posts next try to task_runner_ with the updated
  // client.
  void TryToStartJobForClient(FederatedClient* const federated_client);

  // Registered clients.
  std::vector<FederatedClient> clients_;

  // Not owned
  StorageManager* const storage_manager_;

  // Device status monitor that answers whether training conditions are
  // satisfied.
  std::unique_ptr<DeviceStatusMonitor> device_status_monitor_;

  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
      dlcservice_client_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  const base::WeakPtrFactory<Scheduler> weak_ptr_factory_;
};
}  // namespace federated

#endif  // FEDERATED_SCHEDULER_H_
