// Copyright 2018 The ChromiumOS Authors
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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <xf86drm.h>
#include <cstdint>
#include <cstdlib>
#include <map>

#include "virtualization/linux-headers/virtgpu_drm.h"  // NOLINT(build/include_directory)

#include "linux-dmabuf-unstable-v1-client-protocol.h"  // NOLINT(build/include_directory)
#include "linux-dmabuf-unstable-v1-server-protocol.h"  // NOLINT(build/include_directory)

struct sl_host_linux_dmabuf_feedback;

struct sl_host_linux_dmabuf {
  struct sl_context* ctx;
  // The version that client wants to bind.
  uint32_t version;
  struct wl_resource* resource;
  struct wl_client* client;
  uint32_t id;
  
  // Proxy data
  struct zwp_linux_dmabuf_v1* linux_dmabuf_proxy;
  uint32_t* formats;
  uint64_t* modifiers;
  int format_count;
  int format_capability;
  bool format_complete;
  struct wl_callback* callback;
};

struct sl_dmabuf_plane {
  int32_t fd;
  uint32_t plane_idx;
  uint32_t offset;
  uint32_t stride;
  uint32_t modifier_hi;
  uint32_t modifier_lo;
  struct sl_dmabuf_plane *next;
};

struct sl_host_linux_buffer_params {
  struct sl_host_linux_dmabuf* host_linux_dmabuf;
  struct wl_resource* resource;
  struct wl_client *client;
  int32_t width;
  int32_t height;
  uint32_t format;
  uint32_t flags;
  struct sl_dmabuf_plane *plane_list;
  struct sl_host_buffer *host_buffer;

  // Proxy data
  struct zwp_linux_buffer_params_v1* proxy;
};

struct sl_host_linux_dmabuf_feedback {
  struct sl_host_linux_dmabuf* host_linux_dmabuf;
  struct wl_resource* resource;
  struct wl_client *client;

  // Proxy data
  struct zwp_linux_dmabuf_feedback_v1* proxy;
};


static void sl_linux_dmabuf_destroy(struct wl_client *client,
			                              struct wl_resource *resource) {
  struct sl_host_linux_dmabuf* host =
      static_cast<struct sl_host_linux_dmabuf*>(wl_resource_get_user_data(resource));
  wl_resource_destroy(resource);
}

static void sl_linux_buffer_params_v1_destroy(struct wl_client *client,
                                       struct wl_resource *resource) {
  struct sl_host_linux_buffer_params *host =
      static_cast<sl_host_linux_buffer_params*>(
          wl_resource_get_user_data(resource));
  wl_resource_destroy(resource);
}

static void sl_linux_buffer_params_v1_add(struct wl_client *client,
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
  struct sl_dmabuf_plane *plane = static_cast<sl_dmabuf_plane*>(malloc(sizeof(*plane)));
  struct drm_prime_handle prime_handle;
  int ret = 0;
  int drm_fd = gbm_device_get_fd(host->host_linux_dmabuf->ctx->gbm);

  assert(plane);
  printf("%s(): modifier = %lx\n", __func__, (uint64_t) modifier_hi << 32 | modifier_lo);

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
      }
    }

    // Always close the handle we imported.
    memset(&gem_close, 0, sizeof(gem_close));
    gem_close.handle = prime_handle.handle;
    drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
  }
  plane->next = host->plane_list;
  plane->fd = fd;
  plane->offset = offset;
  plane->stride = stride;
  plane->modifier_lo = modifier_lo;
  plane->modifier_hi = modifier_hi;
  host->plane_list = plane;

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
  printf("%s(): format = %x\n", __func__, format);

  zwp_linux_buffer_params_v1_create(host->proxy, width, height, format, flags);
  host->width = width;
  host->height = height;
  host->format = format;
  host->flags = flags;
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
  printf("%s(): format = %x\n", __func__, format);
  host->width = width;
  host->height = height;
  host->format = format;
  host->flags = flags;
  struct wl_buffer *buffer = zwp_linux_buffer_params_v1_create_immed(
      host->proxy, width, height, format, flags);
  struct sl_host_buffer* host_buffer = sl_create_host_buffer(
      host->host_linux_dmabuf->ctx, client, buffer_id, buffer, width, height, true);
  host->host_buffer = host_buffer;
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
  struct sl_dmabuf_plane *plane = host->plane_list;
  while (plane) {
    host->plane_list = plane->next;
    close(plane->fd);
    free(plane);
    plane = host->plane_list;
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

  host_linux_buffer_params->host_linux_dmabuf = host;
  host_linux_buffer_params->client = client;
  host_linux_buffer_params->plane_list = nullptr;
  host_linux_buffer_params->resource = wl_resource_create(
      client, &zwp_linux_buffer_params_v1_interface, host->version, params_id);
  wl_resource_set_user_data(host_linux_buffer_params->resource, host_linux_buffer_params);
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

static bool devid_from_fd(int fd, dev_t *devid) {
	struct stat stat;
	if (fstat(fd, &stat) != 0) {
		fprintf(stderr, "fstat failed\n");
		return false;
	}
	*devid = stat.st_rdev;
	return true;
}


static void sl_linux_dmabuf_feedback_destroy(struct wl_client *client,
			                              struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

const struct zwp_linux_dmabuf_feedback_v1_interface  sl_linux_dmabuf_feedback_v1_implementation = {
  .destroy = sl_linux_dmabuf_feedback_destroy,
};

static void sl_destroy_host_linux_dmabuf_feedback(struct wl_resource* resource) {
  struct sl_host_linux_dmabuf_feedback* feedback =
      static_cast<sl_host_linux_dmabuf_feedback*>(wl_resource_get_user_data(resource));

  printf("%s(): desctroy feedback\n", __func__);
  wl_resource_set_user_data(resource, nullptr);
  free(feedback);
}

static void send_format_table(struct sl_host_linux_dmabuf_feedback *feedback,
                              uint32_t* &formats,
                              uint64_t* &modifiers,
                              int format_count) {
  printf("%s(): format_count = %d\n", __func__, format_count);
  int fd = memfd_create("format_table", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  struct {
    uint32_t format;
    uint32_t padding;
    uint64_t modifier;
  } format_table_item = {0};
  if (fd == -1) {
    perror("failed to craete memfd");
  }
  for (int i = 0; i < format_count; ++i) {
    format_table_item.format = formats[i];
    format_table_item.modifier = modifiers[i];
    write(fd, &format_table_item, sizeof(format_table_item));
  }
  unsigned int seals = F_SEAL_SEAL | F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_GROW;
  fcntl(fd, F_ADD_SEALS, seals);
  if (fd == -1) {
    perror("failed to seal fd");
  }
  zwp_linux_dmabuf_feedback_v1_send_format_table(
      feedback->resource, fd, format_count * sizeof(format_table_item));

  close(fd);
}

static int advertise_feedback_event(void* data) {
  struct sl_host_linux_dmabuf_feedback* feedback =
      static_cast<struct sl_host_linux_dmabuf_feedback*>(data);
  struct sl_host_linux_dmabuf* host = feedback->host_linux_dmabuf;
  struct sl_context* ctx = host->ctx;
  struct wl_event_source* timer_event;

  printf("%s(): send event\n", __func__);
  // Send main device
  dev_t devid;
  if (!devid_from_fd(ctx->virtio_gpu_fd, &devid)) {
    if (!devid_from_fd(ctx->render_gpu_fd, &devid)) {
      fprintf(stderr, "failed to get any devid\n");
      exit(EXIT_FAILURE);
    }
  }
  wl_array devid_array = {
    .size = sizeof(devid),
    .data = static_cast<void *>(&devid),
  };
  zwp_linux_dmabuf_feedback_v1_send_main_device(feedback->resource,
                                                &devid_array);

  int format_count = host->format_count;
  send_format_table(feedback, host->formats, host->modifiers, format_count);  
  zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(feedback->resource,
                                                          &devid_array);

  zwp_linux_dmabuf_feedback_v1_send_tranche_flags(feedback->resource, 0);
  wl_array formats_array;
  wl_array_init(&formats_array);
  wl_array_add(&formats_array, format_count * sizeof(uint16_t));
  uint16_t* indices = static_cast<uint16_t *>(formats_array.data);
  for (uint16_t i = 0; i < format_count; ++i) {
    indices[i] = i;
  }
  zwp_linux_dmabuf_feedback_v1_send_tranche_formats(feedback->resource, &formats_array);
  wl_array_release(&formats_array);

  zwp_linux_dmabuf_feedback_v1_send_tranche_done(feedback->resource);
  zwp_linux_dmabuf_feedback_v1_send_done(feedback->resource);
  
  return 0;
}

static void sl_linux_dmabuf_get_default_feedback(struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id) {
  struct sl_host_linux_dmabuf *host =
      static_cast<struct sl_host_linux_dmabuf*>(wl_resource_get_user_data(resource));
  struct sl_context* ctx = host->ctx;
  struct sl_host_linux_dmabuf_feedback* feedback =
      static_cast<struct sl_host_linux_dmabuf_feedback*>(
          malloc(sizeof(feedback)));
  assert(feedback);

  feedback->host_linux_dmabuf = host;
  feedback->client = client;
  feedback->resource = wl_resource_create(
      client, &zwp_linux_dmabuf_feedback_v1_interface, host->version, id);
  wl_resource_set_user_data(feedback->resource, feedback);
  wl_resource_set_implementation(feedback->resource,
                                 &sl_linux_dmabuf_feedback_v1_implementation,
                                 feedback,
                                 sl_destroy_host_linux_dmabuf_feedback);
  advertise_feedback_event(feedback);
}

static void sl_linux_dmabuf_get_surface_feedback(struct wl_client *client,
				                                  struct wl_resource *resource,
				                                  uint32_t id,
				                                  struct wl_resource *surface) {
  sl_linux_dmabuf_get_default_feedback(client, resource, id);
}

static const struct zwp_linux_dmabuf_v1_interface sl_linux_dmabuf_implementation = {
    sl_linux_dmabuf_destroy,
    sl_linux_dmabuf_create_params,
    sl_linux_dmabuf_get_default_feedback,
    sl_linux_dmabuf_get_surface_feedback};

static void sl_destroy_host_linux_dmabuf(struct wl_resource* resource) {
  struct sl_host_linux_dmabuf* host =
      static_cast<sl_host_linux_dmabuf*>(wl_resource_get_user_data(resource));

  zwp_linux_dmabuf_v1_destroy(host->linux_dmabuf_proxy);
  wl_callback_destroy(host->callback);
  wl_resource_set_user_data(resource, nullptr);
  free(host->formats);
  free(host->modifiers);
  free(host);
}

static void sl_linux_dmabuf_format(void* data,
                          struct zwp_linux_dmabuf_v1* linux_dmabuf,
                          uint32_t format) {
  // This event is deprecated since version 3.  Just ignore it.
}

static void sl_linux_dmabuf_modifier(void* data,
                            struct zwp_linux_dmabuf_v1* linux_dmabuf,
                            uint32_t format,
                            uint32_t modifier_hi,
                            uint32_t modifier_lo) {
  struct sl_host_linux_dmabuf* host = static_cast<sl_host_linux_dmabuf*>(
      zwp_linux_dmabuf_v1_get_user_data(linux_dmabuf));
  uint64_t modifier = static_cast<uint64_t>(modifier_hi) << 32 | modifier_lo; 
  if (format != DRM_FORMAT_RGB565 && format != DRM_FORMAT_ARGB8888 &&
      format != DRM_FORMAT_ABGR8888 && format != DRM_FORMAT_XRGB8888 &&
      format != DRM_FORMAT_XBGR8888) {
    printf("%s(): ignore format = %x\n", __func__, format);
    return;
  }

  if (modifier != DRM_FORMAT_MOD_LINEAR) {
    printf("%s(): ignore nonlinear modifier = %lx\n", __func__, modifier);
    return;
  }
  printf("%s(): advertise format = %x, modifier = %lx\n", __func__, format, modifier);
  if (host->format_count >= host->format_capability) {
    host->format_capability *= 2;
    host->formats = (uint32_t*) realloc(host->formats, host->format_capability * sizeof(host->formats[0]));
    host->modifiers = (uint64_t*) realloc(host->modifiers, host->format_capability * sizeof(host->modifiers[0]));
  }
  host->formats[host->format_count] = format;
  host->modifiers[host->format_count] = modifier;
  host->format_count++;

  if (host->version < ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION) {
    zwp_linux_dmabuf_v1_send_modifier(host->resource, format, modifier_hi, modifier_lo);
  }
}

static const struct zwp_linux_dmabuf_v1_listener sl_linux_dmabuf_listener = {
    sl_linux_dmabuf_format,
    sl_linux_dmabuf_modifier};

static void sl_linux_dmabuf_callback_done(void* data,
                                 struct wl_callback* callback,
                                 uint32_t serial) {
  struct sl_host_linux_dmabuf* host =
      static_cast<sl_host_linux_dmabuf*>(wl_callback_get_user_data(callback));
  host->format_complete = true;
}

static const struct wl_callback_listener sl_linux_dmabuf_callback_listener = {
    sl_linux_dmabuf_callback_done};


static void sl_bind_host_linux_dmabuf(struct wl_client* client,
                                      void* data,
                                      uint32_t version,
                                      uint32_t id) {
  struct sl_context* ctx = (struct sl_context*)data;
  struct sl_host_linux_dmabuf* host =
      static_cast<sl_host_linux_dmabuf*>(malloc(sizeof(*host)));
  assert(host);

  host->ctx = ctx;
  host->version = version;
  host->client = client;
  host->id = id;
  host->format_capability = 256;
  host->format_complete = false;
  host->formats = (uint32_t*) malloc(host->format_capability * sizeof(host->formats[0]));
  host->modifiers = (uint64_t*) malloc(host->format_capability * sizeof(host->modifiers[0]));
  host->format_count = 0;
  // TODO We bind the host Linux DMA-BUF object with version up to only 3
  // because in version 4, the format_table event of feedback object includes
  // a fd referencing a share memory region in host, which we have no way to
  // access right now.  However, this won't be a big problem since we only
  // care about formats and modifiers compatable with the host compositor and
  // version 3 of Linux DMA-BUF interface should provide these.
  host->linux_dmabuf_proxy = static_cast<zwp_linux_dmabuf_v1*>(wl_registry_bind(
      wl_display_get_registry(ctx->display), ctx->linux_dmabuf->id,
      &zwp_linux_dmabuf_v1_interface, 3));
  zwp_linux_dmabuf_v1_set_user_data(host->linux_dmabuf_proxy, host);
  zwp_linux_dmabuf_v1_add_listener(host->linux_dmabuf_proxy,
                                   &sl_linux_dmabuf_listener, host);

  host->callback = wl_display_sync(ctx->display);
  wl_callback_set_user_data(host->callback, host);
  wl_callback_add_listener(host->callback, &sl_linux_dmabuf_callback_listener, host);

  host->resource =
      wl_resource_create(host->client, &zwp_linux_dmabuf_v1_interface, host->version, host->id);
  wl_resource_set_implementation(host->resource,
                                 &sl_linux_dmabuf_implementation, host,
                                 sl_destroy_host_linux_dmabuf);

}

struct sl_global* sl_linux_dmabuf_global_create(struct sl_context* ctx) {
  assert(ctx->linux_dmabuf);
  assert(ctx->linux_dmabuf->version >= 3);
  return sl_global_create(ctx, &zwp_linux_dmabuf_v1_interface,
                          ctx->linux_dmabuf->version, ctx,
                          sl_bind_host_linux_dmabuf);
}
