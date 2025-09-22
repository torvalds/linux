//===-- BlockCoverageInference.cpp - Minimal Execution Coverage -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Our algorithm works by first identifying a subset of nodes that must always
// be instrumented. We call these nodes ambiguous because knowing the coverage
// of all remaining nodes is not enough to infer their coverage status.
//
// In general a node v is ambiguous if there exists two entry-to-terminal paths
// P_1 and P_2 such that:
//   1. v not in P_1 but P_1 visits a predecessor of v, and
//   2. v not in P_2 but P_2 visits a successor of v.
//
// If a node v is not ambiguous, then if condition 1 fails, we can infer v’s
// coverage from the coverage of its predecessors, or if condition 2 fails, we
// can infer v’s coverage from the coverage of its successors.
//
// Sadly, there are example CFGs where it is not possible to infer all nodes
// from the ambiguous nodes alone. Our algorithm selects a minimum number of
// extra nodes to add to the ambiguous nodes to form a valid instrumentation S.
//
// Details on this algorithm can be found in https://arxiv.org/abs/2208.13907
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/BlockCoverageInference.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "pgo-block-coverage"

STATISTIC(NumFunctions, "Number of total functions that BCI has processed");
STATISTIC(NumIneligibleFunctions,
          "Number of functions for which BCI cannot run on");
STATISTIC(NumBlocks, "Number of total basic blocks that BCI has processed");
STATISTIC(NumInstrumentedBlocks,
          "Number of basic blocks instrumented for coverage");

BlockCoverageInference::BlockCoverageInference(const Function &F,
                                               bool ForceInstrumentEntry)
    : F(F), ForceInstrumentEntry(ForceInstrumentEntry) {
  findDependencies();
  assert(!ForceInstrumentEntry || shouldInstrumentBlock(F.getEntryBlock()));

  ++NumFunctions;
  for (auto &BB : F) {
    ++NumBlocks;
    if (shouldInstrumentBlock(BB))
      ++NumInstrumentedBlocks;
  }
}

BlockCoverageInference::BlockSet
BlockCoverageInference::getDependencies(const BasicBlock &BB) const {
  assert(BB.getParent() == &F);
  BlockSet Dependencies;
  auto It = PredecessorDependencies.find(&BB);
  if (It != PredecessorDependencies.end())
    Dependencies.set_union(It->second);
  It = SuccessorDependencies.find(&BB);
  if (It != SuccessorDependencies.end())
    Dependencies.set_union(It->second);
  return Dependencies;
}

uint64_t BlockCoverageInference::getInstrumentedBlocksHash() const {
  JamCRC JC;
  uint64_t Index = 0;
  for (auto &BB : F) {
    if (shouldInstrumentBlock(BB)) {
      uint8_t Data[8];
      support::endian::write64le(Data, Index);
      JC.update(Data);
    }
    Index++;
  }
  return JC.getCRC();
}

bool BlockCoverageInference::shouldInstrumentBlock(const BasicBlock &BB) const {
  assert(BB.getParent() == &F);
  auto It = PredecessorDependencies.find(&BB);
  if (It != PredecessorDependencies.end() && It->second.size())
    return false;
  It = SuccessorDependencies.find(&BB);
  if (It != SuccessorDependencies.end() && It->second.size())
    return false;
  return true;
}

void BlockCoverageInference::findDependencies() {
  assert(PredecessorDependencies.empty() && SuccessorDependencies.empty());
  // Empirical analysis shows that this algorithm finishes within 5 seconds for
  // functions with fewer than 1.5K blocks.
  if (F.hasFnAttribute(Attribute::NoReturn) || F.size() > 1500) {
    ++NumIneligibleFunctions;
    return;
  }

  SmallVector<const BasicBlock *, 4> TerminalBlocks;
  for (auto &BB : F)
    if (succ_empty(&BB))
      TerminalBlocks.push_back(&BB);

  // Traverse the CFG backwards from the terminal blocks to make sure every
  // block can reach some terminal block. Otherwise this algorithm will not work
  // and we must fall back to instrumenting every block.
  df_iterator_default_set<const BasicBlock *> Visited;
  for (auto *BB : TerminalBlocks)
    for (auto *N : inverse_depth_first_ext(BB, Visited))
      (void)N;
  if (F.size() != Visited.size()) {
    ++NumIneligibleFunctions;
    return;
  }

  // The current implementation for computing `PredecessorDependencies` and
  // `SuccessorDependencies` runs in quadratic time with respect to the number
  // of basic blocks. While we do have a more complicated linear time algorithm
  // in https://arxiv.org/abs/2208.13907 we do not know if it will give a
  // significant speedup in practice given that most functions tend to be
  // relatively small in size for intended use cases.
  auto &EntryBlock = F.getEntryBlock();
  for (auto &BB : F) {
    // The set of blocks that are reachable while avoiding BB.
    BlockSet ReachableFromEntry, ReachableFromTerminal;
    getReachableAvoiding(EntryBlock, BB, /*IsForward=*/true,
                         ReachableFromEntry);
    for (auto *TerminalBlock : TerminalBlocks)
      getReachableAvoiding(*TerminalBlock, BB, /*IsForward=*/false,
                           ReachableFromTerminal);

    auto Preds = predecessors(&BB);
    bool HasSuperReachablePred = llvm::any_of(Preds, [&](auto *Pred) {
      return ReachableFromEntry.count(Pred) &&
             ReachableFromTerminal.count(Pred);
    });
    if (!HasSuperReachablePred)
      for (auto *Pred : Preds)
        if (ReachableFromEntry.count(Pred))
          PredecessorDependencies[&BB].insert(Pred);

    auto Succs = successors(&BB);
    bool HasSuperReachableSucc = llvm::any_of(Succs, [&](auto *Succ) {
      return ReachableFromEntry.count(Succ) &&
             ReachableFromTerminal.count(Succ);
    });
    if (!HasSuperReachableSucc)
      for (auto *Succ : Succs)
        if (ReachableFromTerminal.count(Succ))
          SuccessorDependencies[&BB].insert(Succ);
  }

  if (ForceInstrumentEntry) {
    // Force the entry block to be instrumented by clearing the blocks it can
    // infer coverage from.
    PredecessorDependencies[&EntryBlock].clear();
    SuccessorDependencies[&EntryBlock].clear();
  }

  // Construct a graph where blocks are connected if there is a mutual
  // dependency between them. This graph has a special property that it contains
  // only paths.
  DenseMap<const BasicBlock *, BlockSet> AdjacencyList;
  for (auto &BB : F) {
    for (auto *Succ : successors(&BB)) {
      if (SuccessorDependencies[&BB].count(Succ) &&
          PredecessorDependencies[Succ].count(&BB)) {
        AdjacencyList[&BB].insert(Succ);
        AdjacencyList[Succ].insert(&BB);
      }
    }
  }

  // Given a path with at least one node, return the next node on the path.
  auto getNextOnPath = [&](BlockSet &Path) -> const BasicBlock * {
    assert(Path.size());
    auto &Neighbors = AdjacencyList[Path.back()];
    if (Path.size() == 1) {
      // This is the first node on the path, return its neighbor.
      assert(Neighbors.size() == 1);
      return Neighbors.front();
    } else if (Neighbors.size() == 2) {
      // This is the middle of the path, find the neighbor that is not on the
      // path already.
      assert(Path.size() >= 2);
      return Path.count(Neighbors[0]) ? Neighbors[1] : Neighbors[0];
    }
    // This is the end of the path.
    assert(Neighbors.size() == 1);
    return nullptr;
  };

  // Remove all cycles in the inferencing graph.
  for (auto &BB : F) {
    if (AdjacencyList[&BB].size() == 1) {
      // We found the head of some path.
      BlockSet Path;
      Path.insert(&BB);
      while (const BasicBlock *Next = getNextOnPath(Path))
        Path.insert(Next);
      LLVM_DEBUG(dbgs() << "Found path: " << getBlockNames(Path) << "\n");

      // Remove these nodes from the graph so we don't discover this path again.
      for (auto *BB : Path)
        AdjacencyList[BB].clear();

      // Finally, remove the cycles.
      if (PredecessorDependencies[Path.front()].size()) {
        for (auto *BB : Path)
          if (BB != Path.back())
            SuccessorDependencies[BB].clear();
      } else {
        for (auto *BB : Path)
          if (BB != Path.front())
            PredecessorDependencies[BB].clear();
      }
    }
  }
  LLVM_DEBUG(dump(dbgs()));
}

void BlockCoverageInference::getReachableAvoiding(const BasicBlock &Start,
                                                  const BasicBlock &Avoid,
                                                  bool IsForward,
                                                  BlockSet &Reachable) const {
  df_iterator_default_set<const BasicBlock *> Visited;
  Visited.insert(&Avoid);
  if (IsForward) {
    auto Range = depth_first_ext(&Start, Visited);
    Reachable.insert(Range.begin(), Range.end());
  } else {
    auto Range = inverse_depth_first_ext(&Start, Visited);
    Reachable.insert(Range.begin(), Range.end());
  }
}

namespace llvm {
class DotFuncBCIInfo {
private:
  const BlockCoverageInference *BCI;
  const DenseMap<const BasicBlock *, bool> *Coverage;

public:
  DotFuncBCIInfo(const BlockCoverageInference *BCI,
                 const DenseMap<const BasicBlock *, bool> *Coverage)
      : BCI(BCI), Coverage(Coverage) {}

  const Function &getFunction() { return BCI->F; }

  bool isInstrumented(const BasicBlock *BB) const {
    return BCI->shouldInstrumentBlock(*BB);
  }

  bool isCovered(const BasicBlock *BB) const {
    return Coverage && Coverage->lookup(BB);
  }

  bool isDependent(const BasicBlock *Src, const BasicBlock *Dest) const {
    return BCI->getDependencies(*Src).count(Dest);
  }
};

template <>
struct GraphTraits<DotFuncBCIInfo *> : public GraphTraits<const BasicBlock *> {
  static NodeRef getEntryNode(DotFuncBCIInfo *Info) {
    return &(Info->getFunction().getEntryBlock());
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<Function::const_iterator>;

  static nodes_iterator nodes_begin(DotFuncBCIInfo *Info) {
    return nodes_iterator(Info->getFunction().begin());
  }

  static nodes_iterator nodes_end(DotFuncBCIInfo *Info) {
    return nodes_iterator(Info->getFunction().end());
  }

  static size_t size(DotFuncBCIInfo *Info) {
    return Info->getFunction().size();
  }
};

template <>
struct DOTGraphTraits<DotFuncBCIInfo *> : public DefaultDOTGraphTraits {

  DOTGraphTraits(bool IsSimple = false) : DefaultDOTGraphTraits(IsSimple) {}

  static std::string getGraphName(DotFuncBCIInfo *Info) {
    return "BCI CFG for " + Info->getFunction().getName().str();
  }

  std::string getNodeLabel(const BasicBlock *Node, DotFuncBCIInfo *Info) {
    return Node->getName().str();
  }

  std::string getEdgeAttributes(const BasicBlock *Src, const_succ_iterator I,
                                DotFuncBCIInfo *Info) {
    const BasicBlock *Dest = *I;
    if (Info->isDependent(Src, Dest))
      return "color=red";
    if (Info->isDependent(Dest, Src))
      return "color=blue";
    return "";
  }

  std::string getNodeAttributes(const BasicBlock *Node, DotFuncBCIInfo *Info) {
    std::string Result;
    if (Info->isInstrumented(Node))
      Result += "style=filled,fillcolor=gray";
    if (Info->isCovered(Node))
      Result += std::string(Result.empty() ? "" : ",") + "color=red";
    return Result;
  }
};

} // namespace llvm

void BlockCoverageInference::viewBlockCoverageGraph(
    const DenseMap<const BasicBlock *, bool> *Coverage) const {
  DotFuncBCIInfo Info(this, Coverage);
  WriteGraph(&Info, "BCI", false,
             "Block Coverage Inference for " + F.getName());
}

void BlockCoverageInference::dump(raw_ostream &OS) const {
  OS << "Minimal block coverage for function \'" << F.getName()
     << "\' (Instrumented=*)\n";
  for (auto &BB : F) {
    OS << (shouldInstrumentBlock(BB) ? "* " : "  ") << BB.getName() << "\n";
    auto It = PredecessorDependencies.find(&BB);
    if (It != PredecessorDependencies.end() && It->second.size())
      OS << "    PredDeps = " << getBlockNames(It->second) << "\n";
    It = SuccessorDependencies.find(&BB);
    if (It != SuccessorDependencies.end() && It->second.size())
      OS << "    SuccDeps = " << getBlockNames(It->second) << "\n";
  }
  OS << "  Instrumented Blocks Hash = 0x"
     << Twine::utohexstr(getInstrumentedBlocksHash()) << "\n";
}

std::string
BlockCoverageInference::getBlockNames(ArrayRef<const BasicBlock *> BBs) {
  std::string Result;
  raw_string_ostream OS(Result);
  OS << "[";
  if (!BBs.empty()) {
    OS << BBs.front()->getName();
    BBs = BBs.drop_front();
  }
  for (auto *BB : BBs)
    OS << ", " << BB->getName();
  OS << "]";
  return OS.str();
}
