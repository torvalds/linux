//===- MarkLive.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements --gc-sections, which is a feature to remove unused
// sections from output. Unused sections are sections that are not reachable
// from known GC-root symbols or sections. Naturally the feature is
// implemented as a mark-sweep garbage collector.
//
// Here's how it works. Each InputSectionBase has a "Live" bit. The bit is off
// by default. Starting with GC-root symbols or sections, markLive function
// defined in this file visits all reachable sections to set their Live
// bits. Writer will then ignore sections whose Live bits are off, so that
// such sections are not included into output.
//
//===----------------------------------------------------------------------===//

#include "MarkLive.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "LinkerScript.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/TimeProfiler.h"
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::elf;

namespace {
template <class ELFT> class MarkLive {
public:
  MarkLive(unsigned partition) : partition(partition) {}

  void run();
  void moveToMain();

private:
  void enqueue(InputSectionBase *sec, uint64_t offset);
  void markSymbol(Symbol *sym);
  void mark();

  template <class RelTy>
  void resolveReloc(InputSectionBase &sec, RelTy &rel, bool fromFDE);

  template <class RelTy>
  void scanEhFrameSection(EhInputSection &eh, ArrayRef<RelTy> rels);

  // The index of the partition that we are currently processing.
  unsigned partition;

  // A list of sections to visit.
  SmallVector<InputSection *, 0> queue;

  // There are normally few input sections whose names are valid C
  // identifiers, so we just store a SmallVector instead of a multimap.
  DenseMap<StringRef, SmallVector<InputSectionBase *, 0>> cNamedSections;
};
} // namespace

template <class ELFT>
static uint64_t getAddend(InputSectionBase &sec,
                          const typename ELFT::Rel &rel) {
  return target->getImplicitAddend(sec.content().begin() + rel.r_offset,
                                   rel.getType(config->isMips64EL));
}

template <class ELFT>
static uint64_t getAddend(InputSectionBase &sec,
                          const typename ELFT::Rela &rel) {
  return rel.r_addend;
}

// Currently, we assume all input CREL relocations have an explicit addend.
template <class ELFT>
static uint64_t getAddend(InputSectionBase &sec,
                          const typename ELFT::Crel &rel) {
  return rel.r_addend;
}

template <class ELFT>
template <class RelTy>
void MarkLive<ELFT>::resolveReloc(InputSectionBase &sec, RelTy &rel,
                                  bool fromFDE) {
  // If a symbol is referenced in a live section, it is used.
  Symbol &sym = sec.file->getRelocTargetSym(rel);
  sym.used = true;

  if (auto *d = dyn_cast<Defined>(&sym)) {
    auto *relSec = dyn_cast_or_null<InputSectionBase>(d->section);
    if (!relSec)
      return;

    uint64_t offset = d->value;
    if (d->isSection())
      offset += getAddend<ELFT>(sec, rel);

    // fromFDE being true means this is referenced by a FDE in a .eh_frame
    // piece. The relocation points to the described function or to a LSDA. We
    // only need to keep the LSDA live, so ignore anything that points to
    // executable sections. If the LSDA is in a section group or has the
    // SHF_LINK_ORDER flag, we ignore the relocation as well because (a) if the
    // associated text section is live, the LSDA will be retained due to section
    // group/SHF_LINK_ORDER rules (b) if the associated text section should be
    // discarded, marking the LSDA will unnecessarily retain the text section.
    if (!(fromFDE && ((relSec->flags & (SHF_EXECINSTR | SHF_LINK_ORDER)) ||
                      relSec->nextInSectionGroup)))
      enqueue(relSec, offset);
    return;
  }

  if (auto *ss = dyn_cast<SharedSymbol>(&sym))
    if (!ss->isWeak())
      cast<SharedFile>(ss->file)->isNeeded = true;

  for (InputSectionBase *sec : cNamedSections.lookup(sym.getName()))
    enqueue(sec, 0);
}

// The .eh_frame section is an unfortunate special case.
// The section is divided in CIEs and FDEs and the relocations it can have are
// * CIEs can refer to a personality function.
// * FDEs can refer to a LSDA
// * FDEs refer to the function they contain information about
// The last kind of relocation cannot keep the referred section alive, or they
// would keep everything alive in a common object file. In fact, each FDE is
// alive if the section it refers to is alive.
// To keep things simple, in here we just ignore the last relocation kind. The
// other two keep the referred section alive.
//
// A possible improvement would be to fully process .eh_frame in the middle of
// the gc pass. With that we would be able to also gc some sections holding
// LSDAs and personality functions if we found that they were unused.
template <class ELFT>
template <class RelTy>
void MarkLive<ELFT>::scanEhFrameSection(EhInputSection &eh,
                                        ArrayRef<RelTy> rels) {
  for (const EhSectionPiece &cie : eh.cies)
    if (cie.firstRelocation != unsigned(-1))
      resolveReloc(eh, rels[cie.firstRelocation], false);
  for (const EhSectionPiece &fde : eh.fdes) {
    size_t firstRelI = fde.firstRelocation;
    if (firstRelI == (unsigned)-1)
      continue;
    uint64_t pieceEnd = fde.inputOff + fde.size;
    for (size_t j = firstRelI, end2 = rels.size();
         j < end2 && rels[j].r_offset < pieceEnd; ++j)
      resolveReloc(eh, rels[j], true);
  }
}

// Some sections are used directly by the loader, so they should never be
// garbage-collected. This function returns true if a given section is such
// section.
static bool isReserved(InputSectionBase *sec) {
  switch (sec->type) {
  case SHT_FINI_ARRAY:
  case SHT_INIT_ARRAY:
  case SHT_PREINIT_ARRAY:
    return true;
  case SHT_NOTE:
    // SHT_NOTE sections in a group are subject to garbage collection.
    return !sec->nextInSectionGroup;
  default:
    // Support SHT_PROGBITS .init_array (https://golang.org/issue/50295) and
    // .init_array.N (https://github.com/rust-lang/rust/issues/92181) for a
    // while.
    StringRef s = sec->name;
    return s == ".init" || s == ".fini" || s.starts_with(".init_array") ||
           s == ".jcr" || s.starts_with(".ctors") || s.starts_with(".dtors");
  }
}

template <class ELFT>
void MarkLive<ELFT>::enqueue(InputSectionBase *sec, uint64_t offset) {
  // Usually, a whole section is marked as live or dead, but in mergeable
  // (splittable) sections, each piece of data has independent liveness bit.
  // So we explicitly tell it which offset is in use.
  if (auto *ms = dyn_cast<MergeInputSection>(sec))
    ms->getSectionPiece(offset).live = true;

  // Set Sec->Partition to the meet (i.e. the "minimum") of Partition and
  // Sec->Partition in the following lattice: 1 < other < 0. If Sec->Partition
  // doesn't change, we don't need to do anything.
  if (sec->partition == 1 || sec->partition == partition)
    return;
  sec->partition = sec->partition ? 1 : partition;

  // Add input section to the queue.
  if (InputSection *s = dyn_cast<InputSection>(sec))
    queue.push_back(s);
}

template <class ELFT> void MarkLive<ELFT>::markSymbol(Symbol *sym) {
  if (auto *d = dyn_cast_or_null<Defined>(sym))
    if (auto *isec = dyn_cast_or_null<InputSectionBase>(d->section))
      enqueue(isec, d->value);
}

// This is the main function of the garbage collector.
// Starting from GC-root sections, this function visits all reachable
// sections to set their "Live" bits.
template <class ELFT> void MarkLive<ELFT>::run() {
  // Add GC root symbols.

  // Preserve externally-visible symbols if the symbols defined by this
  // file can interpose other ELF file's symbols at runtime.
  for (Symbol *sym : symtab.getSymbols())
    if (sym->includeInDynsym() && sym->partition == partition)
      markSymbol(sym);

  // If this isn't the main partition, that's all that we need to preserve.
  if (partition != 1) {
    mark();
    return;
  }

  markSymbol(symtab.find(config->entry));
  markSymbol(symtab.find(config->init));
  markSymbol(symtab.find(config->fini));
  for (StringRef s : config->undefined)
    markSymbol(symtab.find(s));
  for (StringRef s : script->referencedSymbols)
    markSymbol(symtab.find(s));
  for (auto [symName, _] : symtab.cmseSymMap) {
    markSymbol(symtab.cmseSymMap[symName].sym);
    markSymbol(symtab.cmseSymMap[symName].acleSeSym);
  }

  // Mark .eh_frame sections as live because there are usually no relocations
  // that point to .eh_frames. Otherwise, the garbage collector would drop
  // all of them. We also want to preserve personality routines and LSDA
  // referenced by .eh_frame sections, so we scan them for that here.
  for (EhInputSection *eh : ctx.ehInputSections) {
    const RelsOrRelas<ELFT> rels =
        eh->template relsOrRelas<ELFT>(/*supportsCrel=*/false);
    if (rels.areRelocsRel())
      scanEhFrameSection(*eh, rels.rels);
    else if (rels.relas.size())
      scanEhFrameSection(*eh, rels.relas);
  }
  for (InputSectionBase *sec : ctx.inputSections) {
    if (sec->flags & SHF_GNU_RETAIN) {
      enqueue(sec, 0);
      continue;
    }
    if (sec->flags & SHF_LINK_ORDER)
      continue;

    // Usually, non-SHF_ALLOC sections are not removed even if they are
    // unreachable through relocations because reachability is not a good signal
    // whether they are garbage or not (e.g. there is usually no section
    // referring to a .comment section, but we want to keep it.) When a
    // non-SHF_ALLOC section is retained, we also retain sections dependent on
    // it.
    //
    // Note on SHF_LINK_ORDER: Such sections contain metadata and they
    // have a reverse dependency on the InputSection they are linked with.
    // We are able to garbage collect them.
    //
    // Note on SHF_REL{,A}: Such sections reach here only when -r
    // or --emit-reloc were given. And they are subject of garbage
    // collection because, if we remove a text section, we also
    // remove its relocation section.
    //
    // Note on nextInSectionGroup: The ELF spec says that group sections are
    // included or omitted as a unit. We take the interpretation that:
    //
    // - Group members (nextInSectionGroup != nullptr) are subject to garbage
    //   collection.
    // - Groups members are retained or discarded as a unit.
    if (!(sec->flags & SHF_ALLOC)) {
      if (!isStaticRelSecType(sec->type) && !sec->nextInSectionGroup) {
        sec->markLive();
        for (InputSection *isec : sec->dependentSections)
          isec->markLive();
      }
    }

    // Preserve special sections and those which are specified in linker
    // script KEEP command.
    if (isReserved(sec) || script->shouldKeep(sec)) {
      enqueue(sec, 0);
    } else if ((!config->zStartStopGC || sec->name.starts_with("__libc_")) &&
               isValidCIdentifier(sec->name)) {
      // As a workaround for glibc libc.a before 2.34
      // (https://sourceware.org/PR27492), retain __libc_atexit and similar
      // sections regardless of zStartStopGC.
      cNamedSections[saver().save("__start_" + sec->name)].push_back(sec);
      cNamedSections[saver().save("__stop_" + sec->name)].push_back(sec);
    }
  }

  mark();
}

template <class ELFT> void MarkLive<ELFT>::mark() {
  // Mark all reachable sections.
  while (!queue.empty()) {
    InputSectionBase &sec = *queue.pop_back_val();

    const RelsOrRelas<ELFT> rels = sec.template relsOrRelas<ELFT>();
    for (const typename ELFT::Rel &rel : rels.rels)
      resolveReloc(sec, rel, false);
    for (const typename ELFT::Rela &rel : rels.relas)
      resolveReloc(sec, rel, false);
    for (const typename ELFT::Crel &rel : rels.crels)
      resolveReloc(sec, rel, false);

    for (InputSectionBase *isec : sec.dependentSections)
      enqueue(isec, 0);

    // Mark the next group member.
    if (sec.nextInSectionGroup)
      enqueue(sec.nextInSectionGroup, 0);
  }
}

// Move the sections for some symbols to the main partition, specifically ifuncs
// (because they can result in an IRELATIVE being added to the main partition's
// GOT, which means that the ifunc must be available when the main partition is
// loaded) and TLS symbols (because we only know how to correctly process TLS
// relocations for the main partition).
//
// We also need to move sections whose names are C identifiers that are referred
// to from __start_/__stop_ symbols because there will only be one set of
// symbols for the whole program.
template <class ELFT> void MarkLive<ELFT>::moveToMain() {
  for (ELFFileBase *file : ctx.objectFiles)
    for (Symbol *s : file->getSymbols())
      if (auto *d = dyn_cast<Defined>(s))
        if ((d->type == STT_GNU_IFUNC || d->type == STT_TLS) && d->section &&
            d->section->isLive())
          markSymbol(s);

  for (InputSectionBase *sec : ctx.inputSections) {
    if (!sec->isLive() || !isValidCIdentifier(sec->name))
      continue;
    if (symtab.find(("__start_" + sec->name).str()) ||
        symtab.find(("__stop_" + sec->name).str()))
      enqueue(sec, 0);
  }

  mark();
}

// Before calling this function, Live bits are off for all
// input sections. This function make some or all of them on
// so that they are emitted to the output file.
template <class ELFT> void elf::markLive() {
  llvm::TimeTraceScope timeScope("markLive");
  // If --gc-sections is not given, retain all input sections.
  if (!config->gcSections) {
    // If a DSO defines a symbol referenced in a regular object, it is needed.
    for (Symbol *sym : symtab.getSymbols())
      if (auto *s = dyn_cast<SharedSymbol>(sym))
        if (s->isUsedInRegularObj && !s->isWeak())
          cast<SharedFile>(s->file)->isNeeded = true;
    return;
  }

  for (InputSectionBase *sec : ctx.inputSections)
    sec->markDead();

  // Follow the graph to mark all live sections.
  for (unsigned curPart = 1; curPart <= partitions.size(); ++curPart)
    MarkLive<ELFT>(curPart).run();

  // If we have multiple partitions, some sections need to live in the main
  // partition even if they were allocated to a loadable partition. Move them
  // there now.
  if (partitions.size() != 1)
    MarkLive<ELFT>(1).moveToMain();

  // Report garbage-collected sections.
  if (config->printGcSections)
    for (InputSectionBase *sec : ctx.inputSections)
      if (!sec->isLive())
        message("removing unused section " + toString(sec));
}

template void elf::markLive<ELF32LE>();
template void elf::markLive<ELF32BE>();
template void elf::markLive<ELF64LE>();
template void elf::markLive<ELF64BE>();
