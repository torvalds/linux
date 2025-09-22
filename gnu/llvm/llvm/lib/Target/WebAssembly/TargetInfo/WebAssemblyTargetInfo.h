//===-- WebAssemblyTargetInfo.h - WebAssembly Target Impl -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file registers the WebAssembly target.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_TARGETINFO_WEBASSEMBLYTARGETINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_TARGETINFO_WEBASSEMBLYTARGETINFO_H

namespace llvm {

class Target;

Target &getTheWebAssemblyTarget32();
Target &getTheWebAssemblyTarget64();

namespace WebAssembly {

int getStackOpcode(unsigned short Opcode);
int getRegisterOpcode(unsigned short Opcode);
int getWasm64Opcode(unsigned short Opcode);

} // namespace WebAssembly

} // namespace llvm

#endif // LLVM_LIB_TARGET_WEBASSEMBLY_TARGETINFO_WEBASSEMBLYTARGETINFO_H
