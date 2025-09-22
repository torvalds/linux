//===- ThreadSafetyTIL.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Basic/LLVM.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstddef>

using namespace clang;
using namespace threadSafety;
using namespace til;

StringRef til::getUnaryOpcodeString(TIL_UnaryOpcode Op) {
  switch (Op) {
    case UOP_Minus:    return "-";
    case UOP_BitNot:   return "~";
    case UOP_LogicNot: return "!";
  }
  return {};
}

StringRef til::getBinaryOpcodeString(TIL_BinaryOpcode Op) {
  switch (Op) {
    case BOP_Mul:      return "*";
    case BOP_Div:      return "/";
    case BOP_Rem:      return "%";
    case BOP_Add:      return "+";
    case BOP_Sub:      return "-";
    case BOP_Shl:      return "<<";
    case BOP_Shr:      return ">>";
    case BOP_BitAnd:   return "&";
    case BOP_BitXor:   return "^";
    case BOP_BitOr:    return "|";
    case BOP_Eq:       return "==";
    case BOP_Neq:      return "!=";
    case BOP_Lt:       return "<";
    case BOP_Leq:      return "<=";
    case BOP_Cmp:      return "<=>";
    case BOP_LogicAnd: return "&&";
    case BOP_LogicOr:  return "||";
  }
  return {};
}

SExpr* Future::force() {
  Status = FS_evaluating;
  Result = compute();
  Status = FS_done;
  return Result;
}

unsigned BasicBlock::addPredecessor(BasicBlock *Pred) {
  unsigned Idx = Predecessors.size();
  Predecessors.reserveCheck(1, Arena);
  Predecessors.push_back(Pred);
  for (auto *E : Args) {
    if (auto *Ph = dyn_cast<Phi>(E)) {
      Ph->values().reserveCheck(1, Arena);
      Ph->values().push_back(nullptr);
    }
  }
  return Idx;
}

void BasicBlock::reservePredecessors(unsigned NumPreds) {
  Predecessors.reserve(NumPreds, Arena);
  for (auto *E : Args) {
    if (auto *Ph = dyn_cast<Phi>(E)) {
      Ph->values().reserve(NumPreds, Arena);
    }
  }
}

// If E is a variable, then trace back through any aliases or redundant
// Phi nodes to find the canonical definition.
const SExpr *til::getCanonicalVal(const SExpr *E) {
  while (true) {
    if (const auto *V = dyn_cast<Variable>(E)) {
      if (V->kind() == Variable::VK_Let) {
        E = V->definition();
        continue;
      }
    }
    if (const auto *Ph = dyn_cast<Phi>(E)) {
      if (Ph->status() == Phi::PH_SingleVal) {
        E = Ph->values()[0];
        continue;
      }
    }
    break;
  }
  return E;
}

// If E is a variable, then trace back through any aliases or redundant
// Phi nodes to find the canonical definition.
// The non-const version will simplify incomplete Phi nodes.
SExpr *til::simplifyToCanonicalVal(SExpr *E) {
  while (true) {
    if (auto *V = dyn_cast<Variable>(E)) {
      if (V->kind() != Variable::VK_Let)
        return V;
      // Eliminate redundant variables, e.g. x = y, or x = 5,
      // but keep anything more complicated.
      if (til::ThreadSafetyTIL::isTrivial(V->definition())) {
        E = V->definition();
        continue;
      }
      return V;
    }
    if (auto *Ph = dyn_cast<Phi>(E)) {
      if (Ph->status() == Phi::PH_Incomplete)
        simplifyIncompleteArg(Ph);
      // Eliminate redundant Phi nodes.
      if (Ph->status() == Phi::PH_SingleVal) {
        E = Ph->values()[0];
        continue;
      }
    }
    return E;
  }
}

// Trace the arguments of an incomplete Phi node to see if they have the same
// canonical definition.  If so, mark the Phi node as redundant.
// getCanonicalVal() will recursively call simplifyIncompletePhi().
void til::simplifyIncompleteArg(til::Phi *Ph) {
  assert(Ph && Ph->status() == Phi::PH_Incomplete);

  // eliminate infinite recursion -- assume that this node is not redundant.
  Ph->setStatus(Phi::PH_MultiVal);

  SExpr *E0 = simplifyToCanonicalVal(Ph->values()[0]);
  for (unsigned i = 1, n = Ph->values().size(); i < n; ++i) {
    SExpr *Ei = simplifyToCanonicalVal(Ph->values()[i]);
    if (Ei == Ph)
      continue;  // Recursive reference to itself.  Don't count.
    if (Ei != E0) {
      return;    // Status is already set to MultiVal.
    }
  }
  Ph->setStatus(Phi::PH_SingleVal);
}

// Renumbers the arguments and instructions to have unique, sequential IDs.
unsigned BasicBlock::renumberInstrs(unsigned ID) {
  for (auto *Arg : Args)
    Arg->setID(this, ID++);
  for (auto *Instr : Instrs)
    Instr->setID(this, ID++);
  TermInstr->setID(this, ID++);
  return ID;
}

// Sorts the CFGs blocks using a reverse post-order depth-first traversal.
// Each block will be written into the Blocks array in order, and its BlockID
// will be set to the index in the array.  Sorting should start from the entry
// block, and ID should be the total number of blocks.
unsigned BasicBlock::topologicalSort(SimpleArray<BasicBlock *> &Blocks,
                                     unsigned ID) {
  if (Visited) return ID;
  Visited = true;
  for (auto *Block : successors())
    ID = Block->topologicalSort(Blocks, ID);
  // set ID and update block array in place.
  // We may lose pointers to unreachable blocks.
  assert(ID > 0);
  BlockID = --ID;
  Blocks[BlockID] = this;
  return ID;
}

// Performs a reverse topological traversal, starting from the exit block and
// following back-edges.  The dominator is serialized before any predecessors,
// which guarantees that all blocks are serialized after their dominator and
// before their post-dominator (because it's a reverse topological traversal).
// ID should be initially set to 0.
//
// This sort assumes that (1) dominators have been computed, (2) there are no
// critical edges, and (3) the entry block is reachable from the exit block
// and no blocks are accessible via traversal of back-edges from the exit that
// weren't accessible via forward edges from the entry.
unsigned BasicBlock::topologicalFinalSort(SimpleArray<BasicBlock *> &Blocks,
                                          unsigned ID) {
  // Visited is assumed to have been set by the topologicalSort.  This pass
  // assumes !Visited means that we've visited this node before.
  if (!Visited) return ID;
  Visited = false;
  if (DominatorNode.Parent)
    ID = DominatorNode.Parent->topologicalFinalSort(Blocks, ID);
  for (auto *Pred : Predecessors)
    ID = Pred->topologicalFinalSort(Blocks, ID);
  assert(static_cast<size_t>(ID) < Blocks.size());
  BlockID = ID++;
  Blocks[BlockID] = this;
  return ID;
}

// Computes the immediate dominator of the current block.  Assumes that all of
// its predecessors have already computed their dominators.  This is achieved
// by visiting the nodes in topological order.
void BasicBlock::computeDominator() {
  BasicBlock *Candidate = nullptr;
  // Walk backwards from each predecessor to find the common dominator node.
  for (auto *Pred : Predecessors) {
    // Skip back-edges
    if (Pred->BlockID >= BlockID) continue;
    // If we don't yet have a candidate for dominator yet, take this one.
    if (Candidate == nullptr) {
      Candidate = Pred;
      continue;
    }
    // Walk the alternate and current candidate back to find a common ancestor.
    auto *Alternate = Pred;
    while (Alternate != Candidate) {
      if (Candidate->BlockID > Alternate->BlockID)
        Candidate = Candidate->DominatorNode.Parent;
      else
        Alternate = Alternate->DominatorNode.Parent;
    }
  }
  DominatorNode.Parent = Candidate;
  DominatorNode.SizeOfSubTree = 1;
}

// Computes the immediate post-dominator of the current block.  Assumes that all
// of its successors have already computed their post-dominators.  This is
// achieved visiting the nodes in reverse topological order.
void BasicBlock::computePostDominator() {
  BasicBlock *Candidate = nullptr;
  // Walk back from each predecessor to find the common post-dominator node.
  for (auto *Succ : successors()) {
    // Skip back-edges
    if (Succ->BlockID <= BlockID) continue;
    // If we don't yet have a candidate for post-dominator yet, take this one.
    if (Candidate == nullptr) {
      Candidate = Succ;
      continue;
    }
    // Walk the alternate and current candidate back to find a common ancestor.
    auto *Alternate = Succ;
    while (Alternate != Candidate) {
      if (Candidate->BlockID < Alternate->BlockID)
        Candidate = Candidate->PostDominatorNode.Parent;
      else
        Alternate = Alternate->PostDominatorNode.Parent;
    }
  }
  PostDominatorNode.Parent = Candidate;
  PostDominatorNode.SizeOfSubTree = 1;
}

// Renumber instructions in all blocks
void SCFG::renumberInstrs() {
  unsigned InstrID = 0;
  for (auto *Block : Blocks)
    InstrID = Block->renumberInstrs(InstrID);
}

static inline void computeNodeSize(BasicBlock *B,
                                   BasicBlock::TopologyNode BasicBlock::*TN) {
  BasicBlock::TopologyNode *N = &(B->*TN);
  if (N->Parent) {
    BasicBlock::TopologyNode *P = &(N->Parent->*TN);
    // Initially set ID relative to the (as yet uncomputed) parent ID
    N->NodeID = P->SizeOfSubTree;
    P->SizeOfSubTree += N->SizeOfSubTree;
  }
}

static inline void computeNodeID(BasicBlock *B,
                                 BasicBlock::TopologyNode BasicBlock::*TN) {
  BasicBlock::TopologyNode *N = &(B->*TN);
  if (N->Parent) {
    BasicBlock::TopologyNode *P = &(N->Parent->*TN);
    N->NodeID += P->NodeID;    // Fix NodeIDs relative to starting node.
  }
}

// Normalizes a CFG.  Normalization has a few major components:
// 1) Removing unreachable blocks.
// 2) Computing dominators and post-dominators
// 3) Topologically sorting the blocks into the "Blocks" array.
void SCFG::computeNormalForm() {
  // Topologically sort the blocks starting from the entry block.
  unsigned NumUnreachableBlocks = Entry->topologicalSort(Blocks, Blocks.size());
  if (NumUnreachableBlocks > 0) {
    // If there were unreachable blocks shift everything down, and delete them.
    for (unsigned I = NumUnreachableBlocks, E = Blocks.size(); I < E; ++I) {
      unsigned NI = I - NumUnreachableBlocks;
      Blocks[NI] = Blocks[I];
      Blocks[NI]->BlockID = NI;
      // FIXME: clean up predecessor pointers to unreachable blocks?
    }
    Blocks.drop(NumUnreachableBlocks);
  }

  // Compute dominators.
  for (auto *Block : Blocks)
    Block->computeDominator();

  // Once dominators have been computed, the final sort may be performed.
  unsigned NumBlocks = Exit->topologicalFinalSort(Blocks, 0);
  assert(static_cast<size_t>(NumBlocks) == Blocks.size());
  (void) NumBlocks;

  // Renumber the instructions now that we have a final sort.
  renumberInstrs();

  // Compute post-dominators and compute the sizes of each node in the
  // dominator tree.
  for (auto *Block : Blocks.reverse()) {
    Block->computePostDominator();
    computeNodeSize(Block, &BasicBlock::DominatorNode);
  }
  // Compute the sizes of each node in the post-dominator tree and assign IDs in
  // the dominator tree.
  for (auto *Block : Blocks) {
    computeNodeID(Block, &BasicBlock::DominatorNode);
    computeNodeSize(Block, &BasicBlock::PostDominatorNode);
  }
  // Assign IDs in the post-dominator tree.
  for (auto *Block : Blocks.reverse()) {
    computeNodeID(Block, &BasicBlock::PostDominatorNode);
  }
}
