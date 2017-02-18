/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "arc/camera_buffer_mapper.h"

#include <sys/mman.h>

#include <functional>

#include <base/at_exit.h>
#include <drm_fourcc.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/camera_buffer_handle.h"
#include "common/camera_buffer_mapper_internal.h"

// Dummy objects / values used for testing.
struct gbm_device {
  void* dummy;
} dummy_device;

struct gbm_bo {
  void* dummy;
} dummy_bo;

int dummy_fd = 0xdeadbeef;

void* dummy_addr = reinterpret_cast<void*>(0xbeefdead);

// Stubs for global scope mock functions.
static std::function<int(int fd)> _close;
static std::function<struct gbm_device*()> _create_gbm_device;
static std::function<int(struct gbm_device*)> _gbm_device_get_fd;
static std::function<void(struct gbm_device*)> _gbm_device_destroy;
static std::function<struct gbm_bo*(struct gbm_device* device,
                                    uint32_t type,
                                    void* buffer,
                                    uint32_t usage)>
    _gbm_bo_import;
static std::function<void*(struct gbm_bo* bo,
                           uint32_t x,
                           uint32_t y,
                           uint32_t width,
                           uint32_t height,
                           uint32_t flags,
                           uint32_t* stride,
                           void** map_data,
                           size_t plane)>
    _gbm_bo_map;
static std::function<void(struct gbm_bo* bo, void* map_data)> _gbm_bo_unmap;
static std::function<void(struct gbm_bo* bo)> _gbm_bo_destroy;

// Implementations of the mock functions.
struct MockGbm {
  MockGbm() {
    EXPECT_EQ(_close, nullptr);
    _close = [this](int fd) { return Close(fd); };

    EXPECT_EQ(_create_gbm_device, nullptr);
    _create_gbm_device = [this]() { return CreateGbmDevice(); };

    EXPECT_EQ(_gbm_device_get_fd, nullptr);
    _gbm_device_get_fd = [this](struct gbm_device* device) {
      return GbmDeviceGetFd(device);
    };

    EXPECT_EQ(_gbm_device_destroy, nullptr);
    _gbm_device_destroy = [this](struct gbm_device* device) {
      GbmDeviceDestroy(device);
    };

    EXPECT_EQ(_gbm_bo_import, nullptr);
    _gbm_bo_import = [this](struct gbm_device* device, uint32_t type,
                            void* buffer, uint32_t usage) {
      return GbmBoImport(device, type, buffer, usage);
    };

    EXPECT_EQ(_gbm_bo_map, nullptr);
    _gbm_bo_map = [this](struct gbm_bo* bo, uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height, uint32_t flags,
                         uint32_t* stride, void** map_data, size_t plane) {
      return GbmBoMap(bo, x, y, width, height, flags, stride, map_data, plane);
    };

    EXPECT_EQ(_gbm_bo_unmap, nullptr);
    _gbm_bo_unmap = [this](struct gbm_bo* bo, void* map_data) {
      GbmBoUnmap(bo, map_data);
    };

    EXPECT_EQ(_gbm_bo_destroy, nullptr);
    _gbm_bo_destroy = [this](struct gbm_bo* bo) { GbmBoDestroy(bo); };
  }

  MOCK_METHOD1(Close, int(int fd));
  MOCK_METHOD0(CreateGbmDevice, struct gbm_device*());
  MOCK_METHOD1(GbmDeviceGetFd, int(struct gbm_device* device));
  MOCK_METHOD1(GbmDeviceDestroy, void(struct gbm_device* device));
  MOCK_METHOD4(GbmBoImport,
               struct gbm_bo*(struct gbm_device* device,
                              uint32_t type,
                              void* buffer,
                              uint32_t usage));
  MOCK_METHOD9(GbmBoMap,
               void*(struct gbm_bo* bo,
                     uint32_t x,
                     uint32_t y,
                     uint32_t width,
                     uint32_t height,
                     uint32_t flags,
                     uint32_t* stride,
                     void** map_data,
                     size_t plane));
  MOCK_METHOD2(GbmBoUnmap, void(struct gbm_bo* bo, void* map_data));
  MOCK_METHOD1(GbmBoDestroy, void(struct gbm_bo* bo));
};

// global scope mock functions. These functions indirectly invoke the mock
// function implementations through the stubs.
int close(int fd) {
  return _close(fd);
}

struct gbm_device* ::arc::internal::CreateGbmDevice() {
  return _create_gbm_device();
}

int gbm_device_get_fd(struct gbm_device* device) {
  return _gbm_device_get_fd(device);
}

void gbm_device_destroy(struct gbm_device* device) {
  return _gbm_device_destroy(device);
}

struct gbm_bo* gbm_bo_import(struct gbm_device* device,
                             uint32_t type,
                             void* buffer,
                             uint32_t usage) {
  return _gbm_bo_import(device, type, buffer, usage);
}

void* gbm_bo_map(struct gbm_bo* bo,
                 uint32_t x,
                 uint32_t y,
                 uint32_t width,
                 uint32_t height,
                 uint32_t flags,
                 uint32_t* stride,
                 void** map_data,
                 size_t plane) {
  return _gbm_bo_map(bo, x, y, width, height, flags, stride, map_data, plane);
}

void gbm_bo_unmap(struct gbm_bo* bo, void* map_data) {
  return _gbm_bo_unmap(bo, map_data);
}

void gbm_bo_destroy(struct gbm_bo* bo) {
  return _gbm_bo_destroy(bo);
}

namespace arc {

namespace tests {

using ::testing::A;
using ::testing::Return;

class CameraBufferMapperTest : public ::testing::Test {
 public:
  CameraBufferMapperTest() = default;

  void SetUp() {
    EXPECT_CALL(gbm_, CreateGbmDevice())
        .Times(1)
        .WillOnce(Return(&dummy_device));
    cbm_ = new CameraBufferMapper();
  }

  void TearDown() {
    // Verify that gbm_device is properly tear down.
    EXPECT_CALL(gbm_, GbmDeviceGetFd(&dummy_device))
        .Times(1)
        .WillOnce(Return(dummy_fd));
    EXPECT_CALL(gbm_, Close(dummy_fd)).Times(1);
    EXPECT_CALL(gbm_, GbmDeviceDestroy(&dummy_device)).Times(1);
    delete cbm_;
    EXPECT_EQ(::testing::Mock::VerifyAndClear(&gbm_), true);
  }

 protected:
  CameraBufferMapper* cbm_;

  MockGbm gbm_;

 private:
  DISALLOW_COPY_AND_ASSIGN(CameraBufferMapperTest);
};

TEST_F(CameraBufferMapperTest, SimpleTest) {
  // Create a dummy buffer.
  camera_buffer_handle_t buffer;
  buffer_handle_t handle = reinterpret_cast<buffer_handle_t>(&buffer);
  memset(&buffer, 0, sizeof(buffer));
  buffer.fds[0] = dummy_fd;
  buffer.magic = kCameraBufferMagic;
  buffer.buffer_id = 1;
  buffer.type = GRALLOC;
  buffer.format = DRM_FORMAT_ABGR8888;
  buffer.width = 1280;
  buffer.height = 720;
  buffer.strides[0] = 1280;
  buffer.offsets[0] = 0;

  // Register the buffer.
  EXPECT_CALL(gbm_, GbmBoImport(&dummy_device, A<uint32_t>(), A<void*>(),
                                A<uint32_t>()))
      .Times(1)
      .WillOnce(Return(&dummy_bo));
  EXPECT_EQ(cbm_->Register(handle), 0);

  // The call to Map |handle| should fail due to invalid width and height.
  uint32_t stride1;
  EXPECT_EQ(cbm_->Map(handle, 0, 0, 0, 1920, 1080, 0, &stride1), MAP_FAILED);

  // Now the call to Map |handle| should succeed with valid width and height.
  EXPECT_CALL(gbm_,
              GbmBoMap(&dummy_bo, 0, 0, 1280, 720, 0, &stride1, A<void**>(), 0))
      .Times(1)
      .WillOnce(Return(dummy_addr));
  EXPECT_EQ(cbm_->Map(handle, 0, 0, 0, 1280, 720, 0, &stride1), dummy_addr);

  // And the call to Unmap on |handle| should also succeed.
  EXPECT_CALL(gbm_, GbmBoUnmap(&dummy_bo, A<void*>())).Times(1);
  EXPECT_EQ(cbm_->Unmap(handle, 0), 0);

  // Now let's Map |handle| twice.
  EXPECT_CALL(gbm_,
              GbmBoMap(&dummy_bo, 0, 0, 1280, 720, 0, &stride1, A<void**>(), 0))
      .Times(1)
      .WillOnce(Return(dummy_addr));
  EXPECT_EQ(cbm_->Map(handle, 0, 0, 0, 1280, 720, 0, &stride1), dummy_addr);
  EXPECT_CALL(gbm_,
              GbmBoMap(&dummy_bo, 0, 0, 1280, 720, 0, &stride1, A<void**>(), 0))
      .Times(1)
      .WillOnce(Return(dummy_addr));
  EXPECT_EQ(cbm_->Map(handle, 0, 0, 0, 1280, 720, 0, &stride1), dummy_addr);

  // ...And just Unmap |handle| once.
  EXPECT_CALL(gbm_, GbmBoUnmap(&dummy_bo, A<void*>())).Times(1);
  EXPECT_EQ(cbm_->Unmap(handle, 0), 0);

  // Finally the bo for |handle| should be unmapped and destroyed when we
  // unregister the buffer.
  EXPECT_CALL(gbm_, GbmBoUnmap(&dummy_bo, A<void*>())).Times(1);
  EXPECT_CALL(gbm_, GbmBoDestroy(&dummy_bo)).Times(1);
  EXPECT_EQ(cbm_->Deregister(handle), 0);
}

}  // namespace tests

}  // namespace arc

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
