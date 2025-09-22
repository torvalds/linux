//===-- lldb-target-fuzzer.cpp - Fuzz target creation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "utils/TempFile.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBTarget.h"

using namespace lldb;
using namespace lldb_fuzzer;
using namespace llvm;

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  SBDebugger::Initialize();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t *data, size_t size) {
  std::unique_ptr<TempFile> file = TempFile::Create(data, size);
  if (!file)
    return 1;

  SBDebugger debugger = SBDebugger::Create(false);
  SBTarget target = debugger.CreateTarget(file->GetPath().data());
  debugger.DeleteTarget(target);
  SBDebugger::Destroy(debugger);
  SBModule::GarbageCollectAllocatedModules();

  return 0;
}
