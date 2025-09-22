//===-- X86EncodingOptimization.h - X86 Encoding optimization ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the X86 encoding optimization
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86ENCODINGOPTIMIZATION_H
#define LLVM_LIB_TARGET_X86_X86ENCODINGOPTIMIZATION_H
namespace llvm {
class MCInst;
class MCInstrDesc;
namespace X86 {
bool optimizeInstFromVEX3ToVEX2(MCInst &MI, const MCInstrDesc &Desc);
bool optimizeShiftRotateWithImmediateOne(MCInst &MI);
bool optimizeVPCMPWithImmediateOneOrSix(MCInst &MI);
bool optimizeMOVSX(MCInst &MI);
bool optimizeINCDEC(MCInst &MI, bool In64BitMode);
bool optimizeMOV(MCInst &MI, bool In64BitMode);
bool optimizeToFixedRegisterOrShortImmediateForm(MCInst &MI);
unsigned getOpcodeForShortImmediateForm(unsigned Opcode);
unsigned getOpcodeForLongImmediateForm(unsigned Opcode);
} // namespace X86
} // namespace llvm
#endif
