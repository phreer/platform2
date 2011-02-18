// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_OWNER_KEY_H_
#define LOGIN_MANAGER_MOCK_OWNER_KEY_H_

#include "login_manager/owner_key.h"

#include <unistd.h>
#include <gmock/gmock.h>

namespace base {
class RSAPrivateKey;
}

namespace login_manager {
class MockOwnerKey : public OwnerKey {
 public:
  MockOwnerKey() : OwnerKey(FilePath("")) {}
  virtual ~MockOwnerKey() {}
  MOCK_METHOD0(HaveCheckedDisk, bool());
  MOCK_METHOD0(IsPopulated, bool());
  MOCK_METHOD0(PopulateFromDiskIfPossible, bool());
  MOCK_METHOD1(PopulateFromBuffer, bool(const std::vector<uint8>&));
  MOCK_METHOD1(PopulateFromKeypair, bool(base::RSAPrivateKey*));
  MOCK_METHOD0(Persist, bool());
  MOCK_METHOD4(Verify, bool(const char*, uint32, const char*, uint32));
  MOCK_METHOD3(Sign, bool(const char*, uint32, std::vector<uint8>*));
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_OWNER_KEY_H_
