//===- UnwindInfoSection.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UnwindInfoSection.h"
#include "InputSection.h"
#include "Layout.h"
#include "OutputSection.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"

#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Parallel.h"

#include "mach-o/compact_unwind_encoding.h"

#include <numeric>

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::macho;

#define COMMON_ENCODINGS_MAX 127
#define COMPACT_ENCODINGS_MAX 256

#define SECOND_LEVEL_PAGE_BYTES 4096
#define SECOND_LEVEL_PAGE_WORDS (SECOND_LEVEL_PAGE_BYTES / sizeof(uint32_t))
#define REGULAR_SECOND_LEVEL_ENTRIES_MAX                                       \
  ((SECOND_LEVEL_PAGE_BYTES -                                                  \
    sizeof(unwind_info_regular_second_level_page_header)) /                    \
   sizeof(unwind_info_regular_second_level_entry))
#define COMPRESSED_SECOND_LEVEL_ENTRIES_MAX                                    \
  ((SECOND_LEVEL_PAGE_BYTES -                                                  \
    sizeof(unwind_info_compressed_second_level_page_header)) /                 \
   sizeof(uint32_t))

#define COMPRESSED_ENTRY_FUNC_OFFSET_BITS 24
#define COMPRESSED_ENTRY_FUNC_OFFSET_MASK                                      \
  UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(~0)

static_assert(static_cast<uint32_t>(UNWIND_X86_64_DWARF_SECTION_OFFSET) ==
                  static_cast<uint32_t>(UNWIND_ARM64_DWARF_SECTION_OFFSET) &&
              static_cast<uint32_t>(UNWIND_X86_64_DWARF_SECTION_OFFSET) ==
                  static_cast<uint32_t>(UNWIND_X86_DWARF_SECTION_OFFSET));

constexpr uint64_t DWARF_SECTION_OFFSET = UNWIND_X86_64_DWARF_SECTION_OFFSET;

// Compact Unwind format is a Mach-O evolution of DWARF Unwind that
// optimizes space and exception-time lookup.  Most DWARF unwind
// entries can be replaced with Compact Unwind entries, but the ones
// that cannot are retained in DWARF form.
//
// This comment will address macro-level organization of the pre-link
// and post-link compact unwind tables. For micro-level organization
// pertaining to the bitfield layout of the 32-bit compact unwind
// entries, see libunwind/include/mach-o/compact_unwind_encoding.h
//
// Important clarifying factoids:
//
// * __LD,__compact_unwind is the compact unwind format for compiler
// output and linker input. It is never a final output. It could be
// an intermediate output with the `-r` option which retains relocs.
//
// * __TEXT,__unwind_info is the compact unwind format for final
// linker output. It is never an input.
//
// * __TEXT,__eh_frame is the DWARF format for both linker input and output.
//
// * __TEXT,__unwind_info entries are divided into 4 KiB pages (2nd
// level) by ascending address, and the pages are referenced by an
// index (1st level) in the section header.
//
// * Following the headers in __TEXT,__unwind_info, the bulk of the
// section contains a vector of compact unwind entries
// `{functionOffset, encoding}` sorted by ascending `functionOffset`.
// Adjacent entries with the same encoding can be folded to great
// advantage, achieving a 3-order-of-magnitude reduction in the
// number of entries.
//
// Refer to the definition of unwind_info_section_header in
// compact_unwind_encoding.h for an overview of the format we are encoding
// here.

// TODO(gkm): how do we align the 2nd-level pages?

// The various fields in the on-disk representation of each compact unwind
// entry.
#define FOR_EACH_CU_FIELD(DO)                                                  \
  DO(Ptr, functionAddress)                                                     \
  DO(uint32_t, functionLength)                                                 \
  DO(compact_unwind_encoding_t, encoding)                                      \
  DO(Ptr, personality)                                                         \
  DO(Ptr, lsda)

CREATE_LAYOUT_CLASS(CompactUnwind, FOR_EACH_CU_FIELD);

#undef FOR_EACH_CU_FIELD

// LLD's internal representation of a compact unwind entry.
struct CompactUnwindEntry {
  uint64_t functionAddress;
  uint32_t functionLength;
  compact_unwind_encoding_t encoding;
  Symbol *personality;
  InputSection *lsda;
};

using EncodingMap = DenseMap<compact_unwind_encoding_t, size_t>;

struct SecondLevelPage {
  uint32_t kind;
  size_t entryIndex;
  size_t entryCount;
  size_t byteCount;
  std::vector<compact_unwind_encoding_t> localEncodings;
  EncodingMap localEncodingIndexes;
};

// UnwindInfoSectionImpl allows us to avoid cluttering our header file with a
// lengthy definition of UnwindInfoSection.
class UnwindInfoSectionImpl final : public UnwindInfoSection {
public:
  UnwindInfoSectionImpl() : cuLayout(target->wordSize) {}
  uint64_t getSize() const override { return unwindInfoSize; }
  void prepare() override;
  void finalize() override;
  void writeTo(uint8_t *buf) const override;

private:
  void prepareRelocations(ConcatInputSection *);
  void relocateCompactUnwind(std::vector<CompactUnwindEntry> &);
  void encodePersonalities();
  Symbol *canonicalizePersonality(Symbol *);

  uint64_t unwindInfoSize = 0;
  SmallVector<decltype(symbols)::value_type, 0> symbolsVec;
  CompactUnwindLayout cuLayout;
  std::vector<std::pair<compact_unwind_encoding_t, size_t>> commonEncodings;
  EncodingMap commonEncodingIndexes;
  // The entries here will be in the same order as their originating symbols
  // in symbolsVec.
  std::vector<CompactUnwindEntry> cuEntries;
  // Indices into the cuEntries vector.
  std::vector<size_t> cuIndices;
  std::vector<Symbol *> personalities;
  SmallDenseMap<std::pair<InputSection *, uint64_t /* addend */>, Symbol *>
      personalityTable;
  // Indices into cuEntries for CUEs with a non-null LSDA.
  std::vector<size_t> entriesWithLsda;
  // Map of cuEntries index to an index within the LSDA array.
  DenseMap<size_t, uint32_t> lsdaIndex;
  std::vector<SecondLevelPage> secondLevelPages;
  uint64_t level2PagesOffset = 0;
  // The highest-address function plus its size. The unwinder needs this to
  // determine the address range that is covered by unwind info.
  uint64_t cueEndBoundary = 0;
};

UnwindInfoSection::UnwindInfoSection()
    : SyntheticSection(segment_names::text, section_names::unwindInfo) {
  align = 4;
}

// Record function symbols that may need entries emitted in __unwind_info, which
// stores unwind data for address ranges.
//
// Note that if several adjacent functions have the same unwind encoding and
// personality function and no LSDA, they share one unwind entry. For this to
// work, functions without unwind info need explicit "no unwind info" unwind
// entries -- else the unwinder would think they have the unwind info of the
// closest function with unwind info right before in the image. Thus, we add
// function symbols for each unique address regardless of whether they have
// associated unwind info.
void UnwindInfoSection::addSymbol(const Defined *d) {
  if (d->unwindEntry())
    allEntriesAreOmitted = false;
  // We don't yet know the final output address of this symbol, but we know that
  // they are uniquely determined by a combination of the isec and value, so
  // we use that as the key here.
  auto p = symbols.insert({{d->isec(), d->value}, d});
  // If we have multiple symbols at the same address, only one of them can have
  // an associated unwind entry.
  if (!p.second && d->unwindEntry()) {
    assert(p.first->second == d || !p.first->second->unwindEntry());
    p.first->second = d;
  }
}

void UnwindInfoSectionImpl::prepare() {
  // This iteration needs to be deterministic, since prepareRelocations may add
  // entries to the GOT. Hence the use of a MapVector for
  // UnwindInfoSection::symbols.
  for (const Defined *d : make_second_range(symbols))
    if (d->unwindEntry()) {
      if (d->unwindEntry()->getName() == section_names::compactUnwind) {
        prepareRelocations(d->unwindEntry());
      } else {
        // We don't have to add entries to the GOT here because FDEs have
        // explicit GOT relocations, so Writer::scanRelocations() will add those
        // GOT entries. However, we still need to canonicalize the personality
        // pointers (like prepareRelocations() does for CU entries) in order
        // to avoid overflowing the 3-personality limit.
        FDE &fde = cast<ObjFile>(d->getFile())->fdes[d->unwindEntry()];
        fde.personality = canonicalizePersonality(fde.personality);
      }
    }
}

// Compact unwind relocations have different semantics, so we handle them in a
// separate code path from regular relocations. First, we do not wish to add
// rebase opcodes for __LD,__compact_unwind, because that section doesn't
// actually end up in the final binary. Second, personality pointers always
// reside in the GOT and must be treated specially.
void UnwindInfoSectionImpl::prepareRelocations(ConcatInputSection *isec) {
  assert(!isec->shouldOmitFromOutput() &&
         "__compact_unwind section should not be omitted");

  // FIXME: Make this skip relocations for CompactUnwindEntries that
  // point to dead-stripped functions. That might save some amount of
  // work. But since there are usually just few personality functions
  // that are referenced from many places, at least some of them likely
  // live, it wouldn't reduce number of got entries.
  for (size_t i = 0; i < isec->relocs.size(); ++i) {
    Reloc &r = isec->relocs[i];
    assert(target->hasAttr(r.type, RelocAttrBits::UNSIGNED));
    // Since compact unwind sections aren't part of the inputSections vector,
    // they don't get canonicalized by scanRelocations(), so we have to do the
    // canonicalization here.
    if (auto *referentIsec = r.referent.dyn_cast<InputSection *>())
      r.referent = referentIsec->canonical();

    // Functions and LSDA entries always reside in the same object file as the
    // compact unwind entries that references them, and thus appear as section
    // relocs. There is no need to prepare them. We only prepare relocs for
    // personality functions.
    if (r.offset != cuLayout.personalityOffset)
      continue;

    if (auto *s = r.referent.dyn_cast<Symbol *>()) {
      // Personality functions are nearly always system-defined (e.g.,
      // ___gxx_personality_v0 for C++) and relocated as dylib symbols.  When an
      // application provides its own personality function, it might be
      // referenced by an extern Defined symbol reloc, or a local section reloc.
      if (auto *defined = dyn_cast<Defined>(s)) {
        // XXX(vyng) This is a special case for handling duplicate personality
        // symbols. Note that LD64's behavior is a bit different and it is
        // inconsistent with how symbol resolution usually work
        //
        // So we've decided not to follow it. Instead, simply pick the symbol
        // with the same name from the symbol table to replace the local one.
        //
        // (See discussions/alternatives already considered on D107533)
        if (!defined->isExternal())
          if (Symbol *sym = symtab->find(defined->getName()))
            if (!sym->isLazy())
              r.referent = s = sym;
      }
      if (auto *undefined = dyn_cast<Undefined>(s)) {
        treatUndefinedSymbol(*undefined, isec, r.offset);
        // treatUndefinedSymbol() can replace s with a DylibSymbol; re-check.
        if (isa<Undefined>(s))
          continue;
      }

      // Similar to canonicalizePersonality(), but we also register a GOT entry.
      if (auto *defined = dyn_cast<Defined>(s)) {
        // Check if we have created a synthetic symbol at the same address.
        Symbol *&personality =
            personalityTable[{defined->isec(), defined->value}];
        if (personality == nullptr) {
          personality = defined;
          in.got->addEntry(defined);
        } else if (personality != defined) {
          r.referent = personality;
        }
        continue;
      }

      assert(isa<DylibSymbol>(s));
      in.got->addEntry(s);
      continue;
    }

    if (auto *referentIsec = r.referent.dyn_cast<InputSection *>()) {
      assert(!isCoalescedWeak(referentIsec));
      // Personality functions can be referenced via section relocations
      // if they live in the same object file. Create placeholder synthetic
      // symbols for them in the GOT. If the corresponding symbol is already
      // in the GOT, use that to avoid creating a duplicate entry. All GOT
      // entries needed by non-unwind sections will have already been added
      // by this point.
      Symbol *&s = personalityTable[{referentIsec, r.addend}];
      if (s == nullptr) {
        Defined *const *gotEntry =
            llvm::find_if(referentIsec->symbols, [&](Defined const *d) {
              return d->value == static_cast<uint64_t>(r.addend) &&
                     d->isInGot();
            });
        if (gotEntry != referentIsec->symbols.end()) {
          s = *gotEntry;
        } else {
          // This runs after dead stripping, so the noDeadStrip argument does
          // not matter.
          s = make<Defined>("<internal>", /*file=*/nullptr, referentIsec,
                            r.addend, /*size=*/0, /*isWeakDef=*/false,
                            /*isExternal=*/false, /*isPrivateExtern=*/false,
                            /*includeInSymtab=*/true,
                            /*isReferencedDynamically=*/false,
                            /*noDeadStrip=*/false);
          s->used = true;
          in.got->addEntry(s);
        }
      }
      r.referent = s;
      r.addend = 0;
    }
  }
}

Symbol *UnwindInfoSectionImpl::canonicalizePersonality(Symbol *personality) {
  if (auto *defined = dyn_cast_or_null<Defined>(personality)) {
    // Check if we have created a synthetic symbol at the same address.
    Symbol *&synth = personalityTable[{defined->isec(), defined->value}];
    if (synth == nullptr)
      synth = defined;
    else if (synth != defined)
      return synth;
  }
  return personality;
}

// We need to apply the relocations to the pre-link compact unwind section
// before converting it to post-link form. There should only be absolute
// relocations here: since we are not emitting the pre-link CU section, there
// is no source address to make a relative location meaningful.
void UnwindInfoSectionImpl::relocateCompactUnwind(
    std::vector<CompactUnwindEntry> &cuEntries) {
  parallelFor(0, symbolsVec.size(), [&](size_t i) {
    CompactUnwindEntry &cu = cuEntries[i];
    const Defined *d = symbolsVec[i].second;
    cu.functionAddress = d->getVA();
    if (!d->unwindEntry())
      return;

    // If we have DWARF unwind info, create a slimmed-down CU entry that points
    // to it.
    if (d->unwindEntry()->getName() == section_names::ehFrame) {
      // The unwinder will look for the DWARF entry starting at the hint,
      // assuming the hint points to a valid CFI record start. If it
      // fails to find the record, it proceeds in a linear search through the
      // contiguous CFI records from the hint until the end of the section.
      // Ideally, in the case where the offset is too large to be encoded, we
      // would instead encode the largest possible offset to a valid CFI record,
      // but since we don't keep track of that, just encode zero -- the start of
      // the section is always the start of a CFI record.
      uint64_t dwarfOffsetHint =
          d->unwindEntry()->outSecOff <= DWARF_SECTION_OFFSET
              ? d->unwindEntry()->outSecOff
              : 0;
      cu.encoding = target->modeDwarfEncoding | dwarfOffsetHint;
      const FDE &fde = cast<ObjFile>(d->getFile())->fdes[d->unwindEntry()];
      cu.functionLength = fde.funcLength;
      // Omit the DWARF personality from compact-unwind entry so that we
      // don't need to encode it.
      cu.personality = nullptr;
      cu.lsda = fde.lsda;
      return;
    }

    assert(d->unwindEntry()->getName() == section_names::compactUnwind);

    auto buf =
        reinterpret_cast<const uint8_t *>(d->unwindEntry()->data.data()) -
        target->wordSize;
    cu.functionLength =
        support::endian::read32le(buf + cuLayout.functionLengthOffset);
    cu.encoding = support::endian::read32le(buf + cuLayout.encodingOffset);
    for (const Reloc &r : d->unwindEntry()->relocs) {
      if (r.offset == cuLayout.personalityOffset)
        cu.personality = r.referent.get<Symbol *>();
      else if (r.offset == cuLayout.lsdaOffset)
        cu.lsda = r.getReferentInputSection();
    }
  });
}

// There should only be a handful of unique personality pointers, so we can
// encode them as 2-bit indices into a small array.
void UnwindInfoSectionImpl::encodePersonalities() {
  for (size_t idx : cuIndices) {
    CompactUnwindEntry &cu = cuEntries[idx];
    if (cu.personality == nullptr)
      continue;
    // Linear search is fast enough for a small array.
    auto it = find(personalities, cu.personality);
    uint32_t personalityIndex; // 1-based index
    if (it != personalities.end()) {
      personalityIndex = std::distance(personalities.begin(), it) + 1;
    } else {
      personalities.push_back(cu.personality);
      personalityIndex = personalities.size();
    }
    cu.encoding |=
        personalityIndex << llvm::countr_zero(
            static_cast<compact_unwind_encoding_t>(UNWIND_PERSONALITY_MASK));
  }
  if (personalities.size() > 3)
    error("too many personalities (" + Twine(personalities.size()) +
          ") for compact unwind to encode");
}

static bool canFoldEncoding(compact_unwind_encoding_t encoding) {
  // From compact_unwind_encoding.h:
  //  UNWIND_X86_64_MODE_STACK_IND:
  //  A "frameless" (RBP not used as frame pointer) function large constant
  //  stack size.  This case is like the previous, except the stack size is too
  //  large to encode in the compact unwind encoding.  Instead it requires that
  //  the function contains "subq $nnnnnnnn,RSP" in its prolog.  The compact
  //  encoding contains the offset to the nnnnnnnn value in the function in
  //  UNWIND_X86_64_FRAMELESS_STACK_SIZE.
  // Since this means the unwinder has to look at the `subq` in the function
  // of the unwind info's unwind address, two functions that have identical
  // unwind info can't be folded if it's using this encoding since both
  // entries need unique addresses.
  static_assert(static_cast<uint32_t>(UNWIND_X86_64_MODE_STACK_IND) ==
                static_cast<uint32_t>(UNWIND_X86_MODE_STACK_IND));
  if ((target->cpuType == CPU_TYPE_X86_64 || target->cpuType == CPU_TYPE_X86) &&
      (encoding & UNWIND_MODE_MASK) == UNWIND_X86_64_MODE_STACK_IND) {
    // FIXME: Consider passing in the two function addresses and getting
    // their two stack sizes off the `subq` and only returning false if they're
    // actually different.
    return false;
  }
  return true;
}

// Scan the __LD,__compact_unwind entries and compute the space needs of
// __TEXT,__unwind_info and __TEXT,__eh_frame.
void UnwindInfoSectionImpl::finalize() {
  if (symbols.empty())
    return;

  // At this point, the address space for __TEXT,__text has been
  // assigned, so we can relocate the __LD,__compact_unwind entries
  // into a temporary buffer. Relocation is necessary in order to sort
  // the CU entries by function address. Sorting is necessary so that
  // we can fold adjacent CU entries with identical encoding+personality
  // and without any LSDA. Folding is necessary because it reduces the
  // number of CU entries by as much as 3 orders of magnitude!
  cuEntries.resize(symbols.size());
  // The "map" part of the symbols MapVector was only needed for deduplication
  // in addSymbol(). Now that we are done adding, move the contents to a plain
  // std::vector for indexed access.
  symbolsVec = symbols.takeVector();
  relocateCompactUnwind(cuEntries);

  // Rather than sort & fold the 32-byte entries directly, we create a
  // vector of indices to entries and sort & fold that instead.
  cuIndices.resize(cuEntries.size());
  std::iota(cuIndices.begin(), cuIndices.end(), 0);
  llvm::sort(cuIndices, [&](size_t a, size_t b) {
    return cuEntries[a].functionAddress < cuEntries[b].functionAddress;
  });

  // Record the ending boundary before we fold the entries.
  cueEndBoundary = cuEntries[cuIndices.back()].functionAddress +
                   cuEntries[cuIndices.back()].functionLength;

  // Fold adjacent entries with matching encoding+personality and without LSDA
  // We use three iterators on the same cuIndices to fold in-situ:
  // (1) `foldBegin` is the first of a potential sequence of matching entries
  // (2) `foldEnd` is the first non-matching entry after `foldBegin`.
  // The semi-open interval [ foldBegin .. foldEnd ) contains a range
  // entries that can be folded into a single entry and written to ...
  // (3) `foldWrite`
  auto foldWrite = cuIndices.begin();
  for (auto foldBegin = cuIndices.begin(); foldBegin < cuIndices.end();) {
    auto foldEnd = foldBegin;
    // Common LSDA encodings (e.g. for C++ and Objective-C) contain offsets from
    // a base address. The base address is normally not contained directly in
    // the LSDA, and in that case, the personality function treats the starting
    // address of the function (which is computed by the unwinder) as the base
    // address and interprets the LSDA accordingly. The unwinder computes the
    // starting address of a function as the address associated with its CU
    // entry. For this reason, we cannot fold adjacent entries if they have an
    // LSDA, because folding would make the unwinder compute the wrong starting
    // address for the functions with the folded entries, which in turn would
    // cause the personality function to misinterpret the LSDA for those
    // functions. In the very rare case where the base address is encoded
    // directly in the LSDA, two functions at different addresses would
    // necessarily have different LSDAs, so their CU entries would not have been
    // folded anyway.
    while (++foldEnd < cuIndices.end() &&
           cuEntries[*foldBegin].encoding == cuEntries[*foldEnd].encoding &&
           !cuEntries[*foldBegin].lsda && !cuEntries[*foldEnd].lsda &&
           // If we've gotten to this point, we don't have an LSDA, which should
           // also imply that we don't have a personality function, since in all
           // likelihood a personality function needs the LSDA to do anything
           // useful. It can be technically valid to have a personality function
           // and no LSDA though (e.g. the C++ personality __gxx_personality_v0
           // is just a no-op without LSDA), so we still check for personality
           // function equivalence to handle that case.
           cuEntries[*foldBegin].personality ==
               cuEntries[*foldEnd].personality &&
           canFoldEncoding(cuEntries[*foldEnd].encoding))
      ;
    *foldWrite++ = *foldBegin;
    foldBegin = foldEnd;
  }
  cuIndices.erase(foldWrite, cuIndices.end());

  encodePersonalities();

  // Count frequencies of the folded encodings
  EncodingMap encodingFrequencies;
  for (size_t idx : cuIndices)
    encodingFrequencies[cuEntries[idx].encoding]++;

  // Make a vector of encodings, sorted by descending frequency
  for (const auto &frequency : encodingFrequencies)
    commonEncodings.emplace_back(frequency);
  llvm::sort(commonEncodings,
             [](const std::pair<compact_unwind_encoding_t, size_t> &a,
                const std::pair<compact_unwind_encoding_t, size_t> &b) {
               if (a.second == b.second)
                 // When frequencies match, secondarily sort on encoding
                 // to maintain parity with validate-unwind-info.py
                 return a.first > b.first;
               return a.second > b.second;
             });

  // Truncate the vector to 127 elements.
  // Common encoding indexes are limited to 0..126, while encoding
  // indexes 127..255 are local to each second-level page
  if (commonEncodings.size() > COMMON_ENCODINGS_MAX)
    commonEncodings.resize(COMMON_ENCODINGS_MAX);

  // Create a map from encoding to common-encoding-table index
  for (size_t i = 0; i < commonEncodings.size(); i++)
    commonEncodingIndexes[commonEncodings[i].first] = i;

  // Split folded encodings into pages, where each page is limited by ...
  // (a) 4 KiB capacity
  // (b) 24-bit difference between first & final function address
  // (c) 8-bit compact-encoding-table index,
  //     for which 0..126 references the global common-encodings table,
  //     and 127..255 references a local per-second-level-page table.
  // First we try the compact format and determine how many entries fit.
  // If more entries fit in the regular format, we use that.
  for (size_t i = 0; i < cuIndices.size();) {
    size_t idx = cuIndices[i];
    secondLevelPages.emplace_back();
    SecondLevelPage &page = secondLevelPages.back();
    page.entryIndex = i;
    uint64_t functionAddressMax =
        cuEntries[idx].functionAddress + COMPRESSED_ENTRY_FUNC_OFFSET_MASK;
    size_t n = commonEncodings.size();
    size_t wordsRemaining =
        SECOND_LEVEL_PAGE_WORDS -
        sizeof(unwind_info_compressed_second_level_page_header) /
            sizeof(uint32_t);
    while (wordsRemaining >= 1 && i < cuIndices.size()) {
      idx = cuIndices[i];
      const CompactUnwindEntry *cuPtr = &cuEntries[idx];
      if (cuPtr->functionAddress >= functionAddressMax)
        break;
      if (commonEncodingIndexes.count(cuPtr->encoding) ||
          page.localEncodingIndexes.count(cuPtr->encoding)) {
        i++;
        wordsRemaining--;
      } else if (wordsRemaining >= 2 && n < COMPACT_ENCODINGS_MAX) {
        page.localEncodings.emplace_back(cuPtr->encoding);
        page.localEncodingIndexes[cuPtr->encoding] = n++;
        i++;
        wordsRemaining -= 2;
      } else {
        break;
      }
    }
    page.entryCount = i - page.entryIndex;

    // If this is not the final page, see if it's possible to fit more entries
    // by using the regular format. This can happen when there are many unique
    // encodings, and we saturated the local encoding table early.
    if (i < cuIndices.size() &&
        page.entryCount < REGULAR_SECOND_LEVEL_ENTRIES_MAX) {
      page.kind = UNWIND_SECOND_LEVEL_REGULAR;
      page.entryCount = std::min(REGULAR_SECOND_LEVEL_ENTRIES_MAX,
                                 cuIndices.size() - page.entryIndex);
      i = page.entryIndex + page.entryCount;
    } else {
      page.kind = UNWIND_SECOND_LEVEL_COMPRESSED;
    }
  }

  for (size_t idx : cuIndices) {
    lsdaIndex[idx] = entriesWithLsda.size();
    if (cuEntries[idx].lsda)
      entriesWithLsda.push_back(idx);
  }

  // compute size of __TEXT,__unwind_info section
  level2PagesOffset = sizeof(unwind_info_section_header) +
                      commonEncodings.size() * sizeof(uint32_t) +
                      personalities.size() * sizeof(uint32_t) +
                      // The extra second-level-page entry is for the sentinel
                      (secondLevelPages.size() + 1) *
                          sizeof(unwind_info_section_header_index_entry) +
                      entriesWithLsda.size() *
                          sizeof(unwind_info_section_header_lsda_index_entry);
  unwindInfoSize =
      level2PagesOffset + secondLevelPages.size() * SECOND_LEVEL_PAGE_BYTES;
}

// All inputs are relocated and output addresses are known, so write!

void UnwindInfoSectionImpl::writeTo(uint8_t *buf) const {
  assert(!cuIndices.empty() && "call only if there is unwind info");

  // section header
  auto *uip = reinterpret_cast<unwind_info_section_header *>(buf);
  uip->version = 1;
  uip->commonEncodingsArraySectionOffset = sizeof(unwind_info_section_header);
  uip->commonEncodingsArrayCount = commonEncodings.size();
  uip->personalityArraySectionOffset =
      uip->commonEncodingsArraySectionOffset +
      (uip->commonEncodingsArrayCount * sizeof(uint32_t));
  uip->personalityArrayCount = personalities.size();
  uip->indexSectionOffset = uip->personalityArraySectionOffset +
                            (uip->personalityArrayCount * sizeof(uint32_t));
  uip->indexCount = secondLevelPages.size() + 1;

  // Common encodings
  auto *i32p = reinterpret_cast<uint32_t *>(&uip[1]);
  for (const auto &encoding : commonEncodings)
    *i32p++ = encoding.first;

  // Personalities
  for (const Symbol *personality : personalities)
    *i32p++ = personality->getGotVA() - in.header->addr;

  // FIXME: LD64 checks and warns aboutgaps or overlapse in cuEntries address
  // ranges. We should do the same too

  // Level-1 index
  uint32_t lsdaOffset =
      uip->indexSectionOffset +
      uip->indexCount * sizeof(unwind_info_section_header_index_entry);
  uint64_t l2PagesOffset = level2PagesOffset;
  auto *iep = reinterpret_cast<unwind_info_section_header_index_entry *>(i32p);
  for (const SecondLevelPage &page : secondLevelPages) {
    size_t idx = cuIndices[page.entryIndex];
    iep->functionOffset = cuEntries[idx].functionAddress - in.header->addr;
    iep->secondLevelPagesSectionOffset = l2PagesOffset;
    iep->lsdaIndexArraySectionOffset =
        lsdaOffset + lsdaIndex.lookup(idx) *
                         sizeof(unwind_info_section_header_lsda_index_entry);
    iep++;
    l2PagesOffset += SECOND_LEVEL_PAGE_BYTES;
  }
  // Level-1 sentinel
  // XXX(vyng): Note that LD64 adds +1 here.
  // Unsure whether it's a bug or it's their workaround for something else.
  // See comments from https://reviews.llvm.org/D138320.
  iep->functionOffset = cueEndBoundary - in.header->addr;
  iep->secondLevelPagesSectionOffset = 0;
  iep->lsdaIndexArraySectionOffset =
      lsdaOffset + entriesWithLsda.size() *
                       sizeof(unwind_info_section_header_lsda_index_entry);
  iep++;

  // LSDAs
  auto *lep =
      reinterpret_cast<unwind_info_section_header_lsda_index_entry *>(iep);
  for (size_t idx : entriesWithLsda) {
    const CompactUnwindEntry &cu = cuEntries[idx];
    lep->lsdaOffset = cu.lsda->getVA(/*off=*/0) - in.header->addr;
    lep->functionOffset = cu.functionAddress - in.header->addr;
    lep++;
  }

  // Level-2 pages
  auto *pp = reinterpret_cast<uint32_t *>(lep);
  for (const SecondLevelPage &page : secondLevelPages) {
    if (page.kind == UNWIND_SECOND_LEVEL_COMPRESSED) {
      uintptr_t functionAddressBase =
          cuEntries[cuIndices[page.entryIndex]].functionAddress;
      auto *p2p =
          reinterpret_cast<unwind_info_compressed_second_level_page_header *>(
              pp);
      p2p->kind = page.kind;
      p2p->entryPageOffset =
          sizeof(unwind_info_compressed_second_level_page_header);
      p2p->entryCount = page.entryCount;
      p2p->encodingsPageOffset =
          p2p->entryPageOffset + p2p->entryCount * sizeof(uint32_t);
      p2p->encodingsCount = page.localEncodings.size();
      auto *ep = reinterpret_cast<uint32_t *>(&p2p[1]);
      for (size_t i = 0; i < page.entryCount; i++) {
        const CompactUnwindEntry &cue =
            cuEntries[cuIndices[page.entryIndex + i]];
        auto it = commonEncodingIndexes.find(cue.encoding);
        if (it == commonEncodingIndexes.end())
          it = page.localEncodingIndexes.find(cue.encoding);
        *ep++ = (it->second << COMPRESSED_ENTRY_FUNC_OFFSET_BITS) |
                (cue.functionAddress - functionAddressBase);
      }
      if (!page.localEncodings.empty())
        memcpy(ep, page.localEncodings.data(),
               page.localEncodings.size() * sizeof(uint32_t));
    } else {
      auto *p2p =
          reinterpret_cast<unwind_info_regular_second_level_page_header *>(pp);
      p2p->kind = page.kind;
      p2p->entryPageOffset =
          sizeof(unwind_info_regular_second_level_page_header);
      p2p->entryCount = page.entryCount;
      auto *ep = reinterpret_cast<uint32_t *>(&p2p[1]);
      for (size_t i = 0; i < page.entryCount; i++) {
        const CompactUnwindEntry &cue =
            cuEntries[cuIndices[page.entryIndex + i]];
        *ep++ = cue.functionAddress;
        *ep++ = cue.encoding;
      }
    }
    pp += SECOND_LEVEL_PAGE_WORDS;
  }
}

UnwindInfoSection *macho::makeUnwindInfoSection() {
  return make<UnwindInfoSectionImpl>();
}
