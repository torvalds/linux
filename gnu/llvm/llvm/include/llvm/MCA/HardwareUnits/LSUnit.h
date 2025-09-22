//===------------------------- LSUnit.h --------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A Load/Store unit class that models load/store queues and that implements
/// a simple weak memory consistency model.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HARDWAREUNITS_LSUNIT_H
#define LLVM_MCA_HARDWAREUNITS_LSUNIT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"
#include "llvm/MCA/Instruction.h"

namespace llvm {
namespace mca {

/// A node of a memory dependency graph. A MemoryGroup describes a set of
/// instructions with same memory dependencies.
///
/// By construction, instructions of a MemoryGroup don't depend on each other.
/// At dispatch stage, instructions are mapped by the LSUnit to MemoryGroups.
/// A Memory group identifier is then stored as a "token" in field
/// Instruction::LSUTokenID of each dispatched instructions. That token is used
/// internally by the LSUnit to track memory dependencies.
class MemoryGroup {
  unsigned NumPredecessors = 0;
  unsigned NumExecutingPredecessors = 0;
  unsigned NumExecutedPredecessors = 0;

  unsigned NumInstructions = 0;
  unsigned NumExecuting = 0;
  unsigned NumExecuted = 0;
  // Successors that are in a order dependency with this group.
  SmallVector<MemoryGroup *, 4> OrderSucc;
  // Successors that are in a data dependency with this group.
  SmallVector<MemoryGroup *, 4> DataSucc;

  CriticalDependency CriticalPredecessor;
  InstRef CriticalMemoryInstruction;

  MemoryGroup(const MemoryGroup &) = delete;
  MemoryGroup &operator=(const MemoryGroup &) = delete;

public:
  MemoryGroup() = default;
  MemoryGroup(MemoryGroup &&) = default;

  size_t getNumSuccessors() const {
    return OrderSucc.size() + DataSucc.size();
  }
  unsigned getNumPredecessors() const { return NumPredecessors; }
  unsigned getNumExecutingPredecessors() const {
    return NumExecutingPredecessors;
  }
  unsigned getNumExecutedPredecessors() const {
    return NumExecutedPredecessors;
  }
  unsigned getNumInstructions() const { return NumInstructions; }
  unsigned getNumExecuting() const { return NumExecuting; }
  unsigned getNumExecuted() const { return NumExecuted; }

  const InstRef &getCriticalMemoryInstruction() const {
    return CriticalMemoryInstruction;
  }
  const CriticalDependency &getCriticalPredecessor() const {
    return CriticalPredecessor;
  }

  void addSuccessor(MemoryGroup *Group, bool IsDataDependent) {
    // Do not need to add a dependency if there is no data
    // dependency and all instructions from this group have been
    // issued already.
    if (!IsDataDependent && isExecuting())
      return;

    Group->NumPredecessors++;
    assert(!isExecuted() && "Should have been removed!");
    if (isExecuting())
      Group->onGroupIssued(CriticalMemoryInstruction, IsDataDependent);

    if (IsDataDependent)
      DataSucc.emplace_back(Group);
    else
      OrderSucc.emplace_back(Group);
  }

  bool isWaiting() const {
    return NumPredecessors >
           (NumExecutingPredecessors + NumExecutedPredecessors);
  }
  bool isPending() const {
    return NumExecutingPredecessors &&
           ((NumExecutedPredecessors + NumExecutingPredecessors) ==
            NumPredecessors);
  }
  bool isReady() const { return NumExecutedPredecessors == NumPredecessors; }
  bool isExecuting() const {
    return NumExecuting && (NumExecuting == (NumInstructions - NumExecuted));
  }
  bool isExecuted() const { return NumInstructions == NumExecuted; }

  void onGroupIssued(const InstRef &IR, bool ShouldUpdateCriticalDep) {
    assert(!isReady() && "Unexpected group-start event!");
    NumExecutingPredecessors++;

    if (!ShouldUpdateCriticalDep)
      return;

    unsigned Cycles = IR.getInstruction()->getCyclesLeft();
    if (CriticalPredecessor.Cycles < Cycles) {
      CriticalPredecessor.IID = IR.getSourceIndex();
      CriticalPredecessor.Cycles = Cycles;
    }
  }

  void onGroupExecuted() {
    assert(!isReady() && "Inconsistent state found!");
    NumExecutingPredecessors--;
    NumExecutedPredecessors++;
  }

  void onInstructionIssued(const InstRef &IR) {
    assert(!isExecuting() && "Invalid internal state!");
    ++NumExecuting;

    // update the CriticalMemDep.
    const Instruction &IS = *IR.getInstruction();
    if ((bool)CriticalMemoryInstruction) {
      const Instruction &OtherIS = *CriticalMemoryInstruction.getInstruction();
      if (OtherIS.getCyclesLeft() < IS.getCyclesLeft())
        CriticalMemoryInstruction = IR;
    } else {
      CriticalMemoryInstruction = IR;
    }

    if (!isExecuting())
      return;

    // Notify successors that this group started execution.
    for (MemoryGroup *MG : OrderSucc) {
      MG->onGroupIssued(CriticalMemoryInstruction, false);
      // Release the order dependency with this group.
      MG->onGroupExecuted();
    }

    for (MemoryGroup *MG : DataSucc)
      MG->onGroupIssued(CriticalMemoryInstruction, true);
  }

  void onInstructionExecuted(const InstRef &IR) {
    assert(isReady() && !isExecuted() && "Invalid internal state!");
    --NumExecuting;
    ++NumExecuted;

    if (CriticalMemoryInstruction &&
        CriticalMemoryInstruction.getSourceIndex() == IR.getSourceIndex()) {
      CriticalMemoryInstruction.invalidate();
    }

    if (!isExecuted())
      return;

    // Notify data dependent successors that this group has finished execution.
    for (MemoryGroup *MG : DataSucc)
      MG->onGroupExecuted();
  }

  void addInstruction() {
    assert(!getNumSuccessors() && "Cannot add instructions to this group!");
    ++NumInstructions;
  }

  void cycleEvent() {
    if (isWaiting() && CriticalPredecessor.Cycles)
      CriticalPredecessor.Cycles--;
  }
};

/// Abstract base interface for LS (load/store) units in llvm-mca.
class LSUnitBase : public HardwareUnit {
  /// Load queue size.
  ///
  /// A value of zero for this field means that the load queue is unbounded.
  /// Processor models can declare the size of a load queue via tablegen (see
  /// the definition of tablegen class LoadQueue in
  /// llvm/Target/TargetSchedule.td).
  unsigned LQSize;

  /// Load queue size.
  ///
  /// A value of zero for this field means that the store queue is unbounded.
  /// Processor models can declare the size of a store queue via tablegen (see
  /// the definition of tablegen class StoreQueue in
  /// llvm/Target/TargetSchedule.td).
  unsigned SQSize;

  unsigned UsedLQEntries;
  unsigned UsedSQEntries;

  /// True if loads don't alias with stores.
  ///
  /// By default, the LS unit assumes that loads and stores don't alias with
  /// eachother. If this field is set to false, then loads are always assumed to
  /// alias with stores.
  const bool NoAlias;

  /// Used to map group identifiers to MemoryGroups.
  DenseMap<unsigned, std::unique_ptr<MemoryGroup>> Groups;
  unsigned NextGroupID;

public:
  LSUnitBase(const MCSchedModel &SM, unsigned LoadQueueSize,
             unsigned StoreQueueSize, bool AssumeNoAlias);

  virtual ~LSUnitBase();

  /// Returns the total number of entries in the load queue.
  unsigned getLoadQueueSize() const { return LQSize; }

  /// Returns the total number of entries in the store queue.
  unsigned getStoreQueueSize() const { return SQSize; }

  unsigned getUsedLQEntries() const { return UsedLQEntries; }
  unsigned getUsedSQEntries() const { return UsedSQEntries; }
  void acquireLQSlot() { ++UsedLQEntries; }
  void acquireSQSlot() { ++UsedSQEntries; }
  void releaseLQSlot() { --UsedLQEntries; }
  void releaseSQSlot() { --UsedSQEntries; }

  bool assumeNoAlias() const { return NoAlias; }

  enum Status {
    LSU_AVAILABLE = 0,
    LSU_LQUEUE_FULL, // Load Queue unavailable
    LSU_SQUEUE_FULL  // Store Queue unavailable
  };

  /// This method checks the availability of the load/store buffers.
  ///
  /// Returns LSU_AVAILABLE if there are enough load/store queue entries to
  /// accomodate instruction IR. By default, LSU_AVAILABLE is returned if IR is
  /// not a memory operation.
  virtual Status isAvailable(const InstRef &IR) const = 0;

  /// Allocates LS resources for instruction IR.
  ///
  /// This method assumes that a previous call to `isAvailable(IR)` succeeded
  /// with a LSUnitBase::Status value of LSU_AVAILABLE.
  /// Returns the GroupID associated with this instruction. That value will be
  /// used to set the LSUTokenID field in class Instruction.
  virtual unsigned dispatch(const InstRef &IR) = 0;

  bool isSQEmpty() const { return !UsedSQEntries; }
  bool isLQEmpty() const { return !UsedLQEntries; }
  bool isSQFull() const { return SQSize && SQSize == UsedSQEntries; }
  bool isLQFull() const { return LQSize && LQSize == UsedLQEntries; }

  bool isValidGroupID(unsigned Index) const {
    return Index && Groups.contains(Index);
  }

  /// Check if a peviously dispatched instruction IR is now ready for execution.
  bool isReady(const InstRef &IR) const {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const MemoryGroup &Group = getGroup(GroupID);
    return Group.isReady();
  }

  /// Check if instruction IR only depends on memory instructions that are
  /// currently executing.
  bool isPending(const InstRef &IR) const {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const MemoryGroup &Group = getGroup(GroupID);
    return Group.isPending();
  }

  /// Check if instruction IR is still waiting on memory operations, and the
  /// wait time is still unknown.
  bool isWaiting(const InstRef &IR) const {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const MemoryGroup &Group = getGroup(GroupID);
    return Group.isWaiting();
  }

  bool hasDependentUsers(const InstRef &IR) const {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const MemoryGroup &Group = getGroup(GroupID);
    return !Group.isExecuted() && Group.getNumSuccessors();
  }

  const MemoryGroup &getGroup(unsigned Index) const {
    assert(isValidGroupID(Index) && "Group doesn't exist!");
    return *Groups.find(Index)->second;
  }

  MemoryGroup &getGroup(unsigned Index) {
    assert(isValidGroupID(Index) && "Group doesn't exist!");
    return *Groups.find(Index)->second;
  }

  unsigned createMemoryGroup() {
    Groups.insert(
        std::make_pair(NextGroupID, std::make_unique<MemoryGroup>()));
    return NextGroupID++;
  }

  virtual void onInstructionExecuted(const InstRef &IR);

  // Loads are tracked by the LDQ (load queue) from dispatch until completion.
  // Stores are tracked by the STQ (store queue) from dispatch until commitment.
  // By default we conservatively assume that the LDQ receives a load at
  // dispatch. Loads leave the LDQ at retirement stage.
  virtual void onInstructionRetired(const InstRef &IR);

  virtual void onInstructionIssued(const InstRef &IR) {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    Groups[GroupID]->onInstructionIssued(IR);
  }

  virtual void cycleEvent();

#ifndef NDEBUG
  void dump() const;
#endif
};

/// Default Load/Store Unit (LS Unit) for simulated processors.
///
/// Each load (or store) consumes one entry in the load (or store) queue.
///
/// Rules are:
/// 1) A younger load is allowed to pass an older load only if there are no
///    stores nor barriers in between the two loads.
/// 2) An younger store is not allowed to pass an older store.
/// 3) A younger store is not allowed to pass an older load.
/// 4) A younger load is allowed to pass an older store only if the load does
///    not alias with the store.
///
/// This class optimistically assumes that loads don't alias store operations.
/// Under this assumption, younger loads are always allowed to pass older
/// stores (this would only affects rule 4).
/// Essentially, this class doesn't perform any sort alias analysis to
/// identify aliasing loads and stores.
///
/// To enforce aliasing between loads and stores, flag `AssumeNoAlias` must be
/// set to `false` by the constructor of LSUnit.
///
/// Note that this class doesn't know about the existence of different memory
/// types for memory operations (example: write-through, write-combining, etc.).
/// Derived classes are responsible for implementing that extra knowledge, and
/// provide different sets of rules for loads and stores by overriding method
/// `isReady()`.
/// To emulate a write-combining memory type, rule 2. must be relaxed in a
/// derived class to enable the reordering of non-aliasing store operations.
///
/// No assumptions are made by this class on the size of the store buffer.  This
/// class doesn't know how to identify cases where store-to-load forwarding may
/// occur.
///
/// LSUnit doesn't attempt to predict whether a load or store hits or misses
/// the L1 cache. To be more specific, LSUnit doesn't know anything about
/// cache hierarchy and memory types.
/// It only knows if an instruction "mayLoad" and/or "mayStore". For loads, the
/// scheduling model provides an "optimistic" load-to-use latency (which usually
/// matches the load-to-use latency for when there is a hit in the L1D).
/// Derived classes may expand this knowledge.
///
/// Class MCInstrDesc in LLVM doesn't know about serializing operations, nor
/// memory-barrier like instructions.
/// LSUnit conservatively assumes that an instruction which `mayLoad` and has
/// `unmodeled side effects` behave like a "soft" load-barrier. That means, it
/// serializes loads without forcing a flush of the load queue.
/// Similarly, instructions that both `mayStore` and have `unmodeled side
/// effects` are treated like store barriers. A full memory
/// barrier is a 'mayLoad' and 'mayStore' instruction with unmodeled side
/// effects. This is obviously inaccurate, but this is the best that we can do
/// at the moment.
///
/// Each load/store barrier consumes one entry in the load/store queue. A
/// load/store barrier enforces ordering of loads/stores:
///  - A younger load cannot pass a load barrier.
///  - A younger store cannot pass a store barrier.
///
/// A younger load has to wait for the memory load barrier to execute.
/// A load/store barrier is "executed" when it becomes the oldest entry in
/// the load/store queue(s). That also means, all the older loads/stores have
/// already been executed.
class LSUnit : public LSUnitBase {
  // This class doesn't know about the latency of a load instruction. So, it
  // conservatively/pessimistically assumes that the latency of a load opcode
  // matches the instruction latency.
  //
  // FIXME: In the absence of cache misses (i.e. L1I/L1D/iTLB/dTLB hits/misses),
  // and load/store conflicts, the latency of a load is determined by the depth
  // of the load pipeline. So, we could use field `LoadLatency` in the
  // MCSchedModel to model that latency.
  // Field `LoadLatency` often matches the so-called 'load-to-use' latency from
  // L1D, and it usually already accounts for any extra latency due to data
  // forwarding.
  // When doing throughput analysis, `LoadLatency` is likely to
  // be a better predictor of load latency than instruction latency. This is
  // particularly true when simulating code with temporal/spatial locality of
  // memory accesses.
  // Using `LoadLatency` (instead of the instruction latency) is also expected
  // to improve the load queue allocation for long latency instructions with
  // folded memory operands (See PR39829).
  //
  // FIXME: On some processors, load/store operations are split into multiple
  // uOps. For example, X86 AMD Jaguar natively supports 128-bit data types, but
  // not 256-bit data types. So, a 256-bit load is effectively split into two
  // 128-bit loads, and each split load consumes one 'LoadQueue' entry. For
  // simplicity, this class optimistically assumes that a load instruction only
  // consumes one entry in the LoadQueue.  Similarly, store instructions only
  // consume a single entry in the StoreQueue.
  // In future, we should reassess the quality of this design, and consider
  // alternative approaches that let instructions specify the number of
  // load/store queue entries which they consume at dispatch stage (See
  // PR39830).
  //
  // An instruction that both 'mayStore' and 'HasUnmodeledSideEffects' is
  // conservatively treated as a store barrier. It forces older store to be
  // executed before newer stores are issued.
  //
  // An instruction that both 'MayLoad' and 'HasUnmodeledSideEffects' is
  // conservatively treated as a load barrier. It forces older loads to execute
  // before newer loads are issued.
  unsigned CurrentLoadGroupID;
  unsigned CurrentLoadBarrierGroupID;
  unsigned CurrentStoreGroupID;
  unsigned CurrentStoreBarrierGroupID;

public:
  LSUnit(const MCSchedModel &SM)
      : LSUnit(SM, /* LQSize */ 0, /* SQSize */ 0, /* NoAlias */ false) {}
  LSUnit(const MCSchedModel &SM, unsigned LQ, unsigned SQ)
      : LSUnit(SM, LQ, SQ, /* NoAlias */ false) {}
  LSUnit(const MCSchedModel &SM, unsigned LQ, unsigned SQ, bool AssumeNoAlias)
      : LSUnitBase(SM, LQ, SQ, AssumeNoAlias), CurrentLoadGroupID(0),
        CurrentLoadBarrierGroupID(0), CurrentStoreGroupID(0),
        CurrentStoreBarrierGroupID(0) {}

  /// Returns LSU_AVAILABLE if there are enough load/store queue entries to
  /// accomodate instruction IR.
  Status isAvailable(const InstRef &IR) const override;

  /// Allocates LS resources for instruction IR.
  ///
  /// This method assumes that a previous call to `isAvailable(IR)` succeeded
  /// returning LSU_AVAILABLE.
  ///
  /// Rules are:
  /// By default, rules are:
  /// 1. A store may not pass a previous store.
  /// 2. A load may not pass a previous store unless flag 'NoAlias' is set.
  /// 3. A load may pass a previous load.
  /// 4. A store may not pass a previous load (regardless of flag 'NoAlias').
  /// 5. A load has to wait until an older load barrier is fully executed.
  /// 6. A store has to wait until an older store barrier is fully executed.
  unsigned dispatch(const InstRef &IR) override;

  void onInstructionExecuted(const InstRef &IR) override;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_HARDWAREUNITS_LSUNIT_H
