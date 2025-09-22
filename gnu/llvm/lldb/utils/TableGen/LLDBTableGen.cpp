//===- LLDBTableGen.cpp - Top-Level TableGen implementation for LLDB ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the main function for LLDB's TableGen.
//
//===----------------------------------------------------------------------===//

#include "LLDBTableGenBackends.h" // Declares all backends.
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Main.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;
using namespace lldb_private;

enum ActionType {
  PrintRecords,
  DumpJSON,
  GenOptionDefs,
  GenPropertyDefs,
  GenPropertyEnumDefs,
};

static cl::opt<ActionType> Action(
    cl::desc("Action to perform:"),
    cl::values(clEnumValN(PrintRecords, "print-records",
                          "Print all records to stdout (default)"),
               clEnumValN(DumpJSON, "dump-json",
                          "Dump all records as machine-readable JSON"),
               clEnumValN(GenOptionDefs, "gen-lldb-option-defs",
                          "Generate lldb option definitions"),
               clEnumValN(GenPropertyDefs, "gen-lldb-property-defs",
                          "Generate lldb property definitions"),
               clEnumValN(GenPropertyEnumDefs, "gen-lldb-property-enum-defs",
                          "Generate lldb property enum definitions")));

static bool LLDBTableGenMain(raw_ostream &OS, RecordKeeper &Records) {
  switch (Action) {
  case PrintRecords:
    OS << Records; // No argument, dump all contents
    break;
  case DumpJSON:
    EmitJSON(Records, OS);
    break;
  case GenOptionDefs:
    EmitOptionDefs(Records, OS);
    break;
  case GenPropertyDefs:
    EmitPropertyDefs(Records, OS);
    break;
  case GenPropertyEnumDefs:
    EmitPropertyEnumDefs(Records, OS);
    break;
  }
  return false;
}

int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv);
  llvm_shutdown_obj Y;

  return TableGenMain(argv[0], &LLDBTableGenMain);
}

#ifdef __has_feature
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
// Disable LeakSanitizer for this binary as it has too many leaks that are not
// very interesting to fix. See compiler-rt/include/sanitizer/lsan_interface.h .
int __lsan_is_turned_off() { return 1; }
#endif // __has_feature(address_sanitizer)
#endif // defined(__has_feature)
