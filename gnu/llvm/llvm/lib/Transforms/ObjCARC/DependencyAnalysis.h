//===- DependencyAnalysis.h - ObjC ARC Optimization ---*- C++ -*-----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares special dependency analysis routines used in Objective C
/// ARC Optimizations.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_OBJCARC_DEPENDENCYANALYSIS_H
#define LLVM_LIB_TRANSFORMS_OBJCARC_DEPENDENCYANALYSIS_H

#include "llvm/Analysis/ObjCARCInstKind.h"

namespace llvm {
  class BasicBlock;
  class Instruction;
  class Value;
}

namespace llvm {
namespace objcarc {

class ProvenanceAnalysis;

/// \enum DependenceKind
/// Defines different dependence kinds among various ARC constructs.
///
/// There are several kinds of dependence-like concepts in use here.
///
enum DependenceKind {
  NeedsPositiveRetainCount,
  AutoreleasePoolBoundary,
  CanChangeRetainCount,
  RetainAutoreleaseDep,       ///< Blocks objc_retainAutorelease.
  RetainAutoreleaseRVDep      ///< Blocks objc_retainAutoreleaseReturnValue.
};

/// Find dependent instructions. If there is exactly one dependent instruction,
/// return it. Otherwise, return null.
llvm::Instruction *findSingleDependency(DependenceKind Flavor, const Value *Arg,
                                        BasicBlock *StartBB,
                                        Instruction *StartInst,
                                        ProvenanceAnalysis &PA);

bool
Depends(DependenceKind Flavor, Instruction *Inst, const Value *Arg,
        ProvenanceAnalysis &PA);

/// Test whether the given instruction can "use" the given pointer's object in a
/// way that requires the reference count to be positive.
bool CanUse(const Instruction *Inst, const Value *Ptr, ProvenanceAnalysis &PA,
            ARCInstKind Class);

/// Test whether the given instruction can result in a reference count
/// modification (positive or negative) for the pointer's object.
bool CanAlterRefCount(const Instruction *Inst, const Value *Ptr,
                      ProvenanceAnalysis &PA, ARCInstKind Class);

/// Returns true if we can not conservatively prove that Inst can not decrement
/// the reference count of Ptr. Returns false if we can.
bool CanDecrementRefCount(const Instruction *Inst, const Value *Ptr,
                          ProvenanceAnalysis &PA, ARCInstKind Class);

static inline bool CanDecrementRefCount(const Instruction *Inst,
                                        const Value *Ptr,
                                        ProvenanceAnalysis &PA) {
  return CanDecrementRefCount(Inst, Ptr, PA, GetARCInstKind(Inst));
}

} // namespace objcarc
} // namespace llvm

#endif
