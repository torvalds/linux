//=== WebAssemblyRefTypeMem2Local.cpp - WebAssembly RefType Mem2Local -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Assign reference type allocas to local addrspace (addrspace(1)) so that
/// their loads and stores can be lowered to local.gets/local.sets.
///
//===----------------------------------------------------------------------===//

#include "Utils/WasmAddressSpaces.h"
#include "Utils/WebAssemblyTypeUtilities.h"
#include "WebAssembly.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-ref-type-mem2local"

namespace {
class WebAssemblyRefTypeMem2Local final
    : public FunctionPass,
      public InstVisitor<WebAssemblyRefTypeMem2Local> {
  StringRef getPassName() const override {
    return "WebAssembly Reference Types Memory to Local";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnFunction(Function &F) override;
  bool Changed = false;

public:
  static char ID;
  WebAssemblyRefTypeMem2Local() : FunctionPass(ID) {}

  void visitAllocaInst(AllocaInst &AI);
};
} // End anonymous namespace

char WebAssemblyRefTypeMem2Local::ID = 0;
INITIALIZE_PASS(WebAssemblyRefTypeMem2Local, DEBUG_TYPE,
                "Assign reference type allocas to local address space", true,
                false)

FunctionPass *llvm::createWebAssemblyRefTypeMem2Local() {
  return new WebAssemblyRefTypeMem2Local();
}

void WebAssemblyRefTypeMem2Local::visitAllocaInst(AllocaInst &AI) {
  if (WebAssembly::isWebAssemblyReferenceType(AI.getAllocatedType())) {
    Changed = true;
    IRBuilder<> IRB(AI.getContext());
    IRB.SetInsertPoint(&AI);
    auto *NewAI = IRB.CreateAlloca(AI.getAllocatedType(),
                                   WebAssembly::WASM_ADDRESS_SPACE_VAR, nullptr,
                                   AI.getName() + ".var");

    // The below is basically equivalent to AI.replaceAllUsesWith(NewAI), but we
    // cannot use it because it requires the old and new types be the same,
    // which is not true here because the address spaces are different.
    if (AI.hasValueHandle())
      ValueHandleBase::ValueIsRAUWd(&AI, NewAI);
    if (AI.isUsedByMetadata())
      ValueAsMetadata::handleRAUW(&AI, NewAI);
    while (!AI.materialized_use_empty()) {
      Use &U = *AI.materialized_use_begin();
      U.set(NewAI);
    }

    AI.eraseFromParent();
  }
}

bool WebAssemblyRefTypeMem2Local::runOnFunction(Function &F) {
  LLVM_DEBUG(dbgs() << "********** WebAssembly RefType Mem2Local **********\n"
                       "********** Function: "
                    << F.getName() << '\n');

  if (F.getFnAttribute("target-features")
          .getValueAsString()
          .contains("+reference-types"))
    visit(F);
  return Changed;
}
