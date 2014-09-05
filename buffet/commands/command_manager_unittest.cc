// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <gtest/gtest.h>

#include "buffet/commands/command_manager.h"
#include "buffet/commands/unittest_utils.h"

using buffet::unittests::CreateDictionaryValue;

static base::FilePath SaveJsonToTempFile(const base::DictionaryValue& dict) {
  std::string json;
  base::JSONWriter::Write(&dict, &json);
  base::FilePath temp_file;
  base::CreateTemporaryFile(&temp_file);
  base::WriteFile(temp_file, json.data(), static_cast<int>(json.size()));
  return temp_file;
}

TEST(CommandManager, Empty) {
  buffet::CommandManager manager;
  EXPECT_TRUE(manager.GetCommandDictionary().IsEmpty());
}

TEST(CommandManager, LoadBaseCommandsJSON) {
  buffet::CommandManager manager;
  auto json = CreateDictionaryValue(R"({
    'base': {
      'reboot': {
        'parameters': {'delay': 'integer'}
      },
      'shutdown': {
        'parameters': {}
      }
    }
  })");
  EXPECT_TRUE(manager.LoadBaseCommands(*json, nullptr));
}

TEST(CommandManager, LoadBaseCommandsFile) {
  buffet::CommandManager manager;
  auto json = CreateDictionaryValue(R"({
    'base': {
      'reboot': {
        'parameters': {'delay': 'integer'}
      },
      'shutdown': {
        'parameters': {}
      }
    }
  })");
  base::FilePath temp_file = SaveJsonToTempFile(*json);
  EXPECT_TRUE(manager.LoadBaseCommands(temp_file, nullptr));
  base::DeleteFile(temp_file, false);
}

TEST(CommandManager, LoadCommandsJSON) {
  buffet::CommandManager manager;
  auto json = CreateDictionaryValue(R"({
    'robot': {
      '_jump': {
        'parameters': {'height': 'integer'}
      },
      '_speak': {
        'parameters': {'phrase': 'string'}
      }
    }
  })");
  EXPECT_TRUE(manager.LoadCommands(*json, "category", nullptr));
}

TEST(CommandManager, LoadCommandsFile) {
  buffet::CommandManager manager;
  // Load some standard command definitions first.
  auto json = CreateDictionaryValue(R"({
    'base': {
      'reboot': {
        'parameters': {'delay': 'integer'}
      },
      'shutdown': {
        'parameters': {}
      }
    }
  })");
  manager.LoadBaseCommands(*json, nullptr);
  // Load device-supported commands.
  json = CreateDictionaryValue(R"({
    'base': {
      'reboot': {
        'parameters': {'delay': 'integer'}
      }
    },
    'robot': {
      '_jump': {
        'parameters': {'height': 'integer'}
      }
    }
  })");
  base::FilePath temp_file = SaveJsonToTempFile(*json);
  EXPECT_TRUE(manager.LoadCommands(temp_file, nullptr));
  base::DeleteFile(temp_file, false);
  EXPECT_EQ(2, manager.GetCommandDictionary().GetSize());
  EXPECT_NE(nullptr, manager.GetCommandDictionary().FindCommand("base.reboot"));
  EXPECT_NE(nullptr, manager.GetCommandDictionary().FindCommand("robot._jump"));
}
