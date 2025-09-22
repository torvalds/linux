//===-- Hexagon.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "SyntheticSections.h"
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
class Hexagon final : public TargetInfo {
public:
  Hexagon();
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
};
} // namespace

Hexagon::Hexagon() {
  pltRel = R_HEX_JMP_SLOT;
  relativeRel = R_HEX_RELATIVE;
  gotRel = R_HEX_GLOB_DAT;
  symbolicRel = R_HEX_32;

  gotBaseSymInGotPlt = true;
  // The zero'th GOT entry is reserved for the address of _DYNAMIC.  The
  // next 3 are reserved for the dynamic loader.
  gotPltHeaderEntriesNum = 4;

  pltEntrySize = 16;
  pltHeaderSize = 32;

  // Hexagon Linux uses 64K pages by default.
  defaultMaxPageSize = 0x10000;
  tlsGotRel = R_HEX_TPREL_32;
  tlsModuleIndexRel = R_HEX_DTPMOD_32;
  tlsOffsetRel = R_HEX_DTPREL_32;
}

uint32_t Hexagon::calcEFlags() const {
  // The architecture revision must always be equal to or greater than
  // greatest revision in the list of inputs.
  std::optional<uint32_t> ret;
  for (InputFile *f : ctx.objectFiles) {
    uint32_t eflags = cast<ObjFile<ELF32LE>>(f)->getObj().getHeader().e_flags;
    if (!ret || eflags > *ret)
      ret = eflags;
  }
  return ret.value_or(/* Default Arch Rev: */ 0x60);
}

static uint32_t applyMask(uint32_t mask, uint32_t data) {
  uint32_t result = 0;
  size_t off = 0;

  for (size_t bit = 0; bit != 32; ++bit) {
    uint32_t valBit = (data >> off) & 1;
    uint32_t maskBit = (mask >> bit) & 1;
    if (maskBit) {
      result |= (valBit << bit);
      ++off;
    }
  }
  return result;
}

RelExpr Hexagon::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  switch (type) {
  case R_HEX_NONE:
    return R_NONE;
  case R_HEX_6_X:
  case R_HEX_8_X:
  case R_HEX_9_X:
  case R_HEX_10_X:
  case R_HEX_11_X:
  case R_HEX_12_X:
  case R_HEX_16_X:
  case R_HEX_32:
  case R_HEX_32_6_X:
  case R_HEX_HI16:
  case R_HEX_LO16:
  case R_HEX_DTPREL_32:
    return R_ABS;
  case R_HEX_B9_PCREL:
  case R_HEX_B13_PCREL:
  case R_HEX_B15_PCREL:
  case R_HEX_6_PCREL_X:
  case R_HEX_32_PCREL:
    return R_PC;
  case R_HEX_B9_PCREL_X:
  case R_HEX_B15_PCREL_X:
  case R_HEX_B22_PCREL:
  case R_HEX_PLT_B22_PCREL:
  case R_HEX_B22_PCREL_X:
  case R_HEX_B32_PCREL_X:
  case R_HEX_GD_PLT_B22_PCREL:
  case R_HEX_GD_PLT_B22_PCREL_X:
  case R_HEX_GD_PLT_B32_PCREL_X:
    return R_PLT_PC;
  case R_HEX_IE_32_6_X:
  case R_HEX_IE_16_X:
  case R_HEX_IE_HI16:
  case R_HEX_IE_LO16:
    return R_GOT;
  case R_HEX_GD_GOT_11_X:
  case R_HEX_GD_GOT_16_X:
  case R_HEX_GD_GOT_32_6_X:
    return R_TLSGD_GOTPLT;
  case R_HEX_GOTREL_11_X:
  case R_HEX_GOTREL_16_X:
  case R_HEX_GOTREL_32_6_X:
  case R_HEX_GOTREL_HI16:
  case R_HEX_GOTREL_LO16:
    return R_GOTPLTREL;
  case R_HEX_GOT_11_X:
  case R_HEX_GOT_16_X:
  case R_HEX_GOT_32_6_X:
    return R_GOTPLT;
  case R_HEX_IE_GOT_11_X:
  case R_HEX_IE_GOT_16_X:
  case R_HEX_IE_GOT_32_6_X:
  case R_HEX_IE_GOT_HI16:
  case R_HEX_IE_GOT_LO16:
    return R_GOTPLT;
  case R_HEX_TPREL_11_X:
  case R_HEX_TPREL_16:
  case R_HEX_TPREL_16_X:
  case R_HEX_TPREL_32_6_X:
  case R_HEX_TPREL_HI16:
  case R_HEX_TPREL_LO16:
    return R_TPREL;
  default:
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

// There are (arguably too) many relocation masks for the DSP's
// R_HEX_6_X type.  The table below is used to select the correct mask
// for the given instruction.
struct InstructionMask {
  uint32_t cmpMask;
  uint32_t relocMask;
};
static const InstructionMask r6[] = {
    {0x38000000, 0x0000201f}, {0x39000000, 0x0000201f},
    {0x3e000000, 0x00001f80}, {0x3f000000, 0x00001f80},
    {0x40000000, 0x000020f8}, {0x41000000, 0x000007e0},
    {0x42000000, 0x000020f8}, {0x43000000, 0x000007e0},
    {0x44000000, 0x000020f8}, {0x45000000, 0x000007e0},
    {0x46000000, 0x000020f8}, {0x47000000, 0x000007e0},
    {0x6a000000, 0x00001f80}, {0x7c000000, 0x001f2000},
    {0x9a000000, 0x00000f60}, {0x9b000000, 0x00000f60},
    {0x9c000000, 0x00000f60}, {0x9d000000, 0x00000f60},
    {0x9f000000, 0x001f0100}, {0xab000000, 0x0000003f},
    {0xad000000, 0x0000003f}, {0xaf000000, 0x00030078},
    {0xd7000000, 0x006020e0}, {0xd8000000, 0x006020e0},
    {0xdb000000, 0x006020e0}, {0xdf000000, 0x006020e0}};

constexpr uint32_t instParsePacketEnd = 0x0000c000;

static bool isDuplex(uint32_t insn) {
  // Duplex forms have a fixed mask and parse bits 15:14 are always
  // zero.  Non-duplex insns will always have at least one bit set in the
  // parse field.
  return (instParsePacketEnd & insn) == 0;
}

static uint32_t findMaskR6(uint32_t insn) {
  if (isDuplex(insn))
    return 0x03f00000;

  for (InstructionMask i : r6)
    if ((0xff000000 & insn) == i.cmpMask)
      return i.relocMask;

  error("unrecognized instruction for 6_X relocation: 0x" +
        utohexstr(insn));
  return 0;
}

static uint32_t findMaskR8(uint32_t insn) {
  if ((0xff000000 & insn) == 0xde000000)
    return 0x00e020e8;
  if ((0xff000000 & insn) == 0x3c000000)
    return 0x0000207f;
  return 0x00001fe0;
}

static uint32_t findMaskR11(uint32_t insn) {
  if ((0xff000000 & insn) == 0xa1000000)
    return 0x060020ff;
  return 0x06003fe0;
}

static uint32_t findMaskR16(uint32_t insn) {
  if (isDuplex(insn))
    return 0x03f00000;

  // Clear the end-packet-parse bits:
  insn = insn & ~instParsePacketEnd;

  if ((0xff000000 & insn) == 0x48000000)
    return 0x061f20ff;
  if ((0xff000000 & insn) == 0x49000000)
    return 0x061f3fe0;
  if ((0xff000000 & insn) == 0x78000000)
    return 0x00df3fe0;
  if ((0xff000000 & insn) == 0xb0000000)
    return 0x0fe03fe0;

  if ((0xff802000 & insn) == 0x74000000)
    return 0x00001fe0;
  if ((0xff802000 & insn) == 0x74002000)
    return 0x00001fe0;
  if ((0xff802000 & insn) == 0x74800000)
    return 0x00001fe0;
  if ((0xff802000 & insn) == 0x74802000)
    return 0x00001fe0;

  for (InstructionMask i : r6)
    if ((0xff000000 & insn) == i.cmpMask)
      return i.relocMask;

  error("unrecognized instruction for 16_X type: 0x" +
        utohexstr(insn));
  return 0;
}

static void or32le(uint8_t *p, int32_t v) { write32le(p, read32le(p) | v); }

void Hexagon::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  switch (rel.type) {
  case R_HEX_NONE:
    break;
  case R_HEX_6_PCREL_X:
  case R_HEX_6_X:
    or32le(loc, applyMask(findMaskR6(read32le(loc)), val));
    break;
  case R_HEX_8_X:
    or32le(loc, applyMask(findMaskR8(read32le(loc)), val));
    break;
  case R_HEX_9_X:
    or32le(loc, applyMask(0x00003fe0, val & 0x3f));
    break;
  case R_HEX_10_X:
    or32le(loc, applyMask(0x00203fe0, val & 0x3f));
    break;
  case R_HEX_11_X:
  case R_HEX_GD_GOT_11_X:
  case R_HEX_IE_GOT_11_X:
  case R_HEX_GOT_11_X:
  case R_HEX_GOTREL_11_X:
  case R_HEX_TPREL_11_X:
    or32le(loc, applyMask(findMaskR11(read32le(loc)), val & 0x3f));
    break;
  case R_HEX_12_X:
    or32le(loc, applyMask(0x000007e0, val));
    break;
  case R_HEX_16_X: // These relocs only have 6 effective bits.
  case R_HEX_IE_16_X:
  case R_HEX_IE_GOT_16_X:
  case R_HEX_GD_GOT_16_X:
  case R_HEX_GOT_16_X:
  case R_HEX_GOTREL_16_X:
  case R_HEX_TPREL_16_X:
    or32le(loc, applyMask(findMaskR16(read32le(loc)), val & 0x3f));
    break;
  case R_HEX_TPREL_16:
    or32le(loc, applyMask(findMaskR16(read32le(loc)), val & 0xffff));
    break;
  case R_HEX_32:
  case R_HEX_32_PCREL:
  case R_HEX_DTPREL_32:
    or32le(loc, val);
    break;
  case R_HEX_32_6_X:
  case R_HEX_GD_GOT_32_6_X:
  case R_HEX_GOT_32_6_X:
  case R_HEX_GOTREL_32_6_X:
  case R_HEX_IE_GOT_32_6_X:
  case R_HEX_IE_32_6_X:
  case R_HEX_TPREL_32_6_X:
    or32le(loc, applyMask(0x0fff3fff, val >> 6));
    break;
  case R_HEX_B9_PCREL:
    checkInt(loc, val, 11, rel);
    or32le(loc, applyMask(0x003000fe, val >> 2));
    break;
  case R_HEX_B9_PCREL_X:
    or32le(loc, applyMask(0x003000fe, val & 0x3f));
    break;
  case R_HEX_B13_PCREL:
    checkInt(loc, val, 15, rel);
    or32le(loc, applyMask(0x00202ffe, val >> 2));
    break;
  case R_HEX_B15_PCREL:
    checkInt(loc, val, 17, rel);
    or32le(loc, applyMask(0x00df20fe, val >> 2));
    break;
  case R_HEX_B15_PCREL_X:
    or32le(loc, applyMask(0x00df20fe, val & 0x3f));
    break;
  case R_HEX_B22_PCREL:
  case R_HEX_GD_PLT_B22_PCREL:
  case R_HEX_PLT_B22_PCREL:
    checkInt(loc, val, 24, rel);
    or32le(loc, applyMask(0x1ff3ffe, val >> 2));
    break;
  case R_HEX_B22_PCREL_X:
  case R_HEX_GD_PLT_B22_PCREL_X:
    or32le(loc, applyMask(0x1ff3ffe, val & 0x3f));
    break;
  case R_HEX_B32_PCREL_X:
  case R_HEX_GD_PLT_B32_PCREL_X:
    or32le(loc, applyMask(0x0fff3fff, val >> 6));
    break;
  case R_HEX_GOTREL_HI16:
  case R_HEX_HI16:
  case R_HEX_IE_GOT_HI16:
  case R_HEX_IE_HI16:
  case R_HEX_TPREL_HI16:
    or32le(loc, applyMask(0x00c03fff, val >> 16));
    break;
  case R_HEX_GOTREL_LO16:
  case R_HEX_LO16:
  case R_HEX_IE_GOT_LO16:
  case R_HEX_IE_LO16:
  case R_HEX_TPREL_LO16:
    or32le(loc, applyMask(0x00c03fff, val));
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

void Hexagon::writePltHeader(uint8_t *buf) const {
  const uint8_t pltData[] = {
      0x00, 0x40, 0x00, 0x00, // { immext (#0)
      0x1c, 0xc0, 0x49, 0x6a, //   r28 = add (pc, ##GOT0@PCREL) } # @GOT0
      0x0e, 0x42, 0x9c, 0xe2, // { r14 -= add (r28, #16)  # offset of GOTn
      0x4f, 0x40, 0x9c, 0x91, //   r15 = memw (r28 + #8)  # object ID at GOT2
      0x3c, 0xc0, 0x9c, 0x91, //   r28 = memw (r28 + #4) }# dynamic link at GOT1
      0x0e, 0x42, 0x0e, 0x8c, // { r14 = asr (r14, #2)    # index of PLTn
      0x00, 0xc0, 0x9c, 0x52, //   jumpr r28 }            # call dynamic linker
      0x0c, 0xdb, 0x00, 0x54, // trap0(#0xdb) # bring plt0 into 16byte alignment
  };
  memcpy(buf, pltData, sizeof(pltData));

  // Offset from PLT0 to the GOT.
  uint64_t off = in.gotPlt->getVA() - in.plt->getVA();
  relocateNoSym(buf, R_HEX_B32_PCREL_X, off);
  relocateNoSym(buf + 4, R_HEX_6_PCREL_X, off);
}

void Hexagon::writePlt(uint8_t *buf, const Symbol &sym,
                       uint64_t pltEntryAddr) const {
  const uint8_t inst[] = {
      0x00, 0x40, 0x00, 0x00, // { immext (#0)
      0x0e, 0xc0, 0x49, 0x6a, //   r14 = add (pc, ##GOTn@PCREL) }
      0x1c, 0xc0, 0x8e, 0x91, // r28 = memw (r14)
      0x00, 0xc0, 0x9c, 0x52, // jumpr r28
  };
  memcpy(buf, inst, sizeof(inst));

  uint64_t gotPltEntryAddr = sym.getGotPltVA();
  relocateNoSym(buf, R_HEX_B32_PCREL_X, gotPltEntryAddr - pltEntryAddr);
  relocateNoSym(buf + 4, R_HEX_6_PCREL_X, gotPltEntryAddr - pltEntryAddr);
}

RelType Hexagon::getDynRel(RelType type) const {
  if (type == R_HEX_32)
    return type;
  return R_HEX_NONE;
}

int64_t Hexagon::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_HEX_NONE:
  case R_HEX_GLOB_DAT:
  case R_HEX_JMP_SLOT:
    return 0;
  case R_HEX_32:
  case R_HEX_RELATIVE:
  case R_HEX_DTPMOD_32:
  case R_HEX_DTPREL_32:
  case R_HEX_TPREL_32:
    return SignExtend64<32>(read32(buf));
  default:
    internalLinkerError(getErrorLocation(buf),
                        "cannot read addend for relocation " + toString(type));
    return 0;
  }
}

TargetInfo *elf::getHexagonTargetInfo() {
  static Hexagon target;
  return &target;
}
