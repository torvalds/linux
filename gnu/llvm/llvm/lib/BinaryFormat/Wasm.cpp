//===-- llvm/BinaryFormat/Wasm.cpp -------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/Wasm.h"

llvm::StringRef llvm::wasm::toString(wasm::WasmSymbolType Type) {
  switch (Type) {
  case wasm::WASM_SYMBOL_TYPE_FUNCTION:
    return "WASM_SYMBOL_TYPE_FUNCTION";
  case wasm::WASM_SYMBOL_TYPE_GLOBAL:
    return "WASM_SYMBOL_TYPE_GLOBAL";
  case wasm::WASM_SYMBOL_TYPE_TABLE:
    return "WASM_SYMBOL_TYPE_TABLE";
  case wasm::WASM_SYMBOL_TYPE_DATA:
    return "WASM_SYMBOL_TYPE_DATA";
  case wasm::WASM_SYMBOL_TYPE_SECTION:
    return "WASM_SYMBOL_TYPE_SECTION";
  case wasm::WASM_SYMBOL_TYPE_TAG:
    return "WASM_SYMBOL_TYPE_TAG";
  }
  llvm_unreachable("unknown symbol type");
}

llvm::StringRef llvm::wasm::relocTypetoString(uint32_t Type) {
  switch (Type) {
#define WASM_RELOC(NAME, VALUE)                                                \
  case VALUE:                                                                  \
    return #NAME;
#include "llvm/BinaryFormat/WasmRelocs.def"
#undef WASM_RELOC
  default:
    llvm_unreachable("unknown reloc type");
  }
}

llvm::StringRef llvm::wasm::sectionTypeToString(uint32_t Type) {
#define ECase(X)                                                               \
  case wasm::WASM_SEC_##X:                                                     \
    return #X;
  switch (Type) {
    ECase(CUSTOM);
    ECase(TYPE);
    ECase(IMPORT);
    ECase(FUNCTION);
    ECase(TABLE);
    ECase(MEMORY);
    ECase(GLOBAL);
    ECase(EXPORT);
    ECase(START);
    ECase(ELEM);
    ECase(CODE);
    ECase(DATA);
    ECase(DATACOUNT);
    ECase(TAG);
  default:
    llvm_unreachable("unknown section type");
  }
#undef ECase
}

bool llvm::wasm::relocTypeHasAddend(uint32_t Type) {
  switch (Type) {
  case R_WASM_MEMORY_ADDR_LEB:
  case R_WASM_MEMORY_ADDR_LEB64:
  case R_WASM_MEMORY_ADDR_SLEB:
  case R_WASM_MEMORY_ADDR_SLEB64:
  case R_WASM_MEMORY_ADDR_REL_SLEB:
  case R_WASM_MEMORY_ADDR_REL_SLEB64:
  case R_WASM_MEMORY_ADDR_I32:
  case R_WASM_MEMORY_ADDR_I64:
  case R_WASM_MEMORY_ADDR_TLS_SLEB:
  case R_WASM_MEMORY_ADDR_TLS_SLEB64:
  case R_WASM_FUNCTION_OFFSET_I32:
  case R_WASM_FUNCTION_OFFSET_I64:
  case R_WASM_SECTION_OFFSET_I32:
  case R_WASM_MEMORY_ADDR_LOCREL_I32:
    return true;
  default:
    return false;
  }
}
