//==- ConstantHoisting.h - Prepare code for expensive constants --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass identifies expensive constants to hoist and coalesces them to
// better prepare it for SelectionDAG-based code generation. This works around
// the limitations of the basic-block-at-a-time approach.
//
// First it scans all instructions for integer constants and calculates its
// cost. If the constant can be folded into the instruction (the cost is
// TCC_Free) or the cost is just a simple operation (TCC_BASIC), then we don't
// consider it expensive and leave it alone. This is the default behavior and
// the default implementation of getIntImmCostInst will always return TCC_Free.
//
// If the cost is more than TCC_BASIC, then the integer constant can't be folded
// into the instruction and it might be beneficial to hoist the constant.
// Similar constants are coalesced to reduce register pressure and
// materialization code.
//
// When a constant is hoisted, it is also hidden behind a bitcast to force it to
// be live-out of the basic block. Otherwise the constant would be just
// duplicated and each basic block would have its own copy in the SelectionDAG.
// The SelectionDAG recognizes such constants as opaque and doesn't perform
// certain transformations on them, which would create a new expensive constant.
//
// This optimization is only applied to integer constants in instructions and
// simple (this means not nested) constant cast expressions. For example:
// %0 = load i64* inttoptr (i64 big_constant to i64*)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_CONSTANTHOISTING_H
#define LLVM_TRANSFORMS_SCALAR_CONSTANTHOISTING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/PassManager.h"
#include <algorithm>
#include <vector>

namespace llvm {

class BasicBlock;
class BlockFrequencyInfo;
class Constant;
class ConstantInt;
class ConstantExpr;
class DominatorTree;
class Function;
class GlobalVariable;
class Instruction;
class ProfileSummaryInfo;
class TargetTransformInfo;
class TargetTransformInfo;

/// A private "module" namespace for types and utilities used by
/// ConstantHoisting. These are implementation details and should not be used by
/// clients.
namespace consthoist {

/// Keeps track of the user of a constant and the operand index where the
/// constant is used.
struct ConstantUser {
  Instruction *Inst;
  unsigned OpndIdx;

  ConstantUser(Instruction *Inst, unsigned Idx) : Inst(Inst), OpndIdx(Idx) {}
};

using ConstantUseListType = SmallVector<ConstantUser, 8>;

/// Keeps track of a constant candidate and its uses.
struct ConstantCandidate {
  ConstantUseListType Uses;
  // If the candidate is a ConstantExpr (currely only constant GEP expressions
  // whose base pointers are GlobalVariables are supported), ConstInt records
  // its offset from the base GV, ConstExpr tracks the candidate GEP expr.
  ConstantInt *ConstInt;
  ConstantExpr *ConstExpr;
  unsigned CumulativeCost = 0;

  ConstantCandidate(ConstantInt *ConstInt, ConstantExpr *ConstExpr=nullptr) :
      ConstInt(ConstInt), ConstExpr(ConstExpr) {}

  /// Add the user to the use list and update the cost.
  void addUser(Instruction *Inst, unsigned Idx, unsigned Cost) {
    CumulativeCost += Cost;
    Uses.push_back(ConstantUser(Inst, Idx));
  }
};

/// This represents a constant that has been rebased with respect to a
/// base constant. The difference to the base constant is recorded in Offset.
struct RebasedConstantInfo {
  ConstantUseListType Uses;
  Constant *Offset;
  Type *Ty;

  RebasedConstantInfo(ConstantUseListType &&Uses, Constant *Offset,
      Type *Ty=nullptr) : Uses(std::move(Uses)), Offset(Offset), Ty(Ty) {}
};

using RebasedConstantListType = SmallVector<RebasedConstantInfo, 4>;

/// A base constant and all its rebased constants.
struct ConstantInfo {
  // If the candidate is a ConstantExpr (currely only constant GEP expressions
  // whose base pointers are GlobalVariables are supported), ConstInt records
  // its offset from the base GV, ConstExpr tracks the candidate GEP expr.
  ConstantInt *BaseInt;
  ConstantExpr *BaseExpr;
  RebasedConstantListType RebasedConstants;
};

} // end namespace consthoist

class ConstantHoistingPass : public PassInfoMixin<ConstantHoistingPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Glue for old PM.
  bool runImpl(Function &F, TargetTransformInfo &TTI, DominatorTree &DT,
               BlockFrequencyInfo *BFI, BasicBlock &Entry,
               ProfileSummaryInfo *PSI);

  void cleanup() {
    ClonedCastMap.clear();
    ConstIntCandVec.clear();
    for (auto MapEntry : ConstGEPCandMap)
      MapEntry.second.clear();
    ConstGEPCandMap.clear();
    ConstIntInfoVec.clear();
    for (auto MapEntry : ConstGEPInfoMap)
      MapEntry.second.clear();
    ConstGEPInfoMap.clear();
  }

private:
  using ConstPtrUnionType = PointerUnion<ConstantInt *, ConstantExpr *>;
  using ConstCandMapType = DenseMap<ConstPtrUnionType, unsigned>;

  const TargetTransformInfo *TTI;
  DominatorTree *DT;
  BlockFrequencyInfo *BFI;
  LLVMContext *Ctx;
  const DataLayout *DL;
  BasicBlock *Entry;
  ProfileSummaryInfo *PSI;
  bool OptForSize;

  /// Keeps track of constant candidates found in the function.
  using ConstCandVecType = std::vector<consthoist::ConstantCandidate>;
  using GVCandVecMapType = MapVector<GlobalVariable *, ConstCandVecType>;
  ConstCandVecType ConstIntCandVec;
  GVCandVecMapType ConstGEPCandMap;

  /// These are the final constants we decided to hoist.
  using ConstInfoVecType = SmallVector<consthoist::ConstantInfo, 8>;
  using GVInfoVecMapType = MapVector<GlobalVariable *, ConstInfoVecType>;
  ConstInfoVecType ConstIntInfoVec;
  GVInfoVecMapType ConstGEPInfoMap;

  /// Keep track of cast instructions we already cloned.
  MapVector<Instruction *, Instruction *> ClonedCastMap;

  void collectMatInsertPts(
      const consthoist::RebasedConstantListType &RebasedConstants,
      SmallVectorImpl<BasicBlock::iterator> &MatInsertPts) const;
  BasicBlock::iterator findMatInsertPt(Instruction *Inst,
                                       unsigned Idx = ~0U) const;
  SetVector<BasicBlock::iterator> findConstantInsertionPoint(
      const consthoist::ConstantInfo &ConstInfo,
      const ArrayRef<BasicBlock::iterator> MatInsertPts) const;
  void collectConstantCandidates(ConstCandMapType &ConstCandMap,
                                 Instruction *Inst, unsigned Idx,
                                 ConstantInt *ConstInt);
  void collectConstantCandidates(ConstCandMapType &ConstCandMap,
                                 Instruction *Inst, unsigned Idx,
                                 ConstantExpr *ConstExpr);
  void collectConstantCandidates(ConstCandMapType &ConstCandMap,
                                 Instruction *Inst, unsigned Idx);
  void collectConstantCandidates(ConstCandMapType &ConstCandMap,
                                 Instruction *Inst);
  void collectConstantCandidates(Function &Fn);
  void findAndMakeBaseConstant(ConstCandVecType::iterator S,
                               ConstCandVecType::iterator E,
      SmallVectorImpl<consthoist::ConstantInfo> &ConstInfoVec);
  unsigned maximizeConstantsInRange(ConstCandVecType::iterator S,
                                    ConstCandVecType::iterator E,
                                    ConstCandVecType::iterator &MaxCostItr);
  // If BaseGV is nullptr, find base among Constant Integer candidates;
  // otherwise find base among constant GEPs sharing BaseGV as base pointer.
  void findBaseConstants(GlobalVariable *BaseGV);

  /// A ConstantUser grouped with the Type and Constant adjustment. The user
  /// will be adjusted by Offset.
  struct UserAdjustment {
    Constant *Offset;
    Type *Ty;
    BasicBlock::iterator MatInsertPt;
    const consthoist::ConstantUser User;
    UserAdjustment(Constant *O, Type *T, BasicBlock::iterator I,
                   consthoist::ConstantUser U)
        : Offset(O), Ty(T), MatInsertPt(I), User(U) {}
  };
  void emitBaseConstants(Instruction *Base, UserAdjustment *Adj);
  // If BaseGV is nullptr, emit Constant Integer base; otherwise emit
  // constant GEP base.
  bool emitBaseConstants(GlobalVariable *BaseGV);
  void deleteDeadCastInst() const;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_CONSTANTHOISTING_H
