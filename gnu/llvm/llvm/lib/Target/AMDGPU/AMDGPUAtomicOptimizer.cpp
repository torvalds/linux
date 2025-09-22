//===-- AMDGPUAtomicOptimizer.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass optimizes atomic operations by using a single lane of a wavefront
/// to perform the atomic operation, thus reducing contention on that memory
/// location.
/// Atomic optimizer uses following strategies to compute scan and reduced
/// values
/// 1. DPP -
///   This is the most efficient implementation for scan. DPP uses Whole Wave
///   Mode (WWM)
/// 2. Iterative -
//    An alternative implementation iterates over all active lanes
///   of Wavefront using llvm.cttz and performs scan  using readlane & writelane
///   intrinsics
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/InitializePasses.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#define DEBUG_TYPE "amdgpu-atomic-optimizer"

using namespace llvm;
using namespace llvm::AMDGPU;

namespace {

struct ReplacementInfo {
  Instruction *I;
  AtomicRMWInst::BinOp Op;
  unsigned ValIdx;
  bool ValDivergent;
};

class AMDGPUAtomicOptimizer : public FunctionPass {
public:
  static char ID;
  ScanOptions ScanImpl;
  AMDGPUAtomicOptimizer(ScanOptions ScanImpl)
      : FunctionPass(ID), ScanImpl(ScanImpl) {}

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addRequired<UniformityInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
  }
};

class AMDGPUAtomicOptimizerImpl
    : public InstVisitor<AMDGPUAtomicOptimizerImpl> {
private:
  SmallVector<ReplacementInfo, 8> ToReplace;
  const UniformityInfo *UA;
  const DataLayout *DL;
  DomTreeUpdater &DTU;
  const GCNSubtarget *ST;
  bool IsPixelShader;
  ScanOptions ScanImpl;

  Value *buildReduction(IRBuilder<> &B, AtomicRMWInst::BinOp Op, Value *V,
                        Value *const Identity) const;
  Value *buildScan(IRBuilder<> &B, AtomicRMWInst::BinOp Op, Value *V,
                   Value *const Identity) const;
  Value *buildShiftRight(IRBuilder<> &B, Value *V, Value *const Identity) const;

  std::pair<Value *, Value *>
  buildScanIteratively(IRBuilder<> &B, AtomicRMWInst::BinOp Op,
                       Value *const Identity, Value *V, Instruction &I,
                       BasicBlock *ComputeLoop, BasicBlock *ComputeEnd) const;

  void optimizeAtomic(Instruction &I, AtomicRMWInst::BinOp Op, unsigned ValIdx,
                      bool ValDivergent) const;

public:
  AMDGPUAtomicOptimizerImpl() = delete;

  AMDGPUAtomicOptimizerImpl(const UniformityInfo *UA, const DataLayout *DL,
                            DomTreeUpdater &DTU, const GCNSubtarget *ST,
                            bool IsPixelShader, ScanOptions ScanImpl)
      : UA(UA), DL(DL), DTU(DTU), ST(ST), IsPixelShader(IsPixelShader),
        ScanImpl(ScanImpl) {}

  bool run(Function &F);

  void visitAtomicRMWInst(AtomicRMWInst &I);
  void visitIntrinsicInst(IntrinsicInst &I);
};

} // namespace

char AMDGPUAtomicOptimizer::ID = 0;

char &llvm::AMDGPUAtomicOptimizerID = AMDGPUAtomicOptimizer::ID;

bool AMDGPUAtomicOptimizer::runOnFunction(Function &F) {
  if (skipFunction(F)) {
    return false;
  }

  const UniformityInfo *UA =
      &getAnalysis<UniformityInfoWrapperPass>().getUniformityInfo();
  const DataLayout *DL = &F.getDataLayout();

  DominatorTreeWrapperPass *const DTW =
      getAnalysisIfAvailable<DominatorTreeWrapperPass>();
  DomTreeUpdater DTU(DTW ? &DTW->getDomTree() : nullptr,
                     DomTreeUpdater::UpdateStrategy::Lazy);

  const TargetPassConfig &TPC = getAnalysis<TargetPassConfig>();
  const TargetMachine &TM = TPC.getTM<TargetMachine>();
  const GCNSubtarget *ST = &TM.getSubtarget<GCNSubtarget>(F);

  bool IsPixelShader = F.getCallingConv() == CallingConv::AMDGPU_PS;

  return AMDGPUAtomicOptimizerImpl(UA, DL, DTU, ST, IsPixelShader, ScanImpl)
      .run(F);
}

PreservedAnalyses AMDGPUAtomicOptimizerPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {

  const auto *UA = &AM.getResult<UniformityInfoAnalysis>(F);
  const DataLayout *DL = &F.getDataLayout();

  DomTreeUpdater DTU(&AM.getResult<DominatorTreeAnalysis>(F),
                     DomTreeUpdater::UpdateStrategy::Lazy);
  const GCNSubtarget *ST = &TM.getSubtarget<GCNSubtarget>(F);

  bool IsPixelShader = F.getCallingConv() == CallingConv::AMDGPU_PS;

  bool IsChanged =
      AMDGPUAtomicOptimizerImpl(UA, DL, DTU, ST, IsPixelShader, ScanImpl)
          .run(F);

  if (!IsChanged) {
    return PreservedAnalyses::all();
  }

  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

bool AMDGPUAtomicOptimizerImpl::run(Function &F) {

  // Scan option None disables the Pass
  if (ScanImpl == ScanOptions::None) {
    return false;
  }

  visit(F);

  const bool Changed = !ToReplace.empty();

  for (ReplacementInfo &Info : ToReplace) {
    optimizeAtomic(*Info.I, Info.Op, Info.ValIdx, Info.ValDivergent);
  }

  ToReplace.clear();

  return Changed;
}

static bool isLegalCrossLaneType(Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::FloatTyID:
  case Type::DoubleTyID:
    return true;
  case Type::IntegerTyID: {
    unsigned Size = Ty->getIntegerBitWidth();
    return (Size == 32 || Size == 64);
  }
  default:
    return false;
  }
}

void AMDGPUAtomicOptimizerImpl::visitAtomicRMWInst(AtomicRMWInst &I) {
  // Early exit for unhandled address space atomic instructions.
  switch (I.getPointerAddressSpace()) {
  default:
    return;
  case AMDGPUAS::GLOBAL_ADDRESS:
  case AMDGPUAS::LOCAL_ADDRESS:
    break;
  }

  AtomicRMWInst::BinOp Op = I.getOperation();

  switch (Op) {
  default:
    return;
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::And:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::Max:
  case AtomicRMWInst::Min:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::UMin:
  case AtomicRMWInst::FAdd:
  case AtomicRMWInst::FSub:
  case AtomicRMWInst::FMax:
  case AtomicRMWInst::FMin:
    break;
  }

  // Only 32 and 64 bit floating point atomic ops are supported.
  if (AtomicRMWInst::isFPOperation(Op) &&
      !(I.getType()->isFloatTy() || I.getType()->isDoubleTy())) {
    return;
  }

  const unsigned PtrIdx = 0;
  const unsigned ValIdx = 1;

  // If the pointer operand is divergent, then each lane is doing an atomic
  // operation on a different address, and we cannot optimize that.
  if (UA->isDivergentUse(I.getOperandUse(PtrIdx))) {
    return;
  }

  bool ValDivergent = UA->isDivergentUse(I.getOperandUse(ValIdx));

  // If the value operand is divergent, each lane is contributing a different
  // value to the atomic calculation. We can only optimize divergent values if
  // we have DPP available on our subtarget (for DPP strategy), and the atomic
  // operation is 32 or 64 bits.
  if (ValDivergent) {
    if (ScanImpl == ScanOptions::DPP && !ST->hasDPP())
      return;

    if (!isLegalCrossLaneType(I.getType()))
      return;
  }

  // If we get here, we can optimize the atomic using a single wavefront-wide
  // atomic operation to do the calculation for the entire wavefront, so
  // remember the instruction so we can come back to it.
  const ReplacementInfo Info = {&I, Op, ValIdx, ValDivergent};

  ToReplace.push_back(Info);
}

void AMDGPUAtomicOptimizerImpl::visitIntrinsicInst(IntrinsicInst &I) {
  AtomicRMWInst::BinOp Op;

  switch (I.getIntrinsicID()) {
  default:
    return;
  case Intrinsic::amdgcn_struct_buffer_atomic_add:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_add:
  case Intrinsic::amdgcn_raw_buffer_atomic_add:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_add:
    Op = AtomicRMWInst::Add;
    break;
  case Intrinsic::amdgcn_struct_buffer_atomic_sub:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_sub:
  case Intrinsic::amdgcn_raw_buffer_atomic_sub:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_sub:
    Op = AtomicRMWInst::Sub;
    break;
  case Intrinsic::amdgcn_struct_buffer_atomic_and:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_and:
  case Intrinsic::amdgcn_raw_buffer_atomic_and:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_and:
    Op = AtomicRMWInst::And;
    break;
  case Intrinsic::amdgcn_struct_buffer_atomic_or:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_or:
  case Intrinsic::amdgcn_raw_buffer_atomic_or:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_or:
    Op = AtomicRMWInst::Or;
    break;
  case Intrinsic::amdgcn_struct_buffer_atomic_xor:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_xor:
  case Intrinsic::amdgcn_raw_buffer_atomic_xor:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_xor:
    Op = AtomicRMWInst::Xor;
    break;
  case Intrinsic::amdgcn_struct_buffer_atomic_smin:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_smin:
  case Intrinsic::amdgcn_raw_buffer_atomic_smin:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_smin:
    Op = AtomicRMWInst::Min;
    break;
  case Intrinsic::amdgcn_struct_buffer_atomic_umin:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_umin:
  case Intrinsic::amdgcn_raw_buffer_atomic_umin:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_umin:
    Op = AtomicRMWInst::UMin;
    break;
  case Intrinsic::amdgcn_struct_buffer_atomic_smax:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_smax:
  case Intrinsic::amdgcn_raw_buffer_atomic_smax:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_smax:
    Op = AtomicRMWInst::Max;
    break;
  case Intrinsic::amdgcn_struct_buffer_atomic_umax:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_umax:
  case Intrinsic::amdgcn_raw_buffer_atomic_umax:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_umax:
    Op = AtomicRMWInst::UMax;
    break;
  }

  const unsigned ValIdx = 0;

  const bool ValDivergent = UA->isDivergentUse(I.getOperandUse(ValIdx));

  // If the value operand is divergent, each lane is contributing a different
  // value to the atomic calculation. We can only optimize divergent values if
  // we have DPP available on our subtarget (for DPP strategy), and the atomic
  // operation is 32 or 64 bits.
  if (ValDivergent) {
    if (ScanImpl == ScanOptions::DPP && !ST->hasDPP())
      return;

    if (!isLegalCrossLaneType(I.getType()))
      return;
  }

  // If any of the other arguments to the intrinsic are divergent, we can't
  // optimize the operation.
  for (unsigned Idx = 1; Idx < I.getNumOperands(); Idx++) {
    if (UA->isDivergentUse(I.getOperandUse(Idx))) {
      return;
    }
  }

  // If we get here, we can optimize the atomic using a single wavefront-wide
  // atomic operation to do the calculation for the entire wavefront, so
  // remember the instruction so we can come back to it.
  const ReplacementInfo Info = {&I, Op, ValIdx, ValDivergent};

  ToReplace.push_back(Info);
}

// Use the builder to create the non-atomic counterpart of the specified
// atomicrmw binary op.
static Value *buildNonAtomicBinOp(IRBuilder<> &B, AtomicRMWInst::BinOp Op,
                                  Value *LHS, Value *RHS) {
  CmpInst::Predicate Pred;

  switch (Op) {
  default:
    llvm_unreachable("Unhandled atomic op");
  case AtomicRMWInst::Add:
    return B.CreateBinOp(Instruction::Add, LHS, RHS);
  case AtomicRMWInst::FAdd:
    return B.CreateFAdd(LHS, RHS);
  case AtomicRMWInst::Sub:
    return B.CreateBinOp(Instruction::Sub, LHS, RHS);
  case AtomicRMWInst::FSub:
    return B.CreateFSub(LHS, RHS);
  case AtomicRMWInst::And:
    return B.CreateBinOp(Instruction::And, LHS, RHS);
  case AtomicRMWInst::Or:
    return B.CreateBinOp(Instruction::Or, LHS, RHS);
  case AtomicRMWInst::Xor:
    return B.CreateBinOp(Instruction::Xor, LHS, RHS);

  case AtomicRMWInst::Max:
    Pred = CmpInst::ICMP_SGT;
    break;
  case AtomicRMWInst::Min:
    Pred = CmpInst::ICMP_SLT;
    break;
  case AtomicRMWInst::UMax:
    Pred = CmpInst::ICMP_UGT;
    break;
  case AtomicRMWInst::UMin:
    Pred = CmpInst::ICMP_ULT;
    break;
  case AtomicRMWInst::FMax:
    return B.CreateMaxNum(LHS, RHS);
  case AtomicRMWInst::FMin:
    return B.CreateMinNum(LHS, RHS);
  }
  Value *Cond = B.CreateICmp(Pred, LHS, RHS);
  return B.CreateSelect(Cond, LHS, RHS);
}

// Use the builder to create a reduction of V across the wavefront, with all
// lanes active, returning the same result in all lanes.
Value *AMDGPUAtomicOptimizerImpl::buildReduction(IRBuilder<> &B,
                                                 AtomicRMWInst::BinOp Op,
                                                 Value *V,
                                                 Value *const Identity) const {
  Type *AtomicTy = V->getType();
  Module *M = B.GetInsertBlock()->getModule();
  Function *UpdateDPP =
      Intrinsic::getDeclaration(M, Intrinsic::amdgcn_update_dpp, AtomicTy);

  // Reduce within each row of 16 lanes.
  for (unsigned Idx = 0; Idx < 4; Idx++) {
    V = buildNonAtomicBinOp(
        B, Op, V,
        B.CreateCall(UpdateDPP,
                     {Identity, V, B.getInt32(DPP::ROW_XMASK0 | 1 << Idx),
                      B.getInt32(0xf), B.getInt32(0xf), B.getFalse()}));
  }

  // Reduce within each pair of rows (i.e. 32 lanes).
  assert(ST->hasPermLaneX16());
  Value *Permlanex16Call = B.CreateIntrinsic(
      V->getType(), Intrinsic::amdgcn_permlanex16,
      {V, V, B.getInt32(-1), B.getInt32(-1), B.getFalse(), B.getFalse()});
  V = buildNonAtomicBinOp(B, Op, V, Permlanex16Call);
  if (ST->isWave32()) {
    return V;
  }

  if (ST->hasPermLane64()) {
    // Reduce across the upper and lower 32 lanes.
    Value *Permlane64Call =
        B.CreateIntrinsic(V->getType(), Intrinsic::amdgcn_permlane64, V);
    return buildNonAtomicBinOp(B, Op, V, Permlane64Call);
  }

  // Pick an arbitrary lane from 0..31 and an arbitrary lane from 32..63 and
  // combine them with a scalar operation.
  Function *ReadLane =
      Intrinsic::getDeclaration(M, Intrinsic::amdgcn_readlane, AtomicTy);
  Value *Lane0 = B.CreateCall(ReadLane, {V, B.getInt32(0)});
  Value *Lane32 = B.CreateCall(ReadLane, {V, B.getInt32(32)});
  return buildNonAtomicBinOp(B, Op, Lane0, Lane32);
}

// Use the builder to create an inclusive scan of V across the wavefront, with
// all lanes active.
Value *AMDGPUAtomicOptimizerImpl::buildScan(IRBuilder<> &B,
                                            AtomicRMWInst::BinOp Op, Value *V,
                                            Value *Identity) const {
  Type *AtomicTy = V->getType();
  Module *M = B.GetInsertBlock()->getModule();
  Function *UpdateDPP =
      Intrinsic::getDeclaration(M, Intrinsic::amdgcn_update_dpp, AtomicTy);

  for (unsigned Idx = 0; Idx < 4; Idx++) {
    V = buildNonAtomicBinOp(
        B, Op, V,
        B.CreateCall(UpdateDPP,
                     {Identity, V, B.getInt32(DPP::ROW_SHR0 | 1 << Idx),
                      B.getInt32(0xf), B.getInt32(0xf), B.getFalse()}));
  }
  if (ST->hasDPPBroadcasts()) {
    // GFX9 has DPP row broadcast operations.
    V = buildNonAtomicBinOp(
        B, Op, V,
        B.CreateCall(UpdateDPP,
                     {Identity, V, B.getInt32(DPP::BCAST15), B.getInt32(0xa),
                      B.getInt32(0xf), B.getFalse()}));
    V = buildNonAtomicBinOp(
        B, Op, V,
        B.CreateCall(UpdateDPP,
                     {Identity, V, B.getInt32(DPP::BCAST31), B.getInt32(0xc),
                      B.getInt32(0xf), B.getFalse()}));
  } else {
    // On GFX10 all DPP operations are confined to a single row. To get cross-
    // row operations we have to use permlane or readlane.

    // Combine lane 15 into lanes 16..31 (and, for wave 64, lane 47 into lanes
    // 48..63).
    assert(ST->hasPermLaneX16());
    Value *PermX = B.CreateIntrinsic(
        V->getType(), Intrinsic::amdgcn_permlanex16,
        {V, V, B.getInt32(-1), B.getInt32(-1), B.getFalse(), B.getFalse()});

    Value *UpdateDPPCall = B.CreateCall(
        UpdateDPP, {Identity, PermX, B.getInt32(DPP::QUAD_PERM_ID),
                    B.getInt32(0xa), B.getInt32(0xf), B.getFalse()});
    V = buildNonAtomicBinOp(B, Op, V, UpdateDPPCall);

    if (!ST->isWave32()) {
      // Combine lane 31 into lanes 32..63.
      Value *const Lane31 = B.CreateIntrinsic(
          V->getType(), Intrinsic::amdgcn_readlane, {V, B.getInt32(31)});

      Value *UpdateDPPCall = B.CreateCall(
          UpdateDPP, {Identity, Lane31, B.getInt32(DPP::QUAD_PERM_ID),
                      B.getInt32(0xc), B.getInt32(0xf), B.getFalse()});

      V = buildNonAtomicBinOp(B, Op, V, UpdateDPPCall);
    }
  }
  return V;
}

// Use the builder to create a shift right of V across the wavefront, with all
// lanes active, to turn an inclusive scan into an exclusive scan.
Value *AMDGPUAtomicOptimizerImpl::buildShiftRight(IRBuilder<> &B, Value *V,
                                                  Value *Identity) const {
  Type *AtomicTy = V->getType();
  Module *M = B.GetInsertBlock()->getModule();
  Function *UpdateDPP =
      Intrinsic::getDeclaration(M, Intrinsic::amdgcn_update_dpp, AtomicTy);
  if (ST->hasDPPWavefrontShifts()) {
    // GFX9 has DPP wavefront shift operations.
    V = B.CreateCall(UpdateDPP,
                     {Identity, V, B.getInt32(DPP::WAVE_SHR1), B.getInt32(0xf),
                      B.getInt32(0xf), B.getFalse()});
  } else {
    Function *ReadLane =
        Intrinsic::getDeclaration(M, Intrinsic::amdgcn_readlane, AtomicTy);
    Function *WriteLane =
        Intrinsic::getDeclaration(M, Intrinsic::amdgcn_writelane, AtomicTy);

    // On GFX10 all DPP operations are confined to a single row. To get cross-
    // row operations we have to use permlane or readlane.
    Value *Old = V;
    V = B.CreateCall(UpdateDPP,
                     {Identity, V, B.getInt32(DPP::ROW_SHR0 + 1),
                      B.getInt32(0xf), B.getInt32(0xf), B.getFalse()});

    // Copy the old lane 15 to the new lane 16.
    V = B.CreateCall(WriteLane, {B.CreateCall(ReadLane, {Old, B.getInt32(15)}),
                                 B.getInt32(16), V});

    if (!ST->isWave32()) {
      // Copy the old lane 31 to the new lane 32.
      V = B.CreateCall(
          WriteLane,
          {B.CreateCall(ReadLane, {Old, B.getInt32(31)}), B.getInt32(32), V});

      // Copy the old lane 47 to the new lane 48.
      V = B.CreateCall(
          WriteLane,
          {B.CreateCall(ReadLane, {Old, B.getInt32(47)}), B.getInt32(48), V});
    }
  }

  return V;
}

// Use the builder to create an exclusive scan and compute the final reduced
// value using an iterative approach. This provides an alternative
// implementation to DPP which uses WMM for scan computations. This API iterate
// over active lanes to read, compute and update the value using
// readlane and writelane intrinsics.
std::pair<Value *, Value *> AMDGPUAtomicOptimizerImpl::buildScanIteratively(
    IRBuilder<> &B, AtomicRMWInst::BinOp Op, Value *const Identity, Value *V,
    Instruction &I, BasicBlock *ComputeLoop, BasicBlock *ComputeEnd) const {
  auto *Ty = I.getType();
  auto *WaveTy = B.getIntNTy(ST->getWavefrontSize());
  auto *EntryBB = I.getParent();
  auto NeedResult = !I.use_empty();

  auto *Ballot =
      B.CreateIntrinsic(Intrinsic::amdgcn_ballot, WaveTy, B.getTrue());

  // Start inserting instructions for ComputeLoop block
  B.SetInsertPoint(ComputeLoop);
  // Phi nodes for Accumulator, Scan results destination, and Active Lanes
  auto *Accumulator = B.CreatePHI(Ty, 2, "Accumulator");
  Accumulator->addIncoming(Identity, EntryBB);
  PHINode *OldValuePhi = nullptr;
  if (NeedResult) {
    OldValuePhi = B.CreatePHI(Ty, 2, "OldValuePhi");
    OldValuePhi->addIncoming(PoisonValue::get(Ty), EntryBB);
  }
  auto *ActiveBits = B.CreatePHI(WaveTy, 2, "ActiveBits");
  ActiveBits->addIncoming(Ballot, EntryBB);

  // Use llvm.cttz instrinsic to find the lowest remaining active lane.
  auto *FF1 =
      B.CreateIntrinsic(Intrinsic::cttz, WaveTy, {ActiveBits, B.getTrue()});

  auto *LaneIdxInt = B.CreateTrunc(FF1, B.getInt32Ty());

  // Get the value required for atomic operation
  Value *LaneValue = B.CreateIntrinsic(V->getType(), Intrinsic::amdgcn_readlane,
                                       {V, LaneIdxInt});

  // Perform writelane if intermediate scan results are required later in the
  // kernel computations
  Value *OldValue = nullptr;
  if (NeedResult) {
    OldValue = B.CreateIntrinsic(V->getType(), Intrinsic::amdgcn_writelane,
                                 {Accumulator, LaneIdxInt, OldValuePhi});
    OldValuePhi->addIncoming(OldValue, ComputeLoop);
  }

  // Accumulate the results
  auto *NewAccumulator = buildNonAtomicBinOp(B, Op, Accumulator, LaneValue);
  Accumulator->addIncoming(NewAccumulator, ComputeLoop);

  // Set bit to zero of current active lane so that for next iteration llvm.cttz
  // return the next active lane
  auto *Mask = B.CreateShl(ConstantInt::get(WaveTy, 1), FF1);

  auto *InverseMask = B.CreateXor(Mask, ConstantInt::get(WaveTy, -1));
  auto *NewActiveBits = B.CreateAnd(ActiveBits, InverseMask);
  ActiveBits->addIncoming(NewActiveBits, ComputeLoop);

  // Branch out of the loop when all lanes are processed.
  auto *IsEnd = B.CreateICmpEQ(NewActiveBits, ConstantInt::get(WaveTy, 0));
  B.CreateCondBr(IsEnd, ComputeEnd, ComputeLoop);

  B.SetInsertPoint(ComputeEnd);

  return {OldValue, NewAccumulator};
}

static Constant *getIdentityValueForAtomicOp(Type *const Ty,
                                             AtomicRMWInst::BinOp Op) {
  LLVMContext &C = Ty->getContext();
  const unsigned BitWidth = Ty->getPrimitiveSizeInBits();
  switch (Op) {
  default:
    llvm_unreachable("Unhandled atomic op");
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::UMax:
    return ConstantInt::get(C, APInt::getMinValue(BitWidth));
  case AtomicRMWInst::And:
  case AtomicRMWInst::UMin:
    return ConstantInt::get(C, APInt::getMaxValue(BitWidth));
  case AtomicRMWInst::Max:
    return ConstantInt::get(C, APInt::getSignedMinValue(BitWidth));
  case AtomicRMWInst::Min:
    return ConstantInt::get(C, APInt::getSignedMaxValue(BitWidth));
  case AtomicRMWInst::FAdd:
    return ConstantFP::get(C, APFloat::getZero(Ty->getFltSemantics(), true));
  case AtomicRMWInst::FSub:
    return ConstantFP::get(C, APFloat::getZero(Ty->getFltSemantics(), false));
  case AtomicRMWInst::FMin:
  case AtomicRMWInst::FMax:
    // FIXME: atomicrmw fmax/fmin behave like llvm.maxnum/minnum so NaN is the
    // closest thing they have to an identity, but it still does not preserve
    // the difference between quiet and signaling NaNs or NaNs with different
    // payloads.
    return ConstantFP::get(C, APFloat::getNaN(Ty->getFltSemantics()));
  }
}

static Value *buildMul(IRBuilder<> &B, Value *LHS, Value *RHS) {
  const ConstantInt *CI = dyn_cast<ConstantInt>(LHS);
  return (CI && CI->isOne()) ? RHS : B.CreateMul(LHS, RHS);
}

void AMDGPUAtomicOptimizerImpl::optimizeAtomic(Instruction &I,
                                               AtomicRMWInst::BinOp Op,
                                               unsigned ValIdx,
                                               bool ValDivergent) const {
  // Start building just before the instruction.
  IRBuilder<> B(&I);

  if (AtomicRMWInst::isFPOperation(Op)) {
    B.setIsFPConstrained(I.getFunction()->hasFnAttribute(Attribute::StrictFP));
  }

  // If we are in a pixel shader, because of how we have to mask out helper
  // lane invocations, we need to record the entry and exit BB's.
  BasicBlock *PixelEntryBB = nullptr;
  BasicBlock *PixelExitBB = nullptr;

  // If we're optimizing an atomic within a pixel shader, we need to wrap the
  // entire atomic operation in a helper-lane check. We do not want any helper
  // lanes that are around only for the purposes of derivatives to take part
  // in any cross-lane communication, and we use a branch on whether the lane is
  // live to do this.
  if (IsPixelShader) {
    // Record I's original position as the entry block.
    PixelEntryBB = I.getParent();

    Value *const Cond = B.CreateIntrinsic(Intrinsic::amdgcn_ps_live, {}, {});
    Instruction *const NonHelperTerminator =
        SplitBlockAndInsertIfThen(Cond, &I, false, nullptr, &DTU, nullptr);

    // Record I's new position as the exit block.
    PixelExitBB = I.getParent();

    I.moveBefore(NonHelperTerminator);
    B.SetInsertPoint(&I);
  }

  Type *const Ty = I.getType();
  Type *Int32Ty = B.getInt32Ty();
  bool isAtomicFloatingPointTy = Ty->isFloatingPointTy();
  [[maybe_unused]] const unsigned TyBitWidth = DL->getTypeSizeInBits(Ty);

  // This is the value in the atomic operation we need to combine in order to
  // reduce the number of atomic operations.
  Value *V = I.getOperand(ValIdx);

  // We need to know how many lanes are active within the wavefront, and we do
  // this by doing a ballot of active lanes.
  Type *const WaveTy = B.getIntNTy(ST->getWavefrontSize());
  CallInst *const Ballot =
      B.CreateIntrinsic(Intrinsic::amdgcn_ballot, WaveTy, B.getTrue());

  // We need to know how many lanes are active within the wavefront that are
  // below us. If we counted each lane linearly starting from 0, a lane is
  // below us only if its associated index was less than ours. We do this by
  // using the mbcnt intrinsic.
  Value *Mbcnt;
  if (ST->isWave32()) {
    Mbcnt = B.CreateIntrinsic(Intrinsic::amdgcn_mbcnt_lo, {},
                              {Ballot, B.getInt32(0)});
  } else {
    Value *const ExtractLo = B.CreateTrunc(Ballot, Int32Ty);
    Value *const ExtractHi = B.CreateTrunc(B.CreateLShr(Ballot, 32), Int32Ty);
    Mbcnt = B.CreateIntrinsic(Intrinsic::amdgcn_mbcnt_lo, {},
                              {ExtractLo, B.getInt32(0)});
    Mbcnt =
        B.CreateIntrinsic(Intrinsic::amdgcn_mbcnt_hi, {}, {ExtractHi, Mbcnt});
  }

  Function *F = I.getFunction();
  LLVMContext &C = F->getContext();

  // For atomic sub, perform scan with add operation and allow one lane to
  // subtract the reduced value later.
  AtomicRMWInst::BinOp ScanOp = Op;
  if (Op == AtomicRMWInst::Sub) {
    ScanOp = AtomicRMWInst::Add;
  } else if (Op == AtomicRMWInst::FSub) {
    ScanOp = AtomicRMWInst::FAdd;
  }
  Value *Identity = getIdentityValueForAtomicOp(Ty, ScanOp);

  Value *ExclScan = nullptr;
  Value *NewV = nullptr;

  const bool NeedResult = !I.use_empty();

  BasicBlock *ComputeLoop = nullptr;
  BasicBlock *ComputeEnd = nullptr;
  // If we have a divergent value in each lane, we need to combine the value
  // using DPP.
  if (ValDivergent) {
    if (ScanImpl == ScanOptions::DPP) {
      // First we need to set all inactive invocations to the identity value, so
      // that they can correctly contribute to the final result.
      NewV =
          B.CreateIntrinsic(Intrinsic::amdgcn_set_inactive, Ty, {V, Identity});
      if (!NeedResult && ST->hasPermLaneX16()) {
        // On GFX10 the permlanex16 instruction helps us build a reduction
        // without too many readlanes and writelanes, which are generally bad
        // for performance.
        NewV = buildReduction(B, ScanOp, NewV, Identity);
      } else {
        NewV = buildScan(B, ScanOp, NewV, Identity);
        if (NeedResult)
          ExclScan = buildShiftRight(B, NewV, Identity);
        // Read the value from the last lane, which has accumulated the values
        // of each active lane in the wavefront. This will be our new value
        // which we will provide to the atomic operation.
        Value *const LastLaneIdx = B.getInt32(ST->getWavefrontSize() - 1);
        NewV = B.CreateIntrinsic(Ty, Intrinsic::amdgcn_readlane,
                                 {NewV, LastLaneIdx});
      }
      // Finally mark the readlanes in the WWM section.
      NewV = B.CreateIntrinsic(Intrinsic::amdgcn_strict_wwm, Ty, NewV);
    } else if (ScanImpl == ScanOptions::Iterative) {
      // Alternative implementation for scan
      ComputeLoop = BasicBlock::Create(C, "ComputeLoop", F);
      ComputeEnd = BasicBlock::Create(C, "ComputeEnd", F);
      std::tie(ExclScan, NewV) = buildScanIteratively(B, ScanOp, Identity, V, I,
                                                      ComputeLoop, ComputeEnd);
    } else {
      llvm_unreachable("Atomic Optimzer is disabled for None strategy");
    }
  } else {
    switch (Op) {
    default:
      llvm_unreachable("Unhandled atomic op");

    case AtomicRMWInst::Add:
    case AtomicRMWInst::Sub: {
      // The new value we will be contributing to the atomic operation is the
      // old value times the number of active lanes.
      Value *const Ctpop = B.CreateIntCast(
          B.CreateUnaryIntrinsic(Intrinsic::ctpop, Ballot), Ty, false);
      NewV = buildMul(B, V, Ctpop);
      break;
    }
    case AtomicRMWInst::FAdd:
    case AtomicRMWInst::FSub: {
      Value *const Ctpop = B.CreateIntCast(
          B.CreateUnaryIntrinsic(Intrinsic::ctpop, Ballot), Int32Ty, false);
      Value *const CtpopFP = B.CreateUIToFP(Ctpop, Ty);
      NewV = B.CreateFMul(V, CtpopFP);
      break;
    }
    case AtomicRMWInst::And:
    case AtomicRMWInst::Or:
    case AtomicRMWInst::Max:
    case AtomicRMWInst::Min:
    case AtomicRMWInst::UMax:
    case AtomicRMWInst::UMin:
    case AtomicRMWInst::FMin:
    case AtomicRMWInst::FMax:
      // These operations with a uniform value are idempotent: doing the atomic
      // operation multiple times has the same effect as doing it once.
      NewV = V;
      break;

    case AtomicRMWInst::Xor:
      // The new value we will be contributing to the atomic operation is the
      // old value times the parity of the number of active lanes.
      Value *const Ctpop = B.CreateIntCast(
          B.CreateUnaryIntrinsic(Intrinsic::ctpop, Ballot), Ty, false);
      NewV = buildMul(B, V, B.CreateAnd(Ctpop, 1));
      break;
    }
  }

  // We only want a single lane to enter our new control flow, and we do this
  // by checking if there are any active lanes below us. Only one lane will
  // have 0 active lanes below us, so that will be the only one to progress.
  Value *const Cond = B.CreateICmpEQ(Mbcnt, B.getInt32(0));

  // Store I's original basic block before we split the block.
  BasicBlock *const OriginalBB = I.getParent();

  // We need to introduce some new control flow to force a single lane to be
  // active. We do this by splitting I's basic block at I, and introducing the
  // new block such that:
  // entry --> single_lane -\
  //       \------------------> exit
  Instruction *const SingleLaneTerminator =
      SplitBlockAndInsertIfThen(Cond, &I, false, nullptr, &DTU, nullptr);

  // At this point, we have split the I's block to allow one lane in wavefront
  // to update the precomputed reduced value. Also, completed the codegen for
  // new control flow i.e. iterative loop which perform reduction and scan using
  // ComputeLoop and ComputeEnd.
  // For the new control flow, we need to move branch instruction i.e.
  // terminator created during SplitBlockAndInsertIfThen from I's block to
  // ComputeEnd block. We also need to set up predecessor to next block when
  // single lane done updating the final reduced value.
  BasicBlock *Predecessor = nullptr;
  if (ValDivergent && ScanImpl == ScanOptions::Iterative) {
    // Move terminator from I's block to ComputeEnd block.
    //
    // OriginalBB is known to have a branch as terminator because
    // SplitBlockAndInsertIfThen will have inserted one.
    BranchInst *Terminator = cast<BranchInst>(OriginalBB->getTerminator());
    B.SetInsertPoint(ComputeEnd);
    Terminator->removeFromParent();
    B.Insert(Terminator);

    // Branch to ComputeLoop Block unconditionally from the I's block for
    // iterative approach.
    B.SetInsertPoint(OriginalBB);
    B.CreateBr(ComputeLoop);

    // Update the dominator tree for new control flow.
    SmallVector<DominatorTree::UpdateType, 6> DomTreeUpdates(
        {{DominatorTree::Insert, OriginalBB, ComputeLoop},
         {DominatorTree::Insert, ComputeLoop, ComputeEnd}});

    // We're moving the terminator from EntryBB to ComputeEnd, make sure we move
    // the DT edges as well.
    for (auto *Succ : Terminator->successors()) {
      DomTreeUpdates.push_back({DominatorTree::Insert, ComputeEnd, Succ});
      DomTreeUpdates.push_back({DominatorTree::Delete, OriginalBB, Succ});
    }

    DTU.applyUpdates(DomTreeUpdates);

    Predecessor = ComputeEnd;
  } else {
    Predecessor = OriginalBB;
  }
  // Move the IR builder into single_lane next.
  B.SetInsertPoint(SingleLaneTerminator);

  // Clone the original atomic operation into single lane, replacing the
  // original value with our newly created one.
  Instruction *const NewI = I.clone();
  B.Insert(NewI);
  NewI->setOperand(ValIdx, NewV);

  // Move the IR builder into exit next, and start inserting just before the
  // original instruction.
  B.SetInsertPoint(&I);

  if (NeedResult) {
    // Create a PHI node to get our new atomic result into the exit block.
    PHINode *const PHI = B.CreatePHI(Ty, 2);
    PHI->addIncoming(PoisonValue::get(Ty), Predecessor);
    PHI->addIncoming(NewI, SingleLaneTerminator->getParent());

    // We need to broadcast the value who was the lowest active lane (the first
    // lane) to all other lanes in the wavefront. We use an intrinsic for this,
    // but have to handle 64-bit broadcasts with two calls to this intrinsic.
    Value *BroadcastI = nullptr;
    BroadcastI = B.CreateIntrinsic(Ty, Intrinsic::amdgcn_readfirstlane, PHI);

    // Now that we have the result of our single atomic operation, we need to
    // get our individual lane's slice into the result. We use the lane offset
    // we previously calculated combined with the atomic result value we got
    // from the first lane, to get our lane's index into the atomic result.
    Value *LaneOffset = nullptr;
    if (ValDivergent) {
      if (ScanImpl == ScanOptions::DPP) {
        LaneOffset =
            B.CreateIntrinsic(Intrinsic::amdgcn_strict_wwm, Ty, ExclScan);
      } else if (ScanImpl == ScanOptions::Iterative) {
        LaneOffset = ExclScan;
      } else {
        llvm_unreachable("Atomic Optimzer is disabled for None strategy");
      }
    } else {
      Mbcnt = isAtomicFloatingPointTy ? B.CreateUIToFP(Mbcnt, Ty)
                                      : B.CreateIntCast(Mbcnt, Ty, false);
      switch (Op) {
      default:
        llvm_unreachable("Unhandled atomic op");
      case AtomicRMWInst::Add:
      case AtomicRMWInst::Sub:
        LaneOffset = buildMul(B, V, Mbcnt);
        break;
      case AtomicRMWInst::And:
      case AtomicRMWInst::Or:
      case AtomicRMWInst::Max:
      case AtomicRMWInst::Min:
      case AtomicRMWInst::UMax:
      case AtomicRMWInst::UMin:
      case AtomicRMWInst::FMin:
      case AtomicRMWInst::FMax:
        LaneOffset = B.CreateSelect(Cond, Identity, V);
        break;
      case AtomicRMWInst::Xor:
        LaneOffset = buildMul(B, V, B.CreateAnd(Mbcnt, 1));
        break;
      case AtomicRMWInst::FAdd:
      case AtomicRMWInst::FSub: {
        LaneOffset = B.CreateFMul(V, Mbcnt);
        break;
      }
      }
    }
    Value *Result = buildNonAtomicBinOp(B, Op, BroadcastI, LaneOffset);
    if (isAtomicFloatingPointTy) {
      // For fadd/fsub the first active lane of LaneOffset should be the
      // identity (-0.0 for fadd or +0.0 for fsub) but the value we calculated
      // is V * +0.0 which might have the wrong sign or might be nan (if V is
      // inf or nan).
      //
      // For all floating point ops if the in-memory value was a nan then the
      // binop we just built might have quieted it or changed its payload.
      //
      // Correct all these problems by using BroadcastI as the result in the
      // first active lane.
      Result = B.CreateSelect(Cond, BroadcastI, Result);
    }

    if (IsPixelShader) {
      // Need a final PHI to reconverge to above the helper lane branch mask.
      B.SetInsertPoint(PixelExitBB, PixelExitBB->getFirstNonPHIIt());

      PHINode *const PHI = B.CreatePHI(Ty, 2);
      PHI->addIncoming(PoisonValue::get(Ty), PixelEntryBB);
      PHI->addIncoming(Result, I.getParent());
      I.replaceAllUsesWith(PHI);
    } else {
      // Replace the original atomic instruction with the new one.
      I.replaceAllUsesWith(Result);
    }
  }

  // And delete the original.
  I.eraseFromParent();
}

INITIALIZE_PASS_BEGIN(AMDGPUAtomicOptimizer, DEBUG_TYPE,
                      "AMDGPU atomic optimizations", false, false)
INITIALIZE_PASS_DEPENDENCY(UniformityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(AMDGPUAtomicOptimizer, DEBUG_TYPE,
                    "AMDGPU atomic optimizations", false, false)

FunctionPass *llvm::createAMDGPUAtomicOptimizerPass(ScanOptions ScanStrategy) {
  return new AMDGPUAtomicOptimizer(ScanStrategy);
}
