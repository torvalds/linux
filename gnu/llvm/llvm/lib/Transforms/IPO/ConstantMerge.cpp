//===- ConstantMerge.cpp - Merge duplicate global constants ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface to a pass that merges duplicate global
// constants together into a single constant that is shared.  This is useful
// because some passes (ie TraceValues) insert a lot of string constants into
// the program, regardless of whether or not an existing string is available.
//
// Algorithm: ConstantMerge is designed to build up a map of available constants
// and eliminate duplicates when it is initialized.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO.h"
#include <algorithm>
#include <cassert>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "constmerge"

STATISTIC(NumIdenticalMerged, "Number of identical global constants merged");

/// Find values that are marked as llvm.used.
static void FindUsedValues(GlobalVariable *LLVMUsed,
                           SmallPtrSetImpl<const GlobalValue*> &UsedValues) {
  if (!LLVMUsed) return;
  ConstantArray *Inits = cast<ConstantArray>(LLVMUsed->getInitializer());

  for (unsigned i = 0, e = Inits->getNumOperands(); i != e; ++i) {
    Value *Operand = Inits->getOperand(i)->stripPointerCasts();
    GlobalValue *GV = cast<GlobalValue>(Operand);
    UsedValues.insert(GV);
  }
}

// True if A is better than B.
static bool IsBetterCanonical(const GlobalVariable &A,
                              const GlobalVariable &B) {
  if (!A.hasLocalLinkage() && B.hasLocalLinkage())
    return true;

  if (A.hasLocalLinkage() && !B.hasLocalLinkage())
    return false;

  return A.hasGlobalUnnamedAddr();
}

static bool hasMetadataOtherThanDebugLoc(const GlobalVariable *GV) {
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  GV->getAllMetadata(MDs);
  for (const auto &V : MDs)
    if (V.first != LLVMContext::MD_dbg)
      return true;
  return false;
}

static void copyDebugLocMetadata(const GlobalVariable *From,
                                 GlobalVariable *To) {
  SmallVector<DIGlobalVariableExpression *, 1> MDs;
  From->getDebugInfo(MDs);
  for (auto *MD : MDs)
    To->addDebugInfo(MD);
}

static Align getAlign(GlobalVariable *GV) {
  return GV->getAlign().value_or(
      GV->getDataLayout().getPreferredAlign(GV));
}

static bool
isUnmergeableGlobal(GlobalVariable *GV,
                    const SmallPtrSetImpl<const GlobalValue *> &UsedGlobals) {
  // Only process constants with initializers in the default address space.
  return !GV->isConstant() || !GV->hasDefinitiveInitializer() ||
         GV->getType()->getAddressSpace() != 0 || GV->hasSection() ||
         // Don't touch thread-local variables.
         GV->isThreadLocal() ||
         // Don't touch values marked with attribute(used).
         UsedGlobals.count(GV);
}

enum class CanMerge { No, Yes };
static CanMerge makeMergeable(GlobalVariable *Old, GlobalVariable *New) {
  if (!Old->hasGlobalUnnamedAddr() && !New->hasGlobalUnnamedAddr())
    return CanMerge::No;
  if (hasMetadataOtherThanDebugLoc(Old))
    return CanMerge::No;
  assert(!hasMetadataOtherThanDebugLoc(New));
  if (!Old->hasGlobalUnnamedAddr())
    New->setUnnamedAddr(GlobalValue::UnnamedAddr::None);
  return CanMerge::Yes;
}

static void replace(Module &M, GlobalVariable *Old, GlobalVariable *New) {
  Constant *NewConstant = New;

  LLVM_DEBUG(dbgs() << "Replacing global: @" << Old->getName() << " -> @"
                    << New->getName() << "\n");

  // Bump the alignment if necessary.
  if (Old->getAlign() || New->getAlign())
    New->setAlignment(std::max(getAlign(Old), getAlign(New)));

  copyDebugLocMetadata(Old, New);
  Old->replaceAllUsesWith(NewConstant);

  // Delete the global value from the module.
  assert(Old->hasLocalLinkage() &&
         "Refusing to delete an externally visible global variable.");
  Old->eraseFromParent();
}

static bool mergeConstants(Module &M) {
  // Find all the globals that are marked "used".  These cannot be merged.
  SmallPtrSet<const GlobalValue*, 8> UsedGlobals;
  FindUsedValues(M.getGlobalVariable("llvm.used"), UsedGlobals);
  FindUsedValues(M.getGlobalVariable("llvm.compiler.used"), UsedGlobals);

  // Map unique constants to globals.
  DenseMap<Constant *, GlobalVariable *> CMap;

  SmallVector<std::pair<GlobalVariable *, GlobalVariable *>, 32>
      SameContentReplacements;

  size_t ChangesMade = 0;
  size_t OldChangesMade = 0;

  // Iterate constant merging while we are still making progress.  Merging two
  // constants together may allow us to merge other constants together if the
  // second level constants have initializers which point to the globals that
  // were just merged.
  while (true) {
    // Find the canonical constants others will be merged with.
    for (GlobalVariable &GV : llvm::make_early_inc_range(M.globals())) {
      // If this GV is dead, remove it.
      GV.removeDeadConstantUsers();
      if (GV.use_empty() && GV.hasLocalLinkage()) {
        GV.eraseFromParent();
        ++ChangesMade;
        continue;
      }

      if (isUnmergeableGlobal(&GV, UsedGlobals))
        continue;

      // This transformation is legal for weak ODR globals in the sense it
      // doesn't change semantics, but we really don't want to perform it
      // anyway; it's likely to pessimize code generation, and some tools
      // (like the Darwin linker in cases involving CFString) don't expect it.
      if (GV.isWeakForLinker())
        continue;

      // Don't touch globals with metadata other then !dbg.
      if (hasMetadataOtherThanDebugLoc(&GV))
        continue;

      Constant *Init = GV.getInitializer();

      // Check to see if the initializer is already known.
      GlobalVariable *&Slot = CMap[Init];

      // If this is the first constant we find or if the old one is local,
      // replace with the current one. If the current is externally visible
      // it cannot be replace, but can be the canonical constant we merge with.
      bool FirstConstantFound = !Slot;
      if (FirstConstantFound || IsBetterCanonical(GV, *Slot)) {
        Slot = &GV;
        LLVM_DEBUG(dbgs() << "Cmap[" << *Init << "] = " << GV.getName()
                          << (FirstConstantFound ? "\n" : " (updated)\n"));
      }
    }

    // Identify all globals that can be merged together, filling in the
    // SameContentReplacements vector. We cannot do the replacement in this pass
    // because doing so may cause initializers of other globals to be rewritten,
    // invalidating the Constant* pointers in CMap.
    for (GlobalVariable &GV : llvm::make_early_inc_range(M.globals())) {
      if (isUnmergeableGlobal(&GV, UsedGlobals))
        continue;

      // We can only replace constant with local linkage.
      if (!GV.hasLocalLinkage())
        continue;

      Constant *Init = GV.getInitializer();

      // Check to see if the initializer is already known.
      auto Found = CMap.find(Init);
      if (Found == CMap.end())
        continue;

      GlobalVariable *Slot = Found->second;
      if (Slot == &GV)
        continue;

      if (makeMergeable(&GV, Slot) == CanMerge::No)
        continue;

      // Make all uses of the duplicate constant use the canonical version.
      LLVM_DEBUG(dbgs() << "Will replace: @" << GV.getName() << " -> @"
                        << Slot->getName() << "\n");
      SameContentReplacements.push_back(std::make_pair(&GV, Slot));
    }

    // Now that we have figured out which replacements must be made, do them all
    // now.  This avoid invalidating the pointers in CMap, which are unneeded
    // now.
    for (unsigned i = 0, e = SameContentReplacements.size(); i != e; ++i) {
      GlobalVariable *Old = SameContentReplacements[i].first;
      GlobalVariable *New = SameContentReplacements[i].second;
      replace(M, Old, New);
      ++ChangesMade;
      ++NumIdenticalMerged;
    }

    if (ChangesMade == OldChangesMade)
      break;
    OldChangesMade = ChangesMade;

    SameContentReplacements.clear();
    CMap.clear();
  }

  return ChangesMade;
}

PreservedAnalyses ConstantMergePass::run(Module &M, ModuleAnalysisManager &) {
  if (!mergeConstants(M))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
