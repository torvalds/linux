//===- SyntheticSections.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains linker-synthesized sections.
//
//===----------------------------------------------------------------------===//

#include "SyntheticSections.h"

#include "InputChunks.h"
#include "InputElement.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "llvm/Support/Path.h"
#include <optional>

using namespace llvm;
using namespace llvm::wasm;

namespace lld::wasm {

OutStruct out;

namespace {

// Some synthetic sections (e.g. "name" and "linking") have subsections.
// Just like the synthetic sections themselves these need to be created before
// they can be written out (since they are preceded by their length). This
// class is used to create subsections and then write them into the stream
// of the parent section.
class SubSection {
public:
  explicit SubSection(uint32_t type) : type(type) {}

  void writeTo(raw_ostream &to) {
    os.flush();
    writeUleb128(to, type, "subsection type");
    writeUleb128(to, body.size(), "subsection size");
    to.write(body.data(), body.size());
  }

private:
  uint32_t type;
  std::string body;

public:
  raw_string_ostream os{body};
};

} // namespace

bool DylinkSection::isNeeded() const {
  return ctx.isPic ||
         config->unresolvedSymbols == UnresolvedPolicy::ImportDynamic ||
         !ctx.sharedFiles.empty();
}

void DylinkSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  {
    SubSection sub(WASM_DYLINK_MEM_INFO);
    writeUleb128(sub.os, memSize, "MemSize");
    writeUleb128(sub.os, memAlign, "MemAlign");
    writeUleb128(sub.os, out.elemSec->numEntries(), "TableSize");
    writeUleb128(sub.os, 0, "TableAlign");
    sub.writeTo(os);
  }

  if (ctx.sharedFiles.size()) {
    SubSection sub(WASM_DYLINK_NEEDED);
    writeUleb128(sub.os, ctx.sharedFiles.size(), "Needed");
    for (auto *so : ctx.sharedFiles)
      writeStr(sub.os, llvm::sys::path::filename(so->getName()), "so name");
    sub.writeTo(os);
  }

  // Under certain circumstances we need to include extra information about our
  // exports and/or imports to the dynamic linker.
  // For exports we need to notify the linker when an export is TLS since the
  // exported value is relative to __tls_base rather than __memory_base.
  // For imports we need to notify the dynamic linker when an import is weak
  // so that knows not to report an error for such symbols.
  std::vector<const Symbol *> importInfo;
  std::vector<const Symbol *> exportInfo;
  for (const Symbol *sym : symtab->symbols()) {
    if (sym->isLive()) {
      if (sym->isExported() && sym->isTLS() && isa<DefinedData>(sym)) {
        exportInfo.push_back(sym);
      }
      if (sym->isUndefWeak()) {
        importInfo.push_back(sym);
      }
    }
  }

  if (!exportInfo.empty()) {
    SubSection sub(WASM_DYLINK_EXPORT_INFO);
    writeUleb128(sub.os, exportInfo.size(), "num exports");

    for (const Symbol *sym : exportInfo) {
      LLVM_DEBUG(llvm::dbgs() << "export info: " << toString(*sym) << "\n");
      StringRef name = sym->getName();
      if (auto *f = dyn_cast<DefinedFunction>(sym)) {
        if (std::optional<StringRef> exportName =
                f->function->getExportName()) {
          name = *exportName;
        }
      }
      writeStr(sub.os, name, "sym name");
      writeUleb128(sub.os, sym->flags, "sym flags");
    }

    sub.writeTo(os);
  }

  if (!importInfo.empty()) {
    SubSection sub(WASM_DYLINK_IMPORT_INFO);
    writeUleb128(sub.os, importInfo.size(), "num imports");

    for (const Symbol *sym : importInfo) {
      LLVM_DEBUG(llvm::dbgs() << "imports info: " << toString(*sym) << "\n");
      StringRef module = sym->importModule.value_or(defaultModule);
      StringRef name = sym->importName.value_or(sym->getName());
      writeStr(sub.os, module, "import module");
      writeStr(sub.os, name, "import name");
      writeUleb128(sub.os, sym->flags, "sym flags");
    }

    sub.writeTo(os);
  }
}

uint32_t TypeSection::registerType(const WasmSignature &sig) {
  auto pair = typeIndices.insert(std::make_pair(sig, types.size()));
  if (pair.second) {
    LLVM_DEBUG(llvm::dbgs() << "registerType " << toString(sig) << "\n");
    types.push_back(&sig);
  }
  return pair.first->second;
}

uint32_t TypeSection::lookupType(const WasmSignature &sig) {
  auto it = typeIndices.find(sig);
  if (it == typeIndices.end()) {
    error("type not found: " + toString(sig));
    return 0;
  }
  return it->second;
}

void TypeSection::writeBody() {
  writeUleb128(bodyOutputStream, types.size(), "type count");
  for (const WasmSignature *sig : types)
    writeSig(bodyOutputStream, *sig);
}

uint32_t ImportSection::getNumImports() const {
  assert(isSealed);
  uint32_t numImports = importedSymbols.size() + gotSymbols.size();
  if (config->memoryImport.has_value())
    ++numImports;
  return numImports;
}

void ImportSection::addGOTEntry(Symbol *sym) {
  assert(!isSealed);
  if (sym->hasGOTIndex())
    return;
  LLVM_DEBUG(dbgs() << "addGOTEntry: " << toString(*sym) << "\n");
  sym->setGOTIndex(numImportedGlobals++);
  if (ctx.isPic) {
    // Any symbol that is assigned an normal GOT entry must be exported
    // otherwise the dynamic linker won't be able create the entry that contains
    // it.
    sym->forceExport = true;
  }
  gotSymbols.push_back(sym);
}

void ImportSection::addImport(Symbol *sym) {
  assert(!isSealed);
  StringRef module = sym->importModule.value_or(defaultModule);
  StringRef name = sym->importName.value_or(sym->getName());
  if (auto *f = dyn_cast<FunctionSymbol>(sym)) {
    ImportKey<WasmSignature> key(*(f->getSignature()), module, name);
    auto entry = importedFunctions.try_emplace(key, numImportedFunctions);
    if (entry.second) {
      importedSymbols.emplace_back(sym);
      f->setFunctionIndex(numImportedFunctions++);
    } else {
      f->setFunctionIndex(entry.first->second);
    }
  } else if (auto *g = dyn_cast<GlobalSymbol>(sym)) {
    ImportKey<WasmGlobalType> key(*(g->getGlobalType()), module, name);
    auto entry = importedGlobals.try_emplace(key, numImportedGlobals);
    if (entry.second) {
      importedSymbols.emplace_back(sym);
      g->setGlobalIndex(numImportedGlobals++);
    } else {
      g->setGlobalIndex(entry.first->second);
    }
  } else if (auto *t = dyn_cast<TagSymbol>(sym)) {
    ImportKey<WasmSignature> key(*(t->getSignature()), module, name);
    auto entry = importedTags.try_emplace(key, numImportedTags);
    if (entry.second) {
      importedSymbols.emplace_back(sym);
      t->setTagIndex(numImportedTags++);
    } else {
      t->setTagIndex(entry.first->second);
    }
  } else {
    assert(TableSymbol::classof(sym));
    auto *table = cast<TableSymbol>(sym);
    ImportKey<WasmTableType> key(*(table->getTableType()), module, name);
    auto entry = importedTables.try_emplace(key, numImportedTables);
    if (entry.second) {
      importedSymbols.emplace_back(sym);
      table->setTableNumber(numImportedTables++);
    } else {
      table->setTableNumber(entry.first->second);
    }
  }
}

void ImportSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  writeUleb128(os, getNumImports(), "import count");

  bool is64 = config->is64.value_or(false);

  if (config->memoryImport) {
    WasmImport import;
    import.Module = config->memoryImport->first;
    import.Field = config->memoryImport->second;
    import.Kind = WASM_EXTERNAL_MEMORY;
    import.Memory.Flags = 0;
    import.Memory.Minimum = out.memorySec->numMemoryPages;
    if (out.memorySec->maxMemoryPages != 0 || config->sharedMemory) {
      import.Memory.Flags |= WASM_LIMITS_FLAG_HAS_MAX;
      import.Memory.Maximum = out.memorySec->maxMemoryPages;
    }
    if (config->sharedMemory)
      import.Memory.Flags |= WASM_LIMITS_FLAG_IS_SHARED;
    if (is64)
      import.Memory.Flags |= WASM_LIMITS_FLAG_IS_64;
    writeImport(os, import);
  }

  for (const Symbol *sym : importedSymbols) {
    WasmImport import;
    import.Field = sym->importName.value_or(sym->getName());
    import.Module = sym->importModule.value_or(defaultModule);

    if (auto *functionSym = dyn_cast<FunctionSymbol>(sym)) {
      import.Kind = WASM_EXTERNAL_FUNCTION;
      import.SigIndex = out.typeSec->lookupType(*functionSym->signature);
    } else if (auto *globalSym = dyn_cast<GlobalSymbol>(sym)) {
      import.Kind = WASM_EXTERNAL_GLOBAL;
      import.Global = *globalSym->getGlobalType();
    } else if (auto *tagSym = dyn_cast<TagSymbol>(sym)) {
      import.Kind = WASM_EXTERNAL_TAG;
      import.SigIndex = out.typeSec->lookupType(*tagSym->signature);
    } else {
      auto *tableSym = cast<TableSymbol>(sym);
      import.Kind = WASM_EXTERNAL_TABLE;
      import.Table = *tableSym->getTableType();
    }
    writeImport(os, import);
  }

  for (const Symbol *sym : gotSymbols) {
    WasmImport import;
    import.Kind = WASM_EXTERNAL_GLOBAL;
    auto ptrType = is64 ? WASM_TYPE_I64 : WASM_TYPE_I32;
    import.Global = {static_cast<uint8_t>(ptrType), true};
    if (isa<DataSymbol>(sym))
      import.Module = "GOT.mem";
    else
      import.Module = "GOT.func";
    import.Field = sym->getName();
    writeImport(os, import);
  }
}

void FunctionSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  writeUleb128(os, inputFunctions.size(), "function count");
  for (const InputFunction *func : inputFunctions)
    writeUleb128(os, out.typeSec->lookupType(func->signature), "sig index");
}

void FunctionSection::addFunction(InputFunction *func) {
  if (!func->live)
    return;
  uint32_t functionIndex =
      out.importSec->getNumImportedFunctions() + inputFunctions.size();
  inputFunctions.emplace_back(func);
  func->setFunctionIndex(functionIndex);
}

void TableSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  writeUleb128(os, inputTables.size(), "table count");
  for (const InputTable *table : inputTables)
    writeTableType(os, table->getType());
}

void TableSection::addTable(InputTable *table) {
  if (!table->live)
    return;
  // Some inputs require that the indirect function table be assigned to table
  // number 0.
  if (ctx.legacyFunctionTable &&
      isa<DefinedTable>(WasmSym::indirectFunctionTable) &&
      cast<DefinedTable>(WasmSym::indirectFunctionTable)->table == table) {
    if (out.importSec->getNumImportedTables()) {
      // Alack!  Some other input imported a table, meaning that we are unable
      // to assign table number 0 to the indirect function table.
      for (const auto *culprit : out.importSec->importedSymbols) {
        if (isa<UndefinedTable>(culprit)) {
          error("object file not built with 'reference-types' feature "
                "conflicts with import of table " +
                culprit->getName() + " by file " +
                toString(culprit->getFile()));
          return;
        }
      }
      llvm_unreachable("failed to find conflicting table import");
    }
    inputTables.insert(inputTables.begin(), table);
    return;
  }
  inputTables.push_back(table);
}

void TableSection::assignIndexes() {
  uint32_t tableNumber = out.importSec->getNumImportedTables();
  for (InputTable *t : inputTables)
    t->assignIndex(tableNumber++);
}

void MemorySection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  bool hasMax = maxMemoryPages != 0 || config->sharedMemory;
  writeUleb128(os, 1, "memory count");
  unsigned flags = 0;
  if (hasMax)
    flags |= WASM_LIMITS_FLAG_HAS_MAX;
  if (config->sharedMemory)
    flags |= WASM_LIMITS_FLAG_IS_SHARED;
  if (config->is64.value_or(false))
    flags |= WASM_LIMITS_FLAG_IS_64;
  writeUleb128(os, flags, "memory limits flags");
  writeUleb128(os, numMemoryPages, "initial pages");
  if (hasMax)
    writeUleb128(os, maxMemoryPages, "max pages");
}

void TagSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  writeUleb128(os, inputTags.size(), "tag count");
  for (InputTag *t : inputTags) {
    writeUleb128(os, 0, "tag attribute"); // Reserved "attribute" field
    writeUleb128(os, out.typeSec->lookupType(t->signature), "sig index");
  }
}

void TagSection::addTag(InputTag *tag) {
  if (!tag->live)
    return;
  uint32_t tagIndex = out.importSec->getNumImportedTags() + inputTags.size();
  LLVM_DEBUG(dbgs() << "addTag: " << tagIndex << "\n");
  tag->assignIndex(tagIndex);
  inputTags.push_back(tag);
}

void GlobalSection::assignIndexes() {
  uint32_t globalIndex = out.importSec->getNumImportedGlobals();
  for (InputGlobal *g : inputGlobals)
    g->assignIndex(globalIndex++);
  for (Symbol *sym : internalGotSymbols)
    sym->setGOTIndex(globalIndex++);
  isSealed = true;
}

static void ensureIndirectFunctionTable() {
  if (!WasmSym::indirectFunctionTable)
    WasmSym::indirectFunctionTable =
        symtab->resolveIndirectFunctionTable(/*required =*/true);
}

void GlobalSection::addInternalGOTEntry(Symbol *sym) {
  assert(!isSealed);
  if (sym->requiresGOT)
    return;
  LLVM_DEBUG(dbgs() << "addInternalGOTEntry: " << sym->getName() << " "
                    << toString(sym->kind()) << "\n");
  sym->requiresGOT = true;
  if (auto *F = dyn_cast<FunctionSymbol>(sym)) {
    ensureIndirectFunctionTable();
    out.elemSec->addEntry(F);
  }
  internalGotSymbols.push_back(sym);
}

void GlobalSection::generateRelocationCode(raw_ostream &os, bool TLS) const {
  assert(!config->extendedConst);
  bool is64 = config->is64.value_or(false);
  unsigned opcode_ptr_const = is64 ? WASM_OPCODE_I64_CONST
                                   : WASM_OPCODE_I32_CONST;
  unsigned opcode_ptr_add = is64 ? WASM_OPCODE_I64_ADD
                                 : WASM_OPCODE_I32_ADD;

  for (const Symbol *sym : internalGotSymbols) {
    if (TLS != sym->isTLS())
      continue;

    if (auto *d = dyn_cast<DefinedData>(sym)) {
      // Get __memory_base
      writeU8(os, WASM_OPCODE_GLOBAL_GET, "GLOBAL_GET");
      if (sym->isTLS())
        writeUleb128(os, WasmSym::tlsBase->getGlobalIndex(), "__tls_base");
      else
        writeUleb128(os, WasmSym::memoryBase->getGlobalIndex(),
                     "__memory_base");

      // Add the virtual address of the data symbol
      writeU8(os, opcode_ptr_const, "CONST");
      writeSleb128(os, d->getVA(), "offset");
    } else if (auto *f = dyn_cast<FunctionSymbol>(sym)) {
      if (f->isStub)
        continue;
      // Get __table_base
      writeU8(os, WASM_OPCODE_GLOBAL_GET, "GLOBAL_GET");
      writeUleb128(os, WasmSym::tableBase->getGlobalIndex(), "__table_base");

      // Add the table index to __table_base
      writeU8(os, opcode_ptr_const, "CONST");
      writeSleb128(os, f->getTableIndex(), "offset");
    } else {
      assert(isa<UndefinedData>(sym) || isa<SharedData>(sym));
      continue;
    }
    writeU8(os, opcode_ptr_add, "ADD");
    writeU8(os, WASM_OPCODE_GLOBAL_SET, "GLOBAL_SET");
    writeUleb128(os, sym->getGOTIndex(), "got_entry");
  }
}

void GlobalSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  writeUleb128(os, numGlobals(), "global count");
  for (InputGlobal *g : inputGlobals) {
    writeGlobalType(os, g->getType());
    writeInitExpr(os, g->getInitExpr());
  }
  bool is64 = config->is64.value_or(false);
  uint8_t itype = is64 ? WASM_TYPE_I64 : WASM_TYPE_I32;
  for (const Symbol *sym : internalGotSymbols) {
    bool mutable_ = false;
    if (!sym->isStub) {
      // In the case of dynamic linking, unless we have 'extended-const'
      // available, these global must to be mutable since they get updated to
      // the correct runtime value during `__wasm_apply_global_relocs`.
      if (!config->extendedConst && ctx.isPic && !sym->isTLS())
        mutable_ = true;
      // With multi-theadeding any TLS globals must be mutable since they get
      // set during `__wasm_apply_global_tls_relocs`
      if (config->sharedMemory && sym->isTLS())
        mutable_ = true;
    }
    WasmGlobalType type{itype, mutable_};
    writeGlobalType(os, type);

    bool useExtendedConst = false;
    uint32_t globalIdx;
    int64_t offset;
    if (config->extendedConst && ctx.isPic) {
      if (auto *d = dyn_cast<DefinedData>(sym)) {
        if (!sym->isTLS()) {
          globalIdx = WasmSym::memoryBase->getGlobalIndex();
          offset = d->getVA();
          useExtendedConst = true;
        }
      } else if (auto *f = dyn_cast<FunctionSymbol>(sym)) {
        if (!sym->isStub) {
          globalIdx = WasmSym::tableBase->getGlobalIndex();
          offset = f->getTableIndex();
          useExtendedConst = true;
        }
      }
    }
    if (useExtendedConst) {
      // We can use an extended init expression to add a constant
      // offset of __memory_base/__table_base.
      writeU8(os, WASM_OPCODE_GLOBAL_GET, "global get");
      writeUleb128(os, globalIdx, "literal (global index)");
      if (offset) {
        writePtrConst(os, offset, is64, "offset");
        writeU8(os, is64 ? WASM_OPCODE_I64_ADD : WASM_OPCODE_I32_ADD, "add");
      }
      writeU8(os, WASM_OPCODE_END, "opcode:end");
    } else {
      WasmInitExpr initExpr;
      if (auto *d = dyn_cast<DefinedData>(sym))
        initExpr = intConst(d->getVA(), is64);
      else if (auto *f = dyn_cast<FunctionSymbol>(sym))
        initExpr = intConst(f->isStub ? 0 : f->getTableIndex(), is64);
      else {
        assert(isa<UndefinedData>(sym) || isa<SharedData>(sym));
        initExpr = intConst(0, is64);
      }
      writeInitExpr(os, initExpr);
    }
  }
  for (const DefinedData *sym : dataAddressGlobals) {
    WasmGlobalType type{itype, false};
    writeGlobalType(os, type);
    writeInitExpr(os, intConst(sym->getVA(), is64));
  }
}

void GlobalSection::addGlobal(InputGlobal *global) {
  assert(!isSealed);
  if (!global->live)
    return;
  inputGlobals.push_back(global);
}

void ExportSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  writeUleb128(os, exports.size(), "export count");
  for (const WasmExport &export_ : exports)
    writeExport(os, export_);
}

bool StartSection::isNeeded() const {
  return WasmSym::startFunction != nullptr;
}

void StartSection::writeBody() {
  raw_ostream &os = bodyOutputStream;
  writeUleb128(os, WasmSym::startFunction->getFunctionIndex(),
               "function index");
}

void ElemSection::addEntry(FunctionSymbol *sym) {
  // Don't add stub functions to the wasm table.  The address of all stub
  // functions should be zero and they should they don't appear in the table.
  // They only exist so that the calls to missing functions can validate.
  if (sym->hasTableIndex() || sym->isStub)
    return;
  sym->setTableIndex(config->tableBase + indirectFunctions.size());
  indirectFunctions.emplace_back(sym);
}

void ElemSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  assert(WasmSym::indirectFunctionTable);
  writeUleb128(os, 1, "segment count");
  uint32_t tableNumber = WasmSym::indirectFunctionTable->getTableNumber();
  uint32_t flags = 0;
  if (tableNumber)
    flags |= WASM_ELEM_SEGMENT_HAS_TABLE_NUMBER;
  writeUleb128(os, flags, "elem segment flags");
  if (flags & WASM_ELEM_SEGMENT_HAS_TABLE_NUMBER)
    writeUleb128(os, tableNumber, "table number");

  WasmInitExpr initExpr;
  initExpr.Extended = false;
  if (ctx.isPic) {
    initExpr.Inst.Opcode = WASM_OPCODE_GLOBAL_GET;
    initExpr.Inst.Value.Global = WasmSym::tableBase->getGlobalIndex();
  } else {
    bool is64 = config->is64.value_or(false);
    initExpr = intConst(config->tableBase, is64);
  }
  writeInitExpr(os, initExpr);

  if (flags & WASM_ELEM_SEGMENT_MASK_HAS_ELEM_KIND) {
    // We only write active function table initializers, for which the elem kind
    // is specified to be written as 0x00 and interpreted to mean "funcref".
    const uint8_t elemKind = 0;
    writeU8(os, elemKind, "elem kind");
  }

  writeUleb128(os, indirectFunctions.size(), "elem count");
  uint32_t tableIndex = config->tableBase;
  for (const FunctionSymbol *sym : indirectFunctions) {
    assert(sym->getTableIndex() == tableIndex);
    (void) tableIndex;
    writeUleb128(os, sym->getFunctionIndex(), "function index");
    ++tableIndex;
  }
}

DataCountSection::DataCountSection(ArrayRef<OutputSegment *> segments)
    : SyntheticSection(llvm::wasm::WASM_SEC_DATACOUNT),
      numSegments(llvm::count_if(segments, [](OutputSegment *const segment) {
        return segment->requiredInBinary();
      })) {}

void DataCountSection::writeBody() {
  writeUleb128(bodyOutputStream, numSegments, "data count");
}

bool DataCountSection::isNeeded() const {
  return numSegments && config->sharedMemory;
}

void LinkingSection::writeBody() {
  raw_ostream &os = bodyOutputStream;

  writeUleb128(os, WasmMetadataVersion, "Version");

  if (!symtabEntries.empty()) {
    SubSection sub(WASM_SYMBOL_TABLE);
    writeUleb128(sub.os, symtabEntries.size(), "num symbols");

    for (const Symbol *sym : symtabEntries) {
      assert(sym->isDefined() || sym->isUndefined());
      WasmSymbolType kind = sym->getWasmType();
      uint32_t flags = sym->flags;

      writeU8(sub.os, kind, "sym kind");
      writeUleb128(sub.os, flags, "sym flags");

      if (auto *f = dyn_cast<FunctionSymbol>(sym)) {
        if (auto *d = dyn_cast<DefinedFunction>(sym)) {
          writeUleb128(sub.os, d->getExportedFunctionIndex(), "index");
        } else {
          writeUleb128(sub.os, f->getFunctionIndex(), "index");
        }
        if (sym->isDefined() || (flags & WASM_SYMBOL_EXPLICIT_NAME) != 0)
          writeStr(sub.os, sym->getName(), "sym name");
      } else if (auto *g = dyn_cast<GlobalSymbol>(sym)) {
        writeUleb128(sub.os, g->getGlobalIndex(), "index");
        if (sym->isDefined() || (flags & WASM_SYMBOL_EXPLICIT_NAME) != 0)
          writeStr(sub.os, sym->getName(), "sym name");
      } else if (auto *t = dyn_cast<TagSymbol>(sym)) {
        writeUleb128(sub.os, t->getTagIndex(), "index");
        if (sym->isDefined() || (flags & WASM_SYMBOL_EXPLICIT_NAME) != 0)
          writeStr(sub.os, sym->getName(), "sym name");
      } else if (auto *t = dyn_cast<TableSymbol>(sym)) {
        writeUleb128(sub.os, t->getTableNumber(), "table number");
        if (sym->isDefined() || (flags & WASM_SYMBOL_EXPLICIT_NAME) != 0)
          writeStr(sub.os, sym->getName(), "sym name");
      } else if (isa<DataSymbol>(sym)) {
        writeStr(sub.os, sym->getName(), "sym name");
        if (auto *dataSym = dyn_cast<DefinedData>(sym)) {
          if (dataSym->segment) {
            writeUleb128(sub.os, dataSym->getOutputSegmentIndex(), "index");
            writeUleb128(sub.os, dataSym->getOutputSegmentOffset(),
                         "data offset");
          } else {
            writeUleb128(sub.os, 0, "index");
            writeUleb128(sub.os, dataSym->getVA(), "data offset");
          }
          writeUleb128(sub.os, dataSym->getSize(), "data size");
        }
      } else {
        auto *s = cast<OutputSectionSymbol>(sym);
        writeUleb128(sub.os, s->section->sectionIndex, "sym section index");
      }
    }

    sub.writeTo(os);
  }

  if (dataSegments.size()) {
    SubSection sub(WASM_SEGMENT_INFO);
    writeUleb128(sub.os, dataSegments.size(), "num data segments");
    for (const OutputSegment *s : dataSegments) {
      writeStr(sub.os, s->name, "segment name");
      writeUleb128(sub.os, s->alignment, "alignment");
      writeUleb128(sub.os, s->linkingFlags, "flags");
    }
    sub.writeTo(os);
  }

  if (!initFunctions.empty()) {
    SubSection sub(WASM_INIT_FUNCS);
    writeUleb128(sub.os, initFunctions.size(), "num init functions");
    for (const WasmInitEntry &f : initFunctions) {
      writeUleb128(sub.os, f.priority, "priority");
      writeUleb128(sub.os, f.sym->getOutputSymbolIndex(), "function index");
    }
    sub.writeTo(os);
  }

  struct ComdatEntry {
    unsigned kind;
    uint32_t index;
  };
  std::map<StringRef, std::vector<ComdatEntry>> comdats;

  for (const InputFunction *f : out.functionSec->inputFunctions) {
    StringRef comdat = f->getComdatName();
    if (!comdat.empty())
      comdats[comdat].emplace_back(
          ComdatEntry{WASM_COMDAT_FUNCTION, f->getFunctionIndex()});
  }
  for (uint32_t i = 0; i < dataSegments.size(); ++i) {
    const auto &inputSegments = dataSegments[i]->inputSegments;
    if (inputSegments.empty())
      continue;
    StringRef comdat = inputSegments[0]->getComdatName();
#ifndef NDEBUG
    for (const InputChunk *isec : inputSegments)
      assert(isec->getComdatName() == comdat);
#endif
    if (!comdat.empty())
      comdats[comdat].emplace_back(ComdatEntry{WASM_COMDAT_DATA, i});
  }

  if (!comdats.empty()) {
    SubSection sub(WASM_COMDAT_INFO);
    writeUleb128(sub.os, comdats.size(), "num comdats");
    for (const auto &c : comdats) {
      writeStr(sub.os, c.first, "comdat name");
      writeUleb128(sub.os, 0, "comdat flags"); // flags for future use
      writeUleb128(sub.os, c.second.size(), "num entries");
      for (const ComdatEntry &entry : c.second) {
        writeU8(sub.os, entry.kind, "entry kind");
        writeUleb128(sub.os, entry.index, "entry index");
      }
    }
    sub.writeTo(os);
  }
}

void LinkingSection::addToSymtab(Symbol *sym) {
  sym->setOutputSymbolIndex(symtabEntries.size());
  symtabEntries.emplace_back(sym);
}

unsigned NameSection::numNamedFunctions() const {
  unsigned numNames = out.importSec->getNumImportedFunctions();

  for (const InputFunction *f : out.functionSec->inputFunctions)
    if (!f->name.empty() || !f->debugName.empty())
      ++numNames;

  return numNames;
}

unsigned NameSection::numNamedGlobals() const {
  unsigned numNames = out.importSec->getNumImportedGlobals();

  for (const InputGlobal *g : out.globalSec->inputGlobals)
    if (!g->getName().empty())
      ++numNames;

  numNames += out.globalSec->internalGotSymbols.size();
  return numNames;
}

unsigned NameSection::numNamedDataSegments() const {
  unsigned numNames = 0;

  for (const OutputSegment *s : segments)
    if (!s->name.empty() && s->requiredInBinary())
      ++numNames;

  return numNames;
}

// Create the custom "name" section containing debug symbol names.
void NameSection::writeBody() {
  {
    SubSection sub(WASM_NAMES_MODULE);
    StringRef moduleName = config->soName;
    if (config->soName.empty())
      moduleName = llvm::sys::path::filename(config->outputFile);
    writeStr(sub.os, moduleName, "module name");
    sub.writeTo(bodyOutputStream);
  }

  unsigned count = numNamedFunctions();
  if (count) {
    SubSection sub(WASM_NAMES_FUNCTION);
    writeUleb128(sub.os, count, "name count");

    // Function names appear in function index order.  As it happens
    // importedSymbols and inputFunctions are numbered in order with imported
    // functions coming first.
    for (const Symbol *s : out.importSec->importedSymbols) {
      if (auto *f = dyn_cast<FunctionSymbol>(s)) {
        writeUleb128(sub.os, f->getFunctionIndex(), "func index");
        writeStr(sub.os, toString(*s), "symbol name");
      }
    }
    for (const InputFunction *f : out.functionSec->inputFunctions) {
      if (!f->name.empty()) {
        writeUleb128(sub.os, f->getFunctionIndex(), "func index");
        if (!f->debugName.empty()) {
          writeStr(sub.os, f->debugName, "symbol name");
        } else {
          writeStr(sub.os, maybeDemangleSymbol(f->name), "symbol name");
        }
      }
    }
    sub.writeTo(bodyOutputStream);
  }

  count = numNamedGlobals();
  if (count) {
    SubSection sub(WASM_NAMES_GLOBAL);
    writeUleb128(sub.os, count, "name count");

    for (const Symbol *s : out.importSec->importedSymbols) {
      if (auto *g = dyn_cast<GlobalSymbol>(s)) {
        writeUleb128(sub.os, g->getGlobalIndex(), "global index");
        writeStr(sub.os, toString(*s), "symbol name");
      }
    }
    for (const Symbol *s : out.importSec->gotSymbols) {
      writeUleb128(sub.os, s->getGOTIndex(), "global index");
      writeStr(sub.os, toString(*s), "symbol name");
    }
    for (const InputGlobal *g : out.globalSec->inputGlobals) {
      if (!g->getName().empty()) {
        writeUleb128(sub.os, g->getAssignedIndex(), "global index");
        writeStr(sub.os, maybeDemangleSymbol(g->getName()), "symbol name");
      }
    }
    for (Symbol *s : out.globalSec->internalGotSymbols) {
      writeUleb128(sub.os, s->getGOTIndex(), "global index");
      if (isa<FunctionSymbol>(s))
        writeStr(sub.os, "GOT.func.internal." + toString(*s), "symbol name");
      else
        writeStr(sub.os, "GOT.data.internal." + toString(*s), "symbol name");
    }

    sub.writeTo(bodyOutputStream);
  }

  count = numNamedDataSegments();
  if (count) {
    SubSection sub(WASM_NAMES_DATA_SEGMENT);
    writeUleb128(sub.os, count, "name count");

    for (OutputSegment *s : segments) {
      if (!s->name.empty() && s->requiredInBinary()) {
        writeUleb128(sub.os, s->index, "global index");
        writeStr(sub.os, s->name, "segment name");
      }
    }

    sub.writeTo(bodyOutputStream);
  }
}

void ProducersSection::addInfo(const WasmProducerInfo &info) {
  for (auto &producers :
       {std::make_pair(&info.Languages, &languages),
        std::make_pair(&info.Tools, &tools), std::make_pair(&info.SDKs, &sDKs)})
    for (auto &producer : *producers.first)
      if (llvm::none_of(*producers.second,
                        [&](std::pair<std::string, std::string> seen) {
                          return seen.first == producer.first;
                        }))
        producers.second->push_back(producer);
}

void ProducersSection::writeBody() {
  auto &os = bodyOutputStream;
  writeUleb128(os, fieldCount(), "field count");
  for (auto &field :
       {std::make_pair("language", languages),
        std::make_pair("processed-by", tools), std::make_pair("sdk", sDKs)}) {
    if (field.second.empty())
      continue;
    writeStr(os, field.first, "field name");
    writeUleb128(os, field.second.size(), "number of entries");
    for (auto &entry : field.second) {
      writeStr(os, entry.first, "producer name");
      writeStr(os, entry.second, "producer version");
    }
  }
}

void TargetFeaturesSection::writeBody() {
  SmallVector<std::string, 8> emitted(features.begin(), features.end());
  llvm::sort(emitted);
  auto &os = bodyOutputStream;
  writeUleb128(os, emitted.size(), "feature count");
  for (auto &feature : emitted) {
    writeU8(os, WASM_FEATURE_PREFIX_USED, "feature used prefix");
    writeStr(os, feature, "feature name");
  }
}

void RelocSection::writeBody() {
  uint32_t count = sec->getNumRelocations();
  assert(sec->sectionIndex != UINT32_MAX);
  writeUleb128(bodyOutputStream, sec->sectionIndex, "reloc section");
  writeUleb128(bodyOutputStream, count, "reloc count");
  sec->writeRelocations(bodyOutputStream);
}

static size_t getHashSize() {
  switch (config->buildId) {
  case BuildIdKind::Fast:
  case BuildIdKind::Uuid:
    return 16;
  case BuildIdKind::Sha1:
    return 20;
  case BuildIdKind::Hexstring:
    return config->buildIdVector.size();
  case BuildIdKind::None:
    return 0;
  }
  llvm_unreachable("build id kind not implemented");
}

BuildIdSection::BuildIdSection()
    : SyntheticSection(llvm::wasm::WASM_SEC_CUSTOM, buildIdSectionName),
      hashSize(getHashSize()) {}

void BuildIdSection::writeBody() {
  LLVM_DEBUG(llvm::dbgs() << "BuildId writebody\n");
  // Write hash size
  auto &os = bodyOutputStream;
  writeUleb128(os, hashSize, "build id size");
  writeBytes(os, std::vector<char>(hashSize, ' ').data(), hashSize,
             "placeholder");
}

void BuildIdSection::writeBuildId(llvm::ArrayRef<uint8_t> buf) {
  assert(buf.size() == hashSize);
  LLVM_DEBUG(dbgs() << "buildid write " << buf.size() << " "
                    << hashPlaceholderPtr << '\n');
  memcpy(hashPlaceholderPtr, buf.data(), hashSize);
}

} // namespace wasm::lld
