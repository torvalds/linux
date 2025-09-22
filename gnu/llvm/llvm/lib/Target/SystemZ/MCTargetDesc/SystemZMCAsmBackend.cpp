//===-- SystemZMCAsmBackend.cpp - SystemZ assembler backend ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SystemZMCFixups.h"
#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

// Value is a fully-resolved relocation value: Symbol + Addend [- Pivot].
// Return the bits that should be installed in a relocation field for
// fixup kind Kind.
static uint64_t extractBitsForFixup(MCFixupKind Kind, uint64_t Value,
                                    const MCFixup &Fixup, MCContext &Ctx) {
  if (Kind < FirstTargetFixupKind)
    return Value;

  auto checkFixupInRange = [&](int64_t Min, int64_t Max) -> bool {
    int64_t SVal = int64_t(Value);
    if (SVal < Min || SVal > Max) {
      Ctx.reportError(Fixup.getLoc(), "operand out of range (" + Twine(SVal) +
                                          " not between " + Twine(Min) +
                                          " and " + Twine(Max) + ")");
      return false;
    }
    return true;
  };

  auto handlePCRelFixupValue = [&](unsigned W) -> uint64_t {
    if (Value % 2 != 0)
      Ctx.reportError(Fixup.getLoc(), "Non-even PC relative offset.");
    if (!checkFixupInRange(minIntN(W) * 2, maxIntN(W) * 2))
      return 0;
    return (int64_t)Value / 2;
  };

  auto handleImmValue = [&](bool IsSigned, unsigned W) -> uint64_t {
    if (!(IsSigned ? checkFixupInRange(minIntN(W), maxIntN(W))
                   : checkFixupInRange(0, maxUIntN(W))))
      return 0;
    return Value;
  };

  switch (unsigned(Kind)) {
  case SystemZ::FK_390_PC12DBL:
    return handlePCRelFixupValue(12);
  case SystemZ::FK_390_PC16DBL:
    return handlePCRelFixupValue(16);
  case SystemZ::FK_390_PC24DBL:
    return handlePCRelFixupValue(24);
  case SystemZ::FK_390_PC32DBL:
    return handlePCRelFixupValue(32);

  case SystemZ::FK_390_TLS_CALL:
    return 0;

  case SystemZ::FK_390_S8Imm:
    return handleImmValue(true, 8);
  case SystemZ::FK_390_S16Imm:
    return handleImmValue(true, 16);
  case SystemZ::FK_390_S20Imm: {
    Value = handleImmValue(true, 20);
    // S20Imm is used only for signed 20-bit displacements.
    // The high byte of a 20 bit displacement value comes first.
    uint64_t DLo = Value & 0xfff;
    uint64_t DHi = (Value >> 12) & 0xff;
    return (DLo << 8) | DHi;
  }
  case SystemZ::FK_390_S32Imm:
    return handleImmValue(true, 32);
  case SystemZ::FK_390_U1Imm:
    return handleImmValue(false, 1);
  case SystemZ::FK_390_U2Imm:
    return handleImmValue(false, 2);
  case SystemZ::FK_390_U3Imm:
    return handleImmValue(false, 3);
  case SystemZ::FK_390_U4Imm:
    return handleImmValue(false, 4);
  case SystemZ::FK_390_U8Imm:
    return handleImmValue(false, 8);
  case SystemZ::FK_390_U12Imm:
    return handleImmValue(false, 12);
  case SystemZ::FK_390_U16Imm:
    return handleImmValue(false, 16);
  case SystemZ::FK_390_U32Imm:
    return handleImmValue(false, 32);
  case SystemZ::FK_390_U48Imm:
    return handleImmValue(false, 48);
  }

  llvm_unreachable("Unknown fixup kind!");
}

namespace {
class SystemZMCAsmBackend : public MCAsmBackend {
public:
  SystemZMCAsmBackend() : MCAsmBackend(llvm::endianness::big) {}

  // Override MCAsmBackend
  unsigned getNumFixupKinds() const override {
    return SystemZ::NumTargetFixupKinds;
  }
  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override;
  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;
  bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                             const MCValue &Target,
                             const MCSubtargetInfo *STI) override;
  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;
  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
};
} // end anonymous namespace

std::optional<MCFixupKind>
SystemZMCAsmBackend::getFixupKind(StringRef Name) const {
  unsigned Type = llvm::StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y) .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/SystemZ.def"
#undef ELF_RELOC
			.Case("BFD_RELOC_NONE", ELF::R_390_NONE)
			.Case("BFD_RELOC_8", ELF::R_390_8)
			.Case("BFD_RELOC_16", ELF::R_390_16)
			.Case("BFD_RELOC_32", ELF::R_390_32)
			.Case("BFD_RELOC_64", ELF::R_390_64)
			.Default(-1u);
  if (Type != -1u)
    return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
  return std::nullopt;
}

const MCFixupKindInfo &
SystemZMCAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  // Fixup kinds from .reloc directive are like R_390_NONE. They
  // do not require any extra processing.
  if (Kind >= FirstLiteralRelocationKind)
    return MCAsmBackend::getFixupKindInfo(FK_NONE);

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  return SystemZ::MCFixupKindInfos[Kind - FirstTargetFixupKind];
}

bool SystemZMCAsmBackend::shouldForceRelocation(const MCAssembler &,
                                                const MCFixup &Fixup,
                                                const MCValue &,
                                                const MCSubtargetInfo *STI) {
  return Fixup.getKind() >= FirstLiteralRelocationKind;
}

void SystemZMCAsmBackend::applyFixup(const MCAssembler &Asm,
                                     const MCFixup &Fixup,
                                     const MCValue &Target,
                                     MutableArrayRef<char> Data, uint64_t Value,
                                     bool IsResolved,
                                     const MCSubtargetInfo *STI) const {
  MCFixupKind Kind = Fixup.getKind();
  if (Kind >= FirstLiteralRelocationKind)
    return;
  unsigned Offset = Fixup.getOffset();
  unsigned BitSize = getFixupKindInfo(Kind).TargetSize;
  unsigned Size = (BitSize + 7) / 8;

  assert(Offset + Size <= Data.size() && "Invalid fixup offset!");

  // Big-endian insertion of Size bytes.
  Value = extractBitsForFixup(Kind, Value, Fixup, Asm.getContext());
  if (BitSize < 64)
    Value &= ((uint64_t)1 << BitSize) - 1;
  unsigned ShiftValue = (Size * 8) - 8;
  for (unsigned I = 0; I != Size; ++I) {
    Data[Offset + I] |= uint8_t(Value >> ShiftValue);
    ShiftValue -= 8;
  }
}

bool SystemZMCAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                       const MCSubtargetInfo *STI) const {
  for (uint64_t I = 0; I != Count; ++I)
    OS << '\x7';
  return true;
}

namespace {
class ELFSystemZAsmBackend : public SystemZMCAsmBackend {
  uint8_t OSABI;

public:
  ELFSystemZAsmBackend(uint8_t OsABI) : SystemZMCAsmBackend(), OSABI(OsABI){};

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createSystemZELFObjectWriter(OSABI);
  }
};

class GOFFSystemZAsmBackend : public SystemZMCAsmBackend {
public:
  GOFFSystemZAsmBackend() : SystemZMCAsmBackend(){};

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createSystemZGOFFObjectWriter();
  }
};
} // namespace

MCAsmBackend *llvm::createSystemZMCAsmBackend(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              const MCRegisterInfo &MRI,
                                              const MCTargetOptions &Options) {
  if (STI.getTargetTriple().isOSzOS()) {
    return new GOFFSystemZAsmBackend();
  }

  uint8_t OSABI =
      MCELFObjectTargetWriter::getOSABI(STI.getTargetTriple().getOS());
  return new ELFSystemZAsmBackend(OSABI);
}
