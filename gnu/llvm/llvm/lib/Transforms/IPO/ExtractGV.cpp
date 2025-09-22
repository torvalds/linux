//===-- ExtractGV.cpp - Global Value extraction pass ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass extracts global values
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ExtractGV.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include <algorithm>

using namespace llvm;

/// Make sure GV is visible from both modules. Delete is true if it is
/// being deleted from this module.
/// This also makes sure GV cannot be dropped so that references from
/// the split module remain valid.
static void makeVisible(GlobalValue &GV, bool Delete) {
  bool Local = GV.hasLocalLinkage();
  if (Local || Delete) {
    GV.setLinkage(GlobalValue::ExternalLinkage);
    if (Local)
      GV.setVisibility(GlobalValue::HiddenVisibility);
    return;
  }

  if (!GV.hasLinkOnceLinkage()) {
    assert(!GV.isDiscardableIfUnused());
    return;
  }

  // Map linkonce* to weak* so that llvm doesn't drop this GV.
  switch (GV.getLinkage()) {
  default:
    llvm_unreachable("Unexpected linkage");
  case GlobalValue::LinkOnceAnyLinkage:
    GV.setLinkage(GlobalValue::WeakAnyLinkage);
    return;
  case GlobalValue::LinkOnceODRLinkage:
    GV.setLinkage(GlobalValue::WeakODRLinkage);
    return;
  }
}

/// If deleteS is true, this pass deletes the specified global values.
/// Otherwise, it deletes as much of the module as possible, except for the
/// global values specified.
ExtractGVPass::ExtractGVPass(std::vector<GlobalValue *> &GVs, bool deleteS,
                             bool keepConstInit)
    : Named(GVs.begin(), GVs.end()), deleteStuff(deleteS),
      keepConstInit(keepConstInit) {}

PreservedAnalyses ExtractGVPass::run(Module &M, ModuleAnalysisManager &) {
  // Visit the global inline asm.
  if (!deleteStuff)
    M.setModuleInlineAsm("");

  // For simplicity, just give all GlobalValues ExternalLinkage. A trickier
  // implementation could figure out which GlobalValues are actually
  // referenced by the Named set, and which GlobalValues in the rest of
  // the module are referenced by the NamedSet, and get away with leaving
  // more internal and private things internal and private. But for now,
  // be conservative and simple.

  // Visit the GlobalVariables.
  for (GlobalVariable &GV : M.globals()) {
    bool Delete = deleteStuff == (bool)Named.count(&GV) &&
                  !GV.isDeclaration() && (!GV.isConstant() || !keepConstInit);
    if (!Delete) {
      if (GV.hasAvailableExternallyLinkage())
        continue;
      if (GV.getName() == "llvm.global_ctors")
        continue;
    }

    makeVisible(GV, Delete);

    if (Delete) {
      // Make this a declaration and drop it's comdat.
      GV.setInitializer(nullptr);
      GV.setComdat(nullptr);
    }
  }

  // Visit the Functions.
  for (Function &F : M) {
    bool Delete = deleteStuff == (bool)Named.count(&F) && !F.isDeclaration();
    if (!Delete) {
      if (F.hasAvailableExternallyLinkage())
        continue;
    }

    makeVisible(F, Delete);

    if (Delete) {
      // Make this a declaration and drop it's comdat.
      F.deleteBody();
      F.setComdat(nullptr);
    }
  }

  // Visit the Aliases.
  for (GlobalAlias &GA : llvm::make_early_inc_range(M.aliases())) {
    bool Delete = deleteStuff == (bool)Named.count(&GA);
    makeVisible(GA, Delete);

    if (Delete) {
      Type *Ty = GA.getValueType();

      GA.removeFromParent();
      llvm::Value *Declaration;
      if (FunctionType *FTy = dyn_cast<FunctionType>(Ty)) {
        Declaration = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                       GA.getAddressSpace(), GA.getName(), &M);

      } else {
        Declaration = new GlobalVariable(
            M, Ty, false, GlobalValue::ExternalLinkage, nullptr, GA.getName());
      }
      GA.replaceAllUsesWith(Declaration);
      delete &GA;
    }
  }

  // Visit the IFuncs.
  for (GlobalIFunc &IF : llvm::make_early_inc_range(M.ifuncs())) {
    bool Delete = deleteStuff == (bool)Named.count(&IF);
    makeVisible(IF, Delete);

    if (!Delete)
      continue;

    auto *FuncType = dyn_cast<FunctionType>(IF.getValueType());
    IF.removeFromParent();
    llvm::Value *Declaration =
        Function::Create(FuncType, GlobalValue::ExternalLinkage,
                         IF.getAddressSpace(), IF.getName(), &M);
    IF.replaceAllUsesWith(Declaration);
    delete &IF;
  }

  return PreservedAnalyses::none();
}
