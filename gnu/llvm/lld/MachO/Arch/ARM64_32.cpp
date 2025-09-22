//===- ARM64_32.cpp
//----------------------------------------------------------===//
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
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm::MachO;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::macho;

namespace {

struct ARM64_32 : ARM64Common {
  ARM64_32();
  void writeStub(uint8_t *buf, const Symbol &, uint64_t) const override;
  void writeStubHelperHeader(uint8_t *buf) const override;
  void writeStubHelperEntry(uint8_t *buf, const Symbol &,
                            uint64_t entryAddr) const override;
  void writeObjCMsgSendStub(uint8_t *buf, Symbol *sym, uint64_t stubsAddr,
                            uint64_t &stubOffset, uint64_t selrefVA,
                            Symbol *objcMsgSend) const override;
};

} // namespace

// These are very similar to ARM64's relocation attributes, except that we don't
// have the BYTE8 flag set.
static constexpr std::array<RelocAttrs, 11> relocAttrsArray{{
#define B(x) RelocAttrBits::x
    {"UNSIGNED", B(UNSIGNED) | B(ABSOLUTE) | B(EXTERN) | B(LOCAL) | B(BYTE4)},
    {"SUBTRACTOR", B(SUBTRAHEND) | B(EXTERN) | B(BYTE4)},
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

// The stub code is fairly similar to ARM64's, except that we load pointers into
// 32-bit 'w' registers, instead of the 64-bit 'x' ones.

static constexpr uint32_t stubCode[] = {
    0x90000010, // 00: adrp  x16, __la_symbol_ptr@page
    0xb9400210, // 04: ldr   w16, [x16, __la_symbol_ptr@pageoff]
    0xd61f0200, // 08: br    x16
};

void ARM64_32::writeStub(uint8_t *buf8, const Symbol &sym,
                         uint64_t pointerVA) const {
  ::writeStub(buf8, stubCode, sym, pointerVA);
}

static constexpr uint32_t stubHelperHeaderCode[] = {
    0x90000011, // 00: adrp  x17, _dyld_private@page
    0x91000231, // 04: add   x17, x17, _dyld_private@pageoff
    0xa9bf47f0, // 08: stp   x16/x17, [sp, #-16]!
    0x90000010, // 0c: adrp  x16, dyld_stub_binder@page
    0xb9400210, // 10: ldr   w16, [x16, dyld_stub_binder@pageoff]
    0xd61f0200, // 14: br    x16
};

void ARM64_32::writeStubHelperHeader(uint8_t *buf8) const {
  ::writeStubHelperHeader<ILP32>(buf8, stubHelperHeaderCode);
}

static constexpr uint32_t stubHelperEntryCode[] = {
    0x18000050, // 00: ldr  w16, l0
    0x14000000, // 04: b    stubHelperHeader
    0x00000000, // 08: l0: .long 0
};

void ARM64_32::writeStubHelperEntry(uint8_t *buf8, const Symbol &sym,
                                    uint64_t entryVA) const {
  ::writeStubHelperEntry(buf8, stubHelperEntryCode, sym, entryVA);
}

void ARM64_32::writeObjCMsgSendStub(uint8_t *buf, Symbol *sym,
                                    uint64_t stubsAddr, uint64_t &stubOffset,
                                    uint64_t selrefVA,
                                    Symbol *objcMsgSend) const {
  fatal("TODO: implement this");
}

ARM64_32::ARM64_32() : ARM64Common(ILP32()) {
  cpuType = CPU_TYPE_ARM64_32;
  cpuSubtype = CPU_SUBTYPE_ARM64_V8;

  modeDwarfEncoding = 0x04000000;              // UNWIND_ARM_MODE_DWARF
  subtractorRelocType = GENERIC_RELOC_INVALID; // FIXME
  unsignedRelocType = GENERIC_RELOC_INVALID;   // FIXME

  stubSize = sizeof(stubCode);
  stubHelperHeaderSize = sizeof(stubHelperHeaderCode);
  stubHelperEntrySize = sizeof(stubHelperEntryCode);

  relocAttrs = {relocAttrsArray.data(), relocAttrsArray.size()};
}

TargetInfo *macho::createARM64_32TargetInfo() {
  static ARM64_32 t;
  return &t;
}
