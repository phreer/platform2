// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_PCA_AGENT_SERVER_PCA_HTTP_UTILS_H_
#define ATTESTATION_PCA_AGENT_SERVER_PCA_HTTP_UTILS_H_

#include <string>

#include <brillo/http/http_proxy.h>
#include <brillo/http/http_utils.h>

namespace attestation {
namespace pca_agent {

// All the http-related function calls that needs polymorphism are put here.
class PcaHttpUtils {
 public:
  PcaHttpUtils() = default;
  virtual ~PcaHttpUtils() = default;

  // An interface that is related to |brillo::http::GetChromeProxyServersAsync|.
  virtual void GetChromeProxyServersAsync(
      const std::string& url,
      const brillo::http::GetChromeProxyServersCallback& callback) = 0;

  DISALLOW_COPY_AND_ASSIGN(PcaHttpUtils);
};

}  // namespace pca_agent
}  // namespace attestation

#endif  // ATTESTATION_PCA_AGENT_SERVER_PCA_HTTP_UTILS_H_
