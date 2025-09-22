//===-- RISCVMCTargetDesc.h - RISC-V Target Descriptions --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides RISC-V specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVMCTARGETDESC_H
#define LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVMCTARGETDESC_H

#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class Target;

MCCodeEmitter *createRISCVMCCodeEmitter(const MCInstrInfo &MCII,
                                        MCContext &Ctx);

MCAsmBackend *createRISCVAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                    const MCRegisterInfo &MRI,
                                    const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter> createRISCVELFObjectWriter(uint8_t OSABI,
                                                                 bool Is64Bit);

namespace RISCVVInversePseudosTable {

struct PseudoInfo {
  uint16_t Pseudo;
  uint16_t BaseInstr;
  uint8_t VLMul;
  uint8_t SEW;
};

#define GET_RISCVVInversePseudosTable_DECL
#include "RISCVGenSearchableTables.inc"

} // namespace RISCVVInversePseudosTable
} // namespace llvm

// Defines symbolic names for RISC-V registers.
#define GET_REGINFO_ENUM
#include "RISCVGenRegisterInfo.inc"

// Defines symbolic names for RISC-V instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "RISCVGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "RISCVGenSubtargetInfo.inc"

#endif
