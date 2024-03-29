// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/faced_cli/faced_cli.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_format.h>
#include <base/check.h>
#include <base/command_line.h>
#include <base/task/single_thread_task_executor.h>
#include <base/threading/thread.h>
#include <brillo/flag_helper.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include <string>
#include <vector>

#include "faced/faced_cli/faced_client.h"

namespace faced {

namespace {

// CLI documentation.
constexpr std::string_view kUsage = R"(Usage: faced_cli <command> [options]

Commands:
  connect             Set up a Mojo connection to Faced by bootstrapping over
                      Dbus and then disconnect the session.

Full details of options can be shown using "--help".
)";

// Parse a command string into the enum type `Command`.
std::optional<Command> ParseCommand(std::string_view command) {
  if (command == "connect") {
    return Command::kConnectToFaced;
  }
  return std::nullopt;
}

absl::Status RunCommand(const CommandLineArgs& command) {
  switch (command.command) {
    case Command::kConnectToFaced:
      return ConnectAndDisconnectFromFaced();
  }
}

}  // namespace

// Parse the given command line, producing a `CommandLineArgs` on success.
absl::StatusOr<CommandLineArgs> ParseCommandLine(int argc,
                                                 const char* const* argv) {
  CHECK(argc > 0)
      << "Argv must contain at least one element, the program name.";

  if (!brillo::FlagHelper::Init(argc, argv, std::string(kUsage),
                                brillo::FlagHelper::InitFuncType::kReturn)) {
    return absl::InvalidArgumentError("Invalid option.");
  }

  // Parse the sub-command.
  std::vector<std::string> commands =
      base::CommandLine::ForCurrentProcess()->GetArgs();
  if (commands.size() != 1) {
    return absl::InvalidArgumentError("Expected exactly one command.");
  }
  std::optional<Command> command = ParseCommand(commands[0]);
  if (!command.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unknown command '%s'.", commands[0]));
  }

  return CommandLineArgs{
      .command = *command,
  };
}

int Main(int argc, char* argv[]) {
  // Setup task context
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  // Basic Mojo initialization for a new process.
  mojo::core::Init();
  base::Thread ipc_thread("FacedCliIpc");
  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  mojo::core::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  // Parse command line.
  absl::StatusOr<CommandLineArgs> result = ParseCommandLine(argc, argv);
  if (!result.ok()) {
    std::cerr << kUsage << "\n"
              << "Error: " << result.status().message() << "\n";
    return 1;
  }

  // Run the appropriate command.
  absl::Status command_result = RunCommand(*result);
  if (!command_result.ok()) {
    std::cerr << "Error: " << command_result.message() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace faced
