//===- ICF.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ICF is short for Identical Code Folding. That is a size optimization to
// identify and merge two or more read-only sections (typically functions)
// that happened to have the same contents. It usually reduces output size
// by a few percent.
//
// On Windows, ICF is enabled by default.
//
// See ELF/ICF.cpp for the details about the algorithm.
//
//===----------------------------------------------------------------------===//

#include "ICF.h"
#include "COFFLinkerContext.h"
#include "Chunks.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Timer.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <atomic>
#include <vector>

using namespace llvm;

namespace lld::coff {

class ICF {
public:
  ICF(COFFLinkerContext &c) : ctx(c){};
  void run();

private:
  void segregate(size_t begin, size_t end, bool constant);

  bool assocEquals(const SectionChunk *a, const SectionChunk *b);

  bool equalsConstant(const SectionChunk *a, const SectionChunk *b);
  bool equalsVariable(const SectionChunk *a, const SectionChunk *b);

  bool isEligible(SectionChunk *c);

  size_t findBoundary(size_t begin, size_t end);

  void forEachClassRange(size_t begin, size_t end,
                         std::function<void(size_t, size_t)> fn);

  void forEachClass(std::function<void(size_t, size_t)> fn);

  std::vector<SectionChunk *> chunks;
  int cnt = 0;
  std::atomic<bool> repeat = {false};

  COFFLinkerContext &ctx;
};

// Returns true if section S is subject of ICF.
//
// Microsoft's documentation
// (https://msdn.microsoft.com/en-us/library/bxwfs976.aspx; visited April
// 2017) says that /opt:icf folds both functions and read-only data.
// Despite that, the MSVC linker folds only functions. We found
// a few instances of programs that are not safe for data merging.
// Therefore, we merge only functions just like the MSVC tool. However, we also
// merge read-only sections in a couple of cases where the address of the
// section is insignificant to the user program and the behaviour matches that
// of the Visual C++ linker.
bool ICF::isEligible(SectionChunk *c) {
  // Non-comdat chunks, dead chunks, and writable chunks are not eligible.
  bool writable = c->getOutputCharacteristics() & llvm::COFF::IMAGE_SCN_MEM_WRITE;
  if (!c->isCOMDAT() || !c->live || writable)
    return false;

  // Under regular (not safe) ICF, all code sections are eligible.
  if ((ctx.config.doICF == ICFLevel::All) &&
      c->getOutputCharacteristics() & llvm::COFF::IMAGE_SCN_MEM_EXECUTE)
    return true;

  // .pdata and .xdata unwind info sections are eligible.
  StringRef outSecName = c->getSectionName().split('$').first;
  if (outSecName == ".pdata" || outSecName == ".xdata")
    return true;

  // So are vtables.
  const char *itaniumVtablePrefix =
      ctx.config.machine == I386 ? "__ZTV" : "_ZTV";
  if (c->sym && (c->sym->getName().starts_with("??_7") ||
                 c->sym->getName().starts_with(itaniumVtablePrefix)))
    return true;

  // Anything else not in an address-significance table is eligible.
  return !c->keepUnique;
}

// Split an equivalence class into smaller classes.
void ICF::segregate(size_t begin, size_t end, bool constant) {
  while (begin < end) {
    // Divide [Begin, End) into two. Let Mid be the start index of the
    // second group.
    auto bound = std::stable_partition(
        chunks.begin() + begin + 1, chunks.begin() + end, [&](SectionChunk *s) {
          if (constant)
            return equalsConstant(chunks[begin], s);
          return equalsVariable(chunks[begin], s);
        });
    size_t mid = bound - chunks.begin();

    // Split [Begin, End) into [Begin, Mid) and [Mid, End). We use Mid as an
    // equivalence class ID because every group ends with a unique index.
    for (size_t i = begin; i < mid; ++i)
      chunks[i]->eqClass[(cnt + 1) % 2] = mid;

    // If we created a group, we need to iterate the main loop again.
    if (mid != end)
      repeat = true;

    begin = mid;
  }
}

// Returns true if two sections' associative children are equal.
bool ICF::assocEquals(const SectionChunk *a, const SectionChunk *b) {
  // Ignore associated metadata sections that don't participate in ICF, such as
  // debug info and CFGuard metadata.
  auto considerForICF = [](const SectionChunk &assoc) {
    StringRef Name = assoc.getSectionName();
    return !(Name.starts_with(".debug") || Name == ".gfids$y" ||
             Name == ".giats$y" || Name == ".gljmp$y");
  };
  auto ra = make_filter_range(a->children(), considerForICF);
  auto rb = make_filter_range(b->children(), considerForICF);
  return std::equal(ra.begin(), ra.end(), rb.begin(), rb.end(),
                    [&](const SectionChunk &ia, const SectionChunk &ib) {
                      return ia.eqClass[cnt % 2] == ib.eqClass[cnt % 2];
                    });
}

// Compare "non-moving" part of two sections, namely everything
// except relocation targets.
bool ICF::equalsConstant(const SectionChunk *a, const SectionChunk *b) {
  if (a->relocsSize != b->relocsSize)
    return false;

  // Compare relocations.
  auto eq = [&](const coff_relocation &r1, const coff_relocation &r2) {
    if (r1.Type != r2.Type ||
        r1.VirtualAddress != r2.VirtualAddress) {
      return false;
    }
    Symbol *b1 = a->file->getSymbol(r1.SymbolTableIndex);
    Symbol *b2 = b->file->getSymbol(r2.SymbolTableIndex);
    if (b1 == b2)
      return true;
    if (auto *d1 = dyn_cast<DefinedRegular>(b1))
      if (auto *d2 = dyn_cast<DefinedRegular>(b2))
        return d1->getValue() == d2->getValue() &&
               d1->getChunk()->eqClass[cnt % 2] == d2->getChunk()->eqClass[cnt % 2];
    return false;
  };
  if (!std::equal(a->getRelocs().begin(), a->getRelocs().end(),
                  b->getRelocs().begin(), eq))
    return false;

  // Compare section attributes and contents.
  return a->getOutputCharacteristics() == b->getOutputCharacteristics() &&
         a->getSectionName() == b->getSectionName() &&
         a->header->SizeOfRawData == b->header->SizeOfRawData &&
         a->checksum == b->checksum && a->getContents() == b->getContents() &&
         a->getMachine() == b->getMachine() && assocEquals(a, b);
}

// Compare "moving" part of two sections, namely relocation targets.
bool ICF::equalsVariable(const SectionChunk *a, const SectionChunk *b) {
  // Compare relocations.
  auto eqSym = [&](Symbol *b1, Symbol *b2) {
    if (b1 == b2)
      return true;
    if (auto *d1 = dyn_cast<DefinedRegular>(b1))
      if (auto *d2 = dyn_cast<DefinedRegular>(b2))
        return d1->getChunk()->eqClass[cnt % 2] == d2->getChunk()->eqClass[cnt % 2];
    return false;
  };
  auto eq = [&](const coff_relocation &r1, const coff_relocation &r2) {
    Symbol *b1 = a->file->getSymbol(r1.SymbolTableIndex);
    Symbol *b2 = b->file->getSymbol(r2.SymbolTableIndex);
    return eqSym(b1, b2);
  };

  Symbol *e1 = a->getEntryThunk();
  Symbol *e2 = b->getEntryThunk();
  if ((e1 || e2) && (!e1 || !e2 || !eqSym(e1, e2)))
    return false;

  return std::equal(a->getRelocs().begin(), a->getRelocs().end(),
                    b->getRelocs().begin(), eq) &&
         assocEquals(a, b);
}

// Find the first Chunk after Begin that has a different class from Begin.
size_t ICF::findBoundary(size_t begin, size_t end) {
  for (size_t i = begin + 1; i < end; ++i)
    if (chunks[begin]->eqClass[cnt % 2] != chunks[i]->eqClass[cnt % 2])
      return i;
  return end;
}

void ICF::forEachClassRange(size_t begin, size_t end,
                            std::function<void(size_t, size_t)> fn) {
  while (begin < end) {
    size_t mid = findBoundary(begin, end);
    fn(begin, mid);
    begin = mid;
  }
}

// Call Fn on each class group.
void ICF::forEachClass(std::function<void(size_t, size_t)> fn) {
  // If the number of sections are too small to use threading,
  // call Fn sequentially.
  if (chunks.size() < 1024) {
    forEachClassRange(0, chunks.size(), fn);
    ++cnt;
    return;
  }

  // Shard into non-overlapping intervals, and call Fn in parallel.
  // The sharding must be completed before any calls to Fn are made
  // so that Fn can modify the Chunks in its shard without causing data
  // races.
  const size_t numShards = 256;
  size_t step = chunks.size() / numShards;
  size_t boundaries[numShards + 1];
  boundaries[0] = 0;
  boundaries[numShards] = chunks.size();
  parallelFor(1, numShards, [&](size_t i) {
    boundaries[i] = findBoundary((i - 1) * step, chunks.size());
  });
  parallelFor(1, numShards + 1, [&](size_t i) {
    if (boundaries[i - 1] < boundaries[i]) {
      forEachClassRange(boundaries[i - 1], boundaries[i], fn);
    }
  });
  ++cnt;
}

// Merge identical COMDAT sections.
// Two sections are considered the same if their section headers,
// contents and relocations are all the same.
void ICF::run() {
  llvm::TimeTraceScope timeScope("ICF");
  ScopedTimer t(ctx.icfTimer);

  // Collect only mergeable sections and group by hash value.
  uint32_t nextId = 1;
  for (Chunk *c : ctx.symtab.getChunks()) {
    if (auto *sc = dyn_cast<SectionChunk>(c)) {
      if (isEligible(sc))
        chunks.push_back(sc);
      else
        sc->eqClass[0] = nextId++;
    }
  }

  // Make sure that ICF doesn't merge sections that are being handled by string
  // tail merging.
  for (MergeChunk *mc : ctx.mergeChunkInstances)
    if (mc)
      for (SectionChunk *sc : mc->sections)
        sc->eqClass[0] = nextId++;

  // Initially, we use hash values to partition sections.
  parallelForEach(chunks, [&](SectionChunk *sc) {
    sc->eqClass[0] = xxh3_64bits(sc->getContents());
  });

  // Combine the hashes of the sections referenced by each section into its
  // hash.
  for (unsigned cnt = 0; cnt != 2; ++cnt) {
    parallelForEach(chunks, [&](SectionChunk *sc) {
      uint32_t hash = sc->eqClass[cnt % 2];
      for (Symbol *b : sc->symbols())
        if (auto *sym = dyn_cast_or_null<DefinedRegular>(b))
          hash += sym->getChunk()->eqClass[cnt % 2];
      // Set MSB to 1 to avoid collisions with non-hash classes.
      sc->eqClass[(cnt + 1) % 2] = hash | (1U << 31);
    });
  }

  // From now on, sections in Chunks are ordered so that sections in
  // the same group are consecutive in the vector.
  llvm::stable_sort(chunks, [](const SectionChunk *a, const SectionChunk *b) {
    return a->eqClass[0] < b->eqClass[0];
  });

  // Compare static contents and assign unique IDs for each static content.
  forEachClass([&](size_t begin, size_t end) { segregate(begin, end, true); });

  // Split groups by comparing relocations until convergence is obtained.
  do {
    repeat = false;
    forEachClass(
        [&](size_t begin, size_t end) { segregate(begin, end, false); });
  } while (repeat);

  log("ICF needed " + Twine(cnt) + " iterations");

  // Merge sections in the same classes.
  forEachClass([&](size_t begin, size_t end) {
    if (end - begin == 1)
      return;

    log("Selected " + chunks[begin]->getDebugName());
    for (size_t i = begin + 1; i < end; ++i) {
      log("  Removed " + chunks[i]->getDebugName());
      chunks[begin]->replace(chunks[i]);
    }
  });
}

// Entry point to ICF.
void doICF(COFFLinkerContext &ctx) { ICF(ctx).run(); }

} // namespace lld::coff
