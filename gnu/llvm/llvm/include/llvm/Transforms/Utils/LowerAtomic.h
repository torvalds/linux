//===- LowerAtomic.h - Lower atomic intrinsics ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
// This pass lowers atomic intrinsics to non-atomic form for use in a known
// non-preemptible environment.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOWERATOMIC_H
#define LLVM_TRANSFORMS_UTILS_LOWERATOMIC_H

#include "llvm/IR/Instructions.h"

namespace llvm {

class IRBuilderBase;

/// Convert the given Cmpxchg into primitive load and compare.
bool lowerAtomicCmpXchgInst(AtomicCmpXchgInst *CXI);

/// Convert the given RMWI into primitive load and stores,
/// assuming that doing so is legal. Return true if the lowering
/// succeeds.
bool lowerAtomicRMWInst(AtomicRMWInst *RMWI);

/// Emit IR to implement the given atomicrmw operation on values in registers,
/// returning the new value.
Value *buildAtomicRMWValue(AtomicRMWInst::BinOp Op, IRBuilderBase &Builder,
                           Value *Loaded, Value *Val);
}

#endif // LLVM_TRANSFORMS_UTILS_LOWERATOMIC_H
