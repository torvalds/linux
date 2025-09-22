//===-- llvm/BinaryFormat/Swift.h ---Swift Constants-------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//

#ifndef LLVM_BINARYFORMAT_SWIFT_H
#define LLVM_BINARYFORMAT_SWIFT_H

namespace llvm {
namespace binaryformat {

enum Swift5ReflectionSectionKind {
#define HANDLE_SWIFT_SECTION(KIND, MACHO, ELF, COFF) KIND,
#include "llvm/BinaryFormat/Swift.def"
#undef HANDLE_SWIFT_SECTION
  unknown,
  last = unknown
};
} // end of namespace binaryformat
} // end of namespace llvm

#endif
