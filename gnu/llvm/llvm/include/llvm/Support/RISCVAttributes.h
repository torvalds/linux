//===-- RISCVAttributes.h - RISCV Attributes --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains enumerations for RISCV attributes as defined in RISC-V
// ELF psABI specification.
//
// RISC-V ELF psABI specification
//
// https://github.com/riscv/riscv-elf-psabi-doc/blob/master/riscv-elf.md
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_RISCVATTRIBUTES_H
#define LLVM_SUPPORT_RISCVATTRIBUTES_H

#include "llvm/Support/ELFAttributes.h"

namespace llvm {
namespace RISCVAttrs {

const TagNameMap &getRISCVAttributeTags();

enum AttrType : unsigned {
  // Attribute types in ELF/.riscv.attributes.
  STACK_ALIGN = 4,
  ARCH = 5,
  UNALIGNED_ACCESS = 6,
  PRIV_SPEC = 8,
  PRIV_SPEC_MINOR = 10,
  PRIV_SPEC_REVISION = 12,
  ATOMIC_ABI = 14,
};

enum class RISCVAtomicAbiTag : unsigned {
  // Values for Tag_RISCV_atomic_abi
  // Defined at
  // https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-elf.adoc#tag_riscv_atomic_abi-14-uleb128version
  UNKNOWN = 0,
  A6C = 1,
  A6S = 2,
  A7 = 3,
};

enum { NOT_ALLOWED = 0, ALLOWED = 1 };

} // namespace RISCVAttrs
} // namespace llvm

#endif
