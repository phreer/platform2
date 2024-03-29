/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_REQUEST_HANDLER_H_
#define CAMERA_HAL_FAKE_REQUEST_HANDLER_H_

#include <memory>
#include <vector>

#include <absl/status/status.h>
#include <base/containers/flat_map.h>
#include <base/task/sequenced_task_runner.h>
#include <camera/camera_metadata.h>
#include <hardware/camera3.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/jpeg_compressor.h"
#include "hal/fake/capture_request.h"
#include "hal/fake/fake_stream.h"

namespace cros {
// RequestHandler handles all capture request on a dedicated thread, and all
// the methods run on the same thread.
class RequestHandler {
 public:
  RequestHandler(const int id,
                 const camera3_callback_ops_t* callback_ops,
                 const android::CameraMetadata& static_metadata,
                 const scoped_refptr<base::SequencedTaskRunner>& task_runner);
  ~RequestHandler();

  // Handle one request.
  void HandleRequest(std::unique_ptr<CaptureRequest> request);

  // Start streaming and calls callback with resulting status.
  void StreamOn(const std::vector<camera3_stream_t*>& streams,
                base::OnceCallback<void(absl::Status)> callback);

  // Stop streaming and calls callback with resulting status.
  void StreamOff(base::OnceCallback<void(absl::Status)> callback);

 private:
  // Start streaming implementation.
  absl::Status StreamOnImpl(const std::vector<camera3_stream_t*>& streams);

  // Stop streaming implementation.
  absl::Status StreamOffImpl();

  // Handle aborted request.
  void HandleAbortedRequest(CaptureRequest& request);

  // Notify shutter event.
  void NotifyShutter(uint32_t frame_number);

  // Notify request error event.
  void NotifyRequestError(uint32_t frame_number);

  // Fill one result buffer.
  bool FillResultBuffer(camera3_stream_buffer_t& buffer);

  // id of the camera device.
  const int id_;

  // Methods used to call back into the framework.
  const camera3_callback_ops_t* callback_ops_;

  // Task runner for request thread.
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // Map from stream config to fake stream.
  base::flat_map<camera3_stream_t*, std::unique_ptr<FakeStream>> fake_streams_;

  // Camera static characteristics.
  const android::CameraMetadata static_metadata_;
};
}  // namespace cros

#endif  // CAMERA_HAL_FAKE_REQUEST_HANDLER_H_
