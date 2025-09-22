//===- FuzzerSHA1.h - Internal header for the SHA1 utils --------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// SHA1 utils.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_SHA1_H
#define LLVM_FUZZER_SHA1_H

#include "FuzzerDefs.h"
#include <cstddef>
#include <stdint.h>

namespace fuzzer {

// Private copy of SHA1 implementation.
static const int kSHA1NumBytes = 20;

// Computes SHA1 hash of 'Len' bytes in 'Data', writes kSHA1NumBytes to 'Out'.
void ComputeSHA1(const uint8_t *Data, size_t Len, uint8_t *Out);

std::string Sha1ToString(const uint8_t Sha1[kSHA1NumBytes]);

std::string Hash(const Unit &U);

}  // namespace fuzzer

#endif  // LLVM_FUZZER_SHA1_H
