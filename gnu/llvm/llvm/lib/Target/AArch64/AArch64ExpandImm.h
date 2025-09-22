//===- AArch64ExpandImm.h - AArch64 Immediate Expansion ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 immediate expansion stuff.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64EXPANDIMM_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64EXPANDIMM_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

namespace AArch64_IMM {

struct ImmInsnModel {
  unsigned Opcode;
  uint64_t Op1;
  uint64_t Op2;
};

void expandMOVImm(uint64_t Imm, unsigned BitSize,
		  SmallVectorImpl<ImmInsnModel> &Insn);

} // end namespace AArch64_IMM

} // end namespace llvm

#endif
