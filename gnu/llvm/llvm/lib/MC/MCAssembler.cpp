//===- lib/MC/MCAssembler.cpp - Assembler Backend Implementation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAssembler.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <tuple>
#include <utility>

using namespace llvm;

namespace llvm {
class MCSubtargetInfo;
}

#define DEBUG_TYPE "assembler"

namespace {
namespace stats {

STATISTIC(EmittedFragments, "Number of emitted assembler fragments - total");
STATISTIC(EmittedRelaxableFragments,
          "Number of emitted assembler fragments - relaxable");
STATISTIC(EmittedDataFragments,
          "Number of emitted assembler fragments - data");
STATISTIC(EmittedCompactEncodedInstFragments,
          "Number of emitted assembler fragments - compact encoded inst");
STATISTIC(EmittedAlignFragments,
          "Number of emitted assembler fragments - align");
STATISTIC(EmittedFillFragments,
          "Number of emitted assembler fragments - fill");
STATISTIC(EmittedNopsFragments, "Number of emitted assembler fragments - nops");
STATISTIC(EmittedOrgFragments, "Number of emitted assembler fragments - org");
STATISTIC(evaluateFixup, "Number of evaluated fixups");
STATISTIC(ObjectBytes, "Number of emitted object file bytes");
STATISTIC(RelaxationSteps, "Number of assembler layout and relaxation steps");
STATISTIC(RelaxedInstructions, "Number of relaxed instructions");

} // end namespace stats
} // end anonymous namespace

// FIXME FIXME FIXME: There are number of places in this file where we convert
// what is a 64-bit assembler value used for computation into a value in the
// object file, which may truncate it. We should detect that truncation where
// invalid and report errors back.

/* *** */

MCAssembler::MCAssembler(MCContext &Context,
                         std::unique_ptr<MCAsmBackend> Backend,
                         std::unique_ptr<MCCodeEmitter> Emitter,
                         std::unique_ptr<MCObjectWriter> Writer)
    : Context(Context), Backend(std::move(Backend)),
      Emitter(std::move(Emitter)), Writer(std::move(Writer)) {}

void MCAssembler::reset() {
  RelaxAll = false;
  Sections.clear();
  Symbols.clear();
  ThumbFuncs.clear();
  BundleAlignSize = 0;

  // reset objects owned by us
  if (getBackendPtr())
    getBackendPtr()->reset();
  if (getEmitterPtr())
    getEmitterPtr()->reset();
  if (Writer)
    Writer->reset();
}

bool MCAssembler::registerSection(MCSection &Section) {
  if (Section.isRegistered())
    return false;
  assert(Section.curFragList()->Head && "allocInitialFragment not called");
  Sections.push_back(&Section);
  Section.setIsRegistered(true);
  return true;
}

bool MCAssembler::isThumbFunc(const MCSymbol *Symbol) const {
  if (ThumbFuncs.count(Symbol))
    return true;

  if (!Symbol->isVariable())
    return false;

  const MCExpr *Expr = Symbol->getVariableValue();

  MCValue V;
  if (!Expr->evaluateAsRelocatable(V, nullptr, nullptr))
    return false;

  if (V.getSymB() || V.getRefKind() != MCSymbolRefExpr::VK_None)
    return false;

  const MCSymbolRefExpr *Ref = V.getSymA();
  if (!Ref)
    return false;

  if (Ref->getKind() != MCSymbolRefExpr::VK_None)
    return false;

  const MCSymbol &Sym = Ref->getSymbol();
  if (!isThumbFunc(&Sym))
    return false;

  ThumbFuncs.insert(Symbol); // Cache it.
  return true;
}

bool MCAssembler::evaluateFixup(const MCFixup &Fixup, const MCFragment *DF,
                                MCValue &Target, const MCSubtargetInfo *STI,
                                uint64_t &Value, bool &WasForced) const {
  ++stats::evaluateFixup;

  // FIXME: This code has some duplication with recordRelocation. We should
  // probably merge the two into a single callback that tries to evaluate a
  // fixup and records a relocation if one is needed.

  // On error claim to have completely evaluated the fixup, to prevent any
  // further processing from being done.
  const MCExpr *Expr = Fixup.getValue();
  MCContext &Ctx = getContext();
  Value = 0;
  WasForced = false;
  if (!Expr->evaluateAsRelocatable(Target, this, &Fixup)) {
    Ctx.reportError(Fixup.getLoc(), "expected relocatable expression");
    return true;
  }
  if (const MCSymbolRefExpr *RefB = Target.getSymB()) {
    if (RefB->getKind() != MCSymbolRefExpr::VK_None) {
      Ctx.reportError(Fixup.getLoc(),
                      "unsupported subtraction of qualified symbol");
      return true;
    }
  }

  assert(getBackendPtr() && "Expected assembler backend");
  bool IsTarget = getBackendPtr()->getFixupKindInfo(Fixup.getKind()).Flags &
                  MCFixupKindInfo::FKF_IsTarget;

  if (IsTarget)
    return getBackend().evaluateTargetFixup(*this, Fixup, DF, Target, STI,
                                            Value, WasForced);

  unsigned FixupFlags = getBackendPtr()->getFixupKindInfo(Fixup.getKind()).Flags;
  bool IsPCRel = getBackendPtr()->getFixupKindInfo(Fixup.getKind()).Flags &
                 MCFixupKindInfo::FKF_IsPCRel;

  bool IsResolved = false;
  if (IsPCRel) {
    if (Target.getSymB()) {
      IsResolved = false;
    } else if (!Target.getSymA()) {
      IsResolved = false;
    } else {
      const MCSymbolRefExpr *A = Target.getSymA();
      const MCSymbol &SA = A->getSymbol();
      if (A->getKind() != MCSymbolRefExpr::VK_None || SA.isUndefined()) {
        IsResolved = false;
      } else {
        IsResolved = (FixupFlags & MCFixupKindInfo::FKF_Constant) ||
                     getWriter().isSymbolRefDifferenceFullyResolvedImpl(
                         *this, SA, *DF, false, true);
      }
    }
  } else {
    IsResolved = Target.isAbsolute();
  }

  Value = Target.getConstant();

  if (const MCSymbolRefExpr *A = Target.getSymA()) {
    const MCSymbol &Sym = A->getSymbol();
    if (Sym.isDefined())
      Value += getSymbolOffset(Sym);
  }
  if (const MCSymbolRefExpr *B = Target.getSymB()) {
    const MCSymbol &Sym = B->getSymbol();
    if (Sym.isDefined())
      Value -= getSymbolOffset(Sym);
  }

  bool ShouldAlignPC = getBackend().getFixupKindInfo(Fixup.getKind()).Flags &
                       MCFixupKindInfo::FKF_IsAlignedDownTo32Bits;
  assert((ShouldAlignPC ? IsPCRel : true) &&
    "FKF_IsAlignedDownTo32Bits is only allowed on PC-relative fixups!");

  if (IsPCRel) {
    uint64_t Offset = getFragmentOffset(*DF) + Fixup.getOffset();

    // A number of ARM fixups in Thumb mode require that the effective PC
    // address be determined as the 32-bit aligned version of the actual offset.
    if (ShouldAlignPC) Offset &= ~0x3;
    Value -= Offset;
  }

  // Let the backend force a relocation if needed.
  if (IsResolved &&
      getBackend().shouldForceRelocation(*this, Fixup, Target, STI)) {
    IsResolved = false;
    WasForced = true;
  }

  // A linker relaxation target may emit ADD/SUB relocations for A-B+C. Let
  // recordRelocation handle non-VK_None cases like A@plt-B+C.
  if (!IsResolved && Target.getSymA() && Target.getSymB() &&
      Target.getSymA()->getKind() == MCSymbolRefExpr::VK_None &&
      getBackend().handleAddSubRelocations(*this, *DF, Fixup, Target, Value))
    return true;

  return IsResolved;
}

uint64_t MCAssembler::computeFragmentSize(const MCFragment &F) const {
  assert(getBackendPtr() && "Requires assembler backend");
  switch (F.getKind()) {
  case MCFragment::FT_Data:
    return cast<MCDataFragment>(F).getContents().size();
  case MCFragment::FT_Relaxable:
    return cast<MCRelaxableFragment>(F).getContents().size();
  case MCFragment::FT_CompactEncodedInst:
    return cast<MCCompactEncodedInstFragment>(F).getContents().size();
  case MCFragment::FT_Fill: {
    auto &FF = cast<MCFillFragment>(F);
    int64_t NumValues = 0;
    if (!FF.getNumValues().evaluateKnownAbsolute(NumValues, *this)) {
      getContext().reportError(FF.getLoc(),
                               "expected assembly-time absolute expression");
      return 0;
    }
    int64_t Size = NumValues * FF.getValueSize();
    if (Size < 0) {
      getContext().reportError(FF.getLoc(), "invalid number of bytes");
      return 0;
    }
    return Size;
  }

  case MCFragment::FT_Nops:
    return cast<MCNopsFragment>(F).getNumBytes();

  case MCFragment::FT_LEB:
    return cast<MCLEBFragment>(F).getContents().size();

  case MCFragment::FT_BoundaryAlign:
    return cast<MCBoundaryAlignFragment>(F).getSize();

  case MCFragment::FT_SymbolId:
    return 4;

  case MCFragment::FT_Align: {
    const MCAlignFragment &AF = cast<MCAlignFragment>(F);
    unsigned Offset = getFragmentOffset(AF);
    unsigned Size = offsetToAlignment(Offset, AF.getAlignment());

    // Insert extra Nops for code alignment if the target define
    // shouldInsertExtraNopBytesForCodeAlign target hook.
    if (AF.getParent()->useCodeAlign() && AF.hasEmitNops() &&
        getBackend().shouldInsertExtraNopBytesForCodeAlign(AF, Size))
      return Size;

    // If we are padding with nops, force the padding to be larger than the
    // minimum nop size.
    if (Size > 0 && AF.hasEmitNops()) {
      while (Size % getBackend().getMinimumNopSize())
        Size += AF.getAlignment().value();
    }
    if (Size > AF.getMaxBytesToEmit())
      return 0;
    return Size;
  }

  case MCFragment::FT_Org: {
    const MCOrgFragment &OF = cast<MCOrgFragment>(F);
    MCValue Value;
    if (!OF.getOffset().evaluateAsValue(Value, *this)) {
      getContext().reportError(OF.getLoc(),
                               "expected assembly-time absolute expression");
        return 0;
    }

    uint64_t FragmentOffset = getFragmentOffset(OF);
    int64_t TargetLocation = Value.getConstant();
    if (const MCSymbolRefExpr *A = Value.getSymA()) {
      uint64_t Val;
      if (!getSymbolOffset(A->getSymbol(), Val)) {
        getContext().reportError(OF.getLoc(), "expected absolute expression");
        return 0;
      }
      TargetLocation += Val;
    }
    int64_t Size = TargetLocation - FragmentOffset;
    if (Size < 0 || Size >= 0x40000000) {
      getContext().reportError(
          OF.getLoc(), "invalid .org offset '" + Twine(TargetLocation) +
                           "' (at offset '" + Twine(FragmentOffset) + "')");
      return 0;
    }
    return Size;
  }

  case MCFragment::FT_Dwarf:
    return cast<MCDwarfLineAddrFragment>(F).getContents().size();
  case MCFragment::FT_DwarfFrame:
    return cast<MCDwarfCallFrameFragment>(F).getContents().size();
  case MCFragment::FT_CVInlineLines:
    return cast<MCCVInlineLineTableFragment>(F).getContents().size();
  case MCFragment::FT_CVDefRange:
    return cast<MCCVDefRangeFragment>(F).getContents().size();
  case MCFragment::FT_PseudoProbe:
    return cast<MCPseudoProbeAddrFragment>(F).getContents().size();
  case MCFragment::FT_Dummy:
    llvm_unreachable("Should not have been added");
  }

  llvm_unreachable("invalid fragment kind");
}

// Compute the amount of padding required before the fragment \p F to
// obey bundling restrictions, where \p FOffset is the fragment's offset in
// its section and \p FSize is the fragment's size.
static uint64_t computeBundlePadding(unsigned BundleSize,
                                     const MCEncodedFragment *F,
                                     uint64_t FOffset, uint64_t FSize) {
  uint64_t OffsetInBundle = FOffset & (BundleSize - 1);
  uint64_t EndOfFragment = OffsetInBundle + FSize;

  // There are two kinds of bundling restrictions:
  //
  // 1) For alignToBundleEnd(), add padding to ensure that the fragment will
  //    *end* on a bundle boundary.
  // 2) Otherwise, check if the fragment would cross a bundle boundary. If it
  //    would, add padding until the end of the bundle so that the fragment
  //    will start in a new one.
  if (F->alignToBundleEnd()) {
    // Three possibilities here:
    //
    // A) The fragment just happens to end at a bundle boundary, so we're good.
    // B) The fragment ends before the current bundle boundary: pad it just
    //    enough to reach the boundary.
    // C) The fragment ends after the current bundle boundary: pad it until it
    //    reaches the end of the next bundle boundary.
    //
    // Note: this code could be made shorter with some modulo trickery, but it's
    // intentionally kept in its more explicit form for simplicity.
    if (EndOfFragment == BundleSize)
      return 0;
    else if (EndOfFragment < BundleSize)
      return BundleSize - EndOfFragment;
    else { // EndOfFragment > BundleSize
      return 2 * BundleSize - EndOfFragment;
    }
  } else if (OffsetInBundle > 0 && EndOfFragment > BundleSize)
    return BundleSize - OffsetInBundle;
  else
    return 0;
}

void MCAssembler::layoutBundle(MCFragment *Prev, MCFragment *F) const {
  // If bundling is enabled and this fragment has instructions in it, it has to
  // obey the bundling restrictions. With padding, we'll have:
  //
  //
  //        BundlePadding
  //             |||
  // -------------------------------------
  //   Prev  |##########|       F        |
  // -------------------------------------
  //                    ^
  //                    |
  //                    F->Offset
  //
  // The fragment's offset will point to after the padding, and its computed
  // size won't include the padding.
  //
  // ".align N" is an example of a directive that introduces multiple
  // fragments. We could add a special case to handle ".align N" by emitting
  // within-fragment padding (which would produce less padding when N is less
  // than the bundle size), but for now we don't.
  //
  assert(isa<MCEncodedFragment>(F) &&
         "Only MCEncodedFragment implementations have instructions");
  MCEncodedFragment *EF = cast<MCEncodedFragment>(F);
  uint64_t FSize = computeFragmentSize(*EF);

  if (FSize > getBundleAlignSize())
    report_fatal_error("Fragment can't be larger than a bundle size");

  uint64_t RequiredBundlePadding =
      computeBundlePadding(getBundleAlignSize(), EF, EF->Offset, FSize);
  if (RequiredBundlePadding > UINT8_MAX)
    report_fatal_error("Padding cannot exceed 255 bytes");
  EF->setBundlePadding(static_cast<uint8_t>(RequiredBundlePadding));
  EF->Offset += RequiredBundlePadding;
  if (auto *DF = dyn_cast_or_null<MCDataFragment>(Prev))
    if (DF->getContents().empty())
      DF->Offset = EF->Offset;
}

void MCAssembler::ensureValid(MCSection &Sec) const {
  if (Sec.hasLayout())
    return;
  Sec.setHasLayout(true);
  MCFragment *Prev = nullptr;
  uint64_t Offset = 0;
  for (MCFragment &F : Sec) {
    F.Offset = Offset;
    if (isBundlingEnabled() && F.hasInstructions()) {
      layoutBundle(Prev, &F);
      Offset = F.Offset;
    }
    Offset += computeFragmentSize(F);
    Prev = &F;
  }
}

uint64_t MCAssembler::getFragmentOffset(const MCFragment &F) const {
  ensureValid(*F.getParent());
  return F.Offset;
}

// Simple getSymbolOffset helper for the non-variable case.
static bool getLabelOffset(const MCAssembler &Asm, const MCSymbol &S,
                           bool ReportError, uint64_t &Val) {
  if (!S.getFragment()) {
    if (ReportError)
      report_fatal_error("unable to evaluate offset to undefined symbol '" +
                         S.getName() + "'");
    return false;
  }
  Val = Asm.getFragmentOffset(*S.getFragment()) + S.getOffset();
  return true;
}

static bool getSymbolOffsetImpl(const MCAssembler &Asm, const MCSymbol &S,
                                bool ReportError, uint64_t &Val) {
  if (!S.isVariable())
    return getLabelOffset(Asm, S, ReportError, Val);

  // If SD is a variable, evaluate it.
  MCValue Target;
  if (!S.getVariableValue()->evaluateAsValue(Target, Asm))
    report_fatal_error("unable to evaluate offset for variable '" +
                       S.getName() + "'");

  uint64_t Offset = Target.getConstant();

  const MCSymbolRefExpr *A = Target.getSymA();
  if (A) {
    uint64_t ValA;
    // FIXME: On most platforms, `Target`'s component symbols are labels from
    // having been simplified during evaluation, but on Mach-O they can be
    // variables due to PR19203. This, and the line below for `B` can be
    // restored to call `getLabelOffset` when PR19203 is fixed.
    if (!getSymbolOffsetImpl(Asm, A->getSymbol(), ReportError, ValA))
      return false;
    Offset += ValA;
  }

  const MCSymbolRefExpr *B = Target.getSymB();
  if (B) {
    uint64_t ValB;
    if (!getSymbolOffsetImpl(Asm, B->getSymbol(), ReportError, ValB))
      return false;
    Offset -= ValB;
  }

  Val = Offset;
  return true;
}

bool MCAssembler::getSymbolOffset(const MCSymbol &S, uint64_t &Val) const {
  return getSymbolOffsetImpl(*this, S, false, Val);
}

uint64_t MCAssembler::getSymbolOffset(const MCSymbol &S) const {
  uint64_t Val;
  getSymbolOffsetImpl(*this, S, true, Val);
  return Val;
}

const MCSymbol *MCAssembler::getBaseSymbol(const MCSymbol &Symbol) const {
  assert(HasLayout);
  if (!Symbol.isVariable())
    return &Symbol;

  const MCExpr *Expr = Symbol.getVariableValue();
  MCValue Value;
  if (!Expr->evaluateAsValue(Value, *this)) {
    getContext().reportError(Expr->getLoc(),
                             "expression could not be evaluated");
    return nullptr;
  }

  const MCSymbolRefExpr *RefB = Value.getSymB();
  if (RefB) {
    getContext().reportError(
        Expr->getLoc(),
        Twine("symbol '") + RefB->getSymbol().getName() +
            "' could not be evaluated in a subtraction expression");
    return nullptr;
  }

  const MCSymbolRefExpr *A = Value.getSymA();
  if (!A)
    return nullptr;

  const MCSymbol &ASym = A->getSymbol();
  if (ASym.isCommon()) {
    getContext().reportError(Expr->getLoc(),
                             "Common symbol '" + ASym.getName() +
                                 "' cannot be used in assignment expr");
    return nullptr;
  }

  return &ASym;
}

uint64_t MCAssembler::getSectionAddressSize(const MCSection &Sec) const {
  assert(HasLayout);
  // The size is the last fragment's end offset.
  const MCFragment &F = *Sec.curFragList()->Tail;
  return getFragmentOffset(F) + computeFragmentSize(F);
}

uint64_t MCAssembler::getSectionFileSize(const MCSection &Sec) const {
  // Virtual sections have no file size.
  if (Sec.isVirtualSection())
    return 0;
  return getSectionAddressSize(Sec);
}

bool MCAssembler::registerSymbol(const MCSymbol &Symbol) {
  bool Changed = !Symbol.isRegistered();
  if (Changed) {
    Symbol.setIsRegistered(true);
    Symbols.push_back(&Symbol);
  }
  return Changed;
}

void MCAssembler::writeFragmentPadding(raw_ostream &OS,
                                       const MCEncodedFragment &EF,
                                       uint64_t FSize) const {
  assert(getBackendPtr() && "Expected assembler backend");
  // Should NOP padding be written out before this fragment?
  unsigned BundlePadding = EF.getBundlePadding();
  if (BundlePadding > 0) {
    assert(isBundlingEnabled() &&
           "Writing bundle padding with disabled bundling");
    assert(EF.hasInstructions() &&
           "Writing bundle padding for a fragment without instructions");

    unsigned TotalLength = BundlePadding + static_cast<unsigned>(FSize);
    const MCSubtargetInfo *STI = EF.getSubtargetInfo();
    if (EF.alignToBundleEnd() && TotalLength > getBundleAlignSize()) {
      // If the padding itself crosses a bundle boundary, it must be emitted
      // in 2 pieces, since even nop instructions must not cross boundaries.
      //             v--------------v   <- BundleAlignSize
      //        v---------v             <- BundlePadding
      // ----------------------------
      // | Prev |####|####|    F    |
      // ----------------------------
      //        ^-------------------^   <- TotalLength
      unsigned DistanceToBoundary = TotalLength - getBundleAlignSize();
      if (!getBackend().writeNopData(OS, DistanceToBoundary, STI))
        report_fatal_error("unable to write NOP sequence of " +
                           Twine(DistanceToBoundary) + " bytes");
      BundlePadding -= DistanceToBoundary;
    }
    if (!getBackend().writeNopData(OS, BundlePadding, STI))
      report_fatal_error("unable to write NOP sequence of " +
                         Twine(BundlePadding) + " bytes");
  }
}

/// Write the fragment \p F to the output file.
static void writeFragment(raw_ostream &OS, const MCAssembler &Asm,
                          const MCFragment &F) {
  // FIXME: Embed in fragments instead?
  uint64_t FragmentSize = Asm.computeFragmentSize(F);

  llvm::endianness Endian = Asm.getBackend().Endian;

  if (const MCEncodedFragment *EF = dyn_cast<MCEncodedFragment>(&F))
    Asm.writeFragmentPadding(OS, *EF, FragmentSize);

  // This variable (and its dummy usage) is to participate in the assert at
  // the end of the function.
  uint64_t Start = OS.tell();
  (void) Start;

  ++stats::EmittedFragments;

  switch (F.getKind()) {
  case MCFragment::FT_Align: {
    ++stats::EmittedAlignFragments;
    const MCAlignFragment &AF = cast<MCAlignFragment>(F);
    assert(AF.getValueSize() && "Invalid virtual align in concrete fragment!");

    uint64_t Count = FragmentSize / AF.getValueSize();

    // FIXME: This error shouldn't actually occur (the front end should emit
    // multiple .align directives to enforce the semantics it wants), but is
    // severe enough that we want to report it. How to handle this?
    if (Count * AF.getValueSize() != FragmentSize)
      report_fatal_error("undefined .align directive, value size '" +
                        Twine(AF.getValueSize()) +
                        "' is not a divisor of padding size '" +
                        Twine(FragmentSize) + "'");

    // See if we are aligning with nops, and if so do that first to try to fill
    // the Count bytes.  Then if that did not fill any bytes or there are any
    // bytes left to fill use the Value and ValueSize to fill the rest.
    // If we are aligning with nops, ask that target to emit the right data.
    if (AF.hasEmitNops()) {
      if (!Asm.getBackend().writeNopData(OS, Count, AF.getSubtargetInfo()))
        report_fatal_error("unable to write nop sequence of " +
                          Twine(Count) + " bytes");
      break;
    }

    // Otherwise, write out in multiples of the value size.
    for (uint64_t i = 0; i != Count; ++i) {
      switch (AF.getValueSize()) {
      default: llvm_unreachable("Invalid size!");
      case 1: OS << char(AF.getValue()); break;
      case 2:
        support::endian::write<uint16_t>(OS, AF.getValue(), Endian);
        break;
      case 4:
        support::endian::write<uint32_t>(OS, AF.getValue(), Endian);
        break;
      case 8:
        support::endian::write<uint64_t>(OS, AF.getValue(), Endian);
        break;
      }
    }
    break;
  }

  case MCFragment::FT_Data:
    ++stats::EmittedDataFragments;
    OS << cast<MCDataFragment>(F).getContents();
    break;

  case MCFragment::FT_Relaxable:
    ++stats::EmittedRelaxableFragments;
    OS << cast<MCRelaxableFragment>(F).getContents();
    break;

  case MCFragment::FT_CompactEncodedInst:
    ++stats::EmittedCompactEncodedInstFragments;
    OS << cast<MCCompactEncodedInstFragment>(F).getContents();
    break;

  case MCFragment::FT_Fill: {
    ++stats::EmittedFillFragments;
    const MCFillFragment &FF = cast<MCFillFragment>(F);
    uint64_t V = FF.getValue();
    unsigned VSize = FF.getValueSize();
    const unsigned MaxChunkSize = 16;
    char Data[MaxChunkSize];
    assert(0 < VSize && VSize <= MaxChunkSize && "Illegal fragment fill size");
    // Duplicate V into Data as byte vector to reduce number of
    // writes done. As such, do endian conversion here.
    for (unsigned I = 0; I != VSize; ++I) {
      unsigned index = Endian == llvm::endianness::little ? I : (VSize - I - 1);
      Data[I] = uint8_t(V >> (index * 8));
    }
    for (unsigned I = VSize; I < MaxChunkSize; ++I)
      Data[I] = Data[I - VSize];

    // Set to largest multiple of VSize in Data.
    const unsigned NumPerChunk = MaxChunkSize / VSize;
    // Set ChunkSize to largest multiple of VSize in Data
    const unsigned ChunkSize = VSize * NumPerChunk;

    // Do copies by chunk.
    StringRef Ref(Data, ChunkSize);
    for (uint64_t I = 0, E = FragmentSize / ChunkSize; I != E; ++I)
      OS << Ref;

    // do remainder if needed.
    unsigned TrailingCount = FragmentSize % ChunkSize;
    if (TrailingCount)
      OS.write(Data, TrailingCount);
    break;
  }

  case MCFragment::FT_Nops: {
    ++stats::EmittedNopsFragments;
    const MCNopsFragment &NF = cast<MCNopsFragment>(F);

    int64_t NumBytes = NF.getNumBytes();
    int64_t ControlledNopLength = NF.getControlledNopLength();
    int64_t MaximumNopLength =
        Asm.getBackend().getMaximumNopSize(*NF.getSubtargetInfo());

    assert(NumBytes > 0 && "Expected positive NOPs fragment size");
    assert(ControlledNopLength >= 0 && "Expected non-negative NOP size");

    if (ControlledNopLength > MaximumNopLength) {
      Asm.getContext().reportError(NF.getLoc(),
                                   "illegal NOP size " +
                                       std::to_string(ControlledNopLength) +
                                       ". (expected within [0, " +
                                       std::to_string(MaximumNopLength) + "])");
      // Clamp the NOP length as reportError does not stop the execution
      // immediately.
      ControlledNopLength = MaximumNopLength;
    }

    // Use maximum value if the size of each NOP is not specified
    if (!ControlledNopLength)
      ControlledNopLength = MaximumNopLength;

    while (NumBytes) {
      uint64_t NumBytesToEmit =
          (uint64_t)std::min(NumBytes, ControlledNopLength);
      assert(NumBytesToEmit && "try to emit empty NOP instruction");
      if (!Asm.getBackend().writeNopData(OS, NumBytesToEmit,
                                         NF.getSubtargetInfo())) {
        report_fatal_error("unable to write nop sequence of the remaining " +
                           Twine(NumBytesToEmit) + " bytes");
        break;
      }
      NumBytes -= NumBytesToEmit;
    }
    break;
  }

  case MCFragment::FT_LEB: {
    const MCLEBFragment &LF = cast<MCLEBFragment>(F);
    OS << LF.getContents();
    break;
  }

  case MCFragment::FT_BoundaryAlign: {
    const MCBoundaryAlignFragment &BF = cast<MCBoundaryAlignFragment>(F);
    if (!Asm.getBackend().writeNopData(OS, FragmentSize, BF.getSubtargetInfo()))
      report_fatal_error("unable to write nop sequence of " +
                         Twine(FragmentSize) + " bytes");
    break;
  }

  case MCFragment::FT_SymbolId: {
    const MCSymbolIdFragment &SF = cast<MCSymbolIdFragment>(F);
    support::endian::write<uint32_t>(OS, SF.getSymbol()->getIndex(), Endian);
    break;
  }

  case MCFragment::FT_Org: {
    ++stats::EmittedOrgFragments;
    const MCOrgFragment &OF = cast<MCOrgFragment>(F);

    for (uint64_t i = 0, e = FragmentSize; i != e; ++i)
      OS << char(OF.getValue());

    break;
  }

  case MCFragment::FT_Dwarf: {
    const MCDwarfLineAddrFragment &OF = cast<MCDwarfLineAddrFragment>(F);
    OS << OF.getContents();
    break;
  }
  case MCFragment::FT_DwarfFrame: {
    const MCDwarfCallFrameFragment &CF = cast<MCDwarfCallFrameFragment>(F);
    OS << CF.getContents();
    break;
  }
  case MCFragment::FT_CVInlineLines: {
    const auto &OF = cast<MCCVInlineLineTableFragment>(F);
    OS << OF.getContents();
    break;
  }
  case MCFragment::FT_CVDefRange: {
    const auto &DRF = cast<MCCVDefRangeFragment>(F);
    OS << DRF.getContents();
    break;
  }
  case MCFragment::FT_PseudoProbe: {
    const MCPseudoProbeAddrFragment &PF = cast<MCPseudoProbeAddrFragment>(F);
    OS << PF.getContents();
    break;
  }
  case MCFragment::FT_Dummy:
    llvm_unreachable("Should not have been added");
  }

  assert(OS.tell() - Start == FragmentSize &&
         "The stream should advance by fragment size");
}

void MCAssembler::writeSectionData(raw_ostream &OS,
                                   const MCSection *Sec) const {
  assert(getBackendPtr() && "Expected assembler backend");

  // Ignore virtual sections.
  if (Sec->isVirtualSection()) {
    assert(getSectionFileSize(*Sec) == 0 && "Invalid size for section!");

    // Check that contents are only things legal inside a virtual section.
    for (const MCFragment &F : *Sec) {
      switch (F.getKind()) {
      default: llvm_unreachable("Invalid fragment in virtual section!");
      case MCFragment::FT_Data: {
        // Check that we aren't trying to write a non-zero contents (or fixups)
        // into a virtual section. This is to support clients which use standard
        // directives to fill the contents of virtual sections.
        const MCDataFragment &DF = cast<MCDataFragment>(F);
        if (DF.fixup_begin() != DF.fixup_end())
          getContext().reportError(SMLoc(), Sec->getVirtualSectionKind() +
                                                " section '" + Sec->getName() +
                                                "' cannot have fixups");
        for (unsigned i = 0, e = DF.getContents().size(); i != e; ++i)
          if (DF.getContents()[i]) {
            getContext().reportError(SMLoc(),
                                     Sec->getVirtualSectionKind() +
                                         " section '" + Sec->getName() +
                                         "' cannot have non-zero initializers");
            break;
          }
        break;
      }
      case MCFragment::FT_Align:
        // Check that we aren't trying to write a non-zero value into a virtual
        // section.
        assert((cast<MCAlignFragment>(F).getValueSize() == 0 ||
                cast<MCAlignFragment>(F).getValue() == 0) &&
               "Invalid align in virtual section!");
        break;
      case MCFragment::FT_Fill:
        assert((cast<MCFillFragment>(F).getValue() == 0) &&
               "Invalid fill in virtual section!");
        break;
      case MCFragment::FT_Org:
        break;
      }
    }

    return;
  }

  uint64_t Start = OS.tell();
  (void)Start;

  for (const MCFragment &F : *Sec)
    writeFragment(OS, *this, F);

  assert(getContext().hadError() ||
         OS.tell() - Start == getSectionAddressSize(*Sec));
}

std::tuple<MCValue, uint64_t, bool>
MCAssembler::handleFixup(MCFragment &F, const MCFixup &Fixup,
                         const MCSubtargetInfo *STI) {
  // Evaluate the fixup.
  MCValue Target;
  uint64_t FixedValue;
  bool WasForced;
  bool IsResolved =
      evaluateFixup(Fixup, &F, Target, STI, FixedValue, WasForced);
  if (!IsResolved) {
    // The fixup was unresolved, we need a relocation. Inform the object
    // writer of the relocation, and give it an opportunity to adjust the
    // fixup value if need be.
    getWriter().recordRelocation(*this, &F, Fixup, Target, FixedValue);
  }
  return std::make_tuple(Target, FixedValue, IsResolved);
}

void MCAssembler::layout() {
  assert(getBackendPtr() && "Expected assembler backend");
  DEBUG_WITH_TYPE("mc-dump", {
      errs() << "assembler backend - pre-layout\n--\n";
      dump(); });

  // Assign section ordinals.
  unsigned SectionIndex = 0;
  for (MCSection &Sec : *this) {
    Sec.setOrdinal(SectionIndex++);

    // Chain together fragments from all subsections.
    if (Sec.Subsections.size() > 1) {
      MCDummyFragment Dummy;
      MCFragment *Tail = &Dummy;
      for (auto &[_, List] : Sec.Subsections) {
        assert(List.Head);
        Tail->Next = List.Head;
        Tail = List.Tail;
      }
      Sec.Subsections.clear();
      Sec.Subsections.push_back({0u, {Dummy.getNext(), Tail}});
      Sec.CurFragList = &Sec.Subsections[0].second;

      unsigned FragmentIndex = 0;
      for (MCFragment &Frag : Sec)
        Frag.setLayoutOrder(FragmentIndex++);
    }
  }

  // Layout until everything fits.
  this->HasLayout = true;
  while (layoutOnce()) {
    if (getContext().hadError())
      return;
    // Size of fragments in one section can depend on the size of fragments in
    // another. If any fragment has changed size, we have to re-layout (and
    // as a result possibly further relax) all.
    for (MCSection &Sec : *this)
      Sec.setHasLayout(false);
  }

  DEBUG_WITH_TYPE("mc-dump", {
      errs() << "assembler backend - post-relaxation\n--\n";
      dump(); });

  // Finalize the layout, including fragment lowering.
  getBackend().finishLayout(*this);

  DEBUG_WITH_TYPE("mc-dump", {
      errs() << "assembler backend - final-layout\n--\n";
      dump(); });

  // Allow the object writer a chance to perform post-layout binding (for
  // example, to set the index fields in the symbol data).
  getWriter().executePostLayoutBinding(*this);

  // Evaluate and apply the fixups, generating relocation entries as necessary.
  for (MCSection &Sec : *this) {
    for (MCFragment &Frag : Sec) {
      ArrayRef<MCFixup> Fixups;
      MutableArrayRef<char> Contents;
      const MCSubtargetInfo *STI = nullptr;

      // Process MCAlignFragment and MCEncodedFragmentWithFixups here.
      switch (Frag.getKind()) {
      default:
        continue;
      case MCFragment::FT_Align: {
        MCAlignFragment &AF = cast<MCAlignFragment>(Frag);
        // Insert fixup type for code alignment if the target define
        // shouldInsertFixupForCodeAlign target hook.
        if (Sec.useCodeAlign() && AF.hasEmitNops())
          getBackend().shouldInsertFixupForCodeAlign(*this, AF);
        continue;
      }
      case MCFragment::FT_Data: {
        MCDataFragment &DF = cast<MCDataFragment>(Frag);
        Fixups = DF.getFixups();
        Contents = DF.getContents();
        STI = DF.getSubtargetInfo();
        assert(!DF.hasInstructions() || STI != nullptr);
        break;
      }
      case MCFragment::FT_Relaxable: {
        MCRelaxableFragment &RF = cast<MCRelaxableFragment>(Frag);
        Fixups = RF.getFixups();
        Contents = RF.getContents();
        STI = RF.getSubtargetInfo();
        assert(!RF.hasInstructions() || STI != nullptr);
        break;
      }
      case MCFragment::FT_CVDefRange: {
        MCCVDefRangeFragment &CF = cast<MCCVDefRangeFragment>(Frag);
        Fixups = CF.getFixups();
        Contents = CF.getContents();
        break;
      }
      case MCFragment::FT_Dwarf: {
        MCDwarfLineAddrFragment &DF = cast<MCDwarfLineAddrFragment>(Frag);
        Fixups = DF.getFixups();
        Contents = DF.getContents();
        break;
      }
      case MCFragment::FT_DwarfFrame: {
        MCDwarfCallFrameFragment &DF = cast<MCDwarfCallFrameFragment>(Frag);
        Fixups = DF.getFixups();
        Contents = DF.getContents();
        break;
      }
      case MCFragment::FT_LEB: {
        auto &LF = cast<MCLEBFragment>(Frag);
        Fixups = LF.getFixups();
        Contents = LF.getContents();
        break;
      }
      case MCFragment::FT_PseudoProbe: {
        MCPseudoProbeAddrFragment &PF = cast<MCPseudoProbeAddrFragment>(Frag);
        Fixups = PF.getFixups();
        Contents = PF.getContents();
        break;
      }
      }
      for (const MCFixup &Fixup : Fixups) {
        uint64_t FixedValue;
        bool IsResolved;
        MCValue Target;
        std::tie(Target, FixedValue, IsResolved) =
            handleFixup(Frag, Fixup, STI);
        getBackend().applyFixup(*this, Fixup, Target, Contents, FixedValue,
                                IsResolved, STI);
      }
    }
  }
}

void MCAssembler::Finish() {
  layout();

  // Write the object file.
  stats::ObjectBytes += getWriter().writeObject(*this);

  HasLayout = false;
}

bool MCAssembler::fixupNeedsRelaxation(const MCFixup &Fixup,
                                       const MCRelaxableFragment *DF) const {
  assert(getBackendPtr() && "Expected assembler backend");
  MCValue Target;
  uint64_t Value;
  bool WasForced;
  bool Resolved = evaluateFixup(Fixup, DF, Target, DF->getSubtargetInfo(),
                                Value, WasForced);
  if (Target.getSymA() &&
      Target.getSymA()->getKind() == MCSymbolRefExpr::VK_X86_ABS8 &&
      Fixup.getKind() == FK_Data_1)
    return false;
  return getBackend().fixupNeedsRelaxationAdvanced(*this, Fixup, Resolved,
                                                   Value, DF, WasForced);
}

bool MCAssembler::fragmentNeedsRelaxation(const MCRelaxableFragment *F) const {
  assert(getBackendPtr() && "Expected assembler backend");
  // If this inst doesn't ever need relaxation, ignore it. This occurs when we
  // are intentionally pushing out inst fragments, or because we relaxed a
  // previous instruction to one that doesn't need relaxation.
  if (!getBackend().mayNeedRelaxation(F->getInst(), *F->getSubtargetInfo()))
    return false;

  for (const MCFixup &Fixup : F->getFixups())
    if (fixupNeedsRelaxation(Fixup, F))
      return true;

  return false;
}

bool MCAssembler::relaxInstruction(MCRelaxableFragment &F) {
  assert(getEmitterPtr() &&
         "Expected CodeEmitter defined for relaxInstruction");
  if (!fragmentNeedsRelaxation(&F))
    return false;

  ++stats::RelaxedInstructions;

  // FIXME-PERF: We could immediately lower out instructions if we can tell
  // they are fully resolved, to avoid retesting on later passes.

  // Relax the fragment.

  MCInst Relaxed = F.getInst();
  getBackend().relaxInstruction(Relaxed, *F.getSubtargetInfo());

  // Encode the new instruction.
  F.setInst(Relaxed);
  F.getFixups().clear();
  F.getContents().clear();
  getEmitter().encodeInstruction(Relaxed, F.getContents(), F.getFixups(),
                                 *F.getSubtargetInfo());
  return true;
}

bool MCAssembler::relaxLEB(MCLEBFragment &LF) {
  const unsigned OldSize = static_cast<unsigned>(LF.getContents().size());
  unsigned PadTo = OldSize;
  int64_t Value;
  SmallVectorImpl<char> &Data = LF.getContents();
  LF.getFixups().clear();
  // Use evaluateKnownAbsolute for Mach-O as a hack: .subsections_via_symbols
  // requires that .uleb128 A-B is foldable where A and B reside in different
  // fragments. This is used by __gcc_except_table.
  bool Abs = getWriter().getSubsectionsViaSymbols()
                 ? LF.getValue().evaluateKnownAbsolute(Value, *this)
                 : LF.getValue().evaluateAsAbsolute(Value, *this);
  if (!Abs) {
    bool Relaxed, UseZeroPad;
    std::tie(Relaxed, UseZeroPad) = getBackend().relaxLEB128(*this, LF, Value);
    if (!Relaxed) {
      getContext().reportError(LF.getValue().getLoc(),
                               Twine(LF.isSigned() ? ".s" : ".u") +
                                   "leb128 expression is not absolute");
      LF.setValue(MCConstantExpr::create(0, Context));
    }
    uint8_t Tmp[10]; // maximum size: ceil(64/7)
    PadTo = std::max(PadTo, encodeULEB128(uint64_t(Value), Tmp));
    if (UseZeroPad)
      Value = 0;
  }
  Data.clear();
  raw_svector_ostream OSE(Data);
  // The compiler can generate EH table assembly that is impossible to assemble
  // without either adding padding to an LEB fragment or adding extra padding
  // to a later alignment fragment. To accommodate such tables, relaxation can
  // only increase an LEB fragment size here, not decrease it. See PR35809.
  if (LF.isSigned())
    encodeSLEB128(Value, OSE, PadTo);
  else
    encodeULEB128(Value, OSE, PadTo);
  return OldSize != LF.getContents().size();
}

/// Check if the branch crosses the boundary.
///
/// \param StartAddr start address of the fused/unfused branch.
/// \param Size size of the fused/unfused branch.
/// \param BoundaryAlignment alignment requirement of the branch.
/// \returns true if the branch cross the boundary.
static bool mayCrossBoundary(uint64_t StartAddr, uint64_t Size,
                             Align BoundaryAlignment) {
  uint64_t EndAddr = StartAddr + Size;
  return (StartAddr >> Log2(BoundaryAlignment)) !=
         ((EndAddr - 1) >> Log2(BoundaryAlignment));
}

/// Check if the branch is against the boundary.
///
/// \param StartAddr start address of the fused/unfused branch.
/// \param Size size of the fused/unfused branch.
/// \param BoundaryAlignment alignment requirement of the branch.
/// \returns true if the branch is against the boundary.
static bool isAgainstBoundary(uint64_t StartAddr, uint64_t Size,
                              Align BoundaryAlignment) {
  uint64_t EndAddr = StartAddr + Size;
  return (EndAddr & (BoundaryAlignment.value() - 1)) == 0;
}

/// Check if the branch needs padding.
///
/// \param StartAddr start address of the fused/unfused branch.
/// \param Size size of the fused/unfused branch.
/// \param BoundaryAlignment alignment requirement of the branch.
/// \returns true if the branch needs padding.
static bool needPadding(uint64_t StartAddr, uint64_t Size,
                        Align BoundaryAlignment) {
  return mayCrossBoundary(StartAddr, Size, BoundaryAlignment) ||
         isAgainstBoundary(StartAddr, Size, BoundaryAlignment);
}

bool MCAssembler::relaxBoundaryAlign(MCBoundaryAlignFragment &BF) {
  // BoundaryAlignFragment that doesn't need to align any fragment should not be
  // relaxed.
  if (!BF.getLastFragment())
    return false;

  uint64_t AlignedOffset = getFragmentOffset(BF);
  uint64_t AlignedSize = 0;
  for (const MCFragment *F = BF.getNext();; F = F->getNext()) {
    AlignedSize += computeFragmentSize(*F);
    if (F == BF.getLastFragment())
      break;
  }

  Align BoundaryAlignment = BF.getAlignment();
  uint64_t NewSize = needPadding(AlignedOffset, AlignedSize, BoundaryAlignment)
                         ? offsetToAlignment(AlignedOffset, BoundaryAlignment)
                         : 0U;
  if (NewSize == BF.getSize())
    return false;
  BF.setSize(NewSize);
  return true;
}

bool MCAssembler::relaxDwarfLineAddr(MCDwarfLineAddrFragment &DF) {
  bool WasRelaxed;
  if (getBackend().relaxDwarfLineAddr(*this, DF, WasRelaxed))
    return WasRelaxed;

  MCContext &Context = getContext();
  uint64_t OldSize = DF.getContents().size();
  int64_t AddrDelta;
  bool Abs = DF.getAddrDelta().evaluateKnownAbsolute(AddrDelta, *this);
  assert(Abs && "We created a line delta with an invalid expression");
  (void)Abs;
  int64_t LineDelta;
  LineDelta = DF.getLineDelta();
  SmallVectorImpl<char> &Data = DF.getContents();
  Data.clear();
  DF.getFixups().clear();

  MCDwarfLineAddr::encode(Context, getDWARFLinetableParams(), LineDelta,
                          AddrDelta, Data);
  return OldSize != Data.size();
}

bool MCAssembler::relaxDwarfCallFrameFragment(MCDwarfCallFrameFragment &DF) {
  bool WasRelaxed;
  if (getBackend().relaxDwarfCFA(*this, DF, WasRelaxed))
    return WasRelaxed;

  MCContext &Context = getContext();
  int64_t Value;
  bool Abs = DF.getAddrDelta().evaluateAsAbsolute(Value, *this);
  if (!Abs) {
    getContext().reportError(DF.getAddrDelta().getLoc(),
                             "invalid CFI advance_loc expression");
    DF.setAddrDelta(MCConstantExpr::create(0, Context));
    return false;
  }

  SmallVectorImpl<char> &Data = DF.getContents();
  uint64_t OldSize = Data.size();
  Data.clear();
  DF.getFixups().clear();

  MCDwarfFrameEmitter::encodeAdvanceLoc(Context, Value, Data);
  return OldSize != Data.size();
}

bool MCAssembler::relaxCVInlineLineTable(MCCVInlineLineTableFragment &F) {
  unsigned OldSize = F.getContents().size();
  getContext().getCVContext().encodeInlineLineTable(*this, F);
  return OldSize != F.getContents().size();
}

bool MCAssembler::relaxCVDefRange(MCCVDefRangeFragment &F) {
  unsigned OldSize = F.getContents().size();
  getContext().getCVContext().encodeDefRange(*this, F);
  return OldSize != F.getContents().size();
}

bool MCAssembler::relaxPseudoProbeAddr(MCPseudoProbeAddrFragment &PF) {
  uint64_t OldSize = PF.getContents().size();
  int64_t AddrDelta;
  bool Abs = PF.getAddrDelta().evaluateKnownAbsolute(AddrDelta, *this);
  assert(Abs && "We created a pseudo probe with an invalid expression");
  (void)Abs;
  SmallVectorImpl<char> &Data = PF.getContents();
  Data.clear();
  raw_svector_ostream OSE(Data);
  PF.getFixups().clear();

  // AddrDelta is a signed integer
  encodeSLEB128(AddrDelta, OSE, OldSize);
  return OldSize != Data.size();
}

bool MCAssembler::relaxFragment(MCFragment &F) {
  switch(F.getKind()) {
  default:
    return false;
  case MCFragment::FT_Relaxable:
    assert(!getRelaxAll() &&
           "Did not expect a MCRelaxableFragment in RelaxAll mode");
    return relaxInstruction(cast<MCRelaxableFragment>(F));
  case MCFragment::FT_Dwarf:
    return relaxDwarfLineAddr(cast<MCDwarfLineAddrFragment>(F));
  case MCFragment::FT_DwarfFrame:
    return relaxDwarfCallFrameFragment(cast<MCDwarfCallFrameFragment>(F));
  case MCFragment::FT_LEB:
    return relaxLEB(cast<MCLEBFragment>(F));
  case MCFragment::FT_BoundaryAlign:
    return relaxBoundaryAlign(cast<MCBoundaryAlignFragment>(F));
  case MCFragment::FT_CVInlineLines:
    return relaxCVInlineLineTable(cast<MCCVInlineLineTableFragment>(F));
  case MCFragment::FT_CVDefRange:
    return relaxCVDefRange(cast<MCCVDefRangeFragment>(F));
  case MCFragment::FT_PseudoProbe:
    return relaxPseudoProbeAddr(cast<MCPseudoProbeAddrFragment>(F));
  }
}

bool MCAssembler::layoutOnce() {
  ++stats::RelaxationSteps;

  bool Changed = false;
  for (MCSection &Sec : *this)
    for (MCFragment &Frag : Sec)
      if (relaxFragment(Frag))
        Changed = true;
  return Changed;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MCAssembler::dump() const{
  raw_ostream &OS = errs();

  OS << "<MCAssembler\n";
  OS << "  Sections:[\n    ";
  bool First = true;
  for (const MCSection &Sec : *this) {
    if (First)
      First = false;
    else
      OS << ",\n    ";
    Sec.dump();
  }
  OS << "],\n";
  OS << "  Symbols:[";

  First = true;
  for (const MCSymbol &Sym : symbols()) {
    if (First)
      First = false;
    else
      OS << ",\n           ";
    OS << "(";
    Sym.dump();
    OS << ", Index:" << Sym.getIndex() << ", ";
    OS << ")";
  }
  OS << "]>\n";
}
#endif
