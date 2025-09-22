//===-- AArch64AsmBackend.cpp - AArch64 Assembler Backend -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/AArch64FixupKinds.h"
#include "MCTargetDesc/AArch64MCExpr.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

namespace {

class AArch64AsmBackend : public MCAsmBackend {
  static const unsigned PCRelFlagVal =
      MCFixupKindInfo::FKF_IsAlignedDownTo32Bits | MCFixupKindInfo::FKF_IsPCRel;
protected:
  Triple TheTriple;

public:
  AArch64AsmBackend(const Target &T, const Triple &TT, bool IsLittleEndian)
      : MCAsmBackend(IsLittleEndian ? llvm::endianness::little
                                    : llvm::endianness::big),
        TheTriple(TT) {}

  unsigned getNumFixupKinds() const override {
    return AArch64::NumTargetFixupKinds;
  }

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override;

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override {
    const static MCFixupKindInfo Infos[AArch64::NumTargetFixupKinds] = {
        // This table *must* be in the order that the fixup_* kinds are defined
        // in AArch64FixupKinds.h.
        //
        // Name                           Offset (bits) Size (bits)     Flags
        {"fixup_aarch64_pcrel_adr_imm21", 0, 32, PCRelFlagVal},
        {"fixup_aarch64_pcrel_adrp_imm21", 0, 32, PCRelFlagVal},
        {"fixup_aarch64_add_imm12", 10, 12, 0},
        {"fixup_aarch64_ldst_imm12_scale1", 10, 12, 0},
        {"fixup_aarch64_ldst_imm12_scale2", 10, 12, 0},
        {"fixup_aarch64_ldst_imm12_scale4", 10, 12, 0},
        {"fixup_aarch64_ldst_imm12_scale8", 10, 12, 0},
        {"fixup_aarch64_ldst_imm12_scale16", 10, 12, 0},
        {"fixup_aarch64_ldr_pcrel_imm19", 5, 19, PCRelFlagVal},
        {"fixup_aarch64_movw", 5, 16, 0},
        {"fixup_aarch64_pcrel_branch14", 5, 14, PCRelFlagVal},
        {"fixup_aarch64_pcrel_branch16", 5, 16, PCRelFlagVal},
        {"fixup_aarch64_pcrel_branch19", 5, 19, PCRelFlagVal},
        {"fixup_aarch64_pcrel_branch26", 0, 26, PCRelFlagVal},
        {"fixup_aarch64_pcrel_call26", 0, 26, PCRelFlagVal}};

    // Fixup kinds from .reloc directive are like R_AARCH64_NONE. They do not
    // require any extra processing.
    if (Kind >= FirstLiteralRelocationKind)
      return MCAsmBackend::getFixupKindInfo(FK_NONE);

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
           "Invalid kind!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;

  bool fixupNeedsRelaxation(const MCFixup &Fixup,
                            uint64_t Value) const override;
  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override;
  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;

  unsigned getFixupKindContainereSizeInBytes(unsigned Kind) const;

  bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                             const MCValue &Target,
                             const MCSubtargetInfo *STI) override;
};

} // end anonymous namespace

/// The number of bytes the fixup may change.
static unsigned getFixupKindNumBytes(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");

  case FK_Data_1:
    return 1;

  case FK_Data_2:
  case FK_SecRel_2:
    return 2;

  case AArch64::fixup_aarch64_movw:
  case AArch64::fixup_aarch64_pcrel_branch14:
  case AArch64::fixup_aarch64_pcrel_branch16:
  case AArch64::fixup_aarch64_add_imm12:
  case AArch64::fixup_aarch64_ldst_imm12_scale1:
  case AArch64::fixup_aarch64_ldst_imm12_scale2:
  case AArch64::fixup_aarch64_ldst_imm12_scale4:
  case AArch64::fixup_aarch64_ldst_imm12_scale8:
  case AArch64::fixup_aarch64_ldst_imm12_scale16:
  case AArch64::fixup_aarch64_ldr_pcrel_imm19:
  case AArch64::fixup_aarch64_pcrel_branch19:
    return 3;

  case AArch64::fixup_aarch64_pcrel_adr_imm21:
  case AArch64::fixup_aarch64_pcrel_adrp_imm21:
  case AArch64::fixup_aarch64_pcrel_branch26:
  case AArch64::fixup_aarch64_pcrel_call26:
  case FK_Data_4:
  case FK_SecRel_4:
    return 4;

  case FK_Data_8:
    return 8;
  }
}

static unsigned AdrImmBits(unsigned Value) {
  unsigned lo2 = Value & 0x3;
  unsigned hi19 = (Value & 0x1ffffc) >> 2;
  return (hi19 << 5) | (lo2 << 29);
}

static uint64_t adjustFixupValue(const MCFixup &Fixup, const MCValue &Target,
                                 uint64_t Value, MCContext &Ctx,
                                 const Triple &TheTriple, bool IsResolved) {
  int64_t SignedValue = static_cast<int64_t>(Value);
  switch (Fixup.getTargetKind()) {
  default:
    llvm_unreachable("Unknown fixup kind!");
  case AArch64::fixup_aarch64_pcrel_adr_imm21:
    if (!isInt<21>(SignedValue))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    return AdrImmBits(Value & 0x1fffffULL);
  case AArch64::fixup_aarch64_pcrel_adrp_imm21:
    assert(!IsResolved);
    if (TheTriple.isOSBinFormatCOFF()) {
      if (!isInt<21>(SignedValue))
        Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
      return AdrImmBits(Value & 0x1fffffULL);
    }
    return AdrImmBits((Value & 0x1fffff000ULL) >> 12);
  case AArch64::fixup_aarch64_ldr_pcrel_imm19:
  case AArch64::fixup_aarch64_pcrel_branch19:
    // Signed 19-bit immediate which gets multiplied by 4
    if (!isInt<21>(SignedValue))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    if (Value & 0x3)
      Ctx.reportError(Fixup.getLoc(), "fixup not sufficiently aligned");
    // Low two bits are not encoded.
    return (Value >> 2) & 0x7ffff;
  case AArch64::fixup_aarch64_add_imm12:
  case AArch64::fixup_aarch64_ldst_imm12_scale1:
    if (TheTriple.isOSBinFormatCOFF() && !IsResolved)
      Value &= 0xfff;
    // Unsigned 12-bit immediate
    if (!isUInt<12>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    return Value;
  case AArch64::fixup_aarch64_ldst_imm12_scale2:
    if (TheTriple.isOSBinFormatCOFF() && !IsResolved)
      Value &= 0xfff;
    // Unsigned 12-bit immediate which gets multiplied by 2
    if (!isUInt<13>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    if (Value & 0x1)
      Ctx.reportError(Fixup.getLoc(), "fixup must be 2-byte aligned");
    return Value >> 1;
  case AArch64::fixup_aarch64_ldst_imm12_scale4:
    if (TheTriple.isOSBinFormatCOFF() && !IsResolved)
      Value &= 0xfff;
    // Unsigned 12-bit immediate which gets multiplied by 4
    if (!isUInt<14>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    if (Value & 0x3)
      Ctx.reportError(Fixup.getLoc(), "fixup must be 4-byte aligned");
    return Value >> 2;
  case AArch64::fixup_aarch64_ldst_imm12_scale8:
    if (TheTriple.isOSBinFormatCOFF() && !IsResolved)
      Value &= 0xfff;
    // Unsigned 12-bit immediate which gets multiplied by 8
    if (!isUInt<15>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    if (Value & 0x7)
      Ctx.reportError(Fixup.getLoc(), "fixup must be 8-byte aligned");
    return Value >> 3;
  case AArch64::fixup_aarch64_ldst_imm12_scale16:
    if (TheTriple.isOSBinFormatCOFF() && !IsResolved)
      Value &= 0xfff;
    // Unsigned 12-bit immediate which gets multiplied by 16
    if (!isUInt<16>(Value))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    if (Value & 0xf)
      Ctx.reportError(Fixup.getLoc(), "fixup must be 16-byte aligned");
    return Value >> 4;
  case AArch64::fixup_aarch64_movw: {
    AArch64MCExpr::VariantKind RefKind =
        static_cast<AArch64MCExpr::VariantKind>(Target.getRefKind());
    if (AArch64MCExpr::getSymbolLoc(RefKind) != AArch64MCExpr::VK_ABS &&
        AArch64MCExpr::getSymbolLoc(RefKind) != AArch64MCExpr::VK_SABS) {
      if (!RefKind) {
        // The fixup is an expression
        if (SignedValue > 0xFFFF || SignedValue < -0xFFFF)
          Ctx.reportError(Fixup.getLoc(),
                          "fixup value out of range [-0xFFFF, 0xFFFF]");

        // Invert the negative immediate because it will feed into a MOVN.
        if (SignedValue < 0)
          SignedValue = ~SignedValue;
        Value = static_cast<uint64_t>(SignedValue);
      } else
        // VK_GOTTPREL, VK_TPREL, VK_DTPREL are movw fixups, but they can't
        // ever be resolved in the assembler.
        Ctx.reportError(Fixup.getLoc(),
                        "relocation for a thread-local variable points to an "
                        "absolute symbol");
      return Value;
    }

    if (!IsResolved) {
      // FIXME: Figure out when this can actually happen, and verify our
      // behavior.
      Ctx.reportError(Fixup.getLoc(), "unresolved movw fixup not yet "
                                      "implemented");
      return Value;
    }

    if (AArch64MCExpr::getSymbolLoc(RefKind) == AArch64MCExpr::VK_SABS) {
      switch (AArch64MCExpr::getAddressFrag(RefKind)) {
      case AArch64MCExpr::VK_G0:
        break;
      case AArch64MCExpr::VK_G1:
        SignedValue = SignedValue >> 16;
        break;
      case AArch64MCExpr::VK_G2:
        SignedValue = SignedValue >> 32;
        break;
      case AArch64MCExpr::VK_G3:
        SignedValue = SignedValue >> 48;
        break;
      default:
        llvm_unreachable("Variant kind doesn't correspond to fixup");
      }

    } else {
      switch (AArch64MCExpr::getAddressFrag(RefKind)) {
      case AArch64MCExpr::VK_G0:
        break;
      case AArch64MCExpr::VK_G1:
        Value = Value >> 16;
        break;
      case AArch64MCExpr::VK_G2:
        Value = Value >> 32;
        break;
      case AArch64MCExpr::VK_G3:
        Value = Value >> 48;
        break;
      default:
        llvm_unreachable("Variant kind doesn't correspond to fixup");
      }
    }

    if (RefKind & AArch64MCExpr::VK_NC) {
      Value &= 0xFFFF;
    }
    else if (AArch64MCExpr::getSymbolLoc(RefKind) == AArch64MCExpr::VK_SABS) {
      if (SignedValue > 0xFFFF || SignedValue < -0xFFFF)
        Ctx.reportError(Fixup.getLoc(), "fixup value out of range");

      // Invert the negative immediate because it will feed into a MOVN.
      if (SignedValue < 0)
        SignedValue = ~SignedValue;
      Value = static_cast<uint64_t>(SignedValue);
    }
    else if (Value > 0xFFFF) {
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    }
    return Value;
  }
  case AArch64::fixup_aarch64_pcrel_branch14:
    // Signed 16-bit immediate
    if (!isInt<16>(SignedValue))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    // Low two bits are not encoded (4-byte alignment assumed).
    if (Value & 0x3)
      Ctx.reportError(Fixup.getLoc(), "fixup not sufficiently aligned");
    return (Value >> 2) & 0x3fff;
  case AArch64::fixup_aarch64_pcrel_branch16:
    // Unsigned PC-relative offset, so invert the negative immediate.
    SignedValue = -SignedValue;
    Value = static_cast<uint64_t>(SignedValue);
    // Check valid 18-bit unsigned range.
    if (SignedValue < 0 || SignedValue > ((1 << 18) - 1))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    // Low two bits are not encoded (4-byte alignment assumed).
    if (Value & 0b11)
      Ctx.reportError(Fixup.getLoc(), "fixup not sufficiently aligned");
    return (Value >> 2) & 0xffff;
  case AArch64::fixup_aarch64_pcrel_branch26:
  case AArch64::fixup_aarch64_pcrel_call26:
    if (TheTriple.isOSBinFormatCOFF() && !IsResolved && SignedValue != 0) {
      // MSVC link.exe and lld do not support this relocation type
      // with a non-zero offset
      Ctx.reportError(Fixup.getLoc(),
                      "cannot perform a PC-relative fixup with a non-zero "
                      "symbol offset");
    }
    // Signed 28-bit immediate
    if (!isInt<28>(SignedValue))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range");
    // Low two bits are not encoded (4-byte alignment assumed).
    if (Value & 0x3)
      Ctx.reportError(Fixup.getLoc(), "fixup not sufficiently aligned");
    return (Value >> 2) & 0x3ffffff;
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
  case FK_SecRel_2:
  case FK_SecRel_4:
    return Value;
  }
}

std::optional<MCFixupKind>
AArch64AsmBackend::getFixupKind(StringRef Name) const {
  if (!TheTriple.isOSBinFormatELF())
    return std::nullopt;

  unsigned Type = llvm::StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y)  .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/AArch64.def"
#undef ELF_RELOC
                      .Case("BFD_RELOC_NONE", ELF::R_AARCH64_NONE)
                      .Case("BFD_RELOC_16", ELF::R_AARCH64_ABS16)
                      .Case("BFD_RELOC_32", ELF::R_AARCH64_ABS32)
                      .Case("BFD_RELOC_64", ELF::R_AARCH64_ABS64)
                      .Default(-1u);
  if (Type == -1u)
    return std::nullopt;
  return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
}

/// getFixupKindContainereSizeInBytes - The number of bytes of the
/// container involved in big endian or 0 if the item is little endian
unsigned AArch64AsmBackend::getFixupKindContainereSizeInBytes(unsigned Kind) const {
  if (Endian == llvm::endianness::little)
    return 0;

  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");

  case FK_Data_1:
    return 1;
  case FK_Data_2:
    return 2;
  case FK_Data_4:
    return 4;
  case FK_Data_8:
    return 8;

  case AArch64::fixup_aarch64_movw:
  case AArch64::fixup_aarch64_pcrel_branch14:
  case AArch64::fixup_aarch64_pcrel_branch16:
  case AArch64::fixup_aarch64_add_imm12:
  case AArch64::fixup_aarch64_ldst_imm12_scale1:
  case AArch64::fixup_aarch64_ldst_imm12_scale2:
  case AArch64::fixup_aarch64_ldst_imm12_scale4:
  case AArch64::fixup_aarch64_ldst_imm12_scale8:
  case AArch64::fixup_aarch64_ldst_imm12_scale16:
  case AArch64::fixup_aarch64_ldr_pcrel_imm19:
  case AArch64::fixup_aarch64_pcrel_branch19:
  case AArch64::fixup_aarch64_pcrel_adr_imm21:
  case AArch64::fixup_aarch64_pcrel_adrp_imm21:
  case AArch64::fixup_aarch64_pcrel_branch26:
  case AArch64::fixup_aarch64_pcrel_call26:
    // Instructions are always little endian
    return 0;
  }
}

void AArch64AsmBackend::applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                                   const MCValue &Target,
                                   MutableArrayRef<char> Data, uint64_t Value,
                                   bool IsResolved,
                                   const MCSubtargetInfo *STI) const {
  if (Fixup.getTargetKind() == FK_Data_8 && TheTriple.isOSBinFormatELF()) {
    auto RefKind = static_cast<AArch64MCExpr::VariantKind>(Target.getRefKind());
    AArch64MCExpr::VariantKind SymLoc = AArch64MCExpr::getSymbolLoc(RefKind);
    if (SymLoc == AArch64AuthMCExpr::VK_AUTH ||
        SymLoc == AArch64AuthMCExpr::VK_AUTHADDR) {
      assert(Value == 0);
      const auto *Expr = cast<AArch64AuthMCExpr>(Fixup.getValue());
      Value = (uint64_t(Expr->getDiscriminator()) << 32) |
              (uint64_t(Expr->getKey()) << 60) |
              (uint64_t(Expr->hasAddressDiversity()) << 63);
    }
  }

  if (!Value)
    return; // Doesn't change encoding.
  unsigned Kind = Fixup.getKind();
  if (Kind >= FirstLiteralRelocationKind)
    return;
  unsigned NumBytes = getFixupKindNumBytes(Kind);
  MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
  MCContext &Ctx = Asm.getContext();
  int64_t SignedValue = static_cast<int64_t>(Value);
  // Apply any target-specific value adjustments.
  Value = adjustFixupValue(Fixup, Target, Value, Ctx, TheTriple, IsResolved);

  // Shift the value into position.
  Value <<= Info.TargetOffset;

  unsigned Offset = Fixup.getOffset();
  assert(Offset + NumBytes <= Data.size() && "Invalid fixup offset!");

  // Used to point to big endian bytes.
  unsigned FulleSizeInBytes = getFixupKindContainereSizeInBytes(Fixup.getKind());

  // For each byte of the fragment that the fixup touches, mask in the
  // bits from the fixup value.
  if (FulleSizeInBytes == 0) {
    // Handle as little-endian
    for (unsigned i = 0; i != NumBytes; ++i) {
      Data[Offset + i] |= uint8_t((Value >> (i * 8)) & 0xff);
    }
  } else {
    // Handle as big-endian
    assert((Offset + FulleSizeInBytes) <= Data.size() && "Invalid fixup size!");
    assert(NumBytes <= FulleSizeInBytes && "Invalid fixup size!");
    for (unsigned i = 0; i != NumBytes; ++i) {
      unsigned Idx = FulleSizeInBytes - 1 - i;
      Data[Offset + Idx] |= uint8_t((Value >> (i * 8)) & 0xff);
    }
  }

  // FIXME: getFixupKindInfo() and getFixupKindNumBytes() could be fixed to
  // handle this more cleanly. This may affect the output of -show-mc-encoding.
  AArch64MCExpr::VariantKind RefKind =
      static_cast<AArch64MCExpr::VariantKind>(Target.getRefKind());
  if (AArch64MCExpr::getSymbolLoc(RefKind) == AArch64MCExpr::VK_SABS ||
      (!RefKind && Fixup.getTargetKind() == AArch64::fixup_aarch64_movw)) {
    // If the immediate is negative, generate MOVN else MOVZ.
    // (Bit 30 = 0) ==> MOVN, (Bit 30 = 1) ==> MOVZ.
    if (SignedValue < 0)
      Data[Offset + 3] &= ~(1 << 6);
    else
      Data[Offset + 3] |= (1 << 6);
  }
}

bool AArch64AsmBackend::fixupNeedsRelaxation(const MCFixup &Fixup,
                                             uint64_t Value) const {
  // FIXME:  This isn't correct for AArch64. Just moving the "generic" logic
  // into the targets for now.
  //
  // Relax if the value is too big for a (signed) i8.
  return int64_t(Value) != int64_t(int8_t(Value));
}

void AArch64AsmBackend::relaxInstruction(MCInst &Inst,
                                         const MCSubtargetInfo &STI) const {
  llvm_unreachable("AArch64AsmBackend::relaxInstruction() unimplemented");
}

bool AArch64AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                     const MCSubtargetInfo *STI) const {
  // If the count is not 4-byte aligned, we must be writing data into the text
  // section (otherwise we have unaligned instructions, and thus have far
  // bigger problems), so just write zeros instead.
  OS.write_zeros(Count % 4);

  // We are properly aligned, so write NOPs as requested.
  Count /= 4;
  for (uint64_t i = 0; i != Count; ++i)
    OS.write("\x1f\x20\x03\xd5", 4);
  return true;
}

bool AArch64AsmBackend::shouldForceRelocation(const MCAssembler &Asm,
                                              const MCFixup &Fixup,
                                              const MCValue &Target,
                                              const MCSubtargetInfo *STI) {
  unsigned Kind = Fixup.getKind();
  if (Kind >= FirstLiteralRelocationKind)
    return true;

  // The ADRP instruction adds some multiple of 0x1000 to the current PC &
  // ~0xfff. This means that the required offset to reach a symbol can vary by
  // up to one step depending on where the ADRP is in memory. For example:
  //
  //     ADRP x0, there
  //  there:
  //
  // If the ADRP occurs at address 0xffc then "there" will be at 0x1000 and
  // we'll need that as an offset. At any other address "there" will be in the
  // same page as the ADRP and the instruction should encode 0x0. Assuming the
  // section isn't 0x1000-aligned, we therefore need to delegate this decision
  // to the linker -- a relocation!
  if (Kind == AArch64::fixup_aarch64_pcrel_adrp_imm21)
    return true;

  return false;
}

namespace {

namespace CU {

/// Compact unwind encoding values.
enum CompactUnwindEncodings {
  /// A "frameless" leaf function, where no non-volatile registers are
  /// saved. The return remains in LR throughout the function.
  UNWIND_ARM64_MODE_FRAMELESS = 0x02000000,

  /// No compact unwind encoding available. Instead the low 23-bits of
  /// the compact unwind encoding is the offset of the DWARF FDE in the
  /// __eh_frame section. This mode is never used in object files. It is only
  /// generated by the linker in final linked images, which have only DWARF info
  /// for a function.
  UNWIND_ARM64_MODE_DWARF = 0x03000000,

  /// This is a standard arm64 prologue where FP/LR are immediately
  /// pushed on the stack, then SP is copied to FP. If there are any
  /// non-volatile register saved, they are copied into the stack fame in pairs
  /// in a contiguous ranger right below the saved FP/LR pair. Any subset of the
  /// five X pairs and four D pairs can be saved, but the memory layout must be
  /// in register number order.
  UNWIND_ARM64_MODE_FRAME = 0x04000000,

  /// Frame register pair encodings.
  UNWIND_ARM64_FRAME_X19_X20_PAIR = 0x00000001,
  UNWIND_ARM64_FRAME_X21_X22_PAIR = 0x00000002,
  UNWIND_ARM64_FRAME_X23_X24_PAIR = 0x00000004,
  UNWIND_ARM64_FRAME_X25_X26_PAIR = 0x00000008,
  UNWIND_ARM64_FRAME_X27_X28_PAIR = 0x00000010,
  UNWIND_ARM64_FRAME_D8_D9_PAIR = 0x00000100,
  UNWIND_ARM64_FRAME_D10_D11_PAIR = 0x00000200,
  UNWIND_ARM64_FRAME_D12_D13_PAIR = 0x00000400,
  UNWIND_ARM64_FRAME_D14_D15_PAIR = 0x00000800
};

} // end CU namespace

// FIXME: This should be in a separate file.
class DarwinAArch64AsmBackend : public AArch64AsmBackend {
  const MCRegisterInfo &MRI;

  /// Encode compact unwind stack adjustment for frameless functions.
  /// See UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK in compact_unwind_encoding.h.
  /// The stack size always needs to be 16 byte aligned.
  uint32_t encodeStackAdjustment(uint32_t StackSize) const {
    return (StackSize / 16) << 12;
  }

public:
  DarwinAArch64AsmBackend(const Target &T, const Triple &TT,
                          const MCRegisterInfo &MRI)
      : AArch64AsmBackend(T, TT, /*IsLittleEndian*/ true), MRI(MRI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    uint32_t CPUType = cantFail(MachO::getCPUType(TheTriple));
    uint32_t CPUSubType = cantFail(MachO::getCPUSubType(TheTriple));
    return createAArch64MachObjectWriter(CPUType, CPUSubType,
                                         TheTriple.isArch32Bit());
  }

  /// Generate the compact unwind encoding from the CFI directives.
  uint64_t generateCompactUnwindEncoding(const MCDwarfFrameInfo *FI,
                                         const MCContext *Ctxt) const override {
    ArrayRef<MCCFIInstruction> Instrs = FI->Instructions;
    if (Instrs.empty())
      return CU::UNWIND_ARM64_MODE_FRAMELESS;
    if (!isDarwinCanonicalPersonality(FI->Personality) &&
        !Ctxt->emitCompactUnwindNonCanonical())
      return CU::UNWIND_ARM64_MODE_DWARF;

    bool HasFP = false;
    uint64_t StackSize = 0;

    uint64_t CompactUnwindEncoding = 0;
    int64_t CurOffset = 0;
    for (size_t i = 0, e = Instrs.size(); i != e; ++i) {
      const MCCFIInstruction &Inst = Instrs[i];

      switch (Inst.getOperation()) {
      default:
        // Cannot handle this directive:  bail out.
        return CU::UNWIND_ARM64_MODE_DWARF;
      case MCCFIInstruction::OpDefCfa: {
        // Defines a frame pointer.
        unsigned XReg =
            getXRegFromWReg(*MRI.getLLVMRegNum(Inst.getRegister(), true));

        // Other CFA registers than FP are not supported by compact unwind.
        // Fallback on DWARF.
        // FIXME: When opt-remarks are supported in MC, add a remark to notify
        // the user.
        if (XReg != AArch64::FP)
          return CU::UNWIND_ARM64_MODE_DWARF;

        if (i + 2 >= e)
          return CU::UNWIND_ARM64_MODE_DWARF;

        const MCCFIInstruction &LRPush = Instrs[++i];
        if (LRPush.getOperation() != MCCFIInstruction::OpOffset)
          return CU::UNWIND_ARM64_MODE_DWARF;
        const MCCFIInstruction &FPPush = Instrs[++i];
        if (FPPush.getOperation() != MCCFIInstruction::OpOffset)
          return CU::UNWIND_ARM64_MODE_DWARF;

        if (FPPush.getOffset() + 8 != LRPush.getOffset())
          return CU::UNWIND_ARM64_MODE_DWARF;
        CurOffset = FPPush.getOffset();

        unsigned LRReg = *MRI.getLLVMRegNum(LRPush.getRegister(), true);
        unsigned FPReg = *MRI.getLLVMRegNum(FPPush.getRegister(), true);

        LRReg = getXRegFromWReg(LRReg);
        FPReg = getXRegFromWReg(FPReg);

        if (LRReg != AArch64::LR || FPReg != AArch64::FP)
          return CU::UNWIND_ARM64_MODE_DWARF;

        // Indicate that the function has a frame.
        CompactUnwindEncoding |= CU::UNWIND_ARM64_MODE_FRAME;
        HasFP = true;
        break;
      }
      case MCCFIInstruction::OpDefCfaOffset: {
        if (StackSize != 0)
          return CU::UNWIND_ARM64_MODE_DWARF;
        StackSize = std::abs(Inst.getOffset());
        break;
      }
      case MCCFIInstruction::OpOffset: {
        // Registers are saved in pairs. We expect there to be two consecutive
        // `.cfi_offset' instructions with the appropriate registers specified.
        unsigned Reg1 = *MRI.getLLVMRegNum(Inst.getRegister(), true);
        if (i + 1 == e)
          return CU::UNWIND_ARM64_MODE_DWARF;

        if (CurOffset != 0 && Inst.getOffset() != CurOffset - 8)
          return CU::UNWIND_ARM64_MODE_DWARF;
        CurOffset = Inst.getOffset();

        const MCCFIInstruction &Inst2 = Instrs[++i];
        if (Inst2.getOperation() != MCCFIInstruction::OpOffset)
          return CU::UNWIND_ARM64_MODE_DWARF;
        unsigned Reg2 = *MRI.getLLVMRegNum(Inst2.getRegister(), true);

        if (Inst2.getOffset() != CurOffset - 8)
          return CU::UNWIND_ARM64_MODE_DWARF;
        CurOffset = Inst2.getOffset();

        // N.B. The encodings must be in register number order, and the X
        // registers before the D registers.

        // X19/X20 pair = 0x00000001,
        // X21/X22 pair = 0x00000002,
        // X23/X24 pair = 0x00000004,
        // X25/X26 pair = 0x00000008,
        // X27/X28 pair = 0x00000010
        Reg1 = getXRegFromWReg(Reg1);
        Reg2 = getXRegFromWReg(Reg2);

        if (Reg1 == AArch64::X19 && Reg2 == AArch64::X20 &&
            (CompactUnwindEncoding & 0xF1E) == 0)
          CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_X19_X20_PAIR;
        else if (Reg1 == AArch64::X21 && Reg2 == AArch64::X22 &&
                 (CompactUnwindEncoding & 0xF1C) == 0)
          CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_X21_X22_PAIR;
        else if (Reg1 == AArch64::X23 && Reg2 == AArch64::X24 &&
                 (CompactUnwindEncoding & 0xF18) == 0)
          CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_X23_X24_PAIR;
        else if (Reg1 == AArch64::X25 && Reg2 == AArch64::X26 &&
                 (CompactUnwindEncoding & 0xF10) == 0)
          CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_X25_X26_PAIR;
        else if (Reg1 == AArch64::X27 && Reg2 == AArch64::X28 &&
                 (CompactUnwindEncoding & 0xF00) == 0)
          CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_X27_X28_PAIR;
        else {
          Reg1 = getDRegFromBReg(Reg1);
          Reg2 = getDRegFromBReg(Reg2);

          // D8/D9 pair   = 0x00000100,
          // D10/D11 pair = 0x00000200,
          // D12/D13 pair = 0x00000400,
          // D14/D15 pair = 0x00000800
          if (Reg1 == AArch64::D8 && Reg2 == AArch64::D9 &&
              (CompactUnwindEncoding & 0xE00) == 0)
            CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_D8_D9_PAIR;
          else if (Reg1 == AArch64::D10 && Reg2 == AArch64::D11 &&
                   (CompactUnwindEncoding & 0xC00) == 0)
            CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_D10_D11_PAIR;
          else if (Reg1 == AArch64::D12 && Reg2 == AArch64::D13 &&
                   (CompactUnwindEncoding & 0x800) == 0)
            CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_D12_D13_PAIR;
          else if (Reg1 == AArch64::D14 && Reg2 == AArch64::D15)
            CompactUnwindEncoding |= CU::UNWIND_ARM64_FRAME_D14_D15_PAIR;
          else
            // A pair was pushed which we cannot handle.
            return CU::UNWIND_ARM64_MODE_DWARF;
        }

        break;
      }
      }
    }

    if (!HasFP) {
      // With compact unwind info we can only represent stack adjustments of up
      // to 65520 bytes.
      if (StackSize > 65520)
        return CU::UNWIND_ARM64_MODE_DWARF;

      CompactUnwindEncoding |= CU::UNWIND_ARM64_MODE_FRAMELESS;
      CompactUnwindEncoding |= encodeStackAdjustment(StackSize);
    }

    return CompactUnwindEncoding;
  }
};

} // end anonymous namespace

namespace {

class ELFAArch64AsmBackend : public AArch64AsmBackend {
public:
  uint8_t OSABI;
  bool IsILP32;

  ELFAArch64AsmBackend(const Target &T, const Triple &TT, uint8_t OSABI,
                       bool IsLittleEndian, bool IsILP32)
      : AArch64AsmBackend(T, TT, IsLittleEndian), OSABI(OSABI),
        IsILP32(IsILP32) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createAArch64ELFObjectWriter(OSABI, IsILP32);
  }
};

}

namespace {
class COFFAArch64AsmBackend : public AArch64AsmBackend {
public:
  COFFAArch64AsmBackend(const Target &T, const Triple &TheTriple)
      : AArch64AsmBackend(T, TheTriple, /*IsLittleEndian*/ true) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createAArch64WinCOFFObjectWriter(TheTriple);
  }
};
}

MCAsmBackend *llvm::createAArch64leAsmBackend(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              const MCRegisterInfo &MRI,
                                              const MCTargetOptions &Options) {
  const Triple &TheTriple = STI.getTargetTriple();
  if (TheTriple.isOSBinFormatMachO()) {
    return new DarwinAArch64AsmBackend(T, TheTriple, MRI);
  }

  if (TheTriple.isOSBinFormatCOFF())
    return new COFFAArch64AsmBackend(T, TheTriple);

  assert(TheTriple.isOSBinFormatELF() && "Invalid target");

  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TheTriple.getOS());
  bool IsILP32 = STI.getTargetTriple().getEnvironment() == Triple::GNUILP32;
  return new ELFAArch64AsmBackend(T, TheTriple, OSABI, /*IsLittleEndian=*/true,
                                  IsILP32);
}

MCAsmBackend *llvm::createAArch64beAsmBackend(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              const MCRegisterInfo &MRI,
                                              const MCTargetOptions &Options) {
  const Triple &TheTriple = STI.getTargetTriple();
  assert(TheTriple.isOSBinFormatELF() &&
         "Big endian is only supported for ELF targets!");
  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TheTriple.getOS());
  bool IsILP32 = STI.getTargetTriple().getEnvironment() == Triple::GNUILP32;
  return new ELFAArch64AsmBackend(T, TheTriple, OSABI, /*IsLittleEndian=*/false,
                                  IsILP32);
}
