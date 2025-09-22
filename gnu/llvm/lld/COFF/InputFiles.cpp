//===- InputFiles.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "COFFLinkerContext.h"
#include "Chunks.h"
#include "Config.h"
#include "DebugTypes.h"
#include "Driver.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/DWARF.h"
#include "llvm-c/lto.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <cstring>
#include <optional>
#include <system_error>
#include <utility>

using namespace llvm;
using namespace llvm::COFF;
using namespace llvm::codeview;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::coff;

using llvm::Triple;
using llvm::support::ulittle32_t;

// Returns the last element of a path, which is supposed to be a filename.
static StringRef getBasename(StringRef path) {
  return sys::path::filename(path, sys::path::Style::windows);
}

// Returns a string in the format of "foo.obj" or "foo.obj(bar.lib)".
std::string lld::toString(const coff::InputFile *file) {
  if (!file)
    return "<internal>";
  if (file->parentName.empty() || file->kind() == coff::InputFile::ImportKind)
    return std::string(file->getName());

  return (getBasename(file->parentName) + "(" + getBasename(file->getName()) +
          ")")
      .str();
}

/// Checks that Source is compatible with being a weak alias to Target.
/// If Source is Undefined and has no weak alias set, makes it a weak
/// alias to Target.
static void checkAndSetWeakAlias(COFFLinkerContext &ctx, InputFile *f,
                                 Symbol *source, Symbol *target) {
  if (auto *u = dyn_cast<Undefined>(source)) {
    if (u->weakAlias && u->weakAlias != target) {
      // Weak aliases as produced by GCC are named in the form
      // .weak.<weaksymbol>.<othersymbol>, where <othersymbol> is the name
      // of another symbol emitted near the weak symbol.
      // Just use the definition from the first object file that defined
      // this weak symbol.
      if (ctx.config.allowDuplicateWeak)
        return;
      ctx.symtab.reportDuplicate(source, f);
    }
    u->weakAlias = target;
  }
}

static bool ignoredSymbolName(StringRef name) {
  return name == "@feat.00" || name == "@comp.id";
}

ArchiveFile::ArchiveFile(COFFLinkerContext &ctx, MemoryBufferRef m)
    : InputFile(ctx, ArchiveKind, m) {}

void ArchiveFile::parse() {
  // Parse a MemoryBufferRef as an archive file.
  file = CHECK(Archive::create(mb), this);

  // Read the symbol table to construct Lazy objects.
  for (const Archive::Symbol &sym : file->symbols())
    ctx.symtab.addLazyArchive(this, sym);
}

// Returns a buffer pointing to a member file containing a given symbol.
void ArchiveFile::addMember(const Archive::Symbol &sym) {
  const Archive::Child &c =
      CHECK(sym.getMember(),
            "could not get the member for symbol " + toCOFFString(ctx, sym));

  // Return an empty buffer if we have already returned the same buffer.
  if (!seen.insert(c.getChildOffset()).second)
    return;

  ctx.driver.enqueueArchiveMember(c, sym, getName());
}

std::vector<MemoryBufferRef> lld::coff::getArchiveMembers(Archive *file) {
  std::vector<MemoryBufferRef> v;
  Error err = Error::success();
  for (const Archive::Child &c : file->children(err)) {
    MemoryBufferRef mbref =
        CHECK(c.getMemoryBufferRef(),
              file->getFileName() +
                  ": could not get the buffer for a child of the archive");
    v.push_back(mbref);
  }
  if (err)
    fatal(file->getFileName() +
          ": Archive::children failed: " + toString(std::move(err)));
  return v;
}

void ObjFile::parseLazy() {
  // Native object file.
  std::unique_ptr<Binary> coffObjPtr = CHECK(createBinary(mb), this);
  COFFObjectFile *coffObj = cast<COFFObjectFile>(coffObjPtr.get());
  uint32_t numSymbols = coffObj->getNumberOfSymbols();
  for (uint32_t i = 0; i < numSymbols; ++i) {
    COFFSymbolRef coffSym = check(coffObj->getSymbol(i));
    if (coffSym.isUndefined() || !coffSym.isExternal() ||
        coffSym.isWeakExternal())
      continue;
    StringRef name = check(coffObj->getSymbolName(coffSym));
    if (coffSym.isAbsolute() && ignoredSymbolName(name))
      continue;
    ctx.symtab.addLazyObject(this, name);
    i += coffSym.getNumberOfAuxSymbols();
  }
}

struct ECMapEntry {
  ulittle32_t src;
  ulittle32_t dst;
  ulittle32_t type;
};

void ObjFile::initializeECThunks() {
  for (SectionChunk *chunk : hybmpChunks) {
    if (chunk->getContents().size() % sizeof(ECMapEntry)) {
      error("Invalid .hybmp chunk size " + Twine(chunk->getContents().size()));
      continue;
    }

    const uint8_t *end =
        chunk->getContents().data() + chunk->getContents().size();
    for (const uint8_t *iter = chunk->getContents().data(); iter != end;
         iter += sizeof(ECMapEntry)) {
      auto entry = reinterpret_cast<const ECMapEntry *>(iter);
      switch (entry->type) {
      case Arm64ECThunkType::Entry:
        ctx.symtab.addEntryThunk(getSymbol(entry->src), getSymbol(entry->dst));
        break;
      case Arm64ECThunkType::Exit:
      case Arm64ECThunkType::GuestExit:
        break;
      default:
        warn("Ignoring unknown EC thunk type " + Twine(entry->type));
      }
    }
  }
}

void ObjFile::parse() {
  // Parse a memory buffer as a COFF file.
  std::unique_ptr<Binary> bin = CHECK(createBinary(mb), this);

  if (auto *obj = dyn_cast<COFFObjectFile>(bin.get())) {
    bin.release();
    coffObj.reset(obj);
  } else {
    fatal(toString(this) + " is not a COFF file");
  }

  // Read section and symbol tables.
  initializeChunks();
  initializeSymbols();
  initializeFlags();
  initializeDependencies();
  initializeECThunks();
}

const coff_section *ObjFile::getSection(uint32_t i) {
  auto sec = coffObj->getSection(i);
  if (!sec)
    fatal("getSection failed: #" + Twine(i) + ": " + toString(sec.takeError()));
  return *sec;
}

// We set SectionChunk pointers in the SparseChunks vector to this value
// temporarily to mark comdat sections as having an unknown resolution. As we
// walk the object file's symbol table, once we visit either a leader symbol or
// an associative section definition together with the parent comdat's leader,
// we set the pointer to either nullptr (to mark the section as discarded) or a
// valid SectionChunk for that section.
static SectionChunk *const pendingComdat = reinterpret_cast<SectionChunk *>(1);

void ObjFile::initializeChunks() {
  uint32_t numSections = coffObj->getNumberOfSections();
  sparseChunks.resize(numSections + 1);
  for (uint32_t i = 1; i < numSections + 1; ++i) {
    const coff_section *sec = getSection(i);
    if (sec->Characteristics & IMAGE_SCN_LNK_COMDAT)
      sparseChunks[i] = pendingComdat;
    else
      sparseChunks[i] = readSection(i, nullptr, "");
  }
}

SectionChunk *ObjFile::readSection(uint32_t sectionNumber,
                                   const coff_aux_section_definition *def,
                                   StringRef leaderName) {
  const coff_section *sec = getSection(sectionNumber);

  StringRef name;
  if (Expected<StringRef> e = coffObj->getSectionName(sec))
    name = *e;
  else
    fatal("getSectionName failed: #" + Twine(sectionNumber) + ": " +
          toString(e.takeError()));

  if (name == ".drectve") {
    ArrayRef<uint8_t> data;
    cantFail(coffObj->getSectionContents(sec, data));
    directives = StringRef((const char *)data.data(), data.size());
    return nullptr;
  }

  if (name == ".llvm_addrsig") {
    addrsigSec = sec;
    return nullptr;
  }

  if (name == ".llvm.call-graph-profile") {
    callgraphSec = sec;
    return nullptr;
  }

  // Object files may have DWARF debug info or MS CodeView debug info
  // (or both).
  //
  // DWARF sections don't need any special handling from the perspective
  // of the linker; they are just a data section containing relocations.
  // We can just link them to complete debug info.
  //
  // CodeView needs linker support. We need to interpret debug info,
  // and then write it to a separate .pdb file.

  // Ignore DWARF debug info unless requested to be included.
  if (!ctx.config.includeDwarfChunks && name.starts_with(".debug_"))
    return nullptr;

  if (sec->Characteristics & llvm::COFF::IMAGE_SCN_LNK_REMOVE)
    return nullptr;
  SectionChunk *c;
  if (isArm64EC(getMachineType()))
    c = make<SectionChunkEC>(this, sec);
  else
    c = make<SectionChunk>(this, sec);
  if (def)
    c->checksum = def->CheckSum;

  // CodeView sections are stored to a different vector because they are not
  // linked in the regular manner.
  if (c->isCodeView())
    debugChunks.push_back(c);
  else if (name == ".gfids$y")
    guardFidChunks.push_back(c);
  else if (name == ".giats$y")
    guardIATChunks.push_back(c);
  else if (name == ".gljmp$y")
    guardLJmpChunks.push_back(c);
  else if (name == ".gehcont$y")
    guardEHContChunks.push_back(c);
  else if (name == ".sxdata")
    sxDataChunks.push_back(c);
  else if (isArm64EC(getMachineType()) && name == ".hybmp$x")
    hybmpChunks.push_back(c);
  else if (ctx.config.tailMerge && sec->NumberOfRelocations == 0 &&
           name == ".rdata" && leaderName.starts_with("??_C@"))
    // COFF sections that look like string literal sections (i.e. no
    // relocations, in .rdata, leader symbol name matches the MSVC name mangling
    // for string literals) are subject to string tail merging.
    MergeChunk::addSection(ctx, c);
  else if (name == ".rsrc" || name.starts_with(".rsrc$"))
    resourceChunks.push_back(c);
  else
    chunks.push_back(c);

  return c;
}

void ObjFile::includeResourceChunks() {
  chunks.insert(chunks.end(), resourceChunks.begin(), resourceChunks.end());
}

void ObjFile::readAssociativeDefinition(
    COFFSymbolRef sym, const coff_aux_section_definition *def) {
  readAssociativeDefinition(sym, def, def->getNumber(sym.isBigObj()));
}

void ObjFile::readAssociativeDefinition(COFFSymbolRef sym,
                                        const coff_aux_section_definition *def,
                                        uint32_t parentIndex) {
  SectionChunk *parent = sparseChunks[parentIndex];
  int32_t sectionNumber = sym.getSectionNumber();

  auto diag = [&]() {
    StringRef name = check(coffObj->getSymbolName(sym));

    StringRef parentName;
    const coff_section *parentSec = getSection(parentIndex);
    if (Expected<StringRef> e = coffObj->getSectionName(parentSec))
      parentName = *e;
    error(toString(this) + ": associative comdat " + name + " (sec " +
          Twine(sectionNumber) + ") has invalid reference to section " +
          parentName + " (sec " + Twine(parentIndex) + ")");
  };

  if (parent == pendingComdat) {
    // This can happen if an associative comdat refers to another associative
    // comdat that appears after it (invalid per COFF spec) or to a section
    // without any symbols.
    diag();
    return;
  }

  // Check whether the parent is prevailing. If it is, so are we, and we read
  // the section; otherwise mark it as discarded.
  if (parent) {
    SectionChunk *c = readSection(sectionNumber, def, "");
    sparseChunks[sectionNumber] = c;
    if (c) {
      c->selection = IMAGE_COMDAT_SELECT_ASSOCIATIVE;
      parent->addAssociative(c);
    }
  } else {
    sparseChunks[sectionNumber] = nullptr;
  }
}

void ObjFile::recordPrevailingSymbolForMingw(
    COFFSymbolRef sym, DenseMap<StringRef, uint32_t> &prevailingSectionMap) {
  // For comdat symbols in executable sections, where this is the copy
  // of the section chunk we actually include instead of discarding it,
  // add the symbol to a map to allow using it for implicitly
  // associating .[px]data$<func> sections to it.
  // Use the suffix from the .text$<func> instead of the leader symbol
  // name, for cases where the names differ (i386 mangling/decorations,
  // cases where the leader is a weak symbol named .weak.func.default*).
  int32_t sectionNumber = sym.getSectionNumber();
  SectionChunk *sc = sparseChunks[sectionNumber];
  if (sc && sc->getOutputCharacteristics() & IMAGE_SCN_MEM_EXECUTE) {
    StringRef name = sc->getSectionName().split('$').second;
    prevailingSectionMap[name] = sectionNumber;
  }
}

void ObjFile::maybeAssociateSEHForMingw(
    COFFSymbolRef sym, const coff_aux_section_definition *def,
    const DenseMap<StringRef, uint32_t> &prevailingSectionMap) {
  StringRef name = check(coffObj->getSymbolName(sym));
  if (name.consume_front(".pdata$") || name.consume_front(".xdata$") ||
      name.consume_front(".eh_frame$")) {
    // For MinGW, treat .[px]data$<func> and .eh_frame$<func> as implicitly
    // associative to the symbol <func>.
    auto parentSym = prevailingSectionMap.find(name);
    if (parentSym != prevailingSectionMap.end())
      readAssociativeDefinition(sym, def, parentSym->second);
  }
}

Symbol *ObjFile::createRegular(COFFSymbolRef sym) {
  SectionChunk *sc = sparseChunks[sym.getSectionNumber()];
  if (sym.isExternal()) {
    StringRef name = check(coffObj->getSymbolName(sym));
    if (sc)
      return ctx.symtab.addRegular(this, name, sym.getGeneric(), sc,
                                   sym.getValue());
    // For MinGW symbols named .weak.* that point to a discarded section,
    // don't create an Undefined symbol. If nothing ever refers to the symbol,
    // everything should be fine. If something actually refers to the symbol
    // (e.g. the undefined weak alias), linking will fail due to undefined
    // references at the end.
    if (ctx.config.mingw && name.starts_with(".weak."))
      return nullptr;
    return ctx.symtab.addUndefined(name, this, false);
  }
  if (sc)
    return make<DefinedRegular>(this, /*Name*/ "", /*IsCOMDAT*/ false,
                                /*IsExternal*/ false, sym.getGeneric(), sc);
  return nullptr;
}

void ObjFile::initializeSymbols() {
  uint32_t numSymbols = coffObj->getNumberOfSymbols();
  symbols.resize(numSymbols);

  SmallVector<std::pair<Symbol *, uint32_t>, 8> weakAliases;
  std::vector<uint32_t> pendingIndexes;
  pendingIndexes.reserve(numSymbols);

  DenseMap<StringRef, uint32_t> prevailingSectionMap;
  std::vector<const coff_aux_section_definition *> comdatDefs(
      coffObj->getNumberOfSections() + 1);

  for (uint32_t i = 0; i < numSymbols; ++i) {
    COFFSymbolRef coffSym = check(coffObj->getSymbol(i));
    bool prevailingComdat;
    if (coffSym.isUndefined()) {
      symbols[i] = createUndefined(coffSym);
    } else if (coffSym.isWeakExternal()) {
      symbols[i] = createUndefined(coffSym);
      uint32_t tagIndex = coffSym.getAux<coff_aux_weak_external>()->TagIndex;
      weakAliases.emplace_back(symbols[i], tagIndex);
    } else if (std::optional<Symbol *> optSym =
                   createDefined(coffSym, comdatDefs, prevailingComdat)) {
      symbols[i] = *optSym;
      if (ctx.config.mingw && prevailingComdat)
        recordPrevailingSymbolForMingw(coffSym, prevailingSectionMap);
    } else {
      // createDefined() returns std::nullopt if a symbol belongs to a section
      // that was pending at the point when the symbol was read. This can happen
      // in two cases:
      // 1) section definition symbol for a comdat leader;
      // 2) symbol belongs to a comdat section associated with another section.
      // In both of these cases, we can expect the section to be resolved by
      // the time we finish visiting the remaining symbols in the symbol
      // table. So we postpone the handling of this symbol until that time.
      pendingIndexes.push_back(i);
    }
    i += coffSym.getNumberOfAuxSymbols();
  }

  for (uint32_t i : pendingIndexes) {
    COFFSymbolRef sym = check(coffObj->getSymbol(i));
    if (const coff_aux_section_definition *def = sym.getSectionDefinition()) {
      if (def->Selection == IMAGE_COMDAT_SELECT_ASSOCIATIVE)
        readAssociativeDefinition(sym, def);
      else if (ctx.config.mingw)
        maybeAssociateSEHForMingw(sym, def, prevailingSectionMap);
    }
    if (sparseChunks[sym.getSectionNumber()] == pendingComdat) {
      StringRef name = check(coffObj->getSymbolName(sym));
      log("comdat section " + name +
          " without leader and unassociated, discarding");
      continue;
    }
    symbols[i] = createRegular(sym);
  }

  for (auto &kv : weakAliases) {
    Symbol *sym = kv.first;
    uint32_t idx = kv.second;
    checkAndSetWeakAlias(ctx, this, sym, symbols[idx]);
  }

  // Free the memory used by sparseChunks now that symbol loading is finished.
  decltype(sparseChunks)().swap(sparseChunks);
}

Symbol *ObjFile::createUndefined(COFFSymbolRef sym) {
  StringRef name = check(coffObj->getSymbolName(sym));
  return ctx.symtab.addUndefined(name, this, sym.isWeakExternal());
}

static const coff_aux_section_definition *findSectionDef(COFFObjectFile *obj,
                                                         int32_t section) {
  uint32_t numSymbols = obj->getNumberOfSymbols();
  for (uint32_t i = 0; i < numSymbols; ++i) {
    COFFSymbolRef sym = check(obj->getSymbol(i));
    if (sym.getSectionNumber() != section)
      continue;
    if (const coff_aux_section_definition *def = sym.getSectionDefinition())
      return def;
  }
  return nullptr;
}

void ObjFile::handleComdatSelection(
    COFFSymbolRef sym, COMDATType &selection, bool &prevailing,
    DefinedRegular *leader,
    const llvm::object::coff_aux_section_definition *def) {
  if (prevailing)
    return;
  // There's already an existing comdat for this symbol: `Leader`.
  // Use the comdats's selection field to determine if the new
  // symbol in `Sym` should be discarded, produce a duplicate symbol
  // error, etc.

  SectionChunk *leaderChunk = leader->getChunk();
  COMDATType leaderSelection = leaderChunk->selection;

  assert(leader->data && "Comdat leader without SectionChunk?");
  if (isa<BitcodeFile>(leader->file)) {
    // If the leader is only a LTO symbol, we don't know e.g. its final size
    // yet, so we can't do the full strict comdat selection checking yet.
    selection = leaderSelection = IMAGE_COMDAT_SELECT_ANY;
  }

  if ((selection == IMAGE_COMDAT_SELECT_ANY &&
       leaderSelection == IMAGE_COMDAT_SELECT_LARGEST) ||
      (selection == IMAGE_COMDAT_SELECT_LARGEST &&
       leaderSelection == IMAGE_COMDAT_SELECT_ANY)) {
    // cl.exe picks "any" for vftables when building with /GR- and
    // "largest" when building with /GR. To be able to link object files
    // compiled with each flag, "any" and "largest" are merged as "largest".
    leaderSelection = selection = IMAGE_COMDAT_SELECT_LARGEST;
  }

  // GCCs __declspec(selectany) doesn't actually pick "any" but "same size as".
  // Clang on the other hand picks "any". To be able to link two object files
  // with a __declspec(selectany) declaration, one compiled with gcc and the
  // other with clang, we merge them as proper "same size as"
  if (ctx.config.mingw && ((selection == IMAGE_COMDAT_SELECT_ANY &&
                            leaderSelection == IMAGE_COMDAT_SELECT_SAME_SIZE) ||
                           (selection == IMAGE_COMDAT_SELECT_SAME_SIZE &&
                            leaderSelection == IMAGE_COMDAT_SELECT_ANY))) {
    leaderSelection = selection = IMAGE_COMDAT_SELECT_SAME_SIZE;
  }

  // Other than that, comdat selections must match.  This is a bit more
  // strict than link.exe which allows merging "any" and "largest" if "any"
  // is the first symbol the linker sees, and it allows merging "largest"
  // with everything (!) if "largest" is the first symbol the linker sees.
  // Making this symmetric independent of which selection is seen first
  // seems better though.
  // (This behavior matches ModuleLinker::getComdatResult().)
  if (selection != leaderSelection) {
    log(("conflicting comdat type for " + toString(ctx, *leader) + ": " +
         Twine((int)leaderSelection) + " in " + toString(leader->getFile()) +
         " and " + Twine((int)selection) + " in " + toString(this))
            .str());
    ctx.symtab.reportDuplicate(leader, this);
    return;
  }

  switch (selection) {
  case IMAGE_COMDAT_SELECT_NODUPLICATES:
    ctx.symtab.reportDuplicate(leader, this);
    break;

  case IMAGE_COMDAT_SELECT_ANY:
    // Nothing to do.
    break;

  case IMAGE_COMDAT_SELECT_SAME_SIZE:
    if (leaderChunk->getSize() != getSection(sym)->SizeOfRawData) {
      if (!ctx.config.mingw) {
        ctx.symtab.reportDuplicate(leader, this);
      } else {
        const coff_aux_section_definition *leaderDef = nullptr;
        if (leaderChunk->file)
          leaderDef = findSectionDef(leaderChunk->file->getCOFFObj(),
                                     leaderChunk->getSectionNumber());
        if (!leaderDef || leaderDef->Length != def->Length)
          ctx.symtab.reportDuplicate(leader, this);
      }
    }
    break;

  case IMAGE_COMDAT_SELECT_EXACT_MATCH: {
    SectionChunk newChunk(this, getSection(sym));
    // link.exe only compares section contents here and doesn't complain
    // if the two comdat sections have e.g. different alignment.
    // Match that.
    if (leaderChunk->getContents() != newChunk.getContents())
      ctx.symtab.reportDuplicate(leader, this, &newChunk, sym.getValue());
    break;
  }

  case IMAGE_COMDAT_SELECT_ASSOCIATIVE:
    // createDefined() is never called for IMAGE_COMDAT_SELECT_ASSOCIATIVE.
    // (This means lld-link doesn't produce duplicate symbol errors for
    // associative comdats while link.exe does, but associate comdats
    // are never extern in practice.)
    llvm_unreachable("createDefined not called for associative comdats");

  case IMAGE_COMDAT_SELECT_LARGEST:
    if (leaderChunk->getSize() < getSection(sym)->SizeOfRawData) {
      // Replace the existing comdat symbol with the new one.
      StringRef name = check(coffObj->getSymbolName(sym));
      // FIXME: This is incorrect: With /opt:noref, the previous sections
      // make it into the final executable as well. Correct handling would
      // be to undo reading of the whole old section that's being replaced,
      // or doing one pass that determines what the final largest comdat
      // is for all IMAGE_COMDAT_SELECT_LARGEST comdats and then reading
      // only the largest one.
      replaceSymbol<DefinedRegular>(leader, this, name, /*IsCOMDAT*/ true,
                                    /*IsExternal*/ true, sym.getGeneric(),
                                    nullptr);
      prevailing = true;
    }
    break;

  case IMAGE_COMDAT_SELECT_NEWEST:
    llvm_unreachable("should have been rejected earlier");
  }
}

std::optional<Symbol *> ObjFile::createDefined(
    COFFSymbolRef sym,
    std::vector<const coff_aux_section_definition *> &comdatDefs,
    bool &prevailing) {
  prevailing = false;
  auto getName = [&]() { return check(coffObj->getSymbolName(sym)); };

  if (sym.isCommon()) {
    auto *c = make<CommonChunk>(sym);
    chunks.push_back(c);
    return ctx.symtab.addCommon(this, getName(), sym.getValue(),
                                sym.getGeneric(), c);
  }

  if (sym.isAbsolute()) {
    StringRef name = getName();

    if (name == "@feat.00")
      feat00Flags = sym.getValue();
    // Skip special symbols.
    if (ignoredSymbolName(name))
      return nullptr;

    if (sym.isExternal())
      return ctx.symtab.addAbsolute(name, sym);
    return make<DefinedAbsolute>(ctx, name, sym);
  }

  int32_t sectionNumber = sym.getSectionNumber();
  if (sectionNumber == llvm::COFF::IMAGE_SYM_DEBUG)
    return nullptr;

  if (llvm::COFF::isReservedSectionNumber(sectionNumber))
    fatal(toString(this) + ": " + getName() +
          " should not refer to special section " + Twine(sectionNumber));

  if ((uint32_t)sectionNumber >= sparseChunks.size())
    fatal(toString(this) + ": " + getName() +
          " should not refer to non-existent section " + Twine(sectionNumber));

  // Comdat handling.
  // A comdat symbol consists of two symbol table entries.
  // The first symbol entry has the name of the section (e.g. .text), fixed
  // values for the other fields, and one auxiliary record.
  // The second symbol entry has the name of the comdat symbol, called the
  // "comdat leader".
  // When this function is called for the first symbol entry of a comdat,
  // it sets comdatDefs and returns std::nullopt, and when it's called for the
  // second symbol entry it reads comdatDefs and then sets it back to nullptr.

  // Handle comdat leader.
  if (const coff_aux_section_definition *def = comdatDefs[sectionNumber]) {
    comdatDefs[sectionNumber] = nullptr;
    DefinedRegular *leader;

    if (sym.isExternal()) {
      std::tie(leader, prevailing) =
          ctx.symtab.addComdat(this, getName(), sym.getGeneric());
    } else {
      leader = make<DefinedRegular>(this, /*Name*/ "", /*IsCOMDAT*/ false,
                                    /*IsExternal*/ false, sym.getGeneric());
      prevailing = true;
    }

    if (def->Selection < (int)IMAGE_COMDAT_SELECT_NODUPLICATES ||
        // Intentionally ends at IMAGE_COMDAT_SELECT_LARGEST: link.exe
        // doesn't understand IMAGE_COMDAT_SELECT_NEWEST either.
        def->Selection > (int)IMAGE_COMDAT_SELECT_LARGEST) {
      fatal("unknown comdat type " + std::to_string((int)def->Selection) +
            " for " + getName() + " in " + toString(this));
    }
    COMDATType selection = (COMDATType)def->Selection;

    if (leader->isCOMDAT)
      handleComdatSelection(sym, selection, prevailing, leader, def);

    if (prevailing) {
      SectionChunk *c = readSection(sectionNumber, def, getName());
      sparseChunks[sectionNumber] = c;
      if (!c)
        return nullptr;
      c->sym = cast<DefinedRegular>(leader);
      c->selection = selection;
      cast<DefinedRegular>(leader)->data = &c->repl;
    } else {
      sparseChunks[sectionNumber] = nullptr;
    }
    return leader;
  }

  // Prepare to handle the comdat leader symbol by setting the section's
  // ComdatDefs pointer if we encounter a non-associative comdat.
  if (sparseChunks[sectionNumber] == pendingComdat) {
    if (const coff_aux_section_definition *def = sym.getSectionDefinition()) {
      if (def->Selection != IMAGE_COMDAT_SELECT_ASSOCIATIVE)
        comdatDefs[sectionNumber] = def;
    }
    return std::nullopt;
  }

  return createRegular(sym);
}

MachineTypes ObjFile::getMachineType() {
  if (coffObj)
    return static_cast<MachineTypes>(coffObj->getMachine());
  return IMAGE_FILE_MACHINE_UNKNOWN;
}

ArrayRef<uint8_t> ObjFile::getDebugSection(StringRef secName) {
  if (SectionChunk *sec = SectionChunk::findByName(debugChunks, secName))
    return sec->consumeDebugMagic();
  return {};
}

// OBJ files systematically store critical information in a .debug$S stream,
// even if the TU was compiled with no debug info. At least two records are
// always there. S_OBJNAME stores a 32-bit signature, which is loaded into the
// PCHSignature member. S_COMPILE3 stores compile-time cmd-line flags. This is
// currently used to initialize the hotPatchable member.
void ObjFile::initializeFlags() {
  ArrayRef<uint8_t> data = getDebugSection(".debug$S");
  if (data.empty())
    return;

  DebugSubsectionArray subsections;

  BinaryStreamReader reader(data, llvm::endianness::little);
  ExitOnError exitOnErr;
  exitOnErr(reader.readArray(subsections, data.size()));

  for (const DebugSubsectionRecord &ss : subsections) {
    if (ss.kind() != DebugSubsectionKind::Symbols)
      continue;

    unsigned offset = 0;

    // Only parse the first two records. We are only looking for S_OBJNAME
    // and S_COMPILE3, and they usually appear at the beginning of the
    // stream.
    for (unsigned i = 0; i < 2; ++i) {
      Expected<CVSymbol> sym = readSymbolFromStream(ss.getRecordData(), offset);
      if (!sym) {
        consumeError(sym.takeError());
        return;
      }
      if (sym->kind() == SymbolKind::S_COMPILE3) {
        auto cs =
            cantFail(SymbolDeserializer::deserializeAs<Compile3Sym>(sym.get()));
        hotPatchable =
            (cs.Flags & CompileSym3Flags::HotPatch) != CompileSym3Flags::None;
      }
      if (sym->kind() == SymbolKind::S_OBJNAME) {
        auto objName = cantFail(SymbolDeserializer::deserializeAs<ObjNameSym>(
            sym.get()));
        if (objName.Signature)
          pchSignature = objName.Signature;
      }
      offset += sym->length();
    }
  }
}

// Depending on the compilation flags, OBJs can refer to external files,
// necessary to merge this OBJ into the final PDB. We currently support two
// types of external files: Precomp/PCH OBJs, when compiling with /Yc and /Yu.
// And PDB type servers, when compiling with /Zi. This function extracts these
// dependencies and makes them available as a TpiSource interface (see
// DebugTypes.h). Both cases only happen with cl.exe: clang-cl produces regular
// output even with /Yc and /Yu and with /Zi.
void ObjFile::initializeDependencies() {
  if (!ctx.config.debug)
    return;

  bool isPCH = false;

  ArrayRef<uint8_t> data = getDebugSection(".debug$P");
  if (!data.empty())
    isPCH = true;
  else
    data = getDebugSection(".debug$T");

  // symbols but no types, make a plain, empty TpiSource anyway, because it
  // simplifies adding the symbols later.
  if (data.empty()) {
    if (!debugChunks.empty())
      debugTypesObj = makeTpiSource(ctx, this);
    return;
  }

  // Get the first type record. It will indicate if this object uses a type
  // server (/Zi) or a PCH file (/Yu).
  CVTypeArray types;
  BinaryStreamReader reader(data, llvm::endianness::little);
  cantFail(reader.readArray(types, reader.getLength()));
  CVTypeArray::Iterator firstType = types.begin();
  if (firstType == types.end())
    return;

  // Remember the .debug$T or .debug$P section.
  debugTypes = data;

  // This object file is a PCH file that others will depend on.
  if (isPCH) {
    debugTypesObj = makePrecompSource(ctx, this);
    return;
  }

  // This object file was compiled with /Zi. Enqueue the PDB dependency.
  if (firstType->kind() == LF_TYPESERVER2) {
    TypeServer2Record ts = cantFail(
        TypeDeserializer::deserializeAs<TypeServer2Record>(firstType->data()));
    debugTypesObj = makeUseTypeServerSource(ctx, this, ts);
    enqueuePdbFile(ts.getName(), this);
    return;
  }

  // This object was compiled with /Yu. It uses types from another object file
  // with a matching signature.
  if (firstType->kind() == LF_PRECOMP) {
    PrecompRecord precomp = cantFail(
        TypeDeserializer::deserializeAs<PrecompRecord>(firstType->data()));
    // We're better off trusting the LF_PRECOMP signature. In some cases the
    // S_OBJNAME record doesn't contain a valid PCH signature.
    if (precomp.Signature)
      pchSignature = precomp.Signature;
    debugTypesObj = makeUsePrecompSource(ctx, this, precomp);
    // Drop the LF_PRECOMP record from the input stream.
    debugTypes = debugTypes.drop_front(firstType->RecordData.size());
    return;
  }

  // This is a plain old object file.
  debugTypesObj = makeTpiSource(ctx, this);
}

// The casing of the PDB path stamped in the OBJ can differ from the actual path
// on disk. With this, we ensure to always use lowercase as a key for the
// pdbInputFileInstances map, at least on Windows.
static std::string normalizePdbPath(StringRef path) {
#if defined(_WIN32)
  return path.lower();
#else // LINUX
  return std::string(path);
#endif
}

// If existing, return the actual PDB path on disk.
static std::optional<std::string>
findPdbPath(StringRef pdbPath, ObjFile *dependentFile, StringRef outputPath) {
  // Ensure the file exists before anything else. In some cases, if the path
  // points to a removable device, Driver::enqueuePath() would fail with an
  // error (EAGAIN, "resource unavailable try again") which we want to skip
  // silently.
  if (llvm::sys::fs::exists(pdbPath))
    return normalizePdbPath(pdbPath);

  StringRef objPath = !dependentFile->parentName.empty()
                          ? dependentFile->parentName
                          : dependentFile->getName();

  // Currently, type server PDBs are only created by MSVC cl, which only runs
  // on Windows, so we can assume type server paths are Windows style.
  StringRef pdbName = sys::path::filename(pdbPath, sys::path::Style::windows);

  // Check if the PDB is in the same folder as the OBJ.
  SmallString<128> path;
  sys::path::append(path, sys::path::parent_path(objPath), pdbName);
  if (llvm::sys::fs::exists(path))
    return normalizePdbPath(path);

  // Check if the PDB is in the output folder.
  path.clear();
  sys::path::append(path, sys::path::parent_path(outputPath), pdbName);
  if (llvm::sys::fs::exists(path))
    return normalizePdbPath(path);

  return std::nullopt;
}

PDBInputFile::PDBInputFile(COFFLinkerContext &ctx, MemoryBufferRef m)
    : InputFile(ctx, PDBKind, m) {}

PDBInputFile::~PDBInputFile() = default;

PDBInputFile *PDBInputFile::findFromRecordPath(const COFFLinkerContext &ctx,
                                               StringRef path,
                                               ObjFile *fromFile) {
  auto p = findPdbPath(path.str(), fromFile, ctx.config.outputFile);
  if (!p)
    return nullptr;
  auto it = ctx.pdbInputFileInstances.find(*p);
  if (it != ctx.pdbInputFileInstances.end())
    return it->second;
  return nullptr;
}

void PDBInputFile::parse() {
  ctx.pdbInputFileInstances[mb.getBufferIdentifier().str()] = this;

  std::unique_ptr<pdb::IPDBSession> thisSession;
  Error E = pdb::NativeSession::createFromPdb(
      MemoryBuffer::getMemBuffer(mb, false), thisSession);
  if (E) {
    loadErrorStr.emplace(toString(std::move(E)));
    return; // fail silently at this point - the error will be handled later,
            // when merging the debug type stream
  }

  session.reset(static_cast<pdb::NativeSession *>(thisSession.release()));

  pdb::PDBFile &pdbFile = session->getPDBFile();
  auto expectedInfo = pdbFile.getPDBInfoStream();
  // All PDB Files should have an Info stream.
  if (!expectedInfo) {
    loadErrorStr.emplace(toString(expectedInfo.takeError()));
    return;
  }
  debugTypesObj = makeTypeServerSource(ctx, this);
}

// Used only for DWARF debug info, which is not common (except in MinGW
// environments). This returns an optional pair of file name and line
// number for where the variable was defined.
std::optional<std::pair<StringRef, uint32_t>>
ObjFile::getVariableLocation(StringRef var) {
  if (!dwarf) {
    dwarf = make<DWARFCache>(DWARFContext::create(*getCOFFObj()));
    if (!dwarf)
      return std::nullopt;
  }
  if (ctx.config.machine == I386)
    var.consume_front("_");
  std::optional<std::pair<std::string, unsigned>> ret =
      dwarf->getVariableLoc(var);
  if (!ret)
    return std::nullopt;
  return std::make_pair(saver().save(ret->first), ret->second);
}

// Used only for DWARF debug info, which is not common (except in MinGW
// environments).
std::optional<DILineInfo> ObjFile::getDILineInfo(uint32_t offset,
                                                 uint32_t sectionIndex) {
  if (!dwarf) {
    dwarf = make<DWARFCache>(DWARFContext::create(*getCOFFObj()));
    if (!dwarf)
      return std::nullopt;
  }

  return dwarf->getDILineInfo(offset, sectionIndex);
}

void ObjFile::enqueuePdbFile(StringRef path, ObjFile *fromFile) {
  auto p = findPdbPath(path.str(), fromFile, ctx.config.outputFile);
  if (!p)
    return;
  auto it = ctx.pdbInputFileInstances.emplace(*p, nullptr);
  if (!it.second)
    return; // already scheduled for load
  ctx.driver.enqueuePDB(*p);
}

ImportFile::ImportFile(COFFLinkerContext &ctx, MemoryBufferRef m)
    : InputFile(ctx, ImportKind, m), live(!ctx.config.doGC), thunkLive(live) {}

void ImportFile::parse() {
  const auto *hdr =
      reinterpret_cast<const coff_import_header *>(mb.getBufferStart());

  // Check if the total size is valid.
  if (mb.getBufferSize() < sizeof(*hdr) ||
      mb.getBufferSize() != sizeof(*hdr) + hdr->SizeOfData)
    fatal("broken import library");

  // Read names and create an __imp_ symbol.
  StringRef buf = mb.getBuffer().substr(sizeof(*hdr));
  StringRef name = saver().save(buf.split('\0').first);
  StringRef impName = saver().save("__imp_" + name);
  buf = buf.substr(name.size() + 1);
  dllName = buf.split('\0').first;
  StringRef extName;
  switch (hdr->getNameType()) {
  case IMPORT_ORDINAL:
    extName = "";
    break;
  case IMPORT_NAME:
    extName = name;
    break;
  case IMPORT_NAME_NOPREFIX:
    extName = ltrim1(name, "?@_");
    break;
  case IMPORT_NAME_UNDECORATE:
    extName = ltrim1(name, "?@_");
    extName = extName.substr(0, extName.find('@'));
    break;
  case IMPORT_NAME_EXPORTAS:
    extName = buf.substr(dllName.size() + 1).split('\0').first;
    break;
  }

  this->hdr = hdr;
  externalName = extName;

  impSym = ctx.symtab.addImportData(impName, this);
  // If this was a duplicate, we logged an error but may continue;
  // in this case, impSym is nullptr.
  if (!impSym)
    return;

  if (hdr->getType() == llvm::COFF::IMPORT_CONST)
    static_cast<void>(ctx.symtab.addImportData(name, this));

  // If type is function, we need to create a thunk which jump to an
  // address pointed by the __imp_ symbol. (This allows you to call
  // DLL functions just like regular non-DLL functions.)
  if (hdr->getType() == llvm::COFF::IMPORT_CODE)
    thunkSym = ctx.symtab.addImportThunk(
        name, cast_or_null<DefinedImportData>(impSym), hdr->Machine);
}

BitcodeFile::BitcodeFile(COFFLinkerContext &ctx, MemoryBufferRef mb,
                         StringRef archiveName, uint64_t offsetInArchive,
                         bool lazy)
    : InputFile(ctx, BitcodeKind, mb, lazy) {
  std::string path = mb.getBufferIdentifier().str();
  if (ctx.config.thinLTOIndexOnly)
    path = replaceThinLTOSuffix(mb.getBufferIdentifier(),
                                ctx.config.thinLTOObjectSuffixReplace.first,
                                ctx.config.thinLTOObjectSuffixReplace.second);

  // ThinLTO assumes that all MemoryBufferRefs given to it have a unique
  // name. If two archives define two members with the same name, this
  // causes a collision which result in only one of the objects being taken
  // into consideration at LTO time (which very likely causes undefined
  // symbols later in the link stage). So we append file offset to make
  // filename unique.
  MemoryBufferRef mbref(mb.getBuffer(),
                        saver().save(archiveName.empty()
                                         ? path
                                         : archiveName +
                                               sys::path::filename(path) +
                                               utostr(offsetInArchive)));

  obj = check(lto::InputFile::create(mbref));
}

BitcodeFile::~BitcodeFile() = default;

void BitcodeFile::parse() {
  llvm::StringSaver &saver = lld::saver();

  std::vector<std::pair<Symbol *, bool>> comdat(obj->getComdatTable().size());
  for (size_t i = 0; i != obj->getComdatTable().size(); ++i)
    // FIXME: Check nodeduplicate
    comdat[i] =
        ctx.symtab.addComdat(this, saver.save(obj->getComdatTable()[i].first));
  for (const lto::InputFile::Symbol &objSym : obj->symbols()) {
    StringRef symName = saver.save(objSym.getName());
    int comdatIndex = objSym.getComdatIndex();
    Symbol *sym;
    SectionChunk *fakeSC = nullptr;
    if (objSym.isExecutable())
      fakeSC = &ctx.ltoTextSectionChunk.chunk;
    else
      fakeSC = &ctx.ltoDataSectionChunk.chunk;
    if (objSym.isUndefined()) {
      sym = ctx.symtab.addUndefined(symName, this, false);
      if (objSym.isWeak())
        sym->deferUndefined = true;
      // If one LTO object file references (i.e. has an undefined reference to)
      // a symbol with an __imp_ prefix, the LTO compilation itself sees it
      // as unprefixed but with a dllimport attribute instead, and doesn't
      // understand the relation to a concrete IR symbol with the __imp_ prefix.
      //
      // For such cases, mark the symbol as used in a regular object (i.e. the
      // symbol must be retained) so that the linker can associate the
      // references in the end. If the symbol is defined in an import library
      // or in a regular object file, this has no effect, but if it is defined
      // in another LTO object file, this makes sure it is kept, to fulfill
      // the reference when linking the output of the LTO compilation.
      if (symName.starts_with("__imp_"))
        sym->isUsedInRegularObj = true;
    } else if (objSym.isCommon()) {
      sym = ctx.symtab.addCommon(this, symName, objSym.getCommonSize());
    } else if (objSym.isWeak() && objSym.isIndirect()) {
      // Weak external.
      sym = ctx.symtab.addUndefined(symName, this, true);
      std::string fallback = std::string(objSym.getCOFFWeakExternalFallback());
      Symbol *alias = ctx.symtab.addUndefined(saver.save(fallback));
      checkAndSetWeakAlias(ctx, this, sym, alias);
    } else if (comdatIndex != -1) {
      if (symName == obj->getComdatTable()[comdatIndex].first) {
        sym = comdat[comdatIndex].first;
        if (cast<DefinedRegular>(sym)->data == nullptr)
          cast<DefinedRegular>(sym)->data = &fakeSC->repl;
      } else if (comdat[comdatIndex].second) {
        sym = ctx.symtab.addRegular(this, symName, nullptr, fakeSC);
      } else {
        sym = ctx.symtab.addUndefined(symName, this, false);
      }
    } else {
      sym = ctx.symtab.addRegular(this, symName, nullptr, fakeSC, 0,
                                  objSym.isWeak());
    }
    symbols.push_back(sym);
    if (objSym.isUsed())
      ctx.config.gcroot.push_back(sym);
  }
  directives = saver.save(obj->getCOFFLinkerOpts());
}

void BitcodeFile::parseLazy() {
  for (const lto::InputFile::Symbol &sym : obj->symbols())
    if (!sym.isUndefined())
      ctx.symtab.addLazyObject(this, sym.getName());
}

MachineTypes BitcodeFile::getMachineType() {
  switch (Triple(obj->getTargetTriple()).getArch()) {
  case Triple::x86_64:
    return AMD64;
  case Triple::x86:
    return I386;
  case Triple::arm:
  case Triple::thumb:
    return ARMNT;
  case Triple::aarch64:
    return ARM64;
  default:
    return IMAGE_FILE_MACHINE_UNKNOWN;
  }
}

std::string lld::coff::replaceThinLTOSuffix(StringRef path, StringRef suffix,
                                            StringRef repl) {
  if (path.consume_back(suffix))
    return (path + repl).str();
  return std::string(path);
}

static bool isRVACode(COFFObjectFile *coffObj, uint64_t rva, InputFile *file) {
  for (size_t i = 1, e = coffObj->getNumberOfSections(); i <= e; i++) {
    const coff_section *sec = CHECK(coffObj->getSection(i), file);
    if (rva >= sec->VirtualAddress &&
        rva <= sec->VirtualAddress + sec->VirtualSize) {
      return (sec->Characteristics & COFF::IMAGE_SCN_CNT_CODE) != 0;
    }
  }
  return false;
}

void DLLFile::parse() {
  // Parse a memory buffer as a PE-COFF executable.
  std::unique_ptr<Binary> bin = CHECK(createBinary(mb), this);

  if (auto *obj = dyn_cast<COFFObjectFile>(bin.get())) {
    bin.release();
    coffObj.reset(obj);
  } else {
    error(toString(this) + " is not a COFF file");
    return;
  }

  if (!coffObj->getPE32Header() && !coffObj->getPE32PlusHeader()) {
    error(toString(this) + " is not a PE-COFF executable");
    return;
  }

  for (const auto &exp : coffObj->export_directories()) {
    StringRef dllName, symbolName;
    uint32_t exportRVA;
    checkError(exp.getDllName(dllName));
    checkError(exp.getSymbolName(symbolName));
    checkError(exp.getExportRVA(exportRVA));

    if (symbolName.empty())
      continue;

    bool code = isRVACode(coffObj.get(), exportRVA, this);

    Symbol *s = make<Symbol>();
    s->dllName = dllName;
    s->symbolName = symbolName;
    s->importType = code ? ImportType::IMPORT_CODE : ImportType::IMPORT_DATA;
    s->nameType = ImportNameType::IMPORT_NAME;

    if (coffObj->getMachine() == I386) {
      s->symbolName = symbolName = saver().save("_" + symbolName);
      s->nameType = ImportNameType::IMPORT_NAME_NOPREFIX;
    }

    StringRef impName = saver().save("__imp_" + symbolName);
    ctx.symtab.addLazyDLLSymbol(this, s, impName);
    if (code)
      ctx.symtab.addLazyDLLSymbol(this, s, symbolName);
  }
}

MachineTypes DLLFile::getMachineType() {
  if (coffObj)
    return static_cast<MachineTypes>(coffObj->getMachine());
  return IMAGE_FILE_MACHINE_UNKNOWN;
}

void DLLFile::makeImport(DLLFile::Symbol *s) {
  if (!seen.insert(s->symbolName).second)
    return;

  size_t impSize = s->dllName.size() + s->symbolName.size() + 2; // +2 for NULs
  size_t size = sizeof(coff_import_header) + impSize;
  char *buf = bAlloc().Allocate<char>(size);
  memset(buf, 0, size);
  char *p = buf;
  auto *imp = reinterpret_cast<coff_import_header *>(p);
  p += sizeof(*imp);
  imp->Sig2 = 0xFFFF;
  imp->Machine = coffObj->getMachine();
  imp->SizeOfData = impSize;
  imp->OrdinalHint = 0; // Only linking by name
  imp->TypeInfo = (s->nameType << 2) | s->importType;

  // Write symbol name and DLL name.
  memcpy(p, s->symbolName.data(), s->symbolName.size());
  p += s->symbolName.size() + 1;
  memcpy(p, s->dllName.data(), s->dllName.size());
  MemoryBufferRef mbref = MemoryBufferRef(StringRef(buf, size), s->dllName);
  ImportFile *impFile = make<ImportFile>(ctx, mbref);
  ctx.symtab.addFile(impFile);
}
