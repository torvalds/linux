//===-- StructuredDataPlugin.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/StructuredDataPlugin.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"

using namespace lldb;
using namespace lldb_private;

namespace {
class CommandStructuredData : public CommandObjectMultiword {
public:
  CommandStructuredData(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "structured-data",
                               "Parent for per-plugin structured data commands",
                               "plugin structured-data <plugin>") {}

  ~CommandStructuredData() override = default;
};
}

StructuredDataPlugin::StructuredDataPlugin(const ProcessWP &process_wp)
    : PluginInterface(), m_process_wp(process_wp) {}

StructuredDataPlugin::~StructuredDataPlugin() = default;

bool StructuredDataPlugin::GetEnabled(llvm::StringRef type_name) const {
  // By default, plugins are always enabled.  Plugin authors should override
  // this if there is an enabled/disabled state for their plugin.
  return true;
}

ProcessSP StructuredDataPlugin::GetProcess() const {
  return m_process_wp.lock();
}

void StructuredDataPlugin::InitializeBasePluginForDebugger(Debugger &debugger) {
  // Create our mutliword command anchor if it doesn't already exist.
  auto &interpreter = debugger.GetCommandInterpreter();
  if (!interpreter.GetCommandObject("plugin structured-data")) {
    // Find the parent command.
    auto parent_command =
        debugger.GetCommandInterpreter().GetCommandObject("plugin");
    if (!parent_command)
      return;

    // Create the structured-data ommand object.
    auto command_name = "structured-data";
    auto command_sp = CommandObjectSP(new CommandStructuredData(interpreter));

    // Hook it up under the top-level plugin command.
    parent_command->LoadSubCommand(command_name, command_sp);
  }
}

void StructuredDataPlugin::ModulesDidLoad(Process &process,
                                          ModuleList &module_list) {
  // Default implementation does nothing.
}
