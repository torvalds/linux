//===-- llvm/CodeGen/TargetOpcodes.h - Target Indep Opcodes -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the target independent instruction opcodes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETOPCODES_H
#define LLVM_CODEGEN_TARGETOPCODES_H

namespace llvm {

/// Invariant opcodes: All instruction sets have these as their low opcodes.
///
namespace TargetOpcode {
enum {
#define HANDLE_TARGET_OPCODE(OPC) OPC,
#define HANDLE_TARGET_OPCODE_MARKER(IDENT, OPC) IDENT = OPC,
#include "llvm/Support/TargetOpcodes.def"
};
} // end namespace TargetOpcode

/// Check whether the given Opcode is a generic opcode that is not supposed
/// to appear after ISel.
inline bool isPreISelGenericOpcode(unsigned Opcode) {
  return Opcode >= TargetOpcode::PRE_ISEL_GENERIC_OPCODE_START &&
         Opcode <= TargetOpcode::PRE_ISEL_GENERIC_OPCODE_END;
}

/// Check whether the given Opcode is a target-specific opcode.
inline bool isTargetSpecificOpcode(unsigned Opcode) {
  return Opcode > TargetOpcode::PRE_ISEL_GENERIC_OPCODE_END;
}

/// \returns true if \p Opcode is an optimization hint opcode which is not
/// supposed to appear after ISel.
inline bool isPreISelGenericOptimizationHint(unsigned Opcode) {
  return Opcode >= TargetOpcode::PRE_ISEL_GENERIC_OPTIMIZATION_HINT_START &&
         Opcode <= TargetOpcode::PRE_ISEL_GENERIC_OPTIMIZATION_HINT_END;
}

} // end namespace llvm

#endif
