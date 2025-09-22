//===- Relocations.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_RELOCATIONS_H
#define LLD_MACHO_RELOCATIONS_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Endian.h"

#include <cstddef>
#include <cstdint>

namespace lld::macho {
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

class Symbol;
class InputSection;

enum class RelocAttrBits {
  _0 = 0,              // invalid
  PCREL = 1 << 0,      // Value is PC-relative offset
  ABSOLUTE = 1 << 1,   // Value is an absolute address or fixed offset
  BYTE4 = 1 << 2,      // 4 byte datum
  BYTE8 = 1 << 3,      // 8 byte datum
  EXTERN = 1 << 4,     // Can have an external symbol
  LOCAL = 1 << 5,      // Can have a local symbol
  ADDEND = 1 << 6,     // *_ADDEND paired prefix reloc
  SUBTRAHEND = 1 << 7, // *_SUBTRACTOR paired prefix reloc
  BRANCH = 1 << 8,     // Value is branch target
  GOT = 1 << 9,        // References a symbol in the Global Offset Table
  TLV = 1 << 10,       // References a thread-local symbol
  LOAD = 1 << 11,      // Relaxable indirect load
  POINTER = 1 << 12,   // Non-relaxable indirect load (pointer is taken)
  UNSIGNED = 1 << 13,  // *_UNSIGNED relocs
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue*/ (1 << 14) - 1),
};
// Note: SUBTRACTOR always pairs with UNSIGNED (a delta between two symbols).

struct RelocAttrs {
  llvm::StringRef name;
  RelocAttrBits bits;
  bool hasAttr(RelocAttrBits b) const { return (bits & b) == b; }
};

struct Reloc {
  uint8_t type = llvm::MachO::GENERIC_RELOC_INVALID;
  bool pcrel = false;
  uint8_t length = 0;
  // The offset from the start of the subsection that this relocation belongs
  // to.
  uint32_t offset = 0;
  // Adding this offset to the address of the referent symbol or subsection
  // gives the destination that this relocation refers to.
  int64_t addend = 0;
  llvm::PointerUnion<Symbol *, InputSection *> referent = nullptr;

  Reloc() = default;

  Reloc(uint8_t type, bool pcrel, uint8_t length, uint32_t offset,
        int64_t addend, llvm::PointerUnion<Symbol *, InputSection *> referent)
      : type(type), pcrel(pcrel), length(length), offset(offset),
        addend(addend), referent(referent) {}

  InputSection *getReferentInputSection() const;
};

bool validateSymbolRelocation(const Symbol *, const InputSection *,
                              const Reloc &);

/*
 * v: The value the relocation is attempting to encode
 * bits: The number of bits actually available to encode this relocation
 */
void reportRangeError(void *loc, const Reloc &, const llvm::Twine &v,
                      uint8_t bits, int64_t min, uint64_t max);

struct SymbolDiagnostic {
  const Symbol *symbol;
  llvm::StringRef reason;
};

void reportRangeError(void *loc, SymbolDiagnostic, const llvm::Twine &v,
                      uint8_t bits, int64_t min, uint64_t max);

template <typename Diagnostic>
inline void checkInt(void *loc, Diagnostic d, int64_t v, int bits) {
  if (v != llvm::SignExtend64(v, bits))
    reportRangeError(loc, d, llvm::Twine(v), bits, llvm::minIntN(bits),
                     llvm::maxIntN(bits));
}

template <typename Diagnostic>
inline void checkUInt(void *loc, Diagnostic d, uint64_t v, int bits) {
  if ((v >> bits) != 0)
    reportRangeError(loc, d, llvm::Twine(v), bits, 0, llvm::maxUIntN(bits));
}

inline void writeAddress(uint8_t *loc, uint64_t addr, uint8_t length) {
  switch (length) {
  case 2:
    llvm::support::endian::write32le(loc, addr);
    break;
  case 3:
    llvm::support::endian::write64le(loc, addr);
    break;
  default:
    llvm_unreachable("invalid r_length");
  }
}

InputSection *offsetToInputSection(uint64_t *);

extern const RelocAttrs invalidRelocAttrs;

} // namespace lld::Macho

#endif
