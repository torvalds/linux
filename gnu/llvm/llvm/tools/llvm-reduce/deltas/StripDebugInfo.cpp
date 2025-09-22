//===- StripDebugInfo.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StripDebugInfo.h"
#include "Delta.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Metadata.h"

using namespace llvm;

/// Removes all aliases aren't inside any of the
/// desired Chunks.
static void stripDebugInfoImpl(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();
  bool HasDebugInfo = any_of(Program.named_metadata(), [](NamedMDNode &NMD) {
    return NMD.getName().starts_with("llvm.dbg.");
  });
  if (HasDebugInfo && !O.shouldKeep())
    StripDebugInfo(Program);
}

void llvm::stripDebugInfoDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, stripDebugInfoImpl, "Stripping Debug Info");
}
