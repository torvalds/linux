//===- PromoteMemToReg.h - Promote Allocas to Scalars -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes an interface to promote alloca instructions to SSA
// registers, by using the SSA construction algorithm.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_PROMOTEMEMTOREG_H
#define LLVM_TRANSFORMS_UTILS_PROMOTEMEMTOREG_H

namespace llvm {

template <typename T> class ArrayRef;
class AllocaInst;
class DominatorTree;
class AssumptionCache;

/// Return true if this alloca is legal for promotion.
///
/// This is true if there are only loads, stores, and lifetime markers
/// (transitively) using this alloca. This also enforces that there is only
/// ever one layer of bitcasts or GEPs between the alloca and the lifetime
/// markers.
bool isAllocaPromotable(const AllocaInst *AI);

/// Promote the specified list of alloca instructions into scalar
/// registers, inserting PHI nodes as appropriate.
///
/// This function makes use of DominanceFrontier information.  This function
/// does not modify the CFG of the function at all.  All allocas must be from
/// the same function.
///
void PromoteMemToReg(ArrayRef<AllocaInst *> Allocas, DominatorTree &DT,
                     AssumptionCache *AC = nullptr);

} // End llvm namespace

#endif
