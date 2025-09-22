//===-- IPO.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the common infrastructure (including C bindings) for
// libLLVMIPO.a, which implements several transformations over the LLVM
// intermediate representation.
//
//===----------------------------------------------------------------------===//

#include "llvm/InitializePasses.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"

using namespace llvm;

void llvm::initializeIPO(PassRegistry &Registry) {
  initializeDAEPass(Registry);
  initializeDAHPass(Registry);
  initializeAlwaysInlinerLegacyPassPass(Registry);
  initializeLoopExtractorLegacyPassPass(Registry);
  initializeSingleLoopExtractorPass(Registry);
  initializeBarrierNoopPass(Registry);
}
