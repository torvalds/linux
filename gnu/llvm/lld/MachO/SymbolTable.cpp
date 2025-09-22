//===- SymbolTable.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolTable.h"
#include "ConcatOutputSection.h"
#include "Config.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/Demangle/Demangle.h"

using namespace llvm;
using namespace lld;
using namespace lld::macho;

Symbol *SymbolTable::find(CachedHashStringRef cachedName) {
  auto it = symMap.find(cachedName);
  if (it == symMap.end())
    return nullptr;
  return symVector[it->second];
}

std::pair<Symbol *, bool> SymbolTable::insert(StringRef name,
                                              const InputFile *file) {
  auto p = symMap.insert({CachedHashStringRef(name), (int)symVector.size()});

  Symbol *sym;
  if (!p.second) {
    // Name already present in the symbol table.
    sym = symVector[p.first->second];
  } else {
    // Name is a new symbol.
    sym = reinterpret_cast<Symbol *>(make<SymbolUnion>());
    symVector.push_back(sym);
  }

  sym->isUsedInRegularObj |= !file || isa<ObjFile>(file);
  return {sym, p.second};
}

namespace {
struct DuplicateSymbolDiag {
  // Pair containing source location and source file
  const std::pair<std::string, std::string> src1;
  const std::pair<std::string, std::string> src2;
  const Symbol *sym;

  DuplicateSymbolDiag(const std::pair<std::string, std::string> src1,
                      const std::pair<std::string, std::string> src2,
                      const Symbol *sym)
      : src1(src1), src2(src2), sym(sym) {}
};
SmallVector<DuplicateSymbolDiag> dupSymDiags;
} // namespace

// Move symbols at \p fromOff in \p fromIsec into \p toIsec, unless that symbol
// is \p skip.
static void transplantSymbolsAtOffset(InputSection *fromIsec,
                                      InputSection *toIsec, Defined *skip,
                                      uint64_t fromOff, uint64_t toOff) {
  // Ensure the symbols will still be in address order after our insertions.
  auto insertIt = llvm::upper_bound(toIsec->symbols, toOff,
                                    [](uint64_t off, const Symbol *s) {
                                      return cast<Defined>(s)->value < off;
                                    });
  llvm::erase_if(fromIsec->symbols, [&](Symbol *s) {
    auto *d = cast<Defined>(s);
    if (d->value != fromOff)
      return false;
    if (d != skip) {
      // This repeated insertion will be quadratic unless insertIt is the end
      // iterator. However, that is typically the case for files that have
      // .subsections_via_symbols set.
      insertIt = toIsec->symbols.insert(insertIt, d);
      d->originalIsec = toIsec;
      d->value = toOff;
      // We don't want to have more than one unwindEntry at a given address, so
      // drop the redundant ones. We We can safely drop the unwindEntries of
      // the symbols in fromIsec since we will be adding another unwindEntry as
      // we finish parsing toIsec's file. (We can assume that toIsec has its
      // own unwindEntry because of the ODR.)
      d->originalUnwindEntry = nullptr;
    }
    return true;
  });
}

Defined *SymbolTable::addDefined(StringRef name, InputFile *file,
                                 InputSection *isec, uint64_t value,
                                 uint64_t size, bool isWeakDef,
                                 bool isPrivateExtern,
                                 bool isReferencedDynamically, bool noDeadStrip,
                                 bool isWeakDefCanBeHidden) {
  bool overridesWeakDef = false;
  auto [s, wasInserted] = insert(name, file);

  assert(!file || !isa<BitcodeFile>(file) || !isec);

  if (!wasInserted) {
    if (auto *defined = dyn_cast<Defined>(s)) {
      if (isWeakDef) {
        // See further comment in createDefined() in InputFiles.cpp
        if (defined->isWeakDef()) {
          defined->privateExtern &= isPrivateExtern;
          defined->weakDefCanBeHidden &= isWeakDefCanBeHidden;
          defined->referencedDynamically |= isReferencedDynamically;
          defined->noDeadStrip |= noDeadStrip;
        }
        if (auto concatIsec = dyn_cast_or_null<ConcatInputSection>(isec)) {
          concatIsec->wasCoalesced = true;
          // Any local symbols that alias the coalesced symbol should be moved
          // into the prevailing section. Note that we have sorted the symbols
          // in ObjFile::parseSymbols() such that extern weak symbols appear
          // last, so we don't need to worry about subsequent symbols being
          // added to an already-coalesced section.
          if (defined->isec())
            transplantSymbolsAtOffset(concatIsec, defined->isec(),
                                      /*skip=*/nullptr, value, defined->value);
        }
        return defined;
      }

      if (defined->isWeakDef()) {
        if (auto concatIsec =
                dyn_cast_or_null<ConcatInputSection>(defined->isec())) {
          concatIsec->wasCoalesced = true;
          if (isec)
            transplantSymbolsAtOffset(concatIsec, isec, defined, defined->value,
                                      value);
        }
      } else {
        std::string srcLoc1 = defined->getSourceLocation();
        std::string srcLoc2 = isec ? isec->getSourceLocation(value) : "";
        std::string srcFile1 = toString(defined->getFile());
        std::string srcFile2 = toString(file);

        dupSymDiags.push_back({make_pair(srcLoc1, srcFile1),
                               make_pair(srcLoc2, srcFile2), defined});
      }

    } else if (auto *dysym = dyn_cast<DylibSymbol>(s)) {
      overridesWeakDef = !isWeakDef && dysym->isWeakDef();
      dysym->unreference();
    } else if (auto *undef = dyn_cast<Undefined>(s)) {
      if (undef->wasBitcodeSymbol) {
        auto objFile = dyn_cast<ObjFile>(file);
        if (!objFile) {
          // The file must be a native object file, as opposed to potentially
          // being another bitcode file. A situation arises when some symbols
          // are defined thru `module asm` and thus they are not present in the
          // bitcode's symbol table. Consider bitcode modules `A`, `B`, and `C`.
          // LTO compiles only `A` and `C`, since there's no explicit symbol
          // reference to `B` other than a symbol from `A` via `module asm`.
          // After LTO is finished, the missing symbol now appears in the
          // resulting object file for `A`, which  prematurely resolves another
          // prevailing symbol with `B` that hasn't been compiled, instead of
          // the resulting object for `C`. Consequently, an incorrect
          // relocation is generated for the prevailing symbol.
          assert(isa<BitcodeFile>(file) && "Bitcode file is expected.");
          std::string message =
              "The pending prevailing symbol(" + name.str() +
              ") in the bitcode file(" + toString(undef->getFile()) +
              ") is overridden by a non-native object (from bitcode): " +
              toString(file);
          error(message);
        } else if (!objFile->builtFromBitcode) {
          // Ideally, this should be an object file compiled from a bitcode
          // file. However, this might not hold true if a LC linker option is
          // used. In case LTO internalizes a prevailing hidden weak symbol,
          // there's a situation where an unresolved prevailing symbol might be
          // linked with the corresponding one from a native library, which is
          // loaded later after LTO. Although this could potentially result in
          // an ODR violation, we choose to permit this scenario as a warning.
          std::string message = "The pending prevailing symbol(" + name.str() +
                                ") in the bitcode file(" +
                                toString(undef->getFile()) +
                                ") is overridden by a post-processed native "
                                "object (from native archive): " +
                                toString(file);
          warn(message);
        } else {
          // Preserve the original bitcode file name (instead of using the
          // object file name).
          file = undef->getFile();
        }
      }
    }
    // Defined symbols take priority over other types of symbols, so in case
    // of a name conflict, we fall through to the replaceSymbol() call below.
  }

  // With -flat_namespace, all extern symbols in dylibs are interposable.
  // FIXME: Add support for `-interposable` (PR53680).
  bool interposable = config->namespaceKind == NamespaceKind::flat &&
                      config->outputType != MachO::MH_EXECUTE &&
                      !isPrivateExtern;
  Defined *defined = replaceSymbol<Defined>(
      s, name, file, isec, value, size, isWeakDef, /*isExternal=*/true,
      isPrivateExtern, /*includeInSymtab=*/true, isReferencedDynamically,
      noDeadStrip, overridesWeakDef, isWeakDefCanBeHidden, interposable);
  return defined;
}

Defined *SymbolTable::aliasDefined(Defined *src, StringRef target,
                                   InputFile *newFile, bool makePrivateExtern) {
  bool isPrivateExtern = makePrivateExtern || src->privateExtern;
  return addDefined(target, newFile, src->isec(), src->value, src->size,
                    src->isWeakDef(), isPrivateExtern,
                    src->referencedDynamically, src->noDeadStrip,
                    src->weakDefCanBeHidden);
}

Symbol *SymbolTable::addUndefined(StringRef name, InputFile *file,
                                  bool isWeakRef) {
  auto [s, wasInserted] = insert(name, file);

  RefState refState = isWeakRef ? RefState::Weak : RefState::Strong;

  if (wasInserted)
    replaceSymbol<Undefined>(s, name, file, refState,
                             /*wasBitcodeSymbol=*/false);
  else if (auto *lazy = dyn_cast<LazyArchive>(s))
    lazy->fetchArchiveMember();
  else if (isa<LazyObject>(s))
    extract(*s->getFile(), s->getName());
  else if (auto *dynsym = dyn_cast<DylibSymbol>(s))
    dynsym->reference(refState);
  else if (auto *undefined = dyn_cast<Undefined>(s))
    undefined->refState = std::max(undefined->refState, refState);
  return s;
}

Symbol *SymbolTable::addCommon(StringRef name, InputFile *file, uint64_t size,
                               uint32_t align, bool isPrivateExtern) {
  auto [s, wasInserted] = insert(name, file);

  if (!wasInserted) {
    if (auto *common = dyn_cast<CommonSymbol>(s)) {
      if (size < common->size)
        return s;
    } else if (isa<Defined>(s)) {
      return s;
    }
    // Common symbols take priority over all non-Defined symbols, so in case of
    // a name conflict, we fall through to the replaceSymbol() call below.
  }

  replaceSymbol<CommonSymbol>(s, name, file, size, align, isPrivateExtern);
  return s;
}

Symbol *SymbolTable::addDylib(StringRef name, DylibFile *file, bool isWeakDef,
                              bool isTlv) {
  auto [s, wasInserted] = insert(name, file);

  RefState refState = RefState::Unreferenced;
  if (!wasInserted) {
    if (auto *defined = dyn_cast<Defined>(s)) {
      if (isWeakDef && !defined->isWeakDef())
        defined->overridesWeakDef = true;
    } else if (auto *undefined = dyn_cast<Undefined>(s)) {
      refState = undefined->refState;
    } else if (auto *dysym = dyn_cast<DylibSymbol>(s)) {
      refState = dysym->getRefState();
    }
  }

  bool isDynamicLookup = file == nullptr;
  if (wasInserted || isa<Undefined>(s) ||
      (isa<DylibSymbol>(s) &&
       ((!isWeakDef && s->isWeakDef()) ||
        (!isDynamicLookup && cast<DylibSymbol>(s)->isDynamicLookup())))) {
    if (auto *dynsym = dyn_cast<DylibSymbol>(s))
      dynsym->unreference();
    replaceSymbol<DylibSymbol>(s, file, name, isWeakDef, refState, isTlv);
  }

  return s;
}

Symbol *SymbolTable::addDynamicLookup(StringRef name) {
  return addDylib(name, /*file=*/nullptr, /*isWeakDef=*/false, /*isTlv=*/false);
}

Symbol *SymbolTable::addLazyArchive(StringRef name, ArchiveFile *file,
                                    const object::Archive::Symbol &sym) {
  auto [s, wasInserted] = insert(name, file);

  if (wasInserted) {
    replaceSymbol<LazyArchive>(s, file, sym);
  } else if (isa<Undefined>(s)) {
    file->fetch(sym);
  } else if (auto *dysym = dyn_cast<DylibSymbol>(s)) {
    if (dysym->isWeakDef()) {
      if (dysym->getRefState() != RefState::Unreferenced)
        file->fetch(sym);
      else
        replaceSymbol<LazyArchive>(s, file, sym);
    }
  }
  return s;
}

Symbol *SymbolTable::addLazyObject(StringRef name, InputFile &file) {
  auto [s, wasInserted] = insert(name, &file);

  if (wasInserted) {
    replaceSymbol<LazyObject>(s, file, name);
  } else if (isa<Undefined>(s)) {
    extract(file, name);
  } else if (auto *dysym = dyn_cast<DylibSymbol>(s)) {
    if (dysym->isWeakDef()) {
      if (dysym->getRefState() != RefState::Unreferenced)
        extract(file, name);
      else
        replaceSymbol<LazyObject>(s, file, name);
    }
  }
  return s;
}

Defined *SymbolTable::addSynthetic(StringRef name, InputSection *isec,
                                   uint64_t value, bool isPrivateExtern,
                                   bool includeInSymtab,
                                   bool referencedDynamically) {
  assert(!isec || !isec->getFile()); // See makeSyntheticInputSection().
  Defined *s = addDefined(name, /*file=*/nullptr, isec, value, /*size=*/0,
                          /*isWeakDef=*/false, isPrivateExtern,
                          referencedDynamically, /*noDeadStrip=*/false,
                          /*isWeakDefCanBeHidden=*/false);
  s->includeInSymtab = includeInSymtab;
  return s;
}

enum class Boundary {
  Start,
  End,
};

static Defined *createBoundarySymbol(const Undefined &sym) {
  return symtab->addSynthetic(
      sym.getName(), /*isec=*/nullptr, /*value=*/-1, /*isPrivateExtern=*/true,
      /*includeInSymtab=*/false, /*referencedDynamically=*/false);
}

static void handleSectionBoundarySymbol(const Undefined &sym, StringRef segSect,
                                        Boundary which) {
  auto [segName, sectName] = segSect.split('$');

  // Attach the symbol to any InputSection that will end up in the right
  // OutputSection -- it doesn't matter which one we pick.
  // Don't bother looking through inputSections for a matching
  // ConcatInputSection -- we need to create ConcatInputSection for
  // non-existing sections anyways, and that codepath works even if we should
  // already have a ConcatInputSection with the right name.

  OutputSection *osec = nullptr;
  // This looks for __TEXT,__cstring etc.
  for (SyntheticSection *ssec : syntheticSections)
    if (ssec->segname == segName && ssec->name == sectName) {
      osec = ssec->isec->parent;
      break;
    }

  if (!osec) {
    ConcatInputSection *isec = makeSyntheticInputSection(segName, sectName);

    // This runs after markLive() and is only called for Undefineds that are
    // live. Marking the isec live ensures an OutputSection is created that the
    // start/end symbol can refer to.
    assert(sym.isLive());
    assert(isec->live);

    // This runs after gatherInputSections(), so need to explicitly set parent
    // and add to inputSections.
    osec = isec->parent = ConcatOutputSection::getOrCreateForInput(isec);
    inputSections.push_back(isec);
  }

  if (which == Boundary::Start)
    osec->sectionStartSymbols.push_back(createBoundarySymbol(sym));
  else
    osec->sectionEndSymbols.push_back(createBoundarySymbol(sym));
}

static void handleSegmentBoundarySymbol(const Undefined &sym, StringRef segName,
                                        Boundary which) {
  OutputSegment *seg = getOrCreateOutputSegment(segName);
  if (which == Boundary::Start)
    seg->segmentStartSymbols.push_back(createBoundarySymbol(sym));
  else
    seg->segmentEndSymbols.push_back(createBoundarySymbol(sym));
}

// Try to find a definition for an undefined symbol.
// Returns true if a definition was found and no diagnostics are needed.
static bool recoverFromUndefinedSymbol(const Undefined &sym) {
  // Handle start/end symbols.
  StringRef name = sym.getName();
  if (name.consume_front("section$start$")) {
    handleSectionBoundarySymbol(sym, name, Boundary::Start);
    return true;
  }
  if (name.consume_front("section$end$")) {
    handleSectionBoundarySymbol(sym, name, Boundary::End);
    return true;
  }
  if (name.consume_front("segment$start$")) {
    handleSegmentBoundarySymbol(sym, name, Boundary::Start);
    return true;
  }
  if (name.consume_front("segment$end$")) {
    handleSegmentBoundarySymbol(sym, name, Boundary::End);
    return true;
  }

  // Leave dtrace symbols, since we will handle them when we do the relocation
  if (name.starts_with("___dtrace_"))
    return true;

  // Handle -U.
  if (config->explicitDynamicLookups.count(sym.getName())) {
    symtab->addDynamicLookup(sym.getName());
    return true;
  }

  // Handle -undefined.
  if (config->undefinedSymbolTreatment ==
          UndefinedSymbolTreatment::dynamic_lookup ||
      config->undefinedSymbolTreatment == UndefinedSymbolTreatment::suppress) {
    symtab->addDynamicLookup(sym.getName());
    return true;
  }

  // We do not return true here, as we still need to print diagnostics.
  if (config->undefinedSymbolTreatment == UndefinedSymbolTreatment::warning)
    symtab->addDynamicLookup(sym.getName());

  return false;
}

namespace {
struct UndefinedDiag {
  struct SectionAndOffset {
    const InputSection *isec;
    uint64_t offset;
  };

  std::vector<SectionAndOffset> codeReferences;
  std::vector<std::string> otherReferences;
};

MapVector<const Undefined *, UndefinedDiag> undefs;
} // namespace

void macho::reportPendingDuplicateSymbols() {
  for (const auto &duplicate : dupSymDiags) {
    if (!config->deadStripDuplicates || duplicate.sym->isLive()) {
      std::string message =
          "duplicate symbol: " + toString(*duplicate.sym) + "\n>>> defined in ";
      if (!duplicate.src1.first.empty())
        message += duplicate.src1.first + "\n>>>            ";
      message += duplicate.src1.second + "\n>>> defined in ";
      if (!duplicate.src2.first.empty())
        message += duplicate.src2.first + "\n>>>            ";
      error(message + duplicate.src2.second);
    }
  }
}

// Check whether the definition name def is a mangled function name that matches
// the reference name ref.
static bool canSuggestExternCForCXX(StringRef ref, StringRef def) {
  llvm::ItaniumPartialDemangler d;
  std::string name = def.str();
  if (d.partialDemangle(name.c_str()))
    return false;
  char *buf = d.getFunctionName(nullptr, nullptr);
  if (!buf)
    return false;
  bool ret = ref == buf;
  free(buf);
  return ret;
}

// Suggest an alternative spelling of an "undefined symbol" diagnostic. Returns
// the suggested symbol, which is either in the symbol table, or in the same
// file of sym.
static const Symbol *getAlternativeSpelling(const Undefined &sym,
                                            std::string &preHint,
                                            std::string &postHint) {
  DenseMap<StringRef, const Symbol *> map;
  if (sym.getFile() && sym.getFile()->kind() == InputFile::ObjKind) {
    // Build a map of local defined symbols.
    for (const Symbol *s : sym.getFile()->symbols)
      if (auto *defined = dyn_cast_or_null<Defined>(s))
        if (!defined->isExternal())
          map.try_emplace(s->getName(), s);
  }

  auto suggest = [&](StringRef newName) -> const Symbol * {
    // If defined locally.
    if (const Symbol *s = map.lookup(newName))
      return s;

    // If in the symbol table and not undefined.
    if (const Symbol *s = symtab->find(newName))
      if (dyn_cast<Undefined>(s) == nullptr)
        return s;

    return nullptr;
  };

  // This loop enumerates all strings of Levenshtein distance 1 as typo
  // correction candidates and suggests the one that exists as a non-undefined
  // symbol.
  StringRef name = sym.getName();
  for (size_t i = 0, e = name.size(); i != e + 1; ++i) {
    // Insert a character before name[i].
    std::string newName = (name.substr(0, i) + "0" + name.substr(i)).str();
    for (char c = '0'; c <= 'z'; ++c) {
      newName[i] = c;
      if (const Symbol *s = suggest(newName))
        return s;
    }
    if (i == e)
      break;

    // Substitute name[i].
    newName = std::string(name);
    for (char c = '0'; c <= 'z'; ++c) {
      newName[i] = c;
      if (const Symbol *s = suggest(newName))
        return s;
    }

    // Transpose name[i] and name[i+1]. This is of edit distance 2 but it is
    // common.
    if (i + 1 < e) {
      newName[i] = name[i + 1];
      newName[i + 1] = name[i];
      if (const Symbol *s = suggest(newName))
        return s;
    }

    // Delete name[i].
    newName = (name.substr(0, i) + name.substr(i + 1)).str();
    if (const Symbol *s = suggest(newName))
      return s;
  }

  // Case mismatch, e.g. Foo vs FOO.
  for (auto &it : map)
    if (name.equals_insensitive(it.first))
      return it.second;
  for (Symbol *sym : symtab->getSymbols())
    if (dyn_cast<Undefined>(sym) == nullptr &&
        name.equals_insensitive(sym->getName()))
      return sym;

  // The reference may be a mangled name while the definition is not. Suggest a
  // missing extern "C".
  if (name.starts_with("__Z")) {
    std::string buf = name.str();
    llvm::ItaniumPartialDemangler d;
    if (!d.partialDemangle(buf.c_str()))
      if (char *buf = d.getFunctionName(nullptr, nullptr)) {
        const Symbol *s = suggest((Twine("_") + buf).str());
        free(buf);
        if (s) {
          preHint = ": extern \"C\" ";
          return s;
        }
      }
  } else {
    StringRef nameWithoutUnderscore = name;
    nameWithoutUnderscore.consume_front("_");
    const Symbol *s = nullptr;
    for (auto &it : map)
      if (canSuggestExternCForCXX(nameWithoutUnderscore, it.first)) {
        s = it.second;
        break;
      }
    if (!s)
      for (Symbol *sym : symtab->getSymbols())
        if (canSuggestExternCForCXX(nameWithoutUnderscore, sym->getName())) {
          s = sym;
          break;
        }
    if (s) {
      preHint = " to declare ";
      postHint = " as extern \"C\"?";
      return s;
    }
  }

  return nullptr;
}

static void reportUndefinedSymbol(const Undefined &sym,
                                  const UndefinedDiag &locations,
                                  bool correctSpelling) {
  std::string message = "undefined symbol";
  if (config->archMultiple)
    message += (" for arch " + getArchitectureName(config->arch())).str();
  message += ": " + toString(sym);

  const size_t maxUndefinedReferences = 3;
  size_t i = 0;
  for (const std::string &loc : locations.otherReferences) {
    if (i >= maxUndefinedReferences)
      break;
    message += "\n>>> referenced by " + loc;
    ++i;
  }

  for (const UndefinedDiag::SectionAndOffset &loc : locations.codeReferences) {
    if (i >= maxUndefinedReferences)
      break;
    message += "\n>>> referenced by ";
    std::string src = loc.isec->getSourceLocation(loc.offset);
    if (!src.empty())
      message += src + "\n>>>               ";
    message += loc.isec->getLocation(loc.offset);
    ++i;
  }

  size_t totalReferences =
      locations.otherReferences.size() + locations.codeReferences.size();
  if (totalReferences > i)
    message +=
        ("\n>>> referenced " + Twine(totalReferences - i) + " more times")
            .str();

  if (correctSpelling) {
    std::string preHint = ": ", postHint;
    if (const Symbol *corrected =
            getAlternativeSpelling(sym, preHint, postHint)) {
      message +=
          "\n>>> did you mean" + preHint + toString(*corrected) + postHint;
      if (corrected->getFile())
        message += "\n>>> defined in: " + toString(corrected->getFile());
    }
  }

  if (config->undefinedSymbolTreatment == UndefinedSymbolTreatment::error)
    error(message);
  else if (config->undefinedSymbolTreatment ==
           UndefinedSymbolTreatment::warning)
    warn(message);
  else
    assert(false && "diagnostics make sense for -undefined error|warning only");
}

void macho::reportPendingUndefinedSymbols() {
  // Enable spell corrector for the first 2 diagnostics.
  for (const auto &[i, undef] : llvm::enumerate(undefs))
    reportUndefinedSymbol(*undef.first, undef.second, i < 2);

  // This function is called multiple times during execution. Clear the printed
  // diagnostics to avoid printing the same things again the next time.
  undefs.clear();
}

void macho::treatUndefinedSymbol(const Undefined &sym, StringRef source) {
  if (recoverFromUndefinedSymbol(sym))
    return;

  undefs[&sym].otherReferences.push_back(source.str());
}

void macho::treatUndefinedSymbol(const Undefined &sym, const InputSection *isec,
                                 uint64_t offset) {
  if (recoverFromUndefinedSymbol(sym))
    return;

  undefs[&sym].codeReferences.push_back({isec, offset});
}

std::unique_ptr<SymbolTable> macho::symtab;
