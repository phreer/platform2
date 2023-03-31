// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_MISSIVE_MISSIVE_ARGS_H_
#define MISSIVE_MISSIVE_MISSIVE_ARGS_H_

#include <memory>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/strings/string_piece.h>
#include <base/thread_annotations.h>
#include "base/threading/sequence_bound.h"
#include <base/time/time.h>
#include <featured/feature_library.h>

#include "missive/util/statusor.h"

namespace reporting {

// The body of the SequenceBound arguments container.
class MissiveArgs {
 public:
  // Collector feature parameters:
  static constexpr VariationsFeature kCollectorFeature{
      "CrOSLateBootMissiveCollector", FEATURE_ENABLED_BY_DEFAULT};
  static constexpr char kEnqueuingRecordTallierParameter[] =
      "enqueuing_record_tallier";
  static constexpr char kCpuCollectorIntervalParameter[] =
      "cpu_collector_interval";
  static constexpr char kStorageCollectorIntervalParameter[] =
      "storage_collector_interval";
  static constexpr char kMemoryCollectorIntervalParameter[] =
      "memory_collector_interval";

  static constexpr char kEnqueuingRecordTallierDefault[] = "3m";
  static constexpr char kCpuCollectorIntervalDefault[] = "10m";
  static constexpr char kStorageCollectorIntervalDefault[] = "1h";
  static constexpr char kMemoryCollectorIntervalDefault[] = "10m";
  struct CollectionParameters {
    base::TimeDelta enqueuing_record_tallier;
    base::TimeDelta cpu_collector_interval;
    base::TimeDelta storage_collector_interval;
    base::TimeDelta memory_collector_interval;
  };

  // Storage feature parameters:
  static constexpr bool kCompressionEnabledDefault = true;
  static constexpr char kCompressionEnabledParameter[] = "compression_enabled";
  static constexpr bool kEncryptionEnabledDefault = true;
  static constexpr char kEncryptionEnabledParameter[] = "encryption_enabled";
  static constexpr bool kControlledDegradationDefault = false;
  static constexpr char kControlledDegradationParameter[] =
      "controlled_degradation";
  static constexpr VariationsFeature kStorageFeature{
      "CrOSLateBootMissiveStorage", FEATURE_ENABLED_BY_DEFAULT};
  struct StorageParameters {
    bool compression_enabled = kCompressionEnabledDefault;
    bool encryption_enabled = kEncryptionEnabledDefault;
    bool controlled_degradation = kControlledDegradationDefault;
  };

  explicit MissiveArgs(
      std::unique_ptr<feature::PlatformFeaturesInterface> feature_lib);
  MissiveArgs(const MissiveArgs&) = delete;
  MissiveArgs& operator=(const MissiveArgs&) = delete;
  ~MissiveArgs();

  void GetCollectionParameters(
      base::OnceCallback<void(StatusOr<CollectionParameters>)> result_cb);
  void GetStorageParameters(
      base::OnceCallback<void(StatusOr<StorageParameters>)> result_cb);

 private:
  void OnParamResult(feature::PlatformFeaturesInterface::ParamsResult result);

  const std::unique_ptr<feature::PlatformFeaturesInterface> feature_lib_;

  const std::vector<const VariationsFeature*> features_to_load_;

  SEQUENCE_CHECKER(sequence_checker_);
  bool responded_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  CollectionParameters collection_parameters_
      GUARDED_BY_CONTEXT(sequence_checker_);
  StorageParameters storage_parameters_ GUARDED_BY_CONTEXT(sequence_checker_);

  // `Get...Parameters` calls made before `OnParamResult` will be delayed and
  // responded after it.
  std::vector<base::OnceClosure> delayed_response_cbs_
      GUARDED_BY_CONTEXT(sequence_checker_);

  base::WeakPtrFactory<MissiveArgs> weak_ptr_factory_{this};
};

// SequenceBound arguments container.
using SequencedMissiveArgs = base::SequenceBound<MissiveArgs>;

}  // namespace reporting

#endif  // MISSIVE_MISSIVE_MISSIVE_ARGS_H_
