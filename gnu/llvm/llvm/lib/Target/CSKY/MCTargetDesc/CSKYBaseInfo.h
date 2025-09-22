//===-- CSKYBaseInfo.h - Top level definitions for CSKY ---*- C++ -*-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the CSKY target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYBASEINFO_H
#define LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYBASEINFO_H

#include "MCTargetDesc/CSKYMCTargetDesc.h"
#include "llvm/MC/MCInstrDesc.h"

namespace llvm {

// CSKYII - This namespace holds all of the target specific flags that
// instruction info tracks. All definitions must match CSKYInstrFormats.td.
namespace CSKYII {

enum AddrMode {
  AddrModeNone = 0,
  AddrMode32B = 1,   // ld32.b, ld32.bs, st32.b, st32.bs, +4kb
  AddrMode32H = 2,   // ld32.h, ld32.hs, st32.h, st32.hs, +8kb
  AddrMode32WD = 3,  // ld32.w, st32.w, ld32.d, st32.d, +16kb
  AddrMode16B = 4,   // ld16.b, +32b
  AddrMode16H = 5,   // ld16.h, +64b
  AddrMode16W = 6,   // ld16.w, +128b or +1kb
  AddrMode32SDF = 7, // flds, fldd, +1kb
};

// CSKY Specific MachineOperand Flags.
enum TOF {
  MO_None = 0,
  MO_ADDR32,
  MO_GOT32,
  MO_GOTOFF,
  MO_PLT32,
  MO_ADDR_HI16,
  MO_ADDR_LO16,

  // Used to differentiate between target-specific "direct" flags and "bitmask"
  // flags. A machine operand can only have one "direct" flag, but can have
  // multiple "bitmask" flags.
  MO_DIRECT_FLAG_MASK = 15
};

enum {
  AddrModeMask = 0x1f,
};

} // namespace CSKYII

namespace CSKYOp {
enum OperandType : unsigned {
  OPERAND_BARESYMBOL = MCOI::OPERAND_FIRST_TARGET,
  OPERAND_CONSTPOOL
};
} // namespace CSKYOp

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_MCTARGETDESC_CSKYBASEINFO_H
