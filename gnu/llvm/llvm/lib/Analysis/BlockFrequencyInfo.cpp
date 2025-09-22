//===- BlockFrequencyInfo.cpp - Block Frequency Analysis ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Analysis/BlockFrequencyInfoImpl.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <optional>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "block-freq"

static cl::opt<GVDAGType> ViewBlockFreqPropagationDAG(
    "view-block-freq-propagation-dags", cl::Hidden,
    cl::desc("Pop up a window to show a dag displaying how block "
             "frequencies propagation through the CFG."),
    cl::values(clEnumValN(GVDT_None, "none", "do not display graphs."),
               clEnumValN(GVDT_Fraction, "fraction",
                          "display a graph using the "
                          "fractional block frequency representation."),
               clEnumValN(GVDT_Integer, "integer",
                          "display a graph using the raw "
                          "integer fractional block frequency representation."),
               clEnumValN(GVDT_Count, "count", "display a graph using the real "
                                               "profile count if available.")));

namespace llvm {
cl::opt<std::string>
    ViewBlockFreqFuncName("view-bfi-func-name", cl::Hidden,
                          cl::desc("The option to specify "
                                   "the name of the function "
                                   "whose CFG will be displayed."));

cl::opt<unsigned>
    ViewHotFreqPercent("view-hot-freq-percent", cl::init(10), cl::Hidden,
                       cl::desc("An integer in percent used to specify "
                                "the hot blocks/edges to be displayed "
                                "in red: a block or edge whose frequency "
                                "is no less than the max frequency of the "
                                "function multiplied by this percent."));

// Command line option to turn on CFG dot or text dump after profile annotation.
cl::opt<PGOViewCountsType> PGOViewCounts(
    "pgo-view-counts", cl::Hidden,
    cl::desc("A boolean option to show CFG dag or text with "
             "block profile counts and branch probabilities "
             "right after PGO profile annotation step. The "
             "profile counts are computed using branch "
             "probabilities from the runtime profile data and "
             "block frequency propagation algorithm. To view "
             "the raw counts from the profile, use option "
             "-pgo-view-raw-counts instead. To limit graph "
             "display to only one function, use filtering option "
             "-view-bfi-func-name."),
    cl::values(clEnumValN(PGOVCT_None, "none", "do not show."),
               clEnumValN(PGOVCT_Graph, "graph", "show a graph."),
               clEnumValN(PGOVCT_Text, "text", "show in text.")));

static cl::opt<bool> PrintBFI("print-bfi", cl::init(false), cl::Hidden,
                              cl::desc("Print the block frequency info."));

cl::opt<std::string>
    PrintBFIFuncName("print-bfi-func-name", cl::Hidden,
                     cl::desc("The option to specify the name of the function "
                              "whose block frequency info is printed."));
} // namespace llvm

namespace llvm {

static GVDAGType getGVDT() {
  if (PGOViewCounts == PGOVCT_Graph)
    return GVDT_Count;
  return ViewBlockFreqPropagationDAG;
}

template <>
struct GraphTraits<BlockFrequencyInfo *> {
  using NodeRef = const BasicBlock *;
  using ChildIteratorType = const_succ_iterator;
  using nodes_iterator = pointer_iterator<Function::const_iterator>;

  static NodeRef getEntryNode(const BlockFrequencyInfo *G) {
    return &G->getFunction()->front();
  }

  static ChildIteratorType child_begin(const NodeRef N) {
    return succ_begin(N);
  }

  static ChildIteratorType child_end(const NodeRef N) { return succ_end(N); }

  static nodes_iterator nodes_begin(const BlockFrequencyInfo *G) {
    return nodes_iterator(G->getFunction()->begin());
  }

  static nodes_iterator nodes_end(const BlockFrequencyInfo *G) {
    return nodes_iterator(G->getFunction()->end());
  }
};

using BFIDOTGTraitsBase =
    BFIDOTGraphTraitsBase<BlockFrequencyInfo, BranchProbabilityInfo>;

template <>
struct DOTGraphTraits<BlockFrequencyInfo *> : public BFIDOTGTraitsBase {
  explicit DOTGraphTraits(bool isSimple = false)
      : BFIDOTGTraitsBase(isSimple) {}

  std::string getNodeLabel(const BasicBlock *Node,
                           const BlockFrequencyInfo *Graph) {

    return BFIDOTGTraitsBase::getNodeLabel(Node, Graph, getGVDT());
  }

  std::string getNodeAttributes(const BasicBlock *Node,
                                const BlockFrequencyInfo *Graph) {
    return BFIDOTGTraitsBase::getNodeAttributes(Node, Graph,
                                                ViewHotFreqPercent);
  }

  std::string getEdgeAttributes(const BasicBlock *Node, EdgeIter EI,
                                const BlockFrequencyInfo *BFI) {
    return BFIDOTGTraitsBase::getEdgeAttributes(Node, EI, BFI, BFI->getBPI(),
                                                ViewHotFreqPercent);
  }
};

} // end namespace llvm

BlockFrequencyInfo::BlockFrequencyInfo() = default;

BlockFrequencyInfo::BlockFrequencyInfo(const Function &F,
                                       const BranchProbabilityInfo &BPI,
                                       const LoopInfo &LI) {
  calculate(F, BPI, LI);
}

BlockFrequencyInfo::BlockFrequencyInfo(BlockFrequencyInfo &&Arg)
    : BFI(std::move(Arg.BFI)) {}

BlockFrequencyInfo &BlockFrequencyInfo::operator=(BlockFrequencyInfo &&RHS) {
  releaseMemory();
  BFI = std::move(RHS.BFI);
  return *this;
}

// Explicitly define the default constructor otherwise it would be implicitly
// defined at the first ODR-use which is the BFI member in the
// LazyBlockFrequencyInfo header.  The dtor needs the BlockFrequencyInfoImpl
// template instantiated which is not available in the header.
BlockFrequencyInfo::~BlockFrequencyInfo() = default;

bool BlockFrequencyInfo::invalidate(Function &F, const PreservedAnalyses &PA,
                                    FunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on functions, or the function's
  // CFG have been preserved.
  auto PAC = PA.getChecker<BlockFrequencyAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>() ||
           PAC.preservedSet<CFGAnalyses>());
}

void BlockFrequencyInfo::calculate(const Function &F,
                                   const BranchProbabilityInfo &BPI,
                                   const LoopInfo &LI) {
  if (!BFI)
    BFI.reset(new ImplType);
  BFI->calculate(F, BPI, LI);
  if (ViewBlockFreqPropagationDAG != GVDT_None &&
      (ViewBlockFreqFuncName.empty() || F.getName() == ViewBlockFreqFuncName)) {
    view();
  }
  if (PrintBFI &&
      (PrintBFIFuncName.empty() || F.getName() == PrintBFIFuncName)) {
    print(dbgs());
  }
}

BlockFrequency BlockFrequencyInfo::getBlockFreq(const BasicBlock *BB) const {
  return BFI ? BFI->getBlockFreq(BB) : BlockFrequency(0);
}

std::optional<uint64_t>
BlockFrequencyInfo::getBlockProfileCount(const BasicBlock *BB,
                                         bool AllowSynthetic) const {
  if (!BFI)
    return std::nullopt;

  return BFI->getBlockProfileCount(*getFunction(), BB, AllowSynthetic);
}

std::optional<uint64_t>
BlockFrequencyInfo::getProfileCountFromFreq(BlockFrequency Freq) const {
  if (!BFI)
    return std::nullopt;
  return BFI->getProfileCountFromFreq(*getFunction(), Freq);
}

bool BlockFrequencyInfo::isIrrLoopHeader(const BasicBlock *BB) {
  assert(BFI && "Expected analysis to be available");
  return BFI->isIrrLoopHeader(BB);
}

void BlockFrequencyInfo::setBlockFreq(const BasicBlock *BB,
                                      BlockFrequency Freq) {
  assert(BFI && "Expected analysis to be available");
  BFI->setBlockFreq(BB, Freq);
}

void BlockFrequencyInfo::setBlockFreqAndScale(
    const BasicBlock *ReferenceBB, BlockFrequency Freq,
    SmallPtrSetImpl<BasicBlock *> &BlocksToScale) {
  assert(BFI && "Expected analysis to be available");
  // Use 128 bits APInt to avoid overflow.
  APInt NewFreq(128, Freq.getFrequency());
  APInt OldFreq(128, BFI->getBlockFreq(ReferenceBB).getFrequency());
  APInt BBFreq(128, 0);
  for (auto *BB : BlocksToScale) {
    BBFreq = BFI->getBlockFreq(BB).getFrequency();
    // Multiply first by NewFreq and then divide by OldFreq
    // to minimize loss of precision.
    BBFreq *= NewFreq;
    // udiv is an expensive operation in the general case. If this ends up being
    // a hot spot, one of the options proposed in
    // https://reviews.llvm.org/D28535#650071 could be used to avoid this.
    BBFreq = BBFreq.udiv(OldFreq);
    BFI->setBlockFreq(BB, BlockFrequency(BBFreq.getLimitedValue()));
  }
  BFI->setBlockFreq(ReferenceBB, Freq);
}

/// Pop up a ghostview window with the current block frequency propagation
/// rendered using dot.
void BlockFrequencyInfo::view(StringRef title) const {
  ViewGraph(const_cast<BlockFrequencyInfo *>(this), title);
}

const Function *BlockFrequencyInfo::getFunction() const {
  return BFI ? BFI->getFunction() : nullptr;
}

const BranchProbabilityInfo *BlockFrequencyInfo::getBPI() const {
  return BFI ? &BFI->getBPI() : nullptr;
}

BlockFrequency BlockFrequencyInfo::getEntryFreq() const {
  return BFI ? BFI->getEntryFreq() : BlockFrequency(0);
}

void BlockFrequencyInfo::releaseMemory() { BFI.reset(); }

void BlockFrequencyInfo::print(raw_ostream &OS) const {
  if (BFI)
    BFI->print(OS);
}

void BlockFrequencyInfo::verifyMatch(BlockFrequencyInfo &Other) const {
  if (BFI)
    BFI->verifyMatch(*Other.BFI);
}

Printable llvm::printBlockFreq(const BlockFrequencyInfo &BFI,
                               BlockFrequency Freq) {
  return Printable([&BFI, Freq](raw_ostream &OS) {
    printRelativeBlockFreq(OS, BFI.getEntryFreq(), Freq);
  });
}

Printable llvm::printBlockFreq(const BlockFrequencyInfo &BFI,
                               const BasicBlock &BB) {
  return printBlockFreq(BFI, BFI.getBlockFreq(&BB));
}

INITIALIZE_PASS_BEGIN(BlockFrequencyInfoWrapperPass, "block-freq",
                      "Block Frequency Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(BranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(BlockFrequencyInfoWrapperPass, "block-freq",
                    "Block Frequency Analysis", true, true)

char BlockFrequencyInfoWrapperPass::ID = 0;

BlockFrequencyInfoWrapperPass::BlockFrequencyInfoWrapperPass()
    : FunctionPass(ID) {
  initializeBlockFrequencyInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

BlockFrequencyInfoWrapperPass::~BlockFrequencyInfoWrapperPass() = default;

void BlockFrequencyInfoWrapperPass::print(raw_ostream &OS,
                                          const Module *) const {
  BFI.print(OS);
}

void BlockFrequencyInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<BranchProbabilityInfoWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

void BlockFrequencyInfoWrapperPass::releaseMemory() { BFI.releaseMemory(); }

bool BlockFrequencyInfoWrapperPass::runOnFunction(Function &F) {
  BranchProbabilityInfo &BPI =
      getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  BFI.calculate(F, BPI, LI);
  return false;
}

AnalysisKey BlockFrequencyAnalysis::Key;
BlockFrequencyInfo BlockFrequencyAnalysis::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  auto &BP = AM.getResult<BranchProbabilityAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  BlockFrequencyInfo BFI;
  BFI.calculate(F, BP, LI);
  return BFI;
}

PreservedAnalyses
BlockFrequencyPrinterPass::run(Function &F, FunctionAnalysisManager &AM) {
  OS << "Printing analysis results of BFI for function "
     << "'" << F.getName() << "':"
     << "\n";
  AM.getResult<BlockFrequencyAnalysis>(F).print(OS);
  return PreservedAnalyses::all();
}
