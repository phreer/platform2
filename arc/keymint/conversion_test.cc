// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/conversion.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace arc::keymint {

namespace {

constexpr std::array<uint8_t, 3> kBlob1{{3, 23, 59}};
constexpr std::array<uint8_t, 4> kBlob2{{23, 46, 69, 92}};

::testing::AssertionResult VerifyVectorUint8(const uint8_t* a,
                                             const size_t a_size,
                                             const std::vector<uint8_t>& b) {
  if (a_size != b.size()) {
    return ::testing::AssertionFailure()
           << "Sizes differ: a=" << a_size << " b=" << b.size();
  }
  for (size_t i = 0; i < a_size; ++i) {
    if (a[i] != b[i]) {
      return ::testing::AssertionFailure()
             << "Elements differ: a=" << static_cast<int>(a[i])
             << " b=" << static_cast<int>(b[i]);
    }
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult VerifyKeyParametersWithStrictInputs(
    const ::keymaster::AuthorizationSet& a,
    const std::vector<arc::mojom::keymint::KeyParameterPtr>& b) {
  if (a.size() != b.size()) {
    return ::testing::AssertionFailure()
           << "Sizes differ: a=" << a.size() << " b=" << b.size();
  }

  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].tag != static_cast<uint32_t>(b[i]->tag)) {
      return ::testing::AssertionFailure()
             << "Tags differ: i=" << i
             << " a=" << static_cast<uint32_t>(a[i].tag) << " b=" << b[i]->tag;
    }
  }
  if (!(b[0]->value->is_bool_value() && b[1]->value->is_integer() &&
        b[2]->value->is_long_integer() && b[3]->value->is_date_time() &&
        b[4]->value->is_blob())) {
    return ::testing::AssertionFailure() << "Incorrect union value type";
  }
  if (!(a[0].boolean == b[0]->value->get_bool_value() &&
        a[1].integer == b[1]->value->get_integer() &&
        a[2].long_integer == b[2]->value->get_long_integer() &&
        a[3].date_time == b[3]->value->get_date_time())) {
    return ::testing::AssertionFailure() << "Values differ";
  }
  return VerifyVectorUint8(a[4].blob.data, a[4].blob.data_length,
                           b[4]->value->get_blob());
}

}  // namespace

TEST(ConvertFromKeymasterMessage, Uint8Vector) {
  // Convert.
  std::vector<uint8_t> output =
      ConvertFromKeymasterMessage(kBlob1.data(), kBlob1.size());

  // Verify.
  EXPECT_TRUE(VerifyVectorUint8(kBlob1.data(), kBlob1.size(), output));
}

TEST(ConvertFromKeymasterMessage, KeyParameterVector) {
  // Prepare.
  ::keymaster::AuthorizationSet input;
  input.push_back(keymaster_param_bool(KM_TAG_EARLY_BOOT_ONLY));  // bool
  input.push_back(keymaster_param_enum(
      KM_TAG_ALGORITHM,
      KM_ALGORITHM_TRIPLE_DES));  // enum, enum_rep, int, int_rep
  input.push_back(keymaster_param_long(KM_TAG_USER_SECURE_ID, 65537));  // long
  input.push_back(
      keymaster_param_date(KM_TAG_USAGE_EXPIRE_DATETIME, 1507));  // date
  input.push_back(keymaster_param_blob(KM_TAG_APPLICATION_DATA, kBlob1.data(),
                                       kBlob1.size()));  // bignum, bytes

  // Convert.
  std::vector<arc::mojom::keymint::KeyParameterPtr> output =
      ConvertFromKeymasterMessage(input);

  // Verify.
  EXPECT_TRUE(VerifyKeyParametersWithStrictInputs(input, output));
}

TEST(ConvertToKeymasterMessage, Buffer) {
  // Prepare.
  std::vector<uint8_t> input(kBlob1.begin(), kBlob1.end());

  // Convert.
  ::keymaster::Buffer buffer;
  ConvertToKeymasterMessage(input, &buffer);
  uint8_t output[kBlob1.size()];

  // Verify.
  EXPECT_TRUE(buffer.read(output, input.size()));
  EXPECT_TRUE(VerifyVectorUint8(output, input.size(), input));
}

TEST(ConvertToKeymasterMessage, ReusedBuffer) {
  // Prepare.
  std::vector<uint8_t> input1(kBlob1.begin(), kBlob1.end());
  std::vector<uint8_t> input2(kBlob2.begin(), kBlob2.end());

  // Convert.
  ::keymaster::Buffer buffer;
  ConvertToKeymasterMessage(input1, &buffer);
  ConvertToKeymasterMessage(input2, &buffer);
  uint8_t output[kBlob2.size()];

  // Verify.
  EXPECT_TRUE(buffer.read(output, kBlob2.size()));
  EXPECT_TRUE(VerifyVectorUint8(output, kBlob2.size(), input2));
}

TEST(ConvertToKeymasterMessage, ClientIdAndAppData) {
  // Prepare.
  std::vector<uint8_t> clientId(kBlob1.begin(), kBlob1.end());
  std::vector<uint8_t> appData(kBlob2.begin(), kBlob2.end());

  // Convert.
  ::keymaster::AuthorizationSet output;
  ConvertToKeymasterMessage(clientId, appData, &output);

  // Verify.
  ASSERT_EQ(2, output.size());
  EXPECT_EQ(KM_TAG_APPLICATION_ID, output[0].tag);
  EXPECT_EQ(KM_TAG_APPLICATION_DATA, output[1].tag);
  EXPECT_TRUE(VerifyVectorUint8(output[0].blob.data, output[0].blob.data_length,
                                clientId));
  EXPECT_TRUE(VerifyVectorUint8(output[1].blob.data, output[1].blob.data_length,
                                appData));
}

}  // namespace arc::keymint