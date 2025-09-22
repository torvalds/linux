//===- Hash.h - PDB hash functions ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_HASH_H
#define LLVM_DEBUGINFO_PDB_NATIVE_HASH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {
namespace pdb {

uint32_t hashStringV1(StringRef Str);
uint32_t hashStringV2(StringRef Str);
uint32_t hashBufferV8(ArrayRef<uint8_t> Data);

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_HASH_H
