//===- MapFile.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the /map option in the same format as link.exe
// (based on observations)
//
// Header (program name, timestamp info, preferred load address)
//
// Section list (Start = Section index:Base address):
// Start         Length     Name                   Class
// 0001:00001000 00000015H .text                   CODE
//
// Symbols list:
// Address        Publics by Value    Rva + Base          Lib:Object
// 0001:00001000  main                 0000000140001000    main.obj
// 0001:00001300  ?__scrt_common_main@@YAHXZ  0000000140001300 libcmt:exe_main.obj
//
// entry point at        0001:00000360
//
// Static symbols
//
// 0000:00000000  __guard_fids__       0000000140000000     libcmt : exe_main.obj
//===----------------------------------------------------------------------===//

#include "MapFile.h"
#include "COFFLinkerContext.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Writer.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Timer.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;
using namespace lld;
using namespace lld::coff;

// Print out the first two columns of a line.
static void writeHeader(raw_ostream &os, uint32_t sec, uint64_t addr) {
  os << format(" %04x:%08llx", sec, addr);
}

// Write the time stamp with the format used by link.exe
// It seems identical to strftime with "%c" on msvc build, but we need a
// locale-agnostic version.
static void writeFormattedTimestamp(raw_ostream &os, time_t tds) {
  constexpr const char *const days[7] = {"Sun", "Mon", "Tue", "Wed",
                                         "Thu", "Fri", "Sat"};
  constexpr const char *const months[12] = {"Jan", "Feb", "Mar", "Apr",
                                            "May", "Jun", "Jul", "Aug",
                                            "Sep", "Oct", "Nov", "Dec"};
  tm *time = localtime(&tds);
  os << format("%s %s %2d %02d:%02d:%02d %d", days[time->tm_wday],
               months[time->tm_mon], time->tm_mday, time->tm_hour, time->tm_min,
               time->tm_sec, time->tm_year + 1900);
}

static void sortUniqueSymbols(std::vector<Defined *> &syms,
                              uint64_t imageBase) {
  // Build helper vector
  using SortEntry = std::pair<Defined *, size_t>;
  std::vector<SortEntry> v;
  v.resize(syms.size());
  for (size_t i = 0, e = syms.size(); i < e; ++i)
    v[i] = SortEntry(syms[i], i);

  // Remove duplicate symbol pointers
  parallelSort(v, std::less<SortEntry>());
  auto end = std::unique(v.begin(), v.end(),
                         [](const SortEntry &a, const SortEntry &b) {
                           return a.first == b.first;
                         });
  v.erase(end, v.end());

  // Sort by RVA then original order
  parallelSort(v, [imageBase](const SortEntry &a, const SortEntry &b) {
    // Add config.imageBase to avoid comparing "negative" RVAs.
    // This can happen with symbols of Absolute kind
    uint64_t rvaa = imageBase + a.first->getRVA();
    uint64_t rvab = imageBase + b.first->getRVA();
    return rvaa < rvab || (rvaa == rvab && a.second < b.second);
  });

  syms.resize(v.size());
  for (size_t i = 0, e = v.size(); i < e; ++i)
    syms[i] = v[i].first;
}

// Returns the lists of all symbols that we want to print out.
static void getSymbols(const COFFLinkerContext &ctx,
                       std::vector<Defined *> &syms,
                       std::vector<Defined *> &staticSyms) {

  for (ObjFile *file : ctx.objFileInstances)
    for (Symbol *b : file->getSymbols()) {
      if (!b || !b->isLive())
        continue;
      if (auto *sym = dyn_cast<DefinedCOFF>(b)) {
        COFFSymbolRef symRef = sym->getCOFFSymbol();
        if (!symRef.isSectionDefinition() &&
            symRef.getStorageClass() != COFF::IMAGE_SYM_CLASS_LABEL) {
          if (symRef.getStorageClass() == COFF::IMAGE_SYM_CLASS_STATIC)
            staticSyms.push_back(sym);
          else
            syms.push_back(sym);
        }
      } else if (auto *sym = dyn_cast<Defined>(b)) {
        syms.push_back(sym);
      }
    }

  for (ImportFile *file : ctx.importFileInstances) {
    if (!file->live)
      continue;

    if (!file->thunkSym)
      continue;

    if (!file->thunkLive)
      continue;

    if (auto *thunkSym = dyn_cast<Defined>(file->thunkSym))
      syms.push_back(thunkSym);

    if (auto *impSym = dyn_cast_or_null<Defined>(file->impSym))
      syms.push_back(impSym);
  }

  sortUniqueSymbols(syms, ctx.config.imageBase);
  sortUniqueSymbols(staticSyms, ctx.config.imageBase);
}

// Construct a map from symbols to their stringified representations.
static DenseMap<Defined *, std::string>
getSymbolStrings(const COFFLinkerContext &ctx, ArrayRef<Defined *> syms) {
  std::vector<std::string> str(syms.size());
  parallelFor((size_t)0, syms.size(), [&](size_t i) {
    raw_string_ostream os(str[i]);
    Defined *sym = syms[i];

    uint16_t sectionIdx = 0;
    uint64_t address = 0;
    SmallString<128> fileDescr;

    if (auto *absSym = dyn_cast<DefinedAbsolute>(sym)) {
      address = absSym->getVA();
      fileDescr = "<absolute>";
    } else if (isa<DefinedSynthetic>(sym)) {
      fileDescr = "<linker-defined>";
    } else if (isa<DefinedCommon>(sym)) {
      fileDescr = "<common>";
    } else if (Chunk *chunk = sym->getChunk()) {
      address = sym->getRVA();
      if (OutputSection *sec = ctx.getOutputSection(chunk))
        address -= sec->header.VirtualAddress;

      sectionIdx = chunk->getOutputSectionIdx();

      InputFile *file;
      if (auto *impSym = dyn_cast<DefinedImportData>(sym))
        file = impSym->file;
      else if (auto *thunkSym = dyn_cast<DefinedImportThunk>(sym))
        file = thunkSym->wrappedSym->file;
      else
        file = sym->getFile();

      if (file) {
        if (!file->parentName.empty()) {
          fileDescr = sys::path::filename(file->parentName);
          sys::path::replace_extension(fileDescr, "");
          fileDescr += ":";
        }
        fileDescr += sys::path::filename(file->getName());
      }
    }
    writeHeader(os, sectionIdx, address);
    os << "       ";
    os << left_justify(sym->getName(), 26);
    os << " ";
    os << format_hex_no_prefix((ctx.config.imageBase + sym->getRVA()), 16);
    if (!fileDescr.empty()) {
      os << "     "; // FIXME : Handle "f" and "i" flags sometimes generated
                     // by link.exe in those spaces
      os << fileDescr;
    }
  });

  DenseMap<Defined *, std::string> ret;
  for (size_t i = 0, e = syms.size(); i < e; ++i)
    ret[syms[i]] = std::move(str[i]);
  return ret;
}

void lld::coff::writeMapFile(COFFLinkerContext &ctx) {
  if (ctx.config.mapFile.empty())
    return;

  llvm::TimeTraceScope timeScope("Map file");
  std::error_code ec;
  raw_fd_ostream os(ctx.config.mapFile, ec, sys::fs::OF_None);
  if (ec)
    fatal("cannot open " + ctx.config.mapFile + ": " + ec.message());

  ScopedTimer t1(ctx.totalMapTimer);

  // Collect symbol info that we want to print out.
  ScopedTimer t2(ctx.symbolGatherTimer);
  std::vector<Defined *> syms;
  std::vector<Defined *> staticSyms;
  getSymbols(ctx, syms, staticSyms);
  t2.stop();

  ScopedTimer t3(ctx.symbolStringsTimer);
  DenseMap<Defined *, std::string> symStr = getSymbolStrings(ctx, syms);
  DenseMap<Defined *, std::string> staticSymStr =
      getSymbolStrings(ctx, staticSyms);
  t3.stop();

  ScopedTimer t4(ctx.writeTimer);
  SmallString<128> AppName = sys::path::filename(ctx.config.outputFile);
  sys::path::replace_extension(AppName, "");

  // Print out the file header
  os << " " << AppName << "\n";
  os << "\n";

  os << " Timestamp is " << format_hex_no_prefix(ctx.config.timestamp, 8)
     << " (";
  if (ctx.config.repro) {
    os << "Repro mode";
  } else {
    writeFormattedTimestamp(os, ctx.config.timestamp);
  }
  os << ")\n";

  os << "\n";
  os << " Preferred load address is "
     << format_hex_no_prefix(ctx.config.imageBase, 16) << "\n";
  os << "\n";

  // Print out section table.
  os << " Start         Length     Name                   Class\n";

  for (OutputSection *sec : ctx.outputSections) {
    // Merge display of chunks with same sectionName
    std::vector<std::pair<SectionChunk *, SectionChunk *>> ChunkRanges;
    for (Chunk *c : sec->chunks) {
      auto *sc = dyn_cast<SectionChunk>(c);
      if (!sc)
        continue;

      if (ChunkRanges.empty() ||
          c->getSectionName() != ChunkRanges.back().first->getSectionName()) {
        ChunkRanges.emplace_back(sc, sc);
      } else {
        ChunkRanges.back().second = sc;
      }
    }

    const bool isCodeSection =
        (sec->header.Characteristics & COFF::IMAGE_SCN_CNT_CODE) &&
        (sec->header.Characteristics & COFF::IMAGE_SCN_MEM_READ) &&
        (sec->header.Characteristics & COFF::IMAGE_SCN_MEM_EXECUTE);
    StringRef SectionClass = (isCodeSection ? "CODE" : "DATA");

    for (auto &cr : ChunkRanges) {
      size_t size =
          cr.second->getRVA() + cr.second->getSize() - cr.first->getRVA();

      auto address = cr.first->getRVA() - sec->header.VirtualAddress;
      writeHeader(os, sec->sectionIndex, address);
      os << " " << format_hex_no_prefix(size, 8) << "H";
      os << " " << left_justify(cr.first->getSectionName(), 23);
      os << " " << SectionClass;
      os << '\n';
    }
  }

  // Print out the symbols table (without static symbols)
  os << "\n";
  os << "  Address         Publics by Value              Rva+Base"
        "               Lib:Object\n";
  os << "\n";
  for (Defined *sym : syms)
    os << symStr[sym] << '\n';

  // Print out the entry point.
  os << "\n";

  uint16_t entrySecIndex = 0;
  uint64_t entryAddress = 0;

  if (!ctx.config.noEntry) {
    Defined *entry = dyn_cast_or_null<Defined>(ctx.config.entry);
    if (entry) {
      Chunk *chunk = entry->getChunk();
      entrySecIndex = chunk->getOutputSectionIdx();
      entryAddress =
          entry->getRVA() - ctx.getOutputSection(chunk)->header.VirtualAddress;
    }
  }
  os << " entry point at         ";
  os << format("%04x:%08llx", entrySecIndex, entryAddress);
  os << "\n";

  // Print out the static symbols
  os << "\n";
  os << " Static symbols\n";
  os << "\n";
  for (Defined *sym : staticSyms)
    os << staticSymStr[sym] << '\n';

  // Print out the exported functions
  if (ctx.config.mapInfo) {
    os << "\n";
    os << " Exports\n";
    os << "\n";
    os << "  ordinal    name\n\n";
    for (Export &e : ctx.config.exports) {
      os << format("  %7d", e.ordinal) << "    " << e.name << "\n";
      if (!e.extName.empty() && e.extName != e.name)
        os << "               exported name: " << e.extName << "\n";
    }
  }

  t4.stop();
  t1.stop();
}
