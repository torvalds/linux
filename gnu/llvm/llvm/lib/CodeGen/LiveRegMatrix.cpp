//===- LiveRegMatrix.cpp - Track register interference --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LiveRegMatrix analysis pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveRegMatrix.h"
#include "RegisterCoalescer.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervalUnion.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumAssigned   , "Number of registers assigned");
STATISTIC(NumUnassigned , "Number of registers unassigned");

char LiveRegMatrix::ID = 0;
INITIALIZE_PASS_BEGIN(LiveRegMatrix, "liveregmatrix",
                      "Live Register Matrix", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_END(LiveRegMatrix, "liveregmatrix",
                    "Live Register Matrix", false, false)

LiveRegMatrix::LiveRegMatrix() : MachineFunctionPass(ID) {}

void LiveRegMatrix::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<LiveIntervalsWrapperPass>();
  AU.addRequiredTransitive<VirtRegMap>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool LiveRegMatrix::runOnMachineFunction(MachineFunction &MF) {
  TRI = MF.getSubtarget().getRegisterInfo();
  LIS = &getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  VRM = &getAnalysis<VirtRegMap>();

  unsigned NumRegUnits = TRI->getNumRegUnits();
  if (NumRegUnits != Matrix.size())
    Queries.reset(new LiveIntervalUnion::Query[NumRegUnits]);
  Matrix.init(LIUAlloc, NumRegUnits);

  // Make sure no stale queries get reused.
  invalidateVirtRegs();
  return false;
}

void LiveRegMatrix::releaseMemory() {
  for (unsigned i = 0, e = Matrix.size(); i != e; ++i) {
    Matrix[i].clear();
    // No need to clear Queries here, since LiveIntervalUnion::Query doesn't
    // have anything important to clear and LiveRegMatrix's runOnFunction()
    // does a std::unique_ptr::reset anyways.
  }
}

template <typename Callable>
static bool foreachUnit(const TargetRegisterInfo *TRI,
                        const LiveInterval &VRegInterval, MCRegister PhysReg,
                        Callable Func) {
  if (VRegInterval.hasSubRanges()) {
    for (MCRegUnitMaskIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
      unsigned Unit = (*Units).first;
      LaneBitmask Mask = (*Units).second;
      for (const LiveInterval::SubRange &S : VRegInterval.subranges()) {
        if ((S.LaneMask & Mask).any()) {
          if (Func(Unit, S))
            return true;
          break;
        }
      }
    }
  } else {
    for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
      if (Func(Unit, VRegInterval))
        return true;
    }
  }
  return false;
}

void LiveRegMatrix::assign(const LiveInterval &VirtReg, MCRegister PhysReg) {
  LLVM_DEBUG(dbgs() << "assigning " << printReg(VirtReg.reg(), TRI) << " to "
                    << printReg(PhysReg, TRI) << ':');
  assert(!VRM->hasPhys(VirtReg.reg()) && "Duplicate VirtReg assignment");
  VRM->assignVirt2Phys(VirtReg.reg(), PhysReg);

  foreachUnit(
      TRI, VirtReg, PhysReg, [&](unsigned Unit, const LiveRange &Range) {
        LLVM_DEBUG(dbgs() << ' ' << printRegUnit(Unit, TRI) << ' ' << Range);
        Matrix[Unit].unify(VirtReg, Range);
        return false;
      });

  ++NumAssigned;
  LLVM_DEBUG(dbgs() << '\n');
}

void LiveRegMatrix::unassign(const LiveInterval &VirtReg) {
  Register PhysReg = VRM->getPhys(VirtReg.reg());
  LLVM_DEBUG(dbgs() << "unassigning " << printReg(VirtReg.reg(), TRI)
                    << " from " << printReg(PhysReg, TRI) << ':');
  VRM->clearVirt(VirtReg.reg());

  foreachUnit(TRI, VirtReg, PhysReg,
              [&](unsigned Unit, const LiveRange &Range) {
                LLVM_DEBUG(dbgs() << ' ' << printRegUnit(Unit, TRI));
                Matrix[Unit].extract(VirtReg, Range);
                return false;
              });

  ++NumUnassigned;
  LLVM_DEBUG(dbgs() << '\n');
}

bool LiveRegMatrix::isPhysRegUsed(MCRegister PhysReg) const {
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    if (!Matrix[Unit].empty())
      return true;
  }
  return false;
}

bool LiveRegMatrix::checkRegMaskInterference(const LiveInterval &VirtReg,
                                             MCRegister PhysReg) {
  // Check if the cached information is valid.
  // The same BitVector can be reused for all PhysRegs.
  // We could cache multiple VirtRegs if it becomes necessary.
  if (RegMaskVirtReg != VirtReg.reg() || RegMaskTag != UserTag) {
    RegMaskVirtReg = VirtReg.reg();
    RegMaskTag = UserTag;
    RegMaskUsable.clear();
    LIS->checkRegMaskInterference(VirtReg, RegMaskUsable);
  }

  // The BitVector is indexed by PhysReg, not register unit.
  // Regmask interference is more fine grained than regunits.
  // For example, a Win64 call can clobber %ymm8 yet preserve %xmm8.
  return !RegMaskUsable.empty() && (!PhysReg || !RegMaskUsable.test(PhysReg));
}

bool LiveRegMatrix::checkRegUnitInterference(const LiveInterval &VirtReg,
                                             MCRegister PhysReg) {
  if (VirtReg.empty())
    return false;
  CoalescerPair CP(VirtReg.reg(), PhysReg, *TRI);

  bool Result = foreachUnit(TRI, VirtReg, PhysReg, [&](unsigned Unit,
                                                       const LiveRange &Range) {
    const LiveRange &UnitRange = LIS->getRegUnit(Unit);
    return Range.overlaps(UnitRange, CP, *LIS->getSlotIndexes());
  });
  return Result;
}

LiveIntervalUnion::Query &LiveRegMatrix::query(const LiveRange &LR,
                                               MCRegister RegUnit) {
  LiveIntervalUnion::Query &Q = Queries[RegUnit];
  Q.init(UserTag, LR, Matrix[RegUnit]);
  return Q;
}

LiveRegMatrix::InterferenceKind
LiveRegMatrix::checkInterference(const LiveInterval &VirtReg,
                                 MCRegister PhysReg) {
  if (VirtReg.empty())
    return IK_Free;

  // Regmask interference is the fastest check.
  if (checkRegMaskInterference(VirtReg, PhysReg))
    return IK_RegMask;

  // Check for fixed interference.
  if (checkRegUnitInterference(VirtReg, PhysReg))
    return IK_RegUnit;

  // Check the matrix for virtual register interference.
  bool Interference = foreachUnit(TRI, VirtReg, PhysReg,
                                  [&](MCRegister Unit, const LiveRange &LR) {
                                    return query(LR, Unit).checkInterference();
                                  });
  if (Interference)
    return IK_VirtReg;

  return IK_Free;
}

bool LiveRegMatrix::checkInterference(SlotIndex Start, SlotIndex End,
                                      MCRegister PhysReg) {
  // Construct artificial live range containing only one segment [Start, End).
  VNInfo valno(0, Start);
  LiveRange::Segment Seg(Start, End, &valno);
  LiveRange LR;
  LR.addSegment(Seg);

  // Check for interference with that segment
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    // LR is stack-allocated. LiveRegMatrix caches queries by a key that
    // includes the address of the live range. If (for the same reg unit) this
    // checkInterference overload is called twice, without any other query()
    // calls in between (on heap-allocated LiveRanges)  - which would invalidate
    // the cached query - the LR address seen the second time may well be the
    // same as that seen the first time, while the Start/End/valno may not - yet
    // the same cached result would be fetched. To avoid that, we don't cache
    // this query.
    //
    // FIXME: the usability of the Query API needs to be improved to avoid
    // subtle bugs due to query identity. Avoiding caching, for example, would
    // greatly simplify things.
    LiveIntervalUnion::Query Q;
    Q.reset(UserTag, LR, Matrix[Unit]);
    if (Q.checkInterference())
      return true;
  }
  return false;
}

Register LiveRegMatrix::getOneVReg(unsigned PhysReg) const {
  const LiveInterval *VRegInterval = nullptr;
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    if ((VRegInterval = Matrix[Unit].getOneVReg()))
      return VRegInterval->reg();
  }

  return MCRegister::NoRegister;
}
