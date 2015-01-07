// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_CURL_H_
#define LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_CURL_H_

#include <string>

#include <base/memory/ref_counted.h>
#include <base/task_runner.h>
#include <chromeos/chromeos_export.h>
#include <chromeos/http/http_transport.h>

namespace chromeos {
namespace http {
namespace curl {

///////////////////////////////////////////////////////////////////////////////
// An implementation of http::Transport that uses libcurl for
// HTTP communications. This class (as http::Transport base)
// is used by http::Request and http::Response classes to provide HTTP
// functionality to the clients.
// See http_transport.h for more details.
///////////////////////////////////////////////////////////////////////////////
class CHROMEOS_EXPORT Transport : public http::Transport {
 public:
  // Constructs the transport using the current message loop for async
  // operations.
  Transport();
  // Constructs the transport with a custom task runner for async operations.
  explicit Transport(scoped_refptr<base::TaskRunner> task_runner);
  // Creates a transport object using a proxy.
  // |proxy| is of the form [protocol://][user:password@]host[:port].
  // If not defined, protocol is assumed to be http://.
  explicit Transport(const std::string& proxy);
  virtual ~Transport();

  // Overrides from http::Transport.
  std::shared_ptr<http::Connection> CreateConnection(
      std::shared_ptr<http::Transport> transport,
      const std::string& url,
      const std::string& method,
      const HeaderList& headers,
      const std::string& user_agent,
      const std::string& referer,
      chromeos::ErrorPtr* error) override;

  void RunCallbackAsync(const tracked_objects::Location& from_here,
                        const base::Closure& callback) override;

  void StartAsyncTransfer(http::Connection* connection,
                          const SuccessCallback& success_callback,
                          const ErrorCallback& error_callback) override;

 private:
  std::string proxy_;
  scoped_refptr<base::TaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(Transport);
};

}  // namespace curl
}  // namespace http
}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_CURL_H_
