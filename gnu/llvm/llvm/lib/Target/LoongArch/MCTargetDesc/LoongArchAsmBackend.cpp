//===-- LoongArchAsmBackend.cpp - LoongArch Assembler Backend -*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LoongArchAsmBackend class.
//
//===----------------------------------------------------------------------===//

#include "LoongArchAsmBackend.h"
#include "LoongArchFixupKinds.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"

#define DEBUG_TYPE "loongarch-asmbackend"

using namespace llvm;

std::optional<MCFixupKind>
LoongArchAsmBackend::getFixupKind(StringRef Name) const {
  if (STI.getTargetTriple().isOSBinFormatELF()) {
    auto Type = llvm::StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y) .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/LoongArch.def"
#undef ELF_RELOC
                    .Case("BFD_RELOC_NONE", ELF::R_LARCH_NONE)
                    .Case("BFD_RELOC_32", ELF::R_LARCH_32)
                    .Case("BFD_RELOC_64", ELF::R_LARCH_64)
                    .Default(-1u);
    if (Type != -1u)
      return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
  }
  return std::nullopt;
}

const MCFixupKindInfo &
LoongArchAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  const static MCFixupKindInfo Infos[] = {
      // This table *must* be in the order that the fixup_* kinds are defined in
      // LoongArchFixupKinds.h.
      //
      // {name, offset, bits, flags}
      {"fixup_loongarch_b16", 10, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_loongarch_b21", 0, 26, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_loongarch_b26", 0, 26, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_loongarch_abs_hi20", 5, 20, 0},
      {"fixup_loongarch_abs_lo12", 10, 12, 0},
      {"fixup_loongarch_abs64_lo20", 5, 20, 0},
      {"fixup_loongarch_abs64_hi12", 10, 12, 0},
      {"fixup_loongarch_tls_le_hi20", 5, 20, 0},
      {"fixup_loongarch_tls_le_lo12", 10, 12, 0},
      {"fixup_loongarch_tls_le64_lo20", 5, 20, 0},
      {"fixup_loongarch_tls_le64_hi12", 10, 12, 0},
      // TODO: Add more fixup kinds.
  };

  static_assert((std::size(Infos)) == LoongArch::NumTargetFixupKinds,
                "Not all fixup kinds added to Infos array");

  // Fixup kinds from .reloc directive are like R_LARCH_NONE. They
  // do not require any extra processing.
  if (Kind >= FirstLiteralRelocationKind)
    return MCAsmBackend::getFixupKindInfo(FK_NONE);

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  return Infos[Kind - FirstTargetFixupKind];
}

static void reportOutOfRangeError(MCContext &Ctx, SMLoc Loc, unsigned N) {
  Ctx.reportError(Loc, "fixup value out of range [" + Twine(llvm::minIntN(N)) +
                           ", " + Twine(llvm::maxIntN(N)) + "]");
}

static uint64_t adjustFixupValue(const MCFixup &Fixup, uint64_t Value,
                                 MCContext &Ctx) {
  switch (Fixup.getTargetKind()) {
  default:
    llvm_unreachable("Unknown fixup kind");
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
  case FK_Data_leb128:
    return Value;
  case LoongArch::fixup_loongarch_b16: {
    if (!isInt<18>(Value))
      reportOutOfRangeError(Ctx, Fixup.getLoc(), 18);
    if (Value % 4)
      Ctx.reportError(Fixup.getLoc(), "fixup value must be 4-byte aligned");
    return (Value >> 2) & 0xffff;
  }
  case LoongArch::fixup_loongarch_b21: {
    if (!isInt<23>(Value))
      reportOutOfRangeError(Ctx, Fixup.getLoc(), 23);
    if (Value % 4)
      Ctx.reportError(Fixup.getLoc(), "fixup value must be 4-byte aligned");
    return ((Value & 0x3fffc) << 8) | ((Value >> 18) & 0x1f);
  }
  case LoongArch::fixup_loongarch_b26: {
    if (!isInt<28>(Value))
      reportOutOfRangeError(Ctx, Fixup.getLoc(), 28);
    if (Value % 4)
      Ctx.reportError(Fixup.getLoc(), "fixup value must be 4-byte aligned");
    return ((Value & 0x3fffc) << 8) | ((Value >> 18) & 0x3ff);
  }
  case LoongArch::fixup_loongarch_abs_hi20:
  case LoongArch::fixup_loongarch_tls_le_hi20:
    return (Value >> 12) & 0xfffff;
  case LoongArch::fixup_loongarch_abs_lo12:
  case LoongArch::fixup_loongarch_tls_le_lo12:
    return Value & 0xfff;
  case LoongArch::fixup_loongarch_abs64_lo20:
  case LoongArch::fixup_loongarch_tls_le64_lo20:
    return (Value >> 32) & 0xfffff;
  case LoongArch::fixup_loongarch_abs64_hi12:
  case LoongArch::fixup_loongarch_tls_le64_hi12:
    return (Value >> 52) & 0xfff;
  }
}

static void fixupLeb128(MCContext &Ctx, const MCFixup &Fixup,
                        MutableArrayRef<char> Data, uint64_t Value) {
  unsigned I;
  for (I = 0; I != Data.size() && Value; ++I, Value >>= 7)
    Data[I] |= uint8_t(Value & 0x7f);
  if (Value)
    Ctx.reportError(Fixup.getLoc(), "Invalid uleb128 value!");
}

void LoongArchAsmBackend::applyFixup(const MCAssembler &Asm,
                                     const MCFixup &Fixup,
                                     const MCValue &Target,
                                     MutableArrayRef<char> Data, uint64_t Value,
                                     bool IsResolved,
                                     const MCSubtargetInfo *STI) const {
  if (!Value)
    return; // Doesn't change encoding.

  MCFixupKind Kind = Fixup.getKind();
  if (Kind >= FirstLiteralRelocationKind)
    return;
  MCFixupKindInfo Info = getFixupKindInfo(Kind);
  MCContext &Ctx = Asm.getContext();

  // Fixup leb128 separately.
  if (Fixup.getTargetKind() == FK_Data_leb128)
    return fixupLeb128(Ctx, Fixup, Data, Value);

  // Apply any target-specific value adjustments.
  Value = adjustFixupValue(Fixup, Value, Ctx);

  // Shift the value into position.
  Value <<= Info.TargetOffset;

  unsigned Offset = Fixup.getOffset();
  unsigned NumBytes = alignTo(Info.TargetSize + Info.TargetOffset, 8) / 8;

  assert(Offset + NumBytes <= Data.size() && "Invalid fixup offset!");
  // For each byte of the fragment that the fixup touches, mask in the
  // bits from the fixup value.
  for (unsigned I = 0; I != NumBytes; ++I) {
    Data[Offset + I] |= uint8_t((Value >> (I * 8)) & 0xff);
  }
}

// Linker relaxation may change code size. We have to insert Nops
// for .align directive when linker relaxation enabled. So then Linker
// could satisfy alignment by removing Nops.
// The function returns the total Nops Size we need to insert.
bool LoongArchAsmBackend::shouldInsertExtraNopBytesForCodeAlign(
    const MCAlignFragment &AF, unsigned &Size) {
  // Calculate Nops Size only when linker relaxation enabled.
  if (!AF.getSubtargetInfo()->hasFeature(LoongArch::FeatureRelax))
    return false;

  // Ignore alignment if MaxBytesToEmit is less than the minimum Nop size.
  const unsigned MinNopLen = 4;
  if (AF.getMaxBytesToEmit() < MinNopLen)
    return false;
  Size = AF.getAlignment().value() - MinNopLen;
  return AF.getAlignment() > MinNopLen;
}

// We need to insert R_LARCH_ALIGN relocation type to indicate the
// position of Nops and the total bytes of the Nops have been inserted
// when linker relaxation enabled.
// The function inserts fixup_loongarch_align fixup which eventually will
// transfer to R_LARCH_ALIGN relocation type.
// The improved R_LARCH_ALIGN requires symbol index. The lowest 8 bits of
// addend represent alignment and the other bits of addend represent the
// maximum number of bytes to emit. The maximum number of bytes is zero
// means ignore the emit limit.
bool LoongArchAsmBackend::shouldInsertFixupForCodeAlign(MCAssembler &Asm,
                                                        MCAlignFragment &AF) {
  // Insert the fixup only when linker relaxation enabled.
  if (!AF.getSubtargetInfo()->hasFeature(LoongArch::FeatureRelax))
    return false;

  // Calculate total Nops we need to insert. If there are none to insert
  // then simply return.
  unsigned InsertedNopBytes;
  if (!shouldInsertExtraNopBytesForCodeAlign(AF, InsertedNopBytes))
    return false;

  MCSection *Sec = AF.getParent();
  MCContext &Ctx = Asm.getContext();
  const MCExpr *Dummy = MCConstantExpr::create(0, Ctx);
  // Create fixup_loongarch_align fixup.
  MCFixup Fixup =
      MCFixup::create(0, Dummy, MCFixupKind(LoongArch::fixup_loongarch_align));
  unsigned MaxBytesToEmit = AF.getMaxBytesToEmit();

  auto createExtendedValue = [&]() {
    const MCSymbolRefExpr *MCSym = getSecToAlignSym()[Sec];
    if (MCSym == nullptr) {
      // Define a marker symbol at the section with an offset of 0.
      MCSymbol *Sym = Ctx.createNamedTempSymbol("la-relax-align");
      Sym->setFragment(&*Sec->getBeginSymbol()->getFragment());
      Asm.registerSymbol(*Sym);
      MCSym = MCSymbolRefExpr::create(Sym, Ctx);
      getSecToAlignSym()[Sec] = MCSym;
    }
    return MCValue::get(MCSym, nullptr,
                        MaxBytesToEmit << 8 | Log2(AF.getAlignment()));
  };

  uint64_t FixedValue = 0;
  MCValue Value = MaxBytesToEmit >= InsertedNopBytes
                      ? MCValue::get(InsertedNopBytes)
                      : createExtendedValue();
  Asm.getWriter().recordRelocation(Asm, &AF, Fixup, Value, FixedValue);

  return true;
}

bool LoongArchAsmBackend::shouldForceRelocation(const MCAssembler &Asm,
                                                const MCFixup &Fixup,
                                                const MCValue &Target,
                                                const MCSubtargetInfo *STI) {
  if (Fixup.getKind() >= FirstLiteralRelocationKind)
    return true;
  switch (Fixup.getTargetKind()) {
  default:
    return STI->hasFeature(LoongArch::FeatureRelax);
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
  case FK_Data_leb128:
    return !Target.isAbsolute();
  }
}

static inline std::pair<MCFixupKind, MCFixupKind>
getRelocPairForSize(unsigned Size) {
  switch (Size) {
  default:
    llvm_unreachable("unsupported fixup size");
  case 6:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_ADD6),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_SUB6));
  case 8:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_ADD8),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_SUB8));
  case 16:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_ADD16),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_SUB16));
  case 32:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_ADD32),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_SUB32));
  case 64:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_ADD64),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_SUB64));
  case 128:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_ADD_ULEB128),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_LARCH_SUB_ULEB128));
  }
}

std::pair<bool, bool> LoongArchAsmBackend::relaxLEB128(const MCAssembler &Asm,
                                                       MCLEBFragment &LF,
                                                       int64_t &Value) const {
  const MCExpr &Expr = LF.getValue();
  if (LF.isSigned() || !Expr.evaluateKnownAbsolute(Value, Asm))
    return std::make_pair(false, false);
  LF.getFixups().push_back(
      MCFixup::create(0, &Expr, FK_Data_leb128, Expr.getLoc()));
  return std::make_pair(true, true);
}

bool LoongArchAsmBackend::relaxDwarfLineAddr(const MCAssembler &Asm,
                                             MCDwarfLineAddrFragment &DF,
                                             bool &WasRelaxed) const {
  MCContext &C = Asm.getContext();

  int64_t LineDelta = DF.getLineDelta();
  const MCExpr &AddrDelta = DF.getAddrDelta();
  SmallVectorImpl<char> &Data = DF.getContents();
  SmallVectorImpl<MCFixup> &Fixups = DF.getFixups();
  size_t OldSize = Data.size();

  int64_t Value;
  if (AddrDelta.evaluateAsAbsolute(Value, Asm))
    return false;
  bool IsAbsolute = AddrDelta.evaluateKnownAbsolute(Value, Asm);
  assert(IsAbsolute && "CFA with invalid expression");
  (void)IsAbsolute;

  Data.clear();
  Fixups.clear();
  raw_svector_ostream OS(Data);

  // INT64_MAX is a signal that this is actually a DW_LNE_end_sequence.
  if (LineDelta != INT64_MAX) {
    OS << uint8_t(dwarf::DW_LNS_advance_line);
    encodeSLEB128(LineDelta, OS);
  }

  unsigned Offset;
  std::pair<MCFixupKind, MCFixupKind> FK;

  // According to the DWARF specification, the `DW_LNS_fixed_advance_pc` opcode
  // takes a single unsigned half (unencoded) operand. The maximum encodable
  // value is therefore 65535.  Set a conservative upper bound for relaxation.
  if (Value > 60000) {
    unsigned PtrSize = C.getAsmInfo()->getCodePointerSize();

    OS << uint8_t(dwarf::DW_LNS_extended_op);
    encodeULEB128(PtrSize + 1, OS);

    OS << uint8_t(dwarf::DW_LNE_set_address);
    Offset = OS.tell();
    assert((PtrSize == 4 || PtrSize == 8) && "Unexpected pointer size");
    FK = getRelocPairForSize(PtrSize == 4 ? 32 : 64);
    OS.write_zeros(PtrSize);
  } else {
    OS << uint8_t(dwarf::DW_LNS_fixed_advance_pc);
    Offset = OS.tell();
    FK = getRelocPairForSize(16);
    support::endian::write<uint16_t>(OS, 0, llvm::endianness::little);
  }

  const MCBinaryExpr &MBE = cast<MCBinaryExpr>(AddrDelta);
  Fixups.push_back(MCFixup::create(Offset, MBE.getLHS(), std::get<0>(FK)));
  Fixups.push_back(MCFixup::create(Offset, MBE.getRHS(), std::get<1>(FK)));

  if (LineDelta == INT64_MAX) {
    OS << uint8_t(dwarf::DW_LNS_extended_op);
    OS << uint8_t(1);
    OS << uint8_t(dwarf::DW_LNE_end_sequence);
  } else {
    OS << uint8_t(dwarf::DW_LNS_copy);
  }

  WasRelaxed = OldSize != Data.size();
  return true;
}

bool LoongArchAsmBackend::relaxDwarfCFA(const MCAssembler &Asm,
                                        MCDwarfCallFrameFragment &DF,
                                        bool &WasRelaxed) const {
  const MCExpr &AddrDelta = DF.getAddrDelta();
  SmallVectorImpl<char> &Data = DF.getContents();
  SmallVectorImpl<MCFixup> &Fixups = DF.getFixups();
  size_t OldSize = Data.size();

  int64_t Value;
  if (AddrDelta.evaluateAsAbsolute(Value, Asm))
    return false;
  bool IsAbsolute = AddrDelta.evaluateKnownAbsolute(Value, Asm);
  assert(IsAbsolute && "CFA with invalid expression");
  (void)IsAbsolute;

  Data.clear();
  Fixups.clear();
  raw_svector_ostream OS(Data);

  assert(Asm.getContext().getAsmInfo()->getMinInstAlignment() == 1 &&
         "expected 1-byte alignment");
  if (Value == 0) {
    WasRelaxed = OldSize != Data.size();
    return true;
  }

  auto AddFixups = [&Fixups,
                    &AddrDelta](unsigned Offset,
                                std::pair<MCFixupKind, MCFixupKind> FK) {
    const MCBinaryExpr &MBE = cast<MCBinaryExpr>(AddrDelta);
    Fixups.push_back(MCFixup::create(Offset, MBE.getLHS(), std::get<0>(FK)));
    Fixups.push_back(MCFixup::create(Offset, MBE.getRHS(), std::get<1>(FK)));
  };

  if (isUIntN(6, Value)) {
    OS << uint8_t(dwarf::DW_CFA_advance_loc);
    AddFixups(0, getRelocPairForSize(6));
  } else if (isUInt<8>(Value)) {
    OS << uint8_t(dwarf::DW_CFA_advance_loc1);
    support::endian::write<uint8_t>(OS, 0, llvm::endianness::little);
    AddFixups(1, getRelocPairForSize(8));
  } else if (isUInt<16>(Value)) {
    OS << uint8_t(dwarf::DW_CFA_advance_loc2);
    support::endian::write<uint16_t>(OS, 0, llvm::endianness::little);
    AddFixups(1, getRelocPairForSize(16));
  } else if (isUInt<32>(Value)) {
    OS << uint8_t(dwarf::DW_CFA_advance_loc4);
    support::endian::write<uint32_t>(OS, 0, llvm::endianness::little);
    AddFixups(1, getRelocPairForSize(32));
  } else {
    llvm_unreachable("unsupported CFA encoding");
  }

  WasRelaxed = OldSize != Data.size();
  return true;
}

bool LoongArchAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                       const MCSubtargetInfo *STI) const {
  // We mostly follow binutils' convention here: align to 4-byte boundary with a
  // 0-fill padding.
  OS.write_zeros(Count % 4);

  // The remainder is now padded with 4-byte nops.
  // nop: andi r0, r0, 0
  for (; Count >= 4; Count -= 4)
    OS.write("\0\0\x40\x03", 4);

  return true;
}

bool LoongArchAsmBackend::handleAddSubRelocations(const MCAssembler &Asm,
                                                  const MCFragment &F,
                                                  const MCFixup &Fixup,
                                                  const MCValue &Target,
                                                  uint64_t &FixedValue) const {
  std::pair<MCFixupKind, MCFixupKind> FK;
  uint64_t FixedValueA, FixedValueB;
  const MCSymbol &SA = Target.getSymA()->getSymbol();
  const MCSymbol &SB = Target.getSymB()->getSymbol();

  bool force = !SA.isInSection() || !SB.isInSection();
  if (!force) {
    const MCSection &SecA = SA.getSection();
    const MCSection &SecB = SB.getSection();

    // We need record relocation if SecA != SecB. Usually SecB is same as the
    // section of Fixup, which will be record the relocation as PCRel. If SecB
    // is not same as the section of Fixup, it will report error. Just return
    // false and then this work can be finished by handleFixup.
    if (&SecA != &SecB)
      return false;

    // In SecA == SecB case. If the linker relaxation is enabled, we need record
    // the ADD, SUB relocations. Otherwise the FixedValue has already been calc-
    // ulated out in evaluateFixup, return true and avoid record relocations.
    if (!STI.hasFeature(LoongArch::FeatureRelax))
      return true;
  }

  switch (Fixup.getKind()) {
  case llvm::FK_Data_1:
    FK = getRelocPairForSize(8);
    break;
  case llvm::FK_Data_2:
    FK = getRelocPairForSize(16);
    break;
  case llvm::FK_Data_4:
    FK = getRelocPairForSize(32);
    break;
  case llvm::FK_Data_8:
    FK = getRelocPairForSize(64);
    break;
  case llvm::FK_Data_leb128:
    FK = getRelocPairForSize(128);
    break;
  default:
    llvm_unreachable("unsupported fixup size");
  }
  MCValue A = MCValue::get(Target.getSymA(), nullptr, Target.getConstant());
  MCValue B = MCValue::get(Target.getSymB());
  auto FA = MCFixup::create(Fixup.getOffset(), nullptr, std::get<0>(FK));
  auto FB = MCFixup::create(Fixup.getOffset(), nullptr, std::get<1>(FK));
  auto &Assembler = const_cast<MCAssembler &>(Asm);
  Asm.getWriter().recordRelocation(Assembler, &F, FA, A, FixedValueA);
  Asm.getWriter().recordRelocation(Assembler, &F, FB, B, FixedValueB);
  FixedValue = FixedValueA - FixedValueB;
  return true;
}

std::unique_ptr<MCObjectTargetWriter>
LoongArchAsmBackend::createObjectTargetWriter() const {
  return createLoongArchELFObjectWriter(
      OSABI, Is64Bit, STI.hasFeature(LoongArch::FeatureRelax));
}

MCAsmBackend *llvm::createLoongArchAsmBackend(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              const MCRegisterInfo &MRI,
                                              const MCTargetOptions &Options) {
  const Triple &TT = STI.getTargetTriple();
  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TT.getOS());
  return new LoongArchAsmBackend(STI, OSABI, TT.isArch64Bit(), Options);
}
