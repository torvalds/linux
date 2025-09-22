//===- RISCV.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "llvm/Support/ELFAttributes.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/RISCVAttributeParser.h"
#include "llvm/Support/RISCVAttributes.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/TargetParser/RISCVISAInfo.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {

class RISCV final : public TargetInfo {
public:
  RISCV();
  uint32_t calcEFlags() const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  void writeGotHeader(uint8_t *buf) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writeIgotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  RelType getDynRel(RelType type) const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  void relocateAlloc(InputSectionBase &sec, uint8_t *buf) const override;
  bool relaxOnce(int pass) const override;
  void finalizeRelax(int passes) const override;
};

} // end anonymous namespace

// These are internal relocation numbers for GP relaxation. They aren't part
// of the psABI spec.
#define INTERNAL_R_RISCV_GPREL_I 256
#define INTERNAL_R_RISCV_GPREL_S 257

const uint64_t dtpOffset = 0x800;

namespace {
enum Op {
  ADDI = 0x13,
  AUIPC = 0x17,
  JALR = 0x67,
  LD = 0x3003,
  LUI = 0x37,
  LW = 0x2003,
  SRLI = 0x5013,
  SUB = 0x40000033,
};

enum Reg {
  X_RA = 1,
  X_GP = 3,
  X_TP = 4,
  X_T0 = 5,
  X_T1 = 6,
  X_T2 = 7,
  X_A0 = 10,
  X_T3 = 28,
};
} // namespace

static uint32_t hi20(uint32_t val) { return (val + 0x800) >> 12; }
static uint32_t lo12(uint32_t val) { return val & 4095; }

static uint32_t itype(uint32_t op, uint32_t rd, uint32_t rs1, uint32_t imm) {
  return op | (rd << 7) | (rs1 << 15) | (imm << 20);
}
static uint32_t rtype(uint32_t op, uint32_t rd, uint32_t rs1, uint32_t rs2) {
  return op | (rd << 7) | (rs1 << 15) | (rs2 << 20);
}
static uint32_t utype(uint32_t op, uint32_t rd, uint32_t imm) {
  return op | (rd << 7) | (imm << 12);
}

// Extract bits v[begin:end], where range is inclusive, and begin must be < 63.
static uint32_t extractBits(uint64_t v, uint32_t begin, uint32_t end) {
  return (v & ((1ULL << (begin + 1)) - 1)) >> end;
}

static uint32_t setLO12_I(uint32_t insn, uint32_t imm) {
  return (insn & 0xfffff) | (imm << 20);
}
static uint32_t setLO12_S(uint32_t insn, uint32_t imm) {
  return (insn & 0x1fff07f) | (extractBits(imm, 11, 5) << 25) |
         (extractBits(imm, 4, 0) << 7);
}

RISCV::RISCV() {
  copyRel = R_RISCV_COPY;
  pltRel = R_RISCV_JUMP_SLOT;
  relativeRel = R_RISCV_RELATIVE;
  iRelativeRel = R_RISCV_IRELATIVE;
  if (config->is64) {
    symbolicRel = R_RISCV_64;
    tlsModuleIndexRel = R_RISCV_TLS_DTPMOD64;
    tlsOffsetRel = R_RISCV_TLS_DTPREL64;
    tlsGotRel = R_RISCV_TLS_TPREL64;
  } else {
    symbolicRel = R_RISCV_32;
    tlsModuleIndexRel = R_RISCV_TLS_DTPMOD32;
    tlsOffsetRel = R_RISCV_TLS_DTPREL32;
    tlsGotRel = R_RISCV_TLS_TPREL32;
  }
  gotRel = symbolicRel;
  tlsDescRel = R_RISCV_TLSDESC;

  // .got[0] = _DYNAMIC
  gotHeaderEntriesNum = 1;

  // .got.plt[0] = _dl_runtime_resolve, .got.plt[1] = link_map
  gotPltHeaderEntriesNum = 2;

  pltHeaderSize = 32;
  pltEntrySize = 16;
  ipltEntrySize = 16;
}

static uint32_t getEFlags(InputFile *f) {
  if (config->is64)
    return cast<ObjFile<ELF64LE>>(f)->getObj().getHeader().e_flags;
  return cast<ObjFile<ELF32LE>>(f)->getObj().getHeader().e_flags;
}

uint32_t RISCV::calcEFlags() const {
  // If there are only binary input files (from -b binary), use a
  // value of 0 for the ELF header flags.
  if (ctx.objectFiles.empty())
    return 0;

  uint32_t target = getEFlags(ctx.objectFiles.front());

  for (InputFile *f : ctx.objectFiles) {
    uint32_t eflags = getEFlags(f);
    if (eflags & EF_RISCV_RVC)
      target |= EF_RISCV_RVC;

    if ((eflags & EF_RISCV_FLOAT_ABI) != (target & EF_RISCV_FLOAT_ABI))
      warn(
          toString(f) +
          ": cannot link object files with different floating-point ABI from " +
          toString(ctx.objectFiles[0]));

    if ((eflags & EF_RISCV_RVE) != (target & EF_RISCV_RVE))
      error(toString(f) +
            ": cannot link object files with different EF_RISCV_RVE");
  }

  return target;
}

int64_t RISCV::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  default:
    internalLinkerError(getErrorLocation(buf),
                        "cannot read addend for relocation " + toString(type));
    return 0;
  case R_RISCV_32:
  case R_RISCV_TLS_DTPMOD32:
  case R_RISCV_TLS_DTPREL32:
  case R_RISCV_TLS_TPREL32:
    return SignExtend64<32>(read32le(buf));
  case R_RISCV_64:
  case R_RISCV_TLS_DTPMOD64:
  case R_RISCV_TLS_DTPREL64:
  case R_RISCV_TLS_TPREL64:
    return read64le(buf);
  case R_RISCV_RELATIVE:
  case R_RISCV_IRELATIVE:
    return config->is64 ? read64le(buf) : read32le(buf);
  case R_RISCV_NONE:
  case R_RISCV_JUMP_SLOT:
    // These relocations are defined as not having an implicit addend.
    return 0;
  case R_RISCV_TLSDESC:
    return config->is64 ? read64le(buf + 8) : read32le(buf + 4);
  }
}

void RISCV::writeGotHeader(uint8_t *buf) const {
  if (config->is64)
    write64le(buf, mainPart->dynamic->getVA());
  else
    write32le(buf, mainPart->dynamic->getVA());
}

void RISCV::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  if (config->is64)
    write64le(buf, in.plt->getVA());
  else
    write32le(buf, in.plt->getVA());
}

void RISCV::writeIgotPlt(uint8_t *buf, const Symbol &s) const {
  if (config->writeAddends) {
    if (config->is64)
      write64le(buf, s.getVA());
    else
      write32le(buf, s.getVA());
  }
}

void RISCV::writePltHeader(uint8_t *buf) const {
  // 1: auipc t2, %pcrel_hi(.got.plt)
  // sub t1, t1, t3
  // l[wd] t3, %pcrel_lo(1b)(t2); t3 = _dl_runtime_resolve
  // addi t1, t1, -pltHeaderSize-12; t1 = &.plt[i] - &.plt[0]
  // addi t0, t2, %pcrel_lo(1b)
  // srli t1, t1, (rv64?1:2); t1 = &.got.plt[i] - &.got.plt[0]
  // l[wd] t0, Wordsize(t0); t0 = link_map
  // jr t3
  uint32_t offset = in.gotPlt->getVA() - in.plt->getVA();
  uint32_t load = config->is64 ? LD : LW;
  write32le(buf + 0, utype(AUIPC, X_T2, hi20(offset)));
  write32le(buf + 4, rtype(SUB, X_T1, X_T1, X_T3));
  write32le(buf + 8, itype(load, X_T3, X_T2, lo12(offset)));
  write32le(buf + 12, itype(ADDI, X_T1, X_T1, -target->pltHeaderSize - 12));
  write32le(buf + 16, itype(ADDI, X_T0, X_T2, lo12(offset)));
  write32le(buf + 20, itype(SRLI, X_T1, X_T1, config->is64 ? 1 : 2));
  write32le(buf + 24, itype(load, X_T0, X_T0, config->wordsize));
  write32le(buf + 28, itype(JALR, 0, X_T3, 0));
}

void RISCV::writePlt(uint8_t *buf, const Symbol &sym,
                     uint64_t pltEntryAddr) const {
  // 1: auipc t3, %pcrel_hi(f@.got.plt)
  // l[wd] t3, %pcrel_lo(1b)(t3)
  // jalr t1, t3
  // nop
  uint32_t offset = sym.getGotPltVA() - pltEntryAddr;
  write32le(buf + 0, utype(AUIPC, X_T3, hi20(offset)));
  write32le(buf + 4, itype(config->is64 ? LD : LW, X_T3, X_T3, lo12(offset)));
  write32le(buf + 8, itype(JALR, X_T1, X_T3, 0));
  write32le(buf + 12, itype(ADDI, 0, 0, 0));
}

RelType RISCV::getDynRel(RelType type) const {
  return type == target->symbolicRel ? type
                                     : static_cast<RelType>(R_RISCV_NONE);
}

RelExpr RISCV::getRelExpr(const RelType type, const Symbol &s,
                          const uint8_t *loc) const {
  switch (type) {
  case R_RISCV_NONE:
    return R_NONE;
  case R_RISCV_32:
  case R_RISCV_64:
  case R_RISCV_HI20:
  case R_RISCV_LO12_I:
  case R_RISCV_LO12_S:
  case R_RISCV_RVC_LUI:
    return R_ABS;
  case R_RISCV_ADD8:
  case R_RISCV_ADD16:
  case R_RISCV_ADD32:
  case R_RISCV_ADD64:
  case R_RISCV_SET6:
  case R_RISCV_SET8:
  case R_RISCV_SET16:
  case R_RISCV_SET32:
  case R_RISCV_SUB6:
  case R_RISCV_SUB8:
  case R_RISCV_SUB16:
  case R_RISCV_SUB32:
  case R_RISCV_SUB64:
    return R_RISCV_ADD;
  case R_RISCV_JAL:
  case R_RISCV_BRANCH:
  case R_RISCV_PCREL_HI20:
  case R_RISCV_RVC_BRANCH:
  case R_RISCV_RVC_JUMP:
  case R_RISCV_32_PCREL:
    return R_PC;
  case R_RISCV_CALL:
  case R_RISCV_CALL_PLT:
  case R_RISCV_PLT32:
    return R_PLT_PC;
  case R_RISCV_GOT_HI20:
  case R_RISCV_GOT32_PCREL:
    return R_GOT_PC;
  case R_RISCV_PCREL_LO12_I:
  case R_RISCV_PCREL_LO12_S:
    return R_RISCV_PC_INDIRECT;
  case R_RISCV_TLSDESC_HI20:
  case R_RISCV_TLSDESC_LOAD_LO12:
  case R_RISCV_TLSDESC_ADD_LO12:
    return R_TLSDESC_PC;
  case R_RISCV_TLSDESC_CALL:
    return R_TLSDESC_CALL;
  case R_RISCV_TLS_GD_HI20:
    return R_TLSGD_PC;
  case R_RISCV_TLS_GOT_HI20:
    return R_GOT_PC;
  case R_RISCV_TPREL_HI20:
  case R_RISCV_TPREL_LO12_I:
  case R_RISCV_TPREL_LO12_S:
    return R_TPREL;
  case R_RISCV_ALIGN:
    return R_RELAX_HINT;
  case R_RISCV_TPREL_ADD:
  case R_RISCV_RELAX:
    return config->relax ? R_RELAX_HINT : R_NONE;
  case R_RISCV_SET_ULEB128:
  case R_RISCV_SUB_ULEB128:
    return R_RISCV_LEB128;
  default:
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

void RISCV::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  const unsigned bits = config->wordsize * 8;

  switch (rel.type) {
  case R_RISCV_32:
    write32le(loc, val);
    return;
  case R_RISCV_64:
    write64le(loc, val);
    return;

  case R_RISCV_RVC_BRANCH: {
    checkInt(loc, val, 9, rel);
    checkAlignment(loc, val, 2, rel);
    uint16_t insn = read16le(loc) & 0xE383;
    uint16_t imm8 = extractBits(val, 8, 8) << 12;
    uint16_t imm4_3 = extractBits(val, 4, 3) << 10;
    uint16_t imm7_6 = extractBits(val, 7, 6) << 5;
    uint16_t imm2_1 = extractBits(val, 2, 1) << 3;
    uint16_t imm5 = extractBits(val, 5, 5) << 2;
    insn |= imm8 | imm4_3 | imm7_6 | imm2_1 | imm5;

    write16le(loc, insn);
    return;
  }

  case R_RISCV_RVC_JUMP: {
    checkInt(loc, val, 12, rel);
    checkAlignment(loc, val, 2, rel);
    uint16_t insn = read16le(loc) & 0xE003;
    uint16_t imm11 = extractBits(val, 11, 11) << 12;
    uint16_t imm4 = extractBits(val, 4, 4) << 11;
    uint16_t imm9_8 = extractBits(val, 9, 8) << 9;
    uint16_t imm10 = extractBits(val, 10, 10) << 8;
    uint16_t imm6 = extractBits(val, 6, 6) << 7;
    uint16_t imm7 = extractBits(val, 7, 7) << 6;
    uint16_t imm3_1 = extractBits(val, 3, 1) << 3;
    uint16_t imm5 = extractBits(val, 5, 5) << 2;
    insn |= imm11 | imm4 | imm9_8 | imm10 | imm6 | imm7 | imm3_1 | imm5;

    write16le(loc, insn);
    return;
  }

  case R_RISCV_RVC_LUI: {
    int64_t imm = SignExtend64(val + 0x800, bits) >> 12;
    checkInt(loc, imm, 6, rel);
    if (imm == 0) { // `c.lui rd, 0` is illegal, convert to `c.li rd, 0`
      write16le(loc, (read16le(loc) & 0x0F83) | 0x4000);
    } else {
      uint16_t imm17 = extractBits(val + 0x800, 17, 17) << 12;
      uint16_t imm16_12 = extractBits(val + 0x800, 16, 12) << 2;
      write16le(loc, (read16le(loc) & 0xEF83) | imm17 | imm16_12);
    }
    return;
  }

  case R_RISCV_JAL: {
    checkInt(loc, val, 21, rel);
    checkAlignment(loc, val, 2, rel);

    uint32_t insn = read32le(loc) & 0xFFF;
    uint32_t imm20 = extractBits(val, 20, 20) << 31;
    uint32_t imm10_1 = extractBits(val, 10, 1) << 21;
    uint32_t imm11 = extractBits(val, 11, 11) << 20;
    uint32_t imm19_12 = extractBits(val, 19, 12) << 12;
    insn |= imm20 | imm10_1 | imm11 | imm19_12;

    write32le(loc, insn);
    return;
  }

  case R_RISCV_BRANCH: {
    checkInt(loc, val, 13, rel);
    checkAlignment(loc, val, 2, rel);

    uint32_t insn = read32le(loc) & 0x1FFF07F;
    uint32_t imm12 = extractBits(val, 12, 12) << 31;
    uint32_t imm10_5 = extractBits(val, 10, 5) << 25;
    uint32_t imm4_1 = extractBits(val, 4, 1) << 8;
    uint32_t imm11 = extractBits(val, 11, 11) << 7;
    insn |= imm12 | imm10_5 | imm4_1 | imm11;

    write32le(loc, insn);
    return;
  }

  // auipc + jalr pair
  case R_RISCV_CALL:
  case R_RISCV_CALL_PLT: {
    int64_t hi = SignExtend64(val + 0x800, bits) >> 12;
    checkInt(loc, hi, 20, rel);
    if (isInt<20>(hi)) {
      relocateNoSym(loc, R_RISCV_PCREL_HI20, val);
      relocateNoSym(loc + 4, R_RISCV_PCREL_LO12_I, val);
    }
    return;
  }

  case R_RISCV_GOT_HI20:
  case R_RISCV_PCREL_HI20:
  case R_RISCV_TLSDESC_HI20:
  case R_RISCV_TLS_GD_HI20:
  case R_RISCV_TLS_GOT_HI20:
  case R_RISCV_TPREL_HI20:
  case R_RISCV_HI20: {
    uint64_t hi = val + 0x800;
    checkInt(loc, SignExtend64(hi, bits) >> 12, 20, rel);
    write32le(loc, (read32le(loc) & 0xFFF) | (hi & 0xFFFFF000));
    return;
  }

  case R_RISCV_PCREL_LO12_I:
  case R_RISCV_TLSDESC_LOAD_LO12:
  case R_RISCV_TLSDESC_ADD_LO12:
  case R_RISCV_TPREL_LO12_I:
  case R_RISCV_LO12_I: {
    uint64_t hi = (val + 0x800) >> 12;
    uint64_t lo = val - (hi << 12);
    write32le(loc, setLO12_I(read32le(loc), lo & 0xfff));
    return;
  }

  case R_RISCV_PCREL_LO12_S:
  case R_RISCV_TPREL_LO12_S:
  case R_RISCV_LO12_S: {
    uint64_t hi = (val + 0x800) >> 12;
    uint64_t lo = val - (hi << 12);
    write32le(loc, setLO12_S(read32le(loc), lo));
    return;
  }

  case INTERNAL_R_RISCV_GPREL_I:
  case INTERNAL_R_RISCV_GPREL_S: {
    Defined *gp = ElfSym::riscvGlobalPointer;
    int64_t displace = SignExtend64(val - gp->getVA(), bits);
    checkInt(loc, displace, 12, rel);
    uint32_t insn = (read32le(loc) & ~(31 << 15)) | (X_GP << 15);
    if (rel.type == INTERNAL_R_RISCV_GPREL_I)
      insn = setLO12_I(insn, displace);
    else
      insn = setLO12_S(insn, displace);
    write32le(loc, insn);
    return;
  }

  case R_RISCV_ADD8:
    *loc += val;
    return;
  case R_RISCV_ADD16:
    write16le(loc, read16le(loc) + val);
    return;
  case R_RISCV_ADD32:
    write32le(loc, read32le(loc) + val);
    return;
  case R_RISCV_ADD64:
    write64le(loc, read64le(loc) + val);
    return;
  case R_RISCV_SUB6:
    *loc = (*loc & 0xc0) | (((*loc & 0x3f) - val) & 0x3f);
    return;
  case R_RISCV_SUB8:
    *loc -= val;
    return;
  case R_RISCV_SUB16:
    write16le(loc, read16le(loc) - val);
    return;
  case R_RISCV_SUB32:
    write32le(loc, read32le(loc) - val);
    return;
  case R_RISCV_SUB64:
    write64le(loc, read64le(loc) - val);
    return;
  case R_RISCV_SET6:
    *loc = (*loc & 0xc0) | (val & 0x3f);
    return;
  case R_RISCV_SET8:
    *loc = val;
    return;
  case R_RISCV_SET16:
    write16le(loc, val);
    return;
  case R_RISCV_SET32:
  case R_RISCV_32_PCREL:
  case R_RISCV_PLT32:
  case R_RISCV_GOT32_PCREL:
    checkInt(loc, val, 32, rel);
    write32le(loc, val);
    return;

  case R_RISCV_TLS_DTPREL32:
    write32le(loc, val - dtpOffset);
    break;
  case R_RISCV_TLS_DTPREL64:
    write64le(loc, val - dtpOffset);
    break;

  case R_RISCV_RELAX:
    return;
  case R_RISCV_TLSDESC:
    // The addend is stored in the second word.
    if (config->is64)
      write64le(loc + 8, val);
    else
      write32le(loc + 4, val);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

static bool relaxable(ArrayRef<Relocation> relocs, size_t i) {
  return i + 1 != relocs.size() && relocs[i + 1].type == R_RISCV_RELAX;
}

static void tlsdescToIe(uint8_t *loc, const Relocation &rel, uint64_t val) {
  switch (rel.type) {
  case R_RISCV_TLSDESC_HI20:
  case R_RISCV_TLSDESC_LOAD_LO12:
    write32le(loc, 0x00000013); // nop
    break;
  case R_RISCV_TLSDESC_ADD_LO12:
    write32le(loc, utype(AUIPC, X_A0, hi20(val))); // auipc a0,<hi20>
    break;
  case R_RISCV_TLSDESC_CALL:
    if (config->is64)
      write32le(loc, itype(LD, X_A0, X_A0, lo12(val))); // ld a0,<lo12>(a0)
    else
      write32le(loc, itype(LW, X_A0, X_A0, lo12(val))); // lw a0,<lo12>(a0)
    break;
  default:
    llvm_unreachable("unsupported relocation for TLSDESC to IE");
  }
}

static void tlsdescToLe(uint8_t *loc, const Relocation &rel, uint64_t val) {
  switch (rel.type) {
  case R_RISCV_TLSDESC_HI20:
  case R_RISCV_TLSDESC_LOAD_LO12:
    write32le(loc, 0x00000013); // nop
    return;
  case R_RISCV_TLSDESC_ADD_LO12:
    if (isInt<12>(val))
      write32le(loc, 0x00000013); // nop
    else
      write32le(loc, utype(LUI, X_A0, hi20(val))); // lui a0,<hi20>
    return;
  case R_RISCV_TLSDESC_CALL:
    if (isInt<12>(val))
      write32le(loc, itype(ADDI, X_A0, 0, val)); // addi a0,zero,<lo12>
    else
      write32le(loc, itype(ADDI, X_A0, X_A0, lo12(val))); // addi a0,a0,<lo12>
    return;
  default:
    llvm_unreachable("unsupported relocation for TLSDESC to LE");
  }
}

void RISCV::relocateAlloc(InputSectionBase &sec, uint8_t *buf) const {
  uint64_t secAddr = sec.getOutputSection()->addr;
  if (auto *s = dyn_cast<InputSection>(&sec))
    secAddr += s->outSecOff;
  else if (auto *ehIn = dyn_cast<EhInputSection>(&sec))
    secAddr += ehIn->getParent()->outSecOff;
  uint64_t tlsdescVal = 0;
  bool tlsdescRelax = false, isToLe = false;
  const ArrayRef<Relocation> relocs = sec.relocs();
  for (size_t i = 0, size = relocs.size(); i != size; ++i) {
    const Relocation &rel = relocs[i];
    uint8_t *loc = buf + rel.offset;
    uint64_t val =
        sec.getRelocTargetVA(sec.file, rel.type, rel.addend,
                             secAddr + rel.offset, *rel.sym, rel.expr);

    switch (rel.expr) {
    case R_RELAX_HINT:
      continue;
    case R_TLSDESC_PC:
      // For R_RISCV_TLSDESC_HI20, store &got(sym)-PC to be used by the
      // following two instructions L[DW] and ADDI.
      if (rel.type == R_RISCV_TLSDESC_HI20)
        tlsdescVal = val;
      else
        val = tlsdescVal;
      break;
    case R_RELAX_TLS_GD_TO_IE:
      // Only R_RISCV_TLSDESC_HI20 reaches here. tlsdescVal will be finalized
      // after we see R_RISCV_TLSDESC_ADD_LO12 in the R_RELAX_TLS_GD_TO_LE case.
      // The net effect is that tlsdescVal will be smaller than `val` to take
      // into account of NOP instructions (in the absence of R_RISCV_RELAX)
      // before AUIPC.
      tlsdescVal = val + rel.offset;
      isToLe = false;
      tlsdescRelax = relaxable(relocs, i);
      if (!tlsdescRelax)
        tlsdescToIe(loc, rel, val);
      continue;
    case R_RELAX_TLS_GD_TO_LE:
      // See the comment in handleTlsRelocation. For TLSDESC=>IE,
      // R_RISCV_TLSDESC_{LOAD_LO12,ADD_LO12,CALL} also reach here. If isToLe is
      // false, this is actually TLSDESC=>IE optimization.
      if (rel.type == R_RISCV_TLSDESC_HI20) {
        tlsdescVal = val;
        isToLe = true;
        tlsdescRelax = relaxable(relocs, i);
      } else {
        if (!isToLe && rel.type == R_RISCV_TLSDESC_ADD_LO12)
          tlsdescVal -= rel.offset;
        val = tlsdescVal;
      }
      // When NOP conversion is eligible and relaxation applies, don't write a
      // NOP in case an unrelated instruction follows the current instruction.
      if (tlsdescRelax &&
          (rel.type == R_RISCV_TLSDESC_HI20 ||
           rel.type == R_RISCV_TLSDESC_LOAD_LO12 ||
           (rel.type == R_RISCV_TLSDESC_ADD_LO12 && isToLe && !hi20(val))))
        continue;
      if (isToLe)
        tlsdescToLe(loc, rel, val);
      else
        tlsdescToIe(loc, rel, val);
      continue;
    case R_RISCV_LEB128:
      if (i + 1 < size) {
        const Relocation &rel1 = relocs[i + 1];
        if (rel.type == R_RISCV_SET_ULEB128 &&
            rel1.type == R_RISCV_SUB_ULEB128 && rel.offset == rel1.offset) {
          auto val = rel.sym->getVA(rel.addend) - rel1.sym->getVA(rel1.addend);
          if (overwriteULEB128(loc, val) >= 0x80)
            errorOrWarn(sec.getLocation(rel.offset) + ": ULEB128 value " +
                        Twine(val) + " exceeds available space; references '" +
                        lld::toString(*rel.sym) + "'");
          ++i;
          continue;
        }
      }
      errorOrWarn(sec.getLocation(rel.offset) +
                  ": R_RISCV_SET_ULEB128 not paired with R_RISCV_SUB_SET128");
      return;
    default:
      break;
    }
    relocate(loc, rel, val);
  }
}

void elf::initSymbolAnchors() {
  SmallVector<InputSection *, 0> storage;
  for (OutputSection *osec : outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      sec->relaxAux = make<RelaxAux>();
      if (sec->relocs().size()) {
        sec->relaxAux->relocDeltas =
            std::make_unique<uint32_t[]>(sec->relocs().size());
        sec->relaxAux->relocTypes =
            std::make_unique<RelType[]>(sec->relocs().size());
      }
    }
  }
  // Store anchors (st_value and st_value+st_size) for symbols relative to text
  // sections.
  //
  // For a defined symbol foo, we may have `d->file != file` with --wrap=foo.
  // We should process foo, as the defining object file's symbol table may not
  // contain foo after redirectSymbols changed the foo entry to __wrap_foo. To
  // avoid adding a Defined that is undefined in one object file, use
  // `!d->scriptDefined` to exclude symbols that are definitely not wrapped.
  //
  // `relaxAux->anchors` may contain duplicate symbols, but that is fine.
  for (InputFile *file : ctx.objectFiles)
    for (Symbol *sym : file->getSymbols()) {
      auto *d = dyn_cast<Defined>(sym);
      if (!d || (d->file != file && !d->scriptDefined))
        continue;
      if (auto *sec = dyn_cast_or_null<InputSection>(d->section))
        if (sec->flags & SHF_EXECINSTR && sec->relaxAux) {
          // If sec is discarded, relaxAux will be nullptr.
          sec->relaxAux->anchors.push_back({d->value, d, false});
          sec->relaxAux->anchors.push_back({d->value + d->size, d, true});
        }
    }
  // Sort anchors by offset so that we can find the closest relocation
  // efficiently. For a zero size symbol, ensure that its start anchor precedes
  // its end anchor. For two symbols with anchors at the same offset, their
  // order does not matter.
  for (OutputSection *osec : outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      llvm::sort(sec->relaxAux->anchors, [](auto &a, auto &b) {
        return std::make_pair(a.offset, a.end) <
               std::make_pair(b.offset, b.end);
      });
    }
  }
}

// Relax R_RISCV_CALL/R_RISCV_CALL_PLT auipc+jalr to c.j, c.jal, or jal.
static void relaxCall(const InputSection &sec, size_t i, uint64_t loc,
                      Relocation &r, uint32_t &remove) {
  const bool rvc = getEFlags(sec.file) & EF_RISCV_RVC;
  const Symbol &sym = *r.sym;
  const uint64_t insnPair = read64le(sec.content().data() + r.offset);
  const uint32_t rd = extractBits(insnPair, 32 + 11, 32 + 7);
  const uint64_t dest =
      (r.expr == R_PLT_PC ? sym.getPltVA() : sym.getVA()) + r.addend;
  const int64_t displace = dest - loc;

  if (rvc && isInt<12>(displace) && rd == 0) {
    sec.relaxAux->relocTypes[i] = R_RISCV_RVC_JUMP;
    sec.relaxAux->writes.push_back(0xa001); // c.j
    remove = 6;
  } else if (rvc && isInt<12>(displace) && rd == X_RA &&
             !config->is64) { // RV32C only
    sec.relaxAux->relocTypes[i] = R_RISCV_RVC_JUMP;
    sec.relaxAux->writes.push_back(0x2001); // c.jal
    remove = 6;
  } else if (isInt<21>(displace)) {
    sec.relaxAux->relocTypes[i] = R_RISCV_JAL;
    sec.relaxAux->writes.push_back(0x6f | rd << 7); // jal
    remove = 4;
  }
}

// Relax local-exec TLS when hi20 is zero.
static void relaxTlsLe(const InputSection &sec, size_t i, uint64_t loc,
                       Relocation &r, uint32_t &remove) {
  uint64_t val = r.sym->getVA(r.addend);
  if (hi20(val) != 0)
    return;
  uint32_t insn = read32le(sec.content().data() + r.offset);
  switch (r.type) {
  case R_RISCV_TPREL_HI20:
  case R_RISCV_TPREL_ADD:
    // Remove lui rd, %tprel_hi(x) and add rd, rd, tp, %tprel_add(x).
    sec.relaxAux->relocTypes[i] = R_RISCV_RELAX;
    remove = 4;
    break;
  case R_RISCV_TPREL_LO12_I:
    // addi rd, rd, %tprel_lo(x) => addi rd, tp, st_value(x)
    sec.relaxAux->relocTypes[i] = R_RISCV_32;
    insn = (insn & ~(31 << 15)) | (X_TP << 15);
    sec.relaxAux->writes.push_back(setLO12_I(insn, val));
    break;
  case R_RISCV_TPREL_LO12_S:
    // sw rs, %tprel_lo(x)(rd) => sw rs, st_value(x)(rd)
    sec.relaxAux->relocTypes[i] = R_RISCV_32;
    insn = (insn & ~(31 << 15)) | (X_TP << 15);
    sec.relaxAux->writes.push_back(setLO12_S(insn, val));
    break;
  }
}

static void relaxHi20Lo12(const InputSection &sec, size_t i, uint64_t loc,
                          Relocation &r, uint32_t &remove) {
  const Defined *gp = ElfSym::riscvGlobalPointer;
  if (!gp)
    return;

  if (!isInt<12>(r.sym->getVA(r.addend) - gp->getVA()))
    return;

  switch (r.type) {
  case R_RISCV_HI20:
    // Remove lui rd, %hi20(x).
    sec.relaxAux->relocTypes[i] = R_RISCV_RELAX;
    remove = 4;
    break;
  case R_RISCV_LO12_I:
    sec.relaxAux->relocTypes[i] = INTERNAL_R_RISCV_GPREL_I;
    break;
  case R_RISCV_LO12_S:
    sec.relaxAux->relocTypes[i] = INTERNAL_R_RISCV_GPREL_S;
    break;
  }
}

static bool relax(InputSection &sec) {
  const uint64_t secAddr = sec.getVA();
  const MutableArrayRef<Relocation> relocs = sec.relocs();
  auto &aux = *sec.relaxAux;
  bool changed = false;
  ArrayRef<SymbolAnchor> sa = ArrayRef(aux.anchors);
  uint64_t delta = 0;
  bool tlsdescRelax = false, toLeShortForm = false;

  std::fill_n(aux.relocTypes.get(), relocs.size(), R_RISCV_NONE);
  aux.writes.clear();
  for (auto [i, r] : llvm::enumerate(relocs)) {
    const uint64_t loc = secAddr + r.offset - delta;
    uint32_t &cur = aux.relocDeltas[i], remove = 0;
    switch (r.type) {
    case R_RISCV_ALIGN: {
      const uint64_t nextLoc = loc + r.addend;
      const uint64_t align = PowerOf2Ceil(r.addend + 2);
      // All bytes beyond the alignment boundary should be removed.
      remove = nextLoc - ((loc + align - 1) & -align);
      // If we can't satisfy this alignment, we've found a bad input.
      if (LLVM_UNLIKELY(static_cast<int32_t>(remove) < 0)) {
        errorOrWarn(getErrorLocation((const uint8_t*)loc) +
                    "insufficient padding bytes for " + lld::toString(r.type) +
                    ": " + Twine(r.addend) + " bytes available "
                    "for requested alignment of " + Twine(align) + " bytes");
        remove = 0;
      }
      break;
    }
    case R_RISCV_CALL:
    case R_RISCV_CALL_PLT:
      if (relaxable(relocs, i))
        relaxCall(sec, i, loc, r, remove);
      break;
    case R_RISCV_TPREL_HI20:
    case R_RISCV_TPREL_ADD:
    case R_RISCV_TPREL_LO12_I:
    case R_RISCV_TPREL_LO12_S:
      if (relaxable(relocs, i))
        relaxTlsLe(sec, i, loc, r, remove);
      break;
    case R_RISCV_HI20:
    case R_RISCV_LO12_I:
    case R_RISCV_LO12_S:
      if (relaxable(relocs, i))
        relaxHi20Lo12(sec, i, loc, r, remove);
      break;
    case R_RISCV_TLSDESC_HI20:
      // For TLSDESC=>LE, we can use the short form if hi20 is zero.
      tlsdescRelax = relaxable(relocs, i);
      toLeShortForm = tlsdescRelax && r.expr == R_RELAX_TLS_GD_TO_LE &&
                      !hi20(r.sym->getVA(r.addend));
      [[fallthrough]];
    case R_RISCV_TLSDESC_LOAD_LO12:
      // For TLSDESC=>LE/IE, AUIPC and L[DW] are removed if relaxable.
      if (tlsdescRelax && r.expr != R_TLSDESC_PC)
        remove = 4;
      break;
    case R_RISCV_TLSDESC_ADD_LO12:
      if (toLeShortForm)
        remove = 4;
      break;
    }

    // For all anchors whose offsets are <= r.offset, they are preceded by
    // the previous relocation whose `relocDeltas` value equals `delta`.
    // Decrease their st_value and update their st_size.
    for (; sa.size() && sa[0].offset <= r.offset; sa = sa.slice(1)) {
      if (sa[0].end)
        sa[0].d->size = sa[0].offset - delta - sa[0].d->value;
      else
        sa[0].d->value = sa[0].offset - delta;
    }
    delta += remove;
    if (delta != cur) {
      cur = delta;
      changed = true;
    }
  }

  for (const SymbolAnchor &a : sa) {
    if (a.end)
      a.d->size = a.offset - delta - a.d->value;
    else
      a.d->value = a.offset - delta;
  }
  // Inform assignAddresses that the size has changed.
  if (!isUInt<32>(delta))
    fatal("section size decrease is too large: " + Twine(delta));
  sec.bytesDropped = delta;
  return changed;
}

// When relaxing just R_RISCV_ALIGN, relocDeltas is usually changed only once in
// the absence of a linker script. For call and load/store R_RISCV_RELAX, code
// shrinkage may reduce displacement and make more relocations eligible for
// relaxation. Code shrinkage may increase displacement to a call/load/store
// target at a higher fixed address, invalidating an earlier relaxation. Any
// change in section sizes can have cascading effect and require another
// relaxation pass.
bool RISCV::relaxOnce(int pass) const {
  llvm::TimeTraceScope timeScope("RISC-V relaxOnce");
  if (config->relocatable)
    return false;

  if (pass == 0)
    initSymbolAnchors();

  SmallVector<InputSection *, 0> storage;
  bool changed = false;
  for (OutputSection *osec : outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage))
      changed |= relax(*sec);
  }
  return changed;
}

void RISCV::finalizeRelax(int passes) const {
  llvm::TimeTraceScope timeScope("Finalize RISC-V relaxation");
  log("relaxation passes: " + Twine(passes));
  SmallVector<InputSection *, 0> storage;
  for (OutputSection *osec : outputSections) {
    if (!(osec->flags & SHF_EXECINSTR))
      continue;
    for (InputSection *sec : getInputSections(*osec, storage)) {
      RelaxAux &aux = *sec->relaxAux;
      if (!aux.relocDeltas)
        continue;

      MutableArrayRef<Relocation> rels = sec->relocs();
      ArrayRef<uint8_t> old = sec->content();
      size_t newSize = old.size() - aux.relocDeltas[rels.size() - 1];
      size_t writesIdx = 0;
      uint8_t *p = context().bAlloc.Allocate<uint8_t>(newSize);
      uint64_t offset = 0;
      int64_t delta = 0;
      sec->content_ = p;
      sec->size = newSize;
      sec->bytesDropped = 0;

      // Update section content: remove NOPs for R_RISCV_ALIGN and rewrite
      // instructions for relaxed relocations.
      for (size_t i = 0, e = rels.size(); i != e; ++i) {
        uint32_t remove = aux.relocDeltas[i] - delta;
        delta = aux.relocDeltas[i];
        if (remove == 0 && aux.relocTypes[i] == R_RISCV_NONE)
          continue;

        // Copy from last location to the current relocated location.
        const Relocation &r = rels[i];
        uint64_t size = r.offset - offset;
        memcpy(p, old.data() + offset, size);
        p += size;

        // For R_RISCV_ALIGN, we will place `offset` in a location (among NOPs)
        // to satisfy the alignment requirement. If both `remove` and r.addend
        // are multiples of 4, it is as if we have skipped some NOPs. Otherwise
        // we are in the middle of a 4-byte NOP, and we need to rewrite the NOP
        // sequence.
        int64_t skip = 0;
        if (r.type == R_RISCV_ALIGN) {
          if (remove % 4 || r.addend % 4) {
            skip = r.addend - remove;
            int64_t j = 0;
            for (; j + 4 <= skip; j += 4)
              write32le(p + j, 0x00000013); // nop
            if (j != skip) {
              assert(j + 2 == skip);
              write16le(p + j, 0x0001); // c.nop
            }
          }
        } else if (RelType newType = aux.relocTypes[i]) {
          switch (newType) {
          case INTERNAL_R_RISCV_GPREL_I:
          case INTERNAL_R_RISCV_GPREL_S:
            break;
          case R_RISCV_RELAX:
            // Used by relaxTlsLe to indicate the relocation is ignored.
            break;
          case R_RISCV_RVC_JUMP:
            skip = 2;
            write16le(p, aux.writes[writesIdx++]);
            break;
          case R_RISCV_JAL:
            skip = 4;
            write32le(p, aux.writes[writesIdx++]);
            break;
          case R_RISCV_32:
            // Used by relaxTlsLe to write a uint32_t then suppress the handling
            // in relocateAlloc.
            skip = 4;
            write32le(p, aux.writes[writesIdx++]);
            aux.relocTypes[i] = R_RISCV_NONE;
            break;
          default:
            llvm_unreachable("unsupported type");
          }
        }

        p += skip;
        offset = r.offset + skip + remove;
      }
      memcpy(p, old.data() + offset, old.size() - offset);

      // Subtract the previous relocDeltas value from the relocation offset.
      // For a pair of R_RISCV_CALL/R_RISCV_RELAX with the same offset, decrease
      // their r_offset by the same delta.
      delta = 0;
      for (size_t i = 0, e = rels.size(); i != e;) {
        uint64_t cur = rels[i].offset;
        do {
          rels[i].offset -= delta;
          if (aux.relocTypes[i] != R_RISCV_NONE)
            rels[i].type = aux.relocTypes[i];
        } while (++i != e && rels[i].offset == cur);
        delta = aux.relocDeltas[i - 1];
      }
    }
  }
}

namespace {
// Representation of the merged .riscv.attributes input sections. The psABI
// specifies merge policy for attributes. E.g. if we link an object without an
// extension with an object with the extension, the output Tag_RISCV_arch shall
// contain the extension. Some tools like objdump parse .riscv.attributes and
// disabling some instructions if the first Tag_RISCV_arch does not contain an
// extension.
class RISCVAttributesSection final : public SyntheticSection {
public:
  RISCVAttributesSection()
      : SyntheticSection(0, SHT_RISCV_ATTRIBUTES, 1, ".riscv.attributes") {}

  size_t getSize() const override { return size; }
  void writeTo(uint8_t *buf) override;

  static constexpr StringRef vendor = "riscv";
  DenseMap<unsigned, unsigned> intAttr;
  DenseMap<unsigned, StringRef> strAttr;
  size_t size = 0;
};
} // namespace

static void mergeArch(RISCVISAUtils::OrderedExtensionMap &mergedExts,
                      unsigned &mergedXlen, const InputSectionBase *sec,
                      StringRef s) {
  auto maybeInfo = RISCVISAInfo::parseNormalizedArchString(s);
  if (!maybeInfo) {
    errorOrWarn(toString(sec) + ": " + s + ": " +
                llvm::toString(maybeInfo.takeError()));
    return;
  }

  // Merge extensions.
  RISCVISAInfo &info = **maybeInfo;
  if (mergedExts.empty()) {
    mergedExts = info.getExtensions();
    mergedXlen = info.getXLen();
  } else {
    for (const auto &ext : info.getExtensions()) {
      auto p = mergedExts.insert(ext);
      if (!p.second) {
        if (std::tie(p.first->second.Major, p.first->second.Minor) <
            std::tie(ext.second.Major, ext.second.Minor))
          p.first->second = ext.second;
      }
    }
  }
}

static void mergeAtomic(DenseMap<unsigned, unsigned>::iterator it,
                        const InputSectionBase *oldSection,
                        const InputSectionBase *newSection,
                        RISCVAttrs::RISCVAtomicAbiTag oldTag,
                        RISCVAttrs::RISCVAtomicAbiTag newTag) {
  using RISCVAttrs::RISCVAtomicAbiTag;
  // Same tags stay the same, and UNKNOWN is compatible with anything
  if (oldTag == newTag || newTag == RISCVAtomicAbiTag::UNKNOWN)
    return;

  auto reportAbiError = [&]() {
    errorOrWarn("atomic abi mismatch for " + oldSection->name + "\n>>> " +
                toString(oldSection) +
                ": atomic_abi=" + Twine(static_cast<unsigned>(oldTag)) +
                "\n>>> " + toString(newSection) +
                ": atomic_abi=" + Twine(static_cast<unsigned>(newTag)));
  };

  auto reportUnknownAbiError = [](const InputSectionBase *section,
                                  RISCVAtomicAbiTag tag) {
    switch (tag) {
    case RISCVAtomicAbiTag::UNKNOWN:
    case RISCVAtomicAbiTag::A6C:
    case RISCVAtomicAbiTag::A6S:
    case RISCVAtomicAbiTag::A7:
      return;
    };
    errorOrWarn("unknown atomic abi for " + section->name + "\n>>> " +
                toString(section) +
                ": atomic_abi=" + Twine(static_cast<unsigned>(tag)));
  };
  switch (oldTag) {
  case RISCVAtomicAbiTag::UNKNOWN:
    it->getSecond() = static_cast<unsigned>(newTag);
    return;
  case RISCVAtomicAbiTag::A6C:
    switch (newTag) {
    case RISCVAtomicAbiTag::A6S:
      it->getSecond() = static_cast<unsigned>(RISCVAtomicAbiTag::A6C);
      return;
    case RISCVAtomicAbiTag::A7:
      reportAbiError();
      return;
    case RISCVAttrs::RISCVAtomicAbiTag::UNKNOWN:
    case RISCVAttrs::RISCVAtomicAbiTag::A6C:
      return;
    };
    break;

  case RISCVAtomicAbiTag::A6S:
    switch (newTag) {
    case RISCVAtomicAbiTag::A6C:
      it->getSecond() = static_cast<unsigned>(RISCVAtomicAbiTag::A6C);
      return;
    case RISCVAtomicAbiTag::A7:
      it->getSecond() = static_cast<unsigned>(RISCVAtomicAbiTag::A7);
      return;
    case RISCVAttrs::RISCVAtomicAbiTag::UNKNOWN:
    case RISCVAttrs::RISCVAtomicAbiTag::A6S:
      return;
    };
    break;

  case RISCVAtomicAbiTag::A7:
    switch (newTag) {
    case RISCVAtomicAbiTag::A6S:
      it->getSecond() = static_cast<unsigned>(RISCVAtomicAbiTag::A7);
      return;
    case RISCVAtomicAbiTag::A6C:
      reportAbiError();
      return;
    case RISCVAttrs::RISCVAtomicAbiTag::UNKNOWN:
    case RISCVAttrs::RISCVAtomicAbiTag::A7:
      return;
    };
    break;
  };

  // If we get here, then we have an invalid tag, so report it.
  // Putting these checks at the end allows us to only do these checks when we
  // need to, since this is expected to be a rare occurrence.
  reportUnknownAbiError(oldSection, oldTag);
  reportUnknownAbiError(newSection, newTag);
}

static RISCVAttributesSection *
mergeAttributesSection(const SmallVector<InputSectionBase *, 0> &sections) {
  using RISCVAttrs::RISCVAtomicAbiTag;
  RISCVISAUtils::OrderedExtensionMap exts;
  const InputSectionBase *firstStackAlign = nullptr;
  const InputSectionBase *firstAtomicAbi = nullptr;
  unsigned firstStackAlignValue = 0, xlen = 0;
  bool hasArch = false;

  in.riscvAttributes = std::make_unique<RISCVAttributesSection>();
  auto &merged = static_cast<RISCVAttributesSection &>(*in.riscvAttributes);

  // Collect all tags values from attributes section.
  const auto &attributesTags = RISCVAttrs::getRISCVAttributeTags();
  for (const InputSectionBase *sec : sections) {
    RISCVAttributeParser parser;
    if (Error e = parser.parse(sec->content(), llvm::endianness::little))
      warn(toString(sec) + ": " + llvm::toString(std::move(e)));
    for (const auto &tag : attributesTags) {
      switch (RISCVAttrs::AttrType(tag.attr)) {
        // Integer attributes.
      case RISCVAttrs::STACK_ALIGN:
        if (auto i = parser.getAttributeValue(tag.attr)) {
          auto r = merged.intAttr.try_emplace(tag.attr, *i);
          if (r.second) {
            firstStackAlign = sec;
            firstStackAlignValue = *i;
          } else if (r.first->second != *i) {
            errorOrWarn(toString(sec) + " has stack_align=" + Twine(*i) +
                        " but " + toString(firstStackAlign) +
                        " has stack_align=" + Twine(firstStackAlignValue));
          }
        }
        continue;
      case RISCVAttrs::UNALIGNED_ACCESS:
        if (auto i = parser.getAttributeValue(tag.attr))
          merged.intAttr[tag.attr] |= *i;
        continue;

        // String attributes.
      case RISCVAttrs::ARCH:
        if (auto s = parser.getAttributeString(tag.attr)) {
          hasArch = true;
          mergeArch(exts, xlen, sec, *s);
        }
        continue;

        // Attributes which use the default handling.
      case RISCVAttrs::PRIV_SPEC:
      case RISCVAttrs::PRIV_SPEC_MINOR:
      case RISCVAttrs::PRIV_SPEC_REVISION:
        break;

      case RISCVAttrs::AttrType::ATOMIC_ABI:
        if (auto i = parser.getAttributeValue(tag.attr)) {
          auto r = merged.intAttr.try_emplace(tag.attr, *i);
          if (r.second)
            firstAtomicAbi = sec;
          else
            mergeAtomic(r.first, firstAtomicAbi, sec,
                        static_cast<RISCVAtomicAbiTag>(r.first->getSecond()),
                        static_cast<RISCVAtomicAbiTag>(*i));
        }
        continue;
      }

      // Fallback for deprecated priv_spec* and other unknown attributes: retain
      // the attribute if all input sections agree on the value. GNU ld uses 0
      // and empty strings as default values which are not dumped to the output.
      // TODO Adjust after resolution to
      // https://github.com/riscv-non-isa/riscv-elf-psabi-doc/issues/352
      if (tag.attr % 2 == 0) {
        if (auto i = parser.getAttributeValue(tag.attr)) {
          auto r = merged.intAttr.try_emplace(tag.attr, *i);
          if (!r.second && r.first->second != *i)
            r.first->second = 0;
        }
      } else if (auto s = parser.getAttributeString(tag.attr)) {
        auto r = merged.strAttr.try_emplace(tag.attr, *s);
        if (!r.second && r.first->second != *s)
          r.first->second = {};
      }
    }
  }

  if (hasArch && xlen != 0) {
    if (auto result = RISCVISAInfo::createFromExtMap(xlen, exts)) {
      merged.strAttr.try_emplace(RISCVAttrs::ARCH,
                                 saver().save((*result)->toString()));
    } else {
      errorOrWarn(llvm::toString(result.takeError()));
    }
  }

  // The total size of headers: format-version [ <section-length> "vendor-name"
  // [ <file-tag> <size>.
  size_t size = 5 + merged.vendor.size() + 1 + 5;
  for (auto &attr : merged.intAttr)
    if (attr.second != 0)
      size += getULEB128Size(attr.first) + getULEB128Size(attr.second);
  for (auto &attr : merged.strAttr)
    if (!attr.second.empty())
      size += getULEB128Size(attr.first) + attr.second.size() + 1;
  merged.size = size;
  return &merged;
}

void RISCVAttributesSection::writeTo(uint8_t *buf) {
  const size_t size = getSize();
  uint8_t *const end = buf + size;
  *buf = ELFAttrs::Format_Version;
  write32(buf + 1, size - 1);
  buf += 5;

  memcpy(buf, vendor.data(), vendor.size());
  buf += vendor.size() + 1;

  *buf = ELFAttrs::File;
  write32(buf + 1, end - buf);
  buf += 5;

  for (auto &attr : intAttr) {
    if (attr.second == 0)
      continue;
    buf += encodeULEB128(attr.first, buf);
    buf += encodeULEB128(attr.second, buf);
  }
  for (auto &attr : strAttr) {
    if (attr.second.empty())
      continue;
    buf += encodeULEB128(attr.first, buf);
    memcpy(buf, attr.second.data(), attr.second.size());
    buf += attr.second.size() + 1;
  }
}

void elf::mergeRISCVAttributesSections() {
  // Find the first input SHT_RISCV_ATTRIBUTES; return if not found.
  size_t place =
      llvm::find_if(ctx.inputSections,
                    [](auto *s) { return s->type == SHT_RISCV_ATTRIBUTES; }) -
      ctx.inputSections.begin();
  if (place == ctx.inputSections.size())
    return;

  // Extract all SHT_RISCV_ATTRIBUTES sections into `sections`.
  SmallVector<InputSectionBase *, 0> sections;
  llvm::erase_if(ctx.inputSections, [&](InputSectionBase *s) {
    if (s->type != SHT_RISCV_ATTRIBUTES)
      return false;
    sections.push_back(s);
    return true;
  });

  // Add the merged section.
  ctx.inputSections.insert(ctx.inputSections.begin() + place,
                           mergeAttributesSection(sections));
}

TargetInfo *elf::getRISCVTargetInfo() {
  static RISCV target;
  return &target;
}
