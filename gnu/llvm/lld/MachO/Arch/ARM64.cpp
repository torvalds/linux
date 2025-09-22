//===- ARM64.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Arch/ARM64Common.h"
#include "InputFiles.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"

#include "lld/Common/ErrorHandler.h"
#include "mach-o/compact_unwind_encoding.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::macho;

namespace {

struct ARM64 : ARM64Common {
  ARM64();
  void writeStub(uint8_t *buf, const Symbol &, uint64_t) const override;
  void writeStubHelperHeader(uint8_t *buf) const override;
  void writeStubHelperEntry(uint8_t *buf, const Symbol &,
                            uint64_t entryAddr) const override;

  void writeObjCMsgSendStub(uint8_t *buf, Symbol *sym, uint64_t stubsAddr,
                            uint64_t &stubOffset, uint64_t selrefVA,
                            Symbol *objcMsgSend) const override;
  void populateThunk(InputSection *thunk, Symbol *funcSym) override;
  void applyOptimizationHints(uint8_t *, const ObjFile &) const override;
};

} // namespace

// Random notes on reloc types:
// ADDEND always pairs with BRANCH26, PAGE21, or PAGEOFF12
// POINTER_TO_GOT: ld64 supports a 4-byte pc-relative form as well as an 8-byte
// absolute version of this relocation. The semantics of the absolute relocation
// are weird -- it results in the value of the GOT slot being written, instead
// of the address. Let's not support it unless we find a real-world use case.
static constexpr std::array<RelocAttrs, 11> relocAttrsArray{{
#define B(x) RelocAttrBits::x
    {"UNSIGNED",
     B(UNSIGNED) | B(ABSOLUTE) | B(EXTERN) | B(LOCAL) | B(BYTE4) | B(BYTE8)},
    {"SUBTRACTOR", B(SUBTRAHEND) | B(EXTERN) | B(BYTE4) | B(BYTE8)},
    {"BRANCH26", B(PCREL) | B(EXTERN) | B(BRANCH) | B(BYTE4)},
    {"PAGE21", B(PCREL) | B(EXTERN) | B(BYTE4)},
    {"PAGEOFF12", B(ABSOLUTE) | B(EXTERN) | B(BYTE4)},
    {"GOT_LOAD_PAGE21", B(PCREL) | B(EXTERN) | B(GOT) | B(BYTE4)},
    {"GOT_LOAD_PAGEOFF12",
     B(ABSOLUTE) | B(EXTERN) | B(GOT) | B(LOAD) | B(BYTE4)},
    {"POINTER_TO_GOT", B(PCREL) | B(EXTERN) | B(GOT) | B(POINTER) | B(BYTE4)},
    {"TLVP_LOAD_PAGE21", B(PCREL) | B(EXTERN) | B(TLV) | B(BYTE4)},
    {"TLVP_LOAD_PAGEOFF12",
     B(ABSOLUTE) | B(EXTERN) | B(TLV) | B(LOAD) | B(BYTE4)},
    {"ADDEND", B(ADDEND)},
#undef B
}};

static constexpr uint32_t stubCode[] = {
    0x90000010, // 00: adrp  x16, __la_symbol_ptr@page
    0xf9400210, // 04: ldr   x16, [x16, __la_symbol_ptr@pageoff]
    0xd61f0200, // 08: br    x16
};

void ARM64::writeStub(uint8_t *buf8, const Symbol &sym,
                      uint64_t pointerVA) const {
  ::writeStub(buf8, stubCode, sym, pointerVA);
}

static constexpr uint32_t stubHelperHeaderCode[] = {
    0x90000011, // 00: adrp  x17, _dyld_private@page
    0x91000231, // 04: add   x17, x17, _dyld_private@pageoff
    0xa9bf47f0, // 08: stp   x16/x17, [sp, #-16]!
    0x90000010, // 0c: adrp  x16, dyld_stub_binder@page
    0xf9400210, // 10: ldr   x16, [x16, dyld_stub_binder@pageoff]
    0xd61f0200, // 14: br    x16
};

void ARM64::writeStubHelperHeader(uint8_t *buf8) const {
  ::writeStubHelperHeader<LP64>(buf8, stubHelperHeaderCode);
}

static constexpr uint32_t stubHelperEntryCode[] = {
    0x18000050, // 00: ldr  w16, l0
    0x14000000, // 04: b    stubHelperHeader
    0x00000000, // 08: l0: .long 0
};

void ARM64::writeStubHelperEntry(uint8_t *buf8, const Symbol &sym,
                                 uint64_t entryVA) const {
  ::writeStubHelperEntry(buf8, stubHelperEntryCode, sym, entryVA);
}

static constexpr uint32_t objcStubsFastCode[] = {
    0x90000001, // adrp  x1, __objc_selrefs@page
    0xf9400021, // ldr   x1, [x1, @selector("foo")@pageoff]
    0x90000010, // adrp  x16, _got@page
    0xf9400210, // ldr   x16, [x16, _objc_msgSend@pageoff]
    0xd61f0200, // br    x16
    0xd4200020, // brk   #0x1
    0xd4200020, // brk   #0x1
    0xd4200020, // brk   #0x1
};

static constexpr uint32_t objcStubsSmallCode[] = {
    0x90000001, // adrp  x1, __objc_selrefs@page
    0xf9400021, // ldr   x1, [x1, @selector("foo")@pageoff]
    0x14000000, // b     _objc_msgSend
};

void ARM64::writeObjCMsgSendStub(uint8_t *buf, Symbol *sym, uint64_t stubsAddr,
                                 uint64_t &stubOffset, uint64_t selrefVA,
                                 Symbol *objcMsgSend) const {
  uint64_t objcMsgSendAddr;
  uint64_t objcStubSize;
  uint64_t objcMsgSendIndex;

  if (config->objcStubsMode == ObjCStubsMode::fast) {
    objcStubSize = target->objcStubsFastSize;
    objcMsgSendAddr = in.got->addr;
    objcMsgSendIndex = objcMsgSend->gotIndex;
    ::writeObjCMsgSendFastStub<LP64>(buf, objcStubsFastCode, sym, stubsAddr,
                                     stubOffset, selrefVA, objcMsgSendAddr,
                                     objcMsgSendIndex);
  } else {
    assert(config->objcStubsMode == ObjCStubsMode::small);
    objcStubSize = target->objcStubsSmallSize;
    if (auto *d = dyn_cast<Defined>(objcMsgSend)) {
      objcMsgSendAddr = d->getVA();
      objcMsgSendIndex = 0;
    } else {
      objcMsgSendAddr = in.stubs->addr;
      objcMsgSendIndex = objcMsgSend->stubsIndex;
    }
    ::writeObjCMsgSendSmallStub<LP64>(buf, objcStubsSmallCode, sym, stubsAddr,
                                      stubOffset, selrefVA, objcMsgSendAddr,
                                      objcMsgSendIndex);
  }
  stubOffset += objcStubSize;
}

// A thunk is the relaxed variation of stubCode. We don't need the
// extra indirection through a lazy pointer because the target address
// is known at link time.
static constexpr uint32_t thunkCode[] = {
    0x90000010, // 00: adrp  x16, <thunk.ptr>@page
    0x91000210, // 04: add   x16, [x16,<thunk.ptr>@pageoff]
    0xd61f0200, // 08: br    x16
};

void ARM64::populateThunk(InputSection *thunk, Symbol *funcSym) {
  thunk->align = 4;
  thunk->data = {reinterpret_cast<const uint8_t *>(thunkCode),
                 sizeof(thunkCode)};
  thunk->relocs.emplace_back(/*type=*/ARM64_RELOC_PAGEOFF12,
                             /*pcrel=*/false, /*length=*/2,
                             /*offset=*/4, /*addend=*/0,
                             /*referent=*/funcSym);
  thunk->relocs.emplace_back(/*type=*/ARM64_RELOC_PAGE21,
                             /*pcrel=*/true, /*length=*/2,
                             /*offset=*/0, /*addend=*/0,
                             /*referent=*/funcSym);
}

ARM64::ARM64() : ARM64Common(LP64()) {
  cpuType = CPU_TYPE_ARM64;
  cpuSubtype = CPU_SUBTYPE_ARM64_ALL;

  stubSize = sizeof(stubCode);
  thunkSize = sizeof(thunkCode);

  objcStubsFastSize = sizeof(objcStubsFastCode);
  objcStubsFastAlignment = 32;
  objcStubsSmallSize = sizeof(objcStubsSmallCode);
  objcStubsSmallAlignment = 4;

  // Branch immediate is two's complement 26 bits, which is implicitly
  // multiplied by 4 (since all functions are 4-aligned: The branch range
  // is -4*(2**(26-1))..4*(2**(26-1) - 1).
  backwardBranchRange = 128 * 1024 * 1024;
  forwardBranchRange = backwardBranchRange - 4;

  modeDwarfEncoding = UNWIND_ARM64_MODE_DWARF;
  subtractorRelocType = ARM64_RELOC_SUBTRACTOR;
  unsignedRelocType = ARM64_RELOC_UNSIGNED;

  stubHelperHeaderSize = sizeof(stubHelperHeaderCode);
  stubHelperEntrySize = sizeof(stubHelperEntryCode);

  relocAttrs = {relocAttrsArray.data(), relocAttrsArray.size()};
}

namespace {
struct Adrp {
  uint32_t destRegister;
  int64_t addend;
};

struct Add {
  uint8_t destRegister;
  uint8_t srcRegister;
  uint32_t addend;
};

enum ExtendType { ZeroExtend = 1, Sign64 = 2, Sign32 = 3 };

struct Ldr {
  uint8_t destRegister;
  uint8_t baseRegister;
  uint8_t p2Size;
  bool isFloat;
  ExtendType extendType;
  int64_t offset;
};
} // namespace

static bool parseAdrp(uint32_t insn, Adrp &adrp) {
  if ((insn & 0x9f000000) != 0x90000000)
    return false;
  adrp.destRegister = insn & 0x1f;
  uint64_t immHi = (insn >> 5) & 0x7ffff;
  uint64_t immLo = (insn >> 29) & 0x3;
  adrp.addend = SignExtend64<21>(immLo | (immHi << 2)) * 4096;
  return true;
}

static bool parseAdd(uint32_t insn, Add &add) {
  if ((insn & 0xffc00000) != 0x91000000)
    return false;
  add.destRegister = insn & 0x1f;
  add.srcRegister = (insn >> 5) & 0x1f;
  add.addend = (insn >> 10) & 0xfff;
  return true;
}

static bool parseLdr(uint32_t insn, Ldr &ldr) {
  ldr.destRegister = insn & 0x1f;
  ldr.baseRegister = (insn >> 5) & 0x1f;
  uint8_t size = insn >> 30;
  uint8_t opc = (insn >> 22) & 3;

  if ((insn & 0x3fc00000) == 0x39400000) {
    // LDR (immediate), LDRB (immediate), LDRH (immediate)
    ldr.p2Size = size;
    ldr.extendType = ZeroExtend;
    ldr.isFloat = false;
  } else if ((insn & 0x3f800000) == 0x39800000) {
    // LDRSB (immediate), LDRSH (immediate), LDRSW (immediate)
    ldr.p2Size = size;
    ldr.extendType = static_cast<ExtendType>(opc);
    ldr.isFloat = false;
  } else if ((insn & 0x3f400000) == 0x3d400000) {
    // LDR (immediate, SIMD&FP)
    ldr.extendType = ZeroExtend;
    ldr.isFloat = true;
    if (opc == 1)
      ldr.p2Size = size;
    else if (size == 0 && opc == 3)
      ldr.p2Size = 4;
    else
      return false;
  } else {
    return false;
  }
  ldr.offset = ((insn >> 10) & 0xfff) << ldr.p2Size;
  return true;
}

static bool isValidAdrOffset(int32_t delta) { return isInt<21>(delta); }

static void writeAdr(void *loc, uint32_t dest, int32_t delta) {
  assert(isValidAdrOffset(delta));
  uint32_t opcode = 0x10000000;
  uint32_t immHi = (delta & 0x001ffffc) << 3;
  uint32_t immLo = (delta & 0x00000003) << 29;
  write32le(loc, opcode | immHi | immLo | dest);
}

static void writeNop(void *loc) { write32le(loc, 0xd503201f); }

static bool isLiteralLdrEligible(const Ldr &ldr) {
  return ldr.p2Size > 1 && isShiftedInt<19, 2>(ldr.offset);
}

static void writeLiteralLdr(void *loc, const Ldr &ldr) {
  assert(isLiteralLdrEligible(ldr));
  uint32_t imm19 = (ldr.offset / 4 & maskTrailingOnes<uint32_t>(19)) << 5;
  uint32_t opcode;
  switch (ldr.p2Size) {
  case 2:
    if (ldr.isFloat)
      opcode = 0x1c000000;
    else
      opcode = ldr.extendType == Sign64 ? 0x98000000 : 0x18000000;
    break;
  case 3:
    opcode = ldr.isFloat ? 0x5c000000 : 0x58000000;
    break;
  case 4:
    opcode = 0x9c000000;
    break;
  default:
    llvm_unreachable("Invalid literal ldr size");
  }
  write32le(loc, opcode | imm19 | ldr.destRegister);
}

static bool isImmediateLdrEligible(const Ldr &ldr) {
  // Note: We deviate from ld64's behavior, which converts to immediate loads
  // only if ldr.offset < 4096, even though the offset is divided by the load's
  // size in the 12-bit immediate operand. Only the unsigned offset variant is
  // supported.

  uint32_t size = 1 << ldr.p2Size;
  return ldr.offset >= 0 && (ldr.offset % size) == 0 &&
         isUInt<12>(ldr.offset >> ldr.p2Size);
}

static void writeImmediateLdr(void *loc, const Ldr &ldr) {
  assert(isImmediateLdrEligible(ldr));
  uint32_t opcode = 0x39000000;
  if (ldr.isFloat) {
    opcode |= 0x04000000;
    assert(ldr.extendType == ZeroExtend);
  }
  opcode |= ldr.destRegister;
  opcode |= ldr.baseRegister << 5;
  uint8_t size, opc;
  if (ldr.p2Size == 4) {
    size = 0;
    opc = 3;
  } else {
    opc = ldr.extendType;
    size = ldr.p2Size;
  }
  uint32_t immBits = ldr.offset >> ldr.p2Size;
  write32le(loc, opcode | (immBits << 10) | (opc << 22) | (size << 30));
}

// Transforms a pair of adrp+add instructions into an adr instruction if the
// target is within the +/- 1 MiB range allowed by the adr's 21 bit signed
// immediate offset.
//
//   adrp xN, _foo@PAGE
//   add  xM, xN, _foo@PAGEOFF
// ->
//   adr  xM, _foo
//   nop
static void applyAdrpAdd(uint8_t *buf, const ConcatInputSection *isec,
                         uint64_t offset1, uint64_t offset2) {
  uint32_t ins1 = read32le(buf + offset1);
  uint32_t ins2 = read32le(buf + offset2);
  Adrp adrp;
  Add add;
  if (!parseAdrp(ins1, adrp) || !parseAdd(ins2, add))
    return;
  if (adrp.destRegister != add.srcRegister)
    return;

  uint64_t addr1 = isec->getVA() + offset1;
  uint64_t referent = pageBits(addr1) + adrp.addend + add.addend;
  int64_t delta = referent - addr1;
  if (!isValidAdrOffset(delta))
    return;

  writeAdr(buf + offset1, add.destRegister, delta);
  writeNop(buf + offset2);
}

// Transforms two adrp instructions into a single adrp if their referent
// addresses are located on the same 4096 byte page.
//
//   adrp xN, _foo@PAGE
//   adrp xN, _bar@PAGE
// ->
//   adrp xN, _foo@PAGE
//   nop
static void applyAdrpAdrp(uint8_t *buf, const ConcatInputSection *isec,
                          uint64_t offset1, uint64_t offset2) {
  uint32_t ins1 = read32le(buf + offset1);
  uint32_t ins2 = read32le(buf + offset2);
  Adrp adrp1, adrp2;
  if (!parseAdrp(ins1, adrp1) || !parseAdrp(ins2, adrp2))
    return;
  if (adrp1.destRegister != adrp2.destRegister)
    return;

  uint64_t page1 = pageBits(offset1 + isec->getVA()) + adrp1.addend;
  uint64_t page2 = pageBits(offset2 + isec->getVA()) + adrp2.addend;
  if (page1 != page2)
    return;

  writeNop(buf + offset2);
}

// Transforms a pair of adrp+ldr (immediate) instructions into an ldr (literal)
// load from a PC-relative address if it is 4-byte aligned and within +/- 1 MiB,
// as ldr can encode a signed 19-bit offset that gets multiplied by 4.
//
//   adrp xN, _foo@PAGE
//   ldr  xM, [xN, _foo@PAGEOFF]
// ->
//   nop
//   ldr  xM, _foo
static void applyAdrpLdr(uint8_t *buf, const ConcatInputSection *isec,
                         uint64_t offset1, uint64_t offset2) {
  uint32_t ins1 = read32le(buf + offset1);
  uint32_t ins2 = read32le(buf + offset2);
  Adrp adrp;
  Ldr ldr;
  if (!parseAdrp(ins1, adrp) || !parseLdr(ins2, ldr))
    return;
  if (adrp.destRegister != ldr.baseRegister)
    return;

  uint64_t addr1 = isec->getVA() + offset1;
  uint64_t addr2 = isec->getVA() + offset2;
  uint64_t referent = pageBits(addr1) + adrp.addend + ldr.offset;
  ldr.offset = referent - addr2;
  if (!isLiteralLdrEligible(ldr))
    return;

  writeNop(buf + offset1);
  writeLiteralLdr(buf + offset2, ldr);
}

// GOT loads are emitted by the compiler as a pair of adrp and ldr instructions,
// but they may be changed to adrp+add by relaxGotLoad(). This hint performs
// the AdrpLdr or AdrpAdd transformation depending on whether it was relaxed.
static void applyAdrpLdrGot(uint8_t *buf, const ConcatInputSection *isec,
                            uint64_t offset1, uint64_t offset2) {
  uint32_t ins2 = read32le(buf + offset2);
  Add add;
  Ldr ldr;
  if (parseAdd(ins2, add))
    applyAdrpAdd(buf, isec, offset1, offset2);
  else if (parseLdr(ins2, ldr))
    applyAdrpLdr(buf, isec, offset1, offset2);
}

// Optimizes an adrp+add+ldr sequence used for loading from a local symbol's
// address by loading directly if it's close enough, or to an adrp(p)+ldr
// sequence if it's not.
//
//   adrp x0, _foo@PAGE
//   add  x1, x0, _foo@PAGEOFF
//   ldr  x2, [x1, #off]
static void applyAdrpAddLdr(uint8_t *buf, const ConcatInputSection *isec,
                            uint64_t offset1, uint64_t offset2,
                            uint64_t offset3) {
  uint32_t ins1 = read32le(buf + offset1);
  Adrp adrp;
  if (!parseAdrp(ins1, adrp))
    return;
  uint32_t ins2 = read32le(buf + offset2);
  Add add;
  if (!parseAdd(ins2, add))
    return;
  uint32_t ins3 = read32le(buf + offset3);
  Ldr ldr;
  if (!parseLdr(ins3, ldr))
    return;
  if (adrp.destRegister != add.srcRegister)
    return;
  if (add.destRegister != ldr.baseRegister)
    return;

  // Load from the target address directly.
  //   nop
  //   nop
  //   ldr x2, [_foo + #off]
  uint64_t addr1 = isec->getVA() + offset1;
  uint64_t addr3 = isec->getVA() + offset3;
  uint64_t referent = pageBits(addr1) + adrp.addend + add.addend;
  Ldr literalLdr = ldr;
  literalLdr.offset += referent - addr3;
  if (isLiteralLdrEligible(literalLdr)) {
    writeNop(buf + offset1);
    writeNop(buf + offset2);
    writeLiteralLdr(buf + offset3, literalLdr);
    return;
  }

  // Load the target address into a register and load from there indirectly.
  //   adr x1, _foo
  //   nop
  //   ldr x2, [x1, #off]
  int64_t adrOffset = referent - addr1;
  if (isValidAdrOffset(adrOffset)) {
    writeAdr(buf + offset1, ldr.baseRegister, adrOffset);
    // Note: ld64 moves the offset into the adr instruction for AdrpAddLdr, but
    // not for AdrpLdrGotLdr. Its effect is the same either way.
    writeNop(buf + offset2);
    return;
  }

  // Move the target's page offset into the ldr's immediate offset.
  //   adrp x0, _foo@PAGE
  //   nop
  //   ldr x2, [x0, _foo@PAGEOFF + #off]
  Ldr immediateLdr = ldr;
  immediateLdr.baseRegister = adrp.destRegister;
  immediateLdr.offset += add.addend;
  if (isImmediateLdrEligible(immediateLdr)) {
    writeNop(buf + offset2);
    writeImmediateLdr(buf + offset3, immediateLdr);
    return;
  }
}

// Relaxes a GOT-indirect load.
// If the referenced symbol is external and its GOT entry is within +/- 1 MiB,
// the GOT entry can be loaded with a single literal ldr instruction.
// If the referenced symbol is local and thus has been relaxed to adrp+add+ldr,
// we perform the AdrpAddLdr transformation.
static void applyAdrpLdrGotLdr(uint8_t *buf, const ConcatInputSection *isec,
                               uint64_t offset1, uint64_t offset2,
                               uint64_t offset3) {
  uint32_t ins2 = read32le(buf + offset2);
  Add add;
  Ldr ldr2;

  if (parseAdd(ins2, add)) {
    applyAdrpAddLdr(buf, isec, offset1, offset2, offset3);
  } else if (parseLdr(ins2, ldr2)) {
    // adrp x1, _foo@GOTPAGE
    // ldr  x2, [x1, _foo@GOTPAGEOFF]
    // ldr  x3, [x2, #off]

    uint32_t ins1 = read32le(buf + offset1);
    Adrp adrp;
    if (!parseAdrp(ins1, adrp))
      return;
    uint32_t ins3 = read32le(buf + offset3);
    Ldr ldr3;
    if (!parseLdr(ins3, ldr3))
      return;

    if (ldr2.baseRegister != adrp.destRegister)
      return;
    if (ldr3.baseRegister != ldr2.destRegister)
      return;
    // Loads from the GOT must be pointer sized.
    if (ldr2.p2Size != 3 || ldr2.isFloat)
      return;

    uint64_t addr1 = isec->getVA() + offset1;
    uint64_t addr2 = isec->getVA() + offset2;
    uint64_t referent = pageBits(addr1) + adrp.addend + ldr2.offset;
    // Load the GOT entry's address directly.
    //   nop
    //   ldr x2, _foo@GOTPAGE + _foo@GOTPAGEOFF
    //   ldr x3, [x2, #off]
    Ldr literalLdr = ldr2;
    literalLdr.offset = referent - addr2;
    if (isLiteralLdrEligible(literalLdr)) {
      writeNop(buf + offset1);
      writeLiteralLdr(buf + offset2, literalLdr);
    }
  }
}

static uint64_t readValue(const uint8_t *&ptr, const uint8_t *end) {
  unsigned int n = 0;
  uint64_t value = decodeULEB128(ptr, &n, end);
  ptr += n;
  return value;
}

template <typename Callback>
static void forEachHint(ArrayRef<uint8_t> data, Callback callback) {
  std::array<uint64_t, 3> args;

  for (const uint8_t *p = data.begin(), *end = data.end(); p < end;) {
    uint64_t type = readValue(p, end);
    if (type == 0)
      break;

    uint64_t argCount = readValue(p, end);
    // All known LOH types as of 2022-09 have 3 or fewer arguments; skip others.
    if (argCount > 3) {
      for (unsigned i = 0; i < argCount; ++i)
        readValue(p, end);
      continue;
    }

    for (unsigned i = 0; i < argCount; ++i)
      args[i] = readValue(p, end);
    callback(type, ArrayRef<uint64_t>(args.data(), argCount));
  }
}

// On RISC architectures like arm64, materializing a memory address generally
// takes multiple instructions. If the referenced symbol is located close enough
// in memory, fewer instructions are needed.
//
// Linker optimization hints record where addresses are computed. After
// addresses have been assigned, if possible, we change them to a shorter
// sequence of instructions. The size of the binary is not modified; the
// eliminated instructions are replaced with NOPs. This still leads to faster
// code as the CPU can skip over NOPs quickly.
//
// LOHs are specified by the LC_LINKER_OPTIMIZATION_HINTS load command, which
// points to a sequence of ULEB128-encoded numbers. Each entry specifies a
// transformation kind, and 2 or 3 addresses where the instructions are located.
void ARM64::applyOptimizationHints(uint8_t *outBuf, const ObjFile &obj) const {
  ArrayRef<uint8_t> data = obj.getOptimizationHints();
  if (data.empty())
    return;

  const ConcatInputSection *section = nullptr;
  uint64_t sectionAddr = 0;
  uint8_t *buf = nullptr;

  auto findSection = [&](uint64_t addr) {
    if (section && addr >= sectionAddr &&
        addr < sectionAddr + section->getSize())
      return true;

    if (obj.sections.empty())
      return false;
    auto secIt = std::prev(llvm::upper_bound(
        obj.sections, addr,
        [](uint64_t off, const Section *sec) { return off < sec->addr; }));
    const Section *sec = *secIt;

    if (sec->subsections.empty())
      return false;
    auto subsecIt = std::prev(llvm::upper_bound(
        sec->subsections, addr - sec->addr,
        [](uint64_t off, Subsection subsec) { return off < subsec.offset; }));
    const Subsection &subsec = *subsecIt;
    const ConcatInputSection *isec =
        dyn_cast_or_null<ConcatInputSection>(subsec.isec);
    if (!isec || isec->shouldOmitFromOutput())
      return false;

    section = isec;
    sectionAddr = subsec.offset + sec->addr;
    buf = outBuf + section->outSecOff + section->parent->fileOff;
    return true;
  };

  auto isValidOffset = [&](uint64_t offset) {
    if (offset < sectionAddr || offset >= sectionAddr + section->getSize()) {
      error(toString(&obj) +
            ": linker optimization hint spans multiple sections");
      return false;
    }
    return true;
  };

  bool hasAdrpAdrp = false;
  forEachHint(data, [&](uint64_t kind, ArrayRef<uint64_t> args) {
    if (kind == LOH_ARM64_ADRP_ADRP) {
      hasAdrpAdrp = true;
      return;
    }

    if (!findSection(args[0]))
      return;
    switch (kind) {
    case LOH_ARM64_ADRP_ADD:
      if (isValidOffset(args[1]))
        applyAdrpAdd(buf, section, args[0] - sectionAddr,
                     args[1] - sectionAddr);
      break;
    case LOH_ARM64_ADRP_LDR:
      if (isValidOffset(args[1]))
        applyAdrpLdr(buf, section, args[0] - sectionAddr,
                     args[1] - sectionAddr);
      break;
    case LOH_ARM64_ADRP_LDR_GOT:
      if (isValidOffset(args[1]))
        applyAdrpLdrGot(buf, section, args[0] - sectionAddr,
                        args[1] - sectionAddr);
      break;
    case LOH_ARM64_ADRP_ADD_LDR:
      if (isValidOffset(args[1]) && isValidOffset(args[2]))
        applyAdrpAddLdr(buf, section, args[0] - sectionAddr,
                        args[1] - sectionAddr, args[2] - sectionAddr);
      break;
    case LOH_ARM64_ADRP_LDR_GOT_LDR:
      if (isValidOffset(args[1]) && isValidOffset(args[2]))
        applyAdrpLdrGotLdr(buf, section, args[0] - sectionAddr,
                           args[1] - sectionAddr, args[2] - sectionAddr);
      break;
    case LOH_ARM64_ADRP_ADD_STR:
    case LOH_ARM64_ADRP_LDR_GOT_STR:
      // TODO: Implement these
      break;
    }
  });

  if (!hasAdrpAdrp)
    return;

  // AdrpAdrp optimization hints are performed in a second pass because they
  // might interfere with other transformations. For instance, consider the
  // following input:
  //
  //   adrp x0, _foo@PAGE
  //   add  x1, x0, _foo@PAGEOFF
  //   adrp x0, _bar@PAGE
  //   add  x2, x0, _bar@PAGEOFF
  //
  // If we perform the AdrpAdrp relaxation first, we get:
  //
  //   adrp x0, _foo@PAGE
  //   add  x1, x0, _foo@PAGEOFF
  //   nop
  //   add x2, x0, _bar@PAGEOFF
  //
  // If we then apply AdrpAdd to the first two instructions, the add will have a
  // garbage value in x0:
  //
  //   adr  x1, _foo
  //   nop
  //   nop
  //   add  x2, x0, _bar@PAGEOFF
  forEachHint(data, [&](uint64_t kind, ArrayRef<uint64_t> args) {
    if (kind != LOH_ARM64_ADRP_ADRP)
      return;
    if (!findSection(args[0]))
      return;
    if (isValidOffset(args[1]))
      applyAdrpAdrp(buf, section, args[0] - sectionAddr, args[1] - sectionAddr);
  });
}

TargetInfo *macho::createARM64TargetInfo() {
  static ARM64 t;
  return &t;
}
