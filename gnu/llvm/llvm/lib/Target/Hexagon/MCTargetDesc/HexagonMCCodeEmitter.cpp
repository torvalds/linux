//===- HexagonMCCodeEmitter.cpp - Hexagon Target Descriptions -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonMCCodeEmitter.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonFixupKinds.h"
#include "MCTargetDesc/HexagonMCExpr.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#define DEBUG_TYPE "mccodeemitter"

using namespace llvm;
using namespace Hexagon;

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");

static const unsigned fixup_Invalid = ~0u;

#define _ fixup_Invalid
#define P(x) Hexagon::fixup_Hexagon##x
static const std::map<unsigned, std::vector<unsigned>> ExtFixups = {
  { MCSymbolRefExpr::VK_DTPREL,
    { _,                _,              _,                      _,
      _,                _,              P(_DTPREL_16_X),        P(_DTPREL_11_X),
      P(_DTPREL_11_X),  P(_9_X),        _,                      P(_DTPREL_11_X),
      P(_DTPREL_16_X),  _,              _,                      _,
      P(_DTPREL_16_X),  _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_DTPREL_32_6_X) }},
  { MCSymbolRefExpr::VK_GOT,
    { _,                _,              _,                      _,
      _,                _,              P(_GOT_11_X),           _ /* [1] */,
      _ /* [1] */,      P(_9_X),        _,                      P(_GOT_11_X),
      P(_GOT_16_X),     _,              _,                      _,
      P(_GOT_16_X),     _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_GOT_32_6_X)    }},
  { MCSymbolRefExpr::VK_GOTREL,
    { _,                _,              _,                      _,
      _,                _,              P(_GOTREL_11_X),        P(_GOTREL_11_X),
      P(_GOTREL_11_X),  P(_9_X),        _,                      P(_GOTREL_11_X),
      P(_GOTREL_16_X),  _,              _,                      _,
      P(_GOTREL_16_X),  _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_GOTREL_32_6_X) }},
  { MCSymbolRefExpr::VK_TPREL,
    { _,                _,              _,                      _,
      _,                _,              P(_TPREL_16_X),         P(_TPREL_11_X),
      P(_TPREL_11_X),   P(_9_X),        _,                      P(_TPREL_11_X),
      P(_TPREL_16_X),   _,              _,                      _,
      P(_TPREL_16_X),   _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_TPREL_32_6_X)  }},
  { MCSymbolRefExpr::VK_Hexagon_GD_GOT,
    { _,                _,              _,                      _,
      _,                _,              P(_GD_GOT_16_X),        P(_GD_GOT_11_X),
      P(_GD_GOT_11_X),  P(_9_X),        _,                      P(_GD_GOT_11_X),
      P(_GD_GOT_16_X),  _,              _,                      _,
      P(_GD_GOT_16_X),  _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_GD_GOT_32_6_X) }},
  { MCSymbolRefExpr::VK_Hexagon_GD_PLT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                P(_9_X),        _,                      P(_GD_PLT_B22_PCREL_X),
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              P(_GD_PLT_B22_PCREL_X), _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _                 }},
  { MCSymbolRefExpr::VK_Hexagon_IE,
    { _,                _,              _,                      _,
      _,                _,              P(_IE_16_X),            _,
      _,                P(_9_X),        _,                      _,
      P(_IE_16_X),      _,              _,                      _,
      P(_IE_16_X),      _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_IE_32_6_X)     }},
  { MCSymbolRefExpr::VK_Hexagon_IE_GOT,
    { _,                _,              _,                      _,
      _,                _,              P(_IE_GOT_11_X),        P(_IE_GOT_11_X),
      P(_IE_GOT_11_X),  P(_9_X),        _,                      P(_IE_GOT_11_X),
      P(_IE_GOT_16_X),  _,              _,                      _,
      P(_IE_GOT_16_X),  _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_IE_GOT_32_6_X) }},
  { MCSymbolRefExpr::VK_Hexagon_LD_GOT,
    { _,                _,              _,                      _,
      _,                _,              P(_LD_GOT_11_X),        P(_LD_GOT_11_X),
      P(_LD_GOT_11_X),  P(_9_X),        _,                      P(_LD_GOT_11_X),
      P(_LD_GOT_16_X),  _,              _,                      _,
      P(_LD_GOT_16_X),  _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_LD_GOT_32_6_X) }},
  { MCSymbolRefExpr::VK_Hexagon_LD_PLT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                P(_9_X),        _,                      P(_LD_PLT_B22_PCREL_X),
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              P(_LD_PLT_B22_PCREL_X), _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _                 }},
  { MCSymbolRefExpr::VK_PCREL,
    { _,                _,              _,                      _,
      _,                _,              P(_6_PCREL_X),          _,
      _,                P(_9_X),        _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_32_PCREL)      }},
  { MCSymbolRefExpr::VK_None,
    { _,                _,              _,                      _,
      _,                _,              P(_6_X),                P(_8_X),
      P(_8_X),          P(_9_X),        P(_10_X),               P(_11_X),
      P(_12_X),         P(_B13_PCREL),  _,                      P(_B15_PCREL_X),
      P(_16_X),         _,              _,                      _,
      _,                _,              P(_B22_PCREL_X),        _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_32_6_X)        }},
};
// [1] The fixup is GOT_16_X for signed values and GOT_11_X for unsigned.

static const std::map<unsigned, std::vector<unsigned>> StdFixups = {
  { MCSymbolRefExpr::VK_DTPREL,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_DTPREL_16),    _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_DTPREL_32)     }},
  { MCSymbolRefExpr::VK_GOT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_GOT_32)        }},
  { MCSymbolRefExpr::VK_GOTREL,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _ /* [2] */,      _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_GOTREL_32)     }},
  { MCSymbolRefExpr::VK_PLT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              P(_PLT_B22_PCREL),      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _                 }},
  { MCSymbolRefExpr::VK_TPREL,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      P(_TPREL_11_X),
      _,                _,              _,                      _,
      P(_TPREL_16),     _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_TPREL_32)      }},
  { MCSymbolRefExpr::VK_Hexagon_GD_GOT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_GD_GOT_16),    _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_GD_GOT_32)     }},
  { MCSymbolRefExpr::VK_Hexagon_GD_PLT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              P(_GD_PLT_B22_PCREL),   _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _                 }},
  { MCSymbolRefExpr::VK_Hexagon_GPREL,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_GPREL16_0),    _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _                 }},
  { MCSymbolRefExpr::VK_Hexagon_HI16,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_HI16),         _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _                 }},
  { MCSymbolRefExpr::VK_Hexagon_IE,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_IE_32)         }},
  { MCSymbolRefExpr::VK_Hexagon_IE_GOT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_IE_GOT_16),    _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_IE_GOT_32)     }},
  { MCSymbolRefExpr::VK_Hexagon_LD_GOT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_LD_GOT_16),    _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_LD_GOT_32)     }},
  { MCSymbolRefExpr::VK_Hexagon_LD_PLT,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              P(_LD_PLT_B22_PCREL),   _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _                 }},
  { MCSymbolRefExpr::VK_Hexagon_LO16,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_LO16),         _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _                 }},
  { MCSymbolRefExpr::VK_PCREL,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_32_PCREL)      }},
  { MCSymbolRefExpr::VK_None,
    { _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      _,                P(_B13_PCREL),  _,                      P(_B15_PCREL),
      _,                _,              _,                      _,
      _,                _,              P(_B22_PCREL),          _,
      _,                _,              _,                      _,
      _,                _,              _,                      _,
      P(_32)            }},
};
//
// [2] The actual fixup is LO16 or HI16, depending on the instruction.
#undef P
#undef _

uint32_t HexagonMCCodeEmitter::parseBits(size_t Last, MCInst const &MCB,
                                         MCInst const &MCI) const {
  bool Duplex = HexagonMCInstrInfo::isDuplex(MCII, MCI);
  if (State.Index == 0) {
    if (HexagonMCInstrInfo::isInnerLoop(MCB)) {
      assert(!Duplex);
      assert(State.Index != Last);
      return HexagonII::INST_PARSE_LOOP_END;
    }
  }
  if (State.Index == 1) {
    if (HexagonMCInstrInfo::isOuterLoop(MCB)) {
      assert(!Duplex);
      assert(State.Index != Last);
      return HexagonII::INST_PARSE_LOOP_END;
    }
  }
  if (Duplex) {
    assert(State.Index == Last);
    return HexagonII::INST_PARSE_DUPLEX;
  }
  if (State.Index == Last)
    return HexagonII::INST_PARSE_PACKET_END;
  return HexagonII::INST_PARSE_NOT_END;
}

/// Emit the bundle.
void HexagonMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                             SmallVectorImpl<char> &CB,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  MCInst &HMB = const_cast<MCInst &>(MI);

  assert(HexagonMCInstrInfo::isBundle(HMB));
  LLVM_DEBUG(dbgs() << "Encoding bundle\n";);
  State.Addend = 0;
  State.Extended = false;
  State.Bundle = &MI;
  State.Index = 0;
  size_t Last = HexagonMCInstrInfo::bundleSize(HMB) - 1;

  for (auto &I : HexagonMCInstrInfo::bundleInstructions(HMB)) {
    MCInst &HMI = const_cast<MCInst &>(*I.getInst());

    encodeSingleInstruction(HMI, CB, Fixups, STI, parseBits(Last, HMB, HMI));
    State.Extended = HexagonMCInstrInfo::isImmext(HMI);
    State.Addend += HEXAGON_INSTR_SIZE;
    ++State.Index;
  }
}

static bool RegisterMatches(unsigned Consumer, unsigned Producer,
                            unsigned Producer2) {
  return (Consumer == Producer) || (Consumer == Producer2) ||
         HexagonMCInstrInfo::IsSingleConsumerRefPairProducer(Producer,
                                                             Consumer);
}

void HexagonMCCodeEmitter::encodeSingleInstruction(
    const MCInst &MI, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI,
    uint32_t Parse) const {
  assert(!HexagonMCInstrInfo::isBundle(MI));
  uint64_t Binary;

  // Pseudo instructions don't get encoded and shouldn't be here
  // in the first place!
  assert(!HexagonMCInstrInfo::getDesc(MCII, MI).isPseudo() &&
         "pseudo-instruction found");
  LLVM_DEBUG(dbgs() << "Encoding insn `"
                    << HexagonMCInstrInfo::getName(MCII, MI) << "'\n");

  Binary = getBinaryCodeForInstr(MI, Fixups, STI);
  unsigned Opc = MI.getOpcode();

  // Check for unimplemented instructions. Immediate extenders
  // are encoded as zero, so they need to be accounted for.
  if (!Binary && Opc != DuplexIClass0 && Opc != A4_ext) {
    LLVM_DEBUG(dbgs() << "Unimplemented inst `"
                      << HexagonMCInstrInfo::getName(MCII, MI) << "'\n");
    llvm_unreachable("Unimplemented Instruction");
  }
  Binary |= Parse;

  // if we need to emit a duplexed instruction
  if (Opc >= Hexagon::DuplexIClass0 && Opc <= Hexagon::DuplexIClassF) {
    assert(Parse == HexagonII::INST_PARSE_DUPLEX &&
           "Emitting duplex without duplex parse bits");
    unsigned DupIClass = MI.getOpcode() - Hexagon::DuplexIClass0;
    // 29 is the bit position.
    // 0b1110 =0xE bits are masked off and down shifted by 1 bit.
    // Last bit is moved to bit position 13
    Binary = ((DupIClass & 0xE) << (29 - 1)) | ((DupIClass & 0x1) << 13);

    const MCInst *Sub0 = MI.getOperand(0).getInst();
    const MCInst *Sub1 = MI.getOperand(1).getInst();

    // Get subinstruction slot 0.
    unsigned SubBits0 = getBinaryCodeForInstr(*Sub0, Fixups, STI);
    // Get subinstruction slot 1.
    State.SubInst1 = true;
    unsigned SubBits1 = getBinaryCodeForInstr(*Sub1, Fixups, STI);
    State.SubInst1 = false;

    Binary |= SubBits0 | (SubBits1 << 16);
  }
  support::endian::write<uint32_t>(CB, Binary, llvm::endianness::little);
  ++MCNumEmitted;
}

[[noreturn]] static void raise_relocation_error(unsigned Width, unsigned Kind) {
  std::string Text;
  raw_string_ostream Stream(Text);
  Stream << "Unrecognized relocation combination: width=" << Width
         << " kind=" << Kind;
  report_fatal_error(Twine(Stream.str()));
}

/// Some insns are not extended and thus have no bits. These cases require
/// a more brute force method for determining the correct relocation.
Hexagon::Fixups HexagonMCCodeEmitter::getFixupNoBits(
      MCInstrInfo const &MCII, const MCInst &MI, const MCOperand &MO,
      const MCSymbolRefExpr::VariantKind VarKind) const {
  const MCInstrDesc &MCID = HexagonMCInstrInfo::getDesc(MCII, MI);
  unsigned InsnType = HexagonMCInstrInfo::getType(MCII, MI);
  using namespace Hexagon;

  if (InsnType == HexagonII::TypeEXTENDER) {
    if (VarKind == MCSymbolRefExpr::VK_None) {
      auto Instrs = HexagonMCInstrInfo::bundleInstructions(*State.Bundle);
      for (auto I = Instrs.begin(), N = Instrs.end(); I != N; ++I) {
        if (I->getInst() != &MI)
          continue;
        assert(I+1 != N && "Extender cannot be last in packet");
        const MCInst &NextI = *(I+1)->getInst();
        const MCInstrDesc &NextD = HexagonMCInstrInfo::getDesc(MCII, NextI);
        if (NextD.isBranch() || NextD.isCall() ||
            HexagonMCInstrInfo::getType(MCII, NextI) == HexagonII::TypeCR)
          return fixup_Hexagon_B32_PCREL_X;
        return fixup_Hexagon_32_6_X;
      }
    }

    static const std::map<unsigned,unsigned> Relocs = {
      { MCSymbolRefExpr::VK_GOTREL,         fixup_Hexagon_GOTREL_32_6_X },
      { MCSymbolRefExpr::VK_GOT,            fixup_Hexagon_GOT_32_6_X },
      { MCSymbolRefExpr::VK_TPREL,          fixup_Hexagon_TPREL_32_6_X },
      { MCSymbolRefExpr::VK_DTPREL,         fixup_Hexagon_DTPREL_32_6_X },
      { MCSymbolRefExpr::VK_Hexagon_GD_GOT, fixup_Hexagon_GD_GOT_32_6_X },
      { MCSymbolRefExpr::VK_Hexagon_LD_GOT, fixup_Hexagon_LD_GOT_32_6_X },
      { MCSymbolRefExpr::VK_Hexagon_IE,     fixup_Hexagon_IE_32_6_X },
      { MCSymbolRefExpr::VK_Hexagon_IE_GOT, fixup_Hexagon_IE_GOT_32_6_X },
      { MCSymbolRefExpr::VK_PCREL,          fixup_Hexagon_B32_PCREL_X },
      { MCSymbolRefExpr::VK_Hexagon_GD_PLT, fixup_Hexagon_GD_PLT_B32_PCREL_X },
      { MCSymbolRefExpr::VK_Hexagon_LD_PLT, fixup_Hexagon_LD_PLT_B32_PCREL_X },
    };

    auto F = Relocs.find(VarKind);
    if (F != Relocs.end())
      return Hexagon::Fixups(F->second);
    raise_relocation_error(0, VarKind);
  }

  if (MCID.isBranch())
    return fixup_Hexagon_B13_PCREL;

  static const std::map<unsigned,unsigned> RelocsLo = {
    { MCSymbolRefExpr::VK_GOT,            fixup_Hexagon_GOT_LO16 },
    { MCSymbolRefExpr::VK_GOTREL,         fixup_Hexagon_GOTREL_LO16 },
    { MCSymbolRefExpr::VK_Hexagon_GD_GOT, fixup_Hexagon_GD_GOT_LO16 },
    { MCSymbolRefExpr::VK_Hexagon_LD_GOT, fixup_Hexagon_LD_GOT_LO16 },
    { MCSymbolRefExpr::VK_Hexagon_IE,     fixup_Hexagon_IE_LO16 },
    { MCSymbolRefExpr::VK_Hexagon_IE_GOT, fixup_Hexagon_IE_GOT_LO16 },
    { MCSymbolRefExpr::VK_TPREL,          fixup_Hexagon_TPREL_LO16 },
    { MCSymbolRefExpr::VK_DTPREL,         fixup_Hexagon_DTPREL_LO16 },
    { MCSymbolRefExpr::VK_None,           fixup_Hexagon_LO16 },
  };

  static const std::map<unsigned,unsigned> RelocsHi = {
    { MCSymbolRefExpr::VK_GOT,            fixup_Hexagon_GOT_HI16 },
    { MCSymbolRefExpr::VK_GOTREL,         fixup_Hexagon_GOTREL_HI16 },
    { MCSymbolRefExpr::VK_Hexagon_GD_GOT, fixup_Hexagon_GD_GOT_HI16 },
    { MCSymbolRefExpr::VK_Hexagon_LD_GOT, fixup_Hexagon_LD_GOT_HI16 },
    { MCSymbolRefExpr::VK_Hexagon_IE,     fixup_Hexagon_IE_HI16 },
    { MCSymbolRefExpr::VK_Hexagon_IE_GOT, fixup_Hexagon_IE_GOT_HI16 },
    { MCSymbolRefExpr::VK_TPREL,          fixup_Hexagon_TPREL_HI16 },
    { MCSymbolRefExpr::VK_DTPREL,         fixup_Hexagon_DTPREL_HI16 },
    { MCSymbolRefExpr::VK_None,           fixup_Hexagon_HI16 },
  };

  switch (MCID.getOpcode()) {
    case Hexagon::LO:
    case Hexagon::A2_tfril: {
      auto F = RelocsLo.find(VarKind);
      if (F != RelocsLo.end())
        return Hexagon::Fixups(F->second);
      break;
    }
    case Hexagon::HI:
    case Hexagon::A2_tfrih: {
      auto F = RelocsHi.find(VarKind);
      if (F != RelocsHi.end())
        return Hexagon::Fixups(F->second);
      break;
    }
  }

  raise_relocation_error(0, VarKind);
}

static bool isPCRel(unsigned Kind) {
  switch (Kind){
  case fixup_Hexagon_B22_PCREL:
  case fixup_Hexagon_B15_PCREL:
  case fixup_Hexagon_B7_PCREL:
  case fixup_Hexagon_B13_PCREL:
  case fixup_Hexagon_B9_PCREL:
  case fixup_Hexagon_B32_PCREL_X:
  case fixup_Hexagon_B22_PCREL_X:
  case fixup_Hexagon_B15_PCREL_X:
  case fixup_Hexagon_B13_PCREL_X:
  case fixup_Hexagon_B9_PCREL_X:
  case fixup_Hexagon_B7_PCREL_X:
  case fixup_Hexagon_32_PCREL:
  case fixup_Hexagon_PLT_B22_PCREL:
  case fixup_Hexagon_GD_PLT_B22_PCREL:
  case fixup_Hexagon_LD_PLT_B22_PCREL:
  case fixup_Hexagon_GD_PLT_B22_PCREL_X:
  case fixup_Hexagon_LD_PLT_B22_PCREL_X:
  case fixup_Hexagon_6_PCREL_X:
    return true;
  default:
    return false;
  }
}

unsigned HexagonMCCodeEmitter::getExprOpValue(const MCInst &MI,
      const MCOperand &MO, const MCExpr *ME, SmallVectorImpl<MCFixup> &Fixups,
      const MCSubtargetInfo &STI) const {
  if (isa<HexagonMCExpr>(ME))
    ME = &HexagonMCInstrInfo::getExpr(*ME);
  int64_t Value;
  if (ME->evaluateAsAbsolute(Value)) {
    bool InstExtendable = HexagonMCInstrInfo::isExtendable(MCII, MI) ||
                          HexagonMCInstrInfo::isExtended(MCII, MI);
    // Only sub-instruction #1 can be extended in a duplex. If MI is a
    // sub-instruction #0, it is not extended even if Extended is true
    // (it can be true for the duplex as a whole).
    bool IsSub0 = HexagonMCInstrInfo::isSubInstruction(MI) && !State.SubInst1;
    if (State.Extended && InstExtendable && !IsSub0) {
      unsigned OpIdx = ~0u;
      for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
        if (&MO != &MI.getOperand(I))
          continue;
        OpIdx = I;
        break;
      }
      assert(OpIdx != ~0u);
      if (OpIdx == HexagonMCInstrInfo::getExtendableOp(MCII, MI)) {
        unsigned Shift = HexagonMCInstrInfo::getExtentAlignment(MCII, MI);
        Value = (Value & 0x3f) << Shift;
      }
    }
    return Value;
  }
  assert(ME->getKind() == MCExpr::SymbolRef ||
         ME->getKind() == MCExpr::Binary);
  if (ME->getKind() == MCExpr::Binary) {
    MCBinaryExpr const *Binary = cast<MCBinaryExpr>(ME);
    getExprOpValue(MI, MO, Binary->getLHS(), Fixups, STI);
    getExprOpValue(MI, MO, Binary->getRHS(), Fixups, STI);
    return 0;
  }

  unsigned FixupKind = fixup_Invalid;
  const MCSymbolRefExpr *MCSRE = static_cast<const MCSymbolRefExpr *>(ME);
  const MCInstrDesc &MCID = HexagonMCInstrInfo::getDesc(MCII, MI);
  unsigned FixupWidth = HexagonMCInstrInfo::getExtentBits(MCII, MI) -
                        HexagonMCInstrInfo::getExtentAlignment(MCII, MI);
  MCSymbolRefExpr::VariantKind VarKind = MCSRE->getKind();
  unsigned Opc = MCID.getOpcode();
  unsigned IType = HexagonMCInstrInfo::getType(MCII, MI);

  LLVM_DEBUG(dbgs() << "----------------------------------------\n"
                    << "Opcode Name: " << HexagonMCInstrInfo::getName(MCII, MI)
                    << "\nOpcode: " << Opc << "\nRelocation bits: "
                    << FixupWidth << "\nAddend: " << State.Addend
                    << "\nVariant: " << unsigned(VarKind)
                    << "\n----------------------------------------\n");

  // Pick the applicable fixup kind for the symbol.
  // Handle special cases first, the rest will be looked up in the tables.

  if (FixupWidth == 16 && !State.Extended) {
    if (VarKind == MCSymbolRefExpr::VK_None) {
      if (HexagonMCInstrInfo::s27_2_reloc(*MO.getExpr())) {
        // A2_iconst.
        FixupKind = Hexagon::fixup_Hexagon_27_REG;
      } else {
        // Look for GP-relative fixups.
        unsigned Shift = HexagonMCInstrInfo::getExtentAlignment(MCII, MI);
        static const Hexagon::Fixups GPRelFixups[] = {
          Hexagon::fixup_Hexagon_GPREL16_0, Hexagon::fixup_Hexagon_GPREL16_1,
          Hexagon::fixup_Hexagon_GPREL16_2, Hexagon::fixup_Hexagon_GPREL16_3
        };
        assert(Shift < std::size(GPRelFixups));
        auto UsesGP = [](const MCInstrDesc &D) {
          return is_contained(D.implicit_uses(), Hexagon::GP);
        };
        if (UsesGP(MCID))
          FixupKind = GPRelFixups[Shift];
      }
    } else if (VarKind == MCSymbolRefExpr::VK_GOTREL) {
      // Select between LO/HI.
      if (Opc == Hexagon::LO)
        FixupKind = Hexagon::fixup_Hexagon_GOTREL_LO16;
      else if (Opc == Hexagon::HI)
        FixupKind = Hexagon::fixup_Hexagon_GOTREL_HI16;
    }
  } else {
    bool BranchOrCR = MCID.isBranch() || IType == HexagonII::TypeCR;
    switch (FixupWidth) {
      case 9:
        if (BranchOrCR)
          FixupKind = State.Extended ? Hexagon::fixup_Hexagon_B9_PCREL_X
                                     : Hexagon::fixup_Hexagon_B9_PCREL;
        break;
      case 8:
      case 7:
        if (State.Extended && VarKind == MCSymbolRefExpr::VK_GOT)
          FixupKind = HexagonMCInstrInfo::isExtentSigned(MCII, MI)
                        ? Hexagon::fixup_Hexagon_GOT_16_X
                        : Hexagon::fixup_Hexagon_GOT_11_X;
        else if (FixupWidth == 7 && BranchOrCR)
          FixupKind = State.Extended ? Hexagon::fixup_Hexagon_B7_PCREL_X
                                     : Hexagon::fixup_Hexagon_B7_PCREL;
        break;
      case 0:
        FixupKind = getFixupNoBits(MCII, MI, MO, VarKind);
        break;
    }
  }

  if (FixupKind == fixup_Invalid) {
    const auto &FixupTable = State.Extended ? ExtFixups : StdFixups;

    auto FindVK = FixupTable.find(VarKind);
    if (FindVK != FixupTable.end())
      FixupKind = FindVK->second[FixupWidth];
  }

  if (FixupKind == fixup_Invalid)
    raise_relocation_error(FixupWidth, VarKind);

  const MCExpr *FixupExpr = MO.getExpr();
  if (State.Addend != 0 && isPCRel(FixupKind)) {
    const MCExpr *C = MCConstantExpr::create(State.Addend, MCT);
    FixupExpr = MCBinaryExpr::createAdd(FixupExpr, C, MCT);
  }

  MCFixup Fixup = MCFixup::create(State.Addend, FixupExpr,
                                  MCFixupKind(FixupKind), MI.getLoc());
  Fixups.push_back(Fixup);
  // All of the information is in the fixup.
  return 0;
}

unsigned
HexagonMCCodeEmitter::getMachineOpValue(MCInst const &MI, MCOperand const &MO,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        MCSubtargetInfo const &STI) const {
  size_t OperandNumber = ~0U;
  for (unsigned i = 0, n = MI.getNumOperands(); i < n; ++i)
    if (&MI.getOperand(i) == &MO) {
      OperandNumber = i;
      break;
    }
  assert((OperandNumber != ~0U) && "Operand not found");

  if (HexagonMCInstrInfo::isNewValue(MCII, MI) &&
      &MO == &HexagonMCInstrInfo::getNewValueOperand(MCII, MI)) {
    // Calculate the new value distance to the associated producer
    unsigned SOffset = 0;
    unsigned VOffset = 0;
    unsigned UseReg = MO.getReg();
    unsigned DefReg1 = Hexagon::NoRegister;
    unsigned DefReg2 = Hexagon::NoRegister;

    auto Instrs = HexagonMCInstrInfo::bundleInstructions(*State.Bundle);
    const MCOperand *I = Instrs.begin() + State.Index - 1;

    for (;; --I) {
      assert(I != Instrs.begin() - 1 && "Couldn't find producer");
      MCInst const &Inst = *I->getInst();
      if (HexagonMCInstrInfo::isImmext(Inst))
        continue;

      DefReg1 = Hexagon::NoRegister;
      DefReg2 = Hexagon::NoRegister;
      ++SOffset;
      if (HexagonMCInstrInfo::isVector(MCII, Inst)) {
        // Vector instructions don't count scalars.
        ++VOffset;
      }
      if (HexagonMCInstrInfo::hasNewValue(MCII, Inst))
        DefReg1 = HexagonMCInstrInfo::getNewValueOperand(MCII, Inst).getReg();
      if (HexagonMCInstrInfo::hasNewValue2(MCII, Inst))
        DefReg2 = HexagonMCInstrInfo::getNewValueOperand2(MCII, Inst).getReg();
      if (!RegisterMatches(UseReg, DefReg1, DefReg2)) {
        // This isn't the register we're looking for
        continue;
      }
      if (!HexagonMCInstrInfo::isPredicated(MCII, Inst)) {
        // Producer is unpredicated
        break;
      }
      assert(HexagonMCInstrInfo::isPredicated(MCII, MI) &&
             "Unpredicated consumer depending on predicated producer");
      if (HexagonMCInstrInfo::isPredicatedTrue(MCII, Inst) ==
          HexagonMCInstrInfo::isPredicatedTrue(MCII, MI))
        // Producer predicate sense matched ours.
        break;
    }
    // Hexagon PRM 10.11 Construct Nt from distance
    unsigned Offset = HexagonMCInstrInfo::isVector(MCII, MI) ? VOffset
                                                             : SOffset;
    Offset <<= 1;
    Offset |= HexagonMCInstrInfo::SubregisterBit(UseReg, DefReg1, DefReg2);
    return Offset;
  }

  assert(!MO.isImm());
  if (MO.isReg()) {
    unsigned Reg = MO.getReg();
    switch (HexagonMCInstrInfo::getDesc(MCII, MI)
                .operands()[OperandNumber]
                .RegClass) {
    case GeneralSubRegsRegClassID:
    case GeneralDoubleLow8RegsRegClassID:
      return HexagonMCInstrInfo::getDuplexRegisterNumbering(Reg);
    default:
      break;
    }
    return MCT.getRegisterInfo()->getEncodingValue(Reg);
  }

  return getExprOpValue(MI, MO, MO.getExpr(), Fixups, STI);
}

MCCodeEmitter *llvm::createHexagonMCCodeEmitter(MCInstrInfo const &MII,
                                                MCContext &MCT) {
  return new HexagonMCCodeEmitter(MII, MCT);
}

#include "HexagonGenMCCodeEmitter.inc"
