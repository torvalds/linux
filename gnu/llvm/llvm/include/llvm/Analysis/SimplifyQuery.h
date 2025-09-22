//===-- SimplifyQuery.h - Context for simplifications -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SIMPLIFYQUERY_H
#define LLVM_ANALYSIS_SIMPLIFYQUERY_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Operator.h"

namespace llvm {

class AssumptionCache;
class DomConditionCache;
class DominatorTree;
class TargetLibraryInfo;

/// InstrInfoQuery provides an interface to query additional information for
/// instructions like metadata or keywords like nsw, which provides conservative
/// results if the users specified it is safe to use.
struct InstrInfoQuery {
  InstrInfoQuery(bool UMD) : UseInstrInfo(UMD) {}
  InstrInfoQuery() = default;
  bool UseInstrInfo = true;

  MDNode *getMetadata(const Instruction *I, unsigned KindID) const {
    if (UseInstrInfo)
      return I->getMetadata(KindID);
    return nullptr;
  }

  template <class InstT> bool hasNoUnsignedWrap(const InstT *Op) const {
    if (UseInstrInfo)
      return Op->hasNoUnsignedWrap();
    return false;
  }

  template <class InstT> bool hasNoSignedWrap(const InstT *Op) const {
    if (UseInstrInfo)
      return Op->hasNoSignedWrap();
    return false;
  }

  bool isExact(const BinaryOperator *Op) const {
    if (UseInstrInfo && isa<PossiblyExactOperator>(Op))
      return cast<PossiblyExactOperator>(Op)->isExact();
    return false;
  }

  template <class InstT> bool hasNoSignedZeros(const InstT *Op) const {
    if (UseInstrInfo)
      return Op->hasNoSignedZeros();
    return false;
  }
};

/// Evaluate query assuming this condition holds.
struct CondContext {
  Value *Cond;
  bool Invert = false;
  SmallPtrSet<Value *, 4> AffectedValues;

  CondContext(Value *Cond) : Cond(Cond) {}
};

struct SimplifyQuery {
  const DataLayout &DL;
  const TargetLibraryInfo *TLI = nullptr;
  const DominatorTree *DT = nullptr;
  AssumptionCache *AC = nullptr;
  const Instruction *CxtI = nullptr;
  const DomConditionCache *DC = nullptr;
  const CondContext *CC = nullptr;

  // Wrapper to query additional information for instructions like metadata or
  // keywords like nsw, which provides conservative results if those cannot
  // be safely used.
  const InstrInfoQuery IIQ;

  /// Controls whether simplifications are allowed to constrain the range of
  /// possible values for uses of undef. If it is false, simplifications are not
  /// allowed to assume a particular value for a use of undef for example.
  bool CanUseUndef = true;

  SimplifyQuery(const DataLayout &DL, const Instruction *CXTI = nullptr)
      : DL(DL), CxtI(CXTI) {}

  SimplifyQuery(const DataLayout &DL, const TargetLibraryInfo *TLI,
                const DominatorTree *DT = nullptr,
                AssumptionCache *AC = nullptr,
                const Instruction *CXTI = nullptr, bool UseInstrInfo = true,
                bool CanUseUndef = true, const DomConditionCache *DC = nullptr)
      : DL(DL), TLI(TLI), DT(DT), AC(AC), CxtI(CXTI), DC(DC), IIQ(UseInstrInfo),
        CanUseUndef(CanUseUndef) {}

  SimplifyQuery(const DataLayout &DL, const DominatorTree *DT,
                AssumptionCache *AC = nullptr,
                const Instruction *CXTI = nullptr, bool UseInstrInfo = true,
                bool CanUseUndef = true)
      : DL(DL), DT(DT), AC(AC), CxtI(CXTI), IIQ(UseInstrInfo),
        CanUseUndef(CanUseUndef) {}

  SimplifyQuery getWithInstruction(const Instruction *I) const {
    SimplifyQuery Copy(*this);
    Copy.CxtI = I;
    return Copy;
  }
  SimplifyQuery getWithoutUndef() const {
    SimplifyQuery Copy(*this);
    Copy.CanUseUndef = false;
    return Copy;
  }

  /// If CanUseUndef is true, returns whether \p V is undef.
  /// Otherwise always return false.
  bool isUndefValue(Value *V) const;

  SimplifyQuery getWithoutDomCondCache() const {
    SimplifyQuery Copy(*this);
    Copy.DC = nullptr;
    return Copy;
  }

  SimplifyQuery getWithCondContext(const CondContext &CC) const {
    SimplifyQuery Copy(*this);
    Copy.CC = &CC;
    return Copy;
  }

  SimplifyQuery getWithoutCondContext() const {
    SimplifyQuery Copy(*this);
    Copy.CC = nullptr;
    return Copy;
  }
};

} // end namespace llvm

#endif
