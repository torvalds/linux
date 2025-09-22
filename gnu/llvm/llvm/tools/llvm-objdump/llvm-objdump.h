//===--- llvm-objdump.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJDUMP_LLVM_OBJDUMP_H
#define LLVM_TOOLS_LLVM_OBJDUMP_LLVM_OBJDUMP_H

#include "llvm/ADT/StringSet.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/FormattedStream.h"
#include <functional>
#include <memory>

namespace llvm {
class StringRef;
class Twine;

namespace opt {
class Arg;
} // namespace opt

namespace object {
class RelocationRef;
struct VersionEntry;

class COFFObjectFile;
class ELFObjectFileBase;
class MachOObjectFile;
class WasmObjectFile;
class XCOFFObjectFile;
} // namespace object

namespace objdump {

enum DebugVarsFormat { DVDisabled, DVUnicode, DVASCII, DVInvalid };

extern bool ArchiveHeaders;
extern int DbgIndent;
extern DebugVarsFormat DbgVariables;
extern bool Demangle;
extern bool Disassemble;
extern bool DisassembleAll;
extern DIDumpType DwarfDumpType;
extern std::vector<std::string> FilterSections;
extern bool LeadingAddr;
extern std::vector<std::string> MAttrs;
extern std::string MCPU;
extern std::string Prefix;
extern uint32_t PrefixStrip;
extern bool PrintImmHex;
extern bool PrintLines;
extern bool PrintSource;
extern bool PrivateHeaders;
extern bool Relocations;
extern bool SectionHeaders;
extern bool SectionContents;
extern bool ShowRawInsn;
extern bool SymbolDescription;
extern bool TracebackTable;
extern bool SymbolTable;
extern std::string TripleName;
extern bool UnwindInfo;

extern StringSet<> FoundSectionSet;

class Dumper {
  const object::ObjectFile &O;
  StringSet<> Warnings;

protected:
  std::function<Error(const Twine &Msg)> WarningHandler;

public:
  Dumper(const object::ObjectFile &O);
  virtual ~Dumper() {}

  void reportUniqueWarning(Error Err);
  void reportUniqueWarning(const Twine &Msg);

  virtual void printPrivateHeaders();
  virtual void printDynamicRelocations() {}
  void printSymbolTable(StringRef ArchiveName,
                        StringRef ArchitectureName = StringRef(),
                        bool DumpDynamic = false);
  void printSymbol(const object::SymbolRef &Symbol,
                   ArrayRef<object::VersionEntry> SymbolVersions,
                   StringRef FileName, StringRef ArchiveName,
                   StringRef ArchitectureName, bool DumpDynamic);
  void printRelocations();
};

std::unique_ptr<Dumper> createCOFFDumper(const object::COFFObjectFile &Obj);
std::unique_ptr<Dumper> createELFDumper(const object::ELFObjectFileBase &Obj);
std::unique_ptr<Dumper> createMachODumper(const object::MachOObjectFile &Obj);
std::unique_ptr<Dumper> createWasmDumper(const object::WasmObjectFile &Obj);
std::unique_ptr<Dumper> createXCOFFDumper(const object::XCOFFObjectFile &Obj);

// Various helper functions.

/// Creates a SectionFilter with a standard predicate that conditionally skips
/// sections when the --section objdump flag is provided.
///
/// Idx is an optional output parameter that keeps track of which section index
/// this is. This may be different than the actual section number, as some
/// sections may be filtered (e.g. symbol tables).
object::SectionFilter ToolSectionFilter(const llvm::object::ObjectFile &O,
                                        uint64_t *Idx = nullptr);

bool isRelocAddressLess(object::RelocationRef A, object::RelocationRef B);
void printSectionHeaders(object::ObjectFile &O);
void printSectionContents(const object::ObjectFile *O);
[[noreturn]] void reportError(StringRef File, const Twine &Message);
[[noreturn]] void reportError(Error E, StringRef FileName,
                              StringRef ArchiveName = "",
                              StringRef ArchitectureName = "");
void reportWarning(const Twine &Message, StringRef File);

template <typename T, typename... Ts>
T unwrapOrError(Expected<T> EO, Ts &&... Args) {
  if (EO)
    return std::move(*EO);
  reportError(EO.takeError(), std::forward<Ts>(Args)...);
}

void invalidArgValue(const opt::Arg *A);

std::string getFileNameForError(const object::Archive::Child &C,
                                unsigned Index);
SymbolInfoTy createSymbolInfo(const object::ObjectFile &Obj,
                              const object::SymbolRef &Symbol,
                              bool IsMappingSymbol = false);
unsigned getInstStartColumn(const MCSubtargetInfo &STI);
void printRawData(llvm::ArrayRef<uint8_t> Bytes, uint64_t Address,
                  llvm::formatted_raw_ostream &OS,
                  llvm::MCSubtargetInfo const &STI);

} // namespace objdump
} // end namespace llvm

#endif
