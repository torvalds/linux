//===- CFGPrinter.cpp - DOT printer for the control flow graph ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a `-dot-cfg` analysis pass, which emits the
// `<prefix>.<fnname>.dot` file for each function in the program, with a graph
// of the CFG for that function. The default value for `<prefix>` is `cfg` but
// can be customized as needed.
//
// The other main feature of this file is that it implements the
// Function::viewCFG method, which is useful for debugging passes which operate
// on the CFG.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GraphWriter.h"

using namespace llvm;

static cl::opt<std::string>
    CFGFuncName("cfg-func-name", cl::Hidden,
                cl::desc("The name of a function (or its substring)"
                         " whose CFG is viewed/printed."));

static cl::opt<std::string> CFGDotFilenamePrefix(
    "cfg-dot-filename-prefix", cl::Hidden,
    cl::desc("The prefix used for the CFG dot file names."));

static cl::opt<bool> HideUnreachablePaths("cfg-hide-unreachable-paths",
                                          cl::init(false));

static cl::opt<bool> HideDeoptimizePaths("cfg-hide-deoptimize-paths",
                                         cl::init(false));

static cl::opt<double> HideColdPaths(
    "cfg-hide-cold-paths", cl::init(0.0),
    cl::desc("Hide blocks with relative frequency below the given value"));

static cl::opt<bool> ShowHeatColors("cfg-heat-colors", cl::init(true),
                                    cl::Hidden,
                                    cl::desc("Show heat colors in CFG"));

static cl::opt<bool> UseRawEdgeWeight("cfg-raw-weights", cl::init(false),
                                      cl::Hidden,
                                      cl::desc("Use raw weights for labels. "
                                               "Use percentages as default."));

static cl::opt<bool>
    ShowEdgeWeight("cfg-weights", cl::init(false), cl::Hidden,
                   cl::desc("Show edges labeled with weights"));

static void writeCFGToDotFile(Function &F, BlockFrequencyInfo *BFI,
                              BranchProbabilityInfo *BPI, uint64_t MaxFreq,
                              bool CFGOnly = false) {
  std::string Filename =
      (CFGDotFilenamePrefix + "." + F.getName() + ".dot").str();
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::OF_Text);

  DOTFuncInfo CFGInfo(&F, BFI, BPI, MaxFreq);
  CFGInfo.setHeatColors(ShowHeatColors);
  CFGInfo.setEdgeWeights(ShowEdgeWeight);
  CFGInfo.setRawEdgeWeights(UseRawEdgeWeight);

  if (!EC)
    WriteGraph(File, &CFGInfo, CFGOnly);
  else
    errs() << "  error opening file for writing!";
  errs() << "\n";
}

static void viewCFG(Function &F, const BlockFrequencyInfo *BFI,
                    const BranchProbabilityInfo *BPI, uint64_t MaxFreq,
                    bool CFGOnly = false) {
  DOTFuncInfo CFGInfo(&F, BFI, BPI, MaxFreq);
  CFGInfo.setHeatColors(ShowHeatColors);
  CFGInfo.setEdgeWeights(ShowEdgeWeight);
  CFGInfo.setRawEdgeWeights(UseRawEdgeWeight);

  ViewGraph(&CFGInfo, "cfg." + F.getName(), CFGOnly);
}

PreservedAnalyses CFGViewerPass::run(Function &F, FunctionAnalysisManager &AM) {
  if (!CFGFuncName.empty() && !F.getName().contains(CFGFuncName))
    return PreservedAnalyses::all();
  auto *BFI = &AM.getResult<BlockFrequencyAnalysis>(F);
  auto *BPI = &AM.getResult<BranchProbabilityAnalysis>(F);
  viewCFG(F, BFI, BPI, getMaxFreq(F, BFI));
  return PreservedAnalyses::all();
}

PreservedAnalyses CFGOnlyViewerPass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  if (!CFGFuncName.empty() && !F.getName().contains(CFGFuncName))
    return PreservedAnalyses::all();
  auto *BFI = &AM.getResult<BlockFrequencyAnalysis>(F);
  auto *BPI = &AM.getResult<BranchProbabilityAnalysis>(F);
  viewCFG(F, BFI, BPI, getMaxFreq(F, BFI), /*CFGOnly=*/true);
  return PreservedAnalyses::all();
}

PreservedAnalyses CFGPrinterPass::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  if (!CFGFuncName.empty() && !F.getName().contains(CFGFuncName))
    return PreservedAnalyses::all();
  auto *BFI = &AM.getResult<BlockFrequencyAnalysis>(F);
  auto *BPI = &AM.getResult<BranchProbabilityAnalysis>(F);
  writeCFGToDotFile(F, BFI, BPI, getMaxFreq(F, BFI));
  return PreservedAnalyses::all();
}

PreservedAnalyses CFGOnlyPrinterPass::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  if (!CFGFuncName.empty() && !F.getName().contains(CFGFuncName))
    return PreservedAnalyses::all();
  auto *BFI = &AM.getResult<BlockFrequencyAnalysis>(F);
  auto *BPI = &AM.getResult<BranchProbabilityAnalysis>(F);
  writeCFGToDotFile(F, BFI, BPI, getMaxFreq(F, BFI), /*CFGOnly=*/true);
  return PreservedAnalyses::all();
}

/// viewCFG - This function is meant for use from the debugger.  You can just
/// say 'call F->viewCFG()' and a ghostview window should pop up from the
/// program, displaying the CFG of the current function.  This depends on there
/// being a 'dot' and 'gv' program in your path.
///
void Function::viewCFG() const { viewCFG(false, nullptr, nullptr); }

void Function::viewCFG(bool ViewCFGOnly, const BlockFrequencyInfo *BFI,
                       const BranchProbabilityInfo *BPI) const {
  if (!CFGFuncName.empty() && !getName().contains(CFGFuncName))
    return;
  DOTFuncInfo CFGInfo(this, BFI, BPI, BFI ? getMaxFreq(*this, BFI) : 0);
  ViewGraph(&CFGInfo, "cfg" + getName(), ViewCFGOnly);
}

/// viewCFGOnly - This function is meant for use from the debugger.  It works
/// just like viewCFG, but it does not include the contents of basic blocks
/// into the nodes, just the label.  If you are only interested in the CFG
/// this can make the graph smaller.
///
void Function::viewCFGOnly() const { viewCFGOnly(nullptr, nullptr); }

void Function::viewCFGOnly(const BlockFrequencyInfo *BFI,
                           const BranchProbabilityInfo *BPI) const {
  viewCFG(true, BFI, BPI);
}

/// Find all blocks on the paths which terminate with a deoptimize or 
/// unreachable (i.e. all blocks which are post-dominated by a deoptimize 
/// or unreachable). These paths are hidden if the corresponding cl::opts
/// are enabled.
void DOTGraphTraits<DOTFuncInfo *>::computeDeoptOrUnreachablePaths(
    const Function *F) {
  auto evaluateBB = [&](const BasicBlock *Node) {
    if (succ_empty(Node)) {
      const Instruction *TI = Node->getTerminator();
      isOnDeoptOrUnreachablePath[Node] =
          (HideUnreachablePaths && isa<UnreachableInst>(TI)) ||
          (HideDeoptimizePaths && Node->getTerminatingDeoptimizeCall());
      return;
    }
    isOnDeoptOrUnreachablePath[Node] =
        llvm::all_of(successors(Node), [this](const BasicBlock *BB) {
          return isOnDeoptOrUnreachablePath[BB];
        });
  };
  /// The post order traversal iteration is done to know the status of
  /// isOnDeoptOrUnreachablePath for all the successors on the current BB.
  llvm::for_each(post_order(&F->getEntryBlock()), evaluateBB);
}

bool DOTGraphTraits<DOTFuncInfo *>::isNodeHidden(const BasicBlock *Node,
                                                 const DOTFuncInfo *CFGInfo) {
  if (HideColdPaths.getNumOccurrences() > 0)
    if (auto *BFI = CFGInfo->getBFI()) {
      BlockFrequency NodeFreq = BFI->getBlockFreq(Node);
      BlockFrequency EntryFreq = BFI->getEntryFreq();
      // Hide blocks with relative frequency below HideColdPaths threshold.
      if ((double)NodeFreq.getFrequency() / EntryFreq.getFrequency() <
          HideColdPaths)
        return true;
    }
  if (HideUnreachablePaths || HideDeoptimizePaths) {
    if (!isOnDeoptOrUnreachablePath.contains(Node))
      computeDeoptOrUnreachablePaths(Node->getParent());
    return isOnDeoptOrUnreachablePath[Node];
  }
  return false;
}
