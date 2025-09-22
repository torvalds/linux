//===- GCNRegPressure.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the GCNRegPressure class, which tracks registry pressure
/// by bookkeeping number of SGPR/VGPRs used, weights for large SGPR/VGPRs. It
/// also implements a compare function, which compares different register
/// pressures, and declares one with max occupancy as winner.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_GCNREGPRESSURE_H
#define LLVM_LIB_TARGET_AMDGPU_GCNREGPRESSURE_H

#include "GCNSubtarget.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include <algorithm>

namespace llvm {

class MachineRegisterInfo;
class raw_ostream;
class SlotIndex;

struct GCNRegPressure {
  enum RegKind {
    SGPR32,
    SGPR_TUPLE,
    VGPR32,
    VGPR_TUPLE,
    AGPR32,
    AGPR_TUPLE,
    TOTAL_KINDS
  };

  GCNRegPressure() {
    clear();
  }

  bool empty() const { return getSGPRNum() == 0 && getVGPRNum(false) == 0; }

  void clear() { std::fill(&Value[0], &Value[TOTAL_KINDS], 0); }

  unsigned getSGPRNum() const { return Value[SGPR32]; }
  unsigned getVGPRNum(bool UnifiedVGPRFile) const {
    if (UnifiedVGPRFile) {
      return Value[AGPR32] ? alignTo(Value[VGPR32], 4) + Value[AGPR32]
                           : Value[VGPR32] + Value[AGPR32];
    }
    return std::max(Value[VGPR32], Value[AGPR32]);
  }
  unsigned getAGPRNum() const { return Value[AGPR32]; }

  unsigned getVGPRTuplesWeight() const { return std::max(Value[VGPR_TUPLE],
                                                         Value[AGPR_TUPLE]); }
  unsigned getSGPRTuplesWeight() const { return Value[SGPR_TUPLE]; }

  unsigned getOccupancy(const GCNSubtarget &ST) const {
    return std::min(ST.getOccupancyWithNumSGPRs(getSGPRNum()),
             ST.getOccupancyWithNumVGPRs(getVGPRNum(ST.hasGFX90AInsts())));
  }

  void inc(unsigned Reg,
           LaneBitmask PrevMask,
           LaneBitmask NewMask,
           const MachineRegisterInfo &MRI);

  bool higherOccupancy(const GCNSubtarget &ST, const GCNRegPressure& O) const {
    return getOccupancy(ST) > O.getOccupancy(ST);
  }

  /// Compares \p this GCNRegpressure to \p O, returning true if \p this is
  /// less. Since GCNRegpressure contains different types of pressures, and due
  /// to target-specific pecularities (e.g. we care about occupancy rather than
  /// raw register usage), we determine if \p this GCNRegPressure is less than
  /// \p O based on the following tiered comparisons (in order order of
  /// precedence):
  /// 1. Better occupancy
  /// 2. Less spilling (first preference to VGPR spills, then to SGPR spills)
  /// 3. Less tuple register pressure (first preference to VGPR tuples if we
  /// determine that SGPR pressure is not important)
  /// 4. Less raw register pressure (first preference to VGPR tuples if we
  /// determine that SGPR pressure is not important)
  bool less(const MachineFunction &MF, const GCNRegPressure &O,
            unsigned MaxOccupancy = std::numeric_limits<unsigned>::max()) const;

  bool operator==(const GCNRegPressure &O) const {
    return std::equal(&Value[0], &Value[TOTAL_KINDS], O.Value);
  }

  bool operator!=(const GCNRegPressure &O) const {
    return !(*this == O);
  }

  GCNRegPressure &operator+=(const GCNRegPressure &RHS) {
    for (unsigned I = 0; I < TOTAL_KINDS; ++I)
      Value[I] += RHS.Value[I];
    return *this;
  }

  GCNRegPressure &operator-=(const GCNRegPressure &RHS) {
    for (unsigned I = 0; I < TOTAL_KINDS; ++I)
      Value[I] -= RHS.Value[I];
    return *this;
  }

  void dump() const;

private:
  unsigned Value[TOTAL_KINDS];

  static unsigned getRegKind(Register Reg, const MachineRegisterInfo &MRI);

  friend GCNRegPressure max(const GCNRegPressure &P1,
                            const GCNRegPressure &P2);

  friend Printable print(const GCNRegPressure &RP, const GCNSubtarget *ST);
};

inline GCNRegPressure max(const GCNRegPressure &P1, const GCNRegPressure &P2) {
  GCNRegPressure Res;
  for (unsigned I = 0; I < GCNRegPressure::TOTAL_KINDS; ++I)
    Res.Value[I] = std::max(P1.Value[I], P2.Value[I]);
  return Res;
}

inline GCNRegPressure operator+(const GCNRegPressure &P1,
                                const GCNRegPressure &P2) {
  GCNRegPressure Sum = P1;
  Sum += P2;
  return Sum;
}

inline GCNRegPressure operator-(const GCNRegPressure &P1,
                                const GCNRegPressure &P2) {
  GCNRegPressure Diff = P1;
  Diff -= P2;
  return Diff;
}

class GCNRPTracker {
public:
  using LiveRegSet = DenseMap<unsigned, LaneBitmask>;

protected:
  const LiveIntervals &LIS;
  LiveRegSet LiveRegs;
  GCNRegPressure CurPressure, MaxPressure;
  const MachineInstr *LastTrackedMI = nullptr;
  mutable const MachineRegisterInfo *MRI = nullptr;

  GCNRPTracker(const LiveIntervals &LIS_) : LIS(LIS_) {}

  void reset(const MachineInstr &MI, const LiveRegSet *LiveRegsCopy,
             bool After);

public:
  // live regs for the current state
  const decltype(LiveRegs) &getLiveRegs() const { return LiveRegs; }
  const MachineInstr *getLastTrackedMI() const { return LastTrackedMI; }

  void clearMaxPressure() { MaxPressure.clear(); }

  GCNRegPressure getPressure() const { return CurPressure; }

  decltype(LiveRegs) moveLiveRegs() {
    return std::move(LiveRegs);
  }
};

GCNRPTracker::LiveRegSet getLiveRegs(SlotIndex SI, const LiveIntervals &LIS,
                                     const MachineRegisterInfo &MRI);

class GCNUpwardRPTracker : public GCNRPTracker {
public:
  GCNUpwardRPTracker(const LiveIntervals &LIS_) : GCNRPTracker(LIS_) {}

  // reset tracker and set live register set to the specified value.
  void reset(const MachineRegisterInfo &MRI_, const LiveRegSet &LiveRegs_);

  // reset tracker at the specified slot index.
  void reset(const MachineRegisterInfo &MRI, SlotIndex SI) {
    reset(MRI, llvm::getLiveRegs(SI, LIS, MRI));
  }

  // reset tracker to the end of the MBB.
  void reset(const MachineBasicBlock &MBB) {
    reset(MBB.getParent()->getRegInfo(),
          LIS.getSlotIndexes()->getMBBEndIdx(&MBB));
  }

  // reset tracker to the point just after MI (in program order).
  void reset(const MachineInstr &MI) {
    reset(MI.getMF()->getRegInfo(), LIS.getInstructionIndex(MI).getDeadSlot());
  }

  // move to the state just before the MI (in program order).
  void recede(const MachineInstr &MI);

  // checks whether the tracker's state after receding MI corresponds
  // to reported by LIS.
  bool isValid() const;

  const GCNRegPressure &getMaxPressure() const { return MaxPressure; }

  void resetMaxPressure() { MaxPressure = CurPressure; }

  GCNRegPressure getMaxPressureAndReset() {
    GCNRegPressure RP = MaxPressure;
    resetMaxPressure();
    return RP;
  }
};

class GCNDownwardRPTracker : public GCNRPTracker {
  // Last position of reset or advanceBeforeNext
  MachineBasicBlock::const_iterator NextMI;

  MachineBasicBlock::const_iterator MBBEnd;

public:
  GCNDownwardRPTracker(const LiveIntervals &LIS_) : GCNRPTracker(LIS_) {}

  MachineBasicBlock::const_iterator getNext() const { return NextMI; }

  // Return MaxPressure and clear it.
  GCNRegPressure moveMaxPressure() {
    auto Res = MaxPressure;
    MaxPressure.clear();
    return Res;
  }

  // Reset tracker to the point before the MI
  // filling live regs upon this point using LIS.
  // Returns false if block is empty except debug values.
  bool reset(const MachineInstr &MI, const LiveRegSet *LiveRegs = nullptr);

  // Move to the state right before the next MI or after the end of MBB.
  // Returns false if reached end of the block.
  bool advanceBeforeNext();

  // Move to the state at the MI, advanceBeforeNext has to be called first.
  void advanceToNext();

  // Move to the state at the next MI. Returns false if reached end of block.
  bool advance();

  // Advance instructions until before End.
  bool advance(MachineBasicBlock::const_iterator End);

  // Reset to Begin and advance to End.
  bool advance(MachineBasicBlock::const_iterator Begin,
               MachineBasicBlock::const_iterator End,
               const LiveRegSet *LiveRegsCopy = nullptr);
};

LaneBitmask getLiveLaneMask(unsigned Reg,
                            SlotIndex SI,
                            const LiveIntervals &LIS,
                            const MachineRegisterInfo &MRI);

LaneBitmask getLiveLaneMask(const LiveInterval &LI, SlotIndex SI,
                            const MachineRegisterInfo &MRI);

GCNRPTracker::LiveRegSet getLiveRegs(SlotIndex SI, const LiveIntervals &LIS,
                                     const MachineRegisterInfo &MRI);

/// creates a map MachineInstr -> LiveRegSet
/// R - range of iterators on instructions
/// After - upon entry or exit of every instruction
/// Note: there is no entry in the map for instructions with empty live reg set
/// Complexity = O(NumVirtRegs * averageLiveRangeSegmentsPerReg * lg(R))
template <typename Range>
DenseMap<MachineInstr*, GCNRPTracker::LiveRegSet>
getLiveRegMap(Range &&R, bool After, LiveIntervals &LIS) {
  std::vector<SlotIndex> Indexes;
  Indexes.reserve(std::distance(R.begin(), R.end()));
  auto &SII = *LIS.getSlotIndexes();
  for (MachineInstr *I : R) {
    auto SI = SII.getInstructionIndex(*I);
    Indexes.push_back(After ? SI.getDeadSlot() : SI.getBaseIndex());
  }
  llvm::sort(Indexes);

  auto &MRI = (*R.begin())->getParent()->getParent()->getRegInfo();
  DenseMap<MachineInstr *, GCNRPTracker::LiveRegSet> LiveRegMap;
  SmallVector<SlotIndex, 32> LiveIdxs, SRLiveIdxs;
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    auto Reg = Register::index2VirtReg(I);
    if (!LIS.hasInterval(Reg))
      continue;
    auto &LI = LIS.getInterval(Reg);
    LiveIdxs.clear();
    if (!LI.findIndexesLiveAt(Indexes, std::back_inserter(LiveIdxs)))
      continue;
    if (!LI.hasSubRanges()) {
      for (auto SI : LiveIdxs)
        LiveRegMap[SII.getInstructionFromIndex(SI)][Reg] =
          MRI.getMaxLaneMaskForVReg(Reg);
    } else
      for (const auto &S : LI.subranges()) {
        // constrain search for subranges by indexes live at main range
        SRLiveIdxs.clear();
        S.findIndexesLiveAt(LiveIdxs, std::back_inserter(SRLiveIdxs));
        for (auto SI : SRLiveIdxs)
          LiveRegMap[SII.getInstructionFromIndex(SI)][Reg] |= S.LaneMask;
      }
  }
  return LiveRegMap;
}

inline GCNRPTracker::LiveRegSet getLiveRegsAfter(const MachineInstr &MI,
                                                 const LiveIntervals &LIS) {
  return getLiveRegs(LIS.getInstructionIndex(MI).getDeadSlot(), LIS,
                     MI.getParent()->getParent()->getRegInfo());
}

inline GCNRPTracker::LiveRegSet getLiveRegsBefore(const MachineInstr &MI,
                                                  const LiveIntervals &LIS) {
  return getLiveRegs(LIS.getInstructionIndex(MI).getBaseIndex(), LIS,
                     MI.getParent()->getParent()->getRegInfo());
}

template <typename Range>
GCNRegPressure getRegPressure(const MachineRegisterInfo &MRI,
                              Range &&LiveRegs) {
  GCNRegPressure Res;
  for (const auto &RM : LiveRegs)
    Res.inc(RM.first, LaneBitmask::getNone(), RM.second, MRI);
  return Res;
}

bool isEqual(const GCNRPTracker::LiveRegSet &S1,
             const GCNRPTracker::LiveRegSet &S2);

Printable print(const GCNRegPressure &RP, const GCNSubtarget *ST = nullptr);

Printable print(const GCNRPTracker::LiveRegSet &LiveRegs,
                const MachineRegisterInfo &MRI);

Printable reportMismatch(const GCNRPTracker::LiveRegSet &LISLR,
                         const GCNRPTracker::LiveRegSet &TrackedL,
                         const TargetRegisterInfo *TRI, StringRef Pfx = "  ");

struct GCNRegPressurePrinter : public MachineFunctionPass {
  static char ID;

public:
  GCNRegPressurePrinter() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_GCNREGPRESSURE_H
