// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AUDIO_AUDIO_SET_GAIN_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AUDIO_AUDIO_SET_GAIN_H_

#include <string>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/system/context.h"

namespace diagnostics {

class AudioSetGainRoutine final : public DiagnosticRoutine {
 public:
  explicit AudioSetGainRoutine(Context* context,
                               uint64_t node_id,
                               uint8_t gain,
                               bool mute_on);
  AudioSetGainRoutine(const AudioSetGainRoutine&) = delete;
  AudioSetGainRoutine& operator=(const AudioSetGainRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~AudioSetGainRoutine() override;
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(ash::cros_healthd::mojom::RoutineUpdate* response,
                            bool include_output) override;
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus() override;

 private:
  // Tartget node id.
  uint64_t node_id_;

  // Target gain value.
  uint8_t gain_;

  // Mute the device or not.
  bool mute_on_;

  // Context object used to communicate with the executor.
  Context* context_;

  // Status of the routine, reported by GetStatus() or noninteractive routine
  // updates.
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;

  // Details of the routine's status, reported in non-interactive status
  // updates.
  std::string status_message_ = "";
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AUDIO_AUDIO_SET_GAIN_H_