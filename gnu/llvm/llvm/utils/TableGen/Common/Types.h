//===- Types.h - Helper for the selection of C++ types. ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_TYPES_H
#define LLVM_UTILS_TABLEGEN_TYPES_H

#include <cstdint>

namespace llvm {
/// Returns the smallest unsigned integer type that can hold the given range.
/// MaxSize indicates the largest size of integer to consider (in bits) and only
/// supports values of at least 32.
const char *getMinimalTypeForRange(uint64_t Range, unsigned MaxSize = 64);
} // namespace llvm

#endif
