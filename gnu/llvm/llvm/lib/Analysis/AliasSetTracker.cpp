//===- AliasSetTracker.cpp - Alias Sets Tracker implementation-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AliasSetTracker and AliasSet classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static cl::opt<unsigned> SaturationThreshold(
    "alias-set-saturation-threshold", cl::Hidden, cl::init(250),
    cl::desc("The maximum total number of memory locations alias "
             "sets may contain before degradation"));

/// mergeSetIn - Merge the specified alias set into this alias set.
void AliasSet::mergeSetIn(AliasSet &AS, AliasSetTracker &AST,
                          BatchAAResults &BatchAA) {
  assert(!AS.Forward && "Alias set is already forwarding!");
  assert(!Forward && "This set is a forwarding set!!");

  // Update the alias and access types of this set...
  Access |= AS.Access;
  Alias  |= AS.Alias;

  if (Alias == SetMustAlias) {
    // Check that these two merged sets really are must aliases. If we cannot
    // find a must-alias pair between them, this set becomes a may alias.
    if (!any_of(MemoryLocs, [&](const MemoryLocation &MemLoc) {
          return any_of(AS.MemoryLocs, [&](const MemoryLocation &ASMemLoc) {
            return BatchAA.isMustAlias(MemLoc, ASMemLoc);
          });
        }))
      Alias = SetMayAlias;
  }

  // Merge the list of constituent memory locations...
  if (MemoryLocs.empty()) {
    std::swap(MemoryLocs, AS.MemoryLocs);
  } else {
    append_range(MemoryLocs, AS.MemoryLocs);
    AS.MemoryLocs.clear();
  }

  bool ASHadUnknownInsts = !AS.UnknownInsts.empty();
  if (UnknownInsts.empty()) {            // Merge call sites...
    if (ASHadUnknownInsts) {
      std::swap(UnknownInsts, AS.UnknownInsts);
      addRef();
    }
  } else if (ASHadUnknownInsts) {
    llvm::append_range(UnknownInsts, AS.UnknownInsts);
    AS.UnknownInsts.clear();
  }

  AS.Forward = this; // Forward across AS now...
  addRef();          // AS is now pointing to us...

  if (ASHadUnknownInsts)
    AS.dropRef(AST);
}

void AliasSetTracker::removeAliasSet(AliasSet *AS) {
  if (AliasSet *Fwd = AS->Forward) {
    Fwd->dropRef(*this);
    AS->Forward = nullptr;
  } else // Update TotalAliasSetSize only if not forwarding.
    TotalAliasSetSize -= AS->size();

  AliasSets.erase(AS);
  // If we've removed the saturated alias set, set saturated marker back to
  // nullptr and ensure this tracker is empty.
  if (AS == AliasAnyAS) {
    AliasAnyAS = nullptr;
    assert(AliasSets.empty() && "Tracker not empty");
  }
}

void AliasSet::removeFromTracker(AliasSetTracker &AST) {
  assert(RefCount == 0 && "Cannot remove non-dead alias set from tracker!");
  AST.removeAliasSet(this);
}

void AliasSet::addMemoryLocation(AliasSetTracker &AST,
                                 const MemoryLocation &MemLoc,
                                 bool KnownMustAlias) {
  if (isMustAlias() && !KnownMustAlias) {
    // If we cannot find a must-alias with any of the existing MemoryLocs, we
    // must downgrade to may-alias.
    if (!any_of(MemoryLocs, [&](const MemoryLocation &ASMemLoc) {
          return AST.getAliasAnalysis().isMustAlias(MemLoc, ASMemLoc);
        }))
      Alias = SetMayAlias;
  }

  // Add it to the end of the list...
  MemoryLocs.push_back(MemLoc);

  AST.TotalAliasSetSize++;
}

void AliasSet::addUnknownInst(Instruction *I, BatchAAResults &AA) {
  if (UnknownInsts.empty())
    addRef();
  UnknownInsts.emplace_back(I);

  // Guards are marked as modifying memory for control flow modelling purposes,
  // but don't actually modify any specific memory location.
  using namespace PatternMatch;
  bool MayWriteMemory = I->mayWriteToMemory() && !isGuard(I) &&
    !(I->use_empty() && match(I, m_Intrinsic<Intrinsic::invariant_start>()));
  if (!MayWriteMemory) {
    Alias = SetMayAlias;
    Access |= RefAccess;
    return;
  }

  // FIXME: This should use mod/ref information to make this not suck so bad
  Alias = SetMayAlias;
  Access = ModRefAccess;
}

/// aliasesMemoryLocation - If the specified memory location "may" (or must)
/// alias one of the members in the set return the appropriate AliasResult.
/// Otherwise return NoAlias.
///
AliasResult AliasSet::aliasesMemoryLocation(const MemoryLocation &MemLoc,
                                            BatchAAResults &AA) const {
  if (AliasAny)
    return AliasResult::MayAlias;

  // Check all of the memory locations in the set...
  for (const auto &ASMemLoc : MemoryLocs) {
    AliasResult AR = AA.alias(MemLoc, ASMemLoc);
    if (AR != AliasResult::NoAlias)
      return AR;
  }

  // Check the unknown instructions...
  for (Instruction *Inst : UnknownInsts)
    if (isModOrRefSet(AA.getModRefInfo(Inst, MemLoc)))
      return AliasResult::MayAlias;

  return AliasResult::NoAlias;
}

ModRefInfo AliasSet::aliasesUnknownInst(const Instruction *Inst,
                                        BatchAAResults &AA) const {

  if (AliasAny)
    return ModRefInfo::ModRef;

  if (!Inst->mayReadOrWriteMemory())
    return ModRefInfo::NoModRef;

  for (Instruction *UnknownInst : UnknownInsts) {
    const auto *C1 = dyn_cast<CallBase>(UnknownInst);
    const auto *C2 = dyn_cast<CallBase>(Inst);
    if (!C1 || !C2 || isModOrRefSet(AA.getModRefInfo(C1, C2)) ||
        isModOrRefSet(AA.getModRefInfo(C2, C1))) {
      // TODO: Could be more precise, but not really useful right now.
      return ModRefInfo::ModRef;
    }
  }

  ModRefInfo MR = ModRefInfo::NoModRef;
  for (const auto &ASMemLoc : MemoryLocs) {
    MR |= AA.getModRefInfo(Inst, ASMemLoc);
    if (isModAndRefSet(MR))
      return MR;
  }

  return MR;
}

AliasSet::PointerVector AliasSet::getPointers() const {
  SmallSetVector<const Value *, 8> Pointers;
  for (const MemoryLocation &MemLoc : MemoryLocs)
    Pointers.insert(MemLoc.Ptr);
  return Pointers.takeVector();
}

void AliasSetTracker::clear() {
  PointerMap.clear();
  AliasSets.clear();
}

/// mergeAliasSetsForMemoryLocation - Given a memory location, merge all alias
/// sets that may alias it. Return the unified set, or nullptr if no aliasing
/// set was found. A known existing alias set for the pointer value of the
/// memory location can be passed in (or nullptr if not available). MustAliasAll
/// is updated to true/false if the memory location is found to MustAlias all
/// the sets it merged.
AliasSet *AliasSetTracker::mergeAliasSetsForMemoryLocation(
    const MemoryLocation &MemLoc, AliasSet *PtrAS, bool &MustAliasAll) {
  AliasSet *FoundSet = nullptr;
  MustAliasAll = true;
  for (AliasSet &AS : llvm::make_early_inc_range(*this)) {
    if (AS.Forward)
      continue;

    // An alias set that already contains a memory location with the same
    // pointer value is directly assumed to MustAlias; we bypass the AA query in
    // this case.
    // Note: it is not guaranteed that AA would always provide the same result;
    // a known exception are undef pointer values, where alias(undef, undef) is
    // NoAlias, while we treat it as MustAlias.
    if (&AS != PtrAS) {
      AliasResult AR = AS.aliasesMemoryLocation(MemLoc, AA);
      if (AR == AliasResult::NoAlias)
        continue;

      if (AR != AliasResult::MustAlias)
        MustAliasAll = false;
    }

    if (!FoundSet) {
      // If this is the first alias set ptr can go into, remember it.
      FoundSet = &AS;
    } else {
      // Otherwise, we must merge the sets.
      FoundSet->mergeSetIn(AS, *this, AA);
    }
  }

  return FoundSet;
}

AliasSet *AliasSetTracker::findAliasSetForUnknownInst(Instruction *Inst) {
  AliasSet *FoundSet = nullptr;
  for (AliasSet &AS : llvm::make_early_inc_range(*this)) {
    if (AS.Forward || !isModOrRefSet(AS.aliasesUnknownInst(Inst, AA)))
      continue;
    if (!FoundSet) {
      // If this is the first alias set ptr can go into, remember it.
      FoundSet = &AS;
    } else {
      // Otherwise, we must merge the sets.
      FoundSet->mergeSetIn(AS, *this, AA);
    }
  }
  return FoundSet;
}

AliasSet &AliasSetTracker::getAliasSetFor(const MemoryLocation &MemLoc) {
  // The alias sets are indexed with a map from the memory locations' pointer
  // values. If the memory location is already registered, we can find it in the
  // alias set associated with its pointer.
  AliasSet *&MapEntry = PointerMap[MemLoc.Ptr];
  if (MapEntry) {
    collapseForwardingIn(MapEntry);
    if (is_contained(MapEntry->MemoryLocs, MemLoc))
      return *MapEntry;
  }

  AliasSet *AS;
  bool MustAliasAll = false;
  if (AliasAnyAS) {
    // At this point, the AST is saturated, so we only have one active alias
    // set. That means we already know which alias set we want to return, and
    // just need to add the memory location to that set to keep the data
    // structure consistent.
    // This, of course, means that we will never need a merge here.
    AS = AliasAnyAS;
  } else if (AliasSet *AliasAS = mergeAliasSetsForMemoryLocation(
                 MemLoc, MapEntry, MustAliasAll)) {
    // Add it to the alias set it aliases.
    AS = AliasAS;
  } else {
    // Otherwise create a new alias set to hold the new memory location.
    AliasSets.push_back(AS = new AliasSet());
    MustAliasAll = true;
  }

  // Register memory location in selected alias set.
  AS->addMemoryLocation(*this, MemLoc, MustAliasAll);
  // Register selected alias set in pointer map (or ensure it is consistent with
  // earlier map entry after taking into account new merging).
  if (MapEntry) {
    collapseForwardingIn(MapEntry);
    assert(MapEntry == AS && "Memory locations with same pointer value cannot "
                             "be in different alias sets");
  } else {
    AS->addRef();
    MapEntry = AS;
  }
  return *AS;
}

void AliasSetTracker::add(const MemoryLocation &Loc) {
  addMemoryLocation(Loc, AliasSet::NoAccess);
}

void AliasSetTracker::add(LoadInst *LI) {
  if (isStrongerThanMonotonic(LI->getOrdering()))
    return addUnknown(LI);
  addMemoryLocation(MemoryLocation::get(LI), AliasSet::RefAccess);
}

void AliasSetTracker::add(StoreInst *SI) {
  if (isStrongerThanMonotonic(SI->getOrdering()))
    return addUnknown(SI);
  addMemoryLocation(MemoryLocation::get(SI), AliasSet::ModAccess);
}

void AliasSetTracker::add(VAArgInst *VAAI) {
  addMemoryLocation(MemoryLocation::get(VAAI), AliasSet::ModRefAccess);
}

void AliasSetTracker::add(AnyMemSetInst *MSI) {
  addMemoryLocation(MemoryLocation::getForDest(MSI), AliasSet::ModAccess);
}

void AliasSetTracker::add(AnyMemTransferInst *MTI) {
  addMemoryLocation(MemoryLocation::getForDest(MTI), AliasSet::ModAccess);
  addMemoryLocation(MemoryLocation::getForSource(MTI), AliasSet::RefAccess);
}

void AliasSetTracker::addUnknown(Instruction *Inst) {
  if (isa<DbgInfoIntrinsic>(Inst))
    return; // Ignore DbgInfo Intrinsics.

  if (auto *II = dyn_cast<IntrinsicInst>(Inst)) {
    // These intrinsics will show up as affecting memory, but they are just
    // markers.
    switch (II->getIntrinsicID()) {
    default:
      break;
      // FIXME: Add lifetime/invariant intrinsics (See: PR30807).
    case Intrinsic::allow_runtime_check:
    case Intrinsic::allow_ubsan_check:
    case Intrinsic::assume:
    case Intrinsic::experimental_noalias_scope_decl:
    case Intrinsic::sideeffect:
    case Intrinsic::pseudoprobe:
      return;
    }
  }
  if (!Inst->mayReadOrWriteMemory())
    return; // doesn't alias anything

  if (AliasSet *AS = findAliasSetForUnknownInst(Inst)) {
    AS->addUnknownInst(Inst, AA);
    return;
  }
  AliasSets.push_back(new AliasSet());
  AliasSets.back().addUnknownInst(Inst, AA);
}

void AliasSetTracker::add(Instruction *I) {
  // Dispatch to one of the other add methods.
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return add(LI);
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return add(SI);
  if (VAArgInst *VAAI = dyn_cast<VAArgInst>(I))
    return add(VAAI);
  if (AnyMemSetInst *MSI = dyn_cast<AnyMemSetInst>(I))
    return add(MSI);
  if (AnyMemTransferInst *MTI = dyn_cast<AnyMemTransferInst>(I))
    return add(MTI);

  // Handle all calls with known mod/ref sets genericall
  if (auto *Call = dyn_cast<CallBase>(I))
    if (Call->onlyAccessesArgMemory()) {
      auto getAccessFromModRef = [](ModRefInfo MRI) {
        if (isRefSet(MRI) && isModSet(MRI))
          return AliasSet::ModRefAccess;
        else if (isModSet(MRI))
          return AliasSet::ModAccess;
        else if (isRefSet(MRI))
          return AliasSet::RefAccess;
        else
          return AliasSet::NoAccess;
      };

      ModRefInfo CallMask = AA.getMemoryEffects(Call).getModRef();

      // Some intrinsics are marked as modifying memory for control flow
      // modelling purposes, but don't actually modify any specific memory
      // location.
      using namespace PatternMatch;
      if (Call->use_empty() &&
          match(Call, m_Intrinsic<Intrinsic::invariant_start>()))
        CallMask &= ModRefInfo::Ref;

      for (auto IdxArgPair : enumerate(Call->args())) {
        int ArgIdx = IdxArgPair.index();
        const Value *Arg = IdxArgPair.value();
        if (!Arg->getType()->isPointerTy())
          continue;
        MemoryLocation ArgLoc =
            MemoryLocation::getForArgument(Call, ArgIdx, nullptr);
        ModRefInfo ArgMask = AA.getArgModRefInfo(Call, ArgIdx);
        ArgMask &= CallMask;
        if (!isNoModRef(ArgMask))
          addMemoryLocation(ArgLoc, getAccessFromModRef(ArgMask));
      }
      return;
    }

  return addUnknown(I);
}

void AliasSetTracker::add(BasicBlock &BB) {
  for (auto &I : BB)
    add(&I);
}

void AliasSetTracker::add(const AliasSetTracker &AST) {
  assert(&AA == &AST.AA &&
         "Merging AliasSetTracker objects with different Alias Analyses!");

  // Loop over all of the alias sets in AST, adding the members contained
  // therein into the current alias sets.  This can cause alias sets to be
  // merged together in the current AST.
  for (const AliasSet &AS : AST) {
    if (AS.Forward)
      continue; // Ignore forwarding alias sets

    // If there are any call sites in the alias set, add them to this AST.
    for (Instruction *Inst : AS.UnknownInsts)
      add(Inst);

    // Loop over all of the memory locations in this alias set.
    for (const MemoryLocation &ASMemLoc : AS.MemoryLocs)
      addMemoryLocation(ASMemLoc, (AliasSet::AccessLattice)AS.Access);
  }
}

AliasSet &AliasSetTracker::mergeAllAliasSets() {
  assert(!AliasAnyAS && (TotalAliasSetSize > SaturationThreshold) &&
         "Full merge should happen once, when the saturation threshold is "
         "reached");

  // Collect all alias sets, so that we can drop references with impunity
  // without worrying about iterator invalidation.
  std::vector<AliasSet *> ASVector;
  ASVector.reserve(SaturationThreshold);
  for (AliasSet &AS : *this)
    ASVector.push_back(&AS);

  // Copy all instructions and memory locations into a new set, and forward all
  // other sets to it.
  AliasSets.push_back(new AliasSet());
  AliasAnyAS = &AliasSets.back();
  AliasAnyAS->Alias = AliasSet::SetMayAlias;
  AliasAnyAS->Access = AliasSet::ModRefAccess;
  AliasAnyAS->AliasAny = true;

  for (auto *Cur : ASVector) {
    // If Cur was already forwarding, just forward to the new AS instead.
    AliasSet *FwdTo = Cur->Forward;
    if (FwdTo) {
      Cur->Forward = AliasAnyAS;
      AliasAnyAS->addRef();
      FwdTo->dropRef(*this);
      continue;
    }

    // Otherwise, perform the actual merge.
    AliasAnyAS->mergeSetIn(*Cur, *this, AA);
  }

  return *AliasAnyAS;
}

AliasSet &AliasSetTracker::addMemoryLocation(MemoryLocation Loc,
                                             AliasSet::AccessLattice E) {
  AliasSet &AS = getAliasSetFor(Loc);
  AS.Access |= E;

  if (!AliasAnyAS && (TotalAliasSetSize > SaturationThreshold)) {
    // The AST is now saturated. From here on, we conservatively consider all
    // elements to alias each-other.
    return mergeAllAliasSets();
  }

  return AS;
}

//===----------------------------------------------------------------------===//
//               AliasSet/AliasSetTracker Printing Support
//===----------------------------------------------------------------------===//

void AliasSet::print(raw_ostream &OS) const {
  OS << "  AliasSet[" << (const void*)this << ", " << RefCount << "] ";
  OS << (Alias == SetMustAlias ? "must" : "may") << " alias, ";
  switch (Access) {
  case NoAccess:     OS << "No access "; break;
  case RefAccess:    OS << "Ref       "; break;
  case ModAccess:    OS << "Mod       "; break;
  case ModRefAccess: OS << "Mod/Ref   "; break;
  default: llvm_unreachable("Bad value for Access!");
  }
  if (Forward)
    OS << " forwarding to " << (void*)Forward;

  if (!MemoryLocs.empty()) {
    ListSeparator LS;
    OS << "Memory locations: ";
    for (const MemoryLocation &MemLoc : MemoryLocs) {
      OS << LS;
      MemLoc.Ptr->printAsOperand(OS << "(");
      if (MemLoc.Size == LocationSize::afterPointer())
        OS << ", unknown after)";
      else if (MemLoc.Size == LocationSize::beforeOrAfterPointer())
        OS << ", unknown before-or-after)";
      else
        OS << ", " << MemLoc.Size << ")";
    }
  }
  if (!UnknownInsts.empty()) {
    ListSeparator LS;
    OS << "\n    " << UnknownInsts.size() << " Unknown instructions: ";
    for (Instruction *I : UnknownInsts) {
      OS << LS;
      if (I->hasName())
        I->printAsOperand(OS);
      else
        I->print(OS);
    }
  }
  OS << "\n";
}

void AliasSetTracker::print(raw_ostream &OS) const {
  OS << "Alias Set Tracker: " << AliasSets.size();
  if (AliasAnyAS)
    OS << " (Saturated)";
  OS << " alias sets for " << PointerMap.size() << " pointer values.\n";
  for (const AliasSet &AS : *this)
    AS.print(OS);
  OS << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void AliasSet::dump() const { print(dbgs()); }
LLVM_DUMP_METHOD void AliasSetTracker::dump() const { print(dbgs()); }
#endif

//===----------------------------------------------------------------------===//
//                            AliasSetPrinter Pass
//===----------------------------------------------------------------------===//

AliasSetsPrinterPass::AliasSetsPrinterPass(raw_ostream &OS) : OS(OS) {}

PreservedAnalyses AliasSetsPrinterPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  auto &AA = AM.getResult<AAManager>(F);
  BatchAAResults BatchAA(AA);
  AliasSetTracker Tracker(BatchAA);
  OS << "Alias sets for function '" << F.getName() << "':\n";
  for (Instruction &I : instructions(F))
    Tracker.add(&I);
  Tracker.print(OS);
  return PreservedAnalyses::all();
}
