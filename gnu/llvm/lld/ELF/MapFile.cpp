//===- MapFile.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the -Map option. It shows lists in order and
// hierarchically the output sections, input sections, input files and
// symbol:
//
//   Address  Size     Align Out     In      Symbol
//   00201000 00000015     4 .text
//   00201000 0000000e     4         test.o:(.text)
//   0020100e 00000000     0                 local
//   00201005 00000000     0                 f(int)
//
//===----------------------------------------------------------------------===//

#include "MapFile.h"
#include "InputFiles.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;
using namespace lld;
using namespace lld::elf;

using SymbolMapTy = DenseMap<const SectionBase *,
                             SmallVector<std::pair<Defined *, uint64_t>, 0>>;

static constexpr char indent8[] = "        ";          // 8 spaces
static constexpr char indent16[] = "                "; // 16 spaces

// Print out the first three columns of a line.
static void writeHeader(raw_ostream &os, uint64_t vma, uint64_t lma,
                        uint64_t size, uint64_t align) {
  if (config->is64)
    os << format("%16llx %16llx %8llx %5lld ", vma, lma, size, align);
  else
    os << format("%8llx %8llx %8llx %5lld ", vma, lma, size, align);
}

// Returns a list of all symbols that we want to print out.
static std::vector<Defined *> getSymbols() {
  std::vector<Defined *> v;
  for (ELFFileBase *file : ctx.objectFiles)
    for (Symbol *b : file->getSymbols())
      if (auto *dr = dyn_cast<Defined>(b))
        if (!dr->isSection() && dr->section && dr->section->isLive() &&
            (dr->file == file || dr->hasFlag(NEEDS_COPY) || dr->section->bss))
          v.push_back(dr);
  return v;
}

// Returns a map from sections to their symbols.
static SymbolMapTy getSectionSyms(ArrayRef<Defined *> syms) {
  SymbolMapTy ret;
  for (Defined *dr : syms)
    ret[dr->section].emplace_back(dr, dr->getVA());

  // Sort symbols by address. We want to print out symbols in the
  // order in the output file rather than the order they appeared
  // in the input files.
  SmallPtrSet<Defined *, 4> set;
  for (auto &it : ret) {
    // Deduplicate symbols which need a canonical PLT entry/copy relocation.
    set.clear();
    llvm::erase_if(it.second, [&](std::pair<Defined *, uint64_t> a) {
      return !set.insert(a.first).second;
    });

    llvm::stable_sort(it.second, llvm::less_second());
  }
  return ret;
}

// Construct a map from symbols to their stringified representations.
// Demangling symbols (which is what toString() does) is slow, so
// we do that in batch using parallel-for.
static DenseMap<Symbol *, std::string>
getSymbolStrings(ArrayRef<Defined *> syms) {
  auto strs = std::make_unique<std::string[]>(syms.size());
  parallelFor(0, syms.size(), [&](size_t i) {
    raw_string_ostream os(strs[i]);
    OutputSection *osec = syms[i]->getOutputSection();
    uint64_t vma = syms[i]->getVA();
    uint64_t lma = osec ? osec->getLMA() + vma - osec->getVA(0) : 0;
    writeHeader(os, vma, lma, syms[i]->getSize(), 1);
    os << indent16 << toString(*syms[i]);
  });

  DenseMap<Symbol *, std::string> ret;
  for (size_t i = 0, e = syms.size(); i < e; ++i)
    ret[syms[i]] = std::move(strs[i]);
  return ret;
}

// Print .eh_frame contents. Since the section consists of EhSectionPieces,
// we need a specialized printer for that section.
//
// .eh_frame tend to contain a lot of section pieces that are contiguous
// both in input file and output file. Such pieces are squashed before
// being displayed to make output compact.
static void printEhFrame(raw_ostream &os, const EhFrameSection *sec) {
  std::vector<EhSectionPiece> pieces;

  auto add = [&](const EhSectionPiece &p) {
    // If P is adjacent to Last, squash the two.
    if (!pieces.empty()) {
      EhSectionPiece &last = pieces.back();
      if (last.sec == p.sec && last.inputOff + last.size == p.inputOff &&
          last.outputOff + last.size == (unsigned)p.outputOff) {
        last.size += p.size;
        return;
      }
    }
    pieces.push_back(p);
  };

  // Gather section pieces.
  for (const CieRecord *rec : sec->getCieRecords()) {
    add(*rec->cie);
    for (const EhSectionPiece *fde : rec->fdes)
      add(*fde);
  }

  // Print out section pieces.
  const OutputSection *osec = sec->getOutputSection();
  for (EhSectionPiece &p : pieces) {
    writeHeader(os, osec->addr + p.outputOff, osec->getLMA() + p.outputOff,
                p.size, 1);
    os << indent8 << toString(p.sec->file) << ":(" << p.sec->name << "+0x"
       << Twine::utohexstr(p.inputOff) + ")\n";
  }
}

static void writeMapFile(raw_fd_ostream &os) {
  // Collect symbol info that we want to print out.
  std::vector<Defined *> syms = getSymbols();
  SymbolMapTy sectionSyms = getSectionSyms(syms);
  DenseMap<Symbol *, std::string> symStr = getSymbolStrings(syms);

  // Print out the header line.
  int w = config->is64 ? 16 : 8;
  os << right_justify("VMA", w) << ' ' << right_justify("LMA", w)
     << "     Size Align Out     In      Symbol\n";

  OutputSection *osec = nullptr;
  for (SectionCommand *cmd : script->sectionCommands) {
    if (auto *assign = dyn_cast<SymbolAssignment>(cmd)) {
      if (assign->provide && !assign->sym)
        continue;
      uint64_t lma = osec ? osec->getLMA() + assign->addr - osec->getVA(0) : 0;
      writeHeader(os, assign->addr, lma, assign->size, 1);
      os << assign->commandString << '\n';
      continue;
    }

    osec = &cast<OutputDesc>(cmd)->osec;
    writeHeader(os, osec->addr, osec->getLMA(), osec->size, osec->addralign);
    os << osec->name << '\n';

    // Dump symbols for each input section.
    for (SectionCommand *subCmd : osec->commands) {
      if (auto *isd = dyn_cast<InputSectionDescription>(subCmd)) {
        for (InputSection *isec : isd->sections) {
          if (auto *ehSec = dyn_cast<EhFrameSection>(isec)) {
            printEhFrame(os, ehSec);
            continue;
          }

          writeHeader(os, isec->getVA(), osec->getLMA() + isec->outSecOff,
                      isec->getSize(), isec->addralign);
          os << indent8 << toString(isec) << '\n';
          for (Symbol *sym : llvm::make_first_range(sectionSyms[isec]))
            os << symStr[sym] << '\n';
        }
        continue;
      }

      if (auto *data = dyn_cast<ByteCommand>(subCmd)) {
        writeHeader(os, osec->addr + data->offset,
                    osec->getLMA() + data->offset, data->size, 1);
        os << indent8 << data->commandString << '\n';
        continue;
      }

      if (auto *assign = dyn_cast<SymbolAssignment>(subCmd)) {
        if (assign->provide && !assign->sym)
          continue;
        writeHeader(os, assign->addr,
                    osec->getLMA() + assign->addr - osec->getVA(0),
                    assign->size, 1);
        os << indent8 << assign->commandString << '\n';
        continue;
      }
    }
  }
}

// Output a cross reference table to stdout. This is for --cref.
//
// For each global symbol, we print out a file that defines the symbol
// followed by files that uses that symbol. Here is an example.
//
//     strlen     /lib/x86_64-linux-gnu/libc.so.6
//                tools/lld/tools/lld/CMakeFiles/lld.dir/lld.cpp.o
//                lib/libLLVMSupport.a(PrettyStackTrace.cpp.o)
//
// In this case, strlen is defined by libc.so.6 and used by other two
// files.
static void writeCref(raw_fd_ostream &os) {
  // Collect symbols and files.
  MapVector<Symbol *, SetVector<InputFile *>> map;
  for (ELFFileBase *file : ctx.objectFiles) {
    for (Symbol *sym : file->getSymbols()) {
      if (isa<SharedSymbol>(sym))
        map[sym].insert(file);
      if (auto *d = dyn_cast<Defined>(sym))
        if (!d->isLocal())
          map[d].insert(file);
    }
  }

  auto print = [&](StringRef a, StringRef b) {
    os << left_justify(a, 49) << ' ' << b << '\n';
  };

  // Print a blank line and a header. The format matches GNU ld.
  os << "\nCross Reference Table\n\n";
  print("Symbol", "File");

  // Print out a table.
  for (auto kv : map) {
    Symbol *sym = kv.first;
    SetVector<InputFile *> &files = kv.second;

    print(toString(*sym), toString(sym->file));
    for (InputFile *file : files)
      if (file != sym->file)
        print("", toString(file));
  }
}

void elf::writeMapAndCref() {
  if (config->mapFile.empty() && !config->cref)
    return;

  llvm::TimeTraceScope timeScope("Write map file");

  // Open a map file for writing.
  std::error_code ec;
  StringRef mapFile = config->mapFile.empty() ? "-" : config->mapFile;
  raw_fd_ostream os = ctx.openAuxiliaryFile(mapFile, ec);
  if (ec) {
    error("cannot open " + mapFile + ": " + ec.message());
    return;
  }

  if (!config->mapFile.empty())
    writeMapFile(os);
  if (config->cref)
    writeCref(os);
}
