// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "base/logging.h"

#include "perf_reader.h"
#include "perf_test_files.h"
#include "quipper_string.h"
#include "utils.h"

namespace quipper {

TEST(PerfReaderTest, Test1Cycle) {
  for (unsigned int i = 0;
       i < arraysize(perf_test_files::kPerfDataFiles);
       ++i) {
    PerfReader pr;
    string input_perf_data = perf_test_files::kPerfDataFiles[i];
    LOG(INFO) << "Testing " << input_perf_data;
    string output_perf_data = input_perf_data + ".pr.out";
    ASSERT_TRUE(pr.ReadFile(input_perf_data));
    ASSERT_TRUE(pr.WriteFile(output_perf_data));

    EXPECT_TRUE(ComparePerfReports(input_perf_data, output_perf_data));
    EXPECT_TRUE(ComparePerfBuildIDLists(input_perf_data, output_perf_data));
  }

  std::set<string> metadata;
  for (unsigned int i = 0;
       i < arraysize(perf_test_files::kPerfPipedDataFiles);
       ++i) {
    PerfReader pr;
    string input_perf_data = perf_test_files::kPerfPipedDataFiles[i];
    LOG(INFO) << "Testing " << input_perf_data;
    string output_perf_data = input_perf_data + ".pr.out";
    ASSERT_TRUE(pr.ReadFile(input_perf_data));
    ASSERT_TRUE(pr.WriteFile(output_perf_data));

    EXPECT_TRUE(ComparePipedPerfReports(input_perf_data, output_perf_data,
                                        &metadata));
  }

  // For piped data, perf report doesn't check metadata.
  // Make sure that each metadata type is seen at least once.
  for (size_t i = 0; kSupportedMetadata[i]; ++i) {
    EXPECT_NE(metadata.find(kSupportedMetadata[i]), metadata.end())
        << "No output file from piped input files had "
        << kSupportedMetadata[i] << " metadata";
  }
}

}  // namespace quipper

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
