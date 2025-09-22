//===- CallGraphSCCPass.h - Pass that operates BU on call graph -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CallGraphSCCPass class, which is used for passes which
// are implemented as bottom-up traversals on the call graph.  Because there may
// be cycles in the call graph, passes of this type operate on the call-graph in
// SCC order: that is, they process function bottom-up, except for recursive
// functions, which they process all at once.
//
// These passes are inherently interprocedural, and are required to keep the
// call graph up-to-date if they do anything which could modify it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CALLGRAPHSCCPASS_H
#define LLVM_ANALYSIS_CALLGRAPHSCCPASS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Pass.h"
#include <vector>

namespace llvm {

class CallGraph;
class CallGraphNode;
class CallGraphSCC;
class PMStack;

class CallGraphSCCPass : public Pass {
public:
  explicit CallGraphSCCPass(char &pid) : Pass(PT_CallGraphSCC, pid) {}

  /// createPrinterPass - Get a pass that prints the Module
  /// corresponding to a CallGraph.
  Pass *createPrinterPass(raw_ostream &OS,
                          const std::string &Banner) const override;

  using llvm::Pass::doInitialization;
  using llvm::Pass::doFinalization;

  /// doInitialization - This method is called before the SCC's of the program
  /// has been processed, allowing the pass to do initialization as necessary.
  virtual bool doInitialization(CallGraph &CG) {
    return false;
  }

  /// runOnSCC - This method should be implemented by the subclass to perform
  /// whatever action is necessary for the specified SCC.  Note that
  /// non-recursive (or only self-recursive) functions will have an SCC size of
  /// 1, where recursive portions of the call graph will have SCC size > 1.
  ///
  /// SCC passes that add or delete functions to the SCC are required to update
  /// the SCC list, otherwise stale pointers may be dereferenced.
  virtual bool runOnSCC(CallGraphSCC &SCC) = 0;

  /// doFinalization - This method is called after the SCC's of the program has
  /// been processed, allowing the pass to do final cleanup as necessary.
  virtual bool doFinalization(CallGraph &CG) {
    return false;
  }

  /// Assign pass manager to manager this pass
  void assignPassManager(PMStack &PMS, PassManagerType PMT) override;

  ///  Return what kind of Pass Manager can manage this pass.
  PassManagerType getPotentialPassManagerType() const override {
    return PMT_CallGraphPassManager;
  }

  /// getAnalysisUsage - For this class, we declare that we require and preserve
  /// the call graph.  If the derived class implements this method, it should
  /// always explicitly call the implementation here.
  void getAnalysisUsage(AnalysisUsage &Info) const override;

protected:
  /// Optional passes call this function to check whether the pass should be
  /// skipped. This is the case when optimization bisect is over the limit.
  bool skipSCC(CallGraphSCC &SCC) const;
};

/// CallGraphSCC - This is a single SCC that a CallGraphSCCPass is run on.
class CallGraphSCC {
  const CallGraph &CG; // The call graph for this SCC.
  void *Context; // The CGPassManager object that is vending this.
  std::vector<CallGraphNode *> Nodes;

public:
  CallGraphSCC(CallGraph &cg, void *context) : CG(cg), Context(context) {}

  void initialize(ArrayRef<CallGraphNode *> NewNodes) {
    Nodes.assign(NewNodes.begin(), NewNodes.end());
  }

  bool isSingular() const { return Nodes.size() == 1; }
  unsigned size() const { return Nodes.size(); }

  /// ReplaceNode - This informs the SCC and the pass manager that the specified
  /// Old node has been deleted, and New is to be used in its place.
  void ReplaceNode(CallGraphNode *Old, CallGraphNode *New);

  /// DeleteNode - This informs the SCC and the pass manager that the specified
  /// Old node has been deleted.
  void DeleteNode(CallGraphNode *Old);

  using iterator = std::vector<CallGraphNode *>::const_iterator;

  iterator begin() const { return Nodes.begin(); }
  iterator end() const { return Nodes.end(); }

  const CallGraph &getCallGraph() { return CG; }
};

void initializeDummyCGSCCPassPass(PassRegistry &);

/// This pass is required by interprocedural register allocation. It forces
/// codegen to follow bottom up order on call graph.
class DummyCGSCCPass : public CallGraphSCCPass {
public:
  static char ID;

  DummyCGSCCPass() : CallGraphSCCPass(ID) {
    PassRegistry &Registry = *PassRegistry::getPassRegistry();
    initializeDummyCGSCCPassPass(Registry);
  }

  bool runOnSCC(CallGraphSCC &SCC) override { return false; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_CALLGRAPHSCCPASS_H
