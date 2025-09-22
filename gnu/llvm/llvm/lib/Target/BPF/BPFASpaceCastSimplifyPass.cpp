//===-- BPFASpaceCastSimplifyPass.cpp - BPF addrspacecast simplications --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include <optional>

#define DEBUG_TYPE "bpf-aspace-simplify"

using namespace llvm;

namespace {

struct CastGEPCast {
  AddrSpaceCastInst *OuterCast;

  // Match chain of instructions:
  //   %inner = addrspacecast N->M
  //   %gep   = getelementptr %inner, ...
  //   %outer = addrspacecast M->N %gep
  // Where I is %outer.
  static std::optional<CastGEPCast> match(Value *I) {
    auto *OuterCast = dyn_cast<AddrSpaceCastInst>(I);
    if (!OuterCast)
      return std::nullopt;
    auto *GEP = dyn_cast<GetElementPtrInst>(OuterCast->getPointerOperand());
    if (!GEP)
      return std::nullopt;
    auto *InnerCast = dyn_cast<AddrSpaceCastInst>(GEP->getPointerOperand());
    if (!InnerCast)
      return std::nullopt;
    if (InnerCast->getSrcAddressSpace() != OuterCast->getDestAddressSpace())
      return std::nullopt;
    if (InnerCast->getDestAddressSpace() != OuterCast->getSrcAddressSpace())
      return std::nullopt;
    return CastGEPCast{OuterCast};
  }

  static PointerType *changeAddressSpace(PointerType *Ty, unsigned AS) {
    return Ty->get(Ty->getContext(), AS);
  }

  // Assuming match(this->OuterCast) is true, convert:
  //   (addrspacecast M->N (getelementptr (addrspacecast N->M ptr) ...))
  // To:
  //   (getelementptr ptr ...)
  GetElementPtrInst *rewrite() {
    auto *GEP = cast<GetElementPtrInst>(OuterCast->getPointerOperand());
    auto *InnerCast = cast<AddrSpaceCastInst>(GEP->getPointerOperand());
    unsigned AS = OuterCast->getDestAddressSpace();
    auto *NewGEP = cast<GetElementPtrInst>(GEP->clone());
    NewGEP->setName(GEP->getName());
    NewGEP->insertAfter(OuterCast);
    NewGEP->setOperand(0, InnerCast->getPointerOperand());
    auto *GEPTy = cast<PointerType>(GEP->getType());
    NewGEP->mutateType(changeAddressSpace(GEPTy, AS));
    OuterCast->replaceAllUsesWith(NewGEP);
    OuterCast->eraseFromParent();
    if (GEP->use_empty())
      GEP->eraseFromParent();
    if (InnerCast->use_empty())
      InnerCast->eraseFromParent();
    return NewGEP;
  }
};

} // anonymous namespace

PreservedAnalyses BPFASpaceCastSimplifyPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  SmallVector<CastGEPCast, 16> WorkList;
  bool Changed = false;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB)
      if (auto It = CastGEPCast::match(&I))
        WorkList.push_back(It.value());
    Changed |= !WorkList.empty();

    while (!WorkList.empty()) {
      CastGEPCast InsnChain = WorkList.pop_back_val();
      GetElementPtrInst *NewGEP = InsnChain.rewrite();
      for (User *U : NewGEP->users())
        if (auto It = CastGEPCast::match(U))
          WorkList.push_back(It.value());
    }
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
