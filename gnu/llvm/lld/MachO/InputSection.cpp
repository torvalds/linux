//===- InputSection.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputSection.h"
#include "ConcatOutputSection.h"
#include "Config.h"
#include "InputFiles.h"
#include "OutputSegment.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "UnwindInfoSection.h"
#include "Writer.h"

#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/xxhash.h"

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::support;
using namespace lld;
using namespace lld::macho;

// Verify ConcatInputSection's size on 64-bit builds. The size of std::vector
// can differ based on STL debug levels (e.g. iterator debugging on MSVC's STL),
// so account for that.
static_assert(sizeof(void *) != 8 ||
                  sizeof(ConcatInputSection) == sizeof(std::vector<Reloc>) + 88,
              "Try to minimize ConcatInputSection's size, we create many "
              "instances of it");

std::vector<ConcatInputSection *> macho::inputSections;
int macho::inputSectionsOrder = 0;

// Call this function to add a new InputSection and have it routed to the
// appropriate container. Depending on its type and current config, it will
// either be added to 'inputSections' vector or to a synthetic section.
void lld::macho::addInputSection(InputSection *inputSection) {
  if (auto *isec = dyn_cast<ConcatInputSection>(inputSection)) {
    if (isec->isCoalescedWeak())
      return;
    if (config->emitRelativeMethodLists &&
        ObjCMethListSection::isMethodList(isec)) {
      if (in.objcMethList->inputOrder == UnspecifiedInputOrder)
        in.objcMethList->inputOrder = inputSectionsOrder++;
      in.objcMethList->addInput(isec);
      isec->parent = in.objcMethList;
      return;
    }
    if (config->emitInitOffsets &&
        sectionType(isec->getFlags()) == S_MOD_INIT_FUNC_POINTERS) {
      in.initOffsets->addInput(isec);
      return;
    }
    isec->outSecOff = inputSectionsOrder++;
    auto *osec = ConcatOutputSection::getOrCreateForInput(isec);
    isec->parent = osec;
    inputSections.push_back(isec);
  } else if (auto *isec = dyn_cast<CStringInputSection>(inputSection)) {
    if (isec->getName() == section_names::objcMethname) {
      if (in.objcMethnameSection->inputOrder == UnspecifiedInputOrder)
        in.objcMethnameSection->inputOrder = inputSectionsOrder++;
      in.objcMethnameSection->addInput(isec);
    } else {
      if (in.cStringSection->inputOrder == UnspecifiedInputOrder)
        in.cStringSection->inputOrder = inputSectionsOrder++;
      in.cStringSection->addInput(isec);
    }
  } else if (auto *isec = dyn_cast<WordLiteralInputSection>(inputSection)) {
    if (in.wordLiteralSection->inputOrder == UnspecifiedInputOrder)
      in.wordLiteralSection->inputOrder = inputSectionsOrder++;
    in.wordLiteralSection->addInput(isec);
  } else {
    llvm_unreachable("unexpected input section kind");
  }

  assert(inputSectionsOrder <= UnspecifiedInputOrder);
}

uint64_t InputSection::getFileSize() const {
  return isZeroFill(getFlags()) ? 0 : getSize();
}

uint64_t InputSection::getVA(uint64_t off) const {
  return parent->addr + getOffset(off);
}

static uint64_t resolveSymbolVA(const Symbol *sym, uint8_t type) {
  const RelocAttrs &relocAttrs = target->getRelocAttrs(type);
  if (relocAttrs.hasAttr(RelocAttrBits::BRANCH))
    return sym->resolveBranchVA();
  if (relocAttrs.hasAttr(RelocAttrBits::GOT))
    return sym->resolveGotVA();
  if (relocAttrs.hasAttr(RelocAttrBits::TLV))
    return sym->resolveTlvVA();
  return sym->getVA();
}

const Defined *InputSection::getContainingSymbol(uint64_t off) const {
  auto *nextSym = llvm::upper_bound(
      symbols, off, [](uint64_t a, const Defined *b) { return a < b->value; });
  if (nextSym == symbols.begin())
    return nullptr;
  return *std::prev(nextSym);
}

std::string InputSection::getLocation(uint64_t off) const {
  // First, try to find a symbol that's near the offset. Use it as a reference
  // point.
  if (auto *sym = getContainingSymbol(off))
    return (toString(getFile()) + ":(symbol " + toString(*sym) + "+0x" +
            Twine::utohexstr(off - sym->value) + ")")
        .str();

  // If that fails, use the section itself as a reference point.
  for (const Subsection &subsec : section.subsections) {
    if (subsec.isec == this) {
      off += subsec.offset;
      break;
    }
  }

  return (toString(getFile()) + ":(" + getName() + "+0x" +
          Twine::utohexstr(off) + ")")
      .str();
}

std::string InputSection::getSourceLocation(uint64_t off) const {
  auto *obj = dyn_cast_or_null<ObjFile>(getFile());
  if (!obj)
    return {};

  DWARFCache *dwarf = obj->getDwarf();
  if (!dwarf)
    return std::string();

  for (const Subsection &subsec : section.subsections) {
    if (subsec.isec == this) {
      off += subsec.offset;
      break;
    }
  }

  auto createMsg = [&](StringRef path, unsigned line) {
    std::string filename = sys::path::filename(path).str();
    std::string lineStr = (":" + Twine(line)).str();
    if (filename == path)
      return filename + lineStr;
    return (filename + lineStr + " (" + path + lineStr + ")").str();
  };

  // First, look up a function for a given offset.
  if (std::optional<DILineInfo> li = dwarf->getDILineInfo(
          section.addr + off, object::SectionedAddress::UndefSection))
    return createMsg(li->FileName, li->Line);

  // If it failed, look up again as a variable.
  if (const Defined *sym = getContainingSymbol(off)) {
    // Symbols are generally prefixed with an underscore, which is not included
    // in the debug information.
    StringRef symName = sym->getName();
    if (!symName.empty() && symName[0] == '_')
      symName = symName.substr(1);

    if (std::optional<std::pair<std::string, unsigned>> fileLine =
            dwarf->getVariableLoc(symName))
      return createMsg(fileLine->first, fileLine->second);
  }

  // Try to get the source file's name from the DWARF information.
  if (obj->compileUnit)
    return obj->sourceFile();

  return {};
}

const Reloc *InputSection::getRelocAt(uint32_t off) const {
  auto it = llvm::find_if(
      relocs, [=](const macho::Reloc &r) { return r.offset == off; });
  if (it == relocs.end())
    return nullptr;
  return &*it;
}

void ConcatInputSection::foldIdentical(ConcatInputSection *copy) {
  align = std::max(align, copy->align);
  copy->live = false;
  copy->wasCoalesced = true;
  copy->replacement = this;
  for (auto &copySym : copy->symbols)
    copySym->wasIdenticalCodeFolded = true;

  symbols.insert(symbols.end(), copy->symbols.begin(), copy->symbols.end());
  copy->symbols.clear();

  // Remove duplicate compact unwind info for symbols at the same address.
  if (symbols.empty())
    return;
  for (auto it = symbols.begin() + 1; it != symbols.end(); ++it) {
    assert((*it)->value == 0);
    (*it)->originalUnwindEntry = nullptr;
  }
}

void ConcatInputSection::writeTo(uint8_t *buf) {
  assert(!shouldOmitFromOutput());

  if (getFileSize() == 0)
    return;

  memcpy(buf, data.data(), data.size());

  for (size_t i = 0; i < relocs.size(); i++) {
    const Reloc &r = relocs[i];
    uint8_t *loc = buf + r.offset;
    uint64_t referentVA = 0;

    const bool needsFixup = config->emitChainedFixups &&
                            target->hasAttr(r.type, RelocAttrBits::UNSIGNED);
    if (target->hasAttr(r.type, RelocAttrBits::SUBTRAHEND)) {
      const Symbol *fromSym = r.referent.get<Symbol *>();
      const Reloc &minuend = relocs[++i];
      uint64_t minuendVA;
      if (const Symbol *toSym = minuend.referent.dyn_cast<Symbol *>())
        minuendVA = toSym->getVA() + minuend.addend;
      else {
        auto *referentIsec = minuend.referent.get<InputSection *>();
        assert(!::shouldOmitFromOutput(referentIsec));
        minuendVA = referentIsec->getVA(minuend.addend);
      }
      referentVA = minuendVA - fromSym->getVA();
    } else if (auto *referentSym = r.referent.dyn_cast<Symbol *>()) {
      if (target->hasAttr(r.type, RelocAttrBits::LOAD) &&
          !referentSym->isInGot())
        target->relaxGotLoad(loc, r.type);
      // For dtrace symbols, do not handle them as normal undefined symbols
      if (referentSym->getName().starts_with("___dtrace_")) {
        // Change dtrace call site to pre-defined instructions
        target->handleDtraceReloc(referentSym, r, loc);
        continue;
      }
      referentVA = resolveSymbolVA(referentSym, r.type) + r.addend;

      if (isThreadLocalVariables(getFlags()) && isa<Defined>(referentSym)) {
        // References from thread-local variable sections are treated as offsets
        // relative to the start of the thread-local data memory area, which
        // is initialized via copying all the TLV data sections (which are all
        // contiguous).
        referentVA -= firstTLVDataSection->addr;
      } else if (needsFixup) {
        writeChainedFixup(loc, referentSym, r.addend);
        continue;
      }
    } else if (auto *referentIsec = r.referent.dyn_cast<InputSection *>()) {
      assert(!::shouldOmitFromOutput(referentIsec));
      referentVA = referentIsec->getVA(r.addend);

      if (needsFixup) {
        writeChainedRebase(loc, referentVA);
        continue;
      }
    }
    target->relocateOne(loc, r, referentVA, getVA() + r.offset);
  }
}

ConcatInputSection *macho::makeSyntheticInputSection(StringRef segName,
                                                     StringRef sectName,
                                                     uint32_t flags,
                                                     ArrayRef<uint8_t> data,
                                                     uint32_t align) {
  Section &section =
      *make<Section>(/*file=*/nullptr, segName, sectName, flags, /*addr=*/0);
  auto isec = make<ConcatInputSection>(section, data, align);
  // Since this is an explicitly created 'fake' input section,
  // it should not be dead stripped.
  isec->live = true;
  section.subsections.push_back({0, isec});
  return isec;
}

void CStringInputSection::splitIntoPieces() {
  size_t off = 0;
  StringRef s = toStringRef(data);
  while (!s.empty()) {
    size_t end = s.find(0);
    if (end == StringRef::npos)
      fatal(getLocation(off) + ": string is not null terminated");
    uint32_t hash = deduplicateLiterals ? xxh3_64bits(s.take_front(end)) : 0;
    pieces.emplace_back(off, hash);
    size_t size = end + 1; // include null terminator
    s = s.substr(size);
    off += size;
  }
}

StringPiece &CStringInputSection::getStringPiece(uint64_t off) {
  if (off >= data.size())
    fatal(toString(this) + ": offset is outside the section");

  auto it =
      partition_point(pieces, [=](StringPiece p) { return p.inSecOff <= off; });
  return it[-1];
}

const StringPiece &CStringInputSection::getStringPiece(uint64_t off) const {
  return const_cast<CStringInputSection *>(this)->getStringPiece(off);
}

size_t CStringInputSection::getStringPieceIndex(uint64_t off) const {
  if (off >= data.size())
    fatal(toString(this) + ": offset is outside the section");

  auto it =
      partition_point(pieces, [=](StringPiece p) { return p.inSecOff <= off; });
  return std::distance(pieces.begin(), it) - 1;
}

uint64_t CStringInputSection::getOffset(uint64_t off) const {
  const StringPiece &piece = getStringPiece(off);
  uint64_t addend = off - piece.inSecOff;
  return piece.outSecOff + addend;
}

WordLiteralInputSection::WordLiteralInputSection(const Section &section,
                                                 ArrayRef<uint8_t> data,
                                                 uint32_t align)
    : InputSection(WordLiteralKind, section, data, align) {
  switch (sectionType(getFlags())) {
  case S_4BYTE_LITERALS:
    power2LiteralSize = 2;
    break;
  case S_8BYTE_LITERALS:
    power2LiteralSize = 3;
    break;
  case S_16BYTE_LITERALS:
    power2LiteralSize = 4;
    break;
  default:
    llvm_unreachable("invalid literal section type");
  }

  live.resize(data.size() >> power2LiteralSize, !config->deadStrip);
}

uint64_t WordLiteralInputSection::getOffset(uint64_t off) const {
  auto *osec = cast<WordLiteralSection>(parent);
  const uintptr_t buf = reinterpret_cast<uintptr_t>(data.data());
  switch (sectionType(getFlags())) {
  case S_4BYTE_LITERALS:
    return osec->getLiteral4Offset(buf + (off & ~3LLU)) | (off & 3);
  case S_8BYTE_LITERALS:
    return osec->getLiteral8Offset(buf + (off & ~7LLU)) | (off & 7);
  case S_16BYTE_LITERALS:
    return osec->getLiteral16Offset(buf + (off & ~15LLU)) | (off & 15);
  default:
    llvm_unreachable("invalid literal section type");
  }
}

bool macho::isCodeSection(const InputSection *isec) {
  uint32_t type = sectionType(isec->getFlags());
  if (type != S_REGULAR && type != S_COALESCED)
    return false;

  uint32_t attr = isec->getFlags() & SECTION_ATTRIBUTES_USR;
  if (attr == S_ATTR_PURE_INSTRUCTIONS)
    return true;

  if (isec->getSegName() == segment_names::text)
    return StringSwitch<bool>(isec->getName())
        .Cases(section_names::textCoalNt, section_names::staticInit, true)
        .Default(false);

  return false;
}

bool macho::isCfStringSection(const InputSection *isec) {
  return isec->getName() == section_names::cfString &&
         isec->getSegName() == segment_names::data;
}

bool macho::isClassRefsSection(const InputSection *isec) {
  return isec->getName() == section_names::objcClassRefs &&
         isec->getSegName() == segment_names::data;
}

bool macho::isSelRefsSection(const InputSection *isec) {
  return isec->getName() == section_names::objcSelrefs &&
         isec->getSegName() == segment_names::data;
}

bool macho::isEhFrameSection(const InputSection *isec) {
  return isec->getName() == section_names::ehFrame &&
         isec->getSegName() == segment_names::text;
}

bool macho::isGccExceptTabSection(const InputSection *isec) {
  return isec->getName() == section_names::gccExceptTab &&
         isec->getSegName() == segment_names::text;
}

std::string lld::toString(const InputSection *isec) {
  return (toString(isec->getFile()) + ":(" + isec->getName() + ")").str();
}
