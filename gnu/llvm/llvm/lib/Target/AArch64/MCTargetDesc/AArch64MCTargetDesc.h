//===-- AArch64MCTargetDesc.h - AArch64 Target Descriptions -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides AArch64 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCTARGETDESC_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCTARGETDESC_H

#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/DataTypes.h"

#include <memory>

namespace llvm {
class formatted_raw_ostream;
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInst;
class MCInstrInfo;
class MCInstPrinter;
class MCRegisterInfo;
class MCObjectTargetWriter;
class MCStreamer;
class MCSubtargetInfo;
class MCTargetOptions;
class MCTargetStreamer;
class Target;
class Triple;

MCCodeEmitter *createAArch64MCCodeEmitter(const MCInstrInfo &MCII,
                                          MCContext &Ctx);
MCAsmBackend *createAArch64leAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &Options);
MCAsmBackend *createAArch64beAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter>
createAArch64ELFObjectWriter(uint8_t OSABI, bool IsILP32);

std::unique_ptr<MCObjectTargetWriter>
createAArch64MachObjectWriter(uint32_t CPUType, uint32_t CPUSubtype,
                              bool IsILP32);

std::unique_ptr<MCObjectTargetWriter>
createAArch64WinCOFFObjectWriter(const Triple &TheTriple);

MCTargetStreamer *createAArch64AsmTargetStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint);

namespace AArch64_MC {
void initLLVMToCVRegMapping(MCRegisterInfo *MRI);
bool isHForm(const MCInst &MI, const MCInstrInfo *MCII);
bool isQForm(const MCInst &MI, const MCInstrInfo *MCII);
bool isFpOrNEON(const MCInst &MI, const MCInstrInfo *MCII);
}

namespace AArch64 {
enum OperandType {
  OPERAND_IMPLICIT_IMM_0 = MCOI::OPERAND_FIRST_TARGET,
};
} // namespace AArch64

} // End llvm namespace

// Defines symbolic names for AArch64 registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "AArch64GenRegisterInfo.inc"

// Defines symbolic names for the AArch64 instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "AArch64GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "AArch64GenSubtargetInfo.inc"

#endif
