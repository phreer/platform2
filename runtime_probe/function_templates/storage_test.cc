// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <map>
#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/json/json_reader.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "runtime_probe/function_templates/storage.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

constexpr auto kStorageDirPath("sys/class/block/");
constexpr auto kDefaultBytesPerSector = 512;

class FakeStorageFunction : public StorageFunction {
  using StorageFunction::StorageFunction;

 public:
  NAME_PROBE_FUNCTION("fake_storage");

  std::map<base::FilePath, base::Value> tool_map;
  std::map<base::FilePath, base::Value> sysfs_map;

 private:
  std::optional<base::Value> ProbeFromStorageTool(
      const base::FilePath& node_path) const override {
    auto it = tool_map.find(node_path);
    if (it == tool_map.end()) {
      return std::nullopt;
    }
    return it->second.Clone();
  }

  std::optional<base::Value> ProbeFromSysfs(
      const base::FilePath& node_path) const override {
    auto it = sysfs_map.find(node_path);
    if (it == sysfs_map.end()) {
      return std::nullopt;
    }
    return it->second.Clone();
  }
};

class StorageFunctionTest : public BaseFunctionTest {};

TEST_F(StorageFunctionTest, ProbeStorage) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  SetFile({kStorageDirPath, "blk1/removable"}, "0");
  SetFile({kStorageDirPath, "blk1/size"}, "100");
  SetFile({kStorageDirPath, "blk1/queue/logical_block_size"}, "5");
  SetFile({kStorageDirPath, "blk2/removable"}, "0");
  SetFile({kStorageDirPath, "blk2/size"}, "200");
  SetFile({kStorageDirPath, "blk2/queue/logical_block_size"}, "10");

  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");
  probe_function->tool_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_2": "value_2"
  })");
  const auto blk2_path = GetPathUnderRoot({kStorageDirPath, "blk2"});
  probe_function->sysfs_map[blk2_path] = *base::JSONReader::Read(R"({
      "field_3": "value_3",
      "field_4": "value_4"
  })");
  probe_function->tool_map[blk2_path] = *base::JSONReader::Read(R"({
      "field_5": "value_5"
  })");

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "field_1": "value_1",
        "field_2": "value_2",
        "path": "%s",
        "sectors": "100",
        "size": "500"
      },
      {
        "field_3": "value_3",
        "field_4": "value_4",
        "field_5": "value_5",
        "path": "%s",
        "sectors": "200",
        "size": "2000"
      }
    ]
  )JSON",
      blk1_path.value().c_str(), blk2_path.value().c_str()));

  ExpectUnorderedListEqual(result, ans);
}

TEST_F(StorageFunctionTest, RemovableStorage) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  // The storage device is removable.
  SetFile({kStorageDirPath, "blk1/removable"}, "1");
  SetFile({kStorageDirPath, "blk1/size"}, "100");
  SetFile({kStorageDirPath, "blk1/queue/logical_block_size"}, "5");

  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // Removable storage device will not be probed.
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, NoRemovableProperty) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  // No file for removable property.
  SetFile({kStorageDirPath, "blk1/size"}, "100");
  SetFile({kStorageDirPath, "blk1/queue/logical_block_size"}, "5");

  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // Storage devices without removable property will not be probed.
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, LoopbackDevice) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  SetFile({kStorageDirPath, "loop0/removable"}, "0");
  SetFile({kStorageDirPath, "loop0/size"}, "100");
  SetFile({kStorageDirPath, "loop0/queue/logical_block_size"}, "5");

  const auto loop0_path = GetPathUnderRoot({kStorageDirPath, "loop0"});
  probe_function->sysfs_map[loop0_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // Loopback device will not be probed.
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, DmVerityDevice) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  SetFile({kStorageDirPath, "dm-0/removable"}, "0");
  SetFile({kStorageDirPath, "dm-0/size"}, "100");
  SetFile({kStorageDirPath, "dm-0/queue/logical_block_size"}, "5");

  const auto dm0_path = GetPathUnderRoot({kStorageDirPath, "dm-0"});
  probe_function->sysfs_map[dm0_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // dm-verity device will not be probed.
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, NoSysfsResult) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  SetFile({kStorageDirPath, "blk1/removable"}, "0");
  SetFile({kStorageDirPath, "blk1/size"}, "100");
  SetFile({kStorageDirPath, "blk1/queue/logical_block_size"}, "5");
  SetFile({kStorageDirPath, "blk2/removable"}, "0");
  SetFile({kStorageDirPath, "blk2/size"}, "200");
  SetFile({kStorageDirPath, "blk2/queue/logical_block_size"}, "10");

  // No sysfs probe result for blk2.
  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // Only contain results with sysfs probe result.
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "field_1": "value_1",
        "path": "%s",
        "sectors": "100",
        "size": "500"
      }
    ]
  )JSON",
      blk1_path.value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, NoSectorCount) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  // No file for storage sector count.
  SetFile({kStorageDirPath, "blk1/removable"}, "0");
  SetFile({kStorageDirPath, "blk1/queue/logical_block_size"}, "5");

  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // For results without sector count, get -1 for sectors and size.
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "field_1": "value_1",
        "path": "%s",
        "sectors": "-1",
        "size": "-1"
      }
    ]
  )JSON",
      blk1_path.value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, InvalidSectorCount) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  // Invalid format for storage sector count.
  SetFile({kStorageDirPath, "blk1/removable"}, "0");
  SetFile({kStorageDirPath, "blk1/size"}, "invalid format");
  SetFile({kStorageDirPath, "blk1/queue/logical_block_size"}, "5");

  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // For results with invalid sector count, get -1 for sectors and size.
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "field_1": "value_1",
        "path": "%s",
        "sectors": "-1",
        "size": "-1"
      }
    ]
  )JSON",
      blk1_path.value().c_str()));
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, NoLogicalBlockSize) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  // No file for storage logical block size.
  SetFile({kStorageDirPath, "blk1/removable"}, "0");
  SetFile({kStorageDirPath, "blk1/size"}, "100");

  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // For results without logical block size, get |kDefaultBytesPerSector| for
  // logical block size.
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "field_1": "value_1",
        "path": "%s",
        "sectors": "100",
        "size": "%d"
      }
    ]
  )JSON",
      blk1_path.value().c_str(), kDefaultBytesPerSector * 100));
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, InvalidLogicalBlockSize) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  // Invalid format for storage logical block size.
  SetFile({kStorageDirPath, "blk1/removable"}, "0");
  SetFile({kStorageDirPath, "blk1/size"}, "100");
  SetFile({kStorageDirPath, "blk1/queue/logical_block_size"}, "invalid format");

  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // For results with invalid logical block size, get |kDefaultBytesPerSector|
  // for logical block size.
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "field_1": "value_1",
        "path": "%s",
        "sectors": "100",
        "size": "%d"
      }
    ]
  )JSON",
      blk1_path.value().c_str(), kDefaultBytesPerSector * 100));
  EXPECT_EQ(result, ans);
}

TEST_F(StorageFunctionTest, NonpositiveLogicalBlockSize) {
  const base::Value probe_statement(base::Value::Type::DICTIONARY);
  auto probe_function =
      CreateProbeFunction<FakeStorageFunction>(probe_statement);

  // Nonpositive number for storage logical block size.
  SetFile({kStorageDirPath, "blk1/removable"}, "0");
  SetFile({kStorageDirPath, "blk1/size"}, "100");
  SetFile({kStorageDirPath, "blk1/queue/logical_block_size"}, "-1");

  const auto blk1_path = GetPathUnderRoot({kStorageDirPath, "blk1"});
  probe_function->sysfs_map[blk1_path] = *base::JSONReader::Read(R"({
      "field_1": "value_1"
  })");

  auto result = probe_function->Eval();
  // For results with nonpositive logical block size, get
  // |kDefaultBytesPerSector| for logical block size.
  auto ans = CreateProbeResultFromJson(base::StringPrintf(
      R"JSON(
    [
      {
        "field_1": "value_1",
        "path": "%s",
        "sectors": "100",
        "size": "%d"
      }
    ]
  )JSON",
      blk1_path.value().c_str(), kDefaultBytesPerSector * 100));
  EXPECT_EQ(result, ans);
}

}  // namespace
}  // namespace runtime_probe