//===- DumpOutputStyle.cpp ------------------------------------ *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DumpOutputStyle.h"

#include "MinimalSymbolDumper.h"
#include "MinimalTypeDumper.h"
#include "StreamUtil.h"
#include "TypeReferenceTracker.h"
#include "llvm-pdbutil.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/CodeView/CVSymbolVisitor.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugCrossExSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugCrossImpSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSymbolsSubsection.h"
#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbackPipeline.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"
#include "llvm/DebugInfo/CodeView/TypeHashing.h"
#include "llvm/DebugInfo/CodeView/TypeIndexDiscovery.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/FormatUtil.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/ISectionContribVisitor.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiHashing.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"

#include <cctype>

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

DumpOutputStyle::DumpOutputStyle(InputFile &File)
    : File(File), P(2, false, outs(), opts::Filters) {
  if (opts::dump::DumpTypeRefStats)
    RefTracker.reset(new TypeReferenceTracker(File));
}

DumpOutputStyle::~DumpOutputStyle() {}

PDBFile &DumpOutputStyle::getPdb() { return File.pdb(); }
object::COFFObjectFile &DumpOutputStyle::getObj() { return File.obj(); }

void DumpOutputStyle::printStreamNotValidForObj() {
  AutoIndent Indent(P, 4);
  P.formatLine("Dumping this stream is not valid for object files");
}

void DumpOutputStyle::printStreamNotPresent(StringRef StreamName) {
  AutoIndent Indent(P, 4);
  P.formatLine("{0} stream not present", StreamName);
}

Error DumpOutputStyle::dump() {
  // Walk symbols & globals if we are supposed to mark types referenced.
  if (opts::dump::DumpTypeRefStats)
    RefTracker->mark();

  if (opts::dump::DumpSummary) {
    if (auto EC = dumpFileSummary())
      return EC;
    P.NewLine();
  }

  if (opts::dump::DumpStreams) {
    if (auto EC = dumpStreamSummary())
      return EC;
    P.NewLine();
  }

  if (opts::dump::DumpSymbolStats) {
    ExitOnError Err("Unexpected error processing module stats: ");
    Err(dumpSymbolStats());
    P.NewLine();
  }

  if (opts::dump::DumpUdtStats) {
    if (auto EC = dumpUdtStats())
      return EC;
    P.NewLine();
  }

  if (opts::dump::DumpTypeStats || opts::dump::DumpIDStats) {
    if (auto EC = dumpTypeStats())
      return EC;
    P.NewLine();
  }

  if (opts::dump::DumpNamedStreams) {
    if (auto EC = dumpNamedStreams())
      return EC;
    P.NewLine();
  }

  if (opts::dump::DumpStringTable || opts::dump::DumpStringTableDetails) {
    if (auto EC = dumpStringTable())
      return EC;
    P.NewLine();
  }

  if (opts::dump::DumpModules) {
    ExitOnError Err("Unexpected error processing modules: ");
    Err(dumpModules());
  }

  if (opts::dump::DumpModuleFiles) {
    ExitOnError Err("Unexpected error processing files: ");
    Err(dumpModuleFiles());
  }

  if (opts::dump::DumpLines) {
    ExitOnError Err("Unexpected error processing lines: ");
    Err(dumpLines());
  }

  if (opts::dump::DumpInlineeLines) {
    ExitOnError Err("Unexpected error processing inlinee lines: ");
    Err(dumpInlineeLines());
  }

  if (opts::dump::DumpXmi) {
    ExitOnError Err("Unexpected error processing cross module imports: ");
    Err(dumpXmi());
  }

  if (opts::dump::DumpXme) {
    ExitOnError Err("Unexpected error processing cross module exports: ");
    Err(dumpXme());
  }

  if (opts::dump::DumpFpo) {
    if (auto EC = dumpFpo())
      return EC;
  }

  if (File.isObj()) {
    if (opts::dump::DumpTypes || !opts::dump::DumpTypeIndex.empty() ||
        opts::dump::DumpTypeExtras)
      if (auto EC = dumpTypesFromObjectFile())
        return EC;
  } else {
    if (opts::dump::DumpTypes || !opts::dump::DumpTypeIndex.empty() ||
        opts::dump::DumpTypeExtras) {
      if (auto EC = dumpTpiStream(StreamTPI))
        return EC;
    }

    if (opts::dump::DumpIds || !opts::dump::DumpIdIndex.empty() ||
        opts::dump::DumpIdExtras) {
      if (auto EC = dumpTpiStream(StreamIPI))
        return EC;
    }
  }

  if (opts::dump::DumpGSIRecords) {
    if (auto EC = dumpGSIRecords())
      return EC;
  }

  if (opts::dump::DumpGlobals) {
    if (auto EC = dumpGlobals())
      return EC;
  }

  if (opts::dump::DumpPublics) {
    if (auto EC = dumpPublics())
      return EC;
  }

  if (opts::dump::DumpSymbols) {
    ExitOnError Err("Unexpected error processing symbols: ");
    Err(File.isPdb() ? dumpModuleSymsForPdb() : dumpModuleSymsForObj());
  }

  if (opts::dump::DumpTypeRefStats) {
    if (auto EC = dumpTypeRefStats())
      return EC;
  }

  if (opts::dump::DumpSectionHeaders) {
    if (auto EC = dumpSectionHeaders())
      return EC;
  }

  if (opts::dump::DumpSectionContribs) {
    if (auto EC = dumpSectionContribs())
      return EC;
  }

  if (opts::dump::DumpSectionMap) {
    if (auto EC = dumpSectionMap())
      return EC;
  }

  P.NewLine();

  return Error::success();
}

static void printHeader(LinePrinter &P, const Twine &S) {
  P.NewLine();
  P.formatLine("{0,=60}", S);
  P.formatLine("{0}", fmt_repeat('=', 60));
}

Error DumpOutputStyle::dumpFileSummary() {
  printHeader(P, "Summary");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  AutoIndent Indent(P);
  ExitOnError Err("Invalid PDB Format: ");

  P.formatLine("Block Size: {0}", getPdb().getBlockSize());
  P.formatLine("Number of blocks: {0}", getPdb().getBlockCount());
  P.formatLine("Number of streams: {0}", getPdb().getNumStreams());

  auto &PS = Err(getPdb().getPDBInfoStream());
  P.formatLine("Signature: {0}", PS.getSignature());
  P.formatLine("Age: {0}", PS.getAge());
  P.formatLine("GUID: {0}", fmt_guid(PS.getGuid().Guid));
  P.formatLine("Features: {0:x+}", static_cast<uint32_t>(PS.getFeatures()));
  P.formatLine("Has Debug Info: {0}", getPdb().hasPDBDbiStream());
  P.formatLine("Has Types: {0}", getPdb().hasPDBTpiStream());
  P.formatLine("Has IDs: {0}", getPdb().hasPDBIpiStream());
  P.formatLine("Has Globals: {0}", getPdb().hasPDBGlobalsStream());
  P.formatLine("Has Publics: {0}", getPdb().hasPDBPublicsStream());
  if (getPdb().hasPDBDbiStream()) {
    DbiStream &DBI = Err(getPdb().getPDBDbiStream());
    P.formatLine("Is incrementally linked: {0}", DBI.isIncrementallyLinked());
    P.formatLine("Has conflicting types: {0}", DBI.hasCTypes());
    P.formatLine("Is stripped: {0}", DBI.isStripped());
  }

  return Error::success();
}

static StatCollection getSymbolStats(const SymbolGroup &SG,
                                     StatCollection &CumulativeStats) {
  StatCollection Stats;
  if (SG.getFile().isPdb() && SG.hasDebugStream()) {
    // For PDB files, all symbols are packed into one stream.
    for (const auto &S : SG.getPdbModuleStream().symbols(nullptr)) {
      Stats.update(S.kind(), S.length());
      CumulativeStats.update(S.kind(), S.length());
    }
    return Stats;
  }

  for (const auto &SS : SG.getDebugSubsections()) {
    // For object files, all symbols are spread across multiple Symbol
    // subsections of a given .debug$S section.
    if (SS.kind() != DebugSubsectionKind::Symbols)
      continue;
    DebugSymbolsSubsectionRef Symbols;
    BinaryStreamReader Reader(SS.getRecordData());
    cantFail(Symbols.initialize(Reader));
    for (const auto &S : Symbols) {
      Stats.update(S.kind(), S.length());
      CumulativeStats.update(S.kind(), S.length());
    }
  }
  return Stats;
}

static StatCollection getChunkStats(const SymbolGroup &SG,
                                    StatCollection &CumulativeStats) {
  StatCollection Stats;
  for (const auto &Chunk : SG.getDebugSubsections()) {
    Stats.update(uint32_t(Chunk.kind()), Chunk.getRecordLength());
    CumulativeStats.update(uint32_t(Chunk.kind()), Chunk.getRecordLength());
  }
  return Stats;
}

static inline std::string formatModuleDetailKind(DebugSubsectionKind K) {
  return formatChunkKind(K, false);
}

static inline std::string formatModuleDetailKind(SymbolKind K) {
  return formatSymbolKind(K);
}

// Get the stats sorted by size, descending.
std::vector<StatCollection::KindAndStat>
StatCollection::getStatsSortedBySize() const {
  std::vector<KindAndStat> SortedStats(Individual.begin(), Individual.end());
  llvm::stable_sort(SortedStats,
                    [](const KindAndStat &LHS, const KindAndStat &RHS) {
                      return LHS.second.Size > RHS.second.Size;
                    });
  return SortedStats;
}

template <typename Kind>
static void printModuleDetailStats(LinePrinter &P, StringRef Label,
                                   const StatCollection &Stats) {
  P.NewLine();
  P.formatLine("  {0}", Label);
  AutoIndent Indent(P);
  P.formatLine("{0,40}: {1,7} entries ({2,12:N} bytes)", "Total",
               Stats.Totals.Count, Stats.Totals.Size);
  P.formatLine("{0}", fmt_repeat('-', 74));

  for (const auto &K : Stats.getStatsSortedBySize()) {
    std::string KindName = formatModuleDetailKind(Kind(K.first));
    P.formatLine("{0,40}: {1,7} entries ({2,12:N} bytes)", KindName,
                 K.second.Count, K.second.Size);
  }
}

Error DumpOutputStyle::dumpStreamSummary() {
  printHeader(P, "Streams");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  AutoIndent Indent(P);

  if (StreamPurposes.empty())
    discoverStreamPurposes(getPdb(), StreamPurposes);

  uint32_t StreamCount = getPdb().getNumStreams();
  uint32_t MaxStreamSize = getPdb().getMaxStreamSize();

  for (uint32_t StreamIdx = 0; StreamIdx < StreamCount; ++StreamIdx) {
    P.formatLine(
        "Stream {0} ({1} bytes): [{2}]",
        fmt_align(StreamIdx, AlignStyle::Right, NumDigits(StreamCount)),
        fmt_align(getPdb().getStreamByteSize(StreamIdx), AlignStyle::Right,
                  NumDigits(MaxStreamSize)),
        StreamPurposes[StreamIdx].getLongName());

    if (opts::dump::DumpStreamBlocks) {
      auto Blocks = getPdb().getStreamBlockList(StreamIdx);
      std::vector<uint32_t> BV(Blocks.begin(), Blocks.end());
      P.formatLine("       {0}  Blocks: [{1}]",
                   fmt_repeat(' ', NumDigits(StreamCount)),
                   make_range(BV.begin(), BV.end()));
    }
  }

  return Error::success();
}

static Expected<std::pair<std::unique_ptr<MappedBlockStream>,
                          ArrayRef<llvm::object::coff_section>>>
loadSectionHeaders(PDBFile &File, DbgHeaderType Type) {
  if (!File.hasPDBDbiStream())
    return make_error<StringError>(
        "Section headers require a DBI Stream, which could not be loaded",
        inconvertibleErrorCode());

  DbiStream &Dbi = cantFail(File.getPDBDbiStream());
  uint32_t SI = Dbi.getDebugStreamIndex(Type);

  if (SI == kInvalidStreamIndex)
    return make_error<StringError>(
        "PDB does not contain the requested image section header type",
        inconvertibleErrorCode());

  auto Stream = File.createIndexedStream(SI);
  if (!Stream)
    return make_error<StringError>("Could not load the required stream data",
                                   inconvertibleErrorCode());

  ArrayRef<object::coff_section> Headers;
  if (Stream->getLength() % sizeof(object::coff_section) != 0)
    return make_error<StringError>(
        "Section header array size is not a multiple of section header size",
        inconvertibleErrorCode());

  uint32_t NumHeaders = Stream->getLength() / sizeof(object::coff_section);
  BinaryStreamReader Reader(*Stream);
  cantFail(Reader.readArray(Headers, NumHeaders));
  return std::make_pair(std::move(Stream), Headers);
}

static Expected<std::vector<std::string>> getSectionNames(PDBFile &File) {
  auto ExpectedHeaders = loadSectionHeaders(File, DbgHeaderType::SectionHdr);
  if (!ExpectedHeaders)
    return ExpectedHeaders.takeError();

  std::unique_ptr<MappedBlockStream> Stream;
  ArrayRef<object::coff_section> Headers;
  std::tie(Stream, Headers) = std::move(*ExpectedHeaders);
  std::vector<std::string> Names;
  for (const auto &H : Headers)
    Names.push_back(H.Name);
  return Names;
}

static void dumpSectionContrib(LinePrinter &P, const SectionContrib &SC,
                               ArrayRef<std::string> SectionNames,
                               uint32_t FieldWidth) {
  std::string NameInsert;
  if (SC.ISect > 0 && SC.ISect <= SectionNames.size()) {
    StringRef SectionName = SectionNames[SC.ISect - 1];
    NameInsert = formatv("[{0}]", SectionName).str();
  } else
    NameInsert = "[???]";
  P.formatLine("SC{5}  | mod = {2}, {0}, size = {1}, data crc = {3}, reloc "
               "crc = {4}",
               formatSegmentOffset(SC.ISect, SC.Off), fmtle(SC.Size),
               fmtle(SC.Imod), fmtle(SC.DataCrc), fmtle(SC.RelocCrc),
               fmt_align(NameInsert, AlignStyle::Left, FieldWidth + 2));
  AutoIndent Indent(P, FieldWidth + 2);
  P.formatLine("      {0}",
               formatSectionCharacteristics(P.getIndentLevel() + 6,
                                            SC.Characteristics, 3, " | "));
}

static void dumpSectionContrib(LinePrinter &P, const SectionContrib2 &SC,
                               ArrayRef<std::string> SectionNames,
                               uint32_t FieldWidth) {
  P.formatLine("SC2[{6}] | mod = {2}, {0}, size = {1}, data crc = {3}, reloc "
               "crc = {4}, coff section = {5}",
               formatSegmentOffset(SC.Base.ISect, SC.Base.Off),
               fmtle(SC.Base.Size), fmtle(SC.Base.Imod), fmtle(SC.Base.DataCrc),
               fmtle(SC.Base.RelocCrc), fmtle(SC.ISectCoff));
  P.formatLine("      {0}",
               formatSectionCharacteristics(P.getIndentLevel() + 6,
                                            SC.Base.Characteristics, 3, " | "));
}

Error DumpOutputStyle::dumpModules() {
  printHeader(P, "Modules");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  if (!getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  AutoIndent Indent(P);

  Expected<DbiStream &> StreamOrErr = getPdb().getPDBDbiStream();
  if (!StreamOrErr)
    return StreamOrErr.takeError();
  DbiStream &Stream = *StreamOrErr;

  const DbiModuleList &Modules = Stream.modules();
  return iterateSymbolGroups(
      File, PrintScope{P, 11},
      [&](uint32_t Modi, const SymbolGroup &Strings) -> Error {
        auto Desc = Modules.getModuleDescriptor(Modi);
        if (opts::dump::DumpSectionContribs) {
          auto SectionsOrErr = getSectionNames(getPdb());
          if (!SectionsOrErr)
            return SectionsOrErr.takeError();
          ArrayRef<std::string> Sections = *SectionsOrErr;
          dumpSectionContrib(P, Desc.getSectionContrib(), Sections, 0);
        }
        P.formatLine("Obj: `{0}`: ", Desc.getObjFileName());
        P.formatLine("debug stream: {0}, # files: {1}, has ec info: {2}",
                     Desc.getModuleStreamIndex(), Desc.getNumberOfFiles(),
                     Desc.hasECInfo());

        auto PdbPathOrErr = Stream.getECName(Desc.getPdbFilePathNameIndex());
        if (!PdbPathOrErr)
          return PdbPathOrErr.takeError();
        StringRef PdbFilePath = *PdbPathOrErr;

        auto SrcPathOrErr = Stream.getECName(Desc.getSourceFileNameIndex());
        if (!SrcPathOrErr)
          return SrcPathOrErr.takeError();
        StringRef SrcFilePath = *SrcPathOrErr;

        P.formatLine("pdb file ni: {0} `{1}`, src file ni: {2} `{3}`",
                     Desc.getPdbFilePathNameIndex(), PdbFilePath,
                     Desc.getSourceFileNameIndex(), SrcFilePath);
        return Error::success();
      });
}

Error DumpOutputStyle::dumpModuleFiles() {
  printHeader(P, "Files");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  if (!getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  return iterateSymbolGroups(
      File, PrintScope{P, 11},
      [this](uint32_t Modi, const SymbolGroup &Strings) -> Error {
        Expected<DbiStream &> StreamOrErr = getPdb().getPDBDbiStream();
        if (!StreamOrErr)
          return StreamOrErr.takeError();
        DbiStream &Stream = *StreamOrErr;

        const DbiModuleList &Modules = Stream.modules();
        for (const auto &F : Modules.source_files(Modi)) {
          Strings.formatFromFileName(P, F);
        }
        return Error::success();
      });
}

Error DumpOutputStyle::dumpSymbolStats() {
  printHeader(P, "Module Stats");

  if (File.isPdb() && !getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  StatCollection SymStats;
  StatCollection ChunkStats;
  PrintScope Scope(P, 2);

  if (Error Err = iterateSymbolGroups(
          File, Scope, [&](uint32_t Modi, const SymbolGroup &SG) -> Error {
            StatCollection SS = getSymbolStats(SG, SymStats);
            StatCollection CS = getChunkStats(SG, ChunkStats);

            if (!SG.getFile().isPdb())
              return Error::success();

            AutoIndent Indent(P);
            auto Modules = cantFail(File.pdb().getPDBDbiStream()).modules();
            uint32_t ModCount = Modules.getModuleCount();
            DbiModuleDescriptor Desc = Modules.getModuleDescriptor(Modi);
            uint32_t StreamIdx = Desc.getModuleStreamIndex();

            if (StreamIdx == kInvalidStreamIndex) {
              P.formatLine(
                  "Mod {0} (debug info not present): [{1}]",
                  fmt_align(Modi, AlignStyle::Right, NumDigits(ModCount)),
                  Desc.getModuleName());
              return Error::success();
            }
            P.formatLine("Stream {0}, {1} bytes", StreamIdx,
                         getPdb().getStreamByteSize(StreamIdx));

            printModuleDetailStats<SymbolKind>(P, "Symbols", SS);
            printModuleDetailStats<DebugSubsectionKind>(P, "Chunks", CS);

            return Error::success();
          }))
    return Err;

  if (SymStats.Totals.Count > 0) {
    P.printLine("  Summary |");
    AutoIndent Indent(P, 4);
    printModuleDetailStats<SymbolKind>(P, "Symbols", SymStats);
    printModuleDetailStats<DebugSubsectionKind>(P, "Chunks", ChunkStats);
  }

  return Error::success();
}

Error DumpOutputStyle::dumpTypeStats() {
  printHeader(P, "Type Record Stats");

  // Iterate the types, categorize by kind, accumulate size stats.
  StatCollection TypeStats;
  LazyRandomTypeCollection &Types =
      opts::dump::DumpTypeStats ? File.types() : File.ids();
  for (std::optional<TypeIndex> TI = Types.getFirst(); TI;
       TI = Types.getNext(*TI)) {
    CVType Type = Types.getType(*TI);
    TypeStats.update(uint32_t(Type.kind()), Type.length());
  }

  P.NewLine();
  P.formatLine("  Types");
  AutoIndent Indent(P);
  P.formatLine("{0,16}: {1,7} entries ({2,12:N} bytes, {3,7} avg)", "Total",
               TypeStats.Totals.Count, TypeStats.Totals.Size,
               (double)TypeStats.Totals.Size / TypeStats.Totals.Count);
  P.formatLine("{0}", fmt_repeat('-', 74));

  for (const auto &K : TypeStats.getStatsSortedBySize()) {
    P.formatLine("{0,16}: {1,7} entries ({2,12:N} bytes, {3,7} avg)",
                 formatTypeLeafKind(TypeLeafKind(K.first)), K.second.Count,
                 K.second.Size, (double)K.second.Size / K.second.Count);
  }
  return Error::success();
}

static bool isValidNamespaceIdentifier(StringRef S) {
  if (S.empty())
    return false;

  if (std::isdigit(S[0]))
    return false;

  return llvm::all_of(S, [](char C) { return std::isalnum(C); });
}

namespace {
constexpr uint32_t kNoneUdtKind = 0;
constexpr uint32_t kSimpleUdtKind = 1;
constexpr uint32_t kUnknownUdtKind = 2;
} // namespace

static std::string getUdtStatLabel(uint32_t Kind) {
  if (Kind == kNoneUdtKind)
    return "<none type>";

  if (Kind == kSimpleUdtKind)
    return "<simple type>";

  if (Kind == kUnknownUdtKind)
    return "<unknown type>";

  return formatTypeLeafKind(static_cast<TypeLeafKind>(Kind));
}

static uint32_t getLongestTypeLeafName(const StatCollection &Stats) {
  size_t L = 0;
  for (const auto &Stat : Stats.Individual) {
    std::string Label = getUdtStatLabel(Stat.first);
    L = std::max(L, Label.size());
  }
  return static_cast<uint32_t>(L);
}

Error DumpOutputStyle::dumpUdtStats() {
  printHeader(P, "S_UDT Record Stats");

  if (File.isPdb() && !getPdb().hasPDBGlobalsStream()) {
    printStreamNotPresent("Globals");
    return Error::success();
  }

  StatCollection UdtStats;
  StatCollection UdtTargetStats;
  AutoIndent Indent(P, 4);

  auto &TpiTypes = File.types();

  StringMap<StatCollection::Stat> NamespacedStats;

  size_t LongestNamespace = 0;
  auto HandleOneSymbol = [&](const CVSymbol &Sym) {
    if (Sym.kind() != SymbolKind::S_UDT)
      return;
    UdtStats.update(SymbolKind::S_UDT, Sym.length());

    UDTSym UDT = cantFail(SymbolDeserializer::deserializeAs<UDTSym>(Sym));

    uint32_t Kind = 0;
    uint32_t RecordSize = 0;

    if (UDT.Type.isNoneType())
      Kind = kNoneUdtKind;
    else if (UDT.Type.isSimple())
      Kind = kSimpleUdtKind;
    else if (std::optional<CVType> T = TpiTypes.tryGetType(UDT.Type)) {
      Kind = T->kind();
      RecordSize = T->length();
    } else
      Kind = kUnknownUdtKind;

    UdtTargetStats.update(Kind, RecordSize);

    size_t Pos = UDT.Name.find("::");
    if (Pos == StringRef::npos)
      return;

    StringRef Scope = UDT.Name.take_front(Pos);
    if (Scope.empty() || !isValidNamespaceIdentifier(Scope))
      return;

    LongestNamespace = std::max(LongestNamespace, Scope.size());
    NamespacedStats[Scope].update(RecordSize);
  };

  P.NewLine();

  if (File.isPdb()) {
    auto &SymbolRecords = cantFail(getPdb().getPDBSymbolStream());
    auto ExpGlobals = getPdb().getPDBGlobalsStream();
    if (!ExpGlobals)
      return ExpGlobals.takeError();

    for (uint32_t PubSymOff : ExpGlobals->getGlobalsTable()) {
      CVSymbol Sym = SymbolRecords.readRecord(PubSymOff);
      HandleOneSymbol(Sym);
    }
  } else {
    for (const auto &Sec : File.symbol_groups()) {
      for (const auto &SS : Sec.getDebugSubsections()) {
        if (SS.kind() != DebugSubsectionKind::Symbols)
          continue;

        DebugSymbolsSubsectionRef Symbols;
        BinaryStreamReader Reader(SS.getRecordData());
        cantFail(Symbols.initialize(Reader));
        for (const auto &S : Symbols)
          HandleOneSymbol(S);
      }
    }
  }

  LongestNamespace += StringRef(" namespace ''").size();
  size_t LongestTypeLeafKind = getLongestTypeLeafName(UdtTargetStats);
  size_t FieldWidth = std::max(LongestNamespace, LongestTypeLeafKind);

  // Compute the max number of digits for count and size fields, including comma
  // separators.
  StringRef CountHeader("Count");
  StringRef SizeHeader("Size");
  size_t CD = NumDigits(UdtStats.Totals.Count);
  CD += (CD - 1) / 3;
  CD = std::max(CD, CountHeader.size());

  size_t SD = NumDigits(UdtStats.Totals.Size);
  SD += (SD - 1) / 3;
  SD = std::max(SD, SizeHeader.size());

  uint32_t TableWidth = FieldWidth + 3 + CD + 2 + SD + 1;

  P.formatLine("{0} | {1}  {2}",
               fmt_align("Record Kind", AlignStyle::Right, FieldWidth),
               fmt_align(CountHeader, AlignStyle::Right, CD),
               fmt_align(SizeHeader, AlignStyle::Right, SD));

  P.formatLine("{0}", fmt_repeat('-', TableWidth));
  for (const auto &Stat : UdtTargetStats.getStatsSortedBySize()) {
    std::string Label = getUdtStatLabel(Stat.first);
    P.formatLine("{0} | {1:N}  {2:N}",
                 fmt_align(Label, AlignStyle::Right, FieldWidth),
                 fmt_align(Stat.second.Count, AlignStyle::Right, CD),
                 fmt_align(Stat.second.Size, AlignStyle::Right, SD));
  }
  P.formatLine("{0}", fmt_repeat('-', TableWidth));
  P.formatLine("{0} | {1:N}  {2:N}",
               fmt_align("Total (S_UDT)", AlignStyle::Right, FieldWidth),
               fmt_align(UdtStats.Totals.Count, AlignStyle::Right, CD),
               fmt_align(UdtStats.Totals.Size, AlignStyle::Right, SD));
  P.formatLine("{0}", fmt_repeat('-', TableWidth));
  struct StrAndStat {
    StringRef Key;
    StatCollection::Stat Stat;
  };

  // Print namespace stats in descending order of size.
  std::vector<StrAndStat> NamespacedStatsSorted;
  for (const auto &Stat : NamespacedStats)
    NamespacedStatsSorted.push_back({Stat.getKey(), Stat.second});
  llvm::stable_sort(NamespacedStatsSorted,
                    [](const StrAndStat &L, const StrAndStat &R) {
                      return L.Stat.Size > R.Stat.Size;
                    });
  for (const auto &Stat : NamespacedStatsSorted) {
    std::string Label = std::string(formatv("namespace '{0}'", Stat.Key));
    P.formatLine("{0} | {1:N}  {2:N}",
                 fmt_align(Label, AlignStyle::Right, FieldWidth),
                 fmt_align(Stat.Stat.Count, AlignStyle::Right, CD),
                 fmt_align(Stat.Stat.Size, AlignStyle::Right, SD));
  }
  return Error::success();
}

static void typesetLinesAndColumns(LinePrinter &P, uint32_t Start,
                                   const LineColumnEntry &E) {
  const uint32_t kMaxCharsPerLineNumber = 4; // 4 digit line number
  uint32_t MinColumnWidth = kMaxCharsPerLineNumber + 5;

  // Let's try to keep it under 100 characters
  constexpr uint32_t kMaxRowLength = 100;
  // At least 3 spaces between columns.
  uint32_t ColumnsPerRow = kMaxRowLength / (MinColumnWidth + 3);
  uint32_t ItemsLeft = E.LineNumbers.size();
  auto LineIter = E.LineNumbers.begin();
  while (ItemsLeft != 0) {
    uint32_t RowColumns = std::min(ItemsLeft, ColumnsPerRow);
    for (uint32_t I = 0; I < RowColumns; ++I) {
      LineInfo Line(LineIter->Flags);
      std::string LineStr;
      if (Line.isAlwaysStepInto())
        LineStr = "ASI";
      else if (Line.isNeverStepInto())
        LineStr = "NSI";
      else
        LineStr = utostr(Line.getStartLine());
      char Statement = Line.isStatement() ? ' ' : '!';
      P.format("{0} {1:X-} {2} ",
               fmt_align(LineStr, AlignStyle::Right, kMaxCharsPerLineNumber),
               fmt_align(Start + LineIter->Offset, AlignStyle::Right, 8, '0'),
               Statement);
      ++LineIter;
      --ItemsLeft;
    }
    P.NewLine();
  }
}

Error DumpOutputStyle::dumpLines() {
  printHeader(P, "Lines");

  if (File.isPdb() && !getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  uint32_t LastModi = UINT32_MAX;
  uint32_t LastNameIndex = UINT32_MAX;
  return iterateModuleSubsections<DebugLinesSubsectionRef>(
      File, PrintScope{P, 4},
      [this, &LastModi,
       &LastNameIndex](uint32_t Modi, const SymbolGroup &Strings,
                       DebugLinesSubsectionRef &Lines) -> Error {
        uint16_t Segment = Lines.header()->RelocSegment;
        uint32_t Begin = Lines.header()->RelocOffset;
        uint32_t End = Begin + Lines.header()->CodeSize;
        for (const auto &Block : Lines) {
          if (LastModi != Modi || LastNameIndex != Block.NameIndex) {
            LastModi = Modi;
            LastNameIndex = Block.NameIndex;
            Strings.formatFromChecksumsOffset(P, Block.NameIndex);
          }

          AutoIndent Indent(P, 2);
          P.formatLine("{0:X-4}:{1:X-8}-{2:X-8}, ", Segment, Begin, End);
          uint32_t Count = Block.LineNumbers.size();
          if (Lines.hasColumnInfo())
            P.format("line/column/addr entries = {0}", Count);
          else
            P.format("line/addr entries = {0}", Count);

          P.NewLine();
          typesetLinesAndColumns(P, Begin, Block);
        }
        return Error::success();
      });
}

Error DumpOutputStyle::dumpInlineeLines() {
  printHeader(P, "Inlinee Lines");

  if (File.isPdb() && !getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  return iterateModuleSubsections<DebugInlineeLinesSubsectionRef>(
      File, PrintScope{P, 2},
      [this](uint32_t Modi, const SymbolGroup &Strings,
             DebugInlineeLinesSubsectionRef &Lines) -> Error {
        P.formatLine("{0,+8} | {1,+5} | {2}", "Inlinee", "Line", "Source File");
        for (const auto &Entry : Lines) {
          P.formatLine("{0,+8} | {1,+5} | ", Entry.Header->Inlinee,
                       fmtle(Entry.Header->SourceLineNum));
          Strings.formatFromChecksumsOffset(P, Entry.Header->FileID, true);
          for (const auto &ExtraFileID : Entry.ExtraFiles) {
            P.formatLine("                   ");
            Strings.formatFromChecksumsOffset(P, ExtraFileID, true);
          }
        }
        P.NewLine();
        return Error::success();
      });
}

Error DumpOutputStyle::dumpXmi() {
  printHeader(P, "Cross Module Imports");

  if (File.isPdb() && !getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  return iterateModuleSubsections<DebugCrossModuleImportsSubsectionRef>(
      File, PrintScope{P, 2},
      [this](uint32_t Modi, const SymbolGroup &Strings,
             DebugCrossModuleImportsSubsectionRef &Imports) -> Error {
        P.formatLine("{0,=32} | {1}", "Imported Module", "Type IDs");

        for (const auto &Xmi : Imports) {
          auto ExpectedModule =
              Strings.getNameFromStringTable(Xmi.Header->ModuleNameOffset);
          StringRef Module;
          SmallString<32> ModuleStorage;
          if (!ExpectedModule) {
            Module = "(unknown module)";
            consumeError(ExpectedModule.takeError());
          } else
            Module = *ExpectedModule;
          if (Module.size() > 32) {
            ModuleStorage = "...";
            ModuleStorage += Module.take_back(32 - 3);
            Module = ModuleStorage;
          }
          std::vector<std::string> TIs;
          for (const auto I : Xmi.Imports)
            TIs.push_back(std::string(formatv("{0,+10:X+}", fmtle(I))));
          std::string Result =
              typesetItemList(TIs, P.getIndentLevel() + 35, 12, " ");
          P.formatLine("{0,+32} | {1}", Module, Result);
        }
        return Error::success();
      });
}

Error DumpOutputStyle::dumpXme() {
  printHeader(P, "Cross Module Exports");

  if (File.isPdb() && !getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  return iterateModuleSubsections<DebugCrossModuleExportsSubsectionRef>(
      File, PrintScope{P, 2},
      [this](uint32_t Modi, const SymbolGroup &Strings,
             DebugCrossModuleExportsSubsectionRef &Exports) -> Error {
        P.formatLine("{0,-10} | {1}", "Local ID", "Global ID");
        for (const auto &Export : Exports) {
          P.formatLine("{0,+10:X+} | {1}", TypeIndex(Export.Local),
                       TypeIndex(Export.Global));
        }
        return Error::success();
      });
}

std::string formatFrameType(object::frame_type FT) {
  switch (FT) {
  case object::frame_type::Fpo:
    return "FPO";
  case object::frame_type::NonFpo:
    return "Non-FPO";
  case object::frame_type::Trap:
    return "Trap";
  case object::frame_type::Tss:
    return "TSS";
  }
  return "<unknown>";
}

Error DumpOutputStyle::dumpOldFpo(PDBFile &File) {
  printHeader(P, "Old FPO Data");

  ExitOnError Err("Error dumping old fpo data:");
  DbiStream &Dbi = Err(File.getPDBDbiStream());

  if (!Dbi.hasOldFpoRecords()) {
    printStreamNotPresent("FPO");
    return Error::success();
  }

  const FixedStreamArray<object::FpoData>& Records = Dbi.getOldFpoRecords();

  P.printLine("  RVA    | Code | Locals | Params | Prolog | Saved Regs | Use "
              "BP | Has SEH | Frame Type");

  for (const object::FpoData &FD : Records) {
    P.formatLine("{0:X-8} | {1,4} | {2,6} | {3,6} | {4,6} | {5,10} | {6,6} | "
                 "{7,7} | {8,9}",
                 uint32_t(FD.Offset), uint32_t(FD.Size), uint32_t(FD.NumLocals),
                 uint32_t(FD.NumParams), FD.getPrologSize(),
                 FD.getNumSavedRegs(), FD.useBP(), FD.hasSEH(),
                 formatFrameType(FD.getFP()));
  }
  return Error::success();
}

Error DumpOutputStyle::dumpNewFpo(PDBFile &File) {
  printHeader(P, "New FPO Data");

  ExitOnError Err("Error dumping new fpo data:");
  DbiStream &Dbi = Err(File.getPDBDbiStream());

  if (!Dbi.hasNewFpoRecords()) {
    printStreamNotPresent("New FPO");
    return Error::success();
  }

  const DebugFrameDataSubsectionRef& FDS = Dbi.getNewFpoRecords();

  P.printLine("  RVA    | Code | Locals | Params | Stack | Prolog | Saved Regs "
              "| Has SEH | Has C++EH | Start | Program");
  for (const FrameData &FD : FDS) {
    bool IsFuncStart = FD.Flags & FrameData::IsFunctionStart;
    bool HasEH = FD.Flags & FrameData::HasEH;
    bool HasSEH = FD.Flags & FrameData::HasSEH;

    auto &StringTable = Err(File.getStringTable());

    auto Program = Err(StringTable.getStringForID(FD.FrameFunc));
    P.formatLine("{0:X-8} | {1,4} | {2,6} | {3,6} | {4,5} | {5,6} | {6,10} | "
                 "{7,7} | {8,9} | {9,5} | {10}",
                 uint32_t(FD.RvaStart), uint32_t(FD.CodeSize),
                 uint32_t(FD.LocalSize), uint32_t(FD.ParamsSize),
                 uint32_t(FD.MaxStackSize), uint16_t(FD.PrologSize),
                 uint16_t(FD.SavedRegsSize), HasSEH, HasEH, IsFuncStart,
                 Program);
  }
  return Error::success();
}

Error DumpOutputStyle::dumpFpo() {
  if (!File.isPdb()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  PDBFile &File = getPdb();
  if (!File.hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  if (auto EC = dumpOldFpo(File))
    return EC;
  if (auto EC = dumpNewFpo(File))
    return EC;
  return Error::success();
}

Error DumpOutputStyle::dumpStringTableFromPdb() {
  AutoIndent Indent(P);
  auto IS = getPdb().getStringTable();
  if (!IS) {
    P.formatLine("Not present in file");
    consumeError(IS.takeError());
    return Error::success();
  }

  if (opts::dump::DumpStringTable) {
    if (IS->name_ids().empty())
      P.formatLine("Empty");
    else {
      auto MaxID = llvm::max_element(IS->name_ids());
      uint32_t Digits = NumDigits(*MaxID);

      P.formatLine("{0} | {1}", fmt_align("ID", AlignStyle::Right, Digits),
                   "String");

      std::vector<uint32_t> SortedIDs(IS->name_ids().begin(),
                                      IS->name_ids().end());
      llvm::sort(SortedIDs);
      for (uint32_t I : SortedIDs) {
        auto ES = IS->getStringForID(I);
        llvm::SmallString<32> Str;
        if (!ES) {
          consumeError(ES.takeError());
          Str = "Error reading string";
        } else if (!ES->empty()) {
          Str.append("'");
          Str.append(*ES);
          Str.append("'");
        }

        if (!Str.empty())
          P.formatLine("{0} | {1}", fmt_align(I, AlignStyle::Right, Digits),
                       Str);
      }
    }
  }

  if (opts::dump::DumpStringTableDetails) {
    P.NewLine();
    {
      P.printLine("String Table Header:");
      AutoIndent Indent(P);
      P.formatLine("Signature: {0}", IS->getSignature());
      P.formatLine("Hash Version: {0}", IS->getHashVersion());
      P.formatLine("Name Buffer Size: {0}", IS->getByteSize());
      P.NewLine();
    }

    BinaryStreamRef NameBuffer = IS->getStringTable().getBuffer();
    ArrayRef<uint8_t> Contents;
    cantFail(NameBuffer.readBytes(0, NameBuffer.getLength(), Contents));
    P.formatBinary("Name Buffer", Contents, 0);
    P.NewLine();
    {
      P.printLine("Hash Table:");
      AutoIndent Indent(P);
      P.formatLine("Bucket Count: {0}", IS->name_ids().size());
      for (const auto &Entry : enumerate(IS->name_ids()))
        P.formatLine("Bucket[{0}] : {1}", Entry.index(),
                     uint32_t(Entry.value()));
      P.formatLine("Name Count: {0}", IS->getNameCount());
    }
  }
  return Error::success();
}

Error DumpOutputStyle::dumpStringTableFromObj() {
  return iterateModuleSubsections<DebugStringTableSubsectionRef>(
      File, PrintScope{P, 4},
      [&](uint32_t Modi, const SymbolGroup &Strings,
          DebugStringTableSubsectionRef &Strings2) -> Error {
        BinaryStreamRef StringTableBuffer = Strings2.getBuffer();
        BinaryStreamReader Reader(StringTableBuffer);
        while (Reader.bytesRemaining() > 0) {
          StringRef Str;
          uint32_t Offset = Reader.getOffset();
          cantFail(Reader.readCString(Str));
          if (Str.empty())
            continue;

          P.formatLine("{0} | {1}", fmt_align(Offset, AlignStyle::Right, 4),
                       Str);
        }
        return Error::success();
      });
}

Error DumpOutputStyle::dumpNamedStreams() {
  printHeader(P, "Named Streams");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  AutoIndent Indent(P);
  ExitOnError Err("Invalid PDB File: ");

  auto &IS = Err(File.pdb().getPDBInfoStream());
  const NamedStreamMap &NS = IS.getNamedStreams();
  for (const auto &Entry : NS.entries()) {
    P.printLine(Entry.getKey());
    AutoIndent Indent2(P, 2);
    P.formatLine("Index: {0}", Entry.getValue());
    P.formatLine("Size in bytes: {0}",
                 File.pdb().getStreamByteSize(Entry.getValue()));
  }

  return Error::success();
}

Error DumpOutputStyle::dumpStringTable() {
  printHeader(P, "String Table");

  if (File.isPdb())
    return dumpStringTableFromPdb();

  return dumpStringTableFromObj();
}

static void buildDepSet(LazyRandomTypeCollection &Types,
                        ArrayRef<TypeIndex> Indices,
                        std::map<TypeIndex, CVType> &DepSet) {
  SmallVector<TypeIndex, 4> DepList;
  for (const auto &I : Indices) {
    TypeIndex TI(I);
    if (DepSet.find(TI) != DepSet.end() || TI.isSimple() || TI.isNoneType())
      continue;

    CVType Type = Types.getType(TI);
    DepSet[TI] = Type;
    codeview::discoverTypeIndices(Type, DepList);
    buildDepSet(Types, DepList, DepSet);
  }
}

static void
dumpFullTypeStream(LinePrinter &Printer, LazyRandomTypeCollection &Types,
                   TypeReferenceTracker *RefTracker, uint32_t NumTypeRecords,
                   uint32_t NumHashBuckets,
                   FixedStreamArray<support::ulittle32_t> HashValues,
                   TpiStream *Stream, bool Bytes, bool Extras) {

  Printer.formatLine("Showing {0:N} records", NumTypeRecords);
  uint32_t Width = NumDigits(TypeIndex::FirstNonSimpleIndex + NumTypeRecords);

  MinimalTypeDumpVisitor V(Printer, Width + 2, Bytes, Extras, Types, RefTracker,
                           NumHashBuckets, HashValues, Stream);

  if (auto EC = codeview::visitTypeStream(Types, V)) {
    Printer.formatLine("An error occurred dumping type records: {0}",
                       toString(std::move(EC)));
  }
}

static void dumpPartialTypeStream(LinePrinter &Printer,
                                  LazyRandomTypeCollection &Types,
                                  TypeReferenceTracker *RefTracker,
                                  TpiStream &Stream, ArrayRef<TypeIndex> TiList,
                                  bool Bytes, bool Extras, bool Deps) {
  uint32_t Width =
      NumDigits(TypeIndex::FirstNonSimpleIndex + Stream.getNumTypeRecords());

  MinimalTypeDumpVisitor V(Printer, Width + 2, Bytes, Extras, Types, RefTracker,
                           Stream.getNumHashBuckets(), Stream.getHashValues(),
                           &Stream);

  if (opts::dump::DumpTypeDependents) {
    // If we need to dump all dependents, then iterate each index and find
    // all dependents, adding them to a map ordered by TypeIndex.
    std::map<TypeIndex, CVType> DepSet;
    buildDepSet(Types, TiList, DepSet);

    Printer.formatLine(
        "Showing {0:N} records and their dependents ({1:N} records total)",
        TiList.size(), DepSet.size());

    for (auto &Dep : DepSet) {
      if (auto EC = codeview::visitTypeRecord(Dep.second, Dep.first, V))
        Printer.formatLine("An error occurred dumping type record {0}: {1}",
                           Dep.first, toString(std::move(EC)));
    }
  } else {
    Printer.formatLine("Showing {0:N} records.", TiList.size());

    for (const auto &I : TiList) {
      TypeIndex TI(I);
      if (TI.isSimple()) {
        Printer.formatLine("{0} | {1}", fmt_align(I, AlignStyle::Right, Width),
                           Types.getTypeName(TI));
      } else if (std::optional<CVType> Type = Types.tryGetType(TI)) {
        if (auto EC = codeview::visitTypeRecord(*Type, TI, V))
          Printer.formatLine("An error occurred dumping type record {0}: {1}",
                             TI, toString(std::move(EC)));
      } else {
        Printer.formatLine("Type {0} doesn't exist in TPI stream", TI);
      }
    }
  }
}

Error DumpOutputStyle::dumpTypesFromObjectFile() {
  LazyRandomTypeCollection Types(100);

  for (const auto &S : getObj().sections()) {
    Expected<StringRef> NameOrErr = S.getName();
    if (!NameOrErr)
      return NameOrErr.takeError();
    StringRef SectionName = *NameOrErr;

    // .debug$T is a standard CodeView type section, while .debug$P is the same
    // format but used for MSVC precompiled header object files.
    if (SectionName == ".debug$T")
      printHeader(P, "Types (.debug$T)");
    else if (SectionName == ".debug$P")
      printHeader(P, "Precompiled Types (.debug$P)");
    else
      continue;

    Expected<StringRef> ContentsOrErr = S.getContents();
    if (!ContentsOrErr)
      return ContentsOrErr.takeError();

    uint32_t Magic;
    BinaryStreamReader Reader(*ContentsOrErr, llvm::endianness::little);
    if (auto EC = Reader.readInteger(Magic))
      return EC;
    if (Magic != COFF::DEBUG_SECTION_MAGIC)
      return make_error<StringError>("Invalid CodeView debug section.",
                                     inconvertibleErrorCode());

    Types.reset(Reader, 100);

    if (opts::dump::DumpTypes) {
      dumpFullTypeStream(P, Types, RefTracker.get(), 0, 0, {}, nullptr,
                         opts::dump::DumpTypeData, false);
    } else if (opts::dump::DumpTypeExtras) {
      auto LocalHashes = LocallyHashedType::hashTypeCollection(Types);
      auto GlobalHashes = GloballyHashedType::hashTypeCollection(Types);
      assert(LocalHashes.size() == GlobalHashes.size());

      P.formatLine("Local / Global hashes:");
      TypeIndex TI(TypeIndex::FirstNonSimpleIndex);
      for (auto H : zip(LocalHashes, GlobalHashes)) {
        AutoIndent Indent2(P);
        LocallyHashedType &L = std::get<0>(H);
        GloballyHashedType &G = std::get<1>(H);

        P.formatLine("TI: {0}, LocalHash: {1:X}, GlobalHash: {2}", TI, L, G);

        ++TI;
      }
      P.NewLine();
    }
  }

  return Error::success();
}

Error DumpOutputStyle::dumpTpiStream(uint32_t StreamIdx) {
  assert(StreamIdx == StreamTPI || StreamIdx == StreamIPI);

  if (StreamIdx == StreamTPI) {
    printHeader(P, "Types (TPI Stream)");
  } else if (StreamIdx == StreamIPI) {
    printHeader(P, "Types (IPI Stream)");
  }

  assert(!File.isObj());

  bool Present = false;
  bool DumpTypes = false;
  bool DumpBytes = false;
  bool DumpExtras = false;
  std::vector<uint32_t> Indices;
  if (StreamIdx == StreamTPI) {
    Present = getPdb().hasPDBTpiStream();
    DumpTypes = opts::dump::DumpTypes;
    DumpBytes = opts::dump::DumpTypeData;
    DumpExtras = opts::dump::DumpTypeExtras;
    Indices.assign(opts::dump::DumpTypeIndex.begin(),
                   opts::dump::DumpTypeIndex.end());
  } else if (StreamIdx == StreamIPI) {
    Present = getPdb().hasPDBIpiStream();
    DumpTypes = opts::dump::DumpIds;
    DumpBytes = opts::dump::DumpIdData;
    DumpExtras = opts::dump::DumpIdExtras;
    Indices.assign(opts::dump::DumpIdIndex.begin(),
                   opts::dump::DumpIdIndex.end());
  }

  if (!Present) {
    printStreamNotPresent(StreamIdx == StreamTPI ? "TPI" : "IPI");
    return Error::success();
  }

  AutoIndent Indent(P);
  ExitOnError Err("Unexpected error processing types: ");

  auto &Stream = Err((StreamIdx == StreamTPI) ? getPdb().getPDBTpiStream()
                                              : getPdb().getPDBIpiStream());

  auto &Types = (StreamIdx == StreamTPI) ? File.types() : File.ids();

  // Only emit notes about referenced/unreferenced for types.
  TypeReferenceTracker *MaybeTracker =
      (StreamIdx == StreamTPI) ? RefTracker.get() : nullptr;

  // Enable resolving forward decls.
  Stream.buildHashMap();

  if (DumpTypes || !Indices.empty()) {
    if (Indices.empty())
      dumpFullTypeStream(P, Types, MaybeTracker, Stream.getNumTypeRecords(),
                         Stream.getNumHashBuckets(), Stream.getHashValues(),
                         &Stream, DumpBytes, DumpExtras);
    else {
      std::vector<TypeIndex> TiList(Indices.begin(), Indices.end());
      dumpPartialTypeStream(P, Types, MaybeTracker, Stream, TiList, DumpBytes,
                            DumpExtras, opts::dump::DumpTypeDependents);
    }
  }

  if (DumpExtras) {
    P.NewLine();

    P.formatLine("Header Version: {0}",
                 static_cast<uint32_t>(Stream.getTpiVersion()));
    P.formatLine("Hash Stream Index: {0}", Stream.getTypeHashStreamIndex());
    P.formatLine("Aux Hash Stream Index: {0}",
                 Stream.getTypeHashStreamAuxIndex());
    P.formatLine("Hash Key Size: {0}", Stream.getHashKeySize());
    P.formatLine("Num Hash Buckets: {0}", Stream.getNumHashBuckets());

    auto IndexOffsets = Stream.getTypeIndexOffsets();
    P.formatLine("Type Index Offsets:");
    for (const auto &IO : IndexOffsets) {
      AutoIndent Indent2(P);
      P.formatLine("TI: {0}, Offset: {1}", IO.Type, fmtle(IO.Offset));
    }

    if (getPdb().hasPDBStringTable()) {
      P.NewLine();
      P.formatLine("Hash Adjusters:");
      auto &Adjusters = Stream.getHashAdjusters();
      auto &Strings = Err(getPdb().getStringTable());
      for (const auto &A : Adjusters) {
        AutoIndent Indent2(P);
        auto ExpectedStr = Strings.getStringForID(A.first);
        TypeIndex TI(A.second);
        if (ExpectedStr)
          P.formatLine("`{0}` -> {1}", *ExpectedStr, TI);
        else {
          P.formatLine("unknown str id ({0}) -> {1}", A.first, TI);
          consumeError(ExpectedStr.takeError());
        }
      }
    }
  }
  return Error::success();
}

Error DumpOutputStyle::dumpModuleSymsForObj() {
  printHeader(P, "Symbols");

  AutoIndent Indent(P);

  auto &Types = File.types();

  SymbolVisitorCallbackPipeline Pipeline;
  SymbolDeserializer Deserializer(nullptr, CodeViewContainer::ObjectFile);
  MinimalSymbolDumper Dumper(P, opts::dump::DumpSymRecordBytes, Types, Types);

  Pipeline.addCallbackToPipeline(Deserializer);
  Pipeline.addCallbackToPipeline(Dumper);
  CVSymbolVisitor Visitor(Pipeline);

  return iterateModuleSubsections<DebugSymbolsSubsectionRef>(
      File, PrintScope{P, 2},
      [&](uint32_t Modi, const SymbolGroup &Strings,
          DebugSymbolsSubsectionRef &Symbols) -> Error {
        Dumper.setSymbolGroup(&Strings);
        for (auto Symbol : Symbols) {
          if (auto EC = Visitor.visitSymbolRecord(Symbol)) {
            return EC;
          }
        }
        return Error::success();
      });
}

Error DumpOutputStyle::dumpModuleSymsForPdb() {
  printHeader(P, "Symbols");

  if (File.isPdb() && !getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  AutoIndent Indent(P);

  auto &Ids = File.ids();
  auto &Types = File.types();

  return iterateSymbolGroups(
      File, PrintScope{P, 2},
      [&](uint32_t I, const SymbolGroup &Strings) -> Error {
        auto ExpectedModS = getModuleDebugStream(File.pdb(), I);
        if (!ExpectedModS) {
          P.formatLine("Error loading module stream {0}.  {1}", I,
                       toString(ExpectedModS.takeError()));
          return Error::success();
        }

        ModuleDebugStreamRef &ModS = *ExpectedModS;

        SymbolVisitorCallbackPipeline Pipeline;
        SymbolDeserializer Deserializer(nullptr, CodeViewContainer::Pdb);
        MinimalSymbolDumper Dumper(P, opts::dump::DumpSymRecordBytes, Strings,
                                   Ids, Types);

        Pipeline.addCallbackToPipeline(Deserializer);
        Pipeline.addCallbackToPipeline(Dumper);
        CVSymbolVisitor Visitor(Pipeline);
        auto SS = ModS.getSymbolsSubstream();
        if (opts::Filters.SymbolOffset) {
          CVSymbolVisitor::FilterOptions Filter;
          Filter.SymbolOffset = opts::Filters.SymbolOffset;
          Filter.ParentRecursiveDepth = opts::Filters.ParentRecurseDepth;
          Filter.ChildRecursiveDepth = opts::Filters.ChildrenRecurseDepth;
          if (auto EC = Visitor.visitSymbolStreamFiltered(ModS.getSymbolArray(),
                                                          Filter)) {
            P.formatLine("Error while processing symbol records.  {0}",
                         toStringWithoutConsuming(EC));
            return EC;
          }
        } else if (auto EC = Visitor.visitSymbolStream(ModS.getSymbolArray(),
                                                       SS.Offset)) {
          P.formatLine("Error while processing symbol records.  {0}",
                       toStringWithoutConsuming(EC));
          return EC;
        }
        return Error::success();
      });
}

Error DumpOutputStyle::dumpTypeRefStats() {
  printHeader(P, "Type Reference Statistics");
  AutoIndent Indent(P);

  // Sum the byte size of all type records, and the size and count of all
  // referenced records.
  size_t TotalRecs = File.types().size();
  size_t RefRecs = 0;
  size_t TotalBytes = 0;
  size_t RefBytes = 0;
  auto &Types = File.types();
  for (std::optional<TypeIndex> TI = Types.getFirst(); TI;
       TI = Types.getNext(*TI)) {
    CVType Type = File.types().getType(*TI);
    TotalBytes += Type.length();
    if (RefTracker->isTypeReferenced(*TI)) {
      ++RefRecs;
      RefBytes += Type.length();
    }
  }

  P.formatLine("Records referenced: {0:N} / {1:N} {2:P}", RefRecs, TotalRecs,
               (double)RefRecs / TotalRecs);
  P.formatLine("Bytes referenced: {0:N} / {1:N} {2:P}", RefBytes, TotalBytes,
               (double)RefBytes / TotalBytes);

  return Error::success();
}

Error DumpOutputStyle::dumpGSIRecords() {
  printHeader(P, "GSI Records");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  if (!getPdb().hasPDBSymbolStream()) {
    printStreamNotPresent("GSI Common Symbol");
    return Error::success();
  }

  AutoIndent Indent(P);

  auto &Records = cantFail(getPdb().getPDBSymbolStream());
  auto &Types = File.types();
  auto &Ids = File.ids();

  P.printLine("Records");
  SymbolVisitorCallbackPipeline Pipeline;
  SymbolDeserializer Deserializer(nullptr, CodeViewContainer::Pdb);
  MinimalSymbolDumper Dumper(P, opts::dump::DumpSymRecordBytes, Ids, Types);

  Pipeline.addCallbackToPipeline(Deserializer);
  Pipeline.addCallbackToPipeline(Dumper);
  CVSymbolVisitor Visitor(Pipeline);

  BinaryStreamRef SymStream = Records.getSymbolArray().getUnderlyingStream();
  if (auto E = Visitor.visitSymbolStream(Records.getSymbolArray(), 0))
    return E;
  return Error::success();
}

Error DumpOutputStyle::dumpGlobals() {
  printHeader(P, "Global Symbols");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  if (!getPdb().hasPDBGlobalsStream()) {
    printStreamNotPresent("Globals");
    return Error::success();
  }

  AutoIndent Indent(P);
  ExitOnError Err("Error dumping globals stream: ");
  auto &Globals = Err(getPdb().getPDBGlobalsStream());

  if (opts::dump::DumpGlobalNames.empty()) {
    const GSIHashTable &Table = Globals.getGlobalsTable();
    Err(dumpSymbolsFromGSI(Table, opts::dump::DumpGlobalExtras));
  } else {
    SymbolStream &SymRecords = cantFail(getPdb().getPDBSymbolStream());
    auto &Types = File.types();
    auto &Ids = File.ids();

    SymbolVisitorCallbackPipeline Pipeline;
    SymbolDeserializer Deserializer(nullptr, CodeViewContainer::Pdb);
    MinimalSymbolDumper Dumper(P, opts::dump::DumpSymRecordBytes, Ids, Types);

    Pipeline.addCallbackToPipeline(Deserializer);
    Pipeline.addCallbackToPipeline(Dumper);
    CVSymbolVisitor Visitor(Pipeline);

    using ResultEntryType = std::pair<uint32_t, CVSymbol>;
    for (StringRef Name : opts::dump::DumpGlobalNames) {
      AutoIndent Indent(P);
      P.formatLine("Global Name `{0}`", Name);
      std::vector<ResultEntryType> Results =
          Globals.findRecordsByName(Name, SymRecords);
      if (Results.empty()) {
        AutoIndent Indent(P);
        P.printLine("(no matching records found)");
        continue;
      }

      for (ResultEntryType Result : Results) {
        if (auto E = Visitor.visitSymbolRecord(Result.second, Result.first))
          return E;
      }
    }
  }
  return Error::success();
}

Error DumpOutputStyle::dumpPublics() {
  printHeader(P, "Public Symbols");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  if (!getPdb().hasPDBPublicsStream()) {
    printStreamNotPresent("Publics");
    return Error::success();
  }

  AutoIndent Indent(P);
  ExitOnError Err("Error dumping publics stream: ");
  auto &Publics = Err(getPdb().getPDBPublicsStream());

  const GSIHashTable &PublicsTable = Publics.getPublicsTable();
  if (opts::dump::DumpPublicExtras) {
    P.printLine("Publics Header");
    AutoIndent Indent(P);
    P.formatLine("sym hash = {0}, thunk table addr = {1}", Publics.getSymHash(),
                 formatSegmentOffset(Publics.getThunkTableSection(),
                                     Publics.getThunkTableOffset()));
  }
  Err(dumpSymbolsFromGSI(PublicsTable, opts::dump::DumpPublicExtras));

  // Skip the rest if we aren't dumping extras.
  if (!opts::dump::DumpPublicExtras)
    return Error::success();

  P.formatLine("Address Map");
  {
    // These are offsets into the publics stream sorted by secidx:secrel.
    AutoIndent Indent2(P);
    for (uint32_t Addr : Publics.getAddressMap())
      P.formatLine("off = {0}", Addr);
  }

  // The thunk map is optional debug info used for ILT thunks.
  if (!Publics.getThunkMap().empty()) {
    P.formatLine("Thunk Map");
    AutoIndent Indent2(P);
    for (uint32_t Addr : Publics.getThunkMap())
      P.formatLine("{0:x8}", Addr);
  }

  // The section offsets table appears to be empty when incremental linking
  // isn't in use.
  if (!Publics.getSectionOffsets().empty()) {
    P.formatLine("Section Offsets");
    AutoIndent Indent2(P);
    for (const SectionOffset &SO : Publics.getSectionOffsets())
      P.formatLine("{0:x4}:{1:x8}", uint16_t(SO.Isect), uint32_t(SO.Off));
  }

  return Error::success();
}

Error DumpOutputStyle::dumpSymbolsFromGSI(const GSIHashTable &Table,
                                          bool HashExtras) {
  auto ExpectedSyms = getPdb().getPDBSymbolStream();
  if (!ExpectedSyms)
    return ExpectedSyms.takeError();
  auto &Types = File.types();
  auto &Ids = File.ids();

  if (HashExtras) {
    P.printLine("GSI Header");
    AutoIndent Indent(P);
    P.formatLine("sig = {0:X}, hdr = {1:X}, hr size = {2}, num buckets = {3}",
                 Table.getVerSignature(), Table.getVerHeader(),
                 Table.getHashRecordSize(), Table.getNumBuckets());
  }

  {
    P.printLine("Records");
    SymbolVisitorCallbackPipeline Pipeline;
    SymbolDeserializer Deserializer(nullptr, CodeViewContainer::Pdb);
    MinimalSymbolDumper Dumper(P, opts::dump::DumpSymRecordBytes, Ids, Types);

    Pipeline.addCallbackToPipeline(Deserializer);
    Pipeline.addCallbackToPipeline(Dumper);
    CVSymbolVisitor Visitor(Pipeline);


    BinaryStreamRef SymStream =
        ExpectedSyms->getSymbolArray().getUnderlyingStream();
    for (uint32_t PubSymOff : Table) {
      Expected<CVSymbol> Sym = readSymbolFromStream(SymStream, PubSymOff);
      if (!Sym)
        return Sym.takeError();
      if (auto E = Visitor.visitSymbolRecord(*Sym, PubSymOff))
        return E;
    }
  }

  // Return early if we aren't dumping public hash table and address map info.
  if (HashExtras) {
    P.formatLine("Hash Entries");
    {
      AutoIndent Indent2(P);
      for (const PSHashRecord &HR : Table.HashRecords)
        P.formatLine("off = {0}, refcnt = {1}", uint32_t(HR.Off),
          uint32_t(HR.CRef));
    }

    P.formatLine("Hash Buckets");
    {
      AutoIndent Indent2(P);
      for (uint32_t Hash : Table.HashBuckets)
        P.formatLine("{0:x8}", Hash);
    }
  }

  return Error::success();
}

static std::string formatSegMapDescriptorFlag(uint32_t IndentLevel,
                                              OMFSegDescFlags Flags) {
  std::vector<std::string> Opts;
  if (Flags == OMFSegDescFlags::None)
    return "none";

  PUSH_FLAG(OMFSegDescFlags, Read, Flags, "read");
  PUSH_FLAG(OMFSegDescFlags, Write, Flags, "write");
  PUSH_FLAG(OMFSegDescFlags, Execute, Flags, "execute");
  PUSH_FLAG(OMFSegDescFlags, AddressIs32Bit, Flags, "32 bit addr");
  PUSH_FLAG(OMFSegDescFlags, IsSelector, Flags, "selector");
  PUSH_FLAG(OMFSegDescFlags, IsAbsoluteAddress, Flags, "absolute addr");
  PUSH_FLAG(OMFSegDescFlags, IsGroup, Flags, "group");
  return typesetItemList(Opts, IndentLevel, 4, " | ");
}

Error DumpOutputStyle::dumpSectionHeaders() {
  dumpSectionHeaders("Section Headers", DbgHeaderType::SectionHdr);
  dumpSectionHeaders("Original Section Headers", DbgHeaderType::SectionHdrOrig);
  return Error::success();
}

void DumpOutputStyle::dumpSectionHeaders(StringRef Label, DbgHeaderType Type) {
  printHeader(P, Label);

  if (File.isObj()) {
    printStreamNotValidForObj();
    return;
  }

  if (!getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return;
  }

  AutoIndent Indent(P);
  ExitOnError Err("Error dumping section headers: ");
  std::unique_ptr<MappedBlockStream> Stream;
  ArrayRef<object::coff_section> Headers;
  auto ExpectedHeaders = loadSectionHeaders(getPdb(), Type);
  if (!ExpectedHeaders) {
    P.printLine(toString(ExpectedHeaders.takeError()));
    return;
  }
  std::tie(Stream, Headers) = std::move(*ExpectedHeaders);

  uint32_t I = 1;
  for (const auto &Header : Headers) {
    P.NewLine();
    P.formatLine("SECTION HEADER #{0}", I);
    P.formatLine("{0,8} name", Header.Name);
    P.formatLine("{0,8:X-} virtual size", uint32_t(Header.VirtualSize));
    P.formatLine("{0,8:X-} virtual address", uint32_t(Header.VirtualAddress));
    P.formatLine("{0,8:X-} size of raw data", uint32_t(Header.SizeOfRawData));
    P.formatLine("{0,8:X-} file pointer to raw data",
                 uint32_t(Header.PointerToRawData));
    P.formatLine("{0,8:X-} file pointer to relocation table",
                 uint32_t(Header.PointerToRelocations));
    P.formatLine("{0,8:X-} file pointer to line numbers",
                 uint32_t(Header.PointerToLinenumbers));
    P.formatLine("{0,8:X-} number of relocations",
                 uint32_t(Header.NumberOfRelocations));
    P.formatLine("{0,8:X-} number of line numbers",
                 uint32_t(Header.NumberOfLinenumbers));
    P.formatLine("{0,8:X-} flags", uint32_t(Header.Characteristics));
    AutoIndent IndentMore(P, 9);
    P.formatLine("{0}", formatSectionCharacteristics(
                            P.getIndentLevel(), Header.Characteristics, 1, ""));
    ++I;
  }
}

Error DumpOutputStyle::dumpSectionContribs() {
  printHeader(P, "Section Contributions");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  if (!getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  AutoIndent Indent(P);
  ExitOnError Err("Error dumping section contributions: ");

  DbiStream &Dbi = Err(getPdb().getPDBDbiStream());

  class Visitor : public ISectionContribVisitor {
  public:
    Visitor(LinePrinter &P, ArrayRef<std::string> Names) : P(P), Names(Names) {
      auto Max = llvm::max_element(Names, [](StringRef S1, StringRef S2) {
        return S1.size() < S2.size();
      });
      MaxNameLen = (Max == Names.end() ? 0 : Max->size());
    }
    void visit(const SectionContrib &SC) override {
      dumpSectionContrib(P, SC, Names, MaxNameLen);
    }
    void visit(const SectionContrib2 &SC) override {
      dumpSectionContrib(P, SC, Names, MaxNameLen);
    }

  private:
    LinePrinter &P;
    uint32_t MaxNameLen;
    ArrayRef<std::string> Names;
  };

  auto NamesOrErr = getSectionNames(getPdb());
  if (!NamesOrErr)
    return NamesOrErr.takeError();
  ArrayRef<std::string> Names = *NamesOrErr;
  Visitor V(P, Names);
  Dbi.visitSectionContributions(V);
  return Error::success();
}

Error DumpOutputStyle::dumpSectionMap() {
  printHeader(P, "Section Map");

  if (File.isObj()) {
    printStreamNotValidForObj();
    return Error::success();
  }

  if (!getPdb().hasPDBDbiStream()) {
    printStreamNotPresent("DBI");
    return Error::success();
  }

  AutoIndent Indent(P);
  ExitOnError Err("Error dumping section map: ");

  DbiStream &Dbi = Err(getPdb().getPDBDbiStream());

  uint32_t I = 0;
  for (auto &M : Dbi.getSectionMap()) {
    P.formatLine(
        "Section {0:4} | ovl = {1}, group = {2}, frame = {3}, name = {4}", I,
        fmtle(M.Ovl), fmtle(M.Group), fmtle(M.Frame), fmtle(M.SecName));
    P.formatLine("               class = {0}, offset = {1}, size = {2}",
                 fmtle(M.ClassName), fmtle(M.Offset), fmtle(M.SecByteLength));
    P.formatLine("               flags = {0}",
                 formatSegMapDescriptorFlag(
                     P.getIndentLevel() + 13,
                     static_cast<OMFSegDescFlags>(uint16_t(M.Flags))));
    ++I;
  }
  return Error::success();
}
