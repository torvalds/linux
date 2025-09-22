//===-- CommandObjectDiagnostics.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectDiagnostics.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValueEnumeration.h"
#include "lldb/Interpreter/OptionValueUInt64.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/Diagnostics.h"

using namespace lldb;
using namespace lldb_private;

#define LLDB_OPTIONS_diagnostics_dump
#include "CommandOptions.inc"

class CommandObjectDiagnosticsDump : public CommandObjectParsed {
public:
  // Constructors and Destructors
  CommandObjectDiagnosticsDump(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "diagnostics dump",
                            "Dump diagnostics to disk", nullptr) {}

  ~CommandObjectDiagnosticsDump() override = default;

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'd':
        directory.SetDirectory(option_arg);
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      directory.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_diagnostics_dump_options);
    }

    FileSpec directory;
  };

  Options *GetOptions() override { return &m_options; }

protected:
  llvm::Expected<FileSpec> GetDirectory() {
    if (m_options.directory) {
      auto ec =
          llvm::sys::fs::create_directories(m_options.directory.GetPath());
      if (ec)
        return llvm::errorCodeToError(ec);
      return m_options.directory;
    }
    return Diagnostics::CreateUniqueDirectory();
  }

  void DoExecute(Args &args, CommandReturnObject &result) override {
    llvm::Expected<FileSpec> directory = GetDirectory();

    if (!directory) {
      result.AppendError(llvm::toString(directory.takeError()));
      return;
    }

    llvm::Error error = Diagnostics::Instance().Create(*directory);
    if (error) {
      result.AppendErrorWithFormat("failed to write diagnostics to %s",
                                   directory->GetPath().c_str());
      result.AppendError(llvm::toString(std::move(error)));
      return;
    }

    result.GetOutputStream() << "diagnostics written to " << *directory << '\n';

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return;
  }

  CommandOptions m_options;
};

CommandObjectDiagnostics::CommandObjectDiagnostics(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "diagnostics",
                             "Commands controlling LLDB diagnostics.",
                             "diagnostics <subcommand> [<command-options>]") {
  LoadSubCommand(
      "dump", CommandObjectSP(new CommandObjectDiagnosticsDump(interpreter)));
}

CommandObjectDiagnostics::~CommandObjectDiagnostics() = default;
