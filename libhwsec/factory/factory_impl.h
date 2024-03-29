// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FACTORY_FACTORY_IMPL_H_
#define LIBHWSEC_FACTORY_FACTORY_IMPL_H_

#include <memory>
#include <utility>

#include "libhwsec/factory/factory.h"
#include "libhwsec/frontend/client/frontend.h"
#include "libhwsec/frontend/cryptohome/frontend.h"
#include "libhwsec/frontend/pinweaver/frontend.h"
#include "libhwsec/frontend/recovery_crypto/frontend.h"
#include "libhwsec/middleware/middleware.h"

namespace hwsec {

class HWSEC_EXPORT FactoryImpl : public Factory {
 public:
  FactoryImpl();
  ~FactoryImpl() override;
  std::unique_ptr<CryptohomeFrontend> GetCryptohomeFrontend() override;
  std::unique_ptr<PinWeaverFrontend> GetPinWeaverFrontend() override;
  std::unique_ptr<RecoveryCryptoFrontend> GetRecoveryCryptoFrontend() override;
  std::unique_ptr<ClientFrontend> GetClientFrontend() override;

 private:
  MiddlewareOwner middleware_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FACTORY_FACTORY_IMPL_H_
