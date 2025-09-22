//===--- XtensaUtils.h ---- Xtensa Utility Functions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains miscellaneous utility functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XTENSA_XTENSAUTILS_H
#define LLVM_LIB_TARGET_XTENSA_XTENSAUTILS_H

#include "XtensaInstrInfo.h"
#include "llvm/CodeGen/MachineInstr.h"

namespace llvm {
// Check address offset for load/store instructions.
// The offset should be multiple of scale.
bool isValidAddrOffset(int Scale, int64_t OffsetVal);

// Check address offset for load/store instructions.
bool isValidAddrOffset(MachineInstr &MI, int64_t Offset);
} // namespace llvm
#endif // LLVM_LIB_TARGET_XTENSA_XTENSAUTILS_H
