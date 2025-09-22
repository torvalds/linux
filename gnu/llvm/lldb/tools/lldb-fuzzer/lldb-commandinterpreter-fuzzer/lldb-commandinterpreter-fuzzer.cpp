//===-- lldb-commandinterpreter-fuzzer.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include <string>

#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandInterpreterRunOptions.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBTarget.h"

using namespace lldb;

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  SBDebugger::Initialize();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t *data, size_t size) {
  // Convert the data into a null-terminated string
  std::string str((char *)data, size);

  // Create a debugger and a dummy target
  SBDebugger debugger = SBDebugger::Create(false);
  SBTarget target = debugger.GetDummyTarget();

  // Create a command interpreter for the current debugger
  // A return object is needed to run the command interpreter
  SBCommandReturnObject ro = SBCommandReturnObject();
  SBCommandInterpreter ci = debugger.GetCommandInterpreter();

  // Use the fuzzer generated input as input for the command interpreter
  if (ci.IsValid()) {
    ci.HandleCommand(str.c_str(), ro, false);
  }

  debugger.DeleteTarget(target);
  SBDebugger::Destroy(debugger);
  SBModule::GarbageCollectAllocatedModules();

  return 0;
}
