// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_NOTIFICATIOND_NOTIFICATION_SHELL_CLIENT_H_
#define VM_TOOLS_NOTIFICATIOND_NOTIFICATION_SHELL_CLIENT_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <wayland-server.h>

#include "notification-shell-unstable-v1-client-protocol.h"  // NOLINT(build/include_directory)
#include "vm_tools/notificationd/notification_shell_interface.h"

namespace vm_tools {
namespace notificationd {

// Handles notification shell protocol as Wayland client.
class NotificationShellClient {
 public:
  // Creates and returns a NotificationShellClient. The |interface| is required
  // to outlive this NotificationShellClient. Returns nullptr if the the client
  // failed to be initialized and be connected to the compositor for any reason.
  static std::unique_ptr<NotificationShellClient> Create(
      const std::string& display_name,
      const std::string& virtwl_device,
      NotificationShellInterface* interface,
      base::OnceClosure quit_closure);

  ~NotificationShellClient() = default;

  // Sends create_notification request to Wayland server which this client
  // is connected to. Returns true on success. Arguments for this method must be
  // synchronized with the notification shell spec in
  // vm_tools/notificationd/protocol/notification-shell-unstable-v1.xml.
  bool CreateNotification(const std::string& title,
                          const std::string& message,
                          const std::string& display_source,
                          const std::string& notification_key,
                          const std::vector<std::string>& buttons);

  // Sends a close notification request from the notification interface
  // associated with a given notification key. Returns true on success.
  bool CloseNotification(const std::string& notification_key);

 private:
  struct WlEventLoopDeleter {
    void operator()(wl_event_loop* loop) { wl_event_loop_destroy(loop); }
  };
  using WlEventLoop = std::unique_ptr<wl_event_loop, WlEventLoopDeleter>;

  struct WlDisplayDeleter {
    void operator()(wl_display* display) { wl_display_disconnect(display); }
  };
  using WlDisplay = std::unique_ptr<wl_display, WlDisplayDeleter>;

  struct NotificationShellProxyDeleter {
    void operator()(zcr_notification_shell_v1* proxy) {
      // zcr_notification_shell_v1_destroy method is automatically generated by
      // wayland-scanner according to the
      // vm_tools/notificationd/protocol/notification-shell-unstable-v1.xml.
      zcr_notification_shell_v1_destroy(proxy);
    }
  };
  using NotificationShellProxy =
      std::unique_ptr<zcr_notification_shell_v1, NotificationShellProxyDeleter>;

  // Handles notification_shell_notification interface.
  class NotificationClient {
   public:
    NotificationClient(zcr_notification_shell_notification_v1* proxy,
                       const std::string& notification_key,
                       NotificationShellClient* shell_client);
    NotificationClient(const NotificationClient&) = delete;
    NotificationClient& operator=(const NotificationClient&) = delete;

    // Sends a close notification request to Wayland server which this client
    // is connected to.
    void Close();

   private:
    struct NotificationProxyDeleter {
      void operator()(zcr_notification_shell_notification_v1* proxy) {
        // zcr_notification_shell_notification_v1_destroy method is
        // automatically generated by wayland-scanner according to the
        // vm_tools/notificationd/protocol/notification-shell-unstable-v1.xml.
        zcr_notification_shell_notification_v1_destroy(proxy);
      }
    };
    using NotificationProxy =
        std::unique_ptr<zcr_notification_shell_notification_v1,
                        NotificationProxyDeleter>;

    // Handles closed events. Called from the wrapper
    // (HandleNotificationClosedEventCallback), which is compatible with
    // wayland-client library's callback signature.
    void HandleNotificationClosedEvent(bool by_user);

    // Handles clicked events. Called from the wrapper
    // (HandleNotificationClickedEventCallback), which is compatible with
    // wayland-client library's callback signature.
    void HandleNotificationClickedEvent(int32_t button_index);

    // Wrapper for wayland event handlers. Called from wayland-client library.
    static void HandleNotificationClosedEventCallback(
        void* data,
        zcr_notification_shell_notification_v1* notification_proxy,
        uint32_t by_user);
    static void HandleNotificationClickedEventCallback(
        void* data,
        zcr_notification_shell_notification_v1* notification_proxy,
        int32_t button_index);

    const zcr_notification_shell_notification_v1_listener
        notification_listener_ = {HandleNotificationClosedEventCallback,
                                  HandleNotificationClickedEventCallback};

    // Wayland proxy for notification interface.
    NotificationProxy proxy_;

    // Notification key associated with this client.
    const std::string notification_key_;

    NotificationShellClient* shell_client_;  // Not owned.
  };

  NotificationShellClient(NotificationShellInterface* interface,
                          base::OnceClosure quit_closure);
  NotificationShellClient(const NotificationShellClient&) = delete;
  NotificationShellClient& operator=(const NotificationShellClient&) = delete;

  // Initializes the Wayland client. Returns true on success.
  bool Init(const char* display_name, const char* virtwl_device);

  // Requests the server to emit sync event and blocks until receiving the event
  // while handling event loop.
  void WaitForSync();

  // Called when |event_loop_fd_| becomes readable.
  void OnEventReadable();

  // Handles notification closed event.
  void HandleNotificationClosedEvent(const std::string& notification_key,
                                     bool by_user);

  // Handles notification closed event.
  void HandleNotificationClickedEvent(const std::string& notification_key,
                                      int32_t button_index);

  // Handles registry event. Called from event handler for wayland-client
  // library. Called from the wrapper (HandleRegistryCallback), which is
  // compatible with wayland-client library's callback signature.
  void HandleRegistry(wl_registry* registry,
                      int32_t id,
                      const char* interface,
                      uint32_t version);

  // Handles events for display. Returns the number of dispatched events on
  // success or -1 on failure. Called from the wrapper (HandleEventCallback),
  // which is compatible with wayland-client library's callback signature.
  int HandleEvent(uint32_t mask);

  // Handles virtwl socket events. Called from the wrapper
  // (HandleVirtwlSocketEventCallback), which is compatible with wayland-client
  // library's callback signature.
  void HandleVirtwlSocketEvent();

  // Handles virtwl context events. Called from the wrapper
  // (HandleVirtwlCtxEventCallback), which is compatible with wayland-client
  // library's callback signature.
  void HandleVirtwlCtxEvent();

  // Wrapper for wayland event handlers. Called from wayland-client library.
  static int HandleEventCallback(int fd, uint32_t mask, void* data);
  static void HandleRegistryCallback(void* data,
                                     wl_registry* registry,
                                     uint32_t id,
                                     const char* interface,
                                     uint32_t version);
  static int HandleVirtwlSocketEventCallback(int fd, uint32_t mask, void* data);
  static int HandleVirtwlCtxEventCallback(int fd, uint32_t mask, void* data);

  const wl_registry_listener registry_listener_ = {HandleRegistryCallback,
                                                   nullptr};

  // Event loop for handling Wayland event callbacks.
  WlEventLoop event_loop_;

  // File discriptor of the wayland event loop.
  base::ScopedFD event_loop_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  // File discriptor of the socket connected to the Wayland display. Handles
  // socket events from Wayland display by observing this.
  base::ScopedFD virtwl_socket_fd_;

  // File discriptor of the context of Wayland connection created by virtwl.
  base::ScopedFD virtwl_ctx_fd_;

  // Wayland display for the host.
  WlDisplay display_;

  // Wayland proxy for notification shell interface.
  NotificationShellProxy proxy_;

  NotificationShellInterface* const interface_;

  std::map<std::string, std::unique_ptr<NotificationClient>>
      notification_clients_;

  base::OnceClosure quit_closure_;
};

}  // namespace notificationd
}  // namespace vm_tools

#endif  // VM_TOOLS_NOTIFICATIOND_NOTIFICATION_SHELL_CLIENT_H_
