//===-- AMDGPULowerKernelAttributes.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This pass does attempts to make use of reqd_work_group_size metadata
/// to eliminate loads from the dispatch packet and to constant fold OpenCL
/// get_local_size-like functions.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"

#define DEBUG_TYPE "amdgpu-lower-kernel-attributes"

using namespace llvm;

namespace {

// Field offsets in hsa_kernel_dispatch_packet_t.
enum DispatchPackedOffsets {
  WORKGROUP_SIZE_X = 4,
  WORKGROUP_SIZE_Y = 6,
  WORKGROUP_SIZE_Z = 8,

  GRID_SIZE_X = 12,
  GRID_SIZE_Y = 16,
  GRID_SIZE_Z = 20
};

// Field offsets to implicit kernel argument pointer.
enum ImplicitArgOffsets {
  HIDDEN_BLOCK_COUNT_X = 0,
  HIDDEN_BLOCK_COUNT_Y = 4,
  HIDDEN_BLOCK_COUNT_Z = 8,

  HIDDEN_GROUP_SIZE_X = 12,
  HIDDEN_GROUP_SIZE_Y = 14,
  HIDDEN_GROUP_SIZE_Z = 16,

  HIDDEN_REMAINDER_X = 18,
  HIDDEN_REMAINDER_Y = 20,
  HIDDEN_REMAINDER_Z = 22,
};

class AMDGPULowerKernelAttributes : public ModulePass {
public:
  static char ID;

  AMDGPULowerKernelAttributes() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  StringRef getPassName() const override {
    return "AMDGPU Kernel Attributes";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
 }
};

Function *getBasePtrIntrinsic(Module &M, bool IsV5OrAbove) {
  auto IntrinsicId = IsV5OrAbove ? Intrinsic::amdgcn_implicitarg_ptr
                                 : Intrinsic::amdgcn_dispatch_ptr;
  StringRef Name = Intrinsic::getName(IntrinsicId);
  return M.getFunction(Name);
}

} // end anonymous namespace

static bool processUse(CallInst *CI, bool IsV5OrAbove) {
  Function *F = CI->getParent()->getParent();

  auto MD = F->getMetadata("reqd_work_group_size");
  const bool HasReqdWorkGroupSize = MD && MD->getNumOperands() == 3;

  const bool HasUniformWorkGroupSize =
    F->getFnAttribute("uniform-work-group-size").getValueAsBool();

  if (!HasReqdWorkGroupSize && !HasUniformWorkGroupSize)
    return false;

  Value *BlockCounts[3] = {nullptr, nullptr, nullptr};
  Value *GroupSizes[3]  = {nullptr, nullptr, nullptr};
  Value *Remainders[3]  = {nullptr, nullptr, nullptr};
  Value *GridSizes[3]   = {nullptr, nullptr, nullptr};

  const DataLayout &DL = F->getDataLayout();

  // We expect to see several GEP users, casted to the appropriate type and
  // loaded.
  for (User *U : CI->users()) {
    if (!U->hasOneUse())
      continue;

    int64_t Offset = 0;
    auto *Load = dyn_cast<LoadInst>(U); // Load from ImplicitArgPtr/DispatchPtr?
    auto *BCI = dyn_cast<BitCastInst>(U);
    if (!Load && !BCI) {
      if (GetPointerBaseWithConstantOffset(U, Offset, DL) != CI)
        continue;
      Load = dyn_cast<LoadInst>(*U->user_begin()); // Load from GEP?
      BCI = dyn_cast<BitCastInst>(*U->user_begin());
    }

    if (BCI) {
      if (!BCI->hasOneUse())
        continue;
      Load = dyn_cast<LoadInst>(*BCI->user_begin()); // Load from BCI?
    }

    if (!Load || !Load->isSimple())
      continue;

    unsigned LoadSize = DL.getTypeStoreSize(Load->getType());

    // TODO: Handle merged loads.
    if (IsV5OrAbove) { // Base is ImplicitArgPtr.
      switch (Offset) {
      case HIDDEN_BLOCK_COUNT_X:
        if (LoadSize == 4)
          BlockCounts[0] = Load;
        break;
      case HIDDEN_BLOCK_COUNT_Y:
        if (LoadSize == 4)
          BlockCounts[1] = Load;
        break;
      case HIDDEN_BLOCK_COUNT_Z:
        if (LoadSize == 4)
          BlockCounts[2] = Load;
        break;
      case HIDDEN_GROUP_SIZE_X:
        if (LoadSize == 2)
          GroupSizes[0] = Load;
        break;
      case HIDDEN_GROUP_SIZE_Y:
        if (LoadSize == 2)
          GroupSizes[1] = Load;
        break;
      case HIDDEN_GROUP_SIZE_Z:
        if (LoadSize == 2)
          GroupSizes[2] = Load;
        break;
      case HIDDEN_REMAINDER_X:
        if (LoadSize == 2)
          Remainders[0] = Load;
        break;
      case HIDDEN_REMAINDER_Y:
        if (LoadSize == 2)
          Remainders[1] = Load;
        break;
      case HIDDEN_REMAINDER_Z:
        if (LoadSize == 2)
          Remainders[2] = Load;
        break;
      default:
        break;
      }
    } else { // Base is DispatchPtr.
      switch (Offset) {
      case WORKGROUP_SIZE_X:
        if (LoadSize == 2)
          GroupSizes[0] = Load;
        break;
      case WORKGROUP_SIZE_Y:
        if (LoadSize == 2)
          GroupSizes[1] = Load;
        break;
      case WORKGROUP_SIZE_Z:
        if (LoadSize == 2)
          GroupSizes[2] = Load;
        break;
      case GRID_SIZE_X:
        if (LoadSize == 4)
          GridSizes[0] = Load;
        break;
      case GRID_SIZE_Y:
        if (LoadSize == 4)
          GridSizes[1] = Load;
        break;
      case GRID_SIZE_Z:
        if (LoadSize == 4)
          GridSizes[2] = Load;
        break;
      default:
        break;
      }
    }
  }

  bool MadeChange = false;
  if (IsV5OrAbove && HasUniformWorkGroupSize) {
    // Under v5  __ockl_get_local_size returns the value computed by the expression:
    //
    //   workgroup_id < hidden_block_count ? hidden_group_size : hidden_remainder
    //
    // For functions with the attribute uniform-work-group-size=true. we can evaluate
    // workgroup_id < hidden_block_count as true, and thus hidden_group_size is returned
    // for __ockl_get_local_size.
    for (int I = 0; I < 3; ++I) {
      Value *BlockCount = BlockCounts[I];
      if (!BlockCount)
        continue;

      using namespace llvm::PatternMatch;
      auto GroupIDIntrin =
          I == 0 ? m_Intrinsic<Intrinsic::amdgcn_workgroup_id_x>()
                 : (I == 1 ? m_Intrinsic<Intrinsic::amdgcn_workgroup_id_y>()
                           : m_Intrinsic<Intrinsic::amdgcn_workgroup_id_z>());

      for (User *ICmp : BlockCount->users()) {
        ICmpInst::Predicate Pred;
        if (match(ICmp, m_ICmp(Pred, GroupIDIntrin, m_Specific(BlockCount)))) {
          if (Pred != ICmpInst::ICMP_ULT)
            continue;
          ICmp->replaceAllUsesWith(llvm::ConstantInt::getTrue(ICmp->getType()));
          MadeChange = true;
        }
      }
    }

    // All remainders should be 0 with uniform work group size.
    for (Value *Remainder : Remainders) {
      if (!Remainder)
        continue;
      Remainder->replaceAllUsesWith(Constant::getNullValue(Remainder->getType()));
      MadeChange = true;
    }
  } else if (HasUniformWorkGroupSize) { // Pre-V5.
    // Pattern match the code used to handle partial workgroup dispatches in the
    // library implementation of get_local_size, so the entire function can be
    // constant folded with a known group size.
    //
    // uint r = grid_size - group_id * group_size;
    // get_local_size = (r < group_size) ? r : group_size;
    //
    // If we have uniform-work-group-size (which is the default in OpenCL 1.2),
    // the grid_size is required to be a multiple of group_size). In this case:
    //
    // grid_size - (group_id * group_size) < group_size
    // ->
    // grid_size < group_size + (group_id * group_size)
    //
    // (grid_size / group_size) < 1 + group_id
    //
    // grid_size / group_size is at least 1, so we can conclude the select
    // condition is false (except for group_id == 0, where the select result is
    // the same).
    for (int I = 0; I < 3; ++I) {
      Value *GroupSize = GroupSizes[I];
      Value *GridSize = GridSizes[I];
      if (!GroupSize || !GridSize)
        continue;

      using namespace llvm::PatternMatch;
      auto GroupIDIntrin =
          I == 0 ? m_Intrinsic<Intrinsic::amdgcn_workgroup_id_x>()
                 : (I == 1 ? m_Intrinsic<Intrinsic::amdgcn_workgroup_id_y>()
                           : m_Intrinsic<Intrinsic::amdgcn_workgroup_id_z>());

      for (User *U : GroupSize->users()) {
        auto *ZextGroupSize = dyn_cast<ZExtInst>(U);
        if (!ZextGroupSize)
          continue;

        for (User *UMin : ZextGroupSize->users()) {
          if (match(UMin,
                    m_UMin(m_Sub(m_Specific(GridSize),
                                 m_Mul(GroupIDIntrin, m_Specific(ZextGroupSize))),
                           m_Specific(ZextGroupSize)))) {
            if (HasReqdWorkGroupSize) {
              ConstantInt *KnownSize
                = mdconst::extract<ConstantInt>(MD->getOperand(I));
              UMin->replaceAllUsesWith(ConstantFoldIntegerCast(
                  KnownSize, UMin->getType(), false, DL));
            } else {
              UMin->replaceAllUsesWith(ZextGroupSize);
            }

            MadeChange = true;
          }
        }
      }
    }
  }

  // If reqd_work_group_size is set, we can replace work group size with it.
  if (!HasReqdWorkGroupSize)
    return MadeChange;

  for (int I = 0; I < 3; I++) {
    Value *GroupSize = GroupSizes[I];
    if (!GroupSize)
      continue;

    ConstantInt *KnownSize = mdconst::extract<ConstantInt>(MD->getOperand(I));
    GroupSize->replaceAllUsesWith(
        ConstantFoldIntegerCast(KnownSize, GroupSize->getType(), false, DL));
    MadeChange = true;
  }

  return MadeChange;
}


// TODO: Move makeLIDRangeMetadata usage into here. Seem to not get
// TargetPassConfig for subtarget.
bool AMDGPULowerKernelAttributes::runOnModule(Module &M) {
  bool MadeChange = false;
  bool IsV5OrAbove =
      AMDGPU::getAMDHSACodeObjectVersion(M) >= AMDGPU::AMDHSA_COV5;
  Function *BasePtr = getBasePtrIntrinsic(M, IsV5OrAbove);

  if (!BasePtr) // ImplicitArgPtr/DispatchPtr not used.
    return false;

  SmallPtrSet<Instruction *, 4> HandledUses;
  for (auto *U : BasePtr->users()) {
    CallInst *CI = cast<CallInst>(U);
    if (HandledUses.insert(CI).second) {
      if (processUse(CI, IsV5OrAbove))
        MadeChange = true;
    }
  }

  return MadeChange;
}


INITIALIZE_PASS_BEGIN(AMDGPULowerKernelAttributes, DEBUG_TYPE,
                      "AMDGPU Kernel Attributes", false, false)
INITIALIZE_PASS_END(AMDGPULowerKernelAttributes, DEBUG_TYPE,
                    "AMDGPU Kernel Attributes", false, false)

char AMDGPULowerKernelAttributes::ID = 0;

ModulePass *llvm::createAMDGPULowerKernelAttributesPass() {
  return new AMDGPULowerKernelAttributes();
}

PreservedAnalyses
AMDGPULowerKernelAttributesPass::run(Function &F, FunctionAnalysisManager &AM) {
  bool IsV5OrAbove =
      AMDGPU::getAMDHSACodeObjectVersion(*F.getParent()) >= AMDGPU::AMDHSA_COV5;
  Function *BasePtr = getBasePtrIntrinsic(*F.getParent(), IsV5OrAbove);

  if (!BasePtr) // ImplicitArgPtr/DispatchPtr not used.
    return PreservedAnalyses::all();

  for (Instruction &I : instructions(F)) {
    if (CallInst *CI = dyn_cast<CallInst>(&I)) {
      if (CI->getCalledFunction() == BasePtr)
        processUse(CI, IsV5OrAbove);
    }
  }

  return PreservedAnalyses::all();
}
