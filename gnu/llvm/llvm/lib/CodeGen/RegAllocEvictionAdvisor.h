//===- RegAllocEvictionAdvisor.h - Interference resolution ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCEVICTIONADVISOR_H
#define LLVM_CODEGEN_REGALLOCEVICTIONADVISOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Pass.h"

namespace llvm {
class AllocationOrder;
class LiveInterval;
class LiveIntervals;
class LiveRegMatrix;
class MachineFunction;
class MachineRegisterInfo;
class RegisterClassInfo;
class TargetRegisterInfo;
class VirtRegMap;

using SmallVirtRegSet = SmallSet<Register, 16>;

// Live ranges pass through a number of stages as we try to allocate them.
// Some of the stages may also create new live ranges:
//
// - Region splitting.
// - Per-block splitting.
// - Local splitting.
// - Spilling.
//
// Ranges produced by one of the stages skip the previous stages when they are
// dequeued. This improves performance because we can skip interference checks
// that are unlikely to give any results. It also guarantees that the live
// range splitting algorithm terminates, something that is otherwise hard to
// ensure.
enum LiveRangeStage {
  /// Newly created live range that has never been queued.
  RS_New,

  /// Only attempt assignment and eviction. Then requeue as RS_Split.
  RS_Assign,

  /// Attempt live range splitting if assignment is impossible.
  RS_Split,

  /// Attempt more aggressive live range splitting that is guaranteed to make
  /// progress.  This is used for split products that may not be making
  /// progress.
  RS_Split2,

  /// Live range will be spilled.  No more splitting will be attempted.
  RS_Spill,

  /// Live range is in memory. Because of other evictions, it might get moved
  /// in a register in the end.
  RS_Memory,

  /// There is nothing more we can do to this live range.  Abort compilation
  /// if it can't be assigned.
  RS_Done
};

/// Cost of evicting interference - used by default advisor, and the eviction
/// chain heuristic in RegAllocGreedy.
// FIXME: this can be probably made an implementation detail of the default
// advisor, if the eviction chain logic can be refactored.
struct EvictionCost {
  unsigned BrokenHints = 0; ///< Total number of broken hints.
  float MaxWeight = 0;      ///< Maximum spill weight evicted.

  EvictionCost() = default;

  bool isMax() const { return BrokenHints == ~0u; }

  void setMax() { BrokenHints = ~0u; }

  void setBrokenHints(unsigned NHints) { BrokenHints = NHints; }

  bool operator<(const EvictionCost &O) const {
    return std::tie(BrokenHints, MaxWeight) <
           std::tie(O.BrokenHints, O.MaxWeight);
  }
};

/// Interface to the eviction advisor, which is responsible for making a
/// decision as to which live ranges should be evicted (if any).
class RAGreedy;
class RegAllocEvictionAdvisor {
public:
  RegAllocEvictionAdvisor(const RegAllocEvictionAdvisor &) = delete;
  RegAllocEvictionAdvisor(RegAllocEvictionAdvisor &&) = delete;
  virtual ~RegAllocEvictionAdvisor() = default;

  /// Find a physical register that can be freed by evicting the FixedRegisters,
  /// or return NoRegister. The eviction decision is assumed to be correct (i.e.
  /// no fixed live ranges are evicted) and profitable.
  virtual MCRegister tryFindEvictionCandidate(
      const LiveInterval &VirtReg, const AllocationOrder &Order,
      uint8_t CostPerUseLimit, const SmallVirtRegSet &FixedRegisters) const = 0;

  /// Find out if we can evict the live ranges occupying the given PhysReg,
  /// which is a hint (preferred register) for VirtReg.
  virtual bool
  canEvictHintInterference(const LiveInterval &VirtReg, MCRegister PhysReg,
                           const SmallVirtRegSet &FixedRegisters) const = 0;

  /// Returns true if the given \p PhysReg is a callee saved register and has
  /// not been used for allocation yet.
  bool isUnusedCalleeSavedReg(MCRegister PhysReg) const;

protected:
  RegAllocEvictionAdvisor(const MachineFunction &MF, const RAGreedy &RA);

  bool canReassign(const LiveInterval &VirtReg, MCRegister FromReg) const;

  // Get the upper limit of elements in the given Order we need to analize.
  // TODO: is this heuristic,  we could consider learning it.
  std::optional<unsigned> getOrderLimit(const LiveInterval &VirtReg,
                                        const AllocationOrder &Order,
                                        unsigned CostPerUseLimit) const;

  // Determine if it's worth trying to allocate this reg, given the
  // CostPerUseLimit
  // TODO: this is a heuristic component we could consider learning, too.
  bool canAllocatePhysReg(unsigned CostPerUseLimit, MCRegister PhysReg) const;

  const MachineFunction &MF;
  const RAGreedy &RA;
  LiveRegMatrix *const Matrix;
  LiveIntervals *const LIS;
  VirtRegMap *const VRM;
  MachineRegisterInfo *const MRI;
  const TargetRegisterInfo *const TRI;
  const RegisterClassInfo &RegClassInfo;
  const ArrayRef<uint8_t> RegCosts;

  /// Run or not the local reassignment heuristic. This information is
  /// obtained from the TargetSubtargetInfo.
  const bool EnableLocalReassign;
};

/// ImmutableAnalysis abstraction for fetching the Eviction Advisor. We model it
/// as an analysis to decouple the user from the implementation insofar as
/// dependencies on other analyses goes. The motivation for it being an
/// immutable pass is twofold:
/// - in the ML implementation case, the evaluator is stateless but (especially
/// in the development mode) expensive to set up. With an immutable pass, we set
/// it up once.
/// - in the 'development' mode ML case, we want to capture the training log
/// during allocation (this is a log of features encountered and decisions
/// made), and then measure a score, potentially a few steps after allocation
/// completes. So we need the properties of an immutable pass to keep the logger
/// state around until we can make that measurement.
///
/// Because we need to offer additional services in 'development' mode, the
/// implementations of this analysis need to implement RTTI support.
class RegAllocEvictionAdvisorAnalysis : public ImmutablePass {
public:
  enum class AdvisorMode : int { Default, Release, Development };

  RegAllocEvictionAdvisorAnalysis(AdvisorMode Mode)
      : ImmutablePass(ID), Mode(Mode){};
  static char ID;

  /// Get an advisor for the given context (i.e. machine function, etc)
  virtual std::unique_ptr<RegAllocEvictionAdvisor>
  getAdvisor(const MachineFunction &MF, const RAGreedy &RA) = 0;
  AdvisorMode getAdvisorMode() const { return Mode; }
  virtual void logRewardIfNeeded(const MachineFunction &MF,
                                 llvm::function_ref<float()> GetReward){};

protected:
  // This analysis preserves everything, and subclasses may have additional
  // requirements.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

private:
  StringRef getPassName() const override;
  const AdvisorMode Mode;
};

/// Specialization for the API used by the analysis infrastructure to create
/// an instance of the eviction advisor.
template <> Pass *callDefaultCtor<RegAllocEvictionAdvisorAnalysis>();

RegAllocEvictionAdvisorAnalysis *createReleaseModeAdvisor();

RegAllocEvictionAdvisorAnalysis *createDevelopmentModeAdvisor();

// TODO: move to RegAllocEvictionAdvisor.cpp when we move implementation
// out of RegAllocGreedy.cpp
class DefaultEvictionAdvisor : public RegAllocEvictionAdvisor {
public:
  DefaultEvictionAdvisor(const MachineFunction &MF, const RAGreedy &RA)
      : RegAllocEvictionAdvisor(MF, RA) {}

private:
  MCRegister tryFindEvictionCandidate(const LiveInterval &,
                                      const AllocationOrder &, uint8_t,
                                      const SmallVirtRegSet &) const override;
  bool canEvictHintInterference(const LiveInterval &, MCRegister,
                                const SmallVirtRegSet &) const override;
  bool canEvictInterferenceBasedOnCost(const LiveInterval &, MCRegister, bool,
                                       EvictionCost &,
                                       const SmallVirtRegSet &) const;
  bool shouldEvict(const LiveInterval &A, bool, const LiveInterval &B,
                   bool) const;
};
} // namespace llvm

#endif // LLVM_CODEGEN_REGALLOCEVICTIONADVISOR_H
