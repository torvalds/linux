//===- MarkLive.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MarkLive.h"
#include "Config.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "UnwindInfoSection.h"

#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/TimeProfiler.h"

#include "mach-o/compact_unwind_encoding.h"

namespace lld::macho {

using namespace llvm;
using namespace llvm::MachO;

struct WhyLiveEntry {
  InputSection *isec;
  // Keep track of the entry that caused us to mark `isec` as live.
  const WhyLiveEntry *prev;

  WhyLiveEntry(InputSection *isec, const WhyLiveEntry *prev)
      : isec(isec), prev(prev) {}
};

// Type-erased interface to MarkLiveImpl. Used for adding roots to the liveness
// graph.
class MarkLive {
public:
  virtual void enqueue(InputSection *isec, uint64_t off) = 0;
  virtual void addSym(Symbol *s) = 0;
  virtual void markTransitively() = 0;
  virtual ~MarkLive() = default;
};

template <bool RecordWhyLive> class MarkLiveImpl : public MarkLive {
public:
  // -why_live is a rarely used option, so we don't want support for that flag
  // to slow down the main -dead_strip code path. As such, we employ templates
  // to avoid the usage of WhyLiveEntry in the main code path. This saves us
  // from needless allocations and pointer indirections.
  using WorklistEntry =
      std::conditional_t<RecordWhyLive, WhyLiveEntry, InputSection>;

  void enqueue(InputSection *isec, uint64_t off) override {
    enqueue(isec, off, nullptr);
  }
  void addSym(Symbol *s) override { addSym(s, nullptr); }
  void markTransitively() override;

private:
  void enqueue(InputSection *isec, uint64_t off, const WorklistEntry *prev);
  void addSym(Symbol *s, const WorklistEntry *prev);
  const InputSection *getInputSection(const WorklistEntry *) const;
  WorklistEntry *makeEntry(InputSection *, const WorklistEntry *prev) const;

  // We build up a worklist of sections which have been marked as live. We
  // only push into the worklist when we discover an unmarked section, and we
  // mark as we push, so sections never appear twice in the list. Literal
  // sections cannot contain references to other sections, so we only store
  // ConcatInputSections in our worklist.
  SmallVector<WorklistEntry *, 256> worklist;
};

template <bool RecordWhyLive>
void MarkLiveImpl<RecordWhyLive>::enqueue(
    InputSection *isec, uint64_t off,
    const typename MarkLiveImpl<RecordWhyLive>::WorklistEntry *prev) {
  if (isec->isLive(off))
    return;
  isec->markLive(off);
  if (auto s = dyn_cast<ConcatInputSection>(isec)) {
    assert(!s->isCoalescedWeak());
    worklist.push_back(makeEntry(s, prev));
  }
}

static void printWhyLive(const Symbol *s, const WhyLiveEntry *prev) {
  std::string out = toString(*s) + " from " + toString(s->getFile());
  int indent = 2;
  for (const WhyLiveEntry *entry = prev; entry;
       entry = entry->prev, indent += 2) {
    const TinyPtrVector<Defined *> &symbols = entry->isec->symbols;
    // With .subsections_with_symbols set, most isecs will have exactly one
    // entry in their symbols vector, so we just print the first one.
    if (!symbols.empty())
      out += "\n" + std::string(indent, ' ') + toString(*symbols.front()) +
             " from " + toString(symbols.front()->getFile());
  }
  message(out);
}

template <bool RecordWhyLive>
void MarkLiveImpl<RecordWhyLive>::addSym(
    Symbol *s,
    const typename MarkLiveImpl<RecordWhyLive>::WorklistEntry *prev) {
  if (s->used)
    return;
  s->used = true;
  if constexpr (RecordWhyLive)
    if (!config->whyLive.empty() && config->whyLive.match(s->getName()))
      printWhyLive(s, prev);
  if (auto *d = dyn_cast<Defined>(s)) {
    if (d->isec())
      enqueue(d->isec(), d->value, prev);
    if (d->unwindEntry())
      enqueue(d->unwindEntry(), 0, prev);
  }
}

template <bool RecordWhyLive>
const InputSection *MarkLiveImpl<RecordWhyLive>::getInputSection(
    const MarkLiveImpl<RecordWhyLive>::WorklistEntry *entry) const {
  if constexpr (RecordWhyLive)
    return entry->isec;
  else
    return entry;
}

template <bool RecordWhyLive>
typename MarkLiveImpl<RecordWhyLive>::WorklistEntry *
MarkLiveImpl<RecordWhyLive>::makeEntry(
    InputSection *isec,
    const MarkLiveImpl<RecordWhyLive>::WorklistEntry *prev) const {
  if constexpr (RecordWhyLive) {
    if (!isec) {
      assert(!prev);
      return nullptr;
    }
    return make<WhyLiveEntry>(isec, prev);
  } else {
    return isec;
  }
}

template <bool RecordWhyLive>
void MarkLiveImpl<RecordWhyLive>::markTransitively() {
  do {
    // Mark things reachable from GC roots as live.
    while (!worklist.empty()) {
      WorklistEntry *entry = worklist.pop_back_val();
      // Entries that get placed onto the worklist always contain
      // ConcatInputSections. `WhyLiveEntry::prev` may point to entries that
      // contain other types of InputSections (due to S_ATTR_LIVE_SUPPORT), but
      // those entries should never be pushed onto the worklist.
      auto *isec = cast<ConcatInputSection>(getInputSection(entry));
      assert(isec->live && "We mark as live when pushing onto the worklist!");

      // Mark all symbols listed in the relocation table for this section.
      for (const Reloc &r : isec->relocs) {
        if (auto *s = r.referent.dyn_cast<Symbol *>())
          addSym(s, entry);
        else
          enqueue(r.referent.get<InputSection *>(), r.addend, entry);
      }
      for (Defined *d : getInputSection(entry)->symbols)
        addSym(d, entry);
    }

    // S_ATTR_LIVE_SUPPORT sections are live if they point _to_ a live
    // section. Process them in a second pass.
    for (ConcatInputSection *isec : inputSections) {
      // FIXME: Check if copying all S_ATTR_LIVE_SUPPORT sections into a
      // separate vector and only walking that here is faster.
      if (!(isec->getFlags() & S_ATTR_LIVE_SUPPORT) || isec->live)
        continue;

      for (const Reloc &r : isec->relocs) {
        if (auto *s = r.referent.dyn_cast<Symbol *>()) {
          if (s->isLive()) {
            InputSection *referentIsec = nullptr;
            if (auto *d = dyn_cast<Defined>(s))
              referentIsec = d->isec();
            enqueue(isec, 0, makeEntry(referentIsec, nullptr));
          }
        } else {
          auto *referentIsec = r.referent.get<InputSection *>();
          if (referentIsec->isLive(r.addend))
            enqueue(isec, 0, makeEntry(referentIsec, nullptr));
        }
      }
    }

    // S_ATTR_LIVE_SUPPORT could have marked additional sections live,
    // which in turn could mark additional S_ATTR_LIVE_SUPPORT sections live.
    // Iterate. In practice, the second iteration won't mark additional
    // S_ATTR_LIVE_SUPPORT sections live.
  } while (!worklist.empty());
}

// Set live bit on for each reachable chunk. Unmarked (unreachable)
// InputSections will be ignored by Writer, so they will be excluded
// from the final output.
void markLive() {
  TimeTraceScope timeScope("markLive");
  MarkLive *marker;
  if (config->whyLive.empty())
    marker = make<MarkLiveImpl<false>>();
  else
    marker = make<MarkLiveImpl<true>>();
  // Add GC roots.
  if (config->entry)
    marker->addSym(config->entry);
  for (Symbol *sym : symtab->getSymbols()) {
    if (auto *defined = dyn_cast<Defined>(sym)) {
      // -exported_symbol(s_list)
      if (!config->exportedSymbols.empty() &&
          config->exportedSymbols.match(defined->getName())) {
        // NOTE: Even though exporting private externs is an ill-defined
        // operation, we are purposely not checking for privateExtern in
        // order to follow ld64's behavior of treating all exported private
        // extern symbols as live, irrespective of whether they are autohide.
        marker->addSym(defined);
        continue;
      }

      // public symbols explicitly marked .no_dead_strip
      if (defined->referencedDynamically || defined->noDeadStrip) {
        marker->addSym(defined);
        continue;
      }

      // FIXME: When we implement these flags, make symbols from them GC
      // roots:
      // * -reexported_symbol(s_list)
      // * -alias_list
      // * -init

      // In dylibs and bundles and in executables with -export_dynamic,
      // all external functions are GC roots.
      bool externsAreRoots =
          config->outputType != MH_EXECUTE || config->exportDynamic;
      if (externsAreRoots && !defined->privateExtern) {
        marker->addSym(defined);
        continue;
      }
    }
  }
  // -u symbols
  for (Symbol *sym : config->explicitUndefineds)
    marker->addSym(sym);
  // local symbols explicitly marked .no_dead_strip
  for (const InputFile *file : inputFiles)
    if (auto *objFile = dyn_cast<ObjFile>(file))
      for (Symbol *sym : objFile->symbols)
        if (auto *defined = dyn_cast_or_null<Defined>(sym))
          if (!defined->isExternal() && defined->noDeadStrip)
            marker->addSym(defined);
  if (auto *stubBinder =
          dyn_cast_or_null<DylibSymbol>(symtab->find("dyld_stub_binder")))
    marker->addSym(stubBinder);
  for (ConcatInputSection *isec : inputSections) {
    // Sections marked no_dead_strip
    if (isec->getFlags() & S_ATTR_NO_DEAD_STRIP) {
      marker->enqueue(isec, 0);
      continue;
    }

    // mod_init_funcs, mod_term_funcs sections
    if (sectionType(isec->getFlags()) == S_MOD_INIT_FUNC_POINTERS ||
        sectionType(isec->getFlags()) == S_MOD_TERM_FUNC_POINTERS) {
      assert(!config->emitInitOffsets ||
             sectionType(isec->getFlags()) != S_MOD_INIT_FUNC_POINTERS);
      marker->enqueue(isec, 0);
      continue;
    }
  }

  for (ConcatInputSection *isec : in.initOffsets->inputs())
    marker->enqueue(isec, 0);

  marker->markTransitively();
}

} // namespace lld::macho
