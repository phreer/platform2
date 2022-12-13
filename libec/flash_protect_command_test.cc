// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "libec/ec_command.h"
#include "libec/flash_protect_command.h"

namespace ec {
namespace {

TEST(FlashProtectCommand, FlashProtectCommand) {
  uint32_t flags = 0xdeadbeef;
  uint32_t mask = 0xfeedc0de;
  FlashProtectCommand cmd(flags, mask);
  EXPECT_EQ(cmd.Version(), EC_VER_FLASH_PROTECT);
  EXPECT_EQ(cmd.Command(), EC_CMD_FLASH_PROTECT);
  EXPECT_EQ(cmd.Req()->flags, flags);
  EXPECT_EQ(cmd.Req()->mask, mask);
}

TEST(FlashProtectCommand, ParseFlags) {
  std::string result;

  // test each flag string individually
  uint32_t flags = EC_FLASH_PROTECT_RO_AT_BOOT;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_AT_BOOT  ");

  flags = EC_FLASH_PROTECT_RO_NOW;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_NOW  ");

  flags = EC_FLASH_PROTECT_ALL_NOW;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ALL_NOW  ");

  flags = EC_FLASH_PROTECT_GPIO_ASSERTED;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "GPIO_ASSERTED  ");

  flags = EC_FLASH_PROTECT_ERROR_STUCK;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ERROR_STUCK  ");

  flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ERROR_INCONSISTENT  ");

  flags = EC_FLASH_PROTECT_ALL_AT_BOOT;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ALL_AT_BOOT  ");

  flags = EC_FLASH_PROTECT_RW_AT_BOOT;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RW_AT_BOOT  ");

  flags = EC_FLASH_PROTECT_RW_NOW;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RW_NOW  ");

  flags = EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ROLLBACK_AT_BOOT  ");

  flags = EC_FLASH_PROTECT_ROLLBACK_NOW;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ROLLBACK_NOW  ");

  // test a combination of flags
  flags = EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
          EC_FLASH_PROTECT_GPIO_ASSERTED;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_AT_BOOT  RO_NOW  GPIO_ASSERTED  ");
}

}  // namespace
}  // namespace ec