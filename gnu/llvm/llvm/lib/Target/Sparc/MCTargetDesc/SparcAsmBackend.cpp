//===-- SparcAsmBackend.cpp - Sparc Assembler Backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SparcFixupKinds.h"
#include "MCTargetDesc/SparcMCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

static unsigned adjustFixupValue(unsigned Kind, uint64_t Value) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
    return Value;

  case Sparc::fixup_sparc_wplt30:
  case Sparc::fixup_sparc_call30:
    return (Value >> 2) & 0x3fffffff;

  case Sparc::fixup_sparc_br22:
    return (Value >> 2) & 0x3fffff;

  case Sparc::fixup_sparc_br19:
    return (Value >> 2) & 0x7ffff;

  case Sparc::fixup_sparc_br16: {
    // A.3 Branch on Integer Register with Prediction (BPr)
    // Inst{21-20} = d16hi;
    // Inst{13-0}  = d16lo;
    unsigned d16hi = (Value >> 16) & 0x3;
    unsigned d16lo = (Value >> 2) & 0x3fff;
    return (d16hi << 20) | d16lo;
  }

  case Sparc::fixup_sparc_hix22:
    return (~Value >> 10) & 0x3fffff;

  case Sparc::fixup_sparc_pc22:
  case Sparc::fixup_sparc_got22:
  case Sparc::fixup_sparc_tls_gd_hi22:
  case Sparc::fixup_sparc_tls_ldm_hi22:
  case Sparc::fixup_sparc_tls_ie_hi22:
  case Sparc::fixup_sparc_hi22:
  case Sparc::fixup_sparc_lm:
    return (Value >> 10) & 0x3fffff;

  case Sparc::fixup_sparc_got13:
  case Sparc::fixup_sparc_13:
    return Value & 0x1fff;

  case Sparc::fixup_sparc_lox10:
    return (Value & 0x3ff) | 0x1c00;

  case Sparc::fixup_sparc_pc10:
  case Sparc::fixup_sparc_got10:
  case Sparc::fixup_sparc_tls_gd_lo10:
  case Sparc::fixup_sparc_tls_ldm_lo10:
  case Sparc::fixup_sparc_tls_ie_lo10:
  case Sparc::fixup_sparc_lo10:
    return Value & 0x3ff;

  case Sparc::fixup_sparc_h44:
    return (Value >> 22) & 0x3fffff;

  case Sparc::fixup_sparc_m44:
    return (Value >> 12) & 0x3ff;

  case Sparc::fixup_sparc_l44:
    return Value & 0xfff;

  case Sparc::fixup_sparc_hh:
    return (Value >> 42) & 0x3fffff;

  case Sparc::fixup_sparc_hm:
    return (Value >> 32) & 0x3ff;

  case Sparc::fixup_sparc_tls_ldo_hix22:
  case Sparc::fixup_sparc_tls_le_hix22:
  case Sparc::fixup_sparc_tls_ldo_lox10:
  case Sparc::fixup_sparc_tls_le_lox10:
    assert(Value == 0 && "Sparc TLS relocs expect zero Value");
    return 0;

  case Sparc::fixup_sparc_tls_gd_add:
  case Sparc::fixup_sparc_tls_gd_call:
  case Sparc::fixup_sparc_tls_ldm_add:
  case Sparc::fixup_sparc_tls_ldm_call:
  case Sparc::fixup_sparc_tls_ldo_add:
  case Sparc::fixup_sparc_tls_ie_ld:
  case Sparc::fixup_sparc_tls_ie_ldx:
  case Sparc::fixup_sparc_tls_ie_add:
  case Sparc::fixup_sparc_gotdata_lox10:
  case Sparc::fixup_sparc_gotdata_hix22:
  case Sparc::fixup_sparc_gotdata_op:
    return 0;
  }
}

/// getFixupKindNumBytes - The number of bytes the fixup may change.
static unsigned getFixupKindNumBytes(unsigned Kind) {
    switch (Kind) {
  default:
    return 4;
  case FK_Data_1:
    return 1;
  case FK_Data_2:
    return 2;
  case FK_Data_8:
    return 8;
  }
}

namespace {
  class SparcAsmBackend : public MCAsmBackend {
  protected:
    bool Is64Bit;
    bool HasV9;

  public:
    SparcAsmBackend(const MCSubtargetInfo &STI)
        : MCAsmBackend(STI.getTargetTriple().isLittleEndian()
                           ? llvm::endianness::little
                           : llvm::endianness::big),
          Is64Bit(STI.getTargetTriple().isArch64Bit()),
          HasV9(STI.hasFeature(Sparc::FeatureV9)) {}

    unsigned getNumFixupKinds() const override {
      return Sparc::NumTargetFixupKinds;
    }

    std::optional<MCFixupKind> getFixupKind(StringRef Name) const override {
      unsigned Type;
      Type = llvm::StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y) .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/Sparc.def"
#undef ELF_RELOC
                 .Case("BFD_RELOC_NONE", ELF::R_SPARC_NONE)
                 .Case("BFD_RELOC_8", ELF::R_SPARC_8)
                 .Case("BFD_RELOC_16", ELF::R_SPARC_16)
                 .Case("BFD_RELOC_32", ELF::R_SPARC_32)
                 .Case("BFD_RELOC_64", ELF::R_SPARC_64)
                 .Default(-1u);
      if (Type == -1u)
        return std::nullopt;
      return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
    }

    const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override {
      const static MCFixupKindInfo InfosBE[Sparc::NumTargetFixupKinds] = {
        // name                    offset bits  flags
        { "fixup_sparc_call30",     2,     30,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_br22",      10,     22,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_br19",      13,     19,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_br16",       0,     32,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_13",        19,     13,  0 },
        { "fixup_sparc_hi22",      10,     22,  0 },
        { "fixup_sparc_lo10",      22,     10,  0 },
        { "fixup_sparc_h44",       10,     22,  0 },
        { "fixup_sparc_m44",       22,     10,  0 },
        { "fixup_sparc_l44",       20,     12,  0 },
        { "fixup_sparc_hh",        10,     22,  0 },
        { "fixup_sparc_hm",        22,     10,  0 },
        { "fixup_sparc_lm",        10,     22,  0 },
        { "fixup_sparc_pc22",      10,     22,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_pc10",      22,     10,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_got22",     10,     22,  0 },
        { "fixup_sparc_got10",     22,     10,  0 },
        { "fixup_sparc_got13",     19,     13,  0 },
        { "fixup_sparc_wplt30",     2,     30,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_tls_gd_hi22",   10, 22,  0 },
        { "fixup_sparc_tls_gd_lo10",   22, 10,  0 },
        { "fixup_sparc_tls_gd_add",     0,  0,  0 },
        { "fixup_sparc_tls_gd_call",    0,  0,  0 },
        { "fixup_sparc_tls_ldm_hi22",  10, 22,  0 },
        { "fixup_sparc_tls_ldm_lo10",  22, 10,  0 },
        { "fixup_sparc_tls_ldm_add",    0,  0,  0 },
        { "fixup_sparc_tls_ldm_call",   0,  0,  0 },
        { "fixup_sparc_tls_ldo_hix22", 10, 22,  0 },
        { "fixup_sparc_tls_ldo_lox10", 22, 10,  0 },
        { "fixup_sparc_tls_ldo_add",    0,  0,  0 },
        { "fixup_sparc_tls_ie_hi22",   10, 22,  0 },
        { "fixup_sparc_tls_ie_lo10",   22, 10,  0 },
        { "fixup_sparc_tls_ie_ld",      0,  0,  0 },
        { "fixup_sparc_tls_ie_ldx",     0,  0,  0 },
        { "fixup_sparc_tls_ie_add",     0,  0,  0 },
        { "fixup_sparc_tls_le_hix22",   0,  0,  0 },
        { "fixup_sparc_tls_le_lox10",   0,  0,  0 },
        { "fixup_sparc_hix22",         10, 22,  0 },
        { "fixup_sparc_lox10",         19, 13,  0 },
        { "fixup_sparc_gotdata_hix22",  0,  0,  0 },
        { "fixup_sparc_gotdata_lox10",  0,  0,  0 },
        { "fixup_sparc_gotdata_op",     0,  0,  0 },
      };

      const static MCFixupKindInfo InfosLE[Sparc::NumTargetFixupKinds] = {
        // name                    offset bits  flags
        { "fixup_sparc_call30",     0,     30,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_br22",       0,     22,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_br19",       0,     19,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_br16",      32,      0,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_13",         0,     13,  0 },
        { "fixup_sparc_hi22",       0,     22,  0 },
        { "fixup_sparc_lo10",       0,     10,  0 },
        { "fixup_sparc_h44",        0,     22,  0 },
        { "fixup_sparc_m44",        0,     10,  0 },
        { "fixup_sparc_l44",        0,     12,  0 },
        { "fixup_sparc_hh",         0,     22,  0 },
        { "fixup_sparc_hm",         0,     10,  0 },
        { "fixup_sparc_lm",         0,     22,  0 },
        { "fixup_sparc_pc22",       0,     22,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_pc10",       0,     10,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_got22",      0,     22,  0 },
        { "fixup_sparc_got10",      0,     10,  0 },
        { "fixup_sparc_got13",      0,     13,  0 },
        { "fixup_sparc_wplt30",      0,     30,  MCFixupKindInfo::FKF_IsPCRel },
        { "fixup_sparc_tls_gd_hi22",    0, 22,  0 },
        { "fixup_sparc_tls_gd_lo10",    0, 10,  0 },
        { "fixup_sparc_tls_gd_add",     0,  0,  0 },
        { "fixup_sparc_tls_gd_call",    0,  0,  0 },
        { "fixup_sparc_tls_ldm_hi22",   0, 22,  0 },
        { "fixup_sparc_tls_ldm_lo10",   0, 10,  0 },
        { "fixup_sparc_tls_ldm_add",    0,  0,  0 },
        { "fixup_sparc_tls_ldm_call",   0,  0,  0 },
        { "fixup_sparc_tls_ldo_hix22",  0, 22,  0 },
        { "fixup_sparc_tls_ldo_lox10",  0, 10,  0 },
        { "fixup_sparc_tls_ldo_add",    0,  0,  0 },
        { "fixup_sparc_tls_ie_hi22",    0, 22,  0 },
        { "fixup_sparc_tls_ie_lo10",    0, 10,  0 },
        { "fixup_sparc_tls_ie_ld",      0,  0,  0 },
        { "fixup_sparc_tls_ie_ldx",     0,  0,  0 },
        { "fixup_sparc_tls_ie_add",     0,  0,  0 },
        { "fixup_sparc_tls_le_hix22",   0,  0,  0 },
        { "fixup_sparc_tls_le_lox10",   0,  0,  0 },
        { "fixup_sparc_hix22",          0, 22,  0 },
        { "fixup_sparc_lox10",          0, 13,  0 },
        { "fixup_sparc_gotdata_hix22",  0,  0,  0 },
        { "fixup_sparc_gotdata_lox10",  0,  0,  0 },
        { "fixup_sparc_gotdata_op",     0,  0,  0 },
      };

      // Fixup kinds from .reloc directive are like R_SPARC_NONE. They do
      // not require any extra processing.
      if (Kind >= FirstLiteralRelocationKind)
        return MCAsmBackend::getFixupKindInfo(FK_NONE);

      if (Kind < FirstTargetFixupKind)
        return MCAsmBackend::getFixupKindInfo(Kind);

      assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
             "Invalid kind!");
      if (Endian == llvm::endianness::little)
        return InfosLE[Kind - FirstTargetFixupKind];

      return InfosBE[Kind - FirstTargetFixupKind];
    }

    bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                               const MCValue &Target,
                               const MCSubtargetInfo *STI) override {
      if (Fixup.getKind() >= FirstLiteralRelocationKind)
        return true;
      switch ((Sparc::Fixups)Fixup.getKind()) {
      default:
        return false;
      case Sparc::fixup_sparc_wplt30:
        if (Target.getSymA()->getSymbol().isTemporary())
          return false;
        [[fallthrough]];
      case Sparc::fixup_sparc_tls_gd_hi22:
      case Sparc::fixup_sparc_tls_gd_lo10:
      case Sparc::fixup_sparc_tls_gd_add:
      case Sparc::fixup_sparc_tls_gd_call:
      case Sparc::fixup_sparc_tls_ldm_hi22:
      case Sparc::fixup_sparc_tls_ldm_lo10:
      case Sparc::fixup_sparc_tls_ldm_add:
      case Sparc::fixup_sparc_tls_ldm_call:
      case Sparc::fixup_sparc_tls_ldo_hix22:
      case Sparc::fixup_sparc_tls_ldo_lox10:
      case Sparc::fixup_sparc_tls_ldo_add:
      case Sparc::fixup_sparc_tls_ie_hi22:
      case Sparc::fixup_sparc_tls_ie_lo10:
      case Sparc::fixup_sparc_tls_ie_ld:
      case Sparc::fixup_sparc_tls_ie_ldx:
      case Sparc::fixup_sparc_tls_ie_add:
      case Sparc::fixup_sparc_tls_le_hix22:
      case Sparc::fixup_sparc_tls_le_lox10:
        return true;
      }
    }

    void relaxInstruction(MCInst &Inst,
                          const MCSubtargetInfo &STI) const override {
      // FIXME.
      llvm_unreachable("relaxInstruction() unimplemented");
    }

    bool writeNopData(raw_ostream &OS, uint64_t Count,
                      const MCSubtargetInfo *STI) const override {

      // If the count is not 4-byte aligned, we must be writing data into the
      // text section (otherwise we have unaligned instructions, and thus have
      // far bigger problems), so just write zeros instead.
      OS.write_zeros(Count % 4);

      uint64_t NumNops = Count / 4;
      for (uint64_t i = 0; i != NumNops; ++i)
        support::endian::write<uint32_t>(OS, 0x01000000, Endian);

      return true;
    }
  };

  class ELFSparcAsmBackend : public SparcAsmBackend {
    Triple::OSType OSType;
  public:
    ELFSparcAsmBackend(const MCSubtargetInfo &STI, Triple::OSType OSType)
        : SparcAsmBackend(STI), OSType(OSType) {}

    void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                    const MCValue &Target, MutableArrayRef<char> Data,
                    uint64_t Value, bool IsResolved,
                    const MCSubtargetInfo *STI) const override {

      if (Fixup.getKind() >= FirstLiteralRelocationKind)
        return;
      Value = adjustFixupValue(Fixup.getKind(), Value);
      if (!Value) return;           // Doesn't change encoding.

      unsigned NumBytes = getFixupKindNumBytes(Fixup.getKind());
      unsigned Offset = Fixup.getOffset();
      // For each byte of the fragment that the fixup touches, mask in the bits
      // from the fixup value. The Value has been "split up" into the
      // appropriate bitfields above.
      for (unsigned i = 0; i != NumBytes; ++i) {
        unsigned Idx =
            Endian == llvm::endianness::little ? i : (NumBytes - 1) - i;
        Data[Offset + Idx] |= uint8_t((Value >> (i * 8)) & 0xff);
      }
    }

    std::unique_ptr<MCObjectTargetWriter>
    createObjectTargetWriter() const override {
      uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(OSType);
      return createSparcELFObjectWriter(Is64Bit, HasV9, OSABI);
    }
  };

} // end anonymous namespace

MCAsmBackend *llvm::createSparcAsmBackend(const Target &T,
                                          const MCSubtargetInfo &STI,
                                          const MCRegisterInfo &MRI,
                                          const MCTargetOptions &Options) {
  return new ELFSparcAsmBackend(STI, STI.getTargetTriple().getOS());
}
