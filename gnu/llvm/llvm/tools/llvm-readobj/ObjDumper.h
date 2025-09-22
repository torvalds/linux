//===-- ObjDumper.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_OBJDUMPER_H
#define LLVM_TOOLS_LLVM_READOBJ_OBJDUMPER_H

#include <functional>
#include <memory>
#include <system_error>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>

namespace llvm {
namespace object {
class Archive;
class COFFImportFile;
class ObjectFile;
class XCOFFObjectFile;
class ELFObjectFileBase;
} // namespace object
namespace codeview {
class GlobalTypeTableBuilder;
class MergingTypeTableBuilder;
} // namespace codeview

class ScopedPrinter;

// Comparator to compare symbols.
// Usage: the caller registers predicates (i.e., how to compare the symbols) by
// calling addPredicate(). The order in which predicates are registered is also
// their priority.
class SymbolComparator {
public:
  using CompPredicate =
      std::function<bool(object::SymbolRef, object::SymbolRef)>;

  // Each Obj format has a slightly different way of retrieving a symbol's info
  // So we defer the predicate's impl to each format.
  void addPredicate(CompPredicate Pred) { Predicates.push_back(Pred); }

  bool operator()(object::SymbolRef LHS, object::SymbolRef RHS) {
    for (CompPredicate Pred : Predicates) {
      if (Pred(LHS, RHS))
        return true;
      if (Pred(RHS, LHS))
        return false;
    }
    return false;
  }

private:
  SmallVector<CompPredicate, 2> Predicates;
};

class ObjDumper {
public:
  ObjDumper(ScopedPrinter &Writer, StringRef ObjName);
  virtual ~ObjDumper();

  virtual bool canDumpContent() { return true; }

  virtual void printFileSummary(StringRef FileStr, object::ObjectFile &Obj,
                                ArrayRef<std::string> InputFilenames,
                                const object::Archive *A);
  virtual void printFileHeaders() = 0;
  virtual void printSectionHeaders() = 0;
  virtual void printRelocations() = 0;
  virtual void printSymbols(bool PrintSymbols, bool PrintDynamicSymbols,
                            bool ExtraSymInfo) {
    if (PrintSymbols)
      printSymbols(ExtraSymInfo);
    if (PrintDynamicSymbols)
      printDynamicSymbols();
  }
  virtual void printSymbols(bool PrintSymbols, bool PrintDynamicSymbols,
                            bool ExtraSymInfo,
                            std::optional<SymbolComparator> SymComp) {
    if (SymComp) {
      if (PrintSymbols)
        printSymbols(SymComp);
      if (PrintDynamicSymbols)
        printDynamicSymbols(SymComp);
    } else {
      printSymbols(PrintSymbols, PrintDynamicSymbols, ExtraSymInfo);
    }
  }
  virtual void printProgramHeaders(bool PrintProgramHeaders,
                                   cl::boolOrDefault PrintSectionMapping) {
    if (PrintProgramHeaders)
      printProgramHeaders();
    if (PrintSectionMapping == cl::BOU_TRUE)
      printSectionMapping();
  }

  virtual void printUnwindInfo() = 0;

  // Symbol comparison functions.
  virtual bool canCompareSymbols() const { return false; }
  virtual bool compareSymbolsByName(object::SymbolRef LHS,
                                    object::SymbolRef RHS) const {
    return true;
  }
  virtual bool compareSymbolsByType(object::SymbolRef LHS,
                                    object::SymbolRef RHS) const {
    return true;
  }

  // Only implemented for ELF at this time.
  virtual void printDependentLibs() {}
  virtual void printDynamicRelocations() { }
  virtual void printDynamicTable() { }
  virtual void printNeededLibraries() { }
  virtual void printSectionAsHex(StringRef SectionName) {}
  virtual void printHashTable() { }
  virtual void printGnuHashTable() {}
  virtual void printHashSymbols() {}
  virtual void printLoadName() {}
  virtual void printVersionInfo() {}
  virtual void printGroupSections() {}
  virtual void printHashHistograms() {}
  virtual void printCGProfile() {}
  // If PrettyPGOAnalysis is true, prints BFI as relative frequency and BPI as
  // percentage. Otherwise raw values are displayed.
  virtual void printBBAddrMaps(bool PrettyPGOAnalysis) {}
  virtual void printAddrsig() {}
  virtual void printNotes() {}
  virtual void printELFLinkerOptions() {}
  virtual void printStackSizes() {}
  virtual void printSectionDetails() {}
  virtual void printArchSpecificInfo() {}
  virtual void printMemtag() {}

  // Only implemented for PE/COFF.
  virtual void printCOFFImports() { }
  virtual void printCOFFExports() { }
  virtual void printCOFFDirectives() { }
  virtual void printCOFFBaseReloc() { }
  virtual void printCOFFDebugDirectory() { }
  virtual void printCOFFTLSDirectory() {}
  virtual void printCOFFResources() {}
  virtual void printCOFFLoadConfig() { }
  virtual void printCodeViewDebugInfo() { }
  virtual void
  mergeCodeViewTypes(llvm::codeview::MergingTypeTableBuilder &CVIDs,
                     llvm::codeview::MergingTypeTableBuilder &CVTypes,
                     llvm::codeview::GlobalTypeTableBuilder &GlobalCVIDs,
                     llvm::codeview::GlobalTypeTableBuilder &GlobalCVTypes,
                     bool GHash) {}

  // Only implemented for XCOFF.
  virtual void printStringTable() {}
  virtual void printAuxiliaryHeader() {}
  virtual void printExceptionSection() {}
  virtual void printLoaderSection(bool PrintHeader, bool PrintSymbols,
                                  bool PrintRelocations) {}

  // Only implemented for MachO.
  virtual void printMachODataInCode() { }
  virtual void printMachOVersionMin() { }
  virtual void printMachODysymtab() { }
  virtual void printMachOSegment() { }
  virtual void printMachOIndirectSymbols() { }
  virtual void printMachOLinkerOptions() { }

  virtual void printStackMap() const = 0;

  void printAsStringList(StringRef StringContent, size_t StringDataOffset = 0);

  void printSectionsAsString(const object::ObjectFile &Obj,
                             ArrayRef<std::string> Sections, bool Decompress);
  void printSectionsAsHex(const object::ObjectFile &Obj,
                          ArrayRef<std::string> Sections, bool Decompress);

  std::function<Error(const Twine &Msg)> WarningHandler;
  void reportUniqueWarning(Error Err) const;
  void reportUniqueWarning(const Twine &Msg) const;

protected:
  ScopedPrinter &W;

private:
  virtual void printSymbols(bool ExtraSymInfo) {}
  virtual void printSymbols(std::optional<SymbolComparator> Comp) {}
  virtual void printDynamicSymbols() {}
  virtual void printDynamicSymbols(std::optional<SymbolComparator> Comp) {}
  virtual void printProgramHeaders() {}
  virtual void printSectionMapping() {}

  std::unordered_set<std::string> Warnings;
};

std::unique_ptr<ObjDumper> createCOFFDumper(const object::COFFObjectFile &Obj,
                                            ScopedPrinter &Writer);

std::unique_ptr<ObjDumper> createELFDumper(const object::ELFObjectFileBase &Obj,
                                           ScopedPrinter &Writer);

std::unique_ptr<ObjDumper> createMachODumper(const object::MachOObjectFile &Obj,
                                             ScopedPrinter &Writer);

std::unique_ptr<ObjDumper> createWasmDumper(const object::WasmObjectFile &Obj,
                                            ScopedPrinter &Writer);

std::unique_ptr<ObjDumper> createXCOFFDumper(const object::XCOFFObjectFile &Obj,
                                             ScopedPrinter &Writer);

void dumpCOFFImportFile(const object::COFFImportFile *File,
                        ScopedPrinter &Writer);

void dumpCodeViewMergedTypes(ScopedPrinter &Writer,
                             ArrayRef<ArrayRef<uint8_t>> IpiRecords,
                             ArrayRef<ArrayRef<uint8_t>> TpiRecords);

} // namespace llvm

#endif
