//===-- Driver.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DRIVER_DRIVER_H
#define LLDB_TOOLS_DRIVER_DRIVER_H

#include "Platform.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"

#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"

#include <set>
#include <string>
#include <vector>

class Driver : public lldb::SBBroadcaster {
public:
  enum CommandPlacement {
    eCommandPlacementBeforeFile,
    eCommandPlacementAfterFile,
    eCommandPlacementAfterCrash,
  };

  Driver();

  virtual ~Driver();

  /// Runs the main loop.
  ///
  /// \return The exit code that the process should return.
  int MainLoop();

  lldb::SBError ProcessArgs(const llvm::opt::InputArgList &args, bool &exiting);

  void WriteCommandsForSourcing(CommandPlacement placement,
                                lldb::SBStream &strm);

  struct OptionData {
    void AddInitialCommand(std::string command, CommandPlacement placement,
                           bool is_file, lldb::SBError &error);

    struct InitialCmdEntry {
      InitialCmdEntry(std::string contents, bool in_is_file,
                      bool in_quiet = false)
          : contents(std::move(contents)), is_file(in_is_file),
            source_quietly(in_quiet) {}

      std::string contents;
      bool is_file;
      bool source_quietly;
    };

    std::vector<std::string> m_args;

    lldb::LanguageType m_repl_lang = lldb::eLanguageTypeUnknown;
    lldb::pid_t m_process_pid = LLDB_INVALID_PROCESS_ID;

    std::string m_core_file;
    std::string m_crash_log;
    std::string m_repl_options;
    std::string m_process_name;

    std::vector<InitialCmdEntry> m_initial_commands;
    std::vector<InitialCmdEntry> m_after_file_commands;
    std::vector<InitialCmdEntry> m_after_crash_commands;

    bool m_source_quietly = false;
    bool m_print_version = false;
    bool m_print_python_path = false;
    bool m_print_script_interpreter_info = false;
    bool m_wait_for = false;
    bool m_repl = false;
    bool m_batch = false;

    // FIXME: When we have set/show variables we can remove this from here.
    bool m_use_external_editor = false;

    using OptionSet = std::set<char>;
    OptionSet m_seen_options;
  };

  lldb::SBDebugger &GetDebugger() { return m_debugger; }

  void ResizeWindow(unsigned short col);

private:
  lldb::SBDebugger m_debugger;
  OptionData m_option_data;
};

#endif // LLDB_TOOLS_DRIVER_DRIVER_H
