// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TETHERING_MANAGER_H_
#define SHILL_TETHERING_MANAGER_H_

#include <memory>
#include <string>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/patchpanel/dbus/client.h>

#include "shill/network/network.h"
#include "shill/refptr_types.h"
#include "shill/store/property_store.h"
#include "shill/technology.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/local_service.h"
#include "shill/wifi/wifi_rf.h"
#include "shill/wifi/wifi_security.h"

namespace shill {

class Manager;
class StoreInterface;

// TetheringManager handles tethering related logics. It is created by the
// Manager class.
//
// It reuses the Profile class to persist the tethering parameters for each
// user. Without user's input, it uses the default tethering configuration with
// a random SSID and a random passphrase. It saves the current tethering
// configuration to user profile when the user sets tethering config, or user
// enables tethering.
//
// It interacts with HotspotDevice,
// CellularServiceProvider and EthernetProvider classes to prepare upstream and
// downstream technologies. It interacts with patchpanel via dbus to set up the
// tethering network.
class TetheringManager : public Network::EventHandler {
 public:
  enum class EntitlementStatus {
    kReady,
    kNotAllowed,
    kUpstreamNetworkNotAvailable,
  };

  static const char* EntitlementStatusName(EntitlementStatus status);

  enum class SetEnabledResult {
    // Successfully start/stop tethering session
    kSuccess,
    // Tethering is not allowed
    kNotAllowed,
    // Tethering config has invalid property
    kInvalidProperties,
    // Start/stop tethering when it is in a wrong state
    kWrongState,
    // Upstream is not connected or does not have Internet access
    kUpstreamNetworkNotAvailable,
    // Upstream network operation failure
    kUpstreamFailure,
    // Downstream WiFi operation failure
    kDownstreamWiFiFailure,
    // Failed to setup/tear down network layer for tethering
    kNetworkSetupFailure,
    // Other unknown failures
    kFailure,
  };

  static const std::string SetEnabledResultName(SetEnabledResult result);

  enum class TetheringState {
    kTetheringIdle,
    kTetheringStarting,
    kTetheringActive,
    kTetheringStopping,
  };

  // Storage group for tethering configs.
  static constexpr char kStorageId[] = "tethering";

  explicit TetheringManager(Manager* manager);
  TetheringManager(const TetheringManager&) = delete;
  TetheringManager& operator=(const TetheringManager&) = delete;

  virtual ~TetheringManager();

  // Initialize DBus properties related to tethering.
  void InitPropertyStore(PropertyStore* store);
  // Start and initialize TetheringManager.
  void Start();
  // Stop TetheringManager.
  void Stop();
  // Enable or disable a tethering session with existing tethering config.
  void SetEnabled(bool enabled,
                  base::OnceCallback<void(SetEnabledResult result)> callback);
  // Check if upstream network is ready for tethering.
  void CheckReadiness(
      base::OnceCallback<void(EntitlementStatus result)> callback);
  // Load the tethering config available in |profile| if there was any tethering
  // config saved for this |profile|.
  virtual void LoadConfigFromProfile(const ProfileRefPtr& profile);
  // Unload the tethering config related to |profile| and reset the tethering
  // config with default values.
  virtual void UnloadConfigFromProfile();
  static const char* TetheringStateName(const TetheringState& state);
  // Get the current TetheringStatus dictionary.
  KeyValueStore GetStatus();

  // DBus property getters
  // This property is temporary and will be removed when the feature is mature.
  bool allowed() { return allowed_; }

 private:
  friend class TetheringManagerTest;
  FRIEND_TEST(TetheringManagerTest, FromProperties);
  FRIEND_TEST(TetheringManagerTest, GetCapabilities);
  FRIEND_TEST(TetheringManagerTest, GetConfig);
  FRIEND_TEST(TetheringManagerTest, GetTetheringCapabilities);
  FRIEND_TEST(TetheringManagerTest, SaveConfig);
  FRIEND_TEST(TetheringManagerTest, SetEnabled);

  enum class StopReason {
    kInitial,             // Initial idle state.
    kClientStop,          // Client explicitly stops tethering.
    kUserExit,            // User logs out or shuts down device.
    kSuspend,             // Device suspend.
    kUpstreamDisconnect,  // Upstream network disconnects.
    kInactive,            // Inactive timer fires.
    kError,               // Internal error.
  };

  using SetEnabledResultCallback =
      base::OnceCallback<void(SetEnabledResult result)>;

  void HelpRegisterDerivedBool(PropertyStore* store,
                               const std::string& name,
                               bool (TetheringManager::*get)(Error* error),
                               bool (TetheringManager::*set)(const bool&,
                                                             Error*));

  // DBUS accessors
  bool SetAllowed(const bool& value, Error* error);
  bool GetAllowed(Error* /*error*/) { return allowed_; }

  // Tethering properties get handlers.
  KeyValueStore GetCapabilities(Error* error);
  KeyValueStore GetConfig(Error* error);
  KeyValueStore GetStatus(Error* error) { return GetStatus(); }

  // Overrides for Network::EventHandler. See the comments for
  // Network::EventHandler for more details. TetheringManager only cares about
  // NetworkValidationResult NetworkDestroyed and Networkstopped event.
  void OnNetworkValidationResult(int interface_index,
                                 const PortalDetector::Result& result) override;
  void OnNetworkStopped(int interface_index, bool is_failure) override;
  void OnNetworkDestroyed(int interface_index) override;
  // TetheringManager does nothing for the below network events.
  void OnConnectionUpdated(int interface_index) override;
  void OnIPConfigsPropertyUpdated(int interface_index) override;
  void OnGetDHCPLease(int interface_index) override;
  void OnGetDHCPFailure(int interface_index) override;
  void OnGetSLAACAddress(int interface_index) override;
  void OnNetworkValidationStart(int interface_index) override;
  void OnNetworkValidationStop(int interface_index) override;
  void OnIPv4ConfiguredWithDHCPLease(int interface_index) override;
  void OnIPv6ConfiguredWithSLAACAddress(int interface_index) override;
  void OnNeighborReachabilityEvent(int interface_index,
                                   const IPAddress& ip_address,
                                   patchpanel::Client::NeighborRole,
                                   patchpanel::Client::NeighborStatus) override;

  bool SetAndPersistConfig(const KeyValueStore& config, Error* error);
  // Populate the shill D-Bus parameter map |properties| with the
  // parameters contained in |this| and return true if successful.
  bool ToProperties(KeyValueStore* properties) const;
  // Populate tethering config from a dictionary.
  bool FromProperties(const KeyValueStore& properties);
  // Reset tethering config with default value and a random WiFi SSID and
  // a random passphrase.
  void ResetConfiguration();
  // Save the current tethering config to user's profile.
  bool Save(StoreInterface* storage);
  // Load the current tethering config from user's profile.
  bool Load(const StoreInterface* storage);
  // Set tethering state and emit dbus property changed signal.
  void SetState(TetheringState state);
  // Peer assoc event handler.
  void OnPeerAssoc();
  // Peer disassoc event handler.
  void OnPeerDisassoc();
  // Downstream device event handler.
  void OnDownstreamDeviceEvent(LocalDevice::DeviceEvent event,
                               const LocalDevice* device);
  // Upstream network fetch callback handler.
  void OnUpstreamNetworkAcquired(SetEnabledResult result, Network* network);
  // Upstream network release callback handler.
  void OnUpstreamNetworkReleased(bool is_success);
  // Trigger callback function asynchronously to post SetTetheringEnabled dbus
  // result.
  void PostSetEnabledResult(SetEnabledResult result);
  // Check if all the tethering resources are ready. If so post the
  // SetTetheringEnabled(true) dbus result.
  void CheckAndPostTetheringStartResult();
  // Check if all the tethering resources are freed. If so post the
  // SetTetheringEnabled(false) dbus result.
  void CheckAndPostTetheringStopResult();
  // Handler function to be called when starting tethering session times out.
  void OnStartingTetheringTimeout();
  // Prepare tethering resources to start a tethering session.
  void StartTetheringSession();
  // Stop and free tethering resources due to reason |reason|.
  void StopTetheringSession(StopReason reason);
  // Kick off the tethering inactive timer when auto_disable_ is true and
  // TetheringState is kTetheringActive. Will not rearm the timer if it is
  // already running. It will tear down tethering session after timer fires.
  void StartInactiveTimer();
  // Cancel the tethering inactive timer due to station associates or
  // auto_disable_ is changed to false.
  void StopInactiveTimer();
  // Get the number of active clients.
  size_t GetClientCount();
  // Convert stop reason enum to string.
  static const char* StopReasonToString(StopReason reason);

  // TODO(b/267804414): Remove it after fishfood.
  // Asynchronous function triggered when the Allowed property changes.
  void TetheringAllowedUpdated(bool allowed);

  // TetheringManager is created and owned by Manager.
  Manager* manager_;
  // Tethering feature flag.
  bool allowed_;
  // Tethering state as listed in enum TetheringState.
  TetheringState state_;
  // Executes when the tethering start timer expires. Calls
  // OnStartingTetheringTimeout.
  base::CancelableOnceClosure start_timer_callback_;
  // Executes when the inactive timer expires. Calls StopTetheringSession.
  base::CancelableOnceClosure inactive_timer_callback_;

  // Automatically disable tethering if no devices have been associated for
  // |kAutoDisableMinute| minutes.
  bool auto_disable_;
  // MAC address randomization. When it is true, AP will use a randomized MAC
  // each time it is started. If false, it will use the persisted MAC address.
  bool mar_;
  // The hex-encoded tethering SSID name to be used in WiFi downstream.
  std::string hex_ssid_;
  // The passphrase to be used in WiFi downstream.
  std::string passphrase_;
  // The security mode to be used in WiFi downstream.
  WiFiSecurity security_;
  // The preferred band to be used in WiFi downstream.
  WiFiBand band_;
  // Preferred upstream technology to use.
  Technology upstream_technology_;

  // Pointer to upstream network. This pointer is valid when state_ is not
  // kTetheringIdle.
  Network* upstream_network_;
  // Member to hold the result callback function. This callback function gets
  // set when dbus method SetTetheringEnabled is called and runs when the async
  // method call is done.
  SetEnabledResultCallback result_callback_;
  // Downlink hotspot device.
  HotspotDeviceRefPtr hotspot_dev_;
  // If downstream hotspot device event kServiceUp has been received or not.
  bool hotspot_service_up_;
  // The reason why tethering is stopped.
  StopReason stop_reason_;

  base::WeakPtrFactory<TetheringManager> weak_ptr_factory_{this};
};

inline std::ostream& operator<<(std::ostream& stream,
                                TetheringManager::TetheringState state) {
  return stream << TetheringManager::TetheringStateName(state);
}

}  // namespace shill

#endif  // SHILL_TETHERING_MANAGER_H_
