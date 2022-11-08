/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_STREAM_MANIPULATOR_H_
#define CAMERA_COMMON_STREAM_MANIPULATOR_H_

#include <hardware/camera3.h>

#include <bitset>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/synchronization/lock.h>
#include <base/thread_annotations.h>

#include "camera/mojo/cros_camera_service.mojom.h"
#include "common/camera_hal3_helpers.h"
#include "common/vendor_tag_manager.h"
#include "cros-camera/camera_mojo_channel_manager_token.h"
#include "cros-camera/export.h"
#include "gpu/gpu_resources.h"
#include "ml_core/effects_pipeline.h"
#include "ml_core/mojo/effects_pipeline.mojom.h"

namespace cros {

// Interface class that can be used by feature implementations to add hooks into
// the standard camera HAL3 capture pipeline. The StreamManipulators are enabled
// through platform or device specific settings.
//
// The hooks of the StreamManipulators are called by StreamManipulatorManager,
// which is owned by CameraDeviceAdapter, in the various HAL3 APIs. See the
// comments below for details regarding where each hook is called and its
// expected behavior. For ProcessCaptureRequest / ProcessCaptureResult and
// ConfigureStreams / OnConfiguredStreams pairs, StreamManipulatorManager will
// iterate through the list of StreamManipulators with reverse order.
//
// StreamManipulatorManager will iterate through all the StreamManipulators
// regardless of the return value of each hook call. The return value of the
// hook is mainly used to log the status for each StreamManipulator.
class CROS_CAMERA_EXPORT StreamManipulator {
 public:
  struct Options {
    // Used to identify the camera device that the stream manipulators will be
    // created for (e.g. USB v.s. vendor camera HAL).
    std::string camera_module_name;
  };

  class RuntimeOptions {
   public:
    void SetAutoFramingState(mojom::CameraAutoFramingState state);
    void SetSWPrivacySwitchState(mojom::CameraPrivacySwitchState state);
    void SetEffectsConfig(mojom::EffectsConfigPtr config);
    bool IsEffectEnabled(mojom::CameraEffect effect);
    EffectsConfig GetEffectsConfig();
    base::FilePath GetDlcRootPath();
    void SetDlcRootPath(const base::FilePath& path);

    mojom::CameraAutoFramingState auto_framing_state();
    mojom::CameraPrivacySwitchState sw_privacy_switch_state();

   private:
    base::Lock lock_;

    // The state of auto framing. Can be either off, single person mode or
    // multi people mode.
    mojom::CameraAutoFramingState auto_framing_state_ GUARDED_BY(lock_) =
        mojom::CameraAutoFramingState::OFF;

    // The state of camera software privacy switch state. When a user session
    // starts, it will be OFF until it is set by the Mojo API
    // SetCameraSWPrivacySwitchState.
    mojom::CameraPrivacySwitchState sw_privacy_switch_state_ GUARDED_BY(lock_) =
        mojom::CameraPrivacySwitchState::OFF;

    // The state of camera effects. Which is enabled/disabled and the
    // configuration parameters to tune it.
    mojom::EffectsConfigPtr effects_config_ GUARDED_BY(lock_) =
        mojom::EffectsConfig::New();

    // Path to DLC. Empty if DLC isn't available / ready.
    base::FilePath dlc_root_path GUARDED_BY(lock_);
  };

  // Callback for the StreamManipulator to return capture results to the client
  // asynchronously.
  using CaptureResultCallback =
      base::RepeatingCallback<void(Camera3CaptureDescriptor result)>;

  // A one-time initialization hook called by CameraHalAdapter for updating the
  // vendor tag information from stream manipulators.
  static bool UpdateVendorTags(VendorTagManager& vendor_tag_manager);

  // A one-time initialization hook called by CameraHalAdapter for updating the
  // static camera metadata from stream manipulators for each (camera_id,
  // client_type) pair.
  static bool UpdateStaticMetadata(android::CameraMetadata* static_info);

  virtual ~StreamManipulator() = default;

  // The followings are hooks to the camera3_device_ops APIs and will be called
  // by StreamManipulatorManager on the CameraDeviceOpsThread.

  // A hook to the camera3_device_ops::initialize(). Will be called by
  // StreamManipulatorManager with the camera device static metadata
  // |static_info|.
  virtual bool Initialize(const camera_metadata_t* static_info,
                          CaptureResultCallback result_callback) = 0;

  // A hook to the upper part of camera3_device_ops::configure_streams(). Will
  // be called by StreamManipulatorManager with the stream configuration
  // |stream_config| requested by the camera client.
  virtual bool ConfigureStreams(Camera3StreamConfiguration* stream_config) = 0;

  // A hook to the lower part of camera3_device_ops::configure_streams(). Will
  // be called by StreamManipulatorManager with the updated stream configuration
  // |stream_config| returned by the camera HAL implementation.
  virtual bool OnConfiguredStreams(
      Camera3StreamConfiguration* stream_config) = 0;

  // A hook to the camera3_device_ops::construct_default_request_settings().
  // Will be called by StreamManipulatorManager with the default request
  // settings |default_request_settings| prepared by the camera HAL
  // implementation for type |type|.
  virtual bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) = 0;

  // A hook to the camera3_device_ops::process_capture_request(). Will be called
  // by StreamManipulatorManager for each incoming capture request |request|.
  virtual bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) = 0;

  // A hook to the camera3_device_ops::flush(). Will be called by
  // StreamManipulatorManager when the camera client requests a flush.
  virtual bool Flush() = 0;

  // The followings are hooks to the camera3_callback_ops APIs and will be
  // called by StreamManipulatorManager on the CameraCallbackOpsThread.

  // A hook to the camera3_callback_ops::process_capture_result(). Will be
  // called by StreamManipulatorManager for each capture result |result|
  // produced by the camera HAL implementation.
  virtual bool ProcessCaptureResult(Camera3CaptureDescriptor* result) = 0;

  // A hook to the camera3_callback_ops::notify(). Will be called by
  // StreamManipulatorManager for each notify message |msg| produced by the
  // camera HAL implemnetation.
  virtual bool Notify(camera3_notify_msg_t* msg) = 0;
};

}  // namespace cros

#endif  // CAMERA_COMMON_STREAM_MANIPULATOR_H_
