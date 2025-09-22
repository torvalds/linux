//===-LTOModule.h - LLVM Link Time Optimizer ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the LTOModule class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LEGACY_LTOMODULE_H
#define LLVM_LTO_LEGACY_LTOMODULE_H

#include "llvm-c/lto.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Target/TargetMachine.h"
#include <string>
#include <vector>

// Forward references to llvm classes.
namespace llvm {
  class Function;
  class GlobalValue;
  class MemoryBuffer;
  class TargetOptions;
  class Value;

//===----------------------------------------------------------------------===//
/// C++ class which implements the opaque lto_module_t type.
///
struct LTOModule {
private:
  struct NameAndAttributes {
    StringRef name;
    uint32_t           attributes = 0;
    bool               isFunction = false;
    const GlobalValue *symbol = nullptr;
  };

  std::unique_ptr<LLVMContext> OwnedContext;

  std::string LinkerOpts;

  std::unique_ptr<Module> Mod;
  MemoryBufferRef MBRef;
  ModuleSymbolTable SymTab;
  std::unique_ptr<TargetMachine> _target;
  std::vector<NameAndAttributes> _symbols;

  // _defines and _undefines only needed to disambiguate tentative definitions
  StringSet<>                             _defines;
  StringMap<NameAndAttributes> _undefines;
  std::vector<StringRef> _asm_undefines;

  LTOModule(std::unique_ptr<Module> M, MemoryBufferRef MBRef,
            TargetMachine *TM);

public:
  ~LTOModule();

  /// Returns 'true' if the file or memory contents is LLVM bitcode.
  static bool isBitcodeFile(const void *mem, size_t length);
  static bool isBitcodeFile(StringRef path);

  /// Returns 'true' if the Module is produced for ThinLTO.
  bool isThinLTO();

  /// Returns 'true' if the memory buffer is LLVM bitcode for the specified
  /// triple.
  static bool isBitcodeForTarget(MemoryBuffer *memBuffer,
                                 StringRef triplePrefix);

  /// Returns a string representing the producer identification stored in the
  /// bitcode, or "" if the bitcode does not contains any.
  ///
  static std::string getProducerString(MemoryBuffer *Buffer);

  /// Create a MemoryBuffer from a memory range with an optional name.
  static std::unique_ptr<MemoryBuffer>
  makeBuffer(const void *mem, size_t length, StringRef name = "");

  /// Create an LTOModule. N.B. These methods take ownership of the buffer. The
  /// caller must have initialized the Targets, the TargetMCs, the AsmPrinters,
  /// and the AsmParsers by calling:
  ///
  /// InitializeAllTargets();
  /// InitializeAllTargetMCs();
  /// InitializeAllAsmPrinters();
  /// InitializeAllAsmParsers();
  static ErrorOr<std::unique_ptr<LTOModule>>
  createFromFile(LLVMContext &Context, StringRef path,
                 const TargetOptions &options);
  static ErrorOr<std::unique_ptr<LTOModule>>
  createFromOpenFile(LLVMContext &Context, int fd, StringRef path, size_t size,
                     const TargetOptions &options);
  static ErrorOr<std::unique_ptr<LTOModule>>
  createFromOpenFileSlice(LLVMContext &Context, int fd, StringRef path,
                          size_t map_size, off_t offset,
                          const TargetOptions &options);
  static ErrorOr<std::unique_ptr<LTOModule>>
  createFromBuffer(LLVMContext &Context, const void *mem, size_t length,
                   const TargetOptions &options, StringRef path = "");
  static ErrorOr<std::unique_ptr<LTOModule>>
  createInLocalContext(std::unique_ptr<LLVMContext> Context, const void *mem,
                       size_t length, const TargetOptions &options,
                       StringRef path);

  const Module &getModule() const { return *Mod; }
  Module &getModule() { return *Mod; }

  std::unique_ptr<Module> takeModule() { return std::move(Mod); }

  /// Return the Module's target triple.
  const std::string &getTargetTriple() {
    return getModule().getTargetTriple();
  }

  /// Set the Module's target triple.
  void setTargetTriple(StringRef Triple) {
    getModule().setTargetTriple(Triple);
  }

  /// Get the number of symbols
  uint32_t getSymbolCount() {
    return _symbols.size();
  }

  /// Get the attributes for a symbol at the specified index.
  lto_symbol_attributes getSymbolAttributes(uint32_t index) {
    if (index < _symbols.size())
      return lto_symbol_attributes(_symbols[index].attributes);
    return lto_symbol_attributes(0);
  }

  /// Get the name of the symbol at the specified index.
  StringRef getSymbolName(uint32_t index) {
    if (index < _symbols.size())
      return _symbols[index].name;
    return StringRef();
  }

  const GlobalValue *getSymbolGV(uint32_t index) {
    if (index < _symbols.size())
      return _symbols[index].symbol;
    return nullptr;
  }

  StringRef getLinkerOpts() { return LinkerOpts; }

  const std::vector<StringRef> &getAsmUndefinedRefs() { return _asm_undefines; }

  static lto::InputFile *createInputFile(const void *buffer, size_t buffer_size,
                                         const char *path, std::string &out_error);

  static size_t getDependentLibraryCount(lto::InputFile *input);

  static const char *getDependentLibrary(lto::InputFile *input, size_t index, size_t *size);

  Expected<uint32_t> getMachOCPUType() const;

  Expected<uint32_t> getMachOCPUSubType() const;

  /// Returns true if the module has either the @llvm.global_ctors or the
  /// @llvm.global_dtors symbol. Otherwise returns false.
  bool hasCtorDtor() const;

private:
  /// Parse metadata from the module
  // FIXME: it only parses "llvm.linker.options" metadata at the moment
  // FIXME: can't access metadata in lazily loaded modules
  void parseMetadata();

  /// Parse the symbols from the module and model-level ASM and add them to
  /// either the defined or undefined lists.
  void parseSymbols();

  /// Add a symbol which isn't defined just yet to a list to be resolved later.
  void addPotentialUndefinedSymbol(ModuleSymbolTable::Symbol Sym,
                                   bool isFunc);

  /// Add a defined symbol to the list.
  void addDefinedSymbol(StringRef Name, const GlobalValue *def,
                        bool isFunction);

  /// Add a data symbol as defined to the list.
  void addDefinedDataSymbol(ModuleSymbolTable::Symbol Sym);
  void addDefinedDataSymbol(StringRef Name, const GlobalValue *v);

  /// Add a function symbol as defined to the list.
  void addDefinedFunctionSymbol(ModuleSymbolTable::Symbol Sym);
  void addDefinedFunctionSymbol(StringRef Name, const Function *F);

  /// Add a global symbol from module-level ASM to the defined list.
  void addAsmGlobalSymbol(StringRef, lto_symbol_attributes scope);

  /// Add a global symbol from module-level ASM to the undefined list.
  void addAsmGlobalSymbolUndef(StringRef);

  /// Parse i386/ppc ObjC class data structure.
  void addObjCClass(const GlobalVariable *clgv);

  /// Parse i386/ppc ObjC category data structure.
  void addObjCCategory(const GlobalVariable *clgv);

  /// Parse i386/ppc ObjC class list data structure.
  void addObjCClassRef(const GlobalVariable *clgv);

  /// Get string that the data pointer points to.
  bool objcClassNameFromExpression(const Constant *c, std::string &name);

  /// Create an LTOModule (private version).
  static ErrorOr<std::unique_ptr<LTOModule>>
  makeLTOModule(MemoryBufferRef Buffer, const TargetOptions &options,
                LLVMContext &Context, bool ShouldBeLazy);
};
}
#endif
