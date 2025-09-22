//===-- NVPTXMCTargetDesc.h - NVPTX Target Descriptions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides NVPTX specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCTARGETDESC_H
#define LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCTARGETDESC_H

#include <stdint.h>

// Defines symbolic names for PTX registers.
#define GET_REGINFO_ENUM
#include "NVPTXGenRegisterInfo.inc"

// Defines symbolic names for the PTX instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "NVPTXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "NVPTXGenSubtargetInfo.inc"

#endif
