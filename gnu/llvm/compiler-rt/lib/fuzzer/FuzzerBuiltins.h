//===- FuzzerBuiltins.h - Internal header for builtins ----------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Wrapper functions and marcos around builtin functions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_BUILTINS_H
#define LLVM_FUZZER_BUILTINS_H

#include "FuzzerPlatform.h"

#if !LIBFUZZER_MSVC
#include <cstdint>

#define GET_CALLER_PC() __builtin_return_address(0)

namespace fuzzer {

inline uint8_t  Bswap(uint8_t x)  { return x; }
inline uint16_t Bswap(uint16_t x) { return __builtin_bswap16(x); }
inline uint32_t Bswap(uint32_t x) { return __builtin_bswap32(x); }
inline uint64_t Bswap(uint64_t x) { return __builtin_bswap64(x); }

inline uint32_t Clzll(unsigned long long X) { return __builtin_clzll(X); }
inline int Popcountll(unsigned long long X) { return __builtin_popcountll(X); }

}  // namespace fuzzer

#endif  // !LIBFUZZER_MSVC
#endif  // LLVM_FUZZER_BUILTINS_H
