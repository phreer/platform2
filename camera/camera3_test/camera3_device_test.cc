// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_device_fixture.h"

#include <algorithm>

#include <sync/sync.h>

namespace camera3_test {

static void ExpectKeyValueGreaterThanI64(const camera_metadata_t* settings,
                                         int32_t key,
                                         const char* key_name,
                                         int64_t value) {
  camera_metadata_ro_entry_t entry;
  ASSERT_EQ(0, find_camera_metadata_ro_entry(settings, key, &entry))
      << "Cannot find the metadata " << key_name;
  ASSERT_GT(entry.data.i64[0], value) << "Wrong value of metadata " << key_name;
}
#define EXPECT_KEY_VALUE_GT_I64(settings, key, value) \
  ExpectKeyValueGreaterThanI64(settings, key, #key, value)

int Camera3Device::Initialize(Camera3Module* cam_module) {
  VLOGF_ENTER();
  if (!cam_module || !hal_thread_.Start()) {
    return -EINVAL;
  }

  // Open camera device
  cam_device_ = cam_module->OpenDevice(cam_id_);
  if (!cam_device_) {
    return -ENODEV;
  }

  EXPECT_GE(((const hw_device_t*)cam_device_)->version,
            (uint16_t)HARDWARE_MODULE_API_VERSION(3, 3))
      << "The device must support at least HALv3.3";

  // Initialize camera device
  int result = -EIO;
  Camera3Device::notify = Camera3Device::NotifyForwarder;
  Camera3Device::process_capture_result =
      Camera3Device::ProcessCaptureResultForwarder;
  hal_thread_.PostTaskSync(
      base::Bind(&Camera3Device::InitializeOnHalThread, base::Unretained(this),
                 static_cast<const camera3_callback_ops_t*>(this), &result));
  EXPECT_EQ(0, result) << "Camera device initialization fails";

  EXPECT_NE(nullptr, gralloc_) << "Gralloc initialization fails";

  camera_info cam_info;
  EXPECT_EQ(0, cam_module->GetCameraInfo(cam_id_, &cam_info));
  static_info_.reset(new StaticInfo(cam_info));
  EXPECT_TRUE(static_info_->IsHardwareLevelAtLeastLimited())
      << "The device must support at least LIMITED hardware level";

  if (testing::Test::HasFailure()) {
    return -EINVAL;
  }

  sem_init(&shutter_sem_, 0, 0);
  sem_init(&capture_result_sem_, 0, 0);
  initialized_ = true;
  return 0;
}

void Camera3Device::Destroy() {
  VLOGF_ENTER();
  sem_destroy(&shutter_sem_);
  sem_destroy(&capture_result_sem_);

  int result = -EINVAL;
  hal_thread_.PostTaskSync(base::Bind(&Camera3Device::CloseOnHalThread,
                                      base::Unretained(this), &result));
  EXPECT_EQ(0, result) << "Camera device close failed";
  hal_thread_.Stop();
  initialized_ = false;
}

void Camera3Device::RegisterProcessCaptureResultCallback(
    ProcessCaptureResultCallback cb) {
  process_capture_result_cb_ = cb;
}

void Camera3Device::RegisterNotifyCallback(NotifyCallback cb) {
  notify_cb_ = cb;
}

void Camera3Device::RegisterResultMetadataOutputBufferCallback(
    ProcessResultMetadataOutputBuffersCallback cb) {
  process_result_metadata_output_buffers_cb_ = cb;
}

void Camera3Device::RegisterPartialMetadataCallback(
    ProcessPartialMetadataCallback cb) {
  process_partial_metadata_cb_ = cb;
}

bool Camera3Device::IsTemplateSupported(int32_t type) const {
  if (!initialized_) {
    return false;
  }
  return (type != CAMERA3_TEMPLATE_MANUAL ||
          static_info_->IsCapabilitySupported(
              ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR)) &&
         (type != CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG ||
          static_info_->IsCapabilitySupported(
              ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING));
}

const camera_metadata_t* Camera3Device::ConstructDefaultRequestSettings(
    int type) {
  if (!initialized_) {
    return NULL;
  }
  const camera_metadata_t* metadata = nullptr;
  hal_thread_.PostTaskSync(
      base::Bind(&Camera3Device::ConstructDefaultRequestSettingsOnHalThread,
                 base::Unretained(this), type, &metadata));
  return metadata;
}

void Camera3Device::AddOutputStream(int format, int width, int height) {
  VLOGF_ENTER();
  if (!initialized_) {
    return;
  }
  camera3_stream_t stream = {};
  stream.stream_type = CAMERA3_STREAM_OUTPUT;
  stream.width = width;
  stream.height = height;
  stream.format = format;

  // Push to the bin that is not used currently
  cam_stream_[!cam_stream_idx_].push_back(stream);
}

int Camera3Device::ConfigureStreams(
    std::vector<const camera3_stream_t*>* streams) {
  VLOGF_ENTER();
  if (!initialized_) {
    return -ENODEV;
  }

  // Check whether there are streams
  if (cam_stream_[!cam_stream_idx_].size() == 0) {
    return -EINVAL;
  }

  // Prepare stream configuration
  std::vector<camera3_stream_t*> cam_streams;
  camera3_stream_configuration_t cam_stream_config;
  for (size_t i = 0; i < cam_stream_[!cam_stream_idx_].size(); i++) {
    cam_streams.push_back(&cam_stream_[!cam_stream_idx_][i]);
  }
  cam_stream_config.num_streams = cam_streams.size();
  cam_stream_config.streams = cam_streams.data();
  cam_stream_config.operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE;

  // Configure streams now
  int ret = -EIO;
  hal_thread_.PostTaskSync(
      base::Bind(&Camera3Device::ConfigureStreamsOnHalThread,
                 base::Unretained(this), &cam_stream_config, &ret));
  // TODO(hywu): validate cam_stream_config.streams[*].max_buffers > 0

  // Swap to the other bin
  cam_stream_[cam_stream_idx_].clear();
  cam_stream_idx_ = !cam_stream_idx_;

  if (ret == 0 && streams) {
    for (auto const& it : cam_stream_[cam_stream_idx_]) {
      streams->push_back(&it);
    }
  }
  return ret;
}

int Camera3Device::AllocateOutputStreamBuffers(
    std::vector<camera3_stream_buffer_t>* output_buffers) {
  std::vector<const camera3_stream_t*> streams;
  for (const auto& it : cam_stream_[cam_stream_idx_]) {
    streams.push_back(&it);
  }
  return AllocateOutputBuffersByStreams(streams, output_buffers);
}

int Camera3Device::AllocateOutputBuffersByStreams(
    const std::vector<const camera3_stream_t*>& streams,
    std::vector<camera3_stream_buffer_t>* output_buffers) {
  VLOGF_ENTER();
  if (!initialized_) {
    return -ENODEV;
  }
  if (!output_buffers || streams.empty()) {
    return -EINVAL;
  }
  int32_t jpeg_max_size = 0;
  if (std::find_if(streams.begin(), streams.end(),
                   [](const camera3_stream_t* stream) {
                     return stream->format == HAL_PIXEL_FORMAT_BLOB;
                   }) != streams.end()) {
    jpeg_max_size = static_info_->GetJpegMaxSize();
    if (jpeg_max_size <= 0) {
      return -EINVAL;
    }
  }

  for (const auto& it : streams) {
    BufferHandleUniquePtr buffer = gralloc_->Allocate(
        (it->format == HAL_PIXEL_FORMAT_BLOB) ? jpeg_max_size : it->width,
        (it->format == HAL_PIXEL_FORMAT_BLOB) ? 1 : it->height, it->format,
        GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_CAMERA_WRITE);
    if (!buffer) {
      LOG(ERROR) << "Gralloc allocation fails";
      return -ENOMEM;
    }

    camera3_stream_buffer_t stream_buffer;
    {
      base::AutoLock l(stream_buffer_map_lock_);
      camera3_stream_t* stream = const_cast<camera3_stream_t*>(it);

      stream_buffer = {.stream = stream,
                       .buffer = buffer.get(),
                       .status = CAMERA3_BUFFER_STATUS_OK,
                       .acquire_fence = -1,
                       .release_fence = -1};
      stream_buffer_map_[stream].push_back(std::move(buffer));
    }
    output_buffers->push_back(stream_buffer);
  }

  return 0;
}

int Camera3Device::RegisterOutputBuffer(const camera3_stream_t& stream,
                                        BufferHandleUniquePtr unique_buffer) {
  VLOGF_ENTER();
  if (!initialized_) {
    return -ENODEV;
  }
  if (!unique_buffer) {
    return -EINVAL;
  }
  stream_buffer_map_[&stream].push_back(std::move(unique_buffer));
  return 0;
}

int Camera3Device::ProcessCaptureRequest(
    camera3_capture_request_t* capture_request) {
  VLOGF_ENTER();
  if (!initialized_) {
    return -ENODEV;
  }
  base::AutoLock l(request_lock_);
  if (capture_request) {
    capture_request_map_[request_frame_number_] = *capture_request;
    capture_request_map_[request_frame_number_].frame_number =
        request_frame_number_;
  }
  int ret = -EIO;
  hal_thread_.PostTaskSync(base::Bind(
      &Camera3Device::ProcessCaptureRequestOnHalThread, base::Unretained(this),
      capture_request ? &capture_request_map_[request_frame_number_]
                      : capture_request,
      &ret));
  if (ret == 0) {
    capture_request->frame_number = request_frame_number_;
    request_frame_number_++;
  }
  return ret;
}

int Camera3Device::WaitShutter(const struct timespec& timeout) {
  if (!initialized_) {
    return -ENODEV;
  }
  if (!process_capture_result_cb_.is_null()) {
    LOG(ERROR) << "Test has registered its own process_capture_result callback "
                  "function and thus must provide its own "
               << __func__;
    return -EINVAL;
  }
  return sem_timedwait(&shutter_sem_, &timeout);
}

int Camera3Device::WaitCaptureResult(const struct timespec& timeout) {
  if (!initialized_) {
    return -ENODEV;
  }
  if (!process_capture_result_cb_.is_null()) {
    LOG(ERROR) << "Test has registered its own process_capture_result callback "
                  "function and thus must provide its own "
               << __func__;
    return -EINVAL;
  }
  return sem_timedwait(&capture_result_sem_, &timeout);
}

int Camera3Device::Flush() {
  VLOGF_ENTER();
  if (!initialized_) {
    return -ENODEV;
  }
  int ret = -EIO;
  hal_thread_.PostTaskSync(base::Bind(&Camera3Device::FlushOnHalThread,
                                      base::Unretained(this), &ret));
  return ret;
}

const Camera3Device::StaticInfo* Camera3Device::GetStaticInfo() const {
  return static_info_.get();
}

void Camera3Device::ProcessCaptureResult(const camera3_capture_result* result) {
  if (!process_capture_result_cb_.is_null()) {
    process_capture_result_cb_.Run(result);
    return;
  }
  ASSERT_NE(nullptr, result) << "Capture result is null";
  // At least one of metadata or output buffers or input buffer should be
  // returned
  ASSERT_TRUE((result->result != NULL) || (result->num_output_buffers != 0) ||
              (result->input_buffer != NULL))
      << "No result data provided by HAL for frame " << result->frame_number;
  if (result->num_output_buffers) {
    ASSERT_NE(nullptr, result->output_buffers)
        << "No output buffer is returned while " << result->num_output_buffers
        << " are expected";
  }
  ASSERT_NE(capture_request_map_.end(),
            capture_request_map_.find(result->frame_number))
      << "A result is received for nonexistent request (frame number "
      << result->frame_number << ")";

  // For HAL3.2 or above, If HAL doesn't support partial, it must always set
  // partial_result to 1 when metadata is included in this result.
  ASSERT_TRUE(UsePartialResult() || result->result == NULL ||
              result->partial_result == 1)
      << "Result is malformed: partial_result must be 1 if partial result is "
         "not supported";
  // If partial_result > 0, there should be metadata returned in this result;
  // otherwise, there should be none.
  ASSERT_TRUE((result->partial_result > 0) == (result->result != NULL));

  if (result->result) {
    ProcessPartialResult(*result);
    EXPECT_KEY_VALUE_GT_I64(result->result, ANDROID_SENSOR_TIMESTAMP, 0);
  }

  for (uint32_t i = 0; i < result->num_output_buffers; i++) {
    camera3_stream_buffer_t stream_buffer = result->output_buffers[i];
    ASSERT_NE(nullptr, stream_buffer.buffer)
        << "Capture result output buffer is null";
    // An error may be expected while flushing
    EXPECT_EQ(CAMERA3_BUFFER_STATUS_OK, stream_buffer.status)
        << "Capture result buffer status error";
    ASSERT_EQ(-1, stream_buffer.acquire_fence)
        << "Capture result buffer fence error";

    // TODO: check buffers for a given streams are returned in order
    if (stream_buffer.release_fence != -1) {
      ASSERT_EQ(0, sync_wait(stream_buffer.release_fence, 1000))
          << "Error waiting on buffer acquire fence";
      close(stream_buffer.release_fence);
    }
    capture_result_info_map_[result->frame_number].output_buffers_.push_back(
        result->output_buffers[i]);
  }

  capture_result_info_map_[result->frame_number].num_output_buffers_ +=
      result->num_output_buffers;
  ASSERT_LE(capture_result_info_map_[result->frame_number].num_output_buffers_,
            capture_request_map_[result->frame_number].num_output_buffers);
  if (capture_result_info_map_[result->frame_number].num_output_buffers_ ==
          capture_request_map_[result->frame_number].num_output_buffers &&
      capture_result_info_map_[result->frame_number].have_result_metadata_) {
    ASSERT_EQ(completed_request_set_.end(),
              completed_request_set_.find(result->frame_number))
        << "Multiple results are received for the same request";
    completed_request_set_.insert(result->frame_number);

    // Process all received metadata and output buffers
    std::vector<BufferHandleUniquePtr> unique_buffers;
    if (GetOutputStreamBufferHandles(
            capture_result_info_map_[result->frame_number].output_buffers_,
            &unique_buffers)) {
      ADD_FAILURE() << "Failed to get output buffers";
    }
    // Send the final metadata and all output buffer to the test that registers
    // a callback by RegisterResultMetadataOutputBufferCallback().
    CameraMetadataUniquePtr final_metadata(
        capture_result_info_map_[result->frame_number].MergePartialMetadata());
    if (!process_result_metadata_output_buffers_cb_.is_null()) {
      process_result_metadata_output_buffers_cb_.Run(
          result->frame_number, std::move(final_metadata), &unique_buffers);
    }
    // Send all the partial metadata to the test that registers a callback by
    // RegisterPartialMetadataCallback().
    std::vector<CameraMetadataUniquePtr> partial_metadata;
    partial_metadata.swap(
        capture_result_info_map_[result->frame_number].partial_metadata_);
    if (!process_partial_metadata_cb_.is_null()) {
      process_partial_metadata_cb_.Run(&partial_metadata);
    }

    ASSERT_EQ(result->frame_number, result_frame_number_)
        << "Capture result is out of order";
    result_frame_number_++;
    {
      base::AutoLock l(request_lock_);
      capture_request_map_.erase(result->frame_number);
    }
    capture_result_info_map_.erase(result->frame_number);

    // Everything looks fine. Post it now.
    sem_post(&capture_result_sem_);
  }
}

void Camera3Device::Notify(const camera3_notify_msg* msg) {
  if (!notify_cb_.is_null()) {
    notify_cb_.Run(msg);
    return;
  }
  EXPECT_EQ(CAMERA3_MSG_SHUTTER, msg->type)
      << "Shutter error = " << msg->message.error.error_code;

  if (msg->type == CAMERA3_MSG_SHUTTER) {
    sem_post(&shutter_sem_);
  }
}

const std::string Camera3Device::GetThreadName(int cam_id) {
  const size_t kThreadNameLength = 30;
  char thread_name[kThreadNameLength];
  snprintf(thread_name, kThreadNameLength, "Camera3 Test Device %d Thread",
           cam_id);
  return std::string(thread_name);
}

void Camera3Device::ProcessCaptureResultForwarder(
    const camera3_callback_ops* cb,
    const camera3_capture_result* result) {
  // Forward to callback of instance
  Camera3Device* d =
      const_cast<Camera3Device*>(static_cast<const Camera3Device*>(cb));
  d->ProcessCaptureResult(result);
}

void Camera3Device::NotifyForwarder(const camera3_callback_ops* cb,
                                    const camera3_notify_msg* msg) {
  // Forward to callback of instance
  Camera3Device* d =
      const_cast<Camera3Device*>(static_cast<const Camera3Device*>(cb));
  d->Notify(msg);
}

void Camera3Device::InitializeOnHalThread(
    const camera3_callback_ops_t* callback_ops,
    int* result) {
  *result = cam_device_->ops->initialize(cam_device_, callback_ops);
}

void Camera3Device::ConstructDefaultRequestSettingsOnHalThread(
    int type,
    const camera_metadata_t** result) {
  *result =
      cam_device_->ops->construct_default_request_settings(cam_device_, type);
}

void Camera3Device::ConfigureStreamsOnHalThread(
    camera3_stream_configuration_t* config,
    int* result) {
  *result = cam_device_->ops->configure_streams(cam_device_, config);
}

void Camera3Device::ProcessCaptureRequestOnHalThread(
    camera3_capture_request_t* request,
    int* result) {
  VLOGF_ENTER();
  *result = cam_device_->ops->process_capture_request(cam_device_, request);
}

void Camera3Device::FlushOnHalThread(int* result) {
  *result = cam_device_->ops->flush(cam_device_);
}

void Camera3Device::CloseOnHalThread(int* result) {
  ASSERT_NE(nullptr, cam_device_->common.close)
      << "Camera close() is not implemented";
  *result = cam_device_->common.close(&cam_device_->common);
}

int Camera3Device::GetOutputStreamBufferHandles(
    const std::vector<camera3_stream_buffer_t>& output_buffers,
    std::vector<BufferHandleUniquePtr>* unique_buffers) {
  VLOGF_ENTER();
  base::AutoLock l(stream_buffer_map_lock_);

  for (const auto& output_buffer : output_buffers) {
    if (!output_buffer.buffer ||
        stream_buffer_map_.find(output_buffer.stream) ==
            stream_buffer_map_.end()) {
      LOG(ERROR) << "Failed to find configured stream or buffer is invalid";
      return -EINVAL;
    }

    auto stream_buffers = &stream_buffer_map_[output_buffer.stream];
    auto it = std::find_if(stream_buffers->begin(), stream_buffers->end(),
                           [=](const BufferHandleUniquePtr& buffer) {
                             return *buffer == *output_buffer.buffer;
                           });
    if (it != stream_buffers->end()) {
      unique_buffers->push_back(std::move(*it));
      stream_buffers->erase(it);
    } else {
      LOG(ERROR) << "Failed to find output buffer";
      return -EINVAL;
    }
  }
  return 0;
}

bool Camera3Device::UsePartialResult() const {
  return static_info_->GetPartialResultCount() > 1;
}

void Camera3Device::ProcessPartialResult(const camera3_capture_result& result) {
  // True if this partial result is the final one. If HAL does not use partial
  // result, the value is True by default.
  bool is_final_partial_result = !UsePartialResult();
  // Check if this result carries only partial metadata
  if (UsePartialResult() && result.result != NULL) {
    EXPECT_LE(result.partial_result, static_info_->GetPartialResultCount());
    EXPECT_GE(result.partial_result, 1);
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(
            result.result, ANDROID_QUIRKS_PARTIAL_RESULT, &entry) == 0) {
      is_final_partial_result =
          (entry.data.i32[0] == ANDROID_QUIRKS_PARTIAL_RESULT_FINAL);
    }
  }

  // Did we get the (final) result metadata for this capture?
  if (result.result != NULL && is_final_partial_result) {
    EXPECT_FALSE(
        capture_result_info_map_[result.frame_number].have_result_metadata_)
        << "Called multiple times with final metadata";
    capture_result_info_map_[result.frame_number].have_result_metadata_ = true;
  }

  capture_result_info_map_[result.frame_number].AllocateAndCopyMetadata(
      *result.result);
}

void Camera3Device::CaptureResultInfo::AllocateAndCopyMetadata(
    const camera_metadata_t& src) {
  CameraMetadataUniquePtr metadata(allocate_copy_camera_metadata_checked(
      &src, get_camera_metadata_size(&src)));
  ASSERT_NE(nullptr, metadata.get()) << "Copying result partial metadata fails";
  partial_metadata_.push_back(std::move(metadata));
}

bool Camera3Device::CaptureResultInfo::IsMetadataKeyAvailable(
    int32_t key) const {
  return GetMetadataKeyEntry(key, nullptr);
}

int32_t Camera3Device::CaptureResultInfo::GetMetadataKeyValue(
    int32_t key) const {
  camera_metadata_ro_entry_t entry;
  return GetMetadataKeyEntry(key, &entry) ? entry.data.i32[0] : -EINVAL;
}

int64_t Camera3Device::CaptureResultInfo::GetMetadataKeyValue64(
    int32_t key) const {
  camera_metadata_ro_entry_t entry;
  return GetMetadataKeyEntry(key, &entry) ? entry.data.i64[0] : -EINVAL;
}

CameraMetadataUniquePtr
Camera3Device::CaptureResultInfo::MergePartialMetadata() {
  size_t entry_count = 0;
  size_t data_count = 0;
  for (const auto& it : partial_metadata_) {
    entry_count += get_camera_metadata_entry_count(it.get());
    data_count += get_camera_metadata_data_count(it.get());
  }
  camera_metadata_t* metadata =
      allocate_camera_metadata(entry_count, data_count);
  if (!metadata) {
    ADD_FAILURE() << "Can't allocate larger metadata buffer";
    return nullptr;
  }
  for (const auto& it : partial_metadata_) {
    append_camera_metadata(metadata, it.get());
  }
  return CameraMetadataUniquePtr(metadata);
}

bool Camera3Device::CaptureResultInfo::GetMetadataKeyEntry(
    int32_t key,
    camera_metadata_ro_entry_t* entry) const {
  camera_metadata_ro_entry_t local_entry;
  entry = entry ? entry : &local_entry;
  for (const auto& it : partial_metadata_) {
    if (find_camera_metadata_ro_entry(it.get(), key, entry) == 0) {
      return true;
    }
  }
  return false;
}

Camera3Device::StaticInfo::StaticInfo(const camera_info& cam_info)
    : characteristics_(const_cast<camera_metadata_t*>(
          cam_info.static_camera_characteristics)) {}

bool Camera3Device::StaticInfo::IsKeyAvailable(uint32_t tag) const {
  return AreKeysAvailable(std::vector<uint32_t>(1, tag));
}

bool Camera3Device::StaticInfo::AreKeysAvailable(
    std::vector<uint32_t> tags) const {
  for (const auto& tag : tags) {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(characteristics_, tag, &entry)) {
      return false;
    }
  }
  return true;
}

int32_t Camera3Device::StaticInfo::GetHardwareLevel() const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_,
                                    ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
                                    &entry) != 0) {
    ADD_FAILURE()
        << "Cannot find the metadata ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL";
    return -EINVAL;
  }
  return entry.data.i32[0];
}

bool Camera3Device::StaticInfo::IsHardwareLevelAtLeast(int32_t level) const {
  int32_t dev_level = GetHardwareLevel();
  if (dev_level == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY) {
    return dev_level == level;
  }
  // Level is not LEGACY, can use numerical sort
  return dev_level >= level;
}

bool Camera3Device::StaticInfo::IsHardwareLevelAtLeastFull() const {
  return IsHardwareLevelAtLeast(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL);
}

bool Camera3Device::StaticInfo::IsHardwareLevelAtLeastLimited() const {
  return IsHardwareLevelAtLeast(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED);
}

bool Camera3Device::StaticInfo::IsCapabilitySupported(
    int32_t capability) const {
  EXPECT_GE(capability, 0) << "Capability must be non-negative";

  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_,
                                    ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                    &entry) == 0) {
    for (size_t i = 0; i < entry.count; i++) {
      if (entry.data.i32[i] == capability) {
        return true;
      }
    }
  }
  return false;
}

bool Camera3Device::StaticInfo::IsDepthOutputSupported() const {
  return IsCapabilitySupported(
      ANDROID_REQUEST_AVAILABLE_CAPABILITIES_DEPTH_OUTPUT);
}

bool Camera3Device::StaticInfo::IsColorOutputSupported() const {
  return IsCapabilitySupported(
      ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
}

std::set<uint8_t> Camera3Device::StaticInfo::GetAvailableModes(
    int32_t key,
    int32_t min_value,
    int32_t max_value) const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_, key, &entry) != 0) {
    ADD_FAILURE() << "Cannot find the metadata "
                  << get_camera_metadata_tag_name(key);
    return std::set<uint8_t>();
  }
  std::set<uint8_t> modes;
  for (size_t i = 0; i < entry.count; i++) {
    uint8_t mode = entry.data.u8[i];
    // Each element must be distinct
    EXPECT_TRUE(modes.find(mode) == modes.end())
        << "Duplicate modes " << mode << " for the metadata "
        << get_camera_metadata_tag_name(key);
    EXPECT_TRUE(mode >= min_value && mode <= max_value)
        << "Mode " << mode << " is outside of [" << min_value << ","
        << max_value << "] for the metadata "
        << get_camera_metadata_tag_name(key);
    modes.insert(mode);
  }
  return modes;
}

std::set<uint8_t> Camera3Device::StaticInfo::GetAvailableEdgeModes() const {
  std::set<uint8_t> modes = GetAvailableModes(
      ANDROID_EDGE_AVAILABLE_EDGE_MODES, ANDROID_EDGE_MODE_OFF,
      ANDROID_EDGE_MODE_ZERO_SHUTTER_LAG);

  // Full device should always include OFF and FAST
  if (IsHardwareLevelAtLeastFull()) {
    EXPECT_TRUE((modes.find(ANDROID_EDGE_MODE_OFF) != modes.end()) &&
                (modes.find(ANDROID_EDGE_MODE_FAST) != modes.end()))
        << "Full device must contain OFF and FAST edge modes";
  }

  // FAST and HIGH_QUALITY mode must be both present or both not present
  EXPECT_TRUE((modes.find(ANDROID_EDGE_MODE_FAST) != modes.end()) ==
              (modes.find(ANDROID_EDGE_MODE_HIGH_QUALITY) != modes.end()))
      << "FAST and HIGH_QUALITY mode must both present or both not present";

  return modes;
}

std::set<uint8_t> Camera3Device::StaticInfo::GetAvailableNoiseReductionModes()
    const {
  std::set<uint8_t> modes =
      GetAvailableModes(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
                        ANDROID_NOISE_REDUCTION_MODE_OFF,
                        ANDROID_NOISE_REDUCTION_MODE_ZERO_SHUTTER_LAG);

  // Full device should always include OFF and FAST
  if (IsHardwareLevelAtLeastFull()) {
    EXPECT_TRUE((modes.find(ANDROID_NOISE_REDUCTION_MODE_OFF) != modes.end()) &&
                (modes.find(ANDROID_NOISE_REDUCTION_MODE_FAST) != modes.end()))
        << "Full device must contain OFF and FAST noise reduction modes";
  }

  // FAST and HIGH_QUALITY mode must be both present or both not present
  EXPECT_TRUE(
      (modes.find(ANDROID_NOISE_REDUCTION_MODE_FAST) != modes.end()) ==
      (modes.find(ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY) != modes.end()))
      << "FAST and HIGH_QUALITY mode must both present or both not present";

  return modes;
}

std::set<uint8_t> Camera3Device::StaticInfo::GetAvailableColorAberrationModes()
    const {
  std::set<uint8_t> modes =
      GetAvailableModes(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
                        ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF,
                        ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY);

  EXPECT_TRUE((modes.find(ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF) !=
               modes.end()) ||
              (modes.find(ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST) !=
               modes.end()))
      << "Camera devices must always support either OFF or FAST mode";

  // FAST and HIGH_QUALITY mode must be both present or both not present
  EXPECT_TRUE(
      (modes.find(ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST) !=
       modes.end()) ==
      (modes.find(ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY) !=
       modes.end()))
      << "FAST and HIGH_QUALITY mode must both present or both not present";

  return modes;
}

std::set<uint8_t> Camera3Device::StaticInfo::GetAvailableToneMapModes() const {
  std::set<uint8_t> modes = GetAvailableModes(
      ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
      ANDROID_TONEMAP_MODE_CONTRAST_CURVE, ANDROID_TONEMAP_MODE_PRESET_CURVE);

  EXPECT_TRUE(modes.find(ANDROID_TONEMAP_MODE_FAST) != modes.end())
      << "Camera devices must always support FAST mode";

  // FAST and HIGH_QUALITY mode must be both present
  EXPECT_TRUE(modes.find(ANDROID_TONEMAP_MODE_HIGH_QUALITY) != modes.end())
      << "FAST and HIGH_QUALITY mode must both present";

  return modes;
}

void Camera3Device::StaticInfo::GetStreamConfigEntry(
    camera_metadata_ro_entry_t* entry) const {
  entry->count = 0;

  camera_metadata_ro_entry_t local_entry = {};
  ASSERT_EQ(
      0, find_camera_metadata_ro_entry(
             characteristics_, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
             &local_entry))
      << "Fail to find metadata key "
         "ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS";
  ASSERT_NE(0u, local_entry.count) << "Camera stream configuration is empty";
  ASSERT_EQ(0u, local_entry.count % kNumOfElementsInStreamConfigEntry)
      << "Camera stream configuration parsing error";
  *entry = local_entry;
}

std::set<int32_t> Camera3Device::StaticInfo::GetAvailableFormats(
    int32_t direction) const {
  camera_metadata_ro_entry_t available_config = {};
  GetStreamConfigEntry(&available_config);
  std::set<int32_t> formats;
  for (size_t i = 0; i < available_config.count;
       i += kNumOfElementsInStreamConfigEntry) {
    int32_t format = available_config.data.i32[i + STREAM_CONFIG_FORMAT_INDEX];
    int32_t in_or_out =
        available_config.data.i32[i + STREAM_CONFIG_DIRECTION_INDEX];
    if (in_or_out == direction) {
      formats.insert(format);
    }
  }
  return formats;
}

bool Camera3Device::StaticInfo::IsFormatAvailable(int format) const {
  camera_metadata_ro_entry_t available_config = {};
  GetStreamConfigEntry(&available_config);
  for (uint32_t i = 0; i < available_config.count;
       i += kNumOfElementsInStreamConfigEntry) {
    if (available_config.data.i32[i + STREAM_CONFIG_FORMAT_INDEX] == format) {
      return true;
    }
  }
  return false;
}

std::vector<ResolutionInfo>
Camera3Device::StaticInfo::GetSortedOutputResolutions(int32_t format) const {
  camera_metadata_ro_entry_t available_config = {};
  GetStreamConfigEntry(&available_config);
  std::vector<ResolutionInfo> available_resolutions;
  for (uint32_t i = 0; i < available_config.count;
       i += kNumOfElementsInStreamConfigEntry) {
    int32_t fmt = available_config.data.i32[i + STREAM_CONFIG_FORMAT_INDEX];
    int32_t width = available_config.data.i32[i + STREAM_CONFIG_WIDTH_INDEX];
    int32_t height = available_config.data.i32[i + STREAM_CONFIG_HEIGHT_INDEX];
    int32_t in_or_out =
        available_config.data.i32[i + STREAM_CONFIG_DIRECTION_INDEX];
    if ((fmt == format) &&
        (in_or_out == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)) {
      available_resolutions.emplace_back(width, height);
    }
  }
  std::sort(available_resolutions.begin(), available_resolutions.end());
  return available_resolutions;
}

bool Camera3Device::StaticInfo::IsAELockSupported() const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(
          characteristics_, ANDROID_CONTROL_AE_LOCK_AVAILABLE, &entry) != 0) {
    ADD_FAILURE()
        << "Cannot find the metadata ANDROID_CONTROL_AE_LOCK_AVAILABLE";
    return false;
  }
  return entry.data.i32[0] == ANDROID_CONTROL_AE_LOCK_AVAILABLE_TRUE;
}

bool Camera3Device::StaticInfo::IsAWBLockSupported() const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(
          characteristics_, ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &entry) != 0) {
    ADD_FAILURE()
        << "Cannot find the metadata ANDROID_CONTROL_AWB_LOCK_AVAILABLE";
    return false;
  }
  return entry.data.i32[0] == ANDROID_CONTROL_AWB_LOCK_AVAILABLE_TRUE;
}

int32_t Camera3Device::StaticInfo::GetPartialResultCount() const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_,
                                    ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
                                    &entry) != 0) {
    // Optional key. Default value is 1 if key is missing.
    return 1;
  }
  return entry.data.i32[0];
}

int32_t Camera3Device::StaticInfo::GetRequestPipelineMaxDepth() const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(
          characteristics_, ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &entry) != 0) {
    ADD_FAILURE()
        << "Cannot find the metadata ANDROID_REQUEST_PIPELINE_MAX_DEPTH";
    return -EINVAL;
  }
  return entry.data.i32[0];
}

int32_t Camera3Device::StaticInfo::GetJpegMaxSize() const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_, ANDROID_JPEG_MAX_SIZE,
                                    &entry) != 0) {
    ADD_FAILURE() << "Cannot find the metadata ANDROID_JPEG_MAX_SIZE";
    return -EINVAL;
  }
  return entry.data.i32[0];
}

int32_t Camera3Device::StaticInfo::GetSensorOrientation() const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_,
                                    ANDROID_SENSOR_ORIENTATION, &entry) != 0) {
    ADD_FAILURE() << "Cannot find the metadata ANDROID_SENSOR_ORIENTATION";
    return -EINVAL;
  }
  return entry.data.i32[0];
}

int32_t Camera3Device::StaticInfo::GetAvailableThumbnailSizes(
    std::vector<ResolutionInfo>* resolutions) const {
  const size_t kNumOfEntriesForSize = 2;
  enum { WIDTH_ENTRY_INDEX, HEIGHT_ENTRY_INDEX };
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_,
                                    ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
                                    &entry) != 0) {
    ADD_FAILURE()
        << "Cannot find the metadata ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES";
    return -EINVAL;
  }
  if (entry.count % kNumOfEntriesForSize) {
    ADD_FAILURE() << "Camera JPEG available thumbnail sizes parsing error";
    return -EINVAL;
  }
  for (size_t i = 0; i < entry.count; i += kNumOfEntriesForSize) {
    resolutions->emplace_back(entry.data.i32[i + WIDTH_ENTRY_INDEX],
                              entry.data.i32[i + HEIGHT_ENTRY_INDEX]);
  }
  return 0;
}

int32_t Camera3Device::StaticInfo::GetAvailableFocalLengths(
    std::vector<float>* focal_lengths) const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_,
                                    ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                                    &entry) != 0) {
    ADD_FAILURE()
        << "Cannot find the metadata ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS";
    return -EINVAL;
  }
  if (entry.count == 0) {
    ADD_FAILURE() << "There should be at least one available focal length";
    return -EINVAL;
  }
  for (size_t i = 0; i < entry.count; i++) {
    EXPECT_LT(0.0f, entry.data.f[i])
        << "Available focal length " << entry.data.f[i]
        << " should be positive";
    focal_lengths->push_back(entry.data.f[i]);
  }
  EXPECT_EQ(
      focal_lengths->size(),
      std::set<float>(focal_lengths->begin(), focal_lengths->end()).size())
      << "Avaliable focal lengths should be distinct";
  return 0;
}

int32_t Camera3Device::StaticInfo::GetAvailableApertures(
    std::vector<float>* apertures) const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(characteristics_,
                                    ANDROID_LENS_INFO_AVAILABLE_APERTURES,
                                    &entry) != 0) {
    ADD_FAILURE()
        << "Cannot find the metadata ANDROID_LENS_INFO_AVAILABLE_APERTURES";
    return -EINVAL;
  }
  if (entry.count == 0) {
    ADD_FAILURE() << "There should be at least one available apertures";
    return -EINVAL;
  }
  for (size_t i = 0; i < entry.count; i++) {
    EXPECT_LT(0.0f, entry.data.f[i])
        << "Available apertures " << entry.data.f[i] << " should be positive";
    apertures->push_back(entry.data.f[i]);
  }
  EXPECT_EQ(apertures->size(),
            std::set<float>(apertures->begin(), apertures->end()).size())
      << "Avaliable apertures should be distinct";
  return 0;
}

int32_t Camera3Device::StaticInfo::GetAvailableAFModes(
    std::vector<int32_t>* af_modes) const {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(
          characteristics_, ANDROID_CONTROL_AF_AVAILABLE_MODES, &entry) == 0) {
    for (size_t i = 0; i < entry.count; i++) {
      af_modes->push_back(entry.data.i32[i]);
    }
  }
  return 0;
}

// Test fixture

void Camera3DeviceFixture::SetUp() {
  ASSERT_EQ(0, cam_module_.Initialize())
      << "Camera module initialization fails";

  ASSERT_EQ(0, cam_device_.Initialize(&cam_module_))
      << "Camera device initialization fails";
  cam_device_.RegisterResultMetadataOutputBufferCallback(
      base::Bind(&Camera3DeviceFixture::ProcessResultMetadataOutputBuffers,
                 base::Unretained(this)));
  cam_device_.RegisterPartialMetadataCallback(base::Bind(
      &Camera3DeviceFixture::ProcessPartialMetadata, base::Unretained(this)));
}

void Camera3DeviceFixture::TearDown() {
  cam_device_.Destroy();
}

// Test cases

// Test spec:
// - Camera ID
class Camera3DeviceSimpleTest : public Camera3DeviceFixture,
                                public ::testing::WithParamInterface<int> {
 public:
  Camera3DeviceSimpleTest() : Camera3DeviceFixture(GetParam()) {}
};

TEST_P(Camera3DeviceSimpleTest, SensorOrientationTest) {
  // Chromebook has a hardware requirement that the top of the camera should
  // match the top of the display in tablet mode.
  ASSERT_EQ(0, cam_device_.GetStaticInfo()->GetSensorOrientation())
      << "Invalid camera sensor orientation";
}

// Test spec:
// - Camera ID
// - Capture type
class Camera3DeviceDefaultSettings
    : public Camera3DeviceFixture,
      public ::testing::WithParamInterface<std::tuple<int, int>> {
 public:
  Camera3DeviceDefaultSettings()
      : Camera3DeviceFixture(std::get<0>(GetParam())) {}
};

static bool IsMetadataKeyAvailable(const camera_metadata_t* settings,
                                   int32_t key) {
  camera_metadata_ro_entry_t entry;
  return find_camera_metadata_ro_entry(settings, key, &entry) == 0;
}

static void ExpectKeyValue(const camera_metadata_t* settings,
                           int32_t key,
                           const char* key_name,
                           int32_t value,
                           int32_t compare_type) {
  camera_metadata_ro_entry_t entry;
  ASSERT_EQ(0, find_camera_metadata_ro_entry(settings, key, &entry))
      << "Cannot find the metadata " << key_name;
  if (compare_type == 0) {
    ASSERT_EQ(value, entry.data.i32[0]) << "Wrong value of metadata "
                                        << key_name;
  } else {
    ASSERT_NE(value, entry.data.i32[0]) << "Wrong value of metadata "
                                        << key_name;
  }
}
#define EXPECT_KEY_VALUE_EQ(settings, key, value) \
  ExpectKeyValue(settings, key, #key, value, 0)
#define EXPECT_KEY_VALUE_NE(settings, key, value) \
  ExpectKeyValue(settings, key, #key, value, 1)

static void ExpectKeyValueNotEqualsI64(const camera_metadata_t* settings,
                                       int32_t key,
                                       const char* key_name,
                                       int64_t value) {
  camera_metadata_ro_entry_t entry;
  ASSERT_EQ(0, find_camera_metadata_ro_entry(settings, key, &entry))
      << "Cannot find the metadata " << key_name;
  ASSERT_NE(value, entry.data.i64[0]) << "Wrong value of metadata " << key_name;
}
#define EXPECT_KEY_VALUE_NE_I64(settings, key, value) \
  ExpectKeyValueNotEqualsI64(settings, key, #key, value)

TEST_P(Camera3DeviceDefaultSettings, ConstructDefaultSettings) {
  int type = std::get<1>(GetParam());

  const camera_metadata_t* default_settings;
  default_settings = cam_device_.ConstructDefaultRequestSettings(type);
  ASSERT_NE(nullptr, default_settings) << "Camera default settings are NULL";

  auto static_info = cam_device_.GetStaticInfo();

  // Reference: camera2/cts/CameraDeviceTest.java#captureTemplateTestByCamera
  if (!cam_device_.IsTemplateSupported(type)) {
    return;
  } else if (type != CAMERA3_TEMPLATE_PREVIEW &&
             static_info->IsDepthOutputSupported() &&
             !static_info->IsColorOutputSupported()) {
    // Depth-only devices need only support PREVIEW template
    return;
  }

  // Reference: camera2/cts/CameraDeviceTest.java#checkRequestForTemplate
  // 3A settings--control mode
  EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_CONTROL_AE_MODE,
                      ANDROID_CONTROL_AE_MODE_ON);

  EXPECT_KEY_VALUE_EQ(default_settings,
                      ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, 0);

  EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
                      ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE);

  // if AE lock is not supported, expect the control key to be non-existent or
  // false
  if (static_info->IsAELockSupported() ||
      IsMetadataKeyAvailable(default_settings, ANDROID_CONTROL_AE_LOCK)) {
    EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_CONTROL_AE_LOCK,
                        ANDROID_CONTROL_AE_LOCK_OFF);
  }

  EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_CONTROL_AF_TRIGGER,
                      ANDROID_CONTROL_AF_TRIGGER_IDLE);

  EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_CONTROL_AWB_MODE,
                      ANDROID_CONTROL_AWB_MODE_AUTO);

  // if AWB lock is not supported, expect the control key to be non-existent or
  // false
  if (static_info->IsAWBLockSupported() ||
      IsMetadataKeyAvailable(default_settings, ANDROID_CONTROL_AWB_LOCK)) {
    EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_CONTROL_AWB_LOCK,
                        ANDROID_CONTROL_AWB_LOCK_OFF);
  }

  // Check 3A regions
  // TODO: CONTROL_AE_REGIONS, CONTROL_AWB_REGIONS, CONTROL_AF_REGIONS?

  // Sensor settings
  // TODO: LENS_APERTURE, LENS_FILTER_DENSITY, LENS_FOCAL_LENGTH,
  //       LENS_OPTICAL_STABILIZATION_MODE?
  //       BLACK_LEVEL_LOCK?

  if (static_info->IsKeyAvailable(ANDROID_BLACK_LEVEL_LOCK)) {
    EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_BLACK_LEVEL_LOCK,
                        ANDROID_BLACK_LEVEL_LOCK_OFF);
  }

  if (static_info->IsKeyAvailable(ANDROID_SENSOR_FRAME_DURATION)) {
    EXPECT_KEY_VALUE_NE_I64(default_settings, ANDROID_SENSOR_FRAME_DURATION, 0);
  }

  if (static_info->IsKeyAvailable(ANDROID_SENSOR_EXPOSURE_TIME)) {
    EXPECT_KEY_VALUE_NE_I64(default_settings, ANDROID_SENSOR_EXPOSURE_TIME, 0);
  }

  if (static_info->IsKeyAvailable(ANDROID_SENSOR_SENSITIVITY)) {
    EXPECT_KEY_VALUE_NE(default_settings, ANDROID_SENSOR_SENSITIVITY, 0);
  }

  // ISP-processing settings
  EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_STATISTICS_FACE_DETECT_MODE,
                      ANDROID_STATISTICS_FACE_DETECT_MODE_OFF);

  EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_FLASH_MODE,
                      ANDROID_FLASH_MODE_OFF);

  if (static_info->IsKeyAvailable(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE)) {
    // If the device doesn't support RAW, all template should have OFF as
    // default
    if (!static_info->IsCapabilitySupported(
            ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW)) {
      EXPECT_KEY_VALUE_EQ(default_settings,
                          ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,
                          ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF);
    }
  }

  bool support_reprocessing =
      static_info->IsCapabilitySupported(
          ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING) ||
      static_info->IsCapabilitySupported(
          ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING);

  if (type == CAMERA3_TEMPLATE_STILL_CAPTURE) {
    // Not enforce high quality here, as some devices may not effectively have
    // high quality mode
    if (static_info->IsKeyAvailable(ANDROID_COLOR_CORRECTION_MODE)) {
      EXPECT_KEY_VALUE_NE(default_settings, ANDROID_COLOR_CORRECTION_MODE,
                          ANDROID_COLOR_CORRECTION_MODE_TRANSFORM_MATRIX);
    }

    // Edge enhancement, noise reduction and aberration correction modes.
    EXPECT_EQ(static_info->IsKeyAvailable(ANDROID_EDGE_MODE),
              static_info->IsKeyAvailable(ANDROID_EDGE_AVAILABLE_EDGE_MODES))
        << "Edge mode must be present in request if available edge modes are "
           "present in metadata, and vice-versa";
    if (static_info->IsKeyAvailable(ANDROID_EDGE_MODE)) {
      std::set<uint8_t> edge_modes = static_info->GetAvailableEdgeModes();
      // Don't need check fast as fast or high quality must be both present or
      // both not.
      if (edge_modes.find(ANDROID_EDGE_MODE_HIGH_QUALITY) != edge_modes.end()) {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_EDGE_MODE,
                            ANDROID_EDGE_MODE_HIGH_QUALITY);
      } else {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_EDGE_MODE,
                            ANDROID_EDGE_MODE_OFF);
      }
    }

    EXPECT_EQ(static_info->IsKeyAvailable(ANDROID_NOISE_REDUCTION_MODE),
              static_info->IsKeyAvailable(
                  ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES))
        << "Noise reduction mode must be present in request if available noise "
           "reductions are present in metadata, and vice-versa";
    if (static_info->IsKeyAvailable(
            ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES)) {
      std::set<uint8_t> nr_modes =
          static_info->GetAvailableNoiseReductionModes();
      // Don't need check fast as fast or high quality must be both present or
      // both not
      if (nr_modes.find(ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY) !=
          nr_modes.end()) {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_NOISE_REDUCTION_MODE,
                            ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY);
      } else {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_NOISE_REDUCTION_MODE,
                            ANDROID_NOISE_REDUCTION_MODE_OFF);
      }
    }

    EXPECT_EQ(
        static_info->IsKeyAvailable(ANDROID_COLOR_CORRECTION_ABERRATION_MODE),
        static_info->IsKeyAvailable(
            ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES))
        << "Aberration correction mode must be present in request if available "
           "aberration correction reductions are present in metadata, and "
           "vice-versa";
    if (static_info->IsKeyAvailable(ANDROID_COLOR_CORRECTION_ABERRATION_MODE)) {
      std::set<uint8_t> aberration_modes =
          static_info->GetAvailableColorAberrationModes();
      // Don't need check fast as fast or high quality must be both present or
      // both not
      if (aberration_modes.find(
              ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY) !=
          aberration_modes.end()) {
        EXPECT_KEY_VALUE_EQ(
            default_settings, ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
            ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY);
      } else {
        EXPECT_KEY_VALUE_EQ(default_settings,
                            ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                            ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF);
      }
    }
  } else if (type == CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG &&
             support_reprocessing) {
    EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_EDGE_MODE,
                        ANDROID_EDGE_MODE_ZERO_SHUTTER_LAG);
    EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_NOISE_REDUCTION_MODE,
                        ANDROID_NOISE_REDUCTION_MODE_ZERO_SHUTTER_LAG);
  } else if (type == CAMERA3_TEMPLATE_PREVIEW ||
             type == CAMERA3_TEMPLATE_VIDEO_RECORD) {
    if (static_info->IsKeyAvailable(ANDROID_EDGE_MODE)) {
      std::set<uint8_t> edge_modes = static_info->GetAvailableEdgeModes();
      if (edge_modes.find(ANDROID_EDGE_MODE_FAST) != edge_modes.end()) {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_EDGE_MODE,
                            ANDROID_EDGE_MODE_FAST);
      } else {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_EDGE_MODE,
                            ANDROID_EDGE_MODE_OFF);
      }
    }

    if (static_info->IsKeyAvailable(ANDROID_NOISE_REDUCTION_MODE)) {
      std::set<uint8_t> nr_modes =
          static_info->GetAvailableNoiseReductionModes();
      if (nr_modes.find(ANDROID_NOISE_REDUCTION_MODE_FAST) != nr_modes.end()) {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_NOISE_REDUCTION_MODE,
                            ANDROID_NOISE_REDUCTION_MODE_FAST);
      } else {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_NOISE_REDUCTION_MODE,
                            ANDROID_NOISE_REDUCTION_MODE_OFF);
      }
    }

    if (static_info->IsKeyAvailable(ANDROID_COLOR_CORRECTION_ABERRATION_MODE)) {
      std::set<uint8_t> aberration_modes =
          static_info->GetAvailableColorAberrationModes();
      if (aberration_modes.find(
              ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST) !=
          aberration_modes.end()) {
        EXPECT_KEY_VALUE_EQ(default_settings,
                            ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                            ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST);
      } else {
        EXPECT_KEY_VALUE_EQ(default_settings,
                            ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                            ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF);
      }
    }
  } else {
    if (static_info->IsKeyAvailable(ANDROID_EDGE_MODE)) {
      EXPECT_KEY_VALUE_NE(default_settings, ANDROID_EDGE_MODE, 0);
    }

    if (static_info->IsKeyAvailable(ANDROID_NOISE_REDUCTION_MODE)) {
      EXPECT_KEY_VALUE_NE(default_settings, ANDROID_NOISE_REDUCTION_MODE, 0);
    }

    if (static_info->IsKeyAvailable(ANDROID_COLOR_CORRECTION_ABERRATION_MODE)) {
      EXPECT_KEY_VALUE_NE(default_settings,
                          ANDROID_COLOR_CORRECTION_ABERRATION_MODE, 0);
    }
  }

  // Tone map and lens shading modes.
  if (type == CAMERA3_TEMPLATE_STILL_CAPTURE) {
    EXPECT_EQ(
        static_info->IsKeyAvailable(ANDROID_TONEMAP_MODE),
        static_info->IsKeyAvailable(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES))
        << "Tonemap mode must be present in request if available tonemap modes "
           "are present in metadata, and vice-versa";
    if (static_info->IsKeyAvailable(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES)) {
      std::set<uint8_t> tone_map_modes =
          static_info->GetAvailableToneMapModes();
      if (tone_map_modes.find(ANDROID_TONEMAP_MODE_HIGH_QUALITY) !=
          tone_map_modes.end()) {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_TONEMAP_MODE,
                            ANDROID_TONEMAP_MODE_HIGH_QUALITY);
      } else {
        EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_TONEMAP_MODE,
                            ANDROID_TONEMAP_MODE_FAST);
      }
    }

    // Still capture template should have android.statistics.lensShadingMapMode
    // ON when RAW capability is supported.
    if (static_info->IsKeyAvailable(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE) &&
        static_info->IsCapabilitySupported(
            ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW)) {
      EXPECT_KEY_VALUE_EQ(default_settings,
                          ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,
                          ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_ON);
    }
  } else {
    if (static_info->IsKeyAvailable(ANDROID_TONEMAP_MODE)) {
      EXPECT_KEY_VALUE_NE(default_settings, ANDROID_TONEMAP_MODE,
                          ANDROID_TONEMAP_MODE_CONTRAST_CURVE);
      EXPECT_KEY_VALUE_NE(default_settings, ANDROID_TONEMAP_MODE,
                          ANDROID_TONEMAP_MODE_GAMMA_VALUE);
      EXPECT_KEY_VALUE_NE(default_settings, ANDROID_TONEMAP_MODE,
                          ANDROID_TONEMAP_MODE_PRESET_CURVE);
    }
    if (static_info->IsKeyAvailable(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE)) {
      EXPECT_KEY_VALUE_NE(default_settings,
                          ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, 0);
    }
  }

  EXPECT_KEY_VALUE_EQ(default_settings, ANDROID_CONTROL_CAPTURE_INTENT, type);
}

// Test spec:
// - Camera ID
// - Capture type
class CreateInvalidTemplate
    : public Camera3DeviceFixture,
      public ::testing::WithParamInterface<std::tuple<int, int>> {
 public:
  CreateInvalidTemplate() : Camera3DeviceFixture(std::get<0>(GetParam())) {}
};

TEST_P(CreateInvalidTemplate, ConstructDefaultSettings) {
  // Reference:
  // camera2/cts/CameraDeviceTest.java#testCameraDeviceCreateCaptureBuilder
  int type = std::get<1>(GetParam());
  ASSERT_EQ(nullptr, cam_device_.ConstructDefaultRequestSettings(type))
      << "Should get error due to an invalid template ID";
}

INSTANTIATE_TEST_CASE_P(Camera3DeviceTest,
                        Camera3DeviceSimpleTest,
                        ::testing::ValuesIn(Camera3Module().GetCameraIds()));

INSTANTIATE_TEST_CASE_P(
    Camera3DeviceTest,
    Camera3DeviceDefaultSettings,
    ::testing::Combine(::testing::ValuesIn(Camera3Module().GetCameraIds()),
                       ::testing::Values(CAMERA3_TEMPLATE_PREVIEW,
                                         CAMERA3_TEMPLATE_STILL_CAPTURE,
                                         CAMERA3_TEMPLATE_VIDEO_RECORD,
                                         CAMERA3_TEMPLATE_VIDEO_SNAPSHOT,
                                         CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG,
                                         CAMERA3_TEMPLATE_MANUAL)));

INSTANTIATE_TEST_CASE_P(
    Camera3DeviceTest,
    CreateInvalidTemplate,
    ::testing::Combine(::testing::ValuesIn(Camera3Module().GetCameraIds()),
                       ::testing::Values(CAMERA3_TEMPLATE_PREVIEW - 1,
                                         CAMERA3_TEMPLATE_MANUAL + 1)));

}  // namespace camera3_test
