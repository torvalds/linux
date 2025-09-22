//===- ElimAvailExtern.cpp - DCE unreachable internal functions -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This transform is designed to eliminate available external global
// definitions from the program, turning them into declarations.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ElimAvailExtern.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/GlobalStatus.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

#define DEBUG_TYPE "elim-avail-extern"

cl::opt<bool> ConvertToLocal(
    "avail-extern-to-local", cl::Hidden,
    cl::desc("Convert available_externally into locals, renaming them "
             "to avoid link-time clashes."));

STATISTIC(NumRemovals, "Number of functions removed");
STATISTIC(NumConversions, "Number of functions converted");
STATISTIC(NumVariables, "Number of global variables removed");

void deleteFunction(Function &F) {
  // This will set the linkage to external
  F.deleteBody();
  ++NumRemovals;
}

/// Create a copy of the thinlto import, mark it local, and redirect direct
/// calls to the copy. Only direct calls are replaced, so that e.g. indirect
/// call function pointer tests would use the global identity of the function.
///
/// Currently, Value Profiling ("VP") MD_prof data isn't updated to refer to the
/// clone's GUID (which will be different, because the name and linkage is
/// different), under the assumption that the last consumer of this data is
/// upstream the pipeline (e.g. ICP).
static void convertToLocalCopy(Module &M, Function &F) {
  assert(F.hasAvailableExternallyLinkage());
  assert(!F.isDeclaration());
  // If we can't find a single use that's a call, just delete the function.
  if (F.uses().end() == llvm::find_if(F.uses(), [&](Use &U) {
        return isa<CallBase>(U.getUser());
      }))
    return deleteFunction(F);

  auto OrigName = F.getName().str();
  // Build a new name. We still need the old name (see below).
  // We could just rely on internal linking allowing 2 modules have internal
  // functions with the same name, but that just creates more trouble than
  // necessary e.g. distinguishing profiles or debugging. Instead, we append the
  // module identifier.
  auto NewName = OrigName + ".__uniq" + getUniqueModuleId(&M);
  F.setName(NewName);
  if (auto *SP = F.getSubprogram())
    SP->replaceLinkageName(MDString::get(F.getParent()->getContext(), NewName));

  F.setLinkage(GlobalValue::InternalLinkage);
  // Now make a declaration for the old name. We'll use it if there are non-call
  // uses. For those, it would be incorrect to replace them with the local copy:
  // for example, one such use could be taking the address of the function and
  // passing it to an external function, which, in turn, might compare the
  // function pointer to the original (non-local) function pointer, e.g. as part
  // of indirect call promotion.
  auto *Decl =
      Function::Create(F.getFunctionType(), GlobalValue::ExternalLinkage,
                       F.getAddressSpace(), OrigName, F.getParent());
  F.replaceUsesWithIf(Decl,
                      [&](Use &U) { return !isa<CallBase>(U.getUser()); });
  ++NumConversions;
}

static bool eliminateAvailableExternally(Module &M) {
  bool Changed = false;

  // Drop initializers of available externally global variables.
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasAvailableExternallyLinkage())
      continue;
    if (GV.hasInitializer()) {
      Constant *Init = GV.getInitializer();
      GV.setInitializer(nullptr);
      if (isSafeToDestroyConstant(Init))
        Init->destroyConstant();
    }
    GV.removeDeadConstantUsers();
    GV.setLinkage(GlobalValue::ExternalLinkage);
    ++NumVariables;
    Changed = true;
  }

  // Drop the bodies of available externally functions.
  for (Function &F : llvm::make_early_inc_range(M)) {
    if (F.isDeclaration() || !F.hasAvailableExternallyLinkage())
      continue;

    if (ConvertToLocal)
      convertToLocalCopy(M, F);
    else
      deleteFunction(F);

    F.removeDeadConstantUsers();
    Changed = true;
  }

  return Changed;
}

PreservedAnalyses
EliminateAvailableExternallyPass::run(Module &M, ModuleAnalysisManager &) {
  if (!eliminateAvailableExternally(M))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
