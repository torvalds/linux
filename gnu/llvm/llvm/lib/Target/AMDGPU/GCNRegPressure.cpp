//===- GCNRegPressure.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the GCNRegPressure class.
///
//===----------------------------------------------------------------------===//

#include "GCNRegPressure.h"
#include "AMDGPU.h"
#include "llvm/CodeGen/RegisterPressure.h"

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

bool llvm::isEqual(const GCNRPTracker::LiveRegSet &S1,
                   const GCNRPTracker::LiveRegSet &S2) {
  if (S1.size() != S2.size())
    return false;

  for (const auto &P : S1) {
    auto I = S2.find(P.first);
    if (I == S2.end() || I->second != P.second)
      return false;
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// GCNRegPressure

unsigned GCNRegPressure::getRegKind(Register Reg,
                                    const MachineRegisterInfo &MRI) {
  assert(Reg.isVirtual());
  const auto RC = MRI.getRegClass(Reg);
  auto STI = static_cast<const SIRegisterInfo*>(MRI.getTargetRegisterInfo());
  return STI->isSGPRClass(RC)
             ? (STI->getRegSizeInBits(*RC) == 32 ? SGPR32 : SGPR_TUPLE)
         : STI->isAGPRClass(RC)
             ? (STI->getRegSizeInBits(*RC) == 32 ? AGPR32 : AGPR_TUPLE)
             : (STI->getRegSizeInBits(*RC) == 32 ? VGPR32 : VGPR_TUPLE);
}

void GCNRegPressure::inc(unsigned Reg,
                         LaneBitmask PrevMask,
                         LaneBitmask NewMask,
                         const MachineRegisterInfo &MRI) {
  if (SIRegisterInfo::getNumCoveredRegs(NewMask) ==
      SIRegisterInfo::getNumCoveredRegs(PrevMask))
    return;

  int Sign = 1;
  if (NewMask < PrevMask) {
    std::swap(NewMask, PrevMask);
    Sign = -1;
  }

  switch (auto Kind = getRegKind(Reg, MRI)) {
  case SGPR32:
  case VGPR32:
  case AGPR32:
    Value[Kind] += Sign;
    break;

  case SGPR_TUPLE:
  case VGPR_TUPLE:
  case AGPR_TUPLE:
    assert(PrevMask < NewMask);

    Value[Kind == SGPR_TUPLE ? SGPR32 : Kind == AGPR_TUPLE ? AGPR32 : VGPR32] +=
      Sign * SIRegisterInfo::getNumCoveredRegs(~PrevMask & NewMask);

    if (PrevMask.none()) {
      assert(NewMask.any());
      const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
      Value[Kind] +=
          Sign * TRI->getRegClassWeight(MRI.getRegClass(Reg)).RegWeight;
    }
    break;

  default: llvm_unreachable("Unknown register kind");
  }
}

bool GCNRegPressure::less(const MachineFunction &MF, const GCNRegPressure &O,
                          unsigned MaxOccupancy) const {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  const auto SGPROcc = std::min(MaxOccupancy,
                                ST.getOccupancyWithNumSGPRs(getSGPRNum()));
  const auto VGPROcc =
    std::min(MaxOccupancy,
             ST.getOccupancyWithNumVGPRs(getVGPRNum(ST.hasGFX90AInsts())));
  const auto OtherSGPROcc = std::min(MaxOccupancy,
                                ST.getOccupancyWithNumSGPRs(O.getSGPRNum()));
  const auto OtherVGPROcc =
    std::min(MaxOccupancy,
             ST.getOccupancyWithNumVGPRs(O.getVGPRNum(ST.hasGFX90AInsts())));

  const auto Occ = std::min(SGPROcc, VGPROcc);
  const auto OtherOcc = std::min(OtherSGPROcc, OtherVGPROcc);

  // Give first precedence to the better occupancy.
  if (Occ != OtherOcc)
    return Occ > OtherOcc;

  unsigned MaxVGPRs = ST.getMaxNumVGPRs(MF);
  unsigned MaxSGPRs = ST.getMaxNumSGPRs(MF);

  // SGPR excess pressure conditions
  unsigned ExcessSGPR = std::max(static_cast<int>(getSGPRNum() - MaxSGPRs), 0);
  unsigned OtherExcessSGPR =
      std::max(static_cast<int>(O.getSGPRNum() - MaxSGPRs), 0);

  auto WaveSize = ST.getWavefrontSize();
  // The number of virtual VGPRs required to handle excess SGPR
  unsigned VGPRForSGPRSpills = (ExcessSGPR + (WaveSize - 1)) / WaveSize;
  unsigned OtherVGPRForSGPRSpills =
      (OtherExcessSGPR + (WaveSize - 1)) / WaveSize;

  unsigned MaxArchVGPRs = ST.getAddressableNumArchVGPRs();

  // Unified excess pressure conditions, accounting for VGPRs used for SGPR
  // spills
  unsigned ExcessVGPR =
      std::max(static_cast<int>(getVGPRNum(ST.hasGFX90AInsts()) +
                                VGPRForSGPRSpills - MaxVGPRs),
               0);
  unsigned OtherExcessVGPR =
      std::max(static_cast<int>(O.getVGPRNum(ST.hasGFX90AInsts()) +
                                OtherVGPRForSGPRSpills - MaxVGPRs),
               0);
  // Arch VGPR excess pressure conditions, accounting for VGPRs used for SGPR
  // spills
  unsigned ExcessArchVGPR = std::max(
      static_cast<int>(getVGPRNum(false) + VGPRForSGPRSpills - MaxArchVGPRs),
      0);
  unsigned OtherExcessArchVGPR =
      std::max(static_cast<int>(O.getVGPRNum(false) + OtherVGPRForSGPRSpills -
                                MaxArchVGPRs),
               0);
  // AGPR excess pressure conditions
  unsigned ExcessAGPR = std::max(
      static_cast<int>(ST.hasGFX90AInsts() ? (getAGPRNum() - MaxArchVGPRs)
                                           : (getAGPRNum() - MaxVGPRs)),
      0);
  unsigned OtherExcessAGPR = std::max(
      static_cast<int>(ST.hasGFX90AInsts() ? (O.getAGPRNum() - MaxArchVGPRs)
                                           : (O.getAGPRNum() - MaxVGPRs)),
      0);

  bool ExcessRP = ExcessSGPR || ExcessVGPR || ExcessArchVGPR || ExcessAGPR;
  bool OtherExcessRP = OtherExcessSGPR || OtherExcessVGPR ||
                       OtherExcessArchVGPR || OtherExcessAGPR;

  // Give second precedence to the reduced number of spills to hold the register
  // pressure.
  if (ExcessRP || OtherExcessRP) {
    // The difference in excess VGPR pressure, after including VGPRs used for
    // SGPR spills
    int VGPRDiff = ((OtherExcessVGPR + OtherExcessArchVGPR + OtherExcessAGPR) -
                    (ExcessVGPR + ExcessArchVGPR + ExcessAGPR));

    int SGPRDiff = OtherExcessSGPR - ExcessSGPR;

    if (VGPRDiff != 0)
      return VGPRDiff > 0;
    if (SGPRDiff != 0) {
      unsigned PureExcessVGPR =
          std::max(static_cast<int>(getVGPRNum(ST.hasGFX90AInsts()) - MaxVGPRs),
                   0) +
          std::max(static_cast<int>(getVGPRNum(false) - MaxArchVGPRs), 0);
      unsigned OtherPureExcessVGPR =
          std::max(
              static_cast<int>(O.getVGPRNum(ST.hasGFX90AInsts()) - MaxVGPRs),
              0) +
          std::max(static_cast<int>(O.getVGPRNum(false) - MaxArchVGPRs), 0);

      // If we have a special case where there is a tie in excess VGPR, but one
      // of the pressures has VGPR usage from SGPR spills, prefer the pressure
      // with SGPR spills.
      if (PureExcessVGPR != OtherPureExcessVGPR)
        return SGPRDiff < 0;
      // If both pressures have the same excess pressure before and after
      // accounting for SGPR spills, prefer fewer SGPR spills.
      return SGPRDiff > 0;
    }
  }

  bool SGPRImportant = SGPROcc < VGPROcc;
  const bool OtherSGPRImportant = OtherSGPROcc < OtherVGPROcc;

  // If both pressures disagree on what is more important compare vgprs.
  if (SGPRImportant != OtherSGPRImportant) {
    SGPRImportant = false;
  }

  // Give third precedence to lower register tuple pressure.
  bool SGPRFirst = SGPRImportant;
  for (int I = 2; I > 0; --I, SGPRFirst = !SGPRFirst) {
    if (SGPRFirst) {
      auto SW = getSGPRTuplesWeight();
      auto OtherSW = O.getSGPRTuplesWeight();
      if (SW != OtherSW)
        return SW < OtherSW;
    } else {
      auto VW = getVGPRTuplesWeight();
      auto OtherVW = O.getVGPRTuplesWeight();
      if (VW != OtherVW)
        return VW < OtherVW;
    }
  }

  // Give final precedence to lower general RP.
  return SGPRImportant ? (getSGPRNum() < O.getSGPRNum()):
                         (getVGPRNum(ST.hasGFX90AInsts()) <
                          O.getVGPRNum(ST.hasGFX90AInsts()));
}

Printable llvm::print(const GCNRegPressure &RP, const GCNSubtarget *ST) {
  return Printable([&RP, ST](raw_ostream &OS) {
    OS << "VGPRs: " << RP.Value[GCNRegPressure::VGPR32] << ' '
       << "AGPRs: " << RP.getAGPRNum();
    if (ST)
      OS << "(O"
         << ST->getOccupancyWithNumVGPRs(RP.getVGPRNum(ST->hasGFX90AInsts()))
         << ')';
    OS << ", SGPRs: " << RP.getSGPRNum();
    if (ST)
      OS << "(O" << ST->getOccupancyWithNumSGPRs(RP.getSGPRNum()) << ')';
    OS << ", LVGPR WT: " << RP.getVGPRTuplesWeight()
       << ", LSGPR WT: " << RP.getSGPRTuplesWeight();
    if (ST)
      OS << " -> Occ: " << RP.getOccupancy(*ST);
    OS << '\n';
  });
}

static LaneBitmask getDefRegMask(const MachineOperand &MO,
                                 const MachineRegisterInfo &MRI) {
  assert(MO.isDef() && MO.isReg() && MO.getReg().isVirtual());

  // We don't rely on read-undef flag because in case of tentative schedule
  // tracking it isn't set correctly yet. This works correctly however since
  // use mask has been tracked before using LIS.
  return MO.getSubReg() == 0 ?
    MRI.getMaxLaneMaskForVReg(MO.getReg()) :
    MRI.getTargetRegisterInfo()->getSubRegIndexLaneMask(MO.getSubReg());
}

static void
collectVirtualRegUses(SmallVectorImpl<RegisterMaskPair> &RegMaskPairs,
                      const MachineInstr &MI, const LiveIntervals &LIS,
                      const MachineRegisterInfo &MRI) {
  SlotIndex InstrSI;
  for (const auto &MO : MI.operands()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    if (!MO.isUse() || !MO.readsReg())
      continue;

    Register Reg = MO.getReg();
    if (llvm::any_of(RegMaskPairs, [Reg](const RegisterMaskPair &RM) {
          return RM.RegUnit == Reg;
        }))
      continue;

    LaneBitmask UseMask;
    auto &LI = LIS.getInterval(Reg);
    if (!LI.hasSubRanges())
      UseMask = MRI.getMaxLaneMaskForVReg(Reg);
    else {
      // For a tentative schedule LIS isn't updated yet but livemask should
      // remain the same on any schedule. Subreg defs can be reordered but they
      // all must dominate uses anyway.
      if (!InstrSI)
        InstrSI = LIS.getInstructionIndex(*MO.getParent()).getBaseIndex();
      UseMask = getLiveLaneMask(LI, InstrSI, MRI);
    }

    RegMaskPairs.emplace_back(Reg, UseMask);
  }
}

///////////////////////////////////////////////////////////////////////////////
// GCNRPTracker

LaneBitmask llvm::getLiveLaneMask(unsigned Reg, SlotIndex SI,
                                  const LiveIntervals &LIS,
                                  const MachineRegisterInfo &MRI) {
  return getLiveLaneMask(LIS.getInterval(Reg), SI, MRI);
}

LaneBitmask llvm::getLiveLaneMask(const LiveInterval &LI, SlotIndex SI,
                                  const MachineRegisterInfo &MRI) {
  LaneBitmask LiveMask;
  if (LI.hasSubRanges()) {
    for (const auto &S : LI.subranges())
      if (S.liveAt(SI)) {
        LiveMask |= S.LaneMask;
        assert(LiveMask == (LiveMask & MRI.getMaxLaneMaskForVReg(LI.reg())));
      }
  } else if (LI.liveAt(SI)) {
    LiveMask = MRI.getMaxLaneMaskForVReg(LI.reg());
  }
  return LiveMask;
}

GCNRPTracker::LiveRegSet llvm::getLiveRegs(SlotIndex SI,
                                           const LiveIntervals &LIS,
                                           const MachineRegisterInfo &MRI) {
  GCNRPTracker::LiveRegSet LiveRegs;
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    auto Reg = Register::index2VirtReg(I);
    if (!LIS.hasInterval(Reg))
      continue;
    auto LiveMask = getLiveLaneMask(Reg, SI, LIS, MRI);
    if (LiveMask.any())
      LiveRegs[Reg] = LiveMask;
  }
  return LiveRegs;
}

void GCNRPTracker::reset(const MachineInstr &MI,
                         const LiveRegSet *LiveRegsCopy,
                         bool After) {
  const MachineFunction &MF = *MI.getMF();
  MRI = &MF.getRegInfo();
  if (LiveRegsCopy) {
    if (&LiveRegs != LiveRegsCopy)
      LiveRegs = *LiveRegsCopy;
  } else {
    LiveRegs = After ? getLiveRegsAfter(MI, LIS)
                     : getLiveRegsBefore(MI, LIS);
  }

  MaxPressure = CurPressure = getRegPressure(*MRI, LiveRegs);
}

////////////////////////////////////////////////////////////////////////////////
// GCNUpwardRPTracker

void GCNUpwardRPTracker::reset(const MachineRegisterInfo &MRI_,
                               const LiveRegSet &LiveRegs_) {
  MRI = &MRI_;
  LiveRegs = LiveRegs_;
  LastTrackedMI = nullptr;
  MaxPressure = CurPressure = getRegPressure(MRI_, LiveRegs_);
}

void GCNUpwardRPTracker::recede(const MachineInstr &MI) {
  assert(MRI && "call reset first");

  LastTrackedMI = &MI;

  if (MI.isDebugInstr())
    return;

  // Kill all defs.
  GCNRegPressure DefPressure, ECDefPressure;
  bool HasECDefs = false;
  for (const MachineOperand &MO : MI.all_defs()) {
    if (!MO.getReg().isVirtual())
      continue;

    Register Reg = MO.getReg();
    LaneBitmask DefMask = getDefRegMask(MO, *MRI);

    // Treat a def as fully live at the moment of definition: keep a record.
    if (MO.isEarlyClobber()) {
      ECDefPressure.inc(Reg, LaneBitmask::getNone(), DefMask, *MRI);
      HasECDefs = true;
    } else
      DefPressure.inc(Reg, LaneBitmask::getNone(), DefMask, *MRI);

    auto I = LiveRegs.find(Reg);
    if (I == LiveRegs.end())
      continue;

    LaneBitmask &LiveMask = I->second;
    LaneBitmask PrevMask = LiveMask;
    LiveMask &= ~DefMask;
    CurPressure.inc(Reg, PrevMask, LiveMask, *MRI);
    if (LiveMask.none())
      LiveRegs.erase(I);
  }

  // Update MaxPressure with defs pressure.
  DefPressure += CurPressure;
  if (HasECDefs)
    DefPressure += ECDefPressure;
  MaxPressure = max(DefPressure, MaxPressure);

  // Make uses alive.
  SmallVector<RegisterMaskPair, 8> RegUses;
  collectVirtualRegUses(RegUses, MI, LIS, *MRI);
  for (const RegisterMaskPair &U : RegUses) {
    LaneBitmask &LiveMask = LiveRegs[U.RegUnit];
    LaneBitmask PrevMask = LiveMask;
    LiveMask |= U.LaneMask;
    CurPressure.inc(U.RegUnit, PrevMask, LiveMask, *MRI);
  }

  // Update MaxPressure with uses plus early-clobber defs pressure.
  MaxPressure = HasECDefs ? max(CurPressure + ECDefPressure, MaxPressure)
                          : max(CurPressure, MaxPressure);

  assert(CurPressure == getRegPressure(*MRI, LiveRegs));
}

////////////////////////////////////////////////////////////////////////////////
// GCNDownwardRPTracker

bool GCNDownwardRPTracker::reset(const MachineInstr &MI,
                                 const LiveRegSet *LiveRegsCopy) {
  MRI = &MI.getParent()->getParent()->getRegInfo();
  LastTrackedMI = nullptr;
  MBBEnd = MI.getParent()->end();
  NextMI = &MI;
  NextMI = skipDebugInstructionsForward(NextMI, MBBEnd);
  if (NextMI == MBBEnd)
    return false;
  GCNRPTracker::reset(*NextMI, LiveRegsCopy, false);
  return true;
}

bool GCNDownwardRPTracker::advanceBeforeNext() {
  assert(MRI && "call reset first");
  if (!LastTrackedMI)
    return NextMI == MBBEnd;

  assert(NextMI == MBBEnd || !NextMI->isDebugInstr());

  SlotIndex SI = NextMI == MBBEnd
                     ? LIS.getInstructionIndex(*LastTrackedMI).getDeadSlot()
                     : LIS.getInstructionIndex(*NextMI).getBaseIndex();
  assert(SI.isValid());

  // Remove dead registers or mask bits.
  SmallSet<Register, 8> SeenRegs;
  for (auto &MO : LastTrackedMI->operands()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    if (MO.isUse() && !MO.readsReg())
      continue;
    if (!SeenRegs.insert(MO.getReg()).second)
      continue;
    const LiveInterval &LI = LIS.getInterval(MO.getReg());
    if (LI.hasSubRanges()) {
      auto It = LiveRegs.end();
      for (const auto &S : LI.subranges()) {
        if (!S.liveAt(SI)) {
          if (It == LiveRegs.end()) {
            It = LiveRegs.find(MO.getReg());
            if (It == LiveRegs.end())
              llvm_unreachable("register isn't live");
          }
          auto PrevMask = It->second;
          It->second &= ~S.LaneMask;
          CurPressure.inc(MO.getReg(), PrevMask, It->second, *MRI);
        }
      }
      if (It != LiveRegs.end() && It->second.none())
        LiveRegs.erase(It);
    } else if (!LI.liveAt(SI)) {
      auto It = LiveRegs.find(MO.getReg());
      if (It == LiveRegs.end())
        llvm_unreachable("register isn't live");
      CurPressure.inc(MO.getReg(), It->second, LaneBitmask::getNone(), *MRI);
      LiveRegs.erase(It);
    }
  }

  MaxPressure = max(MaxPressure, CurPressure);

  LastTrackedMI = nullptr;

  return NextMI == MBBEnd;
}

void GCNDownwardRPTracker::advanceToNext() {
  LastTrackedMI = &*NextMI++;
  NextMI = skipDebugInstructionsForward(NextMI, MBBEnd);

  // Add new registers or mask bits.
  for (const auto &MO : LastTrackedMI->all_defs()) {
    Register Reg = MO.getReg();
    if (!Reg.isVirtual())
      continue;
    auto &LiveMask = LiveRegs[Reg];
    auto PrevMask = LiveMask;
    LiveMask |= getDefRegMask(MO, *MRI);
    CurPressure.inc(Reg, PrevMask, LiveMask, *MRI);
  }

  MaxPressure = max(MaxPressure, CurPressure);
}

bool GCNDownwardRPTracker::advance() {
  if (NextMI == MBBEnd)
    return false;
  advanceBeforeNext();
  advanceToNext();
  return true;
}

bool GCNDownwardRPTracker::advance(MachineBasicBlock::const_iterator End) {
  while (NextMI != End)
    if (!advance()) return false;
  return true;
}

bool GCNDownwardRPTracker::advance(MachineBasicBlock::const_iterator Begin,
                                   MachineBasicBlock::const_iterator End,
                                   const LiveRegSet *LiveRegsCopy) {
  reset(*Begin, LiveRegsCopy);
  return advance(End);
}

Printable llvm::reportMismatch(const GCNRPTracker::LiveRegSet &LISLR,
                               const GCNRPTracker::LiveRegSet &TrackedLR,
                               const TargetRegisterInfo *TRI, StringRef Pfx) {
  return Printable([&LISLR, &TrackedLR, TRI, Pfx](raw_ostream &OS) {
    for (auto const &P : TrackedLR) {
      auto I = LISLR.find(P.first);
      if (I == LISLR.end()) {
        OS << Pfx << printReg(P.first, TRI) << ":L" << PrintLaneMask(P.second)
           << " isn't found in LIS reported set\n";
      } else if (I->second != P.second) {
        OS << Pfx << printReg(P.first, TRI)
           << " masks doesn't match: LIS reported " << PrintLaneMask(I->second)
           << ", tracked " << PrintLaneMask(P.second) << '\n';
      }
    }
    for (auto const &P : LISLR) {
      auto I = TrackedLR.find(P.first);
      if (I == TrackedLR.end()) {
        OS << Pfx << printReg(P.first, TRI) << ":L" << PrintLaneMask(P.second)
           << " isn't found in tracked set\n";
      }
    }
  });
}

bool GCNUpwardRPTracker::isValid() const {
  const auto &SI = LIS.getInstructionIndex(*LastTrackedMI).getBaseIndex();
  const auto LISLR = llvm::getLiveRegs(SI, LIS, *MRI);
  const auto &TrackedLR = LiveRegs;

  if (!isEqual(LISLR, TrackedLR)) {
    dbgs() << "\nGCNUpwardRPTracker error: Tracked and"
              " LIS reported livesets mismatch:\n"
           << print(LISLR, *MRI);
    reportMismatch(LISLR, TrackedLR, MRI->getTargetRegisterInfo());
    return false;
  }

  auto LISPressure = getRegPressure(*MRI, LISLR);
  if (LISPressure != CurPressure) {
    dbgs() << "GCNUpwardRPTracker error: Pressure sets different\nTracked: "
           << print(CurPressure) << "LIS rpt: " << print(LISPressure);
    return false;
  }
  return true;
}

Printable llvm::print(const GCNRPTracker::LiveRegSet &LiveRegs,
                      const MachineRegisterInfo &MRI) {
  return Printable([&LiveRegs, &MRI](raw_ostream &OS) {
    const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
    for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
      Register Reg = Register::index2VirtReg(I);
      auto It = LiveRegs.find(Reg);
      if (It != LiveRegs.end() && It->second.any())
        OS << ' ' << printVRegOrUnit(Reg, TRI) << ':'
           << PrintLaneMask(It->second);
    }
    OS << '\n';
  });
}

void GCNRegPressure::dump() const { dbgs() << print(*this); }

static cl::opt<bool> UseDownwardTracker(
    "amdgpu-print-rp-downward",
    cl::desc("Use GCNDownwardRPTracker for GCNRegPressurePrinter pass"),
    cl::init(false), cl::Hidden);

char llvm::GCNRegPressurePrinter::ID = 0;
char &llvm::GCNRegPressurePrinterID = GCNRegPressurePrinter::ID;

INITIALIZE_PASS(GCNRegPressurePrinter, "amdgpu-print-rp", "", true, true)

// Return lanemask of Reg's subregs that are live-through at [Begin, End] and
// are fully covered by Mask.
static LaneBitmask
getRegLiveThroughMask(const MachineRegisterInfo &MRI, const LiveIntervals &LIS,
                      Register Reg, SlotIndex Begin, SlotIndex End,
                      LaneBitmask Mask = LaneBitmask::getAll()) {

  auto IsInOneSegment = [Begin, End](const LiveRange &LR) -> bool {
    auto *Segment = LR.getSegmentContaining(Begin);
    return Segment && Segment->contains(End);
  };

  LaneBitmask LiveThroughMask;
  const LiveInterval &LI = LIS.getInterval(Reg);
  if (LI.hasSubRanges()) {
    for (auto &SR : LI.subranges()) {
      if ((SR.LaneMask & Mask) == SR.LaneMask && IsInOneSegment(SR))
        LiveThroughMask |= SR.LaneMask;
    }
  } else {
    LaneBitmask RegMask = MRI.getMaxLaneMaskForVReg(Reg);
    if ((RegMask & Mask) == RegMask && IsInOneSegment(LI))
      LiveThroughMask = RegMask;
  }

  return LiveThroughMask;
}

bool GCNRegPressurePrinter::runOnMachineFunction(MachineFunction &MF) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
  const LiveIntervals &LIS = getAnalysis<LiveIntervalsWrapperPass>().getLIS();

  auto &OS = dbgs();

// Leading spaces are important for YAML syntax.
#define PFX "  "

  OS << "---\nname: " << MF.getName() << "\nbody:             |\n";

  auto printRP = [](const GCNRegPressure &RP) {
    return Printable([&RP](raw_ostream &OS) {
      OS << format(PFX "  %-5d", RP.getSGPRNum())
         << format(" %-5d", RP.getVGPRNum(false));
    });
  };

  auto ReportLISMismatchIfAny = [&](const GCNRPTracker::LiveRegSet &TrackedLR,
                                    const GCNRPTracker::LiveRegSet &LISLR) {
    if (LISLR != TrackedLR) {
      OS << PFX "  mis LIS: " << llvm::print(LISLR, MRI)
         << reportMismatch(LISLR, TrackedLR, TRI, PFX "    ");
    }
  };

  // Register pressure before and at an instruction (in program order).
  SmallVector<std::pair<GCNRegPressure, GCNRegPressure>, 16> RP;

  for (auto &MBB : MF) {
    RP.clear();
    RP.reserve(MBB.size());

    OS << PFX;
    MBB.printName(OS);
    OS << ":\n";

    SlotIndex MBBStartSlot = LIS.getSlotIndexes()->getMBBStartIdx(&MBB);
    SlotIndex MBBEndSlot = LIS.getSlotIndexes()->getMBBEndIdx(&MBB);

    GCNRPTracker::LiveRegSet LiveIn, LiveOut;
    GCNRegPressure RPAtMBBEnd;

    if (UseDownwardTracker) {
      if (MBB.empty()) {
        LiveIn = LiveOut = getLiveRegs(MBBStartSlot, LIS, MRI);
        RPAtMBBEnd = getRegPressure(MRI, LiveIn);
      } else {
        GCNDownwardRPTracker RPT(LIS);
        RPT.reset(MBB.front());

        LiveIn = RPT.getLiveRegs();

        while (!RPT.advanceBeforeNext()) {
          GCNRegPressure RPBeforeMI = RPT.getPressure();
          RPT.advanceToNext();
          RP.emplace_back(RPBeforeMI, RPT.getPressure());
        }

        LiveOut = RPT.getLiveRegs();
        RPAtMBBEnd = RPT.getPressure();
      }
    } else {
      GCNUpwardRPTracker RPT(LIS);
      RPT.reset(MRI, MBBEndSlot);

      LiveOut = RPT.getLiveRegs();
      RPAtMBBEnd = RPT.getPressure();

      for (auto &MI : reverse(MBB)) {
        RPT.resetMaxPressure();
        RPT.recede(MI);
        if (!MI.isDebugInstr())
          RP.emplace_back(RPT.getPressure(), RPT.getMaxPressure());
      }

      LiveIn = RPT.getLiveRegs();
    }

    OS << PFX "  Live-in: " << llvm::print(LiveIn, MRI);
    if (!UseDownwardTracker)
      ReportLISMismatchIfAny(LiveIn, getLiveRegs(MBBStartSlot, LIS, MRI));

    OS << PFX "  SGPR  VGPR\n";
    int I = 0;
    for (auto &MI : MBB) {
      if (!MI.isDebugInstr()) {
        auto &[RPBeforeInstr, RPAtInstr] =
            RP[UseDownwardTracker ? I : (RP.size() - 1 - I)];
        ++I;
        OS << printRP(RPBeforeInstr) << '\n' << printRP(RPAtInstr) << "  ";
      } else
        OS << PFX "               ";
      MI.print(OS);
    }
    OS << printRP(RPAtMBBEnd) << '\n';

    OS << PFX "  Live-out:" << llvm::print(LiveOut, MRI);
    if (UseDownwardTracker)
      ReportLISMismatchIfAny(LiveOut, getLiveRegs(MBBEndSlot, LIS, MRI));

    GCNRPTracker::LiveRegSet LiveThrough;
    for (auto [Reg, Mask] : LiveIn) {
      LaneBitmask MaskIntersection = Mask & LiveOut.lookup(Reg);
      if (MaskIntersection.any()) {
        LaneBitmask LTMask = getRegLiveThroughMask(
            MRI, LIS, Reg, MBBStartSlot, MBBEndSlot, MaskIntersection);
        if (LTMask.any())
          LiveThrough[Reg] = LTMask;
      }
    }
    OS << PFX "  Live-thr:" << llvm::print(LiveThrough, MRI);
    OS << printRP(getRegPressure(MRI, LiveThrough)) << '\n';
  }
  OS << "...\n";
  return false;

#undef PFX
}