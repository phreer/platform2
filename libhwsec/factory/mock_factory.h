// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FACTORY_MOCK_FACTORY_H_
#define LIBHWSEC_FACTORY_MOCK_FACTORY_H_

#include <memory>
#include <utility>

#include <gmock/gmock.h>

#include "libhwsec/factory/factory.h"

// Factory holds the ownership of the middleware and backend.
// And generates different frontend for different usage.

namespace hwsec {

class MockFactory : public Factory {
 public:
  MockFactory() = default;
  ~MockFactory() override = default;
  MOCK_METHOD(std::unique_ptr<CryptohomeFrontend>,
              GetCryptohomeFrontend,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<PinWeaverFrontend>,
              GetPinWeaverFrontend,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<RecoveryCryptoFrontend>,
              GetRecoveryCryptoFrontend,
              (),
              (override));
  MOCK_METHOD(std::unique_ptr<ClientFrontend>,
              GetClientFrontend,
              (),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_FACTORY_MOCK_FACTORY_H_
