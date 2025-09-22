//===- lib/CodeGen/GlobalISel/GISelKnownBits.cpp --------------*- C++ *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// Provides analysis for querying information about KnownBits during GISel
/// passes.
//
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "gisel-known-bits"

using namespace llvm;

char llvm::GISelKnownBitsAnalysis::ID = 0;

INITIALIZE_PASS(GISelKnownBitsAnalysis, DEBUG_TYPE,
                "Analysis for ComputingKnownBits", false, true)

GISelKnownBits::GISelKnownBits(MachineFunction &MF, unsigned MaxDepth)
    : MF(MF), MRI(MF.getRegInfo()), TL(*MF.getSubtarget().getTargetLowering()),
      DL(MF.getFunction().getDataLayout()), MaxDepth(MaxDepth) {}

Align GISelKnownBits::computeKnownAlignment(Register R, unsigned Depth) {
  const MachineInstr *MI = MRI.getVRegDef(R);
  switch (MI->getOpcode()) {
  case TargetOpcode::COPY:
    return computeKnownAlignment(MI->getOperand(1).getReg(), Depth);
  case TargetOpcode::G_ASSERT_ALIGN: {
    // TODO: Min with source
    return Align(MI->getOperand(2).getImm());
  }
  case TargetOpcode::G_FRAME_INDEX: {
    int FrameIdx = MI->getOperand(1).getIndex();
    return MF.getFrameInfo().getObjectAlign(FrameIdx);
  }
  case TargetOpcode::G_INTRINSIC:
  case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS:
  case TargetOpcode::G_INTRINSIC_CONVERGENT:
  case TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
  default:
    return TL.computeKnownAlignForTargetInstr(*this, R, MRI, Depth + 1);
  }
}

KnownBits GISelKnownBits::getKnownBits(MachineInstr &MI) {
  assert(MI.getNumExplicitDefs() == 1 &&
         "expected single return generic instruction");
  return getKnownBits(MI.getOperand(0).getReg());
}

KnownBits GISelKnownBits::getKnownBits(Register R) {
  const LLT Ty = MRI.getType(R);
  // Since the number of lanes in a scalable vector is unknown at compile time,
  // we track one bit which is implicitly broadcast to all lanes.  This means
  // that all lanes in a scalable vector are considered demanded.
  APInt DemandedElts =
      Ty.isFixedVector() ? APInt::getAllOnes(Ty.getNumElements()) : APInt(1, 1);
  return getKnownBits(R, DemandedElts);
}

KnownBits GISelKnownBits::getKnownBits(Register R, const APInt &DemandedElts,
                                       unsigned Depth) {
  // For now, we only maintain the cache during one request.
  assert(ComputeKnownBitsCache.empty() && "Cache should have been cleared");

  KnownBits Known;
  computeKnownBitsImpl(R, Known, DemandedElts, Depth);
  ComputeKnownBitsCache.clear();
  return Known;
}

bool GISelKnownBits::signBitIsZero(Register R) {
  LLT Ty = MRI.getType(R);
  unsigned BitWidth = Ty.getScalarSizeInBits();
  return maskedValueIsZero(R, APInt::getSignMask(BitWidth));
}

APInt GISelKnownBits::getKnownZeroes(Register R) {
  return getKnownBits(R).Zero;
}

APInt GISelKnownBits::getKnownOnes(Register R) { return getKnownBits(R).One; }

LLVM_ATTRIBUTE_UNUSED static void
dumpResult(const MachineInstr &MI, const KnownBits &Known, unsigned Depth) {
  dbgs() << "[" << Depth << "] Compute known bits: " << MI << "[" << Depth
         << "] Computed for: " << MI << "[" << Depth << "] Known: 0x"
         << toString(Known.Zero | Known.One, 16, false) << "\n"
         << "[" << Depth << "] Zero: 0x" << toString(Known.Zero, 16, false)
         << "\n"
         << "[" << Depth << "] One:  0x" << toString(Known.One, 16, false)
         << "\n";
}

/// Compute known bits for the intersection of \p Src0 and \p Src1
void GISelKnownBits::computeKnownBitsMin(Register Src0, Register Src1,
                                         KnownBits &Known,
                                         const APInt &DemandedElts,
                                         unsigned Depth) {
  // Test src1 first, since we canonicalize simpler expressions to the RHS.
  computeKnownBitsImpl(Src1, Known, DemandedElts, Depth);

  // If we don't know any bits, early out.
  if (Known.isUnknown())
    return;

  KnownBits Known2;
  computeKnownBitsImpl(Src0, Known2, DemandedElts, Depth);

  // Only known if known in both the LHS and RHS.
  Known = Known.intersectWith(Known2);
}

// Bitfield extract is computed as (Src >> Offset) & Mask, where Mask is
// created using Width. Use this function when the inputs are KnownBits
// objects. TODO: Move this KnownBits.h if this is usable in more cases.
static KnownBits extractBits(unsigned BitWidth, const KnownBits &SrcOpKnown,
                             const KnownBits &OffsetKnown,
                             const KnownBits &WidthKnown) {
  KnownBits Mask(BitWidth);
  Mask.Zero = APInt::getBitsSetFrom(
      BitWidth, WidthKnown.getMaxValue().getLimitedValue(BitWidth));
  Mask.One = APInt::getLowBitsSet(
      BitWidth, WidthKnown.getMinValue().getLimitedValue(BitWidth));
  return KnownBits::lshr(SrcOpKnown, OffsetKnown) & Mask;
}

void GISelKnownBits::computeKnownBitsImpl(Register R, KnownBits &Known,
                                          const APInt &DemandedElts,
                                          unsigned Depth) {
  MachineInstr &MI = *MRI.getVRegDef(R);
  unsigned Opcode = MI.getOpcode();
  LLT DstTy = MRI.getType(R);

  // Handle the case where this is called on a register that does not have a
  // type constraint (i.e. it has a register class constraint instead). This is
  // unlikely to occur except by looking through copies but it is possible for
  // the initial register being queried to be in this state.
  if (!DstTy.isValid()) {
    Known = KnownBits();
    return;
  }

  unsigned BitWidth = DstTy.getScalarSizeInBits();
  auto CacheEntry = ComputeKnownBitsCache.find(R);
  if (CacheEntry != ComputeKnownBitsCache.end()) {
    Known = CacheEntry->second;
    LLVM_DEBUG(dbgs() << "Cache hit at ");
    LLVM_DEBUG(dumpResult(MI, Known, Depth));
    assert(Known.getBitWidth() == BitWidth && "Cache entry size doesn't match");
    return;
  }
  Known = KnownBits(BitWidth); // Don't know anything

  // Depth may get bigger than max depth if it gets passed to a different
  // GISelKnownBits object.
  // This may happen when say a generic part uses a GISelKnownBits object
  // with some max depth, but then we hit TL.computeKnownBitsForTargetInstr
  // which creates a new GISelKnownBits object with a different and smaller
  // depth. If we just check for equality, we would never exit if the depth
  // that is passed down to the target specific GISelKnownBits object is
  // already bigger than its max depth.
  if (Depth >= getMaxDepth())
    return;

  if (!DemandedElts)
    return; // No demanded elts, better to assume we don't know anything.

  KnownBits Known2;

  switch (Opcode) {
  default:
    TL.computeKnownBitsForTargetInstr(*this, R, Known, DemandedElts, MRI,
                                      Depth);
    break;
  case TargetOpcode::G_BUILD_VECTOR: {
    // Collect the known bits that are shared by every demanded vector element.
    Known.Zero.setAllBits(); Known.One.setAllBits();
    for (unsigned i = 0, e = MI.getNumOperands() - 1; i < e; ++i) {
      if (!DemandedElts[i])
        continue;

      computeKnownBitsImpl(MI.getOperand(i + 1).getReg(), Known2, DemandedElts,
                           Depth + 1);

      // Known bits are the values that are shared by every demanded element.
      Known = Known.intersectWith(Known2);

      // If we don't know any bits, early out.
      if (Known.isUnknown())
        break;
    }
    break;
  }
  case TargetOpcode::COPY:
  case TargetOpcode::G_PHI:
  case TargetOpcode::PHI: {
    Known.One = APInt::getAllOnes(BitWidth);
    Known.Zero = APInt::getAllOnes(BitWidth);
    // Destination registers should not have subregisters at this
    // point of the pipeline, otherwise the main live-range will be
    // defined more than once, which is against SSA.
    assert(MI.getOperand(0).getSubReg() == 0 && "Is this code in SSA?");
    // Record in the cache that we know nothing for MI.
    // This will get updated later and in the meantime, if we reach that
    // phi again, because of a loop, we will cut the search thanks to this
    // cache entry.
    // We could actually build up more information on the phi by not cutting
    // the search, but that additional information is more a side effect
    // than an intended choice.
    // Therefore, for now, save on compile time until we derive a proper way
    // to derive known bits for PHIs within loops.
    ComputeKnownBitsCache[R] = KnownBits(BitWidth);
    // PHI's operand are a mix of registers and basic blocks interleaved.
    // We only care about the register ones.
    for (unsigned Idx = 1; Idx < MI.getNumOperands(); Idx += 2) {
      const MachineOperand &Src = MI.getOperand(Idx);
      Register SrcReg = Src.getReg();
      // Look through trivial copies and phis but don't look through trivial
      // copies or phis of the form `%1:(s32) = OP %0:gpr32`, known-bits
      // analysis is currently unable to determine the bit width of a
      // register class.
      //
      // We can't use NoSubRegister by name as it's defined by each target but
      // it's always defined to be 0 by tablegen.
      if (SrcReg.isVirtual() && Src.getSubReg() == 0 /*NoSubRegister*/ &&
          MRI.getType(SrcReg).isValid()) {
        // For COPYs we don't do anything, don't increase the depth.
        computeKnownBitsImpl(SrcReg, Known2, DemandedElts,
                             Depth + (Opcode != TargetOpcode::COPY));
        Known = Known.intersectWith(Known2);
        // If we reach a point where we don't know anything
        // just stop looking through the operands.
        if (Known.isUnknown())
          break;
      } else {
        // We know nothing.
        Known = KnownBits(BitWidth);
        break;
      }
    }
    break;
  }
  case TargetOpcode::G_CONSTANT: {
    Known = KnownBits::makeConstant(MI.getOperand(1).getCImm()->getValue());
    break;
  }
  case TargetOpcode::G_FRAME_INDEX: {
    int FrameIdx = MI.getOperand(1).getIndex();
    TL.computeKnownBitsForFrameIndex(FrameIdx, Known, MF);
    break;
  }
  case TargetOpcode::G_SUB: {
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), Known2, DemandedElts,
                         Depth + 1);
    Known = KnownBits::computeForAddSub(/*Add=*/false, /*NSW=*/false,
                                        /* NUW=*/false, Known, Known2);
    break;
  }
  case TargetOpcode::G_XOR: {
    computeKnownBitsImpl(MI.getOperand(2).getReg(), Known, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known2, DemandedElts,
                         Depth + 1);

    Known ^= Known2;
    break;
  }
  case TargetOpcode::G_PTR_ADD: {
    if (DstTy.isVector())
      break;
    // G_PTR_ADD is like G_ADD. FIXME: Is this true for all targets?
    LLT Ty = MRI.getType(MI.getOperand(1).getReg());
    if (DL.isNonIntegralAddressSpace(Ty.getAddressSpace()))
      break;
    [[fallthrough]];
  }
  case TargetOpcode::G_ADD: {
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), Known2, DemandedElts,
                         Depth + 1);
    Known = KnownBits::computeForAddSub(/*Add=*/true, /*NSW=*/false,
                                        /* NUW=*/false, Known, Known2);
    break;
  }
  case TargetOpcode::G_AND: {
    // If either the LHS or the RHS are Zero, the result is zero.
    computeKnownBitsImpl(MI.getOperand(2).getReg(), Known, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known2, DemandedElts,
                         Depth + 1);

    Known &= Known2;
    break;
  }
  case TargetOpcode::G_OR: {
    // If either the LHS or the RHS are Zero, the result is zero.
    computeKnownBitsImpl(MI.getOperand(2).getReg(), Known, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known2, DemandedElts,
                         Depth + 1);

    Known |= Known2;
    break;
  }
  case TargetOpcode::G_MUL: {
    computeKnownBitsImpl(MI.getOperand(2).getReg(), Known, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known2, DemandedElts,
                         Depth + 1);
    Known = KnownBits::mul(Known, Known2);
    break;
  }
  case TargetOpcode::G_SELECT: {
    computeKnownBitsMin(MI.getOperand(2).getReg(), MI.getOperand(3).getReg(),
                        Known, DemandedElts, Depth + 1);
    break;
  }
  case TargetOpcode::G_SMIN: {
    // TODO: Handle clamp pattern with number of sign bits
    KnownBits KnownRHS;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), KnownRHS, DemandedElts,
                         Depth + 1);
    Known = KnownBits::smin(Known, KnownRHS);
    break;
  }
  case TargetOpcode::G_SMAX: {
    // TODO: Handle clamp pattern with number of sign bits
    KnownBits KnownRHS;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), KnownRHS, DemandedElts,
                         Depth + 1);
    Known = KnownBits::smax(Known, KnownRHS);
    break;
  }
  case TargetOpcode::G_UMIN: {
    KnownBits KnownRHS;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known,
                         DemandedElts, Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), KnownRHS,
                         DemandedElts, Depth + 1);
    Known = KnownBits::umin(Known, KnownRHS);
    break;
  }
  case TargetOpcode::G_UMAX: {
    KnownBits KnownRHS;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known,
                         DemandedElts, Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), KnownRHS,
                         DemandedElts, Depth + 1);
    Known = KnownBits::umax(Known, KnownRHS);
    break;
  }
  case TargetOpcode::G_FCMP:
  case TargetOpcode::G_ICMP: {
    if (DstTy.isVector())
      break;
    if (TL.getBooleanContents(DstTy.isVector(),
                              Opcode == TargetOpcode::G_FCMP) ==
            TargetLowering::ZeroOrOneBooleanContent &&
        BitWidth > 1)
      Known.Zero.setBitsFrom(1);
    break;
  }
  case TargetOpcode::G_SEXT: {
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known, DemandedElts,
                         Depth + 1);
    // If the sign bit is known to be zero or one, then sext will extend
    // it to the top bits, else it will just zext.
    Known = Known.sext(BitWidth);
    break;
  }
  case TargetOpcode::G_ASSERT_SEXT:
  case TargetOpcode::G_SEXT_INREG: {
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known, DemandedElts,
                         Depth + 1);
    Known = Known.sextInReg(MI.getOperand(2).getImm());
    break;
  }
  case TargetOpcode::G_ANYEXT: {
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known, DemandedElts,
                         Depth + 1);
    Known = Known.anyext(BitWidth);
    break;
  }
  case TargetOpcode::G_LOAD: {
    const MachineMemOperand *MMO = *MI.memoperands_begin();
    KnownBits KnownRange(MMO->getMemoryType().getScalarSizeInBits());
    if (const MDNode *Ranges = MMO->getRanges())
      computeKnownBitsFromRangeMetadata(*Ranges, KnownRange);
    Known = KnownRange.anyext(Known.getBitWidth());
    break;
  }
  case TargetOpcode::G_SEXTLOAD:
  case TargetOpcode::G_ZEXTLOAD: {
    if (DstTy.isVector())
      break;
    const MachineMemOperand *MMO = *MI.memoperands_begin();
    KnownBits KnownRange(MMO->getMemoryType().getScalarSizeInBits());
    if (const MDNode *Ranges = MMO->getRanges())
      computeKnownBitsFromRangeMetadata(*Ranges, KnownRange);
    Known = Opcode == TargetOpcode::G_SEXTLOAD
                ? KnownRange.sext(Known.getBitWidth())
                : KnownRange.zext(Known.getBitWidth());
    break;
  }
  case TargetOpcode::G_ASHR: {
    KnownBits LHSKnown, RHSKnown;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), LHSKnown, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), RHSKnown, DemandedElts,
                         Depth + 1);
    Known = KnownBits::ashr(LHSKnown, RHSKnown);
    break;
  }
  case TargetOpcode::G_LSHR: {
    KnownBits LHSKnown, RHSKnown;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), LHSKnown, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), RHSKnown, DemandedElts,
                         Depth + 1);
    Known = KnownBits::lshr(LHSKnown, RHSKnown);
    break;
  }
  case TargetOpcode::G_SHL: {
    KnownBits LHSKnown, RHSKnown;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), LHSKnown, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), RHSKnown, DemandedElts,
                         Depth + 1);
    Known = KnownBits::shl(LHSKnown, RHSKnown);
    break;
  }
  case TargetOpcode::G_INTTOPTR:
  case TargetOpcode::G_PTRTOINT:
    if (DstTy.isVector())
      break;
    // Fall through and handle them the same as zext/trunc.
    [[fallthrough]];
  case TargetOpcode::G_ASSERT_ZEXT:
  case TargetOpcode::G_ZEXT:
  case TargetOpcode::G_TRUNC: {
    Register SrcReg = MI.getOperand(1).getReg();
    LLT SrcTy = MRI.getType(SrcReg);
    unsigned SrcBitWidth;

    // G_ASSERT_ZEXT stores the original bitwidth in the immediate operand.
    if (Opcode == TargetOpcode::G_ASSERT_ZEXT)
      SrcBitWidth = MI.getOperand(2).getImm();
    else {
      SrcBitWidth = SrcTy.isPointer()
                        ? DL.getIndexSizeInBits(SrcTy.getAddressSpace())
                        : SrcTy.getSizeInBits();
    }
    assert(SrcBitWidth && "SrcBitWidth can't be zero");
    Known = Known.zextOrTrunc(SrcBitWidth);
    computeKnownBitsImpl(SrcReg, Known, DemandedElts, Depth + 1);
    Known = Known.zextOrTrunc(BitWidth);
    if (BitWidth > SrcBitWidth)
      Known.Zero.setBitsFrom(SrcBitWidth);
    break;
  }
  case TargetOpcode::G_ASSERT_ALIGN: {
    int64_t LogOfAlign = Log2_64(MI.getOperand(2).getImm());

    // TODO: Should use maximum with source
    // If a node is guaranteed to be aligned, set low zero bits accordingly as
    // well as clearing one bits.
    Known.Zero.setLowBits(LogOfAlign);
    Known.One.clearLowBits(LogOfAlign);
    break;
  }
  case TargetOpcode::G_MERGE_VALUES: {
    unsigned NumOps = MI.getNumOperands();
    unsigned OpSize = MRI.getType(MI.getOperand(1).getReg()).getSizeInBits();

    for (unsigned I = 0; I != NumOps - 1; ++I) {
      KnownBits SrcOpKnown;
      computeKnownBitsImpl(MI.getOperand(I + 1).getReg(), SrcOpKnown,
                           DemandedElts, Depth + 1);
      Known.insertBits(SrcOpKnown, I * OpSize);
    }
    break;
  }
  case TargetOpcode::G_UNMERGE_VALUES: {
    if (DstTy.isVector())
      break;
    unsigned NumOps = MI.getNumOperands();
    Register SrcReg = MI.getOperand(NumOps - 1).getReg();
    if (MRI.getType(SrcReg).isVector())
      return; // TODO: Handle vectors.

    KnownBits SrcOpKnown;
    computeKnownBitsImpl(SrcReg, SrcOpKnown, DemandedElts, Depth + 1);

    // Figure out the result operand index
    unsigned DstIdx = 0;
    for (; DstIdx != NumOps - 1 && MI.getOperand(DstIdx).getReg() != R;
         ++DstIdx)
      ;

    Known = SrcOpKnown.extractBits(BitWidth, BitWidth * DstIdx);
    break;
  }
  case TargetOpcode::G_BSWAP: {
    Register SrcReg = MI.getOperand(1).getReg();
    computeKnownBitsImpl(SrcReg, Known, DemandedElts, Depth + 1);
    Known = Known.byteSwap();
    break;
  }
  case TargetOpcode::G_BITREVERSE: {
    Register SrcReg = MI.getOperand(1).getReg();
    computeKnownBitsImpl(SrcReg, Known, DemandedElts, Depth + 1);
    Known = Known.reverseBits();
    break;
  }
  case TargetOpcode::G_CTPOP: {
    computeKnownBitsImpl(MI.getOperand(1).getReg(), Known2, DemandedElts,
                         Depth + 1);
    // We can bound the space the count needs.  Also, bits known to be zero can't
    // contribute to the population.
    unsigned BitsPossiblySet = Known2.countMaxPopulation();
    unsigned LowBits = llvm::bit_width(BitsPossiblySet);
    Known.Zero.setBitsFrom(LowBits);
    // TODO: we could bound Known.One using the lower bound on the number of
    // bits which might be set provided by popcnt KnownOne2.
    break;
  }
  case TargetOpcode::G_UBFX: {
    KnownBits SrcOpKnown, OffsetKnown, WidthKnown;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), SrcOpKnown, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), OffsetKnown, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(3).getReg(), WidthKnown, DemandedElts,
                         Depth + 1);
    Known = extractBits(BitWidth, SrcOpKnown, OffsetKnown, WidthKnown);
    break;
  }
  case TargetOpcode::G_SBFX: {
    KnownBits SrcOpKnown, OffsetKnown, WidthKnown;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), SrcOpKnown, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(2).getReg(), OffsetKnown, DemandedElts,
                         Depth + 1);
    computeKnownBitsImpl(MI.getOperand(3).getReg(), WidthKnown, DemandedElts,
                         Depth + 1);
    Known = extractBits(BitWidth, SrcOpKnown, OffsetKnown, WidthKnown);
    // Sign extend the extracted value using shift left and arithmetic shift
    // right.
    KnownBits ExtKnown = KnownBits::makeConstant(APInt(BitWidth, BitWidth));
    KnownBits ShiftKnown = KnownBits::computeForAddSub(
        /*Add=*/false, /*NSW=*/false, /* NUW=*/false, ExtKnown, WidthKnown);
    Known = KnownBits::ashr(KnownBits::shl(Known, ShiftKnown), ShiftKnown);
    break;
  }
  case TargetOpcode::G_UADDO:
  case TargetOpcode::G_UADDE:
  case TargetOpcode::G_SADDO:
  case TargetOpcode::G_SADDE:
  case TargetOpcode::G_USUBO:
  case TargetOpcode::G_USUBE:
  case TargetOpcode::G_SSUBO:
  case TargetOpcode::G_SSUBE:
  case TargetOpcode::G_UMULO:
  case TargetOpcode::G_SMULO: {
    if (MI.getOperand(1).getReg() == R) {
      // If we know the result of a compare has the top bits zero, use this
      // info.
      if (TL.getBooleanContents(DstTy.isVector(), false) ==
              TargetLowering::ZeroOrOneBooleanContent &&
          BitWidth > 1)
        Known.Zero.setBitsFrom(1);
    }
    break;
  }
  case TargetOpcode::G_CTLZ:
  case TargetOpcode::G_CTLZ_ZERO_UNDEF: {
    KnownBits SrcOpKnown;
    computeKnownBitsImpl(MI.getOperand(1).getReg(), SrcOpKnown, DemandedElts,
                         Depth + 1);
    // If we have a known 1, its position is our upper bound.
    unsigned PossibleLZ = SrcOpKnown.countMaxLeadingZeros();
    unsigned LowBits = llvm::bit_width(PossibleLZ);
    Known.Zero.setBitsFrom(LowBits);
    break;
  }
  }

  LLVM_DEBUG(dumpResult(MI, Known, Depth));

  // Update the cache.
  ComputeKnownBitsCache[R] = Known;
}

/// Compute number of sign bits for the intersection of \p Src0 and \p Src1
unsigned GISelKnownBits::computeNumSignBitsMin(Register Src0, Register Src1,
                                               const APInt &DemandedElts,
                                               unsigned Depth) {
  // Test src1 first, since we canonicalize simpler expressions to the RHS.
  unsigned Src1SignBits = computeNumSignBits(Src1, DemandedElts, Depth);
  if (Src1SignBits == 1)
    return 1;
  return std::min(computeNumSignBits(Src0, DemandedElts, Depth), Src1SignBits);
}

/// Compute the known number of sign bits with attached range metadata in the
/// memory operand. If this is an extending load, accounts for the behavior of
/// the high bits.
static unsigned computeNumSignBitsFromRangeMetadata(const GAnyLoad *Ld,
                                                    unsigned TyBits) {
  const MDNode *Ranges = Ld->getRanges();
  if (!Ranges)
    return 1;

  ConstantRange CR = getConstantRangeFromMetadata(*Ranges);
  if (TyBits > CR.getBitWidth()) {
    switch (Ld->getOpcode()) {
    case TargetOpcode::G_SEXTLOAD:
      CR = CR.signExtend(TyBits);
      break;
    case TargetOpcode::G_ZEXTLOAD:
      CR = CR.zeroExtend(TyBits);
      break;
    default:
      break;
    }
  }

  return std::min(CR.getSignedMin().getNumSignBits(),
                  CR.getSignedMax().getNumSignBits());
}

unsigned GISelKnownBits::computeNumSignBits(Register R,
                                            const APInt &DemandedElts,
                                            unsigned Depth) {
  MachineInstr &MI = *MRI.getVRegDef(R);
  unsigned Opcode = MI.getOpcode();

  if (Opcode == TargetOpcode::G_CONSTANT)
    return MI.getOperand(1).getCImm()->getValue().getNumSignBits();

  if (Depth == getMaxDepth())
    return 1;

  if (!DemandedElts)
    return 1; // No demanded elts, better to assume we don't know anything.

  LLT DstTy = MRI.getType(R);
  const unsigned TyBits = DstTy.getScalarSizeInBits();

  // Handle the case where this is called on a register that does not have a
  // type constraint. This is unlikely to occur except by looking through copies
  // but it is possible for the initial register being queried to be in this
  // state.
  if (!DstTy.isValid())
    return 1;

  unsigned FirstAnswer = 1;
  switch (Opcode) {
  case TargetOpcode::COPY: {
    MachineOperand &Src = MI.getOperand(1);
    if (Src.getReg().isVirtual() && Src.getSubReg() == 0 &&
        MRI.getType(Src.getReg()).isValid()) {
      // Don't increment Depth for this one since we didn't do any work.
      return computeNumSignBits(Src.getReg(), DemandedElts, Depth);
    }

    return 1;
  }
  case TargetOpcode::G_SEXT: {
    Register Src = MI.getOperand(1).getReg();
    LLT SrcTy = MRI.getType(Src);
    unsigned Tmp = DstTy.getScalarSizeInBits() - SrcTy.getScalarSizeInBits();
    return computeNumSignBits(Src, DemandedElts, Depth + 1) + Tmp;
  }
  case TargetOpcode::G_ASSERT_SEXT:
  case TargetOpcode::G_SEXT_INREG: {
    // Max of the input and what this extends.
    Register Src = MI.getOperand(1).getReg();
    unsigned SrcBits = MI.getOperand(2).getImm();
    unsigned InRegBits = TyBits - SrcBits + 1;
    return std::max(computeNumSignBits(Src, DemandedElts, Depth + 1), InRegBits);
  }
  case TargetOpcode::G_LOAD: {
    GLoad *Ld = cast<GLoad>(&MI);
    if (DemandedElts != 1 || !getDataLayout().isLittleEndian())
      break;

    return computeNumSignBitsFromRangeMetadata(Ld, TyBits);
  }
  case TargetOpcode::G_SEXTLOAD: {
    GSExtLoad *Ld = cast<GSExtLoad>(&MI);

    // FIXME: We need an in-memory type representation.
    if (DstTy.isVector())
      return 1;

    unsigned NumBits = computeNumSignBitsFromRangeMetadata(Ld, TyBits);
    if (NumBits != 1)
      return NumBits;

    // e.g. i16->i32 = '17' bits known.
    const MachineMemOperand *MMO = *MI.memoperands_begin();
    return TyBits - MMO->getSizeInBits().getValue() + 1;
  }
  case TargetOpcode::G_ZEXTLOAD: {
    GZExtLoad *Ld = cast<GZExtLoad>(&MI);

    // FIXME: We need an in-memory type representation.
    if (DstTy.isVector())
      return 1;

    unsigned NumBits = computeNumSignBitsFromRangeMetadata(Ld, TyBits);
    if (NumBits != 1)
      return NumBits;

    // e.g. i16->i32 = '16' bits known.
    const MachineMemOperand *MMO = *MI.memoperands_begin();
    return TyBits - MMO->getSizeInBits().getValue();
  }
  case TargetOpcode::G_AND:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_XOR: {
    Register Src1 = MI.getOperand(1).getReg();
    unsigned Src1NumSignBits =
        computeNumSignBits(Src1, DemandedElts, Depth + 1);
    if (Src1NumSignBits != 1) {
      Register Src2 = MI.getOperand(2).getReg();
      unsigned Src2NumSignBits =
          computeNumSignBits(Src2, DemandedElts, Depth + 1);
      FirstAnswer = std::min(Src1NumSignBits, Src2NumSignBits);
    }
    break;
  }
  case TargetOpcode::G_TRUNC: {
    Register Src = MI.getOperand(1).getReg();
    LLT SrcTy = MRI.getType(Src);

    // Check if the sign bits of source go down as far as the truncated value.
    unsigned DstTyBits = DstTy.getScalarSizeInBits();
    unsigned NumSrcBits = SrcTy.getScalarSizeInBits();
    unsigned NumSrcSignBits = computeNumSignBits(Src, DemandedElts, Depth + 1);
    if (NumSrcSignBits > (NumSrcBits - DstTyBits))
      return NumSrcSignBits - (NumSrcBits - DstTyBits);
    break;
  }
  case TargetOpcode::G_SELECT: {
    return computeNumSignBitsMin(MI.getOperand(2).getReg(),
                                 MI.getOperand(3).getReg(), DemandedElts,
                                 Depth + 1);
  }
  case TargetOpcode::G_SADDO:
  case TargetOpcode::G_SADDE:
  case TargetOpcode::G_UADDO:
  case TargetOpcode::G_UADDE:
  case TargetOpcode::G_SSUBO:
  case TargetOpcode::G_SSUBE:
  case TargetOpcode::G_USUBO:
  case TargetOpcode::G_USUBE:
  case TargetOpcode::G_SMULO:
  case TargetOpcode::G_UMULO: {
    // If compares returns 0/-1, all bits are sign bits.
    // We know that we have an integer-based boolean since these operations
    // are only available for integer.
    if (MI.getOperand(1).getReg() == R) {
      if (TL.getBooleanContents(DstTy.isVector(), false) ==
          TargetLowering::ZeroOrNegativeOneBooleanContent)
        return TyBits;
    }

    break;
  }
  case TargetOpcode::G_FCMP:
  case TargetOpcode::G_ICMP: {
    bool IsFP = Opcode == TargetOpcode::G_FCMP;
    if (TyBits == 1)
      break;
    auto BC = TL.getBooleanContents(DstTy.isVector(), IsFP);
    if (BC == TargetLoweringBase::ZeroOrNegativeOneBooleanContent)
      return TyBits; // All bits are sign bits.
    if (BC == TargetLowering::ZeroOrOneBooleanContent)
      return TyBits - 1; // Every always-zero bit is a sign bit.
    break;
  }
  case TargetOpcode::G_INTRINSIC:
  case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS:
  case TargetOpcode::G_INTRINSIC_CONVERGENT:
  case TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
  default: {
    unsigned NumBits =
      TL.computeNumSignBitsForTargetInstr(*this, R, DemandedElts, MRI, Depth);
    if (NumBits > 1)
      FirstAnswer = std::max(FirstAnswer, NumBits);
    break;
  }
  }

  // Finally, if we can prove that the top bits of the result are 0's or 1's,
  // use this information.
  KnownBits Known = getKnownBits(R, DemandedElts, Depth);
  APInt Mask;
  if (Known.isNonNegative()) {        // sign bit is 0
    Mask = Known.Zero;
  } else if (Known.isNegative()) {  // sign bit is 1;
    Mask = Known.One;
  } else {
    // Nothing known.
    return FirstAnswer;
  }

  // Okay, we know that the sign bit in Mask is set.  Use CLO to determine
  // the number of identical bits in the top of the input value.
  Mask <<= Mask.getBitWidth() - TyBits;
  return std::max(FirstAnswer, Mask.countl_one());
}

unsigned GISelKnownBits::computeNumSignBits(Register R, unsigned Depth) {
  LLT Ty = MRI.getType(R);
  APInt DemandedElts =
      Ty.isVector() ? APInt::getAllOnes(Ty.getNumElements()) : APInt(1, 1);
  return computeNumSignBits(R, DemandedElts, Depth);
}

void GISelKnownBitsAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool GISelKnownBitsAnalysis::runOnMachineFunction(MachineFunction &MF) {
  return false;
}

GISelKnownBits &GISelKnownBitsAnalysis::get(MachineFunction &MF) {
  if (!Info) {
    unsigned MaxDepth =
        MF.getTarget().getOptLevel() == CodeGenOptLevel::None ? 2 : 6;
    Info = std::make_unique<GISelKnownBits>(MF, MaxDepth);
  }
  return *Info;
}
