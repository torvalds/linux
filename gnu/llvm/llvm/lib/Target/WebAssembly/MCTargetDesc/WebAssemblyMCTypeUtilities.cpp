//===- WebAssemblyMCTypeUtilities.cpp - WebAssembly Type Utility Functions-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements several utility functions for WebAssembly type parsing.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyMCTypeUtilities.h"
#include "WebAssemblyMCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"

using namespace llvm;

std::optional<wasm::ValType> WebAssembly::parseType(StringRef Type) {
  return llvm::StringSwitch<std::optional<wasm::ValType>>{Type}
      .Case("i32", wasm::ValType::I32)
      .Case("i64", wasm::ValType::I64)
      .Case("f32", wasm::ValType::F32)
      .Case("f64", wasm::ValType::F64)
      .Cases("v128", "i8x16", "i16x8", "i32x4", "i64x2", "f32x4", "f64x2",
             wasm::ValType::V128)
      .Case("funcref", wasm::ValType::FUNCREF)
      .Case("externref", wasm::ValType::EXTERNREF)
      .Case("exnref", wasm::ValType::EXNREF)
      .Default(std::nullopt);
}

WebAssembly::BlockType WebAssembly::parseBlockType(StringRef Type) {
  // Multivalue block types are handled separately in parseSignature
  return StringSwitch<WebAssembly::BlockType>(Type)
      .Case("i32", WebAssembly::BlockType::I32)
      .Case("i64", WebAssembly::BlockType::I64)
      .Case("f32", WebAssembly::BlockType::F32)
      .Case("f64", WebAssembly::BlockType::F64)
      .Case("v128", WebAssembly::BlockType::V128)
      .Case("funcref", WebAssembly::BlockType::Funcref)
      .Case("externref", WebAssembly::BlockType::Externref)
      .Case("exnref", WebAssembly::BlockType::Exnref)
      .Case("void", WebAssembly::BlockType::Void)
      .Default(WebAssembly::BlockType::Invalid);
}

// We have various enums representing a subset of these types, use this
// function to convert any of them to text.
const char *WebAssembly::anyTypeToString(unsigned Type) {
  switch (Type) {
  case wasm::WASM_TYPE_I32:
    return "i32";
  case wasm::WASM_TYPE_I64:
    return "i64";
  case wasm::WASM_TYPE_F32:
    return "f32";
  case wasm::WASM_TYPE_F64:
    return "f64";
  case wasm::WASM_TYPE_V128:
    return "v128";
  case wasm::WASM_TYPE_FUNCREF:
    return "funcref";
  case wasm::WASM_TYPE_EXTERNREF:
    return "externref";
  case wasm::WASM_TYPE_EXNREF:
    return "exnref";
  case wasm::WASM_TYPE_FUNC:
    return "func";
  case wasm::WASM_TYPE_NORESULT:
    return "void";
  default:
    return "invalid_type";
  }
}

const char *WebAssembly::typeToString(wasm::ValType Type) {
  return anyTypeToString(static_cast<unsigned>(Type));
}

std::string WebAssembly::typeListToString(ArrayRef<wasm::ValType> List) {
  std::string S;
  for (const auto &Type : List) {
    if (&Type != &List[0])
      S += ", ";
    S += WebAssembly::typeToString(Type);
  }
  return S;
}

std::string WebAssembly::signatureToString(const wasm::WasmSignature *Sig) {
  std::string S("(");
  S += typeListToString(Sig->Params);
  S += ") -> (";
  S += typeListToString(Sig->Returns);
  S += ")";
  return S;
}

wasm::ValType WebAssembly::regClassToValType(unsigned RC) {
  switch (RC) {
  case WebAssembly::I32RegClassID:
    return wasm::ValType::I32;
  case WebAssembly::I64RegClassID:
    return wasm::ValType::I64;
  case WebAssembly::F32RegClassID:
    return wasm::ValType::F32;
  case WebAssembly::F64RegClassID:
    return wasm::ValType::F64;
  case WebAssembly::V128RegClassID:
    return wasm::ValType::V128;
  case WebAssembly::FUNCREFRegClassID:
    return wasm::ValType::FUNCREF;
  case WebAssembly::EXTERNREFRegClassID:
    return wasm::ValType::EXTERNREF;
  case WebAssembly::EXNREFRegClassID:
    return wasm::ValType::EXNREF;
  default:
    llvm_unreachable("unexpected type");
  }
}
