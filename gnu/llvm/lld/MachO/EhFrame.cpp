//===- EhFrame.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EhFrame.h"
#include "InputFiles.h"

#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace lld;
using namespace lld::macho;
using namespace llvm::support::endian;

uint64_t EhReader::readLength(size_t *off) const {
  const size_t errOff = *off;
  if (*off + 4 > data.size())
    failOn(errOff, "CIE/FDE too small");
  uint64_t len = read32le(data.data() + *off);
  *off += 4;
  if (len == dwarf::DW_LENGTH_DWARF64) {
    // FIXME: test this DWARF64 code path
    if (*off + 8 > data.size())
      failOn(errOff, "CIE/FDE too small");
    len = read64le(data.data() + *off);
    *off += 8;
  }
  if (*off + len > data.size())
    failOn(errOff, "CIE/FDE extends past the end of the section");
  return len;
}

void EhReader::skipValidLength(size_t *off) const {
  uint32_t len = read32le(data.data() + *off);
  *off += 4;
  if (len == dwarf::DW_LENGTH_DWARF64)
    *off += 8;
}

// Read a byte and advance off by one byte.
uint8_t EhReader::readByte(size_t *off) const {
  if (*off + 1 > data.size())
    failOn(*off, "unexpected end of CIE/FDE");
  return data[(*off)++];
}

uint32_t EhReader::readU32(size_t *off) const {
  if (*off + 4 > data.size())
    failOn(*off, "unexpected end of CIE/FDE");
  uint32_t v = read32le(data.data() + *off);
  *off += 4;
  return v;
}

uint64_t EhReader::readPointer(size_t *off, uint8_t size) const {
  if (*off + size > data.size())
    failOn(*off, "unexpected end of CIE/FDE");
  uint64_t v;
  if (size == 8)
    v = read64le(data.data() + *off);
  else {
    assert(size == 4);
    v = read32le(data.data() + *off);
  }
  *off += size;
  return v;
}

// Read a null-terminated string.
StringRef EhReader::readString(size_t *off) const {
  if (*off > data.size())
    failOn(*off, "corrupted CIE (failed to read string)");
  const size_t maxlen = data.size() - *off;
  auto *c = reinterpret_cast<const char *>(data.data() + *off);
  size_t len = strnlen(c, maxlen);
  if (len == maxlen) // we failed to find the null terminator
    failOn(*off, "corrupted CIE (failed to read string)");
  *off += len + 1; // skip the null byte too
  return StringRef(c, len);
}

void EhReader::skipLeb128(size_t *off) const {
  const size_t errOff = *off;
  while (*off < data.size()) {
    uint8_t val = data[(*off)++];
    if ((val & 0x80) == 0)
      return;
  }
  failOn(errOff, "corrupted CIE (failed to read LEB128)");
}

void EhReader::failOn(size_t errOff, const Twine &msg) const {
  fatal(toString(file) + ":(__eh_frame+0x" +
        Twine::utohexstr(dataOff + errOff) + "): " + msg);
}

/*
 * Create a pair of relocs to write the value of:
 *   `b - (offset + a)` if Invert == false
 *   `(a + offset) - b` if Invert == true
 */
template <bool Invert = false>
static void createSubtraction(PointerUnion<Symbol *, InputSection *> a,
                              PointerUnion<Symbol *, InputSection *> b,
                              uint64_t off, uint8_t length,
                              SmallVectorImpl<Reloc> *newRelocs) {
  auto subtrahend = a;
  auto minuend = b;
  if (Invert)
    std::swap(subtrahend, minuend);
  assert(subtrahend.is<Symbol *>());
  Reloc subtrahendReloc(target->subtractorRelocType, /*pcrel=*/false, length,
                        off, /*addend=*/0, subtrahend);
  Reloc minuendReloc(target->unsignedRelocType, /*pcrel=*/false, length, off,
                     (Invert ? 1 : -1) * off, minuend);
  newRelocs->push_back(subtrahendReloc);
  newRelocs->push_back(minuendReloc);
}

void EhRelocator::makePcRel(uint64_t off,
                            PointerUnion<Symbol *, InputSection *> target,
                            uint8_t length) {
  createSubtraction(isec->symbols[0], target, off, length, &newRelocs);
}

void EhRelocator::makeNegativePcRel(
    uint64_t off, PointerUnion<Symbol *, InputSection *> target,
    uint8_t length) {
  createSubtraction</*Invert=*/true>(isec, target, off, length, &newRelocs);
}

void EhRelocator::commit() {
  isec->relocs.insert(isec->relocs.end(), newRelocs.begin(), newRelocs.end());
}
