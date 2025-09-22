//===- SymbolTable.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolTable.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputElement.h"
#include "WriterUtils.h"
#include "lld/Common/CommonLinkerContext.h"
#include <optional>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::wasm;
using namespace llvm::object;

namespace lld::wasm {
SymbolTable *symtab;

void SymbolTable::addFile(InputFile *file, StringRef symName) {
  log("Processing: " + toString(file));

  // Lazy object file
  if (file->lazy) {
    if (auto *f = dyn_cast<BitcodeFile>(file)) {
      f->parseLazy();
    } else {
      cast<ObjFile>(file)->parseLazy();
    }
    return;
  }

  // .so file
  if (auto *f = dyn_cast<SharedFile>(file)) {
    // If we are not reporting undefined symbols that we don't actualy
    // parse the shared library symbol table.
    f->parse();
    ctx.sharedFiles.push_back(f);
    return;
  }

  // stub file
  if (auto *f = dyn_cast<StubFile>(file)) {
    f->parse();
    ctx.stubFiles.push_back(f);
    return;
  }

  if (config->trace)
    message(toString(file));

  // LLVM bitcode file
  if (auto *f = dyn_cast<BitcodeFile>(file)) {
    // This order, first adding to `bitcodeFiles` and then parsing is necessary.
    // See https://github.com/llvm/llvm-project/pull/73095
    ctx.bitcodeFiles.push_back(f);
    f->parse(symName);
    return;
  }

  // Regular object file
  auto *f = cast<ObjFile>(file);
  f->parse(false);
  ctx.objectFiles.push_back(f);
}

// This function is where all the optimizations of link-time
// optimization happens. When LTO is in use, some input files are
// not in native object file format but in the LLVM bitcode format.
// This function compiles bitcode files into a few big native files
// using LLVM functions and replaces bitcode symbols with the results.
// Because all bitcode files that the program consists of are passed
// to the compiler at once, it can do whole-program optimization.
void SymbolTable::compileBitcodeFiles() {
  // Prevent further LTO objects being included
  BitcodeFile::doneLTO = true;

  if (ctx.bitcodeFiles.empty())
    return;

  // Compile bitcode files and replace bitcode symbols.
  lto.reset(new BitcodeCompiler);
  for (BitcodeFile *f : ctx.bitcodeFiles)
    lto->add(*f);

  for (StringRef filename : lto->compile()) {
    auto *obj = make<ObjFile>(MemoryBufferRef(filename, "lto.tmp"), "");
    obj->parse(true);
    ctx.objectFiles.push_back(obj);
  }
}

Symbol *SymbolTable::find(StringRef name) {
  auto it = symMap.find(CachedHashStringRef(name));
  if (it == symMap.end() || it->second == -1)
    return nullptr;
  return symVector[it->second];
}

void SymbolTable::replace(StringRef name, Symbol* sym) {
  auto it = symMap.find(CachedHashStringRef(name));
  symVector[it->second] = sym;
}

std::pair<Symbol *, bool> SymbolTable::insertName(StringRef name) {
  bool trace = false;
  auto p = symMap.insert({CachedHashStringRef(name), (int)symVector.size()});
  int &symIndex = p.first->second;
  bool isNew = p.second;
  if (symIndex == -1) {
    symIndex = symVector.size();
    trace = true;
    isNew = true;
  }

  if (!isNew)
    return {symVector[symIndex], false};

  Symbol *sym = reinterpret_cast<Symbol *>(make<SymbolUnion>());
  sym->isUsedInRegularObj = false;
  sym->canInline = true;
  sym->traced = trace;
  sym->forceExport = false;
  sym->referenced = !config->gcSections;
  symVector.emplace_back(sym);
  return {sym, true};
}

std::pair<Symbol *, bool> SymbolTable::insert(StringRef name,
                                              const InputFile *file) {
  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insertName(name);

  if (!file || file->kind() == InputFile::ObjectKind)
    s->isUsedInRegularObj = true;

  return {s, wasInserted};
}

static void reportTypeError(const Symbol *existing, const InputFile *file,
                            llvm::wasm::WasmSymbolType type) {
  error("symbol type mismatch: " + toString(*existing) + "\n>>> defined as " +
        toString(existing->getWasmType()) + " in " +
        toString(existing->getFile()) + "\n>>> defined as " + toString(type) +
        " in " + toString(file));
}

// Check the type of new symbol matches that of the symbol is replacing.
// Returns true if the function types match, false is there is a signature
// mismatch.
static bool signatureMatches(FunctionSymbol *existing,
                             const WasmSignature *newSig) {
  const WasmSignature *oldSig = existing->signature;

  // If either function is missing a signature (this happens for bitcode
  // symbols) then assume they match.  Any mismatch will be reported later
  // when the LTO objects are added.
  if (!newSig || !oldSig)
    return true;

  return *newSig == *oldSig;
}

static void checkGlobalType(const Symbol *existing, const InputFile *file,
                            const WasmGlobalType *newType) {
  if (!isa<GlobalSymbol>(existing)) {
    reportTypeError(existing, file, WASM_SYMBOL_TYPE_GLOBAL);
    return;
  }

  const WasmGlobalType *oldType = cast<GlobalSymbol>(existing)->getGlobalType();
  if (*newType != *oldType) {
    error("Global type mismatch: " + existing->getName() + "\n>>> defined as " +
          toString(*oldType) + " in " + toString(existing->getFile()) +
          "\n>>> defined as " + toString(*newType) + " in " + toString(file));
  }
}

static void checkTagType(const Symbol *existing, const InputFile *file,
                         const WasmSignature *newSig) {
  const auto *existingTag = dyn_cast<TagSymbol>(existing);
  if (!isa<TagSymbol>(existing)) {
    reportTypeError(existing, file, WASM_SYMBOL_TYPE_TAG);
    return;
  }

  const WasmSignature *oldSig = existingTag->signature;
  if (*newSig != *oldSig)
    warn("Tag signature mismatch: " + existing->getName() +
         "\n>>> defined as " + toString(*oldSig) + " in " +
         toString(existing->getFile()) + "\n>>> defined as " +
         toString(*newSig) + " in " + toString(file));
}

static void checkTableType(const Symbol *existing, const InputFile *file,
                           const WasmTableType *newType) {
  if (!isa<TableSymbol>(existing)) {
    reportTypeError(existing, file, WASM_SYMBOL_TYPE_TABLE);
    return;
  }

  const WasmTableType *oldType = cast<TableSymbol>(existing)->getTableType();
  if (newType->ElemType != oldType->ElemType) {
    error("Table type mismatch: " + existing->getName() + "\n>>> defined as " +
          toString(*oldType) + " in " + toString(existing->getFile()) +
          "\n>>> defined as " + toString(*newType) + " in " + toString(file));
  }
  // FIXME: No assertions currently on the limits.
}

static void checkDataType(const Symbol *existing, const InputFile *file) {
  if (!isa<DataSymbol>(existing))
    reportTypeError(existing, file, WASM_SYMBOL_TYPE_DATA);
}

DefinedFunction *SymbolTable::addSyntheticFunction(StringRef name,
                                                   uint32_t flags,
                                                   InputFunction *function) {
  LLVM_DEBUG(dbgs() << "addSyntheticFunction: " << name << "\n");
  assert(!find(name));
  ctx.syntheticFunctions.emplace_back(function);
  return replaceSymbol<DefinedFunction>(insertName(name).first, name,
                                        flags, nullptr, function);
}

// Adds an optional, linker generated, data symbol.  The symbol will only be
// added if there is an undefine reference to it, or if it is explicitly
// exported via the --export flag.  Otherwise we don't add the symbol and return
// nullptr.
DefinedData *SymbolTable::addOptionalDataSymbol(StringRef name,
                                                uint64_t value) {
  Symbol *s = find(name);
  if (!s && (config->exportAll || config->exportedSymbols.count(name) != 0))
    s = insertName(name).first;
  else if (!s || s->isDefined())
    return nullptr;
  LLVM_DEBUG(dbgs() << "addOptionalDataSymbol: " << name << "\n");
  auto *rtn = replaceSymbol<DefinedData>(
      s, name, WASM_SYMBOL_VISIBILITY_HIDDEN | WASM_SYMBOL_ABSOLUTE);
  rtn->setVA(value);
  rtn->referenced = true;
  return rtn;
}

DefinedData *SymbolTable::addSyntheticDataSymbol(StringRef name,
                                                 uint32_t flags) {
  LLVM_DEBUG(dbgs() << "addSyntheticDataSymbol: " << name << "\n");
  assert(!find(name));
  return replaceSymbol<DefinedData>(insertName(name).first, name,
                                    flags | WASM_SYMBOL_ABSOLUTE);
}

DefinedGlobal *SymbolTable::addSyntheticGlobal(StringRef name, uint32_t flags,
                                               InputGlobal *global) {
  LLVM_DEBUG(dbgs() << "addSyntheticGlobal: " << name << " -> " << global
                    << "\n");
  assert(!find(name));
  ctx.syntheticGlobals.emplace_back(global);
  return replaceSymbol<DefinedGlobal>(insertName(name).first, name, flags,
                                      nullptr, global);
}

DefinedGlobal *SymbolTable::addOptionalGlobalSymbol(StringRef name,
                                                    InputGlobal *global) {
  Symbol *s = find(name);
  if (!s || s->isDefined())
    return nullptr;
  LLVM_DEBUG(dbgs() << "addOptionalGlobalSymbol: " << name << " -> " << global
                    << "\n");
  ctx.syntheticGlobals.emplace_back(global);
  return replaceSymbol<DefinedGlobal>(s, name, WASM_SYMBOL_VISIBILITY_HIDDEN,
                                      nullptr, global);
}

DefinedTable *SymbolTable::addSyntheticTable(StringRef name, uint32_t flags,
                                             InputTable *table) {
  LLVM_DEBUG(dbgs() << "addSyntheticTable: " << name << " -> " << table
                    << "\n");
  Symbol *s = find(name);
  assert(!s || s->isUndefined());
  if (!s)
    s = insertName(name).first;
  ctx.syntheticTables.emplace_back(table);
  return replaceSymbol<DefinedTable>(s, name, flags, nullptr, table);
}

static bool shouldReplace(const Symbol *existing, InputFile *newFile,
                          uint32_t newFlags) {
  // If existing symbol is undefined, replace it.
  if (!existing->isDefined()) {
    LLVM_DEBUG(dbgs() << "resolving existing undefined symbol: "
                      << existing->getName() << "\n");
    return true;
  }

  // Now we have two defined symbols. If the new one is weak, we can ignore it.
  if ((newFlags & WASM_SYMBOL_BINDING_MASK) == WASM_SYMBOL_BINDING_WEAK) {
    LLVM_DEBUG(dbgs() << "existing symbol takes precedence\n");
    return false;
  }

  // If the existing symbol is weak, we should replace it.
  if (existing->isWeak()) {
    LLVM_DEBUG(dbgs() << "replacing existing weak symbol\n");
    return true;
  }

  // Similarly with shared symbols
  if (existing->isShared()) {
    LLVM_DEBUG(dbgs() << "replacing existing shared symbol\n");
    return true;
  }

  // Neither symbol is week. They conflict.
  error("duplicate symbol: " + toString(*existing) + "\n>>> defined in " +
        toString(existing->getFile()) + "\n>>> defined in " +
        toString(newFile));
  return true;
}

static void reportFunctionSignatureMismatch(StringRef symName,
                                            FunctionSymbol *sym,
                                            const WasmSignature *signature,
                                            InputFile *file,
                                            bool isError = true) {
  std::string msg =
      ("function signature mismatch: " + symName + "\n>>> defined as " +
       toString(*sym->signature) + " in " + toString(sym->getFile()) +
       "\n>>> defined as " + toString(*signature) + " in " + toString(file))
          .str();
  if (isError)
    error(msg);
  else
    warn(msg);
}

static void reportFunctionSignatureMismatch(StringRef symName,
                                            FunctionSymbol *a,
                                            FunctionSymbol *b,
                                            bool isError = true) {
  reportFunctionSignatureMismatch(symName, a, b->signature, b->getFile(),
                                  isError);
}

Symbol *SymbolTable::addSharedFunction(StringRef name, uint32_t flags,
                                       InputFile *file,
                                       const WasmSignature *sig) {
  LLVM_DEBUG(dbgs() << "addSharedFunction: " << name << " [" << toString(*sig)
                    << "]\n");
  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);

  auto replaceSym = [&](Symbol *sym) {
    replaceSymbol<SharedFunctionSymbol>(sym, name, flags, file, sig);
  };

  if (wasInserted) {
    replaceSym(s);
    return s;
  }

  auto existingFunction = dyn_cast<FunctionSymbol>(s);
  if (!existingFunction) {
    reportTypeError(s, file, WASM_SYMBOL_TYPE_FUNCTION);
    return s;
  }

  // Shared symbols should never replace locally-defined ones
  if (s->isDefined()) {
    return s;
  }

  LLVM_DEBUG(dbgs() << "resolving existing undefined symbol: " << s->getName()
                    << "\n");

  bool checkSig = true;
  if (auto ud = dyn_cast<UndefinedFunction>(existingFunction))
    checkSig = ud->isCalledDirectly;

  if (checkSig && !signatureMatches(existingFunction, sig)) {
    if (config->shlibSigCheck) {
      reportFunctionSignatureMismatch(name, existingFunction, sig, file);
    } else {
      // With --no-shlib-sigcheck we ignore the signature of the function as
      // defined by the shared library and instead use the signature as
      // expected by the program being linked.
      sig = existingFunction->signature;
    }
  }

  replaceSym(s);
  return s;
}

Symbol *SymbolTable::addSharedData(StringRef name, uint32_t flags,
                                   InputFile *file) {
  LLVM_DEBUG(dbgs() << "addSharedData: " << name << "\n");
  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);

  if (wasInserted || s->isUndefined()) {
    replaceSymbol<SharedData>(s, name, flags, file);
  }

  return s;
}

Symbol *SymbolTable::addDefinedFunction(StringRef name, uint32_t flags,
                                        InputFile *file,
                                        InputFunction *function) {
  LLVM_DEBUG(dbgs() << "addDefinedFunction: " << name << " ["
                    << (function ? toString(function->signature) : "none")
                    << "]\n");
  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);

  auto replaceSym = [&](Symbol *sym) {
    // If the new defined function doesn't have signature (i.e. bitcode
    // functions) but the old symbol does, then preserve the old signature
    const WasmSignature *oldSig = s->getSignature();
    auto* newSym = replaceSymbol<DefinedFunction>(sym, name, flags, file, function);
    if (!newSym->signature)
      newSym->signature = oldSig;
  };

  if (wasInserted || s->isLazy()) {
    replaceSym(s);
    return s;
  }

  auto existingFunction = dyn_cast<FunctionSymbol>(s);
  if (!existingFunction) {
    reportTypeError(s, file, WASM_SYMBOL_TYPE_FUNCTION);
    return s;
  }

  bool checkSig = true;
  if (auto ud = dyn_cast<UndefinedFunction>(existingFunction))
    checkSig = ud->isCalledDirectly;

  if (checkSig && function && !signatureMatches(existingFunction, &function->signature)) {
    Symbol* variant;
    if (getFunctionVariant(s, &function->signature, file, &variant))
      // New variant, always replace
      replaceSym(variant);
    else if (shouldReplace(s, file, flags))
      // Variant already exists, replace it after checking shouldReplace
      replaceSym(variant);

    // This variant we found take the place in the symbol table as the primary
    // variant.
    replace(name, variant);
    return variant;
  }

  // Existing function with matching signature.
  if (shouldReplace(s, file, flags))
    replaceSym(s);

  return s;
}

Symbol *SymbolTable::addDefinedData(StringRef name, uint32_t flags,
                                    InputFile *file, InputChunk *segment,
                                    uint64_t address, uint64_t size) {
  LLVM_DEBUG(dbgs() << "addDefinedData:" << name << " addr:" << address
                    << "\n");
  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);

  auto replaceSym = [&]() {
    replaceSymbol<DefinedData>(s, name, flags, file, segment, address, size);
  };

  if (wasInserted || s->isLazy()) {
    replaceSym();
    return s;
  }

  checkDataType(s, file);

  if (shouldReplace(s, file, flags))
    replaceSym();
  return s;
}

Symbol *SymbolTable::addDefinedGlobal(StringRef name, uint32_t flags,
                                      InputFile *file, InputGlobal *global) {
  LLVM_DEBUG(dbgs() << "addDefinedGlobal:" << name << "\n");

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);

  auto replaceSym = [&]() {
    replaceSymbol<DefinedGlobal>(s, name, flags, file, global);
  };

  if (wasInserted || s->isLazy()) {
    replaceSym();
    return s;
  }

  checkGlobalType(s, file, &global->getType());

  if (shouldReplace(s, file, flags))
    replaceSym();
  return s;
}

Symbol *SymbolTable::addDefinedTag(StringRef name, uint32_t flags,
                                   InputFile *file, InputTag *tag) {
  LLVM_DEBUG(dbgs() << "addDefinedTag:" << name << "\n");

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);

  auto replaceSym = [&]() {
    replaceSymbol<DefinedTag>(s, name, flags, file, tag);
  };

  if (wasInserted || s->isLazy()) {
    replaceSym();
    return s;
  }

  checkTagType(s, file, &tag->signature);

  if (shouldReplace(s, file, flags))
    replaceSym();
  return s;
}

Symbol *SymbolTable::addDefinedTable(StringRef name, uint32_t flags,
                                     InputFile *file, InputTable *table) {
  LLVM_DEBUG(dbgs() << "addDefinedTable:" << name << "\n");

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);

  auto replaceSym = [&]() {
    replaceSymbol<DefinedTable>(s, name, flags, file, table);
  };

  if (wasInserted || s->isLazy()) {
    replaceSym();
    return s;
  }

  checkTableType(s, file, &table->getType());

  if (shouldReplace(s, file, flags))
    replaceSym();
  return s;
}

// This function get called when an undefined symbol is added, and there is
// already an existing one in the symbols table.  In this case we check that
// custom 'import-module' and 'import-field' symbol attributes agree.
// With LTO these attributes are not available when the bitcode is read and only
// become available when the LTO object is read.  In this case we silently
// replace the empty attributes with the valid ones.
template <typename T>
static void setImportAttributes(T *existing,
                                std::optional<StringRef> importName,
                                std::optional<StringRef> importModule,
                                uint32_t flags, InputFile *file) {
  if (importName) {
    if (!existing->importName)
      existing->importName = importName;
    if (existing->importName != importName)
      error("import name mismatch for symbol: " + toString(*existing) +
            "\n>>> defined as " + *existing->importName + " in " +
            toString(existing->getFile()) + "\n>>> defined as " + *importName +
            " in " + toString(file));
  }

  if (importModule) {
    if (!existing->importModule)
      existing->importModule = importModule;
    if (existing->importModule != importModule)
      error("import module mismatch for symbol: " + toString(*existing) +
            "\n>>> defined as " + *existing->importModule + " in " +
            toString(existing->getFile()) + "\n>>> defined as " +
            *importModule + " in " + toString(file));
  }

  // Update symbol binding, if the existing symbol is weak
  uint32_t binding = flags & WASM_SYMBOL_BINDING_MASK;
  if (existing->isWeak() && binding != WASM_SYMBOL_BINDING_WEAK) {
    existing->flags = (existing->flags & ~WASM_SYMBOL_BINDING_MASK) | binding;
  }
}

Symbol *SymbolTable::addUndefinedFunction(StringRef name,
                                          std::optional<StringRef> importName,
                                          std::optional<StringRef> importModule,
                                          uint32_t flags, InputFile *file,
                                          const WasmSignature *sig,
                                          bool isCalledDirectly) {
  LLVM_DEBUG(dbgs() << "addUndefinedFunction: " << name << " ["
                    << (sig ? toString(*sig) : "none")
                    << "] IsCalledDirectly:" << isCalledDirectly << " flags=0x"
                    << utohexstr(flags) << "\n");
  assert(flags & WASM_SYMBOL_UNDEFINED);

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);
  if (s->traced)
    printTraceSymbolUndefined(name, file);

  auto replaceSym = [&]() {
    replaceSymbol<UndefinedFunction>(s, name, importName, importModule, flags,
                                     file, sig, isCalledDirectly);
  };

  if (wasInserted) {
    replaceSym();
  } else if (auto *lazy = dyn_cast<LazySymbol>(s)) {
    if ((flags & WASM_SYMBOL_BINDING_MASK) == WASM_SYMBOL_BINDING_WEAK) {
      lazy->setWeak();
      lazy->signature = sig;
    } else {
      lazy->extract();
      if (!config->whyExtract.empty())
        ctx.whyExtractRecords.emplace_back(toString(file), s->getFile(), *s);
    }
  } else {
    auto existingFunction = dyn_cast<FunctionSymbol>(s);
    if (!existingFunction) {
      reportTypeError(s, file, WASM_SYMBOL_TYPE_FUNCTION);
      return s;
    }
    if (!existingFunction->signature && sig)
      existingFunction->signature = sig;
    auto *existingUndefined = dyn_cast<UndefinedFunction>(existingFunction);
    if (isCalledDirectly && !signatureMatches(existingFunction, sig)) {
      if (existingFunction->isShared()) {
        // Special handling for when the existing function is a shared symbol
        if (config->shlibSigCheck) {
          reportFunctionSignatureMismatch(name, existingFunction, sig, file);
        } else {
          existingFunction->signature = sig;
        }
      }
      // If the existing undefined functions is not called directly then let
      // this one take precedence.  Otherwise the existing function is either
      // directly called or defined, in which case we need a function variant.
      else if (existingUndefined && !existingUndefined->isCalledDirectly)
        replaceSym();
      else if (getFunctionVariant(s, sig, file, &s))
        replaceSym();
    }
    if (existingUndefined) {
      setImportAttributes(existingUndefined, importName, importModule, flags,
                          file);
      if (isCalledDirectly)
        existingUndefined->isCalledDirectly = true;
      if (s->isWeak())
        s->flags = flags;
    }
  }

  return s;
}

Symbol *SymbolTable::addUndefinedData(StringRef name, uint32_t flags,
                                      InputFile *file) {
  LLVM_DEBUG(dbgs() << "addUndefinedData: " << name << "\n");
  assert(flags & WASM_SYMBOL_UNDEFINED);

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);
  if (s->traced)
    printTraceSymbolUndefined(name, file);

  if (wasInserted) {
    replaceSymbol<UndefinedData>(s, name, flags, file);
  } else if (auto *lazy = dyn_cast<LazySymbol>(s)) {
    if ((flags & WASM_SYMBOL_BINDING_MASK) == WASM_SYMBOL_BINDING_WEAK)
      lazy->setWeak();
    else
      lazy->extract();
  } else if (s->isDefined()) {
    checkDataType(s, file);
  } else if (s->isWeak()) {
    s->flags = flags;
  }
  return s;
}

Symbol *SymbolTable::addUndefinedGlobal(StringRef name,
                                        std::optional<StringRef> importName,
                                        std::optional<StringRef> importModule,
                                        uint32_t flags, InputFile *file,
                                        const WasmGlobalType *type) {
  LLVM_DEBUG(dbgs() << "addUndefinedGlobal: " << name << "\n");
  assert(flags & WASM_SYMBOL_UNDEFINED);

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);
  if (s->traced)
    printTraceSymbolUndefined(name, file);

  if (wasInserted)
    replaceSymbol<UndefinedGlobal>(s, name, importName, importModule, flags,
                                   file, type);
  else if (auto *lazy = dyn_cast<LazySymbol>(s))
    lazy->extract();
  else if (s->isDefined())
    checkGlobalType(s, file, type);
  else if (s->isWeak())
    s->flags = flags;
  return s;
}

Symbol *SymbolTable::addUndefinedTable(StringRef name,
                                       std::optional<StringRef> importName,
                                       std::optional<StringRef> importModule,
                                       uint32_t flags, InputFile *file,
                                       const WasmTableType *type) {
  LLVM_DEBUG(dbgs() << "addUndefinedTable: " << name << "\n");
  assert(flags & WASM_SYMBOL_UNDEFINED);

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);
  if (s->traced)
    printTraceSymbolUndefined(name, file);

  if (wasInserted)
    replaceSymbol<UndefinedTable>(s, name, importName, importModule, flags,
                                  file, type);
  else if (auto *lazy = dyn_cast<LazySymbol>(s))
    lazy->extract();
  else if (s->isDefined())
    checkTableType(s, file, type);
  else if (s->isWeak())
    s->flags = flags;
  return s;
}

Symbol *SymbolTable::addUndefinedTag(StringRef name,
                                     std::optional<StringRef> importName,
                                     std::optional<StringRef> importModule,
                                     uint32_t flags, InputFile *file,
                                     const WasmSignature *sig) {
  LLVM_DEBUG(dbgs() << "addUndefinedTag: " << name << "\n");
  assert(flags & WASM_SYMBOL_UNDEFINED);

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insert(name, file);
  if (s->traced)
    printTraceSymbolUndefined(name, file);

  if (wasInserted)
    replaceSymbol<UndefinedTag>(s, name, importName, importModule, flags, file,
                                sig);
  else if (auto *lazy = dyn_cast<LazySymbol>(s))
    lazy->extract();
  else if (s->isDefined())
    checkTagType(s, file, sig);
  else if (s->isWeak())
    s->flags = flags;
  return s;
}

TableSymbol *SymbolTable::createUndefinedIndirectFunctionTable(StringRef name) {
  WasmLimits limits{0, 0, 0}; // Set by the writer.
  WasmTableType *type = make<WasmTableType>();
  type->ElemType = ValType::FUNCREF;
  type->Limits = limits;
  uint32_t flags = config->exportTable ? 0 : WASM_SYMBOL_VISIBILITY_HIDDEN;
  flags |= WASM_SYMBOL_UNDEFINED;
  Symbol *sym =
      addUndefinedTable(name, name, defaultModule, flags, nullptr, type);
  sym->markLive();
  sym->forceExport = config->exportTable;
  return cast<TableSymbol>(sym);
}

TableSymbol *SymbolTable::createDefinedIndirectFunctionTable(StringRef name) {
  const uint32_t invalidIndex = -1;
  WasmLimits limits{0, 0, 0}; // Set by the writer.
  WasmTableType type{ValType::FUNCREF, limits};
  WasmTable desc{invalidIndex, type, name};
  InputTable *table = make<InputTable>(desc, nullptr);
  uint32_t flags = config->exportTable ? 0 : WASM_SYMBOL_VISIBILITY_HIDDEN;
  TableSymbol *sym = addSyntheticTable(name, flags, table);
  sym->markLive();
  sym->forceExport = config->exportTable;
  return sym;
}

// Whether or not we need an indirect function table is usually a function of
// whether an input declares a need for it.  However sometimes it's possible for
// no input to need the indirect function table, but then a late
// addInternalGOTEntry causes a function to be allocated an address.  In that
// case address we synthesize a definition at the last minute.
TableSymbol *SymbolTable::resolveIndirectFunctionTable(bool required) {
  Symbol *existing = find(functionTableName);
  if (existing) {
    if (!isa<TableSymbol>(existing)) {
      error(Twine("reserved symbol must be of type table: `") +
            functionTableName + "`");
      return nullptr;
    }
    if (existing->isDefined()) {
      error(Twine("reserved symbol must not be defined in input files: `") +
            functionTableName + "`");
      return nullptr;
    }
  }

  if (config->importTable) {
    if (existing) {
      existing->importModule = defaultModule;
      existing->importName = functionTableName;
      return cast<TableSymbol>(existing);
    }
    if (required)
      return createUndefinedIndirectFunctionTable(functionTableName);
  } else if ((existing && existing->isLive()) || config->exportTable ||
             required) {
    // A defined table is required.  Either because the user request an exported
    // table or because the table symbol is already live.  The existing table is
    // guaranteed to be undefined due to the check above.
    return createDefinedIndirectFunctionTable(functionTableName);
  }

  // An indirect function table will only be present in the symbol table if
  // needed by a reloc; if we get here, we don't need one.
  return nullptr;
}

void SymbolTable::addLazy(StringRef name, InputFile *file) {
  LLVM_DEBUG(dbgs() << "addLazy: " << name << "\n");

  Symbol *s;
  bool wasInserted;
  std::tie(s, wasInserted) = insertName(name);

  if (wasInserted) {
    replaceSymbol<LazySymbol>(s, name, 0, file);
    return;
  }

  if (!s->isUndefined())
    return;

  // The existing symbol is undefined, load a new one from the archive,
  // unless the existing symbol is weak in which case replace the undefined
  // symbols with a LazySymbol.
  if (s->isWeak()) {
    const WasmSignature *oldSig = nullptr;
    // In the case of an UndefinedFunction we need to preserve the expected
    // signature.
    if (auto *f = dyn_cast<UndefinedFunction>(s))
      oldSig = f->signature;
    LLVM_DEBUG(dbgs() << "replacing existing weak undefined symbol\n");
    auto newSym =
        replaceSymbol<LazySymbol>(s, name, WASM_SYMBOL_BINDING_WEAK, file);
    newSym->signature = oldSig;
    return;
  }

  LLVM_DEBUG(dbgs() << "replacing existing undefined\n");
  const InputFile *oldFile = s->getFile();
  LazySymbol(name, 0, file).extract();
  if (!config->whyExtract.empty())
    ctx.whyExtractRecords.emplace_back(toString(oldFile), s->getFile(), *s);
}

bool SymbolTable::addComdat(StringRef name) {
  return comdatGroups.insert(CachedHashStringRef(name)).second;
}

// The new signature doesn't match.  Create a variant to the symbol with the
// signature encoded in the name and return that instead.  These symbols are
// then unified later in handleSymbolVariants.
bool SymbolTable::getFunctionVariant(Symbol* sym, const WasmSignature *sig,
                                     const InputFile *file, Symbol **out) {
  LLVM_DEBUG(dbgs() << "getFunctionVariant: " << sym->getName() << " -> "
                    << " " << toString(*sig) << "\n");
  Symbol *variant = nullptr;

  // Linear search through symbol variants.  Should never be more than two
  // or three entries here.
  auto &variants = symVariants[CachedHashStringRef(sym->getName())];
  if (variants.empty())
    variants.push_back(sym);

  for (Symbol* v : variants) {
    if (*v->getSignature() == *sig) {
      variant = v;
      break;
    }
  }

  bool wasAdded = !variant;
  if (wasAdded) {
    // Create a new variant;
    LLVM_DEBUG(dbgs() << "added new variant\n");
    variant = reinterpret_cast<Symbol *>(make<SymbolUnion>());
    variant->isUsedInRegularObj =
        !file || file->kind() == InputFile::ObjectKind;
    variant->canInline = true;
    variant->traced = false;
    variant->forceExport = false;
    variants.push_back(variant);
  } else {
    LLVM_DEBUG(dbgs() << "variant already exists: " << toString(*variant) << "\n");
    assert(*variant->getSignature() == *sig);
  }

  *out = variant;
  return wasAdded;
}

// Set a flag for --trace-symbol so that we can print out a log message
// if a new symbol with the same name is inserted into the symbol table.
void SymbolTable::trace(StringRef name) {
  symMap.insert({CachedHashStringRef(name), -1});
}

void SymbolTable::wrap(Symbol *sym, Symbol *real, Symbol *wrap) {
  // Swap symbols as instructed by -wrap.
  int &origIdx = symMap[CachedHashStringRef(sym->getName())];
  int &realIdx= symMap[CachedHashStringRef(real->getName())];
  int &wrapIdx = symMap[CachedHashStringRef(wrap->getName())];
  LLVM_DEBUG(dbgs() << "wrap: " << sym->getName() << "\n");

  // Anyone looking up __real symbols should get the original
  realIdx = origIdx;
  // Anyone looking up the original should get the __wrap symbol
  origIdx = wrapIdx;
}

static const uint8_t unreachableFn[] = {
    0x03 /* ULEB length */, 0x00 /* ULEB num locals */,
    0x00 /* opcode unreachable */, 0x0b /* opcode end */
};

// Replace the given symbol body with an unreachable function.
// This is used by handleWeakUndefines in order to generate a callable
// equivalent of an undefined function and also handleSymbolVariants for
// undefined functions that don't match the signature of the definition.
InputFunction *SymbolTable::replaceWithUnreachable(Symbol *sym,
                                                   const WasmSignature &sig,
                                                   StringRef debugName) {
  auto *func = make<SyntheticFunction>(sig, sym->getName(), debugName);
  func->setBody(unreachableFn);
  ctx.syntheticFunctions.emplace_back(func);
  // Mark new symbols as local. For relocatable output we don't want them
  // to be exported outside the object file.
  replaceSymbol<DefinedFunction>(sym, debugName, WASM_SYMBOL_BINDING_LOCAL,
                                 nullptr, func);
  // Ensure the stub function doesn't get a table entry.  Its address
  // should always compare equal to the null pointer.
  sym->isStub = true;
  return func;
}

void SymbolTable::replaceWithUndefined(Symbol *sym) {
  // Add a synthetic dummy for weak undefined functions.  These dummies will
  // be GC'd if not used as the target of any "call" instructions.
  StringRef debugName = saver().save("undefined_weak:" + toString(*sym));
  replaceWithUnreachable(sym, *sym->getSignature(), debugName);
  // Hide our dummy to prevent export.
  sym->setHidden(true);
}

// For weak undefined functions, there may be "call" instructions that reference
// the symbol. In this case, we need to synthesise a dummy/stub function that
// will abort at runtime, so that relocations can still provided an operand to
// the call instruction that passes Wasm validation.
void SymbolTable::handleWeakUndefines() {
  for (Symbol *sym : symbols()) {
    if (sym->isUndefWeak() && sym->isUsedInRegularObj) {
      if (sym->getSignature()) {
        replaceWithUndefined(sym);
      } else {
        // It is possible for undefined functions not to have a signature (eg.
        // if added via "--undefined"), but weak undefined ones do have a
        // signature.  Lazy symbols may not be functions and therefore Sig can
        // still be null in some circumstance.
        assert(!isa<FunctionSymbol>(sym));
      }
    }
  }
}

DefinedFunction *SymbolTable::createUndefinedStub(const WasmSignature &sig) {
  if (stubFunctions.count(sig))
    return stubFunctions[sig];
  LLVM_DEBUG(dbgs() << "createUndefinedStub: " << toString(sig) << "\n");
  auto *sym = reinterpret_cast<DefinedFunction *>(make<SymbolUnion>());
  sym->isUsedInRegularObj = true;
  sym->canInline = true;
  sym->traced = false;
  sym->forceExport = false;
  sym->signature = &sig;
  replaceSymbol<DefinedFunction>(
      sym, "undefined_stub", WASM_SYMBOL_VISIBILITY_HIDDEN, nullptr, nullptr);
  replaceWithUnreachable(sym, sig, "undefined_stub");
  stubFunctions[sig] = sym;
  return sym;
}

// Remove any variant symbols that were created due to function signature
// mismatches.
void SymbolTable::handleSymbolVariants() {
  for (auto pair : symVariants) {
    // Push the initial symbol onto the list of variants.
    StringRef symName = pair.first.val();
    std::vector<Symbol *> &variants = pair.second;

#ifndef NDEBUG
    LLVM_DEBUG(dbgs() << "symbol with (" << variants.size()
                      << ") variants: " << symName << "\n");
    for (auto *s: variants) {
      auto *f = cast<FunctionSymbol>(s);
      LLVM_DEBUG(dbgs() << " variant: " + f->getName() << " "
                        << toString(*f->signature) << "\n");
    }
#endif

    // Find the one definition.
    DefinedFunction *defined = nullptr;
    for (auto *symbol : variants) {
      if (auto f = dyn_cast<DefinedFunction>(symbol)) {
        defined = f;
        break;
      }
    }

    // If there are no definitions, and the undefined symbols disagree on
    // the signature, there is not we can do since we don't know which one
    // to use as the signature on the import.
    if (!defined) {
      reportFunctionSignatureMismatch(symName,
                                      cast<FunctionSymbol>(variants[0]),
                                      cast<FunctionSymbol>(variants[1]));
      return;
    }

    for (auto *symbol : variants) {
      if (symbol != defined) {
        auto *f = cast<FunctionSymbol>(symbol);
        reportFunctionSignatureMismatch(symName, f, defined, false);
        StringRef debugName =
            saver().save("signature_mismatch:" + toString(*f));
        replaceWithUnreachable(f, *f->signature, debugName);
      }
    }
  }
}

} // namespace wasm::lld
