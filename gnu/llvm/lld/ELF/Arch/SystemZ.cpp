//===- SystemZ.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class SystemZ : public TargetInfo {
public:
  SystemZ();
  int getTlsGdRelaxSkip(RelType type) const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override;
  void writeGotHeader(uint8_t *buf) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writeIgotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void addPltHeaderSymbols(InputSection &isd) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  RelExpr adjustTlsExpr(RelType type, RelExpr expr) const override;
  RelExpr adjustGotPcExpr(RelType type, int64_t addend,
                          const uint8_t *loc) const override;
  bool relaxOnce(int pass) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;

private:
  void relaxGot(uint8_t *loc, const Relocation &rel, uint64_t val) const;
  void relaxTlsGdToIe(uint8_t *loc, const Relocation &rel, uint64_t val) const;
  void relaxTlsGdToLe(uint8_t *loc, const Relocation &rel, uint64_t val) const;
  void relaxTlsLdToLe(uint8_t *loc, const Relocation &rel, uint64_t val) const;
};
} // namespace

SystemZ::SystemZ() {
  copyRel = R_390_COPY;
  gotRel = R_390_GLOB_DAT;
  pltRel = R_390_JMP_SLOT;
  relativeRel = R_390_RELATIVE;
  iRelativeRel = R_390_IRELATIVE;
  symbolicRel = R_390_64;
  tlsGotRel = R_390_TLS_TPOFF;
  tlsModuleIndexRel = R_390_TLS_DTPMOD;
  tlsOffsetRel = R_390_TLS_DTPOFF;
  gotHeaderEntriesNum = 3;
  gotPltHeaderEntriesNum = 0;
  gotEntrySize = 8;
  pltHeaderSize = 32;
  pltEntrySize = 32;
  ipltEntrySize = 32;

  // This "trap instruction" is used to fill gaps between sections.
  // On SystemZ, the behavior of the GNU ld is to fill those gaps
  // with nop instructions instead - and unfortunately the default
  // glibc crt object files (used to) rely on that behavior since
  // they use an alignment on the .init section fragments that causes
  // gaps which must be filled with nops as they are being executed.
  // Therefore, we provide a nop instruction as "trapInstr" here.
  trapInstr = {0x07, 0x07, 0x07, 0x07};

  defaultImageBase = 0x1000000;
}

RelExpr SystemZ::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  switch (type) {
  case R_390_NONE:
    return R_NONE;
  // Relocations targeting the symbol value.
  case R_390_8:
  case R_390_12:
  case R_390_16:
  case R_390_20:
  case R_390_32:
  case R_390_64:
    return R_ABS;
  case R_390_PC16:
  case R_390_PC32:
  case R_390_PC64:
  case R_390_PC12DBL:
  case R_390_PC16DBL:
  case R_390_PC24DBL:
  case R_390_PC32DBL:
    return R_PC;
  case R_390_GOTOFF16:
  case R_390_GOTOFF: // a.k.a. R_390_GOTOFF32
  case R_390_GOTOFF64:
    return R_GOTREL;
  // Relocations targeting the PLT associated with the symbol.
  case R_390_PLT32:
  case R_390_PLT64:
  case R_390_PLT12DBL:
  case R_390_PLT16DBL:
  case R_390_PLT24DBL:
  case R_390_PLT32DBL:
    return R_PLT_PC;
  case R_390_PLTOFF16:
  case R_390_PLTOFF32:
  case R_390_PLTOFF64:
    return R_PLT_GOTREL;
  // Relocations targeting the GOT entry associated with the symbol.
  case R_390_GOTENT:
    return R_GOT_PC;
  case R_390_GOT12:
  case R_390_GOT16:
  case R_390_GOT20:
  case R_390_GOT32:
  case R_390_GOT64:
    return R_GOT_OFF;
  // Relocations targeting the GOTPLT entry associated with the symbol.
  case R_390_GOTPLTENT:
    return R_GOTPLT_PC;
  case R_390_GOTPLT12:
  case R_390_GOTPLT16:
  case R_390_GOTPLT20:
  case R_390_GOTPLT32:
  case R_390_GOTPLT64:
    return R_GOTPLT_GOTREL;
  // Relocations targeting _GLOBAL_OFFSET_TABLE_.
  case R_390_GOTPC:
  case R_390_GOTPCDBL:
    return R_GOTONLY_PC;
  // TLS-related relocations.
  case R_390_TLS_LOAD:
    return R_NONE;
  case R_390_TLS_GDCALL:
    return R_TLSGD_PC;
  case R_390_TLS_LDCALL:
    return R_TLSLD_PC;
  case R_390_TLS_GD32:
  case R_390_TLS_GD64:
    return R_TLSGD_GOT;
  case R_390_TLS_LDM32:
  case R_390_TLS_LDM64:
    return R_TLSLD_GOT;
  case R_390_TLS_LDO32:
  case R_390_TLS_LDO64:
    return R_DTPREL;
  case R_390_TLS_LE32:
  case R_390_TLS_LE64:
    return R_TPREL;
  case R_390_TLS_IE32:
  case R_390_TLS_IE64:
    return R_GOT;
  case R_390_TLS_GOTIE12:
  case R_390_TLS_GOTIE20:
  case R_390_TLS_GOTIE32:
  case R_390_TLS_GOTIE64:
    return R_GOT_OFF;
  case R_390_TLS_IEENT:
    return R_GOT_PC;

  default:
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

void SystemZ::writeGotHeader(uint8_t *buf) const {
  // _GLOBAL_OFFSET_TABLE_[0] holds the value of _DYNAMIC.
  // _GLOBAL_OFFSET_TABLE_[1] and [2] are reserved.
  write64be(buf, mainPart->dynamic->getVA());
}

void SystemZ::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  write64be(buf, s.getPltVA() + 14);
}

void SystemZ::writeIgotPlt(uint8_t *buf, const Symbol &s) const {
  if (config->writeAddends)
    write64be(buf, s.getVA());
}

void SystemZ::writePltHeader(uint8_t *buf) const {
  const uint8_t pltData[] = {
      0xe3, 0x10, 0xf0, 0x38, 0x00, 0x24, // stg     %r1,56(%r15)
      0xc0, 0x10, 0x00, 0x00, 0x00, 0x00, // larl    %r1,_GLOBAL_OFFSET_TABLE_
      0xd2, 0x07, 0xf0, 0x30, 0x10, 0x08, // mvc     48(8,%r15),8(%r1)
      0xe3, 0x10, 0x10, 0x10, 0x00, 0x04, // lg      %r1,16(%r1)
      0x07, 0xf1,                         // br      %r1
      0x07, 0x00,                         // nopr
      0x07, 0x00,                         // nopr
      0x07, 0x00,                         // nopr
  };
  memcpy(buf, pltData, sizeof(pltData));
  uint64_t got = in.got->getVA();
  uint64_t plt = in.plt->getVA();
  write32be(buf + 8, (got - plt - 6) >> 1);
}

void SystemZ::addPltHeaderSymbols(InputSection &isec) const {
  // The PLT header needs a reference to _GLOBAL_OFFSET_TABLE_, so we
  // must ensure the .got section is created even if otherwise unused.
  in.got->hasGotOffRel.store(true, std::memory_order_relaxed);
}

void SystemZ::writePlt(uint8_t *buf, const Symbol &sym,
                       uint64_t pltEntryAddr) const {
  const uint8_t inst[] = {
      0xc0, 0x10, 0x00, 0x00, 0x00, 0x00, // larl    %r1,<.got.plt slot>
      0xe3, 0x10, 0x10, 0x00, 0x00, 0x04, // lg      %r1,0(%r1)
      0x07, 0xf1,                         // br      %r1
      0x0d, 0x10,                         // basr    %r1,%r0
      0xe3, 0x10, 0x10, 0x0c, 0x00, 0x14, // lgf     %r1,12(%r1)
      0xc0, 0xf4, 0x00, 0x00, 0x00, 0x00, // jg      <plt header>
      0x00, 0x00, 0x00, 0x00,             // <relocation offset>
  };
  memcpy(buf, inst, sizeof(inst));

  write32be(buf + 2, (sym.getGotPltVA() - pltEntryAddr) >> 1);
  write32be(buf + 24, (in.plt->getVA() - pltEntryAddr - 22) >> 1);
  write32be(buf + 28, in.relaPlt->entsize * sym.getPltIdx());
}

int64_t SystemZ::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_390_8:
    return SignExtend64<8>(*buf);
  case R_390_16:
  case R_390_PC16:
    return SignExtend64<16>(read16be(buf));
  case R_390_PC16DBL:
    return SignExtend64<16>(read16be(buf)) << 1;
  case R_390_32:
  case R_390_PC32:
    return SignExtend64<32>(read32be(buf));
  case R_390_PC32DBL:
    return SignExtend64<32>(read32be(buf)) << 1;
  case R_390_64:
  case R_390_PC64:
  case R_390_TLS_DTPMOD:
  case R_390_TLS_DTPOFF:
  case R_390_TLS_TPOFF:
  case R_390_GLOB_DAT:
  case R_390_RELATIVE:
  case R_390_IRELATIVE:
    return read64be(buf);
  case R_390_COPY:
  case R_390_JMP_SLOT:
  case R_390_NONE:
    // These relocations are defined as not having an implicit addend.
    return 0;
  default:
    internalLinkerError(getErrorLocation(buf),
                        "cannot read addend for relocation " + toString(type));
    return 0;
  }
}

RelType SystemZ::getDynRel(RelType type) const {
  if (type == R_390_64 || type == R_390_PC64)
    return type;
  return R_390_NONE;
}

RelExpr SystemZ::adjustTlsExpr(RelType type, RelExpr expr) const {
  if (expr == R_RELAX_TLS_GD_TO_IE)
    return R_RELAX_TLS_GD_TO_IE_GOT_OFF;
  return expr;
}

int SystemZ::getTlsGdRelaxSkip(RelType type) const {
  // A __tls_get_offset call instruction is marked with 2 relocations:
  //
  //   R_390_TLS_GDCALL / R_390_TLS_LDCALL: marker relocation
  //   R_390_PLT32DBL: __tls_get_offset
  //
  // After the relaxation we no longer call __tls_get_offset and should skip
  // both relocations to not create a false dependence on __tls_get_offset
  // being defined.
  //
  // Note that this mechanism only works correctly if the R_390_TLS_[GL]DCALL
  // is seen immediately *before* the R_390_PLT32DBL.  Unfortunately, current
  // compilers on the platform will typically generate the inverse sequence.
  // To fix this, we sort relocations by offset in RelocationScanner::scan;
  // this ensures the correct sequence as the R_390_TLS_[GL]DCALL applies to
  // the first byte of the brasl instruction, while the R_390_PLT32DBL applies
  // to its third byte (the relative displacement).

  if (type == R_390_TLS_GDCALL || type == R_390_TLS_LDCALL)
    return 2;
  return 1;
}

void SystemZ::relaxTlsGdToIe(uint8_t *loc, const Relocation &rel,
                             uint64_t val) const {
  // The general-dynamic code sequence for a global `x`:
  //
  // Instruction                      Relocation       Symbol
  // ear %rX,%a0
  // sllg %rX,%rX,32
  // ear %rX,%a1
  // larl %r12,_GLOBAL_OFFSET_TABLE_  R_390_GOTPCDBL   _GLOBAL_OFFSET_TABLE_
  // lgrl %r2,.LC0                    R_390_PC32DBL    .LC0
  // brasl %r14,__tls_get_offset@plt  R_390_TLS_GDCALL x
  //            :tls_gdcall:x         R_390_PLT32DBL   __tls_get_offset
  // la %r2,0(%r2,%rX)
  //
  // .LC0:
  // .quad   x@TLSGD                  R_390_TLS_GD64   x
  //
  // Relaxing to initial-exec entails:
  // 1) Replacing the call by a load from the GOT.
  // 2) Replacing the relocation on the constant LC0 by R_390_TLS_GOTIE64.

  switch (rel.type) {
  case R_390_TLS_GDCALL:
    // brasl %r14,__tls_get_offset@plt -> lg %r2,0(%r2,%r12)
    write16be(loc, 0xe322);
    write32be(loc + 2, 0xc0000004);
    break;
  case R_390_TLS_GD64:
    relocateNoSym(loc, R_390_TLS_GOTIE64, val);
    break;
  default:
    llvm_unreachable("unsupported relocation for TLS GD to IE relaxation");
  }
}

void SystemZ::relaxTlsGdToLe(uint8_t *loc, const Relocation &rel,
                             uint64_t val) const {
  // The general-dynamic code sequence for a global `x`:
  //
  // Instruction                      Relocation       Symbol
  // ear %rX,%a0
  // sllg %rX,%rX,32
  // ear %rX,%a1
  // larl %r12,_GLOBAL_OFFSET_TABLE_  R_390_GOTPCDBL   _GLOBAL_OFFSET_TABLE_
  // lgrl %r2,.LC0                    R_390_PC32DBL    .LC0
  // brasl %r14,__tls_get_offset@plt  R_390_TLS_GDCALL x
  //            :tls_gdcall:x         R_390_PLT32DBL   __tls_get_offset
  // la %r2,0(%r2,%rX)
  //
  // .LC0:
  // .quad   x@tlsgd                  R_390_TLS_GD64   x
  //
  // Relaxing to local-exec entails:
  // 1) Replacing the call by a nop.
  // 2) Replacing the relocation on the constant LC0 by R_390_TLS_LE64.

  switch (rel.type) {
  case R_390_TLS_GDCALL:
    // brasl %r14,__tls_get_offset@plt -> brcl 0,.
    write16be(loc, 0xc004);
    write32be(loc + 2, 0x00000000);
    break;
  case R_390_TLS_GD64:
    relocateNoSym(loc, R_390_TLS_LE64, val);
    break;
  default:
    llvm_unreachable("unsupported relocation for TLS GD to LE relaxation");
  }
}

void SystemZ::relaxTlsLdToLe(uint8_t *loc, const Relocation &rel,
                             uint64_t val) const {
  // The local-dynamic code sequence for a global `x`:
  //
  // Instruction                      Relocation       Symbol
  // ear %rX,%a0
  // sllg %rX,%rX,32
  // ear %rX,%a1
  // larl %r12,_GLOBAL_OFFSET_TABLE_  R_390_GOTPCDBL   _GLOBAL_OFFSET_TABLE_
  // lgrl %r2,.LC0                    R_390_PC32DBL    .LC0
  // brasl %r14,__tls_get_offset@plt  R_390_TLS_LDCALL <sym>
  //            :tls_ldcall:<sym>     R_390_PLT32DBL   __tls_get_offset
  // la %r2,0(%r2,%rX)
  // lgrl %rY,.LC1                    R_390_PC32DBL    .LC1
  // la %r2,0(%r2,%rY)
  //
  // .LC0:
  // .quad   <sym>@tlsldm             R_390_TLS_LDM64  <sym>
  // .LC1:
  // .quad   x@dtpoff                 R_390_TLS_LDO64  x
  //
  // Relaxing to local-exec entails:
  // 1) Replacing the call by a nop.
  // 2) Replacing the constant LC0 by 0 (i.e. ignoring the relocation).
  // 3) Replacing the relocation on the constant LC1 by R_390_TLS_LE64.

  switch (rel.type) {
  case R_390_TLS_LDCALL:
    // brasl %r14,__tls_get_offset@plt -> brcl 0,.
    write16be(loc, 0xc004);
    write32be(loc + 2, 0x00000000);
    break;
  case R_390_TLS_LDM64:
    break;
  case R_390_TLS_LDO64:
    relocateNoSym(loc, R_390_TLS_LE64, val);
    break;
  default:
    llvm_unreachable("unsupported relocation for TLS LD to LE relaxation");
  }
}

RelExpr SystemZ::adjustGotPcExpr(RelType type, int64_t addend,
                                 const uint8_t *loc) const {
  // Only R_390_GOTENT with addend 2 can be relaxed.
  if (!config->relax || addend != 2 || type != R_390_GOTENT)
    return R_GOT_PC;
  const uint16_t op = read16be(loc - 2);

  // lgrl rx,sym@GOTENT -> larl rx, sym
  // This relaxation is legal if "sym" binds locally (which was already
  // verified by our caller) and is in-range and properly aligned for a
  // LARL instruction.  We cannot verify the latter constraint here, so
  // we assume it is true and revert the decision later on in relaxOnce
  // if necessary.
  if ((op & 0xff0f) == 0xc408)
    return R_RELAX_GOT_PC;

  return R_GOT_PC;
}

bool SystemZ::relaxOnce(int pass) const {
  // If we decided in adjustGotPcExpr to relax a R_390_GOTENT,
  // we need to validate the target symbol is in-range and aligned.
  SmallVector<InputSection *, 0> storage;
  bool changed = false;
  for (OutputSection *osec : outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      for (Relocation &rel : sec->relocs()) {
        if (rel.expr != R_RELAX_GOT_PC)
          continue;

        uint64_t v = sec->getRelocTargetVA(
            sec->file, rel.type, rel.addend,
            sec->getOutputSection()->addr + rel.offset, *rel.sym, rel.expr);
        if (isInt<33>(v) && !(v & 1))
          continue;
        if (rel.sym->auxIdx == 0) {
          rel.sym->allocateAux();
          addGotEntry(*rel.sym);
          changed = true;
        }
        rel.expr = R_GOT_PC;
      }
    }
  }
  return changed;
}

void SystemZ::relaxGot(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  assert(isInt<33>(val) &&
         "R_390_GOTENT should not have been relaxed if it overflows");
  assert(!(val & 1) &&
         "R_390_GOTENT should not have been relaxed if it is misaligned");
  const uint16_t op = read16be(loc - 2);

  // lgrl rx,sym@GOTENT -> larl rx, sym
  if ((op & 0xff0f) == 0xc408) {
    write16be(loc - 2, 0xc000 | (op & 0x00f0));
    write32be(loc, val >> 1);
  }
}

void SystemZ::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  switch (rel.expr) {
  case R_RELAX_GOT_PC:
    return relaxGot(loc, rel, val);
  case R_RELAX_TLS_GD_TO_IE_GOT_OFF:
    return relaxTlsGdToIe(loc, rel, val);
  case R_RELAX_TLS_GD_TO_LE:
    return relaxTlsGdToLe(loc, rel, val);
  case R_RELAX_TLS_LD_TO_LE:
    return relaxTlsLdToLe(loc, rel, val);
  default:
    break;
  }
  switch (rel.type) {
  case R_390_8:
    checkIntUInt(loc, val, 8, rel);
    *loc = val;
    break;
  case R_390_12:
  case R_390_GOT12:
  case R_390_GOTPLT12:
  case R_390_TLS_GOTIE12:
    checkUInt(loc, val, 12, rel);
    write16be(loc, (read16be(loc) & 0xF000) | val);
    break;
  case R_390_PC12DBL:
  case R_390_PLT12DBL:
    checkInt(loc, val, 13, rel);
    checkAlignment(loc, val, 2, rel);
    write16be(loc, (read16be(loc) & 0xF000) | ((val >> 1) & 0x0FFF));
    break;
  case R_390_16:
  case R_390_GOT16:
  case R_390_GOTPLT16:
  case R_390_GOTOFF16:
  case R_390_PLTOFF16:
    checkIntUInt(loc, val, 16, rel);
    write16be(loc, val);
    break;
  case R_390_PC16:
    checkInt(loc, val, 16, rel);
    write16be(loc, val);
    break;
  case R_390_PC16DBL:
  case R_390_PLT16DBL:
    checkInt(loc, val, 17, rel);
    checkAlignment(loc, val, 2, rel);
    write16be(loc, val >> 1);
    break;
  case R_390_20:
  case R_390_GOT20:
  case R_390_GOTPLT20:
  case R_390_TLS_GOTIE20:
    checkInt(loc, val, 20, rel);
    write32be(loc, (read32be(loc) & 0xF00000FF) | ((val & 0xFFF) << 16) |
                       ((val & 0xFF000) >> 4));
    break;
  case R_390_PC24DBL:
  case R_390_PLT24DBL:
    checkInt(loc, val, 25, rel);
    checkAlignment(loc, val, 2, rel);
    loc[0] = val >> 17;
    loc[1] = val >> 9;
    loc[2] = val >> 1;
    break;
  case R_390_32:
  case R_390_GOT32:
  case R_390_GOTPLT32:
  case R_390_GOTOFF:
  case R_390_PLTOFF32:
  case R_390_TLS_IE32:
  case R_390_TLS_GOTIE32:
  case R_390_TLS_GD32:
  case R_390_TLS_LDM32:
  case R_390_TLS_LDO32:
  case R_390_TLS_LE32:
    checkIntUInt(loc, val, 32, rel);
    write32be(loc, val);
    break;
  case R_390_PC32:
  case R_390_PLT32:
    checkInt(loc, val, 32, rel);
    write32be(loc, val);
    break;
  case R_390_PC32DBL:
  case R_390_PLT32DBL:
  case R_390_GOTPCDBL:
  case R_390_GOTENT:
  case R_390_GOTPLTENT:
  case R_390_TLS_IEENT:
    checkInt(loc, val, 33, rel);
    checkAlignment(loc, val, 2, rel);
    write32be(loc, val >> 1);
    break;
  case R_390_64:
  case R_390_PC64:
  case R_390_PLT64:
  case R_390_GOT64:
  case R_390_GOTPLT64:
  case R_390_GOTOFF64:
  case R_390_PLTOFF64:
  case R_390_GOTPC:
  case R_390_TLS_IE64:
  case R_390_TLS_GOTIE64:
  case R_390_TLS_GD64:
  case R_390_TLS_LDM64:
  case R_390_TLS_LDO64:
  case R_390_TLS_LE64:
  case R_390_TLS_DTPMOD:
  case R_390_TLS_DTPOFF:
  case R_390_TLS_TPOFF:
    write64be(loc, val);
    break;
  case R_390_TLS_LOAD:
  case R_390_TLS_GDCALL:
  case R_390_TLS_LDCALL:
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

TargetInfo *elf::getSystemZTargetInfo() {
  static SystemZ t;
  return &t;
}
