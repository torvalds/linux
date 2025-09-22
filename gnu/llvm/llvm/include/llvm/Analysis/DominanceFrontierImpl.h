//===- llvm/Analysis/DominanceFrontier.h - Dominator Frontiers --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the generic implementation of the DominanceFrontier class, which
// calculate and holds the dominance frontier for a function for.
//
// This should be considered deprecated, don't add any more uses of this data
// structure.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOMINANCEFRONTIERIMPL_H
#define LLVM_ANALYSIS_DOMINANCEFRONTIERIMPL_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GenericDomTree.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <set>
#include <utility>
#include <vector>

namespace llvm {

template <class BlockT>
class DFCalculateWorkObject {
public:
  using DomTreeNodeT = DomTreeNodeBase<BlockT>;

  DFCalculateWorkObject(BlockT *B, BlockT *P, const DomTreeNodeT *N,
                        const DomTreeNodeT *PN)
      : currentBB(B), parentBB(P), Node(N), parentNode(PN) {}

  BlockT *currentBB;
  BlockT *parentBB;
  const DomTreeNodeT *Node;
  const DomTreeNodeT *parentNode;
};

template <class BlockT, bool IsPostDom>
void DominanceFrontierBase<BlockT, IsPostDom>::removeBlock(BlockT *BB) {
  assert(find(BB) != end() && "Block is not in DominanceFrontier!");
  for (iterator I = begin(), E = end(); I != E; ++I)
    I->second.remove(BB);
  Frontiers.erase(BB);
}

template <class BlockT, bool IsPostDom>
void DominanceFrontierBase<BlockT, IsPostDom>::addToFrontier(iterator I,
                                                             BlockT *Node) {
  assert(I != end() && "BB is not in DominanceFrontier!");
  I->second.insert(Node);
}

template <class BlockT, bool IsPostDom>
void DominanceFrontierBase<BlockT, IsPostDom>::removeFromFrontier(
    iterator I, BlockT *Node) {
  assert(I != end() && "BB is not in DominanceFrontier!");
  assert(I->second.count(Node) && "Node is not in DominanceFrontier of BB");
  I->second.remove(Node);
}

template <class BlockT, bool IsPostDom>
bool DominanceFrontierBase<BlockT, IsPostDom>::compareDomSet(
    DomSetType &DS1, const DomSetType &DS2) const {
  std::set<BlockT *> tmpSet;
  for (BlockT *BB : DS2)
    tmpSet.insert(BB);

  for (typename DomSetType::const_iterator I = DS1.begin(), E = DS1.end();
       I != E;) {
    BlockT *Node = *I++;

    if (tmpSet.erase(Node) == 0)
      // Node is in DS1 but tnot in DS2.
      return true;
  }

  if (!tmpSet.empty()) {
    // There are nodes that are in DS2 but not in DS1.
    return true;
  }

  // DS1 and DS2 matches.
  return false;
}

template <class BlockT, bool IsPostDom>
bool DominanceFrontierBase<BlockT, IsPostDom>::compare(
    DominanceFrontierBase<BlockT, IsPostDom> &Other) const {
  DomSetMapType tmpFrontiers;
  for (typename DomSetMapType::const_iterator I = Other.begin(),
                                              E = Other.end();
       I != E; ++I)
    tmpFrontiers.insert(std::make_pair(I->first, I->second));

  for (typename DomSetMapType::iterator I = tmpFrontiers.begin(),
                                        E = tmpFrontiers.end();
       I != E;) {
    BlockT *Node = I->first;
    const_iterator DFI = find(Node);
    if (DFI == end())
      return true;

    if (compareDomSet(I->second, DFI->second))
      return true;

    ++I;
    tmpFrontiers.erase(Node);
  }

  if (!tmpFrontiers.empty())
    return true;

  return false;
}

template <class BlockT, bool IsPostDom>
void DominanceFrontierBase<BlockT, IsPostDom>::print(raw_ostream &OS) const {
  for (const_iterator I = begin(), E = end(); I != E; ++I) {
    OS << "  DomFrontier for BB ";
    if (I->first)
      I->first->printAsOperand(OS, false);
    else
      OS << " <<exit node>>";
    OS << " is:\t";

    const SetVector<BlockT *> &BBs = I->second;

    for (const BlockT *BB : BBs) {
      OS << ' ';
      if (BB)
        BB->printAsOperand(OS, false);
      else
        OS << "<<exit node>>";
    }
    OS << '\n';
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
template <class BlockT, bool IsPostDom>
void DominanceFrontierBase<BlockT, IsPostDom>::dump() const {
  print(dbgs());
}
#endif

template <class BlockT>
const typename ForwardDominanceFrontierBase<BlockT>::DomSetType &
ForwardDominanceFrontierBase<BlockT>::calculate(const DomTreeT &DT,
                                                const DomTreeNodeT *Node) {
  BlockT *BB = Node->getBlock();
  DomSetType *Result = nullptr;

  std::vector<DFCalculateWorkObject<BlockT>> workList;
  SmallPtrSet<BlockT *, 32> visited;

  workList.push_back(DFCalculateWorkObject<BlockT>(BB, nullptr, Node, nullptr));
  do {
    DFCalculateWorkObject<BlockT> *currentW = &workList.back();
    assert(currentW && "Missing work object.");

    BlockT *currentBB = currentW->currentBB;
    BlockT *parentBB = currentW->parentBB;
    const DomTreeNodeT *currentNode = currentW->Node;
    const DomTreeNodeT *parentNode = currentW->parentNode;
    assert(currentBB && "Invalid work object. Missing current Basic Block");
    assert(currentNode && "Invalid work object. Missing current Node");
    DomSetType &S = this->Frontiers[currentBB];

    // Visit each block only once.
    if (visited.insert(currentBB).second) {
      // Loop over CFG successors to calculate DFlocal[currentNode]
      for (const auto Succ : children<BlockT *>(currentBB)) {
        // Does Node immediately dominate this successor?
        if (DT[Succ]->getIDom() != currentNode)
          S.insert(Succ);
      }
    }

    // At this point, S is DFlocal.  Now we union in DFup's of our children...
    // Loop through and visit the nodes that Node immediately dominates (Node's
    // children in the IDomTree)
    bool visitChild = false;
    for (typename DomTreeNodeT::const_iterator NI = currentNode->begin(),
                                               NE = currentNode->end();
         NI != NE; ++NI) {
      DomTreeNodeT *IDominee = *NI;
      BlockT *childBB = IDominee->getBlock();
      if (visited.count(childBB) == 0) {
        workList.push_back(DFCalculateWorkObject<BlockT>(
            childBB, currentBB, IDominee, currentNode));
        visitChild = true;
      }
    }

    // If all children are visited or there is any child then pop this block
    // from the workList.
    if (!visitChild) {
      if (!parentBB) {
        Result = &S;
        break;
      }

      typename DomSetType::const_iterator CDFI = S.begin(), CDFE = S.end();
      DomSetType &parentSet = this->Frontiers[parentBB];
      for (; CDFI != CDFE; ++CDFI) {
        if (!DT.properlyDominates(parentNode, DT[*CDFI]))
          parentSet.insert(*CDFI);
      }
      workList.pop_back();
    }

  } while (!workList.empty());

  return *Result;
}

} // end namespace llvm

#endif // LLVM_ANALYSIS_DOMINANCEFRONTIERIMPL_H
