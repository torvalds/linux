//===-- DebugLoc.cpp - Implement DebugLoc class ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DebugLoc.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfo.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
// DebugLoc Implementation
//===----------------------------------------------------------------------===//
DebugLoc::DebugLoc(const DILocation *L) : Loc(const_cast<DILocation *>(L)) {}
DebugLoc::DebugLoc(const MDNode *L) : Loc(const_cast<MDNode *>(L)) {}

DILocation *DebugLoc::get() const {
  return cast_or_null<DILocation>(Loc.get());
}

unsigned DebugLoc::getLine() const {
  assert(get() && "Expected valid DebugLoc");
  return get()->getLine();
}

unsigned DebugLoc::getCol() const {
  assert(get() && "Expected valid DebugLoc");
  return get()->getColumn();
}

MDNode *DebugLoc::getScope() const {
  assert(get() && "Expected valid DebugLoc");
  return get()->getScope();
}

DILocation *DebugLoc::getInlinedAt() const {
  assert(get() && "Expected valid DebugLoc");
  return get()->getInlinedAt();
}

MDNode *DebugLoc::getInlinedAtScope() const {
  return cast<DILocation>(Loc)->getInlinedAtScope();
}

DebugLoc DebugLoc::getFnDebugLoc() const {
  // FIXME: Add a method on \a DILocation that does this work.
  const MDNode *Scope = getInlinedAtScope();
  if (auto *SP = getDISubprogram(Scope))
    return DILocation::get(SP->getContext(), SP->getScopeLine(), 0, SP);

  return DebugLoc();
}

bool DebugLoc::isImplicitCode() const {
  if (DILocation *Loc = get()) {
    return Loc->isImplicitCode();
  }
  return true;
}

void DebugLoc::setImplicitCode(bool ImplicitCode) {
  if (DILocation *Loc = get()) {
    Loc->setImplicitCode(ImplicitCode);
  }
}

DebugLoc DebugLoc::replaceInlinedAtSubprogram(
    const DebugLoc &RootLoc, DISubprogram &NewSP, LLVMContext &Ctx,
    DenseMap<const MDNode *, MDNode *> &Cache) {
  SmallVector<DILocation *> LocChain;
  DILocation *CachedResult = nullptr;

  // Collect the inline chain, stopping if we find a location that has already
  // been processed.
  for (DILocation *Loc = RootLoc; Loc; Loc = Loc->getInlinedAt()) {
    if (auto It = Cache.find(Loc); It != Cache.end()) {
      CachedResult = cast<DILocation>(It->second);
      break;
    }
    LocChain.push_back(Loc);
  }

  DILocation *UpdatedLoc = CachedResult;
  if (!UpdatedLoc) {
    // If no cache hits, then back() is the end of the inline chain, that is,
    // the DILocation whose scope ends in the Subprogram to be replaced.
    DILocation *LocToUpdate = LocChain.pop_back_val();
    DIScope *NewScope = DILocalScope::cloneScopeForSubprogram(
        *LocToUpdate->getScope(), NewSP, Ctx, Cache);
    UpdatedLoc = DILocation::get(Ctx, LocToUpdate->getLine(),
                                 LocToUpdate->getColumn(), NewScope);
    Cache[LocToUpdate] = UpdatedLoc;
  }

  // Recreate the location chain, bottom-up, starting at the new scope (or a
  // cached result).
  for (const DILocation *LocToUpdate : reverse(LocChain)) {
    UpdatedLoc =
        DILocation::get(Ctx, LocToUpdate->getLine(), LocToUpdate->getColumn(),
                        LocToUpdate->getScope(), UpdatedLoc);
    Cache[LocToUpdate] = UpdatedLoc;
  }

  return UpdatedLoc;
}

DebugLoc DebugLoc::appendInlinedAt(const DebugLoc &DL, DILocation *InlinedAt,
                                   LLVMContext &Ctx,
                                   DenseMap<const MDNode *, MDNode *> &Cache) {
  SmallVector<DILocation *, 3> InlinedAtLocations;
  DILocation *Last = InlinedAt;
  DILocation *CurInlinedAt = DL;

  // Gather all the inlined-at nodes.
  while (DILocation *IA = CurInlinedAt->getInlinedAt()) {
    // Skip any we've already built nodes for.
    if (auto *Found = Cache[IA]) {
      Last = cast<DILocation>(Found);
      break;
    }

    InlinedAtLocations.push_back(IA);
    CurInlinedAt = IA;
  }

  // Starting from the top, rebuild the nodes to point to the new inlined-at
  // location (then rebuilding the rest of the chain behind it) and update the
  // map of already-constructed inlined-at nodes.
  for (const DILocation *MD : reverse(InlinedAtLocations))
    Cache[MD] = Last = DILocation::getDistinct(
        Ctx, MD->getLine(), MD->getColumn(), MD->getScope(), Last);

  return Last;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DebugLoc::dump() const { print(dbgs()); }
#endif

void DebugLoc::print(raw_ostream &OS) const {
  if (!Loc)
    return;

  // Print source line info.
  auto *Scope = cast<DIScope>(getScope());
  OS << Scope->getFilename();
  OS << ':' << getLine();
  if (getCol() != 0)
    OS << ':' << getCol();

  if (DebugLoc InlinedAtDL = getInlinedAt()) {
    OS << " @[ ";
    InlinedAtDL.print(OS);
    OS << " ]";
  }
}
