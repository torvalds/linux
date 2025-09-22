//===- BasicTTIImpl.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides a helper that implements much of the TTI interface in
/// terms of the target-independent code generator and TargetLowering
/// interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BASICTTIIMPL_H
#define LLVM_CODEGEN_BASICTTIIMPL_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfoImpl.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace llvm {

class Function;
class GlobalValue;
class LLVMContext;
class ScalarEvolution;
class SCEV;
class TargetMachine;

extern cl::opt<unsigned> PartialUnrollingThreshold;

/// Base class which can be used to help build a TTI implementation.
///
/// This class provides as much implementation of the TTI interface as is
/// possible using the target independent parts of the code generator.
///
/// In order to subclass it, your class must implement a getST() method to
/// return the subtarget, and a getTLI() method to return the target lowering.
/// We need these methods implemented in the derived class so that this class
/// doesn't have to duplicate storage for them.
template <typename T>
class BasicTTIImplBase : public TargetTransformInfoImplCRTPBase<T> {
private:
  using BaseT = TargetTransformInfoImplCRTPBase<T>;
  using TTI = TargetTransformInfo;

  /// Helper function to access this as a T.
  T *thisT() { return static_cast<T *>(this); }

  /// Estimate a cost of Broadcast as an extract and sequence of insert
  /// operations.
  InstructionCost getBroadcastShuffleOverhead(FixedVectorType *VTy,
                                              TTI::TargetCostKind CostKind) {
    InstructionCost Cost = 0;
    // Broadcast cost is equal to the cost of extracting the zero'th element
    // plus the cost of inserting it into every element of the result vector.
    Cost += thisT()->getVectorInstrCost(Instruction::ExtractElement, VTy,
                                        CostKind, 0, nullptr, nullptr);

    for (int i = 0, e = VTy->getNumElements(); i < e; ++i) {
      Cost += thisT()->getVectorInstrCost(Instruction::InsertElement, VTy,
                                          CostKind, i, nullptr, nullptr);
    }
    return Cost;
  }

  /// Estimate a cost of shuffle as a sequence of extract and insert
  /// operations.
  InstructionCost getPermuteShuffleOverhead(FixedVectorType *VTy,
                                            TTI::TargetCostKind CostKind) {
    InstructionCost Cost = 0;
    // Shuffle cost is equal to the cost of extracting element from its argument
    // plus the cost of inserting them onto the result vector.

    // e.g. <4 x float> has a mask of <0,5,2,7> i.e we need to extract from
    // index 0 of first vector, index 1 of second vector,index 2 of first
    // vector and finally index 3 of second vector and insert them at index
    // <0,1,2,3> of result vector.
    for (int i = 0, e = VTy->getNumElements(); i < e; ++i) {
      Cost += thisT()->getVectorInstrCost(Instruction::InsertElement, VTy,
                                          CostKind, i, nullptr, nullptr);
      Cost += thisT()->getVectorInstrCost(Instruction::ExtractElement, VTy,
                                          CostKind, i, nullptr, nullptr);
    }
    return Cost;
  }

  /// Estimate a cost of subvector extraction as a sequence of extract and
  /// insert operations.
  InstructionCost getExtractSubvectorOverhead(VectorType *VTy,
                                              TTI::TargetCostKind CostKind,
                                              int Index,
                                              FixedVectorType *SubVTy) {
    assert(VTy && SubVTy &&
           "Can only extract subvectors from vectors");
    int NumSubElts = SubVTy->getNumElements();
    assert((!isa<FixedVectorType>(VTy) ||
            (Index + NumSubElts) <=
                (int)cast<FixedVectorType>(VTy)->getNumElements()) &&
           "SK_ExtractSubvector index out of range");

    InstructionCost Cost = 0;
    // Subvector extraction cost is equal to the cost of extracting element from
    // the source type plus the cost of inserting them into the result vector
    // type.
    for (int i = 0; i != NumSubElts; ++i) {
      Cost +=
          thisT()->getVectorInstrCost(Instruction::ExtractElement, VTy,
                                      CostKind, i + Index, nullptr, nullptr);
      Cost += thisT()->getVectorInstrCost(Instruction::InsertElement, SubVTy,
                                          CostKind, i, nullptr, nullptr);
    }
    return Cost;
  }

  /// Estimate a cost of subvector insertion as a sequence of extract and
  /// insert operations.
  InstructionCost getInsertSubvectorOverhead(VectorType *VTy,
                                             TTI::TargetCostKind CostKind,
                                             int Index,
                                             FixedVectorType *SubVTy) {
    assert(VTy && SubVTy &&
           "Can only insert subvectors into vectors");
    int NumSubElts = SubVTy->getNumElements();
    assert((!isa<FixedVectorType>(VTy) ||
            (Index + NumSubElts) <=
                (int)cast<FixedVectorType>(VTy)->getNumElements()) &&
           "SK_InsertSubvector index out of range");

    InstructionCost Cost = 0;
    // Subvector insertion cost is equal to the cost of extracting element from
    // the source type plus the cost of inserting them into the result vector
    // type.
    for (int i = 0; i != NumSubElts; ++i) {
      Cost += thisT()->getVectorInstrCost(Instruction::ExtractElement, SubVTy,
                                          CostKind, i, nullptr, nullptr);
      Cost +=
          thisT()->getVectorInstrCost(Instruction::InsertElement, VTy, CostKind,
                                      i + Index, nullptr, nullptr);
    }
    return Cost;
  }

  /// Local query method delegates up to T which *must* implement this!
  const TargetSubtargetInfo *getST() const {
    return static_cast<const T *>(this)->getST();
  }

  /// Local query method delegates up to T which *must* implement this!
  const TargetLoweringBase *getTLI() const {
    return static_cast<const T *>(this)->getTLI();
  }

  static ISD::MemIndexedMode getISDIndexedMode(TTI::MemIndexedMode M) {
    switch (M) {
      case TTI::MIM_Unindexed:
        return ISD::UNINDEXED;
      case TTI::MIM_PreInc:
        return ISD::PRE_INC;
      case TTI::MIM_PreDec:
        return ISD::PRE_DEC;
      case TTI::MIM_PostInc:
        return ISD::POST_INC;
      case TTI::MIM_PostDec:
        return ISD::POST_DEC;
    }
    llvm_unreachable("Unexpected MemIndexedMode");
  }

  InstructionCost getCommonMaskedMemoryOpCost(unsigned Opcode, Type *DataTy,
                                              Align Alignment,
                                              bool VariableMask,
                                              bool IsGatherScatter,
                                              TTI::TargetCostKind CostKind,
                                              unsigned AddressSpace = 0) {
    // We cannot scalarize scalable vectors, so return Invalid.
    if (isa<ScalableVectorType>(DataTy))
      return InstructionCost::getInvalid();

    auto *VT = cast<FixedVectorType>(DataTy);
    unsigned VF = VT->getNumElements();

    // Assume the target does not have support for gather/scatter operations
    // and provide a rough estimate.
    //
    // First, compute the cost of the individual memory operations.
    InstructionCost AddrExtractCost =
        IsGatherScatter
            ? getScalarizationOverhead(
                  FixedVectorType::get(
                      PointerType::get(VT->getElementType(), 0), VF),
                  /*Insert=*/false, /*Extract=*/true, CostKind)
            : 0;

    // The cost of the scalar loads/stores.
    InstructionCost MemoryOpCost =
        VF * thisT()->getMemoryOpCost(Opcode, VT->getElementType(), Alignment,
                                      AddressSpace, CostKind);

    // Next, compute the cost of packing the result in a vector.
    InstructionCost PackingCost =
        getScalarizationOverhead(VT, Opcode != Instruction::Store,
                                 Opcode == Instruction::Store, CostKind);

    InstructionCost ConditionalCost = 0;
    if (VariableMask) {
      // Compute the cost of conditionally executing the memory operations with
      // variable masks. This includes extracting the individual conditions, a
      // branches and PHIs to combine the results.
      // NOTE: Estimating the cost of conditionally executing the memory
      // operations accurately is quite difficult and the current solution
      // provides a very rough estimate only.
      ConditionalCost =
          getScalarizationOverhead(
              FixedVectorType::get(Type::getInt1Ty(DataTy->getContext()), VF),
              /*Insert=*/false, /*Extract=*/true, CostKind) +
          VF * (thisT()->getCFInstrCost(Instruction::Br, CostKind) +
                thisT()->getCFInstrCost(Instruction::PHI, CostKind));
    }

    return AddrExtractCost + MemoryOpCost + PackingCost + ConditionalCost;
  }

protected:
  explicit BasicTTIImplBase(const TargetMachine *TM, const DataLayout &DL)
      : BaseT(DL) {}
  virtual ~BasicTTIImplBase() = default;

  using TargetTransformInfoImplBase::DL;

public:
  /// \name Scalar TTI Implementations
  /// @{
  bool allowsMisalignedMemoryAccesses(LLVMContext &Context, unsigned BitWidth,
                                      unsigned AddressSpace, Align Alignment,
                                      unsigned *Fast) const {
    EVT E = EVT::getIntegerVT(Context, BitWidth);
    return getTLI()->allowsMisalignedMemoryAccesses(
        E, AddressSpace, Alignment, MachineMemOperand::MONone, Fast);
  }

  bool hasBranchDivergence(const Function *F = nullptr) { return false; }

  bool isSourceOfDivergence(const Value *V) { return false; }

  bool isAlwaysUniform(const Value *V) { return false; }

  bool isValidAddrSpaceCast(unsigned FromAS, unsigned ToAS) const {
    return false;
  }

  bool addrspacesMayAlias(unsigned AS0, unsigned AS1) const {
    return true;
  }

  unsigned getFlatAddressSpace() {
    // Return an invalid address space.
    return -1;
  }

  bool collectFlatAddressOperands(SmallVectorImpl<int> &OpIndexes,
                                  Intrinsic::ID IID) const {
    return false;
  }

  bool isNoopAddrSpaceCast(unsigned FromAS, unsigned ToAS) const {
    return getTLI()->getTargetMachine().isNoopAddrSpaceCast(FromAS, ToAS);
  }

  unsigned getAssumedAddrSpace(const Value *V) const {
    return getTLI()->getTargetMachine().getAssumedAddrSpace(V);
  }

  bool isSingleThreaded() const {
    return getTLI()->getTargetMachine().Options.ThreadModel ==
           ThreadModel::Single;
  }

  std::pair<const Value *, unsigned>
  getPredicatedAddrSpace(const Value *V) const {
    return getTLI()->getTargetMachine().getPredicatedAddrSpace(V);
  }

  Value *rewriteIntrinsicWithAddressSpace(IntrinsicInst *II, Value *OldV,
                                          Value *NewV) const {
    return nullptr;
  }

  bool isLegalAddImmediate(int64_t imm) {
    return getTLI()->isLegalAddImmediate(imm);
  }

  bool isLegalAddScalableImmediate(int64_t Imm) {
    return getTLI()->isLegalAddScalableImmediate(Imm);
  }

  bool isLegalICmpImmediate(int64_t imm) {
    return getTLI()->isLegalICmpImmediate(imm);
  }

  bool isLegalAddressingMode(Type *Ty, GlobalValue *BaseGV, int64_t BaseOffset,
                             bool HasBaseReg, int64_t Scale, unsigned AddrSpace,
                             Instruction *I = nullptr,
                             int64_t ScalableOffset = 0) {
    TargetLoweringBase::AddrMode AM;
    AM.BaseGV = BaseGV;
    AM.BaseOffs = BaseOffset;
    AM.HasBaseReg = HasBaseReg;
    AM.Scale = Scale;
    AM.ScalableOffset = ScalableOffset;
    return getTLI()->isLegalAddressingMode(DL, AM, Ty, AddrSpace, I);
  }

  int64_t getPreferredLargeGEPBaseOffset(int64_t MinOffset, int64_t MaxOffset) {
    return getTLI()->getPreferredLargeGEPBaseOffset(MinOffset, MaxOffset);
  }

  unsigned getStoreMinimumVF(unsigned VF, Type *ScalarMemTy,
                             Type *ScalarValTy) const {
    auto &&IsSupportedByTarget = [this, ScalarMemTy, ScalarValTy](unsigned VF) {
      auto *SrcTy = FixedVectorType::get(ScalarMemTy, VF / 2);
      EVT VT = getTLI()->getValueType(DL, SrcTy);
      if (getTLI()->isOperationLegal(ISD::STORE, VT) ||
          getTLI()->isOperationCustom(ISD::STORE, VT))
        return true;

      EVT ValVT =
          getTLI()->getValueType(DL, FixedVectorType::get(ScalarValTy, VF / 2));
      EVT LegalizedVT =
          getTLI()->getTypeToTransformTo(ScalarMemTy->getContext(), VT);
      return getTLI()->isTruncStoreLegal(LegalizedVT, ValVT);
    };
    while (VF > 2 && IsSupportedByTarget(VF))
      VF /= 2;
    return VF;
  }

  bool isIndexedLoadLegal(TTI::MemIndexedMode M, Type *Ty,
                          const DataLayout &DL) const {
    EVT VT = getTLI()->getValueType(DL, Ty);
    return getTLI()->isIndexedLoadLegal(getISDIndexedMode(M), VT);
  }

  bool isIndexedStoreLegal(TTI::MemIndexedMode M, Type *Ty,
                           const DataLayout &DL) const {
    EVT VT = getTLI()->getValueType(DL, Ty);
    return getTLI()->isIndexedStoreLegal(getISDIndexedMode(M), VT);
  }

  bool isLSRCostLess(TTI::LSRCost C1, TTI::LSRCost C2) {
    return TargetTransformInfoImplBase::isLSRCostLess(C1, C2);
  }

  bool isNumRegsMajorCostOfLSR() {
    return TargetTransformInfoImplBase::isNumRegsMajorCostOfLSR();
  }

  bool shouldFoldTerminatingConditionAfterLSR() const {
    return TargetTransformInfoImplBase::
        shouldFoldTerminatingConditionAfterLSR();
  }

  bool shouldDropLSRSolutionIfLessProfitable() const {
    return TargetTransformInfoImplBase::shouldDropLSRSolutionIfLessProfitable();
  }

  bool isProfitableLSRChainElement(Instruction *I) {
    return TargetTransformInfoImplBase::isProfitableLSRChainElement(I);
  }

  InstructionCost getScalingFactorCost(Type *Ty, GlobalValue *BaseGV,
                                       StackOffset BaseOffset, bool HasBaseReg,
                                       int64_t Scale, unsigned AddrSpace) {
    TargetLoweringBase::AddrMode AM;
    AM.BaseGV = BaseGV;
    AM.BaseOffs = BaseOffset.getFixed();
    AM.HasBaseReg = HasBaseReg;
    AM.Scale = Scale;
    AM.ScalableOffset = BaseOffset.getScalable();
    if (getTLI()->isLegalAddressingMode(DL, AM, Ty, AddrSpace))
      return 0;
    return -1;
  }

  bool isTruncateFree(Type *Ty1, Type *Ty2) {
    return getTLI()->isTruncateFree(Ty1, Ty2);
  }

  bool isProfitableToHoist(Instruction *I) {
    return getTLI()->isProfitableToHoist(I);
  }

  bool useAA() const { return getST()->useAA(); }

  bool isTypeLegal(Type *Ty) {
    EVT VT = getTLI()->getValueType(DL, Ty, /*AllowUnknown=*/true);
    return getTLI()->isTypeLegal(VT);
  }

  unsigned getRegUsageForType(Type *Ty) {
    EVT ETy = getTLI()->getValueType(DL, Ty);
    return getTLI()->getNumRegisters(Ty->getContext(), ETy);
  }

  InstructionCost getGEPCost(Type *PointeeType, const Value *Ptr,
                             ArrayRef<const Value *> Operands, Type *AccessType,
                             TTI::TargetCostKind CostKind) {
    return BaseT::getGEPCost(PointeeType, Ptr, Operands, AccessType, CostKind);
  }

  unsigned getEstimatedNumberOfCaseClusters(const SwitchInst &SI,
                                            unsigned &JumpTableSize,
                                            ProfileSummaryInfo *PSI,
                                            BlockFrequencyInfo *BFI) {
    /// Try to find the estimated number of clusters. Note that the number of
    /// clusters identified in this function could be different from the actual
    /// numbers found in lowering. This function ignore switches that are
    /// lowered with a mix of jump table / bit test / BTree. This function was
    /// initially intended to be used when estimating the cost of switch in
    /// inline cost heuristic, but it's a generic cost model to be used in other
    /// places (e.g., in loop unrolling).
    unsigned N = SI.getNumCases();
    const TargetLoweringBase *TLI = getTLI();
    const DataLayout &DL = this->getDataLayout();

    JumpTableSize = 0;
    bool IsJTAllowed = TLI->areJTsAllowed(SI.getParent()->getParent());

    // Early exit if both a jump table and bit test are not allowed.
    if (N < 1 || (!IsJTAllowed && DL.getIndexSizeInBits(0u) < N))
      return N;

    APInt MaxCaseVal = SI.case_begin()->getCaseValue()->getValue();
    APInt MinCaseVal = MaxCaseVal;
    for (auto CI : SI.cases()) {
      const APInt &CaseVal = CI.getCaseValue()->getValue();
      if (CaseVal.sgt(MaxCaseVal))
        MaxCaseVal = CaseVal;
      if (CaseVal.slt(MinCaseVal))
        MinCaseVal = CaseVal;
    }

    // Check if suitable for a bit test
    if (N <= DL.getIndexSizeInBits(0u)) {
      SmallPtrSet<const BasicBlock *, 4> Dests;
      for (auto I : SI.cases())
        Dests.insert(I.getCaseSuccessor());

      if (TLI->isSuitableForBitTests(Dests.size(), N, MinCaseVal, MaxCaseVal,
                                     DL))
        return 1;
    }

    // Check if suitable for a jump table.
    if (IsJTAllowed) {
      if (N < 2 || N < TLI->getMinimumJumpTableEntries())
        return N;
      uint64_t Range =
          (MaxCaseVal - MinCaseVal)
              .getLimitedValue(std::numeric_limits<uint64_t>::max() - 1) + 1;
      // Check whether a range of clusters is dense enough for a jump table
      if (TLI->isSuitableForJumpTable(&SI, N, Range, PSI, BFI)) {
        JumpTableSize = Range;
        return 1;
      }
    }
    return N;
  }

  bool shouldBuildLookupTables() {
    const TargetLoweringBase *TLI = getTLI();
    return TLI->isOperationLegalOrCustom(ISD::BR_JT, MVT::Other) ||
           TLI->isOperationLegalOrCustom(ISD::BRIND, MVT::Other);
  }

  bool shouldBuildRelLookupTables() const {
    const TargetMachine &TM = getTLI()->getTargetMachine();
    // If non-PIC mode, do not generate a relative lookup table.
    if (!TM.isPositionIndependent())
      return false;

    /// Relative lookup table entries consist of 32-bit offsets.
    /// Do not generate relative lookup tables for large code models
    /// in 64-bit achitectures where 32-bit offsets might not be enough.
    if (TM.getCodeModel() == CodeModel::Medium ||
        TM.getCodeModel() == CodeModel::Large)
      return false;

    Triple TargetTriple = TM.getTargetTriple();
    if (!TargetTriple.isArch64Bit())
      return false;

    // TODO: Triggers issues on aarch64 on darwin, so temporarily disable it
    // there.
    if (TargetTriple.getArch() == Triple::aarch64 && TargetTriple.isOSDarwin())
      return false;

    return true;
  }

  bool haveFastSqrt(Type *Ty) {
    const TargetLoweringBase *TLI = getTLI();
    EVT VT = TLI->getValueType(DL, Ty);
    return TLI->isTypeLegal(VT) &&
           TLI->isOperationLegalOrCustom(ISD::FSQRT, VT);
  }

  bool isFCmpOrdCheaperThanFCmpZero(Type *Ty) {
    return true;
  }

  InstructionCost getFPOpCost(Type *Ty) {
    // Check whether FADD is available, as a proxy for floating-point in
    // general.
    const TargetLoweringBase *TLI = getTLI();
    EVT VT = TLI->getValueType(DL, Ty);
    if (TLI->isOperationLegalOrCustomOrPromote(ISD::FADD, VT))
      return TargetTransformInfo::TCC_Basic;
    return TargetTransformInfo::TCC_Expensive;
  }

  bool preferToKeepConstantsAttached(const Instruction &Inst,
                                     const Function &Fn) const {
    switch (Inst.getOpcode()) {
    default:
      break;
    case Instruction::SDiv:
    case Instruction::SRem:
    case Instruction::UDiv:
    case Instruction::URem: {
      if (!isa<ConstantInt>(Inst.getOperand(1)))
        return false;
      EVT VT = getTLI()->getValueType(DL, Inst.getType());
      return !getTLI()->isIntDivCheap(VT, Fn.getAttributes());
    }
    };

    return false;
  }

  unsigned getInliningThresholdMultiplier() const { return 1; }
  unsigned adjustInliningThreshold(const CallBase *CB) { return 0; }
  unsigned getCallerAllocaCost(const CallBase *CB, const AllocaInst *AI) const {
    return 0;
  }

  int getInlinerVectorBonusPercent() const { return 150; }

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE) {
    // This unrolling functionality is target independent, but to provide some
    // motivation for its intended use, for x86:

    // According to the Intel 64 and IA-32 Architectures Optimization Reference
    // Manual, Intel Core models and later have a loop stream detector (and
    // associated uop queue) that can benefit from partial unrolling.
    // The relevant requirements are:
    //  - The loop must have no more than 4 (8 for Nehalem and later) branches
    //    taken, and none of them may be calls.
    //  - The loop can have no more than 18 (28 for Nehalem and later) uops.

    // According to the Software Optimization Guide for AMD Family 15h
    // Processors, models 30h-4fh (Steamroller and later) have a loop predictor
    // and loop buffer which can benefit from partial unrolling.
    // The relevant requirements are:
    //  - The loop must have fewer than 16 branches
    //  - The loop must have less than 40 uops in all executed loop branches

    // The number of taken branches in a loop is hard to estimate here, and
    // benchmarking has revealed that it is better not to be conservative when
    // estimating the branch count. As a result, we'll ignore the branch limits
    // until someone finds a case where it matters in practice.

    unsigned MaxOps;
    const TargetSubtargetInfo *ST = getST();
    if (PartialUnrollingThreshold.getNumOccurrences() > 0)
      MaxOps = PartialUnrollingThreshold;
    else if (ST->getSchedModel().LoopMicroOpBufferSize > 0)
      MaxOps = ST->getSchedModel().LoopMicroOpBufferSize;
    else
      return;

    // Scan the loop: don't unroll loops with calls.
    for (BasicBlock *BB : L->blocks()) {
      for (Instruction &I : *BB) {
        if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
          if (const Function *F = cast<CallBase>(I).getCalledFunction()) {
            if (!thisT()->isLoweredToCall(F))
              continue;
          }

          if (ORE) {
            ORE->emit([&]() {
              return OptimizationRemark("TTI", "DontUnroll", L->getStartLoc(),
                                        L->getHeader())
                     << "advising against unrolling the loop because it "
                        "contains a "
                     << ore::NV("Call", &I);
            });
          }
          return;
        }
      }
    }

    // Enable runtime and partial unrolling up to the specified size.
    // Enable using trip count upper bound to unroll loops.
    UP.Partial = UP.Runtime = UP.UpperBound = true;
    UP.PartialThreshold = MaxOps;

    // Avoid unrolling when optimizing for size.
    UP.OptSizeThreshold = 0;
    UP.PartialOptSizeThreshold = 0;

    // Set number of instructions optimized when "back edge"
    // becomes "fall through" to default value of 2.
    UP.BEInsns = 2;
  }

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP) {
    PP.PeelCount = 0;
    PP.AllowPeeling = true;
    PP.AllowLoopNestsPeeling = false;
    PP.PeelProfiledIterations = true;
  }

  bool isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                AssumptionCache &AC,
                                TargetLibraryInfo *LibInfo,
                                HardwareLoopInfo &HWLoopInfo) {
    return BaseT::isHardwareLoopProfitable(L, SE, AC, LibInfo, HWLoopInfo);
  }

  bool preferPredicateOverEpilogue(TailFoldingInfo *TFI) {
    return BaseT::preferPredicateOverEpilogue(TFI);
  }

  TailFoldingStyle
  getPreferredTailFoldingStyle(bool IVUpdateMayOverflow = true) {
    return BaseT::getPreferredTailFoldingStyle(IVUpdateMayOverflow);
  }

  std::optional<Instruction *> instCombineIntrinsic(InstCombiner &IC,
                                               IntrinsicInst &II) {
    return BaseT::instCombineIntrinsic(IC, II);
  }

  std::optional<Value *>
  simplifyDemandedUseBitsIntrinsic(InstCombiner &IC, IntrinsicInst &II,
                                   APInt DemandedMask, KnownBits &Known,
                                   bool &KnownBitsComputed) {
    return BaseT::simplifyDemandedUseBitsIntrinsic(IC, II, DemandedMask, Known,
                                                   KnownBitsComputed);
  }

  std::optional<Value *> simplifyDemandedVectorEltsIntrinsic(
      InstCombiner &IC, IntrinsicInst &II, APInt DemandedElts, APInt &UndefElts,
      APInt &UndefElts2, APInt &UndefElts3,
      std::function<void(Instruction *, unsigned, APInt, APInt &)>
          SimplifyAndSetOp) {
    return BaseT::simplifyDemandedVectorEltsIntrinsic(
        IC, II, DemandedElts, UndefElts, UndefElts2, UndefElts3,
        SimplifyAndSetOp);
  }

  virtual std::optional<unsigned>
  getCacheSize(TargetTransformInfo::CacheLevel Level) const {
    return std::optional<unsigned>(
        getST()->getCacheSize(static_cast<unsigned>(Level)));
  }

  virtual std::optional<unsigned>
  getCacheAssociativity(TargetTransformInfo::CacheLevel Level) const {
    std::optional<unsigned> TargetResult =
        getST()->getCacheAssociativity(static_cast<unsigned>(Level));

    if (TargetResult)
      return TargetResult;

    return BaseT::getCacheAssociativity(Level);
  }

  virtual unsigned getCacheLineSize() const {
    return getST()->getCacheLineSize();
  }

  virtual unsigned getPrefetchDistance() const {
    return getST()->getPrefetchDistance();
  }

  virtual unsigned getMinPrefetchStride(unsigned NumMemAccesses,
                                        unsigned NumStridedMemAccesses,
                                        unsigned NumPrefetches,
                                        bool HasCall) const {
    return getST()->getMinPrefetchStride(NumMemAccesses, NumStridedMemAccesses,
                                         NumPrefetches, HasCall);
  }

  virtual unsigned getMaxPrefetchIterationsAhead() const {
    return getST()->getMaxPrefetchIterationsAhead();
  }

  virtual bool enableWritePrefetching() const {
    return getST()->enableWritePrefetching();
  }

  virtual bool shouldPrefetchAddressSpace(unsigned AS) const {
    return getST()->shouldPrefetchAddressSpace(AS);
  }

  /// @}

  /// \name Vector TTI Implementations
  /// @{

  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
    return TypeSize::getFixed(32);
  }

  std::optional<unsigned> getMaxVScale() const { return std::nullopt; }
  std::optional<unsigned> getVScaleForTuning() const { return std::nullopt; }
  bool isVScaleKnownToBeAPowerOfTwo() const { return false; }

  /// Estimate the overhead of scalarizing an instruction. Insert and Extract
  /// are set if the demanded result elements need to be inserted and/or
  /// extracted from vectors.
  InstructionCost getScalarizationOverhead(VectorType *InTy,
                                           const APInt &DemandedElts,
                                           bool Insert, bool Extract,
                                           TTI::TargetCostKind CostKind) {
    /// FIXME: a bitfield is not a reasonable abstraction for talking about
    /// which elements are needed from a scalable vector
    if (isa<ScalableVectorType>(InTy))
      return InstructionCost::getInvalid();
    auto *Ty = cast<FixedVectorType>(InTy);

    assert(DemandedElts.getBitWidth() == Ty->getNumElements() &&
           "Vector size mismatch");

    InstructionCost Cost = 0;

    for (int i = 0, e = Ty->getNumElements(); i < e; ++i) {
      if (!DemandedElts[i])
        continue;
      if (Insert)
        Cost += thisT()->getVectorInstrCost(Instruction::InsertElement, Ty,
                                            CostKind, i, nullptr, nullptr);
      if (Extract)
        Cost += thisT()->getVectorInstrCost(Instruction::ExtractElement, Ty,
                                            CostKind, i, nullptr, nullptr);
    }

    return Cost;
  }

  /// Helper wrapper for the DemandedElts variant of getScalarizationOverhead.
  InstructionCost getScalarizationOverhead(VectorType *InTy, bool Insert,
                                           bool Extract,
                                           TTI::TargetCostKind CostKind) {
    if (isa<ScalableVectorType>(InTy))
      return InstructionCost::getInvalid();
    auto *Ty = cast<FixedVectorType>(InTy);

    APInt DemandedElts = APInt::getAllOnes(Ty->getNumElements());
    return thisT()->getScalarizationOverhead(Ty, DemandedElts, Insert, Extract,
                                             CostKind);
  }

  /// Estimate the overhead of scalarizing an instructions unique
  /// non-constant operands. The (potentially vector) types to use for each of
  /// argument are passes via Tys.
  InstructionCost
  getOperandsScalarizationOverhead(ArrayRef<const Value *> Args,
                                   ArrayRef<Type *> Tys,
                                   TTI::TargetCostKind CostKind) {
    assert(Args.size() == Tys.size() && "Expected matching Args and Tys");

    InstructionCost Cost = 0;
    SmallPtrSet<const Value*, 4> UniqueOperands;
    for (int I = 0, E = Args.size(); I != E; I++) {
      // Disregard things like metadata arguments.
      const Value *A = Args[I];
      Type *Ty = Tys[I];
      if (!Ty->isIntOrIntVectorTy() && !Ty->isFPOrFPVectorTy() &&
          !Ty->isPtrOrPtrVectorTy())
        continue;

      if (!isa<Constant>(A) && UniqueOperands.insert(A).second) {
        if (auto *VecTy = dyn_cast<VectorType>(Ty))
          Cost += getScalarizationOverhead(VecTy, /*Insert*/ false,
                                           /*Extract*/ true, CostKind);
      }
    }

    return Cost;
  }

  /// Estimate the overhead of scalarizing the inputs and outputs of an
  /// instruction, with return type RetTy and arguments Args of type Tys. If
  /// Args are unknown (empty), then the cost associated with one argument is
  /// added as a heuristic.
  InstructionCost getScalarizationOverhead(VectorType *RetTy,
                                           ArrayRef<const Value *> Args,
                                           ArrayRef<Type *> Tys,
                                           TTI::TargetCostKind CostKind) {
    InstructionCost Cost = getScalarizationOverhead(
        RetTy, /*Insert*/ true, /*Extract*/ false, CostKind);
    if (!Args.empty())
      Cost += getOperandsScalarizationOverhead(Args, Tys, CostKind);
    else
      // When no information on arguments is provided, we add the cost
      // associated with one argument as a heuristic.
      Cost += getScalarizationOverhead(RetTy, /*Insert*/ false,
                                       /*Extract*/ true, CostKind);

    return Cost;
  }

  /// Estimate the cost of type-legalization and the legalized type.
  std::pair<InstructionCost, MVT> getTypeLegalizationCost(Type *Ty) const {
    LLVMContext &C = Ty->getContext();
    EVT MTy = getTLI()->getValueType(DL, Ty);

    InstructionCost Cost = 1;
    // We keep legalizing the type until we find a legal kind. We assume that
    // the only operation that costs anything is the split. After splitting
    // we need to handle two types.
    while (true) {
      TargetLoweringBase::LegalizeKind LK = getTLI()->getTypeConversion(C, MTy);

      if (LK.first == TargetLoweringBase::TypeScalarizeScalableVector) {
        // Ensure we return a sensible simple VT here, since many callers of
        // this function require it.
        MVT VT = MTy.isSimple() ? MTy.getSimpleVT() : MVT::i64;
        return std::make_pair(InstructionCost::getInvalid(), VT);
      }

      if (LK.first == TargetLoweringBase::TypeLegal)
        return std::make_pair(Cost, MTy.getSimpleVT());

      if (LK.first == TargetLoweringBase::TypeSplitVector ||
          LK.first == TargetLoweringBase::TypeExpandInteger)
        Cost *= 2;

      // Do not loop with f128 type.
      if (MTy == LK.second)
        return std::make_pair(Cost, MTy.getSimpleVT());

      // Keep legalizing the type.
      MTy = LK.second;
    }
  }

  unsigned getMaxInterleaveFactor(ElementCount VF) { return 1; }

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Opd1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Opd2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = std::nullopt,
      const Instruction *CxtI = nullptr) {
    // Check if any of the operands are vector operands.
    const TargetLoweringBase *TLI = getTLI();
    int ISD = TLI->InstructionOpcodeToISD(Opcode);
    assert(ISD && "Invalid opcode");

    // TODO: Handle more cost kinds.
    if (CostKind != TTI::TCK_RecipThroughput)
      return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind,
                                           Opd1Info, Opd2Info,
                                           Args, CxtI);

    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);

    bool IsFloat = Ty->isFPOrFPVectorTy();
    // Assume that floating point arithmetic operations cost twice as much as
    // integer operations.
    InstructionCost OpCost = (IsFloat ? 2 : 1);

    if (TLI->isOperationLegalOrPromote(ISD, LT.second)) {
      // The operation is legal. Assume it costs 1.
      // TODO: Once we have extract/insert subvector cost we need to use them.
      return LT.first * OpCost;
    }

    if (!TLI->isOperationExpand(ISD, LT.second)) {
      // If the operation is custom lowered, then assume that the code is twice
      // as expensive.
      return LT.first * 2 * OpCost;
    }

    // An 'Expand' of URem and SRem is special because it may default
    // to expanding the operation into a sequence of sub-operations
    // i.e. X % Y -> X-(X/Y)*Y.
    if (ISD == ISD::UREM || ISD == ISD::SREM) {
      bool IsSigned = ISD == ISD::SREM;
      if (TLI->isOperationLegalOrCustom(IsSigned ? ISD::SDIVREM : ISD::UDIVREM,
                                        LT.second) ||
          TLI->isOperationLegalOrCustom(IsSigned ? ISD::SDIV : ISD::UDIV,
                                        LT.second)) {
        unsigned DivOpc = IsSigned ? Instruction::SDiv : Instruction::UDiv;
        InstructionCost DivCost = thisT()->getArithmeticInstrCost(
            DivOpc, Ty, CostKind, Opd1Info, Opd2Info);
        InstructionCost MulCost =
            thisT()->getArithmeticInstrCost(Instruction::Mul, Ty, CostKind);
        InstructionCost SubCost =
            thisT()->getArithmeticInstrCost(Instruction::Sub, Ty, CostKind);
        return DivCost + MulCost + SubCost;
      }
    }

    // We cannot scalarize scalable vectors, so return Invalid.
    if (isa<ScalableVectorType>(Ty))
      return InstructionCost::getInvalid();

    // Else, assume that we need to scalarize this op.
    // TODO: If one of the types get legalized by splitting, handle this
    // similarly to what getCastInstrCost() does.
    if (auto *VTy = dyn_cast<FixedVectorType>(Ty)) {
      InstructionCost Cost = thisT()->getArithmeticInstrCost(
          Opcode, VTy->getScalarType(), CostKind, Opd1Info, Opd2Info,
          Args, CxtI);
      // Return the cost of multiple scalar invocation plus the cost of
      // inserting and extracting the values.
      SmallVector<Type *> Tys(Args.size(), Ty);
      return getScalarizationOverhead(VTy, Args, Tys, CostKind) +
             VTy->getNumElements() * Cost;
    }

    // We don't know anything about this scalar instruction.
    return OpCost;
  }

  TTI::ShuffleKind improveShuffleKindFromMask(TTI::ShuffleKind Kind,
                                              ArrayRef<int> Mask,
                                              VectorType *Ty, int &Index,
                                              VectorType *&SubTy) const {
    if (Mask.empty())
      return Kind;
    int NumSrcElts = Ty->getElementCount().getKnownMinValue();
    switch (Kind) {
    case TTI::SK_PermuteSingleSrc:
      if (ShuffleVectorInst::isReverseMask(Mask, NumSrcElts))
        return TTI::SK_Reverse;
      if (ShuffleVectorInst::isZeroEltSplatMask(Mask, NumSrcElts))
        return TTI::SK_Broadcast;
      if (ShuffleVectorInst::isExtractSubvectorMask(Mask, NumSrcElts, Index) &&
          (Index + Mask.size()) <= (size_t)NumSrcElts) {
        SubTy = FixedVectorType::get(Ty->getElementType(), Mask.size());
        return TTI::SK_ExtractSubvector;
      }
      break;
    case TTI::SK_PermuteTwoSrc: {
      int NumSubElts;
      if (Mask.size() > 2 && ShuffleVectorInst::isInsertSubvectorMask(
                                 Mask, NumSrcElts, NumSubElts, Index)) {
        if (Index + NumSubElts > NumSrcElts)
          return Kind;
        SubTy = FixedVectorType::get(Ty->getElementType(), NumSubElts);
        return TTI::SK_InsertSubvector;
      }
      if (ShuffleVectorInst::isSelectMask(Mask, NumSrcElts))
        return TTI::SK_Select;
      if (ShuffleVectorInst::isTransposeMask(Mask, NumSrcElts))
        return TTI::SK_Transpose;
      if (ShuffleVectorInst::isSpliceMask(Mask, NumSrcElts, Index))
        return TTI::SK_Splice;
      break;
    }
    case TTI::SK_Select:
    case TTI::SK_Reverse:
    case TTI::SK_Broadcast:
    case TTI::SK_Transpose:
    case TTI::SK_InsertSubvector:
    case TTI::SK_ExtractSubvector:
    case TTI::SK_Splice:
      break;
    }
    return Kind;
  }

  InstructionCost getShuffleCost(TTI::ShuffleKind Kind, VectorType *Tp,
                                 ArrayRef<int> Mask,
                                 TTI::TargetCostKind CostKind, int Index,
                                 VectorType *SubTp,
                                 ArrayRef<const Value *> Args = std::nullopt,
                                 const Instruction *CxtI = nullptr) {
    switch (improveShuffleKindFromMask(Kind, Mask, Tp, Index, SubTp)) {
    case TTI::SK_Broadcast:
      if (auto *FVT = dyn_cast<FixedVectorType>(Tp))
        return getBroadcastShuffleOverhead(FVT, CostKind);
      return InstructionCost::getInvalid();
    case TTI::SK_Select:
    case TTI::SK_Splice:
    case TTI::SK_Reverse:
    case TTI::SK_Transpose:
    case TTI::SK_PermuteSingleSrc:
    case TTI::SK_PermuteTwoSrc:
      if (auto *FVT = dyn_cast<FixedVectorType>(Tp))
        return getPermuteShuffleOverhead(FVT, CostKind);
      return InstructionCost::getInvalid();
    case TTI::SK_ExtractSubvector:
      return getExtractSubvectorOverhead(Tp, CostKind, Index,
                                         cast<FixedVectorType>(SubTp));
    case TTI::SK_InsertSubvector:
      return getInsertSubvectorOverhead(Tp, CostKind, Index,
                                        cast<FixedVectorType>(SubTp));
    }
    llvm_unreachable("Unknown TTI::ShuffleKind");
  }

  InstructionCost getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                   TTI::CastContextHint CCH,
                                   TTI::TargetCostKind CostKind,
                                   const Instruction *I = nullptr) {
    if (BaseT::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I) == 0)
      return 0;

    const TargetLoweringBase *TLI = getTLI();
    int ISD = TLI->InstructionOpcodeToISD(Opcode);
    assert(ISD && "Invalid opcode");
    std::pair<InstructionCost, MVT> SrcLT = getTypeLegalizationCost(Src);
    std::pair<InstructionCost, MVT> DstLT = getTypeLegalizationCost(Dst);

    TypeSize SrcSize = SrcLT.second.getSizeInBits();
    TypeSize DstSize = DstLT.second.getSizeInBits();
    bool IntOrPtrSrc = Src->isIntegerTy() || Src->isPointerTy();
    bool IntOrPtrDst = Dst->isIntegerTy() || Dst->isPointerTy();

    switch (Opcode) {
    default:
      break;
    case Instruction::Trunc:
      // Check for NOOP conversions.
      if (TLI->isTruncateFree(SrcLT.second, DstLT.second))
        return 0;
      [[fallthrough]];
    case Instruction::BitCast:
      // Bitcast between types that are legalized to the same type are free and
      // assume int to/from ptr of the same size is also free.
      if (SrcLT.first == DstLT.first && IntOrPtrSrc == IntOrPtrDst &&
          SrcSize == DstSize)
        return 0;
      break;
    case Instruction::FPExt:
      if (I && getTLI()->isExtFree(I))
        return 0;
      break;
    case Instruction::ZExt:
      if (TLI->isZExtFree(SrcLT.second, DstLT.second))
        return 0;
      [[fallthrough]];
    case Instruction::SExt:
      if (I && getTLI()->isExtFree(I))
        return 0;

      // If this is a zext/sext of a load, return 0 if the corresponding
      // extending load exists on target and the result type is legal.
      if (CCH == TTI::CastContextHint::Normal) {
        EVT ExtVT = EVT::getEVT(Dst);
        EVT LoadVT = EVT::getEVT(Src);
        unsigned LType =
          ((Opcode == Instruction::ZExt) ? ISD::ZEXTLOAD : ISD::SEXTLOAD);
        if (DstLT.first == SrcLT.first &&
            TLI->isLoadExtLegal(LType, ExtVT, LoadVT))
          return 0;
      }
      break;
    case Instruction::AddrSpaceCast:
      if (TLI->isFreeAddrSpaceCast(Src->getPointerAddressSpace(),
                                   Dst->getPointerAddressSpace()))
        return 0;
      break;
    }

    auto *SrcVTy = dyn_cast<VectorType>(Src);
    auto *DstVTy = dyn_cast<VectorType>(Dst);

    // If the cast is marked as legal (or promote) then assume low cost.
    if (SrcLT.first == DstLT.first &&
        TLI->isOperationLegalOrPromote(ISD, DstLT.second))
      return SrcLT.first;

    // Handle scalar conversions.
    if (!SrcVTy && !DstVTy) {
      // Just check the op cost. If the operation is legal then assume it costs
      // 1.
      if (!TLI->isOperationExpand(ISD, DstLT.second))
        return 1;

      // Assume that illegal scalar instruction are expensive.
      return 4;
    }

    // Check vector-to-vector casts.
    if (DstVTy && SrcVTy) {
      // If the cast is between same-sized registers, then the check is simple.
      if (SrcLT.first == DstLT.first && SrcSize == DstSize) {

        // Assume that Zext is done using AND.
        if (Opcode == Instruction::ZExt)
          return SrcLT.first;

        // Assume that sext is done using SHL and SRA.
        if (Opcode == Instruction::SExt)
          return SrcLT.first * 2;

        // Just check the op cost. If the operation is legal then assume it
        // costs
        // 1 and multiply by the type-legalization overhead.
        if (!TLI->isOperationExpand(ISD, DstLT.second))
          return SrcLT.first * 1;
      }

      // If we are legalizing by splitting, query the concrete TTI for the cost
      // of casting the original vector twice. We also need to factor in the
      // cost of the split itself. Count that as 1, to be consistent with
      // getTypeLegalizationCost().
      bool SplitSrc =
          TLI->getTypeAction(Src->getContext(), TLI->getValueType(DL, Src)) ==
          TargetLowering::TypeSplitVector;
      bool SplitDst =
          TLI->getTypeAction(Dst->getContext(), TLI->getValueType(DL, Dst)) ==
          TargetLowering::TypeSplitVector;
      if ((SplitSrc || SplitDst) && SrcVTy->getElementCount().isVector() &&
          DstVTy->getElementCount().isVector()) {
        Type *SplitDstTy = VectorType::getHalfElementsVectorType(DstVTy);
        Type *SplitSrcTy = VectorType::getHalfElementsVectorType(SrcVTy);
        T *TTI = static_cast<T *>(this);
        // If both types need to be split then the split is free.
        InstructionCost SplitCost =
            (!SplitSrc || !SplitDst) ? TTI->getVectorSplitCost() : 0;
        return SplitCost +
               (2 * TTI->getCastInstrCost(Opcode, SplitDstTy, SplitSrcTy, CCH,
                                          CostKind, I));
      }

      // Scalarization cost is Invalid, can't assume any num elements.
      if (isa<ScalableVectorType>(DstVTy))
        return InstructionCost::getInvalid();

      // In other cases where the source or destination are illegal, assume
      // the operation will get scalarized.
      unsigned Num = cast<FixedVectorType>(DstVTy)->getNumElements();
      InstructionCost Cost = thisT()->getCastInstrCost(
          Opcode, Dst->getScalarType(), Src->getScalarType(), CCH, CostKind, I);

      // Return the cost of multiple scalar invocation plus the cost of
      // inserting and extracting the values.
      return getScalarizationOverhead(DstVTy, /*Insert*/ true, /*Extract*/ true,
                                      CostKind) +
             Num * Cost;
    }

    // We already handled vector-to-vector and scalar-to-scalar conversions.
    // This
    // is where we handle bitcast between vectors and scalars. We need to assume
    //  that the conversion is scalarized in one way or another.
    if (Opcode == Instruction::BitCast) {
      // Illegal bitcasts are done by storing and loading from a stack slot.
      return (SrcVTy ? getScalarizationOverhead(SrcVTy, /*Insert*/ false,
                                                /*Extract*/ true, CostKind)
                     : 0) +
             (DstVTy ? getScalarizationOverhead(DstVTy, /*Insert*/ true,
                                                /*Extract*/ false, CostKind)
                     : 0);
    }

    llvm_unreachable("Unhandled cast");
  }

  InstructionCost getExtractWithExtendCost(unsigned Opcode, Type *Dst,
                                           VectorType *VecTy, unsigned Index) {
    TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
    return thisT()->getVectorInstrCost(Instruction::ExtractElement, VecTy,
                                       CostKind, Index, nullptr, nullptr) +
           thisT()->getCastInstrCost(Opcode, Dst, VecTy->getElementType(),
                                     TTI::CastContextHint::None, CostKind);
  }

  InstructionCost getCFInstrCost(unsigned Opcode, TTI::TargetCostKind CostKind,
                                 const Instruction *I = nullptr) {
    return BaseT::getCFInstrCost(Opcode, CostKind, I);
  }

  InstructionCost getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                                     CmpInst::Predicate VecPred,
                                     TTI::TargetCostKind CostKind,
                                     const Instruction *I = nullptr) {
    const TargetLoweringBase *TLI = getTLI();
    int ISD = TLI->InstructionOpcodeToISD(Opcode);
    assert(ISD && "Invalid opcode");

    // TODO: Handle other cost kinds.
    if (CostKind != TTI::TCK_RecipThroughput)
      return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind,
                                       I);

    // Selects on vectors are actually vector selects.
    if (ISD == ISD::SELECT) {
      assert(CondTy && "CondTy must exist");
      if (CondTy->isVectorTy())
        ISD = ISD::VSELECT;
    }
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);

    if (!(ValTy->isVectorTy() && !LT.second.isVector()) &&
        !TLI->isOperationExpand(ISD, LT.second)) {
      // The operation is legal. Assume it costs 1. Multiply
      // by the type-legalization overhead.
      return LT.first * 1;
    }

    // Otherwise, assume that the cast is scalarized.
    // TODO: If one of the types get legalized by splitting, handle this
    // similarly to what getCastInstrCost() does.
    if (auto *ValVTy = dyn_cast<VectorType>(ValTy)) {
      if (isa<ScalableVectorType>(ValTy))
        return InstructionCost::getInvalid();

      unsigned Num = cast<FixedVectorType>(ValVTy)->getNumElements();
      if (CondTy)
        CondTy = CondTy->getScalarType();
      InstructionCost Cost = thisT()->getCmpSelInstrCost(
          Opcode, ValVTy->getScalarType(), CondTy, VecPred, CostKind, I);

      // Return the cost of multiple scalar invocation plus the cost of
      // inserting and extracting the values.
      return getScalarizationOverhead(ValVTy, /*Insert*/ true,
                                      /*Extract*/ false, CostKind) +
             Num * Cost;
    }

    // Unknown scalar opcode.
    return 1;
  }

  InstructionCost getVectorInstrCost(unsigned Opcode, Type *Val,
                                     TTI::TargetCostKind CostKind,
                                     unsigned Index, Value *Op0, Value *Op1) {
    return getRegUsageForType(Val->getScalarType());
  }

  InstructionCost getVectorInstrCost(const Instruction &I, Type *Val,
                                     TTI::TargetCostKind CostKind,
                                     unsigned Index) {
    Value *Op0 = nullptr;
    Value *Op1 = nullptr;
    if (auto *IE = dyn_cast<InsertElementInst>(&I)) {
      Op0 = IE->getOperand(0);
      Op1 = IE->getOperand(1);
    }
    return thisT()->getVectorInstrCost(I.getOpcode(), Val, CostKind, Index, Op0,
                                       Op1);
  }

  InstructionCost getReplicationShuffleCost(Type *EltTy, int ReplicationFactor,
                                            int VF,
                                            const APInt &DemandedDstElts,
                                            TTI::TargetCostKind CostKind) {
    assert(DemandedDstElts.getBitWidth() == (unsigned)VF * ReplicationFactor &&
           "Unexpected size of DemandedDstElts.");

    InstructionCost Cost;

    auto *SrcVT = FixedVectorType::get(EltTy, VF);
    auto *ReplicatedVT = FixedVectorType::get(EltTy, VF * ReplicationFactor);

    // The Mask shuffling cost is extract all the elements of the Mask
    // and insert each of them Factor times into the wide vector:
    //
    // E.g. an interleaved group with factor 3:
    //    %mask = icmp ult <8 x i32> %vec1, %vec2
    //    %interleaved.mask = shufflevector <8 x i1> %mask, <8 x i1> undef,
    //        <24 x i32> <0,0,0,1,1,1,2,2,2,3,3,3,4,4,4,5,5,5,6,6,6,7,7,7>
    // The cost is estimated as extract all mask elements from the <8xi1> mask
    // vector and insert them factor times into the <24xi1> shuffled mask
    // vector.
    APInt DemandedSrcElts = APIntOps::ScaleBitMask(DemandedDstElts, VF);
    Cost += thisT()->getScalarizationOverhead(SrcVT, DemandedSrcElts,
                                              /*Insert*/ false,
                                              /*Extract*/ true, CostKind);
    Cost += thisT()->getScalarizationOverhead(ReplicatedVT, DemandedDstElts,
                                              /*Insert*/ true,
                                              /*Extract*/ false, CostKind);

    return Cost;
  }

  InstructionCost
  getMemoryOpCost(unsigned Opcode, Type *Src, MaybeAlign Alignment,
                  unsigned AddressSpace, TTI::TargetCostKind CostKind,
                  TTI::OperandValueInfo OpInfo = {TTI::OK_AnyValue, TTI::OP_None},
                  const Instruction *I = nullptr) {
    assert(!Src->isVoidTy() && "Invalid type");
    // Assume types, such as structs, are expensive.
    if (getTLI()->getValueType(DL, Src,  true) == MVT::Other)
      return 4;
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Src);

    // Assuming that all loads of legal types cost 1.
    InstructionCost Cost = LT.first;
    if (CostKind != TTI::TCK_RecipThroughput)
      return Cost;

    const DataLayout &DL = this->getDataLayout();
    if (Src->isVectorTy() &&
        // In practice it's not currently possible to have a change in lane
        // length for extending loads or truncating stores so both types should
        // have the same scalable property.
        TypeSize::isKnownLT(DL.getTypeStoreSizeInBits(Src),
                            LT.second.getSizeInBits())) {
      // This is a vector load that legalizes to a larger type than the vector
      // itself. Unless the corresponding extending load or truncating store is
      // legal, then this will scalarize.
      TargetLowering::LegalizeAction LA = TargetLowering::Expand;
      EVT MemVT = getTLI()->getValueType(DL, Src);
      if (Opcode == Instruction::Store)
        LA = getTLI()->getTruncStoreAction(LT.second, MemVT);
      else
        LA = getTLI()->getLoadExtAction(ISD::EXTLOAD, LT.second, MemVT);

      if (LA != TargetLowering::Legal && LA != TargetLowering::Custom) {
        // This is a vector load/store for some illegal type that is scalarized.
        // We must account for the cost of building or decomposing the vector.
        Cost += getScalarizationOverhead(
            cast<VectorType>(Src), Opcode != Instruction::Store,
            Opcode == Instruction::Store, CostKind);
      }
    }

    return Cost;
  }

  InstructionCost getMaskedMemoryOpCost(unsigned Opcode, Type *DataTy,
                                        Align Alignment, unsigned AddressSpace,
                                        TTI::TargetCostKind CostKind) {
    // TODO: Pass on AddressSpace when we have test coverage.
    return getCommonMaskedMemoryOpCost(Opcode, DataTy, Alignment, true, false,
                                       CostKind);
  }

  InstructionCost getGatherScatterOpCost(unsigned Opcode, Type *DataTy,
                                         const Value *Ptr, bool VariableMask,
                                         Align Alignment,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I = nullptr) {
    return getCommonMaskedMemoryOpCost(Opcode, DataTy, Alignment, VariableMask,
                                       true, CostKind);
  }

  InstructionCost getStridedMemoryOpCost(unsigned Opcode, Type *DataTy,
                                         const Value *Ptr, bool VariableMask,
                                         Align Alignment,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I) {
    // For a target without strided memory operations (or for an illegal
    // operation type on one which does), assume we lower to a gather/scatter
    // operation.  (Which may in turn be scalarized.)
    return thisT()->getGatherScatterOpCost(Opcode, DataTy, Ptr, VariableMask,
                                           Alignment, CostKind, I);
  }

  InstructionCost getInterleavedMemoryOpCost(
      unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
      Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
      bool UseMaskForCond = false, bool UseMaskForGaps = false) {

    // We cannot scalarize scalable vectors, so return Invalid.
    if (isa<ScalableVectorType>(VecTy))
      return InstructionCost::getInvalid();

    auto *VT = cast<FixedVectorType>(VecTy);

    unsigned NumElts = VT->getNumElements();
    assert(Factor > 1 && NumElts % Factor == 0 && "Invalid interleave factor");

    unsigned NumSubElts = NumElts / Factor;
    auto *SubVT = FixedVectorType::get(VT->getElementType(), NumSubElts);

    // Firstly, the cost of load/store operation.
    InstructionCost Cost;
    if (UseMaskForCond || UseMaskForGaps)
      Cost = thisT()->getMaskedMemoryOpCost(Opcode, VecTy, Alignment,
                                            AddressSpace, CostKind);
    else
      Cost = thisT()->getMemoryOpCost(Opcode, VecTy, Alignment, AddressSpace,
                                      CostKind);

    // Legalize the vector type, and get the legalized and unlegalized type
    // sizes.
    MVT VecTyLT = getTypeLegalizationCost(VecTy).second;
    unsigned VecTySize = thisT()->getDataLayout().getTypeStoreSize(VecTy);
    unsigned VecTyLTSize = VecTyLT.getStoreSize();

    // Scale the cost of the memory operation by the fraction of legalized
    // instructions that will actually be used. We shouldn't account for the
    // cost of dead instructions since they will be removed.
    //
    // E.g., An interleaved load of factor 8:
    //       %vec = load <16 x i64>, <16 x i64>* %ptr
    //       %v0 = shufflevector %vec, undef, <0, 8>
    //
    // If <16 x i64> is legalized to 8 v2i64 loads, only 2 of the loads will be
    // used (those corresponding to elements [0:1] and [8:9] of the unlegalized
    // type). The other loads are unused.
    //
    // TODO: Note that legalization can turn masked loads/stores into unmasked
    // (legalized) loads/stores. This can be reflected in the cost.
    if (Cost.isValid() && VecTySize > VecTyLTSize) {
      // The number of loads of a legal type it will take to represent a load
      // of the unlegalized vector type.
      unsigned NumLegalInsts = divideCeil(VecTySize, VecTyLTSize);

      // The number of elements of the unlegalized type that correspond to a
      // single legal instruction.
      unsigned NumEltsPerLegalInst = divideCeil(NumElts, NumLegalInsts);

      // Determine which legal instructions will be used.
      BitVector UsedInsts(NumLegalInsts, false);
      for (unsigned Index : Indices)
        for (unsigned Elt = 0; Elt < NumSubElts; ++Elt)
          UsedInsts.set((Index + Elt * Factor) / NumEltsPerLegalInst);

      // Scale the cost of the load by the fraction of legal instructions that
      // will be used.
      Cost = divideCeil(UsedInsts.count() * *Cost.getValue(), NumLegalInsts);
    }

    // Then plus the cost of interleave operation.
    assert(Indices.size() <= Factor &&
           "Interleaved memory op has too many members");

    const APInt DemandedAllSubElts = APInt::getAllOnes(NumSubElts);
    const APInt DemandedAllResultElts = APInt::getAllOnes(NumElts);

    APInt DemandedLoadStoreElts = APInt::getZero(NumElts);
    for (unsigned Index : Indices) {
      assert(Index < Factor && "Invalid index for interleaved memory op");
      for (unsigned Elm = 0; Elm < NumSubElts; Elm++)
        DemandedLoadStoreElts.setBit(Index + Elm * Factor);
    }

    if (Opcode == Instruction::Load) {
      // The interleave cost is similar to extract sub vectors' elements
      // from the wide vector, and insert them into sub vectors.
      //
      // E.g. An interleaved load of factor 2 (with one member of index 0):
      //      %vec = load <8 x i32>, <8 x i32>* %ptr
      //      %v0 = shuffle %vec, undef, <0, 2, 4, 6>         ; Index 0
      // The cost is estimated as extract elements at 0, 2, 4, 6 from the
      // <8 x i32> vector and insert them into a <4 x i32> vector.
      InstructionCost InsSubCost = thisT()->getScalarizationOverhead(
          SubVT, DemandedAllSubElts,
          /*Insert*/ true, /*Extract*/ false, CostKind);
      Cost += Indices.size() * InsSubCost;
      Cost += thisT()->getScalarizationOverhead(VT, DemandedLoadStoreElts,
                                                /*Insert*/ false,
                                                /*Extract*/ true, CostKind);
    } else {
      // The interleave cost is extract elements from sub vectors, and
      // insert them into the wide vector.
      //
      // E.g. An interleaved store of factor 3 with 2 members at indices 0,1:
      // (using VF=4):
      //    %v0_v1 = shuffle %v0, %v1, <0,4,undef,1,5,undef,2,6,undef,3,7,undef>
      //    %gaps.mask = <true, true, false, true, true, false,
      //                  true, true, false, true, true, false>
      //    call llvm.masked.store <12 x i32> %v0_v1, <12 x i32>* %ptr,
      //                           i32 Align, <12 x i1> %gaps.mask
      // The cost is estimated as extract all elements (of actual members,
      // excluding gaps) from both <4 x i32> vectors and insert into the <12 x
      // i32> vector.
      InstructionCost ExtSubCost = thisT()->getScalarizationOverhead(
          SubVT, DemandedAllSubElts,
          /*Insert*/ false, /*Extract*/ true, CostKind);
      Cost += ExtSubCost * Indices.size();
      Cost += thisT()->getScalarizationOverhead(VT, DemandedLoadStoreElts,
                                                /*Insert*/ true,
                                                /*Extract*/ false, CostKind);
    }

    if (!UseMaskForCond)
      return Cost;

    Type *I8Type = Type::getInt8Ty(VT->getContext());

    Cost += thisT()->getReplicationShuffleCost(
        I8Type, Factor, NumSubElts,
        UseMaskForGaps ? DemandedLoadStoreElts : DemandedAllResultElts,
        CostKind);

    // The Gaps mask is invariant and created outside the loop, therefore the
    // cost of creating it is not accounted for here. However if we have both
    // a MaskForGaps and some other mask that guards the execution of the
    // memory access, we need to account for the cost of And-ing the two masks
    // inside the loop.
    if (UseMaskForGaps) {
      auto *MaskVT = FixedVectorType::get(I8Type, NumElts);
      Cost += thisT()->getArithmeticInstrCost(BinaryOperator::And, MaskVT,
                                              CostKind);
    }

    return Cost;
  }

  /// Get intrinsic cost based on arguments.
  InstructionCost getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                        TTI::TargetCostKind CostKind) {
    // Check for generically free intrinsics.
    if (BaseT::getIntrinsicInstrCost(ICA, CostKind) == 0)
      return 0;

    // Assume that target intrinsics are cheap.
    Intrinsic::ID IID = ICA.getID();
    if (Function::isTargetIntrinsic(IID))
      return TargetTransformInfo::TCC_Basic;

    if (ICA.isTypeBasedOnly())
      return getTypeBasedIntrinsicInstrCost(ICA, CostKind);

    Type *RetTy = ICA.getReturnType();

    ElementCount RetVF =
        (RetTy->isVectorTy() ? cast<VectorType>(RetTy)->getElementCount()
                             : ElementCount::getFixed(1));
    const IntrinsicInst *I = ICA.getInst();
    const SmallVectorImpl<const Value *> &Args = ICA.getArgs();
    FastMathFlags FMF = ICA.getFlags();
    switch (IID) {
    default:
      break;

    case Intrinsic::powi:
      if (auto *RHSC = dyn_cast<ConstantInt>(Args[1])) {
        bool ShouldOptForSize = I->getParent()->getParent()->hasOptSize();
        if (getTLI()->isBeneficialToExpandPowI(RHSC->getSExtValue(),
                                               ShouldOptForSize)) {
          // The cost is modeled on the expansion performed by ExpandPowI in
          // SelectionDAGBuilder.
          APInt Exponent = RHSC->getValue().abs();
          unsigned ActiveBits = Exponent.getActiveBits();
          unsigned PopCount = Exponent.popcount();
          InstructionCost Cost = (ActiveBits + PopCount - 2) *
                                 thisT()->getArithmeticInstrCost(
                                     Instruction::FMul, RetTy, CostKind);
          if (RHSC->isNegative())
            Cost += thisT()->getArithmeticInstrCost(Instruction::FDiv, RetTy,
                                                    CostKind);
          return Cost;
        }
      }
      break;
    case Intrinsic::cttz:
      // FIXME: If necessary, this should go in target-specific overrides.
      if (RetVF.isScalar() && getTLI()->isCheapToSpeculateCttz(RetTy))
        return TargetTransformInfo::TCC_Basic;
      break;

    case Intrinsic::ctlz:
      // FIXME: If necessary, this should go in target-specific overrides.
      if (RetVF.isScalar() && getTLI()->isCheapToSpeculateCtlz(RetTy))
        return TargetTransformInfo::TCC_Basic;
      break;

    case Intrinsic::memcpy:
      return thisT()->getMemcpyCost(ICA.getInst());

    case Intrinsic::masked_scatter: {
      const Value *Mask = Args[3];
      bool VarMask = !isa<Constant>(Mask);
      Align Alignment = cast<ConstantInt>(Args[2])->getAlignValue();
      return thisT()->getGatherScatterOpCost(Instruction::Store,
                                             ICA.getArgTypes()[0], Args[1],
                                             VarMask, Alignment, CostKind, I);
    }
    case Intrinsic::masked_gather: {
      const Value *Mask = Args[2];
      bool VarMask = !isa<Constant>(Mask);
      Align Alignment = cast<ConstantInt>(Args[1])->getAlignValue();
      return thisT()->getGatherScatterOpCost(Instruction::Load, RetTy, Args[0],
                                             VarMask, Alignment, CostKind, I);
    }
    case Intrinsic::experimental_vp_strided_store: {
      const Value *Data = Args[0];
      const Value *Ptr = Args[1];
      const Value *Mask = Args[3];
      const Value *EVL = Args[4];
      bool VarMask = !isa<Constant>(Mask) || !isa<Constant>(EVL);
      Align Alignment = I->getParamAlign(1).valueOrOne();
      return thisT()->getStridedMemoryOpCost(Instruction::Store,
                                             Data->getType(), Ptr, VarMask,
                                             Alignment, CostKind, I);
    }
    case Intrinsic::experimental_vp_strided_load: {
      const Value *Ptr = Args[0];
      const Value *Mask = Args[2];
      const Value *EVL = Args[3];
      bool VarMask = !isa<Constant>(Mask) || !isa<Constant>(EVL);
      Align Alignment = I->getParamAlign(0).valueOrOne();
      return thisT()->getStridedMemoryOpCost(Instruction::Load, RetTy, Ptr,
                                             VarMask, Alignment, CostKind, I);
    }
    case Intrinsic::experimental_stepvector: {
      if (isa<ScalableVectorType>(RetTy))
        return BaseT::getIntrinsicInstrCost(ICA, CostKind);
      // The cost of materialising a constant integer vector.
      return TargetTransformInfo::TCC_Basic;
    }
    case Intrinsic::vector_extract: {
      // FIXME: Handle case where a scalable vector is extracted from a scalable
      // vector
      if (isa<ScalableVectorType>(RetTy))
        return BaseT::getIntrinsicInstrCost(ICA, CostKind);
      unsigned Index = cast<ConstantInt>(Args[1])->getZExtValue();
      return thisT()->getShuffleCost(
          TTI::SK_ExtractSubvector, cast<VectorType>(Args[0]->getType()),
          std::nullopt, CostKind, Index, cast<VectorType>(RetTy));
    }
    case Intrinsic::vector_insert: {
      // FIXME: Handle case where a scalable vector is inserted into a scalable
      // vector
      if (isa<ScalableVectorType>(Args[1]->getType()))
        return BaseT::getIntrinsicInstrCost(ICA, CostKind);
      unsigned Index = cast<ConstantInt>(Args[2])->getZExtValue();
      return thisT()->getShuffleCost(
          TTI::SK_InsertSubvector, cast<VectorType>(Args[0]->getType()),
          std::nullopt, CostKind, Index, cast<VectorType>(Args[1]->getType()));
    }
    case Intrinsic::vector_reverse: {
      return thisT()->getShuffleCost(
          TTI::SK_Reverse, cast<VectorType>(Args[0]->getType()), std::nullopt,
          CostKind, 0, cast<VectorType>(RetTy));
    }
    case Intrinsic::vector_splice: {
      unsigned Index = cast<ConstantInt>(Args[2])->getZExtValue();
      return thisT()->getShuffleCost(
          TTI::SK_Splice, cast<VectorType>(Args[0]->getType()), std::nullopt,
          CostKind, Index, cast<VectorType>(RetTy));
    }
    case Intrinsic::vector_reduce_add:
    case Intrinsic::vector_reduce_mul:
    case Intrinsic::vector_reduce_and:
    case Intrinsic::vector_reduce_or:
    case Intrinsic::vector_reduce_xor:
    case Intrinsic::vector_reduce_smax:
    case Intrinsic::vector_reduce_smin:
    case Intrinsic::vector_reduce_fmax:
    case Intrinsic::vector_reduce_fmin:
    case Intrinsic::vector_reduce_fmaximum:
    case Intrinsic::vector_reduce_fminimum:
    case Intrinsic::vector_reduce_umax:
    case Intrinsic::vector_reduce_umin: {
      IntrinsicCostAttributes Attrs(IID, RetTy, Args[0]->getType(), FMF, I, 1);
      return getTypeBasedIntrinsicInstrCost(Attrs, CostKind);
    }
    case Intrinsic::vector_reduce_fadd:
    case Intrinsic::vector_reduce_fmul: {
      IntrinsicCostAttributes Attrs(
          IID, RetTy, {Args[0]->getType(), Args[1]->getType()}, FMF, I, 1);
      return getTypeBasedIntrinsicInstrCost(Attrs, CostKind);
    }
    case Intrinsic::fshl:
    case Intrinsic::fshr: {
      const Value *X = Args[0];
      const Value *Y = Args[1];
      const Value *Z = Args[2];
      const TTI::OperandValueInfo OpInfoX = TTI::getOperandInfo(X);
      const TTI::OperandValueInfo OpInfoY = TTI::getOperandInfo(Y);
      const TTI::OperandValueInfo OpInfoZ = TTI::getOperandInfo(Z);
      const TTI::OperandValueInfo OpInfoBW =
        {TTI::OK_UniformConstantValue,
         isPowerOf2_32(RetTy->getScalarSizeInBits()) ? TTI::OP_PowerOf2
         : TTI::OP_None};

      // fshl: (X << (Z % BW)) | (Y >> (BW - (Z % BW)))
      // fshr: (X << (BW - (Z % BW))) | (Y >> (Z % BW))
      InstructionCost Cost = 0;
      Cost +=
          thisT()->getArithmeticInstrCost(BinaryOperator::Or, RetTy, CostKind);
      Cost +=
          thisT()->getArithmeticInstrCost(BinaryOperator::Sub, RetTy, CostKind);
      Cost += thisT()->getArithmeticInstrCost(
          BinaryOperator::Shl, RetTy, CostKind, OpInfoX,
          {OpInfoZ.Kind, TTI::OP_None});
      Cost += thisT()->getArithmeticInstrCost(
          BinaryOperator::LShr, RetTy, CostKind, OpInfoY,
          {OpInfoZ.Kind, TTI::OP_None});
      // Non-constant shift amounts requires a modulo.
      if (!OpInfoZ.isConstant())
        Cost += thisT()->getArithmeticInstrCost(BinaryOperator::URem, RetTy,
                                                CostKind, OpInfoZ, OpInfoBW);
      // For non-rotates (X != Y) we must add shift-by-zero handling costs.
      if (X != Y) {
        Type *CondTy = RetTy->getWithNewBitWidth(1);
        Cost +=
            thisT()->getCmpSelInstrCost(BinaryOperator::ICmp, RetTy, CondTy,
                                        CmpInst::ICMP_EQ, CostKind);
        Cost +=
            thisT()->getCmpSelInstrCost(BinaryOperator::Select, RetTy, CondTy,
                                        CmpInst::ICMP_EQ, CostKind);
      }
      return Cost;
    }
    case Intrinsic::get_active_lane_mask: {
      EVT ResVT = getTLI()->getValueType(DL, RetTy, true);
      EVT ArgType = getTLI()->getValueType(DL, ICA.getArgTypes()[0], true);

      // If we're not expanding the intrinsic then we assume this is cheap
      // to implement.
      if (!getTLI()->shouldExpandGetActiveLaneMask(ResVT, ArgType)) {
        return getTypeLegalizationCost(RetTy).first;
      }

      // Create the expanded types that will be used to calculate the uadd_sat
      // operation.
      Type *ExpRetTy = VectorType::get(
          ICA.getArgTypes()[0], cast<VectorType>(RetTy)->getElementCount());
      IntrinsicCostAttributes Attrs(Intrinsic::uadd_sat, ExpRetTy, {}, FMF);
      InstructionCost Cost =
          thisT()->getTypeBasedIntrinsicInstrCost(Attrs, CostKind);
      Cost += thisT()->getCmpSelInstrCost(BinaryOperator::ICmp, ExpRetTy, RetTy,
                                          CmpInst::ICMP_ULT, CostKind);
      return Cost;
    }
    case Intrinsic::experimental_cttz_elts: {
      EVT ArgType = getTLI()->getValueType(DL, ICA.getArgTypes()[0], true);

      // If we're not expanding the intrinsic then we assume this is cheap
      // to implement.
      if (!getTLI()->shouldExpandCttzElements(ArgType))
        return getTypeLegalizationCost(RetTy).first;

      // TODO: The costs below reflect the expansion code in
      // SelectionDAGBuilder, but we may want to sacrifice some accuracy in
      // favour of compile time.

      // Find the smallest "sensible" element type to use for the expansion.
      bool ZeroIsPoison = !cast<ConstantInt>(Args[1])->isZero();
      ConstantRange VScaleRange(APInt(64, 1), APInt::getZero(64));
      if (isa<ScalableVectorType>(ICA.getArgTypes()[0]) && I && I->getCaller())
        VScaleRange = getVScaleRange(I->getCaller(), 64);

      unsigned EltWidth = getTLI()->getBitWidthForCttzElements(
          RetTy, ArgType.getVectorElementCount(), ZeroIsPoison, &VScaleRange);
      Type *NewEltTy = IntegerType::getIntNTy(RetTy->getContext(), EltWidth);

      // Create the new vector type & get the vector length
      Type *NewVecTy = VectorType::get(
          NewEltTy, cast<VectorType>(Args[0]->getType())->getElementCount());

      IntrinsicCostAttributes StepVecAttrs(Intrinsic::experimental_stepvector,
                                           NewVecTy, {}, FMF);
      InstructionCost Cost =
          thisT()->getIntrinsicInstrCost(StepVecAttrs, CostKind);

      Cost +=
          thisT()->getArithmeticInstrCost(Instruction::Sub, NewVecTy, CostKind);
      Cost += thisT()->getCastInstrCost(Instruction::SExt, NewVecTy,
                                        Args[0]->getType(),
                                        TTI::CastContextHint::None, CostKind);
      Cost +=
          thisT()->getArithmeticInstrCost(Instruction::And, NewVecTy, CostKind);

      IntrinsicCostAttributes ReducAttrs(Intrinsic::vector_reduce_umax,
                                         NewEltTy, NewVecTy, FMF, I, 1);
      Cost += thisT()->getTypeBasedIntrinsicInstrCost(ReducAttrs, CostKind);
      Cost +=
          thisT()->getArithmeticInstrCost(Instruction::Sub, NewEltTy, CostKind);

      return Cost;
    }
    }

    // VP Intrinsics should have the same cost as their non-vp counterpart.
    // TODO: Adjust the cost to make the vp intrinsic cheaper than its non-vp
    // counterpart when the vector length argument is smaller than the maximum
    // vector length.
    // TODO: Support other kinds of VPIntrinsics
    if (VPIntrinsic::isVPIntrinsic(ICA.getID())) {
      std::optional<unsigned> FOp =
          VPIntrinsic::getFunctionalOpcodeForVP(ICA.getID());
      if (FOp) {
        if (ICA.getID() == Intrinsic::vp_load) {
          Align Alignment;
          if (auto *VPI = dyn_cast_or_null<VPIntrinsic>(ICA.getInst()))
            Alignment = VPI->getPointerAlignment().valueOrOne();
          unsigned AS = 0;
          if (ICA.getArgs().size() > 1)
            if (auto *PtrTy =
                    dyn_cast<PointerType>(ICA.getArgs()[0]->getType()))
              AS = PtrTy->getAddressSpace();
          return thisT()->getMemoryOpCost(*FOp, ICA.getReturnType(), Alignment,
                                          AS, CostKind);
        }
        if (ICA.getID() == Intrinsic::vp_store) {
          Align Alignment;
          if (auto *VPI = dyn_cast_or_null<VPIntrinsic>(ICA.getInst()))
            Alignment = VPI->getPointerAlignment().valueOrOne();
          unsigned AS = 0;
          if (ICA.getArgs().size() >= 2)
            if (auto *PtrTy =
                    dyn_cast<PointerType>(ICA.getArgs()[1]->getType()))
              AS = PtrTy->getAddressSpace();
          return thisT()->getMemoryOpCost(*FOp, Args[0]->getType(), Alignment,
                                          AS, CostKind);
        }
        if (VPBinOpIntrinsic::isVPBinOp(ICA.getID())) {
          return thisT()->getArithmeticInstrCost(*FOp, ICA.getReturnType(),
                                                 CostKind);
        }
      }

      std::optional<Intrinsic::ID> FID =
          VPIntrinsic::getFunctionalIntrinsicIDForVP(ICA.getID());
      if (FID) {
        // Non-vp version will have same Args/Tys except mask and vector length.
        assert(ICA.getArgs().size() >= 2 && ICA.getArgTypes().size() >= 2 &&
               "Expected VPIntrinsic to have Mask and Vector Length args and "
               "types");
        ArrayRef<Type *> NewTys = ArrayRef(ICA.getArgTypes()).drop_back(2);

        // VPReduction intrinsics have a start value argument that their non-vp
        // counterparts do not have, except for the fadd and fmul non-vp
        // counterpart.
        if (VPReductionIntrinsic::isVPReduction(ICA.getID()) &&
            *FID != Intrinsic::vector_reduce_fadd &&
            *FID != Intrinsic::vector_reduce_fmul)
          NewTys = NewTys.drop_front();

        IntrinsicCostAttributes NewICA(*FID, ICA.getReturnType(), NewTys,
                                       ICA.getFlags());
        return thisT()->getIntrinsicInstrCost(NewICA, CostKind);
      }
    }

    // Assume that we need to scalarize this intrinsic.)
    // Compute the scalarization overhead based on Args for a vector
    // intrinsic.
    InstructionCost ScalarizationCost = InstructionCost::getInvalid();
    if (RetVF.isVector() && !RetVF.isScalable()) {
      ScalarizationCost = 0;
      if (!RetTy->isVoidTy())
        ScalarizationCost += getScalarizationOverhead(
            cast<VectorType>(RetTy),
            /*Insert*/ true, /*Extract*/ false, CostKind);
      ScalarizationCost +=
          getOperandsScalarizationOverhead(Args, ICA.getArgTypes(), CostKind);
    }

    IntrinsicCostAttributes Attrs(IID, RetTy, ICA.getArgTypes(), FMF, I,
                                  ScalarizationCost);
    return thisT()->getTypeBasedIntrinsicInstrCost(Attrs, CostKind);
  }

  /// Get intrinsic cost based on argument types.
  /// If ScalarizationCostPassed is std::numeric_limits<unsigned>::max(), the
  /// cost of scalarizing the arguments and the return value will be computed
  /// based on types.
  InstructionCost
  getTypeBasedIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                 TTI::TargetCostKind CostKind) {
    Intrinsic::ID IID = ICA.getID();
    Type *RetTy = ICA.getReturnType();
    const SmallVectorImpl<Type *> &Tys = ICA.getArgTypes();
    FastMathFlags FMF = ICA.getFlags();
    InstructionCost ScalarizationCostPassed = ICA.getScalarizationCost();
    bool SkipScalarizationCost = ICA.skipScalarizationCost();

    VectorType *VecOpTy = nullptr;
    if (!Tys.empty()) {
      // The vector reduction operand is operand 0 except for fadd/fmul.
      // Their operand 0 is a scalar start value, so the vector op is operand 1.
      unsigned VecTyIndex = 0;
      if (IID == Intrinsic::vector_reduce_fadd ||
          IID == Intrinsic::vector_reduce_fmul)
        VecTyIndex = 1;
      assert(Tys.size() > VecTyIndex && "Unexpected IntrinsicCostAttributes");
      VecOpTy = dyn_cast<VectorType>(Tys[VecTyIndex]);
    }

    // Library call cost - other than size, make it expensive.
    unsigned SingleCallCost = CostKind == TTI::TCK_CodeSize ? 1 : 10;
    unsigned ISD = 0;
    switch (IID) {
    default: {
      // Scalable vectors cannot be scalarized, so return Invalid.
      if (isa<ScalableVectorType>(RetTy) || any_of(Tys, [](const Type *Ty) {
            return isa<ScalableVectorType>(Ty);
          }))
        return InstructionCost::getInvalid();

      // Assume that we need to scalarize this intrinsic.
      InstructionCost ScalarizationCost =
          SkipScalarizationCost ? ScalarizationCostPassed : 0;
      unsigned ScalarCalls = 1;
      Type *ScalarRetTy = RetTy;
      if (auto *RetVTy = dyn_cast<VectorType>(RetTy)) {
        if (!SkipScalarizationCost)
          ScalarizationCost = getScalarizationOverhead(
              RetVTy, /*Insert*/ true, /*Extract*/ false, CostKind);
        ScalarCalls = std::max(ScalarCalls,
                               cast<FixedVectorType>(RetVTy)->getNumElements());
        ScalarRetTy = RetTy->getScalarType();
      }
      SmallVector<Type *, 4> ScalarTys;
      for (Type *Ty : Tys) {
        if (auto *VTy = dyn_cast<VectorType>(Ty)) {
          if (!SkipScalarizationCost)
            ScalarizationCost += getScalarizationOverhead(
                VTy, /*Insert*/ false, /*Extract*/ true, CostKind);
          ScalarCalls = std::max(ScalarCalls,
                                 cast<FixedVectorType>(VTy)->getNumElements());
          Ty = Ty->getScalarType();
        }
        ScalarTys.push_back(Ty);
      }
      if (ScalarCalls == 1)
        return 1; // Return cost of a scalar intrinsic. Assume it to be cheap.

      IntrinsicCostAttributes ScalarAttrs(IID, ScalarRetTy, ScalarTys, FMF);
      InstructionCost ScalarCost =
          thisT()->getIntrinsicInstrCost(ScalarAttrs, CostKind);

      return ScalarCalls * ScalarCost + ScalarizationCost;
    }
    // Look for intrinsics that can be lowered directly or turned into a scalar
    // intrinsic call.
    case Intrinsic::sqrt:
      ISD = ISD::FSQRT;
      break;
    case Intrinsic::sin:
      ISD = ISD::FSIN;
      break;
    case Intrinsic::cos:
      ISD = ISD::FCOS;
      break;
    case Intrinsic::tan:
      ISD = ISD::FTAN;
      break;
    case Intrinsic::asin:
      ISD = ISD::FASIN;
      break;
    case Intrinsic::acos:
      ISD = ISD::FACOS;
      break;
    case Intrinsic::atan:
      ISD = ISD::FATAN;
      break;
    case Intrinsic::sinh:
      ISD = ISD::FSINH;
      break;
    case Intrinsic::cosh:
      ISD = ISD::FCOSH;
      break;
    case Intrinsic::tanh:
      ISD = ISD::FTANH;
      break;
    case Intrinsic::exp:
      ISD = ISD::FEXP;
      break;
    case Intrinsic::exp2:
      ISD = ISD::FEXP2;
      break;
    case Intrinsic::exp10:
      ISD = ISD::FEXP10;
      break;
    case Intrinsic::log:
      ISD = ISD::FLOG;
      break;
    case Intrinsic::log10:
      ISD = ISD::FLOG10;
      break;
    case Intrinsic::log2:
      ISD = ISD::FLOG2;
      break;
    case Intrinsic::fabs:
      ISD = ISD::FABS;
      break;
    case Intrinsic::canonicalize:
      ISD = ISD::FCANONICALIZE;
      break;
    case Intrinsic::minnum:
      ISD = ISD::FMINNUM;
      break;
    case Intrinsic::maxnum:
      ISD = ISD::FMAXNUM;
      break;
    case Intrinsic::minimum:
      ISD = ISD::FMINIMUM;
      break;
    case Intrinsic::maximum:
      ISD = ISD::FMAXIMUM;
      break;
    case Intrinsic::copysign:
      ISD = ISD::FCOPYSIGN;
      break;
    case Intrinsic::floor:
      ISD = ISD::FFLOOR;
      break;
    case Intrinsic::ceil:
      ISD = ISD::FCEIL;
      break;
    case Intrinsic::trunc:
      ISD = ISD::FTRUNC;
      break;
    case Intrinsic::nearbyint:
      ISD = ISD::FNEARBYINT;
      break;
    case Intrinsic::rint:
      ISD = ISD::FRINT;
      break;
    case Intrinsic::lrint:
      ISD = ISD::LRINT;
      break;
    case Intrinsic::llrint:
      ISD = ISD::LLRINT;
      break;
    case Intrinsic::round:
      ISD = ISD::FROUND;
      break;
    case Intrinsic::roundeven:
      ISD = ISD::FROUNDEVEN;
      break;
    case Intrinsic::pow:
      ISD = ISD::FPOW;
      break;
    case Intrinsic::fma:
      ISD = ISD::FMA;
      break;
    case Intrinsic::fmuladd:
      ISD = ISD::FMA;
      break;
    case Intrinsic::experimental_constrained_fmuladd:
      ISD = ISD::STRICT_FMA;
      break;
    // FIXME: We should return 0 whenever getIntrinsicCost == TCC_Free.
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::sideeffect:
    case Intrinsic::pseudoprobe:
    case Intrinsic::arithmetic_fence:
      return 0;
    case Intrinsic::masked_store: {
      Type *Ty = Tys[0];
      Align TyAlign = thisT()->DL.getABITypeAlign(Ty);
      return thisT()->getMaskedMemoryOpCost(Instruction::Store, Ty, TyAlign, 0,
                                            CostKind);
    }
    case Intrinsic::masked_load: {
      Type *Ty = RetTy;
      Align TyAlign = thisT()->DL.getABITypeAlign(Ty);
      return thisT()->getMaskedMemoryOpCost(Instruction::Load, Ty, TyAlign, 0,
                                            CostKind);
    }
    case Intrinsic::vector_reduce_add:
    case Intrinsic::vector_reduce_mul:
    case Intrinsic::vector_reduce_and:
    case Intrinsic::vector_reduce_or:
    case Intrinsic::vector_reduce_xor:
      return thisT()->getArithmeticReductionCost(
          getArithmeticReductionInstruction(IID), VecOpTy, std::nullopt,
          CostKind);
    case Intrinsic::vector_reduce_fadd:
    case Intrinsic::vector_reduce_fmul:
      return thisT()->getArithmeticReductionCost(
          getArithmeticReductionInstruction(IID), VecOpTy, FMF, CostKind);
    case Intrinsic::vector_reduce_smax:
    case Intrinsic::vector_reduce_smin:
    case Intrinsic::vector_reduce_umax:
    case Intrinsic::vector_reduce_umin:
    case Intrinsic::vector_reduce_fmax:
    case Intrinsic::vector_reduce_fmin:
    case Intrinsic::vector_reduce_fmaximum:
    case Intrinsic::vector_reduce_fminimum:
      return thisT()->getMinMaxReductionCost(getMinMaxReductionIntrinsicOp(IID),
                                             VecOpTy, ICA.getFlags(), CostKind);
    case Intrinsic::abs: {
      // abs(X) = select(icmp(X,0),X,sub(0,X))
      Type *CondTy = RetTy->getWithNewBitWidth(1);
      CmpInst::Predicate Pred = CmpInst::ICMP_SGT;
      InstructionCost Cost = 0;
      Cost += thisT()->getCmpSelInstrCost(BinaryOperator::ICmp, RetTy, CondTy,
                                          Pred, CostKind);
      Cost += thisT()->getCmpSelInstrCost(BinaryOperator::Select, RetTy, CondTy,
                                          Pred, CostKind);
      // TODO: Should we add an OperandValueProperties::OP_Zero property?
      Cost += thisT()->getArithmeticInstrCost(
         BinaryOperator::Sub, RetTy, CostKind, {TTI::OK_UniformConstantValue, TTI::OP_None});
      return Cost;
    }
    case Intrinsic::smax:
    case Intrinsic::smin:
    case Intrinsic::umax:
    case Intrinsic::umin: {
      // minmax(X,Y) = select(icmp(X,Y),X,Y)
      Type *CondTy = RetTy->getWithNewBitWidth(1);
      bool IsUnsigned = IID == Intrinsic::umax || IID == Intrinsic::umin;
      CmpInst::Predicate Pred =
          IsUnsigned ? CmpInst::ICMP_UGT : CmpInst::ICMP_SGT;
      InstructionCost Cost = 0;
      Cost += thisT()->getCmpSelInstrCost(BinaryOperator::ICmp, RetTy, CondTy,
                                          Pred, CostKind);
      Cost += thisT()->getCmpSelInstrCost(BinaryOperator::Select, RetTy, CondTy,
                                          Pred, CostKind);
      return Cost;
    }
    case Intrinsic::sadd_sat:
    case Intrinsic::ssub_sat: {
      Type *CondTy = RetTy->getWithNewBitWidth(1);

      Type *OpTy = StructType::create({RetTy, CondTy});
      Intrinsic::ID OverflowOp = IID == Intrinsic::sadd_sat
                                     ? Intrinsic::sadd_with_overflow
                                     : Intrinsic::ssub_with_overflow;
      CmpInst::Predicate Pred = CmpInst::ICMP_SGT;

      // SatMax -> Overflow && SumDiff < 0
      // SatMin -> Overflow && SumDiff >= 0
      InstructionCost Cost = 0;
      IntrinsicCostAttributes Attrs(OverflowOp, OpTy, {RetTy, RetTy}, FMF,
                                    nullptr, ScalarizationCostPassed);
      Cost += thisT()->getIntrinsicInstrCost(Attrs, CostKind);
      Cost += thisT()->getCmpSelInstrCost(BinaryOperator::ICmp, RetTy, CondTy,
                                          Pred, CostKind);
      Cost += 2 * thisT()->getCmpSelInstrCost(BinaryOperator::Select, RetTy,
                                              CondTy, Pred, CostKind);
      return Cost;
    }
    case Intrinsic::uadd_sat:
    case Intrinsic::usub_sat: {
      Type *CondTy = RetTy->getWithNewBitWidth(1);

      Type *OpTy = StructType::create({RetTy, CondTy});
      Intrinsic::ID OverflowOp = IID == Intrinsic::uadd_sat
                                     ? Intrinsic::uadd_with_overflow
                                     : Intrinsic::usub_with_overflow;

      InstructionCost Cost = 0;
      IntrinsicCostAttributes Attrs(OverflowOp, OpTy, {RetTy, RetTy}, FMF,
                                    nullptr, ScalarizationCostPassed);
      Cost += thisT()->getIntrinsicInstrCost(Attrs, CostKind);
      Cost +=
          thisT()->getCmpSelInstrCost(BinaryOperator::Select, RetTy, CondTy,
                                      CmpInst::BAD_ICMP_PREDICATE, CostKind);
      return Cost;
    }
    case Intrinsic::smul_fix:
    case Intrinsic::umul_fix: {
      unsigned ExtSize = RetTy->getScalarSizeInBits() * 2;
      Type *ExtTy = RetTy->getWithNewBitWidth(ExtSize);

      unsigned ExtOp =
          IID == Intrinsic::smul_fix ? Instruction::SExt : Instruction::ZExt;
      TTI::CastContextHint CCH = TTI::CastContextHint::None;

      InstructionCost Cost = 0;
      Cost += 2 * thisT()->getCastInstrCost(ExtOp, ExtTy, RetTy, CCH, CostKind);
      Cost +=
          thisT()->getArithmeticInstrCost(Instruction::Mul, ExtTy, CostKind);
      Cost += 2 * thisT()->getCastInstrCost(Instruction::Trunc, RetTy, ExtTy,
                                            CCH, CostKind);
      Cost += thisT()->getArithmeticInstrCost(Instruction::LShr, RetTy,
                                              CostKind,
                                              {TTI::OK_AnyValue, TTI::OP_None},
                                              {TTI::OK_UniformConstantValue, TTI::OP_None});
      Cost += thisT()->getArithmeticInstrCost(Instruction::Shl, RetTy, CostKind,
                                              {TTI::OK_AnyValue, TTI::OP_None},
                                              {TTI::OK_UniformConstantValue, TTI::OP_None});
      Cost += thisT()->getArithmeticInstrCost(Instruction::Or, RetTy, CostKind);
      return Cost;
    }
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::ssub_with_overflow: {
      Type *SumTy = RetTy->getContainedType(0);
      Type *OverflowTy = RetTy->getContainedType(1);
      unsigned Opcode = IID == Intrinsic::sadd_with_overflow
                            ? BinaryOperator::Add
                            : BinaryOperator::Sub;

      //   Add:
      //   Overflow -> (Result < LHS) ^ (RHS < 0)
      //   Sub:
      //   Overflow -> (Result < LHS) ^ (RHS > 0)
      InstructionCost Cost = 0;
      Cost += thisT()->getArithmeticInstrCost(Opcode, SumTy, CostKind);
      Cost += 2 * thisT()->getCmpSelInstrCost(
                      Instruction::ICmp, SumTy, OverflowTy,
                      CmpInst::ICMP_SGT, CostKind);
      Cost += thisT()->getArithmeticInstrCost(BinaryOperator::Xor, OverflowTy,
                                              CostKind);
      return Cost;
    }
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::usub_with_overflow: {
      Type *SumTy = RetTy->getContainedType(0);
      Type *OverflowTy = RetTy->getContainedType(1);
      unsigned Opcode = IID == Intrinsic::uadd_with_overflow
                            ? BinaryOperator::Add
                            : BinaryOperator::Sub;
      CmpInst::Predicate Pred = IID == Intrinsic::uadd_with_overflow
                                    ? CmpInst::ICMP_ULT
                                    : CmpInst::ICMP_UGT;

      InstructionCost Cost = 0;
      Cost += thisT()->getArithmeticInstrCost(Opcode, SumTy, CostKind);
      Cost +=
          thisT()->getCmpSelInstrCost(BinaryOperator::ICmp, SumTy, OverflowTy,
                                      Pred, CostKind);
      return Cost;
    }
    case Intrinsic::smul_with_overflow:
    case Intrinsic::umul_with_overflow: {
      Type *MulTy = RetTy->getContainedType(0);
      Type *OverflowTy = RetTy->getContainedType(1);
      unsigned ExtSize = MulTy->getScalarSizeInBits() * 2;
      Type *ExtTy = MulTy->getWithNewBitWidth(ExtSize);
      bool IsSigned = IID == Intrinsic::smul_with_overflow;

      unsigned ExtOp = IsSigned ? Instruction::SExt : Instruction::ZExt;
      TTI::CastContextHint CCH = TTI::CastContextHint::None;

      InstructionCost Cost = 0;
      Cost += 2 * thisT()->getCastInstrCost(ExtOp, ExtTy, MulTy, CCH, CostKind);
      Cost +=
          thisT()->getArithmeticInstrCost(Instruction::Mul, ExtTy, CostKind);
      Cost += 2 * thisT()->getCastInstrCost(Instruction::Trunc, MulTy, ExtTy,
                                            CCH, CostKind);
      Cost += thisT()->getArithmeticInstrCost(Instruction::LShr, ExtTy,
                                              CostKind,
                                              {TTI::OK_AnyValue, TTI::OP_None},
                                              {TTI::OK_UniformConstantValue, TTI::OP_None});

      if (IsSigned)
        Cost += thisT()->getArithmeticInstrCost(Instruction::AShr, MulTy,
                                                CostKind,
                                                {TTI::OK_AnyValue, TTI::OP_None},
                                                {TTI::OK_UniformConstantValue, TTI::OP_None});

      Cost += thisT()->getCmpSelInstrCost(
          BinaryOperator::ICmp, MulTy, OverflowTy, CmpInst::ICMP_NE, CostKind);
      return Cost;
    }
    case Intrinsic::fptosi_sat:
    case Intrinsic::fptoui_sat: {
      if (Tys.empty())
        break;
      Type *FromTy = Tys[0];
      bool IsSigned = IID == Intrinsic::fptosi_sat;

      InstructionCost Cost = 0;
      IntrinsicCostAttributes Attrs1(Intrinsic::minnum, FromTy,
                                     {FromTy, FromTy});
      Cost += thisT()->getIntrinsicInstrCost(Attrs1, CostKind);
      IntrinsicCostAttributes Attrs2(Intrinsic::maxnum, FromTy,
                                     {FromTy, FromTy});
      Cost += thisT()->getIntrinsicInstrCost(Attrs2, CostKind);
      Cost += thisT()->getCastInstrCost(
          IsSigned ? Instruction::FPToSI : Instruction::FPToUI, RetTy, FromTy,
          TTI::CastContextHint::None, CostKind);
      if (IsSigned) {
        Type *CondTy = RetTy->getWithNewBitWidth(1);
        Cost += thisT()->getCmpSelInstrCost(
            BinaryOperator::FCmp, FromTy, CondTy, CmpInst::FCMP_UNO, CostKind);
        Cost += thisT()->getCmpSelInstrCost(
            BinaryOperator::Select, RetTy, CondTy, CmpInst::FCMP_UNO, CostKind);
      }
      return Cost;
    }
    case Intrinsic::ctpop:
      ISD = ISD::CTPOP;
      // In case of legalization use TCC_Expensive. This is cheaper than a
      // library call but still not a cheap instruction.
      SingleCallCost = TargetTransformInfo::TCC_Expensive;
      break;
    case Intrinsic::ctlz:
      ISD = ISD::CTLZ;
      break;
    case Intrinsic::cttz:
      ISD = ISD::CTTZ;
      break;
    case Intrinsic::bswap:
      ISD = ISD::BSWAP;
      break;
    case Intrinsic::bitreverse:
      ISD = ISD::BITREVERSE;
      break;
    }

    const TargetLoweringBase *TLI = getTLI();
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(RetTy);

    if (TLI->isOperationLegalOrPromote(ISD, LT.second)) {
      if (IID == Intrinsic::fabs && LT.second.isFloatingPoint() &&
          TLI->isFAbsFree(LT.second)) {
        return 0;
      }

      // The operation is legal. Assume it costs 1.
      // If the type is split to multiple registers, assume that there is some
      // overhead to this.
      // TODO: Once we have extract/insert subvector cost we need to use them.
      if (LT.first > 1)
        return (LT.first * 2);
      else
        return (LT.first * 1);
    } else if (!TLI->isOperationExpand(ISD, LT.second)) {
      // If the operation is custom lowered then assume
      // that the code is twice as expensive.
      return (LT.first * 2);
    }

    // If we can't lower fmuladd into an FMA estimate the cost as a floating
    // point mul followed by an add.
    if (IID == Intrinsic::fmuladd)
      return thisT()->getArithmeticInstrCost(BinaryOperator::FMul, RetTy,
                                             CostKind) +
             thisT()->getArithmeticInstrCost(BinaryOperator::FAdd, RetTy,
                                             CostKind);
    if (IID == Intrinsic::experimental_constrained_fmuladd) {
      IntrinsicCostAttributes FMulAttrs(
        Intrinsic::experimental_constrained_fmul, RetTy, Tys);
      IntrinsicCostAttributes FAddAttrs(
        Intrinsic::experimental_constrained_fadd, RetTy, Tys);
      return thisT()->getIntrinsicInstrCost(FMulAttrs, CostKind) +
             thisT()->getIntrinsicInstrCost(FAddAttrs, CostKind);
    }

    // Else, assume that we need to scalarize this intrinsic. For math builtins
    // this will emit a costly libcall, adding call overhead and spills. Make it
    // very expensive.
    if (auto *RetVTy = dyn_cast<VectorType>(RetTy)) {
      // Scalable vectors cannot be scalarized, so return Invalid.
      if (isa<ScalableVectorType>(RetTy) || any_of(Tys, [](const Type *Ty) {
            return isa<ScalableVectorType>(Ty);
          }))
        return InstructionCost::getInvalid();

      InstructionCost ScalarizationCost =
          SkipScalarizationCost
              ? ScalarizationCostPassed
              : getScalarizationOverhead(RetVTy, /*Insert*/ true,
                                         /*Extract*/ false, CostKind);

      unsigned ScalarCalls = cast<FixedVectorType>(RetVTy)->getNumElements();
      SmallVector<Type *, 4> ScalarTys;
      for (Type *Ty : Tys) {
        if (Ty->isVectorTy())
          Ty = Ty->getScalarType();
        ScalarTys.push_back(Ty);
      }
      IntrinsicCostAttributes Attrs(IID, RetTy->getScalarType(), ScalarTys, FMF);
      InstructionCost ScalarCost =
          thisT()->getIntrinsicInstrCost(Attrs, CostKind);
      for (Type *Ty : Tys) {
        if (auto *VTy = dyn_cast<VectorType>(Ty)) {
          if (!ICA.skipScalarizationCost())
            ScalarizationCost += getScalarizationOverhead(
                VTy, /*Insert*/ false, /*Extract*/ true, CostKind);
          ScalarCalls = std::max(ScalarCalls,
                                 cast<FixedVectorType>(VTy)->getNumElements());
        }
      }
      return ScalarCalls * ScalarCost + ScalarizationCost;
    }

    // This is going to be turned into a library call, make it expensive.
    return SingleCallCost;
  }

  /// Compute a cost of the given call instruction.
  ///
  /// Compute the cost of calling function F with return type RetTy and
  /// argument types Tys. F might be nullptr, in this case the cost of an
  /// arbitrary call with the specified signature will be returned.
  /// This is used, for instance,  when we estimate call of a vector
  /// counterpart of the given function.
  /// \param F Called function, might be nullptr.
  /// \param RetTy Return value types.
  /// \param Tys Argument types.
  /// \returns The cost of Call instruction.
  InstructionCost getCallInstrCost(Function *F, Type *RetTy,
                                   ArrayRef<Type *> Tys,
                                   TTI::TargetCostKind CostKind) {
    return 10;
  }

  unsigned getNumberOfParts(Type *Tp) {
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);
    return LT.first.isValid() ? *LT.first.getValue() : 0;
  }

  InstructionCost getAddressComputationCost(Type *Ty, ScalarEvolution *,
                                            const SCEV *) {
    return 0;
  }

  /// Try to calculate arithmetic and shuffle op costs for reduction intrinsics.
  /// We're assuming that reduction operation are performing the following way:
  ///
  /// %val1 = shufflevector<n x t> %val, <n x t> %undef,
  /// <n x i32> <i32 n/2, i32 n/2 + 1, ..., i32 n, i32 undef, ..., i32 undef>
  ///            \----------------v-------------/  \----------v------------/
  ///                            n/2 elements               n/2 elements
  /// %red1 = op <n x t> %val, <n x t> val1
  /// After this operation we have a vector %red1 where only the first n/2
  /// elements are meaningful, the second n/2 elements are undefined and can be
  /// dropped. All other operations are actually working with the vector of
  /// length n/2, not n, though the real vector length is still n.
  /// %val2 = shufflevector<n x t> %red1, <n x t> %undef,
  /// <n x i32> <i32 n/4, i32 n/4 + 1, ..., i32 n/2, i32 undef, ..., i32 undef>
  ///            \----------------v-------------/  \----------v------------/
  ///                            n/4 elements               3*n/4 elements
  /// %red2 = op <n x t> %red1, <n x t> val2  - working with the vector of
  /// length n/2, the resulting vector has length n/4 etc.
  ///
  /// The cost model should take into account that the actual length of the
  /// vector is reduced on each iteration.
  InstructionCost getTreeReductionCost(unsigned Opcode, VectorType *Ty,
                                       TTI::TargetCostKind CostKind) {
    // Targets must implement a default value for the scalable case, since
    // we don't know how many lanes the vector has.
    if (isa<ScalableVectorType>(Ty))
      return InstructionCost::getInvalid();

    Type *ScalarTy = Ty->getElementType();
    unsigned NumVecElts = cast<FixedVectorType>(Ty)->getNumElements();
    if ((Opcode == Instruction::Or || Opcode == Instruction::And) &&
        ScalarTy == IntegerType::getInt1Ty(Ty->getContext()) &&
        NumVecElts >= 2) {
      // Or reduction for i1 is represented as:
      // %val = bitcast <ReduxWidth x i1> to iReduxWidth
      // %res = cmp ne iReduxWidth %val, 0
      // And reduction for i1 is represented as:
      // %val = bitcast <ReduxWidth x i1> to iReduxWidth
      // %res = cmp eq iReduxWidth %val, 11111
      Type *ValTy = IntegerType::get(Ty->getContext(), NumVecElts);
      return thisT()->getCastInstrCost(Instruction::BitCast, ValTy, Ty,
                                       TTI::CastContextHint::None, CostKind) +
             thisT()->getCmpSelInstrCost(Instruction::ICmp, ValTy,
                                         CmpInst::makeCmpResultType(ValTy),
                                         CmpInst::BAD_ICMP_PREDICATE, CostKind);
    }
    unsigned NumReduxLevels = Log2_32(NumVecElts);
    InstructionCost ArithCost = 0;
    InstructionCost ShuffleCost = 0;
    std::pair<InstructionCost, MVT> LT = thisT()->getTypeLegalizationCost(Ty);
    unsigned LongVectorCount = 0;
    unsigned MVTLen =
        LT.second.isVector() ? LT.second.getVectorNumElements() : 1;
    while (NumVecElts > MVTLen) {
      NumVecElts /= 2;
      VectorType *SubTy = FixedVectorType::get(ScalarTy, NumVecElts);
      ShuffleCost +=
          thisT()->getShuffleCost(TTI::SK_ExtractSubvector, Ty, std::nullopt,
                                  CostKind, NumVecElts, SubTy);
      ArithCost += thisT()->getArithmeticInstrCost(Opcode, SubTy, CostKind);
      Ty = SubTy;
      ++LongVectorCount;
    }

    NumReduxLevels -= LongVectorCount;

    // The minimal length of the vector is limited by the real length of vector
    // operations performed on the current platform. That's why several final
    // reduction operations are performed on the vectors with the same
    // architecture-dependent length.

    // By default reductions need one shuffle per reduction level.
    ShuffleCost +=
        NumReduxLevels * thisT()->getShuffleCost(TTI::SK_PermuteSingleSrc, Ty,
                                                 std::nullopt, CostKind, 0, Ty);
    ArithCost +=
        NumReduxLevels * thisT()->getArithmeticInstrCost(Opcode, Ty, CostKind);
    return ShuffleCost + ArithCost +
           thisT()->getVectorInstrCost(Instruction::ExtractElement, Ty,
                                       CostKind, 0, nullptr, nullptr);
  }

  /// Try to calculate the cost of performing strict (in-order) reductions,
  /// which involves doing a sequence of floating point additions in lane
  /// order, starting with an initial value. For example, consider a scalar
  /// initial value 'InitVal' of type float and a vector of type <4 x float>:
  ///
  ///   Vector = <float %v0, float %v1, float %v2, float %v3>
  ///
  ///   %add1 = %InitVal + %v0
  ///   %add2 = %add1 + %v1
  ///   %add3 = %add2 + %v2
  ///   %add4 = %add3 + %v3
  ///
  /// As a simple estimate we can say the cost of such a reduction is 4 times
  /// the cost of a scalar FP addition. We can only estimate the costs for
  /// fixed-width vectors here because for scalable vectors we do not know the
  /// runtime number of operations.
  InstructionCost getOrderedReductionCost(unsigned Opcode, VectorType *Ty,
                                          TTI::TargetCostKind CostKind) {
    // Targets must implement a default value for the scalable case, since
    // we don't know how many lanes the vector has.
    if (isa<ScalableVectorType>(Ty))
      return InstructionCost::getInvalid();

    auto *VTy = cast<FixedVectorType>(Ty);
    InstructionCost ExtractCost = getScalarizationOverhead(
        VTy, /*Insert=*/false, /*Extract=*/true, CostKind);
    InstructionCost ArithCost = thisT()->getArithmeticInstrCost(
        Opcode, VTy->getElementType(), CostKind);
    ArithCost *= VTy->getNumElements();

    return ExtractCost + ArithCost;
  }

  InstructionCost getArithmeticReductionCost(unsigned Opcode, VectorType *Ty,
                                             std::optional<FastMathFlags> FMF,
                                             TTI::TargetCostKind CostKind) {
    assert(Ty && "Unknown reduction vector type");
    if (TTI::requiresOrderedReduction(FMF))
      return getOrderedReductionCost(Opcode, Ty, CostKind);
    return getTreeReductionCost(Opcode, Ty, CostKind);
  }

  /// Try to calculate op costs for min/max reduction operations.
  /// \param CondTy Conditional type for the Select instruction.
  InstructionCost getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                         FastMathFlags FMF,
                                         TTI::TargetCostKind CostKind) {
    // Targets must implement a default value for the scalable case, since
    // we don't know how many lanes the vector has.
    if (isa<ScalableVectorType>(Ty))
      return InstructionCost::getInvalid();

    Type *ScalarTy = Ty->getElementType();
    unsigned NumVecElts = cast<FixedVectorType>(Ty)->getNumElements();
    unsigned NumReduxLevels = Log2_32(NumVecElts);
    InstructionCost MinMaxCost = 0;
    InstructionCost ShuffleCost = 0;
    std::pair<InstructionCost, MVT> LT = thisT()->getTypeLegalizationCost(Ty);
    unsigned LongVectorCount = 0;
    unsigned MVTLen =
        LT.second.isVector() ? LT.second.getVectorNumElements() : 1;
    while (NumVecElts > MVTLen) {
      NumVecElts /= 2;
      auto *SubTy = FixedVectorType::get(ScalarTy, NumVecElts);

      ShuffleCost +=
          thisT()->getShuffleCost(TTI::SK_ExtractSubvector, Ty, std::nullopt,
                                  CostKind, NumVecElts, SubTy);

      IntrinsicCostAttributes Attrs(IID, SubTy, {SubTy, SubTy}, FMF);
      MinMaxCost += getIntrinsicInstrCost(Attrs, CostKind);
      Ty = SubTy;
      ++LongVectorCount;
    }

    NumReduxLevels -= LongVectorCount;

    // The minimal length of the vector is limited by the real length of vector
    // operations performed on the current platform. That's why several final
    // reduction opertions are perfomed on the vectors with the same
    // architecture-dependent length.
    ShuffleCost +=
        NumReduxLevels * thisT()->getShuffleCost(TTI::SK_PermuteSingleSrc, Ty,
                                                 std::nullopt, CostKind, 0, Ty);
    IntrinsicCostAttributes Attrs(IID, Ty, {Ty, Ty}, FMF);
    MinMaxCost += NumReduxLevels * getIntrinsicInstrCost(Attrs, CostKind);
    // The last min/max should be in vector registers and we counted it above.
    // So just need a single extractelement.
    return ShuffleCost + MinMaxCost +
           thisT()->getVectorInstrCost(Instruction::ExtractElement, Ty,
                                       CostKind, 0, nullptr, nullptr);
  }

  InstructionCost getExtendedReductionCost(unsigned Opcode, bool IsUnsigned,
                                           Type *ResTy, VectorType *Ty,
                                           FastMathFlags FMF,
                                           TTI::TargetCostKind CostKind) {
    // Without any native support, this is equivalent to the cost of
    // vecreduce.opcode(ext(Ty A)).
    VectorType *ExtTy = VectorType::get(ResTy, Ty);
    InstructionCost RedCost =
        thisT()->getArithmeticReductionCost(Opcode, ExtTy, FMF, CostKind);
    InstructionCost ExtCost = thisT()->getCastInstrCost(
        IsUnsigned ? Instruction::ZExt : Instruction::SExt, ExtTy, Ty,
        TTI::CastContextHint::None, CostKind);

    return RedCost + ExtCost;
  }

  InstructionCost getMulAccReductionCost(bool IsUnsigned, Type *ResTy,
                                         VectorType *Ty,
                                         TTI::TargetCostKind CostKind) {
    // Without any native support, this is equivalent to the cost of
    // vecreduce.add(mul(ext(Ty A), ext(Ty B))) or
    // vecreduce.add(mul(A, B)).
    VectorType *ExtTy = VectorType::get(ResTy, Ty);
    InstructionCost RedCost = thisT()->getArithmeticReductionCost(
        Instruction::Add, ExtTy, std::nullopt, CostKind);
    InstructionCost ExtCost = thisT()->getCastInstrCost(
        IsUnsigned ? Instruction::ZExt : Instruction::SExt, ExtTy, Ty,
        TTI::CastContextHint::None, CostKind);

    InstructionCost MulCost =
        thisT()->getArithmeticInstrCost(Instruction::Mul, ExtTy, CostKind);

    return RedCost + MulCost + 2 * ExtCost;
  }

  InstructionCost getVectorSplitCost() { return 1; }

  /// @}
};

/// Concrete BasicTTIImpl that can be used if no further customization
/// is needed.
class BasicTTIImpl : public BasicTTIImplBase<BasicTTIImpl> {
  using BaseT = BasicTTIImplBase<BasicTTIImpl>;

  friend class BasicTTIImplBase<BasicTTIImpl>;

  const TargetSubtargetInfo *ST;
  const TargetLoweringBase *TLI;

  const TargetSubtargetInfo *getST() const { return ST; }
  const TargetLoweringBase *getTLI() const { return TLI; }

public:
  explicit BasicTTIImpl(const TargetMachine *TM, const Function &F);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_BASICTTIIMPL_H
