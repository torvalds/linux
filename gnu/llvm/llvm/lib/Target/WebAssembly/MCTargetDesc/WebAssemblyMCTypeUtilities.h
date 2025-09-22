//===-- WebAssemblyMCTypeUtilities - WebAssembly Type Utilities-*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the WebAssembly-specific type parsing
/// utility functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYMCTYPEUTILITIES_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYMCTYPEUTILITIES_H

#include "llvm/BinaryFormat/Wasm.h"

namespace llvm {

namespace WebAssembly {

/// Used as immediate MachineOperands for block signatures
enum class BlockType : unsigned {
  Invalid = 0x00,
  Void = 0x40,
  I32 = unsigned(wasm::ValType::I32),
  I64 = unsigned(wasm::ValType::I64),
  F32 = unsigned(wasm::ValType::F32),
  F64 = unsigned(wasm::ValType::F64),
  V128 = unsigned(wasm::ValType::V128),
  Externref = unsigned(wasm::ValType::EXTERNREF),
  Funcref = unsigned(wasm::ValType::FUNCREF),
  Exnref = unsigned(wasm::ValType::EXNREF),
  // Multivalue blocks (and other non-void blocks) are only emitted when the
  // blocks will never be exited and are at the ends of functions (see
  // WebAssemblyCFGStackify::fixEndsAtEndOfFunction). They also are never made
  // to pop values off the stack, so the exact multivalue signature can always
  // be inferred from the return type of the parent function in MCInstLower.
  Multivalue = 0xffff,
};

inline bool isRefType(wasm::ValType Type) {
  return Type == wasm::ValType::EXTERNREF || Type == wasm::ValType::FUNCREF ||
         Type == wasm::ValType::EXNREF;
}

// Convert ValType or a list/signature of ValTypes to a string.

// Convert an unsigned integer, which can be among wasm::ValType enum, to its
// type name string. If the input is not within wasm::ValType, returns
// "invalid_type".
const char *anyTypeToString(unsigned Type);
const char *typeToString(wasm::ValType Type);
// Convert a list of ValTypes into a string in the format of
// "type0, type1, ... typeN"
std::string typeListToString(ArrayRef<wasm::ValType> List);
// Convert a wasm signature into a string in the format of
// "(params) -> (results)", where params and results are a string of ValType
// lists.
std::string signatureToString(const wasm::WasmSignature *Sig);

// Convert a register class ID to a wasm ValType.
wasm::ValType regClassToValType(unsigned RC);

// Convert StringRef to ValType / HealType / BlockType

std::optional<wasm::ValType> parseType(StringRef Type);
BlockType parseBlockType(StringRef Type);

} // end namespace WebAssembly
} // end namespace llvm

#endif
