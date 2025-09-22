//===- Writer.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Writer.h"
#include "COFFLinkerContext.h"
#include "CallGraphSort.h"
#include "Config.h"
#include "DLL.h"
#include "InputFiles.h"
#include "LLDMapFile.h"
#include "MapFile.h"
#include "PDB.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Timer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <utility>

using namespace llvm;
using namespace llvm::COFF;
using namespace llvm::object;
using namespace llvm::support;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::coff;

/* To re-generate DOSProgram:
$ cat > /tmp/DOSProgram.asm
org 0
        ; Copy cs to ds.
        push cs
        pop ds
        ; Point ds:dx at the $-terminated string.
        mov dx, str
        ; Int 21/AH=09h: Write string to standard output.
        mov ah, 0x9
        int 0x21
        ; Int 21/AH=4Ch: Exit with return code (in AL).
        mov ax, 0x4C01
        int 0x21
str:
        db 'This program cannot be run in DOS mode.$'
align 8, db 0
$ nasm -fbin /tmp/DOSProgram.asm -o /tmp/DOSProgram.bin
$ xxd -i /tmp/DOSProgram.bin
*/
static unsigned char dosProgram[] = {
  0x0e, 0x1f, 0xba, 0x0e, 0x00, 0xb4, 0x09, 0xcd, 0x21, 0xb8, 0x01, 0x4c,
  0xcd, 0x21, 0x54, 0x68, 0x69, 0x73, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72,
  0x61, 0x6d, 0x20, 0x63, 0x61, 0x6e, 0x6e, 0x6f, 0x74, 0x20, 0x62, 0x65,
  0x20, 0x72, 0x75, 0x6e, 0x20, 0x69, 0x6e, 0x20, 0x44, 0x4f, 0x53, 0x20,
  0x6d, 0x6f, 0x64, 0x65, 0x2e, 0x24, 0x00, 0x00
};
static_assert(sizeof(dosProgram) % 8 == 0,
              "DOSProgram size must be multiple of 8");

static const int dosStubSize = sizeof(dos_header) + sizeof(dosProgram);
static_assert(dosStubSize % 8 == 0, "DOSStub size must be multiple of 8");

static const int numberOfDataDirectory = 16;

namespace {

class DebugDirectoryChunk : public NonSectionChunk {
public:
  DebugDirectoryChunk(const COFFLinkerContext &c,
                      const std::vector<std::pair<COFF::DebugType, Chunk *>> &r,
                      bool writeRepro)
      : records(r), writeRepro(writeRepro), ctx(c) {}

  size_t getSize() const override {
    return (records.size() + int(writeRepro)) * sizeof(debug_directory);
  }

  void writeTo(uint8_t *b) const override {
    auto *d = reinterpret_cast<debug_directory *>(b);

    for (const std::pair<COFF::DebugType, Chunk *>& record : records) {
      Chunk *c = record.second;
      const OutputSection *os = ctx.getOutputSection(c);
      uint64_t offs = os->getFileOff() + (c->getRVA() - os->getRVA());
      fillEntry(d, record.first, c->getSize(), c->getRVA(), offs);
      ++d;
    }

    if (writeRepro) {
      // FIXME: The COFF spec allows either a 0-sized entry to just say
      // "the timestamp field is really a hash", or a 4-byte size field
      // followed by that many bytes containing a longer hash (with the
      // lowest 4 bytes usually being the timestamp in little-endian order).
      // Consider storing the full 8 bytes computed by xxh3_64bits here.
      fillEntry(d, COFF::IMAGE_DEBUG_TYPE_REPRO, 0, 0, 0);
    }
  }

  void setTimeDateStamp(uint32_t timeDateStamp) {
    for (support::ulittle32_t *tds : timeDateStamps)
      *tds = timeDateStamp;
  }

private:
  void fillEntry(debug_directory *d, COFF::DebugType debugType, size_t size,
                 uint64_t rva, uint64_t offs) const {
    d->Characteristics = 0;
    d->TimeDateStamp = 0;
    d->MajorVersion = 0;
    d->MinorVersion = 0;
    d->Type = debugType;
    d->SizeOfData = size;
    d->AddressOfRawData = rva;
    d->PointerToRawData = offs;

    timeDateStamps.push_back(&d->TimeDateStamp);
  }

  mutable std::vector<support::ulittle32_t *> timeDateStamps;
  const std::vector<std::pair<COFF::DebugType, Chunk *>> &records;
  bool writeRepro;
  const COFFLinkerContext &ctx;
};

class CVDebugRecordChunk : public NonSectionChunk {
public:
  CVDebugRecordChunk(const COFFLinkerContext &c) : ctx(c) {}

  size_t getSize() const override {
    return sizeof(codeview::DebugInfo) + ctx.config.pdbAltPath.size() + 1;
  }

  void writeTo(uint8_t *b) const override {
    // Save off the DebugInfo entry to backfill the file signature (build id)
    // in Writer::writeBuildId
    buildId = reinterpret_cast<codeview::DebugInfo *>(b);

    // variable sized field (PDB Path)
    char *p = reinterpret_cast<char *>(b + sizeof(*buildId));
    if (!ctx.config.pdbAltPath.empty())
      memcpy(p, ctx.config.pdbAltPath.data(), ctx.config.pdbAltPath.size());
    p[ctx.config.pdbAltPath.size()] = '\0';
  }

  mutable codeview::DebugInfo *buildId = nullptr;

private:
  const COFFLinkerContext &ctx;
};

class ExtendedDllCharacteristicsChunk : public NonSectionChunk {
public:
  ExtendedDllCharacteristicsChunk(uint32_t c) : characteristics(c) {}

  size_t getSize() const override { return 4; }

  void writeTo(uint8_t *buf) const override { write32le(buf, characteristics); }

  uint32_t characteristics = 0;
};

// PartialSection represents a group of chunks that contribute to an
// OutputSection. Collating a collection of PartialSections of same name and
// characteristics constitutes the OutputSection.
class PartialSectionKey {
public:
  StringRef name;
  unsigned characteristics;

  bool operator<(const PartialSectionKey &other) const {
    int c = name.compare(other.name);
    if (c > 0)
      return false;
    if (c == 0)
      return characteristics < other.characteristics;
    return true;
  }
};

struct ChunkRange {
  Chunk *first = nullptr, *last;
};

// The writer writes a SymbolTable result to a file.
class Writer {
public:
  Writer(COFFLinkerContext &c)
      : buffer(errorHandler().outputBuffer), delayIdata(c), edata(c), ctx(c) {}
  void run();

private:
  void createSections();
  void createMiscChunks();
  void createImportTables();
  void appendImportThunks();
  void locateImportTables();
  void createExportTable();
  void mergeSections();
  void sortECChunks();
  void removeUnusedSections();
  void assignAddresses();
  bool isInRange(uint16_t relType, uint64_t s, uint64_t p, int margin);
  std::pair<Defined *, bool> getThunk(DenseMap<uint64_t, Defined *> &lastThunks,
                                      Defined *target, uint64_t p,
                                      uint16_t type, int margin);
  bool createThunks(OutputSection *os, int margin);
  bool verifyRanges(const std::vector<Chunk *> chunks);
  void createECCodeMap();
  void finalizeAddresses();
  void removeEmptySections();
  void assignOutputSectionIndices();
  void createSymbolAndStringTable();
  void openFile(StringRef outputPath);
  template <typename PEHeaderTy> void writeHeader();
  void createSEHTable();
  void createRuntimePseudoRelocs();
  void createECChunks();
  void insertCtorDtorSymbols();
  void markSymbolsWithRelocations(ObjFile *file, SymbolRVASet &usedSymbols);
  void createGuardCFTables();
  void markSymbolsForRVATable(ObjFile *file,
                              ArrayRef<SectionChunk *> symIdxChunks,
                              SymbolRVASet &tableSymbols);
  void getSymbolsFromSections(ObjFile *file,
                              ArrayRef<SectionChunk *> symIdxChunks,
                              std::vector<Symbol *> &symbols);
  void maybeAddRVATable(SymbolRVASet tableSymbols, StringRef tableSym,
                        StringRef countSym, bool hasFlag=false);
  void setSectionPermissions();
  void setECSymbols();
  void writeSections();
  void writeBuildId();
  void writePEChecksum();
  void sortSections();
  template <typename T> void sortExceptionTable(ChunkRange &exceptionTable);
  void sortExceptionTables();
  void sortCRTSectionChunks(std::vector<Chunk *> &chunks);
  void addSyntheticIdata();
  void sortBySectionOrder(std::vector<Chunk *> &chunks);
  void fixPartialSectionChars(StringRef name, uint32_t chars);
  bool fixGnuImportChunks();
  void fixTlsAlignment();
  PartialSection *createPartialSection(StringRef name, uint32_t outChars);
  PartialSection *findPartialSection(StringRef name, uint32_t outChars);

  std::optional<coff_symbol16> createSymbol(Defined *d);
  size_t addEntryToStringTable(StringRef str);

  OutputSection *findSection(StringRef name);
  void addBaserels();
  void addBaserelBlocks(std::vector<Baserel> &v);

  uint32_t getSizeOfInitializedData();

  void prepareLoadConfig();
  template <typename T> void prepareLoadConfig(T *loadConfig);
  template <typename T> void checkLoadConfigGuardData(const T *loadConfig);

  std::unique_ptr<FileOutputBuffer> &buffer;
  std::map<PartialSectionKey, PartialSection *> partialSections;
  std::vector<char> strtab;
  std::vector<llvm::object::coff_symbol16> outputSymtab;
  std::vector<ECCodeMapEntry> codeMap;
  IdataContents idata;
  Chunk *importTableStart = nullptr;
  uint64_t importTableSize = 0;
  Chunk *edataStart = nullptr;
  Chunk *edataEnd = nullptr;
  Chunk *iatStart = nullptr;
  uint64_t iatSize = 0;
  DelayLoadContents delayIdata;
  EdataContents edata;
  bool setNoSEHCharacteristic = false;
  uint32_t tlsAlignment = 0;

  DebugDirectoryChunk *debugDirectory = nullptr;
  std::vector<std::pair<COFF::DebugType, Chunk *>> debugRecords;
  CVDebugRecordChunk *buildId = nullptr;
  ArrayRef<uint8_t> sectionTable;

  uint64_t fileSize;
  uint32_t pointerToSymbolTable = 0;
  uint64_t sizeOfImage;
  uint64_t sizeOfHeaders;

  OutputSection *textSec;
  OutputSection *rdataSec;
  OutputSection *buildidSec;
  OutputSection *dataSec;
  OutputSection *pdataSec;
  OutputSection *idataSec;
  OutputSection *edataSec;
  OutputSection *didatSec;
  OutputSection *rsrcSec;
  OutputSection *relocSec;
  OutputSection *ctorsSec;
  OutputSection *dtorsSec;
  // Either .rdata section or .buildid section.
  OutputSection *debugInfoSec;

  // The range of .pdata sections in the output file.
  //
  // We need to keep track of the location of .pdata in whichever section it
  // gets merged into so that we can sort its contents and emit a correct data
  // directory entry for the exception table. This is also the case for some
  // other sections (such as .edata) but because the contents of those sections
  // are entirely linker-generated we can keep track of their locations using
  // the chunks that the linker creates. All .pdata chunks come from input
  // files, so we need to keep track of them separately.
  ChunkRange pdata;

  // x86_64 .pdata sections on ARM64EC/ARM64X targets.
  ChunkRange hybridPdata;

  COFFLinkerContext &ctx;
};
} // anonymous namespace

void lld::coff::writeResult(COFFLinkerContext &ctx) {
  llvm::TimeTraceScope timeScope("Write output(s)");
  Writer(ctx).run();
}

void OutputSection::addChunk(Chunk *c) {
  chunks.push_back(c);
}

void OutputSection::insertChunkAtStart(Chunk *c) {
  chunks.insert(chunks.begin(), c);
}

void OutputSection::setPermissions(uint32_t c) {
  header.Characteristics &= ~permMask;
  header.Characteristics |= c;
}

void OutputSection::merge(OutputSection *other) {
  chunks.insert(chunks.end(), other->chunks.begin(), other->chunks.end());
  other->chunks.clear();
  contribSections.insert(contribSections.end(), other->contribSections.begin(),
                         other->contribSections.end());
  other->contribSections.clear();

  // MS link.exe compatibility: when merging a code section into a data section,
  // mark the target section as a code section.
  if (other->header.Characteristics & IMAGE_SCN_CNT_CODE) {
    header.Characteristics |= IMAGE_SCN_CNT_CODE;
    header.Characteristics &=
        ~(IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_CNT_UNINITIALIZED_DATA);
  }
}

// Write the section header to a given buffer.
void OutputSection::writeHeaderTo(uint8_t *buf, bool isDebug) {
  auto *hdr = reinterpret_cast<coff_section *>(buf);
  *hdr = header;
  if (stringTableOff) {
    // If name is too long, write offset into the string table as a name.
    encodeSectionName(hdr->Name, stringTableOff);
  } else {
    assert(!isDebug || name.size() <= COFF::NameSize ||
           (hdr->Characteristics & IMAGE_SCN_MEM_DISCARDABLE) == 0);
    strncpy(hdr->Name, name.data(),
            std::min(name.size(), (size_t)COFF::NameSize));
  }
}

void OutputSection::addContributingPartialSection(PartialSection *sec) {
  contribSections.push_back(sec);
}

// Check whether the target address S is in range from a relocation
// of type relType at address P.
bool Writer::isInRange(uint16_t relType, uint64_t s, uint64_t p, int margin) {
  if (ctx.config.machine == ARMNT) {
    int64_t diff = AbsoluteDifference(s, p + 4) + margin;
    switch (relType) {
    case IMAGE_REL_ARM_BRANCH20T:
      return isInt<21>(diff);
    case IMAGE_REL_ARM_BRANCH24T:
    case IMAGE_REL_ARM_BLX23T:
      return isInt<25>(diff);
    default:
      return true;
    }
  } else if (ctx.config.machine == ARM64) {
    int64_t diff = AbsoluteDifference(s, p) + margin;
    switch (relType) {
    case IMAGE_REL_ARM64_BRANCH26:
      return isInt<28>(diff);
    case IMAGE_REL_ARM64_BRANCH19:
      return isInt<21>(diff);
    case IMAGE_REL_ARM64_BRANCH14:
      return isInt<16>(diff);
    default:
      return true;
    }
  } else {
    llvm_unreachable("Unexpected architecture");
  }
}

// Return the last thunk for the given target if it is in range,
// or create a new one.
std::pair<Defined *, bool>
Writer::getThunk(DenseMap<uint64_t, Defined *> &lastThunks, Defined *target,
                 uint64_t p, uint16_t type, int margin) {
  Defined *&lastThunk = lastThunks[target->getRVA()];
  if (lastThunk && isInRange(type, lastThunk->getRVA(), p, margin))
    return {lastThunk, false};
  Chunk *c;
  switch (ctx.config.machine) {
  case ARMNT:
    c = make<RangeExtensionThunkARM>(ctx, target);
    break;
  case ARM64:
    c = make<RangeExtensionThunkARM64>(ctx, target);
    break;
  default:
    llvm_unreachable("Unexpected architecture");
  }
  Defined *d = make<DefinedSynthetic>("range_extension_thunk", c);
  lastThunk = d;
  return {d, true};
}

// This checks all relocations, and for any relocation which isn't in range
// it adds a thunk after the section chunk that contains the relocation.
// If the latest thunk for the specific target is in range, that is used
// instead of creating a new thunk. All range checks are done with the
// specified margin, to make sure that relocations that originally are in
// range, but only barely, also get thunks - in case other added thunks makes
// the target go out of range.
//
// After adding thunks, we verify that all relocations are in range (with
// no extra margin requirements). If this failed, we restart (throwing away
// the previously created thunks) and retry with a wider margin.
bool Writer::createThunks(OutputSection *os, int margin) {
  bool addressesChanged = false;
  DenseMap<uint64_t, Defined *> lastThunks;
  DenseMap<std::pair<ObjFile *, Defined *>, uint32_t> thunkSymtabIndices;
  size_t thunksSize = 0;
  // Recheck Chunks.size() each iteration, since we can insert more
  // elements into it.
  for (size_t i = 0; i != os->chunks.size(); ++i) {
    SectionChunk *sc = dyn_cast_or_null<SectionChunk>(os->chunks[i]);
    if (!sc)
      continue;
    size_t thunkInsertionSpot = i + 1;

    // Try to get a good enough estimate of where new thunks will be placed.
    // Offset this by the size of the new thunks added so far, to make the
    // estimate slightly better.
    size_t thunkInsertionRVA = sc->getRVA() + sc->getSize() + thunksSize;
    ObjFile *file = sc->file;
    std::vector<std::pair<uint32_t, uint32_t>> relocReplacements;
    ArrayRef<coff_relocation> originalRelocs =
        file->getCOFFObj()->getRelocations(sc->header);
    for (size_t j = 0, e = originalRelocs.size(); j < e; ++j) {
      const coff_relocation &rel = originalRelocs[j];
      Symbol *relocTarget = file->getSymbol(rel.SymbolTableIndex);

      // The estimate of the source address P should be pretty accurate,
      // but we don't know whether the target Symbol address should be
      // offset by thunksSize or not (or by some of thunksSize but not all of
      // it), giving us some uncertainty once we have added one thunk.
      uint64_t p = sc->getRVA() + rel.VirtualAddress + thunksSize;

      Defined *sym = dyn_cast_or_null<Defined>(relocTarget);
      if (!sym)
        continue;

      uint64_t s = sym->getRVA();

      if (isInRange(rel.Type, s, p, margin))
        continue;

      // If the target isn't in range, hook it up to an existing or new thunk.
      auto [thunk, wasNew] = getThunk(lastThunks, sym, p, rel.Type, margin);
      if (wasNew) {
        Chunk *thunkChunk = thunk->getChunk();
        thunkChunk->setRVA(
            thunkInsertionRVA); // Estimate of where it will be located.
        os->chunks.insert(os->chunks.begin() + thunkInsertionSpot, thunkChunk);
        thunkInsertionSpot++;
        thunksSize += thunkChunk->getSize();
        thunkInsertionRVA += thunkChunk->getSize();
        addressesChanged = true;
      }

      // To redirect the relocation, add a symbol to the parent object file's
      // symbol table, and replace the relocation symbol table index with the
      // new index.
      auto insertion = thunkSymtabIndices.insert({{file, thunk}, ~0U});
      uint32_t &thunkSymbolIndex = insertion.first->second;
      if (insertion.second)
        thunkSymbolIndex = file->addRangeThunkSymbol(thunk);
      relocReplacements.emplace_back(j, thunkSymbolIndex);
    }

    // Get a writable copy of this section's relocations so they can be
    // modified. If the relocations point into the object file, allocate new
    // memory. Otherwise, this must be previously allocated memory that can be
    // modified in place.
    ArrayRef<coff_relocation> curRelocs = sc->getRelocs();
    MutableArrayRef<coff_relocation> newRelocs;
    if (originalRelocs.data() == curRelocs.data()) {
      newRelocs = MutableArrayRef(
          bAlloc().Allocate<coff_relocation>(originalRelocs.size()),
          originalRelocs.size());
    } else {
      newRelocs = MutableArrayRef(
          const_cast<coff_relocation *>(curRelocs.data()), curRelocs.size());
    }

    // Copy each relocation, but replace the symbol table indices which need
    // thunks.
    auto nextReplacement = relocReplacements.begin();
    auto endReplacement = relocReplacements.end();
    for (size_t i = 0, e = originalRelocs.size(); i != e; ++i) {
      newRelocs[i] = originalRelocs[i];
      if (nextReplacement != endReplacement && nextReplacement->first == i) {
        newRelocs[i].SymbolTableIndex = nextReplacement->second;
        ++nextReplacement;
      }
    }

    sc->setRelocs(newRelocs);
  }
  return addressesChanged;
}

// Create a code map for CHPE metadata.
void Writer::createECCodeMap() {
  if (!isArm64EC(ctx.config.machine))
    return;

  // Clear the map in case we were're recomputing the map after adding
  // a range extension thunk.
  codeMap.clear();

  std::optional<chpe_range_type> lastType;
  Chunk *first, *last;

  auto closeRange = [&]() {
    if (lastType) {
      codeMap.push_back({first, last, *lastType});
      lastType.reset();
    }
  };

  for (OutputSection *sec : ctx.outputSections) {
    for (Chunk *c : sec->chunks) {
      // Skip empty section chunks. MS link.exe does not seem to do that and
      // generates empty code ranges in some cases.
      if (isa<SectionChunk>(c) && !c->getSize())
        continue;

      std::optional<chpe_range_type> chunkType = c->getArm64ECRangeType();
      if (chunkType != lastType) {
        closeRange();
        first = c;
        lastType = chunkType;
      }
      last = c;
    }
  }

  closeRange();

  Symbol *tableCountSym = ctx.symtab.findUnderscore("__hybrid_code_map_count");
  cast<DefinedAbsolute>(tableCountSym)->setVA(codeMap.size());
}

// Verify that all relocations are in range, with no extra margin requirements.
bool Writer::verifyRanges(const std::vector<Chunk *> chunks) {
  for (Chunk *c : chunks) {
    SectionChunk *sc = dyn_cast_or_null<SectionChunk>(c);
    if (!sc)
      continue;

    ArrayRef<coff_relocation> relocs = sc->getRelocs();
    for (const coff_relocation &rel : relocs) {
      Symbol *relocTarget = sc->file->getSymbol(rel.SymbolTableIndex);

      Defined *sym = dyn_cast_or_null<Defined>(relocTarget);
      if (!sym)
        continue;

      uint64_t p = sc->getRVA() + rel.VirtualAddress;
      uint64_t s = sym->getRVA();

      if (!isInRange(rel.Type, s, p, 0))
        return false;
    }
  }
  return true;
}

// Assign addresses and add thunks if necessary.
void Writer::finalizeAddresses() {
  assignAddresses();
  if (ctx.config.machine != ARMNT && ctx.config.machine != ARM64)
    return;

  size_t origNumChunks = 0;
  for (OutputSection *sec : ctx.outputSections) {
    sec->origChunks = sec->chunks;
    origNumChunks += sec->chunks.size();
  }

  int pass = 0;
  int margin = 1024 * 100;
  while (true) {
    llvm::TimeTraceScope timeScope2("Add thunks pass");

    // First check whether we need thunks at all, or if the previous pass of
    // adding them turned out ok.
    bool rangesOk = true;
    size_t numChunks = 0;
    {
      llvm::TimeTraceScope timeScope3("Verify ranges");
      for (OutputSection *sec : ctx.outputSections) {
        if (!verifyRanges(sec->chunks)) {
          rangesOk = false;
          break;
        }
        numChunks += sec->chunks.size();
      }
    }
    if (rangesOk) {
      if (pass > 0)
        log("Added " + Twine(numChunks - origNumChunks) + " thunks with " +
            "margin " + Twine(margin) + " in " + Twine(pass) + " passes");
      return;
    }

    if (pass >= 10)
      fatal("adding thunks hasn't converged after " + Twine(pass) + " passes");

    if (pass > 0) {
      // If the previous pass didn't work out, reset everything back to the
      // original conditions before retrying with a wider margin. This should
      // ideally never happen under real circumstances.
      for (OutputSection *sec : ctx.outputSections)
        sec->chunks = sec->origChunks;
      margin *= 2;
    }

    // Try adding thunks everywhere where it is needed, with a margin
    // to avoid things going out of range due to the added thunks.
    bool addressesChanged = false;
    {
      llvm::TimeTraceScope timeScope3("Create thunks");
      for (OutputSection *sec : ctx.outputSections)
        addressesChanged |= createThunks(sec, margin);
    }
    // If the verification above thought we needed thunks, we should have
    // added some.
    assert(addressesChanged);
    (void)addressesChanged;

    // Recalculate the layout for the whole image (and verify the ranges at
    // the start of the next round).
    assignAddresses();

    pass++;
  }
}

void Writer::writePEChecksum() {
  if (!ctx.config.writeCheckSum) {
    return;
  }

  llvm::TimeTraceScope timeScope("PE checksum");

  // https://docs.microsoft.com/en-us/windows/win32/debug/pe-format#checksum
  uint32_t *buf = (uint32_t *)buffer->getBufferStart();
  uint32_t size = (uint32_t)(buffer->getBufferSize());

  coff_file_header *coffHeader =
      (coff_file_header *)((uint8_t *)buf + dosStubSize + sizeof(PEMagic));
  pe32_header *peHeader =
      (pe32_header *)((uint8_t *)coffHeader + sizeof(coff_file_header));

  uint64_t sum = 0;
  uint32_t count = size;
  ulittle16_t *addr = (ulittle16_t *)buf;

  // The PE checksum algorithm, implemented as suggested in RFC1071
  while (count > 1) {
    sum += *addr++;
    count -= 2;
  }

  // Add left-over byte, if any
  if (count > 0)
    sum += *(unsigned char *)addr;

  // Fold 32-bit sum to 16 bits
  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }

  sum += size;
  peHeader->CheckSum = sum;
}

// The main function of the writer.
void Writer::run() {
  {
    llvm::TimeTraceScope timeScope("Write PE");
    ScopedTimer t1(ctx.codeLayoutTimer);

    createImportTables();
    createSections();
    appendImportThunks();
    // Import thunks must be added before the Control Flow Guard tables are
    // added.
    createMiscChunks();
    createExportTable();
    mergeSections();
    sortECChunks();
    removeUnusedSections();
    finalizeAddresses();
    removeEmptySections();
    assignOutputSectionIndices();
    setSectionPermissions();
    setECSymbols();
    createSymbolAndStringTable();

    if (fileSize > UINT32_MAX)
      fatal("image size (" + Twine(fileSize) + ") " +
            "exceeds maximum allowable size (" + Twine(UINT32_MAX) + ")");

    openFile(ctx.config.outputFile);
    if (ctx.config.is64()) {
      writeHeader<pe32plus_header>();
    } else {
      writeHeader<pe32_header>();
    }
    writeSections();
    prepareLoadConfig();
    sortExceptionTables();

    // Fix up the alignment in the TLS Directory's characteristic field,
    // if a specific alignment value is needed
    if (tlsAlignment)
      fixTlsAlignment();
  }

  if (!ctx.config.pdbPath.empty() && ctx.config.debug) {
    assert(buildId);
    createPDB(ctx, sectionTable, buildId->buildId);
  }
  writeBuildId();

  writeLLDMapFile(ctx);
  writeMapFile(ctx);

  writePEChecksum();

  if (errorCount())
    return;

  llvm::TimeTraceScope timeScope("Commit PE to disk");
  ScopedTimer t2(ctx.outputCommitTimer);
  if (auto e = buffer->commit())
    fatal("failed to write output '" + buffer->getPath() +
          "': " + toString(std::move(e)));
}

static StringRef getOutputSectionName(StringRef name) {
  StringRef s = name.split('$').first;

  // Treat a later period as a separator for MinGW, for sections like
  // ".ctors.01234".
  return s.substr(0, s.find('.', 1));
}

// For /order.
void Writer::sortBySectionOrder(std::vector<Chunk *> &chunks) {
  auto getPriority = [&ctx = ctx](const Chunk *c) {
    if (auto *sec = dyn_cast<SectionChunk>(c))
      if (sec->sym)
        return ctx.config.order.lookup(sec->sym->getName());
    return 0;
  };

  llvm::stable_sort(chunks, [=](const Chunk *a, const Chunk *b) {
    return getPriority(a) < getPriority(b);
  });
}

// Change the characteristics of existing PartialSections that belong to the
// section Name to Chars.
void Writer::fixPartialSectionChars(StringRef name, uint32_t chars) {
  for (auto it : partialSections) {
    PartialSection *pSec = it.second;
    StringRef curName = pSec->name;
    if (!curName.consume_front(name) ||
        (!curName.empty() && !curName.starts_with("$")))
      continue;
    if (pSec->characteristics == chars)
      continue;
    PartialSection *destSec = createPartialSection(pSec->name, chars);
    destSec->chunks.insert(destSec->chunks.end(), pSec->chunks.begin(),
                           pSec->chunks.end());
    pSec->chunks.clear();
  }
}

// Sort concrete section chunks from GNU import libraries.
//
// GNU binutils doesn't use short import files, but instead produces import
// libraries that consist of object files, with section chunks for the .idata$*
// sections. These are linked just as regular static libraries. Each import
// library consists of one header object, one object file for every imported
// symbol, and one trailer object. In order for the .idata tables/lists to
// be formed correctly, the section chunks within each .idata$* section need
// to be grouped by library, and sorted alphabetically within each library
// (which makes sure the header comes first and the trailer last).
bool Writer::fixGnuImportChunks() {
  uint32_t rdata = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;

  // Make sure all .idata$* section chunks are mapped as RDATA in order to
  // be sorted into the same sections as our own synthesized .idata chunks.
  fixPartialSectionChars(".idata", rdata);

  bool hasIdata = false;
  // Sort all .idata$* chunks, grouping chunks from the same library,
  // with alphabetical ordering of the object files within a library.
  for (auto it : partialSections) {
    PartialSection *pSec = it.second;
    if (!pSec->name.starts_with(".idata"))
      continue;

    if (!pSec->chunks.empty())
      hasIdata = true;
    llvm::stable_sort(pSec->chunks, [&](Chunk *s, Chunk *t) {
      SectionChunk *sc1 = dyn_cast_or_null<SectionChunk>(s);
      SectionChunk *sc2 = dyn_cast_or_null<SectionChunk>(t);
      if (!sc1 || !sc2) {
        // if SC1, order them ascending. If SC2 or both null,
        // S is not less than T.
        return sc1 != nullptr;
      }
      // Make a string with "libraryname/objectfile" for sorting, achieving
      // both grouping by library and sorting of objects within a library,
      // at once.
      std::string key1 =
          (sc1->file->parentName + "/" + sc1->file->getName()).str();
      std::string key2 =
          (sc2->file->parentName + "/" + sc2->file->getName()).str();
      return key1 < key2;
    });
  }
  return hasIdata;
}

// Add generated idata chunks, for imported symbols and DLLs, and a
// terminator in .idata$2.
void Writer::addSyntheticIdata() {
  uint32_t rdata = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
  idata.create(ctx);

  // Add the .idata content in the right section groups, to allow
  // chunks from other linked in object files to be grouped together.
  // See Microsoft PE/COFF spec 5.4 for details.
  auto add = [&](StringRef n, std::vector<Chunk *> &v) {
    PartialSection *pSec = createPartialSection(n, rdata);
    pSec->chunks.insert(pSec->chunks.end(), v.begin(), v.end());
  };

  // The loader assumes a specific order of data.
  // Add each type in the correct order.
  add(".idata$2", idata.dirs);
  add(".idata$4", idata.lookups);
  add(".idata$5", idata.addresses);
  if (!idata.hints.empty())
    add(".idata$6", idata.hints);
  add(".idata$7", idata.dllNames);
}

// Locate the first Chunk and size of the import directory list and the
// IAT.
void Writer::locateImportTables() {
  uint32_t rdata = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;

  if (PartialSection *importDirs = findPartialSection(".idata$2", rdata)) {
    if (!importDirs->chunks.empty())
      importTableStart = importDirs->chunks.front();
    for (Chunk *c : importDirs->chunks)
      importTableSize += c->getSize();
  }

  if (PartialSection *importAddresses = findPartialSection(".idata$5", rdata)) {
    if (!importAddresses->chunks.empty())
      iatStart = importAddresses->chunks.front();
    for (Chunk *c : importAddresses->chunks)
      iatSize += c->getSize();
  }
}

// Return whether a SectionChunk's suffix (the dollar and any trailing
// suffix) should be removed and sorted into the main suffixless
// PartialSection.
static bool shouldStripSectionSuffix(SectionChunk *sc, StringRef name,
                                     bool isMinGW) {
  // On MinGW, comdat groups are formed by putting the comdat group name
  // after the '$' in the section name. For .eh_frame$<symbol>, that must
  // still be sorted before the .eh_frame trailer from crtend.o, thus just
  // strip the section name trailer. For other sections, such as
  // .tls$$<symbol> (where non-comdat .tls symbols are otherwise stored in
  // ".tls$"), they must be strictly sorted after .tls. And for the
  // hypothetical case of comdat .CRT$XCU, we definitely need to keep the
  // suffix for sorting. Thus, to play it safe, only strip the suffix for
  // the standard sections.
  if (!isMinGW)
    return false;
  if (!sc || !sc->isCOMDAT())
    return false;
  return name.starts_with(".text$") || name.starts_with(".data$") ||
         name.starts_with(".rdata$") || name.starts_with(".pdata$") ||
         name.starts_with(".xdata$") || name.starts_with(".eh_frame$");
}

void Writer::sortSections() {
  if (!ctx.config.callGraphProfile.empty()) {
    DenseMap<const SectionChunk *, int> order =
        computeCallGraphProfileOrder(ctx);
    for (auto it : order) {
      if (DefinedRegular *sym = it.first->sym)
        ctx.config.order[sym->getName()] = it.second;
    }
  }
  if (!ctx.config.order.empty())
    for (auto it : partialSections)
      sortBySectionOrder(it.second->chunks);
}

// Create output section objects and add them to OutputSections.
void Writer::createSections() {
  llvm::TimeTraceScope timeScope("Output sections");
  // First, create the builtin sections.
  const uint32_t data = IMAGE_SCN_CNT_INITIALIZED_DATA;
  const uint32_t bss = IMAGE_SCN_CNT_UNINITIALIZED_DATA;
  const uint32_t code = IMAGE_SCN_CNT_CODE;
  const uint32_t discardable = IMAGE_SCN_MEM_DISCARDABLE;
  const uint32_t r = IMAGE_SCN_MEM_READ;
  const uint32_t w = IMAGE_SCN_MEM_WRITE;
  const uint32_t x = IMAGE_SCN_MEM_EXECUTE;

  SmallDenseMap<std::pair<StringRef, uint32_t>, OutputSection *> sections;
  auto createSection = [&](StringRef name, uint32_t outChars) {
    OutputSection *&sec = sections[{name, outChars}];
    if (!sec) {
      sec = make<OutputSection>(name, outChars);
      ctx.outputSections.push_back(sec);
    }
    return sec;
  };

  // Try to match the section order used by link.exe.
  textSec = createSection(".text", code | r | x);
  createSection(".bss", bss | r | w);
  rdataSec = createSection(".rdata", data | r);
  buildidSec = createSection(".buildid", data | r);
  dataSec = createSection(".data", data | r | w);
  pdataSec = createSection(".pdata", data | r);
  idataSec = createSection(".idata", data | r);
  edataSec = createSection(".edata", data | r);
  didatSec = createSection(".didat", data | r);
  rsrcSec = createSection(".rsrc", data | r);
  relocSec = createSection(".reloc", data | discardable | r);
  ctorsSec = createSection(".ctors", data | r | w);
  dtorsSec = createSection(".dtors", data | r | w);

  // Then bin chunks by name and output characteristics.
  for (Chunk *c : ctx.symtab.getChunks()) {
    auto *sc = dyn_cast<SectionChunk>(c);
    if (sc && !sc->live) {
      if (ctx.config.verbose)
        sc->printDiscardedMessage();
      continue;
    }
    StringRef name = c->getSectionName();
    if (shouldStripSectionSuffix(sc, name, ctx.config.mingw))
      name = name.split('$').first;

    if (name.starts_with(".tls"))
      tlsAlignment = std::max(tlsAlignment, c->getAlignment());

    PartialSection *pSec = createPartialSection(name,
                                                c->getOutputCharacteristics());
    pSec->chunks.push_back(c);
  }

  fixPartialSectionChars(".rsrc", data | r);
  fixPartialSectionChars(".edata", data | r);
  // Even in non MinGW cases, we might need to link against GNU import
  // libraries.
  bool hasIdata = fixGnuImportChunks();
  if (!idata.empty())
    hasIdata = true;

  if (hasIdata)
    addSyntheticIdata();

  sortSections();

  if (hasIdata)
    locateImportTables();

  // Then create an OutputSection for each section.
  // '$' and all following characters in input section names are
  // discarded when determining output section. So, .text$foo
  // contributes to .text, for example. See PE/COFF spec 3.2.
  for (auto it : partialSections) {
    PartialSection *pSec = it.second;
    StringRef name = getOutputSectionName(pSec->name);
    uint32_t outChars = pSec->characteristics;

    if (name == ".CRT") {
      // In link.exe, there is a special case for the I386 target where .CRT
      // sections are treated as if they have output characteristics DATA | R if
      // their characteristics are DATA | R | W. This implements the same
      // special case for all architectures.
      outChars = data | r;

      log("Processing section " + pSec->name + " -> " + name);

      sortCRTSectionChunks(pSec->chunks);
    }

    OutputSection *sec = createSection(name, outChars);
    for (Chunk *c : pSec->chunks)
      sec->addChunk(c);

    sec->addContributingPartialSection(pSec);
  }

  // Finally, move some output sections to the end.
  auto sectionOrder = [&](const OutputSection *s) {
    // Move DISCARDABLE (or non-memory-mapped) sections to the end of file
    // because the loader cannot handle holes. Stripping can remove other
    // discardable ones than .reloc, which is first of them (created early).
    if (s->header.Characteristics & IMAGE_SCN_MEM_DISCARDABLE) {
      // Move discardable sections named .debug_ to the end, after other
      // discardable sections. Stripping only removes the sections named
      // .debug_* - thus try to avoid leaving holes after stripping.
      if (s->name.starts_with(".debug_"))
        return 3;
      return 2;
    }
    // .rsrc should come at the end of the non-discardable sections because its
    // size may change by the Win32 UpdateResources() function, causing
    // subsequent sections to move (see https://crbug.com/827082).
    if (s == rsrcSec)
      return 1;
    return 0;
  };
  llvm::stable_sort(ctx.outputSections,
                    [&](const OutputSection *s, const OutputSection *t) {
                      return sectionOrder(s) < sectionOrder(t);
                    });
}

void Writer::createMiscChunks() {
  llvm::TimeTraceScope timeScope("Misc chunks");
  Configuration *config = &ctx.config;

  for (MergeChunk *p : ctx.mergeChunkInstances) {
    if (p) {
      p->finalizeContents();
      rdataSec->addChunk(p);
    }
  }

  // Create thunks for locally-dllimported symbols.
  if (!ctx.symtab.localImportChunks.empty()) {
    for (Chunk *c : ctx.symtab.localImportChunks)
      rdataSec->addChunk(c);
  }

  // Create Debug Information Chunks
  debugInfoSec = config->mingw ? buildidSec : rdataSec;
  if (config->buildIDHash != BuildIDHash::None || config->debug ||
      config->repro || config->cetCompat) {
    debugDirectory =
        make<DebugDirectoryChunk>(ctx, debugRecords, config->repro);
    debugDirectory->setAlignment(4);
    debugInfoSec->addChunk(debugDirectory);
  }

  if (config->debug || config->buildIDHash != BuildIDHash::None) {
    // Make a CVDebugRecordChunk even when /DEBUG:CV is not specified.  We
    // output a PDB no matter what, and this chunk provides the only means of
    // allowing a debugger to match a PDB and an executable.  So we need it even
    // if we're ultimately not going to write CodeView data to the PDB.
    buildId = make<CVDebugRecordChunk>(ctx);
    debugRecords.emplace_back(COFF::IMAGE_DEBUG_TYPE_CODEVIEW, buildId);
    if (Symbol *buildidSym = ctx.symtab.findUnderscore("__buildid"))
      replaceSymbol<DefinedSynthetic>(buildidSym, buildidSym->getName(),
                                      buildId, 4);
  }

  if (config->cetCompat) {
    debugRecords.emplace_back(COFF::IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS,
                              make<ExtendedDllCharacteristicsChunk>(
                                  IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT));
  }

  // Align and add each chunk referenced by the debug data directory.
  for (std::pair<COFF::DebugType, Chunk *> r : debugRecords) {
    r.second->setAlignment(4);
    debugInfoSec->addChunk(r.second);
  }

  // Create SEH table. x86-only.
  if (config->safeSEH)
    createSEHTable();

  // Create /guard:cf tables if requested.
  if (config->guardCF != GuardCFLevel::Off)
    createGuardCFTables();

  if (isArm64EC(config->machine))
    createECChunks();

  if (config->autoImport)
    createRuntimePseudoRelocs();

  if (config->mingw)
    insertCtorDtorSymbols();
}

// Create .idata section for the DLL-imported symbol table.
// The format of this section is inherently Windows-specific.
// IdataContents class abstracted away the details for us,
// so we just let it create chunks and add them to the section.
void Writer::createImportTables() {
  llvm::TimeTraceScope timeScope("Import tables");
  // Initialize DLLOrder so that import entries are ordered in
  // the same order as in the command line. (That affects DLL
  // initialization order, and this ordering is MSVC-compatible.)
  for (ImportFile *file : ctx.importFileInstances) {
    if (!file->live)
      continue;

    std::string dll = StringRef(file->dllName).lower();
    if (ctx.config.dllOrder.count(dll) == 0)
      ctx.config.dllOrder[dll] = ctx.config.dllOrder.size();

    if (file->impSym && !isa<DefinedImportData>(file->impSym))
      fatal(toString(ctx, *file->impSym) + " was replaced");
    DefinedImportData *impSym = cast_or_null<DefinedImportData>(file->impSym);
    if (ctx.config.delayLoads.count(StringRef(file->dllName).lower())) {
      if (!file->thunkSym)
        fatal("cannot delay-load " + toString(file) +
              " due to import of data: " + toString(ctx, *impSym));
      delayIdata.add(impSym);
    } else {
      idata.add(impSym);
    }
  }
}

void Writer::appendImportThunks() {
  if (ctx.importFileInstances.empty())
    return;

  llvm::TimeTraceScope timeScope("Import thunks");
  for (ImportFile *file : ctx.importFileInstances) {
    if (!file->live)
      continue;

    if (!file->thunkSym)
      continue;

    if (!isa<DefinedImportThunk>(file->thunkSym))
      fatal(toString(ctx, *file->thunkSym) + " was replaced");
    DefinedImportThunk *thunk = cast<DefinedImportThunk>(file->thunkSym);
    if (file->thunkLive)
      textSec->addChunk(thunk->getChunk());
  }

  if (!delayIdata.empty()) {
    Defined *helper = cast<Defined>(ctx.config.delayLoadHelper);
    delayIdata.create(helper);
    for (Chunk *c : delayIdata.getChunks())
      didatSec->addChunk(c);
    for (Chunk *c : delayIdata.getDataChunks())
      dataSec->addChunk(c);
    for (Chunk *c : delayIdata.getCodeChunks())
      textSec->addChunk(c);
    for (Chunk *c : delayIdata.getCodePData())
      pdataSec->addChunk(c);
    for (Chunk *c : delayIdata.getCodeUnwindInfo())
      rdataSec->addChunk(c);
  }
}

void Writer::createExportTable() {
  llvm::TimeTraceScope timeScope("Export table");
  if (!edataSec->chunks.empty()) {
    // Allow using a custom built export table from input object files, instead
    // of having the linker synthesize the tables.
    if (ctx.config.hadExplicitExports)
      warn("literal .edata sections override exports");
  } else if (!ctx.config.exports.empty()) {
    for (Chunk *c : edata.chunks)
      edataSec->addChunk(c);
  }
  if (!edataSec->chunks.empty()) {
    edataStart = edataSec->chunks.front();
    edataEnd = edataSec->chunks.back();
  }
  // Warn on exported deleting destructor.
  for (auto e : ctx.config.exports)
    if (e.sym && e.sym->getName().starts_with("??_G"))
      warn("export of deleting dtor: " + toString(ctx, *e.sym));
}

void Writer::removeUnusedSections() {
  llvm::TimeTraceScope timeScope("Remove unused sections");
  // Remove sections that we can be sure won't get content, to avoid
  // allocating space for their section headers.
  auto isUnused = [this](OutputSection *s) {
    if (s == relocSec)
      return false; // This section is populated later.
    // MergeChunks have zero size at this point, as their size is finalized
    // later. Only remove sections that have no Chunks at all.
    return s->chunks.empty();
  };
  llvm::erase_if(ctx.outputSections, isUnused);
}

// The Windows loader doesn't seem to like empty sections,
// so we remove them if any.
void Writer::removeEmptySections() {
  llvm::TimeTraceScope timeScope("Remove empty sections");
  auto isEmpty = [](OutputSection *s) { return s->getVirtualSize() == 0; };
  llvm::erase_if(ctx.outputSections, isEmpty);
}

void Writer::assignOutputSectionIndices() {
  llvm::TimeTraceScope timeScope("Output sections indices");
  // Assign final output section indices, and assign each chunk to its output
  // section.
  uint32_t idx = 1;
  for (OutputSection *os : ctx.outputSections) {
    os->sectionIndex = idx;
    for (Chunk *c : os->chunks)
      c->setOutputSectionIdx(idx);
    ++idx;
  }

  // Merge chunks are containers of chunks, so assign those an output section
  // too.
  for (MergeChunk *mc : ctx.mergeChunkInstances)
    if (mc)
      for (SectionChunk *sc : mc->sections)
        if (sc && sc->live)
          sc->setOutputSectionIdx(mc->getOutputSectionIdx());
}

size_t Writer::addEntryToStringTable(StringRef str) {
  assert(str.size() > COFF::NameSize);
  size_t offsetOfEntry = strtab.size() + 4; // +4 for the size field
  strtab.insert(strtab.end(), str.begin(), str.end());
  strtab.push_back('\0');
  return offsetOfEntry;
}

std::optional<coff_symbol16> Writer::createSymbol(Defined *def) {
  coff_symbol16 sym;
  switch (def->kind()) {
  case Symbol::DefinedAbsoluteKind: {
    auto *da = dyn_cast<DefinedAbsolute>(def);
    // Note: COFF symbol can only store 32-bit values, so 64-bit absolute
    // values will be truncated.
    sym.Value = da->getVA();
    sym.SectionNumber = IMAGE_SYM_ABSOLUTE;
    break;
  }
  default: {
    // Don't write symbols that won't be written to the output to the symbol
    // table.
    // We also try to write DefinedSynthetic as a normal symbol. Some of these
    // symbols do point to an actual chunk, like __safe_se_handler_table. Others
    // like __ImageBase are outside of sections and thus cannot be represented.
    Chunk *c = def->getChunk();
    if (!c)
      return std::nullopt;
    OutputSection *os = ctx.getOutputSection(c);
    if (!os)
      return std::nullopt;

    sym.Value = def->getRVA() - os->getRVA();
    sym.SectionNumber = os->sectionIndex;
    break;
  }
  }

  // Symbols that are runtime pseudo relocations don't point to the actual
  // symbol data itself (as they are imported), but points to the IAT entry
  // instead. Avoid emitting them to the symbol table, as they can confuse
  // debuggers.
  if (def->isRuntimePseudoReloc)
    return std::nullopt;

  StringRef name = def->getName();
  if (name.size() > COFF::NameSize) {
    sym.Name.Offset.Zeroes = 0;
    sym.Name.Offset.Offset = addEntryToStringTable(name);
  } else {
    memset(sym.Name.ShortName, 0, COFF::NameSize);
    memcpy(sym.Name.ShortName, name.data(), name.size());
  }

  if (auto *d = dyn_cast<DefinedCOFF>(def)) {
    COFFSymbolRef ref = d->getCOFFSymbol();
    sym.Type = ref.getType();
    sym.StorageClass = ref.getStorageClass();
  } else if (def->kind() == Symbol::DefinedImportThunkKind) {
    sym.Type = (IMAGE_SYM_DTYPE_FUNCTION << SCT_COMPLEX_TYPE_SHIFT) |
               IMAGE_SYM_TYPE_NULL;
    sym.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
  } else {
    sym.Type = IMAGE_SYM_TYPE_NULL;
    sym.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
  }
  sym.NumberOfAuxSymbols = 0;
  return sym;
}

void Writer::createSymbolAndStringTable() {
  llvm::TimeTraceScope timeScope("Symbol and string table");
  // PE/COFF images are limited to 8 byte section names. Longer names can be
  // supported by writing a non-standard string table, but this string table is
  // not mapped at runtime and the long names will therefore be inaccessible.
  // link.exe always truncates section names to 8 bytes, whereas binutils always
  // preserves long section names via the string table. LLD adopts a hybrid
  // solution where discardable sections have long names preserved and
  // non-discardable sections have their names truncated, to ensure that any
  // section which is mapped at runtime also has its name mapped at runtime.
  for (OutputSection *sec : ctx.outputSections) {
    if (sec->name.size() <= COFF::NameSize)
      continue;
    if ((sec->header.Characteristics & IMAGE_SCN_MEM_DISCARDABLE) == 0)
      continue;
    if (ctx.config.warnLongSectionNames) {
      warn("section name " + sec->name +
           " is longer than 8 characters and will use a non-standard string "
           "table");
    }
    sec->setStringTableOff(addEntryToStringTable(sec->name));
  }

  if (ctx.config.writeSymtab) {
    for (ObjFile *file : ctx.objFileInstances) {
      for (Symbol *b : file->getSymbols()) {
        auto *d = dyn_cast_or_null<Defined>(b);
        if (!d || d->writtenToSymtab)
          continue;
        d->writtenToSymtab = true;
        if (auto *dc = dyn_cast_or_null<DefinedCOFF>(d)) {
          COFFSymbolRef symRef = dc->getCOFFSymbol();
          if (symRef.isSectionDefinition() ||
              symRef.getStorageClass() == COFF::IMAGE_SYM_CLASS_LABEL)
            continue;
        }

        if (std::optional<coff_symbol16> sym = createSymbol(d))
          outputSymtab.push_back(*sym);

        if (auto *dthunk = dyn_cast<DefinedImportThunk>(d)) {
          if (!dthunk->wrappedSym->writtenToSymtab) {
            dthunk->wrappedSym->writtenToSymtab = true;
            if (std::optional<coff_symbol16> sym =
                    createSymbol(dthunk->wrappedSym))
              outputSymtab.push_back(*sym);
          }
        }
      }
    }
  }

  if (outputSymtab.empty() && strtab.empty())
    return;

  // We position the symbol table to be adjacent to the end of the last section.
  uint64_t fileOff = fileSize;
  pointerToSymbolTable = fileOff;
  fileOff += outputSymtab.size() * sizeof(coff_symbol16);
  fileOff += 4 + strtab.size();
  fileSize = alignTo(fileOff, ctx.config.fileAlign);
}

void Writer::mergeSections() {
  llvm::TimeTraceScope timeScope("Merge sections");
  if (!pdataSec->chunks.empty()) {
    if (isArm64EC(ctx.config.machine)) {
      // On ARM64EC .pdata may contain both ARM64 and X64 data. Split them by
      // sorting and store their regions separately.
      llvm::stable_sort(pdataSec->chunks, [=](const Chunk *a, const Chunk *b) {
        return (a->getMachine() == AMD64) < (b->getMachine() == AMD64);
      });

      for (auto chunk : pdataSec->chunks) {
        if (chunk->getMachine() == AMD64) {
          hybridPdata.first = chunk;
          hybridPdata.last = pdataSec->chunks.back();
          break;
        }

        if (!pdata.first)
          pdata.first = chunk;
        pdata.last = chunk;
      }
    } else {
      pdata.first = pdataSec->chunks.front();
      pdata.last = pdataSec->chunks.back();
    }
  }

  for (auto &p : ctx.config.merge) {
    StringRef toName = p.second;
    if (p.first == toName)
      continue;
    StringSet<> names;
    while (true) {
      if (!names.insert(toName).second)
        fatal("/merge: cycle found for section '" + p.first + "'");
      auto i = ctx.config.merge.find(toName);
      if (i == ctx.config.merge.end())
        break;
      toName = i->second;
    }
    OutputSection *from = findSection(p.first);
    OutputSection *to = findSection(toName);
    if (!from)
      continue;
    if (!to) {
      from->name = toName;
      continue;
    }
    to->merge(from);
  }
}

// EC targets may have chunks of various architectures mixed together at this
// point. Group code chunks of the same architecture together by sorting chunks
// by their EC range type.
void Writer::sortECChunks() {
  if (!isArm64EC(ctx.config.machine))
    return;

  for (OutputSection *sec : ctx.outputSections) {
    if (sec->isCodeSection())
      llvm::stable_sort(sec->chunks, [=](const Chunk *a, const Chunk *b) {
        std::optional<chpe_range_type> aType = a->getArm64ECRangeType(),
                                       bType = b->getArm64ECRangeType();
        return bType && (!aType || *aType < *bType);
      });
  }
}

// Visits all sections to assign incremental, non-overlapping RVAs and
// file offsets.
void Writer::assignAddresses() {
  llvm::TimeTraceScope timeScope("Assign addresses");
  Configuration *config = &ctx.config;

  // We need to create EC code map so that ECCodeMapChunk knows its size.
  // We do it here to make sure that we account for range extension chunks.
  createECCodeMap();

  sizeOfHeaders = dosStubSize + sizeof(PEMagic) + sizeof(coff_file_header) +
                  sizeof(data_directory) * numberOfDataDirectory +
                  sizeof(coff_section) * ctx.outputSections.size();
  sizeOfHeaders +=
      config->is64() ? sizeof(pe32plus_header) : sizeof(pe32_header);
  sizeOfHeaders = alignTo(sizeOfHeaders, config->fileAlign);
  fileSize = sizeOfHeaders;

  // The first page is kept unmapped.
  uint64_t rva = alignTo(sizeOfHeaders, config->align);

  for (OutputSection *sec : ctx.outputSections) {
    llvm::TimeTraceScope timeScope("Section: ", sec->name);
    if (sec == relocSec)
      addBaserels();
    uint64_t rawSize = 0, virtualSize = 0;
    sec->header.VirtualAddress = rva;

    // If /FUNCTIONPADMIN is used, functions are padded in order to create a
    // hotpatchable image.
    uint32_t padding = sec->isCodeSection() ? config->functionPadMin : 0;
    std::optional<chpe_range_type> prevECRange;

    for (Chunk *c : sec->chunks) {
      // Alignment EC code range baudaries.
      if (isArm64EC(ctx.config.machine) && sec->isCodeSection()) {
        std::optional<chpe_range_type> rangeType = c->getArm64ECRangeType();
        if (rangeType != prevECRange) {
          virtualSize = alignTo(virtualSize, 4096);
          prevECRange = rangeType;
        }
      }
      if (padding && c->isHotPatchable())
        virtualSize += padding;
      // If chunk has EC entry thunk, reserve a space for an offset to the
      // thunk.
      if (c->getEntryThunk())
        virtualSize += sizeof(uint32_t);
      virtualSize = alignTo(virtualSize, c->getAlignment());
      c->setRVA(rva + virtualSize);
      virtualSize += c->getSize();
      if (c->hasData)
        rawSize = alignTo(virtualSize, config->fileAlign);
    }
    if (virtualSize > UINT32_MAX)
      error("section larger than 4 GiB: " + sec->name);
    sec->header.VirtualSize = virtualSize;
    sec->header.SizeOfRawData = rawSize;
    if (rawSize != 0)
      sec->header.PointerToRawData = fileSize;
    rva += alignTo(virtualSize, config->align);
    fileSize += alignTo(rawSize, config->fileAlign);
  }
  sizeOfImage = alignTo(rva, config->align);

  // Assign addresses to sections in MergeChunks.
  for (MergeChunk *mc : ctx.mergeChunkInstances)
    if (mc)
      mc->assignSubsectionRVAs();
}

template <typename PEHeaderTy> void Writer::writeHeader() {
  // Write DOS header. For backwards compatibility, the first part of a PE/COFF
  // executable consists of an MS-DOS MZ executable. If the executable is run
  // under DOS, that program gets run (usually to just print an error message).
  // When run under Windows, the loader looks at AddressOfNewExeHeader and uses
  // the PE header instead.
  Configuration *config = &ctx.config;
  uint8_t *buf = buffer->getBufferStart();
  auto *dos = reinterpret_cast<dos_header *>(buf);
  buf += sizeof(dos_header);
  dos->Magic[0] = 'M';
  dos->Magic[1] = 'Z';
  dos->UsedBytesInTheLastPage = dosStubSize % 512;
  dos->FileSizeInPages = divideCeil(dosStubSize, 512);
  dos->HeaderSizeInParagraphs = sizeof(dos_header) / 16;

  dos->AddressOfRelocationTable = sizeof(dos_header);
  dos->AddressOfNewExeHeader = dosStubSize;

  // Write DOS program.
  memcpy(buf, dosProgram, sizeof(dosProgram));
  buf += sizeof(dosProgram);

  // Write PE magic
  memcpy(buf, PEMagic, sizeof(PEMagic));
  buf += sizeof(PEMagic);

  // Write COFF header
  auto *coff = reinterpret_cast<coff_file_header *>(buf);
  buf += sizeof(*coff);
  switch (config->machine) {
  case ARM64EC:
    coff->Machine = AMD64;
    break;
  case ARM64X:
    coff->Machine = ARM64;
    break;
  default:
    coff->Machine = config->machine;
  }
  coff->NumberOfSections = ctx.outputSections.size();
  coff->Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE;
  if (config->largeAddressAware)
    coff->Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
  if (!config->is64())
    coff->Characteristics |= IMAGE_FILE_32BIT_MACHINE;
  if (config->dll)
    coff->Characteristics |= IMAGE_FILE_DLL;
  if (config->driverUponly)
    coff->Characteristics |= IMAGE_FILE_UP_SYSTEM_ONLY;
  if (!config->relocatable)
    coff->Characteristics |= IMAGE_FILE_RELOCS_STRIPPED;
  if (config->swaprunCD)
    coff->Characteristics |= IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP;
  if (config->swaprunNet)
    coff->Characteristics |= IMAGE_FILE_NET_RUN_FROM_SWAP;
  coff->SizeOfOptionalHeader =
      sizeof(PEHeaderTy) + sizeof(data_directory) * numberOfDataDirectory;

  // Write PE header
  auto *pe = reinterpret_cast<PEHeaderTy *>(buf);
  buf += sizeof(*pe);
  pe->Magic = config->is64() ? PE32Header::PE32_PLUS : PE32Header::PE32;

  // If {Major,Minor}LinkerVersion is left at 0.0, then for some
  // reason signing the resulting PE file with Authenticode produces a
  // signature that fails to validate on Windows 7 (but is OK on 10).
  // Set it to 14.0, which is what VS2015 outputs, and which avoids
  // that problem.
  pe->MajorLinkerVersion = 14;
  pe->MinorLinkerVersion = 0;

  pe->ImageBase = config->imageBase;
  pe->SectionAlignment = config->align;
  pe->FileAlignment = config->fileAlign;
  pe->MajorImageVersion = config->majorImageVersion;
  pe->MinorImageVersion = config->minorImageVersion;
  pe->MajorOperatingSystemVersion = config->majorOSVersion;
  pe->MinorOperatingSystemVersion = config->minorOSVersion;
  pe->MajorSubsystemVersion = config->majorSubsystemVersion;
  pe->MinorSubsystemVersion = config->minorSubsystemVersion;
  pe->Subsystem = config->subsystem;
  pe->SizeOfImage = sizeOfImage;
  pe->SizeOfHeaders = sizeOfHeaders;
  if (!config->noEntry) {
    Defined *entry = cast<Defined>(config->entry);
    pe->AddressOfEntryPoint = entry->getRVA();
    // Pointer to thumb code must have the LSB set, so adjust it.
    if (config->machine == ARMNT)
      pe->AddressOfEntryPoint |= 1;
  }
  pe->SizeOfStackReserve = config->stackReserve;
  pe->SizeOfStackCommit = config->stackCommit;
  pe->SizeOfHeapReserve = config->heapReserve;
  pe->SizeOfHeapCommit = config->heapCommit;
  if (config->appContainer)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_APPCONTAINER;
  if (config->driverWdm)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER;
  if (config->dynamicBase)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE;
  if (config->highEntropyVA)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA;
  if (!config->allowBind)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_NO_BIND;
  if (config->nxCompat)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_NX_COMPAT;
  if (!config->allowIsolation)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION;
  if (config->guardCF != GuardCFLevel::Off)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_GUARD_CF;
  if (config->integrityCheck)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY;
  if (setNoSEHCharacteristic || config->noSEH)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_NO_SEH;
  if (config->terminalServerAware)
    pe->DLLCharacteristics |= IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE;
  pe->NumberOfRvaAndSize = numberOfDataDirectory;
  if (textSec->getVirtualSize()) {
    pe->BaseOfCode = textSec->getRVA();
    pe->SizeOfCode = textSec->getRawSize();
  }
  pe->SizeOfInitializedData = getSizeOfInitializedData();

  // Write data directory
  auto *dir = reinterpret_cast<data_directory *>(buf);
  buf += sizeof(*dir) * numberOfDataDirectory;
  if (edataStart) {
    dir[EXPORT_TABLE].RelativeVirtualAddress = edataStart->getRVA();
    dir[EXPORT_TABLE].Size =
        edataEnd->getRVA() + edataEnd->getSize() - edataStart->getRVA();
  }
  if (importTableStart) {
    dir[IMPORT_TABLE].RelativeVirtualAddress = importTableStart->getRVA();
    dir[IMPORT_TABLE].Size = importTableSize;
  }
  if (iatStart) {
    dir[IAT].RelativeVirtualAddress = iatStart->getRVA();
    dir[IAT].Size = iatSize;
  }
  if (rsrcSec->getVirtualSize()) {
    dir[RESOURCE_TABLE].RelativeVirtualAddress = rsrcSec->getRVA();
    dir[RESOURCE_TABLE].Size = rsrcSec->getVirtualSize();
  }
  // ARM64EC (but not ARM64X) contains x86_64 exception table in data directory.
  ChunkRange &exceptionTable =
      ctx.config.machine == ARM64EC ? hybridPdata : pdata;
  if (exceptionTable.first) {
    dir[EXCEPTION_TABLE].RelativeVirtualAddress =
        exceptionTable.first->getRVA();
    dir[EXCEPTION_TABLE].Size = exceptionTable.last->getRVA() +
                                exceptionTable.last->getSize() -
                                exceptionTable.first->getRVA();
  }
  if (relocSec->getVirtualSize()) {
    dir[BASE_RELOCATION_TABLE].RelativeVirtualAddress = relocSec->getRVA();
    dir[BASE_RELOCATION_TABLE].Size = relocSec->getVirtualSize();
  }
  if (Symbol *sym = ctx.symtab.findUnderscore("_tls_used")) {
    if (Defined *b = dyn_cast<Defined>(sym)) {
      dir[TLS_TABLE].RelativeVirtualAddress = b->getRVA();
      dir[TLS_TABLE].Size = config->is64()
                                ? sizeof(object::coff_tls_directory64)
                                : sizeof(object::coff_tls_directory32);
    }
  }
  if (debugDirectory) {
    dir[DEBUG_DIRECTORY].RelativeVirtualAddress = debugDirectory->getRVA();
    dir[DEBUG_DIRECTORY].Size = debugDirectory->getSize();
  }
  if (Symbol *sym = ctx.symtab.findUnderscore("_load_config_used")) {
    if (auto *b = dyn_cast<DefinedRegular>(sym)) {
      SectionChunk *sc = b->getChunk();
      assert(b->getRVA() >= sc->getRVA());
      uint64_t offsetInChunk = b->getRVA() - sc->getRVA();
      if (!sc->hasData || offsetInChunk + 4 > sc->getSize())
        fatal("_load_config_used is malformed");

      ArrayRef<uint8_t> secContents = sc->getContents();
      uint32_t loadConfigSize =
          *reinterpret_cast<const ulittle32_t *>(&secContents[offsetInChunk]);
      if (offsetInChunk + loadConfigSize > sc->getSize())
        fatal("_load_config_used is too large");
      dir[LOAD_CONFIG_TABLE].RelativeVirtualAddress = b->getRVA();
      dir[LOAD_CONFIG_TABLE].Size = loadConfigSize;
    }
  }
  if (!delayIdata.empty()) {
    dir[DELAY_IMPORT_DESCRIPTOR].RelativeVirtualAddress =
        delayIdata.getDirRVA();
    dir[DELAY_IMPORT_DESCRIPTOR].Size = delayIdata.getDirSize();
  }

  // Write section table
  for (OutputSection *sec : ctx.outputSections) {
    sec->writeHeaderTo(buf, config->debug);
    buf += sizeof(coff_section);
  }
  sectionTable = ArrayRef<uint8_t>(
      buf - ctx.outputSections.size() * sizeof(coff_section), buf);

  if (outputSymtab.empty() && strtab.empty())
    return;

  coff->PointerToSymbolTable = pointerToSymbolTable;
  uint32_t numberOfSymbols = outputSymtab.size();
  coff->NumberOfSymbols = numberOfSymbols;
  auto *symbolTable = reinterpret_cast<coff_symbol16 *>(
      buffer->getBufferStart() + coff->PointerToSymbolTable);
  for (size_t i = 0; i != numberOfSymbols; ++i)
    symbolTable[i] = outputSymtab[i];
  // Create the string table, it follows immediately after the symbol table.
  // The first 4 bytes is length including itself.
  buf = reinterpret_cast<uint8_t *>(&symbolTable[numberOfSymbols]);
  write32le(buf, strtab.size() + 4);
  if (!strtab.empty())
    memcpy(buf + 4, strtab.data(), strtab.size());
}

void Writer::openFile(StringRef path) {
  buffer = CHECK(
      FileOutputBuffer::create(path, fileSize, FileOutputBuffer::F_executable),
      "failed to open " + path);
}

void Writer::createSEHTable() {
  SymbolRVASet handlers;
  for (ObjFile *file : ctx.objFileInstances) {
    if (!file->hasSafeSEH())
      error("/safeseh: " + file->getName() + " is not compatible with SEH");
    markSymbolsForRVATable(file, file->getSXDataChunks(), handlers);
  }

  // Set the "no SEH" characteristic if there really were no handlers, or if
  // there is no load config object to point to the table of handlers.
  setNoSEHCharacteristic =
      handlers.empty() || !ctx.symtab.findUnderscore("_load_config_used");

  maybeAddRVATable(std::move(handlers), "__safe_se_handler_table",
                   "__safe_se_handler_count");
}

// Add a symbol to an RVA set. Two symbols may have the same RVA, but an RVA set
// cannot contain duplicates. Therefore, the set is uniqued by Chunk and the
// symbol's offset into that Chunk.
static void addSymbolToRVASet(SymbolRVASet &rvaSet, Defined *s) {
  Chunk *c = s->getChunk();
  if (!c)
    return;
  if (auto *sc = dyn_cast<SectionChunk>(c))
    c = sc->repl; // Look through ICF replacement.
  uint32_t off = s->getRVA() - (c ? c->getRVA() : 0);
  rvaSet.insert({c, off});
}

// Given a symbol, add it to the GFIDs table if it is a live, defined, function
// symbol in an executable section.
static void maybeAddAddressTakenFunction(SymbolRVASet &addressTakenSyms,
                                         Symbol *s) {
  if (!s)
    return;

  switch (s->kind()) {
  case Symbol::DefinedLocalImportKind:
  case Symbol::DefinedImportDataKind:
    // Defines an __imp_ pointer, so it is data, so it is ignored.
    break;
  case Symbol::DefinedCommonKind:
    // Common is always data, so it is ignored.
    break;
  case Symbol::DefinedAbsoluteKind:
  case Symbol::DefinedSyntheticKind:
    // Absolute is never code, synthetic generally isn't and usually isn't
    // determinable.
    break;
  case Symbol::LazyArchiveKind:
  case Symbol::LazyObjectKind:
  case Symbol::LazyDLLSymbolKind:
  case Symbol::UndefinedKind:
    // Undefined symbols resolve to zero, so they don't have an RVA. Lazy
    // symbols shouldn't have relocations.
    break;

  case Symbol::DefinedImportThunkKind:
    // Thunks are always code, include them.
    addSymbolToRVASet(addressTakenSyms, cast<Defined>(s));
    break;

  case Symbol::DefinedRegularKind: {
    // This is a regular, defined, symbol from a COFF file. Mark the symbol as
    // address taken if the symbol type is function and it's in an executable
    // section.
    auto *d = cast<DefinedRegular>(s);
    if (d->getCOFFSymbol().getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION) {
      SectionChunk *sc = dyn_cast<SectionChunk>(d->getChunk());
      if (sc && sc->live &&
          sc->getOutputCharacteristics() & IMAGE_SCN_MEM_EXECUTE)
        addSymbolToRVASet(addressTakenSyms, d);
    }
    break;
  }
  }
}

// Visit all relocations from all section contributions of this object file and
// mark the relocation target as address-taken.
void Writer::markSymbolsWithRelocations(ObjFile *file,
                                        SymbolRVASet &usedSymbols) {
  for (Chunk *c : file->getChunks()) {
    // We only care about live section chunks. Common chunks and other chunks
    // don't generally contain relocations.
    SectionChunk *sc = dyn_cast<SectionChunk>(c);
    if (!sc || !sc->live)
      continue;

    for (const coff_relocation &reloc : sc->getRelocs()) {
      if (ctx.config.machine == I386 &&
          reloc.Type == COFF::IMAGE_REL_I386_REL32)
        // Ignore relative relocations on x86. On x86_64 they can't be ignored
        // since they're also used to compute absolute addresses.
        continue;

      Symbol *ref = sc->file->getSymbol(reloc.SymbolTableIndex);
      maybeAddAddressTakenFunction(usedSymbols, ref);
    }
  }
}

// Create the guard function id table. This is a table of RVAs of all
// address-taken functions. It is sorted and uniqued, just like the safe SEH
// table.
void Writer::createGuardCFTables() {
  Configuration *config = &ctx.config;

  SymbolRVASet addressTakenSyms;
  SymbolRVASet giatsRVASet;
  std::vector<Symbol *> giatsSymbols;
  SymbolRVASet longJmpTargets;
  SymbolRVASet ehContTargets;
  for (ObjFile *file : ctx.objFileInstances) {
    // If the object was compiled with /guard:cf, the address taken symbols
    // are in .gfids$y sections, and the longjmp targets are in .gljmp$y
    // sections. If the object was not compiled with /guard:cf, we assume there
    // were no setjmp targets, and that all code symbols with relocations are
    // possibly address-taken.
    if (file->hasGuardCF()) {
      markSymbolsForRVATable(file, file->getGuardFidChunks(), addressTakenSyms);
      markSymbolsForRVATable(file, file->getGuardIATChunks(), giatsRVASet);
      getSymbolsFromSections(file, file->getGuardIATChunks(), giatsSymbols);
      markSymbolsForRVATable(file, file->getGuardLJmpChunks(), longJmpTargets);
    } else {
      markSymbolsWithRelocations(file, addressTakenSyms);
    }
    // If the object was compiled with /guard:ehcont, the ehcont targets are in
    // .gehcont$y sections.
    if (file->hasGuardEHCont())
      markSymbolsForRVATable(file, file->getGuardEHContChunks(), ehContTargets);
  }

  // Mark the image entry as address-taken.
  if (config->entry)
    maybeAddAddressTakenFunction(addressTakenSyms, config->entry);

  // Mark exported symbols in executable sections as address-taken.
  for (Export &e : config->exports)
    maybeAddAddressTakenFunction(addressTakenSyms, e.sym);

  // For each entry in the .giats table, check if it has a corresponding load
  // thunk (e.g. because the DLL that defines it will be delay-loaded) and, if
  // so, add the load thunk to the address taken (.gfids) table.
  for (Symbol *s : giatsSymbols) {
    if (auto *di = dyn_cast<DefinedImportData>(s)) {
      if (di->loadThunkSym)
        addSymbolToRVASet(addressTakenSyms, di->loadThunkSym);
    }
  }

  // Ensure sections referenced in the gfid table are 16-byte aligned.
  for (const ChunkAndOffset &c : addressTakenSyms)
    if (c.inputChunk->getAlignment() < 16)
      c.inputChunk->setAlignment(16);

  maybeAddRVATable(std::move(addressTakenSyms), "__guard_fids_table",
                   "__guard_fids_count");

  // Add the Guard Address Taken IAT Entry Table (.giats).
  maybeAddRVATable(std::move(giatsRVASet), "__guard_iat_table",
                   "__guard_iat_count");

  // Add the longjmp target table unless the user told us not to.
  if (config->guardCF & GuardCFLevel::LongJmp)
    maybeAddRVATable(std::move(longJmpTargets), "__guard_longjmp_table",
                     "__guard_longjmp_count");

  // Add the ehcont target table unless the user told us not to.
  if (config->guardCF & GuardCFLevel::EHCont)
    maybeAddRVATable(std::move(ehContTargets), "__guard_eh_cont_table",
                     "__guard_eh_cont_count");

  // Set __guard_flags, which will be used in the load config to indicate that
  // /guard:cf was enabled.
  uint32_t guardFlags = uint32_t(GuardFlags::CF_INSTRUMENTED) |
                        uint32_t(GuardFlags::CF_FUNCTION_TABLE_PRESENT);
  if (config->guardCF & GuardCFLevel::LongJmp)
    guardFlags |= uint32_t(GuardFlags::CF_LONGJUMP_TABLE_PRESENT);
  if (config->guardCF & GuardCFLevel::EHCont)
    guardFlags |= uint32_t(GuardFlags::EH_CONTINUATION_TABLE_PRESENT);
  Symbol *flagSym = ctx.symtab.findUnderscore("__guard_flags");
  cast<DefinedAbsolute>(flagSym)->setVA(guardFlags);
}

// Take a list of input sections containing symbol table indices and add those
// symbols to a vector. The challenge is that symbol RVAs are not known and
// depend on the table size, so we can't directly build a set of integers.
void Writer::getSymbolsFromSections(ObjFile *file,
                                    ArrayRef<SectionChunk *> symIdxChunks,
                                    std::vector<Symbol *> &symbols) {
  for (SectionChunk *c : symIdxChunks) {
    // Skip sections discarded by linker GC. This comes up when a .gfids section
    // is associated with something like a vtable and the vtable is discarded.
    // In this case, the associated gfids section is discarded, and we don't
    // mark the virtual member functions as address-taken by the vtable.
    if (!c->live)
      continue;

    // Validate that the contents look like symbol table indices.
    ArrayRef<uint8_t> data = c->getContents();
    if (data.size() % 4 != 0) {
      warn("ignoring " + c->getSectionName() +
           " symbol table index section in object " + toString(file));
      continue;
    }

    // Read each symbol table index and check if that symbol was included in the
    // final link. If so, add it to the vector of symbols.
    ArrayRef<ulittle32_t> symIndices(
        reinterpret_cast<const ulittle32_t *>(data.data()), data.size() / 4);
    ArrayRef<Symbol *> objSymbols = file->getSymbols();
    for (uint32_t symIndex : symIndices) {
      if (symIndex >= objSymbols.size()) {
        warn("ignoring invalid symbol table index in section " +
             c->getSectionName() + " in object " + toString(file));
        continue;
      }
      if (Symbol *s = objSymbols[symIndex]) {
        if (s->isLive())
          symbols.push_back(cast<Symbol>(s));
      }
    }
  }
}

// Take a list of input sections containing symbol table indices and add those
// symbols to an RVA table.
void Writer::markSymbolsForRVATable(ObjFile *file,
                                    ArrayRef<SectionChunk *> symIdxChunks,
                                    SymbolRVASet &tableSymbols) {
  std::vector<Symbol *> syms;
  getSymbolsFromSections(file, symIdxChunks, syms);

  for (Symbol *s : syms)
    addSymbolToRVASet(tableSymbols, cast<Defined>(s));
}

// Replace the absolute table symbol with a synthetic symbol pointing to
// tableChunk so that we can emit base relocations for it and resolve section
// relative relocations.
void Writer::maybeAddRVATable(SymbolRVASet tableSymbols, StringRef tableSym,
                              StringRef countSym, bool hasFlag) {
  if (tableSymbols.empty())
    return;

  NonSectionChunk *tableChunk;
  if (hasFlag)
    tableChunk = make<RVAFlagTableChunk>(std::move(tableSymbols));
  else
    tableChunk = make<RVATableChunk>(std::move(tableSymbols));
  rdataSec->addChunk(tableChunk);

  Symbol *t = ctx.symtab.findUnderscore(tableSym);
  Symbol *c = ctx.symtab.findUnderscore(countSym);
  replaceSymbol<DefinedSynthetic>(t, t->getName(), tableChunk);
  cast<DefinedAbsolute>(c)->setVA(tableChunk->getSize() / (hasFlag ? 5 : 4));
}

// Create CHPE metadata chunks.
void Writer::createECChunks() {
  auto codeMapChunk = make<ECCodeMapChunk>(codeMap);
  rdataSec->addChunk(codeMapChunk);
  Symbol *codeMapSym = ctx.symtab.findUnderscore("__hybrid_code_map");
  replaceSymbol<DefinedSynthetic>(codeMapSym, codeMapSym->getName(),
                                  codeMapChunk);
}

// MinGW specific. Gather all relocations that are imported from a DLL even
// though the code didn't expect it to, produce the table that the runtime
// uses for fixing them up, and provide the synthetic symbols that the
// runtime uses for finding the table.
void Writer::createRuntimePseudoRelocs() {
  std::vector<RuntimePseudoReloc> rels;

  for (Chunk *c : ctx.symtab.getChunks()) {
    auto *sc = dyn_cast<SectionChunk>(c);
    if (!sc || !sc->live)
      continue;
    // Don't create pseudo relocations for sections that won't be
    // mapped at runtime.
    if (sc->header->Characteristics & IMAGE_SCN_MEM_DISCARDABLE)
      continue;
    sc->getRuntimePseudoRelocs(rels);
  }

  if (!ctx.config.pseudoRelocs) {
    // Not writing any pseudo relocs; if some were needed, error out and
    // indicate what required them.
    for (const RuntimePseudoReloc &rpr : rels)
      error("automatic dllimport of " + rpr.sym->getName() + " in " +
            toString(rpr.target->file) + " requires pseudo relocations");
    return;
  }

  if (!rels.empty()) {
    log("Writing " + Twine(rels.size()) + " runtime pseudo relocations");
    const char *symbolName = "_pei386_runtime_relocator";
    Symbol *relocator = ctx.symtab.findUnderscore(symbolName);
    if (!relocator)
      error("output image has runtime pseudo relocations, but the function " +
            Twine(symbolName) +
            " is missing; it is needed for fixing the relocations at runtime");
  }

  PseudoRelocTableChunk *table = make<PseudoRelocTableChunk>(rels);
  rdataSec->addChunk(table);
  EmptyChunk *endOfList = make<EmptyChunk>();
  rdataSec->addChunk(endOfList);

  Symbol *headSym = ctx.symtab.findUnderscore("__RUNTIME_PSEUDO_RELOC_LIST__");
  Symbol *endSym =
      ctx.symtab.findUnderscore("__RUNTIME_PSEUDO_RELOC_LIST_END__");
  replaceSymbol<DefinedSynthetic>(headSym, headSym->getName(), table);
  replaceSymbol<DefinedSynthetic>(endSym, endSym->getName(), endOfList);
}

// MinGW specific.
// The MinGW .ctors and .dtors lists have sentinels at each end;
// a (uintptr_t)-1 at the start and a (uintptr_t)0 at the end.
// There's a symbol pointing to the start sentinel pointer, __CTOR_LIST__
// and __DTOR_LIST__ respectively.
void Writer::insertCtorDtorSymbols() {
  AbsolutePointerChunk *ctorListHead = make<AbsolutePointerChunk>(ctx, -1);
  AbsolutePointerChunk *ctorListEnd = make<AbsolutePointerChunk>(ctx, 0);
  AbsolutePointerChunk *dtorListHead = make<AbsolutePointerChunk>(ctx, -1);
  AbsolutePointerChunk *dtorListEnd = make<AbsolutePointerChunk>(ctx, 0);
  ctorsSec->insertChunkAtStart(ctorListHead);
  ctorsSec->addChunk(ctorListEnd);
  dtorsSec->insertChunkAtStart(dtorListHead);
  dtorsSec->addChunk(dtorListEnd);

  Symbol *ctorListSym = ctx.symtab.findUnderscore("__CTOR_LIST__");
  Symbol *dtorListSym = ctx.symtab.findUnderscore("__DTOR_LIST__");
  replaceSymbol<DefinedSynthetic>(ctorListSym, ctorListSym->getName(),
                                  ctorListHead);
  replaceSymbol<DefinedSynthetic>(dtorListSym, dtorListSym->getName(),
                                  dtorListHead);
}

// Handles /section options to allow users to overwrite
// section attributes.
void Writer::setSectionPermissions() {
  llvm::TimeTraceScope timeScope("Sections permissions");
  for (auto &p : ctx.config.section) {
    StringRef name = p.first;
    uint32_t perm = p.second;
    for (OutputSection *sec : ctx.outputSections)
      if (sec->name == name)
        sec->setPermissions(perm);
  }
}

// Set symbols used by ARM64EC metadata.
void Writer::setECSymbols() {
  if (!isArm64EC(ctx.config.machine))
    return;

  Symbol *rfeTableSym = ctx.symtab.findUnderscore("__arm64x_extra_rfe_table");
  replaceSymbol<DefinedSynthetic>(rfeTableSym, "__arm64x_extra_rfe_table",
                                  pdata.first);

  if (pdata.first) {
    Symbol *rfeSizeSym =
        ctx.symtab.findUnderscore("__arm64x_extra_rfe_table_size");
    cast<DefinedAbsolute>(rfeSizeSym)
        ->setVA(pdata.last->getRVA() + pdata.last->getSize() -
                pdata.first->getRVA());
  }
}

// Write section contents to a mmap'ed file.
void Writer::writeSections() {
  llvm::TimeTraceScope timeScope("Write sections");
  uint8_t *buf = buffer->getBufferStart();
  for (OutputSection *sec : ctx.outputSections) {
    uint8_t *secBuf = buf + sec->getFileOff();
    // Fill gaps between functions in .text with INT3 instructions
    // instead of leaving as NUL bytes (which can be interpreted as
    // ADD instructions). Only fill the gaps between chunks. Most
    // chunks overwrite it anyway, but uninitialized data chunks
    // merged into a code section don't.
    if ((sec->header.Characteristics & IMAGE_SCN_CNT_CODE) &&
        (ctx.config.machine == AMD64 || ctx.config.machine == I386)) {
      uint32_t prevEnd = 0;
      for (Chunk *c : sec->chunks) {
        uint32_t off = c->getRVA() - sec->getRVA();
        memset(secBuf + prevEnd, 0xCC, off - prevEnd);
        prevEnd = off + c->getSize();
      }
      memset(secBuf + prevEnd, 0xCC, sec->getRawSize() - prevEnd);
    }

    parallelForEach(sec->chunks, [&](Chunk *c) {
      c->writeTo(secBuf + c->getRVA() - sec->getRVA());
    });
  }
}

void Writer::writeBuildId() {
  llvm::TimeTraceScope timeScope("Write build ID");

  // There are two important parts to the build ID.
  // 1) If building with debug info, the COFF debug directory contains a
  //    timestamp as well as a Guid and Age of the PDB.
  // 2) In all cases, the PE COFF file header also contains a timestamp.
  // For reproducibility, instead of a timestamp we want to use a hash of the
  // PE contents.
  Configuration *config = &ctx.config;
  bool generateSyntheticBuildId = config->buildIDHash == BuildIDHash::Binary;
  if (generateSyntheticBuildId) {
    assert(buildId && "BuildId is not set!");
    // BuildId->BuildId was filled in when the PDB was written.
  }

  // At this point the only fields in the COFF file which remain unset are the
  // "timestamp" in the COFF file header, and the ones in the coff debug
  // directory.  Now we can hash the file and write that hash to the various
  // timestamp fields in the file.
  StringRef outputFileData(
      reinterpret_cast<const char *>(buffer->getBufferStart()),
      buffer->getBufferSize());

  uint32_t timestamp = config->timestamp;
  uint64_t hash = 0;

  if (config->repro || generateSyntheticBuildId)
    hash = xxh3_64bits(outputFileData);

  if (config->repro)
    timestamp = static_cast<uint32_t>(hash);

  if (generateSyntheticBuildId) {
    buildId->buildId->PDB70.CVSignature = OMF::Signature::PDB70;
    buildId->buildId->PDB70.Age = 1;
    memcpy(buildId->buildId->PDB70.Signature, &hash, 8);
    // xxhash only gives us 8 bytes, so put some fixed data in the other half.
    memcpy(&buildId->buildId->PDB70.Signature[8], "LLD PDB.", 8);
  }

  if (debugDirectory)
    debugDirectory->setTimeDateStamp(timestamp);

  uint8_t *buf = buffer->getBufferStart();
  buf += dosStubSize + sizeof(PEMagic);
  object::coff_file_header *coffHeader =
      reinterpret_cast<coff_file_header *>(buf);
  coffHeader->TimeDateStamp = timestamp;
}

// Sort .pdata section contents according to PE/COFF spec 5.5.
template <typename T>
void Writer::sortExceptionTable(ChunkRange &exceptionTable) {
  if (!exceptionTable.first)
    return;

  // We assume .pdata contains function table entries only.
  auto bufAddr = [&](Chunk *c) {
    OutputSection *os = ctx.getOutputSection(c);
    return buffer->getBufferStart() + os->getFileOff() + c->getRVA() -
           os->getRVA();
  };
  uint8_t *begin = bufAddr(exceptionTable.first);
  uint8_t *end = bufAddr(exceptionTable.last) + exceptionTable.last->getSize();
  if ((end - begin) % sizeof(T) != 0) {
    fatal("unexpected .pdata size: " + Twine(end - begin) +
          " is not a multiple of " + Twine(sizeof(T)));
  }

  parallelSort(MutableArrayRef<T>(reinterpret_cast<T *>(begin),
                                  reinterpret_cast<T *>(end)),
               [](const T &a, const T &b) { return a.begin < b.begin; });
}

// Sort .pdata section contents according to PE/COFF spec 5.5.
void Writer::sortExceptionTables() {
  llvm::TimeTraceScope timeScope("Sort exception table");

  struct EntryX64 {
    ulittle32_t begin, end, unwind;
  };
  struct EntryArm {
    ulittle32_t begin, unwind;
  };

  switch (ctx.config.machine) {
  case AMD64:
    sortExceptionTable<EntryX64>(pdata);
    break;
  case ARM64EC:
  case ARM64X:
    sortExceptionTable<EntryX64>(hybridPdata);
    [[fallthrough]];
  case ARMNT:
  case ARM64:
    sortExceptionTable<EntryArm>(pdata);
    break;
  default:
    if (pdata.first)
      lld::errs() << "warning: don't know how to handle .pdata.\n";
    break;
  }
}

// The CRT section contains, among other things, the array of function
// pointers that initialize every global variable that is not trivially
// constructed. The CRT calls them one after the other prior to invoking
// main().
//
// As per C++ spec, 3.6.2/2.3,
// "Variables with ordered initialization defined within a single
// translation unit shall be initialized in the order of their definitions
// in the translation unit"
//
// It is therefore critical to sort the chunks containing the function
// pointers in the order that they are listed in the object file (top to
// bottom), otherwise global objects might not be initialized in the
// correct order.
void Writer::sortCRTSectionChunks(std::vector<Chunk *> &chunks) {
  auto sectionChunkOrder = [](const Chunk *a, const Chunk *b) {
    auto sa = dyn_cast<SectionChunk>(a);
    auto sb = dyn_cast<SectionChunk>(b);
    assert(sa && sb && "Non-section chunks in CRT section!");

    StringRef sAObj = sa->file->mb.getBufferIdentifier();
    StringRef sBObj = sb->file->mb.getBufferIdentifier();

    return sAObj == sBObj && sa->getSectionNumber() < sb->getSectionNumber();
  };
  llvm::stable_sort(chunks, sectionChunkOrder);

  if (ctx.config.verbose) {
    for (auto &c : chunks) {
      auto sc = dyn_cast<SectionChunk>(c);
      log("  " + sc->file->mb.getBufferIdentifier().str() +
          ", SectionID: " + Twine(sc->getSectionNumber()));
    }
  }
}

OutputSection *Writer::findSection(StringRef name) {
  for (OutputSection *sec : ctx.outputSections)
    if (sec->name == name)
      return sec;
  return nullptr;
}

uint32_t Writer::getSizeOfInitializedData() {
  uint32_t res = 0;
  for (OutputSection *s : ctx.outputSections)
    if (s->header.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
      res += s->getRawSize();
  return res;
}

// Add base relocations to .reloc section.
void Writer::addBaserels() {
  if (!ctx.config.relocatable)
    return;
  relocSec->chunks.clear();
  std::vector<Baserel> v;
  for (OutputSection *sec : ctx.outputSections) {
    if (sec->header.Characteristics & IMAGE_SCN_MEM_DISCARDABLE)
      continue;
    llvm::TimeTraceScope timeScope("Base relocations: ", sec->name);
    // Collect all locations for base relocations.
    for (Chunk *c : sec->chunks)
      c->getBaserels(&v);
    // Add the addresses to .reloc section.
    if (!v.empty())
      addBaserelBlocks(v);
    v.clear();
  }
}

// Add addresses to .reloc section. Note that addresses are grouped by page.
void Writer::addBaserelBlocks(std::vector<Baserel> &v) {
  const uint32_t mask = ~uint32_t(pageSize - 1);
  uint32_t page = v[0].rva & mask;
  size_t i = 0, j = 1;
  for (size_t e = v.size(); j < e; ++j) {
    uint32_t p = v[j].rva & mask;
    if (p == page)
      continue;
    relocSec->addChunk(make<BaserelChunk>(page, &v[i], &v[0] + j));
    i = j;
    page = p;
  }
  if (i == j)
    return;
  relocSec->addChunk(make<BaserelChunk>(page, &v[i], &v[0] + j));
}

PartialSection *Writer::createPartialSection(StringRef name,
                                             uint32_t outChars) {
  PartialSection *&pSec = partialSections[{name, outChars}];
  if (pSec)
    return pSec;
  pSec = make<PartialSection>(name, outChars);
  return pSec;
}

PartialSection *Writer::findPartialSection(StringRef name, uint32_t outChars) {
  auto it = partialSections.find({name, outChars});
  if (it != partialSections.end())
    return it->second;
  return nullptr;
}

void Writer::fixTlsAlignment() {
  Defined *tlsSym =
      dyn_cast_or_null<Defined>(ctx.symtab.findUnderscore("_tls_used"));
  if (!tlsSym)
    return;

  OutputSection *sec = ctx.getOutputSection(tlsSym->getChunk());
  assert(sec && tlsSym->getRVA() >= sec->getRVA() &&
         "no output section for _tls_used");

  uint8_t *secBuf = buffer->getBufferStart() + sec->getFileOff();
  uint64_t tlsOffset = tlsSym->getRVA() - sec->getRVA();
  uint64_t directorySize = ctx.config.is64()
                               ? sizeof(object::coff_tls_directory64)
                               : sizeof(object::coff_tls_directory32);

  if (tlsOffset + directorySize > sec->getRawSize())
    fatal("_tls_used sym is malformed");

  if (ctx.config.is64()) {
    object::coff_tls_directory64 *tlsDir =
        reinterpret_cast<object::coff_tls_directory64 *>(&secBuf[tlsOffset]);
    tlsDir->setAlignment(tlsAlignment);
  } else {
    object::coff_tls_directory32 *tlsDir =
        reinterpret_cast<object::coff_tls_directory32 *>(&secBuf[tlsOffset]);
    tlsDir->setAlignment(tlsAlignment);
  }
}

void Writer::prepareLoadConfig() {
  Symbol *sym = ctx.symtab.findUnderscore("_load_config_used");
  auto *b = cast_if_present<DefinedRegular>(sym);
  if (!b) {
    if (ctx.config.guardCF != GuardCFLevel::Off)
      warn("Control Flow Guard is enabled but '_load_config_used' is missing");
    return;
  }

  OutputSection *sec = ctx.getOutputSection(b->getChunk());
  uint8_t *buf = buffer->getBufferStart();
  uint8_t *secBuf = buf + sec->getFileOff();
  uint8_t *symBuf = secBuf + (b->getRVA() - sec->getRVA());
  uint32_t expectedAlign = ctx.config.is64() ? 8 : 4;
  if (b->getChunk()->getAlignment() < expectedAlign)
    warn("'_load_config_used' is misaligned (expected alignment to be " +
         Twine(expectedAlign) + " bytes, got " +
         Twine(b->getChunk()->getAlignment()) + " instead)");
  else if (!isAligned(Align(expectedAlign), b->getRVA()))
    warn("'_load_config_used' is misaligned (RVA is 0x" +
         Twine::utohexstr(b->getRVA()) + " not aligned to " +
         Twine(expectedAlign) + " bytes)");

  if (ctx.config.is64())
    prepareLoadConfig(reinterpret_cast<coff_load_configuration64 *>(symBuf));
  else
    prepareLoadConfig(reinterpret_cast<coff_load_configuration32 *>(symBuf));
}

template <typename T> void Writer::prepareLoadConfig(T *loadConfig) {
  if (ctx.config.dependentLoadFlags)
    loadConfig->DependentLoadFlags = ctx.config.dependentLoadFlags;

  checkLoadConfigGuardData(loadConfig);
}

template <typename T>
void Writer::checkLoadConfigGuardData(const T *loadConfig) {
  size_t loadConfigSize = loadConfig->Size;

#define RETURN_IF_NOT_CONTAINS(field)                                          \
  if (loadConfigSize < offsetof(T, field) + sizeof(T::field)) {                \
    warn("'_load_config_used' structure too small to include " #field);        \
    return;                                                                    \
  }

#define IF_CONTAINS(field)                                                     \
  if (loadConfigSize >= offsetof(T, field) + sizeof(T::field))

#define CHECK_VA(field, sym)                                                   \
  if (auto *s = dyn_cast<DefinedSynthetic>(ctx.symtab.findUnderscore(sym)))    \
    if (loadConfig->field != ctx.config.imageBase + s->getRVA())               \
      warn(#field " not set correctly in '_load_config_used'");

#define CHECK_ABSOLUTE(field, sym)                                             \
  if (auto *s = dyn_cast<DefinedAbsolute>(ctx.symtab.findUnderscore(sym)))     \
    if (loadConfig->field != s->getVA())                                       \
      warn(#field " not set correctly in '_load_config_used'");

  if (ctx.config.guardCF == GuardCFLevel::Off)
    return;
  RETURN_IF_NOT_CONTAINS(GuardFlags)
  CHECK_VA(GuardCFFunctionTable, "__guard_fids_table")
  CHECK_ABSOLUTE(GuardCFFunctionCount, "__guard_fids_count")
  CHECK_ABSOLUTE(GuardFlags, "__guard_flags")
  IF_CONTAINS(GuardAddressTakenIatEntryCount) {
    CHECK_VA(GuardAddressTakenIatEntryTable, "__guard_iat_table")
    CHECK_ABSOLUTE(GuardAddressTakenIatEntryCount, "__guard_iat_count")
  }

  if (!(ctx.config.guardCF & GuardCFLevel::LongJmp))
    return;
  RETURN_IF_NOT_CONTAINS(GuardLongJumpTargetCount)
  CHECK_VA(GuardLongJumpTargetTable, "__guard_longjmp_table")
  CHECK_ABSOLUTE(GuardLongJumpTargetCount, "__guard_longjmp_count")

  if (!(ctx.config.guardCF & GuardCFLevel::EHCont))
    return;
  RETURN_IF_NOT_CONTAINS(GuardEHContinuationCount)
  CHECK_VA(GuardEHContinuationTable, "__guard_eh_cont_table")
  CHECK_ABSOLUTE(GuardEHContinuationCount, "__guard_eh_cont_count")

#undef RETURN_IF_NOT_CONTAINS
#undef IF_CONTAINS
#undef CHECK_VA
#undef CHECK_ABSOLUTE
}
