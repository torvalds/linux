//===-- ARMAsmBackend.cpp - ARM Assembler Backend -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/ARMAsmBackend.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMAsmBackendDarwin.h"
#include "MCTargetDesc/ARMAsmBackendELF.h"
#include "MCTargetDesc/ARMAsmBackendWinCOFF.h"
#include "MCTargetDesc/ARMFixupKinds.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
class ARMELFObjectWriter : public MCELFObjectTargetWriter {
public:
  ARMELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit*/ false, OSABI, ELF::EM_ARM,
                                /*HasRelocationAddend*/ false) {}
};
} // end anonymous namespace

std::optional<MCFixupKind> ARMAsmBackend::getFixupKind(StringRef Name) const {
  return std::nullopt;
}

std::optional<MCFixupKind>
ARMAsmBackendELF::getFixupKind(StringRef Name) const {
  unsigned Type = llvm::StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y) .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/ARM.def"
#undef ELF_RELOC
                      .Case("BFD_RELOC_NONE", ELF::R_ARM_NONE)
                      .Case("BFD_RELOC_8", ELF::R_ARM_ABS8)
                      .Case("BFD_RELOC_16", ELF::R_ARM_ABS16)
                      .Case("BFD_RELOC_32", ELF::R_ARM_ABS32)
                      .Default(-1u);
  if (Type == -1u)
    return std::nullopt;
  return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
}

const MCFixupKindInfo &ARMAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  const static MCFixupKindInfo InfosLE[ARM::NumTargetFixupKinds] = {
      // This table *must* be in the order that the fixup_* kinds are defined in
      // ARMFixupKinds.h.
      //
      // Name                      Offset (bits) Size (bits)     Flags
      {"fixup_arm_ldst_pcrel_12", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_ldst_pcrel_12", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_pcrel_10_unscaled", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_pcrel_10", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_pcrel_10", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_pcrel_9", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_pcrel_9", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_ldst_abs_12", 0, 32, 0},
      {"fixup_thumb_adr_pcrel_10", 0, 8,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_adr_pcrel_12", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_adr_pcrel_12", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_condbranch", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_uncondbranch", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_condbranch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_uncondbranch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_br", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_uncondbl", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_condbl", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_blx", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_bl", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_blx", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_thumb_cb", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_cp", 0, 8,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_thumb_bcc", 0, 8, MCFixupKindInfo::FKF_IsPCRel},
      // movw / movt: 16-bits immediate but scattered into two chunks 0 - 12, 16
      // - 19.
      {"fixup_arm_movt_hi16", 0, 20, 0},
      {"fixup_arm_movw_lo16", 0, 20, 0},
      {"fixup_t2_movt_hi16", 0, 20, 0},
      {"fixup_t2_movw_lo16", 0, 20, 0},
      {"fixup_arm_thumb_upper_8_15", 0, 8, 0},
      {"fixup_arm_thumb_upper_0_7", 0, 8, 0},
      {"fixup_arm_thumb_lower_8_15", 0, 8, 0},
      {"fixup_arm_thumb_lower_0_7", 0, 8, 0},
      {"fixup_arm_mod_imm", 0, 12, 0},
      {"fixup_t2_so_imm", 0, 26, 0},
      {"fixup_bf_branch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_bf_target", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_bfl_target", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_bfc_target", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_bfcsel_else_target", 0, 32, 0},
      {"fixup_wls", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_le", 0, 32, MCFixupKindInfo::FKF_IsPCRel}};
  const static MCFixupKindInfo InfosBE[ARM::NumTargetFixupKinds] = {
      // This table *must* be in the order that the fixup_* kinds are defined in
      // ARMFixupKinds.h.
      //
      // Name                      Offset (bits) Size (bits)     Flags
      {"fixup_arm_ldst_pcrel_12", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_ldst_pcrel_12", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_pcrel_10_unscaled", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_pcrel_10", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_pcrel_10", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_pcrel_9", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_pcrel_9", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_ldst_abs_12", 0, 32, 0},
      {"fixup_thumb_adr_pcrel_10", 8, 8,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_adr_pcrel_12", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_adr_pcrel_12", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_condbranch", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_uncondbranch", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_condbranch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_t2_uncondbranch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_br", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_uncondbl", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_condbl", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_blx", 8, 24, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_bl", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_blx", 0, 32,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_thumb_cb", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_arm_thumb_cp", 8, 8,
       MCFixupKindInfo::FKF_IsPCRel |
           MCFixupKindInfo::FKF_IsAlignedDownTo32Bits},
      {"fixup_arm_thumb_bcc", 8, 8, MCFixupKindInfo::FKF_IsPCRel},
      // movw / movt: 16-bits immediate but scattered into two chunks 0 - 12, 16
      // - 19.
      {"fixup_arm_movt_hi16", 12, 20, 0},
      {"fixup_arm_movw_lo16", 12, 20, 0},
      {"fixup_t2_movt_hi16", 12, 20, 0},
      {"fixup_t2_movw_lo16", 12, 20, 0},
      {"fixup_arm_thumb_upper_8_15", 24, 8, 0},
      {"fixup_arm_thumb_upper_0_7", 24, 8, 0},
      {"fixup_arm_thumb_lower_8_15", 24, 8, 0},
      {"fixup_arm_thumb_lower_0_7", 24, 8, 0},
      {"fixup_arm_mod_imm", 20, 12, 0},
      {"fixup_t2_so_imm", 26, 6, 0},
      {"fixup_bf_branch", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_bf_target", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_bfl_target", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_bfc_target", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_bfcsel_else_target", 0, 32, 0},
      {"fixup_wls", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_le", 0, 32, MCFixupKindInfo::FKF_IsPCRel}};

  // Fixup kinds from .reloc directive are like R_ARM_NONE. They do not require
  // any extra processing.
  if (Kind >= FirstLiteralRelocationKind)
    return MCAsmBackend::getFixupKindInfo(FK_NONE);

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  return (Endian == llvm::endianness::little
              ? InfosLE
              : InfosBE)[Kind - FirstTargetFixupKind];
}

void ARMAsmBackend::handleAssemblerFlag(MCAssemblerFlag Flag) {
  switch (Flag) {
  default:
    break;
  case MCAF_Code16:
    setIsThumb(true);
    break;
  case MCAF_Code32:
    setIsThumb(false);
    break;
  }
}

unsigned ARMAsmBackend::getRelaxedOpcode(unsigned Op,
                                         const MCSubtargetInfo &STI) const {
  bool HasThumb2 = STI.hasFeature(ARM::FeatureThumb2);
  bool HasV8MBaselineOps = STI.hasFeature(ARM::HasV8MBaselineOps);

  switch (Op) {
  default:
    return Op;
  case ARM::tBcc:
    return HasThumb2 ? (unsigned)ARM::t2Bcc : Op;
  case ARM::tLDRpci:
    return HasThumb2 ? (unsigned)ARM::t2LDRpci : Op;
  case ARM::tADR:
    return HasThumb2 ? (unsigned)ARM::t2ADR : Op;
  case ARM::tB:
    return HasV8MBaselineOps ? (unsigned)ARM::t2B : Op;
  case ARM::tCBZ:
    return ARM::tHINT;
  case ARM::tCBNZ:
    return ARM::tHINT;
  }
}

bool ARMAsmBackend::mayNeedRelaxation(const MCInst &Inst,
                                      const MCSubtargetInfo &STI) const {
  if (getRelaxedOpcode(Inst.getOpcode(), STI) != Inst.getOpcode())
    return true;
  return false;
}

static const char *checkPCRelOffset(uint64_t Value, int64_t Min, int64_t Max) {
  int64_t Offset = int64_t(Value) - 4;
  if (Offset < Min || Offset > Max)
    return "out of range pc-relative fixup value";
  return nullptr;
}

const char *ARMAsmBackend::reasonForFixupRelaxation(const MCFixup &Fixup,
                                                    uint64_t Value) const {
  switch (Fixup.getTargetKind()) {
  case ARM::fixup_arm_thumb_br: {
    // Relaxing tB to t2B. tB has a signed 12-bit displacement with the
    // low bit being an implied zero. There's an implied +4 offset for the
    // branch, so we adjust the other way here to determine what's
    // encodable.
    //
    // Relax if the value is too big for a (signed) i8.
    int64_t Offset = int64_t(Value) - 4;
    if (Offset > 2046 || Offset < -2048)
      return "out of range pc-relative fixup value";
    break;
  }
  case ARM::fixup_arm_thumb_bcc: {
    // Relaxing tBcc to t2Bcc. tBcc has a signed 9-bit displacement with the
    // low bit being an implied zero. There's an implied +4 offset for the
    // branch, so we adjust the other way here to determine what's
    // encodable.
    //
    // Relax if the value is too big for a (signed) i8.
    int64_t Offset = int64_t(Value) - 4;
    if (Offset > 254 || Offset < -256)
      return "out of range pc-relative fixup value";
    break;
  }
  case ARM::fixup_thumb_adr_pcrel_10:
  case ARM::fixup_arm_thumb_cp: {
    // If the immediate is negative, greater than 1020, or not a multiple
    // of four, the wide version of the instruction must be used.
    int64_t Offset = int64_t(Value) - 4;
    if (Offset & 3)
      return "misaligned pc-relative fixup value";
    else if (Offset > 1020 || Offset < 0)
      return "out of range pc-relative fixup value";
    break;
  }
  case ARM::fixup_arm_thumb_cb: {
    // If we have a Thumb CBZ or CBNZ instruction and its target is the next
    // instruction it is actually out of range for the instruction.
    // It will be changed to a NOP.
    int64_t Offset = (Value & ~1);
    if (Offset == 2)
      return "will be converted to nop";
    break;
  }
  case ARM::fixup_bf_branch:
    return checkPCRelOffset(Value, 0, 30);
  case ARM::fixup_bf_target:
    return checkPCRelOffset(Value, -0x10000, +0xfffe);
  case ARM::fixup_bfl_target:
    return checkPCRelOffset(Value, -0x40000, +0x3fffe);
  case ARM::fixup_bfc_target:
    return checkPCRelOffset(Value, -0x1000, +0xffe);
  case ARM::fixup_wls:
    return checkPCRelOffset(Value, 0, +0xffe);
  case ARM::fixup_le:
    // The offset field in the LE and LETP instructions is an 11-bit
    // value shifted left by 2 (i.e. 0,2,4,...,4094), and it is
    // interpreted as a negative offset from the value read from pc,
    // i.e. from instruction_address+4.
    //
    // So an LE instruction can in principle address the instruction
    // immediately after itself, or (not very usefully) the address
    // half way through the 4-byte LE.
    return checkPCRelOffset(Value, -0xffe, 0);
  case ARM::fixup_bfcsel_else_target: {
    if (Value != 2 && Value != 4)
      return "out of range label-relative fixup value";
    break;
  }

  default:
    llvm_unreachable("Unexpected fixup kind in reasonForFixupRelaxation()!");
  }
  return nullptr;
}

bool ARMAsmBackend::fixupNeedsRelaxation(const MCFixup &Fixup,
                                         uint64_t Value) const {
  return reasonForFixupRelaxation(Fixup, Value);
}

void ARMAsmBackend::relaxInstruction(MCInst &Inst,
                                     const MCSubtargetInfo &STI) const {
  unsigned RelaxedOp = getRelaxedOpcode(Inst.getOpcode(), STI);

  // Return a diagnostic if we get here w/ a bogus instruction.
  if (RelaxedOp == Inst.getOpcode()) {
    SmallString<256> Tmp;
    raw_svector_ostream OS(Tmp);
    Inst.dump_pretty(OS);
    OS << "\n";
    report_fatal_error("unexpected instruction to relax: " + OS.str());
  }

  // If we are changing Thumb CBZ or CBNZ instruction to a NOP, aka tHINT, we
  // have to change the operands too.
  if ((Inst.getOpcode() == ARM::tCBZ || Inst.getOpcode() == ARM::tCBNZ) &&
      RelaxedOp == ARM::tHINT) {
    MCInst Res;
    Res.setOpcode(RelaxedOp);
    Res.addOperand(MCOperand::createImm(0));
    Res.addOperand(MCOperand::createImm(14));
    Res.addOperand(MCOperand::createReg(0));
    Inst = std::move(Res);
    return;
  }

  // The rest of instructions we're relaxing have the same operands.
  // We just need to update to the proper opcode.
  Inst.setOpcode(RelaxedOp);
}

bool ARMAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                 const MCSubtargetInfo *STI) const {
  const uint16_t Thumb1_16bitNopEncoding = 0x46c0; // using MOV r8,r8
  const uint16_t Thumb2_16bitNopEncoding = 0xbf00; // NOP
  const uint32_t ARMv4_NopEncoding = 0xe1a00000;   // using MOV r0,r0
  const uint32_t ARMv6T2_NopEncoding = 0xe320f000; // NOP
  if (isThumb()) {
    const uint16_t nopEncoding =
        hasNOP(STI) ? Thumb2_16bitNopEncoding : Thumb1_16bitNopEncoding;
    uint64_t NumNops = Count / 2;
    for (uint64_t i = 0; i != NumNops; ++i)
      support::endian::write(OS, nopEncoding, Endian);
    if (Count & 1)
      OS << '\0';
    return true;
  }
  // ARM mode
  const uint32_t nopEncoding =
      hasNOP(STI) ? ARMv6T2_NopEncoding : ARMv4_NopEncoding;
  uint64_t NumNops = Count / 4;
  for (uint64_t i = 0; i != NumNops; ++i)
    support::endian::write(OS, nopEncoding, Endian);
  // FIXME: should this function return false when unable to write exactly
  // 'Count' bytes with NOP encodings?
  switch (Count % 4) {
  default:
    break; // No leftover bytes to write
  case 1:
    OS << '\0';
    break;
  case 2:
    OS.write("\0\0", 2);
    break;
  case 3:
    OS.write("\0\0\xa0", 3);
    break;
  }

  return true;
}

static uint32_t swapHalfWords(uint32_t Value, bool IsLittleEndian) {
  if (IsLittleEndian) {
    // Note that the halfwords are stored high first and low second in thumb;
    // so we need to swap the fixup value here to map properly.
    uint32_t Swapped = (Value & 0xFFFF0000) >> 16;
    Swapped |= (Value & 0x0000FFFF) << 16;
    return Swapped;
  } else
    return Value;
}

static uint32_t joinHalfWords(uint32_t FirstHalf, uint32_t SecondHalf,
                              bool IsLittleEndian) {
  uint32_t Value;

  if (IsLittleEndian) {
    Value = (SecondHalf & 0xFFFF) << 16;
    Value |= (FirstHalf & 0xFFFF);
  } else {
    Value = (SecondHalf & 0xFFFF);
    Value |= (FirstHalf & 0xFFFF) << 16;
  }

  return Value;
}

unsigned ARMAsmBackend::adjustFixupValue(const MCAssembler &Asm,
                                         const MCFixup &Fixup,
                                         const MCValue &Target, uint64_t Value,
                                         bool IsResolved, MCContext &Ctx,
                                         const MCSubtargetInfo* STI) const {
  unsigned Kind = Fixup.getKind();

  // MachO tries to make .o files that look vaguely pre-linked, so for MOVW/MOVT
  // and .word relocations they put the Thumb bit into the addend if possible.
  // Other relocation types don't want this bit though (branches couldn't encode
  // it if it *was* present, and no other relocations exist) and it can
  // interfere with checking valid expressions.
  if (const MCSymbolRefExpr *A = Target.getSymA()) {
    if (A->hasSubsectionsViaSymbols() && Asm.isThumbFunc(&A->getSymbol()) &&
        A->getSymbol().isExternal() &&
        (Kind == FK_Data_4 || Kind == ARM::fixup_arm_movw_lo16 ||
         Kind == ARM::fixup_arm_movt_hi16 || Kind == ARM::fixup_t2_movw_lo16 ||
         Kind == ARM::fixup_t2_movt_hi16))
      Value |= 1;
  }

  switch (Kind) {
  default:
    return 0;
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
    return Value;
  case FK_SecRel_2:
    return Value;
  case FK_SecRel_4:
    return Value;
  case ARM::fixup_arm_movt_hi16:
    assert(STI != nullptr);
    if (IsResolved || !STI->getTargetTriple().isOSBinFormatELF())
      Value >>= 16;
    [[fallthrough]];
  case ARM::fixup_arm_movw_lo16: {
    unsigned Hi4 = (Value & 0xF000) >> 12;
    unsigned Lo12 = Value & 0x0FFF;
    // inst{19-16} = Hi4;
    // inst{11-0} = Lo12;
    Value = (Hi4 << 16) | (Lo12);
    return Value;
  }
  case ARM::fixup_t2_movt_hi16:
    assert(STI != nullptr);
    if (IsResolved || !STI->getTargetTriple().isOSBinFormatELF())
      Value >>= 16;
    [[fallthrough]];
  case ARM::fixup_t2_movw_lo16: {
    unsigned Hi4 = (Value & 0xF000) >> 12;
    unsigned i = (Value & 0x800) >> 11;
    unsigned Mid3 = (Value & 0x700) >> 8;
    unsigned Lo8 = Value & 0x0FF;
    // inst{19-16} = Hi4;
    // inst{26} = i;
    // inst{14-12} = Mid3;
    // inst{7-0} = Lo8;
    Value = (Hi4 << 16) | (i << 26) | (Mid3 << 12) | (Lo8);
    return swapHalfWords(Value, Endian == llvm::endianness::little);
  }
  case ARM::fixup_arm_thumb_upper_8_15:
    if (IsResolved || !STI->getTargetTriple().isOSBinFormatELF())
      return (Value & 0xff000000) >> 24;
    return Value & 0xff;
  case ARM::fixup_arm_thumb_upper_0_7:
    if (IsResolved || !STI->getTargetTriple().isOSBinFormatELF())
      return (Value & 0x00ff0000) >> 16;
    return Value & 0xff;
  case ARM::fixup_arm_thumb_lower_8_15:
    if (IsResolved || !STI->getTargetTriple().isOSBinFormatELF())
      return (Value & 0x0000ff00) >> 8;
    return Value & 0xff;
  case ARM::fixup_arm_thumb_lower_0_7:
    return Value & 0x000000ff;
  case ARM::fixup_arm_ldst_pcrel_12:
    // ARM PC-relative values are offset by 8.
    Value -= 4;
    [[fallthrough]];
  case ARM::fixup_t2_ldst_pcrel_12:
    // Offset by 4, adjusted by two due to the half-word ordering of thumb.
    Value -= 4;
    [[fallthrough]];
  case ARM::fixup_arm_ldst_abs_12: {
    bool isAdd = true;
    if ((int64_t)Value < 0) {
      Value = -Value;
      isAdd = false;
    }
    if (Value >= 4096) {
      Ctx.reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    Value |= isAdd << 23;

    // Same addressing mode as fixup_arm_pcrel_10,
    // but with 16-bit halfwords swapped.
    if (Kind == ARM::fixup_t2_ldst_pcrel_12)
      return swapHalfWords(Value, Endian == llvm::endianness::little);

    return Value;
  }
  case ARM::fixup_arm_adr_pcrel_12: {
    // ARM PC-relative values are offset by 8.
    Value -= 8;
    unsigned opc = 4; // bits {24-21}. Default to add: 0b0100
    if ((int64_t)Value < 0) {
      Value = -Value;
      opc = 2; // 0b0010
    }
    if (ARM_AM::getSOImmVal(Value) == -1) {
      Ctx.reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    // Encode the immediate and shift the opcode into place.
    return ARM_AM::getSOImmVal(Value) | (opc << 21);
  }

  case ARM::fixup_t2_adr_pcrel_12: {
    Value -= 4;
    unsigned opc = 0;
    if ((int64_t)Value < 0) {
      Value = -Value;
      opc = 5;
    }

    uint32_t out = (opc << 21);
    out |= (Value & 0x800) << 15;
    out |= (Value & 0x700) << 4;
    out |= (Value & 0x0FF);

    return swapHalfWords(out, Endian == llvm::endianness::little);
  }

  case ARM::fixup_arm_condbranch:
  case ARM::fixup_arm_uncondbranch:
  case ARM::fixup_arm_uncondbl:
  case ARM::fixup_arm_condbl:
  case ARM::fixup_arm_blx:
    // These values don't encode the low two bits since they're always zero.
    // Offset by 8 just as above.
    if (const MCSymbolRefExpr *SRE =
            dyn_cast<MCSymbolRefExpr>(Fixup.getValue()))
      if (SRE->getKind() == MCSymbolRefExpr::VK_TLSCALL)
        return 0;
    return 0xffffff & ((Value - 8) >> 2);
  case ARM::fixup_t2_uncondbranch: {
    Value = Value - 4;
    if (!isInt<25>(Value)) {
      Ctx.reportError(Fixup.getLoc(), "Relocation out of range");
      return 0;
    }

    Value >>= 1; // Low bit is not encoded.

    uint32_t out = 0;
    bool I = Value & 0x800000;
    bool J1 = Value & 0x400000;
    bool J2 = Value & 0x200000;
    J1 ^= I;
    J2 ^= I;

    out |= I << 26;                 // S bit
    out |= !J1 << 13;               // J1 bit
    out |= !J2 << 11;               // J2 bit
    out |= (Value & 0x1FF800) << 5; // imm6 field
    out |= (Value & 0x0007FF);      // imm11 field

    return swapHalfWords(out, Endian == llvm::endianness::little);
  }
  case ARM::fixup_t2_condbranch: {
    Value = Value - 4;
    if (!isInt<21>(Value)) {
      Ctx.reportError(Fixup.getLoc(), "Relocation out of range");
      return 0;
    }

    Value >>= 1; // Low bit is not encoded.

    uint64_t out = 0;
    out |= (Value & 0x80000) << 7; // S bit
    out |= (Value & 0x40000) >> 7; // J2 bit
    out |= (Value & 0x20000) >> 4; // J1 bit
    out |= (Value & 0x1F800) << 5; // imm6 field
    out |= (Value & 0x007FF);      // imm11 field

    return swapHalfWords(out, Endian == llvm::endianness::little);
  }
  case ARM::fixup_arm_thumb_bl: {
    if (!isInt<25>(Value - 4) ||
        (!STI->hasFeature(ARM::FeatureThumb2) &&
         !STI->hasFeature(ARM::HasV8MBaselineOps) &&
         !STI->hasFeature(ARM::HasV6MOps) &&
         !isInt<23>(Value - 4))) {
      Ctx.reportError(Fixup.getLoc(), "Relocation out of range");
      return 0;
    }

    // The value doesn't encode the low bit (always zero) and is offset by
    // four. The 32-bit immediate value is encoded as
    //   imm32 = SignExtend(S:I1:I2:imm10:imm11:0)
    // where I1 = NOT(J1 ^ S) and I2 = NOT(J2 ^ S).
    // The value is encoded into disjoint bit positions in the destination
    // opcode. x = unchanged, I = immediate value bit, S = sign extension bit,
    // J = either J1 or J2 bit
    //
    //   BL:  xxxxxSIIIIIIIIII xxJxJIIIIIIIIIII
    //
    // Note that the halfwords are stored high first, low second; so we need
    // to transpose the fixup value here to map properly.
    uint32_t offset = (Value - 4) >> 1;
    uint32_t signBit = (offset & 0x800000) >> 23;
    uint32_t I1Bit = (offset & 0x400000) >> 22;
    uint32_t J1Bit = (I1Bit ^ 0x1) ^ signBit;
    uint32_t I2Bit = (offset & 0x200000) >> 21;
    uint32_t J2Bit = (I2Bit ^ 0x1) ^ signBit;
    uint32_t imm10Bits = (offset & 0x1FF800) >> 11;
    uint32_t imm11Bits = (offset & 0x000007FF);

    uint32_t FirstHalf = (((uint16_t)signBit << 10) | (uint16_t)imm10Bits);
    uint32_t SecondHalf = (((uint16_t)J1Bit << 13) | ((uint16_t)J2Bit << 11) |
                           (uint16_t)imm11Bits);
    return joinHalfWords(FirstHalf, SecondHalf,
                         Endian == llvm::endianness::little);
  }
  case ARM::fixup_arm_thumb_blx: {
    // The value doesn't encode the low two bits (always zero) and is offset by
    // four (see fixup_arm_thumb_cp). The 32-bit immediate value is encoded as
    //   imm32 = SignExtend(S:I1:I2:imm10H:imm10L:00)
    // where I1 = NOT(J1 ^ S) and I2 = NOT(J2 ^ S).
    // The value is encoded into disjoint bit positions in the destination
    // opcode. x = unchanged, I = immediate value bit, S = sign extension bit,
    // J = either J1 or J2 bit, 0 = zero.
    //
    //   BLX: xxxxxSIIIIIIIIII xxJxJIIIIIIIIII0
    //
    // Note that the halfwords are stored high first, low second; so we need
    // to transpose the fixup value here to map properly.
    if (Value % 4 != 0) {
      Ctx.reportError(Fixup.getLoc(), "misaligned ARM call destination");
      return 0;
    }

    uint32_t offset = (Value - 4) >> 2;
    if (const MCSymbolRefExpr *SRE =
            dyn_cast<MCSymbolRefExpr>(Fixup.getValue()))
      if (SRE->getKind() == MCSymbolRefExpr::VK_TLSCALL)
        offset = 0;
    uint32_t signBit = (offset & 0x400000) >> 22;
    uint32_t I1Bit = (offset & 0x200000) >> 21;
    uint32_t J1Bit = (I1Bit ^ 0x1) ^ signBit;
    uint32_t I2Bit = (offset & 0x100000) >> 20;
    uint32_t J2Bit = (I2Bit ^ 0x1) ^ signBit;
    uint32_t imm10HBits = (offset & 0xFFC00) >> 10;
    uint32_t imm10LBits = (offset & 0x3FF);

    uint32_t FirstHalf = (((uint16_t)signBit << 10) | (uint16_t)imm10HBits);
    uint32_t SecondHalf = (((uint16_t)J1Bit << 13) | ((uint16_t)J2Bit << 11) |
                           ((uint16_t)imm10LBits) << 1);
    return joinHalfWords(FirstHalf, SecondHalf,
                         Endian == llvm::endianness::little);
  }
  case ARM::fixup_thumb_adr_pcrel_10:
  case ARM::fixup_arm_thumb_cp:
    // On CPUs supporting Thumb2, this will be relaxed to an ldr.w, otherwise we
    // could have an error on our hands.
    assert(STI != nullptr);
    if (!STI->hasFeature(ARM::FeatureThumb2) && IsResolved) {
      const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
      if (FixupDiagnostic) {
        Ctx.reportError(Fixup.getLoc(), FixupDiagnostic);
        return 0;
      }
    }
    // Offset by 4, and don't encode the low two bits.
    return ((Value - 4) >> 2) & 0xff;
  case ARM::fixup_arm_thumb_cb: {
    // CB instructions can only branch to offsets in [4, 126] in multiples of 2
    // so ensure that the raw value LSB is zero and it lies in [2, 130].
    // An offset of 2 will be relaxed to a NOP.
    if ((int64_t)Value < 2 || Value > 0x82 || Value & 1) {
      Ctx.reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    // Offset by 4 and don't encode the lower bit, which is always 0.
    // FIXME: diagnose if no Thumb2
    uint32_t Binary = (Value - 4) >> 1;
    return ((Binary & 0x20) << 4) | ((Binary & 0x1f) << 3);
  }
  case ARM::fixup_arm_thumb_br:
    // Offset by 4 and don't encode the lower bit, which is always 0.
    assert(STI != nullptr);
    if (!STI->hasFeature(ARM::FeatureThumb2) &&
        !STI->hasFeature(ARM::HasV8MBaselineOps)) {
      const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
      if (FixupDiagnostic) {
        Ctx.reportError(Fixup.getLoc(), FixupDiagnostic);
        return 0;
      }
    }
    return ((Value - 4) >> 1) & 0x7ff;
  case ARM::fixup_arm_thumb_bcc:
    // Offset by 4 and don't encode the lower bit, which is always 0.
    assert(STI != nullptr);
    if (!STI->hasFeature(ARM::FeatureThumb2)) {
      const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
      if (FixupDiagnostic) {
        Ctx.reportError(Fixup.getLoc(), FixupDiagnostic);
        return 0;
      }
    }
    return ((Value - 4) >> 1) & 0xff;
  case ARM::fixup_arm_pcrel_10_unscaled: {
    Value = Value - 8; // ARM fixups offset by an additional word and don't
                       // need to adjust for the half-word ordering.
    bool isAdd = true;
    if ((int64_t)Value < 0) {
      Value = -Value;
      isAdd = false;
    }
    // The value has the low 4 bits encoded in [3:0] and the high 4 in [11:8].
    if (Value >= 256) {
      Ctx.reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    Value = (Value & 0xf) | ((Value & 0xf0) << 4);
    return Value | (isAdd << 23);
  }
  case ARM::fixup_arm_pcrel_10:
    Value = Value - 4; // ARM fixups offset by an additional word and don't
                       // need to adjust for the half-word ordering.
    [[fallthrough]];
  case ARM::fixup_t2_pcrel_10: {
    // Offset by 4, adjusted by two due to the half-word ordering of thumb.
    Value = Value - 4;
    bool isAdd = true;
    if ((int64_t)Value < 0) {
      Value = -Value;
      isAdd = false;
    }
    // These values don't encode the low two bits since they're always zero.
    Value >>= 2;
    if (Value >= 256) {
      Ctx.reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    Value |= isAdd << 23;

    // Same addressing mode as fixup_arm_pcrel_10, but with 16-bit halfwords
    // swapped.
    if (Kind == ARM::fixup_t2_pcrel_10)
      return swapHalfWords(Value, Endian == llvm::endianness::little);

    return Value;
  }
  case ARM::fixup_arm_pcrel_9:
    Value = Value - 4; // ARM fixups offset by an additional word and don't
                       // need to adjust for the half-word ordering.
    [[fallthrough]];
  case ARM::fixup_t2_pcrel_9: {
    // Offset by 4, adjusted by two due to the half-word ordering of thumb.
    Value = Value - 4;
    bool isAdd = true;
    if ((int64_t)Value < 0) {
      Value = -Value;
      isAdd = false;
    }
    // These values don't encode the low bit since it's always zero.
    if (Value & 1) {
      Ctx.reportError(Fixup.getLoc(), "invalid value for this fixup");
      return 0;
    }
    Value >>= 1;
    if (Value >= 256) {
      Ctx.reportError(Fixup.getLoc(), "out of range pc-relative fixup value");
      return 0;
    }
    Value |= isAdd << 23;

    // Same addressing mode as fixup_arm_pcrel_9, but with 16-bit halfwords
    // swapped.
    if (Kind == ARM::fixup_t2_pcrel_9)
      return swapHalfWords(Value, Endian == llvm::endianness::little);

    return Value;
  }
  case ARM::fixup_arm_mod_imm:
    Value = ARM_AM::getSOImmVal(Value);
    if (Value >> 12) {
      Ctx.reportError(Fixup.getLoc(), "out of range immediate fixup value");
      return 0;
    }
    return Value;
  case ARM::fixup_t2_so_imm: {
    Value = ARM_AM::getT2SOImmVal(Value);
    if ((int64_t)Value < 0) {
      Ctx.reportError(Fixup.getLoc(), "out of range immediate fixup value");
      return 0;
    }
    // Value will contain a 12-bit value broken up into a 4-bit shift in bits
    // 11:8 and the 8-bit immediate in 0:7. The instruction has the immediate
    // in 0:7. The 4-bit shift is split up into i:imm3 where i is placed at bit
    // 10 of the upper half-word and imm3 is placed at 14:12 of the lower
    // half-word.
    uint64_t EncValue = 0;
    EncValue |= (Value & 0x800) << 15;
    EncValue |= (Value & 0x700) << 4;
    EncValue |= (Value & 0xff);
    return swapHalfWords(EncValue, Endian == llvm::endianness::little);
  }
  case ARM::fixup_bf_branch: {
    const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
    if (FixupDiagnostic) {
      Ctx.reportError(Fixup.getLoc(), FixupDiagnostic);
      return 0;
    }
    uint32_t out = (((Value - 4) >> 1) & 0xf) << 23;
    return swapHalfWords(out, Endian == llvm::endianness::little);
  }
  case ARM::fixup_bf_target:
  case ARM::fixup_bfl_target:
  case ARM::fixup_bfc_target: {
    const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
    if (FixupDiagnostic) {
      Ctx.reportError(Fixup.getLoc(), FixupDiagnostic);
      return 0;
    }
    uint32_t out = 0;
    uint32_t HighBitMask = (Kind == ARM::fixup_bf_target ? 0xf800 :
                            Kind == ARM::fixup_bfl_target ? 0x3f800 : 0x800);
    out |= (((Value - 4) >> 1) & 0x1) << 11;
    out |= (((Value - 4) >> 1) & 0x7fe);
    out |= (((Value - 4) >> 1) & HighBitMask) << 5;
    return swapHalfWords(out, Endian == llvm::endianness::little);
  }
  case ARM::fixup_bfcsel_else_target: {
    // If this is a fixup of a branch future's else target then it should be a
    // constant MCExpr representing the distance between the branch targetted
    // and the instruction after that same branch.
    Value = Target.getConstant();

    const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
    if (FixupDiagnostic) {
      Ctx.reportError(Fixup.getLoc(), FixupDiagnostic);
      return 0;
    }
    uint32_t out = ((Value >> 2) & 1) << 17;
    return swapHalfWords(out, Endian == llvm::endianness::little);
  }
  case ARM::fixup_wls:
  case ARM::fixup_le: {
    const char *FixupDiagnostic = reasonForFixupRelaxation(Fixup, Value);
    if (FixupDiagnostic) {
      Ctx.reportError(Fixup.getLoc(), FixupDiagnostic);
      return 0;
    }
    uint64_t real_value = Value - 4;
    uint32_t out = 0;
    if (Kind == ARM::fixup_le)
      real_value = -real_value;
    out |= ((real_value >> 1) & 0x1) << 11;
    out |= ((real_value >> 1) & 0x7fe);
    return swapHalfWords(out, Endian == llvm::endianness::little);
  }
  }
}

bool ARMAsmBackend::shouldForceRelocation(const MCAssembler &Asm,
                                          const MCFixup &Fixup,
                                          const MCValue &Target,
                                          const MCSubtargetInfo *STI) {
  const MCSymbolRefExpr *A = Target.getSymA();
  const MCSymbol *Sym = A ? &A->getSymbol() : nullptr;
  const unsigned FixupKind = Fixup.getKind();
  if (FixupKind >= FirstLiteralRelocationKind)
    return true;
  if (FixupKind == ARM::fixup_arm_thumb_bl) {
    assert(Sym && "How did we resolve this?");

    // If the symbol is external the linker will handle it.
    // FIXME: Should we handle it as an optimization?

    // If the symbol is out of range, produce a relocation and hope the
    // linker can handle it. GNU AS produces an error in this case.
    if (Sym->isExternal())
      return true;
  }
  // Create relocations for unconditional branches to function symbols with
  // different execution mode in ELF binaries.
  if (Sym && Sym->isELF()) {
    unsigned Type = cast<MCSymbolELF>(Sym)->getType();
    if ((Type == ELF::STT_FUNC || Type == ELF::STT_GNU_IFUNC)) {
      if (Asm.isThumbFunc(Sym) && (FixupKind == ARM::fixup_arm_uncondbranch))
        return true;
      if (!Asm.isThumbFunc(Sym) && (FixupKind == ARM::fixup_arm_thumb_br ||
                                    FixupKind == ARM::fixup_arm_thumb_bl ||
                                    FixupKind == ARM::fixup_t2_condbranch ||
                                    FixupKind == ARM::fixup_t2_uncondbranch))
        return true;
    }
  }
  // We must always generate a relocation for BL/BLX instructions if we have
  // a symbol to reference, as the linker relies on knowing the destination
  // symbol's thumb-ness to get interworking right.
  if (A && (FixupKind == ARM::fixup_arm_thumb_blx ||
            FixupKind == ARM::fixup_arm_blx ||
            FixupKind == ARM::fixup_arm_uncondbl ||
            FixupKind == ARM::fixup_arm_condbl))
    return true;
  return false;
}

/// getFixupKindNumBytes - The number of bytes the fixup may change.
static unsigned getFixupKindNumBytes(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");

  case FK_Data_1:
  case ARM::fixup_arm_thumb_bcc:
  case ARM::fixup_arm_thumb_cp:
  case ARM::fixup_thumb_adr_pcrel_10:
  case ARM::fixup_arm_thumb_upper_8_15:
  case ARM::fixup_arm_thumb_upper_0_7:
  case ARM::fixup_arm_thumb_lower_8_15:
  case ARM::fixup_arm_thumb_lower_0_7:
    return 1;

  case FK_Data_2:
  case ARM::fixup_arm_thumb_br:
  case ARM::fixup_arm_thumb_cb:
  case ARM::fixup_arm_mod_imm:
    return 2;

  case ARM::fixup_arm_pcrel_10_unscaled:
  case ARM::fixup_arm_ldst_pcrel_12:
  case ARM::fixup_arm_pcrel_10:
  case ARM::fixup_arm_pcrel_9:
  case ARM::fixup_arm_ldst_abs_12:
  case ARM::fixup_arm_adr_pcrel_12:
  case ARM::fixup_arm_uncondbl:
  case ARM::fixup_arm_condbl:
  case ARM::fixup_arm_blx:
  case ARM::fixup_arm_condbranch:
  case ARM::fixup_arm_uncondbranch:
    return 3;

  case FK_Data_4:
  case ARM::fixup_t2_ldst_pcrel_12:
  case ARM::fixup_t2_condbranch:
  case ARM::fixup_t2_uncondbranch:
  case ARM::fixup_t2_pcrel_10:
  case ARM::fixup_t2_pcrel_9:
  case ARM::fixup_t2_adr_pcrel_12:
  case ARM::fixup_arm_thumb_bl:
  case ARM::fixup_arm_thumb_blx:
  case ARM::fixup_arm_movt_hi16:
  case ARM::fixup_arm_movw_lo16:
  case ARM::fixup_t2_movt_hi16:
  case ARM::fixup_t2_movw_lo16:
  case ARM::fixup_t2_so_imm:
  case ARM::fixup_bf_branch:
  case ARM::fixup_bf_target:
  case ARM::fixup_bfl_target:
  case ARM::fixup_bfc_target:
  case ARM::fixup_bfcsel_else_target:
  case ARM::fixup_wls:
  case ARM::fixup_le:
    return 4;

  case FK_SecRel_2:
    return 2;
  case FK_SecRel_4:
    return 4;
  }
}

/// getFixupKindContainerSizeBytes - The number of bytes of the
/// container involved in big endian.
static unsigned getFixupKindContainerSizeBytes(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Unknown fixup kind!");

  case FK_Data_1:
    return 1;
  case FK_Data_2:
    return 2;
  case FK_Data_4:
    return 4;

  case ARM::fixup_arm_thumb_bcc:
  case ARM::fixup_arm_thumb_cp:
  case ARM::fixup_thumb_adr_pcrel_10:
  case ARM::fixup_arm_thumb_br:
  case ARM::fixup_arm_thumb_cb:
  case ARM::fixup_arm_thumb_upper_8_15:
  case ARM::fixup_arm_thumb_upper_0_7:
  case ARM::fixup_arm_thumb_lower_8_15:
  case ARM::fixup_arm_thumb_lower_0_7:
    // Instruction size is 2 bytes.
    return 2;

  case ARM::fixup_arm_pcrel_10_unscaled:
  case ARM::fixup_arm_ldst_pcrel_12:
  case ARM::fixup_arm_pcrel_10:
  case ARM::fixup_arm_pcrel_9:
  case ARM::fixup_arm_adr_pcrel_12:
  case ARM::fixup_arm_uncondbl:
  case ARM::fixup_arm_condbl:
  case ARM::fixup_arm_blx:
  case ARM::fixup_arm_condbranch:
  case ARM::fixup_arm_uncondbranch:
  case ARM::fixup_t2_ldst_pcrel_12:
  case ARM::fixup_t2_condbranch:
  case ARM::fixup_t2_uncondbranch:
  case ARM::fixup_t2_pcrel_10:
  case ARM::fixup_t2_pcrel_9:
  case ARM::fixup_t2_adr_pcrel_12:
  case ARM::fixup_arm_thumb_bl:
  case ARM::fixup_arm_thumb_blx:
  case ARM::fixup_arm_movt_hi16:
  case ARM::fixup_arm_movw_lo16:
  case ARM::fixup_t2_movt_hi16:
  case ARM::fixup_t2_movw_lo16:
  case ARM::fixup_arm_mod_imm:
  case ARM::fixup_t2_so_imm:
  case ARM::fixup_bf_branch:
  case ARM::fixup_bf_target:
  case ARM::fixup_bfl_target:
  case ARM::fixup_bfc_target:
  case ARM::fixup_bfcsel_else_target:
  case ARM::fixup_wls:
  case ARM::fixup_le:
    // Instruction size is 4 bytes.
    return 4;
  }
}

void ARMAsmBackend::applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                               const MCValue &Target,
                               MutableArrayRef<char> Data, uint64_t Value,
                               bool IsResolved,
                               const MCSubtargetInfo* STI) const {
  unsigned Kind = Fixup.getKind();
  if (Kind >= FirstLiteralRelocationKind)
    return;
  MCContext &Ctx = Asm.getContext();
  Value = adjustFixupValue(Asm, Fixup, Target, Value, IsResolved, Ctx, STI);
  if (!Value)
    return; // Doesn't change encoding.
  const unsigned NumBytes = getFixupKindNumBytes(Kind);

  unsigned Offset = Fixup.getOffset();
  assert(Offset + NumBytes <= Data.size() && "Invalid fixup offset!");

  // Used to point to big endian bytes.
  unsigned FullSizeBytes;
  if (Endian == llvm::endianness::big) {
    FullSizeBytes = getFixupKindContainerSizeBytes(Kind);
    assert((Offset + FullSizeBytes) <= Data.size() && "Invalid fixup size!");
    assert(NumBytes <= FullSizeBytes && "Invalid fixup size!");
  }

  // For each byte of the fragment that the fixup touches, mask in the bits from
  // the fixup value. The Value has been "split up" into the appropriate
  // bitfields above.
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned Idx =
        Endian == llvm::endianness::little ? i : (FullSizeBytes - 1 - i);
    Data[Offset + Idx] |= uint8_t((Value >> (i * 8)) & 0xff);
  }
}

namespace CU {

/// Compact unwind encoding values.
enum CompactUnwindEncodings {
  UNWIND_ARM_MODE_MASK                         = 0x0F000000,
  UNWIND_ARM_MODE_FRAME                        = 0x01000000,
  UNWIND_ARM_MODE_FRAME_D                      = 0x02000000,
  UNWIND_ARM_MODE_DWARF                        = 0x04000000,

  UNWIND_ARM_FRAME_STACK_ADJUST_MASK           = 0x00C00000,

  UNWIND_ARM_FRAME_FIRST_PUSH_R4               = 0x00000001,
  UNWIND_ARM_FRAME_FIRST_PUSH_R5               = 0x00000002,
  UNWIND_ARM_FRAME_FIRST_PUSH_R6               = 0x00000004,

  UNWIND_ARM_FRAME_SECOND_PUSH_R8              = 0x00000008,
  UNWIND_ARM_FRAME_SECOND_PUSH_R9              = 0x00000010,
  UNWIND_ARM_FRAME_SECOND_PUSH_R10             = 0x00000020,
  UNWIND_ARM_FRAME_SECOND_PUSH_R11             = 0x00000040,
  UNWIND_ARM_FRAME_SECOND_PUSH_R12             = 0x00000080,

  UNWIND_ARM_FRAME_D_REG_COUNT_MASK            = 0x00000F00,

  UNWIND_ARM_DWARF_SECTION_OFFSET              = 0x00FFFFFF
};

} // end CU namespace

/// Generate compact unwind encoding for the function based on the CFI
/// instructions. If the CFI instructions describe a frame that cannot be
/// encoded in compact unwind, the method returns UNWIND_ARM_MODE_DWARF which
/// tells the runtime to fallback and unwind using dwarf.
uint64_t ARMAsmBackendDarwin::generateCompactUnwindEncoding(
    const MCDwarfFrameInfo *FI, const MCContext *Ctxt) const {
  DEBUG_WITH_TYPE("compact-unwind", llvm::dbgs() << "generateCU()\n");
  // Only armv7k uses CFI based unwinding.
  if (Subtype != MachO::CPU_SUBTYPE_ARM_V7K)
    return 0;
  // No .cfi directives means no frame.
  ArrayRef<MCCFIInstruction> Instrs = FI->Instructions;
  if (Instrs.empty())
    return 0;
  if (!isDarwinCanonicalPersonality(FI->Personality) &&
      !Ctxt->emitCompactUnwindNonCanonical())
    return CU::UNWIND_ARM_MODE_DWARF;

  // Start off assuming CFA is at SP+0.
  unsigned CFARegister = ARM::SP;
  int CFARegisterOffset = 0;
  // Mark savable registers as initially unsaved
  DenseMap<unsigned, int> RegOffsets;
  int FloatRegCount = 0;
  // Process each .cfi directive and build up compact unwind info.
  for (const MCCFIInstruction &Inst : Instrs) {
    unsigned Reg;
    switch (Inst.getOperation()) {
    case MCCFIInstruction::OpDefCfa: // DW_CFA_def_cfa
      CFARegisterOffset = Inst.getOffset();
      CFARegister = *MRI.getLLVMRegNum(Inst.getRegister(), true);
      break;
    case MCCFIInstruction::OpDefCfaOffset: // DW_CFA_def_cfa_offset
      CFARegisterOffset = Inst.getOffset();
      break;
    case MCCFIInstruction::OpDefCfaRegister: // DW_CFA_def_cfa_register
      CFARegister = *MRI.getLLVMRegNum(Inst.getRegister(), true);
      break;
    case MCCFIInstruction::OpOffset: // DW_CFA_offset
      Reg = *MRI.getLLVMRegNum(Inst.getRegister(), true);
      if (ARMMCRegisterClasses[ARM::GPRRegClassID].contains(Reg))
        RegOffsets[Reg] = Inst.getOffset();
      else if (ARMMCRegisterClasses[ARM::DPRRegClassID].contains(Reg)) {
        RegOffsets[Reg] = Inst.getOffset();
        ++FloatRegCount;
      } else {
        DEBUG_WITH_TYPE("compact-unwind",
                        llvm::dbgs() << ".cfi_offset on unknown register="
                                     << Inst.getRegister() << "\n");
        return CU::UNWIND_ARM_MODE_DWARF;
      }
      break;
    case MCCFIInstruction::OpRelOffset: // DW_CFA_advance_loc
      // Ignore
      break;
    default:
      // Directive not convertable to compact unwind, bail out.
      DEBUG_WITH_TYPE("compact-unwind",
                      llvm::dbgs()
                          << "CFI directive not compatible with compact "
                             "unwind encoding, opcode="
                          << uint8_t(Inst.getOperation()) << "\n");
      return CU::UNWIND_ARM_MODE_DWARF;
      break;
    }
  }

  // If no frame set up, return no unwind info.
  if ((CFARegister == ARM::SP) && (CFARegisterOffset == 0))
    return 0;

  // Verify standard frame (lr/r7) was used.
  if (CFARegister != ARM::R7) {
    DEBUG_WITH_TYPE("compact-unwind", llvm::dbgs() << "frame register is "
                                                   << CFARegister
                                                   << " instead of r7\n");
    return CU::UNWIND_ARM_MODE_DWARF;
  }
  int StackAdjust = CFARegisterOffset - 8;
  if (RegOffsets.lookup(ARM::LR) != (-4 - StackAdjust)) {
    DEBUG_WITH_TYPE("compact-unwind",
                    llvm::dbgs()
                        << "LR not saved as standard frame, StackAdjust="
                        << StackAdjust
                        << ", CFARegisterOffset=" << CFARegisterOffset
                        << ", lr save at offset=" << RegOffsets[14] << "\n");
    return CU::UNWIND_ARM_MODE_DWARF;
  }
  if (RegOffsets.lookup(ARM::R7) != (-8 - StackAdjust)) {
    DEBUG_WITH_TYPE("compact-unwind",
                    llvm::dbgs() << "r7 not saved as standard frame\n");
    return CU::UNWIND_ARM_MODE_DWARF;
  }
  uint32_t CompactUnwindEncoding = CU::UNWIND_ARM_MODE_FRAME;

  // If var-args are used, there may be a stack adjust required.
  switch (StackAdjust) {
  case 0:
    break;
  case 4:
    CompactUnwindEncoding |= 0x00400000;
    break;
  case 8:
    CompactUnwindEncoding |= 0x00800000;
    break;
  case 12:
    CompactUnwindEncoding |= 0x00C00000;
    break;
  default:
    DEBUG_WITH_TYPE("compact-unwind", llvm::dbgs()
                                          << ".cfi_def_cfa stack adjust ("
                                          << StackAdjust << ") out of range\n");
    return CU::UNWIND_ARM_MODE_DWARF;
  }

  // If r6 is saved, it must be right below r7.
  static struct {
    unsigned Reg;
    unsigned Encoding;
  } GPRCSRegs[] = {{ARM::R6, CU::UNWIND_ARM_FRAME_FIRST_PUSH_R6},
                   {ARM::R5, CU::UNWIND_ARM_FRAME_FIRST_PUSH_R5},
                   {ARM::R4, CU::UNWIND_ARM_FRAME_FIRST_PUSH_R4},
                   {ARM::R12, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R12},
                   {ARM::R11, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R11},
                   {ARM::R10, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R10},
                   {ARM::R9, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R9},
                   {ARM::R8, CU::UNWIND_ARM_FRAME_SECOND_PUSH_R8}};

  int CurOffset = -8 - StackAdjust;
  for (auto CSReg : GPRCSRegs) {
    auto Offset = RegOffsets.find(CSReg.Reg);
    if (Offset == RegOffsets.end())
      continue;

    int RegOffset = Offset->second;
    if (RegOffset != CurOffset - 4) {
      DEBUG_WITH_TYPE("compact-unwind",
                      llvm::dbgs() << MRI.getName(CSReg.Reg) << " saved at "
                                   << RegOffset << " but only supported at "
                                   << CurOffset << "\n");
      return CU::UNWIND_ARM_MODE_DWARF;
    }
    CompactUnwindEncoding |= CSReg.Encoding;
    CurOffset -= 4;
  }

  // If no floats saved, we are done.
  if (FloatRegCount == 0)
    return CompactUnwindEncoding;

  // Switch mode to include D register saving.
  CompactUnwindEncoding &= ~CU::UNWIND_ARM_MODE_MASK;
  CompactUnwindEncoding |= CU::UNWIND_ARM_MODE_FRAME_D;

  // FIXME: supporting more than 4 saved D-registers compactly would be trivial,
  // but needs coordination with the linker and libunwind.
  if (FloatRegCount > 4) {
    DEBUG_WITH_TYPE("compact-unwind",
                    llvm::dbgs() << "unsupported number of D registers saved ("
                                 << FloatRegCount << ")\n");
      return CU::UNWIND_ARM_MODE_DWARF;
  }

  // Floating point registers must either be saved sequentially, or we defer to
  // DWARF. No gaps allowed here so check that each saved d-register is
  // precisely where it should be.
  static unsigned FPRCSRegs[] = { ARM::D8, ARM::D10, ARM::D12, ARM::D14 };
  for (int Idx = FloatRegCount - 1; Idx >= 0; --Idx) {
    auto Offset = RegOffsets.find(FPRCSRegs[Idx]);
    if (Offset == RegOffsets.end()) {
      DEBUG_WITH_TYPE("compact-unwind",
                      llvm::dbgs() << FloatRegCount << " D-regs saved, but "
                                   << MRI.getName(FPRCSRegs[Idx])
                                   << " not saved\n");
      return CU::UNWIND_ARM_MODE_DWARF;
    } else if (Offset->second != CurOffset - 8) {
      DEBUG_WITH_TYPE("compact-unwind",
                      llvm::dbgs() << FloatRegCount << " D-regs saved, but "
                                   << MRI.getName(FPRCSRegs[Idx])
                                   << " saved at " << Offset->second
                                   << ", expected at " << CurOffset - 8
                                   << "\n");
      return CU::UNWIND_ARM_MODE_DWARF;
    }
    CurOffset -= 8;
  }

  return CompactUnwindEncoding | ((FloatRegCount - 1) << 8);
}

static MCAsmBackend *createARMAsmBackend(const Target &T,
                                         const MCSubtargetInfo &STI,
                                         const MCRegisterInfo &MRI,
                                         const MCTargetOptions &Options,
                                         llvm::endianness Endian) {
  const Triple &TheTriple = STI.getTargetTriple();
  switch (TheTriple.getObjectFormat()) {
  default:
    llvm_unreachable("unsupported object format");
  case Triple::MachO:
    return new ARMAsmBackendDarwin(T, STI, MRI);
  case Triple::COFF:
    assert(TheTriple.isOSWindows() && "non-Windows ARM COFF is not supported");
    return new ARMAsmBackendWinCOFF(T, STI.getTargetTriple().isThumb());
  case Triple::ELF:
    assert(TheTriple.isOSBinFormatELF() && "using ELF for non-ELF target");
    uint8_t OSABI = Options.FDPIC
                        ? ELF::ELFOSABI_ARM_FDPIC
                        : MCELFObjectTargetWriter::getOSABI(TheTriple.getOS());
    return new ARMAsmBackendELF(T, STI.getTargetTriple().isThumb(), OSABI,
                                Endian);
  }
}

MCAsmBackend *llvm::createARMLEAsmBackend(const Target &T,
                                          const MCSubtargetInfo &STI,
                                          const MCRegisterInfo &MRI,
                                          const MCTargetOptions &Options) {
  return createARMAsmBackend(T, STI, MRI, Options, llvm::endianness::little);
}

MCAsmBackend *llvm::createARMBEAsmBackend(const Target &T,
                                          const MCSubtargetInfo &STI,
                                          const MCRegisterInfo &MRI,
                                          const MCTargetOptions &Options) {
  return createARMAsmBackend(T, STI, MRI, Options, llvm::endianness::big);
}
