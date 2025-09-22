//===-- VEAsmBackend.cpp - VE Assembler Backend ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/VEFixupKinds.h"
#include "MCTargetDesc/VEMCTargetDesc.h"
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

static uint64_t adjustFixupValue(unsigned Kind, uint64_t Value) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
  case FK_PCRel_1:
  case FK_PCRel_2:
  case FK_PCRel_4:
  case FK_PCRel_8:
    return Value;
  case VE::fixup_ve_hi32:
  case VE::fixup_ve_pc_hi32:
  case VE::fixup_ve_got_hi32:
  case VE::fixup_ve_gotoff_hi32:
  case VE::fixup_ve_plt_hi32:
  case VE::fixup_ve_tls_gd_hi32:
  case VE::fixup_ve_tpoff_hi32:
    return (Value >> 32) & 0xffffffff;
  case VE::fixup_ve_reflong:
  case VE::fixup_ve_srel32:
  case VE::fixup_ve_lo32:
  case VE::fixup_ve_pc_lo32:
  case VE::fixup_ve_got_lo32:
  case VE::fixup_ve_gotoff_lo32:
  case VE::fixup_ve_plt_lo32:
  case VE::fixup_ve_tls_gd_lo32:
  case VE::fixup_ve_tpoff_lo32:
    return Value & 0xffffffff;
  }
}

/// getFixupKindNumBytes - The number of bytes the fixup may change.
static unsigned getFixupKindNumBytes(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");
  case FK_Data_1:
  case FK_PCRel_1:
    return 1;
  case FK_Data_2:
  case FK_PCRel_2:
    return 2;
    return 4;
  case FK_Data_4:
  case FK_PCRel_4:
  case VE::fixup_ve_reflong:
  case VE::fixup_ve_srel32:
  case VE::fixup_ve_hi32:
  case VE::fixup_ve_lo32:
  case VE::fixup_ve_pc_hi32:
  case VE::fixup_ve_pc_lo32:
  case VE::fixup_ve_got_hi32:
  case VE::fixup_ve_got_lo32:
  case VE::fixup_ve_gotoff_hi32:
  case VE::fixup_ve_gotoff_lo32:
  case VE::fixup_ve_plt_hi32:
  case VE::fixup_ve_plt_lo32:
  case VE::fixup_ve_tls_gd_hi32:
  case VE::fixup_ve_tls_gd_lo32:
  case VE::fixup_ve_tpoff_hi32:
  case VE::fixup_ve_tpoff_lo32:
    return 4;
  case FK_Data_8:
  case FK_PCRel_8:
    return 8;
  }
}

namespace {
class VEAsmBackend : public MCAsmBackend {
protected:
  const Target &TheTarget;

public:
  VEAsmBackend(const Target &T)
      : MCAsmBackend(llvm::endianness::little), TheTarget(T) {}

  unsigned getNumFixupKinds() const override { return VE::NumTargetFixupKinds; }

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override {
    const static MCFixupKindInfo Infos[VE::NumTargetFixupKinds] = {
        // name, offset, bits, flags
        {"fixup_ve_reflong", 0, 32, 0},
        {"fixup_ve_srel32", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
        {"fixup_ve_hi32", 0, 32, 0},
        {"fixup_ve_lo32", 0, 32, 0},
        {"fixup_ve_pc_hi32", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
        {"fixup_ve_pc_lo32", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
        {"fixup_ve_got_hi32", 0, 32, 0},
        {"fixup_ve_got_lo32", 0, 32, 0},
        {"fixup_ve_gotoff_hi32", 0, 32, 0},
        {"fixup_ve_gotoff_lo32", 0, 32, 0},
        {"fixup_ve_plt_hi32", 0, 32, 0},
        {"fixup_ve_plt_lo32", 0, 32, 0},
        {"fixup_ve_tls_gd_hi32", 0, 32, 0},
        {"fixup_ve_tls_gd_lo32", 0, 32, 0},
        {"fixup_ve_tpoff_hi32", 0, 32, 0},
        {"fixup_ve_tpoff_lo32", 0, 32, 0},
    };

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
           "Invalid kind!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                             const MCValue &Target,
                             const MCSubtargetInfo *STI) override {
    switch ((VE::Fixups)Fixup.getKind()) {
    default:
      return false;
    case VE::fixup_ve_tls_gd_hi32:
    case VE::fixup_ve_tls_gd_lo32:
    case VE::fixup_ve_tpoff_hi32:
    case VE::fixup_ve_tpoff_lo32:
      return true;
    }
  }

  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override {
    // Not implemented yet.  For example, if we have a branch with
    // lager than SIMM32 immediate value, we want to relaxation such
    // branch instructions.
    return false;
  }

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {
    // Aurora VE doesn't support relaxInstruction yet.
    llvm_unreachable("relaxInstruction() should not be called");
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    if ((Count % 8) != 0)
      return false;

    for (uint64_t i = 0; i < Count; i += 8)
      support::endian::write<uint64_t>(OS, 0x7900000000000000ULL,
                                       llvm::endianness::little);

    return true;
  }
};

class ELFVEAsmBackend : public VEAsmBackend {
  Triple::OSType OSType;

public:
  ELFVEAsmBackend(const Target &T, Triple::OSType OSType)
      : VEAsmBackend(T), OSType(OSType) {}

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override {
    Value = adjustFixupValue(Fixup.getKind(), Value);
    if (!Value)
      return; // Doesn't change encoding.

    MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());

    // Shift the value into position.
    Value <<= Info.TargetOffset;

    unsigned NumBytes = getFixupKindNumBytes(Fixup.getKind());
    unsigned Offset = Fixup.getOffset();
    assert(Offset + NumBytes <= Data.size() && "Invalid fixup offset!");
    // For each byte of the fragment that the fixup touches, mask in the bits
    // from the fixup value. The Value has been "split up" into the
    // appropriate bitfields above.
    for (unsigned i = 0; i != NumBytes; ++i) {
      unsigned Idx =
          Endian == llvm::endianness::little ? i : (NumBytes - 1) - i;
      Data[Offset + Idx] |= static_cast<uint8_t>((Value >> (i * 8)) & 0xff);
    }
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(OSType);
    return createVEELFObjectWriter(OSABI);
  }
};
} // end anonymous namespace

MCAsmBackend *llvm::createVEAsmBackend(const Target &T,
                                       const MCSubtargetInfo &STI,
                                       const MCRegisterInfo &MRI,
                                       const MCTargetOptions &Options) {
  return new ELFVEAsmBackend(T, STI.getTargetTriple().getOS());
}
