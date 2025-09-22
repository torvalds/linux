//===- ObjCARCAnalysisUtils.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements common infrastructure for libLLVMObjCARCOpts.a, which
// implements several scalar transformations over the LLVM intermediate
// representation, including the C bindings for that library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace llvm::objcarc;

/// A handy option to enable/disable all ARC Optimizations.
bool llvm::objcarc::EnableARCOpts;
static cl::opt<bool, true> EnableARCOptimizations(
    "enable-objc-arc-opts", cl::desc("enable/disable all ARC Optimizations"),
    cl::location(EnableARCOpts), cl::init(true), cl::Hidden);

bool llvm::objcarc::IsPotentialRetainableObjPtr(const Value *Op,
                                                AAResults &AA) {
  // First make the rudimentary check.
  if (!IsPotentialRetainableObjPtr(Op))
    return false;

  // Objects in constant memory are not reference-counted.
  if (AA.pointsToConstantMemory(Op))
    return false;

  // Pointers in constant memory are not pointing to reference-counted objects.
  if (const LoadInst *LI = dyn_cast<LoadInst>(Op))
    if (AA.pointsToConstantMemory(LI->getPointerOperand()))
      return false;

  // Otherwise assume the worst.
  return true;
}
