//===-- NVPTXBaseInfo.h - Top-level definitions for NVPTX -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the NVPTX target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXBASEINFO_H
#define LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXBASEINFO_H

namespace llvm {

enum AddressSpace {
  ADDRESS_SPACE_GENERIC = 0,
  ADDRESS_SPACE_GLOBAL = 1,
  ADDRESS_SPACE_SHARED = 3,
  ADDRESS_SPACE_CONST = 4,
  ADDRESS_SPACE_LOCAL = 5,

  // NVVM Internal
  ADDRESS_SPACE_PARAM = 101
};

namespace NVPTXII {
enum {
  // These must be kept in sync with TSFlags in NVPTXInstrFormats.td
  IsTexFlag = 0x80,
  IsSuldMask = 0x300,
  IsSuldShift = 8,
  IsSustFlag = 0x400,
  IsSurfTexQueryFlag = 0x800,
  IsTexModeUnifiedFlag = 0x1000
};
} // namespace NVPTXII

} // namespace llvm
#endif
