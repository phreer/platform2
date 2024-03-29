// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <math.h>
#include <set>
#include <vector>

#include "shill/tethering_manager.h"

#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/error.h"
#include "shill/manager.h"
#include "shill/store/property_accessor.h"
#include "shill/technology.h"

namespace shill {

namespace {
static constexpr char kSSIDPrefix[] = "chromeOS-";
// Random suffix should provide enough uniqueness to have low SSID collision
// possibility, while have enough anonymity among chromeOS population to make
// the device untrackable. Use 4 digit numbers as random SSID suffix.
static constexpr size_t kSSIDSuffixLength = 4;
static constexpr size_t kMinWiFiPassphraseLength = 8;
static constexpr size_t kMaxWiFiPassphraseLength = 63;
// Auto disable tethering if no clients for |kAutoDisableMinute| minutes.
// static constexpr uint8_t kAutoDisableMinute = 10;

static const std::unordered_map<std::string, TetheringManager::WiFiBand>
    bands_map = {
        {kBand2GHz, TetheringManager::WiFiBand::kLowBand},
        {kBand5GHz, TetheringManager::WiFiBand::kHighBand},
        {kBand6GHz, TetheringManager::WiFiBand::kUltraHighBand},
        {kBandAll, TetheringManager::WiFiBand::kAllBands},
};
std::string BandToString(const TetheringManager::WiFiBand band) {
  auto it = std::find_if(std::begin(bands_map), std::end(bands_map),
                         [&band](auto& p) { return p.second == band; });
  return it == bands_map.end() ? std::string() : it->first;
}
TetheringManager::WiFiBand StringToBand(const std::string& band) {
  return base::Contains(bands_map, band)
             ? bands_map.at(band)
             : TetheringManager::WiFiBand::kInvalidBand;
}
std::ostream& operator<<(std::ostream& stream,
                         TetheringManager::WiFiBand band) {
  return stream << BandToString(band);
}

}  // namespace

TetheringManager::TetheringManager(Manager* manager)
    : manager_(manager),
      allowed_(false),
      state_(TetheringState::kTetheringIdle),
      auto_disable_(true),
      mar_(true),
      band_(WiFiBand::kAllBands),
      upstream_technology_(Technology::kUnknown) {
  GenerateRandomWiFiProfile();
  security_ = WiFiSecurity(WiFiSecurity::kWpa2Wpa3);
}

TetheringManager::~TetheringManager() = default;

void TetheringManager::GenerateRandomWiFiProfile() {
  uint64_t rand = base::RandInt(pow(10, kSSIDSuffixLength), INT_MAX);
  std::string suffix = std::to_string(rand);
  std::string ssid =
      kSSIDPrefix + suffix.substr(suffix.size() - kSSIDSuffixLength);
  hex_ssid_ = base::HexEncode(ssid.data(), ssid.size());

  std::string passphrase =
      base::RandBytesAsString(kMinWiFiPassphraseLength >> 1);
  passphrase_ = base::HexEncode(passphrase.data(), passphrase.size());
}

void TetheringManager::InitPropertyStore(PropertyStore* store) {
  store->RegisterBool(kTetheringAllowedProperty, &allowed_);
  store->RegisterDerivedKeyValueStore(
      kTetheringConfigProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetConfig, &TetheringManager::SetConfig)));
  store->RegisterDerivedKeyValueStore(
      kTetheringCapabilitiesProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetCapabilities, nullptr)));
  store->RegisterDerivedKeyValueStore(
      kTetheringStatusProperty,
      KeyValueStoreAccessor(new CustomAccessor<TetheringManager, KeyValueStore>(
          this, &TetheringManager::GetStatus, nullptr)));
}

bool TetheringManager::ToProperties(KeyValueStore* properties) const {
  DCHECK(properties);

  if (hex_ssid_.empty() || passphrase_.empty()) {
    LOG(ERROR) << "Missing SSID or passphrase";
    properties->Clear();
    return false;
  }

  properties->Set<bool>(kTetheringConfAutoDisableProperty, auto_disable_);
  properties->Set<bool>(kTetheringConfMARProperty, mar_);
  properties->Set<std::string>(kTetheringConfSSIDProperty, hex_ssid_);
  properties->Set<std::string>(kTetheringConfPassphraseProperty, passphrase_);
  properties->Set<std::string>(kTetheringConfSecurityProperty,
                               security_.ToString());
  if (band_ != WiFiBand::kAllBands && band_ != WiFiBand::kInvalidBand) {
    properties->Set<std::string>(kTetheringConfBandProperty,
                                 BandToString(band_));
  }
  if (upstream_technology_ != Technology::kUnknown) {
    properties->Set<std::string>(kTetheringConfUpstreamTechProperty,
                                 TechnologyName(upstream_technology_));
  }

  return true;
}

bool TetheringManager::FromProperties(const KeyValueStore& properties) {
  if (properties.Contains<bool>(kTetheringConfAutoDisableProperty))
    auto_disable_ = properties.Get<bool>(kTetheringConfAutoDisableProperty);

  if (properties.Contains<bool>(kTetheringConfMARProperty))
    mar_ = properties.Get<bool>(kTetheringConfMARProperty);

  if (properties.Contains<std::string>(kTetheringConfSSIDProperty)) {
    auto ssid = properties.Get<std::string>(kTetheringConfSSIDProperty);
    if (ssid.empty() || !std::all_of(ssid.begin(), ssid.end(), ::isxdigit)) {
      LOG(ERROR) << "Invalid SSID provided in tethering config: " << ssid;
      return false;
    }
    hex_ssid_ = ssid;
  }

  if (properties.Contains<std::string>(kTetheringConfPassphraseProperty)) {
    auto passphrase =
        properties.Get<std::string>(kTetheringConfPassphraseProperty);
    if (passphrase.length() < kMinWiFiPassphraseLength ||
        passphrase.length() > kMaxWiFiPassphraseLength) {
      LOG(ERROR)
          << "Passphrase provided in tethering config has invalid length: "
          << passphrase;
      return false;
    }
    passphrase_ = passphrase;
  }

  if (properties.Contains<std::string>(kTetheringConfSecurityProperty)) {
    auto sec = WiFiSecurity(
        properties.Get<std::string>(kTetheringConfSecurityProperty));
    if (!sec.IsValid() || !(sec == WiFiSecurity(WiFiSecurity::kWpa2) ||
                            sec == WiFiSecurity(WiFiSecurity::kWpa3) ||
                            sec == WiFiSecurity(WiFiSecurity::kWpa2Wpa3))) {
      LOG(ERROR) << "Invalid security mode provided in tethering config: "
                 << sec;
      return false;
    }
    security_ = sec;
  }

  if (properties.Contains<std::string>(kTetheringConfBandProperty)) {
    auto band =
        StringToBand(properties.Get<std::string>(kTetheringConfBandProperty));
    if (band == WiFiBand::kInvalidBand) {
      LOG(ERROR) << "Invalid WiFi band: " << band;
      return false;
    }
    band_ = band;
  }

  if (properties.Contains<std::string>(kTetheringConfUpstreamTechProperty)) {
    auto tech = TechnologyFromName(
        properties.Get<std::string>(kTetheringConfUpstreamTechProperty));
    if (tech != Technology::kEthernet && tech != Technology::kCellular) {
      LOG(ERROR) << "Invalid upstream technology provided in tethering config: "
                 << tech;
      return false;
    }
    upstream_technology_ = tech;
  }

  return true;
}

KeyValueStore TetheringManager::GetConfig(Error* error) {
  KeyValueStore config;
  if (!ToProperties(&config)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to get TetheringConfig");
  }
  return config;
}

bool TetheringManager::SetConfig(const KeyValueStore& config, Error* error) {
  if (!allowed_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "Tethering is not allowed");
    return false;
  }

  if (!FromProperties(config)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Invalid tethering configuration");
    return false;
  }
  return true;
}

KeyValueStore TetheringManager::GetCapabilities(Error* /* error */) {
  KeyValueStore caps;
  std::vector<std::string> upstream_technologies;
  std::vector<std::string> downstream_technologies;

  if (manager_->GetProviderWithTechnology(Technology::kEthernet))
    upstream_technologies.push_back(TechnologyName(Technology::kEthernet));

  // TODO(b/244334719): add a check with the CellularProvider to see if
  // tethering is enabled for the given SIM card and modem.
  if (manager_->GetProviderWithTechnology(Technology::kCellular))
    upstream_technologies.push_back(TechnologyName(Technology::kCellular));

  if (manager_->GetProviderWithTechnology(Technology::kWiFi)) {
    // TODO(b/244335143): This should be based on static SoC capability
    // information. Need to revisit this when Shill has a SoC capability
    // database.
    const auto wifi_devices = manager_->FilterByTechnology(Technology::kWiFi);
    if (!wifi_devices.empty()) {
      WiFi* wifi_device = static_cast<WiFi*>(wifi_devices.front().get());
      if (wifi_device->SupportAP()) {
        downstream_technologies.push_back(TechnologyName(Technology::kWiFi));
        // Wi-Fi specific tethering capabilities.
        std::vector<std::string> security = {kSecurityWpa2};
        if (wifi_device->SupportsWPA3()) {
          security.push_back(kSecurityWpa3);
          security.push_back(kSecurityWpa2Wpa3);
        }
        caps.Set<Strings>(kTetheringCapSecurityProperty, security);
      }
    }
  }

  caps.Set<Strings>(kTetheringCapUpstreamProperty, upstream_technologies);
  caps.Set<Strings>(kTetheringCapDownstreamProperty, downstream_technologies);

  return caps;
}

KeyValueStore TetheringManager::GetStatus(Error* /* error */) {
  KeyValueStore status;
  status.Set<std::string>(kTetheringStatusStateProperty,
                          TetheringStateToString(state_));
  return status;
}

// static
const char* TetheringManager::TetheringStateToString(
    const TetheringState& state) {
  switch (state) {
    case TetheringState::kTetheringIdle:
      return kTetheringStateIdle;
    case TetheringState::kTetheringStarting:
      return kTetheringStateStarting;
    case TetheringState::kTetheringActive:
      return kTetheringStateActive;
    case TetheringState::kTetheringStopping:
      return kTetheringStateStopping;
    case TetheringState::kTetheringFailure:
      return kTetheringStateFailure;
    default:
      NOTREACHED() << "Unhandled tethering state " << static_cast<int>(state);
      return "Invalid";
  }
}

void TetheringManager::Start() {
}

void TetheringManager::Stop() {}

bool TetheringManager::SetEnabled(bool enabled, Error* error) {
  if (!allowed_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "Tethering is not allowed");
    return false;
  }

  return true;
}

std::string TetheringManager::TetheringManager::CheckReadiness(Error* error) {
  if (!allowed_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "Tethering is not allowed");
    return kTetheringReadinessNotAllowed;
  }

  // TODO(b/235762746): Stub method handler always return "ready". Add more
  // status code later.
  return kTetheringReadinessReady;
}

}  // namespace shill
