//===- LoongArchMCTargetDesc.h - LoongArch Target Descriptions --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides LoongArch specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHMCTARGETDESC_H
#define LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHMCTARGETDESC_H

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

MCCodeEmitter *createLoongArchMCCodeEmitter(const MCInstrInfo &MCII,
                                            MCContext &Ctx);

MCAsmBackend *createLoongArchAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter>
createLoongArchELFObjectWriter(uint8_t OSABI, bool Is64Bit, bool Relax);

} // end namespace llvm

// Defines symbolic names for LoongArch registers.
#define GET_REGINFO_ENUM
#include "LoongArchGenRegisterInfo.inc"

// Defines symbolic names for LoongArch instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "LoongArchGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "LoongArchGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHMCTARGETDESC_H
