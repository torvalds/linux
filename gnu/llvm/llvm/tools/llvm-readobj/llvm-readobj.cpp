//===- llvm-readobj.cpp - Dump contents of an Object File -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a tool similar to readelf, except it works on multiple object file
// formats. The main purpose of this tool is to provide detailed output suitable
// for FileCheck.
//
// Flags should be similar to readelf where supported, but the output format
// does not need to be identical. The point is to not make users learn yet
// another set of flags.
//
// Output should be specialized for each format where appropriate.
//
//===----------------------------------------------------------------------===//

#include "llvm-readobj.h"
#include "ObjDumper.h"
#include "WindowsResourceDumper.h"
#include "llvm/DebugInfo/CodeView/GlobalTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Object/XCOFFObjectFile.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace llvm::object;

namespace {
using namespace llvm::opt; // for HelpHidden in Opts.inc
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class ReadobjOptTable : public opt::GenericOptTable {
public:
  ReadobjOptTable() : opt::GenericOptTable(InfoTable) {
    setGroupedShortOptions(true);
  }
};

enum OutputFormatTy { bsd, sysv, posix, darwin, just_symbols };

enum SortSymbolKeyTy {
  NAME = 0,
  TYPE = 1,
  UNKNOWN = 100,
  // TODO: add ADDRESS, SIZE as needed.
};

} // namespace

namespace opts {
static bool Addrsig;
static bool All;
static bool ArchSpecificInfo;
static bool BBAddrMap;
static bool PrettyPGOAnalysisMap;
bool ExpandRelocs;
static bool CGProfile;
static bool Decompress;
bool Demangle;
static bool DependentLibraries;
static bool DynRelocs;
static bool DynamicSymbols;
static bool ExtraSymInfo;
static bool FileHeaders;
static bool Headers;
static std::vector<std::string> HexDump;
static bool PrettyPrint;
static bool PrintStackMap;
static bool PrintStackSizes;
static bool Relocations;
bool SectionData;
static bool SectionDetails;
static bool SectionHeaders;
bool SectionRelocations;
bool SectionSymbols;
static std::vector<std::string> StringDump;
static bool StringTable;
static bool Symbols;
static bool UnwindInfo;
static cl::boolOrDefault SectionMapping;
static SmallVector<SortSymbolKeyTy> SortKeys;

// ELF specific options.
static bool DynamicTable;
static bool ELFLinkerOptions;
static bool GnuHashTable;
static bool HashSymbols;
static bool HashTable;
static bool HashHistogram;
static bool Memtag;
static bool NeededLibraries;
static bool Notes;
static bool ProgramHeaders;
static bool SectionGroups;
static bool VersionInfo;

// Mach-O specific options.
static bool MachODataInCode;
static bool MachODysymtab;
static bool MachOIndirectSymbols;
static bool MachOLinkerOptions;
static bool MachOSegment;
static bool MachOVersionMin;

// PE/COFF specific options.
static bool CodeView;
static bool CodeViewEnableGHash;
static bool CodeViewMergedTypes;
bool CodeViewSubsectionBytes;
static bool COFFBaseRelocs;
static bool COFFDebugDirectory;
static bool COFFDirectives;
static bool COFFExports;
static bool COFFImports;
static bool COFFLoadConfig;
static bool COFFResources;
static bool COFFTLSDirectory;

// XCOFF specific options.
static bool XCOFFAuxiliaryHeader;
static bool XCOFFLoaderSectionHeader;
static bool XCOFFLoaderSectionSymbol;
static bool XCOFFLoaderSectionRelocation;
static bool XCOFFExceptionSection;

OutputStyleTy Output = OutputStyleTy::LLVM;
static std::vector<std::string> InputFilenames;
} // namespace opts

static StringRef ToolName;

namespace llvm {

[[noreturn]] static void error(Twine Msg) {
  // Flush the standard output to print the error at a
  // proper place.
  fouts().flush();
  WithColor::error(errs(), ToolName) << Msg << "\n";
  exit(1);
}

[[noreturn]] void reportError(Error Err, StringRef Input) {
  assert(Err);
  if (Input == "-")
    Input = "<stdin>";
  handleAllErrors(createFileError(Input, std::move(Err)),
                  [&](const ErrorInfoBase &EI) { error(EI.message()); });
  llvm_unreachable("error() call should never return");
}

void reportWarning(Error Err, StringRef Input) {
  assert(Err);
  if (Input == "-")
    Input = "<stdin>";

  // Flush the standard output to print the warning at a
  // proper place.
  fouts().flush();
  handleAllErrors(
      createFileError(Input, std::move(Err)), [&](const ErrorInfoBase &EI) {
        WithColor::warning(errs(), ToolName) << EI.message() << "\n";
      });
}

} // namespace llvm

static void parseOptions(const opt::InputArgList &Args) {
  opts::Addrsig = Args.hasArg(OPT_addrsig);
  opts::All = Args.hasArg(OPT_all);
  opts::ArchSpecificInfo = Args.hasArg(OPT_arch_specific);
  opts::BBAddrMap = Args.hasArg(OPT_bb_addr_map);
  opts::PrettyPGOAnalysisMap = Args.hasArg(OPT_pretty_pgo_analysis_map);
  if (opts::PrettyPGOAnalysisMap && !opts::BBAddrMap)
    WithColor::warning(errs(), ToolName)
        << "--bb-addr-map must be enabled for --pretty-pgo-analysis-map to "
           "have an effect\n";
  opts::CGProfile = Args.hasArg(OPT_cg_profile);
  opts::Decompress = Args.hasArg(OPT_decompress);
  opts::Demangle = Args.hasFlag(OPT_demangle, OPT_no_demangle, false);
  opts::DependentLibraries = Args.hasArg(OPT_dependent_libraries);
  opts::DynRelocs = Args.hasArg(OPT_dyn_relocations);
  opts::DynamicSymbols = Args.hasArg(OPT_dyn_syms);
  opts::ExpandRelocs = Args.hasArg(OPT_expand_relocs);
  opts::ExtraSymInfo = Args.hasArg(OPT_extra_sym_info);
  opts::FileHeaders = Args.hasArg(OPT_file_header);
  opts::Headers = Args.hasArg(OPT_headers);
  opts::HexDump = Args.getAllArgValues(OPT_hex_dump_EQ);
  opts::Relocations = Args.hasArg(OPT_relocs);
  opts::SectionData = Args.hasArg(OPT_section_data);
  opts::SectionDetails = Args.hasArg(OPT_section_details);
  opts::SectionHeaders = Args.hasArg(OPT_section_headers);
  opts::SectionRelocations = Args.hasArg(OPT_section_relocations);
  opts::SectionSymbols = Args.hasArg(OPT_section_symbols);
  if (Args.hasArg(OPT_section_mapping))
    opts::SectionMapping = cl::BOU_TRUE;
  else if (Args.hasArg(OPT_section_mapping_EQ_false))
    opts::SectionMapping = cl::BOU_FALSE;
  else
    opts::SectionMapping = cl::BOU_UNSET;
  opts::PrintStackSizes = Args.hasArg(OPT_stack_sizes);
  opts::PrintStackMap = Args.hasArg(OPT_stackmap);
  opts::StringDump = Args.getAllArgValues(OPT_string_dump_EQ);
  opts::StringTable = Args.hasArg(OPT_string_table);
  opts::Symbols = Args.hasArg(OPT_symbols);
  opts::UnwindInfo = Args.hasArg(OPT_unwind);

  // ELF specific options.
  opts::DynamicTable = Args.hasArg(OPT_dynamic_table);
  opts::ELFLinkerOptions = Args.hasArg(OPT_elf_linker_options);
  if (Arg *A = Args.getLastArg(OPT_elf_output_style_EQ)) {
    std::string OutputStyleChoice = A->getValue();
    opts::Output = StringSwitch<opts::OutputStyleTy>(OutputStyleChoice)
                       .Case("LLVM", opts::OutputStyleTy::LLVM)
                       .Case("GNU", opts::OutputStyleTy::GNU)
                       .Case("JSON", opts::OutputStyleTy::JSON)
                       .Default(opts::OutputStyleTy::UNKNOWN);
    if (opts::Output == opts::OutputStyleTy::UNKNOWN) {
      error("--elf-output-style value should be either 'LLVM', 'GNU', or "
            "'JSON', but was '" +
            OutputStyleChoice + "'");
    }
  }
  opts::GnuHashTable = Args.hasArg(OPT_gnu_hash_table);
  opts::HashSymbols = Args.hasArg(OPT_hash_symbols);
  opts::HashTable = Args.hasArg(OPT_hash_table);
  opts::HashHistogram = Args.hasArg(OPT_histogram);
  opts::Memtag = Args.hasArg(OPT_memtag);
  opts::NeededLibraries = Args.hasArg(OPT_needed_libs);
  opts::Notes = Args.hasArg(OPT_notes);
  opts::PrettyPrint = Args.hasArg(OPT_pretty_print);
  opts::ProgramHeaders = Args.hasArg(OPT_program_headers);
  opts::SectionGroups = Args.hasArg(OPT_section_groups);
  if (Arg *A = Args.getLastArg(OPT_sort_symbols_EQ)) {
    std::string SortKeysString = A->getValue();
    for (StringRef KeyStr : llvm::split(A->getValue(), ",")) {
      SortSymbolKeyTy KeyType = StringSwitch<SortSymbolKeyTy>(KeyStr)
                                    .Case("name", SortSymbolKeyTy::NAME)
                                    .Case("type", SortSymbolKeyTy::TYPE)
                                    .Default(SortSymbolKeyTy::UNKNOWN);
      if (KeyType == SortSymbolKeyTy::UNKNOWN)
        error("--sort-symbols value should be 'name' or 'type', but was '" +
              Twine(KeyStr) + "'");
      opts::SortKeys.push_back(KeyType);
    }
  }
  opts::VersionInfo = Args.hasArg(OPT_version_info);

  // Mach-O specific options.
  opts::MachODataInCode = Args.hasArg(OPT_macho_data_in_code);
  opts::MachODysymtab = Args.hasArg(OPT_macho_dysymtab);
  opts::MachOIndirectSymbols = Args.hasArg(OPT_macho_indirect_symbols);
  opts::MachOLinkerOptions = Args.hasArg(OPT_macho_linker_options);
  opts::MachOSegment = Args.hasArg(OPT_macho_segment);
  opts::MachOVersionMin = Args.hasArg(OPT_macho_version_min);

  // PE/COFF specific options.
  opts::CodeView = Args.hasArg(OPT_codeview);
  opts::CodeViewEnableGHash = Args.hasArg(OPT_codeview_ghash);
  opts::CodeViewMergedTypes = Args.hasArg(OPT_codeview_merged_types);
  opts::CodeViewSubsectionBytes = Args.hasArg(OPT_codeview_subsection_bytes);
  opts::COFFBaseRelocs = Args.hasArg(OPT_coff_basereloc);
  opts::COFFDebugDirectory = Args.hasArg(OPT_coff_debug_directory);
  opts::COFFDirectives = Args.hasArg(OPT_coff_directives);
  opts::COFFExports = Args.hasArg(OPT_coff_exports);
  opts::COFFImports = Args.hasArg(OPT_coff_imports);
  opts::COFFLoadConfig = Args.hasArg(OPT_coff_load_config);
  opts::COFFResources = Args.hasArg(OPT_coff_resources);
  opts::COFFTLSDirectory = Args.hasArg(OPT_coff_tls_directory);

  // XCOFF specific options.
  opts::XCOFFAuxiliaryHeader = Args.hasArg(OPT_auxiliary_header);
  opts::XCOFFLoaderSectionHeader = Args.hasArg(OPT_loader_section_header);
  opts::XCOFFLoaderSectionSymbol = Args.hasArg(OPT_loader_section_symbols);
  opts::XCOFFLoaderSectionRelocation =
      Args.hasArg(OPT_loader_section_relocations);
  opts::XCOFFExceptionSection = Args.hasArg(OPT_exception_section);

  opts::InputFilenames = Args.getAllArgValues(OPT_INPUT);
}

namespace {
struct ReadObjTypeTableBuilder {
  ReadObjTypeTableBuilder()
      : IDTable(Allocator), TypeTable(Allocator), GlobalIDTable(Allocator),
        GlobalTypeTable(Allocator) {}

  llvm::BumpPtrAllocator Allocator;
  llvm::codeview::MergingTypeTableBuilder IDTable;
  llvm::codeview::MergingTypeTableBuilder TypeTable;
  llvm::codeview::GlobalTypeTableBuilder GlobalIDTable;
  llvm::codeview::GlobalTypeTableBuilder GlobalTypeTable;
  std::vector<OwningBinary<Binary>> Binaries;
};
} // namespace
static ReadObjTypeTableBuilder CVTypes;

/// Creates an format-specific object file dumper.
static Expected<std::unique_ptr<ObjDumper>>
createDumper(const ObjectFile &Obj, ScopedPrinter &Writer) {
  if (const COFFObjectFile *COFFObj = dyn_cast<COFFObjectFile>(&Obj))
    return createCOFFDumper(*COFFObj, Writer);

  if (const ELFObjectFileBase *ELFObj = dyn_cast<ELFObjectFileBase>(&Obj))
    return createELFDumper(*ELFObj, Writer);

  if (const MachOObjectFile *MachOObj = dyn_cast<MachOObjectFile>(&Obj))
    return createMachODumper(*MachOObj, Writer);

  if (const WasmObjectFile *WasmObj = dyn_cast<WasmObjectFile>(&Obj))
    return createWasmDumper(*WasmObj, Writer);

  if (const XCOFFObjectFile *XObj = dyn_cast<XCOFFObjectFile>(&Obj))
    return createXCOFFDumper(*XObj, Writer);

  return createStringError(errc::invalid_argument,
                           "unsupported object file format");
}

/// Dumps the specified object file.
static void dumpObject(ObjectFile &Obj, ScopedPrinter &Writer,
                       const Archive *A = nullptr) {
  std::string FileStr =
      A ? Twine(A->getFileName() + "(" + Obj.getFileName() + ")").str()
        : Obj.getFileName().str();

  std::string ContentErrString;
  if (Error ContentErr = Obj.initContent())
    ContentErrString = "unable to continue dumping, the file is corrupt: " +
                       toString(std::move(ContentErr));

  ObjDumper *Dumper;
  std::optional<SymbolComparator> SymComp;
  Expected<std::unique_ptr<ObjDumper>> DumperOrErr = createDumper(Obj, Writer);
  if (!DumperOrErr)
    reportError(DumperOrErr.takeError(), FileStr);
  Dumper = (*DumperOrErr).get();

  if (!opts::SortKeys.empty()) {
    if (Dumper->canCompareSymbols()) {
      SymComp = SymbolComparator();
      for (SortSymbolKeyTy Key : opts::SortKeys) {
        switch (Key) {
        case NAME:
          SymComp->addPredicate([Dumper](SymbolRef LHS, SymbolRef RHS) {
            return Dumper->compareSymbolsByName(LHS, RHS);
          });
          break;
        case TYPE:
          SymComp->addPredicate([Dumper](SymbolRef LHS, SymbolRef RHS) {
            return Dumper->compareSymbolsByType(LHS, RHS);
          });
          break;
        case UNKNOWN:
          llvm_unreachable("Unsupported sort key");
        }
      }

    } else {
      reportWarning(createStringError(
                        errc::invalid_argument,
                        "--sort-symbols is not supported yet for this format"),
                    FileStr);
    }
  }
  Dumper->printFileSummary(FileStr, Obj, opts::InputFilenames, A);

  if (opts::FileHeaders)
    Dumper->printFileHeaders();

  // Auxiliary header in XOCFF is right after the file header, so print the data
  // here.
  if (Obj.isXCOFF() && opts::XCOFFAuxiliaryHeader)
    Dumper->printAuxiliaryHeader();

  // This is only used for ELF currently. In some cases, when an object is
  // corrupt (e.g. truncated), we can't dump anything except the file header.
  if (!ContentErrString.empty())
    reportError(createError(ContentErrString), FileStr);

  if (opts::SectionDetails || opts::SectionHeaders) {
    if (opts::Output == opts::GNU && opts::SectionDetails)
      Dumper->printSectionDetails();
    else
      Dumper->printSectionHeaders();
  }

  if (opts::HashSymbols)
    Dumper->printHashSymbols();
  if (opts::ProgramHeaders || opts::SectionMapping == cl::BOU_TRUE)
    Dumper->printProgramHeaders(opts::ProgramHeaders, opts::SectionMapping);
  if (opts::DynamicTable)
    Dumper->printDynamicTable();
  if (opts::NeededLibraries)
    Dumper->printNeededLibraries();
  if (opts::Relocations)
    Dumper->printRelocations();
  if (opts::DynRelocs)
    Dumper->printDynamicRelocations();
  if (opts::UnwindInfo)
    Dumper->printUnwindInfo();
  if (opts::Symbols || opts::DynamicSymbols)
    Dumper->printSymbols(opts::Symbols, opts::DynamicSymbols,
                         opts::ExtraSymInfo, SymComp);
  if (!opts::StringDump.empty())
    Dumper->printSectionsAsString(Obj, opts::StringDump, opts::Decompress);
  if (!opts::HexDump.empty())
    Dumper->printSectionsAsHex(Obj, opts::HexDump, opts::Decompress);
  if (opts::HashTable)
    Dumper->printHashTable();
  if (opts::GnuHashTable)
    Dumper->printGnuHashTable();
  if (opts::VersionInfo)
    Dumper->printVersionInfo();
  if (opts::StringTable)
    Dumper->printStringTable();
  if (Obj.isELF()) {
    if (opts::DependentLibraries)
      Dumper->printDependentLibs();
    if (opts::ELFLinkerOptions)
      Dumper->printELFLinkerOptions();
    if (opts::ArchSpecificInfo)
      Dumper->printArchSpecificInfo();
    if (opts::SectionGroups)
      Dumper->printGroupSections();
    if (opts::HashHistogram)
      Dumper->printHashHistograms();
    if (opts::CGProfile)
      Dumper->printCGProfile();
    if (opts::BBAddrMap)
      Dumper->printBBAddrMaps(opts::PrettyPGOAnalysisMap);
    if (opts::Addrsig)
      Dumper->printAddrsig();
    if (opts::Notes)
      Dumper->printNotes();
    if (opts::Memtag)
      Dumper->printMemtag();
  }
  if (Obj.isCOFF()) {
    if (opts::COFFImports)
      Dumper->printCOFFImports();
    if (opts::COFFExports)
      Dumper->printCOFFExports();
    if (opts::COFFDirectives)
      Dumper->printCOFFDirectives();
    if (opts::COFFBaseRelocs)
      Dumper->printCOFFBaseReloc();
    if (opts::COFFDebugDirectory)
      Dumper->printCOFFDebugDirectory();
    if (opts::COFFTLSDirectory)
      Dumper->printCOFFTLSDirectory();
    if (opts::COFFResources)
      Dumper->printCOFFResources();
    if (opts::COFFLoadConfig)
      Dumper->printCOFFLoadConfig();
    if (opts::CGProfile)
      Dumper->printCGProfile();
    if (opts::Addrsig)
      Dumper->printAddrsig();
    if (opts::CodeView)
      Dumper->printCodeViewDebugInfo();
    if (opts::CodeViewMergedTypes)
      Dumper->mergeCodeViewTypes(CVTypes.IDTable, CVTypes.TypeTable,
                                 CVTypes.GlobalIDTable, CVTypes.GlobalTypeTable,
                                 opts::CodeViewEnableGHash);
  }
  if (Obj.isMachO()) {
    if (opts::MachODataInCode)
      Dumper->printMachODataInCode();
    if (opts::MachOIndirectSymbols)
      Dumper->printMachOIndirectSymbols();
    if (opts::MachOLinkerOptions)
      Dumper->printMachOLinkerOptions();
    if (opts::MachOSegment)
      Dumper->printMachOSegment();
    if (opts::MachOVersionMin)
      Dumper->printMachOVersionMin();
    if (opts::MachODysymtab)
      Dumper->printMachODysymtab();
    if (opts::CGProfile)
      Dumper->printCGProfile();
  }

  if (Obj.isXCOFF()) {
    if (opts::XCOFFLoaderSectionHeader || opts::XCOFFLoaderSectionSymbol ||
        opts::XCOFFLoaderSectionRelocation)
      Dumper->printLoaderSection(opts::XCOFFLoaderSectionHeader,
                                 opts::XCOFFLoaderSectionSymbol,
                                 opts::XCOFFLoaderSectionRelocation);

    if (opts::XCOFFExceptionSection)
      Dumper->printExceptionSection();
  }

  if (opts::PrintStackMap)
    Dumper->printStackMap();
  if (opts::PrintStackSizes)
    Dumper->printStackSizes();
}

/// Dumps each object file in \a Arc;
static void dumpArchive(const Archive *Arc, ScopedPrinter &Writer) {
  Error Err = Error::success();
  for (auto &Child : Arc->children(Err)) {
    Expected<std::unique_ptr<Binary>> ChildOrErr = Child.getAsBinary();
    if (!ChildOrErr) {
      if (auto E = isNotObjectErrorInvalidFileType(ChildOrErr.takeError()))
        reportError(std::move(E), Arc->getFileName());
      continue;
    }

    Binary *Bin = ChildOrErr->get();
    if (ObjectFile *Obj = dyn_cast<ObjectFile>(Bin))
      dumpObject(*Obj, Writer, Arc);
    else if (COFFImportFile *Imp = dyn_cast<COFFImportFile>(Bin))
      dumpCOFFImportFile(Imp, Writer);
    else
      reportWarning(createStringError(errc::invalid_argument,
                                      Bin->getFileName() +
                                          " has an unsupported file type"),
                    Arc->getFileName());
  }
  if (Err)
    reportError(std::move(Err), Arc->getFileName());
}

/// Dumps each object file in \a MachO Universal Binary;
static void dumpMachOUniversalBinary(const MachOUniversalBinary *UBinary,
                                     ScopedPrinter &Writer) {
  for (const MachOUniversalBinary::ObjectForArch &Obj : UBinary->objects()) {
    Expected<std::unique_ptr<MachOObjectFile>> ObjOrErr = Obj.getAsObjectFile();
    if (ObjOrErr)
      dumpObject(*ObjOrErr.get(), Writer);
    else if (auto E = isNotObjectErrorInvalidFileType(ObjOrErr.takeError()))
      reportError(ObjOrErr.takeError(), UBinary->getFileName());
    else if (Expected<std::unique_ptr<Archive>> AOrErr = Obj.getAsArchive())
      dumpArchive(&*AOrErr.get(), Writer);
  }
}

/// Dumps \a WinRes, Windows Resource (.res) file;
static void dumpWindowsResourceFile(WindowsResource *WinRes,
                                    ScopedPrinter &Printer) {
  WindowsRes::Dumper Dumper(WinRes, Printer);
  if (auto Err = Dumper.printData())
    reportError(std::move(Err), WinRes->getFileName());
}


/// Opens \a File and dumps it.
static void dumpInput(StringRef File, ScopedPrinter &Writer) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(File, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (std::error_code EC = FileOrErr.getError())
    return reportError(errorCodeToError(EC), File);

  std::unique_ptr<MemoryBuffer> &Buffer = FileOrErr.get();
  file_magic Type = identify_magic(Buffer->getBuffer());
  if (Type == file_magic::bitcode) {
    reportWarning(createStringError(errc::invalid_argument,
                                    "bitcode files are not supported"),
                  File);
    return;
  }

  Expected<std::unique_ptr<Binary>> BinaryOrErr = createBinary(
      Buffer->getMemBufferRef(), /*Context=*/nullptr, /*InitContent=*/false);
  if (!BinaryOrErr)
    reportError(BinaryOrErr.takeError(), File);

  std::unique_ptr<Binary> Bin = std::move(*BinaryOrErr);
  if (Archive *Arc = dyn_cast<Archive>(Bin.get()))
    dumpArchive(Arc, Writer);
  else if (MachOUniversalBinary *UBinary =
               dyn_cast<MachOUniversalBinary>(Bin.get()))
    dumpMachOUniversalBinary(UBinary, Writer);
  else if (ObjectFile *Obj = dyn_cast<ObjectFile>(Bin.get()))
    dumpObject(*Obj, Writer);
  else if (COFFImportFile *Import = dyn_cast<COFFImportFile>(Bin.get()))
    dumpCOFFImportFile(Import, Writer);
  else if (WindowsResource *WinRes = dyn_cast<WindowsResource>(Bin.get()))
    dumpWindowsResourceFile(WinRes, Writer);
  else
    llvm_unreachable("unrecognized file type");

  CVTypes.Binaries.push_back(
      OwningBinary<Binary>(std::move(Bin), std::move(Buffer)));
}

std::unique_ptr<ScopedPrinter> createWriter() {
  if (opts::Output == opts::JSON)
    return std::make_unique<JSONScopedPrinter>(
        fouts(), opts::PrettyPrint ? 2 : 0, std::make_unique<ListScope>());
  return std::make_unique<ScopedPrinter>(fouts());
}

int llvm_readobj_main(int argc, char **argv, const llvm::ToolContext &) {
  BumpPtrAllocator A;
  StringSaver Saver(A);
  ReadobjOptTable Tbl;
  ToolName = argv[0];
  opt::InputArgList Args =
      Tbl.parseArgs(argc, argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) {
        error(Msg);
        exit(1);
      });
  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(
        outs(),
        (Twine(ToolName) + " [options] <input object files>").str().c_str(),
        "LLVM Object Reader");
    // TODO Replace this with OptTable API once it adds extrahelp support.
    outs() << "\nPass @FILE as argument to read options from FILE.\n";
    return 0;
  }
  if (Args.hasArg(OPT_version)) {
    cl::PrintVersionMessage();
    return 0;
  }

  if (sys::path::stem(argv[0]).contains("readelf"))
    opts::Output = opts::GNU;
  parseOptions(Args);

  // Default to print error if no filename is specified.
  if (opts::InputFilenames.empty()) {
    error("no input files specified");
  }

  if (opts::All) {
    opts::FileHeaders = true;
    opts::XCOFFAuxiliaryHeader = true;
    opts::ProgramHeaders = true;
    opts::SectionHeaders = true;
    opts::Symbols = true;
    opts::Relocations = true;
    opts::DynamicTable = true;
    opts::Notes = true;
    opts::VersionInfo = true;
    opts::UnwindInfo = true;
    opts::SectionGroups = true;
    opts::HashHistogram = true;
    if (opts::Output == opts::LLVM) {
      opts::Addrsig = true;
      opts::PrintStackSizes = true;
    }
    opts::Memtag = true;
  }

  if (opts::Headers) {
    opts::FileHeaders = true;
    opts::XCOFFAuxiliaryHeader = true;
    opts::ProgramHeaders = true;
    opts::SectionHeaders = true;
  }

  std::unique_ptr<ScopedPrinter> Writer = createWriter();

  for (const std::string &I : opts::InputFilenames)
    dumpInput(I, *Writer);

  if (opts::CodeViewMergedTypes) {
    if (opts::CodeViewEnableGHash)
      dumpCodeViewMergedTypes(*Writer, CVTypes.GlobalIDTable.records(),
                              CVTypes.GlobalTypeTable.records());
    else
      dumpCodeViewMergedTypes(*Writer, CVTypes.IDTable.records(),
                              CVTypes.TypeTable.records());
  }

  return 0;
}
