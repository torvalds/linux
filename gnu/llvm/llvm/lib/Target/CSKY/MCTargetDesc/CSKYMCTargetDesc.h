//===-- CSKYMCTargetDesc.h - CSKY Target Descriptions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides CSKY specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYMCTARGETDESC_H
#define LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYMCTARGETDESC_H

#include "llvm/MC/MCTargetOptions.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCRegisterInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class Target;
class Triple;

std::unique_ptr<MCObjectTargetWriter> createCSKYELFObjectWriter();

MCAsmBackend *createCSKYAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                   const MCRegisterInfo &MRI,
                                   const MCTargetOptions &Options);

MCCodeEmitter *createCSKYMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);
} // namespace llvm

#define GET_REGINFO_ENUM
#include "CSKYGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "CSKYGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "CSKYGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYMCTARGETDESC_H
