//===-- lldb-expression-fuzzer.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// \file
// This file is a fuzzer for LLDB's expression evaluator. It uses protobufs
// and the libprotobuf-mutator to create valid C-like inputs for the
// expression evaluator.
//
//===---------------------------------------------------------------------===//

#include <string>

#include "cxx_proto.pb.h"
#include "handle-cxx/handle_cxx.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBLaunchInfo.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include "proto-to-cxx/proto_to_cxx.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/WithColor.h"

using namespace lldb;
using namespace llvm;
using namespace clang_fuzzer;

const char *target_path = nullptr;

void ReportError(llvm::StringRef message) {
  WithColor::error() << message << '\n';
  exit(1);
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
#if !defined(_WIN32)
  signal(SIGPIPE, SIG_IGN);
#endif

  // `target_path` can be set by either the "--lldb_fuzzer_target" commandline
  // flag or the "LLDB_FUZZER_TARGET" environment variable. Arbitrarily, we
  // always do flag parsing and only check the environment variable if the
  // commandline flag is not set.
  for (int i = 1; i < *argc; ++i) {
    auto this_arg = llvm::StringRef((*argv)[i]);
    WithColor::note() << "argv[" << i << "] = " << this_arg << "\n";
    if (this_arg.consume_front("--lldb_fuzzer_target="))
      target_path = this_arg.data();
  }

  if (!target_path)
    target_path = ::getenv("LLDB_FUZZER_TARGET");

  if (!target_path)
    ReportError("No target path specified. Set one either as an environment "
                "variable (i.e. LLDB_FUZZER_TARGET=target_path) or pass as a "
                "command line flag (i.e. --lldb_fuzzer_target=target_path).");

  if (!sys::fs::exists(target_path))
    ReportError(formatv("target path '{0}' does not exist", target_path).str());

  SBDebugger::Initialize();

  return 0;
}

DEFINE_BINARY_PROTO_FUZZER(const clang_fuzzer::Function &input) {
  std::string expression = clang_fuzzer::FunctionToString(input);

  // Create a debugger and a target
  SBDebugger debugger = SBDebugger::Create(false);
  if (!debugger.IsValid())
    ReportError("Couldn't create debugger");

  SBTarget target = debugger.CreateTarget(target_path);
  if (!target.IsValid())
    ReportError(formatv("Couldn't create target '{0}'", target_path).str());

  // Create a breakpoint on the only line in the program
  SBBreakpoint breakpoint = target.BreakpointCreateByName("main", target_path);
  if (!breakpoint.IsValid())
    ReportError("Couldn't create breakpoint");

  // Create launch info and error for launching the process
  SBLaunchInfo launch_info = target.GetLaunchInfo();
  SBError error;

  // Launch the process and evaluate the fuzzer's input data
  // as an expression
  SBProcess process = target.Launch(launch_info, error);
  if (!process.IsValid() || error.Fail())
    ReportError("Couldn't launch process");

  SBValue value = target.EvaluateExpression(expression.c_str());

  debugger.DeleteTarget(target);
  SBDebugger::Destroy(debugger);
  SBModule::GarbageCollectAllocatedModules();
}
