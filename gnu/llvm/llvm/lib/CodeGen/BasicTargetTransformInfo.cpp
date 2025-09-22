//===- BasicTargetTransformInfo.cpp - Basic target-independent TTI impl ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the implementation of a basic TargetTransformInfo pass
/// predicated on the target abstractions present in the target independent
/// code generator. It uses these (primarily TargetLowering) to model as much
/// of the TTI query interface as possible. It is included by most targets so
/// that they can specialize only a small subset of the query space.
///
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

// This flag is used by the template base class for BasicTTIImpl, and here to
// provide a definition.
cl::opt<unsigned>
llvm::PartialUnrollingThreshold("partial-unrolling-threshold", cl::init(0),
                                cl::desc("Threshold for partial unrolling"),
                                cl::Hidden);

BasicTTIImpl::BasicTTIImpl(const TargetMachine *TM, const Function &F)
    : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
      TLI(ST->getTargetLowering()) {}
