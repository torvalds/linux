//===- Assumptions.cpp ------ Collection of helpers for assumptions -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements helper functions for accessing assumption infomration
//  inside of the "llvm.assume" metadata.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Assumptions.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"

using namespace llvm;

namespace {
bool hasAssumption(const Attribute &A,
                   const KnownAssumptionString &AssumptionStr) {
  if (!A.isValid())
    return false;
  assert(A.isStringAttribute() && "Expected a string attribute!");

  SmallVector<StringRef, 8> Strings;
  A.getValueAsString().split(Strings, ",");

  return llvm::is_contained(Strings, AssumptionStr);
}

DenseSet<StringRef> getAssumptions(const Attribute &A) {
  if (!A.isValid())
    return DenseSet<StringRef>();
  assert(A.isStringAttribute() && "Expected a string attribute!");

  DenseSet<StringRef> Assumptions;
  SmallVector<StringRef, 8> Strings;
  A.getValueAsString().split(Strings, ",");

  for (StringRef Str : Strings)
    Assumptions.insert(Str);
  return Assumptions;
}

template <typename AttrSite>
bool addAssumptionsImpl(AttrSite &Site,
                        const DenseSet<StringRef> &Assumptions) {
  if (Assumptions.empty())
    return false;

  DenseSet<StringRef> CurAssumptions = getAssumptions(Site);

  if (!set_union(CurAssumptions, Assumptions))
    return false;

  LLVMContext &Ctx = Site.getContext();
  Site.addFnAttr(llvm::Attribute::get(
      Ctx, llvm::AssumptionAttrKey,
      llvm::join(CurAssumptions.begin(), CurAssumptions.end(), ",")));

  return true;
}
} // namespace

bool llvm::hasAssumption(const Function &F,
                         const KnownAssumptionString &AssumptionStr) {
  const Attribute &A = F.getFnAttribute(AssumptionAttrKey);
  return ::hasAssumption(A, AssumptionStr);
}

bool llvm::hasAssumption(const CallBase &CB,
                         const KnownAssumptionString &AssumptionStr) {
  if (Function *F = CB.getCalledFunction())
    if (hasAssumption(*F, AssumptionStr))
      return true;

  const Attribute &A = CB.getFnAttr(AssumptionAttrKey);
  return ::hasAssumption(A, AssumptionStr);
}

DenseSet<StringRef> llvm::getAssumptions(const Function &F) {
  const Attribute &A = F.getFnAttribute(AssumptionAttrKey);
  return ::getAssumptions(A);
}

DenseSet<StringRef> llvm::getAssumptions(const CallBase &CB) {
  const Attribute &A = CB.getFnAttr(AssumptionAttrKey);
  return ::getAssumptions(A);
}

bool llvm::addAssumptions(Function &F, const DenseSet<StringRef> &Assumptions) {
  return ::addAssumptionsImpl(F, Assumptions);
}

bool llvm::addAssumptions(CallBase &CB,
                          const DenseSet<StringRef> &Assumptions) {
  return ::addAssumptionsImpl(CB, Assumptions);
}

StringSet<> llvm::KnownAssumptionStrings({
    "omp_no_openmp",          // OpenMP 5.1
    "omp_no_openmp_routines", // OpenMP 5.1
    "omp_no_parallelism",     // OpenMP 5.1
    "ompx_spmd_amenable",     // OpenMPOpt extension
    "ompx_no_call_asm",       // OpenMPOpt extension
});
