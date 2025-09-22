//===- CallGraphSCCPass.cpp - Pass that operates BU on call graph ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CallGraphSCCPass class, which is used for passes
// which are implemented as bottom-up traversals on the call graph.  Because
// there may be cycles in the call graph, passes of this type operate on the
// call-graph in SCC order: that is, they process function bottom-up, except for
// recursive functions, which they process all at once.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/AbstractCallSite.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManagers.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "cgscc-passmgr"

namespace llvm {
cl::opt<unsigned> MaxDevirtIterations("max-devirt-iterations", cl::ReallyHidden,
                                      cl::init(4));
} // namespace llvm

STATISTIC(MaxSCCIterations, "Maximum CGSCCPassMgr iterations on one SCC");

//===----------------------------------------------------------------------===//
// CGPassManager
//
/// CGPassManager manages FPPassManagers and CallGraphSCCPasses.

namespace {

class CGPassManager : public ModulePass, public PMDataManager {
public:
  static char ID;

  explicit CGPassManager() : ModulePass(ID) {}

  /// Execute all of the passes scheduled for execution.  Keep track of
  /// whether any of the passes modifies the module, and if so, return true.
  bool runOnModule(Module &M) override;

  using ModulePass::doInitialization;
  using ModulePass::doFinalization;

  bool doInitialization(CallGraph &CG);
  bool doFinalization(CallGraph &CG);

  /// Pass Manager itself does not invalidate any analysis info.
  void getAnalysisUsage(AnalysisUsage &Info) const override {
    // CGPassManager walks SCC and it needs CallGraph.
    Info.addRequired<CallGraphWrapperPass>();
    Info.setPreservesAll();
  }

  StringRef getPassName() const override { return "CallGraph Pass Manager"; }

  PMDataManager *getAsPMDataManager() override { return this; }
  Pass *getAsPass() override { return this; }

  // Print passes managed by this manager
  void dumpPassStructure(unsigned Offset) override {
    errs().indent(Offset*2) << "Call Graph SCC Pass Manager\n";
    for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
      Pass *P = getContainedPass(Index);
      P->dumpPassStructure(Offset + 1);
      dumpLastUses(P, Offset+1);
    }
  }

  Pass *getContainedPass(unsigned N) {
    assert(N < PassVector.size() && "Pass number out of range!");
    return static_cast<Pass *>(PassVector[N]);
  }

  PassManagerType getPassManagerType() const override {
    return PMT_CallGraphPassManager;
  }

private:
  bool RunAllPassesOnSCC(CallGraphSCC &CurSCC, CallGraph &CG,
                         bool &DevirtualizedCall);

  bool RunPassOnSCC(Pass *P, CallGraphSCC &CurSCC,
                    CallGraph &CG, bool &CallGraphUpToDate,
                    bool &DevirtualizedCall);
  bool RefreshCallGraph(const CallGraphSCC &CurSCC, CallGraph &CG,
                        bool IsCheckingMode);
};

} // end anonymous namespace.

char CGPassManager::ID = 0;

bool CGPassManager::RunPassOnSCC(Pass *P, CallGraphSCC &CurSCC,
                                 CallGraph &CG, bool &CallGraphUpToDate,
                                 bool &DevirtualizedCall) {
  bool Changed = false;
  PMDataManager *PM = P->getAsPMDataManager();
  Module &M = CG.getModule();

  if (!PM) {
    CallGraphSCCPass *CGSP = (CallGraphSCCPass *)P;
    if (!CallGraphUpToDate) {
      DevirtualizedCall |= RefreshCallGraph(CurSCC, CG, false);
      CallGraphUpToDate = true;
    }

    {
      unsigned InstrCount, SCCCount = 0;
      StringMap<std::pair<unsigned, unsigned>> FunctionToInstrCount;
      bool EmitICRemark = M.shouldEmitInstrCountChangedRemark();
      TimeRegion PassTimer(getPassTimer(CGSP));
      if (EmitICRemark)
        InstrCount = initSizeRemarkInfo(M, FunctionToInstrCount);
      Changed = CGSP->runOnSCC(CurSCC);

      if (EmitICRemark) {
        // FIXME: Add getInstructionCount to CallGraphSCC.
        SCCCount = M.getInstructionCount();
        // Is there a difference in the number of instructions in the module?
        if (SCCCount != InstrCount) {
          // Yep. Emit a remark and update InstrCount.
          int64_t Delta =
              static_cast<int64_t>(SCCCount) - static_cast<int64_t>(InstrCount);
          emitInstrCountChangedRemark(P, M, Delta, InstrCount,
                                      FunctionToInstrCount);
          InstrCount = SCCCount;
        }
      }
    }

    // After the CGSCCPass is done, when assertions are enabled, use
    // RefreshCallGraph to verify that the callgraph was correctly updated.
#ifndef NDEBUG
    if (Changed)
      RefreshCallGraph(CurSCC, CG, true);
#endif

    return Changed;
  }

  assert(PM->getPassManagerType() == PMT_FunctionPassManager &&
         "Invalid CGPassManager member");
  FPPassManager *FPP = (FPPassManager*)P;

  // Run pass P on all functions in the current SCC.
  for (CallGraphNode *CGN : CurSCC) {
    if (Function *F = CGN->getFunction()) {
      dumpPassInfo(P, EXECUTION_MSG, ON_FUNCTION_MSG, F->getName());
      {
        TimeRegion PassTimer(getPassTimer(FPP));
        Changed |= FPP->runOnFunction(*F);
      }
      F->getContext().yield();
    }
  }

  // The function pass(es) modified the IR, they may have clobbered the
  // callgraph.
  if (Changed && CallGraphUpToDate) {
    LLVM_DEBUG(dbgs() << "CGSCCPASSMGR: Pass Dirtied SCC: " << P->getPassName()
                      << '\n');
    CallGraphUpToDate = false;
  }
  return Changed;
}

/// Scan the functions in the specified CFG and resync the
/// callgraph with the call sites found in it.  This is used after
/// FunctionPasses have potentially munged the callgraph, and can be used after
/// CallGraphSCC passes to verify that they correctly updated the callgraph.
///
/// This function returns true if it devirtualized an existing function call,
/// meaning it turned an indirect call into a direct call.  This happens when
/// a function pass like GVN optimizes away stuff feeding the indirect call.
/// This never happens in checking mode.
bool CGPassManager::RefreshCallGraph(const CallGraphSCC &CurSCC, CallGraph &CG,
                                     bool CheckingMode) {
  DenseMap<Value *, CallGraphNode *> Calls;

  LLVM_DEBUG(dbgs() << "CGSCCPASSMGR: Refreshing SCC with " << CurSCC.size()
                    << " nodes:\n";
             for (CallGraphNode *CGN
                  : CurSCC) CGN->dump(););

  bool MadeChange = false;
  bool DevirtualizedCall = false;

  // Scan all functions in the SCC.
  unsigned FunctionNo = 0;
  for (CallGraphSCC::iterator SCCIdx = CurSCC.begin(), E = CurSCC.end();
       SCCIdx != E; ++SCCIdx, ++FunctionNo) {
    CallGraphNode *CGN = *SCCIdx;
    Function *F = CGN->getFunction();
    if (!F || F->isDeclaration()) continue;

    // Walk the function body looking for call sites.  Sync up the call sites in
    // CGN with those actually in the function.

    // Keep track of the number of direct and indirect calls that were
    // invalidated and removed.
    unsigned NumDirectRemoved = 0, NumIndirectRemoved = 0;

    CallGraphNode::iterator CGNEnd = CGN->end();

    auto RemoveAndCheckForDone = [&](CallGraphNode::iterator I) {
      // Just remove the edge from the set of callees, keep track of whether
      // I points to the last element of the vector.
      bool WasLast = I + 1 == CGNEnd;
      CGN->removeCallEdge(I);

      // If I pointed to the last element of the vector, we have to bail out:
      // iterator checking rejects comparisons of the resultant pointer with
      // end.
      if (WasLast)
        return true;

      CGNEnd = CGN->end();
      return false;
    };

    // Get the set of call sites currently in the function.
    for (CallGraphNode::iterator I = CGN->begin(); I != CGNEnd;) {
      // Delete "reference" call records that do not have call instruction. We
      // reinsert them as needed later. However, keep them in checking mode.
      if (!I->first) {
        if (CheckingMode) {
          ++I;
          continue;
        }
        if (RemoveAndCheckForDone(I))
          break;
        continue;
      }

      // If this call site is null, then the function pass deleted the call
      // entirely and the WeakTrackingVH nulled it out.
      auto *Call = dyn_cast_or_null<CallBase>(*I->first);
      if (!Call ||
          // If we've already seen this call site, then the FunctionPass RAUW'd
          // one call with another, which resulted in two "uses" in the edge
          // list of the same call.
          Calls.count(Call)) {
        assert(!CheckingMode &&
               "CallGraphSCCPass did not update the CallGraph correctly!");

        // If this was an indirect call site, count it.
        if (!I->second->getFunction())
          ++NumIndirectRemoved;
        else
          ++NumDirectRemoved;

        if (RemoveAndCheckForDone(I))
          break;
        continue;
      }

      assert(!Calls.count(Call) && "Call site occurs in node multiple times");

      if (Call) {
        Function *Callee = Call->getCalledFunction();
        // Ignore intrinsics because they're not really function calls.
        if (!Callee || !(Callee->isIntrinsic()))
          Calls.insert(std::make_pair(Call, I->second));
      }
      ++I;
    }

    // Loop over all of the instructions in the function, getting the callsites.
    // Keep track of the number of direct/indirect calls added.
    unsigned NumDirectAdded = 0, NumIndirectAdded = 0;

    for (BasicBlock &BB : *F)
      for (Instruction &I : BB) {
        auto *Call = dyn_cast<CallBase>(&I);
        if (!Call)
          continue;
        Function *Callee = Call->getCalledFunction();
        if (Callee && Callee->isIntrinsic())
          continue;

        // If we are not in checking mode, insert potential callback calls as
        // references. This is not a requirement but helps to iterate over the
        // functions in the right order.
        if (!CheckingMode) {
          forEachCallbackFunction(*Call, [&](Function *CB) {
            CGN->addCalledFunction(nullptr, CG.getOrInsertFunction(CB));
          });
        }

        // If this call site already existed in the callgraph, just verify it
        // matches up to expectations and remove it from Calls.
        DenseMap<Value *, CallGraphNode *>::iterator ExistingIt =
            Calls.find(Call);
        if (ExistingIt != Calls.end()) {
          CallGraphNode *ExistingNode = ExistingIt->second;

          // Remove from Calls since we have now seen it.
          Calls.erase(ExistingIt);

          // Verify that the callee is right.
          if (ExistingNode->getFunction() == Call->getCalledFunction())
            continue;

          // If we are in checking mode, we are not allowed to actually mutate
          // the callgraph.  If this is a case where we can infer that the
          // callgraph is less precise than it could be (e.g. an indirect call
          // site could be turned direct), don't reject it in checking mode, and
          // don't tweak it to be more precise.
          if (CheckingMode && Call->getCalledFunction() &&
              ExistingNode->getFunction() == nullptr)
            continue;

          assert(!CheckingMode &&
                 "CallGraphSCCPass did not update the CallGraph correctly!");

          // If not, we either went from a direct call to indirect, indirect to
          // direct, or direct to different direct.
          CallGraphNode *CalleeNode;
          if (Function *Callee = Call->getCalledFunction()) {
            CalleeNode = CG.getOrInsertFunction(Callee);
            // Keep track of whether we turned an indirect call into a direct
            // one.
            if (!ExistingNode->getFunction()) {
              DevirtualizedCall = true;
              LLVM_DEBUG(dbgs() << "  CGSCCPASSMGR: Devirtualized call to '"
                                << Callee->getName() << "'\n");
            }
          } else {
            CalleeNode = CG.getCallsExternalNode();
          }

          // Update the edge target in CGN.
          CGN->replaceCallEdge(*Call, *Call, CalleeNode);
          MadeChange = true;
          continue;
        }

        assert(!CheckingMode &&
               "CallGraphSCCPass did not update the CallGraph correctly!");

        // If the call site didn't exist in the CGN yet, add it.
        CallGraphNode *CalleeNode;
        if (Function *Callee = Call->getCalledFunction()) {
          CalleeNode = CG.getOrInsertFunction(Callee);
          ++NumDirectAdded;
        } else {
          CalleeNode = CG.getCallsExternalNode();
          ++NumIndirectAdded;
        }

        CGN->addCalledFunction(Call, CalleeNode);
        MadeChange = true;
      }

    // We scanned the old callgraph node, removing invalidated call sites and
    // then added back newly found call sites.  One thing that can happen is
    // that an old indirect call site was deleted and replaced with a new direct
    // call.  In this case, we have devirtualized a call, and CGSCCPM would like
    // to iteratively optimize the new code.  Unfortunately, we don't really
    // have a great way to detect when this happens.  As an approximation, we
    // just look at whether the number of indirect calls is reduced and the
    // number of direct calls is increased.  There are tons of ways to fool this
    // (e.g. DCE'ing an indirect call and duplicating an unrelated block with a
    // direct call) but this is close enough.
    if (NumIndirectRemoved > NumIndirectAdded &&
        NumDirectRemoved < NumDirectAdded)
      DevirtualizedCall = true;

    // After scanning this function, if we still have entries in callsites, then
    // they are dangling pointers.  WeakTrackingVH should save us for this, so
    // abort if
    // this happens.
    assert(Calls.empty() && "Dangling pointers found in call sites map");

    // Periodically do an explicit clear to remove tombstones when processing
    // large scc's.
    if ((FunctionNo & 15) == 15)
      Calls.clear();
  }

  LLVM_DEBUG(if (MadeChange) {
    dbgs() << "CGSCCPASSMGR: Refreshed SCC is now:\n";
    for (CallGraphNode *CGN : CurSCC)
      CGN->dump();
    if (DevirtualizedCall)
      dbgs() << "CGSCCPASSMGR: Refresh devirtualized a call!\n";
  } else {
    dbgs() << "CGSCCPASSMGR: SCC Refresh didn't change call graph.\n";
  });
  (void)MadeChange;

  return DevirtualizedCall;
}

/// Execute the body of the entire pass manager on the specified SCC.
/// This keeps track of whether a function pass devirtualizes
/// any calls and returns it in DevirtualizedCall.
bool CGPassManager::RunAllPassesOnSCC(CallGraphSCC &CurSCC, CallGraph &CG,
                                      bool &DevirtualizedCall) {
  bool Changed = false;

  // Keep track of whether the callgraph is known to be up-to-date or not.
  // The CGSSC pass manager runs two types of passes:
  // CallGraphSCC Passes and other random function passes.  Because other
  // random function passes are not CallGraph aware, they may clobber the
  // call graph by introducing new calls or deleting other ones.  This flag
  // is set to false when we run a function pass so that we know to clean up
  // the callgraph when we need to run a CGSCCPass again.
  bool CallGraphUpToDate = true;

  // Run all passes on current SCC.
  for (unsigned PassNo = 0, e = getNumContainedPasses();
       PassNo != e; ++PassNo) {
    Pass *P = getContainedPass(PassNo);

    // If we're in -debug-pass=Executions mode, construct the SCC node list,
    // otherwise avoid constructing this string as it is expensive.
    if (isPassDebuggingExecutionsOrMore()) {
      std::string Functions;
  #ifndef NDEBUG
      raw_string_ostream OS(Functions);
      ListSeparator LS;
      for (const CallGraphNode *CGN : CurSCC) {
        OS << LS;
        CGN->print(OS);
      }
      OS.flush();
  #endif
      dumpPassInfo(P, EXECUTION_MSG, ON_CG_MSG, Functions);
    }
    dumpRequiredSet(P);

    initializeAnalysisImpl(P);

#ifdef EXPENSIVE_CHECKS
    uint64_t RefHash = P->structuralHash(CG.getModule());
#endif

    // Actually run this pass on the current SCC.
    bool LocalChanged =
        RunPassOnSCC(P, CurSCC, CG, CallGraphUpToDate, DevirtualizedCall);

    Changed |= LocalChanged;

#ifdef EXPENSIVE_CHECKS
    if (!LocalChanged && (RefHash != P->structuralHash(CG.getModule()))) {
      llvm::errs() << "Pass modifies its input and doesn't report it: "
                   << P->getPassName() << "\n";
      llvm_unreachable("Pass modifies its input and doesn't report it");
    }
#endif
    if (LocalChanged)
      dumpPassInfo(P, MODIFICATION_MSG, ON_CG_MSG, "");
    dumpPreservedSet(P);

    verifyPreservedAnalysis(P);
    if (LocalChanged)
      removeNotPreservedAnalysis(P);
    recordAvailableAnalysis(P);
    removeDeadPasses(P, "", ON_CG_MSG);
  }

  // If the callgraph was left out of date (because the last pass run was a
  // functionpass), refresh it before we move on to the next SCC.
  if (!CallGraphUpToDate)
    DevirtualizedCall |= RefreshCallGraph(CurSCC, CG, false);
  return Changed;
}

/// Execute all of the passes scheduled for execution.  Keep track of
/// whether any of the passes modifies the module, and if so, return true.
bool CGPassManager::runOnModule(Module &M) {
  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  bool Changed = doInitialization(CG);

  // Walk the callgraph in bottom-up SCC order.
  scc_iterator<CallGraph*> CGI = scc_begin(&CG);

  CallGraphSCC CurSCC(CG, &CGI);
  while (!CGI.isAtEnd()) {
    // Copy the current SCC and increment past it so that the pass can hack
    // on the SCC if it wants to without invalidating our iterator.
    const std::vector<CallGraphNode *> &NodeVec = *CGI;
    CurSCC.initialize(NodeVec);
    ++CGI;

    // At the top level, we run all the passes in this pass manager on the
    // functions in this SCC.  However, we support iterative compilation in the
    // case where a function pass devirtualizes a call to a function.  For
    // example, it is very common for a function pass (often GVN or instcombine)
    // to eliminate the addressing that feeds into a call.  With that improved
    // information, we would like the call to be an inline candidate, infer
    // mod-ref information etc.
    //
    // Because of this, we allow iteration up to a specified iteration count.
    // This only happens in the case of a devirtualized call, so we only burn
    // compile time in the case that we're making progress.  We also have a hard
    // iteration count limit in case there is crazy code.
    unsigned Iteration = 0;
    bool DevirtualizedCall = false;
    do {
      LLVM_DEBUG(if (Iteration) dbgs()
                 << "  SCCPASSMGR: Re-visiting SCC, iteration #" << Iteration
                 << '\n');
      DevirtualizedCall = false;
      Changed |= RunAllPassesOnSCC(CurSCC, CG, DevirtualizedCall);
    } while (Iteration++ < MaxDevirtIterations && DevirtualizedCall);

    if (DevirtualizedCall)
      LLVM_DEBUG(dbgs() << "  CGSCCPASSMGR: Stopped iteration after "
                        << Iteration
                        << " times, due to -max-devirt-iterations\n");

    MaxSCCIterations.updateMax(Iteration);
  }
  Changed |= doFinalization(CG);
  return Changed;
}

/// Initialize CG
bool CGPassManager::doInitialization(CallGraph &CG) {
  bool Changed = false;
  for (unsigned i = 0, e = getNumContainedPasses(); i != e; ++i) {
    if (PMDataManager *PM = getContainedPass(i)->getAsPMDataManager()) {
      assert(PM->getPassManagerType() == PMT_FunctionPassManager &&
             "Invalid CGPassManager member");
      Changed |= ((FPPassManager*)PM)->doInitialization(CG.getModule());
    } else {
      Changed |= ((CallGraphSCCPass*)getContainedPass(i))->doInitialization(CG);
    }
  }
  return Changed;
}

/// Finalize CG
bool CGPassManager::doFinalization(CallGraph &CG) {
  bool Changed = false;
  for (unsigned i = 0, e = getNumContainedPasses(); i != e; ++i) {
    if (PMDataManager *PM = getContainedPass(i)->getAsPMDataManager()) {
      assert(PM->getPassManagerType() == PMT_FunctionPassManager &&
             "Invalid CGPassManager member");
      Changed |= ((FPPassManager*)PM)->doFinalization(CG.getModule());
    } else {
      Changed |= ((CallGraphSCCPass*)getContainedPass(i))->doFinalization(CG);
    }
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
// CallGraphSCC Implementation
//===----------------------------------------------------------------------===//

/// This informs the SCC and the pass manager that the specified
/// Old node has been deleted, and New is to be used in its place.
void CallGraphSCC::ReplaceNode(CallGraphNode *Old, CallGraphNode *New) {
  assert(Old != New && "Should not replace node with self");
  for (unsigned i = 0; ; ++i) {
    assert(i != Nodes.size() && "Node not in SCC");
    if (Nodes[i] != Old) continue;
    if (New)
      Nodes[i] = New;
    else
      Nodes.erase(Nodes.begin() + i);
    break;
  }

  // Update the active scc_iterator so that it doesn't contain dangling
  // pointers to the old CallGraphNode.
  scc_iterator<CallGraph*> *CGI = (scc_iterator<CallGraph*>*)Context;
  CGI->ReplaceNode(Old, New);
}

void CallGraphSCC::DeleteNode(CallGraphNode *Old) {
  ReplaceNode(Old, /*New=*/nullptr);
}

//===----------------------------------------------------------------------===//
// CallGraphSCCPass Implementation
//===----------------------------------------------------------------------===//

/// Assign pass manager to manage this pass.
void CallGraphSCCPass::assignPassManager(PMStack &PMS,
                                         PassManagerType PreferredType) {
  // Find CGPassManager
  while (!PMS.empty() &&
         PMS.top()->getPassManagerType() > PMT_CallGraphPassManager)
    PMS.pop();

  assert(!PMS.empty() && "Unable to handle Call Graph Pass");
  CGPassManager *CGP;

  if (PMS.top()->getPassManagerType() == PMT_CallGraphPassManager)
    CGP = (CGPassManager*)PMS.top();
  else {
    // Create new Call Graph SCC Pass Manager if it does not exist.
    assert(!PMS.empty() && "Unable to create Call Graph Pass Manager");
    PMDataManager *PMD = PMS.top();

    // [1] Create new Call Graph Pass Manager
    CGP = new CGPassManager();

    // [2] Set up new manager's top level manager
    PMTopLevelManager *TPM = PMD->getTopLevelManager();
    TPM->addIndirectPassManager(CGP);

    // [3] Assign manager to manage this new manager. This may create
    // and push new managers into PMS
    Pass *P = CGP;
    TPM->schedulePass(P);

    // [4] Push new manager into PMS
    PMS.push(CGP);
  }

  CGP->add(this);
}

/// For this class, we declare that we require and preserve the call graph.
/// If the derived class implements this method, it should
/// always explicitly call the implementation here.
void CallGraphSCCPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<CallGraphWrapperPass>();
  AU.addPreserved<CallGraphWrapperPass>();
}

//===----------------------------------------------------------------------===//
// PrintCallGraphPass Implementation
//===----------------------------------------------------------------------===//

namespace {

  /// PrintCallGraphPass - Print a Module corresponding to a call graph.
  ///
  class PrintCallGraphPass : public CallGraphSCCPass {
    std::string Banner;
    raw_ostream &OS;       // raw_ostream to print on.

  public:
    static char ID;

    PrintCallGraphPass(const std::string &B, raw_ostream &OS)
      : CallGraphSCCPass(ID), Banner(B), OS(OS) {}

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

    bool runOnSCC(CallGraphSCC &SCC) override {
      bool BannerPrinted = false;
      auto PrintBannerOnce = [&]() {
        if (BannerPrinted)
          return;
        OS << Banner;
        BannerPrinted = true;
      };

      bool NeedModule = llvm::forcePrintModuleIR();
      if (isFunctionInPrintList("*") && NeedModule) {
        PrintBannerOnce();
        OS << "\n";
        SCC.getCallGraph().getModule().print(OS, nullptr);
        return false;
      }
      bool FoundFunction = false;
      for (CallGraphNode *CGN : SCC) {
        if (Function *F = CGN->getFunction()) {
          if (!F->isDeclaration() && isFunctionInPrintList(F->getName())) {
            FoundFunction = true;
            if (!NeedModule) {
              PrintBannerOnce();
              F->print(OS);
            }
          }
        } else if (isFunctionInPrintList("*")) {
          PrintBannerOnce();
          OS << "\nPrinting <null> Function\n";
        }
      }
      if (NeedModule && FoundFunction) {
        PrintBannerOnce();
        OS << "\n";
        SCC.getCallGraph().getModule().print(OS, nullptr);
      }
      return false;
    }

    StringRef getPassName() const override { return "Print CallGraph IR"; }
  };

} // end anonymous namespace.

char PrintCallGraphPass::ID = 0;

Pass *CallGraphSCCPass::createPrinterPass(raw_ostream &OS,
                                          const std::string &Banner) const {
  return new PrintCallGraphPass(Banner, OS);
}

static std::string getDescription(const CallGraphSCC &SCC) {
  std::string Desc = "SCC (";
  ListSeparator LS;
  for (CallGraphNode *CGN : SCC) {
    Desc += LS;
    Function *F = CGN->getFunction();
    if (F)
      Desc += F->getName();
    else
      Desc += "<<null function>>";
  }
  Desc += ")";
  return Desc;
}

bool CallGraphSCCPass::skipSCC(CallGraphSCC &SCC) const {
  OptPassGate &Gate =
      SCC.getCallGraph().getModule().getContext().getOptPassGate();
  return Gate.isEnabled() &&
         !Gate.shouldRunPass(this->getPassName(), getDescription(SCC));
}

char DummyCGSCCPass::ID = 0;

INITIALIZE_PASS(DummyCGSCCPass, "DummyCGSCCPass", "DummyCGSCCPass", false,
                false)
