//===-- M68kMCCodeEmitter.h - M68k Code Emitter -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations for the code emitter which are useful
/// outside of the emitter itself.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCCODEEMITTER_H
#define LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCCODEEMITTER_H

#include <cstdint>

namespace llvm {
namespace M68k {

const uint8_t *getMCInstrBeads(unsigned);

} // namespace M68k
} // namespace llvm

#endif // LLVM_LIB_TARGET_M68K_MCTARGETDESC_M68KMCCODEEMITTER_H
