//===- AVR.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AVR is a Harvard-architecture 8-bit microcontroller designed for small
// baremetal programs. All AVR-family processors have 32 8-bit registers.
// The tiniest AVR has 32 byte RAM and 1 KiB program memory, and the largest
// one supports up to 2^24 data address space and 2^22 code address space.
//
// Since it is a baremetal programming, there's usually no loader to load
// ELF files on AVRs. You are expected to link your program against address
// 0 and pull out a .text section from the result using objcopy, so that you
// can write the linked code to on-chip flush memory. You can do that with
// the following commands:
//
//   ld.lld -Ttext=0 -o foo foo.o
//   objcopy -O binary --only-section=.text foo output.bin
//
// Note that the current AVR support is very preliminary so you can't
// link any useful program yet, though.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "Thunks.h"
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
class AVR final : public TargetInfo {
public:
  AVR() { needsThunks = true; }
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  bool needsThunk(RelExpr expr, RelType type, const InputFile *file,
                  uint64_t branchAddr, const Symbol &s,
                  int64_t a) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

RelExpr AVR::getRelExpr(RelType type, const Symbol &s,
                        const uint8_t *loc) const {
  switch (type) {
  case R_AVR_6:
  case R_AVR_6_ADIW:
  case R_AVR_8:
  case R_AVR_8_LO8:
  case R_AVR_8_HI8:
  case R_AVR_8_HLO8:
  case R_AVR_16:
  case R_AVR_16_PM:
  case R_AVR_32:
  case R_AVR_LDI:
  case R_AVR_LO8_LDI:
  case R_AVR_LO8_LDI_NEG:
  case R_AVR_HI8_LDI:
  case R_AVR_HI8_LDI_NEG:
  case R_AVR_HH8_LDI_NEG:
  case R_AVR_HH8_LDI:
  case R_AVR_MS8_LDI_NEG:
  case R_AVR_MS8_LDI:
  case R_AVR_LO8_LDI_GS:
  case R_AVR_LO8_LDI_PM:
  case R_AVR_LO8_LDI_PM_NEG:
  case R_AVR_HI8_LDI_GS:
  case R_AVR_HI8_LDI_PM:
  case R_AVR_HI8_LDI_PM_NEG:
  case R_AVR_HH8_LDI_PM:
  case R_AVR_HH8_LDI_PM_NEG:
  case R_AVR_LDS_STS_16:
  case R_AVR_PORT5:
  case R_AVR_PORT6:
  case R_AVR_CALL:
    return R_ABS;
  case R_AVR_7_PCREL:
  case R_AVR_13_PCREL:
    return R_PC;
  default:
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

static void writeLDI(uint8_t *loc, uint64_t val) {
  write16le(loc, (read16le(loc) & 0xf0f0) | (val & 0xf0) << 4 | (val & 0x0f));
}

bool AVR::needsThunk(RelExpr expr, RelType type, const InputFile *file,
                     uint64_t branchAddr, const Symbol &s, int64_t a) const {
  switch (type) {
  case R_AVR_LO8_LDI_GS:
  case R_AVR_HI8_LDI_GS:
    // A thunk is needed if the symbol's virtual address is out of range
    // [0, 0x1ffff].
    return s.getVA() >= 0x20000;
  default:
    return false;
  }
}

void AVR::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_AVR_8:
    checkUInt(loc, val, 8, rel);
    *loc = val;
    break;
  case R_AVR_8_LO8:
    checkUInt(loc, val, 32, rel);
    *loc = val & 0xff;
    break;
  case R_AVR_8_HI8:
    checkUInt(loc, val, 32, rel);
    *loc = (val >> 8) & 0xff;
    break;
  case R_AVR_8_HLO8:
    checkUInt(loc, val, 32, rel);
    *loc = (val >> 16) & 0xff;
    break;
  case R_AVR_16:
    // Note: this relocation is often used between code and data space, which
    // are 0x800000 apart in the output ELF file. The bitmask cuts off the high
    // bit.
    write16le(loc, val & 0xffff);
    break;
  case R_AVR_16_PM:
    checkAlignment(loc, val, 2, rel);
    checkUInt(loc, val >> 1, 16, rel);
    write16le(loc, val >> 1);
    break;
  case R_AVR_32:
    checkUInt(loc, val, 32, rel);
    write32le(loc, val);
    break;

  case R_AVR_LDI:
    checkUInt(loc, val, 8, rel);
    writeLDI(loc, val & 0xff);
    break;

  case R_AVR_LO8_LDI_NEG:
    writeLDI(loc, -val & 0xff);
    break;
  case R_AVR_LO8_LDI:
    writeLDI(loc, val & 0xff);
    break;
  case R_AVR_HI8_LDI_NEG:
    writeLDI(loc, (-val >> 8) & 0xff);
    break;
  case R_AVR_HI8_LDI:
    writeLDI(loc, (val >> 8) & 0xff);
    break;
  case R_AVR_HH8_LDI_NEG:
    writeLDI(loc, (-val >> 16) & 0xff);
    break;
  case R_AVR_HH8_LDI:
    writeLDI(loc, (val >> 16) & 0xff);
    break;
  case R_AVR_MS8_LDI_NEG:
    writeLDI(loc, (-val >> 24) & 0xff);
    break;
  case R_AVR_MS8_LDI:
    writeLDI(loc, (val >> 24) & 0xff);
    break;

  case R_AVR_LO8_LDI_GS:
    checkUInt(loc, val, 17, rel);
    [[fallthrough]];
  case R_AVR_LO8_LDI_PM:
    checkAlignment(loc, val, 2, rel);
    writeLDI(loc, (val >> 1) & 0xff);
    break;
  case R_AVR_HI8_LDI_GS:
    checkUInt(loc, val, 17, rel);
    [[fallthrough]];
  case R_AVR_HI8_LDI_PM:
    checkAlignment(loc, val, 2, rel);
    writeLDI(loc, (val >> 9) & 0xff);
    break;
  case R_AVR_HH8_LDI_PM:
    checkAlignment(loc, val, 2, rel);
    writeLDI(loc, (val >> 17) & 0xff);
    break;

  case R_AVR_LO8_LDI_PM_NEG:
    checkAlignment(loc, val, 2, rel);
    writeLDI(loc, (-val >> 1) & 0xff);
    break;
  case R_AVR_HI8_LDI_PM_NEG:
    checkAlignment(loc, val, 2, rel);
    writeLDI(loc, (-val >> 9) & 0xff);
    break;
  case R_AVR_HH8_LDI_PM_NEG:
    checkAlignment(loc, val, 2, rel);
    writeLDI(loc, (-val >> 17) & 0xff);
    break;

  case R_AVR_LDS_STS_16: {
    checkUInt(loc, val, 7, rel);
    const uint16_t hi = val >> 4;
    const uint16_t lo = val & 0xf;
    write16le(loc, (read16le(loc) & 0xf8f0) | ((hi << 8) | lo));
    break;
  }

  case R_AVR_PORT5:
    checkUInt(loc, val, 5, rel);
    write16le(loc, (read16le(loc) & 0xff07) | (val << 3));
    break;
  case R_AVR_PORT6:
    checkUInt(loc, val, 6, rel);
    write16le(loc, (read16le(loc) & 0xf9f0) | (val & 0x30) << 5 | (val & 0x0f));
    break;

  // Since every jump destination is word aligned we gain an extra bit
  case R_AVR_7_PCREL: {
    checkInt(loc, val - 2, 8, rel);
    checkAlignment(loc, val, 2, rel);
    const uint16_t target = (val - 2) >> 1;
    write16le(loc, (read16le(loc) & 0xfc07) | ((target & 0x7f) << 3));
    break;
  }
  case R_AVR_13_PCREL: {
    checkAlignment(loc, val, 2, rel);
    const uint16_t target = (val - 2) >> 1;
    write16le(loc, (read16le(loc) & 0xf000) | (target & 0xfff));
    break;
  }

  case R_AVR_6:
    checkInt(loc, val, 6, rel);
    write16le(loc, (read16le(loc) & 0xd3f8) | (val & 0x20) << 8 |
                       (val & 0x18) << 7 | (val & 0x07));
    break;
  case R_AVR_6_ADIW:
    checkInt(loc, val, 6, rel);
    write16le(loc, (read16le(loc) & 0xff30) | (val & 0x30) << 2 | (val & 0x0F));
    break;

  case R_AVR_CALL: {
    checkAlignment(loc, val, 2, rel);
    uint16_t hi = val >> 17;
    uint16_t lo = val >> 1;
    write16le(loc, read16le(loc) | ((hi >> 1) << 4) | (hi & 1));
    write16le(loc + 2, lo);
    break;
  }
  default:
    llvm_unreachable("unknown relocation");
  }
}

TargetInfo *elf::getAVRTargetInfo() {
  static AVR target;
  return &target;
}

static uint32_t getEFlags(InputFile *file) {
  return cast<ObjFile<ELF32LE>>(file)->getObj().getHeader().e_flags;
}

uint32_t AVR::calcEFlags() const {
  assert(!ctx.objectFiles.empty());

  uint32_t flags = getEFlags(ctx.objectFiles[0]);
  bool hasLinkRelaxFlag = flags & EF_AVR_LINKRELAX_PREPARED;

  for (InputFile *f : ArrayRef(ctx.objectFiles).slice(1)) {
    uint32_t objFlags = getEFlags(f);
    if ((objFlags & EF_AVR_ARCH_MASK) != (flags & EF_AVR_ARCH_MASK))
      error(toString(f) +
            ": cannot link object files with incompatible target ISA");
    if (!(objFlags & EF_AVR_LINKRELAX_PREPARED))
      hasLinkRelaxFlag = false;
  }

  if (!hasLinkRelaxFlag)
    flags &= ~EF_AVR_LINKRELAX_PREPARED;

  return flags;
}
