//===-- AMDGPUMemoryUtils.cpp - -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMemoryUtils.h"
#include "AMDGPU.h"
#include "AMDGPUBaseInfo.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ReplaceConstant.h"

#define DEBUG_TYPE "amdgpu-memory-utils"

using namespace llvm;

namespace llvm::AMDGPU {

Align getAlign(const DataLayout &DL, const GlobalVariable *GV) {
  return DL.getValueOrABITypeAlignment(GV->getPointerAlignment(DL),
                                       GV->getValueType());
}

bool isDynamicLDS(const GlobalVariable &GV) {
  // external zero size addrspace(3) without initializer is dynlds.
  const Module *M = GV.getParent();
  const DataLayout &DL = M->getDataLayout();
  if (GV.getType()->getPointerAddressSpace() != AMDGPUAS::LOCAL_ADDRESS)
    return false;
  return DL.getTypeAllocSize(GV.getValueType()) == 0;
}

bool isLDSVariableToLower(const GlobalVariable &GV) {
  if (GV.getType()->getPointerAddressSpace() != AMDGPUAS::LOCAL_ADDRESS) {
    return false;
  }
  if (isDynamicLDS(GV)) {
    return true;
  }
  if (GV.isConstant()) {
    // A constant undef variable can't be written to, and any load is
    // undef, so it should be eliminated by the optimizer. It could be
    // dropped by the back end if not. This pass skips over it.
    return false;
  }
  if (GV.hasInitializer() && !isa<UndefValue>(GV.getInitializer())) {
    // Initializers are unimplemented for LDS address space.
    // Leave such variables in place for consistent error reporting.
    return false;
  }
  return true;
}

bool eliminateConstantExprUsesOfLDSFromAllInstructions(Module &M) {
  // Constants are uniqued within LLVM. A ConstantExpr referring to a LDS
  // global may have uses from multiple different functions as a result.
  // This pass specialises LDS variables with respect to the kernel that
  // allocates them.

  // This is semantically equivalent to (the unimplemented as slow):
  // for (auto &F : M.functions())
  //   for (auto &BB : F)
  //     for (auto &I : BB)
  //       for (Use &Op : I.operands())
  //         if (constantExprUsesLDS(Op))
  //           replaceConstantExprInFunction(I, Op);

  SmallVector<Constant *> LDSGlobals;
  for (auto &GV : M.globals())
    if (AMDGPU::isLDSVariableToLower(GV))
      LDSGlobals.push_back(&GV);
  return convertUsersOfConstantsToInstructions(LDSGlobals);
}

void getUsesOfLDSByFunction(const CallGraph &CG, Module &M,
                            FunctionVariableMap &kernels,
                            FunctionVariableMap &Functions) {
  // Get uses from the current function, excluding uses by called Functions
  // Two output variables to avoid walking the globals list twice
  for (auto &GV : M.globals()) {
    if (!AMDGPU::isLDSVariableToLower(GV))
      continue;
    for (User *V : GV.users()) {
      if (auto *I = dyn_cast<Instruction>(V)) {
        Function *F = I->getFunction();
        if (isKernelLDS(F))
          kernels[F].insert(&GV);
        else
          Functions[F].insert(&GV);
      }
    }
  }
}

bool isKernelLDS(const Function *F) {
  // Some weirdness here. AMDGPU::isKernelCC does not call into
  // AMDGPU::isKernel with the calling conv, it instead calls into
  // isModuleEntryFunction which returns true for more calling conventions
  // than AMDGPU::isKernel does. There's a FIXME on AMDGPU::isKernel.
  // There's also a test that checks that the LDS lowering does not hit on
  // a graphics shader, denoted amdgpu_ps, so stay with the limited case.
  // Putting LDS in the name of the function to draw attention to this.
  return AMDGPU::isKernel(F->getCallingConv());
}

LDSUsesInfoTy getTransitiveUsesOfLDS(const CallGraph &CG, Module &M) {

  FunctionVariableMap DirectMapKernel;
  FunctionVariableMap DirectMapFunction;
  getUsesOfLDSByFunction(CG, M, DirectMapKernel, DirectMapFunction);

  // Collect variables that are used by functions whose address has escaped
  DenseSet<GlobalVariable *> VariablesReachableThroughFunctionPointer;
  for (Function &F : M.functions()) {
    if (!isKernelLDS(&F))
      if (F.hasAddressTaken(nullptr,
                            /* IgnoreCallbackUses */ false,
                            /* IgnoreAssumeLikeCalls */ false,
                            /* IgnoreLLVMUsed */ true,
                            /* IgnoreArcAttachedCall */ false)) {
        set_union(VariablesReachableThroughFunctionPointer,
                  DirectMapFunction[&F]);
      }
  }

  auto FunctionMakesUnknownCall = [&](const Function *F) -> bool {
    assert(!F->isDeclaration());
    for (const CallGraphNode::CallRecord &R : *CG[F]) {
      if (!R.second->getFunction())
        return true;
    }
    return false;
  };

  // Work out which variables are reachable through function calls
  FunctionVariableMap TransitiveMapFunction = DirectMapFunction;

  // If the function makes any unknown call, assume the worst case that it can
  // access all variables accessed by functions whose address escaped
  for (Function &F : M.functions()) {
    if (!F.isDeclaration() && FunctionMakesUnknownCall(&F)) {
      if (!isKernelLDS(&F)) {
        set_union(TransitiveMapFunction[&F],
                  VariablesReachableThroughFunctionPointer);
      }
    }
  }

  // Direct implementation of collecting all variables reachable from each
  // function
  for (Function &Func : M.functions()) {
    if (Func.isDeclaration() || isKernelLDS(&Func))
      continue;

    DenseSet<Function *> seen; // catches cycles
    SmallVector<Function *, 4> wip = {&Func};

    while (!wip.empty()) {
      Function *F = wip.pop_back_val();

      // Can accelerate this by referring to transitive map for functions that
      // have already been computed, with more care than this
      set_union(TransitiveMapFunction[&Func], DirectMapFunction[F]);

      for (const CallGraphNode::CallRecord &R : *CG[F]) {
        Function *Ith = R.second->getFunction();
        if (Ith) {
          if (!seen.contains(Ith)) {
            seen.insert(Ith);
            wip.push_back(Ith);
          }
        }
      }
    }
  }

  // DirectMapKernel lists which variables are used by the kernel
  // find the variables which are used through a function call
  FunctionVariableMap IndirectMapKernel;

  for (Function &Func : M.functions()) {
    if (Func.isDeclaration() || !isKernelLDS(&Func))
      continue;

    for (const CallGraphNode::CallRecord &R : *CG[&Func]) {
      Function *Ith = R.second->getFunction();
      if (Ith) {
        set_union(IndirectMapKernel[&Func], TransitiveMapFunction[Ith]);
      } else {
        set_union(IndirectMapKernel[&Func],
                  VariablesReachableThroughFunctionPointer);
      }
    }
  }

  // Verify that we fall into one of 2 cases:
  //    - All variables are either absolute 
  //      or direct mapped dynamic LDS that is not lowered.
  //      this is a re-run of the pass
  //      so we don't have anything to do.
  //    - No variables are absolute.
  std::optional<bool> HasAbsoluteGVs;
  for (auto &Map : {DirectMapKernel, IndirectMapKernel}) {
    for (auto &[Fn, GVs] : Map) {
      for (auto *GV : GVs) {
        bool IsAbsolute = GV->isAbsoluteSymbolRef();
        bool IsDirectMapDynLDSGV = AMDGPU::isDynamicLDS(*GV) && DirectMapKernel.contains(Fn);
        if (IsDirectMapDynLDSGV)
          continue;
        if (HasAbsoluteGVs.has_value()) {
          if (*HasAbsoluteGVs != IsAbsolute) {
            report_fatal_error(
                "Module cannot mix absolute and non-absolute LDS GVs");
          }
        } else
          HasAbsoluteGVs = IsAbsolute;
      }
    }
  }

  // If we only had absolute GVs, we have nothing to do, return an empty
  // result.
  if (HasAbsoluteGVs && *HasAbsoluteGVs)
    return {FunctionVariableMap(), FunctionVariableMap()};

  return {std::move(DirectMapKernel), std::move(IndirectMapKernel)};
}

void removeFnAttrFromReachable(CallGraph &CG, Function *KernelRoot,
                               ArrayRef<StringRef> FnAttrs) {
  for (StringRef Attr : FnAttrs)
    KernelRoot->removeFnAttr(Attr);

  SmallVector<Function *> WorkList = {CG[KernelRoot]->getFunction()};
  SmallPtrSet<Function *, 8> Visited;
  bool SeenUnknownCall = false;

  while (!WorkList.empty()) {
    Function *F = WorkList.pop_back_val();

    for (auto &CallRecord : *CG[F]) {
      if (!CallRecord.second)
        continue;

      Function *Callee = CallRecord.second->getFunction();
      if (!Callee) {
        if (!SeenUnknownCall) {
          SeenUnknownCall = true;

          // If we see any indirect calls, assume nothing about potential
          // targets.
          // TODO: This could be refined to possible LDS global users.
          for (auto &ExternalCallRecord : *CG.getExternalCallingNode()) {
            Function *PotentialCallee =
                ExternalCallRecord.second->getFunction();
            assert(PotentialCallee);
            if (!isKernelLDS(PotentialCallee)) {
              for (StringRef Attr : FnAttrs)
                PotentialCallee->removeFnAttr(Attr);
            }
          }
        }
      } else {
        for (StringRef Attr : FnAttrs)
          Callee->removeFnAttr(Attr);
        if (Visited.insert(Callee).second)
          WorkList.push_back(Callee);
      }
    }
  }
}

bool isReallyAClobber(const Value *Ptr, MemoryDef *Def, AAResults *AA) {
  Instruction *DefInst = Def->getMemoryInst();

  if (isa<FenceInst>(DefInst))
    return false;

  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(DefInst)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::amdgcn_s_barrier:
    case Intrinsic::amdgcn_s_barrier_signal:
    case Intrinsic::amdgcn_s_barrier_signal_var:
    case Intrinsic::amdgcn_s_barrier_signal_isfirst:
    case Intrinsic::amdgcn_s_barrier_signal_isfirst_var:
    case Intrinsic::amdgcn_s_barrier_init:
    case Intrinsic::amdgcn_s_barrier_join:
    case Intrinsic::amdgcn_s_barrier_wait:
    case Intrinsic::amdgcn_s_barrier_leave:
    case Intrinsic::amdgcn_s_get_barrier_state:
    case Intrinsic::amdgcn_s_wakeup_barrier:
    case Intrinsic::amdgcn_wave_barrier:
    case Intrinsic::amdgcn_sched_barrier:
    case Intrinsic::amdgcn_sched_group_barrier:
      return false;
    default:
      break;
    }
  }

  // Ignore atomics not aliasing with the original load, any atomic is a
  // universal MemoryDef from MSSA's point of view too, just like a fence.
  const auto checkNoAlias = [AA, Ptr](auto I) -> bool {
    return I && AA->isNoAlias(I->getPointerOperand(), Ptr);
  };

  if (checkNoAlias(dyn_cast<AtomicCmpXchgInst>(DefInst)) ||
      checkNoAlias(dyn_cast<AtomicRMWInst>(DefInst)))
    return false;

  return true;
}

bool isClobberedInFunction(const LoadInst *Load, MemorySSA *MSSA,
                           AAResults *AA) {
  MemorySSAWalker *Walker = MSSA->getWalker();
  SmallVector<MemoryAccess *> WorkList{Walker->getClobberingMemoryAccess(Load)};
  SmallSet<MemoryAccess *, 8> Visited;
  MemoryLocation Loc(MemoryLocation::get(Load));

  LLVM_DEBUG(dbgs() << "Checking clobbering of: " << *Load << '\n');

  // Start with a nearest dominating clobbering access, it will be either
  // live on entry (nothing to do, load is not clobbered), MemoryDef, or
  // MemoryPhi if several MemoryDefs can define this memory state. In that
  // case add all Defs to WorkList and continue going up and checking all
  // the definitions of this memory location until the root. When all the
  // defs are exhausted and came to the entry state we have no clobber.
  // Along the scan ignore barriers and fences which are considered clobbers
  // by the MemorySSA, but not really writing anything into the memory.
  while (!WorkList.empty()) {
    MemoryAccess *MA = WorkList.pop_back_val();
    if (!Visited.insert(MA).second)
      continue;

    if (MSSA->isLiveOnEntryDef(MA))
      continue;

    if (MemoryDef *Def = dyn_cast<MemoryDef>(MA)) {
      LLVM_DEBUG(dbgs() << "  Def: " << *Def->getMemoryInst() << '\n');

      if (isReallyAClobber(Load->getPointerOperand(), Def, AA)) {
        LLVM_DEBUG(dbgs() << "      -> load is clobbered\n");
        return true;
      }

      WorkList.push_back(
          Walker->getClobberingMemoryAccess(Def->getDefiningAccess(), Loc));
      continue;
    }

    const MemoryPhi *Phi = cast<MemoryPhi>(MA);
    for (const auto &Use : Phi->incoming_values())
      WorkList.push_back(cast<MemoryAccess>(&Use));
  }

  LLVM_DEBUG(dbgs() << "      -> no clobber\n");
  return false;
}

} // end namespace llvm::AMDGPU
