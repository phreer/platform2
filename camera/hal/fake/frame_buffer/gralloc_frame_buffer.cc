/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/frame_buffer/gralloc_frame_buffer.h"

#include <sys/mman.h>

#include <utility>

#include <base/memory/ptr_util.h>
#include <hardware/gralloc.h>
#include <linux/videodev2.h>
#include <libyuv.h>

#include "cros-camera/common.h"

namespace cros {

GrallocFrameBuffer::ScopedMapping::ScopedMapping(buffer_handle_t buffer)
    : scoped_mapping_(buffer) {}

GrallocFrameBuffer::ScopedMapping::~ScopedMapping() = default;

// static
std::unique_ptr<GrallocFrameBuffer::ScopedMapping>
GrallocFrameBuffer::ScopedMapping::Create(buffer_handle_t buffer) {
  auto mapping = base::WrapUnique(new ScopedMapping(buffer));
  if (!mapping->is_valid()) {
    return nullptr;
  }
  return mapping;
}

uint32_t GrallocFrameBuffer::ScopedMapping::num_planes() const {
  return scoped_mapping_.num_planes();
}

FrameBuffer::ScopedMapping::Plane GrallocFrameBuffer::ScopedMapping::plane(
    int planeIdx) const {
  CHECK(planeIdx >= 0 && planeIdx < scoped_mapping_.num_planes());
  auto plane = scoped_mapping_.plane(planeIdx);
  CHECK(plane.addr != nullptr);
  return plane;
}

bool GrallocFrameBuffer::ScopedMapping::is_valid() const {
  return scoped_mapping_.is_valid();
}

// static
std::unique_ptr<GrallocFrameBuffer> GrallocFrameBuffer::Wrap(
    buffer_handle_t buffer, Size size) {
  auto frame_buffer = base::WrapUnique(new GrallocFrameBuffer());
  if (!frame_buffer->Initialize(buffer, size)) {
    return nullptr;
  }
  return frame_buffer;
}

// static
std::unique_ptr<GrallocFrameBuffer> GrallocFrameBuffer::Create(
    Size size, android_pixel_format_t hal_format) {
  auto frame_buffer = base::WrapUnique(new GrallocFrameBuffer());
  if (!frame_buffer->Initialize(size, hal_format)) {
    return nullptr;
  }
  return frame_buffer;
}

GrallocFrameBuffer::GrallocFrameBuffer()
    : buffer_manager_(CameraBufferManager::GetInstance()) {}

bool GrallocFrameBuffer::Initialize(buffer_handle_t buffer, Size size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (buffer_manager_ == nullptr) {
    LOGF(ERROR) << "Buffer manager instance is null";
    return false;
  }

  int ret = buffer_manager_->Register(buffer);
  if (ret != 0) {
    LOGF(ERROR) << "Failed to register buffer";
    return false;
  }

  buffer_ = buffer;
  size_ = size;
  fourcc_ = buffer_manager_->GetV4L2PixelFormat(buffer_);
  if (fourcc_ == 0) {
    LOGF(ERROR) << "Failed to get V4L2 pixel format";
    return false;
  }
  uint32_t num_planes = buffer_manager_->GetNumPlanes(buffer_);
  if (num_planes == 0) {
    LOGF(ERROR) << "Failed to get number of planes";
    return false;
  }

  return true;
}

bool GrallocFrameBuffer::Initialize(Size size,
                                    android_pixel_format_t hal_format) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  uint32_t hal_usage =
      GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;

  uint32_t stride;
  int ret = buffer_manager_->Allocate(size.width, size.height, hal_format,
                                      hal_usage, &buffer_, &stride);
  if (ret) {
    LOGF(ERROR) << "Failed to allocate buffer";
    return false;
  }

  is_buffer_owned_ = true;
  size_ = size;
  fourcc_ = buffer_manager_->GetV4L2PixelFormat(buffer_);
  if (fourcc_ == 0) {
    LOGF(ERROR) << "Failed to get V4L2 pixel format";
    return false;
  }
  uint32_t num_planes = buffer_manager_->GetNumPlanes(buffer_);
  if (num_planes == 0) {
    LOGF(ERROR) << "Failed to get number of planes";
    return false;
  }

  return true;
}

GrallocFrameBuffer::~GrallocFrameBuffer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (buffer_ == nullptr) {
    return;
  }

  if (is_buffer_owned_) {
    int ret = buffer_manager_->Free(buffer_);
    if (ret != 0) {
      LOGF(ERROR) << "Failed to free buffer";
    }
  } else {
    int ret = buffer_manager_->Deregister(buffer_);
    if (ret != 0) {
      LOGF(ERROR) << "Failed to unregister buffer";
    }
  }
}

std::unique_ptr<FrameBuffer::ScopedMapping> GrallocFrameBuffer::Map() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return ScopedMapping::Create(buffer_);
}

// static
std::unique_ptr<GrallocFrameBuffer> GrallocFrameBuffer::Resize(
    FrameBuffer& buffer, Size size) {
  if (buffer.GetFourcc() != V4L2_PIX_FMT_NV12) {
    LOGF(WARNING) << "Only V4L2_PIX_FMT_NV12 is supported for resize";
    return nullptr;
  }

  auto mapped_buffer = buffer.Map();
  if (mapped_buffer == nullptr) {
    LOGF(WARNING) << "Failed to map temporary buffer";
    return nullptr;
  }

  auto y_plane = mapped_buffer->plane(0);
  auto uv_plane = mapped_buffer->plane(1);
  DCHECK(y_plane.addr != nullptr);
  DCHECK(uv_plane.addr != nullptr);

  auto output_buffer =
      GrallocFrameBuffer::Create(size, HAL_PIXEL_FORMAT_YCbCr_420_888);
  if (output_buffer == nullptr) {
    LOGF(WARNING) << "Failed to create buffer";
    return nullptr;
  }

  auto mapped_output_buffer = output_buffer->Map();
  if (mapped_output_buffer == nullptr) {
    LOGF(WARNING) << "Failed to map buffer";
    return nullptr;
  }

  auto output_y_plane = mapped_output_buffer->plane(0);
  auto output_uv_plane = mapped_output_buffer->plane(1);
  DCHECK(output_y_plane.addr != nullptr);
  DCHECK(output_uv_plane.addr != nullptr);

  // TODO(pihsun): Support "object-fit" for different scaling method.
  int ret = libyuv::NV12Scale(
      y_plane.addr, y_plane.stride, uv_plane.addr, uv_plane.stride,
      buffer.GetSize().width, buffer.GetSize().height, output_y_plane.addr,
      output_y_plane.stride, output_uv_plane.addr, output_uv_plane.stride,
      size.width, size.height, libyuv::kFilterBilinear);
  if (ret != 0) {
    LOGF(WARNING) << "NV12Scale() failed with " << ret;
    return nullptr;
  }

  return output_buffer;
}

}  // namespace cros
