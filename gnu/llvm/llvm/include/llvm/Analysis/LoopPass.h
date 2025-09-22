//===- LoopPass.h - LoopPass class ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines LoopPass class. All loop optimization
// and transformation passes are derived from LoopPass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPPASS_H
#define LLVM_ANALYSIS_LOOPPASS_H

#include "llvm/IR/LegacyPassManagers.h"
#include "llvm/Pass.h"
#include <deque>

namespace llvm {

class Loop;
class LoopInfo;
class LPPassManager;
class Function;

class LoopPass : public Pass {
public:
  explicit LoopPass(char &pid) : Pass(PT_Loop, pid) {}

  /// getPrinterPass - Get a pass to print the function corresponding
  /// to a Loop.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override;

  // runOnLoop - This method should be implemented by the subclass to perform
  // whatever action is necessary for the specified Loop.
  virtual bool runOnLoop(Loop *L, LPPassManager &LPM) = 0;

  using llvm::Pass::doInitialization;
  using llvm::Pass::doFinalization;

  // Initialization and finalization hooks.
  virtual bool doInitialization(Loop *L, LPPassManager &LPM) {
    return false;
  }

  // Finalization hook does not supply Loop because at this time
  // loop nest is completely different.
  virtual bool doFinalization() { return false; }

  // Check if this pass is suitable for the current LPPassManager, if
  // available. This pass P is not suitable for a LPPassManager if P
  // is not preserving higher level analysis info used by other
  // LPPassManager passes. In such case, pop LPPassManager from the
  // stack. This will force assignPassManager() to create new
  // LPPassManger as expected.
  void preparePassManager(PMStack &PMS) override;

  /// Assign pass manager to manage this pass
  void assignPassManager(PMStack &PMS, PassManagerType PMT) override;

  ///  Return what kind of Pass Manager can manage this pass.
  PassManagerType getPotentialPassManagerType() const override {
    return PMT_LoopPassManager;
  }

protected:
  /// Optional passes call this function to check whether the pass should be
  /// skipped. This is the case when Attribute::OptimizeNone is set or when
  /// optimization bisect is over the limit.
  bool skipLoop(const Loop *L) const;
};

class LPPassManager : public FunctionPass, public PMDataManager {
public:
  static char ID;
  explicit LPPassManager();

  /// run - Execute all of the passes scheduled for execution.  Keep track of
  /// whether any of the passes modifies the module, and if so, return true.
  bool runOnFunction(Function &F) override;

  /// Pass Manager itself does not invalidate any analysis info.
  // LPPassManager needs LoopInfo.
  void getAnalysisUsage(AnalysisUsage &Info) const override;

  StringRef getPassName() const override { return "Loop Pass Manager"; }

  PMDataManager *getAsPMDataManager() override { return this; }
  Pass *getAsPass() override { return this; }

  /// Print passes managed by this manager
  void dumpPassStructure(unsigned Offset) override;

  LoopPass *getContainedPass(unsigned N) {
    assert(N < PassVector.size() && "Pass number out of range!");
    LoopPass *LP = static_cast<LoopPass *>(PassVector[N]);
    return LP;
  }

  PassManagerType getPassManagerType() const override {
    return PMT_LoopPassManager;
  }

public:
  // Add a new loop into the loop queue.
  void addLoop(Loop &L);

  // Mark \p L as deleted.
  void markLoopAsDeleted(Loop &L);

private:
  std::deque<Loop *> LQ;
  LoopInfo *LI;
  Loop *CurrentLoop;
  bool CurrentLoopDeleted;
};

// This pass is required by the LCSSA transformation. It is used inside
// LPPassManager to check if current pass preserves LCSSA form, and if it does
// pass manager calls lcssa verification for the current loop.
struct LCSSAVerificationPass : public FunctionPass {
  static char ID;
  LCSSAVerificationPass();

  bool runOnFunction(Function &F) override { return false; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // End llvm namespace

#endif
