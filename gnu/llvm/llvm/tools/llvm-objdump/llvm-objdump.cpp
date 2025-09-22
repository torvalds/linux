//===-- llvm-objdump.cpp - Object file dumping utility for llvm -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that works like binutils "objdump", that is, it
// dumps out a plethora of information about an object file depending on the
// flags.
//
// The flags and output of this program should be near identical to those of
// binutils objdump.
//
//===----------------------------------------------------------------------===//

#include "llvm-objdump.h"
#include "COFFDump.h"
#include "ELFDump.h"
#include "MachODump.h"
#include "ObjdumpOptID.h"
#include "OffloadDump.h"
#include "SourcePrinter.h"
#include "WasmDump.h"
#include "XCOFFDump.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/DebugInfo/BTF/BTFParser.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/Debuginfod/BuildIDFetcher.h"
#include "llvm/Debuginfod/Debuginfod.h"
#include "llvm/Debuginfod/HTTPClient.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCDisassembler/MCRelocationInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/FaultMapParser.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <set>
#include <system_error>
#include <unordered_map>
#include <utility>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::objdump;
using namespace llvm::opt;

namespace {

class CommonOptTable : public opt::GenericOptTable {
public:
  CommonOptTable(ArrayRef<Info> OptionInfos, const char *Usage,
                 const char *Description)
      : opt::GenericOptTable(OptionInfos), Usage(Usage),
        Description(Description) {
    setGroupedShortOptions(true);
  }

  void printHelp(StringRef Argv0, bool ShowHidden = false) const {
    Argv0 = sys::path::filename(Argv0);
    opt::GenericOptTable::printHelp(outs(), (Argv0 + Usage).str().c_str(),
                                    Description, ShowHidden, ShowHidden);
    // TODO Replace this with OptTable API once it adds extrahelp support.
    outs() << "\nPass @FILE as argument to read options from FILE.\n";
  }

private:
  const char *Usage;
  const char *Description;
};

// ObjdumpOptID is in ObjdumpOptID.h
namespace objdump_opt {
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "ObjdumpOpts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info ObjdumpInfoTable[] = {
#define OPTION(...)                                                            \
  LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(OBJDUMP_, __VA_ARGS__),
#include "ObjdumpOpts.inc"
#undef OPTION
};
} // namespace objdump_opt

class ObjdumpOptTable : public CommonOptTable {
public:
  ObjdumpOptTable()
      : CommonOptTable(objdump_opt::ObjdumpInfoTable,
                       " [options] <input object files>",
                       "llvm object file dumper") {}
};

enum OtoolOptID {
  OTOOL_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(OTOOL_, __VA_ARGS__),
#include "OtoolOpts.inc"
#undef OPTION
};

namespace otool {
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "OtoolOpts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info OtoolInfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(OTOOL_, __VA_ARGS__),
#include "OtoolOpts.inc"
#undef OPTION
};
} // namespace otool

class OtoolOptTable : public CommonOptTable {
public:
  OtoolOptTable()
      : CommonOptTable(otool::OtoolInfoTable, " [option...] [file...]",
                       "Mach-O object file displaying tool") {}
};

struct BBAddrMapLabel {
  std::string BlockLabel;
  std::string PGOAnalysis;
};

// This class represents the BBAddrMap and PGOMap associated with a single
// function.
class BBAddrMapFunctionEntry {
public:
  BBAddrMapFunctionEntry(BBAddrMap AddrMap, PGOAnalysisMap PGOMap)
      : AddrMap(std::move(AddrMap)), PGOMap(std::move(PGOMap)) {}

  const BBAddrMap &getAddrMap() const { return AddrMap; }

  // Returns the PGO string associated with the entry of index `PGOBBEntryIndex`
  // in `PGOMap`. If PrettyPGOAnalysis is true, prints BFI as relative frequency
  // and BPI as percentage. Otherwise raw values are displayed.
  std::string constructPGOLabelString(size_t PGOBBEntryIndex,
                                      bool PrettyPGOAnalysis) const {
    if (!PGOMap.FeatEnable.hasPGOAnalysis())
      return "";
    std::string PGOString;
    raw_string_ostream PGOSS(PGOString);

    PGOSS << " (";
    if (PGOMap.FeatEnable.FuncEntryCount && PGOBBEntryIndex == 0) {
      PGOSS << "Entry count: " << Twine(PGOMap.FuncEntryCount);
      if (PGOMap.FeatEnable.hasPGOAnalysisBBData()) {
        PGOSS << ", ";
      }
    }

    if (PGOMap.FeatEnable.hasPGOAnalysisBBData()) {

      assert(PGOBBEntryIndex < PGOMap.BBEntries.size() &&
             "Expected PGOAnalysisMap and BBAddrMap to have the same entries");
      const PGOAnalysisMap::PGOBBEntry &PGOBBEntry =
          PGOMap.BBEntries[PGOBBEntryIndex];

      if (PGOMap.FeatEnable.BBFreq) {
        PGOSS << "Frequency: ";
        if (PrettyPGOAnalysis)
          printRelativeBlockFreq(PGOSS, PGOMap.BBEntries.front().BlockFreq,
                                 PGOBBEntry.BlockFreq);
        else
          PGOSS << Twine(PGOBBEntry.BlockFreq.getFrequency());
        if (PGOMap.FeatEnable.BrProb && PGOBBEntry.Successors.size() > 0) {
          PGOSS << ", ";
        }
      }
      if (PGOMap.FeatEnable.BrProb && PGOBBEntry.Successors.size() > 0) {
        PGOSS << "Successors: ";
        interleaveComma(
            PGOBBEntry.Successors, PGOSS,
            [&](const PGOAnalysisMap::PGOBBEntry::SuccessorEntry &SE) {
              PGOSS << "BB" << SE.ID << ":";
              if (PrettyPGOAnalysis)
                PGOSS << "[" << SE.Prob << "]";
              else
                PGOSS.write_hex(SE.Prob.getNumerator());
            });
      }
    }
    PGOSS << ")";

    return PGOString;
  }

private:
  const BBAddrMap AddrMap;
  const PGOAnalysisMap PGOMap;
};

// This class represents the BBAddrMap and PGOMap of potentially multiple
// functions in a section.
class BBAddrMapInfo {
public:
  void clear() {
    FunctionAddrToMap.clear();
    RangeBaseAddrToFunctionAddr.clear();
  }

  bool empty() const { return FunctionAddrToMap.empty(); }

  void AddFunctionEntry(BBAddrMap AddrMap, PGOAnalysisMap PGOMap) {
    uint64_t FunctionAddr = AddrMap.getFunctionAddress();
    for (size_t I = 1; I < AddrMap.BBRanges.size(); ++I)
      RangeBaseAddrToFunctionAddr.emplace(AddrMap.BBRanges[I].BaseAddress,
                                          FunctionAddr);
    [[maybe_unused]] auto R = FunctionAddrToMap.try_emplace(
        FunctionAddr, std::move(AddrMap), std::move(PGOMap));
    assert(R.second && "duplicate function address");
  }

  // Returns the BBAddrMap entry for the function associated with `BaseAddress`.
  // `BaseAddress` could be the function address or the address of a range
  // associated with that function. Returns `nullptr` if `BaseAddress` is not
  // mapped to any entry.
  const BBAddrMapFunctionEntry *getEntryForAddress(uint64_t BaseAddress) const {
    uint64_t FunctionAddr = BaseAddress;
    auto S = RangeBaseAddrToFunctionAddr.find(BaseAddress);
    if (S != RangeBaseAddrToFunctionAddr.end())
      FunctionAddr = S->second;
    auto R = FunctionAddrToMap.find(FunctionAddr);
    if (R == FunctionAddrToMap.end())
      return nullptr;
    return &R->second;
  }

private:
  std::unordered_map<uint64_t, BBAddrMapFunctionEntry> FunctionAddrToMap;
  std::unordered_map<uint64_t, uint64_t> RangeBaseAddrToFunctionAddr;
};

} // namespace

#define DEBUG_TYPE "objdump"

enum class ColorOutput {
  Auto,
  Enable,
  Disable,
  Invalid,
};

static uint64_t AdjustVMA;
static bool AllHeaders;
static std::string ArchName;
bool objdump::ArchiveHeaders;
bool objdump::Demangle;
bool objdump::Disassemble;
bool objdump::DisassembleAll;
bool objdump::SymbolDescription;
bool objdump::TracebackTable;
static std::vector<std::string> DisassembleSymbols;
static bool DisassembleZeroes;
static std::vector<std::string> DisassemblerOptions;
static ColorOutput DisassemblyColor;
DIDumpType objdump::DwarfDumpType;
static bool DynamicRelocations;
static bool FaultMapSection;
static bool FileHeaders;
bool objdump::SectionContents;
static std::vector<std::string> InputFilenames;
bool objdump::PrintLines;
static bool MachOOpt;
std::string objdump::MCPU;
std::vector<std::string> objdump::MAttrs;
bool objdump::ShowRawInsn;
bool objdump::LeadingAddr;
static bool Offloading;
static bool RawClangAST;
bool objdump::Relocations;
bool objdump::PrintImmHex;
bool objdump::PrivateHeaders;
std::vector<std::string> objdump::FilterSections;
bool objdump::SectionHeaders;
static bool ShowAllSymbols;
static bool ShowLMA;
bool objdump::PrintSource;

static uint64_t StartAddress;
static bool HasStartAddressFlag;
static uint64_t StopAddress = UINT64_MAX;
static bool HasStopAddressFlag;

bool objdump::SymbolTable;
static bool SymbolizeOperands;
static bool PrettyPGOAnalysisMap;
static bool DynamicSymbolTable;
std::string objdump::TripleName;
bool objdump::UnwindInfo;
static bool Wide;
std::string objdump::Prefix;
uint32_t objdump::PrefixStrip;

DebugVarsFormat objdump::DbgVariables = DVDisabled;

int objdump::DbgIndent = 52;

static StringSet<> DisasmSymbolSet;
StringSet<> objdump::FoundSectionSet;
static StringRef ToolName;

std::unique_ptr<BuildIDFetcher> BIDFetcher;

Dumper::Dumper(const object::ObjectFile &O) : O(O) {
  WarningHandler = [this](const Twine &Msg) {
    if (Warnings.insert(Msg.str()).second)
      reportWarning(Msg, this->O.getFileName());
    return Error::success();
  };
}

void Dumper::reportUniqueWarning(Error Err) {
  reportUniqueWarning(toString(std::move(Err)));
}

void Dumper::reportUniqueWarning(const Twine &Msg) {
  cantFail(WarningHandler(Msg));
}

static Expected<std::unique_ptr<Dumper>> createDumper(const ObjectFile &Obj) {
  if (const auto *O = dyn_cast<COFFObjectFile>(&Obj))
    return createCOFFDumper(*O);
  if (const auto *O = dyn_cast<ELFObjectFileBase>(&Obj))
    return createELFDumper(*O);
  if (const auto *O = dyn_cast<MachOObjectFile>(&Obj))
    return createMachODumper(*O);
  if (const auto *O = dyn_cast<WasmObjectFile>(&Obj))
    return createWasmDumper(*O);
  if (const auto *O = dyn_cast<XCOFFObjectFile>(&Obj))
    return createXCOFFDumper(*O);

  return createStringError(errc::invalid_argument,
                           "unsupported object file format");
}

namespace {
struct FilterResult {
  // True if the section should not be skipped.
  bool Keep;

  // True if the index counter should be incremented, even if the section should
  // be skipped. For example, sections may be skipped if they are not included
  // in the --section flag, but we still want those to count toward the section
  // count.
  bool IncrementIndex;
};
} // namespace

static FilterResult checkSectionFilter(object::SectionRef S) {
  if (FilterSections.empty())
    return {/*Keep=*/true, /*IncrementIndex=*/true};

  Expected<StringRef> SecNameOrErr = S.getName();
  if (!SecNameOrErr) {
    consumeError(SecNameOrErr.takeError());
    return {/*Keep=*/false, /*IncrementIndex=*/false};
  }
  StringRef SecName = *SecNameOrErr;

  // StringSet does not allow empty key so avoid adding sections with
  // no name (such as the section with index 0) here.
  if (!SecName.empty())
    FoundSectionSet.insert(SecName);

  // Only show the section if it's in the FilterSections list, but always
  // increment so the indexing is stable.
  return {/*Keep=*/is_contained(FilterSections, SecName),
          /*IncrementIndex=*/true};
}

SectionFilter objdump::ToolSectionFilter(object::ObjectFile const &O,
                                         uint64_t *Idx) {
  // Start at UINT64_MAX so that the first index returned after an increment is
  // zero (after the unsigned wrap).
  if (Idx)
    *Idx = UINT64_MAX;
  return SectionFilter(
      [Idx](object::SectionRef S) {
        FilterResult Result = checkSectionFilter(S);
        if (Idx != nullptr && Result.IncrementIndex)
          *Idx += 1;
        return Result.Keep;
      },
      O);
}

std::string objdump::getFileNameForError(const object::Archive::Child &C,
                                         unsigned Index) {
  Expected<StringRef> NameOrErr = C.getName();
  if (NameOrErr)
    return std::string(NameOrErr.get());
  // If we have an error getting the name then we print the index of the archive
  // member. Since we are already in an error state, we just ignore this error.
  consumeError(NameOrErr.takeError());
  return "<file index: " + std::to_string(Index) + ">";
}

void objdump::reportWarning(const Twine &Message, StringRef File) {
  // Output order between errs() and outs() matters especially for archive
  // files where the output is per member object.
  outs().flush();
  WithColor::warning(errs(), ToolName)
      << "'" << File << "': " << Message << "\n";
}

[[noreturn]] void objdump::reportError(StringRef File, const Twine &Message) {
  outs().flush();
  WithColor::error(errs(), ToolName) << "'" << File << "': " << Message << "\n";
  exit(1);
}

[[noreturn]] void objdump::reportError(Error E, StringRef FileName,
                                       StringRef ArchiveName,
                                       StringRef ArchitectureName) {
  assert(E);
  outs().flush();
  WithColor::error(errs(), ToolName);
  if (ArchiveName != "")
    errs() << ArchiveName << "(" << FileName << ")";
  else
    errs() << "'" << FileName << "'";
  if (!ArchitectureName.empty())
    errs() << " (for architecture " << ArchitectureName << ")";
  errs() << ": ";
  logAllUnhandledErrors(std::move(E), errs());
  exit(1);
}

static void reportCmdLineWarning(const Twine &Message) {
  WithColor::warning(errs(), ToolName) << Message << "\n";
}

[[noreturn]] static void reportCmdLineError(const Twine &Message) {
  WithColor::error(errs(), ToolName) << Message << "\n";
  exit(1);
}

static void warnOnNoMatchForSections() {
  SetVector<StringRef> MissingSections;
  for (StringRef S : FilterSections) {
    if (FoundSectionSet.count(S))
      return;
    // User may specify a unnamed section. Don't warn for it.
    if (!S.empty())
      MissingSections.insert(S);
  }

  // Warn only if no section in FilterSections is matched.
  for (StringRef S : MissingSections)
    reportCmdLineWarning("section '" + S +
                         "' mentioned in a -j/--section option, but not "
                         "found in any input file");
}

static const Target *getTarget(const ObjectFile *Obj) {
  // Figure out the target triple.
  Triple TheTriple("unknown-unknown-unknown");
  if (TripleName.empty()) {
    TheTriple = Obj->makeTriple();
  } else {
    TheTriple.setTriple(Triple::normalize(TripleName));
    auto Arch = Obj->getArch();
    if (Arch == Triple::arm || Arch == Triple::armeb)
      Obj->setARMSubArch(TheTriple);
  }

  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(ArchName, TheTriple,
                                                         Error);
  if (!TheTarget)
    reportError(Obj->getFileName(), "can't find target: " + Error);

  // Update the triple name and return the found target.
  TripleName = TheTriple.getTriple();
  return TheTarget;
}

bool objdump::isRelocAddressLess(RelocationRef A, RelocationRef B) {
  return A.getOffset() < B.getOffset();
}

static Error getRelocationValueString(const RelocationRef &Rel,
                                      bool SymbolDescription,
                                      SmallVectorImpl<char> &Result) {
  const ObjectFile *Obj = Rel.getObject();
  if (auto *ELF = dyn_cast<ELFObjectFileBase>(Obj))
    return getELFRelocationValueString(ELF, Rel, Result);
  if (auto *COFF = dyn_cast<COFFObjectFile>(Obj))
    return getCOFFRelocationValueString(COFF, Rel, Result);
  if (auto *Wasm = dyn_cast<WasmObjectFile>(Obj))
    return getWasmRelocationValueString(Wasm, Rel, Result);
  if (auto *MachO = dyn_cast<MachOObjectFile>(Obj))
    return getMachORelocationValueString(MachO, Rel, Result);
  if (auto *XCOFF = dyn_cast<XCOFFObjectFile>(Obj))
    return getXCOFFRelocationValueString(*XCOFF, Rel, SymbolDescription,
                                         Result);
  llvm_unreachable("unknown object file format");
}

/// Indicates whether this relocation should hidden when listing
/// relocations, usually because it is the trailing part of a multipart
/// relocation that will be printed as part of the leading relocation.
static bool getHidden(RelocationRef RelRef) {
  auto *MachO = dyn_cast<MachOObjectFile>(RelRef.getObject());
  if (!MachO)
    return false;

  unsigned Arch = MachO->getArch();
  DataRefImpl Rel = RelRef.getRawDataRefImpl();
  uint64_t Type = MachO->getRelocationType(Rel);

  // On arches that use the generic relocations, GENERIC_RELOC_PAIR
  // is always hidden.
  if (Arch == Triple::x86 || Arch == Triple::arm || Arch == Triple::ppc)
    return Type == MachO::GENERIC_RELOC_PAIR;

  if (Arch == Triple::x86_64) {
    // On x86_64, X86_64_RELOC_UNSIGNED is hidden only when it follows
    // an X86_64_RELOC_SUBTRACTOR.
    if (Type == MachO::X86_64_RELOC_UNSIGNED && Rel.d.a > 0) {
      DataRefImpl RelPrev = Rel;
      RelPrev.d.a--;
      uint64_t PrevType = MachO->getRelocationType(RelPrev);
      if (PrevType == MachO::X86_64_RELOC_SUBTRACTOR)
        return true;
    }
  }

  return false;
}

/// Get the column at which we want to start printing the instruction
/// disassembly, taking into account anything which appears to the left of it.
unsigned objdump::getInstStartColumn(const MCSubtargetInfo &STI) {
  return !ShowRawInsn ? 16 : STI.getTargetTriple().isX86() ? 40 : 24;
}

static void AlignToInstStartColumn(size_t Start, const MCSubtargetInfo &STI,
                                   raw_ostream &OS) {
  // The output of printInst starts with a tab. Print some spaces so that
  // the tab has 1 column and advances to the target tab stop.
  unsigned TabStop = getInstStartColumn(STI);
  unsigned Column = OS.tell() - Start;
  OS.indent(Column < TabStop - 1 ? TabStop - 1 - Column : 7 - Column % 8);
}

void objdump::printRawData(ArrayRef<uint8_t> Bytes, uint64_t Address,
                           formatted_raw_ostream &OS,
                           MCSubtargetInfo const &STI) {
  size_t Start = OS.tell();
  if (LeadingAddr)
    OS << format("%8" PRIx64 ":", Address);
  if (ShowRawInsn) {
    OS << ' ';
    dumpBytes(Bytes, OS);
  }
  AlignToInstStartColumn(Start, STI, OS);
}

namespace {

static bool isAArch64Elf(const ObjectFile &Obj) {
  const auto *Elf = dyn_cast<ELFObjectFileBase>(&Obj);
  return Elf && Elf->getEMachine() == ELF::EM_AARCH64;
}

static bool isArmElf(const ObjectFile &Obj) {
  const auto *Elf = dyn_cast<ELFObjectFileBase>(&Obj);
  return Elf && Elf->getEMachine() == ELF::EM_ARM;
}

static bool isCSKYElf(const ObjectFile &Obj) {
  const auto *Elf = dyn_cast<ELFObjectFileBase>(&Obj);
  return Elf && Elf->getEMachine() == ELF::EM_CSKY;
}

static bool hasMappingSymbols(const ObjectFile &Obj) {
  return isArmElf(Obj) || isAArch64Elf(Obj) || isCSKYElf(Obj) ;
}

static void printRelocation(formatted_raw_ostream &OS, StringRef FileName,
                            const RelocationRef &Rel, uint64_t Address,
                            bool Is64Bits) {
  StringRef Fmt = Is64Bits ? "%016" PRIx64 ":  " : "%08" PRIx64 ":  ";
  SmallString<16> Name;
  SmallString<32> Val;
  Rel.getTypeName(Name);
  if (Error E = getRelocationValueString(Rel, SymbolDescription, Val))
    reportError(std::move(E), FileName);
  OS << (Is64Bits || !LeadingAddr ? "\t\t" : "\t\t\t");
  if (LeadingAddr)
    OS << format(Fmt.data(), Address);
  OS << Name << "\t" << Val;
}

static void printBTFRelocation(formatted_raw_ostream &FOS, llvm::BTFParser &BTF,
                               object::SectionedAddress Address,
                               LiveVariablePrinter &LVP) {
  const llvm::BTF::BPFFieldReloc *Reloc = BTF.findFieldReloc(Address);
  if (!Reloc)
    return;

  SmallString<64> Val;
  BTF.symbolize(Reloc, Val);
  FOS << "\t\t";
  if (LeadingAddr)
    FOS << format("%016" PRIx64 ":  ", Address.Address + AdjustVMA);
  FOS << "CO-RE " << Val;
  LVP.printAfterOtherLine(FOS, true);
}

class PrettyPrinter {
public:
  virtual ~PrettyPrinter() = default;
  virtual void
  printInst(MCInstPrinter &IP, const MCInst *MI, ArrayRef<uint8_t> Bytes,
            object::SectionedAddress Address, formatted_raw_ostream &OS,
            StringRef Annot, MCSubtargetInfo const &STI, SourcePrinter *SP,
            StringRef ObjectFilename, std::vector<RelocationRef> *Rels,
            LiveVariablePrinter &LVP) {
    if (SP && (PrintSource || PrintLines))
      SP->printSourceLine(OS, Address, ObjectFilename, LVP);
    LVP.printBetweenInsts(OS, false);

    printRawData(Bytes, Address.Address, OS, STI);

    if (MI) {
      // See MCInstPrinter::printInst. On targets where a PC relative immediate
      // is relative to the next instruction and the length of a MCInst is
      // difficult to measure (x86), this is the address of the next
      // instruction.
      uint64_t Addr =
          Address.Address + (STI.getTargetTriple().isX86() ? Bytes.size() : 0);
      IP.printInst(MI, Addr, "", STI, OS);
    } else
      OS << "\t<unknown>";
  }
};
PrettyPrinter PrettyPrinterInst;

class HexagonPrettyPrinter : public PrettyPrinter {
public:
  void printLead(ArrayRef<uint8_t> Bytes, uint64_t Address,
                 formatted_raw_ostream &OS) {
    uint32_t opcode =
      (Bytes[3] << 24) | (Bytes[2] << 16) | (Bytes[1] << 8) | Bytes[0];
    if (LeadingAddr)
      OS << format("%8" PRIx64 ":", Address);
    if (ShowRawInsn) {
      OS << "\t";
      dumpBytes(Bytes.slice(0, 4), OS);
      OS << format("\t%08" PRIx32, opcode);
    }
  }
  void printInst(MCInstPrinter &IP, const MCInst *MI, ArrayRef<uint8_t> Bytes,
                 object::SectionedAddress Address, formatted_raw_ostream &OS,
                 StringRef Annot, MCSubtargetInfo const &STI, SourcePrinter *SP,
                 StringRef ObjectFilename, std::vector<RelocationRef> *Rels,
                 LiveVariablePrinter &LVP) override {
    if (SP && (PrintSource || PrintLines))
      SP->printSourceLine(OS, Address, ObjectFilename, LVP, "");
    if (!MI) {
      printLead(Bytes, Address.Address, OS);
      OS << " <unknown>";
      return;
    }
    std::string Buffer;
    {
      raw_string_ostream TempStream(Buffer);
      IP.printInst(MI, Address.Address, "", STI, TempStream);
    }
    StringRef Contents(Buffer);
    // Split off bundle attributes
    auto PacketBundle = Contents.rsplit('\n');
    // Split off first instruction from the rest
    auto HeadTail = PacketBundle.first.split('\n');
    auto Preamble = " { ";
    auto Separator = "";

    // Hexagon's packets require relocations to be inline rather than
    // clustered at the end of the packet.
    std::vector<RelocationRef>::const_iterator RelCur = Rels->begin();
    std::vector<RelocationRef>::const_iterator RelEnd = Rels->end();
    auto PrintReloc = [&]() -> void {
      while ((RelCur != RelEnd) && (RelCur->getOffset() <= Address.Address)) {
        if (RelCur->getOffset() == Address.Address) {
          printRelocation(OS, ObjectFilename, *RelCur, Address.Address, false);
          return;
        }
        ++RelCur;
      }
    };

    while (!HeadTail.first.empty()) {
      OS << Separator;
      Separator = "\n";
      if (SP && (PrintSource || PrintLines))
        SP->printSourceLine(OS, Address, ObjectFilename, LVP, "");
      printLead(Bytes, Address.Address, OS);
      OS << Preamble;
      Preamble = "   ";
      StringRef Inst;
      auto Duplex = HeadTail.first.split('\v');
      if (!Duplex.second.empty()) {
        OS << Duplex.first;
        OS << "; ";
        Inst = Duplex.second;
      }
      else
        Inst = HeadTail.first;
      OS << Inst;
      HeadTail = HeadTail.second.split('\n');
      if (HeadTail.first.empty())
        OS << " } " << PacketBundle.second;
      PrintReloc();
      Bytes = Bytes.slice(4);
      Address.Address += 4;
    }
  }
};
HexagonPrettyPrinter HexagonPrettyPrinterInst;

class AMDGCNPrettyPrinter : public PrettyPrinter {
public:
  void printInst(MCInstPrinter &IP, const MCInst *MI, ArrayRef<uint8_t> Bytes,
                 object::SectionedAddress Address, formatted_raw_ostream &OS,
                 StringRef Annot, MCSubtargetInfo const &STI, SourcePrinter *SP,
                 StringRef ObjectFilename, std::vector<RelocationRef> *Rels,
                 LiveVariablePrinter &LVP) override {
    if (SP && (PrintSource || PrintLines))
      SP->printSourceLine(OS, Address, ObjectFilename, LVP);

    if (MI) {
      SmallString<40> InstStr;
      raw_svector_ostream IS(InstStr);

      IP.printInst(MI, Address.Address, "", STI, IS);

      OS << left_justify(IS.str(), 60);
    } else {
      // an unrecognized encoding - this is probably data so represent it
      // using the .long directive, or .byte directive if fewer than 4 bytes
      // remaining
      if (Bytes.size() >= 4) {
        OS << format(
            "\t.long 0x%08" PRIx32 " ",
            support::endian::read32<llvm::endianness::little>(Bytes.data()));
        OS.indent(42);
      } else {
          OS << format("\t.byte 0x%02" PRIx8, Bytes[0]);
          for (unsigned int i = 1; i < Bytes.size(); i++)
            OS << format(", 0x%02" PRIx8, Bytes[i]);
          OS.indent(55 - (6 * Bytes.size()));
      }
    }

    OS << format("// %012" PRIX64 ":", Address.Address);
    if (Bytes.size() >= 4) {
      // D should be casted to uint32_t here as it is passed by format to
      // snprintf as vararg.
      for (uint32_t D :
           ArrayRef(reinterpret_cast<const support::little32_t *>(Bytes.data()),
                    Bytes.size() / 4))
          OS << format(" %08" PRIX32, D);
    } else {
      for (unsigned char B : Bytes)
        OS << format(" %02" PRIX8, B);
    }

    if (!Annot.empty())
      OS << " // " << Annot;
  }
};
AMDGCNPrettyPrinter AMDGCNPrettyPrinterInst;

class BPFPrettyPrinter : public PrettyPrinter {
public:
  void printInst(MCInstPrinter &IP, const MCInst *MI, ArrayRef<uint8_t> Bytes,
                 object::SectionedAddress Address, formatted_raw_ostream &OS,
                 StringRef Annot, MCSubtargetInfo const &STI, SourcePrinter *SP,
                 StringRef ObjectFilename, std::vector<RelocationRef> *Rels,
                 LiveVariablePrinter &LVP) override {
    if (SP && (PrintSource || PrintLines))
      SP->printSourceLine(OS, Address, ObjectFilename, LVP);
    if (LeadingAddr)
      OS << format("%8" PRId64 ":", Address.Address / 8);
    if (ShowRawInsn) {
      OS << "\t";
      dumpBytes(Bytes, OS);
    }
    if (MI)
      IP.printInst(MI, Address.Address, "", STI, OS);
    else
      OS << "\t<unknown>";
  }
};
BPFPrettyPrinter BPFPrettyPrinterInst;

class ARMPrettyPrinter : public PrettyPrinter {
public:
  void printInst(MCInstPrinter &IP, const MCInst *MI, ArrayRef<uint8_t> Bytes,
                 object::SectionedAddress Address, formatted_raw_ostream &OS,
                 StringRef Annot, MCSubtargetInfo const &STI, SourcePrinter *SP,
                 StringRef ObjectFilename, std::vector<RelocationRef> *Rels,
                 LiveVariablePrinter &LVP) override {
    if (SP && (PrintSource || PrintLines))
      SP->printSourceLine(OS, Address, ObjectFilename, LVP);
    LVP.printBetweenInsts(OS, false);

    size_t Start = OS.tell();
    if (LeadingAddr)
      OS << format("%8" PRIx64 ":", Address.Address);
    if (ShowRawInsn) {
      size_t Pos = 0, End = Bytes.size();
      if (STI.checkFeatures("+thumb-mode")) {
        for (; Pos + 2 <= End; Pos += 2)
          OS << ' '
             << format_hex_no_prefix(
                    llvm::support::endian::read<uint16_t>(
                        Bytes.data() + Pos, InstructionEndianness),
                    4);
      } else {
        for (; Pos + 4 <= End; Pos += 4)
          OS << ' '
             << format_hex_no_prefix(
                    llvm::support::endian::read<uint32_t>(
                        Bytes.data() + Pos, InstructionEndianness),
                    8);
      }
      if (Pos < End) {
        OS << ' ';
        dumpBytes(Bytes.slice(Pos), OS);
      }
    }

    AlignToInstStartColumn(Start, STI, OS);

    if (MI) {
      IP.printInst(MI, Address.Address, "", STI, OS);
    } else
      OS << "\t<unknown>";
  }

  void setInstructionEndianness(llvm::endianness Endianness) {
    InstructionEndianness = Endianness;
  }

private:
  llvm::endianness InstructionEndianness = llvm::endianness::little;
};
ARMPrettyPrinter ARMPrettyPrinterInst;

class AArch64PrettyPrinter : public PrettyPrinter {
public:
  void printInst(MCInstPrinter &IP, const MCInst *MI, ArrayRef<uint8_t> Bytes,
                 object::SectionedAddress Address, formatted_raw_ostream &OS,
                 StringRef Annot, MCSubtargetInfo const &STI, SourcePrinter *SP,
                 StringRef ObjectFilename, std::vector<RelocationRef> *Rels,
                 LiveVariablePrinter &LVP) override {
    if (SP && (PrintSource || PrintLines))
      SP->printSourceLine(OS, Address, ObjectFilename, LVP);
    LVP.printBetweenInsts(OS, false);

    size_t Start = OS.tell();
    if (LeadingAddr)
      OS << format("%8" PRIx64 ":", Address.Address);
    if (ShowRawInsn) {
      size_t Pos = 0, End = Bytes.size();
      for (; Pos + 4 <= End; Pos += 4)
        OS << ' '
           << format_hex_no_prefix(
                  llvm::support::endian::read<uint32_t>(
                      Bytes.data() + Pos, llvm::endianness::little),
                  8);
      if (Pos < End) {
        OS << ' ';
        dumpBytes(Bytes.slice(Pos), OS);
      }
    }

    AlignToInstStartColumn(Start, STI, OS);

    if (MI) {
      IP.printInst(MI, Address.Address, "", STI, OS);
    } else
      OS << "\t<unknown>";
  }
};
AArch64PrettyPrinter AArch64PrettyPrinterInst;

class RISCVPrettyPrinter : public PrettyPrinter {
public:
  void printInst(MCInstPrinter &IP, const MCInst *MI, ArrayRef<uint8_t> Bytes,
                 object::SectionedAddress Address, formatted_raw_ostream &OS,
                 StringRef Annot, MCSubtargetInfo const &STI, SourcePrinter *SP,
                 StringRef ObjectFilename, std::vector<RelocationRef> *Rels,
                 LiveVariablePrinter &LVP) override {
    if (SP && (PrintSource || PrintLines))
      SP->printSourceLine(OS, Address, ObjectFilename, LVP);
    LVP.printBetweenInsts(OS, false);

    size_t Start = OS.tell();
    if (LeadingAddr)
      OS << format("%8" PRIx64 ":", Address.Address);
    if (ShowRawInsn) {
      size_t Pos = 0, End = Bytes.size();
      if (End % 4 == 0) {
        // 32-bit and 64-bit instructions.
        for (; Pos + 4 <= End; Pos += 4)
          OS << ' '
             << format_hex_no_prefix(
                    llvm::support::endian::read<uint32_t>(
                        Bytes.data() + Pos, llvm::endianness::little),
                    8);
      } else if (End % 2 == 0) {
        // 16-bit and 48-bits instructions.
        for (; Pos + 2 <= End; Pos += 2)
          OS << ' '
             << format_hex_no_prefix(
                    llvm::support::endian::read<uint16_t>(
                        Bytes.data() + Pos, llvm::endianness::little),
                    4);
      }
      if (Pos < End) {
        OS << ' ';
        dumpBytes(Bytes.slice(Pos), OS);
      }
    }

    AlignToInstStartColumn(Start, STI, OS);

    if (MI) {
      IP.printInst(MI, Address.Address, "", STI, OS);
    } else
      OS << "\t<unknown>";
  }
};
RISCVPrettyPrinter RISCVPrettyPrinterInst;

PrettyPrinter &selectPrettyPrinter(Triple const &Triple) {
  switch(Triple.getArch()) {
  default:
    return PrettyPrinterInst;
  case Triple::hexagon:
    return HexagonPrettyPrinterInst;
  case Triple::amdgcn:
    return AMDGCNPrettyPrinterInst;
  case Triple::bpfel:
  case Triple::bpfeb:
    return BPFPrettyPrinterInst;
  case Triple::arm:
  case Triple::armeb:
  case Triple::thumb:
  case Triple::thumbeb:
    return ARMPrettyPrinterInst;
  case Triple::aarch64:
  case Triple::aarch64_be:
  case Triple::aarch64_32:
    return AArch64PrettyPrinterInst;
  case Triple::riscv32:
  case Triple::riscv64:
    return RISCVPrettyPrinterInst;
  }
}

class DisassemblerTarget {
public:
  const Target *TheTarget;
  std::unique_ptr<const MCSubtargetInfo> SubtargetInfo;
  std::shared_ptr<MCContext> Context;
  std::unique_ptr<MCDisassembler> DisAsm;
  std::shared_ptr<MCInstrAnalysis> InstrAnalysis;
  std::shared_ptr<MCInstPrinter> InstPrinter;
  PrettyPrinter *Printer;

  DisassemblerTarget(const Target *TheTarget, ObjectFile &Obj,
                     StringRef TripleName, StringRef MCPU,
                     SubtargetFeatures &Features);
  DisassemblerTarget(DisassemblerTarget &Other, SubtargetFeatures &Features);

private:
  MCTargetOptions Options;
  std::shared_ptr<const MCRegisterInfo> RegisterInfo;
  std::shared_ptr<const MCAsmInfo> AsmInfo;
  std::shared_ptr<const MCInstrInfo> InstrInfo;
  std::shared_ptr<MCObjectFileInfo> ObjectFileInfo;
};

DisassemblerTarget::DisassemblerTarget(const Target *TheTarget, ObjectFile &Obj,
                                       StringRef TripleName, StringRef MCPU,
                                       SubtargetFeatures &Features)
    : TheTarget(TheTarget),
      Printer(&selectPrettyPrinter(Triple(TripleName))),
      RegisterInfo(TheTarget->createMCRegInfo(TripleName)) {
  if (!RegisterInfo)
    reportError(Obj.getFileName(), "no register info for target " + TripleName);

  // Set up disassembler.
  AsmInfo.reset(TheTarget->createMCAsmInfo(*RegisterInfo, TripleName, Options));
  if (!AsmInfo)
    reportError(Obj.getFileName(), "no assembly info for target " + TripleName);

  SubtargetInfo.reset(
      TheTarget->createMCSubtargetInfo(TripleName, MCPU, Features.getString()));
  if (!SubtargetInfo)
    reportError(Obj.getFileName(),
                "no subtarget info for target " + TripleName);
  InstrInfo.reset(TheTarget->createMCInstrInfo());
  if (!InstrInfo)
    reportError(Obj.getFileName(),
                "no instruction info for target " + TripleName);
  Context =
      std::make_shared<MCContext>(Triple(TripleName), AsmInfo.get(),
                                  RegisterInfo.get(), SubtargetInfo.get());

  // FIXME: for now initialize MCObjectFileInfo with default values
  ObjectFileInfo.reset(
      TheTarget->createMCObjectFileInfo(*Context, /*PIC=*/false));
  Context->setObjectFileInfo(ObjectFileInfo.get());

  DisAsm.reset(TheTarget->createMCDisassembler(*SubtargetInfo, *Context));
  if (!DisAsm)
    reportError(Obj.getFileName(), "no disassembler for target " + TripleName);

  if (auto *ELFObj = dyn_cast<ELFObjectFileBase>(&Obj))
    DisAsm->setABIVersion(ELFObj->getEIdentABIVersion());

  InstrAnalysis.reset(TheTarget->createMCInstrAnalysis(InstrInfo.get()));

  int AsmPrinterVariant = AsmInfo->getAssemblerDialect();
  InstPrinter.reset(TheTarget->createMCInstPrinter(Triple(TripleName),
                                                   AsmPrinterVariant, *AsmInfo,
                                                   *InstrInfo, *RegisterInfo));
  if (!InstPrinter)
    reportError(Obj.getFileName(),
                "no instruction printer for target " + TripleName);
  InstPrinter->setPrintImmHex(PrintImmHex);
  InstPrinter->setPrintBranchImmAsAddress(true);
  InstPrinter->setSymbolizeOperands(SymbolizeOperands);
  InstPrinter->setMCInstrAnalysis(InstrAnalysis.get());

  switch (DisassemblyColor) {
  case ColorOutput::Enable:
    InstPrinter->setUseColor(true);
    break;
  case ColorOutput::Auto:
    InstPrinter->setUseColor(outs().has_colors());
    break;
  case ColorOutput::Disable:
  case ColorOutput::Invalid:
    InstPrinter->setUseColor(false);
    break;
  };
}

DisassemblerTarget::DisassemblerTarget(DisassemblerTarget &Other,
                                       SubtargetFeatures &Features)
    : TheTarget(Other.TheTarget),
      SubtargetInfo(TheTarget->createMCSubtargetInfo(TripleName, MCPU,
                                                     Features.getString())),
      Context(Other.Context),
      DisAsm(TheTarget->createMCDisassembler(*SubtargetInfo, *Context)),
      InstrAnalysis(Other.InstrAnalysis), InstPrinter(Other.InstPrinter),
      Printer(Other.Printer), RegisterInfo(Other.RegisterInfo),
      AsmInfo(Other.AsmInfo), InstrInfo(Other.InstrInfo),
      ObjectFileInfo(Other.ObjectFileInfo) {}
} // namespace

static uint8_t getElfSymbolType(const ObjectFile &Obj, const SymbolRef &Sym) {
  assert(Obj.isELF());
  if (auto *Elf32LEObj = dyn_cast<ELF32LEObjectFile>(&Obj))
    return unwrapOrError(Elf32LEObj->getSymbol(Sym.getRawDataRefImpl()),
                         Obj.getFileName())
        ->getType();
  if (auto *Elf64LEObj = dyn_cast<ELF64LEObjectFile>(&Obj))
    return unwrapOrError(Elf64LEObj->getSymbol(Sym.getRawDataRefImpl()),
                         Obj.getFileName())
        ->getType();
  if (auto *Elf32BEObj = dyn_cast<ELF32BEObjectFile>(&Obj))
    return unwrapOrError(Elf32BEObj->getSymbol(Sym.getRawDataRefImpl()),
                         Obj.getFileName())
        ->getType();
  if (auto *Elf64BEObj = cast<ELF64BEObjectFile>(&Obj))
    return unwrapOrError(Elf64BEObj->getSymbol(Sym.getRawDataRefImpl()),
                         Obj.getFileName())
        ->getType();
  llvm_unreachable("Unsupported binary format");
}

template <class ELFT>
static void
addDynamicElfSymbols(const ELFObjectFile<ELFT> &Obj,
                     std::map<SectionRef, SectionSymbolsTy> &AllSymbols) {
  for (auto Symbol : Obj.getDynamicSymbolIterators()) {
    uint8_t SymbolType = Symbol.getELFType();
    if (SymbolType == ELF::STT_SECTION)
      continue;

    uint64_t Address = unwrapOrError(Symbol.getAddress(), Obj.getFileName());
    // ELFSymbolRef::getAddress() returns size instead of value for common
    // symbols which is not desirable for disassembly output. Overriding.
    if (SymbolType == ELF::STT_COMMON)
      Address = unwrapOrError(Obj.getSymbol(Symbol.getRawDataRefImpl()),
                              Obj.getFileName())
                    ->st_value;

    StringRef Name = unwrapOrError(Symbol.getName(), Obj.getFileName());
    if (Name.empty())
      continue;

    section_iterator SecI =
        unwrapOrError(Symbol.getSection(), Obj.getFileName());
    if (SecI == Obj.section_end())
      continue;

    AllSymbols[*SecI].emplace_back(Address, Name, SymbolType);
  }
}

static void
addDynamicElfSymbols(const ELFObjectFileBase &Obj,
                     std::map<SectionRef, SectionSymbolsTy> &AllSymbols) {
  if (auto *Elf32LEObj = dyn_cast<ELF32LEObjectFile>(&Obj))
    addDynamicElfSymbols(*Elf32LEObj, AllSymbols);
  else if (auto *Elf64LEObj = dyn_cast<ELF64LEObjectFile>(&Obj))
    addDynamicElfSymbols(*Elf64LEObj, AllSymbols);
  else if (auto *Elf32BEObj = dyn_cast<ELF32BEObjectFile>(&Obj))
    addDynamicElfSymbols(*Elf32BEObj, AllSymbols);
  else if (auto *Elf64BEObj = cast<ELF64BEObjectFile>(&Obj))
    addDynamicElfSymbols(*Elf64BEObj, AllSymbols);
  else
    llvm_unreachable("Unsupported binary format");
}

static std::optional<SectionRef> getWasmCodeSection(const WasmObjectFile &Obj) {
  for (auto SecI : Obj.sections()) {
    const WasmSection &Section = Obj.getWasmSection(SecI);
    if (Section.Type == wasm::WASM_SEC_CODE)
      return SecI;
  }
  return std::nullopt;
}

static void
addMissingWasmCodeSymbols(const WasmObjectFile &Obj,
                          std::map<SectionRef, SectionSymbolsTy> &AllSymbols) {
  std::optional<SectionRef> Section = getWasmCodeSection(Obj);
  if (!Section)
    return;
  SectionSymbolsTy &Symbols = AllSymbols[*Section];

  std::set<uint64_t> SymbolAddresses;
  for (const auto &Sym : Symbols)
    SymbolAddresses.insert(Sym.Addr);

  for (const wasm::WasmFunction &Function : Obj.functions()) {
    // This adjustment mirrors the one in WasmObjectFile::getSymbolAddress.
    uint32_t Adjustment = Obj.isRelocatableObject() || Obj.isSharedObject()
                              ? 0
                              : Section->getAddress();
    uint64_t Address = Function.CodeSectionOffset + Adjustment;
    // Only add fallback symbols for functions not already present in the symbol
    // table.
    if (SymbolAddresses.count(Address))
      continue;
    // This function has no symbol, so it should have no SymbolName.
    assert(Function.SymbolName.empty());
    // We use DebugName for the name, though it may be empty if there is no
    // "name" custom section, or that section is missing a name for this
    // function.
    StringRef Name = Function.DebugName;
    Symbols.emplace_back(Address, Name, ELF::STT_NOTYPE);
  }
}

static void addPltEntries(const ObjectFile &Obj,
                          std::map<SectionRef, SectionSymbolsTy> &AllSymbols,
                          StringSaver &Saver) {
  auto *ElfObj = dyn_cast<ELFObjectFileBase>(&Obj);
  if (!ElfObj)
    return;
  DenseMap<StringRef, SectionRef> Sections;
  for (SectionRef Section : Obj.sections()) {
    Expected<StringRef> SecNameOrErr = Section.getName();
    if (!SecNameOrErr) {
      consumeError(SecNameOrErr.takeError());
      continue;
    }
    Sections[*SecNameOrErr] = Section;
  }
  for (auto Plt : ElfObj->getPltEntries()) {
    if (Plt.Symbol) {
      SymbolRef Symbol(*Plt.Symbol, ElfObj);
      uint8_t SymbolType = getElfSymbolType(Obj, Symbol);
      if (Expected<StringRef> NameOrErr = Symbol.getName()) {
        if (!NameOrErr->empty())
          AllSymbols[Sections[Plt.Section]].emplace_back(
              Plt.Address, Saver.save((*NameOrErr + "@plt").str()), SymbolType);
        continue;
      } else {
        // The warning has been reported in disassembleObject().
        consumeError(NameOrErr.takeError());
      }
    }
    reportWarning("PLT entry at 0x" + Twine::utohexstr(Plt.Address) +
                      " references an invalid symbol",
                  Obj.getFileName());
  }
}

// Normally the disassembly output will skip blocks of zeroes. This function
// returns the number of zero bytes that can be skipped when dumping the
// disassembly of the instructions in Buf.
static size_t countSkippableZeroBytes(ArrayRef<uint8_t> Buf) {
  // Find the number of leading zeroes.
  size_t N = 0;
  while (N < Buf.size() && !Buf[N])
    ++N;

  // We may want to skip blocks of zero bytes, but unless we see
  // at least 8 of them in a row.
  if (N < 8)
    return 0;

  // We skip zeroes in multiples of 4 because do not want to truncate an
  // instruction if it starts with a zero byte.
  return N & ~0x3;
}

// Returns a map from sections to their relocations.
static std::map<SectionRef, std::vector<RelocationRef>>
getRelocsMap(object::ObjectFile const &Obj) {
  std::map<SectionRef, std::vector<RelocationRef>> Ret;
  uint64_t I = (uint64_t)-1;
  for (SectionRef Sec : Obj.sections()) {
    ++I;
    Expected<section_iterator> RelocatedOrErr = Sec.getRelocatedSection();
    if (!RelocatedOrErr)
      reportError(Obj.getFileName(),
                  "section (" + Twine(I) +
                      "): failed to get a relocated section: " +
                      toString(RelocatedOrErr.takeError()));

    section_iterator Relocated = *RelocatedOrErr;
    if (Relocated == Obj.section_end() || !checkSectionFilter(*Relocated).Keep)
      continue;
    std::vector<RelocationRef> &V = Ret[*Relocated];
    append_range(V, Sec.relocations());
    // Sort relocations by address.
    llvm::stable_sort(V, isRelocAddressLess);
  }
  return Ret;
}

// Used for --adjust-vma to check if address should be adjusted by the
// specified value for a given section.
// For ELF we do not adjust non-allocatable sections like debug ones,
// because they are not loadable.
// TODO: implement for other file formats.
static bool shouldAdjustVA(const SectionRef &Section) {
  const ObjectFile *Obj = Section.getObject();
  if (Obj->isELF())
    return ELFSectionRef(Section).getFlags() & ELF::SHF_ALLOC;
  return false;
}


typedef std::pair<uint64_t, char> MappingSymbolPair;
static char getMappingSymbolKind(ArrayRef<MappingSymbolPair> MappingSymbols,
                                 uint64_t Address) {
  auto It =
      partition_point(MappingSymbols, [Address](const MappingSymbolPair &Val) {
        return Val.first <= Address;
      });
  // Return zero for any address before the first mapping symbol; this means
  // we should use the default disassembly mode, depending on the target.
  if (It == MappingSymbols.begin())
    return '\x00';
  return (It - 1)->second;
}

static uint64_t dumpARMELFData(uint64_t SectionAddr, uint64_t Index,
                               uint64_t End, const ObjectFile &Obj,
                               ArrayRef<uint8_t> Bytes,
                               ArrayRef<MappingSymbolPair> MappingSymbols,
                               const MCSubtargetInfo &STI, raw_ostream &OS) {
  llvm::endianness Endian =
      Obj.isLittleEndian() ? llvm::endianness::little : llvm::endianness::big;
  size_t Start = OS.tell();
  OS << format("%8" PRIx64 ": ", SectionAddr + Index);
  if (Index + 4 <= End) {
    dumpBytes(Bytes.slice(Index, 4), OS);
    AlignToInstStartColumn(Start, STI, OS);
    OS << "\t.word\t"
           << format_hex(support::endian::read32(Bytes.data() + Index, Endian),
                         10);
    return 4;
  }
  if (Index + 2 <= End) {
    dumpBytes(Bytes.slice(Index, 2), OS);
    AlignToInstStartColumn(Start, STI, OS);
    OS << "\t.short\t"
       << format_hex(support::endian::read16(Bytes.data() + Index, Endian), 6);
    return 2;
  }
  dumpBytes(Bytes.slice(Index, 1), OS);
  AlignToInstStartColumn(Start, STI, OS);
  OS << "\t.byte\t" << format_hex(Bytes[Index], 4);
  return 1;
}

static void dumpELFData(uint64_t SectionAddr, uint64_t Index, uint64_t End,
                        ArrayRef<uint8_t> Bytes) {
  // print out data up to 8 bytes at a time in hex and ascii
  uint8_t AsciiData[9] = {'\0'};
  uint8_t Byte;
  int NumBytes = 0;

  for (; Index < End; ++Index) {
    if (NumBytes == 0)
      outs() << format("%8" PRIx64 ":", SectionAddr + Index);
    Byte = Bytes.slice(Index)[0];
    outs() << format(" %02x", Byte);
    AsciiData[NumBytes] = isPrint(Byte) ? Byte : '.';

    uint8_t IndentOffset = 0;
    NumBytes++;
    if (Index == End - 1 || NumBytes > 8) {
      // Indent the space for less than 8 bytes data.
      // 2 spaces for byte and one for space between bytes
      IndentOffset = 3 * (8 - NumBytes);
      for (int Excess = NumBytes; Excess < 8; Excess++)
        AsciiData[Excess] = '\0';
      NumBytes = 8;
    }
    if (NumBytes == 8) {
      AsciiData[8] = '\0';
      outs() << std::string(IndentOffset, ' ') << "         ";
      outs() << reinterpret_cast<char *>(AsciiData);
      outs() << '\n';
      NumBytes = 0;
    }
  }
}

SymbolInfoTy objdump::createSymbolInfo(const ObjectFile &Obj,
                                       const SymbolRef &Symbol,
                                       bool IsMappingSymbol) {
  const StringRef FileName = Obj.getFileName();
  const uint64_t Addr = unwrapOrError(Symbol.getAddress(), FileName);
  const StringRef Name = unwrapOrError(Symbol.getName(), FileName);

  if (Obj.isXCOFF() && (SymbolDescription || TracebackTable)) {
    const auto &XCOFFObj = cast<XCOFFObjectFile>(Obj);
    DataRefImpl SymbolDRI = Symbol.getRawDataRefImpl();

    const uint32_t SymbolIndex = XCOFFObj.getSymbolIndex(SymbolDRI.p);
    std::optional<XCOFF::StorageMappingClass> Smc =
        getXCOFFSymbolCsectSMC(XCOFFObj, Symbol);
    return SymbolInfoTy(Smc, Addr, Name, SymbolIndex,
                        isLabel(XCOFFObj, Symbol));
  } else if (Obj.isXCOFF()) {
    const SymbolRef::Type SymType = unwrapOrError(Symbol.getType(), FileName);
    return SymbolInfoTy(Addr, Name, SymType, /*IsMappingSymbol=*/false,
                        /*IsXCOFF=*/true);
  } else if (Obj.isWasm()) {
    uint8_t SymType =
        cast<WasmObjectFile>(&Obj)->getWasmSymbol(Symbol).Info.Kind;
    return SymbolInfoTy(Addr, Name, SymType, false);
  } else {
    uint8_t Type =
        Obj.isELF() ? getElfSymbolType(Obj, Symbol) : (uint8_t)ELF::STT_NOTYPE;
    return SymbolInfoTy(Addr, Name, Type, IsMappingSymbol);
  }
}

static SymbolInfoTy createDummySymbolInfo(const ObjectFile &Obj,
                                          const uint64_t Addr, StringRef &Name,
                                          uint8_t Type) {
  if (Obj.isXCOFF() && (SymbolDescription || TracebackTable))
    return SymbolInfoTy(std::nullopt, Addr, Name, std::nullopt, false);
  if (Obj.isWasm())
    return SymbolInfoTy(Addr, Name, wasm::WASM_SYMBOL_TYPE_SECTION);
  return SymbolInfoTy(Addr, Name, Type);
}

static void collectBBAddrMapLabels(
    const BBAddrMapInfo &FullAddrMap, uint64_t SectionAddr, uint64_t Start,
    uint64_t End,
    std::unordered_map<uint64_t, std::vector<BBAddrMapLabel>> &Labels) {
  if (FullAddrMap.empty())
    return;
  Labels.clear();
  uint64_t StartAddress = SectionAddr + Start;
  uint64_t EndAddress = SectionAddr + End;
  const BBAddrMapFunctionEntry *FunctionMap =
      FullAddrMap.getEntryForAddress(StartAddress);
  if (!FunctionMap)
    return;
  std::optional<size_t> BBRangeIndex =
      FunctionMap->getAddrMap().getBBRangeIndexForBaseAddress(StartAddress);
  if (!BBRangeIndex)
    return;
  size_t NumBBEntriesBeforeRange = 0;
  for (size_t I = 0; I < *BBRangeIndex; ++I)
    NumBBEntriesBeforeRange +=
        FunctionMap->getAddrMap().BBRanges[I].BBEntries.size();
  const auto &BBRange = FunctionMap->getAddrMap().BBRanges[*BBRangeIndex];
  for (size_t I = 0; I < BBRange.BBEntries.size(); ++I) {
    const BBAddrMap::BBEntry &BBEntry = BBRange.BBEntries[I];
    uint64_t BBAddress = BBEntry.Offset + BBRange.BaseAddress;
    if (BBAddress >= EndAddress)
      continue;

    std::string LabelString = ("BB" + Twine(BBEntry.ID)).str();
    Labels[BBAddress].push_back(
        {LabelString, FunctionMap->constructPGOLabelString(
                          NumBBEntriesBeforeRange + I, PrettyPGOAnalysisMap)});
  }
}

static void
collectLocalBranchTargets(ArrayRef<uint8_t> Bytes, MCInstrAnalysis *MIA,
                          MCDisassembler *DisAsm, MCInstPrinter *IP,
                          const MCSubtargetInfo *STI, uint64_t SectionAddr,
                          uint64_t Start, uint64_t End,
                          std::unordered_map<uint64_t, std::string> &Labels) {
  // So far only supports PowerPC and X86.
  const bool isPPC = STI->getTargetTriple().isPPC();
  if (!isPPC && !STI->getTargetTriple().isX86())
    return;

  if (MIA)
    MIA->resetState();

  Labels.clear();
  unsigned LabelCount = 0;
  Start += SectionAddr;
  End += SectionAddr;
  const bool isXCOFF = STI->getTargetTriple().isOSBinFormatXCOFF();
  for (uint64_t Index = Start; Index < End;) {
    // Disassemble a real instruction and record function-local branch labels.
    MCInst Inst;
    uint64_t Size;
    ArrayRef<uint8_t> ThisBytes = Bytes.slice(Index - SectionAddr);
    bool Disassembled =
        DisAsm->getInstruction(Inst, Size, ThisBytes, Index, nulls());
    if (Size == 0)
      Size = std::min<uint64_t>(ThisBytes.size(),
                                DisAsm->suggestBytesToSkip(ThisBytes, Index));

    if (MIA) {
      if (Disassembled) {
        uint64_t Target;
        bool TargetKnown = MIA->evaluateBranch(Inst, Index, Size, Target);
        if (TargetKnown && (Target >= Start && Target < End) &&
            !Labels.count(Target)) {
          // On PowerPC and AIX, a function call is encoded as a branch to 0.
          // On other PowerPC platforms (ELF), a function call is encoded as
          // a branch to self. Do not add a label for these cases.
          if (!(isPPC &&
                ((Target == 0 && isXCOFF) || (Target == Index && !isXCOFF))))
            Labels[Target] = ("L" + Twine(LabelCount++)).str();
        }
        MIA->updateState(Inst, Index);
      } else
        MIA->resetState();
    }
    Index += Size;
  }
}

// Create an MCSymbolizer for the target and add it to the MCDisassembler.
// This is currently only used on AMDGPU, and assumes the format of the
// void * argument passed to AMDGPU's createMCSymbolizer.
static void addSymbolizer(
    MCContext &Ctx, const Target *Target, StringRef TripleName,
    MCDisassembler *DisAsm, uint64_t SectionAddr, ArrayRef<uint8_t> Bytes,
    SectionSymbolsTy &Symbols,
    std::vector<std::unique_ptr<std::string>> &SynthesizedLabelNames) {

  std::unique_ptr<MCRelocationInfo> RelInfo(
      Target->createMCRelocationInfo(TripleName, Ctx));
  if (!RelInfo)
    return;
  std::unique_ptr<MCSymbolizer> Symbolizer(Target->createMCSymbolizer(
      TripleName, nullptr, nullptr, &Symbols, &Ctx, std::move(RelInfo)));
  MCSymbolizer *SymbolizerPtr = &*Symbolizer;
  DisAsm->setSymbolizer(std::move(Symbolizer));

  if (!SymbolizeOperands)
    return;

  // Synthesize labels referenced by branch instructions by
  // disassembling, discarding the output, and collecting the referenced
  // addresses from the symbolizer.
  for (size_t Index = 0; Index != Bytes.size();) {
    MCInst Inst;
    uint64_t Size;
    ArrayRef<uint8_t> ThisBytes = Bytes.slice(Index);
    const uint64_t ThisAddr = SectionAddr + Index;
    DisAsm->getInstruction(Inst, Size, ThisBytes, ThisAddr, nulls());
    if (Size == 0)
      Size = std::min<uint64_t>(ThisBytes.size(),
                                DisAsm->suggestBytesToSkip(ThisBytes, Index));
    Index += Size;
  }
  ArrayRef<uint64_t> LabelAddrsRef = SymbolizerPtr->getReferencedAddresses();
  // Copy and sort to remove duplicates.
  std::vector<uint64_t> LabelAddrs;
  LabelAddrs.insert(LabelAddrs.end(), LabelAddrsRef.begin(),
                    LabelAddrsRef.end());
  llvm::sort(LabelAddrs);
  LabelAddrs.resize(llvm::unique(LabelAddrs) - LabelAddrs.begin());
  // Add the labels.
  for (unsigned LabelNum = 0; LabelNum != LabelAddrs.size(); ++LabelNum) {
    auto Name = std::make_unique<std::string>();
    *Name = (Twine("L") + Twine(LabelNum)).str();
    SynthesizedLabelNames.push_back(std::move(Name));
    Symbols.push_back(SymbolInfoTy(
        LabelAddrs[LabelNum], *SynthesizedLabelNames.back(), ELF::STT_NOTYPE));
  }
  llvm::stable_sort(Symbols);
  // Recreate the symbolizer with the new symbols list.
  RelInfo.reset(Target->createMCRelocationInfo(TripleName, Ctx));
  Symbolizer.reset(Target->createMCSymbolizer(
      TripleName, nullptr, nullptr, &Symbols, &Ctx, std::move(RelInfo)));
  DisAsm->setSymbolizer(std::move(Symbolizer));
}

static StringRef getSegmentName(const MachOObjectFile *MachO,
                                const SectionRef &Section) {
  if (MachO) {
    DataRefImpl DR = Section.getRawDataRefImpl();
    StringRef SegmentName = MachO->getSectionFinalSegmentName(DR);
    return SegmentName;
  }
  return "";
}

static void emitPostInstructionInfo(formatted_raw_ostream &FOS,
                                    const MCAsmInfo &MAI,
                                    const MCSubtargetInfo &STI,
                                    StringRef Comments,
                                    LiveVariablePrinter &LVP) {
  do {
    if (!Comments.empty()) {
      // Emit a line of comments.
      StringRef Comment;
      std::tie(Comment, Comments) = Comments.split('\n');
      // MAI.getCommentColumn() assumes that instructions are printed at the
      // position of 8, while getInstStartColumn() returns the actual position.
      unsigned CommentColumn =
          MAI.getCommentColumn() - 8 + getInstStartColumn(STI);
      FOS.PadToColumn(CommentColumn);
      FOS << MAI.getCommentString() << ' ' << Comment;
    }
    LVP.printAfterInst(FOS);
    FOS << '\n';
  } while (!Comments.empty());
  FOS.flush();
}

static void createFakeELFSections(ObjectFile &Obj) {
  assert(Obj.isELF());
  if (auto *Elf32LEObj = dyn_cast<ELF32LEObjectFile>(&Obj))
    Elf32LEObj->createFakeSections();
  else if (auto *Elf64LEObj = dyn_cast<ELF64LEObjectFile>(&Obj))
    Elf64LEObj->createFakeSections();
  else if (auto *Elf32BEObj = dyn_cast<ELF32BEObjectFile>(&Obj))
    Elf32BEObj->createFakeSections();
  else if (auto *Elf64BEObj = cast<ELF64BEObjectFile>(&Obj))
    Elf64BEObj->createFakeSections();
  else
    llvm_unreachable("Unsupported binary format");
}

// Tries to fetch a more complete version of the given object file using its
// Build ID. Returns std::nullopt if nothing was found.
static std::optional<OwningBinary<Binary>>
fetchBinaryByBuildID(const ObjectFile &Obj) {
  object::BuildIDRef BuildID = getBuildID(&Obj);
  if (BuildID.empty())
    return std::nullopt;
  std::optional<std::string> Path = BIDFetcher->fetch(BuildID);
  if (!Path)
    return std::nullopt;
  Expected<OwningBinary<Binary>> DebugBinary = createBinary(*Path);
  if (!DebugBinary) {
    reportWarning(toString(DebugBinary.takeError()), *Path);
    return std::nullopt;
  }
  return std::move(*DebugBinary);
}

static void
disassembleObject(ObjectFile &Obj, const ObjectFile &DbgObj,
                  DisassemblerTarget &PrimaryTarget,
                  std::optional<DisassemblerTarget> &SecondaryTarget,
                  SourcePrinter &SP, bool InlineRelocs) {
  DisassemblerTarget *DT = &PrimaryTarget;
  bool PrimaryIsThumb = false;
  SmallVector<std::pair<uint64_t, uint64_t>, 0> CHPECodeMap;

  if (SecondaryTarget) {
    if (isArmElf(Obj)) {
      PrimaryIsThumb =
          PrimaryTarget.SubtargetInfo->checkFeatures("+thumb-mode");
    } else if (const auto *COFFObj = dyn_cast<COFFObjectFile>(&Obj)) {
      const chpe_metadata *CHPEMetadata = COFFObj->getCHPEMetadata();
      if (CHPEMetadata && CHPEMetadata->CodeMapCount) {
        uintptr_t CodeMapInt;
        cantFail(COFFObj->getRvaPtr(CHPEMetadata->CodeMap, CodeMapInt));
        auto CodeMap = reinterpret_cast<const chpe_range_entry *>(CodeMapInt);

        for (uint32_t i = 0; i < CHPEMetadata->CodeMapCount; ++i) {
          if (CodeMap[i].getType() == chpe_range_type::Amd64 &&
              CodeMap[i].Length) {
            // Store x86_64 CHPE code ranges.
            uint64_t Start = CodeMap[i].getStart() + COFFObj->getImageBase();
            CHPECodeMap.emplace_back(Start, Start + CodeMap[i].Length);
          }
        }
        llvm::sort(CHPECodeMap);
      }
    }
  }

  std::map<SectionRef, std::vector<RelocationRef>> RelocMap;
  if (InlineRelocs || Obj.isXCOFF())
    RelocMap = getRelocsMap(Obj);
  bool Is64Bits = Obj.getBytesInAddress() > 4;

  // Create a mapping from virtual address to symbol name.  This is used to
  // pretty print the symbols while disassembling.
  std::map<SectionRef, SectionSymbolsTy> AllSymbols;
  std::map<SectionRef, SmallVector<MappingSymbolPair, 0>> AllMappingSymbols;
  SectionSymbolsTy AbsoluteSymbols;
  const StringRef FileName = Obj.getFileName();
  const MachOObjectFile *MachO = dyn_cast<const MachOObjectFile>(&Obj);
  for (const SymbolRef &Symbol : Obj.symbols()) {
    Expected<StringRef> NameOrErr = Symbol.getName();
    if (!NameOrErr) {
      reportWarning(toString(NameOrErr.takeError()), FileName);
      continue;
    }
    if (NameOrErr->empty() && !(Obj.isXCOFF() && SymbolDescription))
      continue;

    if (Obj.isELF() &&
        (cantFail(Symbol.getFlags()) & SymbolRef::SF_FormatSpecific)) {
      // Symbol is intended not to be displayed by default (STT_FILE,
      // STT_SECTION, or a mapping symbol). Ignore STT_SECTION symbols. We will
      // synthesize a section symbol if no symbol is defined at offset 0.
      //
      // For a mapping symbol, store it within both AllSymbols and
      // AllMappingSymbols. If --show-all-symbols is unspecified, its label will
      // not be printed in disassembly listing.
      if (getElfSymbolType(Obj, Symbol) != ELF::STT_SECTION &&
          hasMappingSymbols(Obj)) {
        section_iterator SecI = unwrapOrError(Symbol.getSection(), FileName);
        if (SecI != Obj.section_end()) {
          uint64_t SectionAddr = SecI->getAddress();
          uint64_t Address = cantFail(Symbol.getAddress());
          StringRef Name = *NameOrErr;
          if (Name.consume_front("$") && Name.size() &&
              strchr("adtx", Name[0])) {
            AllMappingSymbols[*SecI].emplace_back(Address - SectionAddr,
                                                  Name[0]);
            AllSymbols[*SecI].push_back(
                createSymbolInfo(Obj, Symbol, /*MappingSymbol=*/true));
          }
        }
      }
      continue;
    }

    if (MachO) {
      // __mh_(execute|dylib|dylinker|bundle|preload|object)_header are special
      // symbols that support MachO header introspection. They do not bind to
      // code locations and are irrelevant for disassembly.
      if (NameOrErr->starts_with("__mh_") && NameOrErr->ends_with("_header"))
        continue;
      // Don't ask a Mach-O STAB symbol for its section unless you know that
      // STAB symbol's section field refers to a valid section index. Otherwise
      // the symbol may error trying to load a section that does not exist.
      DataRefImpl SymDRI = Symbol.getRawDataRefImpl();
      uint8_t NType = (MachO->is64Bit() ?
                       MachO->getSymbol64TableEntry(SymDRI).n_type:
                       MachO->getSymbolTableEntry(SymDRI).n_type);
      if (NType & MachO::N_STAB)
        continue;
    }

    section_iterator SecI = unwrapOrError(Symbol.getSection(), FileName);
    if (SecI != Obj.section_end())
      AllSymbols[*SecI].push_back(createSymbolInfo(Obj, Symbol));
    else
      AbsoluteSymbols.push_back(createSymbolInfo(Obj, Symbol));
  }

  if (AllSymbols.empty() && Obj.isELF())
    addDynamicElfSymbols(cast<ELFObjectFileBase>(Obj), AllSymbols);

  if (Obj.isWasm())
    addMissingWasmCodeSymbols(cast<WasmObjectFile>(Obj), AllSymbols);

  if (Obj.isELF() && Obj.sections().empty())
    createFakeELFSections(Obj);

  BumpPtrAllocator A;
  StringSaver Saver(A);
  addPltEntries(Obj, AllSymbols, Saver);

  // Create a mapping from virtual address to section. An empty section can
  // cause more than one section at the same address. Sort such sections to be
  // before same-addressed non-empty sections so that symbol lookups prefer the
  // non-empty section.
  std::vector<std::pair<uint64_t, SectionRef>> SectionAddresses;
  for (SectionRef Sec : Obj.sections())
    SectionAddresses.emplace_back(Sec.getAddress(), Sec);
  llvm::stable_sort(SectionAddresses, [](const auto &LHS, const auto &RHS) {
    if (LHS.first != RHS.first)
      return LHS.first < RHS.first;
    return LHS.second.getSize() < RHS.second.getSize();
  });

  // Linked executables (.exe and .dll files) typically don't include a real
  // symbol table but they might contain an export table.
  if (const auto *COFFObj = dyn_cast<COFFObjectFile>(&Obj)) {
    for (const auto &ExportEntry : COFFObj->export_directories()) {
      StringRef Name;
      if (Error E = ExportEntry.getSymbolName(Name))
        reportError(std::move(E), Obj.getFileName());
      if (Name.empty())
        continue;

      uint32_t RVA;
      if (Error E = ExportEntry.getExportRVA(RVA))
        reportError(std::move(E), Obj.getFileName());

      uint64_t VA = COFFObj->getImageBase() + RVA;
      auto Sec = partition_point(
          SectionAddresses, [VA](const std::pair<uint64_t, SectionRef> &O) {
            return O.first <= VA;
          });
      if (Sec != SectionAddresses.begin()) {
        --Sec;
        AllSymbols[Sec->second].emplace_back(VA, Name, ELF::STT_NOTYPE);
      } else
        AbsoluteSymbols.emplace_back(VA, Name, ELF::STT_NOTYPE);
    }
  }

  // Sort all the symbols, this allows us to use a simple binary search to find
  // Multiple symbols can have the same address. Use a stable sort to stabilize
  // the output.
  StringSet<> FoundDisasmSymbolSet;
  for (std::pair<const SectionRef, SectionSymbolsTy> &SecSyms : AllSymbols)
    llvm::stable_sort(SecSyms.second);
  llvm::stable_sort(AbsoluteSymbols);

  std::unique_ptr<DWARFContext> DICtx;
  LiveVariablePrinter LVP(*DT->Context->getRegisterInfo(), *DT->SubtargetInfo);

  if (DbgVariables != DVDisabled) {
    DICtx = DWARFContext::create(DbgObj);
    for (const std::unique_ptr<DWARFUnit> &CU : DICtx->compile_units())
      LVP.addCompileUnit(CU->getUnitDIE(false));
  }

  LLVM_DEBUG(LVP.dump());

  BBAddrMapInfo FullAddrMap;
  auto ReadBBAddrMap = [&](std::optional<unsigned> SectionIndex =
                               std::nullopt) {
    FullAddrMap.clear();
    if (const auto *Elf = dyn_cast<ELFObjectFileBase>(&Obj)) {
      std::vector<PGOAnalysisMap> PGOAnalyses;
      auto BBAddrMapsOrErr = Elf->readBBAddrMap(SectionIndex, &PGOAnalyses);
      if (!BBAddrMapsOrErr) {
        reportWarning(toString(BBAddrMapsOrErr.takeError()), Obj.getFileName());
        return;
      }
      for (auto &&[FunctionBBAddrMap, FunctionPGOAnalysis] :
           zip_equal(*std::move(BBAddrMapsOrErr), std::move(PGOAnalyses))) {
        FullAddrMap.AddFunctionEntry(std::move(FunctionBBAddrMap),
                                     std::move(FunctionPGOAnalysis));
      }
    }
  };

  // For non-relocatable objects, Read all LLVM_BB_ADDR_MAP sections into a
  // single mapping, since they don't have any conflicts.
  if (SymbolizeOperands && !Obj.isRelocatableObject())
    ReadBBAddrMap();

  std::optional<llvm::BTFParser> BTF;
  if (InlineRelocs && BTFParser::hasBTFSections(Obj)) {
    BTF.emplace();
    BTFParser::ParseOptions Opts = {};
    Opts.LoadTypes = true;
    Opts.LoadRelocs = true;
    if (Error E = BTF->parse(Obj, Opts))
      WithColor::defaultErrorHandler(std::move(E));
  }

  for (const SectionRef &Section : ToolSectionFilter(Obj)) {
    if (FilterSections.empty() && !DisassembleAll &&
        (!Section.isText() || Section.isVirtual()))
      continue;

    uint64_t SectionAddr = Section.getAddress();
    uint64_t SectSize = Section.getSize();
    if (!SectSize)
      continue;

    // For relocatable object files, read the LLVM_BB_ADDR_MAP section
    // corresponding to this section, if present.
    if (SymbolizeOperands && Obj.isRelocatableObject())
      ReadBBAddrMap(Section.getIndex());

    // Get the list of all the symbols in this section.
    SectionSymbolsTy &Symbols = AllSymbols[Section];
    auto &MappingSymbols = AllMappingSymbols[Section];
    llvm::sort(MappingSymbols);

    ArrayRef<uint8_t> Bytes = arrayRefFromStringRef(
        unwrapOrError(Section.getContents(), Obj.getFileName()));

    std::vector<std::unique_ptr<std::string>> SynthesizedLabelNames;
    if (Obj.isELF() && Obj.getArch() == Triple::amdgcn) {
      // AMDGPU disassembler uses symbolizer for printing labels
      addSymbolizer(*DT->Context, DT->TheTarget, TripleName, DT->DisAsm.get(),
                    SectionAddr, Bytes, Symbols, SynthesizedLabelNames);
    }

    StringRef SegmentName = getSegmentName(MachO, Section);
    StringRef SectionName = unwrapOrError(Section.getName(), Obj.getFileName());
    // If the section has no symbol at the start, just insert a dummy one.
    // Without --show-all-symbols, also insert one if all symbols at the start
    // are mapping symbols.
    bool CreateDummy = Symbols.empty();
    if (!CreateDummy) {
      CreateDummy = true;
      for (auto &Sym : Symbols) {
        if (Sym.Addr != SectionAddr)
          break;
        if (!Sym.IsMappingSymbol || ShowAllSymbols)
          CreateDummy = false;
      }
    }
    if (CreateDummy) {
      SymbolInfoTy Sym = createDummySymbolInfo(
          Obj, SectionAddr, SectionName,
          Section.isText() ? ELF::STT_FUNC : ELF::STT_OBJECT);
      if (Obj.isXCOFF())
        Symbols.insert(Symbols.begin(), Sym);
      else
        Symbols.insert(llvm::lower_bound(Symbols, Sym), Sym);
    }

    SmallString<40> Comments;
    raw_svector_ostream CommentStream(Comments);

    uint64_t VMAAdjustment = 0;
    if (shouldAdjustVA(Section))
      VMAAdjustment = AdjustVMA;

    // In executable and shared objects, r_offset holds a virtual address.
    // Subtract SectionAddr from the r_offset field of a relocation to get
    // the section offset.
    uint64_t RelAdjustment = Obj.isRelocatableObject() ? 0 : SectionAddr;
    uint64_t Size;
    uint64_t Index;
    bool PrintedSection = false;
    std::vector<RelocationRef> Rels = RelocMap[Section];
    std::vector<RelocationRef>::const_iterator RelCur = Rels.begin();
    std::vector<RelocationRef>::const_iterator RelEnd = Rels.end();

    // Loop over each chunk of code between two points where at least
    // one symbol is defined.
    for (size_t SI = 0, SE = Symbols.size(); SI != SE;) {
      // Advance SI past all the symbols starting at the same address,
      // and make an ArrayRef of them.
      unsigned FirstSI = SI;
      uint64_t Start = Symbols[SI].Addr;
      ArrayRef<SymbolInfoTy> SymbolsHere;
      while (SI != SE && Symbols[SI].Addr == Start)
        ++SI;
      SymbolsHere = ArrayRef<SymbolInfoTy>(&Symbols[FirstSI], SI - FirstSI);

      // Get the demangled names of all those symbols. We end up with a vector
      // of StringRef that holds the names we're going to use, and a vector of
      // std::string that stores the new strings returned by demangle(), if
      // any. If we don't call demangle() then that vector can stay empty.
      std::vector<StringRef> SymNamesHere;
      std::vector<std::string> DemangledSymNamesHere;
      if (Demangle) {
        // Fetch the demangled names and store them locally.
        for (const SymbolInfoTy &Symbol : SymbolsHere)
          DemangledSymNamesHere.push_back(demangle(Symbol.Name));
        // Now we've finished modifying that vector, it's safe to make
        // a vector of StringRefs pointing into it.
        SymNamesHere.insert(SymNamesHere.begin(), DemangledSymNamesHere.begin(),
                            DemangledSymNamesHere.end());
      } else {
        for (const SymbolInfoTy &Symbol : SymbolsHere)
          SymNamesHere.push_back(Symbol.Name);
      }

      // Distinguish ELF data from code symbols, which will be used later on to
      // decide whether to 'disassemble' this chunk as a data declaration via
      // dumpELFData(), or whether to treat it as code.
      //
      // If data _and_ code symbols are defined at the same address, the code
      // takes priority, on the grounds that disassembling code is our main
      // purpose here, and it would be a worse failure to _not_ interpret
      // something that _was_ meaningful as code than vice versa.
      //
      // Any ELF symbol type that is not clearly data will be regarded as code.
      // In particular, one of the uses of STT_NOTYPE is for branch targets
      // inside functions, for which STT_FUNC would be inaccurate.
      //
      // So here, we spot whether there's any non-data symbol present at all,
      // and only set the DisassembleAsELFData flag if there isn't. Also, we use
      // this distinction to inform the decision of which symbol to print at
      // the head of the section, so that if we're printing code, we print a
      // code-related symbol name to go with it.
      bool DisassembleAsELFData = false;
      size_t DisplaySymIndex = SymbolsHere.size() - 1;
      if (Obj.isELF() && !DisassembleAll && Section.isText()) {
        DisassembleAsELFData = true; // unless we find a code symbol below

        for (size_t i = 0; i < SymbolsHere.size(); ++i) {
          uint8_t SymTy = SymbolsHere[i].Type;
          if (SymTy != ELF::STT_OBJECT && SymTy != ELF::STT_COMMON) {
            DisassembleAsELFData = false;
            DisplaySymIndex = i;
          }
        }
      }

      // Decide which symbol(s) from this collection we're going to print.
      std::vector<bool> SymsToPrint(SymbolsHere.size(), false);
      // If the user has given the --disassemble-symbols option, then we must
      // display every symbol in that set, and no others.
      if (!DisasmSymbolSet.empty()) {
        bool FoundAny = false;
        for (size_t i = 0; i < SymbolsHere.size(); ++i) {
          if (DisasmSymbolSet.count(SymNamesHere[i])) {
            SymsToPrint[i] = true;
            FoundAny = true;
          }
        }

        // And if none of the symbols here is one that the user asked for, skip
        // disassembling this entire chunk of code.
        if (!FoundAny)
          continue;
      } else if (!SymbolsHere[DisplaySymIndex].IsMappingSymbol) {
        // Otherwise, print whichever symbol at this location is last in the
        // Symbols array, because that array is pre-sorted in a way intended to
        // correlate with priority of which symbol to display.
        SymsToPrint[DisplaySymIndex] = true;
      }

      // Now that we know we're disassembling this section, override the choice
      // of which symbols to display by printing _all_ of them at this address
      // if the user asked for all symbols.
      //
      // That way, '--show-all-symbols --disassemble-symbol=foo' will print
      // only the chunk of code headed by 'foo', but also show any other
      // symbols defined at that address, such as aliases for 'foo', or the ARM
      // mapping symbol preceding its code.
      if (ShowAllSymbols) {
        for (size_t i = 0; i < SymbolsHere.size(); ++i)
          SymsToPrint[i] = true;
      }

      if (Start < SectionAddr || StopAddress <= Start)
        continue;

      for (size_t i = 0; i < SymbolsHere.size(); ++i)
        FoundDisasmSymbolSet.insert(SymNamesHere[i]);

      // The end is the section end, the beginning of the next symbol, or
      // --stop-address.
      uint64_t End = std::min<uint64_t>(SectionAddr + SectSize, StopAddress);
      if (SI < SE)
        End = std::min(End, Symbols[SI].Addr);
      if (Start >= End || End <= StartAddress)
        continue;
      Start -= SectionAddr;
      End -= SectionAddr;

      if (!PrintedSection) {
        PrintedSection = true;
        outs() << "\nDisassembly of section ";
        if (!SegmentName.empty())
          outs() << SegmentName << ",";
        outs() << SectionName << ":\n";
      }

      bool PrintedLabel = false;
      for (size_t i = 0; i < SymbolsHere.size(); ++i) {
        if (!SymsToPrint[i])
          continue;

        const SymbolInfoTy &Symbol = SymbolsHere[i];
        const StringRef SymbolName = SymNamesHere[i];

        if (!PrintedLabel) {
          outs() << '\n';
          PrintedLabel = true;
        }
        if (LeadingAddr)
          outs() << format(Is64Bits ? "%016" PRIx64 " " : "%08" PRIx64 " ",
                           SectionAddr + Start + VMAAdjustment);
        if (Obj.isXCOFF() && SymbolDescription) {
          outs() << getXCOFFSymbolDescription(Symbol, SymbolName) << ":\n";
        } else
          outs() << '<' << SymbolName << ">:\n";
      }

      // Don't print raw contents of a virtual section. A virtual section
      // doesn't have any contents in the file.
      if (Section.isVirtual()) {
        outs() << "...\n";
        continue;
      }

      // See if any of the symbols defined at this location triggers target-
      // specific disassembly behavior, e.g. of special descriptors or function
      // prelude information.
      //
      // We stop this loop at the first symbol that triggers some kind of
      // interesting behavior (if any), on the assumption that if two symbols
      // defined at the same address trigger two conflicting symbol handlers,
      // the object file is probably confused anyway, and it would make even
      // less sense to present the output of _both_ handlers, because that
      // would describe the same data twice.
      for (size_t SHI = 0; SHI < SymbolsHere.size(); ++SHI) {
        SymbolInfoTy Symbol = SymbolsHere[SHI];

        Expected<bool> RespondedOrErr = DT->DisAsm->onSymbolStart(
            Symbol, Size, Bytes.slice(Start, End - Start), SectionAddr + Start);

        if (RespondedOrErr && !*RespondedOrErr) {
          // This symbol didn't trigger any interesting handling. Try the other
          // symbols defined at this address.
          continue;
        }

        // If onSymbolStart returned an Error, that means it identified some
        // kind of special data at this address, but wasn't able to disassemble
        // it meaningfully. So we fall back to printing the error out and
        // disassembling the failed region as bytes, assuming that the target
        // detected the failure before printing anything.
        if (!RespondedOrErr) {
          std::string ErrMsgStr = toString(RespondedOrErr.takeError());
          StringRef ErrMsg = ErrMsgStr;
          do {
            StringRef Line;
            std::tie(Line, ErrMsg) = ErrMsg.split('\n');
            outs() << DT->Context->getAsmInfo()->getCommentString()
                   << " error decoding " << SymNamesHere[SHI] << ": " << Line
                   << '\n';
          } while (!ErrMsg.empty());

          if (Size) {
            outs() << DT->Context->getAsmInfo()->getCommentString()
                   << " decoding failed region as bytes\n";
            for (uint64_t I = 0; I < Size; ++I)
              outs() << "\t.byte\t " << format_hex(Bytes[I], 1, /*Upper=*/true)
                     << '\n';
          }
        }

        // Regardless of whether onSymbolStart returned an Error or true, 'Size'
        // will have been set to the amount of data covered by whatever prologue
        // the target identified. So we advance our own position to beyond that.
        // Sometimes that will be the entire distance to the next symbol, and
        // sometimes it will be just a prologue and we should start
        // disassembling instructions from where it left off.
        Start += Size;
        break;
      }

      Index = Start;
      if (SectionAddr < StartAddress)
        Index = std::max<uint64_t>(Index, StartAddress - SectionAddr);

      if (DisassembleAsELFData) {
        dumpELFData(SectionAddr, Index, End, Bytes);
        Index = End;
        continue;
      }

      // Skip relocations from symbols that are not dumped.
      for (; RelCur != RelEnd; ++RelCur) {
        uint64_t Offset = RelCur->getOffset() - RelAdjustment;
        if (Index <= Offset)
          break;
      }

      bool DumpARMELFData = false;
      bool DumpTracebackTableForXCOFFFunction =
          Obj.isXCOFF() && Section.isText() && TracebackTable &&
          Symbols[SI - 1].XCOFFSymInfo.StorageMappingClass &&
          (*Symbols[SI - 1].XCOFFSymInfo.StorageMappingClass == XCOFF::XMC_PR);

      formatted_raw_ostream FOS(outs());

      std::unordered_map<uint64_t, std::string> AllLabels;
      std::unordered_map<uint64_t, std::vector<BBAddrMapLabel>> BBAddrMapLabels;
      if (SymbolizeOperands) {
        collectLocalBranchTargets(Bytes, DT->InstrAnalysis.get(),
                                  DT->DisAsm.get(), DT->InstPrinter.get(),
                                  PrimaryTarget.SubtargetInfo.get(),
                                  SectionAddr, Index, End, AllLabels);
        collectBBAddrMapLabels(FullAddrMap, SectionAddr, Index, End,
                               BBAddrMapLabels);
      }

      if (DT->InstrAnalysis)
        DT->InstrAnalysis->resetState();

      while (Index < End) {
        uint64_t RelOffset;

        // ARM and AArch64 ELF binaries can interleave data and text in the
        // same section. We rely on the markers introduced to understand what
        // we need to dump. If the data marker is within a function, it is
        // denoted as a word/short etc.
        if (!MappingSymbols.empty()) {
          char Kind = getMappingSymbolKind(MappingSymbols, Index);
          DumpARMELFData = Kind == 'd';
          if (SecondaryTarget) {
            if (Kind == 'a') {
              DT = PrimaryIsThumb ? &*SecondaryTarget : &PrimaryTarget;
            } else if (Kind == 't') {
              DT = PrimaryIsThumb ? &PrimaryTarget : &*SecondaryTarget;
            }
          }
        } else if (!CHPECodeMap.empty()) {
          uint64_t Address = SectionAddr + Index;
          auto It = partition_point(
              CHPECodeMap,
              [Address](const std::pair<uint64_t, uint64_t> &Entry) {
                return Entry.first <= Address;
              });
          if (It != CHPECodeMap.begin() && Address < (It - 1)->second) {
            DT = &*SecondaryTarget;
          } else {
            DT = &PrimaryTarget;
            // X64 disassembler range may have left Index unaligned, so
            // make sure that it's aligned when we switch back to ARM64
            // code.
            Index = llvm::alignTo(Index, 4);
            if (Index >= End)
              break;
          }
        }

        auto findRel = [&]() {
          while (RelCur != RelEnd) {
            RelOffset = RelCur->getOffset() - RelAdjustment;
            // If this relocation is hidden, skip it.
            if (getHidden(*RelCur) || SectionAddr + RelOffset < StartAddress) {
              ++RelCur;
              continue;
            }

            // Stop when RelCur's offset is past the disassembled
            // instruction/data.
            if (RelOffset >= Index + Size)
              return false;
            if (RelOffset >= Index)
              return true;
            ++RelCur;
          }
          return false;
        };

        if (DumpARMELFData) {
          Size = dumpARMELFData(SectionAddr, Index, End, Obj, Bytes,
                                MappingSymbols, *DT->SubtargetInfo, FOS);
        } else {
          // When -z or --disassemble-zeroes are given we always dissasemble
          // them. Otherwise we might want to skip zero bytes we see.
          if (!DisassembleZeroes) {
            uint64_t MaxOffset = End - Index;
            // For --reloc: print zero blocks patched by relocations, so that
            // relocations can be shown in the dump.
            if (InlineRelocs && RelCur != RelEnd)
              MaxOffset = std::min(RelCur->getOffset() - RelAdjustment - Index,
                                   MaxOffset);

            if (size_t N =
                    countSkippableZeroBytes(Bytes.slice(Index, MaxOffset))) {
              FOS << "\t\t..." << '\n';
              Index += N;
              continue;
            }
          }

          if (DumpTracebackTableForXCOFFFunction &&
              doesXCOFFTracebackTableBegin(Bytes.slice(Index, 4))) {
            dumpTracebackTable(Bytes.slice(Index),
                               SectionAddr + Index + VMAAdjustment, FOS,
                               SectionAddr + End + VMAAdjustment,
                               *DT->SubtargetInfo, cast<XCOFFObjectFile>(&Obj));
            Index = End;
            continue;
          }

          // Print local label if there's any.
          auto Iter1 = BBAddrMapLabels.find(SectionAddr + Index);
          if (Iter1 != BBAddrMapLabels.end()) {
            for (const auto &BBLabel : Iter1->second)
              FOS << "<" << BBLabel.BlockLabel << ">" << BBLabel.PGOAnalysis
                  << ":\n";
          } else {
            auto Iter2 = AllLabels.find(SectionAddr + Index);
            if (Iter2 != AllLabels.end())
              FOS << "<" << Iter2->second << ">:\n";
          }

          // Disassemble a real instruction or a data when disassemble all is
          // provided
          MCInst Inst;
          ArrayRef<uint8_t> ThisBytes = Bytes.slice(Index);
          uint64_t ThisAddr = SectionAddr + Index;
          bool Disassembled = DT->DisAsm->getInstruction(
              Inst, Size, ThisBytes, ThisAddr, CommentStream);
          if (Size == 0)
            Size = std::min<uint64_t>(
                ThisBytes.size(),
                DT->DisAsm->suggestBytesToSkip(ThisBytes, ThisAddr));

          LVP.update({Index, Section.getIndex()},
                     {Index + Size, Section.getIndex()}, Index + Size != End);

          DT->InstPrinter->setCommentStream(CommentStream);

          DT->Printer->printInst(
              *DT->InstPrinter, Disassembled ? &Inst : nullptr,
              Bytes.slice(Index, Size),
              {SectionAddr + Index + VMAAdjustment, Section.getIndex()}, FOS,
              "", *DT->SubtargetInfo, &SP, Obj.getFileName(), &Rels, LVP);

          DT->InstPrinter->setCommentStream(llvm::nulls());

          // If disassembly succeeds, we try to resolve the target address
          // (jump target or memory operand address) and print it to the
          // right of the instruction.
          //
          // Otherwise, we don't print anything else so that we avoid
          // analyzing invalid or incomplete instruction information.
          if (Disassembled && DT->InstrAnalysis) {
            llvm::raw_ostream *TargetOS = &FOS;
            uint64_t Target;
            bool PrintTarget = DT->InstrAnalysis->evaluateBranch(
                Inst, SectionAddr + Index, Size, Target);

            if (!PrintTarget) {
              if (std::optional<uint64_t> MaybeTarget =
                      DT->InstrAnalysis->evaluateMemoryOperandAddress(
                          Inst, DT->SubtargetInfo.get(), SectionAddr + Index,
                          Size)) {
                Target = *MaybeTarget;
                PrintTarget = true;
                // Do not print real address when symbolizing.
                if (!SymbolizeOperands) {
                  // Memory operand addresses are printed as comments.
                  TargetOS = &CommentStream;
                  *TargetOS << "0x" << Twine::utohexstr(Target);
                }
              }
            }

            if (PrintTarget) {
              // In a relocatable object, the target's section must reside in
              // the same section as the call instruction or it is accessed
              // through a relocation.
              //
              // In a non-relocatable object, the target may be in any section.
              // In that case, locate the section(s) containing the target
              // address and find the symbol in one of those, if possible.
              //
              // N.B. Except for XCOFF, we don't walk the relocations in the
              // relocatable case yet.
              std::vector<const SectionSymbolsTy *> TargetSectionSymbols;
              if (!Obj.isRelocatableObject()) {
                auto It = llvm::partition_point(
                    SectionAddresses,
                    [=](const std::pair<uint64_t, SectionRef> &O) {
                      return O.first <= Target;
                    });
                uint64_t TargetSecAddr = 0;
                while (It != SectionAddresses.begin()) {
                  --It;
                  if (TargetSecAddr == 0)
                    TargetSecAddr = It->first;
                  if (It->first != TargetSecAddr)
                    break;
                  TargetSectionSymbols.push_back(&AllSymbols[It->second]);
                }
              } else {
                TargetSectionSymbols.push_back(&Symbols);
              }
              TargetSectionSymbols.push_back(&AbsoluteSymbols);

              // Find the last symbol in the first candidate section whose
              // offset is less than or equal to the target. If there are no
              // such symbols, try in the next section and so on, before finally
              // using the nearest preceding absolute symbol (if any), if there
              // are no other valid symbols.
              const SymbolInfoTy *TargetSym = nullptr;
              for (const SectionSymbolsTy *TargetSymbols :
                   TargetSectionSymbols) {
                auto It = llvm::partition_point(
                    *TargetSymbols,
                    [=](const SymbolInfoTy &O) { return O.Addr <= Target; });
                while (It != TargetSymbols->begin()) {
                  --It;
                  // Skip mapping symbols to avoid possible ambiguity as they
                  // do not allow uniquely identifying the target address.
                  if (!It->IsMappingSymbol) {
                    TargetSym = &*It;
                    break;
                  }
                }
                if (TargetSym)
                  break;
              }

              // Branch targets are printed just after the instructions.
              // Print the labels corresponding to the target if there's any.
              bool BBAddrMapLabelAvailable = BBAddrMapLabels.count(Target);
              bool LabelAvailable = AllLabels.count(Target);

              if (TargetSym != nullptr) {
                uint64_t TargetAddress = TargetSym->Addr;
                uint64_t Disp = Target - TargetAddress;
                std::string TargetName = Demangle ? demangle(TargetSym->Name)
                                                  : TargetSym->Name.str();
                bool RelFixedUp = false;
                SmallString<32> Val;

                *TargetOS << " <";
                // On XCOFF, we use relocations, even without -r, so we
                // can print the correct name for an extern function call.
                if (Obj.isXCOFF() && findRel()) {
                  // Check for possible branch relocations and
                  // branches to fixup code.
                  bool BranchRelocationType = true;
                  XCOFF::RelocationType RelocType;
                  if (Obj.is64Bit()) {
                    const XCOFFRelocation64 *Reloc =
                        reinterpret_cast<XCOFFRelocation64 *>(
                            RelCur->getRawDataRefImpl().p);
                    RelFixedUp = Reloc->isFixupIndicated();
                    RelocType = Reloc->Type;
                  } else {
                    const XCOFFRelocation32 *Reloc =
                        reinterpret_cast<XCOFFRelocation32 *>(
                            RelCur->getRawDataRefImpl().p);
                    RelFixedUp = Reloc->isFixupIndicated();
                    RelocType = Reloc->Type;
                  }
                  BranchRelocationType =
                      RelocType == XCOFF::R_BA || RelocType == XCOFF::R_BR ||
                      RelocType == XCOFF::R_RBA || RelocType == XCOFF::R_RBR;

                  // If we have a valid relocation, try to print its
                  // corresponding symbol name. Multiple relocations on the
                  // same instruction are not handled.
                  // Branches to fixup code will have the RelFixedUp flag set in
                  // the RLD. For these instructions, we print the correct
                  // branch target, but print the referenced symbol as a
                  // comment.
                  if (Error E = getRelocationValueString(*RelCur, false, Val)) {
                    // If -r was used, this error will be printed later.
                    // Otherwise, we ignore the error and print what
                    // would have been printed without using relocations.
                    consumeError(std::move(E));
                    *TargetOS << TargetName;
                    RelFixedUp = false; // Suppress comment for RLD sym name
                  } else if (BranchRelocationType && !RelFixedUp)
                    *TargetOS << Val;
                  else
                    *TargetOS << TargetName;
                  if (Disp)
                    *TargetOS << "+0x" << Twine::utohexstr(Disp);
                } else if (!Disp) {
                  *TargetOS << TargetName;
                } else if (BBAddrMapLabelAvailable) {
                  *TargetOS << BBAddrMapLabels[Target].front().BlockLabel;
                } else if (LabelAvailable) {
                  *TargetOS << AllLabels[Target];
                } else {
                  // Always Print the binary symbol plus an offset if there's no
                  // local label corresponding to the target address.
                  *TargetOS << TargetName << "+0x" << Twine::utohexstr(Disp);
                }
                *TargetOS << ">";
                if (RelFixedUp && !InlineRelocs) {
                  // We have fixup code for a relocation. We print the
                  // referenced symbol as a comment.
                  *TargetOS << "\t# " << Val;
                }

              } else if (BBAddrMapLabelAvailable) {
                *TargetOS << " <" << BBAddrMapLabels[Target].front().BlockLabel
                          << ">";
              } else if (LabelAvailable) {
                *TargetOS << " <" << AllLabels[Target] << ">";
              }
              // By convention, each record in the comment stream should be
              // terminated.
              if (TargetOS == &CommentStream)
                *TargetOS << "\n";
            }

            DT->InstrAnalysis->updateState(Inst, SectionAddr + Index);
          } else if (!Disassembled && DT->InstrAnalysis) {
            DT->InstrAnalysis->resetState();
          }
        }

        assert(DT->Context->getAsmInfo());
        emitPostInstructionInfo(FOS, *DT->Context->getAsmInfo(),
                                *DT->SubtargetInfo, CommentStream.str(), LVP);
        Comments.clear();

        if (BTF)
          printBTFRelocation(FOS, *BTF, {Index, Section.getIndex()}, LVP);

        // Hexagon handles relocs in pretty printer
        if (InlineRelocs && Obj.getArch() != Triple::hexagon) {
          while (findRel()) {
            // When --adjust-vma is used, update the address printed.
            if (RelCur->getSymbol() != Obj.symbol_end()) {
              Expected<section_iterator> SymSI =
                  RelCur->getSymbol()->getSection();
              if (SymSI && *SymSI != Obj.section_end() &&
                  shouldAdjustVA(**SymSI))
                RelOffset += AdjustVMA;
            }

            printRelocation(FOS, Obj.getFileName(), *RelCur,
                            SectionAddr + RelOffset, Is64Bits);
            LVP.printAfterOtherLine(FOS, true);
            ++RelCur;
          }
        }

        Index += Size;
      }
    }
  }
  StringSet<> MissingDisasmSymbolSet =
      set_difference(DisasmSymbolSet, FoundDisasmSymbolSet);
  for (StringRef Sym : MissingDisasmSymbolSet.keys())
    reportWarning("failed to disassemble missing symbol " + Sym, FileName);
}

static void disassembleObject(ObjectFile *Obj, bool InlineRelocs) {
  // If information useful for showing the disassembly is missing, try to find a
  // more complete binary and disassemble that instead.
  OwningBinary<Binary> FetchedBinary;
  if (Obj->symbols().empty()) {
    if (std::optional<OwningBinary<Binary>> FetchedBinaryOpt =
            fetchBinaryByBuildID(*Obj)) {
      if (auto *O = dyn_cast<ObjectFile>(FetchedBinaryOpt->getBinary())) {
        if (!O->symbols().empty() ||
            (!O->sections().empty() && Obj->sections().empty())) {
          FetchedBinary = std::move(*FetchedBinaryOpt);
          Obj = O;
        }
      }
    }
  }

  const Target *TheTarget = getTarget(Obj);

  // Package up features to be passed to target/subtarget
  Expected<SubtargetFeatures> FeaturesValue = Obj->getFeatures();
  if (!FeaturesValue)
    reportError(FeaturesValue.takeError(), Obj->getFileName());
  SubtargetFeatures Features = *FeaturesValue;
  if (!MAttrs.empty()) {
    for (unsigned I = 0; I != MAttrs.size(); ++I)
      Features.AddFeature(MAttrs[I]);
  } else if (MCPU.empty() && Obj->getArch() == llvm::Triple::aarch64) {
    Features.AddFeature("+all");
  }

  if (MCPU.empty())
    MCPU = Obj->tryGetCPUName().value_or("").str();

  if (isArmElf(*Obj)) {
    // When disassembling big-endian Arm ELF, the instruction endianness is
    // determined in a complex way. In relocatable objects, AAELF32 mandates
    // that instruction endianness matches the ELF file endianness; in
    // executable images, that's true unless the file header has the EF_ARM_BE8
    // flag, in which case instructions are little-endian regardless of data
    // endianness.
    //
    // We must set the big-endian-instructions SubtargetFeature to make the
    // disassembler read the instructions the right way round, and also tell
    // our own prettyprinter to retrieve the encodings the same way to print in
    // hex.
    const auto *Elf32BE = dyn_cast<ELF32BEObjectFile>(Obj);

    if (Elf32BE && (Elf32BE->isRelocatableObject() ||
                    !(Elf32BE->getPlatformFlags() & ELF::EF_ARM_BE8))) {
      Features.AddFeature("+big-endian-instructions");
      ARMPrettyPrinterInst.setInstructionEndianness(llvm::endianness::big);
    } else {
      ARMPrettyPrinterInst.setInstructionEndianness(llvm::endianness::little);
    }
  }

  DisassemblerTarget PrimaryTarget(TheTarget, *Obj, TripleName, MCPU, Features);

  // If we have an ARM object file, we need a second disassembler, because
  // ARM CPUs have two different instruction sets: ARM mode, and Thumb mode.
  // We use mapping symbols to switch between the two assemblers, where
  // appropriate.
  std::optional<DisassemblerTarget> SecondaryTarget;

  if (isArmElf(*Obj)) {
    if (!PrimaryTarget.SubtargetInfo->checkFeatures("+mclass")) {
      if (PrimaryTarget.SubtargetInfo->checkFeatures("+thumb-mode"))
        Features.AddFeature("-thumb-mode");
      else
        Features.AddFeature("+thumb-mode");
      SecondaryTarget.emplace(PrimaryTarget, Features);
    }
  } else if (const auto *COFFObj = dyn_cast<COFFObjectFile>(Obj)) {
    const chpe_metadata *CHPEMetadata = COFFObj->getCHPEMetadata();
    if (CHPEMetadata && CHPEMetadata->CodeMapCount) {
      // Set up x86_64 disassembler for ARM64EC binaries.
      Triple X64Triple(TripleName);
      X64Triple.setArch(Triple::ArchType::x86_64);

      std::string Error;
      const Target *X64Target =
          TargetRegistry::lookupTarget("", X64Triple, Error);
      if (X64Target) {
        SubtargetFeatures X64Features;
        SecondaryTarget.emplace(X64Target, *Obj, X64Triple.getTriple(), "",
                                X64Features);
      } else {
        reportWarning(Error, Obj->getFileName());
      }
    }
  }

  const ObjectFile *DbgObj = Obj;
  if (!FetchedBinary.getBinary() && !Obj->hasDebugInfo()) {
    if (std::optional<OwningBinary<Binary>> DebugBinaryOpt =
            fetchBinaryByBuildID(*Obj)) {
      if (auto *FetchedObj =
              dyn_cast<const ObjectFile>(DebugBinaryOpt->getBinary())) {
        if (FetchedObj->hasDebugInfo()) {
          FetchedBinary = std::move(*DebugBinaryOpt);
          DbgObj = FetchedObj;
        }
      }
    }
  }

  std::unique_ptr<object::Binary> DSYMBinary;
  std::unique_ptr<MemoryBuffer> DSYMBuf;
  if (!DbgObj->hasDebugInfo()) {
    if (const MachOObjectFile *MachOOF = dyn_cast<MachOObjectFile>(&*Obj)) {
      DbgObj = objdump::getMachODSymObject(MachOOF, Obj->getFileName(),
                                           DSYMBinary, DSYMBuf);
      if (!DbgObj)
        return;
    }
  }

  SourcePrinter SP(DbgObj, TheTarget->getName());

  for (StringRef Opt : DisassemblerOptions)
    if (!PrimaryTarget.InstPrinter->applyTargetSpecificCLOption(Opt))
      reportError(Obj->getFileName(),
                  "Unrecognized disassembler option: " + Opt);

  disassembleObject(*Obj, *DbgObj, PrimaryTarget, SecondaryTarget, SP,
                    InlineRelocs);
}

void Dumper::printRelocations() {
  StringRef Fmt = O.getBytesInAddress() > 4 ? "%016" PRIx64 : "%08" PRIx64;

  // Build a mapping from relocation target to a vector of relocation
  // sections. Usually, there is an only one relocation section for
  // each relocated section.
  MapVector<SectionRef, std::vector<SectionRef>> SecToRelSec;
  uint64_t Ndx;
  for (const SectionRef &Section : ToolSectionFilter(O, &Ndx)) {
    if (O.isELF() && (ELFSectionRef(Section).getFlags() & ELF::SHF_ALLOC))
      continue;
    if (Section.relocation_begin() == Section.relocation_end())
      continue;
    Expected<section_iterator> SecOrErr = Section.getRelocatedSection();
    if (!SecOrErr)
      reportError(O.getFileName(),
                  "section (" + Twine(Ndx) +
                      "): unable to get a relocation target: " +
                      toString(SecOrErr.takeError()));
    SecToRelSec[**SecOrErr].push_back(Section);
  }

  for (std::pair<SectionRef, std::vector<SectionRef>> &P : SecToRelSec) {
    StringRef SecName = unwrapOrError(P.first.getName(), O.getFileName());
    outs() << "\nRELOCATION RECORDS FOR [" << SecName << "]:\n";
    uint32_t OffsetPadding = (O.getBytesInAddress() > 4 ? 16 : 8);
    uint32_t TypePadding = 24;
    outs() << left_justify("OFFSET", OffsetPadding) << " "
           << left_justify("TYPE", TypePadding) << " "
           << "VALUE\n";

    for (SectionRef Section : P.second) {
      // CREL sections require decoding, each section may have its own specific
      // decode problems.
      if (O.isELF() && ELFSectionRef(Section).getType() == ELF::SHT_CREL) {
        StringRef Err =
            cast<const ELFObjectFileBase>(O).getCrelDecodeProblem(Section);
        if (!Err.empty()) {
          reportUniqueWarning(Err);
          continue;
        }
      }
      for (const RelocationRef &Reloc : Section.relocations()) {
        uint64_t Address = Reloc.getOffset();
        SmallString<32> RelocName;
        SmallString<32> ValueStr;
        if (Address < StartAddress || Address > StopAddress || getHidden(Reloc))
          continue;
        Reloc.getTypeName(RelocName);
        if (Error E =
                getRelocationValueString(Reloc, SymbolDescription, ValueStr))
          reportUniqueWarning(std::move(E));

        outs() << format(Fmt.data(), Address) << " "
               << left_justify(RelocName, TypePadding) << " " << ValueStr
               << "\n";
      }
    }
  }
}

// Returns true if we need to show LMA column when dumping section headers. We
// show it only when the platform is ELF and either we have at least one section
// whose VMA and LMA are different and/or when --show-lma flag is used.
static bool shouldDisplayLMA(const ObjectFile &Obj) {
  if (!Obj.isELF())
    return false;
  for (const SectionRef &S : ToolSectionFilter(Obj))
    if (S.getAddress() != getELFSectionLMA(S))
      return true;
  return ShowLMA;
}

static size_t getMaxSectionNameWidth(const ObjectFile &Obj) {
  // Default column width for names is 13 even if no names are that long.
  size_t MaxWidth = 13;
  for (const SectionRef &Section : ToolSectionFilter(Obj)) {
    StringRef Name = unwrapOrError(Section.getName(), Obj.getFileName());
    MaxWidth = std::max(MaxWidth, Name.size());
  }
  return MaxWidth;
}

void objdump::printSectionHeaders(ObjectFile &Obj) {
  if (Obj.isELF() && Obj.sections().empty())
    createFakeELFSections(Obj);

  size_t NameWidth = getMaxSectionNameWidth(Obj);
  size_t AddressWidth = 2 * Obj.getBytesInAddress();
  bool HasLMAColumn = shouldDisplayLMA(Obj);
  outs() << "\nSections:\n";
  if (HasLMAColumn)
    outs() << "Idx " << left_justify("Name", NameWidth) << " Size     "
           << left_justify("VMA", AddressWidth) << " "
           << left_justify("LMA", AddressWidth) << " Type\n";
  else
    outs() << "Idx " << left_justify("Name", NameWidth) << " Size     "
           << left_justify("VMA", AddressWidth) << " Type\n";

  uint64_t Idx;
  for (const SectionRef &Section : ToolSectionFilter(Obj, &Idx)) {
    StringRef Name = unwrapOrError(Section.getName(), Obj.getFileName());
    uint64_t VMA = Section.getAddress();
    if (shouldAdjustVA(Section))
      VMA += AdjustVMA;

    uint64_t Size = Section.getSize();

    std::string Type = Section.isText() ? "TEXT" : "";
    if (Section.isData())
      Type += Type.empty() ? "DATA" : ", DATA";
    if (Section.isBSS())
      Type += Type.empty() ? "BSS" : ", BSS";
    if (Section.isDebugSection())
      Type += Type.empty() ? "DEBUG" : ", DEBUG";

    if (HasLMAColumn)
      outs() << format("%3" PRIu64 " %-*s %08" PRIx64 " ", Idx, NameWidth,
                       Name.str().c_str(), Size)
             << format_hex_no_prefix(VMA, AddressWidth) << " "
             << format_hex_no_prefix(getELFSectionLMA(Section), AddressWidth)
             << " " << Type << "\n";
    else
      outs() << format("%3" PRIu64 " %-*s %08" PRIx64 " ", Idx, NameWidth,
                       Name.str().c_str(), Size)
             << format_hex_no_prefix(VMA, AddressWidth) << " " << Type << "\n";
  }
}

void objdump::printSectionContents(const ObjectFile *Obj) {
  const MachOObjectFile *MachO = dyn_cast<const MachOObjectFile>(Obj);

  for (const SectionRef &Section : ToolSectionFilter(*Obj)) {
    StringRef Name = unwrapOrError(Section.getName(), Obj->getFileName());
    uint64_t BaseAddr = Section.getAddress();
    uint64_t Size = Section.getSize();
    if (!Size)
      continue;

    outs() << "Contents of section ";
    StringRef SegmentName = getSegmentName(MachO, Section);
    if (!SegmentName.empty())
      outs() << SegmentName << ",";
    outs() << Name << ":\n";
    if (Section.isBSS()) {
      outs() << format("<skipping contents of bss section at [%04" PRIx64
                       ", %04" PRIx64 ")>\n",
                       BaseAddr, BaseAddr + Size);
      continue;
    }

    StringRef Contents = unwrapOrError(Section.getContents(), Obj->getFileName());

    // Dump out the content as hex and printable ascii characters.
    for (std::size_t Addr = 0, End = Contents.size(); Addr < End; Addr += 16) {
      outs() << format(" %04" PRIx64 " ", BaseAddr + Addr);
      // Dump line of hex.
      for (std::size_t I = 0; I < 16; ++I) {
        if (I != 0 && I % 4 == 0)
          outs() << ' ';
        if (Addr + I < End)
          outs() << hexdigit((Contents[Addr + I] >> 4) & 0xF, true)
                 << hexdigit(Contents[Addr + I] & 0xF, true);
        else
          outs() << "  ";
      }
      // Print ascii.
      outs() << "  ";
      for (std::size_t I = 0; I < 16 && Addr + I < End; ++I) {
        if (isPrint(static_cast<unsigned char>(Contents[Addr + I]) & 0xFF))
          outs() << Contents[Addr + I];
        else
          outs() << ".";
      }
      outs() << "\n";
    }
  }
}

void Dumper::printSymbolTable(StringRef ArchiveName, StringRef ArchitectureName,
                              bool DumpDynamic) {
  if (O.isCOFF() && !DumpDynamic) {
    outs() << "\nSYMBOL TABLE:\n";
    printCOFFSymbolTable(cast<const COFFObjectFile>(O));
    return;
  }

  const StringRef FileName = O.getFileName();

  if (!DumpDynamic) {
    outs() << "\nSYMBOL TABLE:\n";
    for (auto I = O.symbol_begin(); I != O.symbol_end(); ++I)
      printSymbol(*I, {}, FileName, ArchiveName, ArchitectureName, DumpDynamic);
    return;
  }

  outs() << "\nDYNAMIC SYMBOL TABLE:\n";
  if (!O.isELF()) {
    reportWarning(
        "this operation is not currently supported for this file format",
        FileName);
    return;
  }

  const ELFObjectFileBase *ELF = cast<const ELFObjectFileBase>(&O);
  auto Symbols = ELF->getDynamicSymbolIterators();
  Expected<std::vector<VersionEntry>> SymbolVersionsOrErr =
      ELF->readDynsymVersions();
  if (!SymbolVersionsOrErr) {
    reportWarning(toString(SymbolVersionsOrErr.takeError()), FileName);
    SymbolVersionsOrErr = std::vector<VersionEntry>();
    (void)!SymbolVersionsOrErr;
  }
  for (auto &Sym : Symbols)
    printSymbol(Sym, *SymbolVersionsOrErr, FileName, ArchiveName,
                ArchitectureName, DumpDynamic);
}

void Dumper::printSymbol(const SymbolRef &Symbol,
                         ArrayRef<VersionEntry> SymbolVersions,
                         StringRef FileName, StringRef ArchiveName,
                         StringRef ArchitectureName, bool DumpDynamic) {
  const MachOObjectFile *MachO = dyn_cast<const MachOObjectFile>(&O);
  Expected<uint64_t> AddrOrErr = Symbol.getAddress();
  if (!AddrOrErr) {
    reportUniqueWarning(AddrOrErr.takeError());
    return;
  }
  uint64_t Address = *AddrOrErr;
  section_iterator SecI = unwrapOrError(Symbol.getSection(), FileName);
  if (SecI != O.section_end() && shouldAdjustVA(*SecI))
    Address += AdjustVMA;
  if ((Address < StartAddress) || (Address > StopAddress))
    return;
  SymbolRef::Type Type =
      unwrapOrError(Symbol.getType(), FileName, ArchiveName, ArchitectureName);
  uint32_t Flags =
      unwrapOrError(Symbol.getFlags(), FileName, ArchiveName, ArchitectureName);

  // Don't ask a Mach-O STAB symbol for its section unless you know that
  // STAB symbol's section field refers to a valid section index. Otherwise
  // the symbol may error trying to load a section that does not exist.
  bool IsSTAB = false;
  if (MachO) {
    DataRefImpl SymDRI = Symbol.getRawDataRefImpl();
    uint8_t NType =
        (MachO->is64Bit() ? MachO->getSymbol64TableEntry(SymDRI).n_type
                          : MachO->getSymbolTableEntry(SymDRI).n_type);
    if (NType & MachO::N_STAB)
      IsSTAB = true;
  }
  section_iterator Section = IsSTAB
                                 ? O.section_end()
                                 : unwrapOrError(Symbol.getSection(), FileName,
                                                 ArchiveName, ArchitectureName);

  StringRef Name;
  if (Type == SymbolRef::ST_Debug && Section != O.section_end()) {
    if (Expected<StringRef> NameOrErr = Section->getName())
      Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

  } else {
    Name = unwrapOrError(Symbol.getName(), FileName, ArchiveName,
                         ArchitectureName);
  }

  bool Global = Flags & SymbolRef::SF_Global;
  bool Weak = Flags & SymbolRef::SF_Weak;
  bool Absolute = Flags & SymbolRef::SF_Absolute;
  bool Common = Flags & SymbolRef::SF_Common;
  bool Hidden = Flags & SymbolRef::SF_Hidden;

  char GlobLoc = ' ';
  if ((Section != O.section_end() || Absolute) && !Weak)
    GlobLoc = Global ? 'g' : 'l';
  char IFunc = ' ';
  if (O.isELF()) {
    if (ELFSymbolRef(Symbol).getELFType() == ELF::STT_GNU_IFUNC)
      IFunc = 'i';
    if (ELFSymbolRef(Symbol).getBinding() == ELF::STB_GNU_UNIQUE)
      GlobLoc = 'u';
  }

  char Debug = ' ';
  if (DumpDynamic)
    Debug = 'D';
  else if (Type == SymbolRef::ST_Debug || Type == SymbolRef::ST_File)
    Debug = 'd';

  char FileFunc = ' ';
  if (Type == SymbolRef::ST_File)
    FileFunc = 'f';
  else if (Type == SymbolRef::ST_Function)
    FileFunc = 'F';
  else if (Type == SymbolRef::ST_Data)
    FileFunc = 'O';

  const char *Fmt = O.getBytesInAddress() > 4 ? "%016" PRIx64 : "%08" PRIx64;

  outs() << format(Fmt, Address) << " "
         << GlobLoc            // Local -> 'l', Global -> 'g', Neither -> ' '
         << (Weak ? 'w' : ' ') // Weak?
         << ' '                // Constructor. Not supported yet.
         << ' '                // Warning. Not supported yet.
         << IFunc              // Indirect reference to another symbol.
         << Debug              // Debugging (d) or dynamic (D) symbol.
         << FileFunc           // Name of function (F), file (f) or object (O).
         << ' ';
  if (Absolute) {
    outs() << "*ABS*";
  } else if (Common) {
    outs() << "*COM*";
  } else if (Section == O.section_end()) {
    if (O.isXCOFF()) {
      XCOFFSymbolRef XCOFFSym = cast<const XCOFFObjectFile>(O).toSymbolRef(
          Symbol.getRawDataRefImpl());
      if (XCOFF::N_DEBUG == XCOFFSym.getSectionNumber())
        outs() << "*DEBUG*";
      else
        outs() << "*UND*";
    } else
      outs() << "*UND*";
  } else {
    StringRef SegmentName = getSegmentName(MachO, *Section);
    if (!SegmentName.empty())
      outs() << SegmentName << ",";
    StringRef SectionName = unwrapOrError(Section->getName(), FileName);
    outs() << SectionName;
    if (O.isXCOFF()) {
      std::optional<SymbolRef> SymRef =
          getXCOFFSymbolContainingSymbolRef(cast<XCOFFObjectFile>(O), Symbol);
      if (SymRef) {

        Expected<StringRef> NameOrErr = SymRef->getName();

        if (NameOrErr) {
          outs() << " (csect:";
          std::string SymName =
              Demangle ? demangle(*NameOrErr) : NameOrErr->str();

          if (SymbolDescription)
            SymName = getXCOFFSymbolDescription(createSymbolInfo(O, *SymRef),
                                                SymName);

          outs() << ' ' << SymName;
          outs() << ") ";
        } else
          reportWarning(toString(NameOrErr.takeError()), FileName);
      }
    }
  }

  if (Common)
    outs() << '\t' << format(Fmt, static_cast<uint64_t>(Symbol.getAlignment()));
  else if (O.isXCOFF())
    outs() << '\t'
           << format(Fmt, cast<XCOFFObjectFile>(O).getSymbolSize(
                              Symbol.getRawDataRefImpl()));
  else if (O.isELF())
    outs() << '\t' << format(Fmt, ELFSymbolRef(Symbol).getSize());
  else if (O.isWasm())
    outs() << '\t'
           << format(Fmt, static_cast<uint64_t>(
                              cast<WasmObjectFile>(O).getSymbolSize(Symbol)));

  if (O.isELF()) {
    if (!SymbolVersions.empty()) {
      const VersionEntry &Ver =
          SymbolVersions[Symbol.getRawDataRefImpl().d.b - 1];
      std::string Str;
      if (!Ver.Name.empty())
        Str = Ver.IsVerDef ? ' ' + Ver.Name : '(' + Ver.Name + ')';
      outs() << ' ' << left_justify(Str, 12);
    }

    uint8_t Other = ELFSymbolRef(Symbol).getOther();
    switch (Other) {
    case ELF::STV_DEFAULT:
      break;
    case ELF::STV_INTERNAL:
      outs() << " .internal";
      break;
    case ELF::STV_HIDDEN:
      outs() << " .hidden";
      break;
    case ELF::STV_PROTECTED:
      outs() << " .protected";
      break;
    default:
      outs() << format(" 0x%02x", Other);
      break;
    }
  } else if (Hidden) {
    outs() << " .hidden";
  }

  std::string SymName = Demangle ? demangle(Name) : Name.str();
  if (O.isXCOFF() && SymbolDescription)
    SymName = getXCOFFSymbolDescription(createSymbolInfo(O, Symbol), SymName);

  outs() << ' ' << SymName << '\n';
}

static void printUnwindInfo(const ObjectFile *O) {
  outs() << "Unwind info:\n\n";

  if (const COFFObjectFile *Coff = dyn_cast<COFFObjectFile>(O))
    printCOFFUnwindInfo(Coff);
  else if (const MachOObjectFile *MachO = dyn_cast<MachOObjectFile>(O))
    printMachOUnwindInfo(MachO);
  else
    // TODO: Extract DWARF dump tool to objdump.
    WithColor::error(errs(), ToolName)
        << "This operation is only currently supported "
           "for COFF and MachO object files.\n";
}

/// Dump the raw contents of the __clangast section so the output can be piped
/// into llvm-bcanalyzer.
static void printRawClangAST(const ObjectFile *Obj) {
  if (outs().is_displayed()) {
    WithColor::error(errs(), ToolName)
        << "The -raw-clang-ast option will dump the raw binary contents of "
           "the clang ast section.\n"
           "Please redirect the output to a file or another program such as "
           "llvm-bcanalyzer.\n";
    return;
  }

  StringRef ClangASTSectionName("__clangast");
  if (Obj->isCOFF()) {
    ClangASTSectionName = "clangast";
  }

  std::optional<object::SectionRef> ClangASTSection;
  for (auto Sec : ToolSectionFilter(*Obj)) {
    StringRef Name;
    if (Expected<StringRef> NameOrErr = Sec.getName())
      Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

    if (Name == ClangASTSectionName) {
      ClangASTSection = Sec;
      break;
    }
  }
  if (!ClangASTSection)
    return;

  StringRef ClangASTContents =
      unwrapOrError(ClangASTSection->getContents(), Obj->getFileName());
  outs().write(ClangASTContents.data(), ClangASTContents.size());
}

static void printFaultMaps(const ObjectFile *Obj) {
  StringRef FaultMapSectionName;

  if (Obj->isELF()) {
    FaultMapSectionName = ".llvm_faultmaps";
  } else if (Obj->isMachO()) {
    FaultMapSectionName = "__llvm_faultmaps";
  } else {
    WithColor::error(errs(), ToolName)
        << "This operation is only currently supported "
           "for ELF and Mach-O executable files.\n";
    return;
  }

  std::optional<object::SectionRef> FaultMapSection;

  for (auto Sec : ToolSectionFilter(*Obj)) {
    StringRef Name;
    if (Expected<StringRef> NameOrErr = Sec.getName())
      Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

    if (Name == FaultMapSectionName) {
      FaultMapSection = Sec;
      break;
    }
  }

  outs() << "FaultMap table:\n";

  if (!FaultMapSection) {
    outs() << "<not found>\n";
    return;
  }

  StringRef FaultMapContents =
      unwrapOrError(FaultMapSection->getContents(), Obj->getFileName());
  FaultMapParser FMP(FaultMapContents.bytes_begin(),
                     FaultMapContents.bytes_end());

  outs() << FMP;
}

void Dumper::printPrivateHeaders() {
  reportError(O.getFileName(), "Invalid/Unsupported object file format");
}

static void printFileHeaders(const ObjectFile *O) {
  if (!O->isELF() && !O->isCOFF() && !O->isXCOFF())
    reportError(O->getFileName(), "Invalid/Unsupported object file format");

  Triple::ArchType AT = O->getArch();
  outs() << "architecture: " << Triple::getArchTypeName(AT) << "\n";
  uint64_t Address = unwrapOrError(O->getStartAddress(), O->getFileName());

  StringRef Fmt = O->getBytesInAddress() > 4 ? "%016" PRIx64 : "%08" PRIx64;
  outs() << "start address: "
         << "0x" << format(Fmt.data(), Address) << "\n";
}

static void printArchiveChild(StringRef Filename, const Archive::Child &C) {
  Expected<sys::fs::perms> ModeOrErr = C.getAccessMode();
  if (!ModeOrErr) {
    WithColor::error(errs(), ToolName) << "ill-formed archive entry.\n";
    consumeError(ModeOrErr.takeError());
    return;
  }
  sys::fs::perms Mode = ModeOrErr.get();
  outs() << ((Mode & sys::fs::owner_read) ? "r" : "-");
  outs() << ((Mode & sys::fs::owner_write) ? "w" : "-");
  outs() << ((Mode & sys::fs::owner_exe) ? "x" : "-");
  outs() << ((Mode & sys::fs::group_read) ? "r" : "-");
  outs() << ((Mode & sys::fs::group_write) ? "w" : "-");
  outs() << ((Mode & sys::fs::group_exe) ? "x" : "-");
  outs() << ((Mode & sys::fs::others_read) ? "r" : "-");
  outs() << ((Mode & sys::fs::others_write) ? "w" : "-");
  outs() << ((Mode & sys::fs::others_exe) ? "x" : "-");

  outs() << " ";

  outs() << format("%d/%d %6" PRId64 " ", unwrapOrError(C.getUID(), Filename),
                   unwrapOrError(C.getGID(), Filename),
                   unwrapOrError(C.getRawSize(), Filename));

  StringRef RawLastModified = C.getRawLastModified();
  unsigned Seconds;
  if (RawLastModified.getAsInteger(10, Seconds))
    outs() << "(date: \"" << RawLastModified
           << "\" contains non-decimal chars) ";
  else {
    // Since ctime(3) returns a 26 character string of the form:
    // "Sun Sep 16 01:03:52 1973\n\0"
    // just print 24 characters.
    time_t t = Seconds;
    outs() << format("%.24s ", ctime(&t));
  }

  StringRef Name = "";
  Expected<StringRef> NameOrErr = C.getName();
  if (!NameOrErr) {
    consumeError(NameOrErr.takeError());
    Name = unwrapOrError(C.getRawName(), Filename);
  } else {
    Name = NameOrErr.get();
  }
  outs() << Name << "\n";
}

// For ELF only now.
static bool shouldWarnForInvalidStartStopAddress(ObjectFile *Obj) {
  if (const auto *Elf = dyn_cast<ELFObjectFileBase>(Obj)) {
    if (Elf->getEType() != ELF::ET_REL)
      return true;
  }
  return false;
}

static void checkForInvalidStartStopAddress(ObjectFile *Obj,
                                            uint64_t Start, uint64_t Stop) {
  if (!shouldWarnForInvalidStartStopAddress(Obj))
    return;

  for (const SectionRef &Section : Obj->sections())
    if (ELFSectionRef(Section).getFlags() & ELF::SHF_ALLOC) {
      uint64_t BaseAddr = Section.getAddress();
      uint64_t Size = Section.getSize();
      if ((Start < BaseAddr + Size) && Stop > BaseAddr)
        return;
    }

  if (!HasStartAddressFlag)
    reportWarning("no section has address less than 0x" +
                      Twine::utohexstr(Stop) + " specified by --stop-address",
                  Obj->getFileName());
  else if (!HasStopAddressFlag)
    reportWarning("no section has address greater than or equal to 0x" +
                      Twine::utohexstr(Start) + " specified by --start-address",
                  Obj->getFileName());
  else
    reportWarning("no section overlaps the range [0x" +
                      Twine::utohexstr(Start) + ",0x" + Twine::utohexstr(Stop) +
                      ") specified by --start-address/--stop-address",
                  Obj->getFileName());
}

static void dumpObject(ObjectFile *O, const Archive *A = nullptr,
                       const Archive::Child *C = nullptr) {
  Expected<std::unique_ptr<Dumper>> DumperOrErr = createDumper(*O);
  if (!DumperOrErr) {
    reportError(DumperOrErr.takeError(), O->getFileName(),
                A ? A->getFileName() : "");
    return;
  }
  Dumper &D = **DumperOrErr;

  // Avoid other output when using a raw option.
  if (!RawClangAST) {
    outs() << '\n';
    if (A)
      outs() << A->getFileName() << "(" << O->getFileName() << ")";
    else
      outs() << O->getFileName();
    outs() << ":\tfile format " << O->getFileFormatName().lower() << "\n";
  }

  if (HasStartAddressFlag || HasStopAddressFlag)
    checkForInvalidStartStopAddress(O, StartAddress, StopAddress);

  // TODO: Change print* free functions to Dumper member functions to utilitize
  // stateful functions like reportUniqueWarning.

  // Note: the order here matches GNU objdump for compatability.
  StringRef ArchiveName = A ? A->getFileName() : "";
  if (ArchiveHeaders && !MachOOpt && C)
    printArchiveChild(ArchiveName, *C);
  if (FileHeaders)
    printFileHeaders(O);
  if (PrivateHeaders || FirstPrivateHeader)
    D.printPrivateHeaders();
  if (SectionHeaders)
    printSectionHeaders(*O);
  if (SymbolTable)
    D.printSymbolTable(ArchiveName);
  if (DynamicSymbolTable)
    D.printSymbolTable(ArchiveName, /*ArchitectureName=*/"",
                       /*DumpDynamic=*/true);
  if (DwarfDumpType != DIDT_Null) {
    std::unique_ptr<DIContext> DICtx = DWARFContext::create(*O);
    // Dump the complete DWARF structure.
    DIDumpOptions DumpOpts;
    DumpOpts.DumpType = DwarfDumpType;
    DICtx->dump(outs(), DumpOpts);
  }
  if (Relocations && !Disassemble)
    D.printRelocations();
  if (DynamicRelocations)
    D.printDynamicRelocations();
  if (SectionContents)
    printSectionContents(O);
  if (Disassemble)
    disassembleObject(O, Relocations);
  if (UnwindInfo)
    printUnwindInfo(O);

  // Mach-O specific options:
  if (ExportsTrie)
    printExportsTrie(O);
  if (Rebase)
    printRebaseTable(O);
  if (Bind)
    printBindTable(O);
  if (LazyBind)
    printLazyBindTable(O);
  if (WeakBind)
    printWeakBindTable(O);

  // Other special sections:
  if (RawClangAST)
    printRawClangAST(O);
  if (FaultMapSection)
    printFaultMaps(O);
  if (Offloading)
    dumpOffloadBinary(*O);
}

static void dumpObject(const COFFImportFile *I, const Archive *A,
                       const Archive::Child *C = nullptr) {
  StringRef ArchiveName = A ? A->getFileName() : "";

  // Avoid other output when using a raw option.
  if (!RawClangAST)
    outs() << '\n'
           << ArchiveName << "(" << I->getFileName() << ")"
           << ":\tfile format COFF-import-file"
           << "\n\n";

  if (ArchiveHeaders && !MachOOpt && C)
    printArchiveChild(ArchiveName, *C);
  if (SymbolTable)
    printCOFFSymbolTable(*I);
}

/// Dump each object file in \a a;
static void dumpArchive(const Archive *A) {
  Error Err = Error::success();
  unsigned I = -1;
  for (auto &C : A->children(Err)) {
    ++I;
    Expected<std::unique_ptr<Binary>> ChildOrErr = C.getAsBinary();
    if (!ChildOrErr) {
      if (auto E = isNotObjectErrorInvalidFileType(ChildOrErr.takeError()))
        reportError(std::move(E), getFileNameForError(C, I), A->getFileName());
      continue;
    }
    if (ObjectFile *O = dyn_cast<ObjectFile>(&*ChildOrErr.get()))
      dumpObject(O, A, &C);
    else if (COFFImportFile *I = dyn_cast<COFFImportFile>(&*ChildOrErr.get()))
      dumpObject(I, A, &C);
    else
      reportError(errorCodeToError(object_error::invalid_file_type),
                  A->getFileName());
  }
  if (Err)
    reportError(std::move(Err), A->getFileName());
}

/// Open file and figure out how to dump it.
static void dumpInput(StringRef file) {
  // If we are using the Mach-O specific object file parser, then let it parse
  // the file and process the command line options.  So the -arch flags can
  // be used to select specific slices, etc.
  if (MachOOpt) {
    parseInputMachO(file);
    return;
  }

  // Attempt to open the binary.
  OwningBinary<Binary> OBinary = unwrapOrError(createBinary(file), file);
  Binary &Binary = *OBinary.getBinary();

  if (Archive *A = dyn_cast<Archive>(&Binary))
    dumpArchive(A);
  else if (ObjectFile *O = dyn_cast<ObjectFile>(&Binary))
    dumpObject(O);
  else if (MachOUniversalBinary *UB = dyn_cast<MachOUniversalBinary>(&Binary))
    parseInputMachO(UB);
  else if (OffloadBinary *OB = dyn_cast<OffloadBinary>(&Binary))
    dumpOffloadSections(*OB);
  else
    reportError(errorCodeToError(object_error::invalid_file_type), file);
}

template <typename T>
static void parseIntArg(const llvm::opt::InputArgList &InputArgs, int ID,
                        T &Value) {
  if (const opt::Arg *A = InputArgs.getLastArg(ID)) {
    StringRef V(A->getValue());
    if (!llvm::to_integer(V, Value, 0)) {
      reportCmdLineError(A->getSpelling() +
                         ": expected a non-negative integer, but got '" + V +
                         "'");
    }
  }
}

static object::BuildID parseBuildIDArg(const opt::Arg *A) {
  StringRef V(A->getValue());
  object::BuildID BID = parseBuildID(V);
  if (BID.empty())
    reportCmdLineError(A->getSpelling() + ": expected a build ID, but got '" +
                       V + "'");
  return BID;
}

void objdump::invalidArgValue(const opt::Arg *A) {
  reportCmdLineError("'" + StringRef(A->getValue()) +
                     "' is not a valid value for '" + A->getSpelling() + "'");
}

static std::vector<std::string>
commaSeparatedValues(const llvm::opt::InputArgList &InputArgs, int ID) {
  std::vector<std::string> Values;
  for (StringRef Value : InputArgs.getAllArgValues(ID)) {
    llvm::SmallVector<StringRef, 2> SplitValues;
    llvm::SplitString(Value, SplitValues, ",");
    for (StringRef SplitValue : SplitValues)
      Values.push_back(SplitValue.str());
  }
  return Values;
}

static void parseOtoolOptions(const llvm::opt::InputArgList &InputArgs) {
  MachOOpt = true;
  FullLeadingAddr = true;
  PrintImmHex = true;

  ArchName = InputArgs.getLastArgValue(OTOOL_arch).str();
  LinkOptHints = InputArgs.hasArg(OTOOL_C);
  if (InputArgs.hasArg(OTOOL_d))
    FilterSections.push_back("__DATA,__data");
  DylibId = InputArgs.hasArg(OTOOL_D);
  UniversalHeaders = InputArgs.hasArg(OTOOL_f);
  DataInCode = InputArgs.hasArg(OTOOL_G);
  FirstPrivateHeader = InputArgs.hasArg(OTOOL_h);
  IndirectSymbols = InputArgs.hasArg(OTOOL_I);
  ShowRawInsn = InputArgs.hasArg(OTOOL_j);
  PrivateHeaders = InputArgs.hasArg(OTOOL_l);
  DylibsUsed = InputArgs.hasArg(OTOOL_L);
  MCPU = InputArgs.getLastArgValue(OTOOL_mcpu_EQ).str();
  ObjcMetaData = InputArgs.hasArg(OTOOL_o);
  DisSymName = InputArgs.getLastArgValue(OTOOL_p).str();
  InfoPlist = InputArgs.hasArg(OTOOL_P);
  Relocations = InputArgs.hasArg(OTOOL_r);
  if (const Arg *A = InputArgs.getLastArg(OTOOL_s)) {
    auto Filter = (A->getValue(0) + StringRef(",") + A->getValue(1)).str();
    FilterSections.push_back(Filter);
  }
  if (InputArgs.hasArg(OTOOL_t))
    FilterSections.push_back("__TEXT,__text");
  Verbose = InputArgs.hasArg(OTOOL_v) || InputArgs.hasArg(OTOOL_V) ||
            InputArgs.hasArg(OTOOL_o);
  SymbolicOperands = InputArgs.hasArg(OTOOL_V);
  if (InputArgs.hasArg(OTOOL_x))
    FilterSections.push_back(",__text");
  LeadingAddr = LeadingHeaders = !InputArgs.hasArg(OTOOL_X);

  ChainedFixups = InputArgs.hasArg(OTOOL_chained_fixups);
  DyldInfo = InputArgs.hasArg(OTOOL_dyld_info);

  InputFilenames = InputArgs.getAllArgValues(OTOOL_INPUT);
  if (InputFilenames.empty())
    reportCmdLineError("no input file");

  for (const Arg *A : InputArgs) {
    const Option &O = A->getOption();
    if (O.getGroup().isValid() && O.getGroup().getID() == OTOOL_grp_obsolete) {
      reportCmdLineWarning(O.getPrefixedName() +
                           " is obsolete and not implemented");
    }
  }
}

static void parseObjdumpOptions(const llvm::opt::InputArgList &InputArgs) {
  parseIntArg(InputArgs, OBJDUMP_adjust_vma_EQ, AdjustVMA);
  AllHeaders = InputArgs.hasArg(OBJDUMP_all_headers);
  ArchName = InputArgs.getLastArgValue(OBJDUMP_arch_name_EQ).str();
  ArchiveHeaders = InputArgs.hasArg(OBJDUMP_archive_headers);
  Demangle = InputArgs.hasArg(OBJDUMP_demangle);
  Disassemble = InputArgs.hasArg(OBJDUMP_disassemble);
  DisassembleAll = InputArgs.hasArg(OBJDUMP_disassemble_all);
  SymbolDescription = InputArgs.hasArg(OBJDUMP_symbol_description);
  TracebackTable = InputArgs.hasArg(OBJDUMP_traceback_table);
  DisassembleSymbols =
      commaSeparatedValues(InputArgs, OBJDUMP_disassemble_symbols_EQ);
  DisassembleZeroes = InputArgs.hasArg(OBJDUMP_disassemble_zeroes);
  if (const opt::Arg *A = InputArgs.getLastArg(OBJDUMP_dwarf_EQ)) {
    DwarfDumpType = StringSwitch<DIDumpType>(A->getValue())
                        .Case("frames", DIDT_DebugFrame)
                        .Default(DIDT_Null);
    if (DwarfDumpType == DIDT_Null)
      invalidArgValue(A);
  }
  DynamicRelocations = InputArgs.hasArg(OBJDUMP_dynamic_reloc);
  FaultMapSection = InputArgs.hasArg(OBJDUMP_fault_map_section);
  Offloading = InputArgs.hasArg(OBJDUMP_offloading);
  FileHeaders = InputArgs.hasArg(OBJDUMP_file_headers);
  SectionContents = InputArgs.hasArg(OBJDUMP_full_contents);
  PrintLines = InputArgs.hasArg(OBJDUMP_line_numbers);
  InputFilenames = InputArgs.getAllArgValues(OBJDUMP_INPUT);
  MachOOpt = InputArgs.hasArg(OBJDUMP_macho);
  MCPU = InputArgs.getLastArgValue(OBJDUMP_mcpu_EQ).str();
  MAttrs = commaSeparatedValues(InputArgs, OBJDUMP_mattr_EQ);
  ShowRawInsn = !InputArgs.hasArg(OBJDUMP_no_show_raw_insn);
  LeadingAddr = !InputArgs.hasArg(OBJDUMP_no_leading_addr);
  RawClangAST = InputArgs.hasArg(OBJDUMP_raw_clang_ast);
  Relocations = InputArgs.hasArg(OBJDUMP_reloc);
  PrintImmHex =
      InputArgs.hasFlag(OBJDUMP_print_imm_hex, OBJDUMP_no_print_imm_hex, true);
  PrivateHeaders = InputArgs.hasArg(OBJDUMP_private_headers);
  FilterSections = InputArgs.getAllArgValues(OBJDUMP_section_EQ);
  SectionHeaders = InputArgs.hasArg(OBJDUMP_section_headers);
  ShowAllSymbols = InputArgs.hasArg(OBJDUMP_show_all_symbols);
  ShowLMA = InputArgs.hasArg(OBJDUMP_show_lma);
  PrintSource = InputArgs.hasArg(OBJDUMP_source);
  parseIntArg(InputArgs, OBJDUMP_start_address_EQ, StartAddress);
  HasStartAddressFlag = InputArgs.hasArg(OBJDUMP_start_address_EQ);
  parseIntArg(InputArgs, OBJDUMP_stop_address_EQ, StopAddress);
  HasStopAddressFlag = InputArgs.hasArg(OBJDUMP_stop_address_EQ);
  SymbolTable = InputArgs.hasArg(OBJDUMP_syms);
  SymbolizeOperands = InputArgs.hasArg(OBJDUMP_symbolize_operands);
  PrettyPGOAnalysisMap = InputArgs.hasArg(OBJDUMP_pretty_pgo_analysis_map);
  if (PrettyPGOAnalysisMap && !SymbolizeOperands)
    reportCmdLineWarning("--symbolize-operands must be enabled for "
                         "--pretty-pgo-analysis-map to have an effect");
  DynamicSymbolTable = InputArgs.hasArg(OBJDUMP_dynamic_syms);
  TripleName = InputArgs.getLastArgValue(OBJDUMP_triple_EQ).str();
  UnwindInfo = InputArgs.hasArg(OBJDUMP_unwind_info);
  Wide = InputArgs.hasArg(OBJDUMP_wide);
  Prefix = InputArgs.getLastArgValue(OBJDUMP_prefix).str();
  parseIntArg(InputArgs, OBJDUMP_prefix_strip, PrefixStrip);
  if (const opt::Arg *A = InputArgs.getLastArg(OBJDUMP_debug_vars_EQ)) {
    DbgVariables = StringSwitch<DebugVarsFormat>(A->getValue())
                       .Case("ascii", DVASCII)
                       .Case("unicode", DVUnicode)
                       .Default(DVInvalid);
    if (DbgVariables == DVInvalid)
      invalidArgValue(A);
  }
  if (const opt::Arg *A = InputArgs.getLastArg(OBJDUMP_disassembler_color_EQ)) {
    DisassemblyColor = StringSwitch<ColorOutput>(A->getValue())
                           .Case("on", ColorOutput::Enable)
                           .Case("off", ColorOutput::Disable)
                           .Case("terminal", ColorOutput::Auto)
                           .Default(ColorOutput::Invalid);
    if (DisassemblyColor == ColorOutput::Invalid)
      invalidArgValue(A);
  }

  parseIntArg(InputArgs, OBJDUMP_debug_vars_indent_EQ, DbgIndent);

  parseMachOOptions(InputArgs);

  // Parse -M (--disassembler-options) and deprecated
  // --x86-asm-syntax={att,intel}.
  //
  // Note, for x86, the asm dialect (AssemblerDialect) is initialized when the
  // MCAsmInfo is constructed. MCInstPrinter::applyTargetSpecificCLOption is
  // called too late. For now we have to use the internal cl::opt option.
  const char *AsmSyntax = nullptr;
  for (const auto *A : InputArgs.filtered(OBJDUMP_disassembler_options_EQ,
                                          OBJDUMP_x86_asm_syntax_att,
                                          OBJDUMP_x86_asm_syntax_intel)) {
    switch (A->getOption().getID()) {
    case OBJDUMP_x86_asm_syntax_att:
      AsmSyntax = "--x86-asm-syntax=att";
      continue;
    case OBJDUMP_x86_asm_syntax_intel:
      AsmSyntax = "--x86-asm-syntax=intel";
      continue;
    }

    SmallVector<StringRef, 2> Values;
    llvm::SplitString(A->getValue(), Values, ",");
    for (StringRef V : Values) {
      if (V == "att")
        AsmSyntax = "--x86-asm-syntax=att";
      else if (V == "intel")
        AsmSyntax = "--x86-asm-syntax=intel";
      else
        DisassemblerOptions.push_back(V.str());
    }
  }
  SmallVector<const char *> Args = {"llvm-objdump"};
  for (const opt::Arg *A : InputArgs.filtered(OBJDUMP_mllvm))
    Args.push_back(A->getValue());
  if (AsmSyntax)
    Args.push_back(AsmSyntax);
  if (Args.size() > 1)
    llvm::cl::ParseCommandLineOptions(Args.size(), Args.data());

  // Look up any provided build IDs, then append them to the input filenames.
  for (const opt::Arg *A : InputArgs.filtered(OBJDUMP_build_id)) {
    object::BuildID BuildID = parseBuildIDArg(A);
    std::optional<std::string> Path = BIDFetcher->fetch(BuildID);
    if (!Path) {
      reportCmdLineError(A->getSpelling() + ": could not find build ID '" +
                         A->getValue() + "'");
    }
    InputFilenames.push_back(std::move(*Path));
  }

  // objdump defaults to a.out if no filenames specified.
  if (InputFilenames.empty())
    InputFilenames.push_back("a.out");
}

int llvm_objdump_main(int argc, char **argv, const llvm::ToolContext &) {
  using namespace llvm;

  ToolName = argv[0];
  std::unique_ptr<CommonOptTable> T;
  OptSpecifier Unknown, HelpFlag, HelpHiddenFlag, VersionFlag;

  StringRef Stem = sys::path::stem(ToolName);
  auto Is = [=](StringRef Tool) {
    // We need to recognize the following filenames:
    //
    // llvm-objdump -> objdump
    // llvm-otool-10.exe -> otool
    // powerpc64-unknown-freebsd13-objdump -> objdump
    auto I = Stem.rfind_insensitive(Tool);
    return I != StringRef::npos &&
           (I + Tool.size() == Stem.size() || !isAlnum(Stem[I + Tool.size()]));
  };
  if (Is("otool")) {
    T = std::make_unique<OtoolOptTable>();
    Unknown = OTOOL_UNKNOWN;
    HelpFlag = OTOOL_help;
    HelpHiddenFlag = OTOOL_help_hidden;
    VersionFlag = OTOOL_version;
  } else {
    T = std::make_unique<ObjdumpOptTable>();
    Unknown = OBJDUMP_UNKNOWN;
    HelpFlag = OBJDUMP_help;
    HelpHiddenFlag = OBJDUMP_help_hidden;
    VersionFlag = OBJDUMP_version;
  }

  BumpPtrAllocator A;
  StringSaver Saver(A);
  opt::InputArgList InputArgs =
      T->parseArgs(argc, argv, Unknown, Saver,
                   [&](StringRef Msg) { reportCmdLineError(Msg); });

  if (InputArgs.size() == 0 || InputArgs.hasArg(HelpFlag)) {
    T->printHelp(ToolName);
    return 0;
  }
  if (InputArgs.hasArg(HelpHiddenFlag)) {
    T->printHelp(ToolName, /*ShowHidden=*/true);
    return 0;
  }

  // Initialize targets and assembly printers/parsers.
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllDisassemblers();

  if (InputArgs.hasArg(VersionFlag)) {
    cl::PrintVersionMessage();
    if (!Is("otool")) {
      outs() << '\n';
      TargetRegistry::printRegisteredTargetsForVersion(outs());
    }
    return 0;
  }

  // Initialize debuginfod.
  const bool ShouldUseDebuginfodByDefault =
      InputArgs.hasArg(OBJDUMP_build_id) || canUseDebuginfod();
  std::vector<std::string> DebugFileDirectories =
      InputArgs.getAllArgValues(OBJDUMP_debug_file_directory);
  if (InputArgs.hasFlag(OBJDUMP_debuginfod, OBJDUMP_no_debuginfod,
                        ShouldUseDebuginfodByDefault)) {
    HTTPClient::initialize();
    BIDFetcher =
        std::make_unique<DebuginfodFetcher>(std::move(DebugFileDirectories));
  } else {
    BIDFetcher =
        std::make_unique<BuildIDFetcher>(std::move(DebugFileDirectories));
  }

  if (Is("otool"))
    parseOtoolOptions(InputArgs);
  else
    parseObjdumpOptions(InputArgs);

  if (StartAddress >= StopAddress)
    reportCmdLineError("start address should be less than stop address");

  // Removes trailing separators from prefix.
  while (!Prefix.empty() && sys::path::is_separator(Prefix.back()))
    Prefix.pop_back();

  if (AllHeaders)
    ArchiveHeaders = FileHeaders = PrivateHeaders = Relocations =
        SectionHeaders = SymbolTable = true;

  if (DisassembleAll || PrintSource || PrintLines || TracebackTable ||
      !DisassembleSymbols.empty())
    Disassemble = true;

  if (!ArchiveHeaders && !Disassemble && DwarfDumpType == DIDT_Null &&
      !DynamicRelocations && !FileHeaders && !PrivateHeaders && !RawClangAST &&
      !Relocations && !SectionHeaders && !SectionContents && !SymbolTable &&
      !DynamicSymbolTable && !UnwindInfo && !FaultMapSection && !Offloading &&
      !(MachOOpt &&
        (Bind || DataInCode || ChainedFixups || DyldInfo || DylibId ||
         DylibsUsed || ExportsTrie || FirstPrivateHeader ||
         FunctionStartsType != FunctionStartsMode::None || IndirectSymbols ||
         InfoPlist || LazyBind || LinkOptHints || ObjcMetaData || Rebase ||
         Rpaths || UniversalHeaders || WeakBind || !FilterSections.empty()))) {
    T->printHelp(ToolName);
    return 2;
  }

  DisasmSymbolSet.insert(DisassembleSymbols.begin(), DisassembleSymbols.end());

  llvm::for_each(InputFilenames, dumpInput);

  warnOnNoMatchForSections();

  return EXIT_SUCCESS;
}
