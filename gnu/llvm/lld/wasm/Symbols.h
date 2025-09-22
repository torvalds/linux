//===- Symbols.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_SYMBOLS_H
#define LLD_WASM_SYMBOLS_H

#include "Config.h"
#include "lld/Common/LLVM.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Wasm.h"
#include <optional>

namespace lld {
namespace wasm {

// Shared string constants

// The default module name to use for symbol imports.
extern const char *defaultModule;

// The name under which to import or export the wasm table.
extern const char *functionTableName;

// The name under which to import or export the wasm memory.
extern const char *memoryName;

using llvm::wasm::WasmSymbolType;

class InputFile;
class InputChunk;
class InputSegment;
class InputFunction;
class InputGlobal;
class InputTag;
class InputSection;
class InputTable;
class OutputSection;

#define INVALID_INDEX UINT32_MAX

// The base class for real symbol classes.
class Symbol {
public:
  enum Kind : uint8_t {
    DefinedFunctionKind,
    DefinedDataKind,
    DefinedGlobalKind,
    DefinedTagKind,
    DefinedTableKind,
    SectionKind,
    OutputSectionKind,
    UndefinedFunctionKind,
    UndefinedDataKind,
    UndefinedGlobalKind,
    UndefinedTableKind,
    UndefinedTagKind,
    LazyKind,
    SharedFunctionKind,
    SharedDataKind,
  };

  Kind kind() const { return symbolKind; }

  bool isDefined() const { return !isLazy() && !isUndefined(); }

  bool isUndefined() const {
    return symbolKind == UndefinedFunctionKind ||
           symbolKind == UndefinedDataKind ||
           symbolKind == UndefinedGlobalKind ||
           symbolKind == UndefinedTableKind || symbolKind == UndefinedTagKind;
  }

  bool isLazy() const { return symbolKind == LazyKind; }
  bool isShared() const {
    return symbolKind == SharedFunctionKind || symbolKind == SharedDataKind;
  }

  bool isLocal() const;
  bool isWeak() const;
  bool isHidden() const;
  bool isTLS() const;

  // Returns true if this symbol exists in a discarded (due to COMDAT) section
  bool isDiscarded() const;

  // True if this is an undefined weak symbol. This only works once
  // all input files have been added.
  bool isUndefWeak() const {
    // See comment on lazy symbols for details.
    return isWeak() && (isUndefined() || isLazy());
  }

  // Returns the symbol name.
  StringRef getName() const { return name; }

  // Returns the file from which this symbol was created.
  InputFile *getFile() const { return file; }

  InputChunk *getChunk() const;

  // Indicates that the section or import for this symbol will be included in
  // the final image.
  bool isLive() const;

  // Marks the symbol's InputChunk as Live, so that it will be included in the
  // final image.
  void markLive();

  void setHidden(bool isHidden);

  // Get/set the index in the output symbol table.  This is only used for
  // relocatable output.
  uint32_t getOutputSymbolIndex() const;
  void setOutputSymbolIndex(uint32_t index);

  WasmSymbolType getWasmType() const;
  bool isImported() const;
  bool isExported() const;
  bool isExportedExplicit() const;

  // Indicates that the symbol is used in an __attribute__((used)) directive
  // or similar.
  bool isNoStrip() const;

  const WasmSignature* getSignature() const;

  uint32_t getGOTIndex() const {
    assert(gotIndex != INVALID_INDEX);
    return gotIndex;
  }

  void setGOTIndex(uint32_t index);
  bool hasGOTIndex() const { return gotIndex != INVALID_INDEX; }

protected:
  Symbol(StringRef name, Kind k, uint32_t flags, InputFile *f)
      : name(name), file(f), symbolKind(k), referenced(!config->gcSections),
        requiresGOT(false), isUsedInRegularObj(false), forceExport(false),
        forceImport(false), canInline(false), traced(false), isStub(false),
        flags(flags) {}

  StringRef name;
  InputFile *file;
  uint32_t outputSymbolIndex = INVALID_INDEX;
  uint32_t gotIndex = INVALID_INDEX;
  Kind symbolKind;

public:
  bool referenced : 1;

  // True for data symbols that needs a dummy GOT entry.  Used for static
  // linking of GOT accesses.
  bool requiresGOT : 1;

  // True if the symbol was used for linking and thus need to be added to the
  // output file's symbol table. This is true for all symbols except for
  // unreferenced DSO symbols, lazy (archive) symbols, and bitcode symbols that
  // are unreferenced except by other bitcode objects.
  bool isUsedInRegularObj : 1;

  // True if this symbol is explicitly marked for export (i.e. via the
  // -e/--export command line flag)
  bool forceExport : 1;

  bool forceImport : 1;

  // False if LTO shouldn't inline whatever this symbol points to. If a symbol
  // is overwritten after LTO, LTO shouldn't inline the symbol because it
  // doesn't know the final contents of the symbol.
  bool canInline : 1;

  // True if this symbol is specified by --trace-symbol option.
  bool traced : 1;

  // True if this symbol is a linker-synthesized stub function (traps when
  // called) and should otherwise be treated as missing/undefined.  See
  // SymbolTable::replaceWithUndefined.
  // These stubs never appear in the table and any table index relocations
  // against them will produce address 0 (The table index representing
  // the null function pointer).
  bool isStub : 1;

  uint32_t flags;

  std::optional<StringRef> importName;
  std::optional<StringRef> importModule;
};

class FunctionSymbol : public Symbol {
public:
  static bool classof(const Symbol *s) {
    return s->kind() == DefinedFunctionKind ||
           s->kind() == SharedFunctionKind ||
           s->kind() == UndefinedFunctionKind;
  }

  // Get/set the table index
  void setTableIndex(uint32_t index);
  uint32_t getTableIndex() const;
  bool hasTableIndex() const;

  // Get/set the function index
  uint32_t getFunctionIndex() const;
  void setFunctionIndex(uint32_t index);
  bool hasFunctionIndex() const;

  const WasmSignature *signature;

protected:
  FunctionSymbol(StringRef name, Kind k, uint32_t flags, InputFile *f,
                 const WasmSignature *sig)
      : Symbol(name, k, flags, f), signature(sig) {}

  uint32_t tableIndex = INVALID_INDEX;
  uint32_t functionIndex = INVALID_INDEX;
};

class DefinedFunction : public FunctionSymbol {
public:
  DefinedFunction(StringRef name, uint32_t flags, InputFile *f,
                  InputFunction *function);

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedFunctionKind;
  }

  // Get the function index to be used when exporting.  This only applies to
  // defined functions and can be differ from the regular function index for
  // weakly defined functions (that are imported and used via one index but
  // defined and exported via another).
  uint32_t getExportedFunctionIndex() const;

  InputFunction *function;
};

class UndefinedFunction : public FunctionSymbol {
public:
  UndefinedFunction(StringRef name, std::optional<StringRef> importName,
                    std::optional<StringRef> importModule, uint32_t flags,
                    InputFile *file = nullptr,
                    const WasmSignature *type = nullptr,
                    bool isCalledDirectly = true)
      : FunctionSymbol(name, UndefinedFunctionKind, flags, file, type),
        isCalledDirectly(isCalledDirectly) {
    this->importName = importName;
    this->importModule = importModule;
  }

  static bool classof(const Symbol *s) {
    return s->kind() == UndefinedFunctionKind;
  }

  DefinedFunction *stubFunction = nullptr;
  bool isCalledDirectly;
};

// Section symbols for output sections are different from those for input
// section.  These are generated by the linker and point the OutputSection
// rather than an InputSection.
class OutputSectionSymbol : public Symbol {
public:
  OutputSectionSymbol(const OutputSection *s)
      : Symbol("", OutputSectionKind, llvm::wasm::WASM_SYMBOL_BINDING_LOCAL,
               nullptr),
        section(s) {}

  static bool classof(const Symbol *s) {
    return s->kind() == OutputSectionKind;
  }

  const OutputSection *section;
};

class SectionSymbol : public Symbol {
public:
  SectionSymbol(uint32_t flags, const InputChunk *s, InputFile *f = nullptr)
      : Symbol("", SectionKind, flags, f), section(s) {}

  static bool classof(const Symbol *s) { return s->kind() == SectionKind; }

  const OutputSectionSymbol *getOutputSectionSymbol() const;

  const InputChunk *section;
};

class DataSymbol : public Symbol {
public:
  static bool classof(const Symbol *s) {
    return s->kind() == DefinedDataKind || s->kind() == UndefinedDataKind ||
           s->kind() == SharedDataKind;
  }

protected:
  DataSymbol(StringRef name, Kind k, uint32_t flags, InputFile *f)
      : Symbol(name, k, flags, f) {}
};

class DefinedData : public DataSymbol {
public:
  // Constructor for regular data symbols originating from input files.
  DefinedData(StringRef name, uint32_t flags, InputFile *f, InputChunk *segment,
              uint64_t value, uint64_t size)
      : DataSymbol(name, DefinedDataKind, flags, f), segment(segment),
        value(value), size(size) {}

  // Constructor for linker synthetic data symbols.
  DefinedData(StringRef name, uint32_t flags)
      : DataSymbol(name, DefinedDataKind, flags, nullptr) {}

  static bool classof(const Symbol *s) { return s->kind() == DefinedDataKind; }

  // Returns the output virtual address of a defined data symbol.
  uint64_t getVA() const;
  void setVA(uint64_t va);

  // Returns the offset of a defined data symbol within its OutputSegment.
  uint64_t getOutputSegmentOffset() const;
  uint64_t getOutputSegmentIndex() const;
  uint64_t getSize() const { return size; }

  InputChunk *segment = nullptr;
  uint64_t value = 0;

protected:
  uint64_t size = 0;
};

class SharedData : public DataSymbol {
public:
  SharedData(StringRef name, uint32_t flags, InputFile *f)
      : DataSymbol(name, SharedDataKind, flags, f) {}
};

class UndefinedData : public DataSymbol {
public:
  UndefinedData(StringRef name, uint32_t flags, InputFile *file = nullptr)
      : DataSymbol(name, UndefinedDataKind, flags, file) {}
  static bool classof(const Symbol *s) {
    return s->kind() == UndefinedDataKind;
  }
};

class GlobalSymbol : public Symbol {
public:
  static bool classof(const Symbol *s) {
    return s->kind() == DefinedGlobalKind || s->kind() == UndefinedGlobalKind;
  }

  const WasmGlobalType *getGlobalType() const { return globalType; }

  // Get/set the global index
  uint32_t getGlobalIndex() const;
  void setGlobalIndex(uint32_t index);
  bool hasGlobalIndex() const;

protected:
  GlobalSymbol(StringRef name, Kind k, uint32_t flags, InputFile *f,
               const WasmGlobalType *globalType)
      : Symbol(name, k, flags, f), globalType(globalType) {}

  const WasmGlobalType *globalType;
  uint32_t globalIndex = INVALID_INDEX;
};

class DefinedGlobal : public GlobalSymbol {
public:
  DefinedGlobal(StringRef name, uint32_t flags, InputFile *file,
                InputGlobal *global);

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedGlobalKind;
  }

  InputGlobal *global;
};

class UndefinedGlobal : public GlobalSymbol {
public:
  UndefinedGlobal(StringRef name, std::optional<StringRef> importName,
                  std::optional<StringRef> importModule, uint32_t flags,
                  InputFile *file = nullptr,
                  const WasmGlobalType *type = nullptr)
      : GlobalSymbol(name, UndefinedGlobalKind, flags, file, type) {
    this->importName = importName;
    this->importModule = importModule;
  }

  static bool classof(const Symbol *s) {
    return s->kind() == UndefinedGlobalKind;
  }
};

class TableSymbol : public Symbol {
public:
  static bool classof(const Symbol *s) {
    return s->kind() == DefinedTableKind || s->kind() == UndefinedTableKind;
  }

  const WasmTableType *getTableType() const { return tableType; }
  void setLimits(const WasmLimits &limits);

  // Get/set the table number
  uint32_t getTableNumber() const;
  void setTableNumber(uint32_t number);
  bool hasTableNumber() const;

protected:
  TableSymbol(StringRef name, Kind k, uint32_t flags, InputFile *f,
              const WasmTableType *type)
      : Symbol(name, k, flags, f), tableType(type) {}

  const WasmTableType *tableType;
  uint32_t tableNumber = INVALID_INDEX;
};

class DefinedTable : public TableSymbol {
public:
  DefinedTable(StringRef name, uint32_t flags, InputFile *file,
               InputTable *table);

  static bool classof(const Symbol *s) { return s->kind() == DefinedTableKind; }

  InputTable *table;
};

class UndefinedTable : public TableSymbol {
public:
  UndefinedTable(StringRef name, std::optional<StringRef> importName,
                 std::optional<StringRef> importModule, uint32_t flags,
                 InputFile *file, const WasmTableType *type)
      : TableSymbol(name, UndefinedTableKind, flags, file, type) {
    this->importName = importName;
    this->importModule = importModule;
  }

  static bool classof(const Symbol *s) {
    return s->kind() == UndefinedTableKind;
  }
};

// A tag is a general format to distinguish typed entities. Each tag has an
// attribute and a type. Currently the attribute can only specify that the tag
// is for an exception tag.
//
// In exception handling, tags are used to distinguish different kinds of
// exceptions. For example, they can be used to distinguish different language's
// exceptions, e.g., all C++ exceptions have the same tag and Java exceptions
// would have a distinct tag. Wasm can filter the exceptions it catches based on
// their tag.
//
// A single TagSymbol object represents a single tag. The C++ exception symbol
// is a weak symbol generated in every object file in which exceptions are used,
// and is named '__cpp_exception' for linking.
class TagSymbol : public Symbol {
public:
  static bool classof(const Symbol *s) {
    return s->kind() == DefinedTagKind || s->kind() == UndefinedTagKind;
  }

  // Get/set the tag index
  uint32_t getTagIndex() const;
  void setTagIndex(uint32_t index);
  bool hasTagIndex() const;

  const WasmSignature *signature;

protected:
  TagSymbol(StringRef name, Kind k, uint32_t flags, InputFile *f,
            const WasmSignature *sig)
      : Symbol(name, k, flags, f), signature(sig) {}

  uint32_t tagIndex = INVALID_INDEX;
};

class DefinedTag : public TagSymbol {
public:
  DefinedTag(StringRef name, uint32_t flags, InputFile *file, InputTag *tag);

  static bool classof(const Symbol *s) { return s->kind() == DefinedTagKind; }

  InputTag *tag;
};

class UndefinedTag : public TagSymbol {
public:
  UndefinedTag(StringRef name, std::optional<StringRef> importName,
               std::optional<StringRef> importModule, uint32_t flags,
               InputFile *file = nullptr, const WasmSignature *sig = nullptr)
      : TagSymbol(name, UndefinedTagKind, flags, file, sig) {
    this->importName = importName;
    this->importModule = importModule;
  }

  static bool classof(const Symbol *s) { return s->kind() == UndefinedTagKind; }
};

class SharedFunctionSymbol : public FunctionSymbol {
public:
  SharedFunctionSymbol(StringRef name, uint32_t flags, InputFile *file,
                       const WasmSignature *sig)
      : FunctionSymbol(name, SharedFunctionKind, flags, file, sig) {}
  static bool classof(const Symbol *s) {
    return s->kind() == SharedFunctionKind;
  }
};

// LazySymbol symbols represent symbols in object files between --start-lib and
// --end-lib options. LLD also handles traditional archives as if all the files
// in the archive are surrounded by --start-lib and --end-lib.
//
// A special complication is the handling of weak undefined symbols. They should
// not load a file, but we have to remember we have seen both the weak undefined
// and the lazy. We represent that with a lazy symbol with a weak binding. This
// means that code looking for undefined symbols normally also has to take lazy
// symbols into consideration.
class LazySymbol : public Symbol {
public:
  LazySymbol(StringRef name, uint32_t flags, InputFile *file)
      : Symbol(name, LazyKind, flags, file) {}

  static bool classof(const Symbol *s) { return s->kind() == LazyKind; }
  void extract();
  void setWeak();

  // Lazy symbols can have a signature because they can replace an
  // UndefinedFunction in which case we need to be able to preserve the
  // signature.
  // TODO(sbc): This repetition of the signature field is inelegant.  Revisit
  // the use of class hierarchy to represent symbol taxonomy.
  const WasmSignature *signature = nullptr;
};

// linker-generated symbols
struct WasmSym {
  // __global_base
  // Symbol marking the start of the global section.
  static DefinedData *globalBase;

  // __stack_pointer/__stack_low/__stack_high
  // Global that holds current value of stack pointer and data symbols marking
  // the start and end of the stack region.  stackPointer is initialized to
  // stackHigh and grows downwards towards stackLow
  static GlobalSymbol *stackPointer;
  static DefinedData *stackLow;
  static DefinedData *stackHigh;

  // __tls_base
  // Global that holds the address of the base of the current thread's
  // TLS block.
  static GlobalSymbol *tlsBase;

  // __tls_size
  // Symbol whose value is the size of the TLS block.
  static GlobalSymbol *tlsSize;

  // __tls_size
  // Symbol whose value is the alignment of the TLS block.
  static GlobalSymbol *tlsAlign;

  // __data_end
  // Symbol marking the end of the data and bss.
  static DefinedData *dataEnd;

  // __heap_base/__heap_end
  // Symbols marking the beginning and end of the "heap". It starts at the end
  // of the data, bss and explicit stack, and extends to the end of the linear
  // memory allocated by wasm-ld. This region of memory is not used by the
  // linked code, so it may be used as a backing store for `sbrk` or `malloc`
  // implementations.
  static DefinedData *heapBase;
  static DefinedData *heapEnd;

  // __wasm_init_memory_flag
  // Symbol whose contents are nonzero iff memory has already been initialized.
  static DefinedData *initMemoryFlag;

  // __wasm_init_memory
  // Function that initializes passive data segments during instantiation.
  static DefinedFunction *initMemory;

  // __wasm_call_ctors
  // Function that directly calls all ctors in priority order.
  static DefinedFunction *callCtors;

  // __wasm_call_dtors
  // Function that calls the libc/etc. cleanup function.
  static DefinedFunction *callDtors;

  // __wasm_apply_data_relocs
  // Function that applies relocations to data segment post-instantiation.
  static DefinedFunction *applyDataRelocs;

  // __wasm_apply_global_relocs
  // Function that applies relocations to wasm globals post-instantiation.
  // Unlike __wasm_apply_data_relocs this needs to run on every thread.
  static DefinedFunction *applyGlobalRelocs;

  // __wasm_apply_tls_relocs
  // Like applyDataRelocs but for TLS section.  These must be delayed until
  // __wasm_init_tls.
  static DefinedFunction *applyTLSRelocs;

  // __wasm_apply_global_tls_relocs
  // Like applyGlobalRelocs but for globals that hold TLS addresses.  These
  // must be delayed until __wasm_init_tls.
  static DefinedFunction *applyGlobalTLSRelocs;

  // __wasm_init_tls
  // Function that allocates thread-local storage and initializes it.
  static DefinedFunction *initTLS;

  // Pointer to the function that is to be used in the start section.
  // (normally an alias of initMemory, or applyGlobalRelocs).
  static DefinedFunction *startFunction;

  // __dso_handle
  // Symbol used in calls to __cxa_atexit to determine current DLL
  static DefinedData *dsoHandle;

  // __table_base
  // Used in PIC code for offset of indirect function table
  static UndefinedGlobal *tableBase;
  static DefinedData *definedTableBase;

  // __memory_base
  // Used in PIC code for offset of global data
  static UndefinedGlobal *memoryBase;
  static DefinedData *definedMemoryBase;

  // __indirect_function_table
  // Used as an address space for function pointers, with each function that is
  // used as a function pointer being allocated a slot.
  static TableSymbol *indirectFunctionTable;
};

// A buffer class that is large enough to hold any Symbol-derived
// object. We allocate memory using this class and instantiate a symbol
// using the placement new.
union SymbolUnion {
  alignas(DefinedFunction) char a[sizeof(DefinedFunction)];
  alignas(DefinedData) char b[sizeof(DefinedData)];
  alignas(DefinedGlobal) char c[sizeof(DefinedGlobal)];
  alignas(DefinedTag) char d[sizeof(DefinedTag)];
  alignas(DefinedTable) char e[sizeof(DefinedTable)];
  alignas(LazySymbol) char f[sizeof(LazySymbol)];
  alignas(UndefinedFunction) char g[sizeof(UndefinedFunction)];
  alignas(UndefinedData) char h[sizeof(UndefinedData)];
  alignas(UndefinedGlobal) char i[sizeof(UndefinedGlobal)];
  alignas(UndefinedTable) char j[sizeof(UndefinedTable)];
  alignas(SectionSymbol) char k[sizeof(SectionSymbol)];
  alignas(SharedFunctionSymbol) char l[sizeof(SharedFunctionSymbol)];
};

// It is important to keep the size of SymbolUnion small for performance and
// memory usage reasons. 96 bytes is a soft limit based on the size of
// UndefinedFunction on a 64-bit system.
static_assert(sizeof(SymbolUnion) <= 120, "SymbolUnion too large");

void printTraceSymbol(Symbol *sym);
void printTraceSymbolUndefined(StringRef name, const InputFile* file);

template <typename T, typename... ArgT>
T *replaceSymbol(Symbol *s, ArgT &&... arg) {
  static_assert(std::is_trivially_destructible<T>(),
                "Symbol types must be trivially destructible");
  static_assert(sizeof(T) <= sizeof(SymbolUnion), "SymbolUnion too small");
  static_assert(alignof(T) <= alignof(SymbolUnion),
                "SymbolUnion not aligned enough");
  assert(static_cast<Symbol *>(static_cast<T *>(nullptr)) == nullptr &&
         "Not a Symbol");

  Symbol symCopy = *s;

  T *s2 = new (s) T(std::forward<ArgT>(arg)...);
  s2->isUsedInRegularObj = symCopy.isUsedInRegularObj;
  s2->forceExport = symCopy.forceExport;
  s2->forceImport = symCopy.forceImport;
  s2->canInline = symCopy.canInline;
  s2->traced = symCopy.traced;
  s2->referenced = symCopy.referenced;

  // Print out a log message if --trace-symbol was specified.
  // This is for debugging.
  if (s2->traced)
    printTraceSymbol(s2);

  return s2;
}

} // namespace wasm

// Returns a symbol name for an error message.
std::string toString(const wasm::Symbol &sym);
std::string toString(wasm::Symbol::Kind kind);
std::string maybeDemangleSymbol(StringRef name);

} // namespace lld

#endif
