//===-- MSP430MCTargetDesc.h - MSP430 Target Descriptions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides MSP430 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSP430_MCTARGETDESC_MSP430MCTARGETDESC_H
#define LLVM_LIB_TARGET_MSP430_MCTARGETDESC_MSP430MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class Target;
class MCAsmBackend;
class MCCodeEmitter;
class MCInstrInfo;
class MCSubtargetInfo;
class MCRegisterInfo;
class MCContext;
class MCTargetOptions;
class MCObjectTargetWriter;
class MCStreamer;
class MCTargetStreamer;

/// Creates a machine code emitter for MSP430.
MCCodeEmitter *createMSP430MCCodeEmitter(const MCInstrInfo &MCII,
                                         MCContext &Ctx);

MCAsmBackend *createMSP430MCAsmBackend(const Target &T,
                                       const MCSubtargetInfo &STI,
                                       const MCRegisterInfo &MRI,
                                       const MCTargetOptions &Options);

MCTargetStreamer *
createMSP430ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

std::unique_ptr<MCObjectTargetWriter>
createMSP430ELFObjectWriter(uint8_t OSABI);

} // End llvm namespace

// Defines symbolic names for MSP430 registers.
// This defines a mapping from register name to register number.
#define GET_REGINFO_ENUM
#include "MSP430GenRegisterInfo.inc"

// Defines symbolic names for the MSP430 instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "MSP430GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "MSP430GenSubtargetInfo.inc"

#endif
