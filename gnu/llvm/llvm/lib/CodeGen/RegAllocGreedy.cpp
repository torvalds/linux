//===- RegAllocGreedy.cpp - greedy register allocator ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RAGreedy function pass for register allocation in
// optimized builds.
//
//===----------------------------------------------------------------------===//

#include "RegAllocGreedy.h"
#include "AllocationOrder.h"
#include "InterferenceCache.h"
#include "RegAllocBase.h"
#include "RegAllocEvictionAdvisor.h"
#include "RegAllocPriorityAdvisor.h"
#include "SpillPlacement.h"
#include "SplitKit.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/EdgeBundles.h"
#include "llvm/CodeGen/LiveDebugVariables.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervalUnion.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/Spiller.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumGlobalSplits, "Number of split global live ranges");
STATISTIC(NumLocalSplits,  "Number of split local live ranges");
STATISTIC(NumEvicted,      "Number of interferences evicted");

static cl::opt<SplitEditor::ComplementSpillMode> SplitSpillMode(
    "split-spill-mode", cl::Hidden,
    cl::desc("Spill mode for splitting live ranges"),
    cl::values(clEnumValN(SplitEditor::SM_Partition, "default", "Default"),
               clEnumValN(SplitEditor::SM_Size, "size", "Optimize for size"),
               clEnumValN(SplitEditor::SM_Speed, "speed", "Optimize for speed")),
    cl::init(SplitEditor::SM_Speed));

static cl::opt<unsigned>
LastChanceRecoloringMaxDepth("lcr-max-depth", cl::Hidden,
                             cl::desc("Last chance recoloring max depth"),
                             cl::init(5));

static cl::opt<unsigned> LastChanceRecoloringMaxInterference(
    "lcr-max-interf", cl::Hidden,
    cl::desc("Last chance recoloring maximum number of considered"
             " interference at a time"),
    cl::init(8));

static cl::opt<bool> ExhaustiveSearch(
    "exhaustive-register-search", cl::NotHidden,
    cl::desc("Exhaustive Search for registers bypassing the depth "
             "and interference cutoffs of last chance recoloring"),
    cl::Hidden);

static cl::opt<bool> EnableDeferredSpilling(
    "enable-deferred-spilling", cl::Hidden,
    cl::desc("Instead of spilling a variable right away, defer the actual "
             "code insertion to the end of the allocation. That way the "
             "allocator might still find a suitable coloring for this "
             "variable because of other evicted variables."),
    cl::init(false));

// FIXME: Find a good default for this flag and remove the flag.
static cl::opt<unsigned>
CSRFirstTimeCost("regalloc-csr-first-time-cost",
              cl::desc("Cost for first time use of callee-saved register."),
              cl::init(0), cl::Hidden);

static cl::opt<unsigned long> GrowRegionComplexityBudget(
    "grow-region-complexity-budget",
    cl::desc("growRegion() does not scale with the number of BB edges, so "
             "limit its budget and bail out once we reach the limit."),
    cl::init(10000), cl::Hidden);

static cl::opt<bool> GreedyRegClassPriorityTrumpsGlobalness(
    "greedy-regclass-priority-trumps-globalness",
    cl::desc("Change the greedy register allocator's live range priority "
             "calculation to make the AllocationPriority of the register class "
             "more important then whether the range is global"),
    cl::Hidden);

static cl::opt<bool> GreedyReverseLocalAssignment(
    "greedy-reverse-local-assignment",
    cl::desc("Reverse allocation order of local live ranges, such that "
             "shorter local live ranges will tend to be allocated first"),
    cl::Hidden);

static cl::opt<unsigned> SplitThresholdForRegWithHint(
    "split-threshold-for-reg-with-hint",
    cl::desc("The threshold for splitting a virtual register with a hint, in "
             "percentate"),
    cl::init(75), cl::Hidden);

static RegisterRegAlloc greedyRegAlloc("greedy", "greedy register allocator",
                                       createGreedyRegisterAllocator);

char RAGreedy::ID = 0;
char &llvm::RAGreedyID = RAGreedy::ID;

INITIALIZE_PASS_BEGIN(RAGreedy, "greedy",
                "Greedy Register Allocator", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveDebugVariables)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(RegisterCoalescer)
INITIALIZE_PASS_DEPENDENCY(MachineScheduler)
INITIALIZE_PASS_DEPENDENCY(LiveStacks)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_DEPENDENCY(LiveRegMatrix)
INITIALIZE_PASS_DEPENDENCY(EdgeBundles)
INITIALIZE_PASS_DEPENDENCY(SpillPlacement)
INITIALIZE_PASS_DEPENDENCY(MachineOptimizationRemarkEmitterPass)
INITIALIZE_PASS_DEPENDENCY(RegAllocEvictionAdvisorAnalysis)
INITIALIZE_PASS_DEPENDENCY(RegAllocPriorityAdvisorAnalysis)
INITIALIZE_PASS_END(RAGreedy, "greedy",
                "Greedy Register Allocator", false, false)

#ifndef NDEBUG
const char *const RAGreedy::StageName[] = {
    "RS_New",
    "RS_Assign",
    "RS_Split",
    "RS_Split2",
    "RS_Spill",
    "RS_Memory",
    "RS_Done"
};
#endif

// Hysteresis to use when comparing floats.
// This helps stabilize decisions based on float comparisons.
const float Hysteresis = (2007 / 2048.0f); // 0.97998046875

FunctionPass* llvm::createGreedyRegisterAllocator() {
  return new RAGreedy();
}

FunctionPass *llvm::createGreedyRegisterAllocator(RegAllocFilterFunc Ftor) {
  return new RAGreedy(Ftor);
}

RAGreedy::RAGreedy(RegAllocFilterFunc F)
    : MachineFunctionPass(ID), RegAllocBase(F) {}

void RAGreedy::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
  AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
  AU.addRequired<LiveIntervalsWrapperPass>();
  AU.addPreserved<LiveIntervalsWrapperPass>();
  AU.addRequired<SlotIndexesWrapperPass>();
  AU.addPreserved<SlotIndexesWrapperPass>();
  AU.addRequired<LiveDebugVariables>();
  AU.addPreserved<LiveDebugVariables>();
  AU.addRequired<LiveStacks>();
  AU.addPreserved<LiveStacks>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  AU.addRequired<VirtRegMap>();
  AU.addPreserved<VirtRegMap>();
  AU.addRequired<LiveRegMatrix>();
  AU.addPreserved<LiveRegMatrix>();
  AU.addRequired<EdgeBundles>();
  AU.addRequired<SpillPlacement>();
  AU.addRequired<MachineOptimizationRemarkEmitterPass>();
  AU.addRequired<RegAllocEvictionAdvisorAnalysis>();
  AU.addRequired<RegAllocPriorityAdvisorAnalysis>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

//===----------------------------------------------------------------------===//
//                     LiveRangeEdit delegate methods
//===----------------------------------------------------------------------===//

bool RAGreedy::LRE_CanEraseVirtReg(Register VirtReg) {
  LiveInterval &LI = LIS->getInterval(VirtReg);
  if (VRM->hasPhys(VirtReg)) {
    Matrix->unassign(LI);
    aboutToRemoveInterval(LI);
    return true;
  }
  // Unassigned virtreg is probably in the priority queue.
  // RegAllocBase will erase it after dequeueing.
  // Nonetheless, clear the live-range so that the debug
  // dump will show the right state for that VirtReg.
  LI.clear();
  return false;
}

void RAGreedy::LRE_WillShrinkVirtReg(Register VirtReg) {
  if (!VRM->hasPhys(VirtReg))
    return;

  // Register is assigned, put it back on the queue for reassignment.
  LiveInterval &LI = LIS->getInterval(VirtReg);
  Matrix->unassign(LI);
  RegAllocBase::enqueue(&LI);
}

void RAGreedy::LRE_DidCloneVirtReg(Register New, Register Old) {
  ExtraInfo->LRE_DidCloneVirtReg(New, Old);
}

void RAGreedy::ExtraRegInfo::LRE_DidCloneVirtReg(Register New, Register Old) {
  // Cloning a register we haven't even heard about yet?  Just ignore it.
  if (!Info.inBounds(Old))
    return;

  // LRE may clone a virtual register because dead code elimination causes it to
  // be split into connected components. The new components are much smaller
  // than the original, so they should get a new chance at being assigned.
  // same stage as the parent.
  Info[Old].Stage = RS_Assign;
  Info.grow(New.id());
  Info[New] = Info[Old];
}

void RAGreedy::releaseMemory() {
  SpillerInstance.reset();
  GlobalCand.clear();
}

void RAGreedy::enqueueImpl(const LiveInterval *LI) { enqueue(Queue, LI); }

void RAGreedy::enqueue(PQueue &CurQueue, const LiveInterval *LI) {
  // Prioritize live ranges by size, assigning larger ranges first.
  // The queue holds (size, reg) pairs.
  const Register Reg = LI->reg();
  assert(Reg.isVirtual() && "Can only enqueue virtual registers");

  auto Stage = ExtraInfo->getOrInitStage(Reg);
  if (Stage == RS_New) {
    Stage = RS_Assign;
    ExtraInfo->setStage(Reg, Stage);
  }

  unsigned Ret = PriorityAdvisor->getPriority(*LI);

  // The virtual register number is a tie breaker for same-sized ranges.
  // Give lower vreg numbers higher priority to assign them first.
  CurQueue.push(std::make_pair(Ret, ~Reg));
}

unsigned DefaultPriorityAdvisor::getPriority(const LiveInterval &LI) const {
  const unsigned Size = LI.getSize();
  const Register Reg = LI.reg();
  unsigned Prio;
  LiveRangeStage Stage = RA.getExtraInfo().getStage(LI);

  if (Stage == RS_Split) {
    // Unsplit ranges that couldn't be allocated immediately are deferred until
    // everything else has been allocated.
    Prio = Size;
  } else if (Stage == RS_Memory) {
    // Memory operand should be considered last.
    // Change the priority such that Memory operand are assigned in
    // the reverse order that they came in.
    // TODO: Make this a member variable and probably do something about hints.
    static unsigned MemOp = 0;
    Prio = MemOp++;
  } else {
    // Giant live ranges fall back to the global assignment heuristic, which
    // prevents excessive spilling in pathological cases.
    const TargetRegisterClass &RC = *MRI->getRegClass(Reg);
    bool ForceGlobal = RC.GlobalPriority ||
                       (!ReverseLocalAssignment &&
                        (Size / SlotIndex::InstrDist) >
                            (2 * RegClassInfo.getNumAllocatableRegs(&RC)));
    unsigned GlobalBit = 0;

    if (Stage == RS_Assign && !ForceGlobal && !LI.empty() &&
        LIS->intervalIsInOneMBB(LI)) {
      // Allocate original local ranges in linear instruction order. Since they
      // are singly defined, this produces optimal coloring in the absence of
      // global interference and other constraints.
      if (!ReverseLocalAssignment)
        Prio = LI.beginIndex().getApproxInstrDistance(Indexes->getLastIndex());
      else {
        // Allocating bottom up may allow many short LRGs to be assigned first
        // to one of the cheap registers. This could be much faster for very
        // large blocks on targets with many physical registers.
        Prio = Indexes->getZeroIndex().getApproxInstrDistance(LI.endIndex());
      }
    } else {
      // Allocate global and split ranges in long->short order. Long ranges that
      // don't fit should be spilled (or split) ASAP so they don't create
      // interference.  Mark a bit to prioritize global above local ranges.
      Prio = Size;
      GlobalBit = 1;
    }

    // Priority bit layout:
    // 31 RS_Assign priority
    // 30 Preference priority
    // if (RegClassPriorityTrumpsGlobalness)
    //   29-25 AllocPriority
    //   24 GlobalBit
    // else
    //   29 Global bit
    //   28-24 AllocPriority
    // 0-23 Size/Instr distance

    // Clamp the size to fit with the priority masking scheme
    Prio = std::min(Prio, (unsigned)maxUIntN(24));
    assert(isUInt<5>(RC.AllocationPriority) && "allocation priority overflow");

    if (RegClassPriorityTrumpsGlobalness)
      Prio |= RC.AllocationPriority << 25 | GlobalBit << 24;
    else
      Prio |= GlobalBit << 29 | RC.AllocationPriority << 24;

    // Mark a higher bit to prioritize global and local above RS_Split.
    Prio |= (1u << 31);

    // Boost ranges that have a physical register hint.
    if (VRM->hasKnownPreference(Reg))
      Prio |= (1u << 30);
  }

  return Prio;
}

const LiveInterval *RAGreedy::dequeue() { return dequeue(Queue); }

const LiveInterval *RAGreedy::dequeue(PQueue &CurQueue) {
  if (CurQueue.empty())
    return nullptr;
  LiveInterval *LI = &LIS->getInterval(~CurQueue.top().second);
  CurQueue.pop();
  return LI;
}

//===----------------------------------------------------------------------===//
//                            Direct Assignment
//===----------------------------------------------------------------------===//

/// tryAssign - Try to assign VirtReg to an available register.
MCRegister RAGreedy::tryAssign(const LiveInterval &VirtReg,
                               AllocationOrder &Order,
                               SmallVectorImpl<Register> &NewVRegs,
                               const SmallVirtRegSet &FixedRegisters) {
  MCRegister PhysReg;
  for (auto I = Order.begin(), E = Order.end(); I != E && !PhysReg; ++I) {
    assert(*I);
    if (!Matrix->checkInterference(VirtReg, *I)) {
      if (I.isHint())
        return *I;
      else
        PhysReg = *I;
    }
  }
  if (!PhysReg.isValid())
    return PhysReg;

  // PhysReg is available, but there may be a better choice.

  // If we missed a simple hint, try to cheaply evict interference from the
  // preferred register.
  if (Register Hint = MRI->getSimpleHint(VirtReg.reg()))
    if (Order.isHint(Hint)) {
      MCRegister PhysHint = Hint.asMCReg();
      LLVM_DEBUG(dbgs() << "missed hint " << printReg(PhysHint, TRI) << '\n');

      if (EvictAdvisor->canEvictHintInterference(VirtReg, PhysHint,
                                                 FixedRegisters)) {
        evictInterference(VirtReg, PhysHint, NewVRegs);
        return PhysHint;
      }

      // We can also split the virtual register in cold blocks.
      if (trySplitAroundHintReg(PhysHint, VirtReg, NewVRegs, Order))
        return 0;

      // Record the missed hint, we may be able to recover
      // at the end if the surrounding allocation changed.
      SetOfBrokenHints.insert(&VirtReg);
    }

  // Try to evict interference from a cheaper alternative.
  uint8_t Cost = RegCosts[PhysReg];

  // Most registers have 0 additional cost.
  if (!Cost)
    return PhysReg;

  LLVM_DEBUG(dbgs() << printReg(PhysReg, TRI) << " is available at cost "
                    << (unsigned)Cost << '\n');
  MCRegister CheapReg = tryEvict(VirtReg, Order, NewVRegs, Cost, FixedRegisters);
  return CheapReg ? CheapReg : PhysReg;
}

//===----------------------------------------------------------------------===//
//                         Interference eviction
//===----------------------------------------------------------------------===//

bool RegAllocEvictionAdvisor::canReassign(const LiveInterval &VirtReg,
                                          MCRegister FromReg) const {
  auto HasRegUnitInterference = [&](MCRegUnit Unit) {
    // Instantiate a "subquery", not to be confused with the Queries array.
    LiveIntervalUnion::Query SubQ(VirtReg, Matrix->getLiveUnions()[Unit]);
    return SubQ.checkInterference();
  };

  for (MCRegister Reg :
       AllocationOrder::create(VirtReg.reg(), *VRM, RegClassInfo, Matrix)) {
    if (Reg == FromReg)
      continue;
    // If no units have interference, reassignment is possible.
    if (none_of(TRI->regunits(Reg), HasRegUnitInterference)) {
      LLVM_DEBUG(dbgs() << "can reassign: " << VirtReg << " from "
                        << printReg(FromReg, TRI) << " to "
                        << printReg(Reg, TRI) << '\n');
      return true;
    }
  }
  return false;
}

/// evictInterference - Evict any interferring registers that prevent VirtReg
/// from being assigned to Physreg. This assumes that canEvictInterference
/// returned true.
void RAGreedy::evictInterference(const LiveInterval &VirtReg,
                                 MCRegister PhysReg,
                                 SmallVectorImpl<Register> &NewVRegs) {
  // Make sure that VirtReg has a cascade number, and assign that cascade
  // number to every evicted register. These live ranges than then only be
  // evicted by a newer cascade, preventing infinite loops.
  unsigned Cascade = ExtraInfo->getOrAssignNewCascade(VirtReg.reg());

  LLVM_DEBUG(dbgs() << "evicting " << printReg(PhysReg, TRI)
                    << " interference: Cascade " << Cascade << '\n');

  // Collect all interfering virtregs first.
  SmallVector<const LiveInterval *, 8> Intfs;
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    LiveIntervalUnion::Query &Q = Matrix->query(VirtReg, Unit);
    // We usually have the interfering VRegs cached so collectInterferingVRegs()
    // should be fast, we may need to recalculate if when different physregs
    // overlap the same register unit so we had different SubRanges queried
    // against it.
    ArrayRef<const LiveInterval *> IVR = Q.interferingVRegs();
    Intfs.append(IVR.begin(), IVR.end());
  }

  // Evict them second. This will invalidate the queries.
  for (const LiveInterval *Intf : Intfs) {
    // The same VirtReg may be present in multiple RegUnits. Skip duplicates.
    if (!VRM->hasPhys(Intf->reg()))
      continue;

    Matrix->unassign(*Intf);
    assert((ExtraInfo->getCascade(Intf->reg()) < Cascade ||
            VirtReg.isSpillable() < Intf->isSpillable()) &&
           "Cannot decrease cascade number, illegal eviction");
    ExtraInfo->setCascade(Intf->reg(), Cascade);
    ++NumEvicted;
    NewVRegs.push_back(Intf->reg());
  }
}

/// Returns true if the given \p PhysReg is a callee saved register and has not
/// been used for allocation yet.
bool RegAllocEvictionAdvisor::isUnusedCalleeSavedReg(MCRegister PhysReg) const {
  MCRegister CSR = RegClassInfo.getLastCalleeSavedAlias(PhysReg);
  if (!CSR)
    return false;

  return !Matrix->isPhysRegUsed(PhysReg);
}

std::optional<unsigned>
RegAllocEvictionAdvisor::getOrderLimit(const LiveInterval &VirtReg,
                                       const AllocationOrder &Order,
                                       unsigned CostPerUseLimit) const {
  unsigned OrderLimit = Order.getOrder().size();

  if (CostPerUseLimit < uint8_t(~0u)) {
    // Check of any registers in RC are below CostPerUseLimit.
    const TargetRegisterClass *RC = MRI->getRegClass(VirtReg.reg());
    uint8_t MinCost = RegClassInfo.getMinCost(RC);
    if (MinCost >= CostPerUseLimit) {
      LLVM_DEBUG(dbgs() << TRI->getRegClassName(RC) << " minimum cost = "
                        << MinCost << ", no cheaper registers to be found.\n");
      return std::nullopt;
    }

    // It is normal for register classes to have a long tail of registers with
    // the same cost. We don't need to look at them if they're too expensive.
    if (RegCosts[Order.getOrder().back()] >= CostPerUseLimit) {
      OrderLimit = RegClassInfo.getLastCostChange(RC);
      LLVM_DEBUG(dbgs() << "Only trying the first " << OrderLimit
                        << " regs.\n");
    }
  }
  return OrderLimit;
}

bool RegAllocEvictionAdvisor::canAllocatePhysReg(unsigned CostPerUseLimit,
                                                 MCRegister PhysReg) const {
  if (RegCosts[PhysReg] >= CostPerUseLimit)
    return false;
  // The first use of a callee-saved register in a function has cost 1.
  // Don't start using a CSR when the CostPerUseLimit is low.
  if (CostPerUseLimit == 1 && isUnusedCalleeSavedReg(PhysReg)) {
    LLVM_DEBUG(
        dbgs() << printReg(PhysReg, TRI) << " would clobber CSR "
               << printReg(RegClassInfo.getLastCalleeSavedAlias(PhysReg), TRI)
               << '\n');
    return false;
  }
  return true;
}

/// tryEvict - Try to evict all interferences for a physreg.
/// @param  VirtReg Currently unassigned virtual register.
/// @param  Order   Physregs to try.
/// @return         Physreg to assign VirtReg, or 0.
MCRegister RAGreedy::tryEvict(const LiveInterval &VirtReg,
                              AllocationOrder &Order,
                              SmallVectorImpl<Register> &NewVRegs,
                              uint8_t CostPerUseLimit,
                              const SmallVirtRegSet &FixedRegisters) {
  NamedRegionTimer T("evict", "Evict", TimerGroupName, TimerGroupDescription,
                     TimePassesIsEnabled);

  MCRegister BestPhys = EvictAdvisor->tryFindEvictionCandidate(
      VirtReg, Order, CostPerUseLimit, FixedRegisters);
  if (BestPhys.isValid())
    evictInterference(VirtReg, BestPhys, NewVRegs);
  return BestPhys;
}

//===----------------------------------------------------------------------===//
//                              Region Splitting
//===----------------------------------------------------------------------===//

/// addSplitConstraints - Fill out the SplitConstraints vector based on the
/// interference pattern in Physreg and its aliases. Add the constraints to
/// SpillPlacement and return the static cost of this split in Cost, assuming
/// that all preferences in SplitConstraints are met.
/// Return false if there are no bundles with positive bias.
bool RAGreedy::addSplitConstraints(InterferenceCache::Cursor Intf,
                                   BlockFrequency &Cost) {
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();

  // Reset interference dependent info.
  SplitConstraints.resize(UseBlocks.size());
  BlockFrequency StaticCost = BlockFrequency(0);
  for (unsigned I = 0; I != UseBlocks.size(); ++I) {
    const SplitAnalysis::BlockInfo &BI = UseBlocks[I];
    SpillPlacement::BlockConstraint &BC = SplitConstraints[I];

    BC.Number = BI.MBB->getNumber();
    Intf.moveToBlock(BC.Number);
    BC.Entry = BI.LiveIn ? SpillPlacement::PrefReg : SpillPlacement::DontCare;
    BC.Exit = (BI.LiveOut &&
               !LIS->getInstructionFromIndex(BI.LastInstr)->isImplicitDef())
                  ? SpillPlacement::PrefReg
                  : SpillPlacement::DontCare;
    BC.ChangesValue = BI.FirstDef.isValid();

    if (!Intf.hasInterference())
      continue;

    // Number of spill code instructions to insert.
    unsigned Ins = 0;

    // Interference for the live-in value.
    if (BI.LiveIn) {
      if (Intf.first() <= Indexes->getMBBStartIdx(BC.Number)) {
        BC.Entry = SpillPlacement::MustSpill;
        ++Ins;
      } else if (Intf.first() < BI.FirstInstr) {
        BC.Entry = SpillPlacement::PrefSpill;
        ++Ins;
      } else if (Intf.first() < BI.LastInstr) {
        ++Ins;
      }

      // Abort if the spill cannot be inserted at the MBB' start
      if (((BC.Entry == SpillPlacement::MustSpill) ||
           (BC.Entry == SpillPlacement::PrefSpill)) &&
          SlotIndex::isEarlierInstr(BI.FirstInstr,
                                    SA->getFirstSplitPoint(BC.Number)))
        return false;
    }

    // Interference for the live-out value.
    if (BI.LiveOut) {
      if (Intf.last() >= SA->getLastSplitPoint(BC.Number)) {
        BC.Exit = SpillPlacement::MustSpill;
        ++Ins;
      } else if (Intf.last() > BI.LastInstr) {
        BC.Exit = SpillPlacement::PrefSpill;
        ++Ins;
      } else if (Intf.last() > BI.FirstInstr) {
        ++Ins;
      }
    }

    // Accumulate the total frequency of inserted spill code.
    while (Ins--)
      StaticCost += SpillPlacer->getBlockFrequency(BC.Number);
  }
  Cost = StaticCost;

  // Add constraints for use-blocks. Note that these are the only constraints
  // that may add a positive bias, it is downhill from here.
  SpillPlacer->addConstraints(SplitConstraints);
  return SpillPlacer->scanActiveBundles();
}

/// addThroughConstraints - Add constraints and links to SpillPlacer from the
/// live-through blocks in Blocks.
bool RAGreedy::addThroughConstraints(InterferenceCache::Cursor Intf,
                                     ArrayRef<unsigned> Blocks) {
  const unsigned GroupSize = 8;
  SpillPlacement::BlockConstraint BCS[GroupSize];
  unsigned TBS[GroupSize];
  unsigned B = 0, T = 0;

  for (unsigned Number : Blocks) {
    Intf.moveToBlock(Number);

    if (!Intf.hasInterference()) {
      assert(T < GroupSize && "Array overflow");
      TBS[T] = Number;
      if (++T == GroupSize) {
        SpillPlacer->addLinks(ArrayRef(TBS, T));
        T = 0;
      }
      continue;
    }

    assert(B < GroupSize && "Array overflow");
    BCS[B].Number = Number;

    // Abort if the spill cannot be inserted at the MBB' start
    MachineBasicBlock *MBB = MF->getBlockNumbered(Number);
    auto FirstNonDebugInstr = MBB->getFirstNonDebugInstr();
    if (FirstNonDebugInstr != MBB->end() &&
        SlotIndex::isEarlierInstr(LIS->getInstructionIndex(*FirstNonDebugInstr),
                                  SA->getFirstSplitPoint(Number)))
      return false;
    // Interference for the live-in value.
    if (Intf.first() <= Indexes->getMBBStartIdx(Number))
      BCS[B].Entry = SpillPlacement::MustSpill;
    else
      BCS[B].Entry = SpillPlacement::PrefSpill;

    // Interference for the live-out value.
    if (Intf.last() >= SA->getLastSplitPoint(Number))
      BCS[B].Exit = SpillPlacement::MustSpill;
    else
      BCS[B].Exit = SpillPlacement::PrefSpill;

    if (++B == GroupSize) {
      SpillPlacer->addConstraints(ArrayRef(BCS, B));
      B = 0;
    }
  }

  SpillPlacer->addConstraints(ArrayRef(BCS, B));
  SpillPlacer->addLinks(ArrayRef(TBS, T));
  return true;
}

bool RAGreedy::growRegion(GlobalSplitCandidate &Cand) {
  // Keep track of through blocks that have not been added to SpillPlacer.
  BitVector Todo = SA->getThroughBlocks();
  SmallVectorImpl<unsigned> &ActiveBlocks = Cand.ActiveBlocks;
  unsigned AddedTo = 0;
#ifndef NDEBUG
  unsigned Visited = 0;
#endif

  unsigned long Budget = GrowRegionComplexityBudget;
  while (true) {
    ArrayRef<unsigned> NewBundles = SpillPlacer->getRecentPositive();
    // Find new through blocks in the periphery of PrefRegBundles.
    for (unsigned Bundle : NewBundles) {
      // Look at all blocks connected to Bundle in the full graph.
      ArrayRef<unsigned> Blocks = Bundles->getBlocks(Bundle);
      // Limit compilation time by bailing out after we use all our budget.
      if (Blocks.size() >= Budget)
        return false;
      Budget -= Blocks.size();
      for (unsigned Block : Blocks) {
        if (!Todo.test(Block))
          continue;
        Todo.reset(Block);
        // This is a new through block. Add it to SpillPlacer later.
        ActiveBlocks.push_back(Block);
#ifndef NDEBUG
        ++Visited;
#endif
      }
    }
    // Any new blocks to add?
    if (ActiveBlocks.size() == AddedTo)
      break;

    // Compute through constraints from the interference, or assume that all
    // through blocks prefer spilling when forming compact regions.
    auto NewBlocks = ArrayRef(ActiveBlocks).slice(AddedTo);
    if (Cand.PhysReg) {
      if (!addThroughConstraints(Cand.Intf, NewBlocks))
        return false;
    } else {
      // Providing that the variable being spilled does not look like a loop
      // induction variable, which is expensive to spill around and better
      // pushed into a condition inside the loop if possible, provide a strong
      // negative bias on through blocks to prevent unwanted liveness on loop
      // backedges.
      bool PrefSpill = true;
      if (SA->looksLikeLoopIV() && NewBlocks.size() >= 2) {
        // Check that the current bundle is adding a Header + start+end of
        // loop-internal blocks. If the block is indeed a header, don't make
        // the NewBlocks as PrefSpill to allow the variable to be live in
        // Header<->Latch.
        MachineLoop *L = Loops->getLoopFor(MF->getBlockNumbered(NewBlocks[0]));
        if (L && L->getHeader()->getNumber() == (int)NewBlocks[0] &&
            all_of(NewBlocks.drop_front(), [&](unsigned Block) {
              return L == Loops->getLoopFor(MF->getBlockNumbered(Block));
            }))
          PrefSpill = false;
      }
      if (PrefSpill)
        SpillPlacer->addPrefSpill(NewBlocks, /* Strong= */ true);
    }
    AddedTo = ActiveBlocks.size();

    // Perhaps iterating can enable more bundles?
    SpillPlacer->iterate();
  }
  LLVM_DEBUG(dbgs() << ", v=" << Visited);
  return true;
}

/// calcCompactRegion - Compute the set of edge bundles that should be live
/// when splitting the current live range into compact regions.  Compact
/// regions can be computed without looking at interference.  They are the
/// regions formed by removing all the live-through blocks from the live range.
///
/// Returns false if the current live range is already compact, or if the
/// compact regions would form single block regions anyway.
bool RAGreedy::calcCompactRegion(GlobalSplitCandidate &Cand) {
  // Without any through blocks, the live range is already compact.
  if (!SA->getNumThroughBlocks())
    return false;

  // Compact regions don't correspond to any physreg.
  Cand.reset(IntfCache, MCRegister::NoRegister);

  LLVM_DEBUG(dbgs() << "Compact region bundles");

  // Use the spill placer to determine the live bundles. GrowRegion pretends
  // that all the through blocks have interference when PhysReg is unset.
  SpillPlacer->prepare(Cand.LiveBundles);

  // The static split cost will be zero since Cand.Intf reports no interference.
  BlockFrequency Cost;
  if (!addSplitConstraints(Cand.Intf, Cost)) {
    LLVM_DEBUG(dbgs() << ", none.\n");
    return false;
  }

  if (!growRegion(Cand)) {
    LLVM_DEBUG(dbgs() << ", cannot spill all interferences.\n");
    return false;
  }

  SpillPlacer->finish();

  if (!Cand.LiveBundles.any()) {
    LLVM_DEBUG(dbgs() << ", none.\n");
    return false;
  }

  LLVM_DEBUG({
    for (int I : Cand.LiveBundles.set_bits())
      dbgs() << " EB#" << I;
    dbgs() << ".\n";
  });
  return true;
}

/// calcSpillCost - Compute how expensive it would be to split the live range in
/// SA around all use blocks instead of forming bundle regions.
BlockFrequency RAGreedy::calcSpillCost() {
  BlockFrequency Cost = BlockFrequency(0);
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();
  for (const SplitAnalysis::BlockInfo &BI : UseBlocks) {
    unsigned Number = BI.MBB->getNumber();
    // We normally only need one spill instruction - a load or a store.
    Cost += SpillPlacer->getBlockFrequency(Number);

    // Unless the value is redefined in the block.
    if (BI.LiveIn && BI.LiveOut && BI.FirstDef)
      Cost += SpillPlacer->getBlockFrequency(Number);
  }
  return Cost;
}

/// calcGlobalSplitCost - Return the global split cost of following the split
/// pattern in LiveBundles. This cost should be added to the local cost of the
/// interference pattern in SplitConstraints.
///
BlockFrequency RAGreedy::calcGlobalSplitCost(GlobalSplitCandidate &Cand,
                                             const AllocationOrder &Order) {
  BlockFrequency GlobalCost = BlockFrequency(0);
  const BitVector &LiveBundles = Cand.LiveBundles;
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();
  for (unsigned I = 0; I != UseBlocks.size(); ++I) {
    const SplitAnalysis::BlockInfo &BI = UseBlocks[I];
    SpillPlacement::BlockConstraint &BC = SplitConstraints[I];
    bool RegIn  = LiveBundles[Bundles->getBundle(BC.Number, false)];
    bool RegOut = LiveBundles[Bundles->getBundle(BC.Number, true)];
    unsigned Ins = 0;

    Cand.Intf.moveToBlock(BC.Number);

    if (BI.LiveIn)
      Ins += RegIn != (BC.Entry == SpillPlacement::PrefReg);
    if (BI.LiveOut)
      Ins += RegOut != (BC.Exit == SpillPlacement::PrefReg);
    while (Ins--)
      GlobalCost += SpillPlacer->getBlockFrequency(BC.Number);
  }

  for (unsigned Number : Cand.ActiveBlocks) {
    bool RegIn  = LiveBundles[Bundles->getBundle(Number, false)];
    bool RegOut = LiveBundles[Bundles->getBundle(Number, true)];
    if (!RegIn && !RegOut)
      continue;
    if (RegIn && RegOut) {
      // We need double spill code if this block has interference.
      Cand.Intf.moveToBlock(Number);
      if (Cand.Intf.hasInterference()) {
        GlobalCost += SpillPlacer->getBlockFrequency(Number);
        GlobalCost += SpillPlacer->getBlockFrequency(Number);
      }
      continue;
    }
    // live-in / stack-out or stack-in live-out.
    GlobalCost += SpillPlacer->getBlockFrequency(Number);
  }
  return GlobalCost;
}

/// splitAroundRegion - Split the current live range around the regions
/// determined by BundleCand and GlobalCand.
///
/// Before calling this function, GlobalCand and BundleCand must be initialized
/// so each bundle is assigned to a valid candidate, or NoCand for the
/// stack-bound bundles.  The shared SA/SE SplitAnalysis and SplitEditor
/// objects must be initialized for the current live range, and intervals
/// created for the used candidates.
///
/// @param LREdit    The LiveRangeEdit object handling the current split.
/// @param UsedCands List of used GlobalCand entries. Every BundleCand value
///                  must appear in this list.
void RAGreedy::splitAroundRegion(LiveRangeEdit &LREdit,
                                 ArrayRef<unsigned> UsedCands) {
  // These are the intervals created for new global ranges. We may create more
  // intervals for local ranges.
  const unsigned NumGlobalIntvs = LREdit.size();
  LLVM_DEBUG(dbgs() << "splitAroundRegion with " << NumGlobalIntvs
                    << " globals.\n");
  assert(NumGlobalIntvs && "No global intervals configured");

  // Isolate even single instructions when dealing with a proper sub-class.
  // That guarantees register class inflation for the stack interval because it
  // is all copies.
  Register Reg = SA->getParent().reg();
  bool SingleInstrs = RegClassInfo.isProperSubClass(MRI->getRegClass(Reg));

  // First handle all the blocks with uses.
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();
  for (const SplitAnalysis::BlockInfo &BI : UseBlocks) {
    unsigned Number = BI.MBB->getNumber();
    unsigned IntvIn = 0, IntvOut = 0;
    SlotIndex IntfIn, IntfOut;
    if (BI.LiveIn) {
      unsigned CandIn = BundleCand[Bundles->getBundle(Number, false)];
      if (CandIn != NoCand) {
        GlobalSplitCandidate &Cand = GlobalCand[CandIn];
        IntvIn = Cand.IntvIdx;
        Cand.Intf.moveToBlock(Number);
        IntfIn = Cand.Intf.first();
      }
    }
    if (BI.LiveOut) {
      unsigned CandOut = BundleCand[Bundles->getBundle(Number, true)];
      if (CandOut != NoCand) {
        GlobalSplitCandidate &Cand = GlobalCand[CandOut];
        IntvOut = Cand.IntvIdx;
        Cand.Intf.moveToBlock(Number);
        IntfOut = Cand.Intf.last();
      }
    }

    // Create separate intervals for isolated blocks with multiple uses.
    if (!IntvIn && !IntvOut) {
      LLVM_DEBUG(dbgs() << printMBBReference(*BI.MBB) << " isolated.\n");
      if (SA->shouldSplitSingleBlock(BI, SingleInstrs))
        SE->splitSingleBlock(BI);
      continue;
    }

    if (IntvIn && IntvOut)
      SE->splitLiveThroughBlock(Number, IntvIn, IntfIn, IntvOut, IntfOut);
    else if (IntvIn)
      SE->splitRegInBlock(BI, IntvIn, IntfIn);
    else
      SE->splitRegOutBlock(BI, IntvOut, IntfOut);
  }

  // Handle live-through blocks. The relevant live-through blocks are stored in
  // the ActiveBlocks list with each candidate. We need to filter out
  // duplicates.
  BitVector Todo = SA->getThroughBlocks();
  for (unsigned UsedCand : UsedCands) {
    ArrayRef<unsigned> Blocks = GlobalCand[UsedCand].ActiveBlocks;
    for (unsigned Number : Blocks) {
      if (!Todo.test(Number))
        continue;
      Todo.reset(Number);

      unsigned IntvIn = 0, IntvOut = 0;
      SlotIndex IntfIn, IntfOut;

      unsigned CandIn = BundleCand[Bundles->getBundle(Number, false)];
      if (CandIn != NoCand) {
        GlobalSplitCandidate &Cand = GlobalCand[CandIn];
        IntvIn = Cand.IntvIdx;
        Cand.Intf.moveToBlock(Number);
        IntfIn = Cand.Intf.first();
      }

      unsigned CandOut = BundleCand[Bundles->getBundle(Number, true)];
      if (CandOut != NoCand) {
        GlobalSplitCandidate &Cand = GlobalCand[CandOut];
        IntvOut = Cand.IntvIdx;
        Cand.Intf.moveToBlock(Number);
        IntfOut = Cand.Intf.last();
      }
      if (!IntvIn && !IntvOut)
        continue;
      SE->splitLiveThroughBlock(Number, IntvIn, IntfIn, IntvOut, IntfOut);
    }
  }

  ++NumGlobalSplits;

  SmallVector<unsigned, 8> IntvMap;
  SE->finish(&IntvMap);
  DebugVars->splitRegister(Reg, LREdit.regs(), *LIS);

  unsigned OrigBlocks = SA->getNumLiveBlocks();

  // Sort out the new intervals created by splitting. We get four kinds:
  // - Remainder intervals should not be split again.
  // - Candidate intervals can be assigned to Cand.PhysReg.
  // - Block-local splits are candidates for local splitting.
  // - DCE leftovers should go back on the queue.
  for (unsigned I = 0, E = LREdit.size(); I != E; ++I) {
    const LiveInterval &Reg = LIS->getInterval(LREdit.get(I));

    // Ignore old intervals from DCE.
    if (ExtraInfo->getOrInitStage(Reg.reg()) != RS_New)
      continue;

    // Remainder interval. Don't try splitting again, spill if it doesn't
    // allocate.
    if (IntvMap[I] == 0) {
      ExtraInfo->setStage(Reg, RS_Spill);
      continue;
    }

    // Global intervals. Allow repeated splitting as long as the number of live
    // blocks is strictly decreasing.
    if (IntvMap[I] < NumGlobalIntvs) {
      if (SA->countLiveBlocks(&Reg) >= OrigBlocks) {
        LLVM_DEBUG(dbgs() << "Main interval covers the same " << OrigBlocks
                          << " blocks as original.\n");
        // Don't allow repeated splitting as a safe guard against looping.
        ExtraInfo->setStage(Reg, RS_Split2);
      }
      continue;
    }

    // Other intervals are treated as new. This includes local intervals created
    // for blocks with multiple uses, and anything created by DCE.
  }

  if (VerifyEnabled)
    MF->verify(this, "After splitting live range around region");
}

MCRegister RAGreedy::tryRegionSplit(const LiveInterval &VirtReg,
                                    AllocationOrder &Order,
                                    SmallVectorImpl<Register> &NewVRegs) {
  if (!TRI->shouldRegionSplitForVirtReg(*MF, VirtReg))
    return MCRegister::NoRegister;
  unsigned NumCands = 0;
  BlockFrequency SpillCost = calcSpillCost();
  BlockFrequency BestCost;

  // Check if we can split this live range around a compact region.
  bool HasCompact = calcCompactRegion(GlobalCand.front());
  if (HasCompact) {
    // Yes, keep GlobalCand[0] as the compact region candidate.
    NumCands = 1;
    BestCost = BlockFrequency::max();
  } else {
    // No benefit from the compact region, our fallback will be per-block
    // splitting. Make sure we find a solution that is cheaper than spilling.
    BestCost = SpillCost;
    LLVM_DEBUG(dbgs() << "Cost of isolating all blocks = "
                      << printBlockFreq(*MBFI, BestCost) << '\n');
  }

  unsigned BestCand = calculateRegionSplitCost(VirtReg, Order, BestCost,
                                               NumCands, false /*IgnoreCSR*/);

  // No solutions found, fall back to single block splitting.
  if (!HasCompact && BestCand == NoCand)
    return MCRegister::NoRegister;

  return doRegionSplit(VirtReg, BestCand, HasCompact, NewVRegs);
}

unsigned
RAGreedy::calculateRegionSplitCostAroundReg(MCPhysReg PhysReg,
                                            AllocationOrder &Order,
                                            BlockFrequency &BestCost,
                                            unsigned &NumCands,
                                            unsigned &BestCand) {
  // Discard bad candidates before we run out of interference cache cursors.
  // This will only affect register classes with a lot of registers (>32).
  if (NumCands == IntfCache.getMaxCursors()) {
    unsigned WorstCount = ~0u;
    unsigned Worst = 0;
    for (unsigned CandIndex = 0; CandIndex != NumCands; ++CandIndex) {
      if (CandIndex == BestCand || !GlobalCand[CandIndex].PhysReg)
        continue;
      unsigned Count = GlobalCand[CandIndex].LiveBundles.count();
      if (Count < WorstCount) {
        Worst = CandIndex;
        WorstCount = Count;
      }
    }
    --NumCands;
    GlobalCand[Worst] = GlobalCand[NumCands];
    if (BestCand == NumCands)
      BestCand = Worst;
  }

  if (GlobalCand.size() <= NumCands)
    GlobalCand.resize(NumCands+1);
  GlobalSplitCandidate &Cand = GlobalCand[NumCands];
  Cand.reset(IntfCache, PhysReg);

  SpillPlacer->prepare(Cand.LiveBundles);
  BlockFrequency Cost;
  if (!addSplitConstraints(Cand.Intf, Cost)) {
    LLVM_DEBUG(dbgs() << printReg(PhysReg, TRI) << "\tno positive bundles\n");
    return BestCand;
  }
  LLVM_DEBUG(dbgs() << printReg(PhysReg, TRI)
                    << "\tstatic = " << printBlockFreq(*MBFI, Cost));
  if (Cost >= BestCost) {
    LLVM_DEBUG({
      if (BestCand == NoCand)
        dbgs() << " worse than no bundles\n";
      else
        dbgs() << " worse than "
               << printReg(GlobalCand[BestCand].PhysReg, TRI) << '\n';
    });
    return BestCand;
  }
  if (!growRegion(Cand)) {
    LLVM_DEBUG(dbgs() << ", cannot spill all interferences.\n");
    return BestCand;
  }

  SpillPlacer->finish();

  // No live bundles, defer to splitSingleBlocks().
  if (!Cand.LiveBundles.any()) {
    LLVM_DEBUG(dbgs() << " no bundles.\n");
    return BestCand;
  }

  Cost += calcGlobalSplitCost(Cand, Order);
  LLVM_DEBUG({
    dbgs() << ", total = " << printBlockFreq(*MBFI, Cost) << " with bundles";
    for (int I : Cand.LiveBundles.set_bits())
      dbgs() << " EB#" << I;
    dbgs() << ".\n";
  });
  if (Cost < BestCost) {
    BestCand = NumCands;
    BestCost = Cost;
  }
  ++NumCands;

  return BestCand;
}

unsigned RAGreedy::calculateRegionSplitCost(const LiveInterval &VirtReg,
                                            AllocationOrder &Order,
                                            BlockFrequency &BestCost,
                                            unsigned &NumCands,
                                            bool IgnoreCSR) {
  unsigned BestCand = NoCand;
  for (MCPhysReg PhysReg : Order) {
    assert(PhysReg);
    if (IgnoreCSR && EvictAdvisor->isUnusedCalleeSavedReg(PhysReg))
      continue;

    calculateRegionSplitCostAroundReg(PhysReg, Order, BestCost, NumCands,
                                      BestCand);
  }

  return BestCand;
}

unsigned RAGreedy::doRegionSplit(const LiveInterval &VirtReg, unsigned BestCand,
                                 bool HasCompact,
                                 SmallVectorImpl<Register> &NewVRegs) {
  SmallVector<unsigned, 8> UsedCands;
  // Prepare split editor.
  LiveRangeEdit LREdit(&VirtReg, NewVRegs, *MF, *LIS, VRM, this, &DeadRemats);
  SE->reset(LREdit, SplitSpillMode);

  // Assign all edge bundles to the preferred candidate, or NoCand.
  BundleCand.assign(Bundles->getNumBundles(), NoCand);

  // Assign bundles for the best candidate region.
  if (BestCand != NoCand) {
    GlobalSplitCandidate &Cand = GlobalCand[BestCand];
    if (unsigned B = Cand.getBundles(BundleCand, BestCand)) {
      UsedCands.push_back(BestCand);
      Cand.IntvIdx = SE->openIntv();
      LLVM_DEBUG(dbgs() << "Split for " << printReg(Cand.PhysReg, TRI) << " in "
                        << B << " bundles, intv " << Cand.IntvIdx << ".\n");
      (void)B;
    }
  }

  // Assign bundles for the compact region.
  if (HasCompact) {
    GlobalSplitCandidate &Cand = GlobalCand.front();
    assert(!Cand.PhysReg && "Compact region has no physreg");
    if (unsigned B = Cand.getBundles(BundleCand, 0)) {
      UsedCands.push_back(0);
      Cand.IntvIdx = SE->openIntv();
      LLVM_DEBUG(dbgs() << "Split for compact region in " << B
                        << " bundles, intv " << Cand.IntvIdx << ".\n");
      (void)B;
    }
  }

  splitAroundRegion(LREdit, UsedCands);
  return 0;
}

// VirtReg has a physical Hint, this function tries to split VirtReg around
// Hint if we can place new COPY instructions in cold blocks.
bool RAGreedy::trySplitAroundHintReg(MCPhysReg Hint,
                                     const LiveInterval &VirtReg,
                                     SmallVectorImpl<Register> &NewVRegs,
                                     AllocationOrder &Order) {
  // Split the VirtReg may generate COPY instructions in multiple cold basic
  // blocks, and increase code size. So we avoid it when the function is
  // optimized for size.
  if (MF->getFunction().hasOptSize())
    return false;

  // Don't allow repeated splitting as a safe guard against looping.
  if (ExtraInfo->getStage(VirtReg) >= RS_Split2)
    return false;

  BlockFrequency Cost = BlockFrequency(0);
  Register Reg = VirtReg.reg();

  // Compute the cost of assigning a non Hint physical register to VirtReg.
  // We define it as the total frequency of broken COPY instructions to/from
  // Hint register, and after split, they can be deleted.
  for (const MachineInstr &Instr : MRI->reg_nodbg_instructions(Reg)) {
    if (!TII->isFullCopyInstr(Instr))
      continue;
    Register OtherReg = Instr.getOperand(1).getReg();
    if (OtherReg == Reg) {
      OtherReg = Instr.getOperand(0).getReg();
      if (OtherReg == Reg)
        continue;
      // Check if VirtReg interferes with OtherReg after this COPY instruction.
      if (VirtReg.liveAt(LIS->getInstructionIndex(Instr).getRegSlot()))
        continue;
    }
    MCRegister OtherPhysReg =
        OtherReg.isPhysical() ? OtherReg.asMCReg() : VRM->getPhys(OtherReg);
    if (OtherPhysReg == Hint)
      Cost += MBFI->getBlockFreq(Instr.getParent());
  }

  // Decrease the cost so it will be split in colder blocks.
  BranchProbability Threshold(SplitThresholdForRegWithHint, 100);
  Cost *= Threshold;
  if (Cost == BlockFrequency(0))
    return false;

  unsigned NumCands = 0;
  unsigned BestCand = NoCand;
  SA->analyze(&VirtReg);
  calculateRegionSplitCostAroundReg(Hint, Order, Cost, NumCands, BestCand);
  if (BestCand == NoCand)
    return false;

  doRegionSplit(VirtReg, BestCand, false/*HasCompact*/, NewVRegs);
  return true;
}

//===----------------------------------------------------------------------===//
//                            Per-Block Splitting
//===----------------------------------------------------------------------===//

/// tryBlockSplit - Split a global live range around every block with uses. This
/// creates a lot of local live ranges, that will be split by tryLocalSplit if
/// they don't allocate.
unsigned RAGreedy::tryBlockSplit(const LiveInterval &VirtReg,
                                 AllocationOrder &Order,
                                 SmallVectorImpl<Register> &NewVRegs) {
  assert(&SA->getParent() == &VirtReg && "Live range wasn't analyzed");
  Register Reg = VirtReg.reg();
  bool SingleInstrs = RegClassInfo.isProperSubClass(MRI->getRegClass(Reg));
  LiveRangeEdit LREdit(&VirtReg, NewVRegs, *MF, *LIS, VRM, this, &DeadRemats);
  SE->reset(LREdit, SplitSpillMode);
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();
  for (const SplitAnalysis::BlockInfo &BI : UseBlocks) {
    if (SA->shouldSplitSingleBlock(BI, SingleInstrs))
      SE->splitSingleBlock(BI);
  }
  // No blocks were split.
  if (LREdit.empty())
    return 0;

  // We did split for some blocks.
  SmallVector<unsigned, 8> IntvMap;
  SE->finish(&IntvMap);

  // Tell LiveDebugVariables about the new ranges.
  DebugVars->splitRegister(Reg, LREdit.regs(), *LIS);

  // Sort out the new intervals created by splitting. The remainder interval
  // goes straight to spilling, the new local ranges get to stay RS_New.
  for (unsigned I = 0, E = LREdit.size(); I != E; ++I) {
    const LiveInterval &LI = LIS->getInterval(LREdit.get(I));
    if (ExtraInfo->getOrInitStage(LI.reg()) == RS_New && IntvMap[I] == 0)
      ExtraInfo->setStage(LI, RS_Spill);
  }

  if (VerifyEnabled)
    MF->verify(this, "After splitting live range around basic blocks");
  return 0;
}

//===----------------------------------------------------------------------===//
//                         Per-Instruction Splitting
//===----------------------------------------------------------------------===//

/// Get the number of allocatable registers that match the constraints of \p Reg
/// on \p MI and that are also in \p SuperRC.
static unsigned getNumAllocatableRegsForConstraints(
    const MachineInstr *MI, Register Reg, const TargetRegisterClass *SuperRC,
    const TargetInstrInfo *TII, const TargetRegisterInfo *TRI,
    const RegisterClassInfo &RCI) {
  assert(SuperRC && "Invalid register class");

  const TargetRegisterClass *ConstrainedRC =
      MI->getRegClassConstraintEffectForVReg(Reg, SuperRC, TII, TRI,
                                             /* ExploreBundle */ true);
  if (!ConstrainedRC)
    return 0;
  return RCI.getNumAllocatableRegs(ConstrainedRC);
}

static LaneBitmask getInstReadLaneMask(const MachineRegisterInfo &MRI,
                                       const TargetRegisterInfo &TRI,
                                       const MachineInstr &FirstMI,
                                       Register Reg) {
  LaneBitmask Mask;
  SmallVector<std::pair<MachineInstr *, unsigned>, 8> Ops;
  (void)AnalyzeVirtRegInBundle(const_cast<MachineInstr &>(FirstMI), Reg, &Ops);

  for (auto [MI, OpIdx] : Ops) {
    const MachineOperand &MO = MI->getOperand(OpIdx);
    assert(MO.isReg() && MO.getReg() == Reg);
    unsigned SubReg = MO.getSubReg();
    if (SubReg == 0 && MO.isUse()) {
      if (MO.isUndef())
        continue;
      return MRI.getMaxLaneMaskForVReg(Reg);
    }

    LaneBitmask SubRegMask = TRI.getSubRegIndexLaneMask(SubReg);
    if (MO.isDef()) {
      if (!MO.isUndef())
        Mask |= ~SubRegMask;
    } else
      Mask |= SubRegMask;
  }

  return Mask;
}

/// Return true if \p MI at \P Use reads a subset of the lanes live in \p
/// VirtReg.
static bool readsLaneSubset(const MachineRegisterInfo &MRI,
                            const MachineInstr *MI, const LiveInterval &VirtReg,
                            const TargetRegisterInfo *TRI, SlotIndex Use,
                            const TargetInstrInfo *TII) {
  // Early check the common case. Beware of the semi-formed bundles SplitKit
  // creates by setting the bundle flag on copies without a matching BUNDLE.

  auto DestSrc = TII->isCopyInstr(*MI);
  if (DestSrc && !MI->isBundled() &&
      DestSrc->Destination->getSubReg() == DestSrc->Source->getSubReg())
    return false;

  // FIXME: We're only considering uses, but should be consider defs too?
  LaneBitmask ReadMask = getInstReadLaneMask(MRI, *TRI, *MI, VirtReg.reg());

  LaneBitmask LiveAtMask;
  for (const LiveInterval::SubRange &S : VirtReg.subranges()) {
    if (S.liveAt(Use))
      LiveAtMask |= S.LaneMask;
  }

  // If the live lanes aren't different from the lanes used by the instruction,
  // this doesn't help.
  return (ReadMask & ~(LiveAtMask & TRI->getCoveringLanes())).any();
}

/// tryInstructionSplit - Split a live range around individual instructions.
/// This is normally not worthwhile since the spiller is doing essentially the
/// same thing. However, when the live range is in a constrained register
/// class, it may help to insert copies such that parts of the live range can
/// be moved to a larger register class.
///
/// This is similar to spilling to a larger register class.
unsigned RAGreedy::tryInstructionSplit(const LiveInterval &VirtReg,
                                       AllocationOrder &Order,
                                       SmallVectorImpl<Register> &NewVRegs) {
  const TargetRegisterClass *CurRC = MRI->getRegClass(VirtReg.reg());
  // There is no point to this if there are no larger sub-classes.

  bool SplitSubClass = true;
  if (!RegClassInfo.isProperSubClass(CurRC)) {
    if (!VirtReg.hasSubRanges())
      return 0;
    SplitSubClass = false;
  }

  // Always enable split spill mode, since we're effectively spilling to a
  // register.
  LiveRangeEdit LREdit(&VirtReg, NewVRegs, *MF, *LIS, VRM, this, &DeadRemats);
  SE->reset(LREdit, SplitEditor::SM_Size);

  ArrayRef<SlotIndex> Uses = SA->getUseSlots();
  if (Uses.size() <= 1)
    return 0;

  LLVM_DEBUG(dbgs() << "Split around " << Uses.size()
                    << " individual instrs.\n");

  const TargetRegisterClass *SuperRC =
      TRI->getLargestLegalSuperClass(CurRC, *MF);
  unsigned SuperRCNumAllocatableRegs =
      RegClassInfo.getNumAllocatableRegs(SuperRC);
  // Split around every non-copy instruction if this split will relax
  // the constraints on the virtual register.
  // Otherwise, splitting just inserts uncoalescable copies that do not help
  // the allocation.
  for (const SlotIndex Use : Uses) {
    if (const MachineInstr *MI = Indexes->getInstructionFromIndex(Use)) {
      if (TII->isFullCopyInstr(*MI) ||
          (SplitSubClass &&
           SuperRCNumAllocatableRegs ==
               getNumAllocatableRegsForConstraints(MI, VirtReg.reg(), SuperRC,
                                                   TII, TRI, RegClassInfo)) ||
          // TODO: Handle split for subranges with subclass constraints?
          (!SplitSubClass && VirtReg.hasSubRanges() &&
           !readsLaneSubset(*MRI, MI, VirtReg, TRI, Use, TII))) {
        LLVM_DEBUG(dbgs() << "    skip:\t" << Use << '\t' << *MI);
        continue;
      }
    }
    SE->openIntv();
    SlotIndex SegStart = SE->enterIntvBefore(Use);
    SlotIndex SegStop = SE->leaveIntvAfter(Use);
    SE->useIntv(SegStart, SegStop);
  }

  if (LREdit.empty()) {
    LLVM_DEBUG(dbgs() << "All uses were copies.\n");
    return 0;
  }

  SmallVector<unsigned, 8> IntvMap;
  SE->finish(&IntvMap);
  DebugVars->splitRegister(VirtReg.reg(), LREdit.regs(), *LIS);
  // Assign all new registers to RS_Spill. This was the last chance.
  ExtraInfo->setStage(LREdit.begin(), LREdit.end(), RS_Spill);
  return 0;
}

//===----------------------------------------------------------------------===//
//                             Local Splitting
//===----------------------------------------------------------------------===//

/// calcGapWeights - Compute the maximum spill weight that needs to be evicted
/// in order to use PhysReg between two entries in SA->UseSlots.
///
/// GapWeight[I] represents the gap between UseSlots[I] and UseSlots[I + 1].
///
void RAGreedy::calcGapWeights(MCRegister PhysReg,
                              SmallVectorImpl<float> &GapWeight) {
  assert(SA->getUseBlocks().size() == 1 && "Not a local interval");
  const SplitAnalysis::BlockInfo &BI = SA->getUseBlocks().front();
  ArrayRef<SlotIndex> Uses = SA->getUseSlots();
  const unsigned NumGaps = Uses.size()-1;

  // Start and end points for the interference check.
  SlotIndex StartIdx =
    BI.LiveIn ? BI.FirstInstr.getBaseIndex() : BI.FirstInstr;
  SlotIndex StopIdx =
    BI.LiveOut ? BI.LastInstr.getBoundaryIndex() : BI.LastInstr;

  GapWeight.assign(NumGaps, 0.0f);

  // Add interference from each overlapping register.
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    if (!Matrix->query(const_cast<LiveInterval &>(SA->getParent()), Unit)
             .checkInterference())
      continue;

    // We know that VirtReg is a continuous interval from FirstInstr to
    // LastInstr, so we don't need InterferenceQuery.
    //
    // Interference that overlaps an instruction is counted in both gaps
    // surrounding the instruction. The exception is interference before
    // StartIdx and after StopIdx.
    //
    LiveIntervalUnion::SegmentIter IntI =
        Matrix->getLiveUnions()[Unit].find(StartIdx);
    for (unsigned Gap = 0; IntI.valid() && IntI.start() < StopIdx; ++IntI) {
      // Skip the gaps before IntI.
      while (Uses[Gap+1].getBoundaryIndex() < IntI.start())
        if (++Gap == NumGaps)
          break;
      if (Gap == NumGaps)
        break;

      // Update the gaps covered by IntI.
      const float weight = IntI.value()->weight();
      for (; Gap != NumGaps; ++Gap) {
        GapWeight[Gap] = std::max(GapWeight[Gap], weight);
        if (Uses[Gap+1].getBaseIndex() >= IntI.stop())
          break;
      }
      if (Gap == NumGaps)
        break;
    }
  }

  // Add fixed interference.
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    const LiveRange &LR = LIS->getRegUnit(Unit);
    LiveRange::const_iterator I = LR.find(StartIdx);
    LiveRange::const_iterator E = LR.end();

    // Same loop as above. Mark any overlapped gaps as HUGE_VALF.
    for (unsigned Gap = 0; I != E && I->start < StopIdx; ++I) {
      while (Uses[Gap+1].getBoundaryIndex() < I->start)
        if (++Gap == NumGaps)
          break;
      if (Gap == NumGaps)
        break;

      for (; Gap != NumGaps; ++Gap) {
        GapWeight[Gap] = huge_valf;
        if (Uses[Gap+1].getBaseIndex() >= I->end)
          break;
      }
      if (Gap == NumGaps)
        break;
    }
  }
}

/// tryLocalSplit - Try to split VirtReg into smaller intervals inside its only
/// basic block.
///
unsigned RAGreedy::tryLocalSplit(const LiveInterval &VirtReg,
                                 AllocationOrder &Order,
                                 SmallVectorImpl<Register> &NewVRegs) {
  // TODO: the function currently only handles a single UseBlock; it should be
  // possible to generalize.
  if (SA->getUseBlocks().size() != 1)
    return 0;

  const SplitAnalysis::BlockInfo &BI = SA->getUseBlocks().front();

  // Note that it is possible to have an interval that is live-in or live-out
  // while only covering a single block - A phi-def can use undef values from
  // predecessors, and the block could be a single-block loop.
  // We don't bother doing anything clever about such a case, we simply assume
  // that the interval is continuous from FirstInstr to LastInstr. We should
  // make sure that we don't do anything illegal to such an interval, though.

  ArrayRef<SlotIndex> Uses = SA->getUseSlots();
  if (Uses.size() <= 2)
    return 0;
  const unsigned NumGaps = Uses.size()-1;

  LLVM_DEBUG({
    dbgs() << "tryLocalSplit: ";
    for (const auto &Use : Uses)
      dbgs() << ' ' << Use;
    dbgs() << '\n';
  });

  // If VirtReg is live across any register mask operands, compute a list of
  // gaps with register masks.
  SmallVector<unsigned, 8> RegMaskGaps;
  if (Matrix->checkRegMaskInterference(VirtReg)) {
    // Get regmask slots for the whole block.
    ArrayRef<SlotIndex> RMS = LIS->getRegMaskSlotsInBlock(BI.MBB->getNumber());
    LLVM_DEBUG(dbgs() << RMS.size() << " regmasks in block:");
    // Constrain to VirtReg's live range.
    unsigned RI =
        llvm::lower_bound(RMS, Uses.front().getRegSlot()) - RMS.begin();
    unsigned RE = RMS.size();
    for (unsigned I = 0; I != NumGaps && RI != RE; ++I) {
      // Look for Uses[I] <= RMS <= Uses[I + 1].
      assert(!SlotIndex::isEarlierInstr(RMS[RI], Uses[I]));
      if (SlotIndex::isEarlierInstr(Uses[I + 1], RMS[RI]))
        continue;
      // Skip a regmask on the same instruction as the last use. It doesn't
      // overlap the live range.
      if (SlotIndex::isSameInstr(Uses[I + 1], RMS[RI]) && I + 1 == NumGaps)
        break;
      LLVM_DEBUG(dbgs() << ' ' << RMS[RI] << ':' << Uses[I] << '-'
                        << Uses[I + 1]);
      RegMaskGaps.push_back(I);
      // Advance ri to the next gap. A regmask on one of the uses counts in
      // both gaps.
      while (RI != RE && SlotIndex::isEarlierInstr(RMS[RI], Uses[I + 1]))
        ++RI;
    }
    LLVM_DEBUG(dbgs() << '\n');
  }

  // Since we allow local split results to be split again, there is a risk of
  // creating infinite loops. It is tempting to require that the new live
  // ranges have less instructions than the original. That would guarantee
  // convergence, but it is too strict. A live range with 3 instructions can be
  // split 2+3 (including the COPY), and we want to allow that.
  //
  // Instead we use these rules:
  //
  // 1. Allow any split for ranges with getStage() < RS_Split2. (Except for the
  //    noop split, of course).
  // 2. Require progress be made for ranges with getStage() == RS_Split2. All
  //    the new ranges must have fewer instructions than before the split.
  // 3. New ranges with the same number of instructions are marked RS_Split2,
  //    smaller ranges are marked RS_New.
  //
  // These rules allow a 3 -> 2+3 split once, which we need. They also prevent
  // excessive splitting and infinite loops.
  //
  bool ProgressRequired = ExtraInfo->getStage(VirtReg) >= RS_Split2;

  // Best split candidate.
  unsigned BestBefore = NumGaps;
  unsigned BestAfter = 0;
  float BestDiff = 0;

  const float blockFreq =
      SpillPlacer->getBlockFrequency(BI.MBB->getNumber()).getFrequency() *
      (1.0f / MBFI->getEntryFreq().getFrequency());
  SmallVector<float, 8> GapWeight;

  for (MCPhysReg PhysReg : Order) {
    assert(PhysReg);
    // Keep track of the largest spill weight that would need to be evicted in
    // order to make use of PhysReg between UseSlots[I] and UseSlots[I + 1].
    calcGapWeights(PhysReg, GapWeight);

    // Remove any gaps with regmask clobbers.
    if (Matrix->checkRegMaskInterference(VirtReg, PhysReg))
      for (unsigned Gap : RegMaskGaps)
        GapWeight[Gap] = huge_valf;

    // Try to find the best sequence of gaps to close.
    // The new spill weight must be larger than any gap interference.

    // We will split before Uses[SplitBefore] and after Uses[SplitAfter].
    unsigned SplitBefore = 0, SplitAfter = 1;

    // MaxGap should always be max(GapWeight[SplitBefore..SplitAfter-1]).
    // It is the spill weight that needs to be evicted.
    float MaxGap = GapWeight[0];

    while (true) {
      // Live before/after split?
      const bool LiveBefore = SplitBefore != 0 || BI.LiveIn;
      const bool LiveAfter = SplitAfter != NumGaps || BI.LiveOut;

      LLVM_DEBUG(dbgs() << printReg(PhysReg, TRI) << ' ' << Uses[SplitBefore]
                        << '-' << Uses[SplitAfter] << " I=" << MaxGap);

      // Stop before the interval gets so big we wouldn't be making progress.
      if (!LiveBefore && !LiveAfter) {
        LLVM_DEBUG(dbgs() << " all\n");
        break;
      }
      // Should the interval be extended or shrunk?
      bool Shrink = true;

      // How many gaps would the new range have?
      unsigned NewGaps = LiveBefore + SplitAfter - SplitBefore + LiveAfter;

      // Legally, without causing looping?
      bool Legal = !ProgressRequired || NewGaps < NumGaps;

      if (Legal && MaxGap < huge_valf) {
        // Estimate the new spill weight. Each instruction reads or writes the
        // register. Conservatively assume there are no read-modify-write
        // instructions.
        //
        // Try to guess the size of the new interval.
        const float EstWeight = normalizeSpillWeight(
            blockFreq * (NewGaps + 1),
            Uses[SplitBefore].distance(Uses[SplitAfter]) +
                (LiveBefore + LiveAfter) * SlotIndex::InstrDist,
            1);
        // Would this split be possible to allocate?
        // Never allocate all gaps, we wouldn't be making progress.
        LLVM_DEBUG(dbgs() << " w=" << EstWeight);
        if (EstWeight * Hysteresis >= MaxGap) {
          Shrink = false;
          float Diff = EstWeight - MaxGap;
          if (Diff > BestDiff) {
            LLVM_DEBUG(dbgs() << " (best)");
            BestDiff = Hysteresis * Diff;
            BestBefore = SplitBefore;
            BestAfter = SplitAfter;
          }
        }
      }

      // Try to shrink.
      if (Shrink) {
        if (++SplitBefore < SplitAfter) {
          LLVM_DEBUG(dbgs() << " shrink\n");
          // Recompute the max when necessary.
          if (GapWeight[SplitBefore - 1] >= MaxGap) {
            MaxGap = GapWeight[SplitBefore];
            for (unsigned I = SplitBefore + 1; I != SplitAfter; ++I)
              MaxGap = std::max(MaxGap, GapWeight[I]);
          }
          continue;
        }
        MaxGap = 0;
      }

      // Try to extend the interval.
      if (SplitAfter >= NumGaps) {
        LLVM_DEBUG(dbgs() << " end\n");
        break;
      }

      LLVM_DEBUG(dbgs() << " extend\n");
      MaxGap = std::max(MaxGap, GapWeight[SplitAfter++]);
    }
  }

  // Didn't find any candidates?
  if (BestBefore == NumGaps)
    return 0;

  LLVM_DEBUG(dbgs() << "Best local split range: " << Uses[BestBefore] << '-'
                    << Uses[BestAfter] << ", " << BestDiff << ", "
                    << (BestAfter - BestBefore + 1) << " instrs\n");

  LiveRangeEdit LREdit(&VirtReg, NewVRegs, *MF, *LIS, VRM, this, &DeadRemats);
  SE->reset(LREdit);

  SE->openIntv();
  SlotIndex SegStart = SE->enterIntvBefore(Uses[BestBefore]);
  SlotIndex SegStop  = SE->leaveIntvAfter(Uses[BestAfter]);
  SE->useIntv(SegStart, SegStop);
  SmallVector<unsigned, 8> IntvMap;
  SE->finish(&IntvMap);
  DebugVars->splitRegister(VirtReg.reg(), LREdit.regs(), *LIS);
  // If the new range has the same number of instructions as before, mark it as
  // RS_Split2 so the next split will be forced to make progress. Otherwise,
  // leave the new intervals as RS_New so they can compete.
  bool LiveBefore = BestBefore != 0 || BI.LiveIn;
  bool LiveAfter = BestAfter != NumGaps || BI.LiveOut;
  unsigned NewGaps = LiveBefore + BestAfter - BestBefore + LiveAfter;
  if (NewGaps >= NumGaps) {
    LLVM_DEBUG(dbgs() << "Tagging non-progress ranges:");
    assert(!ProgressRequired && "Didn't make progress when it was required.");
    for (unsigned I = 0, E = IntvMap.size(); I != E; ++I)
      if (IntvMap[I] == 1) {
        ExtraInfo->setStage(LIS->getInterval(LREdit.get(I)), RS_Split2);
        LLVM_DEBUG(dbgs() << ' ' << printReg(LREdit.get(I)));
      }
    LLVM_DEBUG(dbgs() << '\n');
  }
  ++NumLocalSplits;

  return 0;
}

//===----------------------------------------------------------------------===//
//                          Live Range Splitting
//===----------------------------------------------------------------------===//

/// trySplit - Try to split VirtReg or one of its interferences, making it
/// assignable.
/// @return Physreg when VirtReg may be assigned and/or new NewVRegs.
unsigned RAGreedy::trySplit(const LiveInterval &VirtReg, AllocationOrder &Order,
                            SmallVectorImpl<Register> &NewVRegs,
                            const SmallVirtRegSet &FixedRegisters) {
  // Ranges must be Split2 or less.
  if (ExtraInfo->getStage(VirtReg) >= RS_Spill)
    return 0;

  // Local intervals are handled separately.
  if (LIS->intervalIsInOneMBB(VirtReg)) {
    NamedRegionTimer T("local_split", "Local Splitting", TimerGroupName,
                       TimerGroupDescription, TimePassesIsEnabled);
    SA->analyze(&VirtReg);
    Register PhysReg = tryLocalSplit(VirtReg, Order, NewVRegs);
    if (PhysReg || !NewVRegs.empty())
      return PhysReg;
    return tryInstructionSplit(VirtReg, Order, NewVRegs);
  }

  NamedRegionTimer T("global_split", "Global Splitting", TimerGroupName,
                     TimerGroupDescription, TimePassesIsEnabled);

  SA->analyze(&VirtReg);

  // First try to split around a region spanning multiple blocks. RS_Split2
  // ranges already made dubious progress with region splitting, so they go
  // straight to single block splitting.
  if (ExtraInfo->getStage(VirtReg) < RS_Split2) {
    MCRegister PhysReg = tryRegionSplit(VirtReg, Order, NewVRegs);
    if (PhysReg || !NewVRegs.empty())
      return PhysReg;
  }

  // Then isolate blocks.
  return tryBlockSplit(VirtReg, Order, NewVRegs);
}

//===----------------------------------------------------------------------===//
//                          Last Chance Recoloring
//===----------------------------------------------------------------------===//

/// Return true if \p reg has any tied def operand.
static bool hasTiedDef(MachineRegisterInfo *MRI, unsigned reg) {
  for (const MachineOperand &MO : MRI->def_operands(reg))
    if (MO.isTied())
      return true;

  return false;
}

/// Return true if the existing assignment of \p Intf overlaps, but is not the
/// same, as \p PhysReg.
static bool assignedRegPartiallyOverlaps(const TargetRegisterInfo &TRI,
                                         const VirtRegMap &VRM,
                                         MCRegister PhysReg,
                                         const LiveInterval &Intf) {
  MCRegister AssignedReg = VRM.getPhys(Intf.reg());
  if (PhysReg == AssignedReg)
    return false;
  return TRI.regsOverlap(PhysReg, AssignedReg);
}

/// mayRecolorAllInterferences - Check if the virtual registers that
/// interfere with \p VirtReg on \p PhysReg (or one of its aliases) may be
/// recolored to free \p PhysReg.
/// When true is returned, \p RecoloringCandidates has been augmented with all
/// the live intervals that need to be recolored in order to free \p PhysReg
/// for \p VirtReg.
/// \p FixedRegisters contains all the virtual registers that cannot be
/// recolored.
bool RAGreedy::mayRecolorAllInterferences(
    MCRegister PhysReg, const LiveInterval &VirtReg,
    SmallLISet &RecoloringCandidates, const SmallVirtRegSet &FixedRegisters) {
  const TargetRegisterClass *CurRC = MRI->getRegClass(VirtReg.reg());

  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    LiveIntervalUnion::Query &Q = Matrix->query(VirtReg, Unit);
    // If there is LastChanceRecoloringMaxInterference or more interferences,
    // chances are one would not be recolorable.
    if (Q.interferingVRegs(LastChanceRecoloringMaxInterference).size() >=
            LastChanceRecoloringMaxInterference &&
        !ExhaustiveSearch) {
      LLVM_DEBUG(dbgs() << "Early abort: too many interferences.\n");
      CutOffInfo |= CO_Interf;
      return false;
    }
    for (const LiveInterval *Intf : reverse(Q.interferingVRegs())) {
      // If Intf is done and sits on the same register class as VirtReg, it
      // would not be recolorable as it is in the same state as
      // VirtReg. However there are at least two exceptions.
      //
      // If VirtReg has tied defs and Intf doesn't, then
      // there is still a point in examining if it can be recolorable.
      //
      // Additionally, if the register class has overlapping tuple members, it
      // may still be recolorable using a different tuple. This is more likely
      // if the existing assignment aliases with the candidate.
      //
      if (((ExtraInfo->getStage(*Intf) == RS_Done &&
            MRI->getRegClass(Intf->reg()) == CurRC &&
            !assignedRegPartiallyOverlaps(*TRI, *VRM, PhysReg, *Intf)) &&
           !(hasTiedDef(MRI, VirtReg.reg()) &&
             !hasTiedDef(MRI, Intf->reg()))) ||
          FixedRegisters.count(Intf->reg())) {
        LLVM_DEBUG(
            dbgs() << "Early abort: the interference is not recolorable.\n");
        return false;
      }
      RecoloringCandidates.insert(Intf);
    }
  }
  return true;
}

/// tryLastChanceRecoloring - Try to assign a color to \p VirtReg by recoloring
/// its interferences.
/// Last chance recoloring chooses a color for \p VirtReg and recolors every
/// virtual register that was using it. The recoloring process may recursively
/// use the last chance recoloring. Therefore, when a virtual register has been
/// assigned a color by this mechanism, it is marked as Fixed, i.e., it cannot
/// be last-chance-recolored again during this recoloring "session".
/// E.g.,
/// Let
/// vA can use {R1, R2    }
/// vB can use {    R2, R3}
/// vC can use {R1        }
/// Where vA, vB, and vC cannot be split anymore (they are reloads for
/// instance) and they all interfere.
///
/// vA is assigned R1
/// vB is assigned R2
/// vC tries to evict vA but vA is already done.
/// Regular register allocation fails.
///
/// Last chance recoloring kicks in:
/// vC does as if vA was evicted => vC uses R1.
/// vC is marked as fixed.
/// vA needs to find a color.
/// None are available.
/// vA cannot evict vC: vC is a fixed virtual register now.
/// vA does as if vB was evicted => vA uses R2.
/// vB needs to find a color.
/// R3 is available.
/// Recoloring => vC = R1, vA = R2, vB = R3
///
/// \p Order defines the preferred allocation order for \p VirtReg.
/// \p NewRegs will contain any new virtual register that have been created
/// (split, spill) during the process and that must be assigned.
/// \p FixedRegisters contains all the virtual registers that cannot be
/// recolored.
///
/// \p RecolorStack tracks the original assignments of successfully recolored
/// registers.
///
/// \p Depth gives the current depth of the last chance recoloring.
/// \return a physical register that can be used for VirtReg or ~0u if none
/// exists.
unsigned RAGreedy::tryLastChanceRecoloring(const LiveInterval &VirtReg,
                                           AllocationOrder &Order,
                                           SmallVectorImpl<Register> &NewVRegs,
                                           SmallVirtRegSet &FixedRegisters,
                                           RecoloringStack &RecolorStack,
                                           unsigned Depth) {
  if (!TRI->shouldUseLastChanceRecoloringForVirtReg(*MF, VirtReg))
    return ~0u;

  LLVM_DEBUG(dbgs() << "Try last chance recoloring for " << VirtReg << '\n');

  const ssize_t EntryStackSize = RecolorStack.size();

  // Ranges must be Done.
  assert((ExtraInfo->getStage(VirtReg) >= RS_Done || !VirtReg.isSpillable()) &&
         "Last chance recoloring should really be last chance");
  // Set the max depth to LastChanceRecoloringMaxDepth.
  // We may want to reconsider that if we end up with a too large search space
  // for target with hundreds of registers.
  // Indeed, in that case we may want to cut the search space earlier.
  if (Depth >= LastChanceRecoloringMaxDepth && !ExhaustiveSearch) {
    LLVM_DEBUG(dbgs() << "Abort because max depth has been reached.\n");
    CutOffInfo |= CO_Depth;
    return ~0u;
  }

  // Set of Live intervals that will need to be recolored.
  SmallLISet RecoloringCandidates;

  // Mark VirtReg as fixed, i.e., it will not be recolored pass this point in
  // this recoloring "session".
  assert(!FixedRegisters.count(VirtReg.reg()));
  FixedRegisters.insert(VirtReg.reg());
  SmallVector<Register, 4> CurrentNewVRegs;

  for (MCRegister PhysReg : Order) {
    assert(PhysReg.isValid());
    LLVM_DEBUG(dbgs() << "Try to assign: " << VirtReg << " to "
                      << printReg(PhysReg, TRI) << '\n');
    RecoloringCandidates.clear();
    CurrentNewVRegs.clear();

    // It is only possible to recolor virtual register interference.
    if (Matrix->checkInterference(VirtReg, PhysReg) >
        LiveRegMatrix::IK_VirtReg) {
      LLVM_DEBUG(
          dbgs() << "Some interferences are not with virtual registers.\n");

      continue;
    }

    // Early give up on this PhysReg if it is obvious we cannot recolor all
    // the interferences.
    if (!mayRecolorAllInterferences(PhysReg, VirtReg, RecoloringCandidates,
                                    FixedRegisters)) {
      LLVM_DEBUG(dbgs() << "Some interferences cannot be recolored.\n");
      continue;
    }

    // RecoloringCandidates contains all the virtual registers that interfere
    // with VirtReg on PhysReg (or one of its aliases). Enqueue them for
    // recoloring and perform the actual recoloring.
    PQueue RecoloringQueue;
    for (const LiveInterval *RC : RecoloringCandidates) {
      Register ItVirtReg = RC->reg();
      enqueue(RecoloringQueue, RC);
      assert(VRM->hasPhys(ItVirtReg) &&
             "Interferences are supposed to be with allocated variables");

      // Record the current allocation.
      RecolorStack.push_back(std::make_pair(RC, VRM->getPhys(ItVirtReg)));

      // unset the related struct.
      Matrix->unassign(*RC);
    }

    // Do as if VirtReg was assigned to PhysReg so that the underlying
    // recoloring has the right information about the interferes and
    // available colors.
    Matrix->assign(VirtReg, PhysReg);

    // Save the current recoloring state.
    // If we cannot recolor all the interferences, we will have to start again
    // at this point for the next physical register.
    SmallVirtRegSet SaveFixedRegisters(FixedRegisters);
    if (tryRecoloringCandidates(RecoloringQueue, CurrentNewVRegs,
                                FixedRegisters, RecolorStack, Depth)) {
      // Push the queued vregs into the main queue.
      for (Register NewVReg : CurrentNewVRegs)
        NewVRegs.push_back(NewVReg);
      // Do not mess up with the global assignment process.
      // I.e., VirtReg must be unassigned.
      Matrix->unassign(VirtReg);
      return PhysReg;
    }

    LLVM_DEBUG(dbgs() << "Fail to assign: " << VirtReg << " to "
                      << printReg(PhysReg, TRI) << '\n');

    // The recoloring attempt failed, undo the changes.
    FixedRegisters = SaveFixedRegisters;
    Matrix->unassign(VirtReg);

    // For a newly created vreg which is also in RecoloringCandidates,
    // don't add it to NewVRegs because its physical register will be restored
    // below. Other vregs in CurrentNewVRegs are created by calling
    // selectOrSplit and should be added into NewVRegs.
    for (Register R : CurrentNewVRegs) {
      if (RecoloringCandidates.count(&LIS->getInterval(R)))
        continue;
      NewVRegs.push_back(R);
    }

    // Roll back our unsuccessful recoloring. Also roll back any successful
    // recolorings in any recursive recoloring attempts, since it's possible
    // they would have introduced conflicts with assignments we will be
    // restoring further up the stack. Perform all unassignments prior to
    // reassigning, since sub-recolorings may have conflicted with the registers
    // we are going to restore to their original assignments.
    for (ssize_t I = RecolorStack.size() - 1; I >= EntryStackSize; --I) {
      const LiveInterval *LI;
      MCRegister PhysReg;
      std::tie(LI, PhysReg) = RecolorStack[I];

      if (VRM->hasPhys(LI->reg()))
        Matrix->unassign(*LI);
    }

    for (size_t I = EntryStackSize; I != RecolorStack.size(); ++I) {
      const LiveInterval *LI;
      MCRegister PhysReg;
      std::tie(LI, PhysReg) = RecolorStack[I];
      if (!LI->empty() && !MRI->reg_nodbg_empty(LI->reg()))
        Matrix->assign(*LI, PhysReg);
    }

    // Pop the stack of recoloring attempts.
    RecolorStack.resize(EntryStackSize);
  }

  // Last chance recoloring did not worked either, give up.
  return ~0u;
}

/// tryRecoloringCandidates - Try to assign a new color to every register
/// in \RecoloringQueue.
/// \p NewRegs will contain any new virtual register created during the
/// recoloring process.
/// \p FixedRegisters[in/out] contains all the registers that have been
/// recolored.
/// \return true if all virtual registers in RecoloringQueue were successfully
/// recolored, false otherwise.
bool RAGreedy::tryRecoloringCandidates(PQueue &RecoloringQueue,
                                       SmallVectorImpl<Register> &NewVRegs,
                                       SmallVirtRegSet &FixedRegisters,
                                       RecoloringStack &RecolorStack,
                                       unsigned Depth) {
  while (!RecoloringQueue.empty()) {
    const LiveInterval *LI = dequeue(RecoloringQueue);
    LLVM_DEBUG(dbgs() << "Try to recolor: " << *LI << '\n');
    MCRegister PhysReg = selectOrSplitImpl(*LI, NewVRegs, FixedRegisters,
                                           RecolorStack, Depth + 1);
    // When splitting happens, the live-range may actually be empty.
    // In that case, this is okay to continue the recoloring even
    // if we did not find an alternative color for it. Indeed,
    // there will not be anything to color for LI in the end.
    if (PhysReg == ~0u || (!PhysReg && !LI->empty()))
      return false;

    if (!PhysReg) {
      assert(LI->empty() && "Only empty live-range do not require a register");
      LLVM_DEBUG(dbgs() << "Recoloring of " << *LI
                        << " succeeded. Empty LI.\n");
      continue;
    }
    LLVM_DEBUG(dbgs() << "Recoloring of " << *LI
                      << " succeeded with: " << printReg(PhysReg, TRI) << '\n');

    Matrix->assign(*LI, PhysReg);
    FixedRegisters.insert(LI->reg());
  }
  return true;
}

//===----------------------------------------------------------------------===//
//                            Main Entry Point
//===----------------------------------------------------------------------===//

MCRegister RAGreedy::selectOrSplit(const LiveInterval &VirtReg,
                                   SmallVectorImpl<Register> &NewVRegs) {
  CutOffInfo = CO_None;
  LLVMContext &Ctx = MF->getFunction().getContext();
  SmallVirtRegSet FixedRegisters;
  RecoloringStack RecolorStack;
  MCRegister Reg =
      selectOrSplitImpl(VirtReg, NewVRegs, FixedRegisters, RecolorStack);
  if (Reg == ~0U && (CutOffInfo != CO_None)) {
    uint8_t CutOffEncountered = CutOffInfo & (CO_Depth | CO_Interf);
    if (CutOffEncountered == CO_Depth)
      Ctx.emitError("register allocation failed: maximum depth for recoloring "
                    "reached. Use -fexhaustive-register-search to skip "
                    "cutoffs");
    else if (CutOffEncountered == CO_Interf)
      Ctx.emitError("register allocation failed: maximum interference for "
                    "recoloring reached. Use -fexhaustive-register-search "
                    "to skip cutoffs");
    else if (CutOffEncountered == (CO_Depth | CO_Interf))
      Ctx.emitError("register allocation failed: maximum interference and "
                    "depth for recoloring reached. Use "
                    "-fexhaustive-register-search to skip cutoffs");
  }
  return Reg;
}

/// Using a CSR for the first time has a cost because it causes push|pop
/// to be added to prologue|epilogue. Splitting a cold section of the live
/// range can have lower cost than using the CSR for the first time;
/// Spilling a live range in the cold path can have lower cost than using
/// the CSR for the first time. Returns the physical register if we decide
/// to use the CSR; otherwise return 0.
MCRegister RAGreedy::tryAssignCSRFirstTime(
    const LiveInterval &VirtReg, AllocationOrder &Order, MCRegister PhysReg,
    uint8_t &CostPerUseLimit, SmallVectorImpl<Register> &NewVRegs) {
  if (ExtraInfo->getStage(VirtReg) == RS_Spill && VirtReg.isSpillable()) {
    // We choose spill over using the CSR for the first time if the spill cost
    // is lower than CSRCost.
    SA->analyze(&VirtReg);
    if (calcSpillCost() >= CSRCost)
      return PhysReg;

    // We are going to spill, set CostPerUseLimit to 1 to make sure that
    // we will not use a callee-saved register in tryEvict.
    CostPerUseLimit = 1;
    return 0;
  }
  if (ExtraInfo->getStage(VirtReg) < RS_Split) {
    // We choose pre-splitting over using the CSR for the first time if
    // the cost of splitting is lower than CSRCost.
    SA->analyze(&VirtReg);
    unsigned NumCands = 0;
    BlockFrequency BestCost = CSRCost; // Don't modify CSRCost.
    unsigned BestCand = calculateRegionSplitCost(VirtReg, Order, BestCost,
                                                 NumCands, true /*IgnoreCSR*/);
    if (BestCand == NoCand)
      // Use the CSR if we can't find a region split below CSRCost.
      return PhysReg;

    // Perform the actual pre-splitting.
    doRegionSplit(VirtReg, BestCand, false/*HasCompact*/, NewVRegs);
    return 0;
  }
  return PhysReg;
}

void RAGreedy::aboutToRemoveInterval(const LiveInterval &LI) {
  // Do not keep invalid information around.
  SetOfBrokenHints.remove(&LI);
}

void RAGreedy::initializeCSRCost() {
  // We use the larger one out of the command-line option and the value report
  // by TRI.
  CSRCost = BlockFrequency(
      std::max((unsigned)CSRFirstTimeCost, TRI->getCSRFirstUseCost()));
  if (!CSRCost.getFrequency())
    return;

  // Raw cost is relative to Entry == 2^14; scale it appropriately.
  uint64_t ActualEntry = MBFI->getEntryFreq().getFrequency();
  if (!ActualEntry) {
    CSRCost = BlockFrequency(0);
    return;
  }
  uint64_t FixedEntry = 1 << 14;
  if (ActualEntry < FixedEntry)
    CSRCost *= BranchProbability(ActualEntry, FixedEntry);
  else if (ActualEntry <= UINT32_MAX)
    // Invert the fraction and divide.
    CSRCost /= BranchProbability(FixedEntry, ActualEntry);
  else
    // Can't use BranchProbability in general, since it takes 32-bit numbers.
    CSRCost =
        BlockFrequency(CSRCost.getFrequency() * (ActualEntry / FixedEntry));
}

/// Collect the hint info for \p Reg.
/// The results are stored into \p Out.
/// \p Out is not cleared before being populated.
void RAGreedy::collectHintInfo(Register Reg, HintsInfo &Out) {
  for (const MachineInstr &Instr : MRI->reg_nodbg_instructions(Reg)) {
    if (!TII->isFullCopyInstr(Instr))
      continue;
    // Look for the other end of the copy.
    Register OtherReg = Instr.getOperand(0).getReg();
    if (OtherReg == Reg) {
      OtherReg = Instr.getOperand(1).getReg();
      if (OtherReg == Reg)
        continue;
    }
    // Get the current assignment.
    MCRegister OtherPhysReg =
        OtherReg.isPhysical() ? OtherReg.asMCReg() : VRM->getPhys(OtherReg);
    // Push the collected information.
    Out.push_back(HintInfo(MBFI->getBlockFreq(Instr.getParent()), OtherReg,
                           OtherPhysReg));
  }
}

/// Using the given \p List, compute the cost of the broken hints if
/// \p PhysReg was used.
/// \return The cost of \p List for \p PhysReg.
BlockFrequency RAGreedy::getBrokenHintFreq(const HintsInfo &List,
                                           MCRegister PhysReg) {
  BlockFrequency Cost = BlockFrequency(0);
  for (const HintInfo &Info : List) {
    if (Info.PhysReg != PhysReg)
      Cost += Info.Freq;
  }
  return Cost;
}

/// Using the register assigned to \p VirtReg, try to recolor
/// all the live ranges that are copy-related with \p VirtReg.
/// The recoloring is then propagated to all the live-ranges that have
/// been recolored and so on, until no more copies can be coalesced or
/// it is not profitable.
/// For a given live range, profitability is determined by the sum of the
/// frequencies of the non-identity copies it would introduce with the old
/// and new register.
void RAGreedy::tryHintRecoloring(const LiveInterval &VirtReg) {
  // We have a broken hint, check if it is possible to fix it by
  // reusing PhysReg for the copy-related live-ranges. Indeed, we evicted
  // some register and PhysReg may be available for the other live-ranges.
  SmallSet<Register, 4> Visited;
  SmallVector<unsigned, 2> RecoloringCandidates;
  HintsInfo Info;
  Register Reg = VirtReg.reg();
  MCRegister PhysReg = VRM->getPhys(Reg);
  // Start the recoloring algorithm from the input live-interval, then
  // it will propagate to the ones that are copy-related with it.
  Visited.insert(Reg);
  RecoloringCandidates.push_back(Reg);

  LLVM_DEBUG(dbgs() << "Trying to reconcile hints for: " << printReg(Reg, TRI)
                    << '(' << printReg(PhysReg, TRI) << ")\n");

  do {
    Reg = RecoloringCandidates.pop_back_val();

    // We cannot recolor physical register.
    if (Reg.isPhysical())
      continue;

    // This may be a skipped register.
    if (!VRM->hasPhys(Reg)) {
      assert(!shouldAllocateRegister(Reg) &&
             "We have an unallocated variable which should have been handled");
      continue;
    }

    // Get the live interval mapped with this virtual register to be able
    // to check for the interference with the new color.
    LiveInterval &LI = LIS->getInterval(Reg);
    MCRegister CurrPhys = VRM->getPhys(Reg);
    // Check that the new color matches the register class constraints and
    // that it is free for this live range.
    if (CurrPhys != PhysReg && (!MRI->getRegClass(Reg)->contains(PhysReg) ||
                                Matrix->checkInterference(LI, PhysReg)))
      continue;

    LLVM_DEBUG(dbgs() << printReg(Reg, TRI) << '(' << printReg(CurrPhys, TRI)
                      << ") is recolorable.\n");

    // Gather the hint info.
    Info.clear();
    collectHintInfo(Reg, Info);
    // Check if recoloring the live-range will increase the cost of the
    // non-identity copies.
    if (CurrPhys != PhysReg) {
      LLVM_DEBUG(dbgs() << "Checking profitability:\n");
      BlockFrequency OldCopiesCost = getBrokenHintFreq(Info, CurrPhys);
      BlockFrequency NewCopiesCost = getBrokenHintFreq(Info, PhysReg);
      LLVM_DEBUG(dbgs() << "Old Cost: " << printBlockFreq(*MBFI, OldCopiesCost)
                        << "\nNew Cost: "
                        << printBlockFreq(*MBFI, NewCopiesCost) << '\n');
      if (OldCopiesCost < NewCopiesCost) {
        LLVM_DEBUG(dbgs() << "=> Not profitable.\n");
        continue;
      }
      // At this point, the cost is either cheaper or equal. If it is
      // equal, we consider this is profitable because it may expose
      // more recoloring opportunities.
      LLVM_DEBUG(dbgs() << "=> Profitable.\n");
      // Recolor the live-range.
      Matrix->unassign(LI);
      Matrix->assign(LI, PhysReg);
    }
    // Push all copy-related live-ranges to keep reconciling the broken
    // hints.
    for (const HintInfo &HI : Info) {
      if (Visited.insert(HI.Reg).second)
        RecoloringCandidates.push_back(HI.Reg);
    }
  } while (!RecoloringCandidates.empty());
}

/// Try to recolor broken hints.
/// Broken hints may be repaired by recoloring when an evicted variable
/// freed up a register for a larger live-range.
/// Consider the following example:
/// BB1:
///   a =
///   b =
/// BB2:
///   ...
///   = b
///   = a
/// Let us assume b gets split:
/// BB1:
///   a =
///   b =
/// BB2:
///   c = b
///   ...
///   d = c
///   = d
///   = a
/// Because of how the allocation work, b, c, and d may be assigned different
/// colors. Now, if a gets evicted later:
/// BB1:
///   a =
///   st a, SpillSlot
///   b =
/// BB2:
///   c = b
///   ...
///   d = c
///   = d
///   e = ld SpillSlot
///   = e
/// This is likely that we can assign the same register for b, c, and d,
/// getting rid of 2 copies.
void RAGreedy::tryHintsRecoloring() {
  for (const LiveInterval *LI : SetOfBrokenHints) {
    assert(LI->reg().isVirtual() &&
           "Recoloring is possible only for virtual registers");
    // Some dead defs may be around (e.g., because of debug uses).
    // Ignore those.
    if (!VRM->hasPhys(LI->reg()))
      continue;
    tryHintRecoloring(*LI);
  }
}

MCRegister RAGreedy::selectOrSplitImpl(const LiveInterval &VirtReg,
                                       SmallVectorImpl<Register> &NewVRegs,
                                       SmallVirtRegSet &FixedRegisters,
                                       RecoloringStack &RecolorStack,
                                       unsigned Depth) {
  uint8_t CostPerUseLimit = uint8_t(~0u);
  // First try assigning a free register.
  auto Order =
      AllocationOrder::create(VirtReg.reg(), *VRM, RegClassInfo, Matrix);
  if (MCRegister PhysReg =
          tryAssign(VirtReg, Order, NewVRegs, FixedRegisters)) {
    // When NewVRegs is not empty, we may have made decisions such as evicting
    // a virtual register, go with the earlier decisions and use the physical
    // register.
    if (CSRCost.getFrequency() &&
        EvictAdvisor->isUnusedCalleeSavedReg(PhysReg) && NewVRegs.empty()) {
      MCRegister CSRReg = tryAssignCSRFirstTime(VirtReg, Order, PhysReg,
                                                CostPerUseLimit, NewVRegs);
      if (CSRReg || !NewVRegs.empty())
        // Return now if we decide to use a CSR or create new vregs due to
        // pre-splitting.
        return CSRReg;
    } else
      return PhysReg;
  }
  // Non emtpy NewVRegs means VirtReg has been split.
  if (!NewVRegs.empty())
    return 0;

  LiveRangeStage Stage = ExtraInfo->getStage(VirtReg);
  LLVM_DEBUG(dbgs() << StageName[Stage] << " Cascade "
                    << ExtraInfo->getCascade(VirtReg.reg()) << '\n');

  // Try to evict a less worthy live range, but only for ranges from the primary
  // queue. The RS_Split ranges already failed to do this, and they should not
  // get a second chance until they have been split.
  if (Stage != RS_Split)
    if (Register PhysReg =
            tryEvict(VirtReg, Order, NewVRegs, CostPerUseLimit,
                     FixedRegisters)) {
      Register Hint = MRI->getSimpleHint(VirtReg.reg());
      // If VirtReg has a hint and that hint is broken record this
      // virtual register as a recoloring candidate for broken hint.
      // Indeed, since we evicted a variable in its neighborhood it is
      // likely we can at least partially recolor some of the
      // copy-related live-ranges.
      if (Hint && Hint != PhysReg)
        SetOfBrokenHints.insert(&VirtReg);
      return PhysReg;
    }

  assert((NewVRegs.empty() || Depth) && "Cannot append to existing NewVRegs");

  // The first time we see a live range, don't try to split or spill.
  // Wait until the second time, when all smaller ranges have been allocated.
  // This gives a better picture of the interference to split around.
  if (Stage < RS_Split) {
    ExtraInfo->setStage(VirtReg, RS_Split);
    LLVM_DEBUG(dbgs() << "wait for second round\n");
    NewVRegs.push_back(VirtReg.reg());
    return 0;
  }

  if (Stage < RS_Spill) {
    // Try splitting VirtReg or interferences.
    unsigned NewVRegSizeBefore = NewVRegs.size();
    Register PhysReg = trySplit(VirtReg, Order, NewVRegs, FixedRegisters);
    if (PhysReg || (NewVRegs.size() - NewVRegSizeBefore))
      return PhysReg;
  }

  // If we couldn't allocate a register from spilling, there is probably some
  // invalid inline assembly. The base class will report it.
  if (Stage >= RS_Done || !VirtReg.isSpillable()) {
    return tryLastChanceRecoloring(VirtReg, Order, NewVRegs, FixedRegisters,
                                   RecolorStack, Depth);
  }

  // Finally spill VirtReg itself.
  if ((EnableDeferredSpilling ||
       TRI->shouldUseDeferredSpillingForVirtReg(*MF, VirtReg)) &&
      ExtraInfo->getStage(VirtReg) < RS_Memory) {
    // TODO: This is experimental and in particular, we do not model
    // the live range splitting done by spilling correctly.
    // We would need a deep integration with the spiller to do the
    // right thing here. Anyway, that is still good for early testing.
    ExtraInfo->setStage(VirtReg, RS_Memory);
    LLVM_DEBUG(dbgs() << "Do as if this register is in memory\n");
    NewVRegs.push_back(VirtReg.reg());
  } else {
    NamedRegionTimer T("spill", "Spiller", TimerGroupName,
                       TimerGroupDescription, TimePassesIsEnabled);
    LiveRangeEdit LRE(&VirtReg, NewVRegs, *MF, *LIS, VRM, this, &DeadRemats);
    spiller().spill(LRE);
    ExtraInfo->setStage(NewVRegs.begin(), NewVRegs.end(), RS_Done);

    // Tell LiveDebugVariables about the new ranges. Ranges not being covered by
    // the new regs are kept in LDV (still mapping to the old register), until
    // we rewrite spilled locations in LDV at a later stage.
    DebugVars->splitRegister(VirtReg.reg(), LRE.regs(), *LIS);

    if (VerifyEnabled)
      MF->verify(this, "After spilling");
  }

  // The live virtual register requesting allocation was spilled, so tell
  // the caller not to allocate anything during this round.
  return 0;
}

void RAGreedy::RAGreedyStats::report(MachineOptimizationRemarkMissed &R) {
  using namespace ore;
  if (Spills) {
    R << NV("NumSpills", Spills) << " spills ";
    R << NV("TotalSpillsCost", SpillsCost) << " total spills cost ";
  }
  if (FoldedSpills) {
    R << NV("NumFoldedSpills", FoldedSpills) << " folded spills ";
    R << NV("TotalFoldedSpillsCost", FoldedSpillsCost)
      << " total folded spills cost ";
  }
  if (Reloads) {
    R << NV("NumReloads", Reloads) << " reloads ";
    R << NV("TotalReloadsCost", ReloadsCost) << " total reloads cost ";
  }
  if (FoldedReloads) {
    R << NV("NumFoldedReloads", FoldedReloads) << " folded reloads ";
    R << NV("TotalFoldedReloadsCost", FoldedReloadsCost)
      << " total folded reloads cost ";
  }
  if (ZeroCostFoldedReloads)
    R << NV("NumZeroCostFoldedReloads", ZeroCostFoldedReloads)
      << " zero cost folded reloads ";
  if (Copies) {
    R << NV("NumVRCopies", Copies) << " virtual registers copies ";
    R << NV("TotalCopiesCost", CopiesCost) << " total copies cost ";
  }
}

RAGreedy::RAGreedyStats RAGreedy::computeStats(MachineBasicBlock &MBB) {
  RAGreedyStats Stats;
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  int FI;

  auto isSpillSlotAccess = [&MFI](const MachineMemOperand *A) {
    return MFI.isSpillSlotObjectIndex(cast<FixedStackPseudoSourceValue>(
        A->getPseudoValue())->getFrameIndex());
  };
  auto isPatchpointInstr = [](const MachineInstr &MI) {
    return MI.getOpcode() == TargetOpcode::PATCHPOINT ||
           MI.getOpcode() == TargetOpcode::STACKMAP ||
           MI.getOpcode() == TargetOpcode::STATEPOINT;
  };
  for (MachineInstr &MI : MBB) {
    auto DestSrc = TII->isCopyInstr(MI);
    if (DestSrc) {
      const MachineOperand &Dest = *DestSrc->Destination;
      const MachineOperand &Src = *DestSrc->Source;
      Register SrcReg = Src.getReg();
      Register DestReg = Dest.getReg();
      // Only count `COPY`s with a virtual register as source or destination.
      if (SrcReg.isVirtual() || DestReg.isVirtual()) {
        if (SrcReg.isVirtual()) {
          SrcReg = VRM->getPhys(SrcReg);
          if (SrcReg && Src.getSubReg())
            SrcReg = TRI->getSubReg(SrcReg, Src.getSubReg());
        }
        if (DestReg.isVirtual()) {
          DestReg = VRM->getPhys(DestReg);
          if (DestReg && Dest.getSubReg())
            DestReg = TRI->getSubReg(DestReg, Dest.getSubReg());
        }
        if (SrcReg != DestReg)
          ++Stats.Copies;
      }
      continue;
    }

    SmallVector<const MachineMemOperand *, 2> Accesses;
    if (TII->isLoadFromStackSlot(MI, FI) && MFI.isSpillSlotObjectIndex(FI)) {
      ++Stats.Reloads;
      continue;
    }
    if (TII->isStoreToStackSlot(MI, FI) && MFI.isSpillSlotObjectIndex(FI)) {
      ++Stats.Spills;
      continue;
    }
    if (TII->hasLoadFromStackSlot(MI, Accesses) &&
        llvm::any_of(Accesses, isSpillSlotAccess)) {
      if (!isPatchpointInstr(MI)) {
        Stats.FoldedReloads += Accesses.size();
        continue;
      }
      // For statepoint there may be folded and zero cost folded stack reloads.
      std::pair<unsigned, unsigned> NonZeroCostRange =
          TII->getPatchpointUnfoldableRange(MI);
      SmallSet<unsigned, 16> FoldedReloads;
      SmallSet<unsigned, 16> ZeroCostFoldedReloads;
      for (unsigned Idx = 0, E = MI.getNumOperands(); Idx < E; ++Idx) {
        MachineOperand &MO = MI.getOperand(Idx);
        if (!MO.isFI() || !MFI.isSpillSlotObjectIndex(MO.getIndex()))
          continue;
        if (Idx >= NonZeroCostRange.first && Idx < NonZeroCostRange.second)
          FoldedReloads.insert(MO.getIndex());
        else
          ZeroCostFoldedReloads.insert(MO.getIndex());
      }
      // If stack slot is used in folded reload it is not zero cost then.
      for (unsigned Slot : FoldedReloads)
        ZeroCostFoldedReloads.erase(Slot);
      Stats.FoldedReloads += FoldedReloads.size();
      Stats.ZeroCostFoldedReloads += ZeroCostFoldedReloads.size();
      continue;
    }
    Accesses.clear();
    if (TII->hasStoreToStackSlot(MI, Accesses) &&
        llvm::any_of(Accesses, isSpillSlotAccess)) {
      Stats.FoldedSpills += Accesses.size();
    }
  }
  // Set cost of collected statistic by multiplication to relative frequency of
  // this basic block.
  float RelFreq = MBFI->getBlockFreqRelativeToEntryBlock(&MBB);
  Stats.ReloadsCost = RelFreq * Stats.Reloads;
  Stats.FoldedReloadsCost = RelFreq * Stats.FoldedReloads;
  Stats.SpillsCost = RelFreq * Stats.Spills;
  Stats.FoldedSpillsCost = RelFreq * Stats.FoldedSpills;
  Stats.CopiesCost = RelFreq * Stats.Copies;
  return Stats;
}

RAGreedy::RAGreedyStats RAGreedy::reportStats(MachineLoop *L) {
  RAGreedyStats Stats;

  // Sum up the spill and reloads in subloops.
  for (MachineLoop *SubLoop : *L)
    Stats.add(reportStats(SubLoop));

  for (MachineBasicBlock *MBB : L->getBlocks())
    // Handle blocks that were not included in subloops.
    if (Loops->getLoopFor(MBB) == L)
      Stats.add(computeStats(*MBB));

  if (!Stats.isEmpty()) {
    using namespace ore;

    ORE->emit([&]() {
      MachineOptimizationRemarkMissed R(DEBUG_TYPE, "LoopSpillReloadCopies",
                                        L->getStartLoc(), L->getHeader());
      Stats.report(R);
      R << "generated in loop";
      return R;
    });
  }
  return Stats;
}

void RAGreedy::reportStats() {
  if (!ORE->allowExtraAnalysis(DEBUG_TYPE))
    return;
  RAGreedyStats Stats;
  for (MachineLoop *L : *Loops)
    Stats.add(reportStats(L));
  // Process non-loop blocks.
  for (MachineBasicBlock &MBB : *MF)
    if (!Loops->getLoopFor(&MBB))
      Stats.add(computeStats(MBB));
  if (!Stats.isEmpty()) {
    using namespace ore;

    ORE->emit([&]() {
      DebugLoc Loc;
      if (auto *SP = MF->getFunction().getSubprogram())
        Loc = DILocation::get(SP->getContext(), SP->getLine(), 1, SP);
      MachineOptimizationRemarkMissed R(DEBUG_TYPE, "SpillReloadCopies", Loc,
                                        &MF->front());
      Stats.report(R);
      R << "generated in function";
      return R;
    });
  }
}

bool RAGreedy::hasVirtRegAlloc() {
  for (unsigned I = 0, E = MRI->getNumVirtRegs(); I != E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    if (MRI->reg_nodbg_empty(Reg))
      continue;
    const TargetRegisterClass *RC = MRI->getRegClass(Reg);
    if (!RC)
      continue;
    if (shouldAllocateRegister(Reg))
      return true;
  }

  return false;
}

bool RAGreedy::runOnMachineFunction(MachineFunction &mf) {
  LLVM_DEBUG(dbgs() << "********** GREEDY REGISTER ALLOCATION **********\n"
                    << "********** Function: " << mf.getName() << '\n');

  MF = &mf;
  TII = MF->getSubtarget().getInstrInfo();

  if (VerifyEnabled)
    MF->verify(this, "Before greedy register allocator");

  RegAllocBase::init(getAnalysis<VirtRegMap>(),
                     getAnalysis<LiveIntervalsWrapperPass>().getLIS(),
                     getAnalysis<LiveRegMatrix>());

  // Early return if there is no virtual register to be allocated to a
  // physical register.
  if (!hasVirtRegAlloc())
    return false;

  Indexes = &getAnalysis<SlotIndexesWrapperPass>().getSI();
  // Renumber to get accurate and consistent results from
  // SlotIndexes::getApproxInstrDistance.
  Indexes->packIndexes();
  MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
  DomTree = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  ORE = &getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE();
  Loops = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  Bundles = &getAnalysis<EdgeBundles>();
  SpillPlacer = &getAnalysis<SpillPlacement>();
  DebugVars = &getAnalysis<LiveDebugVariables>();

  initializeCSRCost();

  RegCosts = TRI->getRegisterCosts(*MF);
  RegClassPriorityTrumpsGlobalness =
      GreedyRegClassPriorityTrumpsGlobalness.getNumOccurrences()
          ? GreedyRegClassPriorityTrumpsGlobalness
          : TRI->regClassPriorityTrumpsGlobalness(*MF);

  ReverseLocalAssignment = GreedyReverseLocalAssignment.getNumOccurrences()
                               ? GreedyReverseLocalAssignment
                               : TRI->reverseLocalAssignment();

  ExtraInfo.emplace();
  EvictAdvisor =
      getAnalysis<RegAllocEvictionAdvisorAnalysis>().getAdvisor(*MF, *this);
  PriorityAdvisor =
      getAnalysis<RegAllocPriorityAdvisorAnalysis>().getAdvisor(*MF, *this);

  VRAI = std::make_unique<VirtRegAuxInfo>(*MF, *LIS, *VRM, *Loops, *MBFI);
  SpillerInstance.reset(createInlineSpiller(*this, *MF, *VRM, *VRAI));

  VRAI->calculateSpillWeightsAndHints();

  LLVM_DEBUG(LIS->dump());

  SA.reset(new SplitAnalysis(*VRM, *LIS, *Loops));
  SE.reset(new SplitEditor(*SA, *LIS, *VRM, *DomTree, *MBFI, *VRAI));

  IntfCache.init(MF, Matrix->getLiveUnions(), Indexes, LIS, TRI);
  GlobalCand.resize(32);  // This will grow as needed.
  SetOfBrokenHints.clear();

  allocatePhysRegs();
  tryHintsRecoloring();

  if (VerifyEnabled)
    MF->verify(this, "Before post optimization");
  postOptimization();
  reportStats();

  releaseMemory();
  return true;
}
