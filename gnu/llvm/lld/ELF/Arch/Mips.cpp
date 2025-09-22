//===- MIPS.cpp -----------------------------------------------------------===//
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
#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
template <class ELFT> class MIPS final : public TargetInfo {
public:
  MIPS();
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  RelType getDynRel(RelType type) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  bool needsThunk(RelExpr expr, RelType type, const InputFile *file,
                  uint64_t branchAddr, const Symbol &s,
                  int64_t a) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  bool usesOnlyLowPageBits(RelType type) const override;
};
} // namespace

template <class ELFT> MIPS<ELFT>::MIPS() {
  gotPltHeaderEntriesNum = 2;
  defaultMaxPageSize = 65536;
  pltEntrySize = 16;
  pltHeaderSize = 32;
  copyRel = R_MIPS_COPY;
  pltRel = R_MIPS_JUMP_SLOT;
  needsThunks = true;

  // Set `sigrie 1` as a trap instruction.
  write32(trapInstr.data(), 0x04170001);

  if (ELFT::Is64Bits) {
    relativeRel = (R_MIPS_64 << 8) | R_MIPS_REL32;
    symbolicRel = R_MIPS_64;
    tlsGotRel = R_MIPS_TLS_TPREL64;
    tlsModuleIndexRel = R_MIPS_TLS_DTPMOD64;
    tlsOffsetRel = R_MIPS_TLS_DTPREL64;
  } else {
    relativeRel = R_MIPS_REL32;
    symbolicRel = R_MIPS_32;
    tlsGotRel = R_MIPS_TLS_TPREL32;
    tlsModuleIndexRel = R_MIPS_TLS_DTPMOD32;
    tlsOffsetRel = R_MIPS_TLS_DTPREL32;
  }
}

template <class ELFT> uint32_t MIPS<ELFT>::calcEFlags() const {
  return calcMipsEFlags<ELFT>();
}

template <class ELFT>
RelExpr MIPS<ELFT>::getRelExpr(RelType type, const Symbol &s,
                               const uint8_t *loc) const {
  // See comment in the calculateMipsRelChain.
  if (ELFT::Is64Bits || config->mipsN32Abi)
    type &= 0xff;

  switch (type) {
  case R_MIPS_JALR:
    // Older versions of clang would erroneously emit this relocation not only
    // against functions (loaded from the GOT) but also against data symbols
    // (e.g. a table of function pointers). When we encounter this, ignore the
    // relocation and emit a warning instead.
    if (!s.isFunc() && s.type != STT_NOTYPE) {
      warn(getErrorLocation(loc) +
           "found R_MIPS_JALR relocation against non-function symbol " +
           toString(s) + ". This is invalid and most likely a compiler bug.");
      return R_NONE;
    }

    // If the target symbol is not preemptible and is not microMIPS,
    // it might be possible to replace jalr/jr instruction by bal/b.
    // It depends on the target symbol's offset.
    if (!s.isPreemptible && !(s.getVA() & 0x1))
      return R_PC;
    return R_NONE;
  case R_MICROMIPS_JALR:
    return R_NONE;
  case R_MIPS_GPREL16:
  case R_MIPS_GPREL32:
  case R_MICROMIPS_GPREL16:
  case R_MICROMIPS_GPREL7_S2:
    return R_MIPS_GOTREL;
  case R_MIPS_26:
  case R_MICROMIPS_26_S1:
    return R_PLT;
  case R_MICROMIPS_PC26_S1:
    return R_PLT_PC;
  case R_MIPS_HI16:
  case R_MIPS_LO16:
  case R_MIPS_HIGHER:
  case R_MIPS_HIGHEST:
  case R_MICROMIPS_HI16:
  case R_MICROMIPS_LO16:
    // R_MIPS_HI16/R_MIPS_LO16 relocations against _gp_disp calculate
    // offset between start of function and 'gp' value which by default
    // equal to the start of .got section. In that case we consider these
    // relocations as relative.
    if (&s == ElfSym::mipsGpDisp)
      return R_MIPS_GOT_GP_PC;
    if (&s == ElfSym::mipsLocalGp)
      return R_MIPS_GOT_GP;
    [[fallthrough]];
  case R_MIPS_32:
  case R_MIPS_64:
  case R_MIPS_GOT_OFST:
  case R_MIPS_SUB:
    return R_ABS;
  case R_MIPS_TLS_DTPREL_HI16:
  case R_MIPS_TLS_DTPREL_LO16:
  case R_MIPS_TLS_DTPREL32:
  case R_MIPS_TLS_DTPREL64:
  case R_MICROMIPS_TLS_DTPREL_HI16:
  case R_MICROMIPS_TLS_DTPREL_LO16:
    return R_DTPREL;
  case R_MIPS_TLS_TPREL_HI16:
  case R_MIPS_TLS_TPREL_LO16:
  case R_MIPS_TLS_TPREL32:
  case R_MIPS_TLS_TPREL64:
  case R_MICROMIPS_TLS_TPREL_HI16:
  case R_MICROMIPS_TLS_TPREL_LO16:
    return R_TPREL;
  case R_MIPS_PC32:
  case R_MIPS_PC16:
  case R_MIPS_PC19_S2:
  case R_MIPS_PC21_S2:
  case R_MIPS_PC26_S2:
  case R_MIPS_PCHI16:
  case R_MIPS_PCLO16:
  case R_MICROMIPS_PC7_S1:
  case R_MICROMIPS_PC10_S1:
  case R_MICROMIPS_PC16_S1:
  case R_MICROMIPS_PC18_S3:
  case R_MICROMIPS_PC19_S2:
  case R_MICROMIPS_PC23_S2:
  case R_MICROMIPS_PC21_S1:
    return R_PC;
  case R_MIPS_GOT16:
  case R_MICROMIPS_GOT16:
    if (s.isLocal())
      return R_MIPS_GOT_LOCAL_PAGE;
    [[fallthrough]];
  case R_MIPS_CALL16:
  case R_MIPS_GOT_DISP:
  case R_MIPS_TLS_GOTTPREL:
  case R_MICROMIPS_CALL16:
  case R_MICROMIPS_TLS_GOTTPREL:
    return R_MIPS_GOT_OFF;
  case R_MIPS_CALL_HI16:
  case R_MIPS_CALL_LO16:
  case R_MIPS_GOT_HI16:
  case R_MIPS_GOT_LO16:
  case R_MICROMIPS_CALL_HI16:
  case R_MICROMIPS_CALL_LO16:
  case R_MICROMIPS_GOT_HI16:
  case R_MICROMIPS_GOT_LO16:
    return R_MIPS_GOT_OFF32;
  case R_MIPS_GOT_PAGE:
    return R_MIPS_GOT_LOCAL_PAGE;
  case R_MIPS_TLS_GD:
  case R_MICROMIPS_TLS_GD:
    return R_MIPS_TLSGD;
  case R_MIPS_TLS_LDM:
  case R_MICROMIPS_TLS_LDM:
    return R_MIPS_TLSLD;
  case R_MIPS_NONE:
    return R_NONE;
  default:
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

template <class ELFT> RelType MIPS<ELFT>::getDynRel(RelType type) const {
  if (type == symbolicRel)
    return type;
  return R_MIPS_NONE;
}

template <class ELFT>
void MIPS<ELFT>::writeGotPlt(uint8_t *buf, const Symbol &) const {
  uint64_t va = in.plt->getVA();
  if (isMicroMips())
    va |= 1;
  write32(buf, va);
}

template <endianness E> static uint32_t readShuffle(const uint8_t *loc) {
  // The major opcode of a microMIPS instruction needs to appear
  // in the first 16-bit word (lowest address) for efficient hardware
  // decode so that it knows if the instruction is 16-bit or 32-bit
  // as early as possible. To do so, little-endian binaries keep 16-bit
  // words in a big-endian order. That is why we have to swap these
  // words to get a correct value.
  uint32_t v = read32(loc);
  if (E == llvm::endianness::little)
    return (v << 16) | (v >> 16);
  return v;
}

static void writeValue(uint8_t *loc, uint64_t v, uint8_t bitsSize,
                       uint8_t shift) {
  uint32_t instr = read32(loc);
  uint32_t mask = 0xffffffff >> (32 - bitsSize);
  uint32_t data = (instr & ~mask) | ((v >> shift) & mask);
  write32(loc, data);
}

template <endianness E>
static void writeShuffleValue(uint8_t *loc, uint64_t v, uint8_t bitsSize,
                              uint8_t shift) {
  // See comments in readShuffle for purpose of this code.
  uint16_t *words = (uint16_t *)loc;
  if (E == llvm::endianness::little)
    std::swap(words[0], words[1]);

  writeValue(loc, v, bitsSize, shift);

  if (E == llvm::endianness::little)
    std::swap(words[0], words[1]);
}

template <endianness E>
static void writeMicroRelocation16(uint8_t *loc, uint64_t v, uint8_t bitsSize,
                                   uint8_t shift) {
  uint16_t instr = read16(loc);
  uint16_t mask = 0xffff >> (16 - bitsSize);
  uint16_t data = (instr & ~mask) | ((v >> shift) & mask);
  write16(loc, data);
}

template <class ELFT> void MIPS<ELFT>::writePltHeader(uint8_t *buf) const {
  if (isMicroMips()) {
    uint64_t gotPlt = in.gotPlt->getVA();
    uint64_t plt = in.plt->getVA();
    // Overwrite trap instructions written by Writer::writeTrapInstr.
    memset(buf, 0, pltHeaderSize);

    write16(buf, isMipsR6() ? 0x7860 : 0x7980);  // addiupc v1, (GOTPLT) - .
    write16(buf + 4, 0xff23);    // lw      $25, 0($3)
    write16(buf + 8, 0x0535);    // subu16  $2,  $2, $3
    write16(buf + 10, 0x2525);   // srl16   $2,  $2, 2
    write16(buf + 12, 0x3302);   // addiu   $24, $2, -2
    write16(buf + 14, 0xfffe);
    write16(buf + 16, 0x0dff);   // move    $15, $31
    if (isMipsR6()) {
      write16(buf + 18, 0x0f83); // move    $28, $3
      write16(buf + 20, 0x472b); // jalrc   $25
      write16(buf + 22, 0x0c00); // nop
      relocateNoSym(buf, R_MICROMIPS_PC19_S2, gotPlt - plt);
    } else {
      write16(buf + 18, 0x45f9); // jalrc   $25
      write16(buf + 20, 0x0f83); // move    $28, $3
      write16(buf + 22, 0x0c00); // nop
      relocateNoSym(buf, R_MICROMIPS_PC23_S2, gotPlt - plt);
    }
    return;
  }

  if (config->mipsN32Abi) {
    write32(buf, 0x3c0e0000);      // lui   $14, %hi(&GOTPLT[0])
    write32(buf + 4, 0x8dd90000);  // lw    $25, %lo(&GOTPLT[0])($14)
    write32(buf + 8, 0x25ce0000);  // addiu $14, $14, %lo(&GOTPLT[0])
    write32(buf + 12, 0x030ec023); // subu  $24, $24, $14
    write32(buf + 16, 0x03e07825); // move  $15, $31
    write32(buf + 20, 0x0018c082); // srl   $24, $24, 2
  } else if (ELFT::Is64Bits) {
    write32(buf, 0x3c0e0000);      // lui   $14, %hi(&GOTPLT[0])
    write32(buf + 4, 0xddd90000);  // ld    $25, %lo(&GOTPLT[0])($14)
    write32(buf + 8, 0x25ce0000);  // addiu $14, $14, %lo(&GOTPLT[0])
    write32(buf + 12, 0x030ec023); // subu  $24, $24, $14
    write32(buf + 16, 0x03e07825); // move  $15, $31
    write32(buf + 20, 0x0018c0c2); // srl   $24, $24, 3
  } else {
    write32(buf, 0x3c1c0000);      // lui   $28, %hi(&GOTPLT[0])
    write32(buf + 4, 0x8f990000);  // lw    $25, %lo(&GOTPLT[0])($28)
    write32(buf + 8, 0x279c0000);  // addiu $28, $28, %lo(&GOTPLT[0])
    write32(buf + 12, 0x031cc023); // subu  $24, $24, $28
    write32(buf + 16, 0x03e07825); // move  $15, $31
    write32(buf + 20, 0x0018c082); // srl   $24, $24, 2
  }

  uint32_t jalrInst = config->zHazardplt ? 0x0320fc09 : 0x0320f809;
  write32(buf + 24, jalrInst); // jalr.hb $25 or jalr $25
  write32(buf + 28, 0x2718fffe); // subu  $24, $24, 2

  uint64_t gotPlt = in.gotPlt->getVA();
  writeValue(buf, gotPlt + 0x8000, 16, 16);
  writeValue(buf + 4, gotPlt, 16, 0);
  writeValue(buf + 8, gotPlt, 16, 0);
}

template <class ELFT>
void MIPS<ELFT>::writePlt(uint8_t *buf, const Symbol &sym,
                          uint64_t pltEntryAddr) const {
  uint64_t gotPltEntryAddr = sym.getGotPltVA();
  if (isMicroMips()) {
    // Overwrite trap instructions written by Writer::writeTrapInstr.
    memset(buf, 0, pltEntrySize);

    if (isMipsR6()) {
      write16(buf, 0x7840);      // addiupc $2, (GOTPLT) - .
      write16(buf + 4, 0xff22);  // lw $25, 0($2)
      write16(buf + 8, 0x0f02);  // move $24, $2
      write16(buf + 10, 0x4723); // jrc $25 / jr16 $25
      relocateNoSym(buf, R_MICROMIPS_PC19_S2, gotPltEntryAddr - pltEntryAddr);
    } else {
      write16(buf, 0x7900);      // addiupc $2, (GOTPLT) - .
      write16(buf + 4, 0xff22);  // lw $25, 0($2)
      write16(buf + 8, 0x4599);  // jrc $25 / jr16 $25
      write16(buf + 10, 0x0f02); // move $24, $2
      relocateNoSym(buf, R_MICROMIPS_PC23_S2, gotPltEntryAddr - pltEntryAddr);
    }
    return;
  }

  uint32_t loadInst = ELFT::Is64Bits ? 0xddf90000 : 0x8df90000;
  uint32_t jrInst = isMipsR6() ? (config->zHazardplt ? 0x03200409 : 0x03200009)
                               : (config->zHazardplt ? 0x03200408 : 0x03200008);
  uint32_t addInst = ELFT::Is64Bits ? 0x65f80000 : 0x25f80000;

  write32(buf, 0x3c0f0000);     // lui   $15, %hi(.got.plt entry)
  write32(buf + 4, loadInst);   // l[wd] $25, %lo(.got.plt entry)($15)
  write32(buf + 8, jrInst);     // jr  $25 / jr.hb $25
  write32(buf + 12, addInst);   // [d]addiu $24, $15, %lo(.got.plt entry)
  writeValue(buf, gotPltEntryAddr + 0x8000, 16, 16);
  writeValue(buf + 4, gotPltEntryAddr, 16, 0);
  writeValue(buf + 12, gotPltEntryAddr, 16, 0);
}

template <class ELFT>
bool MIPS<ELFT>::needsThunk(RelExpr expr, RelType type, const InputFile *file,
                            uint64_t branchAddr, const Symbol &s,
                            int64_t /*a*/) const {
  // Any MIPS PIC code function is invoked with its address in register $t9.
  // So if we have a branch instruction from non-PIC code to the PIC one
  // we cannot make the jump directly and need to create a small stubs
  // to save the target function address.
  // See page 3-38 ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
  if (type != R_MIPS_26 && type != R_MIPS_PC26_S2 &&
      type != R_MICROMIPS_26_S1 && type != R_MICROMIPS_PC26_S1)
    return false;
  auto *f = dyn_cast_or_null<ObjFile<ELFT>>(file);
  if (!f)
    return false;
  // If current file has PIC code, LA25 stub is not required.
  if (f->getObj().getHeader().e_flags & EF_MIPS_PIC)
    return false;
  auto *d = dyn_cast<Defined>(&s);
  // LA25 is required if target file has PIC code
  // or target symbol is a PIC symbol.
  return d && isMipsPIC<ELFT>(d);
}

template <class ELFT>
int64_t MIPS<ELFT>::getImplicitAddend(const uint8_t *buf, RelType type) const {
  const endianness e = ELFT::Endianness;
  switch (type) {
  case R_MIPS_32:
  case R_MIPS_REL32:
  case R_MIPS_GPREL32:
  case R_MIPS_TLS_DTPREL32:
  case R_MIPS_TLS_DTPMOD32:
  case R_MIPS_TLS_TPREL32:
    return SignExtend64<32>(read32(buf));
  case R_MIPS_26:
    // FIXME (simon): If the relocation target symbol is not a PLT entry
    // we should use another expression for calculation:
    // ((A << 2) | (P & 0xf0000000)) >> 2
    return SignExtend64<28>(read32(buf) << 2);
  case R_MIPS_CALL_HI16:
  case R_MIPS_GOT16:
  case R_MIPS_GOT_HI16:
  case R_MIPS_HI16:
  case R_MIPS_PCHI16:
    return SignExtend64<16>(read32(buf)) << 16;
  case R_MIPS_CALL16:
  case R_MIPS_CALL_LO16:
  case R_MIPS_GOT_LO16:
  case R_MIPS_GPREL16:
  case R_MIPS_LO16:
  case R_MIPS_PCLO16:
  case R_MIPS_TLS_DTPREL_HI16:
  case R_MIPS_TLS_DTPREL_LO16:
  case R_MIPS_TLS_GD:
  case R_MIPS_TLS_GOTTPREL:
  case R_MIPS_TLS_LDM:
  case R_MIPS_TLS_TPREL_HI16:
  case R_MIPS_TLS_TPREL_LO16:
    return SignExtend64<16>(read32(buf));
  case R_MICROMIPS_GOT16:
  case R_MICROMIPS_HI16:
    return SignExtend64<16>(readShuffle<e>(buf)) << 16;
  case R_MICROMIPS_CALL16:
  case R_MICROMIPS_GPREL16:
  case R_MICROMIPS_LO16:
  case R_MICROMIPS_TLS_DTPREL_HI16:
  case R_MICROMIPS_TLS_DTPREL_LO16:
  case R_MICROMIPS_TLS_GD:
  case R_MICROMIPS_TLS_GOTTPREL:
  case R_MICROMIPS_TLS_LDM:
  case R_MICROMIPS_TLS_TPREL_HI16:
  case R_MICROMIPS_TLS_TPREL_LO16:
    return SignExtend64<16>(readShuffle<e>(buf));
  case R_MICROMIPS_GPREL7_S2:
    return SignExtend64<9>(readShuffle<e>(buf) << 2);
  case R_MIPS_PC16:
    return SignExtend64<18>(read32(buf) << 2);
  case R_MIPS_PC19_S2:
    return SignExtend64<21>(read32(buf) << 2);
  case R_MIPS_PC21_S2:
    return SignExtend64<23>(read32(buf) << 2);
  case R_MIPS_PC26_S2:
    return SignExtend64<28>(read32(buf) << 2);
  case R_MIPS_PC32:
    return SignExtend64<32>(read32(buf));
  case R_MICROMIPS_26_S1:
    return SignExtend64<27>(readShuffle<e>(buf) << 1);
  case R_MICROMIPS_PC7_S1:
    return SignExtend64<8>(read16(buf) << 1);
  case R_MICROMIPS_PC10_S1:
    return SignExtend64<11>(read16(buf) << 1);
  case R_MICROMIPS_PC16_S1:
    return SignExtend64<17>(readShuffle<e>(buf) << 1);
  case R_MICROMIPS_PC18_S3:
    return SignExtend64<21>(readShuffle<e>(buf) << 3);
  case R_MICROMIPS_PC19_S2:
    return SignExtend64<21>(readShuffle<e>(buf) << 2);
  case R_MICROMIPS_PC21_S1:
    return SignExtend64<22>(readShuffle<e>(buf) << 1);
  case R_MICROMIPS_PC23_S2:
    return SignExtend64<25>(readShuffle<e>(buf) << 2);
  case R_MICROMIPS_PC26_S1:
    return SignExtend64<27>(readShuffle<e>(buf) << 1);
  case R_MIPS_64:
  case R_MIPS_TLS_DTPMOD64:
  case R_MIPS_TLS_DTPREL64:
  case R_MIPS_TLS_TPREL64:
  case (R_MIPS_64 << 8) | R_MIPS_REL32:
    return read64(buf);
  case R_MIPS_COPY:
    return config->is64 ? read64(buf) : read32(buf);
  case R_MIPS_NONE:
  case R_MIPS_JUMP_SLOT:
  case R_MIPS_JALR:
    // These relocations are defined as not having an implicit addend.
    return 0;
  default:
    internalLinkerError(getErrorLocation(buf),
                        "cannot read addend for relocation " + toString(type));
    return 0;
  }
}

static std::pair<uint32_t, uint64_t>
calculateMipsRelChain(uint8_t *loc, RelType type, uint64_t val) {
  // MIPS N64 ABI packs multiple relocations into the single relocation
  // record. In general, all up to three relocations can have arbitrary
  // types. In fact, Clang and GCC uses only a few combinations. For now,
  // we support two of them. That is allow to pass at least all LLVM
  // test suite cases.
  // <any relocation> / R_MIPS_SUB / R_MIPS_HI16 | R_MIPS_LO16
  // <any relocation> / R_MIPS_64 / R_MIPS_NONE
  // The first relocation is a 'real' relocation which is calculated
  // using the corresponding symbol's value. The second and the third
  // relocations used to modify result of the first one: extend it to
  // 64-bit, extract high or low part etc. For details, see part 2.9 Relocation
  // at the https://dmz-portal.mips.com/mw/images/8/82/007-4658-001.pdf
  RelType type2 = (type >> 8) & 0xff;
  RelType type3 = (type >> 16) & 0xff;
  if (type2 == R_MIPS_NONE && type3 == R_MIPS_NONE)
    return std::make_pair(type, val);
  if (type2 == R_MIPS_64 && type3 == R_MIPS_NONE)
    return std::make_pair(type2, val);
  if (type2 == R_MIPS_SUB && (type3 == R_MIPS_HI16 || type3 == R_MIPS_LO16))
    return std::make_pair(type3, -val);
  error(getErrorLocation(loc) + "unsupported relocations combination " +
        Twine(type));
  return std::make_pair(type & 0xff, val);
}

static bool isBranchReloc(RelType type) {
  return type == R_MIPS_26 || type == R_MIPS_PC26_S2 ||
         type == R_MIPS_PC21_S2 || type == R_MIPS_PC16;
}

static bool isMicroBranchReloc(RelType type) {
  return type == R_MICROMIPS_26_S1 || type == R_MICROMIPS_PC16_S1 ||
         type == R_MICROMIPS_PC10_S1 || type == R_MICROMIPS_PC7_S1;
}

template <class ELFT>
static uint64_t fixupCrossModeJump(uint8_t *loc, RelType type, uint64_t val) {
  // Here we need to detect jump/branch from regular MIPS code
  // to a microMIPS target and vice versa. In that cases jump
  // instructions need to be replaced by their "cross-mode"
  // equivalents.
  const endianness e = ELFT::Endianness;
  bool isMicroTgt = val & 0x1;
  bool isCrossJump = (isMicroTgt && isBranchReloc(type)) ||
                     (!isMicroTgt && isMicroBranchReloc(type));
  if (!isCrossJump)
    return val;

  switch (type) {
  case R_MIPS_26: {
    uint32_t inst = read32(loc) >> 26;
    if (inst == 0x3 || inst == 0x1d) { // JAL or JALX
      writeValue(loc, 0x1d << 26, 32, 0);
      return val;
    }
    break;
  }
  case R_MICROMIPS_26_S1: {
    uint32_t inst = readShuffle<e>(loc) >> 26;
    if (inst == 0x3d || inst == 0x3c) { // JAL32 or JALX32
      val >>= 1;
      writeShuffleValue<e>(loc, 0x3c << 26, 32, 0);
      return val;
    }
    break;
  }
  case R_MIPS_PC26_S2:
  case R_MIPS_PC21_S2:
  case R_MIPS_PC16:
  case R_MICROMIPS_PC16_S1:
  case R_MICROMIPS_PC10_S1:
  case R_MICROMIPS_PC7_S1:
    // FIXME (simon): Support valid branch relocations.
    break;
  default:
    llvm_unreachable("unexpected jump/branch relocation");
  }

  error(getErrorLocation(loc) +
        "unsupported jump/branch instruction between ISA modes referenced by " +
        toString(type) + " relocation");
  return val;
}

template <class ELFT>
void MIPS<ELFT>::relocate(uint8_t *loc, const Relocation &rel,
                          uint64_t val) const {
  const endianness e = ELFT::Endianness;
  RelType type = rel.type;

  if (ELFT::Is64Bits || config->mipsN32Abi)
    std::tie(type, val) = calculateMipsRelChain(loc, type, val);

  // Detect cross-mode jump/branch and fix instruction.
  val = fixupCrossModeJump<ELFT>(loc, type, val);

  // Thread pointer and DRP offsets from the start of TLS data area.
  // https://www.linux-mips.org/wiki/NPTL
  if (type == R_MIPS_TLS_DTPREL_HI16 || type == R_MIPS_TLS_DTPREL_LO16 ||
      type == R_MIPS_TLS_DTPREL32 || type == R_MIPS_TLS_DTPREL64 ||
      type == R_MICROMIPS_TLS_DTPREL_HI16 ||
      type == R_MICROMIPS_TLS_DTPREL_LO16) {
    val -= 0x8000;
  }

  switch (type) {
  case R_MIPS_32:
  case R_MIPS_GPREL32:
  case R_MIPS_TLS_DTPREL32:
  case R_MIPS_TLS_TPREL32:
    write32(loc, val);
    break;
  case R_MIPS_64:
  case R_MIPS_TLS_DTPREL64:
  case R_MIPS_TLS_TPREL64:
    write64(loc, val);
    break;
  case R_MIPS_26:
    writeValue(loc, val, 26, 2);
    break;
  case R_MIPS_GOT16:
    // The R_MIPS_GOT16 relocation's value in "relocatable" linking mode
    // is updated addend (not a GOT index). In that case write high 16 bits
    // to store a correct addend value.
    if (config->relocatable) {
      writeValue(loc, val + 0x8000, 16, 16);
    } else {
      checkInt(loc, val, 16, rel);
      writeValue(loc, val, 16, 0);
    }
    break;
  case R_MICROMIPS_GOT16:
    if (config->relocatable) {
      writeShuffleValue<e>(loc, val + 0x8000, 16, 16);
    } else {
      checkInt(loc, val, 16, rel);
      writeShuffleValue<e>(loc, val, 16, 0);
    }
    break;
  case R_MIPS_CALL16:
  case R_MIPS_GOT_DISP:
  case R_MIPS_GOT_PAGE:
  case R_MIPS_GPREL16:
  case R_MIPS_TLS_GD:
  case R_MIPS_TLS_GOTTPREL:
  case R_MIPS_TLS_LDM:
    checkInt(loc, val, 16, rel);
    [[fallthrough]];
  case R_MIPS_CALL_LO16:
  case R_MIPS_GOT_LO16:
  case R_MIPS_GOT_OFST:
  case R_MIPS_LO16:
  case R_MIPS_PCLO16:
  case R_MIPS_TLS_DTPREL_LO16:
  case R_MIPS_TLS_TPREL_LO16:
    writeValue(loc, val, 16, 0);
    break;
  case R_MICROMIPS_GPREL16:
  case R_MICROMIPS_TLS_GD:
  case R_MICROMIPS_TLS_LDM:
    checkInt(loc, val, 16, rel);
    writeShuffleValue<e>(loc, val, 16, 0);
    break;
  case R_MICROMIPS_CALL16:
  case R_MICROMIPS_CALL_LO16:
  case R_MICROMIPS_LO16:
  case R_MICROMIPS_TLS_DTPREL_LO16:
  case R_MICROMIPS_TLS_GOTTPREL:
  case R_MICROMIPS_TLS_TPREL_LO16:
    writeShuffleValue<e>(loc, val, 16, 0);
    break;
  case R_MICROMIPS_GPREL7_S2:
    checkInt(loc, val, 7, rel);
    writeShuffleValue<e>(loc, val, 7, 2);
    break;
  case R_MIPS_CALL_HI16:
  case R_MIPS_GOT_HI16:
  case R_MIPS_HI16:
  case R_MIPS_PCHI16:
  case R_MIPS_TLS_DTPREL_HI16:
  case R_MIPS_TLS_TPREL_HI16:
    writeValue(loc, val + 0x8000, 16, 16);
    break;
  case R_MICROMIPS_CALL_HI16:
  case R_MICROMIPS_GOT_HI16:
  case R_MICROMIPS_HI16:
  case R_MICROMIPS_TLS_DTPREL_HI16:
  case R_MICROMIPS_TLS_TPREL_HI16:
    writeShuffleValue<e>(loc, val + 0x8000, 16, 16);
    break;
  case R_MIPS_HIGHER:
    writeValue(loc, val + 0x80008000, 16, 32);
    break;
  case R_MIPS_HIGHEST:
    writeValue(loc, val + 0x800080008000, 16, 48);
    break;
  case R_MIPS_JALR:
    val -= 4;
    // Replace jalr/jr instructions by bal/b if the target
    // offset fits into the 18-bit range.
    if (isInt<18>(val)) {
      switch (read32(loc)) {
      case 0x0320f809:  // jalr $25 => bal sym
        write32(loc, 0x04110000 | ((val >> 2) & 0xffff));
        break;
      case 0x03200008:  // jr $25 => b sym
        write32(loc, 0x10000000 | ((val >> 2) & 0xffff));
        break;
      }
    }
    break;
  case R_MICROMIPS_JALR:
    // Ignore this optimization relocation for now
    break;
  case R_MIPS_PC16:
    checkAlignment(loc, val, 4, rel);
    checkInt(loc, val, 18, rel);
    writeValue(loc, val, 16, 2);
    break;
  case R_MIPS_PC19_S2:
    checkAlignment(loc, val, 4, rel);
    checkInt(loc, val, 21, rel);
    writeValue(loc, val, 19, 2);
    break;
  case R_MIPS_PC21_S2:
    checkAlignment(loc, val, 4, rel);
    checkInt(loc, val, 23, rel);
    writeValue(loc, val, 21, 2);
    break;
  case R_MIPS_PC26_S2:
    checkAlignment(loc, val, 4, rel);
    checkInt(loc, val, 28, rel);
    writeValue(loc, val, 26, 2);
    break;
  case R_MIPS_PC32:
    writeValue(loc, val, 32, 0);
    break;
  case R_MICROMIPS_26_S1:
  case R_MICROMIPS_PC26_S1:
    checkInt(loc, val, 27, rel);
    writeShuffleValue<e>(loc, val, 26, 1);
    break;
  case R_MICROMIPS_PC7_S1:
    checkInt(loc, val, 8, rel);
    writeMicroRelocation16<e>(loc, val, 7, 1);
    break;
  case R_MICROMIPS_PC10_S1:
    checkInt(loc, val, 11, rel);
    writeMicroRelocation16<e>(loc, val, 10, 1);
    break;
  case R_MICROMIPS_PC16_S1:
    checkInt(loc, val, 17, rel);
    writeShuffleValue<e>(loc, val, 16, 1);
    break;
  case R_MICROMIPS_PC18_S3:
    checkInt(loc, val, 21, rel);
    writeShuffleValue<e>(loc, val, 18, 3);
    break;
  case R_MICROMIPS_PC19_S2:
    checkInt(loc, val, 21, rel);
    writeShuffleValue<e>(loc, val, 19, 2);
    break;
  case R_MICROMIPS_PC21_S1:
    checkInt(loc, val, 22, rel);
    writeShuffleValue<e>(loc, val, 21, 1);
    break;
  case R_MICROMIPS_PC23_S2:
    checkInt(loc, val, 25, rel);
    writeShuffleValue<e>(loc, val, 23, 2);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

template <class ELFT> bool MIPS<ELFT>::usesOnlyLowPageBits(RelType type) const {
  return type == R_MIPS_LO16 || type == R_MIPS_GOT_OFST ||
         type == R_MICROMIPS_LO16;
}

// Return true if the symbol is a PIC function.
template <class ELFT> bool elf::isMipsPIC(const Defined *sym) {
  if (!sym->isFunc())
    return false;

  if (sym->stOther & STO_MIPS_PIC)
    return true;

  if (!sym->section)
    return false;

  InputFile *file = cast<InputSectionBase>(sym->section)->file;
  if (!file || file->isInternal())
    return false;

  return cast<ObjFile<ELFT>>(file)->getObj().getHeader().e_flags & EF_MIPS_PIC;
}

template <class ELFT> TargetInfo *elf::getMipsTargetInfo() {
  static MIPS<ELFT> target;
  return &target;
}

template TargetInfo *elf::getMipsTargetInfo<ELF32LE>();
template TargetInfo *elf::getMipsTargetInfo<ELF32BE>();
template TargetInfo *elf::getMipsTargetInfo<ELF64LE>();
template TargetInfo *elf::getMipsTargetInfo<ELF64BE>();

template bool elf::isMipsPIC<ELF32LE>(const Defined *);
template bool elf::isMipsPIC<ELF32BE>(const Defined *);
template bool elf::isMipsPIC<ELF64LE>(const Defined *);
template bool elf::isMipsPIC<ELF64BE>(const Defined *);
