//===-- LVReader.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVReader class, which is used to describe a debug
// information reader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVREADER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVREADER_H

#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/Core/LVRange.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/ToolOutputFile.h"
#include <map>

namespace llvm {
namespace logicalview {

constexpr LVSectionIndex UndefinedSectionIndex = 0;

class LVScopeCompileUnit;
class LVObject;

class LVSplitContext final {
  std::unique_ptr<ToolOutputFile> OutputFile;
  std::string Location;

public:
  LVSplitContext() = default;
  LVSplitContext(const LVSplitContext &) = delete;
  LVSplitContext &operator=(const LVSplitContext &) = delete;
  ~LVSplitContext() = default;

  Error createSplitFolder(StringRef Where);
  std::error_code open(std::string Name, std::string Extension,
                       raw_ostream &OS);
  void close() {
    if (OutputFile) {
      OutputFile->os().close();
      OutputFile = nullptr;
    }
  }

  std::string getLocation() const { return Location; }
  raw_fd_ostream &os() { return OutputFile->os(); }
};

/// The logical reader owns of all the logical elements created during
/// the debug information parsing. For its creation it uses a specific
///  bump allocator for each type of logical element.
class LVReader {
  LVBinaryType BinaryType;

  // Context used by '--output=split' command line option.
  LVSplitContext SplitContext;

  // Compile Units DIE Offset => Scope.
  using LVCompileUnits = std::map<LVOffset, LVScopeCompileUnit *>;
  LVCompileUnits CompileUnits;

  // Added elements to be used during elements comparison.
  LVLines Lines;
  LVScopes Scopes;
  LVSymbols Symbols;
  LVTypes Types;

  // Create split folder.
  Error createSplitFolder();
  bool OutputSplit = false;

// Define a specific bump allocator for the given KIND.
#define LV_OBJECT_ALLOCATOR(KIND)                                              \
  llvm::SpecificBumpPtrAllocator<LV##KIND> Allocated##KIND;

  // Lines allocator.
  LV_OBJECT_ALLOCATOR(Line)
  LV_OBJECT_ALLOCATOR(LineDebug)
  LV_OBJECT_ALLOCATOR(LineAssembler)

  // Locations allocator.
  LV_OBJECT_ALLOCATOR(Location)
  LV_OBJECT_ALLOCATOR(LocationSymbol)

  // Operations allocator.
  LV_OBJECT_ALLOCATOR(Operation)

  // Scopes allocator.
  LV_OBJECT_ALLOCATOR(Scope)
  LV_OBJECT_ALLOCATOR(ScopeAggregate)
  LV_OBJECT_ALLOCATOR(ScopeAlias)
  LV_OBJECT_ALLOCATOR(ScopeArray)
  LV_OBJECT_ALLOCATOR(ScopeCompileUnit)
  LV_OBJECT_ALLOCATOR(ScopeEnumeration)
  LV_OBJECT_ALLOCATOR(ScopeFormalPack)
  LV_OBJECT_ALLOCATOR(ScopeFunction)
  LV_OBJECT_ALLOCATOR(ScopeFunctionInlined)
  LV_OBJECT_ALLOCATOR(ScopeFunctionType)
  LV_OBJECT_ALLOCATOR(ScopeNamespace)
  LV_OBJECT_ALLOCATOR(ScopeRoot)
  LV_OBJECT_ALLOCATOR(ScopeTemplatePack)

  // Symbols allocator.
  LV_OBJECT_ALLOCATOR(Symbol)

  // Types allocator.
  LV_OBJECT_ALLOCATOR(Type)
  LV_OBJECT_ALLOCATOR(TypeDefinition)
  LV_OBJECT_ALLOCATOR(TypeEnumerator)
  LV_OBJECT_ALLOCATOR(TypeImport)
  LV_OBJECT_ALLOCATOR(TypeParam)
  LV_OBJECT_ALLOCATOR(TypeSubrange)

#undef LV_OBJECT_ALLOCATOR

protected:
  LVScopeRoot *Root = nullptr;
  std::string InputFilename;
  std::string FileFormatName;
  ScopedPrinter &W;
  raw_ostream &OS;
  LVScopeCompileUnit *CompileUnit = nullptr;

  // Only for ELF format. The CodeView is handled in a different way.
  LVSectionIndex DotTextSectionIndex = UndefinedSectionIndex;

  // Record Compilation Unit entry.
  void addCompileUnitOffset(LVOffset Offset, LVScopeCompileUnit *CompileUnit) {
    CompileUnits.emplace(Offset, CompileUnit);
  }

  // Create the Scope Root.
  virtual Error createScopes() {
    Root = createScopeRoot();
    Root->setName(getFilename());
    if (options().getAttributeFormat())
      Root->setFileFormatName(FileFormatName);
    return Error::success();
  }

  // Return a pathname composed by: parent_path(InputFilename)/filename(From).
  // This is useful when a type server (PDB file associated with an object
  // file or a precompiled header file) or a DWARF split object have been
  // moved from their original location. That is the case when running
  // regression tests, where object files are created in one location and
  // executed in a different location.
  std::string createAlternativePath(StringRef From) {
    // During the reader initialization, any backslashes in 'InputFilename'
    // are converted to forward slashes.
    SmallString<128> Path;
    sys::path::append(Path, sys::path::Style::posix,
                      sys::path::parent_path(InputFilename),
                      sys::path::filename(sys::path::convert_to_slash(
                          From, sys::path::Style::windows)));
    return std::string(Path);
  }

  virtual Error printScopes();
  virtual Error printMatchedElements(bool UseMatchedElements);
  virtual void sortScopes() {}

public:
  LVReader() = delete;
  LVReader(StringRef InputFilename, StringRef FileFormatName, ScopedPrinter &W,
           LVBinaryType BinaryType = LVBinaryType::NONE)
      : BinaryType(BinaryType), OutputSplit(options().getOutputSplit()),
        InputFilename(InputFilename), FileFormatName(FileFormatName), W(W),
        OS(W.getOStream()) {}
  LVReader(const LVReader &) = delete;
  LVReader &operator=(const LVReader &) = delete;
  virtual ~LVReader() = default;

// Creates a logical object of the given KIND. The signature for the created
// functions looks like:
//   ...
//   LVScope *createScope()
//   LVScopeRoot *creatScopeRoot()
//   LVType *createType();
//   ...
#define LV_CREATE_OBJECT(KIND)                                                 \
  LV##KIND *create##KIND() {                                                   \
    return new (Allocated##KIND.Allocate()) LV##KIND();                        \
  }

  // Lines creation.
  LV_CREATE_OBJECT(Line)
  LV_CREATE_OBJECT(LineDebug)
  LV_CREATE_OBJECT(LineAssembler)

  // Locations creation.
  LV_CREATE_OBJECT(Location)
  LV_CREATE_OBJECT(LocationSymbol)

  // Scopes creation.
  LV_CREATE_OBJECT(Scope)
  LV_CREATE_OBJECT(ScopeAggregate)
  LV_CREATE_OBJECT(ScopeAlias)
  LV_CREATE_OBJECT(ScopeArray)
  LV_CREATE_OBJECT(ScopeCompileUnit)
  LV_CREATE_OBJECT(ScopeEnumeration)
  LV_CREATE_OBJECT(ScopeFormalPack)
  LV_CREATE_OBJECT(ScopeFunction)
  LV_CREATE_OBJECT(ScopeFunctionInlined)
  LV_CREATE_OBJECT(ScopeFunctionType)
  LV_CREATE_OBJECT(ScopeNamespace)
  LV_CREATE_OBJECT(ScopeRoot)
  LV_CREATE_OBJECT(ScopeTemplatePack)

  // Symbols creation.
  LV_CREATE_OBJECT(Symbol)

  // Types creation.
  LV_CREATE_OBJECT(Type)
  LV_CREATE_OBJECT(TypeDefinition)
  LV_CREATE_OBJECT(TypeEnumerator)
  LV_CREATE_OBJECT(TypeImport)
  LV_CREATE_OBJECT(TypeParam)
  LV_CREATE_OBJECT(TypeSubrange)

#undef LV_CREATE_OBJECT

  // Operations creation.
  LVOperation *createOperation(LVSmall OpCode, ArrayRef<LVUnsigned> Operands) {
    return new (AllocatedOperation.Allocate()) LVOperation(OpCode, Operands);
  }

  StringRef getFilename(LVObject *Object, size_t Index) const;
  StringRef getFilename() const { return InputFilename; }
  void setFilename(std::string Name) { InputFilename = std::move(Name); }
  StringRef getFileFormatName() const { return FileFormatName; }

  raw_ostream &outputStream() { return OS; }

  bool isBinaryTypeNone() const { return BinaryType == LVBinaryType::NONE; }
  bool isBinaryTypeELF() const { return BinaryType == LVBinaryType::ELF; }
  bool isBinaryTypeCOFF() const { return BinaryType == LVBinaryType::COFF; }

  LVScopeCompileUnit *getCompileUnit() const { return CompileUnit; }
  void setCompileUnit(LVScope *Scope) {
    assert(Scope && Scope->isCompileUnit() && "Scope is not a compile unit");
    CompileUnit = static_cast<LVScopeCompileUnit *>(Scope);
  }
  void setCompileUnitCPUType(codeview::CPUType Type) {
    CompileUnit->setCPUType(Type);
  }
  codeview::CPUType getCompileUnitCPUType() {
    return CompileUnit->getCPUType();
  }

  // Access to the scopes root.
  LVScopeRoot *getScopesRoot() const { return Root; }

  Error doPrint();
  Error doLoad();

  virtual std::string getRegisterName(LVSmall Opcode,
                                      ArrayRef<uint64_t> Operands) {
    llvm_unreachable("Invalid instance reader.");
    return {};
  }

  LVSectionIndex getDotTextSectionIndex() const { return DotTextSectionIndex; }
  virtual LVSectionIndex getSectionIndex(LVScope *Scope) {
    return getDotTextSectionIndex();
  }

  virtual bool isSystemEntry(LVElement *Element, StringRef Name = {}) const {
    return false;
  };

  // Access to split context.
  LVSplitContext &getSplitContext() { return SplitContext; }

  // In the case of element comparison, register that added element.
  void notifyAddedElement(LVLine *Line) {
    if (!options().getCompareContext() && options().getCompareLines())
      Lines.push_back(Line);
  }
  void notifyAddedElement(LVScope *Scope) {
    if (!options().getCompareContext() && options().getCompareScopes())
      Scopes.push_back(Scope);
  }
  void notifyAddedElement(LVSymbol *Symbol) {
    if (!options().getCompareContext() && options().getCompareSymbols())
      Symbols.push_back(Symbol);
  }
  void notifyAddedElement(LVType *Type) {
    if (!options().getCompareContext() && options().getCompareTypes())
      Types.push_back(Type);
  }

  const LVLines &getLines() const { return Lines; }
  const LVScopes &getScopes() const { return Scopes; }
  const LVSymbols &getSymbols() const { return Symbols; }
  const LVTypes &getTypes() const { return Types; }

  // Conditions to print an object.
  bool doPrintLine(const LVLine *Line) const {
    return patterns().printElement(Line);
  }
  bool doPrintLocation(const LVLocation *Location) const {
    return patterns().printObject(Location);
  }
  bool doPrintScope(const LVScope *Scope) const {
    return patterns().printElement(Scope);
  }
  bool doPrintSymbol(const LVSymbol *Symbol) const {
    return patterns().printElement(Symbol);
  }
  bool doPrintType(const LVType *Type) const {
    return patterns().printElement(Type);
  }

  static LVReader &getInstance();
  static void setInstance(LVReader *Reader);

  void print(raw_ostream &OS) const;
  virtual void printRecords(raw_ostream &OS) const {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const { print(dbgs()); }
#endif
};

inline LVReader &getReader() { return LVReader::getInstance(); }
inline LVSplitContext &getReaderSplitContext() {
  return getReader().getSplitContext();
}
inline LVScopeCompileUnit *getReaderCompileUnit() {
  return getReader().getCompileUnit();
}

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVREADER_H
