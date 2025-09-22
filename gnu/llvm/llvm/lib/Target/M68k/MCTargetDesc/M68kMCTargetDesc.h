//===-- M68kMCTargetDesc.h - M68k Target Descriptions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides M68k specific target descriptions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCTARGETDESC_H
#define LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCTARGETDESC_H

#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCRelocationInfo;
class MCTargetOptions;
class Target;
class Triple;
class StringRef;
class raw_ostream;
class raw_pwrite_stream;

MCAsmBackend *createM68kAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                   const MCRegisterInfo &MRI,
                                   const MCTargetOptions &Options);

MCCodeEmitter *createM68kMCCodeEmitter(const MCInstrInfo &MCII,
                                       MCContext &Ctx);

/// Construct an M68k ELF object writer.
std::unique_ptr<MCObjectTargetWriter> createM68kELFObjectWriter(uint8_t OSABI);

} // namespace llvm

// Defines symbolic names for M68k registers. This defines a mapping from
// register name to register number.
#define GET_REGINFO_ENUM
#include "M68kGenRegisterInfo.inc"

// Defines symbolic names for the M68k instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "M68kGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "M68kGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCTARGETDESC_H
