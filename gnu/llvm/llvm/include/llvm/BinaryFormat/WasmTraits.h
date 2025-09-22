//===- WasmTraits.h - DenseMap traits for the Wasm structures ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides llvm::DenseMapInfo traits for the Wasm structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_WASMTRAITS_H
#define LLVM_BINARYFORMAT_WASMTRAITS_H

#include "llvm/ADT/Hashing.h"
#include "llvm/BinaryFormat/Wasm.h"

namespace llvm {

// Traits for using WasmSignature in a DenseMap.
template <> struct DenseMapInfo<wasm::WasmSignature, void> {
  static wasm::WasmSignature getEmptyKey() {
    wasm::WasmSignature Sig;
    Sig.State = wasm::WasmSignature::Empty;
    return Sig;
  }
  static wasm::WasmSignature getTombstoneKey() {
    wasm::WasmSignature Sig;
    Sig.State = wasm::WasmSignature::Tombstone;
    return Sig;
  }
  static unsigned getHashValue(const wasm::WasmSignature &Sig) {
    uintptr_t H = hash_value(Sig.State);
    for (auto Ret : Sig.Returns)
      H = hash_combine(H, Ret);
    for (auto Param : Sig.Params)
      H = hash_combine(H, Param);
    return H;
  }
  static bool isEqual(const wasm::WasmSignature &LHS,
                      const wasm::WasmSignature &RHS) {
    return LHS == RHS;
  }
};

// Traits for using WasmGlobalType in a DenseMap
template <> struct DenseMapInfo<wasm::WasmGlobalType, void> {
  static wasm::WasmGlobalType getEmptyKey() {
    return wasm::WasmGlobalType{1, true};
  }
  static wasm::WasmGlobalType getTombstoneKey() {
    return wasm::WasmGlobalType{2, true};
  }
  static unsigned getHashValue(const wasm::WasmGlobalType &GlobalType) {
    return hash_combine(GlobalType.Type, GlobalType.Mutable);
  }
  static bool isEqual(const wasm::WasmGlobalType &LHS,
                      const wasm::WasmGlobalType &RHS) {
    return LHS == RHS;
  }
};

// Traits for using WasmLimits in a DenseMap
template <> struct DenseMapInfo<wasm::WasmLimits, void> {
  static wasm::WasmLimits getEmptyKey() {
    return wasm::WasmLimits{0xff, 0xff, 0xff};
  }
  static wasm::WasmLimits getTombstoneKey() {
    return wasm::WasmLimits{0xee, 0xee, 0xee};
  }
  static unsigned getHashValue(const wasm::WasmLimits &Limits) {
    unsigned Hash = hash_value(Limits.Flags);
    Hash = hash_combine(Hash, Limits.Minimum);
    if (Limits.Flags & llvm::wasm::WASM_LIMITS_FLAG_HAS_MAX) {
      Hash = hash_combine(Hash, Limits.Maximum);
    }
    return Hash;
  }
  static bool isEqual(const wasm::WasmLimits &LHS,
                      const wasm::WasmLimits &RHS) {
    return LHS == RHS;
  }
};

// Traits for using WasmTableType in a DenseMap
template <> struct DenseMapInfo<wasm::WasmTableType, void> {
  static wasm::WasmTableType getEmptyKey() {
    return wasm::WasmTableType{
        wasm::ValType(0), DenseMapInfo<wasm::WasmLimits, void>::getEmptyKey()};
  }
  static wasm::WasmTableType getTombstoneKey() {
    return wasm::WasmTableType{
        wasm::ValType(1),
        DenseMapInfo<wasm::WasmLimits, void>::getTombstoneKey()};
  }
  static unsigned getHashValue(const wasm::WasmTableType &TableType) {
    return hash_combine(
        TableType.ElemType,
        DenseMapInfo<wasm::WasmLimits, void>::getHashValue(TableType.Limits));
  }
  static bool isEqual(const wasm::WasmTableType &LHS,
                      const wasm::WasmTableType &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // LLVM_BINARYFORMAT_WASMTRAITS_H
