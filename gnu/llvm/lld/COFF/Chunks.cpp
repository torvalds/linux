//===- Chunks.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Chunks.h"
#include "COFFLinkerContext.h"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Writer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <iterator>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::COFF;
using llvm::support::ulittle32_t;

namespace lld::coff {

SectionChunk::SectionChunk(ObjFile *f, const coff_section *h, Kind k)
    : Chunk(k), file(f), header(h), repl(this) {
  // Initialize relocs.
  if (file)
    setRelocs(file->getCOFFObj()->getRelocations(header));

  // Initialize sectionName.
  StringRef sectionName;
  if (file) {
    if (Expected<StringRef> e = file->getCOFFObj()->getSectionName(header))
      sectionName = *e;
  }
  sectionNameData = sectionName.data();
  sectionNameSize = sectionName.size();

  setAlignment(header->getAlignment());

  hasData = !(header->Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA);

  // If linker GC is disabled, every chunk starts out alive.  If linker GC is
  // enabled, treat non-comdat sections as roots. Generally optimized object
  // files will be built with -ffunction-sections or /Gy, so most things worth
  // stripping will be in a comdat.
  if (file)
    live = !file->ctx.config.doGC || !isCOMDAT();
  else
    live = true;
}

// SectionChunk is one of the most frequently allocated classes, so it is
// important to keep it as compact as possible. As of this writing, the number
// below is the size of this class on x64 platforms.
static_assert(sizeof(SectionChunk) <= 88, "SectionChunk grew unexpectedly");

static void add16(uint8_t *p, int16_t v) { write16le(p, read16le(p) + v); }
static void add32(uint8_t *p, int32_t v) { write32le(p, read32le(p) + v); }
static void add64(uint8_t *p, int64_t v) { write64le(p, read64le(p) + v); }
static void or16(uint8_t *p, uint16_t v) { write16le(p, read16le(p) | v); }
static void or32(uint8_t *p, uint32_t v) { write32le(p, read32le(p) | v); }

// Verify that given sections are appropriate targets for SECREL
// relocations. This check is relaxed because unfortunately debug
// sections have section-relative relocations against absolute symbols.
static bool checkSecRel(const SectionChunk *sec, OutputSection *os) {
  if (os)
    return true;
  if (sec->isCodeView())
    return false;
  error("SECREL relocation cannot be applied to absolute symbols");
  return false;
}

static void applySecRel(const SectionChunk *sec, uint8_t *off,
                        OutputSection *os, uint64_t s) {
  if (!checkSecRel(sec, os))
    return;
  uint64_t secRel = s - os->getRVA();
  if (secRel > UINT32_MAX) {
    error("overflow in SECREL relocation in section: " + sec->getSectionName());
    return;
  }
  add32(off, secRel);
}

static void applySecIdx(uint8_t *off, OutputSection *os,
                        unsigned numOutputSections) {
  // numOutputSections is the largest valid section index. Make sure that
  // it fits in 16 bits.
  assert(numOutputSections <= 0xffff && "size of outputSections is too big");

  // Absolute symbol doesn't have section index, but section index relocation
  // against absolute symbol should be resolved to one plus the last output
  // section index. This is required for compatibility with MSVC.
  if (os)
    add16(off, os->sectionIndex);
  else
    add16(off, numOutputSections + 1);
}

void SectionChunk::applyRelX64(uint8_t *off, uint16_t type, OutputSection *os,
                               uint64_t s, uint64_t p,
                               uint64_t imageBase) const {
  switch (type) {
  case IMAGE_REL_AMD64_ADDR32:
    add32(off, s + imageBase);
    break;
  case IMAGE_REL_AMD64_ADDR64:
    add64(off, s + imageBase);
    break;
  case IMAGE_REL_AMD64_ADDR32NB: add32(off, s); break;
  case IMAGE_REL_AMD64_REL32:    add32(off, s - p - 4); break;
  case IMAGE_REL_AMD64_REL32_1:  add32(off, s - p - 5); break;
  case IMAGE_REL_AMD64_REL32_2:  add32(off, s - p - 6); break;
  case IMAGE_REL_AMD64_REL32_3:  add32(off, s - p - 7); break;
  case IMAGE_REL_AMD64_REL32_4:  add32(off, s - p - 8); break;
  case IMAGE_REL_AMD64_REL32_5:  add32(off, s - p - 9); break;
  case IMAGE_REL_AMD64_SECTION:
    applySecIdx(off, os, file->ctx.outputSections.size());
    break;
  case IMAGE_REL_AMD64_SECREL:   applySecRel(this, off, os, s); break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(type) + " in " +
          toString(file));
  }
}

void SectionChunk::applyRelX86(uint8_t *off, uint16_t type, OutputSection *os,
                               uint64_t s, uint64_t p,
                               uint64_t imageBase) const {
  switch (type) {
  case IMAGE_REL_I386_ABSOLUTE: break;
  case IMAGE_REL_I386_DIR32:
    add32(off, s + imageBase);
    break;
  case IMAGE_REL_I386_DIR32NB:  add32(off, s); break;
  case IMAGE_REL_I386_REL32:    add32(off, s - p - 4); break;
  case IMAGE_REL_I386_SECTION:
    applySecIdx(off, os, file->ctx.outputSections.size());
    break;
  case IMAGE_REL_I386_SECREL:   applySecRel(this, off, os, s); break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(type) + " in " +
          toString(file));
  }
}

static void applyMOV(uint8_t *off, uint16_t v) {
  write16le(off, (read16le(off) & 0xfbf0) | ((v & 0x800) >> 1) | ((v >> 12) & 0xf));
  write16le(off + 2, (read16le(off + 2) & 0x8f00) | ((v & 0x700) << 4) | (v & 0xff));
}

static uint16_t readMOV(uint8_t *off, bool movt) {
  uint16_t op1 = read16le(off);
  if ((op1 & 0xfbf0) != (movt ? 0xf2c0 : 0xf240))
    error("unexpected instruction in " + Twine(movt ? "MOVT" : "MOVW") +
          " instruction in MOV32T relocation");
  uint16_t op2 = read16le(off + 2);
  if ((op2 & 0x8000) != 0)
    error("unexpected instruction in " + Twine(movt ? "MOVT" : "MOVW") +
          " instruction in MOV32T relocation");
  return (op2 & 0x00ff) | ((op2 >> 4) & 0x0700) | ((op1 << 1) & 0x0800) |
         ((op1 & 0x000f) << 12);
}

void applyMOV32T(uint8_t *off, uint32_t v) {
  uint16_t immW = readMOV(off, false);    // read MOVW operand
  uint16_t immT = readMOV(off + 4, true); // read MOVT operand
  uint32_t imm = immW | (immT << 16);
  v += imm;                         // add the immediate offset
  applyMOV(off, v);           // set MOVW operand
  applyMOV(off + 4, v >> 16); // set MOVT operand
}

static void applyBranch20T(uint8_t *off, int32_t v) {
  if (!isInt<21>(v))
    error("relocation out of range");
  uint32_t s = v < 0 ? 1 : 0;
  uint32_t j1 = (v >> 19) & 1;
  uint32_t j2 = (v >> 18) & 1;
  or16(off, (s << 10) | ((v >> 12) & 0x3f));
  or16(off + 2, (j1 << 13) | (j2 << 11) | ((v >> 1) & 0x7ff));
}

void applyBranch24T(uint8_t *off, int32_t v) {
  if (!isInt<25>(v))
    error("relocation out of range");
  uint32_t s = v < 0 ? 1 : 0;
  uint32_t j1 = ((~v >> 23) & 1) ^ s;
  uint32_t j2 = ((~v >> 22) & 1) ^ s;
  or16(off, (s << 10) | ((v >> 12) & 0x3ff));
  // Clear out the J1 and J2 bits which may be set.
  write16le(off + 2, (read16le(off + 2) & 0xd000) | (j1 << 13) | (j2 << 11) | ((v >> 1) & 0x7ff));
}

void SectionChunk::applyRelARM(uint8_t *off, uint16_t type, OutputSection *os,
                               uint64_t s, uint64_t p,
                               uint64_t imageBase) const {
  // Pointer to thumb code must have the LSB set.
  uint64_t sx = s;
  if (os && (os->header.Characteristics & IMAGE_SCN_MEM_EXECUTE))
    sx |= 1;
  switch (type) {
  case IMAGE_REL_ARM_ADDR32:
    add32(off, sx + imageBase);
    break;
  case IMAGE_REL_ARM_ADDR32NB:  add32(off, sx); break;
  case IMAGE_REL_ARM_MOV32T:
    applyMOV32T(off, sx + imageBase);
    break;
  case IMAGE_REL_ARM_BRANCH20T: applyBranch20T(off, sx - p - 4); break;
  case IMAGE_REL_ARM_BRANCH24T: applyBranch24T(off, sx - p - 4); break;
  case IMAGE_REL_ARM_BLX23T:    applyBranch24T(off, sx - p - 4); break;
  case IMAGE_REL_ARM_SECTION:
    applySecIdx(off, os, file->ctx.outputSections.size());
    break;
  case IMAGE_REL_ARM_SECREL:    applySecRel(this, off, os, s); break;
  case IMAGE_REL_ARM_REL32:     add32(off, sx - p - 4); break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(type) + " in " +
          toString(file));
  }
}

// Interpret the existing immediate value as a byte offset to the
// target symbol, then update the instruction with the immediate as
// the page offset from the current instruction to the target.
void applyArm64Addr(uint8_t *off, uint64_t s, uint64_t p, int shift) {
  uint32_t orig = read32le(off);
  int64_t imm =
      SignExtend64<21>(((orig >> 29) & 0x3) | ((orig >> 3) & 0x1FFFFC));
  s += imm;
  imm = (s >> shift) - (p >> shift);
  uint32_t immLo = (imm & 0x3) << 29;
  uint32_t immHi = (imm & 0x1FFFFC) << 3;
  uint64_t mask = (0x3 << 29) | (0x1FFFFC << 3);
  write32le(off, (orig & ~mask) | immLo | immHi);
}

// Update the immediate field in a AARCH64 ldr, str, and add instruction.
// Optionally limit the range of the written immediate by one or more bits
// (rangeLimit).
void applyArm64Imm(uint8_t *off, uint64_t imm, uint32_t rangeLimit) {
  uint32_t orig = read32le(off);
  imm += (orig >> 10) & 0xFFF;
  orig &= ~(0xFFF << 10);
  write32le(off, orig | ((imm & (0xFFF >> rangeLimit)) << 10));
}

// Add the 12 bit page offset to the existing immediate.
// Ldr/str instructions store the opcode immediate scaled
// by the load/store size (giving a larger range for larger
// loads/stores). The immediate is always (both before and after
// fixing up the relocation) stored scaled similarly.
// Even if larger loads/stores have a larger range, limit the
// effective offset to 12 bit, since it is intended to be a
// page offset.
static void applyArm64Ldr(uint8_t *off, uint64_t imm) {
  uint32_t orig = read32le(off);
  uint32_t size = orig >> 30;
  // 0x04000000 indicates SIMD/FP registers
  // 0x00800000 indicates 128 bit
  if ((orig & 0x4800000) == 0x4800000)
    size += 4;
  if ((imm & ((1 << size) - 1)) != 0)
    error("misaligned ldr/str offset");
  applyArm64Imm(off, imm >> size, size);
}

static void applySecRelLow12A(const SectionChunk *sec, uint8_t *off,
                              OutputSection *os, uint64_t s) {
  if (checkSecRel(sec, os))
    applyArm64Imm(off, (s - os->getRVA()) & 0xfff, 0);
}

static void applySecRelHigh12A(const SectionChunk *sec, uint8_t *off,
                               OutputSection *os, uint64_t s) {
  if (!checkSecRel(sec, os))
    return;
  uint64_t secRel = (s - os->getRVA()) >> 12;
  if (0xfff < secRel) {
    error("overflow in SECREL_HIGH12A relocation in section: " +
          sec->getSectionName());
    return;
  }
  applyArm64Imm(off, secRel & 0xfff, 0);
}

static void applySecRelLdr(const SectionChunk *sec, uint8_t *off,
                           OutputSection *os, uint64_t s) {
  if (checkSecRel(sec, os))
    applyArm64Ldr(off, (s - os->getRVA()) & 0xfff);
}

void applyArm64Branch26(uint8_t *off, int64_t v) {
  if (!isInt<28>(v))
    error("relocation out of range");
  or32(off, (v & 0x0FFFFFFC) >> 2);
}

static void applyArm64Branch19(uint8_t *off, int64_t v) {
  if (!isInt<21>(v))
    error("relocation out of range");
  or32(off, (v & 0x001FFFFC) << 3);
}

static void applyArm64Branch14(uint8_t *off, int64_t v) {
  if (!isInt<16>(v))
    error("relocation out of range");
  or32(off, (v & 0x0000FFFC) << 3);
}

void SectionChunk::applyRelARM64(uint8_t *off, uint16_t type, OutputSection *os,
                                 uint64_t s, uint64_t p,
                                 uint64_t imageBase) const {
  switch (type) {
  case IMAGE_REL_ARM64_PAGEBASE_REL21: applyArm64Addr(off, s, p, 12); break;
  case IMAGE_REL_ARM64_REL21:          applyArm64Addr(off, s, p, 0); break;
  case IMAGE_REL_ARM64_PAGEOFFSET_12A: applyArm64Imm(off, s & 0xfff, 0); break;
  case IMAGE_REL_ARM64_PAGEOFFSET_12L: applyArm64Ldr(off, s & 0xfff); break;
  case IMAGE_REL_ARM64_BRANCH26:       applyArm64Branch26(off, s - p); break;
  case IMAGE_REL_ARM64_BRANCH19:       applyArm64Branch19(off, s - p); break;
  case IMAGE_REL_ARM64_BRANCH14:       applyArm64Branch14(off, s - p); break;
  case IMAGE_REL_ARM64_ADDR32:
    add32(off, s + imageBase);
    break;
  case IMAGE_REL_ARM64_ADDR32NB:       add32(off, s); break;
  case IMAGE_REL_ARM64_ADDR64:
    add64(off, s + imageBase);
    break;
  case IMAGE_REL_ARM64_SECREL:         applySecRel(this, off, os, s); break;
  case IMAGE_REL_ARM64_SECREL_LOW12A:  applySecRelLow12A(this, off, os, s); break;
  case IMAGE_REL_ARM64_SECREL_HIGH12A: applySecRelHigh12A(this, off, os, s); break;
  case IMAGE_REL_ARM64_SECREL_LOW12L:  applySecRelLdr(this, off, os, s); break;
  case IMAGE_REL_ARM64_SECTION:
    applySecIdx(off, os, file->ctx.outputSections.size());
    break;
  case IMAGE_REL_ARM64_REL32:          add32(off, s - p - 4); break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(type) + " in " +
          toString(file));
  }
}

static void maybeReportRelocationToDiscarded(const SectionChunk *fromChunk,
                                             Defined *sym,
                                             const coff_relocation &rel,
                                             bool isMinGW) {
  // Don't report these errors when the relocation comes from a debug info
  // section or in mingw mode. MinGW mode object files (built by GCC) can
  // have leftover sections with relocations against discarded comdat
  // sections. Such sections are left as is, with relocations untouched.
  if (fromChunk->isCodeView() || fromChunk->isDWARF() || isMinGW)
    return;

  // Get the name of the symbol. If it's null, it was discarded early, so we
  // have to go back to the object file.
  ObjFile *file = fromChunk->file;
  StringRef name;
  if (sym) {
    name = sym->getName();
  } else {
    COFFSymbolRef coffSym =
        check(file->getCOFFObj()->getSymbol(rel.SymbolTableIndex));
    name = check(file->getCOFFObj()->getSymbolName(coffSym));
  }

  std::vector<std::string> symbolLocations =
      getSymbolLocations(file, rel.SymbolTableIndex);

  std::string out;
  llvm::raw_string_ostream os(out);
  os << "relocation against symbol in discarded section: " + name;
  for (const std::string &s : symbolLocations)
    os << s;
  error(os.str());
}

void SectionChunk::writeTo(uint8_t *buf) const {
  if (!hasData)
    return;
  // Copy section contents from source object file to output file.
  ArrayRef<uint8_t> a = getContents();
  if (!a.empty())
    memcpy(buf, a.data(), a.size());

  // Apply relocations.
  size_t inputSize = getSize();
  for (const coff_relocation &rel : getRelocs()) {
    // Check for an invalid relocation offset. This check isn't perfect, because
    // we don't have the relocation size, which is only known after checking the
    // machine and relocation type. As a result, a relocation may overwrite the
    // beginning of the following input section.
    if (rel.VirtualAddress >= inputSize) {
      error("relocation points beyond the end of its parent section");
      continue;
    }

    applyRelocation(buf + rel.VirtualAddress, rel);
  }

  // Write the offset to EC entry thunk preceding section contents. The low bit
  // is always set, so it's effectively an offset from the last byte of the
  // offset.
  if (Defined *entryThunk = getEntryThunk())
    write32le(buf - sizeof(uint32_t), entryThunk->getRVA() - rva + 1);
}

void SectionChunk::applyRelocation(uint8_t *off,
                                   const coff_relocation &rel) const {
  auto *sym = dyn_cast_or_null<Defined>(file->getSymbol(rel.SymbolTableIndex));

  // Get the output section of the symbol for this relocation.  The output
  // section is needed to compute SECREL and SECTION relocations used in debug
  // info.
  Chunk *c = sym ? sym->getChunk() : nullptr;
  OutputSection *os = c ? file->ctx.getOutputSection(c) : nullptr;

  // Skip the relocation if it refers to a discarded section, and diagnose it
  // as an error if appropriate. If a symbol was discarded early, it may be
  // null. If it was discarded late, the output section will be null, unless
  // it was an absolute or synthetic symbol.
  if (!sym ||
      (!os && !isa<DefinedAbsolute>(sym) && !isa<DefinedSynthetic>(sym))) {
    maybeReportRelocationToDiscarded(this, sym, rel, file->ctx.config.mingw);
    return;
  }

  uint64_t s = sym->getRVA();

  // Compute the RVA of the relocation for relative relocations.
  uint64_t p = rva + rel.VirtualAddress;
  uint64_t imageBase = file->ctx.config.imageBase;
  switch (getArch()) {
  case Triple::x86_64:
    applyRelX64(off, rel.Type, os, s, p, imageBase);
    break;
  case Triple::x86:
    applyRelX86(off, rel.Type, os, s, p, imageBase);
    break;
  case Triple::thumb:
    applyRelARM(off, rel.Type, os, s, p, imageBase);
    break;
  case Triple::aarch64:
    applyRelARM64(off, rel.Type, os, s, p, imageBase);
    break;
  default:
    llvm_unreachable("unknown machine type");
  }
}

// Defend against unsorted relocations. This may be overly conservative.
void SectionChunk::sortRelocations() {
  auto cmpByVa = [](const coff_relocation &l, const coff_relocation &r) {
    return l.VirtualAddress < r.VirtualAddress;
  };
  if (llvm::is_sorted(getRelocs(), cmpByVa))
    return;
  warn("some relocations in " + file->getName() + " are not sorted");
  MutableArrayRef<coff_relocation> newRelocs(
      bAlloc().Allocate<coff_relocation>(relocsSize), relocsSize);
  memcpy(newRelocs.data(), relocsData, relocsSize * sizeof(coff_relocation));
  llvm::sort(newRelocs, cmpByVa);
  setRelocs(newRelocs);
}

// Similar to writeTo, but suitable for relocating a subsection of the overall
// section.
void SectionChunk::writeAndRelocateSubsection(ArrayRef<uint8_t> sec,
                                              ArrayRef<uint8_t> subsec,
                                              uint32_t &nextRelocIndex,
                                              uint8_t *buf) const {
  assert(!subsec.empty() && !sec.empty());
  assert(sec.begin() <= subsec.begin() && subsec.end() <= sec.end() &&
         "subsection is not part of this section");
  size_t vaBegin = std::distance(sec.begin(), subsec.begin());
  size_t vaEnd = std::distance(sec.begin(), subsec.end());
  memcpy(buf, subsec.data(), subsec.size());
  for (; nextRelocIndex < relocsSize; ++nextRelocIndex) {
    const coff_relocation &rel = relocsData[nextRelocIndex];
    // Only apply relocations that apply to this subsection. These checks
    // assume that all subsections completely contain their relocations.
    // Relocations must not straddle the beginning or end of a subsection.
    if (rel.VirtualAddress < vaBegin)
      continue;
    if (rel.VirtualAddress + 1 >= vaEnd)
      break;
    applyRelocation(&buf[rel.VirtualAddress - vaBegin], rel);
  }
}

void SectionChunk::addAssociative(SectionChunk *child) {
  // Insert the child section into the list of associated children. Keep the
  // list ordered by section name so that ICF does not depend on section order.
  assert(child->assocChildren == nullptr &&
         "associated sections cannot have their own associated children");
  SectionChunk *prev = this;
  SectionChunk *next = assocChildren;
  for (; next != nullptr; prev = next, next = next->assocChildren) {
    if (next->getSectionName() <= child->getSectionName())
      break;
  }

  // Insert child between prev and next.
  assert(prev->assocChildren == next);
  prev->assocChildren = child;
  child->assocChildren = next;
}

static uint8_t getBaserelType(const coff_relocation &rel,
                              Triple::ArchType arch) {
  switch (arch) {
  case Triple::x86_64:
    if (rel.Type == IMAGE_REL_AMD64_ADDR64)
      return IMAGE_REL_BASED_DIR64;
    if (rel.Type == IMAGE_REL_AMD64_ADDR32)
      return IMAGE_REL_BASED_HIGHLOW;
    return IMAGE_REL_BASED_ABSOLUTE;
  case Triple::x86:
    if (rel.Type == IMAGE_REL_I386_DIR32)
      return IMAGE_REL_BASED_HIGHLOW;
    return IMAGE_REL_BASED_ABSOLUTE;
  case Triple::thumb:
    if (rel.Type == IMAGE_REL_ARM_ADDR32)
      return IMAGE_REL_BASED_HIGHLOW;
    if (rel.Type == IMAGE_REL_ARM_MOV32T)
      return IMAGE_REL_BASED_ARM_MOV32T;
    return IMAGE_REL_BASED_ABSOLUTE;
  case Triple::aarch64:
    if (rel.Type == IMAGE_REL_ARM64_ADDR64)
      return IMAGE_REL_BASED_DIR64;
    return IMAGE_REL_BASED_ABSOLUTE;
  default:
    llvm_unreachable("unknown machine type");
  }
}

// Windows-specific.
// Collect all locations that contain absolute addresses, which need to be
// fixed by the loader if load-time relocation is needed.
// Only called when base relocation is enabled.
void SectionChunk::getBaserels(std::vector<Baserel> *res) {
  for (const coff_relocation &rel : getRelocs()) {
    uint8_t ty = getBaserelType(rel, getArch());
    if (ty == IMAGE_REL_BASED_ABSOLUTE)
      continue;
    Symbol *target = file->getSymbol(rel.SymbolTableIndex);
    if (!target || isa<DefinedAbsolute>(target))
      continue;
    res->emplace_back(rva + rel.VirtualAddress, ty);
  }
}

// MinGW specific.
// Check whether a static relocation of type Type can be deferred and
// handled at runtime as a pseudo relocation (for references to a module
// local variable, which turned out to actually need to be imported from
// another DLL) This returns the size the relocation is supposed to update,
// in bits, or 0 if the relocation cannot be handled as a runtime pseudo
// relocation.
static int getRuntimePseudoRelocSize(uint16_t type,
                                     llvm::COFF::MachineTypes machine) {
  // Relocations that either contain an absolute address, or a plain
  // relative offset, since the runtime pseudo reloc implementation
  // adds 8/16/32/64 bit values to a memory address.
  //
  // Given a pseudo relocation entry,
  //
  // typedef struct {
  //   DWORD sym;
  //   DWORD target;
  //   DWORD flags;
  // } runtime_pseudo_reloc_item_v2;
  //
  // the runtime relocation performs this adjustment:
  //     *(base + .target) += *(base + .sym) - (base + .sym)
  //
  // This works for both absolute addresses (IMAGE_REL_*_ADDR32/64,
  // IMAGE_REL_I386_DIR32, where the memory location initially contains
  // the address of the IAT slot, and for relative addresses (IMAGE_REL*_REL32),
  // where the memory location originally contains the relative offset to the
  // IAT slot.
  //
  // This requires the target address to be writable, either directly out of
  // the image, or temporarily changed at runtime with VirtualProtect.
  // Since this only operates on direct address values, it doesn't work for
  // ARM/ARM64 relocations, other than the plain ADDR32/ADDR64 relocations.
  switch (machine) {
  case AMD64:
    switch (type) {
    case IMAGE_REL_AMD64_ADDR64:
      return 64;
    case IMAGE_REL_AMD64_ADDR32:
    case IMAGE_REL_AMD64_REL32:
    case IMAGE_REL_AMD64_REL32_1:
    case IMAGE_REL_AMD64_REL32_2:
    case IMAGE_REL_AMD64_REL32_3:
    case IMAGE_REL_AMD64_REL32_4:
    case IMAGE_REL_AMD64_REL32_5:
      return 32;
    default:
      return 0;
    }
  case I386:
    switch (type) {
    case IMAGE_REL_I386_DIR32:
    case IMAGE_REL_I386_REL32:
      return 32;
    default:
      return 0;
    }
  case ARMNT:
    switch (type) {
    case IMAGE_REL_ARM_ADDR32:
      return 32;
    default:
      return 0;
    }
  case ARM64:
    switch (type) {
    case IMAGE_REL_ARM64_ADDR64:
      return 64;
    case IMAGE_REL_ARM64_ADDR32:
      return 32;
    default:
      return 0;
    }
  default:
    llvm_unreachable("unknown machine type");
  }
}

// MinGW specific.
// Append information to the provided vector about all relocations that
// need to be handled at runtime as runtime pseudo relocations (references
// to a module local variable, which turned out to actually need to be
// imported from another DLL).
void SectionChunk::getRuntimePseudoRelocs(
    std::vector<RuntimePseudoReloc> &res) {
  for (const coff_relocation &rel : getRelocs()) {
    auto *target =
        dyn_cast_or_null<Defined>(file->getSymbol(rel.SymbolTableIndex));
    if (!target || !target->isRuntimePseudoReloc)
      continue;
    // If the target doesn't have a chunk allocated, it may be a
    // DefinedImportData symbol which ended up unnecessary after GC.
    // Normally we wouldn't eliminate section chunks that are referenced, but
    // references within DWARF sections don't count for keeping section chunks
    // alive. Thus such dangling references in DWARF sections are expected.
    if (!target->getChunk())
      continue;
    int sizeInBits =
        getRuntimePseudoRelocSize(rel.Type, file->ctx.config.machine);
    if (sizeInBits == 0) {
      error("unable to automatically import from " + target->getName() +
            " with relocation type " +
            file->getCOFFObj()->getRelocationTypeName(rel.Type) + " in " +
            toString(file));
      continue;
    }
    int addressSizeInBits = file->ctx.config.is64() ? 64 : 32;
    if (sizeInBits < addressSizeInBits) {
      warn("runtime pseudo relocation in " + toString(file) + " against " +
           "symbol " + target->getName() + " is too narrow (only " +
           Twine(sizeInBits) + " bits wide); this can fail at runtime " +
           "depending on memory layout");
    }
    // sizeInBits is used to initialize the Flags field; currently no
    // other flags are defined.
    res.emplace_back(target, this, rel.VirtualAddress, sizeInBits);
  }
}

bool SectionChunk::isCOMDAT() const {
  return header->Characteristics & IMAGE_SCN_LNK_COMDAT;
}

void SectionChunk::printDiscardedMessage() const {
  // Removed by dead-stripping. If it's removed by ICF, ICF already
  // printed out the name, so don't repeat that here.
  if (sym && this == repl)
    log("Discarded " + sym->getName());
}

StringRef SectionChunk::getDebugName() const {
  if (sym)
    return sym->getName();
  return "";
}

ArrayRef<uint8_t> SectionChunk::getContents() const {
  ArrayRef<uint8_t> a;
  cantFail(file->getCOFFObj()->getSectionContents(header, a));
  return a;
}

ArrayRef<uint8_t> SectionChunk::consumeDebugMagic() {
  assert(isCodeView());
  return consumeDebugMagic(getContents(), getSectionName());
}

ArrayRef<uint8_t> SectionChunk::consumeDebugMagic(ArrayRef<uint8_t> data,
                                                  StringRef sectionName) {
  if (data.empty())
    return {};

  // First 4 bytes are section magic.
  if (data.size() < 4)
    fatal("the section is too short: " + sectionName);

  if (!sectionName.starts_with(".debug$"))
    fatal("invalid section: " + sectionName);

  uint32_t magic = support::endian::read32le(data.data());
  uint32_t expectedMagic = sectionName == ".debug$H"
                               ? DEBUG_HASHES_SECTION_MAGIC
                               : DEBUG_SECTION_MAGIC;
  if (magic != expectedMagic) {
    warn("ignoring section " + sectionName + " with unrecognized magic 0x" +
         utohexstr(magic));
    return {};
  }
  return data.slice(4);
}

SectionChunk *SectionChunk::findByName(ArrayRef<SectionChunk *> sections,
                                       StringRef name) {
  for (SectionChunk *c : sections)
    if (c->getSectionName() == name)
      return c;
  return nullptr;
}

void SectionChunk::replace(SectionChunk *other) {
  p2Align = std::max(p2Align, other->p2Align);
  other->repl = repl;
  other->live = false;
}

uint32_t SectionChunk::getSectionNumber() const {
  DataRefImpl r;
  r.p = reinterpret_cast<uintptr_t>(header);
  SectionRef s(r, file->getCOFFObj());
  return s.getIndex() + 1;
}

CommonChunk::CommonChunk(const COFFSymbolRef s) : sym(s) {
  // The value of a common symbol is its size. Align all common symbols smaller
  // than 32 bytes naturally, i.e. round the size up to the next power of two.
  // This is what MSVC link.exe does.
  setAlignment(std::min(32U, uint32_t(PowerOf2Ceil(sym.getValue()))));
  hasData = false;
}

uint32_t CommonChunk::getOutputCharacteristics() const {
  return IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
         IMAGE_SCN_MEM_WRITE;
}

void StringChunk::writeTo(uint8_t *buf) const {
  memcpy(buf, str.data(), str.size());
  buf[str.size()] = '\0';
}

ImportThunkChunkX64::ImportThunkChunkX64(COFFLinkerContext &ctx, Defined *s)
    : ImportThunkChunk(ctx, s) {
  // Intel Optimization Manual says that all branch targets
  // should be 16-byte aligned. MSVC linker does this too.
  setAlignment(16);
}

void ImportThunkChunkX64::writeTo(uint8_t *buf) const {
  memcpy(buf, importThunkX86, sizeof(importThunkX86));
  // The first two bytes is a JMP instruction. Fill its operand.
  write32le(buf + 2, impSymbol->getRVA() - rva - getSize());
}

void ImportThunkChunkX86::getBaserels(std::vector<Baserel> *res) {
  res->emplace_back(getRVA() + 2, ctx.config.machine);
}

void ImportThunkChunkX86::writeTo(uint8_t *buf) const {
  memcpy(buf, importThunkX86, sizeof(importThunkX86));
  // The first two bytes is a JMP instruction. Fill its operand.
  write32le(buf + 2, impSymbol->getRVA() + ctx.config.imageBase);
}

void ImportThunkChunkARM::getBaserels(std::vector<Baserel> *res) {
  res->emplace_back(getRVA(), IMAGE_REL_BASED_ARM_MOV32T);
}

void ImportThunkChunkARM::writeTo(uint8_t *buf) const {
  memcpy(buf, importThunkARM, sizeof(importThunkARM));
  // Fix mov.w and mov.t operands.
  applyMOV32T(buf, impSymbol->getRVA() + ctx.config.imageBase);
}

void ImportThunkChunkARM64::writeTo(uint8_t *buf) const {
  int64_t off = impSymbol->getRVA() & 0xfff;
  memcpy(buf, importThunkARM64, sizeof(importThunkARM64));
  applyArm64Addr(buf, impSymbol->getRVA(), rva, 12);
  applyArm64Ldr(buf + 4, off);
}

// A Thumb2, PIC, non-interworking range extension thunk.
const uint8_t armThunk[] = {
    0x40, 0xf2, 0x00, 0x0c, // P:  movw ip,:lower16:S - (P + (L1-P) + 4)
    0xc0, 0xf2, 0x00, 0x0c, //     movt ip,:upper16:S - (P + (L1-P) + 4)
    0xe7, 0x44,             // L1: add  pc, ip
};

size_t RangeExtensionThunkARM::getSize() const {
  assert(ctx.config.machine == ARMNT);
  (void)&ctx;
  return sizeof(armThunk);
}

void RangeExtensionThunkARM::writeTo(uint8_t *buf) const {
  assert(ctx.config.machine == ARMNT);
  uint64_t offset = target->getRVA() - rva - 12;
  memcpy(buf, armThunk, sizeof(armThunk));
  applyMOV32T(buf, uint32_t(offset));
}

// A position independent ARM64 adrp+add thunk, with a maximum range of
// +/- 4 GB, which is enough for any PE-COFF.
const uint8_t arm64Thunk[] = {
    0x10, 0x00, 0x00, 0x90, // adrp x16, Dest
    0x10, 0x02, 0x00, 0x91, // add  x16, x16, :lo12:Dest
    0x00, 0x02, 0x1f, 0xd6, // br   x16
};

size_t RangeExtensionThunkARM64::getSize() const {
  assert(ctx.config.machine == ARM64);
  (void)&ctx;
  return sizeof(arm64Thunk);
}

void RangeExtensionThunkARM64::writeTo(uint8_t *buf) const {
  assert(ctx.config.machine == ARM64);
  memcpy(buf, arm64Thunk, sizeof(arm64Thunk));
  applyArm64Addr(buf + 0, target->getRVA(), rva, 12);
  applyArm64Imm(buf + 4, target->getRVA() & 0xfff, 0);
}

LocalImportChunk::LocalImportChunk(COFFLinkerContext &c, Defined *s)
    : sym(s), ctx(c) {
  setAlignment(ctx.config.wordsize);
}

void LocalImportChunk::getBaserels(std::vector<Baserel> *res) {
  res->emplace_back(getRVA(), ctx.config.machine);
}

size_t LocalImportChunk::getSize() const { return ctx.config.wordsize; }

void LocalImportChunk::writeTo(uint8_t *buf) const {
  if (ctx.config.is64()) {
    write64le(buf, sym->getRVA() + ctx.config.imageBase);
  } else {
    write32le(buf, sym->getRVA() + ctx.config.imageBase);
  }
}

void RVATableChunk::writeTo(uint8_t *buf) const {
  ulittle32_t *begin = reinterpret_cast<ulittle32_t *>(buf);
  size_t cnt = 0;
  for (const ChunkAndOffset &co : syms)
    begin[cnt++] = co.inputChunk->getRVA() + co.offset;
  llvm::sort(begin, begin + cnt);
  assert(std::unique(begin, begin + cnt) == begin + cnt &&
         "RVA tables should be de-duplicated");
}

void RVAFlagTableChunk::writeTo(uint8_t *buf) const {
  struct RVAFlag {
    ulittle32_t rva;
    uint8_t flag;
  };
  auto flags =
      MutableArrayRef(reinterpret_cast<RVAFlag *>(buf), syms.size());
  for (auto t : zip(syms, flags)) {
    const auto &sym = std::get<0>(t);
    auto &flag = std::get<1>(t);
    flag.rva = sym.inputChunk->getRVA() + sym.offset;
    flag.flag = 0;
  }
  llvm::sort(flags,
             [](const RVAFlag &a, const RVAFlag &b) { return a.rva < b.rva; });
  assert(llvm::unique(flags, [](const RVAFlag &a,
                                const RVAFlag &b) { return a.rva == b.rva; }) ==
             flags.end() &&
         "RVA tables should be de-duplicated");
}

size_t ECCodeMapChunk::getSize() const {
  return map.size() * sizeof(chpe_range_entry);
}

void ECCodeMapChunk::writeTo(uint8_t *buf) const {
  auto table = reinterpret_cast<chpe_range_entry *>(buf);
  for (uint32_t i = 0; i < map.size(); i++) {
    const ECCodeMapEntry &entry = map[i];
    uint32_t start = entry.first->getRVA();
    table[i].StartOffset = start | entry.type;
    table[i].Length = entry.last->getRVA() + entry.last->getSize() - start;
  }
}

// MinGW specific, for the "automatic import of variables from DLLs" feature.
size_t PseudoRelocTableChunk::getSize() const {
  if (relocs.empty())
    return 0;
  return 12 + 12 * relocs.size();
}

// MinGW specific.
void PseudoRelocTableChunk::writeTo(uint8_t *buf) const {
  if (relocs.empty())
    return;

  ulittle32_t *table = reinterpret_cast<ulittle32_t *>(buf);
  // This is the list header, to signal the runtime pseudo relocation v2
  // format.
  table[0] = 0;
  table[1] = 0;
  table[2] = 1;

  size_t idx = 3;
  for (const RuntimePseudoReloc &rpr : relocs) {
    table[idx + 0] = rpr.sym->getRVA();
    table[idx + 1] = rpr.target->getRVA() + rpr.targetOffset;
    table[idx + 2] = rpr.flags;
    idx += 3;
  }
}

// Windows-specific. This class represents a block in .reloc section.
// The format is described here.
//
// On Windows, each DLL is linked against a fixed base address and
// usually loaded to that address. However, if there's already another
// DLL that overlaps, the loader has to relocate it. To do that, DLLs
// contain .reloc sections which contain offsets that need to be fixed
// up at runtime. If the loader finds that a DLL cannot be loaded to its
// desired base address, it loads it to somewhere else, and add <actual
// base address> - <desired base address> to each offset that is
// specified by the .reloc section. In ELF terms, .reloc sections
// contain relative relocations in REL format (as opposed to RELA.)
//
// This already significantly reduces the size of relocations compared
// to ELF .rel.dyn, but Windows does more to reduce it (probably because
// it was invented for PCs in the late '80s or early '90s.)  Offsets in
// .reloc are grouped by page where the page size is 12 bits, and
// offsets sharing the same page address are stored consecutively to
// represent them with less space. This is very similar to the page
// table which is grouped by (multiple stages of) pages.
//
// For example, let's say we have 0x00030, 0x00500, 0x00700, 0x00A00,
// 0x20004, and 0x20008 in a .reloc section for x64. The uppermost 4
// bits have a type IMAGE_REL_BASED_DIR64 or 0xA. In the section, they
// are represented like this:
//
//   0x00000  -- page address (4 bytes)
//   16       -- size of this block (4 bytes)
//     0xA030 -- entries (2 bytes each)
//     0xA500
//     0xA700
//     0xAA00
//   0x20000  -- page address (4 bytes)
//   12       -- size of this block (4 bytes)
//     0xA004 -- entries (2 bytes each)
//     0xA008
//
// Usually we have a lot of relocations for each page, so the number of
// bytes for one .reloc entry is close to 2 bytes on average.
BaserelChunk::BaserelChunk(uint32_t page, Baserel *begin, Baserel *end) {
  // Block header consists of 4 byte page RVA and 4 byte block size.
  // Each entry is 2 byte. Last entry may be padding.
  data.resize(alignTo((end - begin) * 2 + 8, 4));
  uint8_t *p = data.data();
  write32le(p, page);
  write32le(p + 4, data.size());
  p += 8;
  for (Baserel *i = begin; i != end; ++i) {
    write16le(p, (i->type << 12) | (i->rva - page));
    p += 2;
  }
}

void BaserelChunk::writeTo(uint8_t *buf) const {
  memcpy(buf, data.data(), data.size());
}

uint8_t Baserel::getDefaultType(llvm::COFF::MachineTypes machine) {
  switch (machine) {
  case AMD64:
  case ARM64:
    return IMAGE_REL_BASED_DIR64;
  case I386:
  case ARMNT:
    return IMAGE_REL_BASED_HIGHLOW;
  default:
    llvm_unreachable("unknown machine type");
  }
}

MergeChunk::MergeChunk(uint32_t alignment)
    : builder(StringTableBuilder::RAW, llvm::Align(alignment)) {
  setAlignment(alignment);
}

void MergeChunk::addSection(COFFLinkerContext &ctx, SectionChunk *c) {
  assert(isPowerOf2_32(c->getAlignment()));
  uint8_t p2Align = llvm::Log2_32(c->getAlignment());
  assert(p2Align < std::size(ctx.mergeChunkInstances));
  auto *&mc = ctx.mergeChunkInstances[p2Align];
  if (!mc)
    mc = make<MergeChunk>(c->getAlignment());
  mc->sections.push_back(c);
}

void MergeChunk::finalizeContents() {
  assert(!finalized && "should only finalize once");
  for (SectionChunk *c : sections)
    if (c->live)
      builder.add(toStringRef(c->getContents()));
  builder.finalize();
  finalized = true;
}

void MergeChunk::assignSubsectionRVAs() {
  for (SectionChunk *c : sections) {
    if (!c->live)
      continue;
    size_t off = builder.getOffset(toStringRef(c->getContents()));
    c->setRVA(rva + off);
  }
}

uint32_t MergeChunk::getOutputCharacteristics() const {
  return IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA;
}

size_t MergeChunk::getSize() const {
  return builder.getSize();
}

void MergeChunk::writeTo(uint8_t *buf) const {
  builder.write(buf);
}

// MinGW specific.
size_t AbsolutePointerChunk::getSize() const { return ctx.config.wordsize; }

void AbsolutePointerChunk::writeTo(uint8_t *buf) const {
  if (ctx.config.is64()) {
    write64le(buf, value);
  } else {
    write32le(buf, value);
  }
}

} // namespace lld::coff
