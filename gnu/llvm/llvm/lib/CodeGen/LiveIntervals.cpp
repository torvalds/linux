//===- LiveIntervals.cpp - Live Interval Analysis -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file implements the LiveInterval analysis pass which is used
/// by the Linear Scan Register allocator. This pass linearizes the
/// basic blocks of the function in DFS order and computes live intervals for
/// each virtual and physical register.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervalCalc.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

AnalysisKey LiveIntervalsAnalysis::Key;

LiveIntervalsAnalysis::Result
LiveIntervalsAnalysis::run(MachineFunction &MF,
                           MachineFunctionAnalysisManager &MFAM) {
  return Result(MF, MFAM.getResult<SlotIndexesAnalysis>(MF),
                MFAM.getResult<MachineDominatorTreeAnalysis>(MF));
}

PreservedAnalyses
LiveIntervalsPrinterPass::run(MachineFunction &MF,
                              MachineFunctionAnalysisManager &MFAM) {
  OS << "Live intervals for machine function: " << MF.getName() << ":\n";
  MFAM.getResult<LiveIntervalsAnalysis>(MF).print(OS);
  return PreservedAnalyses::all();
}

char LiveIntervalsWrapperPass::ID = 0;
char &llvm::LiveIntervalsID = LiveIntervalsWrapperPass::ID;
INITIALIZE_PASS_BEGIN(LiveIntervalsWrapperPass, "liveintervals",
                      "Live Interval Analysis", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_END(LiveIntervalsWrapperPass, "liveintervals",
                    "Live Interval Analysis", false, false)

bool LiveIntervalsWrapperPass::runOnMachineFunction(MachineFunction &MF) {
  LIS.Indexes = &getAnalysis<SlotIndexesWrapperPass>().getSI();
  LIS.DomTree = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  LIS.analyze(MF);
  LLVM_DEBUG(dump());
  return false;
}

#ifndef NDEBUG
static cl::opt<bool> EnablePrecomputePhysRegs(
  "precompute-phys-liveness", cl::Hidden,
  cl::desc("Eagerly compute live intervals for all physreg units."));
#else
static bool EnablePrecomputePhysRegs = false;
#endif // NDEBUG

namespace llvm {

cl::opt<bool> UseSegmentSetForPhysRegs(
    "use-segment-set-for-physregs", cl::Hidden, cl::init(true),
    cl::desc(
        "Use segment set for the computation of the live ranges of physregs."));

} // end namespace llvm

void LiveIntervalsWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addPreserved<LiveVariablesWrapperPass>();
  AU.addPreservedID(MachineLoopInfoID);
  AU.addRequiredTransitiveID(MachineDominatorsID);
  AU.addPreservedID(MachineDominatorsID);
  AU.addPreserved<SlotIndexesWrapperPass>();
  AU.addRequiredTransitive<SlotIndexesWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

LiveIntervalsWrapperPass::LiveIntervalsWrapperPass() : MachineFunctionPass(ID) {
  initializeLiveIntervalsWrapperPassPass(*PassRegistry::getPassRegistry());
}

LiveIntervals::~LiveIntervals() { clear(); }

void LiveIntervals::clear() {
  // Free the live intervals themselves.
  for (unsigned i = 0, e = VirtRegIntervals.size(); i != e; ++i)
    delete VirtRegIntervals[Register::index2VirtReg(i)];
  VirtRegIntervals.clear();
  RegMaskSlots.clear();
  RegMaskBits.clear();
  RegMaskBlocks.clear();

  for (LiveRange *LR : RegUnitRanges)
    delete LR;
  RegUnitRanges.clear();

  // Release VNInfo memory regions, VNInfo objects don't need to be dtor'd.
  VNInfoAllocator.Reset();
}

void LiveIntervals::analyze(MachineFunction &fn) {
  MF = &fn;
  MRI = &MF->getRegInfo();
  TRI = MF->getSubtarget().getRegisterInfo();
  TII = MF->getSubtarget().getInstrInfo();

  if (!LICalc)
    LICalc = std::make_unique<LiveIntervalCalc>();

  // Allocate space for all virtual registers.
  VirtRegIntervals.resize(MRI->getNumVirtRegs());

  computeVirtRegs();
  computeRegMasks();
  computeLiveInRegUnits();

  if (EnablePrecomputePhysRegs) {
    // For stress testing, precompute live ranges of all physical register
    // units, including reserved registers.
    for (unsigned i = 0, e = TRI->getNumRegUnits(); i != e; ++i)
      getRegUnit(i);
  }
}

void LiveIntervals::print(raw_ostream &OS) const {
  OS << "********** INTERVALS **********\n";

  // Dump the regunits.
  for (unsigned Unit = 0, UnitE = RegUnitRanges.size(); Unit != UnitE; ++Unit)
    if (LiveRange *LR = RegUnitRanges[Unit])
      OS << printRegUnit(Unit, TRI) << ' ' << *LR << '\n';

  // Dump the virtregs.
  for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
    Register Reg = Register::index2VirtReg(i);
    if (hasInterval(Reg))
      OS << getInterval(Reg) << '\n';
  }

  OS << "RegMasks:";
  for (SlotIndex Idx : RegMaskSlots)
    OS << ' ' << Idx;
  OS << '\n';

  printInstrs(OS);
}

void LiveIntervals::printInstrs(raw_ostream &OS) const {
  OS << "********** MACHINEINSTRS **********\n";
  MF->print(OS, Indexes);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LiveIntervals::dumpInstrs() const {
  printInstrs(dbgs());
}
#endif

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LiveIntervals::dump() const { print(dbgs()); }
#endif

LiveInterval *LiveIntervals::createInterval(Register reg) {
  float Weight = reg.isPhysical() ? huge_valf : 0.0F;
  return new LiveInterval(reg, Weight);
}

/// Compute the live interval of a virtual register, based on defs and uses.
bool LiveIntervals::computeVirtRegInterval(LiveInterval &LI) {
  assert(LICalc && "LICalc not initialized.");
  assert(LI.empty() && "Should only compute empty intervals.");
  LICalc->reset(MF, getSlotIndexes(), DomTree, &getVNInfoAllocator());
  LICalc->calculate(LI, MRI->shouldTrackSubRegLiveness(LI.reg()));
  return computeDeadValues(LI, nullptr);
}

void LiveIntervals::computeVirtRegs() {
  for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
    Register Reg = Register::index2VirtReg(i);
    if (MRI->reg_nodbg_empty(Reg))
      continue;
    LiveInterval &LI = createEmptyInterval(Reg);
    bool NeedSplit = computeVirtRegInterval(LI);
    if (NeedSplit) {
      SmallVector<LiveInterval*, 8> SplitLIs;
      splitSeparateComponents(LI, SplitLIs);
    }
  }
}

void LiveIntervals::computeRegMasks() {
  RegMaskBlocks.resize(MF->getNumBlockIDs());

  // Find all instructions with regmask operands.
  for (const MachineBasicBlock &MBB : *MF) {
    std::pair<unsigned, unsigned> &RMB = RegMaskBlocks[MBB.getNumber()];
    RMB.first = RegMaskSlots.size();

    // Some block starts, such as EH funclets, create masks.
    if (const uint32_t *Mask = MBB.getBeginClobberMask(TRI)) {
      RegMaskSlots.push_back(Indexes->getMBBStartIdx(&MBB));
      RegMaskBits.push_back(Mask);
    }

    // Unwinders may clobber additional registers.
    // FIXME: This functionality can possibly be merged into
    // MachineBasicBlock::getBeginClobberMask().
    if (MBB.isEHPad())
      if (auto *Mask = TRI->getCustomEHPadPreservedMask(*MBB.getParent())) {
        RegMaskSlots.push_back(Indexes->getMBBStartIdx(&MBB));
        RegMaskBits.push_back(Mask);
      }

    for (const MachineInstr &MI : MBB) {
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isRegMask())
          continue;
        RegMaskSlots.push_back(Indexes->getInstructionIndex(MI).getRegSlot());
        RegMaskBits.push_back(MO.getRegMask());
      }
    }

    // Some block ends, such as funclet returns, create masks. Put the mask on
    // the last instruction of the block, because MBB slot index intervals are
    // half-open.
    if (const uint32_t *Mask = MBB.getEndClobberMask(TRI)) {
      assert(!MBB.empty() && "empty return block?");
      RegMaskSlots.push_back(
          Indexes->getInstructionIndex(MBB.back()).getRegSlot());
      RegMaskBits.push_back(Mask);
    }

    // Compute the number of register mask instructions in this block.
    RMB.second = RegMaskSlots.size() - RMB.first;
  }
}

//===----------------------------------------------------------------------===//
//                           Register Unit Liveness
//===----------------------------------------------------------------------===//
//
// Fixed interference typically comes from ABI boundaries: Function arguments
// and return values are passed in fixed registers, and so are exception
// pointers entering landing pads. Certain instructions require values to be
// present in specific registers. That is also represented through fixed
// interference.
//

/// Compute the live range of a register unit, based on the uses and defs of
/// aliasing registers.  The range should be empty, or contain only dead
/// phi-defs from ABI blocks.
void LiveIntervals::computeRegUnitRange(LiveRange &LR, unsigned Unit) {
  assert(LICalc && "LICalc not initialized.");
  LICalc->reset(MF, getSlotIndexes(), DomTree, &getVNInfoAllocator());

  // The physregs aliasing Unit are the roots and their super-registers.
  // Create all values as dead defs before extending to uses. Note that roots
  // may share super-registers. That's OK because createDeadDefs() is
  // idempotent. It is very rare for a register unit to have multiple roots, so
  // uniquing super-registers is probably not worthwhile.
  bool IsReserved = false;
  for (MCRegUnitRootIterator Root(Unit, TRI); Root.isValid(); ++Root) {
    bool IsRootReserved = true;
    for (MCPhysReg Reg : TRI->superregs_inclusive(*Root)) {
      if (!MRI->reg_empty(Reg))
        LICalc->createDeadDefs(LR, Reg);
      // A register unit is considered reserved if all its roots and all their
      // super registers are reserved.
      if (!MRI->isReserved(Reg))
        IsRootReserved = false;
    }
    IsReserved |= IsRootReserved;
  }
  assert(IsReserved == MRI->isReservedRegUnit(Unit) &&
         "reserved computation mismatch");

  // Now extend LR to reach all uses.
  // Ignore uses of reserved registers. We only track defs of those.
  if (!IsReserved) {
    for (MCRegUnitRootIterator Root(Unit, TRI); Root.isValid(); ++Root) {
      for (MCPhysReg Reg : TRI->superregs_inclusive(*Root)) {
        if (!MRI->reg_empty(Reg))
          LICalc->extendToUses(LR, Reg);
      }
    }
  }

  // Flush the segment set to the segment vector.
  if (UseSegmentSetForPhysRegs)
    LR.flushSegmentSet();
}

/// Precompute the live ranges of any register units that are live-in to an ABI
/// block somewhere. Register values can appear without a corresponding def when
/// entering the entry block or a landing pad.
void LiveIntervals::computeLiveInRegUnits() {
  RegUnitRanges.resize(TRI->getNumRegUnits());
  LLVM_DEBUG(dbgs() << "Computing live-in reg-units in ABI blocks.\n");

  // Keep track of the live range sets allocated.
  SmallVector<unsigned, 8> NewRanges;

  // Check all basic blocks for live-ins.
  for (const MachineBasicBlock &MBB : *MF) {
    // We only care about ABI blocks: Entry + landing pads.
    if ((&MBB != &MF->front() && !MBB.isEHPad()) || MBB.livein_empty())
      continue;

    // Create phi-defs at Begin for all live-in registers.
    SlotIndex Begin = Indexes->getMBBStartIdx(&MBB);
    LLVM_DEBUG(dbgs() << Begin << "\t" << printMBBReference(MBB));
    for (const auto &LI : MBB.liveins()) {
      for (MCRegUnit Unit : TRI->regunits(LI.PhysReg)) {
        LiveRange *LR = RegUnitRanges[Unit];
        if (!LR) {
          // Use segment set to speed-up initial computation of the live range.
          LR = RegUnitRanges[Unit] = new LiveRange(UseSegmentSetForPhysRegs);
          NewRanges.push_back(Unit);
        }
        VNInfo *VNI = LR->createDeadDef(Begin, getVNInfoAllocator());
        (void)VNI;
        LLVM_DEBUG(dbgs() << ' ' << printRegUnit(Unit, TRI) << '#' << VNI->id);
      }
    }
    LLVM_DEBUG(dbgs() << '\n');
  }
  LLVM_DEBUG(dbgs() << "Created " << NewRanges.size() << " new intervals.\n");

  // Compute the 'normal' part of the ranges.
  for (unsigned Unit : NewRanges)
    computeRegUnitRange(*RegUnitRanges[Unit], Unit);
}

static void createSegmentsForValues(LiveRange &LR,
    iterator_range<LiveInterval::vni_iterator> VNIs) {
  for (VNInfo *VNI : VNIs) {
    if (VNI->isUnused())
      continue;
    SlotIndex Def = VNI->def;
    LR.addSegment(LiveRange::Segment(Def, Def.getDeadSlot(), VNI));
  }
}

void LiveIntervals::extendSegmentsToUses(LiveRange &Segments,
                                         ShrinkToUsesWorkList &WorkList,
                                         Register Reg, LaneBitmask LaneMask) {
  // Keep track of the PHIs that are in use.
  SmallPtrSet<VNInfo*, 8> UsedPHIs;
  // Blocks that have already been added to WorkList as live-out.
  SmallPtrSet<const MachineBasicBlock*, 16> LiveOut;

  auto getSubRange = [](const LiveInterval &I, LaneBitmask M)
        -> const LiveRange& {
    if (M.none())
      return I;
    for (const LiveInterval::SubRange &SR : I.subranges()) {
      if ((SR.LaneMask & M).any()) {
        assert(SR.LaneMask == M && "Expecting lane masks to match exactly");
        return SR;
      }
    }
    llvm_unreachable("Subrange for mask not found");
  };

  const LiveInterval &LI = getInterval(Reg);
  const LiveRange &OldRange = getSubRange(LI, LaneMask);

  // Extend intervals to reach all uses in WorkList.
  while (!WorkList.empty()) {
    SlotIndex Idx = WorkList.back().first;
    VNInfo *VNI = WorkList.back().second;
    WorkList.pop_back();
    const MachineBasicBlock *MBB = Indexes->getMBBFromIndex(Idx.getPrevSlot());
    SlotIndex BlockStart = Indexes->getMBBStartIdx(MBB);

    // Extend the live range for VNI to be live at Idx.
    if (VNInfo *ExtVNI = Segments.extendInBlock(BlockStart, Idx)) {
      assert(ExtVNI == VNI && "Unexpected existing value number");
      (void)ExtVNI;
      // Is this a PHIDef we haven't seen before?
      if (!VNI->isPHIDef() || VNI->def != BlockStart ||
          !UsedPHIs.insert(VNI).second)
        continue;
      // The PHI is live, make sure the predecessors are live-out.
      for (const MachineBasicBlock *Pred : MBB->predecessors()) {
        if (!LiveOut.insert(Pred).second)
          continue;
        SlotIndex Stop = Indexes->getMBBEndIdx(Pred);
        // A predecessor is not required to have a live-out value for a PHI.
        if (VNInfo *PVNI = OldRange.getVNInfoBefore(Stop))
          WorkList.push_back(std::make_pair(Stop, PVNI));
      }
      continue;
    }

    // VNI is live-in to MBB.
    LLVM_DEBUG(dbgs() << " live-in at " << BlockStart << '\n');
    Segments.addSegment(LiveRange::Segment(BlockStart, Idx, VNI));

    // Make sure VNI is live-out from the predecessors.
    for (const MachineBasicBlock *Pred : MBB->predecessors()) {
      if (!LiveOut.insert(Pred).second)
        continue;
      SlotIndex Stop = Indexes->getMBBEndIdx(Pred);
      if (VNInfo *OldVNI = OldRange.getVNInfoBefore(Stop)) {
        assert(OldVNI == VNI && "Wrong value out of predecessor");
        (void)OldVNI;
        WorkList.push_back(std::make_pair(Stop, VNI));
      } else {
#ifndef NDEBUG
        // There was no old VNI. Verify that Stop is jointly dominated
        // by <undef>s for this live range.
        assert(LaneMask.any() &&
               "Missing value out of predecessor for main range");
        SmallVector<SlotIndex,8> Undefs;
        LI.computeSubRangeUndefs(Undefs, LaneMask, *MRI, *Indexes);
        assert(LiveRangeCalc::isJointlyDominated(Pred, Undefs, *Indexes) &&
               "Missing value out of predecessor for subrange");
#endif
      }
    }
  }
}

bool LiveIntervals::shrinkToUses(LiveInterval *li,
                                 SmallVectorImpl<MachineInstr*> *dead) {
  LLVM_DEBUG(dbgs() << "Shrink: " << *li << '\n');
  assert(li->reg().isVirtual() && "Can only shrink virtual registers");

  // Shrink subregister live ranges.
  bool NeedsCleanup = false;
  for (LiveInterval::SubRange &S : li->subranges()) {
    shrinkToUses(S, li->reg());
    if (S.empty())
      NeedsCleanup = true;
  }
  if (NeedsCleanup)
    li->removeEmptySubRanges();

  // Find all the values used, including PHI kills.
  ShrinkToUsesWorkList WorkList;

  // Visit all instructions reading li->reg().
  Register Reg = li->reg();
  for (MachineInstr &UseMI : MRI->reg_instructions(Reg)) {
    if (UseMI.isDebugInstr() || !UseMI.readsVirtualRegister(Reg))
      continue;
    SlotIndex Idx = getInstructionIndex(UseMI).getRegSlot();
    LiveQueryResult LRQ = li->Query(Idx);
    VNInfo *VNI = LRQ.valueIn();
    if (!VNI) {
      // This shouldn't happen: readsVirtualRegister returns true, but there is
      // no live value. It is likely caused by a target getting <undef> flags
      // wrong.
      LLVM_DEBUG(
          dbgs() << Idx << '\t' << UseMI
                 << "Warning: Instr claims to read non-existent value in "
                 << *li << '\n');
      continue;
    }
    // Special case: An early-clobber tied operand reads and writes the
    // register one slot early.
    if (VNInfo *DefVNI = LRQ.valueDefined())
      Idx = DefVNI->def;

    WorkList.push_back(std::make_pair(Idx, VNI));
  }

  // Create new live ranges with only minimal live segments per def.
  LiveRange NewLR;
  createSegmentsForValues(NewLR, li->vnis());
  extendSegmentsToUses(NewLR, WorkList, Reg, LaneBitmask::getNone());

  // Move the trimmed segments back.
  li->segments.swap(NewLR.segments);

  // Handle dead values.
  bool CanSeparate = computeDeadValues(*li, dead);
  LLVM_DEBUG(dbgs() << "Shrunk: " << *li << '\n');
  return CanSeparate;
}

bool LiveIntervals::computeDeadValues(LiveInterval &LI,
                                      SmallVectorImpl<MachineInstr*> *dead) {
  bool MayHaveSplitComponents = false;

  for (VNInfo *VNI : LI.valnos) {
    if (VNI->isUnused())
      continue;
    SlotIndex Def = VNI->def;
    LiveRange::iterator I = LI.FindSegmentContaining(Def);
    assert(I != LI.end() && "Missing segment for VNI");

    // Is the register live before? Otherwise we may have to add a read-undef
    // flag for subregister defs.
    Register VReg = LI.reg();
    if (MRI->shouldTrackSubRegLiveness(VReg)) {
      if ((I == LI.begin() || std::prev(I)->end < Def) && !VNI->isPHIDef()) {
        MachineInstr *MI = getInstructionFromIndex(Def);
        MI->setRegisterDefReadUndef(VReg);
      }
    }

    if (I->end != Def.getDeadSlot())
      continue;
    if (VNI->isPHIDef()) {
      // This is a dead PHI. Remove it.
      VNI->markUnused();
      LI.removeSegment(I);
      LLVM_DEBUG(dbgs() << "Dead PHI at " << Def << " may separate interval\n");
    } else {
      // This is a dead def. Make sure the instruction knows.
      MachineInstr *MI = getInstructionFromIndex(Def);
      assert(MI && "No instruction defining live value");
      MI->addRegisterDead(LI.reg(), TRI);

      if (dead && MI->allDefsAreDead()) {
        LLVM_DEBUG(dbgs() << "All defs dead: " << Def << '\t' << *MI);
        dead->push_back(MI);
      }
    }
    MayHaveSplitComponents = true;
  }
  return MayHaveSplitComponents;
}

void LiveIntervals::shrinkToUses(LiveInterval::SubRange &SR, Register Reg) {
  LLVM_DEBUG(dbgs() << "Shrink: " << SR << '\n');
  assert(Reg.isVirtual() && "Can only shrink virtual registers");
  // Find all the values used, including PHI kills.
  ShrinkToUsesWorkList WorkList;

  // Visit all instructions reading Reg.
  SlotIndex LastIdx;
  for (MachineOperand &MO : MRI->use_nodbg_operands(Reg)) {
    // Skip "undef" uses.
    if (!MO.readsReg())
      continue;
    // Maybe the operand is for a subregister we don't care about.
    unsigned SubReg = MO.getSubReg();
    if (SubReg != 0) {
      LaneBitmask LaneMask = TRI->getSubRegIndexLaneMask(SubReg);
      if ((LaneMask & SR.LaneMask).none())
        continue;
    }
    // We only need to visit each instruction once.
    MachineInstr *UseMI = MO.getParent();
    SlotIndex Idx = getInstructionIndex(*UseMI).getRegSlot();
    if (Idx == LastIdx)
      continue;
    LastIdx = Idx;

    LiveQueryResult LRQ = SR.Query(Idx);
    VNInfo *VNI = LRQ.valueIn();
    // For Subranges it is possible that only undef values are left in that
    // part of the subregister, so there is no real liverange at the use
    if (!VNI)
      continue;

    // Special case: An early-clobber tied operand reads and writes the
    // register one slot early.
    if (VNInfo *DefVNI = LRQ.valueDefined())
      Idx = DefVNI->def;

    WorkList.push_back(std::make_pair(Idx, VNI));
  }

  // Create a new live ranges with only minimal live segments per def.
  LiveRange NewLR;
  createSegmentsForValues(NewLR, SR.vnis());
  extendSegmentsToUses(NewLR, WorkList, Reg, SR.LaneMask);

  // Move the trimmed ranges back.
  SR.segments.swap(NewLR.segments);

  // Remove dead PHI value numbers
  for (VNInfo *VNI : SR.valnos) {
    if (VNI->isUnused())
      continue;
    const LiveRange::Segment *Segment = SR.getSegmentContaining(VNI->def);
    assert(Segment != nullptr && "Missing segment for VNI");
    if (Segment->end != VNI->def.getDeadSlot())
      continue;
    if (VNI->isPHIDef()) {
      // This is a dead PHI. Remove it.
      LLVM_DEBUG(dbgs() << "Dead PHI at " << VNI->def
                        << " may separate interval\n");
      VNI->markUnused();
      SR.removeSegment(*Segment);
    }
  }

  LLVM_DEBUG(dbgs() << "Shrunk: " << SR << '\n');
}

void LiveIntervals::extendToIndices(LiveRange &LR,
                                    ArrayRef<SlotIndex> Indices,
                                    ArrayRef<SlotIndex> Undefs) {
  assert(LICalc && "LICalc not initialized.");
  LICalc->reset(MF, getSlotIndexes(), DomTree, &getVNInfoAllocator());
  for (SlotIndex Idx : Indices)
    LICalc->extend(LR, Idx, /*PhysReg=*/0, Undefs);
}

void LiveIntervals::pruneValue(LiveRange &LR, SlotIndex Kill,
                               SmallVectorImpl<SlotIndex> *EndPoints) {
  LiveQueryResult LRQ = LR.Query(Kill);
  VNInfo *VNI = LRQ.valueOutOrDead();
  if (!VNI)
    return;

  MachineBasicBlock *KillMBB = Indexes->getMBBFromIndex(Kill);
  SlotIndex MBBEnd = Indexes->getMBBEndIdx(KillMBB);

  // If VNI isn't live out from KillMBB, the value is trivially pruned.
  if (LRQ.endPoint() < MBBEnd) {
    LR.removeSegment(Kill, LRQ.endPoint());
    if (EndPoints) EndPoints->push_back(LRQ.endPoint());
    return;
  }

  // VNI is live out of KillMBB.
  LR.removeSegment(Kill, MBBEnd);
  if (EndPoints) EndPoints->push_back(MBBEnd);

  // Find all blocks that are reachable from KillMBB without leaving VNI's live
  // range. It is possible that KillMBB itself is reachable, so start a DFS
  // from each successor.
  using VisitedTy = df_iterator_default_set<MachineBasicBlock*,9>;
  VisitedTy Visited;
  for (MachineBasicBlock *Succ : KillMBB->successors()) {
    for (df_ext_iterator<MachineBasicBlock*, VisitedTy>
         I = df_ext_begin(Succ, Visited), E = df_ext_end(Succ, Visited);
         I != E;) {
      MachineBasicBlock *MBB = *I;

      // Check if VNI is live in to MBB.
      SlotIndex MBBStart, MBBEnd;
      std::tie(MBBStart, MBBEnd) = Indexes->getMBBRange(MBB);
      LiveQueryResult LRQ = LR.Query(MBBStart);
      if (LRQ.valueIn() != VNI) {
        // This block isn't part of the VNI segment. Prune the search.
        I.skipChildren();
        continue;
      }

      // Prune the search if VNI is killed in MBB.
      if (LRQ.endPoint() < MBBEnd) {
        LR.removeSegment(MBBStart, LRQ.endPoint());
        if (EndPoints) EndPoints->push_back(LRQ.endPoint());
        I.skipChildren();
        continue;
      }

      // VNI is live through MBB.
      LR.removeSegment(MBBStart, MBBEnd);
      if (EndPoints) EndPoints->push_back(MBBEnd);
      ++I;
    }
  }
}

//===----------------------------------------------------------------------===//
// Register allocator hooks.
//

void LiveIntervals::addKillFlags(const VirtRegMap *VRM) {
  // Keep track of regunit ranges.
  SmallVector<std::pair<const LiveRange*, LiveRange::const_iterator>, 8> RU;

  for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
    Register Reg = Register::index2VirtReg(i);
    if (MRI->reg_nodbg_empty(Reg))
      continue;
    const LiveInterval &LI = getInterval(Reg);
    if (LI.empty())
      continue;

    // Target may have not allocated this yet.
    Register PhysReg = VRM->getPhys(Reg);
    if (!PhysReg)
      continue;

    // Find the regunit intervals for the assigned register. They may overlap
    // the virtual register live range, cancelling any kills.
    RU.clear();
    for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
      const LiveRange &RURange = getRegUnit(Unit);
      if (RURange.empty())
        continue;
      RU.push_back(std::make_pair(&RURange, RURange.find(LI.begin()->end)));
    }
    // Every instruction that kills Reg corresponds to a segment range end
    // point.
    for (LiveInterval::const_iterator RI = LI.begin(), RE = LI.end(); RI != RE;
         ++RI) {
      // A block index indicates an MBB edge.
      if (RI->end.isBlock())
        continue;
      MachineInstr *MI = getInstructionFromIndex(RI->end);
      if (!MI)
        continue;

      // Check if any of the regunits are live beyond the end of RI. That could
      // happen when a physreg is defined as a copy of a virtreg:
      //
      //   %eax = COPY %5
      //   FOO %5             <--- MI, cancel kill because %eax is live.
      //   BAR killed %eax
      //
      // There should be no kill flag on FOO when %5 is rewritten as %eax.
      for (auto &RUP : RU) {
        const LiveRange &RURange = *RUP.first;
        LiveRange::const_iterator &I = RUP.second;
        if (I == RURange.end())
          continue;
        I = RURange.advanceTo(I, RI->end);
        if (I == RURange.end() || I->start >= RI->end)
          continue;
        // I is overlapping RI.
        goto CancelKill;
      }

      if (MRI->subRegLivenessEnabled()) {
        // When reading a partial undefined value we must not add a kill flag.
        // The regalloc might have used the undef lane for something else.
        // Example:
        //     %1 = ...                  ; R32: %1
        //     %2:high16 = ...           ; R64: %2
        //        = read killed %2        ; R64: %2
        //        = read %1              ; R32: %1
        // The <kill> flag is correct for %2, but the register allocator may
        // assign R0L to %1, and R0 to %2 because the low 32bits of R0
        // are actually never written by %2. After assignment the <kill>
        // flag at the read instruction is invalid.
        LaneBitmask DefinedLanesMask;
        if (LI.hasSubRanges()) {
          // Compute a mask of lanes that are defined.
          DefinedLanesMask = LaneBitmask::getNone();
          for (const LiveInterval::SubRange &SR : LI.subranges())
            for (const LiveRange::Segment &Segment : SR.segments) {
              if (Segment.start >= RI->end)
                break;
              if (Segment.end == RI->end) {
                DefinedLanesMask |= SR.LaneMask;
                break;
              }
            }
        } else
          DefinedLanesMask = LaneBitmask::getAll();

        bool IsFullWrite = false;
        for (const MachineOperand &MO : MI->operands()) {
          if (!MO.isReg() || MO.getReg() != Reg)
            continue;
          if (MO.isUse()) {
            // Reading any undefined lanes?
            unsigned SubReg = MO.getSubReg();
            LaneBitmask UseMask = SubReg ? TRI->getSubRegIndexLaneMask(SubReg)
                                         : MRI->getMaxLaneMaskForVReg(Reg);
            if ((UseMask & ~DefinedLanesMask).any())
              goto CancelKill;
          } else if (MO.getSubReg() == 0) {
            // Writing to the full register?
            assert(MO.isDef());
            IsFullWrite = true;
          }
        }

        // If an instruction writes to a subregister, a new segment starts in
        // the LiveInterval. But as this is only overriding part of the register
        // adding kill-flags is not correct here after registers have been
        // assigned.
        if (!IsFullWrite) {
          // Next segment has to be adjacent in the subregister write case.
          LiveRange::const_iterator N = std::next(RI);
          if (N != LI.end() && N->start == RI->end)
            goto CancelKill;
        }
      }

      MI->addRegisterKilled(Reg, nullptr);
      continue;
CancelKill:
      MI->clearRegisterKills(Reg, nullptr);
    }
  }
}

MachineBasicBlock*
LiveIntervals::intervalIsInOneMBB(const LiveInterval &LI) const {
  assert(!LI.empty() && "LiveInterval is empty.");

  // A local live range must be fully contained inside the block, meaning it is
  // defined and killed at instructions, not at block boundaries. It is not
  // live in or out of any block.
  //
  // It is technically possible to have a PHI-defined live range identical to a
  // single block, but we are going to return false in that case.

  SlotIndex Start = LI.beginIndex();
  if (Start.isBlock())
    return nullptr;

  SlotIndex Stop = LI.endIndex();
  if (Stop.isBlock())
    return nullptr;

  // getMBBFromIndex doesn't need to search the MBB table when both indexes
  // belong to proper instructions.
  MachineBasicBlock *MBB1 = Indexes->getMBBFromIndex(Start);
  MachineBasicBlock *MBB2 = Indexes->getMBBFromIndex(Stop);
  return MBB1 == MBB2 ? MBB1 : nullptr;
}

bool
LiveIntervals::hasPHIKill(const LiveInterval &LI, const VNInfo *VNI) const {
  for (const VNInfo *PHI : LI.valnos) {
    if (PHI->isUnused() || !PHI->isPHIDef())
      continue;
    const MachineBasicBlock *PHIMBB = getMBBFromIndex(PHI->def);
    // Conservatively return true instead of scanning huge predecessor lists.
    if (PHIMBB->pred_size() > 100)
      return true;
    for (const MachineBasicBlock *Pred : PHIMBB->predecessors())
      if (VNI == LI.getVNInfoBefore(Indexes->getMBBEndIdx(Pred)))
        return true;
  }
  return false;
}

float LiveIntervals::getSpillWeight(bool isDef, bool isUse,
                                    const MachineBlockFrequencyInfo *MBFI,
                                    const MachineInstr &MI) {
  return getSpillWeight(isDef, isUse, MBFI, MI.getParent());
}

float LiveIntervals::getSpillWeight(bool isDef, bool isUse,
                                    const MachineBlockFrequencyInfo *MBFI,
                                    const MachineBasicBlock *MBB) {
  return (isDef + isUse) * MBFI->getBlockFreqRelativeToEntryBlock(MBB);
}

LiveRange::Segment
LiveIntervals::addSegmentToEndOfBlock(Register Reg, MachineInstr &startInst) {
  LiveInterval &Interval = getOrCreateEmptyInterval(Reg);
  VNInfo *VN = Interval.getNextValue(
      SlotIndex(getInstructionIndex(startInst).getRegSlot()),
      getVNInfoAllocator());
  LiveRange::Segment S(SlotIndex(getInstructionIndex(startInst).getRegSlot()),
                       getMBBEndIdx(startInst.getParent()), VN);
  Interval.addSegment(S);

  return S;
}

//===----------------------------------------------------------------------===//
//                          Register mask functions
//===----------------------------------------------------------------------===//
/// Check whether use of reg in MI is live-through. Live-through means that
/// the value is alive on exit from Machine instruction. The example of such
/// use is a deopt value in statepoint instruction.
static bool hasLiveThroughUse(const MachineInstr *MI, Register Reg) {
  if (MI->getOpcode() != TargetOpcode::STATEPOINT)
    return false;
  StatepointOpers SO(MI);
  if (SO.getFlags() & (uint64_t)StatepointFlags::DeoptLiveIn)
    return false;
  for (unsigned Idx = SO.getNumDeoptArgsIdx(), E = SO.getNumGCPtrIdx(); Idx < E;
       ++Idx) {
    const MachineOperand &MO = MI->getOperand(Idx);
    if (MO.isReg() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

bool LiveIntervals::checkRegMaskInterference(const LiveInterval &LI,
                                             BitVector &UsableRegs) {
  if (LI.empty())
    return false;
  LiveInterval::const_iterator LiveI = LI.begin(), LiveE = LI.end();

  // Use a smaller arrays for local live ranges.
  ArrayRef<SlotIndex> Slots;
  ArrayRef<const uint32_t*> Bits;
  if (MachineBasicBlock *MBB = intervalIsInOneMBB(LI)) {
    Slots = getRegMaskSlotsInBlock(MBB->getNumber());
    Bits = getRegMaskBitsInBlock(MBB->getNumber());
  } else {
    Slots = getRegMaskSlots();
    Bits = getRegMaskBits();
  }

  // We are going to enumerate all the register mask slots contained in LI.
  // Start with a binary search of RegMaskSlots to find a starting point.
  ArrayRef<SlotIndex>::iterator SlotI = llvm::lower_bound(Slots, LiveI->start);
  ArrayRef<SlotIndex>::iterator SlotE = Slots.end();

  // No slots in range, LI begins after the last call.
  if (SlotI == SlotE)
    return false;

  bool Found = false;
  // Utility to union regmasks.
  auto unionBitMask = [&](unsigned Idx) {
      if (!Found) {
        // This is the first overlap. Initialize UsableRegs to all ones.
        UsableRegs.clear();
        UsableRegs.resize(TRI->getNumRegs(), true);
        Found = true;
      }
      // Remove usable registers clobbered by this mask.
      UsableRegs.clearBitsNotInMask(Bits[Idx]);
  };
  while (true) {
    assert(*SlotI >= LiveI->start);
    // Loop over all slots overlapping this segment.
    while (*SlotI < LiveI->end) {
      // *SlotI overlaps LI. Collect mask bits.
      unionBitMask(SlotI - Slots.begin());
      if (++SlotI == SlotE)
        return Found;
    }
    // If segment ends with live-through use we need to collect its regmask.
    if (*SlotI == LiveI->end)
      if (MachineInstr *MI = getInstructionFromIndex(*SlotI))
        if (hasLiveThroughUse(MI, LI.reg()))
          unionBitMask(SlotI++ - Slots.begin());
    // *SlotI is beyond the current LI segment.
    // Special advance implementation to not miss next LiveI->end.
    if (++LiveI == LiveE || SlotI == SlotE || *SlotI > LI.endIndex())
      return Found;
    while (LiveI->end < *SlotI)
      ++LiveI;
    // Advance SlotI until it overlaps.
    while (*SlotI < LiveI->start)
      if (++SlotI == SlotE)
        return Found;
  }
}

//===----------------------------------------------------------------------===//
//                         IntervalUpdate class.
//===----------------------------------------------------------------------===//

/// Toolkit used by handleMove to trim or extend live intervals.
class LiveIntervals::HMEditor {
private:
  LiveIntervals& LIS;
  const MachineRegisterInfo& MRI;
  const TargetRegisterInfo& TRI;
  SlotIndex OldIdx;
  SlotIndex NewIdx;
  SmallPtrSet<LiveRange*, 8> Updated;
  bool UpdateFlags;

public:
  HMEditor(LiveIntervals& LIS, const MachineRegisterInfo& MRI,
           const TargetRegisterInfo& TRI,
           SlotIndex OldIdx, SlotIndex NewIdx, bool UpdateFlags)
    : LIS(LIS), MRI(MRI), TRI(TRI), OldIdx(OldIdx), NewIdx(NewIdx),
      UpdateFlags(UpdateFlags) {}

  // FIXME: UpdateFlags is a workaround that creates live intervals for all
  // physregs, even those that aren't needed for regalloc, in order to update
  // kill flags. This is wasteful. Eventually, LiveVariables will strip all kill
  // flags, and postRA passes will use a live register utility instead.
  LiveRange *getRegUnitLI(unsigned Unit) {
    if (UpdateFlags && !MRI.isReservedRegUnit(Unit))
      return &LIS.getRegUnit(Unit);
    return LIS.getCachedRegUnit(Unit);
  }

  /// Update all live ranges touched by MI, assuming a move from OldIdx to
  /// NewIdx.
  void updateAllRanges(MachineInstr *MI) {
    LLVM_DEBUG(dbgs() << "handleMove " << OldIdx << " -> " << NewIdx << ": "
                      << *MI);
    bool hasRegMask = false;
    for (MachineOperand &MO : MI->operands()) {
      if (MO.isRegMask())
        hasRegMask = true;
      if (!MO.isReg())
        continue;
      if (MO.isUse()) {
        if (!MO.readsReg())
          continue;
        // Aggressively clear all kill flags.
        // They are reinserted by VirtRegRewriter.
        MO.setIsKill(false);
      }

      Register Reg = MO.getReg();
      if (!Reg)
        continue;
      if (Reg.isVirtual()) {
        LiveInterval &LI = LIS.getInterval(Reg);
        if (LI.hasSubRanges()) {
          unsigned SubReg = MO.getSubReg();
          LaneBitmask LaneMask = SubReg ? TRI.getSubRegIndexLaneMask(SubReg)
                                        : MRI.getMaxLaneMaskForVReg(Reg);
          for (LiveInterval::SubRange &S : LI.subranges()) {
            if ((S.LaneMask & LaneMask).none())
              continue;
            updateRange(S, Reg, S.LaneMask);
          }
        }
        updateRange(LI, Reg, LaneBitmask::getNone());
        // If main range has a hole and we are moving a subrange use across
        // the hole updateRange() cannot properly handle it since it only
        // gets the LiveRange and not the whole LiveInterval. As a result
        // we may end up with a main range not covering all subranges.
        // This is extremely rare case, so let's check and reconstruct the
        // main range.
        if (LI.hasSubRanges()) {
          unsigned SubReg = MO.getSubReg();
          LaneBitmask LaneMask = SubReg ? TRI.getSubRegIndexLaneMask(SubReg)
                                        : MRI.getMaxLaneMaskForVReg(Reg);
          for (LiveInterval::SubRange &S : LI.subranges()) {
            if ((S.LaneMask & LaneMask).none() || LI.covers(S))
              continue;
            LI.clear();
            LIS.constructMainRangeFromSubranges(LI);
            break;
          }
        }

        continue;
      }

      // For physregs, only update the regunits that actually have a
      // precomputed live range.
      for (MCRegUnit Unit : TRI.regunits(Reg.asMCReg()))
        if (LiveRange *LR = getRegUnitLI(Unit))
          updateRange(*LR, Unit, LaneBitmask::getNone());
    }
    if (hasRegMask)
      updateRegMaskSlots();
  }

private:
  /// Update a single live range, assuming an instruction has been moved from
  /// OldIdx to NewIdx.
  void updateRange(LiveRange &LR, Register Reg, LaneBitmask LaneMask) {
    if (!Updated.insert(&LR).second)
      return;
    LLVM_DEBUG({
      dbgs() << "     ";
      if (Reg.isVirtual()) {
        dbgs() << printReg(Reg);
        if (LaneMask.any())
          dbgs() << " L" << PrintLaneMask(LaneMask);
      } else {
        dbgs() << printRegUnit(Reg, &TRI);
      }
      dbgs() << ":\t" << LR << '\n';
    });
    if (SlotIndex::isEarlierInstr(OldIdx, NewIdx))
      handleMoveDown(LR);
    else
      handleMoveUp(LR, Reg, LaneMask);
    LLVM_DEBUG(dbgs() << "        -->\t" << LR << '\n');
    LR.verify();
  }

  /// Update LR to reflect an instruction has been moved downwards from OldIdx
  /// to NewIdx (OldIdx < NewIdx).
  void handleMoveDown(LiveRange &LR) {
    LiveRange::iterator E = LR.end();
    // Segment going into OldIdx.
    LiveRange::iterator OldIdxIn = LR.find(OldIdx.getBaseIndex());

    // No value live before or after OldIdx? Nothing to do.
    if (OldIdxIn == E || SlotIndex::isEarlierInstr(OldIdx, OldIdxIn->start))
      return;

    LiveRange::iterator OldIdxOut;
    // Do we have a value live-in to OldIdx?
    if (SlotIndex::isEarlierInstr(OldIdxIn->start, OldIdx)) {
      // If the live-in value already extends to NewIdx, there is nothing to do.
      if (SlotIndex::isEarlierEqualInstr(NewIdx, OldIdxIn->end))
        return;
      // Aggressively remove all kill flags from the old kill point.
      // Kill flags shouldn't be used while live intervals exist, they will be
      // reinserted by VirtRegRewriter.
      if (MachineInstr *KillMI = LIS.getInstructionFromIndex(OldIdxIn->end))
        for (MachineOperand &MOP : mi_bundle_ops(*KillMI))
          if (MOP.isReg() && MOP.isUse())
            MOP.setIsKill(false);

      // Is there a def before NewIdx which is not OldIdx?
      LiveRange::iterator Next = std::next(OldIdxIn);
      if (Next != E && !SlotIndex::isSameInstr(OldIdx, Next->start) &&
          SlotIndex::isEarlierInstr(Next->start, NewIdx)) {
        // If we are here then OldIdx was just a use but not a def. We only have
        // to ensure liveness extends to NewIdx.
        LiveRange::iterator NewIdxIn =
          LR.advanceTo(Next, NewIdx.getBaseIndex());
        // Extend the segment before NewIdx if necessary.
        if (NewIdxIn == E ||
            !SlotIndex::isEarlierInstr(NewIdxIn->start, NewIdx)) {
          LiveRange::iterator Prev = std::prev(NewIdxIn);
          Prev->end = NewIdx.getRegSlot();
        }
        // Extend OldIdxIn.
        OldIdxIn->end = Next->start;
        return;
      }

      // Adjust OldIdxIn->end to reach NewIdx. This may temporarily make LR
      // invalid by overlapping ranges.
      bool isKill = SlotIndex::isSameInstr(OldIdx, OldIdxIn->end);
      OldIdxIn->end = NewIdx.getRegSlot(OldIdxIn->end.isEarlyClobber());
      // If this was not a kill, then there was no def and we're done.
      if (!isKill)
        return;

      // Did we have a Def at OldIdx?
      OldIdxOut = Next;
      if (OldIdxOut == E || !SlotIndex::isSameInstr(OldIdx, OldIdxOut->start))
        return;
    } else {
      OldIdxOut = OldIdxIn;
    }

    // If we are here then there is a Definition at OldIdx. OldIdxOut points
    // to the segment starting there.
    assert(OldIdxOut != E && SlotIndex::isSameInstr(OldIdx, OldIdxOut->start) &&
           "No def?");
    VNInfo *OldIdxVNI = OldIdxOut->valno;
    assert(OldIdxVNI->def == OldIdxOut->start && "Inconsistent def");

    // If the defined value extends beyond NewIdx, just move the beginning
    // of the segment to NewIdx.
    SlotIndex NewIdxDef = NewIdx.getRegSlot(OldIdxOut->start.isEarlyClobber());
    if (SlotIndex::isEarlierInstr(NewIdxDef, OldIdxOut->end)) {
      OldIdxVNI->def = NewIdxDef;
      OldIdxOut->start = OldIdxVNI->def;
      return;
    }

    // If we are here then we have a Definition at OldIdx which ends before
    // NewIdx.

    // Is there an existing Def at NewIdx?
    LiveRange::iterator AfterNewIdx
      = LR.advanceTo(OldIdxOut, NewIdx.getRegSlot());
    bool OldIdxDefIsDead = OldIdxOut->end.isDead();
    if (!OldIdxDefIsDead &&
        SlotIndex::isEarlierInstr(OldIdxOut->end, NewIdxDef)) {
      // OldIdx is not a dead def, and NewIdxDef is inside a new interval.
      VNInfo *DefVNI;
      if (OldIdxOut != LR.begin() &&
          !SlotIndex::isEarlierInstr(std::prev(OldIdxOut)->end,
                                     OldIdxOut->start)) {
        // There is no gap between OldIdxOut and its predecessor anymore,
        // merge them.
        LiveRange::iterator IPrev = std::prev(OldIdxOut);
        DefVNI = OldIdxVNI;
        IPrev->end = OldIdxOut->end;
      } else {
        // The value is live in to OldIdx
        LiveRange::iterator INext = std::next(OldIdxOut);
        assert(INext != E && "Must have following segment");
        // We merge OldIdxOut and its successor. As we're dealing with subreg
        // reordering, there is always a successor to OldIdxOut in the same BB
        // We don't need INext->valno anymore and will reuse for the new segment
        // we create later.
        DefVNI = OldIdxVNI;
        INext->start = OldIdxOut->end;
        INext->valno->def = INext->start;
      }
      // If NewIdx is behind the last segment, extend that and append a new one.
      if (AfterNewIdx == E) {
        // OldIdxOut is undef at this point, Slide (OldIdxOut;AfterNewIdx] up
        // one position.
        //    |-  ?/OldIdxOut -| |- X0 -| ... |- Xn -| end
        // => |- X0/OldIdxOut -| ... |- Xn -| |- undef/NewS -| end
        std::copy(std::next(OldIdxOut), E, OldIdxOut);
        // The last segment is undefined now, reuse it for a dead def.
        LiveRange::iterator NewSegment = std::prev(E);
        *NewSegment = LiveRange::Segment(NewIdxDef, NewIdxDef.getDeadSlot(),
                                         DefVNI);
        DefVNI->def = NewIdxDef;

        LiveRange::iterator Prev = std::prev(NewSegment);
        Prev->end = NewIdxDef;
      } else {
        // OldIdxOut is undef at this point, Slide (OldIdxOut;AfterNewIdx] up
        // one position.
        //    |-  ?/OldIdxOut -| |- X0 -| ... |- Xn/AfterNewIdx -| |- Next -|
        // => |- X0/OldIdxOut -| ... |- Xn -| |- Xn/AfterNewIdx -| |- Next -|
        std::copy(std::next(OldIdxOut), std::next(AfterNewIdx), OldIdxOut);
        LiveRange::iterator Prev = std::prev(AfterNewIdx);
        // We have two cases:
        if (SlotIndex::isEarlierInstr(Prev->start, NewIdxDef)) {
          // Case 1: NewIdx is inside a liverange. Split this liverange at
          // NewIdxDef into the segment "Prev" followed by "NewSegment".
          LiveRange::iterator NewSegment = AfterNewIdx;
          *NewSegment = LiveRange::Segment(NewIdxDef, Prev->end, Prev->valno);
          Prev->valno->def = NewIdxDef;

          *Prev = LiveRange::Segment(Prev->start, NewIdxDef, DefVNI);
          DefVNI->def = Prev->start;
        } else {
          // Case 2: NewIdx is in a lifetime hole. Keep AfterNewIdx as is and
          // turn Prev into a segment from NewIdx to AfterNewIdx->start.
          *Prev = LiveRange::Segment(NewIdxDef, AfterNewIdx->start, DefVNI);
          DefVNI->def = NewIdxDef;
          assert(DefVNI != AfterNewIdx->valno);
        }
      }
      return;
    }

    if (AfterNewIdx != E &&
        SlotIndex::isSameInstr(AfterNewIdx->start, NewIdxDef)) {
      // There is an existing def at NewIdx. The def at OldIdx is coalesced into
      // that value.
      assert(AfterNewIdx->valno != OldIdxVNI && "Multiple defs of value?");
      LR.removeValNo(OldIdxVNI);
    } else {
      // There was no existing def at NewIdx. We need to create a dead def
      // at NewIdx. Shift segments over the old OldIdxOut segment, this frees
      // a new segment at the place where we want to construct the dead def.
      //    |- OldIdxOut -| |- X0 -| ... |- Xn -| |- AfterNewIdx -|
      // => |- X0/OldIdxOut -| ... |- Xn -| |- undef/NewS. -| |- AfterNewIdx -|
      assert(AfterNewIdx != OldIdxOut && "Inconsistent iterators");
      std::copy(std::next(OldIdxOut), AfterNewIdx, OldIdxOut);
      // We can reuse OldIdxVNI now.
      LiveRange::iterator NewSegment = std::prev(AfterNewIdx);
      VNInfo *NewSegmentVNI = OldIdxVNI;
      NewSegmentVNI->def = NewIdxDef;
      *NewSegment = LiveRange::Segment(NewIdxDef, NewIdxDef.getDeadSlot(),
                                       NewSegmentVNI);
    }
  }

  /// Update LR to reflect an instruction has been moved upwards from OldIdx
  /// to NewIdx (NewIdx < OldIdx).
  void handleMoveUp(LiveRange &LR, Register Reg, LaneBitmask LaneMask) {
    LiveRange::iterator E = LR.end();
    // Segment going into OldIdx.
    LiveRange::iterator OldIdxIn = LR.find(OldIdx.getBaseIndex());

    // No value live before or after OldIdx? Nothing to do.
    if (OldIdxIn == E || SlotIndex::isEarlierInstr(OldIdx, OldIdxIn->start))
      return;

    LiveRange::iterator OldIdxOut;
    // Do we have a value live-in to OldIdx?
    if (SlotIndex::isEarlierInstr(OldIdxIn->start, OldIdx)) {
      // If the live-in value isn't killed here, then we have no Def at
      // OldIdx, moreover the value must be live at NewIdx so there is nothing
      // to do.
      bool isKill = SlotIndex::isSameInstr(OldIdx, OldIdxIn->end);
      if (!isKill)
        return;

      // At this point we have to move OldIdxIn->end back to the nearest
      // previous use or (dead-)def but no further than NewIdx.
      SlotIndex DefBeforeOldIdx
        = std::max(OldIdxIn->start.getDeadSlot(),
                   NewIdx.getRegSlot(OldIdxIn->end.isEarlyClobber()));
      OldIdxIn->end = findLastUseBefore(DefBeforeOldIdx, Reg, LaneMask);

      // Did we have a Def at OldIdx? If not we are done now.
      OldIdxOut = std::next(OldIdxIn);
      if (OldIdxOut == E || !SlotIndex::isSameInstr(OldIdx, OldIdxOut->start))
        return;
    } else {
      OldIdxOut = OldIdxIn;
      OldIdxIn = OldIdxOut != LR.begin() ? std::prev(OldIdxOut) : E;
    }

    // If we are here then there is a Definition at OldIdx. OldIdxOut points
    // to the segment starting there.
    assert(OldIdxOut != E && SlotIndex::isSameInstr(OldIdx, OldIdxOut->start) &&
           "No def?");
    VNInfo *OldIdxVNI = OldIdxOut->valno;
    assert(OldIdxVNI->def == OldIdxOut->start && "Inconsistent def");
    bool OldIdxDefIsDead = OldIdxOut->end.isDead();

    // Is there an existing def at NewIdx?
    SlotIndex NewIdxDef = NewIdx.getRegSlot(OldIdxOut->start.isEarlyClobber());
    LiveRange::iterator NewIdxOut = LR.find(NewIdx.getRegSlot());
    if (SlotIndex::isSameInstr(NewIdxOut->start, NewIdx)) {
      assert(NewIdxOut->valno != OldIdxVNI &&
             "Same value defined more than once?");
      // If OldIdx was a dead def remove it.
      if (!OldIdxDefIsDead) {
        // Remove segment starting at NewIdx and move begin of OldIdxOut to
        // NewIdx so it can take its place.
        OldIdxVNI->def = NewIdxDef;
        OldIdxOut->start = NewIdxDef;
        LR.removeValNo(NewIdxOut->valno);
      } else {
        // Simply remove the dead def at OldIdx.
        LR.removeValNo(OldIdxVNI);
      }
    } else {
      // Previously nothing was live after NewIdx, so all we have to do now is
      // move the begin of OldIdxOut to NewIdx.
      if (!OldIdxDefIsDead) {
        // Do we have any intermediate Defs between OldIdx and NewIdx?
        if (OldIdxIn != E &&
            SlotIndex::isEarlierInstr(NewIdxDef, OldIdxIn->start)) {
          // OldIdx is not a dead def and NewIdx is before predecessor start.
          LiveRange::iterator NewIdxIn = NewIdxOut;
          assert(NewIdxIn == LR.find(NewIdx.getBaseIndex()));
          const SlotIndex SplitPos = NewIdxDef;
          OldIdxVNI = OldIdxIn->valno;

          SlotIndex NewDefEndPoint = std::next(NewIdxIn)->end;
          LiveRange::iterator Prev = std::prev(OldIdxIn);
          if (OldIdxIn != LR.begin() &&
              SlotIndex::isEarlierInstr(NewIdx, Prev->end)) {
            // If the segment before OldIdx read a value defined earlier than
            // NewIdx, the moved instruction also reads and forwards that
            // value. Extend the lifetime of the new def point.

            // Extend to where the previous range started, unless there is
            // another redef first.
            NewDefEndPoint = std::min(OldIdxIn->start,
                                      std::next(NewIdxOut)->start);
          }

          // Merge the OldIdxIn and OldIdxOut segments into OldIdxOut.
          OldIdxOut->valno->def = OldIdxIn->start;
          *OldIdxOut = LiveRange::Segment(OldIdxIn->start, OldIdxOut->end,
                                          OldIdxOut->valno);
          // OldIdxIn and OldIdxVNI are now undef and can be overridden.
          // We Slide [NewIdxIn, OldIdxIn) down one position.
          //    |- X0/NewIdxIn -| ... |- Xn-1 -||- Xn/OldIdxIn -||- OldIdxOut -|
          // => |- undef/NexIdxIn -| |- X0 -| ... |- Xn-1 -| |- Xn/OldIdxOut -|
          std::copy_backward(NewIdxIn, OldIdxIn, OldIdxOut);
          // NewIdxIn is now considered undef so we can reuse it for the moved
          // value.
          LiveRange::iterator NewSegment = NewIdxIn;
          LiveRange::iterator Next = std::next(NewSegment);
          if (SlotIndex::isEarlierInstr(Next->start, NewIdx)) {
            // There is no gap between NewSegment and its predecessor.
            *NewSegment = LiveRange::Segment(Next->start, SplitPos,
                                             Next->valno);

            *Next = LiveRange::Segment(SplitPos, NewDefEndPoint, OldIdxVNI);
            Next->valno->def = SplitPos;
          } else {
            // There is a gap between NewSegment and its predecessor
            // Value becomes live in.
            *NewSegment = LiveRange::Segment(SplitPos, Next->start, OldIdxVNI);
            NewSegment->valno->def = SplitPos;
          }
        } else {
          // Leave the end point of a live def.
          OldIdxOut->start = NewIdxDef;
          OldIdxVNI->def = NewIdxDef;
          if (OldIdxIn != E && SlotIndex::isEarlierInstr(NewIdx, OldIdxIn->end))
            OldIdxIn->end = NewIdxDef;
        }
      } else if (OldIdxIn != E
          && SlotIndex::isEarlierInstr(NewIdxOut->start, NewIdx)
          && SlotIndex::isEarlierInstr(NewIdx, NewIdxOut->end)) {
        // OldIdxVNI is a dead def that has been moved into the middle of
        // another value in LR. That can happen when LR is a whole register,
        // but the dead def is a write to a subreg that is dead at NewIdx.
        // The dead def may have been moved across other values
        // in LR, so move OldIdxOut up to NewIdxOut. Slide [NewIdxOut;OldIdxOut)
        // down one position.
        //    |- X0/NewIdxOut -| ... |- Xn-1 -| |- Xn/OldIdxOut -| |- next - |
        // => |- X0/NewIdxOut -| |- X0 -| ... |- Xn-1 -| |- next -|
        std::copy_backward(NewIdxOut, OldIdxOut, std::next(OldIdxOut));
        // Modify the segment at NewIdxOut and the following segment to meet at
        // the point of the dead def, with the following segment getting
        // OldIdxVNI as its value number.
        *NewIdxOut = LiveRange::Segment(
            NewIdxOut->start, NewIdxDef.getRegSlot(), NewIdxOut->valno);
        *(NewIdxOut + 1) = LiveRange::Segment(
            NewIdxDef.getRegSlot(), (NewIdxOut + 1)->end, OldIdxVNI);
        OldIdxVNI->def = NewIdxDef;
        // Modify subsequent segments to be defined by the moved def OldIdxVNI.
        for (auto *Idx = NewIdxOut + 2; Idx <= OldIdxOut; ++Idx)
          Idx->valno = OldIdxVNI;
        // Aggressively remove all dead flags from the former dead definition.
        // Kill/dead flags shouldn't be used while live intervals exist; they
        // will be reinserted by VirtRegRewriter.
        if (MachineInstr *KillMI = LIS.getInstructionFromIndex(NewIdx))
          for (MIBundleOperands MO(*KillMI); MO.isValid(); ++MO)
            if (MO->isReg() && !MO->isUse())
              MO->setIsDead(false);
      } else {
        // OldIdxVNI is a dead def. It may have been moved across other values
        // in LR, so move OldIdxOut up to NewIdxOut. Slide [NewIdxOut;OldIdxOut)
        // down one position.
        //    |- X0/NewIdxOut -| ... |- Xn-1 -| |- Xn/OldIdxOut -| |- next - |
        // => |- undef/NewIdxOut -| |- X0 -| ... |- Xn-1 -| |- next -|
        std::copy_backward(NewIdxOut, OldIdxOut, std::next(OldIdxOut));
        // OldIdxVNI can be reused now to build a new dead def segment.
        LiveRange::iterator NewSegment = NewIdxOut;
        VNInfo *NewSegmentVNI = OldIdxVNI;
        *NewSegment = LiveRange::Segment(NewIdxDef, NewIdxDef.getDeadSlot(),
                                         NewSegmentVNI);
        NewSegmentVNI->def = NewIdxDef;
      }
    }
  }

  void updateRegMaskSlots() {
    SmallVectorImpl<SlotIndex>::iterator RI =
        llvm::lower_bound(LIS.RegMaskSlots, OldIdx);
    assert(RI != LIS.RegMaskSlots.end() && *RI == OldIdx.getRegSlot() &&
           "No RegMask at OldIdx.");
    *RI = NewIdx.getRegSlot();
    assert((RI == LIS.RegMaskSlots.begin() ||
            SlotIndex::isEarlierInstr(*std::prev(RI), *RI)) &&
           "Cannot move regmask instruction above another call");
    assert((std::next(RI) == LIS.RegMaskSlots.end() ||
            SlotIndex::isEarlierInstr(*RI, *std::next(RI))) &&
           "Cannot move regmask instruction below another call");
  }

  // Return the last use of reg between NewIdx and OldIdx.
  SlotIndex findLastUseBefore(SlotIndex Before, Register Reg,
                              LaneBitmask LaneMask) {
    if (Reg.isVirtual()) {
      SlotIndex LastUse = Before;
      for (MachineOperand &MO : MRI.use_nodbg_operands(Reg)) {
        if (MO.isUndef())
          continue;
        unsigned SubReg = MO.getSubReg();
        if (SubReg != 0 && LaneMask.any()
            && (TRI.getSubRegIndexLaneMask(SubReg) & LaneMask).none())
          continue;

        const MachineInstr &MI = *MO.getParent();
        SlotIndex InstSlot = LIS.getSlotIndexes()->getInstructionIndex(MI);
        if (InstSlot > LastUse && InstSlot < OldIdx)
          LastUse = InstSlot.getRegSlot();
      }
      return LastUse;
    }

    // This is a regunit interval, so scanning the use list could be very
    // expensive. Scan upwards from OldIdx instead.
    assert(Before < OldIdx && "Expected upwards move");
    SlotIndexes *Indexes = LIS.getSlotIndexes();
    MachineBasicBlock *MBB = Indexes->getMBBFromIndex(Before);

    // OldIdx may not correspond to an instruction any longer, so set MII to
    // point to the next instruction after OldIdx, or MBB->end().
    MachineBasicBlock::iterator MII = MBB->end();
    if (MachineInstr *MI = Indexes->getInstructionFromIndex(
                           Indexes->getNextNonNullIndex(OldIdx)))
      if (MI->getParent() == MBB)
        MII = MI;

    MachineBasicBlock::iterator Begin = MBB->begin();
    while (MII != Begin) {
      if ((--MII)->isDebugOrPseudoInstr())
        continue;
      SlotIndex Idx = Indexes->getInstructionIndex(*MII);

      // Stop searching when Before is reached.
      if (!SlotIndex::isEarlierInstr(Before, Idx))
        return Before;

      // Check if MII uses Reg.
      for (MIBundleOperands MO(*MII); MO.isValid(); ++MO)
        if (MO->isReg() && !MO->isUndef() && MO->getReg().isPhysical() &&
            TRI.hasRegUnit(MO->getReg(), Reg))
          return Idx.getRegSlot();
    }
    // Didn't reach Before. It must be the first instruction in the block.
    return Before;
  }
};

void LiveIntervals::handleMove(MachineInstr &MI, bool UpdateFlags) {
  // It is fine to move a bundle as a whole, but not an individual instruction
  // inside it.
  assert((!MI.isBundled() || MI.getOpcode() == TargetOpcode::BUNDLE) &&
         "Cannot move instruction in bundle");
  SlotIndex OldIndex = Indexes->getInstructionIndex(MI);
  Indexes->removeMachineInstrFromMaps(MI);
  SlotIndex NewIndex = Indexes->insertMachineInstrInMaps(MI);
  assert(getMBBStartIdx(MI.getParent()) <= OldIndex &&
         OldIndex < getMBBEndIdx(MI.getParent()) &&
         "Cannot handle moves across basic block boundaries.");

  HMEditor HME(*this, *MRI, *TRI, OldIndex, NewIndex, UpdateFlags);
  HME.updateAllRanges(&MI);
}

void LiveIntervals::handleMoveIntoNewBundle(MachineInstr &BundleStart,
                                            bool UpdateFlags) {
  assert((BundleStart.getOpcode() == TargetOpcode::BUNDLE) &&
         "Bundle start is not a bundle");
  SmallVector<SlotIndex, 16> ToProcess;
  const SlotIndex NewIndex = Indexes->insertMachineInstrInMaps(BundleStart);
  auto BundleEnd = getBundleEnd(BundleStart.getIterator());

  auto I = BundleStart.getIterator();
  I++;
  while (I != BundleEnd) {
    if (!Indexes->hasIndex(*I))
      continue;
    SlotIndex OldIndex = Indexes->getInstructionIndex(*I, true);
    ToProcess.push_back(OldIndex);
    Indexes->removeMachineInstrFromMaps(*I, true);
    I++;
  }
  for (SlotIndex OldIndex : ToProcess) {
    HMEditor HME(*this, *MRI, *TRI, OldIndex, NewIndex, UpdateFlags);
    HME.updateAllRanges(&BundleStart);
  }

  // Fix up dead defs
  const SlotIndex Index = getInstructionIndex(BundleStart);
  for (MachineOperand &MO : BundleStart.operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg.isVirtual() && hasInterval(Reg) && !MO.isUndef()) {
      LiveInterval &LI = getInterval(Reg);
      LiveQueryResult LRQ = LI.Query(Index);
      if (LRQ.isDeadDef())
        MO.setIsDead();
    }
  }
}

void LiveIntervals::repairOldRegInRange(const MachineBasicBlock::iterator Begin,
                                        const MachineBasicBlock::iterator End,
                                        const SlotIndex EndIdx, LiveRange &LR,
                                        const Register Reg,
                                        LaneBitmask LaneMask) {
  LiveInterval::iterator LII = LR.find(EndIdx);
  SlotIndex lastUseIdx;
  if (LII != LR.end() && LII->start < EndIdx) {
    lastUseIdx = LII->end;
  } else if (LII == LR.begin()) {
    // We may not have a liverange at all if this is a subregister untouched
    // between \p Begin and \p End.
  } else {
    --LII;
  }

  for (MachineBasicBlock::iterator I = End; I != Begin;) {
    --I;
    MachineInstr &MI = *I;
    if (MI.isDebugOrPseudoInstr())
      continue;

    SlotIndex instrIdx = getInstructionIndex(MI);
    bool isStartValid = getInstructionFromIndex(LII->start);
    bool isEndValid = getInstructionFromIndex(LII->end);

    // FIXME: This doesn't currently handle early-clobber or multiple removed
    // defs inside of the region to repair.
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || MO.getReg() != Reg)
        continue;

      unsigned SubReg = MO.getSubReg();
      LaneBitmask Mask = TRI->getSubRegIndexLaneMask(SubReg);
      if ((Mask & LaneMask).none())
        continue;

      if (MO.isDef()) {
        if (!isStartValid) {
          if (LII->end.isDead()) {
            LII = LR.removeSegment(LII, true);
            if (LII != LR.begin())
              --LII;
          } else {
            LII->start = instrIdx.getRegSlot();
            LII->valno->def = instrIdx.getRegSlot();
            if (MO.getSubReg() && !MO.isUndef())
              lastUseIdx = instrIdx.getRegSlot();
            else
              lastUseIdx = SlotIndex();
            continue;
          }
        }

        if (!lastUseIdx.isValid()) {
          VNInfo *VNI = LR.getNextValue(instrIdx.getRegSlot(), VNInfoAllocator);
          LiveRange::Segment S(instrIdx.getRegSlot(),
                               instrIdx.getDeadSlot(), VNI);
          LII = LR.addSegment(S);
        } else if (LII->start != instrIdx.getRegSlot()) {
          VNInfo *VNI = LR.getNextValue(instrIdx.getRegSlot(), VNInfoAllocator);
          LiveRange::Segment S(instrIdx.getRegSlot(), lastUseIdx, VNI);
          LII = LR.addSegment(S);
        }

        if (MO.getSubReg() && !MO.isUndef())
          lastUseIdx = instrIdx.getRegSlot();
        else
          lastUseIdx = SlotIndex();
      } else if (MO.isUse()) {
        // FIXME: This should probably be handled outside of this branch,
        // either as part of the def case (for defs inside of the region) or
        // after the loop over the region.
        if (!isEndValid && !LII->end.isBlock())
          LII->end = instrIdx.getRegSlot();
        if (!lastUseIdx.isValid())
          lastUseIdx = instrIdx.getRegSlot();
      }
    }
  }

  bool isStartValid = getInstructionFromIndex(LII->start);
  if (!isStartValid && LII->end.isDead())
    LR.removeSegment(*LII, true);
}

void
LiveIntervals::repairIntervalsInRange(MachineBasicBlock *MBB,
                                      MachineBasicBlock::iterator Begin,
                                      MachineBasicBlock::iterator End,
                                      ArrayRef<Register> OrigRegs) {
  // Find anchor points, which are at the beginning/end of blocks or at
  // instructions that already have indexes.
  while (Begin != MBB->begin() && !Indexes->hasIndex(*std::prev(Begin)))
    --Begin;
  while (End != MBB->end() && !Indexes->hasIndex(*End))
    ++End;

  SlotIndex EndIdx;
  if (End == MBB->end())
    EndIdx = getMBBEndIdx(MBB).getPrevSlot();
  else
    EndIdx = getInstructionIndex(*End);

  Indexes->repairIndexesInRange(MBB, Begin, End);

  // Make sure a live interval exists for all register operands in the range.
  SmallVector<Register> RegsToRepair(OrigRegs.begin(), OrigRegs.end());
  for (MachineBasicBlock::iterator I = End; I != Begin;) {
    --I;
    MachineInstr &MI = *I;
    if (MI.isDebugOrPseudoInstr())
      continue;
    for (const MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.getReg().isVirtual()) {
        Register Reg = MO.getReg();
        if (MO.getSubReg() && hasInterval(Reg) &&
            MRI->shouldTrackSubRegLiveness(Reg)) {
          LiveInterval &LI = getInterval(Reg);
          if (!LI.hasSubRanges()) {
            // If the new instructions refer to subregs but the old instructions
            // did not, throw away any old live interval so it will be
            // recomputed with subranges.
            removeInterval(Reg);
          } else if (MO.isDef()) {
            // Similarly if a subreg def has no precise subrange match then
            // assume we need to recompute all subranges.
            unsigned SubReg = MO.getSubReg();
            LaneBitmask Mask = TRI->getSubRegIndexLaneMask(SubReg);
            if (llvm::none_of(LI.subranges(),
                              [Mask](LiveInterval::SubRange &SR) {
                                return SR.LaneMask == Mask;
                              })) {
              removeInterval(Reg);
            }
          }
        }
        if (!hasInterval(Reg)) {
          createAndComputeVirtRegInterval(Reg);
          // Don't bother to repair a freshly calculated live interval.
          llvm::erase(RegsToRepair, Reg);
        }
      }
    }
  }

  for (Register Reg : RegsToRepair) {
    if (!Reg.isVirtual())
      continue;

    LiveInterval &LI = getInterval(Reg);
    // FIXME: Should we support undefs that gain defs?
    if (!LI.hasAtLeastOneValue())
      continue;

    for (LiveInterval::SubRange &S : LI.subranges())
      repairOldRegInRange(Begin, End, EndIdx, S, Reg, S.LaneMask);
    LI.removeEmptySubRanges();

    repairOldRegInRange(Begin, End, EndIdx, LI, Reg);
  }
}

void LiveIntervals::removePhysRegDefAt(MCRegister Reg, SlotIndex Pos) {
  for (MCRegUnit Unit : TRI->regunits(Reg)) {
    if (LiveRange *LR = getCachedRegUnit(Unit))
      if (VNInfo *VNI = LR->getVNInfoAt(Pos))
        LR->removeValNo(VNI);
  }
}

void LiveIntervals::removeVRegDefAt(LiveInterval &LI, SlotIndex Pos) {
  // LI may not have the main range computed yet, but its subranges may
  // be present.
  VNInfo *VNI = LI.getVNInfoAt(Pos);
  if (VNI != nullptr) {
    assert(VNI->def.getBaseIndex() == Pos.getBaseIndex());
    LI.removeValNo(VNI);
  }

  // Also remove the value defined in subranges.
  for (LiveInterval::SubRange &S : LI.subranges()) {
    if (VNInfo *SVNI = S.getVNInfoAt(Pos))
      if (SVNI->def.getBaseIndex() == Pos.getBaseIndex())
        S.removeValNo(SVNI);
  }
  LI.removeEmptySubRanges();
}

void LiveIntervals::splitSeparateComponents(LiveInterval &LI,
    SmallVectorImpl<LiveInterval*> &SplitLIs) {
  ConnectedVNInfoEqClasses ConEQ(*this);
  unsigned NumComp = ConEQ.Classify(LI);
  if (NumComp <= 1)
    return;
  LLVM_DEBUG(dbgs() << "  Split " << NumComp << " components: " << LI << '\n');
  Register Reg = LI.reg();
  for (unsigned I = 1; I < NumComp; ++I) {
    Register NewVReg = MRI->cloneVirtualRegister(Reg);
    LiveInterval &NewLI = createEmptyInterval(NewVReg);
    SplitLIs.push_back(&NewLI);
  }
  ConEQ.Distribute(LI, SplitLIs.data(), *MRI);
}

void LiveIntervals::constructMainRangeFromSubranges(LiveInterval &LI) {
  assert(LICalc && "LICalc not initialized.");
  LICalc->reset(MF, getSlotIndexes(), DomTree, &getVNInfoAllocator());
  LICalc->constructMainRangeFromSubranges(LI);
}
