//===-- fooplugin.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/*
An example plugin for LLDB that provides a new foo command with a child
subcommand
Compile this into a dylib foo.dylib and load by placing in appropriate locations
on disk or
by typing plugin load foo.dylib at the LLDB command line
*/

#include <LLDB/SBCommandInterpreter.h>
#include <LLDB/SBCommandReturnObject.h>
#include <LLDB/SBDebugger.h>

namespace lldb {
bool PluginInitialize(lldb::SBDebugger debugger);
}

class ChildCommand : public lldb::SBCommandPluginInterface {
public:
  virtual bool DoExecute(lldb::SBDebugger debugger, char **command,
                         lldb::SBCommandReturnObject &result) {
    if (command) {
      const char *arg = *command;
      while (arg) {
        result.Printf("%s\n", arg);
        arg = *(++command);
      }
      return true;
    }
    return false;
  }
};

bool lldb::PluginInitialize(lldb::SBDebugger debugger) {
  lldb::SBCommandInterpreter interpreter = debugger.GetCommandInterpreter();
  lldb::SBCommand foo = interpreter.AddMultiwordCommand("foo", NULL);
  foo.AddCommand("child", new ChildCommand(), "a child of foo");
  return true;
}
