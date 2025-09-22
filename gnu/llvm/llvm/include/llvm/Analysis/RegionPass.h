//===- RegionPass.h - RegionPass class --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RegionPass class. All region based analysis,
// optimization and transformation passes are derived from RegionPass.
// This class is implemented following the some ideas of the LoopPass.h class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_REGIONPASS_H
#define LLVM_ANALYSIS_REGIONPASS_H

#include "llvm/IR/LegacyPassManagers.h"
#include "llvm/Pass.h"
#include <deque>

namespace llvm {
class Function;
class RGPassManager;
class Region;
class RegionInfo;

//===----------------------------------------------------------------------===//
/// A pass that runs on each Region in a function.
///
/// RegionPass is managed by RGPassManager.
class RegionPass : public Pass {
public:
  explicit RegionPass(char &pid) : Pass(PT_Region, pid) {}

  //===--------------------------------------------------------------------===//
  /// @name To be implemented by every RegionPass
  ///
  //@{
  /// Run the pass on a specific Region
  ///
  /// Accessing regions not contained in the current region is not allowed.
  ///
  /// @param R The region this pass is run on.
  /// @param RGM The RegionPassManager that manages this Pass.
  ///
  /// @return True if the pass modifies this Region.
  virtual bool runOnRegion(Region *R, RGPassManager &RGM) = 0;

  /// Get a pass to print the LLVM IR in the region.
  ///
  /// @param O      The output stream to print the Region.
  /// @param Banner The banner to separate different printed passes.
  ///
  /// @return The pass to print the LLVM IR in the region.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override;

  using llvm::Pass::doInitialization;
  using llvm::Pass::doFinalization;

  virtual bool doInitialization(Region *R, RGPassManager &RGM) { return false; }
  virtual bool doFinalization() { return false; }
  //@}

  //===--------------------------------------------------------------------===//
  /// @name PassManager API
  ///
  //@{
  void preparePassManager(PMStack &PMS) override;

  void assignPassManager(PMStack &PMS,
                         PassManagerType PMT = PMT_RegionPassManager) override;

  PassManagerType getPotentialPassManagerType() const override {
    return PMT_RegionPassManager;
  }
  //@}

protected:
  /// Optional passes call this function to check whether the pass should be
  /// skipped. This is the case when optimization bisect is over the limit.
  bool skipRegion(Region &R) const;
};

/// The pass manager to schedule RegionPasses.
class RGPassManager : public FunctionPass, public PMDataManager {
  std::deque<Region*> RQ;
  RegionInfo *RI;
  Region *CurrentRegion;

public:
  static char ID;
  explicit RGPassManager();

  /// Execute all of the passes scheduled for execution.
  ///
  /// @return True if any of the passes modifies the function.
  bool runOnFunction(Function &F) override;

  /// Pass Manager itself does not invalidate any analysis info.
  /// RGPassManager needs RegionInfo.
  void getAnalysisUsage(AnalysisUsage &Info) const override;

  StringRef getPassName() const override { return "Region Pass Manager"; }

  PMDataManager *getAsPMDataManager() override { return this; }
  Pass *getAsPass() override { return this; }

  /// Print passes managed by this manager.
  void dumpPassStructure(unsigned Offset) override;

  /// Get passes contained by this manager.
  Pass *getContainedPass(unsigned N) {
    assert(N < PassVector.size() && "Pass number out of range!");
    Pass *FP = static_cast<Pass *>(PassVector[N]);
    return FP;
  }

  PassManagerType getPassManagerType() const override {
    return PMT_RegionPassManager;
  }
};

} // End llvm namespace

#endif
