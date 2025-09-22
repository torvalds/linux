//===- HeaderMapTypes.h - Types for the header map format -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_HEADERMAPTYPES_H
#define LLVM_CLANG_LEX_HEADERMAPTYPES_H

#include <cstdint>

namespace clang {

enum {
  HMAP_HeaderMagicNumber = ('h' << 24) | ('m' << 16) | ('a' << 8) | 'p',
  HMAP_HeaderVersion = 1,
  HMAP_EmptyBucketKey = 0
};

struct HMapBucket {
  uint32_t Key;    // Offset (into strings) of key.
  uint32_t Prefix; // Offset (into strings) of value prefix.
  uint32_t Suffix; // Offset (into strings) of value suffix.
};

struct HMapHeader {
  uint32_t Magic;          // Magic word, also indicates byte order.
  uint16_t Version;        // Version number -- currently 1.
  uint16_t Reserved;       // Reserved for future use - zero for now.
  uint32_t StringsOffset;  // Offset to start of string pool.
  uint32_t NumEntries;     // Number of entries in the string table.
  uint32_t NumBuckets;     // Number of buckets (always a power of 2).
  uint32_t MaxValueLength; // Length of longest result path (excluding nul).
  // An array of 'NumBuckets' HMapBucket objects follows this header.
  // Strings follow the buckets, at StringsOffset.
};

} // end namespace clang.

#endif
