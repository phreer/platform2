// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TPM_CACHE_IMPL_H_
#define TRUNKS_TPM_CACHE_IMPL_H_

#include "trunks/tpm_cache.h"

#include <base/macros.h>
#include <base/optional.h>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"
#include "trunks/trunks_factory.h"

namespace trunks {

// Implementation of the interface TpmCache.
class TRUNKS_EXPORT TpmCacheImpl : public TpmCache {
 public:
  explicit TpmCacheImpl(const TrunksFactory& factory);
  ~TpmCacheImpl() override = default;

  TPM_RC GetSaltingKeyPublicArea(TPMT_PUBLIC* public_area) override;

  TPM_ALG_ID GetBestSupportedKeyType() override;

 private:
  // Salting key public area cache.
  base::Optional<TPMT_PUBLIC> salting_key_pub_area_;

  // Cache of the best SRK and salting key type. If the optional value exists,
  // it can only be TPM_ALG_ECC or TPM_ALG_RSA.
  base::Optional<TPM_ALG_ID> best_key_type_;

  const TrunksFactory& factory_;

  DISALLOW_COPY_AND_ASSIGN(TpmCacheImpl);
};

}  // namespace trunks

#endif  // TRUNKS_TPM_CACHE_IMPL_H_
