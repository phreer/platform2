/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_STREAM_MANIPULATOR_H_
#define CAMERA_COMMON_STREAM_MANIPULATOR_H_

#include <hardware/camera3.h>

#include <memory>
#include <string>
#include <vector>

#include "camera/mojo/cros_camera_service.mojom.h"
#include "common/camera_hal3_helpers.h"
#include "cros-camera/camera_mojo_channel_manager_token.h"
#include "cros-camera/export.h"
#include "gpu/gpu_resources.h"

namespace cros {

class CameraDeviceAdapter;

// Interface class that can be used by feature implementations to add hooks into
// the standard camera HAL3 capture pipeline.
class CROS_CAMERA_EXPORT StreamManipulator {
 public:
  struct Options {
    // Used to identify the camera device that the stream manipulators will be
    // created for (e.g. USB v.s. vendor camera HAL).
    std::string camera_module_name;

    // Whether we should attempt to enable ZSL. We might have vendor-specific
    // ZSL solution, and in which case we should not try to enable our ZSL.
    bool enable_cros_zsl;
  };

  struct RuntimeOptions {
    // The state of auto framing. Can be either off, single person mode or
    // multi people mode.
    mojom::CameraAutoFramingState auto_framing_state;

    // The state of camera software privacy switch state. When a user session
    // starts, it will be OFF until it is set by the Mojo API
    // SetCameraSWPrivacySwitchState.
    mojom::CameraPrivacySwitchState sw_privacy_switch_state =
        mojom::CameraPrivacySwitchState::OFF;
  };

  // Callback for the StreamManipulator to return capture results to the client
  // asynchronously.
  using CaptureResultCallback =
      base::RepeatingCallback<void(Camera3CaptureDescriptor result)>;

  // Gets the set of enabled StreamManipulator instances. The StreamManipulators
  // are enabled through platform or device specific settings. This factory
  // method is called by CameraDeviceAdapter.
  //
  // The hooks of the StreamManipulators are called by CameraDeviceAdapter in
  // the various HAL3 APIs. See the comments below for details regarding where
  // each hook is called and its expected behavior. For
  // ProcessCaptureRequest / ProcessCaptureResult and
  // ConfigureStreams / OnConfiguredStreams pairs, CameraDeviceAdapter will
  // iterate through the list of StreamManipulators with reverse order.
  //
  // CameraDeviceAdapter will iterate through all the StreamManipulators
  // regardless of the return value of each hook call. The return value of the
  // hook is mainly used to log the status for each StreamManipulator.
  static std::vector<std::unique_ptr<StreamManipulator>>
  GetEnabledStreamManipulators(
      Options options,
      RuntimeOptions* runtime_options,
      GpuResources* gpu_resources,
      CameraMojoChannelManagerToken* mojo_manager_token);

  virtual ~StreamManipulator() = default;

  // The followings are hooks to the camera3_device_ops APIs and will be called
  // by CameraDeviceAdapter on the CameraDeviceOpsThread.

  // A hook to the camera3_device_ops::initialize(). Will be called by
  // CameraDeviceAdapter with the camera device static metadata |static_info|.
  virtual bool Initialize(const camera_metadata_t* static_info,
                          CaptureResultCallback result_callback) = 0;

  // A hook to the upper part of camera3_device_ops::configure_streams(). Will
  // be called by CameraDeviceAdapter with the stream configuration
  // |stream_config| requested by the camera client.
  virtual bool ConfigureStreams(Camera3StreamConfiguration* stream_config) = 0;

  // A hook to the lower part of camera3_device_ops::configure_streams().
  // Will be called by CameraDeviceAdapter with the updated stream configuration
  // |stream_config| returned by the camera HAL implementation.
  virtual bool OnConfiguredStreams(
      Camera3StreamConfiguration* stream_config) = 0;

  // A hook to the camera3_device_ops::construct_default_request_settings().
  // Will be called by CameraDeviceAdapter with the default request settings
  // |default_request_settings| prepared by the camera HAL implementation for
  // type |type|.
  virtual bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) = 0;

  // A hook to the camera3_device_ops::process_capture_request(). Will be called
  // by CameraDeviceAdapter for each incoming capture request |request|.
  virtual bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) = 0;

  // A hook to the camera3_device_ops::flush(). Will be called by
  // CameraDeviceAdapter when the camera client requests a flush.
  virtual bool Flush() = 0;

  // The followings are hooks to the camera3_callback_ops APIs and will be
  // called by CameraDeviceAdapter on the CameraCallbackOpsThread.

  // A hook to the camera3_callback_ops::process_capture_result(). Will be
  // called by CameraDeviceAdapter for each capture result |result| produced by
  // the camera HAL implementation.
  virtual bool ProcessCaptureResult(Camera3CaptureDescriptor* result) = 0;

  // A hook to the camera3_callback_ops::notify(). Will be called by
  // CameraDeviceAdapter for each notify message |msg| produced by the camera
  // HAL implemnetation.
  virtual bool Notify(camera3_notify_msg_t* msg) = 0;
};

}  // namespace cros

#endif  // CAMERA_COMMON_STREAM_MANIPULATOR_H_
