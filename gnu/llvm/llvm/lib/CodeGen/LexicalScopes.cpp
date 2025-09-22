//===- LexicalScopes.cpp - Collecting lexical scope info ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements LexicalScopes analysis.
//
// This pass collects lexical scope information and maps machine instructions
// to respective lexical scopes.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "lexicalscopes"

/// reset - Reset the instance so that it's prepared for another function.
void LexicalScopes::reset() {
  MF = nullptr;
  CurrentFnLexicalScope = nullptr;
  LexicalScopeMap.clear();
  AbstractScopeMap.clear();
  InlinedLexicalScopeMap.clear();
  AbstractScopesList.clear();
  DominatedBlocks.clear();
}

/// initialize - Scan machine function and constuct lexical scope nest.
void LexicalScopes::initialize(const MachineFunction &Fn) {
  reset();
  // Don't attempt any lexical scope creation for a NoDebug compile unit.
  if (Fn.getFunction().getSubprogram()->getUnit()->getEmissionKind() ==
      DICompileUnit::NoDebug)
    return;
  MF = &Fn;
  SmallVector<InsnRange, 4> MIRanges;
  DenseMap<const MachineInstr *, LexicalScope *> MI2ScopeMap;
  extractLexicalScopes(MIRanges, MI2ScopeMap);
  if (CurrentFnLexicalScope) {
    constructScopeNest(CurrentFnLexicalScope);
    assignInstructionRanges(MIRanges, MI2ScopeMap);
  }
}

/// extractLexicalScopes - Extract instruction ranges for each lexical scopes
/// for the given machine function.
void LexicalScopes::extractLexicalScopes(
    SmallVectorImpl<InsnRange> &MIRanges,
    DenseMap<const MachineInstr *, LexicalScope *> &MI2ScopeMap) {
  // Scan each instruction and create scopes. First build working set of scopes.
  for (const auto &MBB : *MF) {
    const MachineInstr *RangeBeginMI = nullptr;
    const MachineInstr *PrevMI = nullptr;
    const DILocation *PrevDL = nullptr;
    for (const auto &MInsn : MBB) {
      // Ignore DBG_VALUE and similar instruction that do not contribute to any
      // instruction in the output.
      if (MInsn.isMetaInstruction())
        continue;

      // Check if instruction has valid location information.
      const DILocation *MIDL = MInsn.getDebugLoc();
      if (!MIDL) {
        PrevMI = &MInsn;
        continue;
      }

      // If scope has not changed then skip this instruction.
      if (MIDL == PrevDL) {
        PrevMI = &MInsn;
        continue;
      }

      if (RangeBeginMI) {
        // If we have already seen a beginning of an instruction range and
        // current instruction scope does not match scope of first instruction
        // in this range then create a new instruction range.
        InsnRange R(RangeBeginMI, PrevMI);
        MI2ScopeMap[RangeBeginMI] = getOrCreateLexicalScope(PrevDL);
        MIRanges.push_back(R);
      }

      // This is a beginning of a new instruction range.
      RangeBeginMI = &MInsn;

      // Reset previous markers.
      PrevMI = &MInsn;
      PrevDL = MIDL;
    }

    // Create last instruction range.
    if (RangeBeginMI && PrevMI && PrevDL) {
      InsnRange R(RangeBeginMI, PrevMI);
      MIRanges.push_back(R);
      MI2ScopeMap[RangeBeginMI] = getOrCreateLexicalScope(PrevDL);
    }
  }
}

/// findLexicalScope - Find lexical scope, either regular or inlined, for the
/// given DebugLoc. Return NULL if not found.
LexicalScope *LexicalScopes::findLexicalScope(const DILocation *DL) {
  DILocalScope *Scope = DL->getScope();
  if (!Scope)
    return nullptr;

  // The scope that we were created with could have an extra file - which
  // isn't what we care about in this case.
  Scope = Scope->getNonLexicalBlockFileScope();

  if (auto *IA = DL->getInlinedAt()) {
    auto I = InlinedLexicalScopeMap.find(std::make_pair(Scope, IA));
    return I != InlinedLexicalScopeMap.end() ? &I->second : nullptr;
  }
  return findLexicalScope(Scope);
}

/// getOrCreateLexicalScope - Find lexical scope for the given DebugLoc. If
/// not available then create new lexical scope.
LexicalScope *LexicalScopes::getOrCreateLexicalScope(const DILocalScope *Scope,
                                                     const DILocation *IA) {
  if (IA) {
    // Skip scopes inlined from a NoDebug compile unit.
    if (Scope->getSubprogram()->getUnit()->getEmissionKind() ==
        DICompileUnit::NoDebug)
      return getOrCreateLexicalScope(IA);
    // Create an abstract scope for inlined function.
    getOrCreateAbstractScope(Scope);
    // Create an inlined scope for inlined function.
    return getOrCreateInlinedScope(Scope, IA);
  }

  return getOrCreateRegularScope(Scope);
}

/// getOrCreateRegularScope - Find or create a regular lexical scope.
LexicalScope *
LexicalScopes::getOrCreateRegularScope(const DILocalScope *Scope) {
  assert(Scope && "Invalid Scope encoding!");
  Scope = Scope->getNonLexicalBlockFileScope();

  auto I = LexicalScopeMap.find(Scope);
  if (I != LexicalScopeMap.end())
    return &I->second;

  // FIXME: Should the following dyn_cast be DILexicalBlock?
  LexicalScope *Parent = nullptr;
  if (auto *Block = dyn_cast<DILexicalBlockBase>(Scope))
    Parent = getOrCreateLexicalScope(Block->getScope());
  I = LexicalScopeMap.emplace(std::piecewise_construct,
                              std::forward_as_tuple(Scope),
                              std::forward_as_tuple(Parent, Scope, nullptr,
                                                    false)).first;

  if (!Parent) {
    assert(cast<DISubprogram>(Scope)->describes(&MF->getFunction()));
    assert(!CurrentFnLexicalScope);
    CurrentFnLexicalScope = &I->second;
  }

  return &I->second;
}

/// getOrCreateInlinedScope - Find or create an inlined lexical scope.
LexicalScope *
LexicalScopes::getOrCreateInlinedScope(const DILocalScope *Scope,
                                       const DILocation *InlinedAt) {
  assert(Scope && "Invalid Scope encoding!");
  Scope = Scope->getNonLexicalBlockFileScope();
  std::pair<const DILocalScope *, const DILocation *> P(Scope, InlinedAt);
  auto I = InlinedLexicalScopeMap.find(P);
  if (I != InlinedLexicalScopeMap.end())
    return &I->second;

  LexicalScope *Parent;
  if (auto *Block = dyn_cast<DILexicalBlockBase>(Scope))
    Parent = getOrCreateInlinedScope(Block->getScope(), InlinedAt);
  else
    Parent = getOrCreateLexicalScope(InlinedAt);

  I = InlinedLexicalScopeMap
          .emplace(std::piecewise_construct, std::forward_as_tuple(P),
                   std::forward_as_tuple(Parent, Scope, InlinedAt, false))
          .first;
  return &I->second;
}

/// getOrCreateAbstractScope - Find or create an abstract lexical scope.
LexicalScope *
LexicalScopes::getOrCreateAbstractScope(const DILocalScope *Scope) {
  assert(Scope && "Invalid Scope encoding!");
  Scope = Scope->getNonLexicalBlockFileScope();
  auto I = AbstractScopeMap.find(Scope);
  if (I != AbstractScopeMap.end())
    return &I->second;

  // FIXME: Should the following isa be DILexicalBlock?
  LexicalScope *Parent = nullptr;
  if (auto *Block = dyn_cast<DILexicalBlockBase>(Scope))
    Parent = getOrCreateAbstractScope(Block->getScope());

  I = AbstractScopeMap.emplace(std::piecewise_construct,
                               std::forward_as_tuple(Scope),
                               std::forward_as_tuple(Parent, Scope,
                                                     nullptr, true)).first;
  if (isa<DISubprogram>(Scope))
    AbstractScopesList.push_back(&I->second);
  return &I->second;
}

/// constructScopeNest - Traverse the Scope tree depth-first, storing
/// traversal state in WorkStack and recording the depth-first
/// numbering (setDFSIn, setDFSOut) for edge classification.
void LexicalScopes::constructScopeNest(LexicalScope *Scope) {
  assert(Scope && "Unable to calculate scope dominance graph!");
  SmallVector<std::pair<LexicalScope *, size_t>, 4> WorkStack;
  WorkStack.push_back(std::make_pair(Scope, 0));
  unsigned Counter = 0;
  while (!WorkStack.empty()) {
    auto &ScopePosition = WorkStack.back();
    LexicalScope *WS = ScopePosition.first;
    size_t ChildNum = ScopePosition.second++;
    const SmallVectorImpl<LexicalScope *> &Children = WS->getChildren();
    if (ChildNum < Children.size()) {
      auto &ChildScope = Children[ChildNum];
      WorkStack.push_back(std::make_pair(ChildScope, 0));
      ChildScope->setDFSIn(++Counter);
    } else {
      WorkStack.pop_back();
      WS->setDFSOut(++Counter);
    }
  }
}

/// assignInstructionRanges - Find ranges of instructions covered by each
/// lexical scope.
void LexicalScopes::assignInstructionRanges(
    SmallVectorImpl<InsnRange> &MIRanges,
    DenseMap<const MachineInstr *, LexicalScope *> &MI2ScopeMap) {
  LexicalScope *PrevLexicalScope = nullptr;
  for (const auto &R : MIRanges) {
    LexicalScope *S = MI2ScopeMap.lookup(R.first);
    assert(S && "Lost LexicalScope for a machine instruction!");
    if (PrevLexicalScope && !PrevLexicalScope->dominates(S))
      PrevLexicalScope->closeInsnRange(S);
    S->openInsnRange(R.first);
    S->extendInsnRange(R.second);
    PrevLexicalScope = S;
  }

  if (PrevLexicalScope)
    PrevLexicalScope->closeInsnRange();
}

/// getMachineBasicBlocks - Populate given set using machine basic blocks which
/// have machine instructions that belong to lexical scope identified by
/// DebugLoc.
void LexicalScopes::getMachineBasicBlocks(
    const DILocation *DL, SmallPtrSetImpl<const MachineBasicBlock *> &MBBs) {
  assert(MF && "Method called on a uninitialized LexicalScopes object!");
  MBBs.clear();

  LexicalScope *Scope = getOrCreateLexicalScope(DL);
  if (!Scope)
    return;

  if (Scope == CurrentFnLexicalScope) {
    for (const auto &MBB : *MF)
      MBBs.insert(&MBB);
    return;
  }

  // The scope ranges can cover multiple basic blocks in each span. Iterate over
  // all blocks (in the order they are in the function) until we reach the one
  // containing the end of the span.
  SmallVectorImpl<InsnRange> &InsnRanges = Scope->getRanges();
  for (auto &R : InsnRanges)
    for (auto CurMBBIt = R.first->getParent()->getIterator(),
              EndBBIt = std::next(R.second->getParent()->getIterator());
         CurMBBIt != EndBBIt; CurMBBIt++)
      MBBs.insert(&*CurMBBIt);
}

bool LexicalScopes::dominates(const DILocation *DL, MachineBasicBlock *MBB) {
  assert(MF && "Unexpected uninitialized LexicalScopes object!");
  LexicalScope *Scope = getOrCreateLexicalScope(DL);
  if (!Scope)
    return false;

  // Current function scope covers all basic blocks in the function.
  if (Scope == CurrentFnLexicalScope && MBB->getParent() == MF)
    return true;

  // Fetch all the blocks in DLs scope. Because the range / block list also
  // contain any subscopes, any instruction that DL dominates can be found in
  // the block set.
  //
  // Cache the set of fetched blocks to avoid repeatedly recomputing the set in
  // the LiveDebugValues pass.
  std::unique_ptr<BlockSetT> &Set = DominatedBlocks[DL];
  if (!Set) {
    Set = std::make_unique<BlockSetT>();
    getMachineBasicBlocks(DL, *Set);
  }
  return Set->contains(MBB);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LexicalScope::dump(unsigned Indent) const {
  raw_ostream &err = dbgs();
  err.indent(Indent);
  err << "DFSIn: " << DFSIn << " DFSOut: " << DFSOut << "\n";
  const MDNode *N = Desc;
  err.indent(Indent);
  N->dump();
  if (AbstractScope)
    err << std::string(Indent, ' ') << "Abstract Scope\n";

  if (!Children.empty())
    err << std::string(Indent + 2, ' ') << "Children ...\n";
  for (const LexicalScope *Child : Children)
    if (Child != this)
      Child->dump(Indent + 2);
}
#endif
