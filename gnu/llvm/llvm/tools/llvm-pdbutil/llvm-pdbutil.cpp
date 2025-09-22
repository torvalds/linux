//===- llvm-pdbutil.cpp - Dump debug info from a PDB file -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Dumps debug information present in PDB files.
//
//===----------------------------------------------------------------------===//

#include "llvm-pdbutil.h"

#include "BytesOutputStyle.h"
#include "DumpOutputStyle.h"
#include "ExplainOutputStyle.h"
#include "OutputStyle.h"
#include "PrettyClassDefinitionDumper.h"
#include "PrettyCompilandDumper.h"
#include "PrettyEnumDumper.h"
#include "PrettyExternalSymbolDumper.h"
#include "PrettyFunctionDumper.h"
#include "PrettyTypeDumper.h"
#include "PrettyTypedefDumper.h"
#include "PrettyVariableDumper.h"
#include "YAMLOutputStyle.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Config/config.h"
#include "llvm/DebugInfo/CodeView/AppendingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/CodeView/TypeStreamMerger.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBInjectedSource.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h"
#include "llvm/DebugInfo/PDB/Native/DbiStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBFileBuilder.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolPublicSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolThunk.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionArg.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/COM.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

namespace opts {

cl::SubCommand DumpSubcommand("dump", "Dump MSF and CodeView debug info");
cl::SubCommand BytesSubcommand("bytes", "Dump raw bytes from the PDB file");

cl::SubCommand DiaDumpSubcommand("diadump",
                                 "Dump debug information using a DIA-like API");

cl::SubCommand
    PrettySubcommand("pretty",
                     "Dump semantic information about types and symbols");

cl::SubCommand
    YamlToPdbSubcommand("yaml2pdb",
                        "Generate a PDB file from a YAML description");
cl::SubCommand
    PdbToYamlSubcommand("pdb2yaml",
                        "Generate a detailed YAML description of a PDB File");

cl::SubCommand MergeSubcommand("merge",
                               "Merge multiple PDBs into a single PDB");

cl::SubCommand ExplainSubcommand("explain",
                                 "Explain the meaning of a file offset");

cl::SubCommand ExportSubcommand("export",
                                "Write binary data from a stream to a file");

cl::OptionCategory TypeCategory("Symbol Type Options");
cl::OptionCategory FilterCategory("Filtering and Sorting Options");
cl::OptionCategory OtherOptions("Other Options");

cl::ValuesClass ChunkValues = cl::values(
    clEnumValN(ModuleSubsection::CrossScopeExports, "cme",
               "Cross module exports (DEBUG_S_CROSSSCOPEEXPORTS subsection)"),
    clEnumValN(ModuleSubsection::CrossScopeImports, "cmi",
               "Cross module imports (DEBUG_S_CROSSSCOPEIMPORTS subsection)"),
    clEnumValN(ModuleSubsection::FileChecksums, "fc",
               "File checksums (DEBUG_S_CHECKSUMS subsection)"),
    clEnumValN(ModuleSubsection::InlineeLines, "ilines",
               "Inlinee lines (DEBUG_S_INLINEELINES subsection)"),
    clEnumValN(ModuleSubsection::Lines, "lines",
               "Lines (DEBUG_S_LINES subsection)"),
    clEnumValN(ModuleSubsection::StringTable, "strings",
               "String Table (DEBUG_S_STRINGTABLE subsection) (not "
               "typically present in PDB file)"),
    clEnumValN(ModuleSubsection::FrameData, "frames",
               "Frame Data (DEBUG_S_FRAMEDATA subsection)"),
    clEnumValN(ModuleSubsection::Symbols, "symbols",
               "Symbols (DEBUG_S_SYMBOLS subsection) (not typically "
               "present in PDB file)"),
    clEnumValN(ModuleSubsection::CoffSymbolRVAs, "rvas",
               "COFF Symbol RVAs (DEBUG_S_COFF_SYMBOL_RVA subsection)"),
    clEnumValN(ModuleSubsection::Unknown, "unknown",
               "Any subsection not covered by another option"),
    clEnumValN(ModuleSubsection::All, "all", "All known subsections"));

namespace diadump {
cl::list<std::string> InputFilenames(cl::Positional,
                                     cl::desc("<input PDB files>"),
                                     cl::OneOrMore, cl::sub(DiaDumpSubcommand));

cl::opt<bool> Native("native", cl::desc("Use native PDB reader instead of DIA"),
                     cl::sub(DiaDumpSubcommand));

static cl::opt<bool>
    ShowClassHierarchy("hierarchy", cl::desc("Show lexical and class parents"),
                       cl::sub(DiaDumpSubcommand));
static cl::opt<bool> NoSymIndexIds(
    "no-ids",
    cl::desc("Don't show any SymIndexId fields (overrides -hierarchy)"),
    cl::sub(DiaDumpSubcommand));

static cl::opt<bool>
    Recurse("recurse",
            cl::desc("When dumping a SymIndexId, dump the full details of the "
                     "corresponding record"),
            cl::sub(DiaDumpSubcommand));

static cl::opt<bool> Enums("enums", cl::desc("Dump enum types"),
                           cl::sub(DiaDumpSubcommand));
static cl::opt<bool> Pointers("pointers", cl::desc("Dump enum types"),
                              cl::sub(DiaDumpSubcommand));
static cl::opt<bool> UDTs("udts", cl::desc("Dump udt types"),
                          cl::sub(DiaDumpSubcommand));
static cl::opt<bool> Compilands("compilands",
                                cl::desc("Dump compiland information"),
                                cl::sub(DiaDumpSubcommand));
static cl::opt<bool> Funcsigs("funcsigs",
                              cl::desc("Dump function signature information"),
                              cl::sub(DiaDumpSubcommand));
static cl::opt<bool> Arrays("arrays", cl::desc("Dump array types"),
                            cl::sub(DiaDumpSubcommand));
static cl::opt<bool> VTShapes("vtshapes", cl::desc("Dump virtual table shapes"),
                              cl::sub(DiaDumpSubcommand));
static cl::opt<bool> Typedefs("typedefs", cl::desc("Dump typedefs"),
                              cl::sub(DiaDumpSubcommand));
} // namespace diadump

FilterOptions Filters;

namespace pretty {
cl::list<std::string> InputFilenames(cl::Positional,
                                     cl::desc("<input PDB files>"),
                                     cl::OneOrMore, cl::sub(PrettySubcommand));

cl::opt<bool> InjectedSources("injected-sources",
                              cl::desc("Display injected sources"),
                              cl::cat(OtherOptions), cl::sub(PrettySubcommand));
cl::opt<bool> ShowInjectedSourceContent(
    "injected-source-content",
    cl::desc("When displaying an injected source, display the file content"),
    cl::cat(OtherOptions), cl::sub(PrettySubcommand));

cl::list<std::string> WithName(
    "with-name",
    cl::desc("Display any symbol or type with the specified exact name"),
    cl::cat(TypeCategory), cl::sub(PrettySubcommand));

cl::opt<bool> Compilands("compilands", cl::desc("Display compilands"),
                         cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Symbols("module-syms",
                      cl::desc("Display symbols for each compiland"),
                      cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Globals("globals", cl::desc("Dump global symbols"),
                      cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Externals("externals", cl::desc("Dump external symbols"),
                        cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::list<SymLevel> SymTypes(
    "sym-types", cl::desc("Type of symbols to dump (default all)"),
    cl::cat(TypeCategory), cl::sub(PrettySubcommand),
    cl::values(
        clEnumValN(SymLevel::Thunks, "thunks", "Display thunk symbols"),
        clEnumValN(SymLevel::Data, "data", "Display data symbols"),
        clEnumValN(SymLevel::Functions, "funcs", "Display function symbols"),
        clEnumValN(SymLevel::All, "all", "Display all symbols (default)")));

cl::opt<bool>
    Types("types",
          cl::desc("Display all types (implies -classes, -enums, -typedefs)"),
          cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Classes("classes", cl::desc("Display class types"),
                      cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Enums("enums", cl::desc("Display enum types"),
                    cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Typedefs("typedefs", cl::desc("Display typedef types"),
                       cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Funcsigs("funcsigs", cl::desc("Display function signatures"),
                       cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Pointers("pointers", cl::desc("Display pointer types"),
                       cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> Arrays("arrays", cl::desc("Display arrays"),
                     cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<bool> VTShapes("vtshapes", cl::desc("Display vftable shapes"),
                       cl::cat(TypeCategory), cl::sub(PrettySubcommand));

cl::opt<SymbolSortMode> SymbolOrder(
    "symbol-order", cl::desc("symbol sort order"),
    cl::init(SymbolSortMode::None),
    cl::values(clEnumValN(SymbolSortMode::None, "none",
                          "Undefined / no particular sort order"),
               clEnumValN(SymbolSortMode::Name, "name", "Sort symbols by name"),
               clEnumValN(SymbolSortMode::Size, "size",
                          "Sort symbols by size")),
    cl::cat(TypeCategory), cl::sub(PrettySubcommand));

cl::opt<ClassSortMode> ClassOrder(
    "class-order", cl::desc("Class sort order"), cl::init(ClassSortMode::None),
    cl::values(
        clEnumValN(ClassSortMode::None, "none",
                   "Undefined / no particular sort order"),
        clEnumValN(ClassSortMode::Name, "name", "Sort classes by name"),
        clEnumValN(ClassSortMode::Size, "size", "Sort classes by size"),
        clEnumValN(ClassSortMode::Padding, "padding",
                   "Sort classes by amount of padding"),
        clEnumValN(ClassSortMode::PaddingPct, "padding-pct",
                   "Sort classes by percentage of space consumed by padding"),
        clEnumValN(ClassSortMode::PaddingImmediate, "padding-imm",
                   "Sort classes by amount of immediate padding"),
        clEnumValN(ClassSortMode::PaddingPctImmediate, "padding-pct-imm",
                   "Sort classes by percentage of space consumed by immediate "
                   "padding")),
    cl::cat(TypeCategory), cl::sub(PrettySubcommand));

cl::opt<ClassDefinitionFormat> ClassFormat(
    "class-definitions", cl::desc("Class definition format"),
    cl::init(ClassDefinitionFormat::All),
    cl::values(
        clEnumValN(ClassDefinitionFormat::All, "all",
                   "Display all class members including data, constants, "
                   "typedefs, functions, etc"),
        clEnumValN(ClassDefinitionFormat::Layout, "layout",
                   "Only display members that contribute to class size."),
        clEnumValN(ClassDefinitionFormat::None, "none",
                   "Don't display class definitions")),
    cl::cat(TypeCategory), cl::sub(PrettySubcommand));
cl::opt<uint32_t> ClassRecursionDepth(
    "class-recurse-depth", cl::desc("Class recursion depth (0=no limit)"),
    cl::init(0), cl::cat(TypeCategory), cl::sub(PrettySubcommand));

cl::opt<bool> Lines("lines", cl::desc("Line tables"), cl::cat(TypeCategory),
                    cl::sub(PrettySubcommand));
cl::opt<bool>
    All("all", cl::desc("Implies all other options in 'Symbol Types' category"),
        cl::cat(TypeCategory), cl::sub(PrettySubcommand));

cl::opt<uint64_t> LoadAddress(
    "load-address",
    cl::desc("Assume the module is loaded at the specified address"),
    cl::cat(OtherOptions), cl::sub(PrettySubcommand));
cl::opt<bool> Native("native", cl::desc("Use native PDB reader instead of DIA"),
                     cl::cat(OtherOptions), cl::sub(PrettySubcommand));
cl::opt<cl::boolOrDefault>
    ColorOutput("color-output",
                cl::desc("Override use of color (default = isatty)"),
                cl::cat(OtherOptions), cl::sub(PrettySubcommand));
cl::list<std::string>
    ExcludeTypes("exclude-types",
                 cl::desc("Exclude types by regular expression"),
                 cl::cat(FilterCategory), cl::sub(PrettySubcommand));
cl::list<std::string>
    ExcludeSymbols("exclude-symbols",
                   cl::desc("Exclude symbols by regular expression"),
                   cl::cat(FilterCategory), cl::sub(PrettySubcommand));
cl::list<std::string>
    ExcludeCompilands("exclude-compilands",
                      cl::desc("Exclude compilands by regular expression"),
                      cl::cat(FilterCategory), cl::sub(PrettySubcommand));

cl::list<std::string> IncludeTypes(
    "include-types",
    cl::desc("Include only types which match a regular expression"),
    cl::cat(FilterCategory), cl::sub(PrettySubcommand));
cl::list<std::string> IncludeSymbols(
    "include-symbols",
    cl::desc("Include only symbols which match a regular expression"),
    cl::cat(FilterCategory), cl::sub(PrettySubcommand));
cl::list<std::string> IncludeCompilands(
    "include-compilands",
    cl::desc("Include only compilands those which match a regular expression"),
    cl::cat(FilterCategory), cl::sub(PrettySubcommand));
cl::opt<uint32_t> SizeThreshold(
    "min-type-size", cl::desc("Displays only those types which are greater "
                              "than or equal to the specified size."),
    cl::init(0), cl::cat(FilterCategory), cl::sub(PrettySubcommand));
cl::opt<uint32_t> PaddingThreshold(
    "min-class-padding", cl::desc("Displays only those classes which have at "
                                  "least the specified amount of padding."),
    cl::init(0), cl::cat(FilterCategory), cl::sub(PrettySubcommand));
cl::opt<uint32_t> ImmediatePaddingThreshold(
    "min-class-padding-imm",
    cl::desc("Displays only those classes which have at least the specified "
             "amount of immediate padding, ignoring padding internal to bases "
             "and aggregates."),
    cl::init(0), cl::cat(FilterCategory), cl::sub(PrettySubcommand));

cl::opt<bool> ExcludeCompilerGenerated(
    "no-compiler-generated",
    cl::desc("Don't show compiler generated types and symbols"),
    cl::cat(FilterCategory), cl::sub(PrettySubcommand));
cl::opt<bool>
    ExcludeSystemLibraries("no-system-libs",
                           cl::desc("Don't show symbols from system libraries"),
                           cl::cat(FilterCategory), cl::sub(PrettySubcommand));

cl::opt<bool> NoEnumDefs("no-enum-definitions",
                         cl::desc("Don't display full enum definitions"),
                         cl::cat(FilterCategory), cl::sub(PrettySubcommand));
}

cl::OptionCategory FileOptions("Module & File Options");

namespace bytes {
cl::OptionCategory MsfBytes("MSF File Options");
cl::OptionCategory DbiBytes("Dbi Stream Options");
cl::OptionCategory PdbBytes("PDB Stream Options");
cl::OptionCategory Types("Type Options");
cl::OptionCategory ModuleCategory("Module Options");

std::optional<NumberRange> DumpBlockRange;
std::optional<NumberRange> DumpByteRange;

cl::opt<std::string> DumpBlockRangeOpt(
    "block-range", cl::value_desc("start[-end]"),
    cl::desc("Dump binary data from specified range of blocks."),
    cl::sub(BytesSubcommand), cl::cat(MsfBytes));

cl::opt<std::string>
    DumpByteRangeOpt("byte-range", cl::value_desc("start[-end]"),
                     cl::desc("Dump binary data from specified range of bytes"),
                     cl::sub(BytesSubcommand), cl::cat(MsfBytes));

cl::list<std::string>
    DumpStreamData("stream-data", cl::CommaSeparated,
                   cl::desc("Dump binary data from specified streams.  Format "
                            "is SN[:Start][@Size]"),
                   cl::sub(BytesSubcommand), cl::cat(MsfBytes));

cl::opt<bool> NameMap("name-map", cl::desc("Dump bytes of PDB Name Map"),
                      cl::sub(BytesSubcommand), cl::cat(PdbBytes));
cl::opt<bool> Fpm("fpm", cl::desc("Dump free page map"),
                  cl::sub(BytesSubcommand), cl::cat(MsfBytes));

cl::opt<bool> SectionContributions("sc", cl::desc("Dump section contributions"),
                                   cl::sub(BytesSubcommand), cl::cat(DbiBytes));
cl::opt<bool> SectionMap("sm", cl::desc("Dump section map"),
                         cl::sub(BytesSubcommand), cl::cat(DbiBytes));
cl::opt<bool> ModuleInfos("modi", cl::desc("Dump module info"),
                          cl::sub(BytesSubcommand), cl::cat(DbiBytes));
cl::opt<bool> FileInfo("files", cl::desc("Dump source file info"),
                       cl::sub(BytesSubcommand), cl::cat(DbiBytes));
cl::opt<bool> TypeServerMap("type-server", cl::desc("Dump type server map"),
                            cl::sub(BytesSubcommand), cl::cat(DbiBytes));
cl::opt<bool> ECData("ec", cl::desc("Dump edit and continue map"),
                     cl::sub(BytesSubcommand), cl::cat(DbiBytes));

cl::list<uint32_t> TypeIndex(
    "type", cl::desc("Dump the type record with the given type index"),
    cl::CommaSeparated, cl::sub(BytesSubcommand), cl::cat(TypeCategory));
cl::list<uint32_t>
    IdIndex("id", cl::desc("Dump the id record with the given type index"),
            cl::CommaSeparated, cl::sub(BytesSubcommand),
            cl::cat(TypeCategory));

cl::opt<uint32_t> ModuleIndex(
    "mod",
    cl::desc(
        "Limit options in the Modules category to the specified module index"),
    cl::Optional, cl::sub(BytesSubcommand), cl::cat(ModuleCategory));
cl::opt<bool> ModuleSyms("syms", cl::desc("Dump symbol record substream"),
                         cl::sub(BytesSubcommand), cl::cat(ModuleCategory));
cl::opt<bool> ModuleC11("c11-chunks", cl::Hidden,
                        cl::desc("Dump C11 CodeView debug chunks"),
                        cl::sub(BytesSubcommand), cl::cat(ModuleCategory));
cl::opt<bool> ModuleC13("chunks",
                        cl::desc("Dump C13 CodeView debug chunk subsection"),
                        cl::sub(BytesSubcommand), cl::cat(ModuleCategory));
cl::opt<bool> SplitChunks(
    "split-chunks",
    cl::desc(
        "When dumping debug chunks, show a different section for each chunk"),
    cl::sub(BytesSubcommand), cl::cat(ModuleCategory));
cl::list<std::string> InputFilenames(cl::Positional,
                                     cl::desc("<input PDB files>"),
                                     cl::OneOrMore, cl::sub(BytesSubcommand));

} // namespace bytes

namespace dump {

cl::OptionCategory MsfOptions("MSF Container Options");
cl::OptionCategory TypeOptions("Type Record Options");
cl::OptionCategory SymbolOptions("Symbol Options");
cl::OptionCategory MiscOptions("Miscellaneous Options");

// MSF OPTIONS
cl::opt<bool> DumpSummary("summary", cl::desc("dump file summary"),
                          cl::cat(MsfOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpStreams("streams",
                          cl::desc("dump summary of the PDB streams"),
                          cl::cat(MsfOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpStreamBlocks(
    "stream-blocks",
    cl::desc("Add block information to the output of -streams"),
    cl::cat(MsfOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpSymbolStats(
    "sym-stats",
    cl::desc("Dump a detailed breakdown of symbol usage/size for each module"),
    cl::cat(MsfOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpTypeStats(
    "type-stats",
    cl::desc("Dump a detailed breakdown of type usage/size"),
    cl::cat(MsfOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpIDStats(
    "id-stats",
    cl::desc("Dump a detailed breakdown of IPI types usage/size"),
    cl::cat(MsfOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpUdtStats(
    "udt-stats",
    cl::desc("Dump a detailed breakdown of S_UDT record usage / stats"),
    cl::cat(MsfOptions), cl::sub(DumpSubcommand));

// TYPE OPTIONS
cl::opt<bool> DumpTypes("types",
                        cl::desc("dump CodeView type records from TPI stream"),
                        cl::cat(TypeOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpTypeData(
    "type-data",
    cl::desc("dump CodeView type record raw bytes from TPI stream"),
    cl::cat(TypeOptions), cl::sub(DumpSubcommand));
cl::opt<bool>
    DumpTypeRefStats("type-ref-stats",
                     cl::desc("dump statistics on the number and size of types "
                              "transitively referenced by symbol records"),
                     cl::cat(TypeOptions), cl::sub(DumpSubcommand));

cl::opt<bool> DumpTypeExtras("type-extras",
                             cl::desc("dump type hashes and index offsets"),
                             cl::cat(TypeOptions), cl::sub(DumpSubcommand));

cl::opt<bool> DontResolveForwardRefs(
    "dont-resolve-forward-refs",
    cl::desc("When dumping type records for classes, unions, enums, and "
             "structs, don't try to resolve forward references"),
    cl::cat(TypeOptions), cl::sub(DumpSubcommand));

cl::list<uint32_t> DumpTypeIndex(
    "type-index", cl::CommaSeparated,
    cl::desc("only dump types with the specified hexadecimal type index"),
    cl::cat(TypeOptions), cl::sub(DumpSubcommand));

cl::opt<bool> DumpIds("ids",
                      cl::desc("dump CodeView type records from IPI stream"),
                      cl::cat(TypeOptions), cl::sub(DumpSubcommand));
cl::opt<bool>
    DumpIdData("id-data",
               cl::desc("dump CodeView type record raw bytes from IPI stream"),
               cl::cat(TypeOptions), cl::sub(DumpSubcommand));

cl::opt<bool> DumpIdExtras("id-extras",
                           cl::desc("dump id hashes and index offsets"),
                           cl::cat(TypeOptions), cl::sub(DumpSubcommand));
cl::list<uint32_t> DumpIdIndex(
    "id-index", cl::CommaSeparated,
    cl::desc("only dump ids with the specified hexadecimal type index"),
    cl::cat(TypeOptions), cl::sub(DumpSubcommand));

cl::opt<bool> DumpTypeDependents(
    "dependents",
    cl::desc("In conjunection with -type-index and -id-index, dumps the entire "
             "dependency graph for the specified index instead of "
             "just the single record with the specified index"),
    cl::cat(TypeOptions), cl::sub(DumpSubcommand));

// SYMBOL OPTIONS
cl::opt<bool> DumpGlobals("globals", cl::desc("dump Globals symbol records"),
                          cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpGlobalExtras("global-extras", cl::desc("dump Globals hashes"),
                               cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::list<std::string> DumpGlobalNames(
    "global-name",
    cl::desc(
        "With -globals, only dump globals whose name matches the given value"),
    cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpPublics("publics", cl::desc("dump Publics stream data"),
                          cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpPublicExtras("public-extras",
                               cl::desc("dump Publics hashes and address maps"),
                               cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<bool>
    DumpGSIRecords("gsi-records",
                   cl::desc("dump public / global common record stream"),
                   cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpSymbols("symbols", cl::desc("dump module symbols"),
                          cl::cat(SymbolOptions), cl::sub(DumpSubcommand));

cl::opt<bool>
    DumpSymRecordBytes("sym-data",
                       cl::desc("dump CodeView symbol record raw bytes"),
                       cl::cat(SymbolOptions), cl::sub(DumpSubcommand));

cl::opt<bool> DumpFpo("fpo", cl::desc("dump FPO records"),
                      cl::cat(SymbolOptions), cl::sub(DumpSubcommand));

cl::opt<uint32_t> DumpSymbolOffset(
    "symbol-offset", cl::Optional,
    cl::desc("only dump symbol record with the specified symbol offset"),
    cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpParents("show-parents",
                          cl::desc("dump the symbols record's all parents."),
                          cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<uint32_t>
    DumpParentDepth("parent-recurse-depth", cl::Optional, cl::init(-1U),
                    cl::desc("only recurse to a depth of N when displaying "
                             "parents of a symbol record."),
                    cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpChildren("show-children",
                           cl::desc("dump the symbols record's all children."),
                           cl::cat(SymbolOptions), cl::sub(DumpSubcommand));
cl::opt<uint32_t>
    DumpChildrenDepth("children-recurse-depth", cl::Optional, cl::init(-1U),
                      cl::desc("only recurse to a depth of N when displaying "
                               "children of a symbol record."),
                      cl::cat(SymbolOptions), cl::sub(DumpSubcommand));

// MODULE & FILE OPTIONS
cl::opt<bool> DumpModules("modules", cl::desc("dump compiland information"),
                          cl::cat(FileOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpModuleFiles(
    "files",
    cl::desc("Dump the source files that contribute to each module's."),
    cl::cat(FileOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpLines(
    "l",
    cl::desc("dump source file/line information (DEBUG_S_LINES subsection)"),
    cl::cat(FileOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpInlineeLines(
    "il",
    cl::desc("dump inlinee line information (DEBUG_S_INLINEELINES subsection)"),
    cl::cat(FileOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpXmi(
    "xmi",
    cl::desc(
        "dump cross module imports (DEBUG_S_CROSSSCOPEIMPORTS subsection)"),
    cl::cat(FileOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpXme(
    "xme",
    cl::desc(
        "dump cross module exports (DEBUG_S_CROSSSCOPEEXPORTS subsection)"),
    cl::cat(FileOptions), cl::sub(DumpSubcommand));
cl::opt<uint32_t> DumpModi("modi", cl::Optional,
                           cl::desc("For all options that iterate over "
                                    "modules, limit to the specified module"),
                           cl::cat(FileOptions), cl::sub(DumpSubcommand));
cl::opt<bool> JustMyCode("jmc", cl::Optional,
                         cl::desc("For all options that iterate over modules, "
                                  "ignore modules from system libraries"),
                         cl::cat(FileOptions), cl::sub(DumpSubcommand));

// MISCELLANEOUS OPTIONS
cl::opt<bool> DumpNamedStreams("named-streams",
                               cl::desc("dump PDB named stream table"),
                               cl::cat(MiscOptions), cl::sub(DumpSubcommand));

cl::opt<bool> DumpStringTable("string-table", cl::desc("dump PDB String Table"),
                              cl::cat(MiscOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpStringTableDetails("string-table-details",
                                     cl::desc("dump PDB String Table Details"),
                                     cl::cat(MiscOptions),
                                     cl::sub(DumpSubcommand));

cl::opt<bool> DumpSectionContribs("section-contribs",
                                  cl::desc("dump section contributions"),
                                  cl::cat(MiscOptions),
                                  cl::sub(DumpSubcommand));
cl::opt<bool> DumpSectionMap("section-map", cl::desc("dump section map"),
                             cl::cat(MiscOptions), cl::sub(DumpSubcommand));
cl::opt<bool> DumpSectionHeaders("section-headers",
                                 cl::desc("Dump image section headers"),
                                 cl::cat(MiscOptions), cl::sub(DumpSubcommand));

cl::opt<bool> RawAll("all", cl::desc("Implies most other options."),
                     cl::cat(MiscOptions), cl::sub(DumpSubcommand));

cl::list<std::string> InputFilenames(cl::Positional,
                                     cl::desc("<input PDB files>"),
                                     cl::OneOrMore, cl::sub(DumpSubcommand));
}

namespace yaml2pdb {
cl::opt<std::string>
    YamlPdbOutputFile("pdb", cl::desc("the name of the PDB file to write"),
                      cl::sub(YamlToPdbSubcommand));

cl::opt<std::string> InputFilename(cl::Positional,
                                   cl::desc("<input YAML file>"), cl::Required,
                                   cl::sub(YamlToPdbSubcommand));
}

namespace pdb2yaml {
cl::opt<bool> All("all",
                  cl::desc("Dump everything we know how to dump."),
                  cl::sub(PdbToYamlSubcommand), cl::init(false));
cl::opt<bool> NoFileHeaders("no-file-headers",
                            cl::desc("Do not dump MSF file headers"),
                            cl::sub(PdbToYamlSubcommand), cl::init(false));
cl::opt<bool> Minimal("minimal",
                      cl::desc("Don't write fields with default values"),
                      cl::sub(PdbToYamlSubcommand), cl::init(false));

cl::opt<bool> StreamMetadata(
    "stream-metadata",
    cl::desc("Dump the number of streams and each stream's size"),
    cl::sub(PdbToYamlSubcommand), cl::init(false));
cl::opt<bool> StreamDirectory(
    "stream-directory",
    cl::desc("Dump each stream's block map (implies -stream-metadata)"),
    cl::sub(PdbToYamlSubcommand), cl::init(false));
cl::opt<bool> PdbStream("pdb-stream",
                        cl::desc("Dump the PDB Stream (Stream 1)"),
                        cl::sub(PdbToYamlSubcommand), cl::init(false));

cl::opt<bool> StringTable("string-table", cl::desc("Dump the PDB String Table"),
                          cl::sub(PdbToYamlSubcommand), cl::init(false));

cl::opt<bool> DbiStream("dbi-stream",
                        cl::desc("Dump the DBI Stream Headers (Stream 2)"),
                        cl::sub(PdbToYamlSubcommand), cl::init(false));

cl::opt<bool> TpiStream("tpi-stream",
                        cl::desc("Dump the TPI Stream (Stream 3)"),
                        cl::sub(PdbToYamlSubcommand), cl::init(false));

cl::opt<bool> IpiStream("ipi-stream",
                        cl::desc("Dump the IPI Stream (Stream 5)"),
                        cl::sub(PdbToYamlSubcommand), cl::init(false));

cl::opt<bool> PublicsStream("publics-stream",
                            cl::desc("Dump the Publics Stream"),
                            cl::sub(PdbToYamlSubcommand), cl::init(false));

// MODULE & FILE OPTIONS
cl::opt<bool> DumpModules("modules", cl::desc("dump compiland information"),
                          cl::cat(FileOptions), cl::sub(PdbToYamlSubcommand));
cl::opt<bool> DumpModuleFiles("module-files", cl::desc("dump file information"),
                              cl::cat(FileOptions),
                              cl::sub(PdbToYamlSubcommand));
cl::list<ModuleSubsection> DumpModuleSubsections(
    "subsections", cl::CommaSeparated,
    cl::desc("dump subsections from each module's debug stream"), ChunkValues,
    cl::cat(FileOptions), cl::sub(PdbToYamlSubcommand));
cl::opt<bool> DumpModuleSyms("module-syms", cl::desc("dump module symbols"),
                             cl::cat(FileOptions),
                             cl::sub(PdbToYamlSubcommand));

cl::list<std::string> InputFilename(cl::Positional,
                                    cl::desc("<input PDB file>"), cl::Required,
                                    cl::sub(PdbToYamlSubcommand));
} // namespace pdb2yaml

namespace merge {
cl::list<std::string> InputFilenames(cl::Positional,
                                     cl::desc("<input PDB files>"),
                                     cl::OneOrMore, cl::sub(MergeSubcommand));
cl::opt<std::string>
    PdbOutputFile("pdb", cl::desc("the name of the PDB file to write"),
                  cl::sub(MergeSubcommand));
}

namespace explain {
cl::list<std::string> InputFilename(cl::Positional,
                                    cl::desc("<input PDB file>"), cl::Required,
                                    cl::sub(ExplainSubcommand));

cl::list<uint64_t> Offsets("offset", cl::desc("The file offset to explain"),
                           cl::sub(ExplainSubcommand), cl::OneOrMore);

cl::opt<InputFileType> InputType(
    "input-type", cl::desc("Specify how to interpret the input file"),
    cl::init(InputFileType::PDBFile), cl::Optional, cl::sub(ExplainSubcommand),
    cl::values(clEnumValN(InputFileType::PDBFile, "pdb-file",
                          "Treat input as a PDB file (default)"),
               clEnumValN(InputFileType::PDBStream, "pdb-stream",
                          "Treat input as raw contents of PDB stream"),
               clEnumValN(InputFileType::DBIStream, "dbi-stream",
                          "Treat input as raw contents of DBI stream"),
               clEnumValN(InputFileType::Names, "names-stream",
                          "Treat input as raw contents of /names named stream"),
               clEnumValN(InputFileType::ModuleStream, "mod-stream",
                          "Treat input as raw contents of a module stream")));
} // namespace explain

namespace exportstream {
cl::list<std::string> InputFilename(cl::Positional,
                                    cl::desc("<input PDB file>"), cl::Required,
                                    cl::sub(ExportSubcommand));
cl::opt<std::string> OutputFile("out",
                                cl::desc("The file to write the stream to"),
                                cl::Required, cl::sub(ExportSubcommand));
cl::opt<std::string>
    Stream("stream", cl::Required,
           cl::desc("The index or name of the stream whose contents to export"),
           cl::sub(ExportSubcommand));
cl::opt<bool> ForceName("name",
                        cl::desc("Force the interpretation of -stream as a "
                                 "string, even if it is a valid integer"),
                        cl::sub(ExportSubcommand), cl::Optional,
                        cl::init(false));
} // namespace exportstream
}

static ExitOnError ExitOnErr;

static void yamlToPdb(StringRef Path) {
  BumpPtrAllocator Allocator;
  ErrorOr<std::unique_ptr<MemoryBuffer>> ErrorOrBuffer =
      MemoryBuffer::getFileOrSTDIN(Path, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);

  if (ErrorOrBuffer.getError()) {
    ExitOnErr(createFileError(Path, errorCodeToError(ErrorOrBuffer.getError())));
  }

  std::unique_ptr<MemoryBuffer> &Buffer = ErrorOrBuffer.get();

  llvm::yaml::Input In(Buffer->getBuffer());
  pdb::yaml::PdbObject YamlObj(Allocator);
  In >> YamlObj;

  PDBFileBuilder Builder(Allocator);

  uint32_t BlockSize = 4096;
  if (YamlObj.Headers)
    BlockSize = YamlObj.Headers->SuperBlock.BlockSize;
  ExitOnErr(Builder.initialize(BlockSize));
  // Add each of the reserved streams.  We ignore stream metadata in the
  // yaml, because we will reconstruct our own view of the streams.  For
  // example, the YAML may say that there were 20 streams in the original
  // PDB, but maybe we only dump a subset of those 20 streams, so we will
  // have fewer, and the ones we do have may end up with different indices
  // than the ones in the original PDB.  So we just start with a clean slate.
  for (uint32_t I = 0; I < kSpecialStreamCount; ++I)
    ExitOnErr(Builder.getMsfBuilder().addStream(0));

  StringsAndChecksums Strings;
  Strings.setStrings(std::make_shared<DebugStringTableSubsection>());

  if (YamlObj.StringTable) {
    for (auto S : *YamlObj.StringTable)
      Strings.strings()->insert(S);
  }

  pdb::yaml::PdbInfoStream DefaultInfoStream;
  pdb::yaml::PdbDbiStream DefaultDbiStream;
  pdb::yaml::PdbTpiStream DefaultTpiStream;
  pdb::yaml::PdbTpiStream DefaultIpiStream;

  const auto &Info = YamlObj.PdbStream.value_or(DefaultInfoStream);

  auto &InfoBuilder = Builder.getInfoBuilder();
  InfoBuilder.setAge(Info.Age);
  InfoBuilder.setGuid(Info.Guid);
  InfoBuilder.setSignature(Info.Signature);
  InfoBuilder.setVersion(Info.Version);
  for (auto F : Info.Features)
    InfoBuilder.addFeature(F);

  const auto &Dbi = YamlObj.DbiStream.value_or(DefaultDbiStream);
  auto &DbiBuilder = Builder.getDbiBuilder();
  DbiBuilder.setAge(Dbi.Age);
  DbiBuilder.setBuildNumber(Dbi.BuildNumber);
  DbiBuilder.setFlags(Dbi.Flags);
  DbiBuilder.setMachineType(Dbi.MachineType);
  DbiBuilder.setPdbDllRbld(Dbi.PdbDllRbld);
  DbiBuilder.setPdbDllVersion(Dbi.PdbDllVersion);
  DbiBuilder.setVersionHeader(Dbi.VerHeader);
  for (const auto &MI : Dbi.ModInfos) {
    auto &ModiBuilder = ExitOnErr(DbiBuilder.addModuleInfo(MI.Mod));
    ModiBuilder.setObjFileName(MI.Obj);

    for (auto S : MI.SourceFiles)
      ExitOnErr(DbiBuilder.addModuleSourceFile(ModiBuilder, S));
    if (MI.Modi) {
      const auto &ModiStream = *MI.Modi;
      for (const auto &Symbol : ModiStream.Symbols) {
        ModiBuilder.addSymbol(
            Symbol.toCodeViewSymbol(Allocator, CodeViewContainer::Pdb));
      }
    }

    // Each module has its own checksum subsection, so scan for it every time.
    Strings.setChecksums(nullptr);
    CodeViewYAML::initializeStringsAndChecksums(MI.Subsections, Strings);

    auto CodeViewSubsections = ExitOnErr(CodeViewYAML::toCodeViewSubsectionList(
        Allocator, MI.Subsections, Strings));
    for (auto &SS : CodeViewSubsections) {
      ModiBuilder.addDebugSubsection(SS);
    }
  }

  auto &TpiBuilder = Builder.getTpiBuilder();
  const auto &Tpi = YamlObj.TpiStream.value_or(DefaultTpiStream);
  TpiBuilder.setVersionHeader(Tpi.Version);
  AppendingTypeTableBuilder TS(Allocator);
  for (const auto &R : Tpi.Records) {
    CVType Type = R.toCodeViewRecord(TS);
    TpiBuilder.addTypeRecord(Type.RecordData, std::nullopt);
  }

  const auto &Ipi = YamlObj.IpiStream.value_or(DefaultIpiStream);
  auto &IpiBuilder = Builder.getIpiBuilder();
  IpiBuilder.setVersionHeader(Ipi.Version);
  for (const auto &R : Ipi.Records) {
    CVType Type = R.toCodeViewRecord(TS);
    IpiBuilder.addTypeRecord(Type.RecordData, std::nullopt);
  }

  Builder.getStringTableBuilder().setStrings(*Strings.strings());

  codeview::GUID IgnoredOutGuid;
  ExitOnErr(Builder.commit(opts::yaml2pdb::YamlPdbOutputFile, &IgnoredOutGuid));
}

static PDBFile &loadPDB(StringRef Path, std::unique_ptr<IPDBSession> &Session) {
  ExitOnErr(loadDataForPDB(PDB_ReaderType::Native, Path, Session));

  NativeSession *NS = static_cast<NativeSession *>(Session.get());
  return NS->getPDBFile();
}

static void pdb2Yaml(StringRef Path) {
  std::unique_ptr<IPDBSession> Session;
  auto &File = loadPDB(Path, Session);

  auto O = std::make_unique<YAMLOutputStyle>(File);

  ExitOnErr(O->dump());
}

static void dumpRaw(StringRef Path) {
  InputFile IF = ExitOnErr(InputFile::open(Path));

  auto O = std::make_unique<DumpOutputStyle>(IF);
  ExitOnErr(O->dump());
}

static void dumpBytes(StringRef Path) {
  std::unique_ptr<IPDBSession> Session;
  auto &File = loadPDB(Path, Session);

  auto O = std::make_unique<BytesOutputStyle>(File);

  ExitOnErr(O->dump());
}

bool opts::pretty::shouldDumpSymLevel(SymLevel Search) {
  if (SymTypes.empty())
    return true;
  if (llvm::is_contained(SymTypes, Search))
    return true;
  if (llvm::is_contained(SymTypes, SymLevel::All))
    return true;
  return false;
}

uint32_t llvm::pdb::getTypeLength(const PDBSymbolData &Symbol) {
  auto SymbolType = Symbol.getType();
  const IPDBRawSymbol &RawType = SymbolType->getRawSymbol();

  return RawType.getLength();
}

bool opts::pretty::compareFunctionSymbols(
    const std::unique_ptr<PDBSymbolFunc> &F1,
    const std::unique_ptr<PDBSymbolFunc> &F2) {
  assert(opts::pretty::SymbolOrder != opts::pretty::SymbolSortMode::None);

  if (opts::pretty::SymbolOrder == opts::pretty::SymbolSortMode::Name)
    return F1->getName() < F2->getName();

  // Note that we intentionally sort in descending order on length, since
  // long functions are more interesting than short functions.
  return F1->getLength() > F2->getLength();
}

bool opts::pretty::compareDataSymbols(
    const std::unique_ptr<PDBSymbolData> &F1,
    const std::unique_ptr<PDBSymbolData> &F2) {
  assert(opts::pretty::SymbolOrder != opts::pretty::SymbolSortMode::None);

  if (opts::pretty::SymbolOrder == opts::pretty::SymbolSortMode::Name)
    return F1->getName() < F2->getName();

  // Note that we intentionally sort in descending order on length, since
  // large types are more interesting than short ones.
  return getTypeLength(*F1) > getTypeLength(*F2);
}

static std::string stringOr(std::string Str, std::string IfEmpty) {
  return (Str.empty()) ? IfEmpty : Str;
}

static void dumpInjectedSources(LinePrinter &Printer, IPDBSession &Session) {
  auto Sources = Session.getInjectedSources();
  if (!Sources || !Sources->getChildCount()) {
    Printer.printLine("There are no injected sources.");
    return;
  }

  while (auto IS = Sources->getNext()) {
    Printer.NewLine();
    std::string File = stringOr(IS->getFileName(), "<null>");
    uint64_t Size = IS->getCodeByteSize();
    std::string Obj = stringOr(IS->getObjectFileName(), "<null>");
    std::string VFName = stringOr(IS->getVirtualFileName(), "<null>");
    uint32_t CRC = IS->getCrc32();

    WithColor(Printer, PDB_ColorItem::Path).get() << File;
    Printer << " (";
    WithColor(Printer, PDB_ColorItem::LiteralValue).get() << Size;
    Printer << " bytes): ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "obj";
    Printer << "=";
    WithColor(Printer, PDB_ColorItem::Path).get() << Obj;
    Printer << ", ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "vname";
    Printer << "=";
    WithColor(Printer, PDB_ColorItem::Path).get() << VFName;
    Printer << ", ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "crc";
    Printer << "=";
    WithColor(Printer, PDB_ColorItem::LiteralValue).get() << CRC;
    Printer << ", ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "compression";
    Printer << "=";
    dumpPDBSourceCompression(
        WithColor(Printer, PDB_ColorItem::LiteralValue).get(),
        IS->getCompression());

    if (!opts::pretty::ShowInjectedSourceContent)
      continue;

    // Set the indent level to 0 when printing file content.
    int Indent = Printer.getIndentLevel();
    Printer.Unindent(Indent);

    if (IS->getCompression() == PDB_SourceCompression::None)
      Printer.printLine(IS->getCode());
    else
      Printer.formatBinary("Compressed data",
                           arrayRefFromStringRef(IS->getCode()),
                           /*StartOffset=*/0);

    // Re-indent back to the original level.
    Printer.Indent(Indent);
  }
}

template <typename OuterT, typename ChildT>
void diaDumpChildren(PDBSymbol &Outer, PdbSymbolIdField Ids,
                     PdbSymbolIdField Recurse) {
  OuterT *ConcreteOuter = dyn_cast<OuterT>(&Outer);
  if (!ConcreteOuter)
    return;

  auto Children = ConcreteOuter->template findAllChildren<ChildT>();
  while (auto Child = Children->getNext()) {
    outs() << "  {";
    Child->defaultDump(outs(), 4, Ids, Recurse);
    outs() << "\n  }\n";
  }
}

static void dumpDia(StringRef Path) {
  std::unique_ptr<IPDBSession> Session;

  const auto ReaderType =
      opts::diadump::Native ? PDB_ReaderType::Native : PDB_ReaderType::DIA;
  ExitOnErr(loadDataForPDB(ReaderType, Path, Session));

  auto GlobalScope = Session->getGlobalScope();

  std::vector<PDB_SymType> SymTypes;

  if (opts::diadump::Compilands)
    SymTypes.push_back(PDB_SymType::Compiland);
  if (opts::diadump::Enums)
    SymTypes.push_back(PDB_SymType::Enum);
  if (opts::diadump::Pointers)
    SymTypes.push_back(PDB_SymType::PointerType);
  if (opts::diadump::UDTs)
    SymTypes.push_back(PDB_SymType::UDT);
  if (opts::diadump::Funcsigs)
    SymTypes.push_back(PDB_SymType::FunctionSig);
  if (opts::diadump::Arrays)
    SymTypes.push_back(PDB_SymType::ArrayType);
  if (opts::diadump::VTShapes)
    SymTypes.push_back(PDB_SymType::VTableShape);
  if (opts::diadump::Typedefs)
    SymTypes.push_back(PDB_SymType::Typedef);
  PdbSymbolIdField Ids = opts::diadump::NoSymIndexIds ? PdbSymbolIdField::None
                                                      : PdbSymbolIdField::All;

  PdbSymbolIdField Recurse = PdbSymbolIdField::None;
  if (opts::diadump::Recurse)
    Recurse = PdbSymbolIdField::All;
  if (!opts::diadump::ShowClassHierarchy)
    Ids &= ~(PdbSymbolIdField::ClassParent | PdbSymbolIdField::LexicalParent);

  for (PDB_SymType ST : SymTypes) {
    auto Children = GlobalScope->findAllChildren(ST);
    while (auto Child = Children->getNext()) {
      outs() << "{";
      Child->defaultDump(outs(), 2, Ids, Recurse);

      diaDumpChildren<PDBSymbolTypeEnum, PDBSymbolData>(*Child, Ids, Recurse);
      outs() << "\n}\n";
    }
  }
}

static void dumpPretty(StringRef Path) {
  std::unique_ptr<IPDBSession> Session;

  const auto ReaderType =
      opts::pretty::Native ? PDB_ReaderType::Native : PDB_ReaderType::DIA;
  ExitOnErr(loadDataForPDB(ReaderType, Path, Session));

  if (opts::pretty::LoadAddress)
    Session->setLoadAddress(opts::pretty::LoadAddress);

  auto &Stream = outs();
  const bool UseColor = opts::pretty::ColorOutput == cl::BOU_UNSET
                            ? Stream.has_colors()
                            : opts::pretty::ColorOutput == cl::BOU_TRUE;
  LinePrinter Printer(2, UseColor, Stream, opts::Filters);

  auto GlobalScope(Session->getGlobalScope());
  if (!GlobalScope)
    return;
  std::string FileName(GlobalScope->getSymbolsFileName());

  WithColor(Printer, PDB_ColorItem::None).get() << "Summary for ";
  WithColor(Printer, PDB_ColorItem::Path).get() << FileName;
  Printer.Indent();
  uint64_t FileSize = 0;

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << "Size";
  if (!sys::fs::file_size(FileName, FileSize)) {
    Printer << ": " << FileSize << " bytes";
  } else {
    Printer << ": (Unable to obtain file size)";
  }

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << "Guid";
  Printer << ": " << GlobalScope->getGuid();

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << "Age";
  Printer << ": " << GlobalScope->getAge();

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << "Attributes";
  Printer << ": ";
  if (GlobalScope->hasCTypes())
    outs() << "HasCTypes ";
  if (GlobalScope->hasPrivateSymbols())
    outs() << "HasPrivateSymbols ";
  Printer.Unindent();

  if (!opts::pretty::WithName.empty()) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get()
        << "---SYMBOLS & TYPES BY NAME---";

    for (StringRef Name : opts::pretty::WithName) {
      auto Symbols = GlobalScope->findChildren(
          PDB_SymType::None, Name, PDB_NameSearchFlags::NS_CaseSensitive);
      if (!Symbols || Symbols->getChildCount() == 0) {
        Printer.formatLine("[not found] - {0}", Name);
        continue;
      }
      Printer.formatLine("[{0} occurrences] - {1}", Symbols->getChildCount(),
                         Name);

      AutoIndent Indent(Printer);
      Printer.NewLine();

      while (auto Symbol = Symbols->getNext()) {
        switch (Symbol->getSymTag()) {
        case PDB_SymType::Typedef: {
          TypedefDumper TD(Printer);
          std::unique_ptr<PDBSymbolTypeTypedef> T =
              llvm::unique_dyn_cast<PDBSymbolTypeTypedef>(std::move(Symbol));
          TD.start(*T);
          break;
        }
        case PDB_SymType::Enum: {
          EnumDumper ED(Printer);
          std::unique_ptr<PDBSymbolTypeEnum> E =
              llvm::unique_dyn_cast<PDBSymbolTypeEnum>(std::move(Symbol));
          ED.start(*E);
          break;
        }
        case PDB_SymType::UDT: {
          ClassDefinitionDumper CD(Printer);
          std::unique_ptr<PDBSymbolTypeUDT> C =
              llvm::unique_dyn_cast<PDBSymbolTypeUDT>(std::move(Symbol));
          CD.start(*C);
          break;
        }
        case PDB_SymType::BaseClass:
        case PDB_SymType::Friend: {
          TypeDumper TD(Printer);
          Symbol->dump(TD);
          break;
        }
        case PDB_SymType::Function: {
          FunctionDumper FD(Printer);
          std::unique_ptr<PDBSymbolFunc> F =
              llvm::unique_dyn_cast<PDBSymbolFunc>(std::move(Symbol));
          FD.start(*F, FunctionDumper::PointerType::None);
          break;
        }
        case PDB_SymType::Data: {
          VariableDumper VD(Printer);
          std::unique_ptr<PDBSymbolData> D =
              llvm::unique_dyn_cast<PDBSymbolData>(std::move(Symbol));
          VD.start(*D);
          break;
        }
        case PDB_SymType::PublicSymbol: {
          ExternalSymbolDumper ED(Printer);
          std::unique_ptr<PDBSymbolPublicSymbol> PS =
              llvm::unique_dyn_cast<PDBSymbolPublicSymbol>(std::move(Symbol));
          ED.dump(*PS);
          break;
        }
        default:
          llvm_unreachable("Unexpected symbol tag!");
        }
      }
    }
    llvm::outs().flush();
  }

  if (opts::pretty::Compilands) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get()
        << "---COMPILANDS---";
    auto Compilands = GlobalScope->findAllChildren<PDBSymbolCompiland>();

    if (Compilands) {
      Printer.Indent();
      CompilandDumper Dumper(Printer);
      CompilandDumpFlags options = CompilandDumper::Flags::None;
      if (opts::pretty::Lines)
        options = options | CompilandDumper::Flags::Lines;
      while (auto Compiland = Compilands->getNext())
        Dumper.start(*Compiland, options);
      Printer.Unindent();
    }
  }

  if (opts::pretty::Classes || opts::pretty::Enums || opts::pretty::Typedefs ||
      opts::pretty::Funcsigs || opts::pretty::Pointers ||
      opts::pretty::Arrays || opts::pretty::VTShapes) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get() << "---TYPES---";
    Printer.Indent();
    TypeDumper Dumper(Printer);
    Dumper.start(*GlobalScope);
    Printer.Unindent();
  }

  if (opts::pretty::Symbols) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get() << "---SYMBOLS---";
    if (auto Compilands = GlobalScope->findAllChildren<PDBSymbolCompiland>()) {
      Printer.Indent();
      CompilandDumper Dumper(Printer);
      while (auto Compiland = Compilands->getNext())
        Dumper.start(*Compiland, true);
      Printer.Unindent();
    }
  }

  if (opts::pretty::Globals) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get() << "---GLOBALS---";
    Printer.Indent();
    if (shouldDumpSymLevel(opts::pretty::SymLevel::Functions)) {
      if (auto Functions = GlobalScope->findAllChildren<PDBSymbolFunc>()) {
        FunctionDumper Dumper(Printer);
        if (opts::pretty::SymbolOrder == opts::pretty::SymbolSortMode::None) {
          while (auto Function = Functions->getNext()) {
            Printer.NewLine();
            Dumper.start(*Function, FunctionDumper::PointerType::None);
          }
        } else {
          std::vector<std::unique_ptr<PDBSymbolFunc>> Funcs;
          while (auto Func = Functions->getNext())
            Funcs.push_back(std::move(Func));
          llvm::sort(Funcs, opts::pretty::compareFunctionSymbols);
          for (const auto &Func : Funcs) {
            Printer.NewLine();
            Dumper.start(*Func, FunctionDumper::PointerType::None);
          }
        }
      }
    }
    if (shouldDumpSymLevel(opts::pretty::SymLevel::Data)) {
      if (auto Vars = GlobalScope->findAllChildren<PDBSymbolData>()) {
        VariableDumper Dumper(Printer);
        if (opts::pretty::SymbolOrder == opts::pretty::SymbolSortMode::None) {
          while (auto Var = Vars->getNext())
            Dumper.start(*Var);
        } else {
          std::vector<std::unique_ptr<PDBSymbolData>> Datas;
          while (auto Var = Vars->getNext())
            Datas.push_back(std::move(Var));
          llvm::sort(Datas, opts::pretty::compareDataSymbols);
          for (const auto &Var : Datas)
            Dumper.start(*Var);
        }
      }
    }
    if (shouldDumpSymLevel(opts::pretty::SymLevel::Thunks)) {
      if (auto Thunks = GlobalScope->findAllChildren<PDBSymbolThunk>()) {
        CompilandDumper Dumper(Printer);
        while (auto Thunk = Thunks->getNext())
          Dumper.dump(*Thunk);
      }
    }
    Printer.Unindent();
  }
  if (opts::pretty::Externals) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get() << "---EXTERNALS---";
    Printer.Indent();
    ExternalSymbolDumper Dumper(Printer);
    Dumper.start(*GlobalScope);
  }
  if (opts::pretty::Lines) {
    Printer.NewLine();
  }
  if (opts::pretty::InjectedSources) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get()
        << "---INJECTED SOURCES---";
    AutoIndent Indent1(Printer);
    dumpInjectedSources(Printer, *Session);
  }

  Printer.NewLine();
  outs().flush();
}

static void mergePdbs() {
  BumpPtrAllocator Allocator;
  MergingTypeTableBuilder MergedTpi(Allocator);
  MergingTypeTableBuilder MergedIpi(Allocator);

  // Create a Tpi and Ipi type table with all types from all input files.
  for (const auto &Path : opts::merge::InputFilenames) {
    std::unique_ptr<IPDBSession> Session;
    auto &File = loadPDB(Path, Session);
    SmallVector<TypeIndex, 128> TypeMap;
    SmallVector<TypeIndex, 128> IdMap;
    if (File.hasPDBTpiStream()) {
      auto &Tpi = ExitOnErr(File.getPDBTpiStream());
      ExitOnErr(
          codeview::mergeTypeRecords(MergedTpi, TypeMap, Tpi.typeArray()));
    }
    if (File.hasPDBIpiStream()) {
      auto &Ipi = ExitOnErr(File.getPDBIpiStream());
      ExitOnErr(codeview::mergeIdRecords(MergedIpi, TypeMap, IdMap,
                                         Ipi.typeArray()));
    }
  }

  // Then write the PDB.
  PDBFileBuilder Builder(Allocator);
  ExitOnErr(Builder.initialize(4096));
  // Add each of the reserved streams.  We might not put any data in them,
  // but at least they have to be present.
  for (uint32_t I = 0; I < kSpecialStreamCount; ++I)
    ExitOnErr(Builder.getMsfBuilder().addStream(0));

  auto &DestTpi = Builder.getTpiBuilder();
  auto &DestIpi = Builder.getIpiBuilder();
  MergedTpi.ForEachRecord([&DestTpi](TypeIndex TI, const CVType &Type) {
    DestTpi.addTypeRecord(Type.RecordData, std::nullopt);
  });
  MergedIpi.ForEachRecord([&DestIpi](TypeIndex TI, const CVType &Type) {
    DestIpi.addTypeRecord(Type.RecordData, std::nullopt);
  });
  Builder.getInfoBuilder().addFeature(PdbRaw_FeatureSig::VC140);

  SmallString<64> OutFile(opts::merge::PdbOutputFile);
  if (OutFile.empty()) {
    OutFile = opts::merge::InputFilenames[0];
    llvm::sys::path::replace_extension(OutFile, "merged.pdb");
  }

  codeview::GUID IgnoredOutGuid;
  ExitOnErr(Builder.commit(OutFile, &IgnoredOutGuid));
}

static void explain() {
  std::unique_ptr<IPDBSession> Session;
  InputFile IF =
      ExitOnErr(InputFile::open(opts::explain::InputFilename.front(), true));

  for (uint64_t Off : opts::explain::Offsets) {
    auto O = std::make_unique<ExplainOutputStyle>(IF, Off);

    ExitOnErr(O->dump());
  }
}

static void exportStream() {
  std::unique_ptr<IPDBSession> Session;
  PDBFile &File = loadPDB(opts::exportstream::InputFilename.front(), Session);

  std::unique_ptr<MappedBlockStream> SourceStream;
  uint32_t Index = 0;
  bool Success = false;
  std::string OutFileName = opts::exportstream::OutputFile;

  if (!opts::exportstream::ForceName) {
    // First try to parse it as an integer, if it fails fall back to treating it
    // as a named stream.
    if (to_integer(opts::exportstream::Stream, Index)) {
      if (Index >= File.getNumStreams()) {
        errs() << "Error: " << Index << " is not a valid stream index.\n";
        exit(1);
      }
      Success = true;
      outs() << "Dumping contents of stream index " << Index << " to file "
             << OutFileName << ".\n";
    }
  }

  if (!Success) {
    InfoStream &IS = cantFail(File.getPDBInfoStream());
    Index = ExitOnErr(IS.getNamedStreamIndex(opts::exportstream::Stream));
    outs() << "Dumping contents of stream '" << opts::exportstream::Stream
           << "' (index " << Index << ") to file " << OutFileName << ".\n";
  }

  SourceStream = File.createIndexedStream(Index);
  auto OutFile = ExitOnErr(
      FileOutputBuffer::create(OutFileName, SourceStream->getLength()));
  FileBufferByteStream DestStream(std::move(OutFile), llvm::endianness::little);
  BinaryStreamWriter Writer(DestStream);
  ExitOnErr(Writer.writeStreamRef(*SourceStream));
  ExitOnErr(DestStream.commit());
}

static bool parseRange(StringRef Str,
                       std::optional<opts::bytes::NumberRange> &Parsed) {
  if (Str.empty())
    return true;

  llvm::Regex R("^([^-]+)(-([^-]+))?$");
  llvm::SmallVector<llvm::StringRef, 2> Matches;
  if (!R.match(Str, &Matches))
    return false;

  Parsed.emplace();
  if (!to_integer(Matches[1], Parsed->Min))
    return false;

  if (!Matches[3].empty()) {
    Parsed->Max.emplace();
    if (!to_integer(Matches[3], *Parsed->Max))
      return false;
  }
  return true;
}

static void simplifyChunkList(llvm::cl::list<opts::ModuleSubsection> &Chunks) {
  // If this list contains "All" plus some other stuff, remove the other stuff
  // and just keep "All" in the list.
  if (!llvm::is_contained(Chunks, opts::ModuleSubsection::All))
    return;
  Chunks.reset();
  Chunks.push_back(opts::ModuleSubsection::All);
}

int main(int Argc, const char **Argv) {
  InitLLVM X(Argc, Argv);
  ExitOnErr.setBanner("llvm-pdbutil: ");

  cl::HideUnrelatedOptions(
      {&opts::TypeCategory, &opts::FilterCategory, &opts::OtherOptions});
  cl::ParseCommandLineOptions(Argc, Argv, "LLVM PDB Dumper\n");

  if (opts::BytesSubcommand) {
    if (!parseRange(opts::bytes::DumpBlockRangeOpt,
                    opts::bytes::DumpBlockRange)) {
      errs() << "Argument '" << opts::bytes::DumpBlockRangeOpt
             << "' invalid format.\n";
      errs().flush();
      exit(1);
    }
    if (!parseRange(opts::bytes::DumpByteRangeOpt,
                    opts::bytes::DumpByteRange)) {
      errs() << "Argument '" << opts::bytes::DumpByteRangeOpt
             << "' invalid format.\n";
      errs().flush();
      exit(1);
    }
  }

  if (opts::DumpSubcommand) {
    if (opts::dump::RawAll) {
      opts::dump::DumpGlobals = true;
      opts::dump::DumpFpo = true;
      opts::dump::DumpInlineeLines = true;
      opts::dump::DumpIds = true;
      opts::dump::DumpIdExtras = true;
      opts::dump::DumpLines = true;
      opts::dump::DumpModules = true;
      opts::dump::DumpModuleFiles = true;
      opts::dump::DumpPublics = true;
      opts::dump::DumpSectionContribs = true;
      opts::dump::DumpSectionHeaders = true;
      opts::dump::DumpSectionMap = true;
      opts::dump::DumpStreams = true;
      opts::dump::DumpStreamBlocks = true;
      opts::dump::DumpStringTable = true;
      opts::dump::DumpStringTableDetails = true;
      opts::dump::DumpSummary = true;
      opts::dump::DumpSymbols = true;
      opts::dump::DumpSymbolStats = true;
      opts::dump::DumpTypes = true;
      opts::dump::DumpTypeExtras = true;
      opts::dump::DumpUdtStats = true;
      opts::dump::DumpXme = true;
      opts::dump::DumpXmi = true;
    }
  }
  if (opts::PdbToYamlSubcommand) {
    if (opts::pdb2yaml::All) {
      opts::pdb2yaml::StreamMetadata = true;
      opts::pdb2yaml::StreamDirectory = true;
      opts::pdb2yaml::PdbStream = true;
      opts::pdb2yaml::StringTable = true;
      opts::pdb2yaml::DbiStream = true;
      opts::pdb2yaml::TpiStream = true;
      opts::pdb2yaml::IpiStream = true;
      opts::pdb2yaml::PublicsStream = true;
      opts::pdb2yaml::DumpModules = true;
      opts::pdb2yaml::DumpModuleFiles = true;
      opts::pdb2yaml::DumpModuleSyms = true;
      opts::pdb2yaml::DumpModuleSubsections.push_back(
          opts::ModuleSubsection::All);
    }
    simplifyChunkList(opts::pdb2yaml::DumpModuleSubsections);

    if (opts::pdb2yaml::DumpModuleSyms || opts::pdb2yaml::DumpModuleFiles)
      opts::pdb2yaml::DumpModules = true;

    if (opts::pdb2yaml::DumpModules)
      opts::pdb2yaml::DbiStream = true;
  }

  llvm::sys::InitializeCOMRAII COM(llvm::sys::COMThreadingMode::MultiThreaded);

  // Initialize the filters for LinePrinter.
  auto propagate = [&](auto &Target, auto &Reference) {
    for (std::string &Option : Reference)
      Target.push_back(Option);
  };

  propagate(opts::Filters.ExcludeTypes, opts::pretty::ExcludeTypes);
  propagate(opts::Filters.ExcludeTypes, opts::pretty::ExcludeTypes);
  propagate(opts::Filters.ExcludeSymbols, opts::pretty::ExcludeSymbols);
  propagate(opts::Filters.ExcludeCompilands, opts::pretty::ExcludeCompilands);
  propagate(opts::Filters.IncludeTypes, opts::pretty::IncludeTypes);
  propagate(opts::Filters.IncludeSymbols, opts::pretty::IncludeSymbols);
  propagate(opts::Filters.IncludeCompilands, opts::pretty::IncludeCompilands);
  opts::Filters.PaddingThreshold = opts::pretty::PaddingThreshold;
  opts::Filters.SizeThreshold = opts::pretty::SizeThreshold;
  opts::Filters.JustMyCode = opts::dump::JustMyCode;
  if (opts::dump::DumpModi.getNumOccurrences() > 0) {
    if (opts::dump::DumpModi.getNumOccurrences() != 1) {
      errs() << "argument '-modi' specified more than once.\n";
      errs().flush();
      exit(1);
    }
    opts::Filters.DumpModi = opts::dump::DumpModi;
  }
  if (opts::dump::DumpSymbolOffset) {
    if (opts::dump::DumpModi.getNumOccurrences() != 1) {
      errs()
          << "need to specify argument '-modi' when using '-symbol-offset'.\n";
      errs().flush();
      exit(1);
    }
    opts::Filters.SymbolOffset = opts::dump::DumpSymbolOffset;
    if (opts::dump::DumpParents)
      opts::Filters.ParentRecurseDepth = opts::dump::DumpParentDepth;
    if (opts::dump::DumpChildren)
      opts::Filters.ChildrenRecurseDepth = opts::dump::DumpChildrenDepth;
  }

  if (opts::PdbToYamlSubcommand) {
    pdb2Yaml(opts::pdb2yaml::InputFilename.front());
  } else if (opts::YamlToPdbSubcommand) {
    if (opts::yaml2pdb::YamlPdbOutputFile.empty()) {
      SmallString<16> OutputFilename(opts::yaml2pdb::InputFilename.getValue());
      sys::path::replace_extension(OutputFilename, ".pdb");
      opts::yaml2pdb::YamlPdbOutputFile = std::string(OutputFilename);
    }
    yamlToPdb(opts::yaml2pdb::InputFilename);
  } else if (opts::DiaDumpSubcommand) {
    llvm::for_each(opts::diadump::InputFilenames, dumpDia);
  } else if (opts::PrettySubcommand) {
    if (opts::pretty::Lines)
      opts::pretty::Compilands = true;

    if (opts::pretty::All) {
      opts::pretty::Compilands = true;
      opts::pretty::Symbols = true;
      opts::pretty::Globals = true;
      opts::pretty::Types = true;
      opts::pretty::Externals = true;
      opts::pretty::Lines = true;
    }

    if (opts::pretty::Types) {
      opts::pretty::Classes = true;
      opts::pretty::Typedefs = true;
      opts::pretty::Enums = true;
      opts::pretty::Pointers = true;
      opts::pretty::Funcsigs = true;
    }

    // When adding filters for excluded compilands and types, we need to
    // remember that these are regexes.  So special characters such as * and \
    // need to be escaped in the regex.  In the case of a literal \, this means
    // it needs to be escaped again in the C++.  So matching a single \ in the
    // input requires 4 \es in the C++.
    if (opts::pretty::ExcludeCompilerGenerated) {
      opts::Filters.ExcludeTypes.push_back("__vc_attributes");
      opts::Filters.ExcludeCompilands.push_back("\\* Linker \\*");
    }
    if (opts::pretty::ExcludeSystemLibraries) {
      opts::Filters.ExcludeCompilands.push_back(
          "f:\\\\binaries\\\\Intermediate\\\\vctools\\\\crt_bld");
      opts::Filters.ExcludeCompilands.push_back("f:\\\\dd\\\\vctools\\\\crt");
      opts::Filters.ExcludeCompilands.push_back(
          "d:\\\\th.obj.x86fre\\\\minkernel");
    }
    llvm::for_each(opts::pretty::InputFilenames, dumpPretty);
  } else if (opts::DumpSubcommand) {
    llvm::for_each(opts::dump::InputFilenames, dumpRaw);
  } else if (opts::BytesSubcommand) {
    llvm::for_each(opts::bytes::InputFilenames, dumpBytes);
  } else if (opts::MergeSubcommand) {
    if (opts::merge::InputFilenames.size() < 2) {
      errs() << "merge subcommand requires at least 2 input files.\n";
      exit(1);
    }
    mergePdbs();
  } else if (opts::ExplainSubcommand) {
    explain();
  } else if (opts::ExportSubcommand) {
    exportStream();
  }

  outs().flush();
  return 0;
}
