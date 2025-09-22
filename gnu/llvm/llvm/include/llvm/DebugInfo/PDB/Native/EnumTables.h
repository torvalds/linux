//===- EnumTables.h - Enum to string conversion tables ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_ENUMTABLES_H
#define LLVM_DEBUGINFO_PDB_NATIVE_ENUMTABLES_H

#include "llvm/ADT/ArrayRef.h"

namespace llvm {
template <typename T> struct EnumEntry;
namespace pdb {
ArrayRef<EnumEntry<uint16_t>> getOMFSegMapDescFlagNames();
}
}

#endif // LLVM_DEBUGINFO_PDB_NATIVE_ENUMTABLES_H
