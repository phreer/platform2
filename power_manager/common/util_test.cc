// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/common/util.h"

#include <string>

#include <base/files/file_path.h>
#include <gtest/gtest.h>

namespace power_manager {
namespace util {

namespace {

// Creates a TimeDelta and returns TimeDeltaToString()'s output.
std::string RunTimeDeltaToString(int hours, int minutes, int seconds) {
  return TimeDeltaToString(
      base::Seconds(hours * 3600 + minutes * 60 + seconds));
}

}  // namespace

TEST(UtilTest, TimeDeltaToString) {
  EXPECT_EQ("3h23m13s", RunTimeDeltaToString(3, 23, 13));
  EXPECT_EQ("47m45s", RunTimeDeltaToString(0, 47, 45));
  EXPECT_EQ("7s", RunTimeDeltaToString(0, 0, 7));
  EXPECT_EQ("0s", RunTimeDeltaToString(0, 0, 0));
  EXPECT_EQ("13h17s", RunTimeDeltaToString(13, 0, 17));
  EXPECT_EQ("8h59m", RunTimeDeltaToString(8, 59, 0));
  EXPECT_EQ("5m33s", RunTimeDeltaToString(0, 5, 33));
  EXPECT_EQ("5h", RunTimeDeltaToString(5, 0, 0));
}

TEST(UtilTest, JoinPaths) {
  EXPECT_EQ("", JoinPaths({}, ","));
  EXPECT_EQ("/foo/bar", JoinPaths({base::FilePath("/foo/bar")}, ","));
  EXPECT_EQ("/a,/b,/c", JoinPaths({base::FilePath("/a"), base::FilePath("/b"),
                                   base::FilePath("/c")},
                                  ","));
}

}  // namespace util
}  // namespace power_manager
