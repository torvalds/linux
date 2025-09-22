//==- TargetRegisterInfo.cpp - Target Register Information Implementation --==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetRegisterInfo interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Printable.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>

#define DEBUG_TYPE "target-reg-info"

using namespace llvm;

static cl::opt<unsigned>
    HugeSizeForSplit("huge-size-for-split", cl::Hidden,
                     cl::desc("A threshold of live range size which may cause "
                              "high compile time cost in global splitting."),
                     cl::init(5000));

TargetRegisterInfo::TargetRegisterInfo(
    const TargetRegisterInfoDesc *ID, regclass_iterator RCB,
    regclass_iterator RCE, const char *const *SRINames,
    const SubRegCoveredBits *SubIdxRanges, const LaneBitmask *SRILaneMasks,
    LaneBitmask SRICoveringLanes, const RegClassInfo *const RCIs,
    const MVT::SimpleValueType *const RCVTLists, unsigned Mode)
    : InfoDesc(ID), SubRegIndexNames(SRINames), SubRegIdxRanges(SubIdxRanges),
      SubRegIndexLaneMasks(SRILaneMasks), RegClassBegin(RCB), RegClassEnd(RCE),
      CoveringLanes(SRICoveringLanes), RCInfos(RCIs), RCVTLists(RCVTLists),
      HwMode(Mode) {}

TargetRegisterInfo::~TargetRegisterInfo() = default;

bool TargetRegisterInfo::shouldRegionSplitForVirtReg(
    const MachineFunction &MF, const LiveInterval &VirtReg) const {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineInstr *MI = MRI.getUniqueVRegDef(VirtReg.reg());
  if (MI && TII->isTriviallyReMaterializable(*MI) &&
      VirtReg.size() > HugeSizeForSplit)
    return false;
  return true;
}

void TargetRegisterInfo::markSuperRegs(BitVector &RegisterSet,
                                       MCRegister Reg) const {
  for (MCPhysReg SR : superregs_inclusive(Reg))
    RegisterSet.set(SR);
}

bool TargetRegisterInfo::checkAllSuperRegsMarked(const BitVector &RegisterSet,
    ArrayRef<MCPhysReg> Exceptions) const {
  // Check that all super registers of reserved regs are reserved as well.
  BitVector Checked(getNumRegs());
  for (unsigned Reg : RegisterSet.set_bits()) {
    if (Checked[Reg])
      continue;
    for (MCPhysReg SR : superregs(Reg)) {
      if (!RegisterSet[SR] && !is_contained(Exceptions, Reg)) {
        dbgs() << "Error: Super register " << printReg(SR, this)
               << " of reserved register " << printReg(Reg, this)
               << " is not reserved.\n";
        return false;
      }

      // We transitively check superregs. So we can remember this for later
      // to avoid compiletime explosion in deep register hierarchies.
      Checked.set(SR);
    }
  }
  return true;
}

namespace llvm {

Printable printReg(Register Reg, const TargetRegisterInfo *TRI,
                   unsigned SubIdx, const MachineRegisterInfo *MRI) {
  return Printable([Reg, TRI, SubIdx, MRI](raw_ostream &OS) {
    if (!Reg)
      OS << "$noreg";
    else if (Register::isStackSlot(Reg))
      OS << "SS#" << Register::stackSlot2Index(Reg);
    else if (Reg.isVirtual()) {
      StringRef Name = MRI ? MRI->getVRegName(Reg) : "";
      if (Name != "") {
        OS << '%' << Name;
      } else {
        OS << '%' << Register::virtReg2Index(Reg);
      }
    } else if (!TRI)
      OS << '$' << "physreg" << Reg;
    else if (Reg < TRI->getNumRegs()) {
      OS << '$';
      printLowerCase(TRI->getName(Reg), OS);
    } else
      llvm_unreachable("Register kind is unsupported.");

    if (SubIdx) {
      if (TRI)
        OS << ':' << TRI->getSubRegIndexName(SubIdx);
      else
        OS << ":sub(" << SubIdx << ')';
    }
  });
}

Printable printRegUnit(unsigned Unit, const TargetRegisterInfo *TRI) {
  return Printable([Unit, TRI](raw_ostream &OS) {
    // Generic printout when TRI is missing.
    if (!TRI) {
      OS << "Unit~" << Unit;
      return;
    }

    // Check for invalid register units.
    if (Unit >= TRI->getNumRegUnits()) {
      OS << "BadUnit~" << Unit;
      return;
    }

    // Normal units have at least one root.
    MCRegUnitRootIterator Roots(Unit, TRI);
    assert(Roots.isValid() && "Unit has no roots.");
    OS << TRI->getName(*Roots);
    for (++Roots; Roots.isValid(); ++Roots)
      OS << '~' << TRI->getName(*Roots);
  });
}

Printable printVRegOrUnit(unsigned Unit, const TargetRegisterInfo *TRI) {
  return Printable([Unit, TRI](raw_ostream &OS) {
    if (Register::isVirtualRegister(Unit)) {
      OS << '%' << Register::virtReg2Index(Unit);
    } else {
      OS << printRegUnit(Unit, TRI);
    }
  });
}

Printable printRegClassOrBank(Register Reg, const MachineRegisterInfo &RegInfo,
                              const TargetRegisterInfo *TRI) {
  return Printable([Reg, &RegInfo, TRI](raw_ostream &OS) {
    if (RegInfo.getRegClassOrNull(Reg))
      OS << StringRef(TRI->getRegClassName(RegInfo.getRegClass(Reg))).lower();
    else if (RegInfo.getRegBankOrNull(Reg))
      OS << StringRef(RegInfo.getRegBankOrNull(Reg)->getName()).lower();
    else {
      OS << "_";
      assert((RegInfo.def_empty(Reg) || RegInfo.getType(Reg).isValid()) &&
             "Generic registers must have a valid type");
    }
  });
}

} // end namespace llvm

/// getAllocatableClass - Return the maximal subclass of the given register
/// class that is alloctable, or NULL.
const TargetRegisterClass *
TargetRegisterInfo::getAllocatableClass(const TargetRegisterClass *RC) const {
  if (!RC || RC->isAllocatable())
    return RC;

  for (BitMaskClassIterator It(RC->getSubClassMask(), *this); It.isValid();
       ++It) {
    const TargetRegisterClass *SubRC = getRegClass(It.getID());
    if (SubRC->isAllocatable())
      return SubRC;
  }
  return nullptr;
}

/// getMinimalPhysRegClass - Returns the Register Class of a physical
/// register of the given type, picking the most sub register class of
/// the right type that contains this physreg.
const TargetRegisterClass *
TargetRegisterInfo::getMinimalPhysRegClass(MCRegister reg, MVT VT) const {
  assert(Register::isPhysicalRegister(reg) &&
         "reg must be a physical register");

  // Pick the most sub register class of the right type that contains
  // this physreg.
  const TargetRegisterClass* BestRC = nullptr;
  for (const TargetRegisterClass* RC : regclasses()) {
    if ((VT == MVT::Other || isTypeLegalForClass(*RC, VT)) &&
        RC->contains(reg) && (!BestRC || BestRC->hasSubClass(RC)))
      BestRC = RC;
  }

  assert(BestRC && "Couldn't find the register class");
  return BestRC;
}

const TargetRegisterClass *
TargetRegisterInfo::getMinimalPhysRegClassLLT(MCRegister reg, LLT Ty) const {
  assert(Register::isPhysicalRegister(reg) &&
         "reg must be a physical register");

  // Pick the most sub register class of the right type that contains
  // this physreg.
  const TargetRegisterClass *BestRC = nullptr;
  for (const TargetRegisterClass *RC : regclasses()) {
    if ((!Ty.isValid() || isTypeLegalForClass(*RC, Ty)) && RC->contains(reg) &&
        (!BestRC || BestRC->hasSubClass(RC)))
      BestRC = RC;
  }

  return BestRC;
}

/// getAllocatableSetForRC - Toggle the bits that represent allocatable
/// registers for the specific register class.
static void getAllocatableSetForRC(const MachineFunction &MF,
                                   const TargetRegisterClass *RC, BitVector &R){
  assert(RC->isAllocatable() && "invalid for nonallocatable sets");
  ArrayRef<MCPhysReg> Order = RC->getRawAllocationOrder(MF);
  for (MCPhysReg PR : Order)
    R.set(PR);
}

BitVector TargetRegisterInfo::getAllocatableSet(const MachineFunction &MF,
                                          const TargetRegisterClass *RC) const {
  BitVector Allocatable(getNumRegs());
  if (RC) {
    // A register class with no allocatable subclass returns an empty set.
    const TargetRegisterClass *SubClass = getAllocatableClass(RC);
    if (SubClass)
      getAllocatableSetForRC(MF, SubClass, Allocatable);
  } else {
    for (const TargetRegisterClass *C : regclasses())
      if (C->isAllocatable())
        getAllocatableSetForRC(MF, C, Allocatable);
  }

  // Mask out the reserved registers
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const BitVector &Reserved = MRI.getReservedRegs();
  Allocatable.reset(Reserved);

  return Allocatable;
}

static inline
const TargetRegisterClass *firstCommonClass(const uint32_t *A,
                                            const uint32_t *B,
                                            const TargetRegisterInfo *TRI) {
  for (unsigned I = 0, E = TRI->getNumRegClasses(); I < E; I += 32)
    if (unsigned Common = *A++ & *B++)
      return TRI->getRegClass(I + llvm::countr_zero(Common));
  return nullptr;
}

const TargetRegisterClass *
TargetRegisterInfo::getCommonSubClass(const TargetRegisterClass *A,
                                      const TargetRegisterClass *B) const {
  // First take care of the trivial cases.
  if (A == B)
    return A;
  if (!A || !B)
    return nullptr;

  // Register classes are ordered topologically, so the largest common
  // sub-class it the common sub-class with the smallest ID.
  return firstCommonClass(A->getSubClassMask(), B->getSubClassMask(), this);
}

const TargetRegisterClass *
TargetRegisterInfo::getMatchingSuperRegClass(const TargetRegisterClass *A,
                                             const TargetRegisterClass *B,
                                             unsigned Idx) const {
  assert(A && B && "Missing register class");
  assert(Idx && "Bad sub-register index");

  // Find Idx in the list of super-register indices.
  for (SuperRegClassIterator RCI(B, this); RCI.isValid(); ++RCI)
    if (RCI.getSubReg() == Idx)
      // The bit mask contains all register classes that are projected into B
      // by Idx. Find a class that is also a sub-class of A.
      return firstCommonClass(RCI.getMask(), A->getSubClassMask(), this);
  return nullptr;
}

const TargetRegisterClass *TargetRegisterInfo::
getCommonSuperRegClass(const TargetRegisterClass *RCA, unsigned SubA,
                       const TargetRegisterClass *RCB, unsigned SubB,
                       unsigned &PreA, unsigned &PreB) const {
  assert(RCA && SubA && RCB && SubB && "Invalid arguments");

  // Search all pairs of sub-register indices that project into RCA and RCB
  // respectively. This is quadratic, but usually the sets are very small. On
  // most targets like X86, there will only be a single sub-register index
  // (e.g., sub_16bit projecting into GR16).
  //
  // The worst case is a register class like DPR on ARM.
  // We have indices dsub_0..dsub_7 projecting into that class.
  //
  // It is very common that one register class is a sub-register of the other.
  // Arrange for RCA to be the larger register so the answer will be found in
  // the first iteration. This makes the search linear for the most common
  // case.
  const TargetRegisterClass *BestRC = nullptr;
  unsigned *BestPreA = &PreA;
  unsigned *BestPreB = &PreB;
  if (getRegSizeInBits(*RCA) < getRegSizeInBits(*RCB)) {
    std::swap(RCA, RCB);
    std::swap(SubA, SubB);
    std::swap(BestPreA, BestPreB);
  }

  // Also terminate the search one we have found a register class as small as
  // RCA.
  unsigned MinSize = getRegSizeInBits(*RCA);

  for (SuperRegClassIterator IA(RCA, this, true); IA.isValid(); ++IA) {
    unsigned FinalA = composeSubRegIndices(IA.getSubReg(), SubA);
    for (SuperRegClassIterator IB(RCB, this, true); IB.isValid(); ++IB) {
      // Check if a common super-register class exists for this index pair.
      const TargetRegisterClass *RC =
        firstCommonClass(IA.getMask(), IB.getMask(), this);
      if (!RC || getRegSizeInBits(*RC) < MinSize)
        continue;

      // The indexes must compose identically: PreA+SubA == PreB+SubB.
      unsigned FinalB = composeSubRegIndices(IB.getSubReg(), SubB);
      if (FinalA != FinalB)
        continue;

      // Is RC a better candidate than BestRC?
      if (BestRC && getRegSizeInBits(*RC) >= getRegSizeInBits(*BestRC))
        continue;

      // Yes, RC is the smallest super-register seen so far.
      BestRC = RC;
      *BestPreA = IA.getSubReg();
      *BestPreB = IB.getSubReg();

      // Bail early if we reached MinSize. We won't find a better candidate.
      if (getRegSizeInBits(*BestRC) == MinSize)
        return BestRC;
    }
  }
  return BestRC;
}

/// Check if the registers defined by the pair (RegisterClass, SubReg)
/// share the same register file.
static bool shareSameRegisterFile(const TargetRegisterInfo &TRI,
                                  const TargetRegisterClass *DefRC,
                                  unsigned DefSubReg,
                                  const TargetRegisterClass *SrcRC,
                                  unsigned SrcSubReg) {
  // Same register class.
  if (DefRC == SrcRC)
    return true;

  // Both operands are sub registers. Check if they share a register class.
  unsigned SrcIdx, DefIdx;
  if (SrcSubReg && DefSubReg) {
    return TRI.getCommonSuperRegClass(SrcRC, SrcSubReg, DefRC, DefSubReg,
                                      SrcIdx, DefIdx) != nullptr;
  }

  // At most one of the register is a sub register, make it Src to avoid
  // duplicating the test.
  if (!SrcSubReg) {
    std::swap(DefSubReg, SrcSubReg);
    std::swap(DefRC, SrcRC);
  }

  // One of the register is a sub register, check if we can get a superclass.
  if (SrcSubReg)
    return TRI.getMatchingSuperRegClass(SrcRC, DefRC, SrcSubReg) != nullptr;

  // Plain copy.
  return TRI.getCommonSubClass(DefRC, SrcRC) != nullptr;
}

bool TargetRegisterInfo::shouldRewriteCopySrc(const TargetRegisterClass *DefRC,
                                              unsigned DefSubReg,
                                              const TargetRegisterClass *SrcRC,
                                              unsigned SrcSubReg) const {
  // If this source does not incur a cross register bank copy, use it.
  return shareSameRegisterFile(*this, DefRC, DefSubReg, SrcRC, SrcSubReg);
}

// Compute target-independent register allocator hints to help eliminate copies.
bool TargetRegisterInfo::getRegAllocationHints(
    Register VirtReg, ArrayRef<MCPhysReg> Order,
    SmallVectorImpl<MCPhysReg> &Hints, const MachineFunction &MF,
    const VirtRegMap *VRM, const LiveRegMatrix *Matrix) const {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const std::pair<unsigned, SmallVector<Register, 4>> &Hints_MRI =
      MRI.getRegAllocationHints(VirtReg);

  SmallSet<Register, 32> HintedRegs;
  // First hint may be a target hint.
  bool Skip = (Hints_MRI.first != 0);
  for (auto Reg : Hints_MRI.second) {
    if (Skip) {
      Skip = false;
      continue;
    }

    // Target-independent hints are either a physical or a virtual register.
    Register Phys = Reg;
    if (VRM && Phys.isVirtual())
      Phys = VRM->getPhys(Phys);

    // Don't add the same reg twice (Hints_MRI may contain multiple virtual
    // registers allocated to the same physreg).
    if (!HintedRegs.insert(Phys).second)
      continue;
    // Check that Phys is a valid hint in VirtReg's register class.
    if (!Phys.isPhysical())
      continue;
    if (MRI.isReserved(Phys))
      continue;
    // Check that Phys is in the allocation order. We shouldn't heed hints
    // from VirtReg's register class if they aren't in the allocation order. The
    // target probably has a reason for removing the register.
    if (!is_contained(Order, Phys))
      continue;

    // All clear, tell the register allocator to prefer this register.
    Hints.push_back(Phys);
  }
  return false;
}

bool TargetRegisterInfo::isCalleeSavedPhysReg(
    MCRegister PhysReg, const MachineFunction &MF) const {
  if (PhysReg == 0)
    return false;
  const uint32_t *callerPreservedRegs =
      getCallPreservedMask(MF, MF.getFunction().getCallingConv());
  if (callerPreservedRegs) {
    assert(Register::isPhysicalRegister(PhysReg) &&
           "Expected physical register");
    return (callerPreservedRegs[PhysReg / 32] >> PhysReg % 32) & 1;
  }
  return false;
}

bool TargetRegisterInfo::canRealignStack(const MachineFunction &MF) const {
  return MF.getFrameInfo().isStackRealignable();
}

bool TargetRegisterInfo::shouldRealignStack(const MachineFunction &MF) const {
  return MF.getFrameInfo().shouldRealignStack();
}

bool TargetRegisterInfo::regmaskSubsetEqual(const uint32_t *mask0,
                                            const uint32_t *mask1) const {
  unsigned N = (getNumRegs()+31) / 32;
  for (unsigned I = 0; I < N; ++I)
    if ((mask0[I] & mask1[I]) != mask0[I])
      return false;
  return true;
}

TypeSize
TargetRegisterInfo::getRegSizeInBits(Register Reg,
                                     const MachineRegisterInfo &MRI) const {
  const TargetRegisterClass *RC{};
  if (Reg.isPhysical()) {
    // The size is not directly available for physical registers.
    // Instead, we need to access a register class that contains Reg and
    // get the size of that register class.
    RC = getMinimalPhysRegClass(Reg);
    assert(RC && "Unable to deduce the register class");
    return getRegSizeInBits(*RC);
  }
  LLT Ty = MRI.getType(Reg);
  if (Ty.isValid())
    return Ty.getSizeInBits();

  // Since Reg is not a generic register, it may have a register class.
  RC = MRI.getRegClass(Reg);
  assert(RC && "Unable to deduce the register class");
  return getRegSizeInBits(*RC);
}

bool TargetRegisterInfo::getCoveringSubRegIndexes(
    const MachineRegisterInfo &MRI, const TargetRegisterClass *RC,
    LaneBitmask LaneMask, SmallVectorImpl<unsigned> &NeededIndexes) const {
  SmallVector<unsigned, 8> PossibleIndexes;
  unsigned BestIdx = 0;
  unsigned BestCover = 0;

  for (unsigned Idx = 1, E = getNumSubRegIndices(); Idx < E; ++Idx) {
    // Is this index even compatible with the given class?
    if (getSubClassWithSubReg(RC, Idx) != RC)
      continue;
    LaneBitmask SubRegMask = getSubRegIndexLaneMask(Idx);
    // Early exit if we found a perfect match.
    if (SubRegMask == LaneMask) {
      BestIdx = Idx;
      break;
    }

    // The index must not cover any lanes outside \p LaneMask.
    if ((SubRegMask & ~LaneMask).any())
      continue;

    unsigned PopCount = SubRegMask.getNumLanes();
    PossibleIndexes.push_back(Idx);
    if (PopCount > BestCover) {
      BestCover = PopCount;
      BestIdx = Idx;
    }
  }

  // Abort if we cannot possibly implement the COPY with the given indexes.
  if (BestIdx == 0)
    return false;

  NeededIndexes.push_back(BestIdx);

  // Greedy heuristic: Keep iterating keeping the best covering subreg index
  // each time.
  LaneBitmask LanesLeft = LaneMask & ~getSubRegIndexLaneMask(BestIdx);
  while (LanesLeft.any()) {
    unsigned BestIdx = 0;
    int BestCover = std::numeric_limits<int>::min();
    for (unsigned Idx : PossibleIndexes) {
      LaneBitmask SubRegMask = getSubRegIndexLaneMask(Idx);
      // Early exit if we found a perfect match.
      if (SubRegMask == LanesLeft) {
        BestIdx = Idx;
        break;
      }

      // Do not cover already-covered lanes to avoid creating cycles
      // in copy bundles (= bundle contains copies that write to the
      // registers).
      if ((SubRegMask & ~LanesLeft).any())
        continue;

      // Try to cover as many of the remaining lanes as possible.
      const int Cover = (SubRegMask & LanesLeft).getNumLanes();
      if (Cover > BestCover) {
        BestCover = Cover;
        BestIdx = Idx;
      }
    }

    if (BestIdx == 0)
      return false; // Impossible to handle

    NeededIndexes.push_back(BestIdx);

    LanesLeft &= ~getSubRegIndexLaneMask(BestIdx);
  }

  return BestIdx;
}

unsigned TargetRegisterInfo::getSubRegIdxSize(unsigned Idx) const {
  assert(Idx && Idx < getNumSubRegIndices() &&
         "This is not a subregister index");
  return SubRegIdxRanges[HwMode * getNumSubRegIndices() + Idx].Size;
}

unsigned TargetRegisterInfo::getSubRegIdxOffset(unsigned Idx) const {
  assert(Idx && Idx < getNumSubRegIndices() &&
         "This is not a subregister index");
  return SubRegIdxRanges[HwMode * getNumSubRegIndices() + Idx].Offset;
}

Register
TargetRegisterInfo::lookThruCopyLike(Register SrcReg,
                                     const MachineRegisterInfo *MRI) const {
  while (true) {
    const MachineInstr *MI = MRI->getVRegDef(SrcReg);
    if (!MI->isCopyLike())
      return SrcReg;

    Register CopySrcReg;
    if (MI->isCopy())
      CopySrcReg = MI->getOperand(1).getReg();
    else {
      assert(MI->isSubregToReg() && "Bad opcode for lookThruCopyLike");
      CopySrcReg = MI->getOperand(2).getReg();
    }

    if (!CopySrcReg.isVirtual())
      return CopySrcReg;

    SrcReg = CopySrcReg;
  }
}

Register TargetRegisterInfo::lookThruSingleUseCopyChain(
    Register SrcReg, const MachineRegisterInfo *MRI) const {
  while (true) {
    const MachineInstr *MI = MRI->getVRegDef(SrcReg);
    // Found the real definition, return it if it has a single use.
    if (!MI->isCopyLike())
      return MRI->hasOneNonDBGUse(SrcReg) ? SrcReg : Register();

    Register CopySrcReg;
    if (MI->isCopy())
      CopySrcReg = MI->getOperand(1).getReg();
    else {
      assert(MI->isSubregToReg() && "Bad opcode for lookThruCopyLike");
      CopySrcReg = MI->getOperand(2).getReg();
    }

    // Continue only if the next definition in the chain is for a virtual
    // register that has a single use.
    if (!CopySrcReg.isVirtual() || !MRI->hasOneNonDBGUse(CopySrcReg))
      return Register();

    SrcReg = CopySrcReg;
  }
}

void TargetRegisterInfo::getOffsetOpcodes(
    const StackOffset &Offset, SmallVectorImpl<uint64_t> &Ops) const {
  assert(!Offset.getScalable() && "Scalable offsets are not handled");
  DIExpression::appendOffset(Ops, Offset.getFixed());
}

DIExpression *
TargetRegisterInfo::prependOffsetExpression(const DIExpression *Expr,
                                            unsigned PrependFlags,
                                            const StackOffset &Offset) const {
  assert((PrependFlags &
          ~(DIExpression::DerefBefore | DIExpression::DerefAfter |
            DIExpression::StackValue | DIExpression::EntryValue)) == 0 &&
         "Unsupported prepend flag");
  SmallVector<uint64_t, 16> OffsetExpr;
  if (PrependFlags & DIExpression::DerefBefore)
    OffsetExpr.push_back(dwarf::DW_OP_deref);
  getOffsetOpcodes(Offset, OffsetExpr);
  if (PrependFlags & DIExpression::DerefAfter)
    OffsetExpr.push_back(dwarf::DW_OP_deref);
  return DIExpression::prependOpcodes(Expr, OffsetExpr,
                                      PrependFlags & DIExpression::StackValue,
                                      PrependFlags & DIExpression::EntryValue);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD
void TargetRegisterInfo::dumpReg(Register Reg, unsigned SubRegIndex,
                                 const TargetRegisterInfo *TRI) {
  dbgs() << printReg(Reg, TRI, SubRegIndex) << "\n";
}
#endif
