// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_BACKEND_WAYLAND_MANAGER_H_
#define VM_TOOLS_CROS_IM_BACKEND_WAYLAND_MANAGER_H_

#include <cstdint>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct zwp_text_input_v1;
struct zwp_text_input_v1_listener;
struct zwp_text_input_manager_v1;
struct zcr_extended_text_input_v1;
struct zcr_extended_text_input_v1_listener;
struct zcr_text_input_extension_v1;

namespace cros_im {

// WaylandManager manages the Wayland connection and provides text_input objects
// to clients.
class WaylandManager {
 public:
  static void CreateInstance(wl_display* display);
  static bool HasInstance();
  static WaylandManager* Get();

  // These return non-null if and only if initialization is complete.
  zwp_text_input_v1* CreateTextInput(const zwp_text_input_v1_listener* listener,
                                     void* listener_data);
  zcr_extended_text_input_v1* CreateExtendedTextInput(
      zwp_text_input_v1* text_input,
      const zcr_extended_text_input_v1_listener* listener,
      void* listener_data);

  // Once initialized, this value will not change.
  wl_seat* GetSeat() { return wl_seat_; }

  // Callbacks for wayland global events.
  void OnGlobal(wl_registry* registry,
                uint32_t name,
                const char* interface,
                uint32_t version);
  void OnGlobalRemove(wl_registry* registry, uint32_t name);

 private:
  explicit WaylandManager(wl_display* display);
  ~WaylandManager();

  bool IsInitialized() const;

  wl_seat* wl_seat_ = nullptr;
  uint32_t wl_seat_id_ = 0;
  // Creates text_input objects
  zwp_text_input_manager_v1* text_input_manager_ = nullptr;
  uint32_t text_input_manager_id_ = 0;
  // Creates extended_text_input objects
  zcr_text_input_extension_v1* text_input_extension_ = nullptr;
  uint32_t text_input_extension_id_ = 0;
};

}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_BACKEND_WAYLAND_MANAGER_H_
