// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_METRICS_H_
#define SHILL_MOCK_METRICS_H_

#include <string>

#include "shill/metrics.h"

#include <gmock/gmock.h>

namespace shill {

class MockMetrics : public Metrics {
 public:
  MockMetrics();
  MockMetrics(const MockMetrics&) = delete;
  MockMetrics& operator=(const MockMetrics&) = delete;

  ~MockMetrics() override;

  MOCK_METHOD(void,
              AddServiceStateTransitionTimer,
              (const Service&,
               const std::string&,
               Service::ConnectState,
               Service::ConnectState),
              (override));
  MOCK_METHOD(void, DeregisterDevice, (int), (override));
  MOCK_METHOD(void, NotifyDeviceScanStarted, (int), (override));
  MOCK_METHOD(void, NotifyDeviceScanFinished, (int), (override));
  MOCK_METHOD(void,
              ReportDeviceScanResultToUma,
              (Metrics::WiFiScanResult),
              (override));
  MOCK_METHOD(void, ResetScanTimer, (int), (override));
  MOCK_METHOD(void, NotifyDeviceConnectStarted, (int), (override));
  MOCK_METHOD(void, NotifyDeviceConnectFinished, (int), (override));
  MOCK_METHOD(void, ResetConnectTimer, (int), (override));
  MOCK_METHOD(void,
              NotifyDetailedCellularConnectionResult,
              (Error::Type,
               const std::string&,
               const std::string&,
               const shill::Stringmap&,
               CellularBearer::IPConfigMethod,
               CellularBearer::IPConfigMethod,
               const std::string&,
               const std::string&,
               const std::string&,
               bool use_attach_apn,
               uint32_t tech_used,
               uint32_t iccid_len,
               uint32_t sim_type,
               uint32_t modem_state,
               int interface_index),
              (override));

  MOCK_METHOD(void,
              NotifyServiceStateChanged,
              (const Service&, Service::ConnectState),
              (override));
  MOCK_METHOD(void,
              Notify80211Disconnect,
              (WiFiDisconnectByWhom, IEEE_80211::WiFiReasonCode),
              (override));
  MOCK_METHOD(void,
              NotifyWiFiConnectionAttempt,
              (const Metrics::WiFiConnectionAttemptInfo&, uint64_t),
              (override));
  MOCK_METHOD(void,
              NotifyWiFiConnectionAttemptResult,
              (NetworkServiceError, uint64_t),
              (override));
  MOCK_METHOD(void,
              NotifyWiFiDisconnection,
              (WiFiDisconnectionType, IEEE_80211::WiFiReasonCode, uint64_t),
              (override));
  MOCK_METHOD(void,
              NotifyWiFiAdapterStateChanged,
              (bool, const WiFiAdapterInfo&),
              (override));
  MOCK_METHOD(bool, SendEnumToUMA, (const std::string&, int, int), (override));
  MOCK_METHOD(void,
              SendEnumToUMA,
              (const Metrics::EnumMetric<Metrics::FixedName>& metric, int),
              (override));
  MOCK_METHOD(void,
              SendEnumToUMA,
              (const Metrics::EnumMetric<Metrics::NameByTechnology>& metric,
               Technology,
               int),
              (override));
  MOCK_METHOD(void,
              SendToUMA,
              (const Metrics::HistogramMetric<Metrics::FixedName>& metric, int),
              (override));
  MOCK_METHOD(
      void,
      SendToUMA,
      (const Metrics::HistogramMetric<Metrics::NameByTechnology>& metric,
       Technology,
       int),
      (override));
  MOCK_METHOD(bool, SendBoolToUMA, (const std::string&, bool), (override));
  MOCK_METHOD(bool,
              SendToUMA,
              (const std::string&, int, int, int, int),
              (override));
  MOCK_METHOD(bool, SendSparseToUMA, (const std::string&, int), (override));
  MOCK_METHOD(void,
              NotifyUserInitiatedConnectionFailureReason,
              (const Service::ConnectFailure),
              (override));
  MOCK_METHOD(void,
              NotifyConnectionDiagnosticsIssue,
              (const std::string&),
              (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_METRICS_H_
