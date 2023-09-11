// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier-util.h"
#include "sommelier.h"          // NOLINT(build/include_directory)
#include "sommelier-tracing.h"  // NOLINT(build/include_directory)

#include <assert.h>
#include <gbm.h>
#include <libdrm/drm_fourcc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <xf86drm.h>
#include <cstdlib>

#include "compositor/sommelier-dma-buf.h"
#include "virtualization/linux-headers/virtgpu_drm.h"  // NOLINT(build/include_directory)

#include "linux-dmabuf-unstable-v1-client-protocol.h"  // NOLINT(build/include_directory)
#include "linux-dmabuf-unstable-v1-server-protocol.h"  // NOLINT(build/include_directory)

#define SL_DMABUF_MAX_PLANES 4

struct sl_host_linux_dmabuf_feedback;

struct sl_host_linux_dmabuf {
  struct sl_context* ctx;
  uint32_t version;
  struct wl_resource* resource;
  struct zwp_linux_dmabuf_v1* linux_dmabuf_proxy;
  struct wl_callback* callback;
};

struct sl_dmabuf_plane {
  bool set;
  int fd;
  uint32_t offset;
  uint32_t stride;
  uint32_t modifier_hi;
  uint32_t modifier_lo;
};

struct sl_host_linux_buffer_params {
  struct sl_host_linux_dmabuf* host_linux_dmabuf;
  struct wl_resource* resource;
  struct wl_client *client;
  struct zwp_linux_buffer_params_v1* proxy;
  int32_t width;
  int32_t height;
  uint32_t format;
  uint32_t flags;
  struct sl_dmabuf_plane planes[SL_DMABUF_MAX_PLANES];
  struct sl_host_buffer *host_buffer;
};

static void sl_linux_dmabuf_destroy(struct wl_client *client,
                                    struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void sl_linux_buffer_params_v1_destroy(struct wl_client *client,
                                       struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void sl_linux_buffer_params_v1_add(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t fd,
                                   uint32_t plane_idx,
                                   uint32_t offset,
                                   uint32_t stride,
                                   uint32_t modifier_hi,
                                   uint32_t modifier_lo) {
  struct sl_host_linux_buffer_params *host =
      static_cast<sl_host_linux_buffer_params*>(
          wl_resource_get_user_data(resource));
  struct sl_dmabuf_plane *plane = &host->planes[plane_idx];
  struct drm_prime_handle prime_handle;
  int ret = 0;
  int drm_fd = gbm_device_get_fd(host->host_linux_dmabuf->ctx->gbm);

  if (plane_idx >= SL_DMABUF_MAX_PLANES) {
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                           "plane index out of bound: %u >= %u",
                           plane_idx, SL_DMABUF_MAX_PLANES);
  }

  if (host->planes[plane_idx].set) {
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                           "plane index already set: %u", plane_idx);
  }
  memset(&prime_handle, 0, sizeof(prime_handle));
  prime_handle.fd = fd;
  ret = drmIoctl(drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime_handle);
  if (!ret) {
    struct drm_virtgpu_resource_info_cros info_arg;
    struct drm_gem_close gem_close;

    // Then attempts to get resource information. This will fail silently if
    // the drm device passed to sommelier is not a virtio-gpu device.
    memset(&info_arg, 0, sizeof(info_arg));
    info_arg.bo_handle = prime_handle.handle;
    info_arg.type = VIRTGPU_RESOURCE_INFO_TYPE_EXTENDED;
    ret = drmIoctl(drm_fd, DRM_IOCTL_VIRTGPU_RESOURCE_INFO_CROS, &info_arg);
    // Correct stride0 if we are able to get proper resource info.
    // If the fd is backed by a PRIME buffer, then it is the external's
    // duty to provide a reasonable stride, so we won't correct it.
    if (!ret) {
      if (info_arg.stride && info_arg.blob_mem != VIRTGPU_BLOB_MEM_PRIME) {
        stride = info_arg.stride;
        modifier_lo = info_arg.format_modifier & 0xffffffff;
        modifier_hi = info_arg.format_modifier >> 32;
      }
    }

    // Always close the handle we imported.
    memset(&gem_close, 0, sizeof(gem_close));
    gem_close.handle = prime_handle.handle;
    drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
  }
  plane->fd = fd;
  plane->offset = offset;
  plane->stride = stride;
  plane->modifier_lo = modifier_lo;
  plane->modifier_hi = modifier_hi;

  zwp_linux_buffer_params_v1_add(host->proxy, fd, plane_idx, offset, stride,
                                 modifier_hi, modifier_lo);
}

void sl_linux_buffer_params_v1_create(struct wl_client *client,
                                      struct wl_resource *resource,
                                      int32_t width,
                                      int32_t height,
                                      uint32_t format,
                                      uint32_t flags) {
  struct sl_host_linux_buffer_params *host =
      static_cast<sl_host_linux_buffer_params*>(
          wl_resource_get_user_data(resource));

  zwp_linux_buffer_params_v1_create(host->proxy, width, height, format, flags);
  host->width = width;
  host->height = height;
  host->format = format;
  host->flags = flags;
}

static void sl_fence_sync(struct sl_context* ctx,
                        struct sl_sync_point* sync_point) {
  int drm_fd = gbm_device_get_fd(ctx->gbm);
  struct drm_prime_handle prime_handle;
  int sync_file_fd;
  int ret;

  // Attempt to export a sync_file from prime buffer and wait explicitly.
  ret = sl_dmabuf_get_read_sync_file(sync_point->fd, sync_file_fd);
  if (!ret) {
    TRACE_EVENT("drm", "sl_drm_sync: sync_wait", "prime_fd", sync_point->fd);
    sl_dmabuf_sync_wait(sync_file_fd);
    close(sync_file_fd);
    return;
  }

  // Fallback to waiting on a virtgpu buffer's implicit fence.
  //
  // First imports the prime fd to a gem handle. This will fail if this
  // function was not passed a prime handle that can be imported by the drm
  // device given to sommelier.
  memset(&prime_handle, 0, sizeof(prime_handle));
  prime_handle.fd = sync_point->fd;
  TRACE_EVENT("drm", "sl_drm_sync: virtgpu_wait", "prime_fd", prime_handle.fd);
  ret = drmIoctl(drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime_handle);
  if (!ret) {
    struct drm_virtgpu_3d_wait wait_arg;
    struct drm_gem_close gem_close;

    // Then attempts to wait for GPU operations to complete. This will fail
    // silently if the drm device passed to sommelier is not a virtio-gpu
    // device.
    memset(&wait_arg, 0, sizeof(wait_arg));
    wait_arg.handle = prime_handle.handle;
    drmIoctl(drm_fd, DRM_IOCTL_VIRTGPU_WAIT, &wait_arg);

    // Always close the handle we imported.
    memset(&gem_close, 0, sizeof(gem_close));
    gem_close.handle = prime_handle.handle;
    drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
  }
}

void sl_linux_buffer_params_v1_create_immed(struct wl_client *client,
                                            struct wl_resource *resource,
                                            uint32_t buffer_id,
                                            int32_t width,
                                            int32_t height,
                                            uint32_t format,
                                            uint32_t flags) {
  struct sl_host_linux_buffer_params *host =
      static_cast<sl_host_linux_buffer_params*>(
          wl_resource_get_user_data(resource));
  host->width = width;
  host->height = height;
  host->format = format;
  host->flags = flags;
  struct wl_buffer *buffer = zwp_linux_buffer_params_v1_create_immed(
      host->proxy, width, height, format, flags);
  struct sl_host_buffer* host_buffer = sl_create_host_buffer(
      host->host_linux_dmabuf->ctx, client, buffer_id, buffer, width, height, true);
  host->host_buffer = host_buffer;
  host_buffer->sync_point = sl_sync_point_create(host->planes[0].fd);
  host_buffer->sync_point->sync = sl_fence_sync;
}

static const struct zwp_linux_buffer_params_v1_interface
sl_linux_buffer_params_v1_implementation = {
  .destroy = sl_linux_buffer_params_v1_destroy,
  .add = sl_linux_buffer_params_v1_add,
  .create = sl_linux_buffer_params_v1_create,
  .create_immed = sl_linux_buffer_params_v1_create_immed,
};


static void sl_destroy_host_linux_buffer_params_v1(struct wl_resource* resource) {
  struct sl_host_linux_buffer_params* host =
      static_cast<struct sl_host_linux_buffer_params*>(wl_resource_get_user_data(resource));
  for (int i = 0; i < SL_DMABUF_MAX_PLANES; ++i) {
    if (host->planes[i].set) close(host->planes[i].fd);
  }
  zwp_linux_buffer_params_v1_destroy(host->proxy);
  wl_resource_set_user_data(resource, NULL);
  free(host);
}

void sl_linux_buffer_params_v1_created(void *data,
    struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1,
    struct wl_buffer *buffer) {
  struct sl_host_linux_buffer_params *host =
      static_cast<sl_host_linux_buffer_params *>(
          zwp_linux_buffer_params_v1_get_user_data(zwp_linux_buffer_params_v1));
  host->host_buffer = sl_create_host_buffer(host->host_linux_dmabuf->ctx,
                                            host->client, 0, buffer,
                                            host->width, host->height, true);
  zwp_linux_buffer_params_v1_send_created(host->resource, host->host_buffer->resource);
}

void sl_linux_buffer_params_v1_failed(void *data,
    struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1) {
  struct sl_host_linux_buffer_params *host =
      static_cast<sl_host_linux_buffer_params *>(
          zwp_linux_buffer_params_v1_get_user_data(zwp_linux_buffer_params_v1));
  zwp_linux_buffer_params_v1_send_failed(host->resource);
}

static const struct zwp_linux_buffer_params_v1_listener
sl_linux_buffer_params_v1_listener = {
  sl_linux_buffer_params_v1_created,
  sl_linux_buffer_params_v1_failed,
};

static void sl_linux_dmabuf_create_params(struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t params_id) {
  struct sl_host_linux_dmabuf* host =
      static_cast<struct sl_host_linux_dmabuf*>(wl_resource_get_user_data(resource));
  struct sl_host_linux_buffer_params* host_linux_buffer_params =
      static_cast<struct sl_host_linux_buffer_params*>(
          malloc(sizeof(*host_linux_buffer_params)));
  assert(host_linux_buffer_params);
  memset(host_linux_buffer_params, 0, sizeof(*host_linux_buffer_params));
  host_linux_buffer_params->host_linux_dmabuf = host;
  host_linux_buffer_params->client = client;
  host_linux_buffer_params->resource = wl_resource_create(
      client, &zwp_linux_buffer_params_v1_interface, host->version, params_id);
  wl_resource_set_implementation(host_linux_buffer_params->resource,
                                 &sl_linux_buffer_params_v1_implementation,
                                 host_linux_buffer_params,
                                 sl_destroy_host_linux_buffer_params_v1);
  host_linux_buffer_params->proxy = zwp_linux_dmabuf_v1_create_params(
      host->ctx->linux_dmabuf->internal);
  zwp_linux_buffer_params_v1_set_user_data(host_linux_buffer_params->proxy,
                                           host_linux_buffer_params);
  zwp_linux_buffer_params_v1_add_listener(host_linux_buffer_params->proxy,
                                          &sl_linux_buffer_params_v1_listener,
                                          host_linux_buffer_params);
}

static const struct zwp_linux_dmabuf_v1_interface sl_linux_dmabuf_implementation = {
    sl_linux_dmabuf_destroy, sl_linux_dmabuf_create_params};

static void sl_destroy_host_linux_dmabuf(struct wl_resource* resource) {
  struct sl_host_linux_dmabuf* host =
      static_cast<sl_host_linux_dmabuf*>(wl_resource_get_user_data(resource));

  zwp_linux_dmabuf_v1_destroy(host->linux_dmabuf_proxy);
  wl_callback_destroy(host->callback);
  wl_resource_set_user_data(resource, nullptr);
  free(host);
}

static void sl_linux_dmabuf_format(void* data,
                                   struct zwp_linux_dmabuf_v1* linux_dmabuf,
                                   uint32_t format) {
  struct sl_host_linux_dmabuf* host = static_cast<sl_host_linux_dmabuf*>(
      zwp_linux_dmabuf_v1_get_user_data(linux_dmabuf));

  if (host->version < ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
    zwp_linux_dmabuf_v1_send_format(host->resource, format);
  }
}

static void sl_linux_dmabuf_modifier(void* data,
                                     struct zwp_linux_dmabuf_v1* linux_dmabuf,
                                     uint32_t format,
                                     uint32_t modifier_hi,
                                     uint32_t modifier_lo) {
  struct sl_host_linux_dmabuf* host = static_cast<sl_host_linux_dmabuf*>(
      zwp_linux_dmabuf_v1_get_user_data(linux_dmabuf));

  if (host->version < ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION ||
      host->version >= 4) {
    return;
  }

  if (!host->ctx->enable_linux_dmabuf_modifier) {
    uint64_t modifier = static_cast<uint64_t>(modifier_hi) << 32 | modifier_lo;
    if (format != DRM_FORMAT_RGB565 && format != DRM_FORMAT_ARGB8888 &&
        format != DRM_FORMAT_ABGR8888 && format != DRM_FORMAT_XRGB8888 &&
        format != DRM_FORMAT_XBGR8888) {
      printf("%s(): ignore format = %x\n", __func__, format);
      return;
    }

    if (modifier != DRM_FORMAT_MOD_LINEAR && modifier != DRM_FORMAT_MOD_INVALID) {
      printf("%s(): ignore nonlinear modifier = %lx\n", __func__, modifier);
      return;
    }
  }

  zwp_linux_dmabuf_v1_send_modifier(host->resource, format, modifier_hi, modifier_lo);
}

static const struct zwp_linux_dmabuf_v1_listener sl_linux_dmabuf_listener = {
    sl_linux_dmabuf_format, sl_linux_dmabuf_modifier};

static void sl_bind_host_linux_dmabuf(struct wl_client* client,
                                      void* data,
                                      uint32_t version,
                                      uint32_t id) {
  struct sl_context* ctx = (struct sl_context*)data;
  struct sl_host_linux_dmabuf* host =
      static_cast<sl_host_linux_dmabuf*>(malloc(sizeof(*host)));
  assert(host);
  host->ctx = ctx;
  host->version = MIN(version, 3);
  host->resource =
      wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, host->version, id);
  wl_resource_set_implementation(host->resource,
                                 &sl_linux_dmabuf_implementation, host,
                                 sl_destroy_host_linux_dmabuf);

  host->linux_dmabuf_proxy = static_cast<zwp_linux_dmabuf_v1*>(wl_registry_bind(
      wl_display_get_registry(ctx->display), ctx->linux_dmabuf->id,
      &zwp_linux_dmabuf_v1_interface, ctx->linux_dmabuf->version));
  zwp_linux_dmabuf_v1_set_user_data(host->linux_dmabuf_proxy, host);
  zwp_linux_dmabuf_v1_add_listener(host->linux_dmabuf_proxy,
                                   &sl_linux_dmabuf_listener, host);
}

struct sl_global* sl_linux_dmabuf_global_create(struct sl_context* ctx) {
  assert(ctx->linux_dmabuf);
  assert(ctx->linux_dmabuf->version >= 3);
  return sl_global_create(ctx, &zwp_linux_dmabuf_v1_interface,
                          ctx->linux_dmabuf->version, ctx,
                          sl_bind_host_linux_dmabuf);
}
