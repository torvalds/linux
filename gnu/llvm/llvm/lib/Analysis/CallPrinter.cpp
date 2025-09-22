//===- CallPrinter.cpp - DOT printer for call graph -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines '-dot-callgraph', which emit a callgraph.<fnname>.dot
// containing the call graph of a module.
//
// There is also a pass available to directly call dotty ('-view-callgraph').
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CallPrinter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/HeatUtils.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/GraphWriter.h"

using namespace llvm;

namespace llvm {
template <class GraphType> struct GraphTraits;
} // namespace llvm

// This option shows static (relative) call counts.
// FIXME:
// Need to show real counts when profile data is available
static cl::opt<bool> ShowHeatColors("callgraph-heat-colors", cl::init(false),
                                    cl::Hidden,
                                    cl::desc("Show heat colors in call-graph"));

static cl::opt<bool>
    ShowEdgeWeight("callgraph-show-weights", cl::init(false), cl::Hidden,
                       cl::desc("Show edges labeled with weights"));

static cl::opt<bool>
    CallMultiGraph("callgraph-multigraph", cl::init(false), cl::Hidden,
            cl::desc("Show call-multigraph (do not remove parallel edges)"));

static cl::opt<std::string> CallGraphDotFilenamePrefix(
    "callgraph-dot-filename-prefix", cl::Hidden,
    cl::desc("The prefix used for the CallGraph dot file names."));

namespace llvm {

class CallGraphDOTInfo {
private:
  Module *M;
  CallGraph *CG;
  DenseMap<const Function *, uint64_t> Freq;
  uint64_t MaxFreq;

public:
  std::function<BlockFrequencyInfo *(Function &)> LookupBFI;

  CallGraphDOTInfo(Module *M, CallGraph *CG,
                   function_ref<BlockFrequencyInfo *(Function &)> LookupBFI)
      : M(M), CG(CG), LookupBFI(LookupBFI) {
    MaxFreq = 0;

    for (Function &F : M->getFunctionList()) {
      uint64_t localSumFreq = 0;
      SmallSet<Function *, 16> Callers;
      for (User *U : F.users())
        if (isa<CallInst>(U))
          Callers.insert(cast<Instruction>(U)->getFunction());
      for (Function *Caller : Callers)
        localSumFreq += getNumOfCalls(*Caller, F);
      if (localSumFreq >= MaxFreq)
        MaxFreq = localSumFreq;
      Freq[&F] = localSumFreq;
    }
    if (!CallMultiGraph)
      removeParallelEdges();
  }

  Module *getModule() const { return M; }

  CallGraph *getCallGraph() const { return CG; }

  uint64_t getFreq(const Function *F) { return Freq[F]; }

  uint64_t getMaxFreq() { return MaxFreq; }

private:
  void removeParallelEdges() {
    for (auto &I : (*CG)) {
      CallGraphNode *Node = I.second.get();

      bool FoundParallelEdge = true;
      while (FoundParallelEdge) {
        SmallSet<Function *, 16> Visited;
        FoundParallelEdge = false;
        for (auto CI = Node->begin(), CE = Node->end(); CI != CE; CI++) {
          if (!(Visited.insert(CI->second->getFunction())).second) {
            FoundParallelEdge = true;
            Node->removeCallEdge(CI);
            break;
          }
        }
      }
    }
  }
};

template <>
struct GraphTraits<CallGraphDOTInfo *>
    : public GraphTraits<const CallGraphNode *> {
  static NodeRef getEntryNode(CallGraphDOTInfo *CGInfo) {
    // Start at the external node!
    return CGInfo->getCallGraph()->getExternalCallingNode();
  }

  typedef std::pair<const Function *const, std::unique_ptr<CallGraphNode>>
      PairTy;
  static const CallGraphNode *CGGetValuePtr(const PairTy &P) {
    return P.second.get();
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef mapped_iterator<CallGraph::const_iterator, decltype(&CGGetValuePtr)>
      nodes_iterator;

  static nodes_iterator nodes_begin(CallGraphDOTInfo *CGInfo) {
    return nodes_iterator(CGInfo->getCallGraph()->begin(), &CGGetValuePtr);
  }
  static nodes_iterator nodes_end(CallGraphDOTInfo *CGInfo) {
    return nodes_iterator(CGInfo->getCallGraph()->end(), &CGGetValuePtr);
  }
};

template <>
struct DOTGraphTraits<CallGraphDOTInfo *> : public DefaultDOTGraphTraits {

  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(CallGraphDOTInfo *CGInfo) {
    return "Call graph: " +
           std::string(CGInfo->getModule()->getModuleIdentifier());
  }

  static bool isNodeHidden(const CallGraphNode *Node,
                           const CallGraphDOTInfo *CGInfo) {
    if (CallMultiGraph || Node->getFunction())
      return false;
    return true;
  }

  std::string getNodeLabel(const CallGraphNode *Node,
                           CallGraphDOTInfo *CGInfo) {
    if (Node == CGInfo->getCallGraph()->getExternalCallingNode())
      return "external caller";
    if (Node == CGInfo->getCallGraph()->getCallsExternalNode())
      return "external callee";

    if (Function *Func = Node->getFunction())
      return std::string(Func->getName());
    return "external node";
  }
  static const CallGraphNode *CGGetValuePtr(CallGraphNode::CallRecord P) {
    return P.second;
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef mapped_iterator<CallGraphNode::const_iterator,
                          decltype(&CGGetValuePtr)>
      nodes_iterator;

  std::string getEdgeAttributes(const CallGraphNode *Node, nodes_iterator I,
                                CallGraphDOTInfo *CGInfo) {
    if (!ShowEdgeWeight)
      return "";

    Function *Caller = Node->getFunction();
    if (Caller == nullptr || Caller->isDeclaration())
      return "";

    Function *Callee = (*I)->getFunction();
    if (Callee == nullptr)
      return "";

    uint64_t Counter = getNumOfCalls(*Caller, *Callee);
    double Width =
        1 + 2 * (double(Counter) / CGInfo->getMaxFreq());
    std::string Attrs = "label=\"" + std::to_string(Counter) +
                        "\" penwidth=" + std::to_string(Width);
    return Attrs;
  }

  std::string getNodeAttributes(const CallGraphNode *Node,
                                CallGraphDOTInfo *CGInfo) {
    Function *F = Node->getFunction();
    if (F == nullptr)
      return "";
    std::string attrs;
    if (ShowHeatColors) {
      uint64_t freq = CGInfo->getFreq(F);
      std::string color = getHeatColor(freq, CGInfo->getMaxFreq());
      std::string edgeColor = (freq <= (CGInfo->getMaxFreq() / 2))
                                  ? getHeatColor(0)
                                  : getHeatColor(1);
      attrs = "color=\"" + edgeColor + "ff\", style=filled, fillcolor=\"" +
              color + "80\"";
    }
    return attrs;
  }
};

} // namespace llvm

namespace {
void doCallGraphDOTPrinting(
    Module &M, function_ref<BlockFrequencyInfo *(Function &)> LookupBFI) {
  std::string Filename;
  if (!CallGraphDotFilenamePrefix.empty())
    Filename = (CallGraphDotFilenamePrefix + ".callgraph.dot");
  else
    Filename = (std::string(M.getModuleIdentifier()) + ".callgraph.dot");
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::OF_Text);

  CallGraph CG(M);
  CallGraphDOTInfo CFGInfo(&M, &CG, LookupBFI);

  if (!EC)
    WriteGraph(File, &CFGInfo);
  else
    errs() << "  error opening file for writing!";
  errs() << "\n";
}

void viewCallGraph(Module &M,
                   function_ref<BlockFrequencyInfo *(Function &)> LookupBFI) {
  CallGraph CG(M);
  CallGraphDOTInfo CFGInfo(&M, &CG, LookupBFI);

  std::string Title =
      DOTGraphTraits<CallGraphDOTInfo *>::getGraphName(&CFGInfo);
  ViewGraph(&CFGInfo, "callgraph", true, Title);
}
} // namespace

namespace llvm {
PreservedAnalyses CallGraphDOTPrinterPass::run(Module &M,
                                               ModuleAnalysisManager &AM) {
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto LookupBFI = [&FAM](Function &F) {
    return &FAM.getResult<BlockFrequencyAnalysis>(F);
  };

  doCallGraphDOTPrinting(M, LookupBFI);

  return PreservedAnalyses::all();
}

PreservedAnalyses CallGraphViewerPass::run(Module &M,
                                           ModuleAnalysisManager &AM) {

  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto LookupBFI = [&FAM](Function &F) {
    return &FAM.getResult<BlockFrequencyAnalysis>(F);
  };

  viewCallGraph(M, LookupBFI);

  return PreservedAnalyses::all();
}
} // namespace llvm

namespace {
// Viewer
class CallGraphViewer : public ModulePass {
public:
  static char ID;
  CallGraphViewer() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;
};

void CallGraphViewer::getAnalysisUsage(AnalysisUsage &AU) const {
  ModulePass::getAnalysisUsage(AU);
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.setPreservesAll();
}

bool CallGraphViewer::runOnModule(Module &M) {
  auto LookupBFI = [this](Function &F) {
    return &this->getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();
  };

  viewCallGraph(M, LookupBFI);

  return false;
}

// DOT Printer

class CallGraphDOTPrinter : public ModulePass {
public:
  static char ID;
  CallGraphDOTPrinter() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;
};

void CallGraphDOTPrinter::getAnalysisUsage(AnalysisUsage &AU) const {
  ModulePass::getAnalysisUsage(AU);
  AU.addRequired<BlockFrequencyInfoWrapperPass>();
  AU.setPreservesAll();
}

bool CallGraphDOTPrinter::runOnModule(Module &M) {
  auto LookupBFI = [this](Function &F) {
    return &this->getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();
  };

  doCallGraphDOTPrinting(M, LookupBFI);

  return false;
}

} // end anonymous namespace

char CallGraphViewer::ID = 0;
INITIALIZE_PASS(CallGraphViewer, "view-callgraph", "View call graph", false,
                false)

char CallGraphDOTPrinter::ID = 0;
INITIALIZE_PASS(CallGraphDOTPrinter, "dot-callgraph",
                "Print call graph to 'dot' file", false, false)

// Create methods available outside of this file, to use them
// "include/llvm/LinkAllPasses.h". Otherwise the pass would be deleted by
// the link time optimization.

ModulePass *llvm::createCallGraphViewerPass() { return new CallGraphViewer(); }

ModulePass *llvm::createCallGraphDOTPrinterPass() {
  return new CallGraphDOTPrinter();
}
