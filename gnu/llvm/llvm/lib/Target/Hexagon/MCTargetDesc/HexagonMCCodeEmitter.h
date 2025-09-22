//===- HexagonMCCodeEmitter.h - Hexagon Target Descriptions -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Definition for classes that emit Hexagon machine code from MCInsts
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCCODEEMITTER_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCCODEEMITTER_H

#include "MCTargetDesc/HexagonFixupKinds.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace llvm {

class MCContext;
class MCInst;
class MCInstrInfo;
class MCOperand;
class MCSubtargetInfo;
class raw_ostream;

class HexagonMCCodeEmitter : public MCCodeEmitter {
  MCContext &MCT;
  MCInstrInfo const &MCII;

  // A mutable state of the emitter when encoding bundles and duplexes.
  struct EmitterState {
    unsigned Addend = 0;
    bool Extended = false;
    bool SubInst1 = false;
    const MCInst *Bundle = nullptr;
    size_t Index = 0;
  };
  mutable EmitterState State;

public:
  HexagonMCCodeEmitter(MCInstrInfo const &MII, MCContext &MCT)
    : MCT(MCT), MCII(MII) {}

  void encodeInstruction(MCInst const &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         MCSubtargetInfo const &STI) const override;

  void encodeSingleInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI,
                               uint32_t Parse) const;

  // TableGen'erated function for getting the
  // binary encoding for an instruction.
  uint64_t getBinaryCodeForInstr(MCInst const &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 MCSubtargetInfo const &STI) const;

  /// Return binary encoding of operand.
  unsigned getMachineOpValue(MCInst const &MI, MCOperand const &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             MCSubtargetInfo const &STI) const;

private:
  // helper routine for getMachineOpValue()
  unsigned getExprOpValue(const MCInst &MI, const MCOperand &MO,
                          const MCExpr *ME, SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  Hexagon::Fixups getFixupNoBits(MCInstrInfo const &MCII, const MCInst &MI,
                                 const MCOperand &MO,
                                 const MCSymbolRefExpr::VariantKind Kind) const;

  // Return parse bits for instruction `MCI' inside bundle `MCB'
  uint32_t parseBits(size_t Last, MCInst const &MCB, MCInst const &MCI) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCCODEEMITTER_H
