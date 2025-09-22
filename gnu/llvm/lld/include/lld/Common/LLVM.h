//===--- LLVM.h - Import various common LLVM datatypes ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file forward declares and imports various common LLVM datatypes that
// lld wants to use unqualified.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_LLVM_H
#define LLD_COMMON_LLVM_H

// This should be the only #include, force #includes of all the others on
// clients.
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <utility>

namespace llvm {
// ADT's.
class raw_ostream;
class Error;
class StringRef;
class Twine;
class MemoryBuffer;
class MemoryBufferRef;
template <typename T> class ArrayRef;
template <typename T> class MutableArrayRef;
template <unsigned InternalLen> class SmallString;
template <typename T, unsigned N> class SmallVector;
template <typename T> class ErrorOr;
template <typename T> class Expected;

namespace object {
class WasmObjectFile;
struct WasmSection;
struct WasmSegment;
class WasmSymbol;
} // namespace object

namespace wasm {
struct WasmTag;
struct WasmFunction;
struct WasmGlobal;
struct WasmGlobalType;
struct WasmInitExpr;
struct WasmLimits;
struct WasmRelocation;
struct WasmSignature;
struct WasmTable;
struct WasmTableType;
} // namespace wasm
} // namespace llvm

namespace lld {
llvm::raw_ostream &outs();
llvm::raw_ostream &errs();

// Casting operators.
using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::isa;

// ADT's.
using llvm::ArrayRef;
using llvm::MutableArrayRef;
using llvm::Error;
using llvm::ErrorOr;
using llvm::Expected;
using llvm::MemoryBuffer;
using llvm::MemoryBufferRef;
using llvm::raw_ostream;
using llvm::SmallString;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;

using llvm::object::WasmObjectFile;
using llvm::object::WasmSection;
using llvm::object::WasmSegment;
using llvm::object::WasmSymbol;
using llvm::wasm::WasmFunction;
using llvm::wasm::WasmGlobal;
using llvm::wasm::WasmGlobalType;
using llvm::wasm::WasmInitExpr;
using llvm::wasm::WasmLimits;
using llvm::wasm::WasmRelocation;
using llvm::wasm::WasmSignature;
using llvm::wasm::WasmTable;
using llvm::wasm::WasmTableType;
using llvm::wasm::WasmTag;
} // end namespace lld.

namespace std {
template <> struct hash<llvm::StringRef> {
public:
  size_t operator()(const llvm::StringRef &s) const {
    return llvm::hash_value(s);
  }
};
} // namespace std

#endif
