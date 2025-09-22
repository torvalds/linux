//===-- WebAssemblyTypeUtilities.cpp - WebAssembly Type Utility Functions -===//
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

#include "WebAssemblyTypeUtilities.h"
#include "llvm/ADT/StringSwitch.h"

// Get register classes enum.
#define GET_REGINFO_ENUM
#include "WebAssemblyGenRegisterInfo.inc"

using namespace llvm;

MVT WebAssembly::parseMVT(StringRef Type) {
  return StringSwitch<MVT>(Type)
      .Case("i32", MVT::i32)
      .Case("i64", MVT::i64)
      .Case("f32", MVT::f32)
      .Case("f64", MVT::f64)
      .Case("i64", MVT::i64)
      .Case("v16i8", MVT::v16i8)
      .Case("v8i16", MVT::v8i16)
      .Case("v4i32", MVT::v4i32)
      .Case("v2i64", MVT::v2i64)
      .Case("funcref", MVT::funcref)
      .Case("externref", MVT::externref)
      .Case("exnref", MVT::exnref)
      .Default(MVT::INVALID_SIMPLE_VALUE_TYPE);
}

wasm::ValType WebAssembly::toValType(MVT Type) {
  switch (Type.SimpleTy) {
  case MVT::i32:
    return wasm::ValType::I32;
  case MVT::i64:
    return wasm::ValType::I64;
  case MVT::f32:
    return wasm::ValType::F32;
  case MVT::f64:
    return wasm::ValType::F64;
  case MVT::v16i8:
  case MVT::v8i16:
  case MVT::v4i32:
  case MVT::v2i64:
  case MVT::v8f16:
  case MVT::v4f32:
  case MVT::v2f64:
    return wasm::ValType::V128;
  case MVT::funcref:
    return wasm::ValType::FUNCREF;
  case MVT::externref:
    return wasm::ValType::EXTERNREF;
  case MVT::exnref:
    return wasm::ValType::EXNREF;
  default:
    llvm_unreachable("unexpected type");
  }
}

void WebAssembly::wasmSymbolSetType(MCSymbolWasm *Sym, const Type *GlobalVT,
                                    ArrayRef<MVT> VTs) {
  assert(!Sym->getType());

  // Tables are represented as Arrays in LLVM IR therefore
  // they reach this point as aggregate Array types with an element type
  // that is a reference type.
  wasm::ValType ValTy;
  bool IsTable = false;
  if (WebAssembly::isWebAssemblyTableType(GlobalVT)) {
    IsTable = true;
    const Type *ElTy = GlobalVT->getArrayElementType();
    if (WebAssembly::isWebAssemblyExternrefType(ElTy))
      ValTy = wasm::ValType::EXTERNREF;
    else if (WebAssembly::isWebAssemblyFuncrefType(ElTy))
      ValTy = wasm::ValType::FUNCREF;
    else
      report_fatal_error("unhandled reference type");
  } else if (VTs.size() == 1) {
    ValTy = WebAssembly::toValType(VTs[0]);
  } else
    report_fatal_error("Aggregate globals not yet implemented");

  if (IsTable) {
    Sym->setType(wasm::WASM_SYMBOL_TYPE_TABLE);
    Sym->setTableType(ValTy);
  } else {
    Sym->setType(wasm::WASM_SYMBOL_TYPE_GLOBAL);
    Sym->setGlobalType(wasm::WasmGlobalType{uint8_t(ValTy), /*Mutable=*/true});
  }
}
