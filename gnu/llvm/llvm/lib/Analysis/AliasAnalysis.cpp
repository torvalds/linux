//==- AliasAnalysis.cpp - Generic Alias Analysis Interface Implementation --==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the generic AliasAnalysis interface which is used as the
// common interface used by all clients and implementations of alias analysis.
//
// This file also implements the default version of the AliasAnalysis interface
// that is to be used when no other implementation is specified.  This does some
// simple tests that detect obvious cases: two different global pointers cannot
// alias, a global cannot alias a malloc, two different mallocs cannot alias,
// etc.
//
// This alias analysis implementation really isn't very good for anything, but
// it is very fast, and makes a nice clean default implementation.  Because it
// handles lots of little corner cases, other, more complex, alias analysis
// implementations may choose to rely on this pass to resolve these simple and
// easy cases.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ObjCARCAliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>

#define DEBUG_TYPE "aa"

using namespace llvm;

STATISTIC(NumNoAlias,   "Number of NoAlias results");
STATISTIC(NumMayAlias,  "Number of MayAlias results");
STATISTIC(NumMustAlias, "Number of MustAlias results");

namespace llvm {
/// Allow disabling BasicAA from the AA results. This is particularly useful
/// when testing to isolate a single AA implementation.
cl::opt<bool> DisableBasicAA("disable-basic-aa", cl::Hidden, cl::init(false));
} // namespace llvm

#ifndef NDEBUG
/// Print a trace of alias analysis queries and their results.
static cl::opt<bool> EnableAATrace("aa-trace", cl::Hidden, cl::init(false));
#else
static const bool EnableAATrace = false;
#endif

AAResults::AAResults(const TargetLibraryInfo &TLI) : TLI(TLI) {}

AAResults::AAResults(AAResults &&Arg)
    : TLI(Arg.TLI), AAs(std::move(Arg.AAs)), AADeps(std::move(Arg.AADeps)) {}

AAResults::~AAResults() {}

bool AAResults::invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv) {
  // AAResults preserves the AAManager by default, due to the stateless nature
  // of AliasAnalysis. There is no need to check whether it has been preserved
  // explicitly. Check if any module dependency was invalidated and caused the
  // AAManager to be invalidated. Invalidate ourselves in that case.
  auto PAC = PA.getChecker<AAManager>();
  if (!PAC.preservedWhenStateless())
    return true;

  // Check if any of the function dependencies were invalidated, and invalidate
  // ourselves in that case.
  for (AnalysisKey *ID : AADeps)
    if (Inv.invalidate(ID, F, PA))
      return true;

  // Everything we depend on is still fine, so are we. Nothing to invalidate.
  return false;
}

//===----------------------------------------------------------------------===//
// Default chaining methods
//===----------------------------------------------------------------------===//

AliasResult AAResults::alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB) {
  SimpleAAQueryInfo AAQIP(*this);
  return alias(LocA, LocB, AAQIP, nullptr);
}

AliasResult AAResults::alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB, AAQueryInfo &AAQI,
                             const Instruction *CtxI) {
  AliasResult Result = AliasResult::MayAlias;

  if (EnableAATrace) {
    for (unsigned I = 0; I < AAQI.Depth; ++I)
      dbgs() << "  ";
    dbgs() << "Start " << *LocA.Ptr << " @ " << LocA.Size << ", "
           << *LocB.Ptr << " @ " << LocB.Size << "\n";
  }

  AAQI.Depth++;
  for (const auto &AA : AAs) {
    Result = AA->alias(LocA, LocB, AAQI, CtxI);
    if (Result != AliasResult::MayAlias)
      break;
  }
  AAQI.Depth--;

  if (EnableAATrace) {
    for (unsigned I = 0; I < AAQI.Depth; ++I)
      dbgs() << "  ";
    dbgs() << "End " << *LocA.Ptr << " @ " << LocA.Size << ", "
           << *LocB.Ptr << " @ " << LocB.Size << " = " << Result << "\n";
  }

  if (AAQI.Depth == 0) {
    if (Result == AliasResult::NoAlias)
      ++NumNoAlias;
    else if (Result == AliasResult::MustAlias)
      ++NumMustAlias;
    else
      ++NumMayAlias;
  }
  return Result;
}

ModRefInfo AAResults::getModRefInfoMask(const MemoryLocation &Loc,
                                        bool IgnoreLocals) {
  SimpleAAQueryInfo AAQIP(*this);
  return getModRefInfoMask(Loc, AAQIP, IgnoreLocals);
}

ModRefInfo AAResults::getModRefInfoMask(const MemoryLocation &Loc,
                                        AAQueryInfo &AAQI, bool IgnoreLocals) {
  ModRefInfo Result = ModRefInfo::ModRef;

  for (const auto &AA : AAs) {
    Result &= AA->getModRefInfoMask(Loc, AAQI, IgnoreLocals);

    // Early-exit the moment we reach the bottom of the lattice.
    if (isNoModRef(Result))
      return ModRefInfo::NoModRef;
  }

  return Result;
}

ModRefInfo AAResults::getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) {
  ModRefInfo Result = ModRefInfo::ModRef;

  for (const auto &AA : AAs) {
    Result &= AA->getArgModRefInfo(Call, ArgIdx);

    // Early-exit the moment we reach the bottom of the lattice.
    if (isNoModRef(Result))
      return ModRefInfo::NoModRef;
  }

  return Result;
}

ModRefInfo AAResults::getModRefInfo(const Instruction *I,
                                    const CallBase *Call2) {
  SimpleAAQueryInfo AAQIP(*this);
  return getModRefInfo(I, Call2, AAQIP);
}

ModRefInfo AAResults::getModRefInfo(const Instruction *I, const CallBase *Call2,
                                    AAQueryInfo &AAQI) {
  // We may have two calls.
  if (const auto *Call1 = dyn_cast<CallBase>(I)) {
    // Check if the two calls modify the same memory.
    return getModRefInfo(Call1, Call2, AAQI);
  }
  // If this is a fence, just return ModRef.
  if (I->isFenceLike())
    return ModRefInfo::ModRef;
  // Otherwise, check if the call modifies or references the
  // location this memory access defines.  The best we can say
  // is that if the call references what this instruction
  // defines, it must be clobbered by this location.
  const MemoryLocation DefLoc = MemoryLocation::get(I);
  ModRefInfo MR = getModRefInfo(Call2, DefLoc, AAQI);
  if (isModOrRefSet(MR))
    return ModRefInfo::ModRef;
  return ModRefInfo::NoModRef;
}

ModRefInfo AAResults::getModRefInfo(const CallBase *Call,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  ModRefInfo Result = ModRefInfo::ModRef;

  for (const auto &AA : AAs) {
    Result &= AA->getModRefInfo(Call, Loc, AAQI);

    // Early-exit the moment we reach the bottom of the lattice.
    if (isNoModRef(Result))
      return ModRefInfo::NoModRef;
  }

  // Try to refine the mod-ref info further using other API entry points to the
  // aggregate set of AA results.

  // We can completely ignore inaccessible memory here, because MemoryLocations
  // can only reference accessible memory.
  auto ME = getMemoryEffects(Call, AAQI)
                .getWithoutLoc(IRMemLocation::InaccessibleMem);
  if (ME.doesNotAccessMemory())
    return ModRefInfo::NoModRef;

  ModRefInfo ArgMR = ME.getModRef(IRMemLocation::ArgMem);
  ModRefInfo OtherMR = ME.getWithoutLoc(IRMemLocation::ArgMem).getModRef();
  if ((ArgMR | OtherMR) != OtherMR) {
    // Refine the modref info for argument memory. We only bother to do this
    // if ArgMR is not a subset of OtherMR, otherwise this won't have an impact
    // on the final result.
    ModRefInfo AllArgsMask = ModRefInfo::NoModRef;
    for (const auto &I : llvm::enumerate(Call->args())) {
      const Value *Arg = I.value();
      if (!Arg->getType()->isPointerTy())
        continue;
      unsigned ArgIdx = I.index();
      MemoryLocation ArgLoc = MemoryLocation::getForArgument(Call, ArgIdx, TLI);
      AliasResult ArgAlias = alias(ArgLoc, Loc, AAQI, Call);
      if (ArgAlias != AliasResult::NoAlias)
        AllArgsMask |= getArgModRefInfo(Call, ArgIdx);
    }
    ArgMR &= AllArgsMask;
  }

  Result &= ArgMR | OtherMR;

  // Apply the ModRef mask. This ensures that if Loc is a constant memory
  // location, we take into account the fact that the call definitely could not
  // modify the memory location.
  if (!isNoModRef(Result))
    Result &= getModRefInfoMask(Loc);

  return Result;
}

ModRefInfo AAResults::getModRefInfo(const CallBase *Call1,
                                    const CallBase *Call2, AAQueryInfo &AAQI) {
  ModRefInfo Result = ModRefInfo::ModRef;

  for (const auto &AA : AAs) {
    Result &= AA->getModRefInfo(Call1, Call2, AAQI);

    // Early-exit the moment we reach the bottom of the lattice.
    if (isNoModRef(Result))
      return ModRefInfo::NoModRef;
  }

  // Try to refine the mod-ref info further using other API entry points to the
  // aggregate set of AA results.

  // If Call1 or Call2 are readnone, they don't interact.
  auto Call1B = getMemoryEffects(Call1, AAQI);
  if (Call1B.doesNotAccessMemory())
    return ModRefInfo::NoModRef;

  auto Call2B = getMemoryEffects(Call2, AAQI);
  if (Call2B.doesNotAccessMemory())
    return ModRefInfo::NoModRef;

  // If they both only read from memory, there is no dependence.
  if (Call1B.onlyReadsMemory() && Call2B.onlyReadsMemory())
    return ModRefInfo::NoModRef;

  // If Call1 only reads memory, the only dependence on Call2 can be
  // from Call1 reading memory written by Call2.
  if (Call1B.onlyReadsMemory())
    Result &= ModRefInfo::Ref;
  else if (Call1B.onlyWritesMemory())
    Result &= ModRefInfo::Mod;

  // If Call2 only access memory through arguments, accumulate the mod/ref
  // information from Call1's references to the memory referenced by
  // Call2's arguments.
  if (Call2B.onlyAccessesArgPointees()) {
    if (!Call2B.doesAccessArgPointees())
      return ModRefInfo::NoModRef;
    ModRefInfo R = ModRefInfo::NoModRef;
    for (auto I = Call2->arg_begin(), E = Call2->arg_end(); I != E; ++I) {
      const Value *Arg = *I;
      if (!Arg->getType()->isPointerTy())
        continue;
      unsigned Call2ArgIdx = std::distance(Call2->arg_begin(), I);
      auto Call2ArgLoc =
          MemoryLocation::getForArgument(Call2, Call2ArgIdx, TLI);

      // ArgModRefC2 indicates what Call2 might do to Call2ArgLoc, and the
      // dependence of Call1 on that location is the inverse:
      // - If Call2 modifies location, dependence exists if Call1 reads or
      //   writes.
      // - If Call2 only reads location, dependence exists if Call1 writes.
      ModRefInfo ArgModRefC2 = getArgModRefInfo(Call2, Call2ArgIdx);
      ModRefInfo ArgMask = ModRefInfo::NoModRef;
      if (isModSet(ArgModRefC2))
        ArgMask = ModRefInfo::ModRef;
      else if (isRefSet(ArgModRefC2))
        ArgMask = ModRefInfo::Mod;

      // ModRefC1 indicates what Call1 might do to Call2ArgLoc, and we use
      // above ArgMask to update dependence info.
      ArgMask &= getModRefInfo(Call1, Call2ArgLoc, AAQI);

      R = (R | ArgMask) & Result;
      if (R == Result)
        break;
    }

    return R;
  }

  // If Call1 only accesses memory through arguments, check if Call2 references
  // any of the memory referenced by Call1's arguments. If not, return NoModRef.
  if (Call1B.onlyAccessesArgPointees()) {
    if (!Call1B.doesAccessArgPointees())
      return ModRefInfo::NoModRef;
    ModRefInfo R = ModRefInfo::NoModRef;
    for (auto I = Call1->arg_begin(), E = Call1->arg_end(); I != E; ++I) {
      const Value *Arg = *I;
      if (!Arg->getType()->isPointerTy())
        continue;
      unsigned Call1ArgIdx = std::distance(Call1->arg_begin(), I);
      auto Call1ArgLoc =
          MemoryLocation::getForArgument(Call1, Call1ArgIdx, TLI);

      // ArgModRefC1 indicates what Call1 might do to Call1ArgLoc; if Call1
      // might Mod Call1ArgLoc, then we care about either a Mod or a Ref by
      // Call2. If Call1 might Ref, then we care only about a Mod by Call2.
      ModRefInfo ArgModRefC1 = getArgModRefInfo(Call1, Call1ArgIdx);
      ModRefInfo ModRefC2 = getModRefInfo(Call2, Call1ArgLoc, AAQI);
      if ((isModSet(ArgModRefC1) && isModOrRefSet(ModRefC2)) ||
          (isRefSet(ArgModRefC1) && isModSet(ModRefC2)))
        R = (R | ArgModRefC1) & Result;

      if (R == Result)
        break;
    }

    return R;
  }

  return Result;
}

MemoryEffects AAResults::getMemoryEffects(const CallBase *Call,
                                          AAQueryInfo &AAQI) {
  MemoryEffects Result = MemoryEffects::unknown();

  for (const auto &AA : AAs) {
    Result &= AA->getMemoryEffects(Call, AAQI);

    // Early-exit the moment we reach the bottom of the lattice.
    if (Result.doesNotAccessMemory())
      return Result;
  }

  return Result;
}

MemoryEffects AAResults::getMemoryEffects(const CallBase *Call) {
  SimpleAAQueryInfo AAQI(*this);
  return getMemoryEffects(Call, AAQI);
}

MemoryEffects AAResults::getMemoryEffects(const Function *F) {
  MemoryEffects Result = MemoryEffects::unknown();

  for (const auto &AA : AAs) {
    Result &= AA->getMemoryEffects(F);

    // Early-exit the moment we reach the bottom of the lattice.
    if (Result.doesNotAccessMemory())
      return Result;
  }

  return Result;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, AliasResult AR) {
  switch (AR) {
  case AliasResult::NoAlias:
    OS << "NoAlias";
    break;
  case AliasResult::MustAlias:
    OS << "MustAlias";
    break;
  case AliasResult::MayAlias:
    OS << "MayAlias";
    break;
  case AliasResult::PartialAlias:
    OS << "PartialAlias";
    if (AR.hasOffset())
      OS << " (off " << AR.getOffset() << ")";
    break;
  }
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, ModRefInfo MR) {
  switch (MR) {
  case ModRefInfo::NoModRef:
    OS << "NoModRef";
    break;
  case ModRefInfo::Ref:
    OS << "Ref";
    break;
  case ModRefInfo::Mod:
    OS << "Mod";
    break;
  case ModRefInfo::ModRef:
    OS << "ModRef";
    break;
  }
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, MemoryEffects ME) {
  for (IRMemLocation Loc : MemoryEffects::locations()) {
    switch (Loc) {
    case IRMemLocation::ArgMem:
      OS << "ArgMem: ";
      break;
    case IRMemLocation::InaccessibleMem:
      OS << "InaccessibleMem: ";
      break;
    case IRMemLocation::Other:
      OS << "Other: ";
      break;
    }
    OS << ME.getModRef(Loc) << ", ";
  }
  return OS;
}

//===----------------------------------------------------------------------===//
// Helper method implementation
//===----------------------------------------------------------------------===//

ModRefInfo AAResults::getModRefInfo(const LoadInst *L,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  // Be conservative in the face of atomic.
  if (isStrongerThan(L->getOrdering(), AtomicOrdering::Unordered))
    return ModRefInfo::ModRef;

  // If the load address doesn't alias the given address, it doesn't read
  // or write the specified memory.
  if (Loc.Ptr) {
    AliasResult AR = alias(MemoryLocation::get(L), Loc, AAQI, L);
    if (AR == AliasResult::NoAlias)
      return ModRefInfo::NoModRef;
  }
  // Otherwise, a load just reads.
  return ModRefInfo::Ref;
}

ModRefInfo AAResults::getModRefInfo(const StoreInst *S,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  // Be conservative in the face of atomic.
  if (isStrongerThan(S->getOrdering(), AtomicOrdering::Unordered))
    return ModRefInfo::ModRef;

  if (Loc.Ptr) {
    AliasResult AR = alias(MemoryLocation::get(S), Loc, AAQI, S);
    // If the store address cannot alias the pointer in question, then the
    // specified memory cannot be modified by the store.
    if (AR == AliasResult::NoAlias)
      return ModRefInfo::NoModRef;

    // Examine the ModRef mask. If Mod isn't present, then return NoModRef.
    // This ensures that if Loc is a constant memory location, we take into
    // account the fact that the store definitely could not modify the memory
    // location.
    if (!isModSet(getModRefInfoMask(Loc)))
      return ModRefInfo::NoModRef;
  }

  // Otherwise, a store just writes.
  return ModRefInfo::Mod;
}

ModRefInfo AAResults::getModRefInfo(const FenceInst *S,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  // All we know about a fence instruction is what we get from the ModRef
  // mask: if Loc is a constant memory location, the fence definitely could
  // not modify it.
  if (Loc.Ptr)
    return getModRefInfoMask(Loc);
  return ModRefInfo::ModRef;
}

ModRefInfo AAResults::getModRefInfo(const VAArgInst *V,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  if (Loc.Ptr) {
    AliasResult AR = alias(MemoryLocation::get(V), Loc, AAQI, V);
    // If the va_arg address cannot alias the pointer in question, then the
    // specified memory cannot be accessed by the va_arg.
    if (AR == AliasResult::NoAlias)
      return ModRefInfo::NoModRef;

    // If the pointer is a pointer to invariant memory, then it could not have
    // been modified by this va_arg.
    return getModRefInfoMask(Loc, AAQI);
  }

  // Otherwise, a va_arg reads and writes.
  return ModRefInfo::ModRef;
}

ModRefInfo AAResults::getModRefInfo(const CatchPadInst *CatchPad,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  if (Loc.Ptr) {
    // If the pointer is a pointer to invariant memory,
    // then it could not have been modified by this catchpad.
    return getModRefInfoMask(Loc, AAQI);
  }

  // Otherwise, a catchpad reads and writes.
  return ModRefInfo::ModRef;
}

ModRefInfo AAResults::getModRefInfo(const CatchReturnInst *CatchRet,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  if (Loc.Ptr) {
    // If the pointer is a pointer to invariant memory,
    // then it could not have been modified by this catchpad.
    return getModRefInfoMask(Loc, AAQI);
  }

  // Otherwise, a catchret reads and writes.
  return ModRefInfo::ModRef;
}

ModRefInfo AAResults::getModRefInfo(const AtomicCmpXchgInst *CX,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  // Acquire/Release cmpxchg has properties that matter for arbitrary addresses.
  if (isStrongerThanMonotonic(CX->getSuccessOrdering()))
    return ModRefInfo::ModRef;

  if (Loc.Ptr) {
    AliasResult AR = alias(MemoryLocation::get(CX), Loc, AAQI, CX);
    // If the cmpxchg address does not alias the location, it does not access
    // it.
    if (AR == AliasResult::NoAlias)
      return ModRefInfo::NoModRef;
  }

  return ModRefInfo::ModRef;
}

ModRefInfo AAResults::getModRefInfo(const AtomicRMWInst *RMW,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI) {
  // Acquire/Release atomicrmw has properties that matter for arbitrary addresses.
  if (isStrongerThanMonotonic(RMW->getOrdering()))
    return ModRefInfo::ModRef;

  if (Loc.Ptr) {
    AliasResult AR = alias(MemoryLocation::get(RMW), Loc, AAQI, RMW);
    // If the atomicrmw address does not alias the location, it does not access
    // it.
    if (AR == AliasResult::NoAlias)
      return ModRefInfo::NoModRef;
  }

  return ModRefInfo::ModRef;
}

ModRefInfo AAResults::getModRefInfo(const Instruction *I,
                                    const std::optional<MemoryLocation> &OptLoc,
                                    AAQueryInfo &AAQIP) {
  if (OptLoc == std::nullopt) {
    if (const auto *Call = dyn_cast<CallBase>(I))
      return getMemoryEffects(Call, AAQIP).getModRef();
  }

  const MemoryLocation &Loc = OptLoc.value_or(MemoryLocation());

  switch (I->getOpcode()) {
  case Instruction::VAArg:
    return getModRefInfo((const VAArgInst *)I, Loc, AAQIP);
  case Instruction::Load:
    return getModRefInfo((const LoadInst *)I, Loc, AAQIP);
  case Instruction::Store:
    return getModRefInfo((const StoreInst *)I, Loc, AAQIP);
  case Instruction::Fence:
    return getModRefInfo((const FenceInst *)I, Loc, AAQIP);
  case Instruction::AtomicCmpXchg:
    return getModRefInfo((const AtomicCmpXchgInst *)I, Loc, AAQIP);
  case Instruction::AtomicRMW:
    return getModRefInfo((const AtomicRMWInst *)I, Loc, AAQIP);
  case Instruction::Call:
  case Instruction::CallBr:
  case Instruction::Invoke:
    return getModRefInfo((const CallBase *)I, Loc, AAQIP);
  case Instruction::CatchPad:
    return getModRefInfo((const CatchPadInst *)I, Loc, AAQIP);
  case Instruction::CatchRet:
    return getModRefInfo((const CatchReturnInst *)I, Loc, AAQIP);
  default:
    assert(!I->mayReadOrWriteMemory() &&
           "Unhandled memory access instruction!");
    return ModRefInfo::NoModRef;
  }
}

/// Return information about whether a particular call site modifies
/// or reads the specified memory location \p MemLoc before instruction \p I
/// in a BasicBlock.
/// FIXME: this is really just shoring-up a deficiency in alias analysis.
/// BasicAA isn't willing to spend linear time determining whether an alloca
/// was captured before or after this particular call, while we are. However,
/// with a smarter AA in place, this test is just wasting compile time.
ModRefInfo AAResults::callCapturesBefore(const Instruction *I,
                                         const MemoryLocation &MemLoc,
                                         DominatorTree *DT,
                                         AAQueryInfo &AAQI) {
  if (!DT)
    return ModRefInfo::ModRef;

  const Value *Object = getUnderlyingObject(MemLoc.Ptr);
  if (!isIdentifiedFunctionLocal(Object))
    return ModRefInfo::ModRef;

  const auto *Call = dyn_cast<CallBase>(I);
  if (!Call || Call == Object)
    return ModRefInfo::ModRef;

  if (PointerMayBeCapturedBefore(Object, /* ReturnCaptures */ true,
                                 /* StoreCaptures */ true, I, DT,
                                 /* include Object */ true))
    return ModRefInfo::ModRef;

  unsigned ArgNo = 0;
  ModRefInfo R = ModRefInfo::NoModRef;
  // Set flag only if no May found and all operands processed.
  for (auto CI = Call->data_operands_begin(), CE = Call->data_operands_end();
       CI != CE; ++CI, ++ArgNo) {
    // Only look at the no-capture or byval pointer arguments.  If this
    // pointer were passed to arguments that were neither of these, then it
    // couldn't be no-capture.
    if (!(*CI)->getType()->isPointerTy() ||
        (!Call->doesNotCapture(ArgNo) && ArgNo < Call->arg_size() &&
         !Call->isByValArgument(ArgNo)))
      continue;

    AliasResult AR =
        alias(MemoryLocation::getBeforeOrAfter(*CI),
              MemoryLocation::getBeforeOrAfter(Object), AAQI, Call);
    // If this is a no-capture pointer argument, see if we can tell that it
    // is impossible to alias the pointer we're checking.  If not, we have to
    // assume that the call could touch the pointer, even though it doesn't
    // escape.
    if (AR == AliasResult::NoAlias)
      continue;
    if (Call->doesNotAccessMemory(ArgNo))
      continue;
    if (Call->onlyReadsMemory(ArgNo)) {
      R = ModRefInfo::Ref;
      continue;
    }
    return ModRefInfo::ModRef;
  }
  return R;
}

/// canBasicBlockModify - Return true if it is possible for execution of the
/// specified basic block to modify the location Loc.
///
bool AAResults::canBasicBlockModify(const BasicBlock &BB,
                                    const MemoryLocation &Loc) {
  return canInstructionRangeModRef(BB.front(), BB.back(), Loc, ModRefInfo::Mod);
}

/// canInstructionRangeModRef - Return true if it is possible for the
/// execution of the specified instructions to mod\ref (according to the
/// mode) the location Loc. The instructions to consider are all
/// of the instructions in the range of [I1,I2] INCLUSIVE.
/// I1 and I2 must be in the same basic block.
bool AAResults::canInstructionRangeModRef(const Instruction &I1,
                                          const Instruction &I2,
                                          const MemoryLocation &Loc,
                                          const ModRefInfo Mode) {
  assert(I1.getParent() == I2.getParent() &&
         "Instructions not in same basic block!");
  BasicBlock::const_iterator I = I1.getIterator();
  BasicBlock::const_iterator E = I2.getIterator();
  ++E;  // Convert from inclusive to exclusive range.

  for (; I != E; ++I) // Check every instruction in range
    if (isModOrRefSet(getModRefInfo(&*I, Loc) & Mode))
      return true;
  return false;
}

// Provide a definition for the root virtual destructor.
AAResults::Concept::~Concept() = default;

// Provide a definition for the static object used to identify passes.
AnalysisKey AAManager::Key;

ExternalAAWrapperPass::ExternalAAWrapperPass() : ImmutablePass(ID) {
  initializeExternalAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

ExternalAAWrapperPass::ExternalAAWrapperPass(CallbackT CB)
    : ImmutablePass(ID), CB(std::move(CB)) {
  initializeExternalAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

char ExternalAAWrapperPass::ID = 0;

INITIALIZE_PASS(ExternalAAWrapperPass, "external-aa", "External Alias Analysis",
                false, true)

ImmutablePass *
llvm::createExternalAAWrapperPass(ExternalAAWrapperPass::CallbackT Callback) {
  return new ExternalAAWrapperPass(std::move(Callback));
}

AAResultsWrapperPass::AAResultsWrapperPass() : FunctionPass(ID) {
  initializeAAResultsWrapperPassPass(*PassRegistry::getPassRegistry());
}

char AAResultsWrapperPass::ID = 0;

INITIALIZE_PASS_BEGIN(AAResultsWrapperPass, "aa",
                      "Function Alias Analysis Results", false, true)
INITIALIZE_PASS_DEPENDENCY(BasicAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ExternalAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(SCEVAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScopedNoAliasAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TypeBasedAAWrapperPass)
INITIALIZE_PASS_END(AAResultsWrapperPass, "aa",
                    "Function Alias Analysis Results", false, true)

/// Run the wrapper pass to rebuild an aggregation over known AA passes.
///
/// This is the legacy pass manager's interface to the new-style AA results
/// aggregation object. Because this is somewhat shoe-horned into the legacy
/// pass manager, we hard code all the specific alias analyses available into
/// it. While the particular set enabled is configured via commandline flags,
/// adding a new alias analysis to LLVM will require adding support for it to
/// this list.
bool AAResultsWrapperPass::runOnFunction(Function &F) {
  // NB! This *must* be reset before adding new AA results to the new
  // AAResults object because in the legacy pass manager, each instance
  // of these will refer to the *same* immutable analyses, registering and
  // unregistering themselves with them. We need to carefully tear down the
  // previous object first, in this case replacing it with an empty one, before
  // registering new results.
  AAR.reset(
      new AAResults(getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F)));

  // BasicAA is always available for function analyses. Also, we add it first
  // so that it can trump TBAA results when it proves MustAlias.
  // FIXME: TBAA should have an explicit mode to support this and then we
  // should reconsider the ordering here.
  if (!DisableBasicAA)
    AAR->addAAResult(getAnalysis<BasicAAWrapperPass>().getResult());

  // Populate the results with the currently available AAs.
  if (auto *WrapperPass = getAnalysisIfAvailable<ScopedNoAliasAAWrapperPass>())
    AAR->addAAResult(WrapperPass->getResult());
  if (auto *WrapperPass = getAnalysisIfAvailable<TypeBasedAAWrapperPass>())
    AAR->addAAResult(WrapperPass->getResult());
  if (auto *WrapperPass = getAnalysisIfAvailable<GlobalsAAWrapperPass>())
    AAR->addAAResult(WrapperPass->getResult());
  if (auto *WrapperPass = getAnalysisIfAvailable<SCEVAAWrapperPass>())
    AAR->addAAResult(WrapperPass->getResult());

  // If available, run an external AA providing callback over the results as
  // well.
  if (auto *WrapperPass = getAnalysisIfAvailable<ExternalAAWrapperPass>())
    if (WrapperPass->CB)
      WrapperPass->CB(*this, F, *AAR);

  // Analyses don't mutate the IR, so return false.
  return false;
}

void AAResultsWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<BasicAAWrapperPass>();
  AU.addRequiredTransitive<TargetLibraryInfoWrapperPass>();

  // We also need to mark all the alias analysis passes we will potentially
  // probe in runOnFunction as used here to ensure the legacy pass manager
  // preserves them. This hard coding of lists of alias analyses is specific to
  // the legacy pass manager.
  AU.addUsedIfAvailable<ScopedNoAliasAAWrapperPass>();
  AU.addUsedIfAvailable<TypeBasedAAWrapperPass>();
  AU.addUsedIfAvailable<GlobalsAAWrapperPass>();
  AU.addUsedIfAvailable<SCEVAAWrapperPass>();
  AU.addUsedIfAvailable<ExternalAAWrapperPass>();
}

AAManager::Result AAManager::run(Function &F, FunctionAnalysisManager &AM) {
  Result R(AM.getResult<TargetLibraryAnalysis>(F));
  for (auto &Getter : ResultGetters)
    (*Getter)(F, AM, R);
  return R;
}

bool llvm::isNoAliasCall(const Value *V) {
  if (const auto *Call = dyn_cast<CallBase>(V))
    return Call->hasRetAttr(Attribute::NoAlias);
  return false;
}

static bool isNoAliasOrByValArgument(const Value *V) {
  if (const Argument *A = dyn_cast<Argument>(V))
    return A->hasNoAliasAttr() || A->hasByValAttr();
  return false;
}

bool llvm::isIdentifiedObject(const Value *V) {
  if (isa<AllocaInst>(V))
    return true;
  if (isa<GlobalValue>(V) && !isa<GlobalAlias>(V))
    return true;
  if (isNoAliasCall(V))
    return true;
  if (isNoAliasOrByValArgument(V))
    return true;
  return false;
}

bool llvm::isIdentifiedFunctionLocal(const Value *V) {
  return isa<AllocaInst>(V) || isNoAliasCall(V) || isNoAliasOrByValArgument(V);
}

bool llvm::isEscapeSource(const Value *V) {
  if (auto *CB = dyn_cast<CallBase>(V))
    return !isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(CB,
                                                                        true);

  // The load case works because isNonEscapingLocalObject considers all
  // stores to be escapes (it passes true for the StoreCaptures argument
  // to PointerMayBeCaptured).
  if (isa<LoadInst>(V))
    return true;

  // The inttoptr case works because isNonEscapingLocalObject considers all
  // means of converting or equating a pointer to an int (ptrtoint, ptr store
  // which could be followed by an integer load, ptr<->int compare) as
  // escaping, and objects located at well-known addresses via platform-specific
  // means cannot be considered non-escaping local objects.
  if (isa<IntToPtrInst>(V))
    return true;

  // Same for inttoptr constant expressions.
  if (auto *CE = dyn_cast<ConstantExpr>(V))
    if (CE->getOpcode() == Instruction::IntToPtr)
      return true;

  return false;
}

bool llvm::isNotVisibleOnUnwind(const Value *Object,
                                bool &RequiresNoCaptureBeforeUnwind) {
  RequiresNoCaptureBeforeUnwind = false;

  // Alloca goes out of scope on unwind.
  if (isa<AllocaInst>(Object))
    return true;

  // Byval goes out of scope on unwind.
  if (auto *A = dyn_cast<Argument>(Object))
    return A->hasByValAttr() || A->hasAttribute(Attribute::DeadOnUnwind);

  // A noalias return is not accessible from any other code. If the pointer
  // does not escape prior to the unwind, then the caller cannot access the
  // memory either.
  if (isNoAliasCall(Object)) {
    RequiresNoCaptureBeforeUnwind = true;
    return true;
  }

  return false;
}

// We don't consider globals as writable: While the physical memory is writable,
// we may not have provenance to perform the write.
bool llvm::isWritableObject(const Value *Object,
                            bool &ExplicitlyDereferenceableOnly) {
  ExplicitlyDereferenceableOnly = false;

  // TODO: Alloca might not be writable after its lifetime ends.
  // See https://github.com/llvm/llvm-project/issues/51838.
  if (isa<AllocaInst>(Object))
    return true;

  if (auto *A = dyn_cast<Argument>(Object)) {
    if (A->hasAttribute(Attribute::Writable)) {
      ExplicitlyDereferenceableOnly = true;
      return true;
    }

    return A->hasByValAttr();
  }

  // TODO: Noalias shouldn't imply writability, this should check for an
  // allocator function instead.
  return isNoAliasCall(Object);
}
