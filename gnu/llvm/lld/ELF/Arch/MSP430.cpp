//===- MSP430.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The MSP430 is a 16-bit microcontroller RISC architecture. The instruction set
// has only 27 core instructions orthogonally augmented with a variety
// of addressing modes for source and destination operands. Entire address space
// of MSP430 is 64KB (the extended MSP430X architecture is not considered here).
// A typical MSP430 MCU has several kilobytes of RAM and ROM, plenty
// of peripherals and is generally optimized for a low power consumption.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class MSP430 final : public TargetInfo {
public:
  MSP430();
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

MSP430::MSP430() {
  // mov.b #0, r3
  trapInstr = {0x43, 0x43, 0x43, 0x43};
}

RelExpr MSP430::getRelExpr(RelType type, const Symbol &s,
                           const uint8_t *loc) const {
  switch (type) {
  case R_MSP430_10_PCREL:
  case R_MSP430_16_PCREL:
  case R_MSP430_16_PCREL_BYTE:
  case R_MSP430_2X_PCREL:
  case R_MSP430_RL_PCREL:
  case R_MSP430_SYM_DIFF:
    return R_PC;
  default:
    return R_ABS;
  }
}

void MSP430::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_MSP430_8:
    checkIntUInt(loc, val, 8, rel);
    *loc = val;
    break;
  case R_MSP430_16:
  case R_MSP430_16_PCREL:
  case R_MSP430_16_BYTE:
  case R_MSP430_16_PCREL_BYTE:
    checkIntUInt(loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_MSP430_32:
    checkIntUInt(loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_MSP430_10_PCREL: {
    int16_t offset = ((int16_t)val >> 1) - 1;
    checkInt(loc, offset, 10, rel);
    write16le(loc, (read16le(loc) & 0xFC00) | (offset & 0x3FF));
    break;
  }
  default:
    error(getErrorLocation(loc) + "unrecognized relocation " +
          toString(rel.type));
  }
}

TargetInfo *elf::getMSP430TargetInfo() {
  static MSP430 target;
  return &target;
}
