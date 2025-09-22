//======----------- WindowScheduler.cpp - window scheduler -------------======//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the Window Scheduling software pipelining algorithm.
//
// The concept of the window algorithm was first unveiled in Steven Muchnick's
// book, "Advanced Compiler Design And Implementation", and later elaborated
// upon in Venkatraman Govindaraju's report, "Implementation of Software
// Pipelining Using Window Scheduling".
//
// The window algorithm can be perceived as a modulo scheduling algorithm with a
// stage count of 2. It boasts a higher scheduling success rate in targets with
// severe resource conflicts when compared to the classic Swing Modulo
// Scheduling (SMS) algorithm. To align with the LLVM scheduling framework, we
// have enhanced the original window algorithm. The primary steps are as
// follows:
//
// 1. Instead of duplicating the original MBB twice as mentioned in the
// literature, we copy it three times, generating TripleMBB and the
// corresponding TripleDAG.
//
// 2. We establish a scheduling window on TripleMBB and execute list scheduling
// within it.
//
// 3. After multiple list scheduling, we select the best outcome and expand it
// into the final scheduling result.
//
// To cater to the needs of various targets, we have developed the window
// scheduler in a form that is easily derivable. We recommend employing this
// algorithm in targets with severe resource conflicts, and it can be utilized
// either before or after the Register Allocator (RA).
//
// The default implementation provided here is before RA. If it is to be used
// after RA, certain critical algorithm functions will need to be derived.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_WINDOWSCHEDULER_H
#define LLVM_CODEGEN_WINDOWSCHEDULER_H

#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

namespace llvm {

enum WindowSchedulingFlag {
  WS_Off,  /// Turn off window algorithm.
  WS_On,   /// Use window algorithm after SMS algorithm fails.
  WS_Force /// Use window algorithm instead of SMS algorithm.
};

/// The main class in the implementation of the target independent window
/// scheduler.
class WindowScheduler {
protected:
  MachineSchedContext *Context = nullptr;
  MachineFunction *MF = nullptr;
  MachineBasicBlock *MBB = nullptr;
  MachineLoop &Loop;
  const TargetSubtargetInfo *Subtarget = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  MachineRegisterInfo *MRI = nullptr;

  /// To innovatively identify the dependencies between MIs across two trips, we
  /// construct a DAG for a new MBB, which is created by copying the original
  /// MBB three times. We refer to this new MBB as 'TripleMBB' and the
  /// corresponding DAG as 'TripleDAG'.
  /// If the dependencies are more than two trips, we avoid applying window
  /// algorithm by identifying successive phis in the old MBB.
  std::unique_ptr<ScheduleDAGInstrs> TripleDAG;
  /// OriMIs keeps the MIs removed from the original MBB.
  SmallVector<MachineInstr *> OriMIs;
  /// TriMIs keeps the MIs of TripleMBB, which is used to restore TripleMBB.
  SmallVector<MachineInstr *> TriMIs;
  /// TriToOri keeps the mappings between the MI clones in TripleMBB and their
  /// original MI.
  DenseMap<MachineInstr *, MachineInstr *> TriToOri;
  /// OriToCycle keeps the mappings between the original MI and its issue cycle.
  DenseMap<MachineInstr *, int> OriToCycle;
  /// SchedResult keeps the result of each list scheduling, and the format of
  /// the tuple is <MI pointer, Cycle, Stage, Order ID>.
  SmallVector<std::tuple<MachineInstr *, int, int, int>, 256> SchedResult;
  /// SchedPhiNum records the number of phi in the original MBB, and the
  /// scheduling starts with MI after phis.
  unsigned SchedPhiNum = 0;
  /// SchedInstrNum records the MIs involved in scheduling in the original MBB,
  /// excluding debug instructions.
  unsigned SchedInstrNum = 0;
  /// BestII and BestOffset record the characteristics of the best scheduling
  /// result and are used together with SchedResult as the final window
  /// scheduling result.
  unsigned BestII = UINT_MAX;
  unsigned BestOffset = 0;
  /// BaseII is the II obtained when the window offset is SchedPhiNum. This
  /// offset is the initial position of the sliding window.
  unsigned BaseII = 0;

public:
  WindowScheduler(MachineSchedContext *C, MachineLoop &ML);
  virtual ~WindowScheduler() {}

  bool run();

protected:
  /// Two types of ScheduleDAGs are needed, one for creating dependency graphs
  /// only, and the other for list scheduling as determined by the target.
  virtual ScheduleDAGInstrs *
  createMachineScheduler(bool OnlyBuildGraph = false);
  /// Initializes the algorithm and determines if it can be executed.
  virtual bool initialize();
  /// Add some related processing before running window scheduling.
  virtual void preProcess();
  /// Add some related processing after running window scheduling.
  virtual void postProcess();
  /// Back up the MIs in the original MBB and remove them from MBB.
  void backupMBB();
  /// Erase the MIs in current MBB and restore the original MIs.
  void restoreMBB();
  /// Make three copies of the original MBB to generate a new TripleMBB.
  virtual void generateTripleMBB();
  /// Restore the order of MIs in TripleMBB after each list scheduling.
  virtual void restoreTripleMBB();
  /// Give the folding position in the window algorithm, where different
  /// heuristics can be used. It determines the performance and compilation time
  /// of the algorithm.
  virtual SmallVector<unsigned> getSearchIndexes(unsigned SearchNum,
                                                 unsigned SearchRatio);
  /// Calculate MIs execution cycle after list scheduling.
  virtual int calculateMaxCycle(ScheduleDAGInstrs &DAG, unsigned Offset);
  /// Calculate the stall cycle between two trips after list scheduling.
  virtual int calculateStallCycle(unsigned Offset, int MaxCycle);
  /// Analyzes the II value after each list scheduling.
  virtual unsigned analyseII(ScheduleDAGInstrs &DAG, unsigned Offset);
  /// Phis are scheduled separately after each list scheduling.
  virtual void schedulePhi(int Offset, unsigned &II);
  /// Get the final issue order of all scheduled MIs including phis.
  DenseMap<MachineInstr *, int> getIssueOrder(unsigned Offset, unsigned II);
  /// Update the scheduling result after each list scheduling.
  virtual void updateScheduleResult(unsigned Offset, unsigned II);
  /// Check whether the final result of window scheduling is valid.
  virtual bool isScheduleValid() { return BestOffset != SchedPhiNum; }
  /// Using the scheduling infrastructure to expand the results of window
  /// scheduling. It is usually necessary to add prologue and epilogue MBBs.
  virtual void expand();
  /// Update the live intervals for all registers used within MBB.
  virtual void updateLiveIntervals();
  /// Estimate a II value at which all MIs will be scheduled successfully.
  int getEstimatedII(ScheduleDAGInstrs &DAG);
  /// Gets the iterator range of MIs in the scheduling window.
  iterator_range<MachineBasicBlock::iterator> getScheduleRange(unsigned Offset,
                                                               unsigned Num);
  /// Get the issue cycle of the new MI based on the cycle of the original MI.
  int getOriCycle(MachineInstr *NewMI);
  /// Get the original MI from which the new MI is cloned.
  MachineInstr *getOriMI(MachineInstr *NewMI);
  /// Get the scheduling stage, where the stage of the new MI is identical to
  /// the original MI.
  unsigned getOriStage(MachineInstr *OriMI, unsigned Offset);
  /// Gets the register in phi which is generated from the current MBB.
  Register getAntiRegister(MachineInstr *Phi);
};
} // namespace llvm
#endif
