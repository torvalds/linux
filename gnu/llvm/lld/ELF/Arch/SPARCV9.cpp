//===- SPARCV9.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class SPARCV9 final : public TargetInfo {
public:
  SPARCV9();
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

SPARCV9::SPARCV9() {
  copyRel = R_SPARC_COPY;
  gotRel = R_SPARC_GLOB_DAT;
  pltRel = R_SPARC_JMP_SLOT;
  relativeRel = R_SPARC_RELATIVE;
  symbolicRel = R_SPARC_64;
  pltEntrySize = 32;
  pltHeaderSize = 4 * pltEntrySize;

  defaultCommonPageSize = 8192;
  defaultMaxPageSize = 0x100000;
  defaultImageBase = 0x100000;
}

RelExpr SPARCV9::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  switch (type) {
  case R_SPARC_32:
  case R_SPARC_UA32:
  case R_SPARC_64:
  case R_SPARC_UA64:
  case R_SPARC_H44:
  case R_SPARC_M44:
  case R_SPARC_L44:
  case R_SPARC_HH22:
  case R_SPARC_HM10:
  case R_SPARC_LM22:
  case R_SPARC_HI22:
  case R_SPARC_LO10:
    return R_ABS;
  case R_SPARC_PC10:
  case R_SPARC_PC22:
  case R_SPARC_DISP32:
  case R_SPARC_WDISP30:
    return R_PC;
  case R_SPARC_GOT10:
    return R_GOT_OFF;
  case R_SPARC_GOT22:
    return R_GOT_OFF;
  case R_SPARC_WPLT30:
    return R_PLT_PC;
  case R_SPARC_NONE:
    return R_NONE;
  case R_SPARC_TLS_LE_HIX22:
  case R_SPARC_TLS_LE_LOX10:
    return R_TPREL;
  default:
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

void SPARCV9::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  switch (rel.type) {
  case R_SPARC_32:
  case R_SPARC_UA32:
    // V-word32
    checkUInt(loc, val, 32, rel);
    write32be(loc, val);
    break;
  case R_SPARC_DISP32:
    // V-disp32
    checkInt(loc, val, 32, rel);
    write32be(loc, val);
    break;
  case R_SPARC_WDISP30:
  case R_SPARC_WPLT30:
    // V-disp30
    checkInt(loc, val, 32, rel);
    write32be(loc, (read32be(loc) & ~0x3fffffff) | ((val >> 2) & 0x3fffffff));
    break;
  case R_SPARC_22:
    // V-imm22
    checkUInt(loc, val, 22, rel);
    write32be(loc, (read32be(loc) & ~0x003fffff) | (val & 0x003fffff));
    break;
  case R_SPARC_GOT22:
  case R_SPARC_PC22:
  case R_SPARC_LM22:
    // T-imm22
    write32be(loc, (read32be(loc) & ~0x003fffff) | ((val >> 10) & 0x003fffff));
    break;
  case R_SPARC_HI22:
    // V-imm22
    checkUInt(loc, val >> 10, 22, rel);
    write32be(loc, (read32be(loc) & ~0x003fffff) | ((val >> 10) & 0x003fffff));
    break;
  case R_SPARC_WDISP19:
    // V-disp19
    checkInt(loc, val, 21, rel);
    write32be(loc, (read32be(loc) & ~0x0007ffff) | ((val >> 2) & 0x0007ffff));
    break;
  case R_SPARC_GOT10:
  case R_SPARC_PC10:
    // T-simm10
    write32be(loc, (read32be(loc) & ~0x000003ff) | (val & 0x000003ff));
    break;
  case R_SPARC_LO10:
    // T-simm13
    write32be(loc, (read32be(loc) & ~0x00001fff) | (val & 0x000003ff));
    break;
  case R_SPARC_64:
  case R_SPARC_UA64:
    // V-xword64
    write64be(loc, val);
    break;
  case R_SPARC_HH22:
    // V-imm22
    checkUInt(loc, val >> 42, 22, rel);
    write32be(loc, (read32be(loc) & ~0x003fffff) | ((val >> 42) & 0x003fffff));
    break;
  case R_SPARC_HM10:
    // T-simm13
    write32be(loc, (read32be(loc) & ~0x00001fff) | ((val >> 32) & 0x000003ff));
    break;
  case R_SPARC_H44:
    // V-imm22
    checkUInt(loc, val >> 22, 22, rel);
    write32be(loc, (read32be(loc) & ~0x003fffff) | ((val >> 22) & 0x003fffff));
    break;
  case R_SPARC_M44:
    // T-imm10
    write32be(loc, (read32be(loc) & ~0x000003ff) | ((val >> 12) & 0x000003ff));
    break;
  case R_SPARC_L44:
    // T-imm13
    write32be(loc, (read32be(loc) & ~0x00001fff) | (val & 0x00000fff));
    break;
  case R_SPARC_TLS_LE_HIX22:
    // T-imm22
    write32be(loc, (read32be(loc) & ~0x003fffff) | ((~val >> 10) & 0x003fffff));
    break;
  case R_SPARC_TLS_LE_LOX10:
    // T-simm13
    write32be(loc, (read32be(loc) & ~0x00001fff) | (val & 0x000003ff) | 0x1C00);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

void SPARCV9::writePlt(uint8_t *buf, const Symbol & /*sym*/,
                       uint64_t pltEntryAddr) const {
  const uint8_t pltData[] = {
      0x03, 0x00, 0x00, 0x00, // sethi   (. - .PLT0), %g1
      0x30, 0x68, 0x00, 0x00, // ba,a    %xcc, .PLT1
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00, // nop
      0x01, 0x00, 0x00, 0x00  // nop
  };
  memcpy(buf, pltData, sizeof(pltData));

  uint64_t off = pltEntryAddr - in.plt->getVA();
  relocateNoSym(buf, R_SPARC_22, off);
  relocateNoSym(buf + 4, R_SPARC_WDISP19, -(off + 4 - pltEntrySize));
}

TargetInfo *elf::getSPARCV9TargetInfo() {
  static SPARCV9 target;
  return &target;
}
