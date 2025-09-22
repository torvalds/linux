//===-- HexagonAsmBackend.cpp - Hexagon Assembler Backend -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HexagonFixupKinds.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCChecker.h"
#include "MCTargetDesc/HexagonMCCodeEmitter.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCShuffler.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/EndianStream.h"

#include <sstream>

using namespace llvm;
using namespace Hexagon;

#define DEBUG_TYPE "hexagon-asm-backend"

static cl::opt<bool> DisableFixup
  ("mno-fixup", cl::desc("Disable fixing up resolved relocations for Hexagon"));

namespace {

class HexagonAsmBackend : public MCAsmBackend {
  uint8_t OSABI;
  StringRef CPU;
  mutable uint64_t relaxedCnt;
  std::unique_ptr <MCInstrInfo> MCII;
  std::unique_ptr <MCInst *> RelaxTarget;
  MCInst * Extender;
  unsigned MaxPacketSize;

  void ReplaceInstruction(MCCodeEmitter &E, MCRelaxableFragment &RF,
                          MCInst &HMB) const {
    SmallVector<MCFixup, 4> Fixups;
    SmallString<256> Code;
    E.encodeInstruction(HMB, Code, Fixups, *RF.getSubtargetInfo());

    // Update the fragment.
    RF.setInst(HMB);
    RF.getContents() = Code;
    RF.getFixups() = Fixups;
  }

public:
  HexagonAsmBackend(const Target &T, const Triple &TT, uint8_t OSABI,
                    StringRef CPU)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI), CPU(CPU),
        relaxedCnt(0), MCII(T.createMCInstrInfo()), RelaxTarget(new MCInst *),
        Extender(nullptr), MaxPacketSize(HexagonMCInstrInfo::packetSize(CPU)) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createHexagonELFObjectWriter(OSABI, CPU);
  }

  void setExtender(MCContext &Context) const {
    if (Extender == nullptr)
      const_cast<HexagonAsmBackend *>(this)->Extender = Context.createMCInst();
  }

  MCInst *takeExtender() const {
    assert(Extender != nullptr);
    MCInst * Result = Extender;
    const_cast<HexagonAsmBackend *>(this)->Extender = nullptr;
    return Result;
  }

  unsigned getNumFixupKinds() const override {
    return Hexagon::NumTargetFixupKinds;
  }

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override {
    const static MCFixupKindInfo Infos[Hexagon::NumTargetFixupKinds] = {
      // This table *must* be in same the order of fixup_* kinds in
      // HexagonFixupKinds.h.
      //
      // namei                          offset  bits    flags
      { "fixup_Hexagon_B22_PCREL",      0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_B15_PCREL",      0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_B7_PCREL",       0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_LO16",           0,      32,     0 },
      { "fixup_Hexagon_HI16",           0,      32,     0 },
      { "fixup_Hexagon_32",             0,      32,     0 },
      { "fixup_Hexagon_16",             0,      32,     0 },
      { "fixup_Hexagon_8",              0,      32,     0 },
      { "fixup_Hexagon_GPREL16_0",      0,      32,     0 },
      { "fixup_Hexagon_GPREL16_1",      0,      32,     0 },
      { "fixup_Hexagon_GPREL16_2",      0,      32,     0 },
      { "fixup_Hexagon_GPREL16_3",      0,      32,     0 },
      { "fixup_Hexagon_HL16",           0,      32,     0 },
      { "fixup_Hexagon_B13_PCREL",      0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_B9_PCREL",       0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_B32_PCREL_X",    0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_32_6_X",         0,      32,     0 },
      { "fixup_Hexagon_B22_PCREL_X",    0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_B15_PCREL_X",    0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_B13_PCREL_X",    0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_B9_PCREL_X",     0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_B7_PCREL_X",     0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_16_X",           0,      32,     0 },
      { "fixup_Hexagon_12_X",           0,      32,     0 },
      { "fixup_Hexagon_11_X",           0,      32,     0 },
      { "fixup_Hexagon_10_X",           0,      32,     0 },
      { "fixup_Hexagon_9_X",            0,      32,     0 },
      { "fixup_Hexagon_8_X",            0,      32,     0 },
      { "fixup_Hexagon_7_X",            0,      32,     0 },
      { "fixup_Hexagon_6_X",            0,      32,     0 },
      { "fixup_Hexagon_32_PCREL",       0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_COPY",           0,      32,     0 },
      { "fixup_Hexagon_GLOB_DAT",       0,      32,     0 },
      { "fixup_Hexagon_JMP_SLOT",       0,      32,     0 },
      { "fixup_Hexagon_RELATIVE",       0,      32,     0 },
      { "fixup_Hexagon_PLT_B22_PCREL",  0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_GOTREL_LO16",    0,      32,     0 },
      { "fixup_Hexagon_GOTREL_HI16",    0,      32,     0 },
      { "fixup_Hexagon_GOTREL_32",      0,      32,     0 },
      { "fixup_Hexagon_GOT_LO16",       0,      32,     0 },
      { "fixup_Hexagon_GOT_HI16",       0,      32,     0 },
      { "fixup_Hexagon_GOT_32",         0,      32,     0 },
      { "fixup_Hexagon_GOT_16",         0,      32,     0 },
      { "fixup_Hexagon_DTPMOD_32",      0,      32,     0 },
      { "fixup_Hexagon_DTPREL_LO16",    0,      32,     0 },
      { "fixup_Hexagon_DTPREL_HI16",    0,      32,     0 },
      { "fixup_Hexagon_DTPREL_32",      0,      32,     0 },
      { "fixup_Hexagon_DTPREL_16",      0,      32,     0 },
      { "fixup_Hexagon_GD_PLT_B22_PCREL",0,     32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_LD_PLT_B22_PCREL",0,     32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_GD_GOT_LO16",    0,      32,     0 },
      { "fixup_Hexagon_GD_GOT_HI16",    0,      32,     0 },
      { "fixup_Hexagon_GD_GOT_32",      0,      32,     0 },
      { "fixup_Hexagon_GD_GOT_16",      0,      32,     0 },
      { "fixup_Hexagon_LD_GOT_LO16",    0,      32,     0 },
      { "fixup_Hexagon_LD_GOT_HI16",    0,      32,     0 },
      { "fixup_Hexagon_LD_GOT_32",      0,      32,     0 },
      { "fixup_Hexagon_LD_GOT_16",      0,      32,     0 },
      { "fixup_Hexagon_IE_LO16",        0,      32,     0 },
      { "fixup_Hexagon_IE_HI16",        0,      32,     0 },
      { "fixup_Hexagon_IE_32",          0,      32,     0 },
      { "fixup_Hexagon_IE_16",          0,      32,     0 },
      { "fixup_Hexagon_IE_GOT_LO16",    0,      32,     0 },
      { "fixup_Hexagon_IE_GOT_HI16",    0,      32,     0 },
      { "fixup_Hexagon_IE_GOT_32",      0,      32,     0 },
      { "fixup_Hexagon_IE_GOT_16",      0,      32,     0 },
      { "fixup_Hexagon_TPREL_LO16",     0,      32,     0 },
      { "fixup_Hexagon_TPREL_HI16",     0,      32,     0 },
      { "fixup_Hexagon_TPREL_32",       0,      32,     0 },
      { "fixup_Hexagon_TPREL_16",       0,      32,     0 },
      { "fixup_Hexagon_6_PCREL_X",      0,      32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_GOTREL_32_6_X",  0,      32,     0 },
      { "fixup_Hexagon_GOTREL_16_X",    0,      32,     0 },
      { "fixup_Hexagon_GOTREL_11_X",    0,      32,     0 },
      { "fixup_Hexagon_GOT_32_6_X",     0,      32,     0 },
      { "fixup_Hexagon_GOT_16_X",       0,      32,     0 },
      { "fixup_Hexagon_GOT_11_X",       0,      32,     0 },
      { "fixup_Hexagon_DTPREL_32_6_X",  0,      32,     0 },
      { "fixup_Hexagon_DTPREL_16_X",    0,      32,     0 },
      { "fixup_Hexagon_DTPREL_11_X",    0,      32,     0 },
      { "fixup_Hexagon_GD_GOT_32_6_X",  0,      32,     0 },
      { "fixup_Hexagon_GD_GOT_16_X",    0,      32,     0 },
      { "fixup_Hexagon_GD_GOT_11_X",    0,      32,     0 },
      { "fixup_Hexagon_LD_GOT_32_6_X",  0,      32,     0 },
      { "fixup_Hexagon_LD_GOT_16_X",    0,      32,     0 },
      { "fixup_Hexagon_LD_GOT_11_X",    0,      32,     0 },
      { "fixup_Hexagon_IE_32_6_X",      0,      32,     0 },
      { "fixup_Hexagon_IE_16_X",        0,      32,     0 },
      { "fixup_Hexagon_IE_GOT_32_6_X",  0,      32,     0 },
      { "fixup_Hexagon_IE_GOT_16_X",    0,      32,     0 },
      { "fixup_Hexagon_IE_GOT_11_X",    0,      32,     0 },
      { "fixup_Hexagon_TPREL_32_6_X",   0,      32,     0 },
      { "fixup_Hexagon_TPREL_16_X",     0,      32,     0 },
      { "fixup_Hexagon_TPREL_11_X",     0,      32,     0 },
      { "fixup_Hexagon_GD_PLT_B22_PCREL_X",0,     32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_GD_PLT_B32_PCREL_X",0,     32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_LD_PLT_B22_PCREL_X",0,     32,     MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Hexagon_LD_PLT_B32_PCREL_X",0,     32,     MCFixupKindInfo::FKF_IsPCRel }
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
    switch(Fixup.getTargetKind()) {
      default:
        llvm_unreachable("Unknown Fixup Kind!");

      case fixup_Hexagon_LO16:
      case fixup_Hexagon_HI16:
      case fixup_Hexagon_16:
      case fixup_Hexagon_8:
      case fixup_Hexagon_GPREL16_0:
      case fixup_Hexagon_GPREL16_1:
      case fixup_Hexagon_GPREL16_2:
      case fixup_Hexagon_GPREL16_3:
      case fixup_Hexagon_HL16:
      case fixup_Hexagon_32_6_X:
      case fixup_Hexagon_16_X:
      case fixup_Hexagon_12_X:
      case fixup_Hexagon_11_X:
      case fixup_Hexagon_10_X:
      case fixup_Hexagon_9_X:
      case fixup_Hexagon_8_X:
      case fixup_Hexagon_7_X:
      case fixup_Hexagon_6_X:
      case fixup_Hexagon_COPY:
      case fixup_Hexagon_GLOB_DAT:
      case fixup_Hexagon_JMP_SLOT:
      case fixup_Hexagon_RELATIVE:
      case fixup_Hexagon_PLT_B22_PCREL:
      case fixup_Hexagon_GOTREL_LO16:
      case fixup_Hexagon_GOTREL_HI16:
      case fixup_Hexagon_GOTREL_32:
      case fixup_Hexagon_GOT_LO16:
      case fixup_Hexagon_GOT_HI16:
      case fixup_Hexagon_GOT_32:
      case fixup_Hexagon_GOT_16:
      case fixup_Hexagon_DTPMOD_32:
      case fixup_Hexagon_DTPREL_LO16:
      case fixup_Hexagon_DTPREL_HI16:
      case fixup_Hexagon_DTPREL_32:
      case fixup_Hexagon_DTPREL_16:
      case fixup_Hexagon_GD_PLT_B22_PCREL:
      case fixup_Hexagon_LD_PLT_B22_PCREL:
      case fixup_Hexagon_GD_GOT_LO16:
      case fixup_Hexagon_GD_GOT_HI16:
      case fixup_Hexagon_GD_GOT_32:
      case fixup_Hexagon_GD_GOT_16:
      case fixup_Hexagon_LD_GOT_LO16:
      case fixup_Hexagon_LD_GOT_HI16:
      case fixup_Hexagon_LD_GOT_32:
      case fixup_Hexagon_LD_GOT_16:
      case fixup_Hexagon_IE_LO16:
      case fixup_Hexagon_IE_HI16:
      case fixup_Hexagon_IE_32:
      case fixup_Hexagon_IE_16:
      case fixup_Hexagon_IE_GOT_LO16:
      case fixup_Hexagon_IE_GOT_HI16:
      case fixup_Hexagon_IE_GOT_32:
      case fixup_Hexagon_IE_GOT_16:
      case fixup_Hexagon_TPREL_LO16:
      case fixup_Hexagon_TPREL_HI16:
      case fixup_Hexagon_TPREL_32:
      case fixup_Hexagon_TPREL_16:
      case fixup_Hexagon_GOTREL_32_6_X:
      case fixup_Hexagon_GOTREL_16_X:
      case fixup_Hexagon_GOTREL_11_X:
      case fixup_Hexagon_GOT_32_6_X:
      case fixup_Hexagon_GOT_16_X:
      case fixup_Hexagon_GOT_11_X:
      case fixup_Hexagon_DTPREL_32_6_X:
      case fixup_Hexagon_DTPREL_16_X:
      case fixup_Hexagon_DTPREL_11_X:
      case fixup_Hexagon_GD_GOT_32_6_X:
      case fixup_Hexagon_GD_GOT_16_X:
      case fixup_Hexagon_GD_GOT_11_X:
      case fixup_Hexagon_LD_GOT_32_6_X:
      case fixup_Hexagon_LD_GOT_16_X:
      case fixup_Hexagon_LD_GOT_11_X:
      case fixup_Hexagon_IE_32_6_X:
      case fixup_Hexagon_IE_16_X:
      case fixup_Hexagon_IE_GOT_32_6_X:
      case fixup_Hexagon_IE_GOT_16_X:
      case fixup_Hexagon_IE_GOT_11_X:
      case fixup_Hexagon_TPREL_32_6_X:
      case fixup_Hexagon_TPREL_16_X:
      case fixup_Hexagon_TPREL_11_X:
      case fixup_Hexagon_32_PCREL:
      case fixup_Hexagon_6_PCREL_X:
      case fixup_Hexagon_23_REG:
      case fixup_Hexagon_27_REG:
      case fixup_Hexagon_GD_PLT_B22_PCREL_X:
      case fixup_Hexagon_GD_PLT_B32_PCREL_X:
      case fixup_Hexagon_LD_PLT_B22_PCREL_X:
      case fixup_Hexagon_LD_PLT_B32_PCREL_X:
        // These relocations should always have a relocation recorded
        return true;

      case fixup_Hexagon_B22_PCREL:
        //IsResolved = false;
        break;

      case fixup_Hexagon_B13_PCREL:
      case fixup_Hexagon_B13_PCREL_X:
      case fixup_Hexagon_B32_PCREL_X:
      case fixup_Hexagon_B22_PCREL_X:
      case fixup_Hexagon_B15_PCREL:
      case fixup_Hexagon_B15_PCREL_X:
      case fixup_Hexagon_B9_PCREL:
      case fixup_Hexagon_B9_PCREL_X:
      case fixup_Hexagon_B7_PCREL:
      case fixup_Hexagon_B7_PCREL_X:
        if (DisableFixup)
          return true;
        break;

      case FK_Data_1:
      case FK_Data_2:
      case FK_Data_4:
      case FK_PCRel_4:
      case fixup_Hexagon_32:
        // Leave these relocations alone as they are used for EH.
        return false;
    }
    return false;
  }

  /// getFixupKindNumBytes - The number of bytes the fixup may change.
  static unsigned getFixupKindNumBytes(unsigned Kind) {
    switch (Kind) {
    default:
        return 0;

      case FK_Data_1:
        return 1;
      case FK_Data_2:
        return 2;
      case FK_Data_4:         // this later gets mapped to R_HEX_32
      case FK_PCRel_4:        // this later gets mapped to R_HEX_32_PCREL
      case fixup_Hexagon_32:
      case fixup_Hexagon_B32_PCREL_X:
      case fixup_Hexagon_B22_PCREL:
      case fixup_Hexagon_B22_PCREL_X:
      case fixup_Hexagon_B15_PCREL:
      case fixup_Hexagon_B15_PCREL_X:
      case fixup_Hexagon_B13_PCREL:
      case fixup_Hexagon_B13_PCREL_X:
      case fixup_Hexagon_B9_PCREL:
      case fixup_Hexagon_B9_PCREL_X:
      case fixup_Hexagon_B7_PCREL:
      case fixup_Hexagon_B7_PCREL_X:
      case fixup_Hexagon_GD_PLT_B32_PCREL_X:
      case fixup_Hexagon_LD_PLT_B32_PCREL_X:
        return 4;
    }
  }

  // Make up for left shift when encoding the operand.
  static uint64_t adjustFixupValue(MCFixupKind Kind, uint64_t Value) {
    switch((unsigned)Kind) {
      default:
        break;

      case fixup_Hexagon_B7_PCREL:
      case fixup_Hexagon_B9_PCREL:
      case fixup_Hexagon_B13_PCREL:
      case fixup_Hexagon_B15_PCREL:
      case fixup_Hexagon_B22_PCREL:
        Value >>= 2;
        break;

      case fixup_Hexagon_B7_PCREL_X:
      case fixup_Hexagon_B9_PCREL_X:
      case fixup_Hexagon_B13_PCREL_X:
      case fixup_Hexagon_B15_PCREL_X:
      case fixup_Hexagon_B22_PCREL_X:
        Value &= 0x3f;
        break;

      case fixup_Hexagon_B32_PCREL_X:
      case fixup_Hexagon_GD_PLT_B32_PCREL_X:
      case fixup_Hexagon_LD_PLT_B32_PCREL_X:
        Value >>= 6;
        break;
    }
    return (Value);
  }

  void HandleFixupError(const int bits, const int align_bits,
    const int64_t FixupValue, const char *fixupStr) const {
    // Error: value 1124 out of range: -1024-1023 when resolving
    // symbol in file xprtsock.S
    const APInt IntMin = APInt::getSignedMinValue(bits+align_bits);
    const APInt IntMax = APInt::getSignedMaxValue(bits+align_bits);
    std::stringstream errStr;
    errStr << "\nError: value " <<
      FixupValue <<
      " out of range: " <<
      IntMin.getSExtValue() <<
      "-" <<
      IntMax.getSExtValue() <<
      " when resolving " <<
      fixupStr <<
      " fixup\n";
    llvm_unreachable(errStr.str().c_str());
  }

  /// ApplyFixup - Apply the \arg Value for given \arg Fixup into the provided
  /// data fragment, at the offset specified by the fixup and following the
  /// fixup kind as appropriate.
  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t FixupValue, bool IsResolved,
                  const MCSubtargetInfo *STI) const override {

    // When FixupValue is 0 the relocation is external and there
    // is nothing for us to do.
    if (!FixupValue) return;

    MCFixupKind Kind = Fixup.getKind();
    uint64_t Value;
    uint32_t InstMask;
    uint32_t Reloc;

    // LLVM gives us an encoded value, we have to convert it back
    // to a real offset before we can use it.
    uint32_t Offset = Fixup.getOffset();
    unsigned NumBytes = getFixupKindNumBytes(Kind);
    assert(Offset + NumBytes <= Data.size() && "Invalid fixup offset!");
    char *InstAddr = Data.data() + Offset;

    Value = adjustFixupValue(Kind, FixupValue);
    if(!Value)
      return;
    int sValue = (int)Value;

    switch((unsigned)Kind) {
      default:
        return;

      case fixup_Hexagon_B7_PCREL:
        if (!(isIntN(7, sValue)))
          HandleFixupError(7, 2, (int64_t)FixupValue, "B7_PCREL");
        [[fallthrough]];
      case fixup_Hexagon_B7_PCREL_X:
        InstMask = 0x00001f18;  // Word32_B7
        Reloc = (((Value >> 2) & 0x1f) << 8) |    // Value 6-2 = Target 12-8
                ((Value & 0x3) << 3);             // Value 1-0 = Target 4-3
        break;

      case fixup_Hexagon_B9_PCREL:
        if (!(isIntN(9, sValue)))
          HandleFixupError(9, 2, (int64_t)FixupValue, "B9_PCREL");
        [[fallthrough]];
      case fixup_Hexagon_B9_PCREL_X:
        InstMask = 0x003000fe;  // Word32_B9
        Reloc = (((Value >> 7) & 0x3) << 20) |    // Value 8-7 = Target 21-20
                ((Value & 0x7f) << 1);            // Value 6-0 = Target 7-1
        break;

        // Since the existing branches that use this relocation cannot be
        // extended, they should only be fixed up if the target is within range.
      case fixup_Hexagon_B13_PCREL:
        if (!(isIntN(13, sValue)))
          HandleFixupError(13, 2, (int64_t)FixupValue, "B13_PCREL");
        [[fallthrough]];
      case fixup_Hexagon_B13_PCREL_X:
        InstMask = 0x00202ffe;  // Word32_B13
        Reloc = (((Value >> 12) & 0x1) << 21) |    // Value 12   = Target 21
                (((Value >> 11) & 0x1) << 13) |    // Value 11   = Target 13
                ((Value & 0x7ff) << 1);            // Value 10-0 = Target 11-1
        break;

      case fixup_Hexagon_B15_PCREL:
        if (!(isIntN(15, sValue)))
          HandleFixupError(15, 2, (int64_t)FixupValue, "B15_PCREL");
        [[fallthrough]];
      case fixup_Hexagon_B15_PCREL_X:
        InstMask = 0x00df20fe;  // Word32_B15
        Reloc = (((Value >> 13) & 0x3) << 22) |    // Value 14-13 = Target 23-22
                (((Value >> 8) & 0x1f) << 16) |    // Value 12-8  = Target 20-16
                (((Value >> 7) & 0x1)  << 13) |    // Value 7     = Target 13
                ((Value & 0x7f) << 1);             // Value 6-0   = Target 7-1
        break;

      case fixup_Hexagon_B22_PCREL:
        if (!(isIntN(22, sValue)))
          HandleFixupError(22, 2, (int64_t)FixupValue, "B22_PCREL");
        [[fallthrough]];
      case fixup_Hexagon_B22_PCREL_X:
        InstMask = 0x01ff3ffe;  // Word32_B22
        Reloc = (((Value >> 13) & 0x1ff) << 16) |  // Value 21-13 = Target 24-16
                ((Value & 0x1fff) << 1);           // Value 12-0  = Target 13-1
        break;

      case fixup_Hexagon_B32_PCREL_X:
        InstMask = 0x0fff3fff;  // Word32_X26
        Reloc = (((Value >> 14) & 0xfff) << 16) |  // Value 25-14 = Target 27-16
                (Value & 0x3fff);                  // Value 13-0  = Target 13-0
        break;

      case FK_Data_1:
      case FK_Data_2:
      case FK_Data_4:
      case fixup_Hexagon_32:
        InstMask = 0xffffffff;  // Word32
        Reloc = Value;
        break;
    }

    LLVM_DEBUG(dbgs() << "Name=" << getFixupKindInfo(Kind).Name << "("
                      << (unsigned)Kind << ")\n");
    LLVM_DEBUG(
        uint32_t OldData = 0; for (unsigned i = 0; i < NumBytes; i++) OldData |=
                              (InstAddr[i] << (i * 8)) & (0xff << (i * 8));
        dbgs() << "\tBValue=0x"; dbgs().write_hex(Value) << ": AValue=0x";
        dbgs().write_hex(FixupValue)
        << ": Offset=" << Offset << ": Size=" << Data.size() << ": OInst=0x";
        dbgs().write_hex(OldData) << ": Reloc=0x"; dbgs().write_hex(Reloc););

    // For each byte of the fragment that the fixup touches, mask in the
    // bits from the fixup value. The Value has been "split up" into the
    // appropriate bitfields above.
    for (unsigned i = 0; i < NumBytes; i++){
      InstAddr[i] &= uint8_t(~InstMask >> (i * 8)) & 0xff; // Clear reloc bits
      InstAddr[i] |= uint8_t(Reloc >> (i * 8)) & 0xff;     // Apply new reloc
    }

    LLVM_DEBUG(uint32_t NewData = 0;
               for (unsigned i = 0; i < NumBytes; i++) NewData |=
               (InstAddr[i] << (i * 8)) & (0xff << (i * 8));
               dbgs() << ": NInst=0x"; dbgs().write_hex(NewData) << "\n";);
  }

  bool isInstRelaxable(MCInst const &HMI) const {
    const MCInstrDesc &MCID = HexagonMCInstrInfo::getDesc(*MCII, HMI);
    bool Relaxable = false;
    // Branches and loop-setup insns are handled as necessary by relaxation.
    if (llvm::HexagonMCInstrInfo::getType(*MCII, HMI) == HexagonII::TypeJ ||
        (llvm::HexagonMCInstrInfo::getType(*MCII, HMI) == HexagonII::TypeCJ &&
         MCID.isBranch()) ||
        (llvm::HexagonMCInstrInfo::getType(*MCII, HMI) == HexagonII::TypeNCJ &&
         MCID.isBranch()) ||
        (llvm::HexagonMCInstrInfo::getType(*MCII, HMI) == HexagonII::TypeCR &&
         HMI.getOpcode() != Hexagon::C4_addipc))
      if (HexagonMCInstrInfo::isExtendable(*MCII, HMI)) {
        Relaxable = true;
        MCOperand const &Operand =
            HMI.getOperand(HexagonMCInstrInfo::getExtendableOp(*MCII, HMI));
        if (HexagonMCInstrInfo::mustNotExtend(*Operand.getExpr()))
          Relaxable = false;
      }

    return Relaxable;
  }

  /// MayNeedRelaxation - Check whether the given instruction may need
  /// relaxation.
  ///
  /// \param Inst - The instruction to test.
  bool mayNeedRelaxation(MCInst const &Inst,
                         const MCSubtargetInfo &STI) const override {
    return true;
  }

  /// fixupNeedsRelaxation - Target specific predicate for whether a given
  /// fixup requires the associated instruction to be relaxed.
  bool fixupNeedsRelaxationAdvanced(const MCAssembler &Asm,
                                    const MCFixup &Fixup, bool Resolved,
                                    uint64_t Value,
                                    const MCRelaxableFragment *DF,
                                    const bool WasForced) const override {
    MCInst const &MCB = DF->getInst();
    assert(HexagonMCInstrInfo::isBundle(MCB));

    *RelaxTarget = nullptr;
    MCInst &MCI = const_cast<MCInst &>(HexagonMCInstrInfo::instruction(
        MCB, Fixup.getOffset() / HEXAGON_INSTR_SIZE));
    bool Relaxable = isInstRelaxable(MCI);
    if (Relaxable == false)
      return false;
    // If we cannot resolve the fixup value, it requires relaxation.
    if (!Resolved) {
      switch (Fixup.getTargetKind()) {
      case fixup_Hexagon_B22_PCREL:
        // GetFixupCount assumes B22 won't relax
        [[fallthrough]];
      default:
        return false;
        break;
      case fixup_Hexagon_B13_PCREL:
      case fixup_Hexagon_B15_PCREL:
      case fixup_Hexagon_B9_PCREL:
      case fixup_Hexagon_B7_PCREL: {
        if (HexagonMCInstrInfo::bundleSize(MCB) < HEXAGON_PACKET_SIZE) {
          ++relaxedCnt;
          *RelaxTarget = &MCI;
          setExtender(Asm.getContext());
          return true;
        } else {
          return false;
        }
        break;
      }
      }
    }

    MCFixupKind Kind = Fixup.getKind();
    int64_t sValue = Value;
    int64_t maxValue;

    switch ((unsigned)Kind) {
    case fixup_Hexagon_B7_PCREL:
      maxValue = 1 << 8;
      break;
    case fixup_Hexagon_B9_PCREL:
      maxValue = 1 << 10;
      break;
    case fixup_Hexagon_B15_PCREL:
      maxValue = 1 << 16;
      break;
    case fixup_Hexagon_B22_PCREL:
      maxValue = 1 << 23;
      break;
    default:
      maxValue = INT64_MAX;
      break;
    }

    bool isFarAway = -maxValue > sValue || sValue > maxValue - 1;

    if (isFarAway) {
      if (HexagonMCInstrInfo::bundleSize(MCB) < HEXAGON_PACKET_SIZE) {
        ++relaxedCnt;
        *RelaxTarget = &MCI;
        setExtender(Asm.getContext());
        return true;
      }
    }

    return false;
  }

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {
    assert(HexagonMCInstrInfo::isBundle(Inst) &&
           "Hexagon relaxInstruction only works on bundles");

    MCInst Res;
    Res.setOpcode(Hexagon::BUNDLE);
    Res.addOperand(MCOperand::createImm(Inst.getOperand(0).getImm()));
    // Copy the results into the bundle.
    bool Update = false;
    for (auto &I : HexagonMCInstrInfo::bundleInstructions(Inst)) {
      MCInst &CrntHMI = const_cast<MCInst &>(*I.getInst());

      // if immediate extender needed, add it in
      if (*RelaxTarget == &CrntHMI) {
        Update = true;
        assert((HexagonMCInstrInfo::bundleSize(Res) < HEXAGON_PACKET_SIZE) &&
               "No room to insert extender for relaxation");

        MCInst *HMIx = takeExtender();
        *HMIx = HexagonMCInstrInfo::deriveExtender(
                *MCII, CrntHMI,
                HexagonMCInstrInfo::getExtendableOperand(*MCII, CrntHMI));
        Res.addOperand(MCOperand::createInst(HMIx));
        *RelaxTarget = nullptr;
      }
      // now copy over the original instruction(the one we may have extended)
      Res.addOperand(MCOperand::createInst(I.getInst()));
    }

    Inst = std::move(Res);
    (void)Update;
    assert(Update && "Didn't find relaxation target");
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    static const uint32_t Nopcode = 0x7f000000, // Hard-coded NOP.
        ParseIn = 0x00004000,                   // In packet parse-bits.
        ParseEnd = 0x0000c000;                  // End of packet parse-bits.

    while (Count % HEXAGON_INSTR_SIZE) {
      LLVM_DEBUG(dbgs() << "Alignment not a multiple of the instruction size:"
                        << Count % HEXAGON_INSTR_SIZE << "/"
                        << HEXAGON_INSTR_SIZE << "\n");
      --Count;
      OS << '\0';
    }

    while (Count) {
      Count -= HEXAGON_INSTR_SIZE;
      // Close the packet whenever a multiple of the maximum packet size remains
      uint32_t ParseBits = (Count % (MaxPacketSize * HEXAGON_INSTR_SIZE)) ?
                           ParseIn : ParseEnd;
      support::endian::write<uint32_t>(OS, Nopcode | ParseBits, Endian);
    }
    return true;
  }

  void finishLayout(MCAssembler const &Asm) const override {
    SmallVector<MCFragment *> Frags;
    for (MCSection &Sec : Asm) {
      Frags.clear();
      for (MCFragment &F : Sec)
        Frags.push_back(&F);
      for (size_t J = 0, E = Frags.size(); J != E; ++J) {
        switch (Frags[J]->getKind()) {
        default:
          break;
        case MCFragment::FT_Align: {
          auto Size = Asm.computeFragmentSize(*Frags[J]);
          for (auto K = J; K != 0 && Size >= HEXAGON_PACKET_SIZE;) {
            --K;
            switch (Frags[K]->getKind()) {
            default:
              break;
            case MCFragment::FT_Align: {
              // Don't pad before other alignments
              Size = 0;
              break;
            }
            case MCFragment::FT_Relaxable: {
              MCContext &Context = Asm.getContext();
              auto &RF = cast<MCRelaxableFragment>(*Frags[K]);
              auto &Inst = const_cast<MCInst &>(RF.getInst());
              while (Size > 0 &&
                     HexagonMCInstrInfo::bundleSize(Inst) < MaxPacketSize) {
                MCInst *Nop = Context.createMCInst();
                Nop->setOpcode(Hexagon::A2_nop);
                Inst.addOperand(MCOperand::createInst(Nop));
                Size -= 4;
                if (!HexagonMCChecker(
                         Context, *MCII, *RF.getSubtargetInfo(), Inst,
                         *Context.getRegisterInfo(), false)
                         .check()) {
                  Inst.erase(Inst.end() - 1);
                  Size = 0;
                }
              }
              bool Error = HexagonMCShuffle(Context, true, *MCII,
                                            *RF.getSubtargetInfo(), Inst);
              //assert(!Error);
              (void)Error;
              ReplaceInstruction(Asm.getEmitter(), RF, Inst);
              Sec.setHasLayout(false);
              Size = 0; // Only look back one instruction
              break;
            }
            }
          }
        }
        }
      }
    }
  }
}; // class HexagonAsmBackend

} // namespace

// MCAsmBackend
MCAsmBackend *llvm::createHexagonAsmBackend(Target const &T,
                                            const MCSubtargetInfo &STI,
                                            MCRegisterInfo const & /*MRI*/,
                                            const MCTargetOptions &Options) {
  const Triple &TT = STI.getTargetTriple();
  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TT.getOS());

  StringRef CPUString = Hexagon_MC::selectHexagonCPU(STI.getCPU());
  return new HexagonAsmBackend(T, TT, OSABI, CPUString);
}
