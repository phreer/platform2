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
#include <unistd.h>
#include <wayland-server-core.h>
#include <xf86drm.h>

#include "virtualization/linux-headers/virtgpu_drm.h"  // NOLINT(build/include_directory)

#include "linux-dmabuf-unstable-v1-client-protocol.h"  // NOLINT(build/include_directory)
#include "linux-dmabuf-unstable-v1-server-protocol.h"  // NOLINT(build/include_directory)

struct sl_host_linux_dmabuf {
  struct sl_context* ctx;
  uint32_t version;
  struct wl_resource* resource;
  struct zwp_linux_dmabuf_v1* linux_dmabuf_proxy;
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
  struct zwp_linux_buffer_params_v1* proxy;
  int32_t width;
  int32_t height;
  uint32_t format;
  uint32_t flags;
  struct sl_dmabuf_plane *plane_list;
  struct sl_host_buffer *host_buffer;
};

static void sl_linux_dmabuf_destroy(struct wl_client *client,
			                              struct wl_resource *resource) {}

void sl_linux_buffer_params_v1_destroy(struct wl_client *client,
                                       struct wl_resource *resource) {}
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
  zwp_linux_buffer_params_v1_add(host->proxy, fd, plane_idx, offset, stride,
                                 modifier_hi, modifier_lo);
  struct sl_dmabuf_plane *plane = static_cast<sl_dmabuf_plane*>(malloc(sizeof(*plane)));
  assert(plane);
  plane->next = host->plane_list;
  plane->fd = fd;
  plane->offset = offset;
  plane->stride = stride;
  plane->modifier_lo = modifier_lo;
  plane->modifier_hi = modifier_hi;
  host->plane_list = plane;
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
      host->host_linux_dmabuf->ctx, client, buffer_id, buffer, width, height);
  host->host_buffer = host_buffer;
}

static const struct zwp_linux_buffer_params_v1_interface
sl_linux_buffer_params_v1_implementation = {
  sl_linux_buffer_params_v1_destroy,
  sl_linux_buffer_params_v1_add,
  sl_linux_buffer_params_v1_create,
  sl_linux_buffer_params_v1_create_immed,
};


static void sl_destroy_host_linux_buffer_params_v1(struct wl_resource* resource) {
  struct sl_host_linux_buffer_params* host =
      static_cast<struct sl_host_linux_buffer_params*>(wl_resource_get_user_data(resource));
  struct sl_dmabuf_plane *plane = host->plane_list;
  while (plane) {
    host->plane_list = plane->next;
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
                                            host->width, host->height);
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

  zwp_linux_dmabuf_v1_send_format(host->resource, format);
}

static void sl_linux_dmabuf_modifier(void* data,
                            struct zwp_linux_dmabuf_v1* linux_dmabuf,
                            uint32_t format,
                            uint32_t modifier_hi,
                            uint32_t modifier_lo) {
  struct sl_host_linux_dmabuf* host = static_cast<sl_host_linux_dmabuf*>(
      zwp_linux_dmabuf_v1_get_user_data(linux_dmabuf));

  zwp_linux_dmabuf_v1_send_modifier(host->resource, format, modifier_hi, modifier_lo);
}

static const struct zwp_linux_dmabuf_v1_listener sl_linux_dmabuf_listener = {
    sl_linux_dmabuf_format, sl_linux_dmabuf_modifier};

static void sl_linux_dmabuf_callback_done(void* data,
                                 struct wl_callback* callback,
                                 uint32_t serial) {
  struct sl_host_linux_dmabuf* host =
      static_cast<sl_host_linux_dmabuf*>(wl_callback_get_user_data(callback));

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
  host->version = MIN(version, 1);
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

  host->callback = wl_display_sync(ctx->display);
  wl_callback_set_user_data(host->callback, host);
  wl_callback_add_listener(host->callback, &sl_linux_dmabuf_callback_listener, host);
}

struct sl_global* sl_linux_dmabuf_global_create(struct sl_context* ctx) {
  assert(ctx->linux_dmabuf);

  return sl_global_create(ctx, &zwp_linux_dmabuf_v1_interface,
                          ctx->linux_dmabuf->version, ctx,
                          sl_bind_host_linux_dmabuf);
}
