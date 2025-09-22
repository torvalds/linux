//===-- XCore.h - Top-level interface for XCore representation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// XCore back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCORE_H
#define LLVM_LIB_TARGET_XCORE_XCORE_H

#include "MCTargetDesc/XCoreMCTargetDesc.h"
#include "llvm/PassRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
  class FunctionPass;
  class ModulePass;
  class PassRegistry;
  class TargetMachine;
  class XCoreTargetMachine;

  void initializeXCoreLowerThreadLocalPass(PassRegistry &p);

  FunctionPass *createXCoreFrameToArgsOffsetEliminationPass();
  FunctionPass *createXCoreISelDag(XCoreTargetMachine &TM,
                                   CodeGenOptLevel OptLevel);
  ModulePass *createXCoreLowerThreadLocalPass();
  void initializeXCoreDAGToDAGISelLegacyPass(PassRegistry &);

} // end namespace llvm;

#endif
