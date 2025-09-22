//===- CtorUtils.cpp - Helpers for working with global_ctors ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions that are used to process llvm.global_ctors.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CtorUtils.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <numeric>

#define DEBUG_TYPE "ctor_utils"

using namespace llvm;

/// Given a specified llvm.global_ctors list, remove the listed elements.
static void removeGlobalCtors(GlobalVariable *GCL, const BitVector &CtorsToRemove) {
  // Filter out the initializer elements to remove.
  ConstantArray *OldCA = cast<ConstantArray>(GCL->getInitializer());
  SmallVector<Constant *, 10> CAList;
  for (unsigned I = 0, E = OldCA->getNumOperands(); I < E; ++I)
    if (!CtorsToRemove.test(I))
      CAList.push_back(OldCA->getOperand(I));

  // Create the new array initializer.
  ArrayType *ATy =
      ArrayType::get(OldCA->getType()->getElementType(), CAList.size());
  Constant *CA = ConstantArray::get(ATy, CAList);

  // If we didn't change the number of elements, don't create a new GV.
  if (CA->getType() == OldCA->getType()) {
    GCL->setInitializer(CA);
    return;
  }

  // Create the new global and insert it next to the existing list.
  GlobalVariable *NGV =
      new GlobalVariable(CA->getType(), GCL->isConstant(), GCL->getLinkage(),
                         CA, "", GCL->getThreadLocalMode());
  GCL->getParent()->insertGlobalVariable(GCL->getIterator(), NGV);
  NGV->takeName(GCL);

  // Nuke the old list, replacing any uses with the new one.
  if (!GCL->use_empty())
    GCL->replaceAllUsesWith(NGV);

  GCL->eraseFromParent();
}

/// Given a llvm.global_ctors list that we can understand,
/// return a list of the functions and null terminator as a vector.
static std::vector<std::pair<uint32_t, Function *>>
parseGlobalCtors(GlobalVariable *GV) {
  ConstantArray *CA = cast<ConstantArray>(GV->getInitializer());
  std::vector<std::pair<uint32_t, Function *>> Result;
  Result.reserve(CA->getNumOperands());
  for (auto &V : CA->operands()) {
    ConstantStruct *CS = cast<ConstantStruct>(V);
    Result.emplace_back(cast<ConstantInt>(CS->getOperand(0))->getZExtValue(),
                        dyn_cast<Function>(CS->getOperand(1)));
  }
  return Result;
}

/// Find the llvm.global_ctors list.
static GlobalVariable *findGlobalCtors(Module &M) {
  GlobalVariable *GV = M.getGlobalVariable("llvm.global_ctors");
  if (!GV)
    return nullptr;

  // Verify that the initializer is simple enough for us to handle. We are
  // only allowed to optimize the initializer if it is unique.
  if (!GV->hasUniqueInitializer())
    return nullptr;

  // If there are no ctors, then the initializer might be null/undef/poison.
  // Ignore anything but an array.
  ConstantArray *CA = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!CA)
    return nullptr;

  for (auto &V : CA->operands()) {
    if (isa<ConstantAggregateZero>(V))
      continue;
    ConstantStruct *CS = cast<ConstantStruct>(V);
    if (isa<ConstantPointerNull>(CS->getOperand(1)))
      continue;

    // Can only handle global constructors with no arguments.
    Function *F = dyn_cast<Function>(CS->getOperand(1));
    if (!F || F->arg_size() != 0)
      return nullptr;
  }
  return GV;
}

/// Call "ShouldRemove" for every entry in M's global_ctor list and remove the
/// entries for which it returns true.  Return true if anything changed.
bool llvm::optimizeGlobalCtorsList(
    Module &M, function_ref<bool(uint32_t, Function *)> ShouldRemove) {
  GlobalVariable *GlobalCtors = findGlobalCtors(M);
  if (!GlobalCtors)
    return false;

  std::vector<std::pair<uint32_t, Function *>> Ctors =
      parseGlobalCtors(GlobalCtors);
  if (Ctors.empty())
    return false;

  bool MadeChange = false;
  // Loop over global ctors, optimizing them when we can.
  BitVector CtorsToRemove(Ctors.size());
  std::vector<size_t> CtorsByPriority(Ctors.size());
  std::iota(CtorsByPriority.begin(), CtorsByPriority.end(), 0);
  stable_sort(CtorsByPriority, [&](size_t LHS, size_t RHS) {
    return Ctors[LHS].first < Ctors[RHS].first;
  });
  for (unsigned CtorIndex : CtorsByPriority) {
    const uint32_t Priority = Ctors[CtorIndex].first;
    Function *F = Ctors[CtorIndex].second;
    if (!F)
      continue;

    LLVM_DEBUG(dbgs() << "Optimizing Global Constructor: " << *F << "\n");

    // If we can evaluate the ctor at compile time, do.
    if (ShouldRemove(Priority, F)) {
      Ctors[CtorIndex].second = nullptr;
      CtorsToRemove.set(CtorIndex);
      MadeChange = true;
      continue;
    }
  }

  if (!MadeChange)
    return false;

  removeGlobalCtors(GlobalCtors, CtorsToRemove);
  return true;
}
