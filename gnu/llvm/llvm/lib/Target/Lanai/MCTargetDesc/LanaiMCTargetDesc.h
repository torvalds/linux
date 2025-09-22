//===-- LanaiMCTargetDesc.h - Lanai Target Descriptions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Lanai specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIMCTARGETDESC_H
#define LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIMCTARGETDESC_H

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCSubtargetInfo;
class Target;

MCCodeEmitter *createLanaiMCCodeEmitter(const MCInstrInfo &MCII,
                                        MCContext &Ctx);

MCAsmBackend *createLanaiAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                    const MCRegisterInfo &MRI,
                                    const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter> createLanaiELFObjectWriter(uint8_t OSABI);
} // namespace llvm

// Defines symbolic names for Lanai registers.  This defines a mapping from
// register name to register number.
#define GET_REGINFO_ENUM
#include "LanaiGenRegisterInfo.inc"

// Defines symbolic names for the Lanai instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "LanaiGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "LanaiGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIMCTARGETDESC_H
