//===--------------------- Scheduler.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A scheduler for Processor Resource Units and Processor Resource Groups.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HARDWAREUNITS_SCHEDULER_H
#define LLVM_MCA_HARDWAREUNITS_SCHEDULER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"
#include "llvm/MCA/HardwareUnits/LSUnit.h"
#include "llvm/MCA/HardwareUnits/ResourceManager.h"
#include "llvm/MCA/Support.h"

namespace llvm {
namespace mca {

class SchedulerStrategy {
public:
  SchedulerStrategy() = default;
  virtual ~SchedulerStrategy();

  /// Returns true if Lhs should take priority over Rhs.
  ///
  /// This method is used by class Scheduler to select the "best" ready
  /// instruction to issue to the underlying pipelines.
  virtual bool compare(const InstRef &Lhs, const InstRef &Rhs) const = 0;
};

/// Default instruction selection strategy used by class Scheduler.
class DefaultSchedulerStrategy : public SchedulerStrategy {
  /// This method ranks instructions based on their age, and the number of known
  /// users. The lower the rank value, the better.
  int computeRank(const InstRef &Lhs) const {
    return Lhs.getSourceIndex() - Lhs.getInstruction()->getNumUsers();
  }

public:
  DefaultSchedulerStrategy() = default;
  virtual ~DefaultSchedulerStrategy();

  bool compare(const InstRef &Lhs, const InstRef &Rhs) const override {
    int LhsRank = computeRank(Lhs);
    int RhsRank = computeRank(Rhs);

    /// Prioritize older instructions over younger instructions to minimize the
    /// pressure on the reorder buffer.
    if (LhsRank == RhsRank)
      return Lhs.getSourceIndex() < Rhs.getSourceIndex();
    return LhsRank < RhsRank;
  }
};

/// Class Scheduler is responsible for issuing instructions to pipeline
/// resources.
///
/// Internally, it delegates to a ResourceManager the management of processor
/// resources. This class is also responsible for tracking the progress of
/// instructions from the dispatch stage, until the write-back stage.
///
class Scheduler : public HardwareUnit {
  LSUnitBase &LSU;

  // Instruction selection strategy for this Scheduler.
  std::unique_ptr<SchedulerStrategy> Strategy;

  // Hardware resources that are managed by this scheduler.
  std::unique_ptr<ResourceManager> Resources;

  // Instructions dispatched to the Scheduler are internally classified based on
  // the instruction stage (see Instruction::InstrStage).
  //
  // An Instruction dispatched to the Scheduler is added to the WaitSet if not
  // all its register operands are available, and at least one latency is
  // unknown.  By construction, the WaitSet only contains instructions that are
  // in the IS_DISPATCHED stage.
  //
  // An Instruction transitions from the WaitSet to the PendingSet if the
  // instruction is not ready yet, but the latency of every register read is
  // known.  Instructions in the PendingSet can only be in the IS_PENDING or
  // IS_READY stage.  Only IS_READY instructions that are waiting on memory
  // dependencies can be added to the PendingSet.
  //
  // Instructions in the PendingSet are immediately dominated only by
  // instructions that have already been issued to the underlying pipelines.  In
  // the presence of bottlenecks caused by data dependencies, the PendingSet can
  // be inspected to identify problematic data dependencies between
  // instructions.
  //
  // An instruction is moved to the ReadySet when all register operands become
  // available, and all memory dependencies are met.  Instructions that are
  // moved from the PendingSet to the ReadySet must transition to the 'IS_READY'
  // stage.
  //
  // On every cycle, the Scheduler checks if it can promote instructions from the
  // PendingSet to the ReadySet.
  //
  // An Instruction is moved from the ReadySet to the `IssuedSet` when it starts
  // exection. This event also causes an instruction state transition (i.e. from
  // state IS_READY, to state IS_EXECUTING). An Instruction leaves the IssuedSet
  // only when it reaches the write-back stage.
  std::vector<InstRef> WaitSet;
  std::vector<InstRef> PendingSet;
  std::vector<InstRef> ReadySet;
  std::vector<InstRef> IssuedSet;

  // A mask of busy resource units. It defaults to the empty set (i.e. a zero
  // mask), and it is cleared at the beginning of every cycle.
  // It is updated every time the scheduler fails to issue an instruction from
  // the ready set due to unavailable pipeline resources.
  // Each bit of the mask represents an unavailable resource.
  uint64_t BusyResourceUnits;

  // Counts the number of instructions in the pending set that were dispatched
  // during this cycle.
  unsigned NumDispatchedToThePendingSet;

  // True if the previous pipeline Stage was unable to dispatch a full group of
  // opcodes because scheduler buffers (or LS queues) were unavailable.
  bool HadTokenStall;

  /// Verify the given selection strategy and set the Strategy member
  /// accordingly.  If no strategy is provided, the DefaultSchedulerStrategy is
  /// used.
  void initializeStrategy(std::unique_ptr<SchedulerStrategy> S);

  /// Issue an instruction without updating the ready queue.
  void issueInstructionImpl(
      InstRef &IR,
      SmallVectorImpl<std::pair<ResourceRef, ReleaseAtCycles>> &Pipes);

  // Identify instructions that have finished executing, and remove them from
  // the IssuedSet. References to executed instructions are added to input
  // vector 'Executed'.
  void updateIssuedSet(SmallVectorImpl<InstRef> &Executed);

  // Try to promote instructions from the PendingSet to the ReadySet.
  // Add promoted instructions to the 'Ready' vector in input.
  // Returns true if at least one instruction was promoted.
  bool promoteToReadySet(SmallVectorImpl<InstRef> &Ready);

  // Try to promote instructions from the WaitSet to the PendingSet.
  // Add promoted instructions to the 'Pending' vector in input.
  // Returns true if at least one instruction was promoted.
  bool promoteToPendingSet(SmallVectorImpl<InstRef> &Pending);

public:
  Scheduler(const MCSchedModel &Model, LSUnitBase &Lsu)
      : Scheduler(Model, Lsu, nullptr) {}

  Scheduler(const MCSchedModel &Model, LSUnitBase &Lsu,
            std::unique_ptr<SchedulerStrategy> SelectStrategy)
      : Scheduler(std::make_unique<ResourceManager>(Model), Lsu,
                  std::move(SelectStrategy)) {}

  Scheduler(std::unique_ptr<ResourceManager> RM, LSUnitBase &Lsu,
            std::unique_ptr<SchedulerStrategy> SelectStrategy)
      : LSU(Lsu), Resources(std::move(RM)), BusyResourceUnits(0),
        NumDispatchedToThePendingSet(0), HadTokenStall(false) {
    initializeStrategy(std::move(SelectStrategy));
  }

  // Stalls generated by the scheduler.
  enum Status {
    SC_AVAILABLE,
    SC_LOAD_QUEUE_FULL,
    SC_STORE_QUEUE_FULL,
    SC_BUFFERS_FULL,
    SC_DISPATCH_GROUP_STALL,
  };

  /// Check if the instruction in 'IR' can be dispatched during this cycle.
  /// Return SC_AVAILABLE if both scheduler and LS resources are available.
  ///
  /// This method is also responsible for setting field HadTokenStall if
  /// IR cannot be dispatched to the Scheduler due to unavailable resources.
  Status isAvailable(const InstRef &IR);

  /// Reserves buffer and LSUnit queue resources that are necessary to issue
  /// this instruction.
  ///
  /// Returns true if instruction IR is ready to be issued to the underlying
  /// pipelines. Note that this operation cannot fail; it assumes that a
  /// previous call to method `isAvailable(IR)` returned `SC_AVAILABLE`.
  ///
  /// If IR is a memory operation, then the Scheduler queries the LS unit to
  /// obtain a LS token. An LS token is used internally to track memory
  /// dependencies.
  bool dispatch(InstRef &IR);

  /// Issue an instruction and populates a vector of used pipeline resources,
  /// and a vector of instructions that transitioned to the ready state as a
  /// result of this event.
  void issueInstruction(
      InstRef &IR,
      SmallVectorImpl<std::pair<ResourceRef, ReleaseAtCycles>> &Used,
      SmallVectorImpl<InstRef> &Pending,
      SmallVectorImpl<InstRef> &Ready);

  /// Returns true if IR has to be issued immediately, or if IR is a zero
  /// latency instruction.
  bool mustIssueImmediately(const InstRef &IR) const;

  /// This routine notifies the Scheduler that a new cycle just started.
  ///
  /// It notifies the underlying ResourceManager that a new cycle just started.
  /// Vector `Freed` is populated with resourceRef related to resources that
  /// have changed in state, and that are now available to new instructions.
  /// Instructions executed are added to vector Executed, while vector Ready is
  /// populated with instructions that have become ready in this new cycle.
  /// Vector Pending is popluated by instructions that have transitioned through
  /// the pending stat during this cycle. The Pending and Ready sets may not be
  /// disjoint. An instruction is allowed to transition from the WAIT state to
  /// the READY state (going through the PENDING state) within a single cycle.
  /// That means, instructions may appear in both the Pending and Ready set.
  void cycleEvent(SmallVectorImpl<ResourceRef> &Freed,
                  SmallVectorImpl<InstRef> &Executed,
                  SmallVectorImpl<InstRef> &Pending,
                  SmallVectorImpl<InstRef> &Ready);

  /// Convert a resource mask into a valid llvm processor resource identifier.
  ///
  /// Only the most significant bit of the Mask is used by this method to
  /// identify the processor resource.
  unsigned getResourceID(uint64_t Mask) const {
    return Resources->resolveResourceMask(Mask);
  }

  /// Select the next instruction to issue from the ReadySet. Returns an invalid
  /// instruction reference if there are no ready instructions, or if processor
  /// resources are not available.
  InstRef select();

  bool isReadySetEmpty() const { return ReadySet.empty(); }
  bool isWaitSetEmpty() const { return WaitSet.empty(); }

  /// This method is called by the ExecuteStage at the end of each cycle to
  /// identify bottlenecks caused by data dependencies. Vector RegDeps is
  /// populated by instructions that were not issued because of unsolved
  /// register dependencies.  Vector MemDeps is populated by instructions that
  /// were not issued because of unsolved memory dependencies.
  void analyzeDataDependencies(SmallVectorImpl<InstRef> &RegDeps,
                               SmallVectorImpl<InstRef> &MemDeps);

  /// Returns a mask of busy resources, and populates vector Insts with
  /// instructions that could not be issued to the underlying pipelines because
  /// not all pipeline resources were available.
  uint64_t analyzeResourcePressure(SmallVectorImpl<InstRef> &Insts);

  // Returns true if the dispatch logic couldn't dispatch a full group due to
  // unavailable scheduler and/or LS resources.
  bool hadTokenStall() const { return HadTokenStall; }

#ifndef NDEBUG
  // Update the ready queues.
  void dump() const;

  // This routine performs a basic correctness check.  This routine should only
  // be called when we know that 'IR' is not in the scheduler's instruction
  // queues.
  void instructionCheck(const InstRef &IR) const {
    assert(!is_contained(WaitSet, IR) && "Already in the wait set!");
    assert(!is_contained(ReadySet, IR) && "Already in the ready set!");
    assert(!is_contained(IssuedSet, IR) && "Already executing!");
  }
#endif // !NDEBUG
};
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_HARDWAREUNITS_SCHEDULER_H
