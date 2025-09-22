//===- ProvenanceAnalysis.h - ObjC ARC Optimization -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// This file declares a special form of Alias Analysis called ``Provenance
/// Analysis''. The word ``provenance'' refers to the history of the ownership
/// of an object. Thus ``Provenance Analysis'' is an analysis which attempts to
/// use various techniques to determine if locally
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_OBJCARC_PROVENANCEANALYSIS_H
#define LLVM_LIB_TRANSFORMS_OBJCARC_PROVENANCEANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include <utility>

namespace llvm {

class AAResults;
class PHINode;
class SelectInst;
class Value;

namespace objcarc {

/// This is similar to BasicAliasAnalysis, and it uses many of the same
/// techniques, except it uses special ObjC-specific reasoning about pointer
/// relationships.
///
/// In this context ``Provenance'' is defined as the history of an object's
/// ownership. Thus ``Provenance Analysis'' is defined by using the notion of
/// an ``independent provenance source'' of a pointer to determine whether or
/// not two pointers have the same provenance source and thus could
/// potentially be related.
class ProvenanceAnalysis {
  AAResults *AA;

  using ValuePairTy = std::pair<const Value *, const Value *>;
  using CachedResultsTy = DenseMap<ValuePairTy, bool>;

  CachedResultsTy CachedResults;

  DenseMap<const Value *, std::pair<WeakVH, WeakTrackingVH>>
      UnderlyingObjCPtrCache;

  bool relatedCheck(const Value *A, const Value *B);
  bool relatedSelect(const SelectInst *A, const Value *B);
  bool relatedPHI(const PHINode *A, const Value *B);

public:
  ProvenanceAnalysis() = default;
  ProvenanceAnalysis(const ProvenanceAnalysis &) = delete;
  ProvenanceAnalysis &operator=(const ProvenanceAnalysis &) = delete;

  void setAA(AAResults *aa) { AA = aa; }

  AAResults *getAA() const { return AA; }

  bool related(const Value *A, const Value *B);

  void clear() {
    CachedResults.clear();
    UnderlyingObjCPtrCache.clear();
  }
};

} // end namespace objcarc

} // end namespace llvm

#endif // LLVM_LIB_TRANSFORMS_OBJCARC_PROVENANCEANALYSIS_H
