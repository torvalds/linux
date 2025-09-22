//===-------------- GCNRewritePartialRegUses.cpp --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// RenameIndependentSubregs pass leaves large partially used super registers,
/// for example:
///   undef %0.sub4:VReg_1024 = ...
///   %0.sub5:VReg_1024 = ...
///   %0.sub6:VReg_1024 = ...
///   %0.sub7:VReg_1024 = ...
///   use %0.sub4_sub5_sub6_sub7
///   use %0.sub6_sub7
///
/// GCNRewritePartialRegUses goes right after RenameIndependentSubregs and
/// rewrites such partially used super registers with registers of minimal size:
///   undef %0.sub0:VReg_128 = ...
///   %0.sub1:VReg_128 = ...
///   %0.sub2:VReg_128 = ...
///   %0.sub3:VReg_128 = ...
///   use %0.sub0_sub1_sub2_sub3
///   use %0.sub2_sub3
///
/// This allows to avoid subreg lanemasks tracking during register pressure
/// calculation and creates more possibilities for the code unaware of lanemasks
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIRegisterInfo.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "rewrite-partial-reg-uses"

namespace {

class GCNRewritePartialRegUses : public MachineFunctionPass {
public:
  static char ID;
  GCNRewritePartialRegUses() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Rewrite Partial Register Uses";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  MachineRegisterInfo *MRI;
  const SIRegisterInfo *TRI;
  const TargetInstrInfo *TII;
  LiveIntervals *LIS;

  /// Rewrite partially used register Reg by shifting all its subregisters to
  /// the right and replacing the original register with a register of minimal
  /// size. Return true if the change has been made.
  bool rewriteReg(Register Reg) const;

  /// Value type for SubRegMap below.
  struct SubRegInfo {
    /// Register class required to hold the value stored in the SubReg.
    const TargetRegisterClass *RC;

    /// Index for the right-shifted subregister. If 0 this is the "covering"
    /// subreg i.e. subreg that covers all others. Covering subreg becomes the
    /// whole register after the replacement.
    unsigned SubReg = AMDGPU::NoSubRegister;
    SubRegInfo(const TargetRegisterClass *RC_ = nullptr) : RC(RC_) {}
  };

  /// Map OldSubReg -> { RC, NewSubReg }. Used as in/out container.
  using SubRegMap = SmallDenseMap<unsigned, SubRegInfo>;

  /// Given register class RC and the set of used subregs as keys in the SubRegs
  /// map return new register class and indexes of right-shifted subregs as
  /// values in SubRegs map such that the resulting regclass would contain
  /// registers of minimal size.
  const TargetRegisterClass *getMinSizeReg(const TargetRegisterClass *RC,
                                           SubRegMap &SubRegs) const;

  /// Given regclass RC and pairs of [OldSubReg, SubRegRC] in SubRegs try to
  /// find new regclass such that:
  ///   1. It has subregs obtained by shifting each OldSubReg by RShift number
  ///      of bits to the right. Every "shifted" subreg should have the same
  ///      SubRegRC. If CoverSubregIdx is not zero it's a subreg that "covers"
  ///      all other subregs in pairs. Basically such subreg becomes a whole
  ///      register.
  ///   2. Resulting register class contains registers of minimal size but not
  ///      less than RegNumBits.
  ///
  /// SubRegs is map of OldSubReg -> [SubRegRC, NewSubReg] and is used as in/out
  /// parameter:
  ///   OldSubReg - input parameter,
  ///   SubRegRC  - input parameter (cannot be null),
  ///   NewSubReg - output, contains shifted subregs on return.
  const TargetRegisterClass *
  getRegClassWithShiftedSubregs(const TargetRegisterClass *RC, unsigned RShift,
                                unsigned RegNumBits, unsigned CoverSubregIdx,
                                SubRegMap &SubRegs) const;

  /// Update live intervals after rewriting OldReg to NewReg with SubRegs map
  /// describing OldSubReg -> NewSubReg mapping.
  void updateLiveIntervals(Register OldReg, Register NewReg,
                           SubRegMap &SubRegs) const;

  /// Helper methods.

  /// Return reg class expected by a MO's parent instruction for a given MO.
  const TargetRegisterClass *getOperandRegClass(MachineOperand &MO) const;

  /// Find right-shifted by RShift amount version of the SubReg if it exists,
  /// return 0 otherwise.
  unsigned shiftSubReg(unsigned SubReg, unsigned RShift) const;

  /// Find subreg index with a given Offset and Size, return 0 if there is no
  /// such subregister index. The result is cached in SubRegs data-member.
  unsigned getSubReg(unsigned Offset, unsigned Size) const;

  /// Cache for getSubReg method: {Offset, Size} -> SubReg index.
  mutable SmallDenseMap<std::pair<unsigned, unsigned>, unsigned> SubRegs;

  /// Return bit mask that contains all register classes that are projected into
  /// RC by SubRegIdx. The result is cached in SuperRegMasks data-member.
  const uint32_t *getSuperRegClassMask(const TargetRegisterClass *RC,
                                       unsigned SubRegIdx) const;

  /// Cache for getSuperRegClassMask method: { RC, SubRegIdx } -> Class bitmask.
  mutable SmallDenseMap<std::pair<const TargetRegisterClass *, unsigned>,
                        const uint32_t *>
      SuperRegMasks;

  /// Return bitmask containing all allocatable register classes with registers
  /// aligned at AlignNumBits. The result is cached in
  /// AllocatableAndAlignedRegClassMasks data-member.
  const BitVector &
  getAllocatableAndAlignedRegClassMask(unsigned AlignNumBits) const;

  /// Cache for getAllocatableAndAlignedRegClassMask method:
  ///   AlignNumBits -> Class bitmask.
  mutable SmallDenseMap<unsigned, BitVector> AllocatableAndAlignedRegClassMasks;
};

} // end anonymous namespace

// TODO: move this to the tablegen and use binary search by Offset.
unsigned GCNRewritePartialRegUses::getSubReg(unsigned Offset,
                                             unsigned Size) const {
  const auto [I, Inserted] = SubRegs.try_emplace({Offset, Size}, 0);
  if (Inserted) {
    for (unsigned Idx = 1, E = TRI->getNumSubRegIndices(); Idx < E; ++Idx) {
      if (TRI->getSubRegIdxOffset(Idx) == Offset &&
          TRI->getSubRegIdxSize(Idx) == Size) {
        I->second = Idx;
        break;
      }
    }
  }
  return I->second;
}

unsigned GCNRewritePartialRegUses::shiftSubReg(unsigned SubReg,
                                               unsigned RShift) const {
  unsigned Offset = TRI->getSubRegIdxOffset(SubReg) - RShift;
  return getSubReg(Offset, TRI->getSubRegIdxSize(SubReg));
}

const uint32_t *
GCNRewritePartialRegUses::getSuperRegClassMask(const TargetRegisterClass *RC,
                                               unsigned SubRegIdx) const {
  const auto [I, Inserted] =
      SuperRegMasks.try_emplace({RC, SubRegIdx}, nullptr);
  if (Inserted) {
    for (SuperRegClassIterator RCI(RC, TRI); RCI.isValid(); ++RCI) {
      if (RCI.getSubReg() == SubRegIdx) {
        I->second = RCI.getMask();
        break;
      }
    }
  }
  return I->second;
}

const BitVector &GCNRewritePartialRegUses::getAllocatableAndAlignedRegClassMask(
    unsigned AlignNumBits) const {
  const auto [I, Inserted] =
      AllocatableAndAlignedRegClassMasks.try_emplace(AlignNumBits);
  if (Inserted) {
    BitVector &BV = I->second;
    BV.resize(TRI->getNumRegClasses());
    for (unsigned ClassID = 0; ClassID < TRI->getNumRegClasses(); ++ClassID) {
      auto *RC = TRI->getRegClass(ClassID);
      if (RC->isAllocatable() && TRI->isRegClassAligned(RC, AlignNumBits))
        BV.set(ClassID);
    }
  }
  return I->second;
}

const TargetRegisterClass *
GCNRewritePartialRegUses::getRegClassWithShiftedSubregs(
    const TargetRegisterClass *RC, unsigned RShift, unsigned RegNumBits,
    unsigned CoverSubregIdx, SubRegMap &SubRegs) const {

  unsigned RCAlign = TRI->getRegClassAlignmentNumBits(RC);
  LLVM_DEBUG(dbgs() << "  Shift " << RShift << ", reg align " << RCAlign
                    << '\n');

  BitVector ClassMask(getAllocatableAndAlignedRegClassMask(RCAlign));
  for (auto &[OldSubReg, SRI] : SubRegs) {
    auto &[SubRegRC, NewSubReg] = SRI;
    assert(SubRegRC);

    LLVM_DEBUG(dbgs() << "  " << TRI->getSubRegIndexName(OldSubReg) << ':'
                      << TRI->getRegClassName(SubRegRC)
                      << (SubRegRC->isAllocatable() ? "" : " not alloc")
                      << " -> ");

    if (OldSubReg == CoverSubregIdx) {
      // Covering subreg will become a full register, RC should be allocatable.
      assert(SubRegRC->isAllocatable());
      NewSubReg = AMDGPU::NoSubRegister;
      LLVM_DEBUG(dbgs() << "whole reg");
    } else {
      NewSubReg = shiftSubReg(OldSubReg, RShift);
      if (!NewSubReg) {
        LLVM_DEBUG(dbgs() << "none\n");
        return nullptr;
      }
      LLVM_DEBUG(dbgs() << TRI->getSubRegIndexName(NewSubReg));
    }

    const uint32_t *Mask = NewSubReg ? getSuperRegClassMask(SubRegRC, NewSubReg)
                                     : SubRegRC->getSubClassMask();
    if (!Mask)
      llvm_unreachable("no register class mask?");

    ClassMask.clearBitsNotInMask(Mask);
    // Don't try to early exit because checking if ClassMask has set bits isn't
    // that cheap and we expect it to pass in most cases.
    LLVM_DEBUG(dbgs() << ", num regclasses " << ClassMask.count() << '\n');
  }

  // ClassMask is the set of all register classes such that each class is
  // allocatable, aligned, has all shifted subregs and each subreg has required
  // register class (see SubRegRC above). Now select first (that is largest)
  // register class with registers of minimal but not less than RegNumBits size.
  // We have to check register size because we may encounter classes of smaller
  // registers like VReg_1 in some situations.
  const TargetRegisterClass *MinRC = nullptr;
  unsigned MinNumBits = std::numeric_limits<unsigned>::max();
  for (unsigned ClassID : ClassMask.set_bits()) {
    auto *RC = TRI->getRegClass(ClassID);
    unsigned NumBits = TRI->getRegSizeInBits(*RC);
    if (NumBits < MinNumBits && NumBits >= RegNumBits) {
      MinNumBits = NumBits;
      MinRC = RC;
    }
    if (MinNumBits == RegNumBits)
      break;
  }
#ifndef NDEBUG
  if (MinRC) {
    assert(MinRC->isAllocatable() && TRI->isRegClassAligned(MinRC, RCAlign));
    for (auto [SubReg, SRI] : SubRegs)
      // Check that all registers in MinRC support SRI.SubReg subregister.
      assert(MinRC == TRI->getSubClassWithSubReg(MinRC, SRI.SubReg));
  }
#endif
  // There might be zero RShift - in this case we just trying to find smaller
  // register.
  return (MinRC != RC || RShift != 0) ? MinRC : nullptr;
}

const TargetRegisterClass *
GCNRewritePartialRegUses::getMinSizeReg(const TargetRegisterClass *RC,
                                        SubRegMap &SubRegs) const {
  unsigned CoverSubreg = AMDGPU::NoSubRegister;
  unsigned Offset = std::numeric_limits<unsigned>::max();
  unsigned End = 0;
  for (auto [SubReg, SRI] : SubRegs) {
    unsigned SubRegOffset = TRI->getSubRegIdxOffset(SubReg);
    unsigned SubRegEnd = SubRegOffset + TRI->getSubRegIdxSize(SubReg);
    if (SubRegOffset < Offset) {
      Offset = SubRegOffset;
      CoverSubreg = AMDGPU::NoSubRegister;
    }
    if (SubRegEnd > End) {
      End = SubRegEnd;
      CoverSubreg = AMDGPU::NoSubRegister;
    }
    if (SubRegOffset == Offset && SubRegEnd == End)
      CoverSubreg = SubReg;
  }
  // If covering subreg is found shift everything so the covering subreg would
  // be in the rightmost position.
  if (CoverSubreg != AMDGPU::NoSubRegister)
    return getRegClassWithShiftedSubregs(RC, Offset, End - Offset, CoverSubreg,
                                         SubRegs);

  // Otherwise find subreg with maximum required alignment and shift it and all
  // other subregs to the rightmost possible position with respect to the
  // alignment.
  unsigned MaxAlign = 0;
  for (auto [SubReg, SRI] : SubRegs)
    MaxAlign = std::max(MaxAlign, TRI->getSubRegAlignmentNumBits(RC, SubReg));

  unsigned FirstMaxAlignedSubRegOffset = std::numeric_limits<unsigned>::max();
  for (auto [SubReg, SRI] : SubRegs) {
    if (TRI->getSubRegAlignmentNumBits(RC, SubReg) != MaxAlign)
      continue;
    FirstMaxAlignedSubRegOffset =
        std::min(FirstMaxAlignedSubRegOffset, TRI->getSubRegIdxOffset(SubReg));
    if (FirstMaxAlignedSubRegOffset == Offset)
      break;
  }

  unsigned NewOffsetOfMaxAlignedSubReg =
      alignTo(FirstMaxAlignedSubRegOffset - Offset, MaxAlign);

  if (NewOffsetOfMaxAlignedSubReg > FirstMaxAlignedSubRegOffset)
    llvm_unreachable("misaligned subreg");

  unsigned RShift = FirstMaxAlignedSubRegOffset - NewOffsetOfMaxAlignedSubReg;
  return getRegClassWithShiftedSubregs(RC, RShift, End - RShift, 0, SubRegs);
}

// Only the subrange's lanemasks of the original interval need to be modified.
// Subrange for a covering subreg becomes the main range.
void GCNRewritePartialRegUses::updateLiveIntervals(Register OldReg,
                                                   Register NewReg,
                                                   SubRegMap &SubRegs) const {
  if (!LIS->hasInterval(OldReg))
    return;

  auto &OldLI = LIS->getInterval(OldReg);
  auto &NewLI = LIS->createEmptyInterval(NewReg);

  auto &Allocator = LIS->getVNInfoAllocator();
  NewLI.setWeight(OldLI.weight());

  for (auto &SR : OldLI.subranges()) {
    auto I = find_if(SubRegs, [&](auto &P) {
      return SR.LaneMask == TRI->getSubRegIndexLaneMask(P.first);
    });

    if (I == SubRegs.end()) {
      // There might be a situation when subranges don't exactly match used
      // subregs, for example:
      // %120 [160r,1392r:0) 0@160r
      //    L000000000000C000 [160r,1392r:0) 0@160r
      //    L0000000000003000 [160r,1392r:0) 0@160r
      //    L0000000000000C00 [160r,1392r:0) 0@160r
      //    L0000000000000300 [160r,1392r:0) 0@160r
      //    L0000000000000003 [160r,1104r:0) 0@160r
      //    L000000000000000C [160r,1104r:0) 0@160r
      //    L0000000000000030 [160r,1104r:0) 0@160r
      //    L00000000000000C0 [160r,1104r:0) 0@160r
      // but used subregs are:
      //    sub0_sub1_sub2_sub3_sub4_sub5_sub6_sub7, L000000000000FFFF
      //    sub0_sub1_sub2_sub3, L00000000000000FF
      //    sub4_sub5_sub6_sub7, L000000000000FF00
      // In this example subregs sub0_sub1_sub2_sub3 and sub4_sub5_sub6_sub7
      // have several subranges with the same lifetime. For such cases just
      // recreate the interval.
      LIS->removeInterval(OldReg);
      LIS->removeInterval(NewReg);
      LIS->createAndComputeVirtRegInterval(NewReg);
      return;
    }

    if (unsigned NewSubReg = I->second.SubReg)
      NewLI.createSubRangeFrom(Allocator,
                               TRI->getSubRegIndexLaneMask(NewSubReg), SR);
    else // This is the covering subreg (0 index) - set it as main range.
      NewLI.assign(SR, Allocator);

    SubRegs.erase(I);
  }
  if (NewLI.empty())
    NewLI.assign(OldLI, Allocator);
  NewLI.verify(MRI);
  LIS->removeInterval(OldReg);
}

const TargetRegisterClass *
GCNRewritePartialRegUses::getOperandRegClass(MachineOperand &MO) const {
  MachineInstr *MI = MO.getParent();
  return TII->getRegClass(TII->get(MI->getOpcode()), MI->getOperandNo(&MO), TRI,
                          *MI->getParent()->getParent());
}

bool GCNRewritePartialRegUses::rewriteReg(Register Reg) const {
  auto Range = MRI->reg_nodbg_operands(Reg);
  if (Range.empty() || any_of(Range, [](MachineOperand &MO) {
        return MO.getSubReg() == AMDGPU::NoSubRegister; // Whole reg used. [1]
      }))
    return false;

  auto *RC = MRI->getRegClass(Reg);
  LLVM_DEBUG(dbgs() << "Try to rewrite partial reg " << printReg(Reg, TRI)
                    << ':' << TRI->getRegClassName(RC) << '\n');

  // Collect used subregs and their reg classes infered from instruction
  // operands.
  SubRegMap SubRegs;
  for (MachineOperand &MO : Range) {
    const unsigned SubReg = MO.getSubReg();
    assert(SubReg != AMDGPU::NoSubRegister); // Due to [1].
    LLVM_DEBUG(dbgs() << "  " << TRI->getSubRegIndexName(SubReg) << ':');

    const auto [I, Inserted] = SubRegs.try_emplace(SubReg);
    const TargetRegisterClass *&SubRegRC = I->second.RC;

    if (Inserted)
      SubRegRC = TRI->getSubRegisterClass(RC, SubReg);

    if (SubRegRC) {
      if (const TargetRegisterClass *OpDescRC = getOperandRegClass(MO)) {
        LLVM_DEBUG(dbgs() << TRI->getRegClassName(SubRegRC) << " & "
                          << TRI->getRegClassName(OpDescRC) << " = ");
        SubRegRC = TRI->getCommonSubClass(SubRegRC, OpDescRC);
      }
    }

    if (!SubRegRC) {
      LLVM_DEBUG(dbgs() << "couldn't find target regclass\n");
      return false;
    }
    LLVM_DEBUG(dbgs() << TRI->getRegClassName(SubRegRC) << '\n');
  }

  auto *NewRC = getMinSizeReg(RC, SubRegs);
  if (!NewRC) {
    LLVM_DEBUG(dbgs() << "  No improvement achieved\n");
    return false;
  }

  Register NewReg = MRI->createVirtualRegister(NewRC);
  LLVM_DEBUG(dbgs() << "  Success " << printReg(Reg, TRI) << ':'
                    << TRI->getRegClassName(RC) << " -> "
                    << printReg(NewReg, TRI) << ':'
                    << TRI->getRegClassName(NewRC) << '\n');

  for (auto &MO : make_early_inc_range(MRI->reg_operands(Reg))) {
    MO.setReg(NewReg);
    // Debug info can refer to the whole reg, just leave it as it is for now.
    // TODO: create some DI shift expression?
    if (MO.isDebug() && MO.getSubReg() == 0)
      continue;
    unsigned SubReg = SubRegs[MO.getSubReg()].SubReg;
    MO.setSubReg(SubReg);
    if (SubReg == AMDGPU::NoSubRegister && MO.isDef())
      MO.setIsUndef(false);
  }

  if (LIS)
    updateLiveIntervals(Reg, NewReg, SubRegs);

  return true;
}

bool GCNRewritePartialRegUses::runOnMachineFunction(MachineFunction &MF) {
  MRI = &MF.getRegInfo();
  TRI = static_cast<const SIRegisterInfo *>(MRI->getTargetRegisterInfo());
  TII = MF.getSubtarget().getInstrInfo();
  auto *LISWrapper = getAnalysisIfAvailable<LiveIntervalsWrapperPass>();
  LIS = LISWrapper ? &LISWrapper->getLIS() : nullptr;
  bool Changed = false;
  for (size_t I = 0, E = MRI->getNumVirtRegs(); I < E; ++I) {
    Changed |= rewriteReg(Register::index2VirtReg(I));
  }
  return Changed;
}

char GCNRewritePartialRegUses::ID;

char &llvm::GCNRewritePartialRegUsesID = GCNRewritePartialRegUses::ID;

INITIALIZE_PASS_BEGIN(GCNRewritePartialRegUses, DEBUG_TYPE,
                      "Rewrite Partial Register Uses", false, false)
INITIALIZE_PASS_END(GCNRewritePartialRegUses, DEBUG_TYPE,
                    "Rewrite Partial Register Uses", false, false)
