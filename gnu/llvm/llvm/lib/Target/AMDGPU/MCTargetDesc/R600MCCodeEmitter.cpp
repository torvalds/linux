//===- R600MCCodeEmitter.cpp - Code Emitter for R600->Cayman GPU families -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// The R600 code emitter produces machine code that can be executed
/// directly on the GPU device.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/R600MCTargetDesc.h"
#include "R600Defines.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/TargetParser/SubtargetFeature.h"

using namespace llvm;

namespace {

class R600MCCodeEmitter : public MCCodeEmitter {
  const MCRegisterInfo &MRI;
  const MCInstrInfo &MCII;

public:
  R600MCCodeEmitter(const MCInstrInfo &mcii, const MCRegisterInfo &mri)
    : MRI(mri), MCII(mcii) {}
  R600MCCodeEmitter(const R600MCCodeEmitter &) = delete;
  R600MCCodeEmitter &operator=(const R600MCCodeEmitter &) = delete;

  /// Encode the instruction and write it to the OS.
  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  /// \returns the encoding for an MCOperand.
  uint64_t getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

private:
  void emit(uint32_t value, SmallVectorImpl<char> &CB) const;
  void emit(uint64_t value, SmallVectorImpl<char> &CB) const;

  unsigned getHWReg(unsigned regNo) const;

  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
};

} // end anonymous namespace

enum RegElement {
  ELEMENT_X = 0,
  ELEMENT_Y,
  ELEMENT_Z,
  ELEMENT_W
};

enum FCInstr {
  FC_IF_PREDICATE = 0,
  FC_ELSE,
  FC_ENDIF,
  FC_BGNLOOP,
  FC_ENDLOOP,
  FC_BREAK_PREDICATE,
  FC_CONTINUE
};

MCCodeEmitter *llvm::createR600MCCodeEmitter(const MCInstrInfo &MCII,
                                             MCContext &Ctx) {
  return new R600MCCodeEmitter(MCII, *Ctx.getRegisterInfo());
}

void R600MCCodeEmitter::encodeInstruction(const MCInst &MI,
                                          SmallVectorImpl<char> &CB,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  if (MI.getOpcode() == R600::RETURN ||
    MI.getOpcode() == R600::FETCH_CLAUSE ||
    MI.getOpcode() == R600::ALU_CLAUSE ||
    MI.getOpcode() == R600::BUNDLE ||
    MI.getOpcode() == R600::KILL) {
    return;
  }
  if (IS_VTX(Desc)) {
    uint64_t InstWord01 = getBinaryCodeForInstr(MI, Fixups, STI);
    uint32_t InstWord2 = MI.getOperand(2).getImm(); // Offset
    if (!(STI.hasFeature(R600::FeatureCaymanISA))) {
      InstWord2 |= 1 << 19; // Mega-Fetch bit
    }

    emit(InstWord01, CB);
    emit(InstWord2, CB);
    emit((uint32_t)0, CB);
  } else if (IS_TEX(Desc)) {
    int64_t Sampler = MI.getOperand(14).getImm();

    int64_t SrcSelect[4] = {
        MI.getOperand(2).getImm(), MI.getOperand(3).getImm(),
        MI.getOperand(4).getImm(), MI.getOperand(5).getImm()};
    int64_t Offsets[3] = {MI.getOperand(6).getImm() & 0x1F,
                          MI.getOperand(7).getImm() & 0x1F,
                          MI.getOperand(8).getImm() & 0x1F};

    uint64_t Word01 = getBinaryCodeForInstr(MI, Fixups, STI);
    uint32_t Word2 = Sampler << 15 | SrcSelect[ELEMENT_X] << 20 |
                     SrcSelect[ELEMENT_Y] << 23 | SrcSelect[ELEMENT_Z] << 26 |
                     SrcSelect[ELEMENT_W] << 29 | Offsets[0] << 0 |
                     Offsets[1] << 5 | Offsets[2] << 10;

    emit(Word01, CB);
    emit(Word2, CB);
    emit((uint32_t)0, CB);
  } else {
    uint64_t Inst = getBinaryCodeForInstr(MI, Fixups, STI);
    if ((STI.hasFeature(R600::FeatureR600ALUInst)) &&
       ((Desc.TSFlags & R600_InstFlag::OP1) ||
         Desc.TSFlags & R600_InstFlag::OP2)) {
      uint64_t ISAOpCode = Inst & (0x3FFULL << 39);
      Inst &= ~(0x3FFULL << 39);
      Inst |= ISAOpCode << 1;
    }
    emit(Inst, CB);
  }
}

void R600MCCodeEmitter::emit(uint32_t Value, SmallVectorImpl<char> &CB) const {
  support::endian::write(CB, Value, llvm::endianness::little);
}

void R600MCCodeEmitter::emit(uint64_t Value, SmallVectorImpl<char> &CB) const {
  support::endian::write(CB, Value, llvm::endianness::little);
}

unsigned R600MCCodeEmitter::getHWReg(unsigned RegNo) const {
  return MRI.getEncodingValue(RegNo) & HW_REG_MASK;
}

uint64_t R600MCCodeEmitter::getMachineOpValue(const MCInst &MI,
                                              const MCOperand &MO,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    if (HAS_NATIVE_OPERANDS(MCII.get(MI.getOpcode()).TSFlags))
      return MRI.getEncodingValue(MO.getReg());
    return getHWReg(MO.getReg());
  }

  if (MO.isExpr()) {
    // We put rodata at the end of code section, then map the entire
    // code secetion as vtx buf. Thus the section relative address is the
    // correct one.
    // Each R600 literal instruction has two operands
    // We can't easily get the order of the current one, so compare against
    // the first one and adjust offset.
    const unsigned offset = (&MO == &MI.getOperand(0)) ? 0 : 4;
    Fixups.push_back(MCFixup::create(offset, MO.getExpr(), FK_SecRel_4, MI.getLoc()));
    return 0;
  }

  assert(MO.isImm());
  return MO.getImm();
}

#include "R600GenMCCodeEmitter.inc"
