// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(yusukes): Uprev libmojo to >=r415560 and remove this file.

// Copied from Chromium r485157's
// mojo/edk/embedder/named_platform_handle_utils_posix.cc with the following
// modifications:
// 1) Change the namespace from mojo::edk:: to login_manager::.
// 2) Change the include path from mojo/edk/ to login_manager/.
// 3) Remove CreateClientHandle().
// 4) Define ScopedPlatformHandle (in the header.)
// 5) Define PlatformHandle as int, rather than as a struct (in the header.)

#include "login_manager/named_platform_handle_utils.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"

namespace login_manager {

namespace {

constexpr size_t kMaxSocketNameLength = 104;

// This function fills in |unix_addr| with the appropriate data for the socket,
// and sets |unix_addr_len| to the length of the data therein.
// Returns true on success, or false on failure (typically because |handle.name|
// violated the naming rules).
bool MakeUnixAddr(const NamedPlatformHandle& handle,
                  struct sockaddr_un* unix_addr,
                  size_t* unix_addr_len) {
  DCHECK(unix_addr);
  DCHECK(unix_addr_len);
  DCHECK(handle.is_valid());

  // We reject handle.name.length() == kMaxSocketNameLength to make room for the
  // NUL terminator at the end of the string.
  if (handle.name.length() >= kMaxSocketNameLength) {
    LOG(ERROR) << "Socket name too long: " << handle.name;
    return false;
  }

  // Create unix_addr structure.
  memset(unix_addr, 0, sizeof(struct sockaddr_un));
  unix_addr->sun_family = AF_UNIX;
  strncpy(unix_addr->sun_path, handle.name.c_str(), kMaxSocketNameLength);
  *unix_addr_len =
      offsetof(struct sockaddr_un, sun_path) + handle.name.length();
  return true;
}

// This function creates a unix domain socket, and set it as non-blocking.
ScopedPlatformHandle CreateUnixDomainSocket() {
  // Create the unix domain socket.
  PlatformHandle socket_handle(socket(AF_UNIX, SOCK_STREAM, 0));
  ScopedPlatformHandle handle(socket_handle);
  if (!handle.is_valid()) {
    PLOG(ERROR) << "Failed to create AF_UNIX socket.";
    return ScopedPlatformHandle();
  }

  // Now set it as non-blocking.
  if (!base::SetNonBlocking(handle.get())) {
    PLOG(ERROR) << "base::SetNonBlocking() failed " << handle.get();
    return ScopedPlatformHandle();
  }
  return handle;
}

}  // namespace

ScopedPlatformHandle CreateServerHandle(
    const NamedPlatformHandle& named_handle) {
  if (!named_handle.is_valid())
    return ScopedPlatformHandle();

  // Make sure the path we need exists.
  base::FilePath socket_dir = base::FilePath(named_handle.name).DirName();
  if (!base::CreateDirectory(socket_dir)) {
    LOG(ERROR) << "Couldn't create directory: " << socket_dir.value();
    return ScopedPlatformHandle();
  }

  // Delete any old FS instances.
  if (unlink(named_handle.name.c_str()) < 0 && errno != ENOENT) {
    PLOG(ERROR) << "unlink " << named_handle.name;
    return ScopedPlatformHandle();
  }

  struct sockaddr_un unix_addr;
  size_t unix_addr_len;
  if (!MakeUnixAddr(named_handle, &unix_addr, &unix_addr_len))
    return ScopedPlatformHandle();

  ScopedPlatformHandle handle = CreateUnixDomainSocket();
  if (!handle.is_valid())
    return ScopedPlatformHandle();

  // Bind the socket.
  if (bind(handle.get(), reinterpret_cast<const sockaddr*>(&unix_addr),
           unix_addr_len) < 0) {
    PLOG(ERROR) << "bind " << named_handle.name;
    return ScopedPlatformHandle();
  }

  // Start listening on the socket.
  if (listen(handle.get(), SOMAXCONN) < 0) {
    PLOG(ERROR) << "listen " << named_handle.name;
    unlink(named_handle.name.c_str());
    return ScopedPlatformHandle();
  }
  return handle;
}

}  // namespace login_manager
