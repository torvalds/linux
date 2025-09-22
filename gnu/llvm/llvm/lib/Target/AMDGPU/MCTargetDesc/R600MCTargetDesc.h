//===-- R600MCTargetDesc.h - R600 Target Descriptions -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Provides R600 specific target descriptions.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_R600MCTARGETDESC_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_R600MCTARGETDESC_H

#include <cstdint>

namespace llvm {
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;

MCCodeEmitter *createR600MCCodeEmitter(const MCInstrInfo &MCII,
                                       MCContext &Ctx);
MCInstrInfo *createR600MCInstrInfo();

} // namespace llvm

#define GET_REGINFO_ENUM
#include "R600GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_OPERAND_ENUM
#define GET_INSTRINFO_SCHED_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "R600GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "R600GenSubtargetInfo.inc"

#endif
