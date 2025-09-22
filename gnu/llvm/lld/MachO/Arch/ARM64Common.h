//===- ARM64Common.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_ARCH_ARM64COMMON_H
#define LLD_MACHO_ARCH_ARM64COMMON_H

#include "InputFiles.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"

#include "llvm/BinaryFormat/MachO.h"

namespace lld::macho {

struct ARM64Common : TargetInfo {
  template <class LP> ARM64Common(LP lp) : TargetInfo(lp) {}

  int64_t getEmbeddedAddend(MemoryBufferRef, uint64_t offset,
                            const llvm::MachO::relocation_info) const override;
  void relocateOne(uint8_t *loc, const Reloc &, uint64_t va,
                   uint64_t pc) const override;

  void relaxGotLoad(uint8_t *loc, uint8_t type) const override;
  uint64_t getPageSize() const override { return 16 * 1024; }

  void handleDtraceReloc(const Symbol *sym, const Reloc &r,
                         uint8_t *loc) const override;
};

inline uint64_t bitField(uint64_t value, int right, int width, int left) {
  return ((value >> right) & ((1 << width) - 1)) << left;
}

//              25                                                0
// +-----------+---------------------------------------------------+
// |           |                       imm26                       |
// +-----------+---------------------------------------------------+

inline void encodeBranch26(uint32_t *loc, const Reloc &r, uint32_t base,
                           uint64_t va) {
  checkInt(loc, r, va, 28);
  // Since branch destinations are 4-byte aligned, the 2 least-
  // significant bits are 0. They are right shifted off the end.
  llvm::support::endian::write32le(loc, base | bitField(va, 2, 26, 0));
}

inline void encodeBranch26(uint32_t *loc, SymbolDiagnostic d, uint32_t base,
                           uint64_t va) {
  checkInt(loc, d, va, 28);
  llvm::support::endian::write32le(loc, base | bitField(va, 2, 26, 0));
}

//   30 29          23                                  5
// +-+---+---------+-------------------------------------+---------+
// | |ilo|         |                immhi                |         |
// +-+---+---------+-------------------------------------+---------+

inline void encodePage21(uint32_t *loc, const Reloc &r, uint32_t base,
                         uint64_t va) {
  checkInt(loc, r, va, 35);
  llvm::support::endian::write32le(loc, base | bitField(va, 12, 2, 29) |
                                            bitField(va, 14, 19, 5));
}

inline void encodePage21(uint32_t *loc, SymbolDiagnostic d, uint32_t base,
                         uint64_t va) {
  checkInt(loc, d, va, 35);
  llvm::support::endian::write32le(loc, base | bitField(va, 12, 2, 29) |
                                            bitField(va, 14, 19, 5));
}

void reportUnalignedLdrStr(void *loc, const Reloc &, uint64_t va, int align);
void reportUnalignedLdrStr(void *loc, SymbolDiagnostic, uint64_t va, int align);

//                      21                   10
// +-------------------+-----------------------+-------------------+
// |                   |         imm12         |                   |
// +-------------------+-----------------------+-------------------+

template <typename Target>
inline void encodePageOff12(uint32_t *loc, Target t, uint32_t base,
                            uint64_t va) {
  int scale = 0;
  if ((base & 0x3b00'0000) == 0x3900'0000) { // load/store
    scale = base >> 30;
    if (scale == 0 && (base & 0x0480'0000) == 0x0480'0000) // 128-bit variant
      scale = 4;
  }
  const int size = 1 << scale;
  if ((va & (size - 1)) != 0)
    reportUnalignedLdrStr(loc, t, va, size);

  // TODO(gkm): extract embedded addend and warn if != 0
  // uint64_t addend = ((base & 0x003FFC00) >> 10);
  llvm::support::endian::write32le(loc,
                                   base | bitField(va, scale, 12 - scale, 10));
}

inline uint64_t pageBits(uint64_t address) {
  const uint64_t pageMask = ~0xfffull;
  return address & pageMask;
}

inline void writeStub(uint8_t *buf8, const uint32_t stubCode[3],
                      const macho::Symbol &sym, uint64_t pointerVA) {
  auto *buf32 = reinterpret_cast<uint32_t *>(buf8);
  constexpr size_t stubCodeSize = 3 * sizeof(uint32_t);
  SymbolDiagnostic d = {&sym, "stub"};
  uint64_t pcPageBits =
      pageBits(in.stubs->addr + sym.stubsIndex * stubCodeSize);
  encodePage21(&buf32[0], d, stubCode[0], pageBits(pointerVA) - pcPageBits);
  encodePageOff12(&buf32[1], d, stubCode[1], pointerVA);
  buf32[2] = stubCode[2];
}

template <class LP>
inline void writeStubHelperHeader(uint8_t *buf8,
                                  const uint32_t stubHelperHeaderCode[6]) {
  auto *buf32 = reinterpret_cast<uint32_t *>(buf8);
  auto pcPageBits = [](int i) {
    return pageBits(in.stubHelper->addr + i * sizeof(uint32_t));
  };
  uint64_t loaderVA = in.imageLoaderCache->getVA();
  SymbolDiagnostic d = {nullptr, "stub header helper"};
  encodePage21(&buf32[0], d, stubHelperHeaderCode[0],
               pageBits(loaderVA) - pcPageBits(0));
  encodePageOff12(&buf32[1], d, stubHelperHeaderCode[1], loaderVA);
  buf32[2] = stubHelperHeaderCode[2];
  uint64_t binderVA =
      in.got->addr + in.stubHelper->stubBinder->gotIndex * LP::wordSize;
  encodePage21(&buf32[3], d, stubHelperHeaderCode[3],
               pageBits(binderVA) - pcPageBits(3));
  encodePageOff12(&buf32[4], d, stubHelperHeaderCode[4], binderVA);
  buf32[5] = stubHelperHeaderCode[5];
}

inline void writeStubHelperEntry(uint8_t *buf8,
                                 const uint32_t stubHelperEntryCode[3],
                                 const Symbol &sym, uint64_t entryVA) {
  auto *buf32 = reinterpret_cast<uint32_t *>(buf8);
  auto pcVA = [entryVA](int i) { return entryVA + i * sizeof(uint32_t); };
  uint64_t stubHelperHeaderVA = in.stubHelper->addr;
  buf32[0] = stubHelperEntryCode[0];
  encodeBranch26(&buf32[1], {&sym, "stub helper"}, stubHelperEntryCode[1],
                 stubHelperHeaderVA - pcVA(1));
  buf32[2] = sym.lazyBindOffset;
}

template <class LP>
inline void writeObjCMsgSendFastStub(uint8_t *buf,
                                     const uint32_t objcStubsFastCode[8],
                                     Symbol *sym, uint64_t stubsAddr,
                                     uint64_t stubOffset, uint64_t selrefVA,
                                     uint64_t gotAddr, uint64_t msgSendIndex) {
  SymbolDiagnostic d = {sym, sym->getName()};
  auto *buf32 = reinterpret_cast<uint32_t *>(buf);

  auto pcPageBits = [stubsAddr, stubOffset](int i) {
    return pageBits(stubsAddr + stubOffset + i * sizeof(uint32_t));
  };

  encodePage21(&buf32[0], d, objcStubsFastCode[0],
               pageBits(selrefVA) - pcPageBits(0));
  encodePageOff12(&buf32[1], d, objcStubsFastCode[1], selrefVA);
  uint64_t gotOffset = msgSendIndex * LP::wordSize;
  encodePage21(&buf32[2], d, objcStubsFastCode[2],
               pageBits(gotAddr + gotOffset) - pcPageBits(2));
  encodePageOff12(&buf32[3], d, objcStubsFastCode[3], gotAddr + gotOffset);
  buf32[4] = objcStubsFastCode[4];
  buf32[5] = objcStubsFastCode[5];
  buf32[6] = objcStubsFastCode[6];
  buf32[7] = objcStubsFastCode[7];
}

template <class LP>
inline void
writeObjCMsgSendSmallStub(uint8_t *buf, const uint32_t objcStubsSmallCode[3],
                          Symbol *sym, uint64_t stubsAddr, uint64_t stubOffset,
                          uint64_t selrefVA, uint64_t msgSendAddr,
                          uint64_t msgSendIndex) {
  SymbolDiagnostic d = {sym, sym->getName()};
  auto *buf32 = reinterpret_cast<uint32_t *>(buf);

  auto pcPageBits = [stubsAddr, stubOffset](int i) {
    return pageBits(stubsAddr + stubOffset + i * sizeof(uint32_t));
  };

  encodePage21(&buf32[0], d, objcStubsSmallCode[0],
               pageBits(selrefVA) - pcPageBits(0));
  encodePageOff12(&buf32[1], d, objcStubsSmallCode[1], selrefVA);
  uint64_t msgSendStubVA = msgSendAddr + msgSendIndex * target->stubSize;
  uint64_t pcVA = stubsAddr + stubOffset + 2 * sizeof(uint32_t);
  encodeBranch26(&buf32[2], {nullptr, "objc_msgSend stub"},
                 objcStubsSmallCode[2], msgSendStubVA - pcVA);
}

} // namespace lld::macho

#endif
