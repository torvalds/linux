//===- llvm/ADT/StableHashing.h - Utilities for stable hashing * C++ *-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides types and functions for computing and combining stable
// hashes. Stable hashes can be useful for hashing across different modules,
// processes, or compiler runs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STABLEHASHING_H
#define LLVM_ADT_STABLEHASHING_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

/// An opaque object representing a stable hash code. It can be serialized,
/// deserialized, and is stable across processes and executions.
using stable_hash = uint64_t;

// Implementation details
namespace hashing {
namespace detail {

// Stable hashes are based on the 64-bit FNV-1 hash:
// https://en.wikipedia.org/wiki/Fowler-Noll-Vo_hash_function

const uint64_t FNV_PRIME_64 = 1099511628211u;
const uint64_t FNV_OFFSET_64 = 14695981039346656037u;

inline void stable_hash_append(stable_hash &Hash, const char Value) {
  Hash = Hash ^ (Value & 0xFF);
  Hash = Hash * FNV_PRIME_64;
}

inline void stable_hash_append(stable_hash &Hash, stable_hash Value) {
  for (unsigned I = 0; I < 8; ++I) {
    stable_hash_append(Hash, static_cast<char>(Value));
    Value >>= 8;
  }
}

} // namespace detail
} // namespace hashing

inline stable_hash stable_hash_combine(stable_hash A, stable_hash B) {
  stable_hash Hash = hashing::detail::FNV_OFFSET_64;
  hashing::detail::stable_hash_append(Hash, A);
  hashing::detail::stable_hash_append(Hash, B);
  return Hash;
}

inline stable_hash stable_hash_combine(stable_hash A, stable_hash B,
                                       stable_hash C) {
  stable_hash Hash = hashing::detail::FNV_OFFSET_64;
  hashing::detail::stable_hash_append(Hash, A);
  hashing::detail::stable_hash_append(Hash, B);
  hashing::detail::stable_hash_append(Hash, C);
  return Hash;
}

inline stable_hash stable_hash_combine(stable_hash A, stable_hash B,
                                       stable_hash C, stable_hash D) {
  stable_hash Hash = hashing::detail::FNV_OFFSET_64;
  hashing::detail::stable_hash_append(Hash, A);
  hashing::detail::stable_hash_append(Hash, B);
  hashing::detail::stable_hash_append(Hash, C);
  hashing::detail::stable_hash_append(Hash, D);
  return Hash;
}

/// Compute a stable_hash for a sequence of values.
///
/// This hashes a sequence of values. It produces the same stable_hash as
/// 'stable_hash_combine(a, b, c, ...)', but can run over arbitrary sized
/// sequences and is significantly faster given pointers and types which
/// can be hashed as a sequence of bytes.
template <typename InputIteratorT>
stable_hash stable_hash_combine_range(InputIteratorT First,
                                      InputIteratorT Last) {
  stable_hash Hash = hashing::detail::FNV_OFFSET_64;
  for (auto I = First; I != Last; ++I)
    hashing::detail::stable_hash_append(Hash, *I);
  return Hash;
}

inline stable_hash stable_hash_combine_array(const stable_hash *P, size_t C) {
  stable_hash Hash = hashing::detail::FNV_OFFSET_64;
  for (size_t I = 0; I < C; ++I)
    hashing::detail::stable_hash_append(Hash, P[I]);
  return Hash;
}

inline stable_hash stable_hash_combine_string(const StringRef &S) {
  return stable_hash_combine_range(S.begin(), S.end());
}

inline stable_hash stable_hash_combine_string(const char *C) {
  stable_hash Hash = hashing::detail::FNV_OFFSET_64;
  while (*C)
    hashing::detail::stable_hash_append(Hash, *(C++));
  return Hash;
}

} // namespace llvm

#endif
