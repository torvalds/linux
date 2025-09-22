//===-- sancov.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file is a command-line tool for reading and analyzing sanitizer
// coverage.
//===----------------------------------------------------------------------===//
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <vector>

using namespace llvm;

namespace {

// Command-line option boilerplate.
namespace {
using namespace llvm::opt;
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

class SancovOptTable : public opt::GenericOptTable {
public:
  SancovOptTable() : GenericOptTable(InfoTable) {}
};
} // namespace

// --------- COMMAND LINE FLAGS ---------

enum ActionType {
  CoveredFunctionsAction,
  HtmlReportAction,
  MergeAction,
  NotCoveredFunctionsAction,
  PrintAction,
  PrintCovPointsAction,
  StatsAction,
  SymbolizeAction
};

static ActionType Action;
static std::vector<std::string> ClInputFiles;
static bool ClDemangle;
static bool ClSkipDeadFiles;
static bool ClUseDefaultIgnorelist;
static std::string ClStripPathPrefix;
static std::string ClIgnorelist;

static const char *const DefaultIgnorelistStr = "fun:__sanitizer_.*\n"
                                                "src:/usr/include/.*\n"
                                                "src:.*/libc\\+\\+/.*\n";

// --------- FORMAT SPECIFICATION ---------

struct FileHeader {
  uint32_t Bitness;
  uint32_t Magic;
};

static const uint32_t BinCoverageMagic = 0xC0BFFFFF;
static const uint32_t Bitness32 = 0xFFFFFF32;
static const uint32_t Bitness64 = 0xFFFFFF64;

static const Regex SancovFileRegex("(.*)\\.[0-9]+\\.sancov");
static const Regex SymcovFileRegex(".*\\.symcov");

// --------- MAIN DATASTRUCTURES ----------

// Contents of .sancov file: list of coverage point addresses that were
// executed.
struct RawCoverage {
  explicit RawCoverage(std::unique_ptr<std::set<uint64_t>> Addrs)
      : Addrs(std::move(Addrs)) {}

  // Read binary .sancov file.
  static ErrorOr<std::unique_ptr<RawCoverage>>
  read(const std::string &FileName);

  std::unique_ptr<std::set<uint64_t>> Addrs;
};

// Coverage point has an opaque Id and corresponds to multiple source locations.
struct CoveragePoint {
  explicit CoveragePoint(const std::string &Id) : Id(Id) {}

  std::string Id;
  SmallVector<DILineInfo, 1> Locs;
};

// Symcov file content: set of covered Ids plus information about all available
// coverage points.
struct SymbolizedCoverage {
  // Read json .symcov file.
  static std::unique_ptr<SymbolizedCoverage> read(const std::string &InputFile);

  std::set<std::string> CoveredIds;
  std::string BinaryHash;
  std::vector<CoveragePoint> Points;
};

struct CoverageStats {
  size_t AllPoints;
  size_t CovPoints;
  size_t AllFns;
  size_t CovFns;
};

// --------- ERROR HANDLING ---------

static void fail(const llvm::Twine &E) {
  errs() << "ERROR: " << E << "\n";
  exit(1);
}

static void failIf(bool B, const llvm::Twine &E) {
  if (B)
    fail(E);
}

static void failIfError(std::error_code Error) {
  if (!Error)
    return;
  errs() << "ERROR: " << Error.message() << "(" << Error.value() << ")\n";
  exit(1);
}

template <typename T> static void failIfError(const ErrorOr<T> &E) {
  failIfError(E.getError());
}

static void failIfError(Error Err) {
  if (Err) {
    logAllUnhandledErrors(std::move(Err), errs(), "ERROR: ");
    exit(1);
  }
}

template <typename T> static void failIfError(Expected<T> &E) {
  failIfError(E.takeError());
}

static void failIfNotEmpty(const llvm::Twine &E) {
  if (E.str().empty())
    return;
  fail(E);
}

template <typename T>
static void failIfEmpty(const std::unique_ptr<T> &Ptr,
                        const std::string &Message) {
  if (Ptr.get())
    return;
  fail(Message);
}

// ----------- Coverage I/O ----------
template <typename T>
static void readInts(const char *Start, const char *End,
                     std::set<uint64_t> *Ints) {
  const T *S = reinterpret_cast<const T *>(Start);
  const T *E = reinterpret_cast<const T *>(End);
  std::copy(S, E, std::inserter(*Ints, Ints->end()));
}

ErrorOr<std::unique_ptr<RawCoverage>>
RawCoverage::read(const std::string &FileName) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(FileName);
  if (!BufOrErr)
    return BufOrErr.getError();
  std::unique_ptr<MemoryBuffer> Buf = std::move(BufOrErr.get());
  if (Buf->getBufferSize() < 8) {
    errs() << "File too small (<8): " << Buf->getBufferSize() << '\n';
    return make_error_code(errc::illegal_byte_sequence);
  }
  const FileHeader *Header =
      reinterpret_cast<const FileHeader *>(Buf->getBufferStart());

  if (Header->Magic != BinCoverageMagic) {
    errs() << "Wrong magic: " << Header->Magic << '\n';
    return make_error_code(errc::illegal_byte_sequence);
  }

  auto Addrs = std::make_unique<std::set<uint64_t>>();

  switch (Header->Bitness) {
  case Bitness64:
    readInts<uint64_t>(Buf->getBufferStart() + 8, Buf->getBufferEnd(),
                       Addrs.get());
    break;
  case Bitness32:
    readInts<uint32_t>(Buf->getBufferStart() + 8, Buf->getBufferEnd(),
                       Addrs.get());
    break;
  default:
    errs() << "Unsupported bitness: " << Header->Bitness << '\n';
    return make_error_code(errc::illegal_byte_sequence);
  }

  // Ignore slots that are zero, so a runtime implementation is not required
  // to compactify the data.
  Addrs->erase(0);

  return std::make_unique<RawCoverage>(std::move(Addrs));
}

// Print coverage addresses.
raw_ostream &operator<<(raw_ostream &OS, const RawCoverage &CoverageData) {
  for (auto Addr : *CoverageData.Addrs) {
    OS << "0x";
    OS.write_hex(Addr);
    OS << "\n";
  }
  return OS;
}

static raw_ostream &operator<<(raw_ostream &OS, const CoverageStats &Stats) {
  OS << "all-edges: " << Stats.AllPoints << "\n";
  OS << "cov-edges: " << Stats.CovPoints << "\n";
  OS << "all-functions: " << Stats.AllFns << "\n";
  OS << "cov-functions: " << Stats.CovFns << "\n";
  return OS;
}

// Output symbolized information for coverage points in JSON.
// Format:
// {
//   '<file_name>' : {
//     '<function_name>' : {
//       '<point_id'> : '<line_number>:'<column_number'.
//          ....
//       }
//    }
// }
static void operator<<(json::OStream &W,
                       const std::vector<CoveragePoint> &Points) {
  // Group points by file.
  std::map<std::string, std::vector<const CoveragePoint *>> PointsByFile;
  for (const auto &Point : Points) {
    for (const DILineInfo &Loc : Point.Locs) {
      PointsByFile[Loc.FileName].push_back(&Point);
    }
  }

  for (const auto &P : PointsByFile) {
    std::string FileName = P.first;
    std::map<std::string, std::vector<const CoveragePoint *>> PointsByFn;
    for (auto PointPtr : P.second) {
      for (const DILineInfo &Loc : PointPtr->Locs) {
        PointsByFn[Loc.FunctionName].push_back(PointPtr);
      }
    }

    W.attributeObject(P.first, [&] {
      // Group points by function.
      for (const auto &P : PointsByFn) {
        std::string FunctionName = P.first;
        std::set<std::string> WrittenIds;

        W.attributeObject(FunctionName, [&] {
          for (const CoveragePoint *Point : P.second) {
            for (const auto &Loc : Point->Locs) {
              if (Loc.FileName != FileName || Loc.FunctionName != FunctionName)
                continue;
              if (WrittenIds.find(Point->Id) != WrittenIds.end())
                continue;

              // Output <point_id> : "<line>:<col>".
              WrittenIds.insert(Point->Id);
              W.attribute(Point->Id,
                          (utostr(Loc.Line) + ":" + utostr(Loc.Column)));
            }
          }
        });
      }
    });
  }
}

static void operator<<(json::OStream &W, const SymbolizedCoverage &C) {
  W.object([&] {
    W.attributeArray("covered-points", [&] {
      for (const std::string &P : C.CoveredIds) {
        W.value(P);
      }
    });
    W.attribute("binary-hash", C.BinaryHash);
    W.attributeObject("point-symbol-info", [&] { W << C.Points; });
  });
}

static std::string parseScalarString(yaml::Node *N) {
  SmallString<64> StringStorage;
  yaml::ScalarNode *S = dyn_cast<yaml::ScalarNode>(N);
  failIf(!S, "expected string");
  return std::string(S->getValue(StringStorage));
}

std::unique_ptr<SymbolizedCoverage>
SymbolizedCoverage::read(const std::string &InputFile) {
  auto Coverage(std::make_unique<SymbolizedCoverage>());

  std::map<std::string, CoveragePoint> Points;
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(InputFile);
  failIfError(BufOrErr);

  SourceMgr SM;
  yaml::Stream S(**BufOrErr, SM);

  yaml::document_iterator DI = S.begin();
  failIf(DI == S.end(), "empty document: " + InputFile);
  yaml::Node *Root = DI->getRoot();
  failIf(!Root, "expecting root node: " + InputFile);
  yaml::MappingNode *Top = dyn_cast<yaml::MappingNode>(Root);
  failIf(!Top, "expecting mapping node: " + InputFile);

  for (auto &KVNode : *Top) {
    auto Key = parseScalarString(KVNode.getKey());

    if (Key == "covered-points") {
      yaml::SequenceNode *Points =
          dyn_cast<yaml::SequenceNode>(KVNode.getValue());
      failIf(!Points, "expected array: " + InputFile);

      for (auto I = Points->begin(), E = Points->end(); I != E; ++I) {
        Coverage->CoveredIds.insert(parseScalarString(&*I));
      }
    } else if (Key == "binary-hash") {
      Coverage->BinaryHash = parseScalarString(KVNode.getValue());
    } else if (Key == "point-symbol-info") {
      yaml::MappingNode *PointSymbolInfo =
          dyn_cast<yaml::MappingNode>(KVNode.getValue());
      failIf(!PointSymbolInfo, "expected mapping node: " + InputFile);

      for (auto &FileKVNode : *PointSymbolInfo) {
        auto Filename = parseScalarString(FileKVNode.getKey());

        yaml::MappingNode *FileInfo =
            dyn_cast<yaml::MappingNode>(FileKVNode.getValue());
        failIf(!FileInfo, "expected mapping node: " + InputFile);

        for (auto &FunctionKVNode : *FileInfo) {
          auto FunctionName = parseScalarString(FunctionKVNode.getKey());

          yaml::MappingNode *FunctionInfo =
              dyn_cast<yaml::MappingNode>(FunctionKVNode.getValue());
          failIf(!FunctionInfo, "expected mapping node: " + InputFile);

          for (auto &PointKVNode : *FunctionInfo) {
            auto PointId = parseScalarString(PointKVNode.getKey());
            auto Loc = parseScalarString(PointKVNode.getValue());

            size_t ColonPos = Loc.find(':');
            failIf(ColonPos == std::string::npos, "expected ':': " + InputFile);

            auto LineStr = Loc.substr(0, ColonPos);
            auto ColStr = Loc.substr(ColonPos + 1, Loc.size());

            if (Points.find(PointId) == Points.end())
              Points.insert(std::make_pair(PointId, CoveragePoint(PointId)));

            DILineInfo LineInfo;
            LineInfo.FileName = Filename;
            LineInfo.FunctionName = FunctionName;
            char *End;
            LineInfo.Line = std::strtoul(LineStr.c_str(), &End, 10);
            LineInfo.Column = std::strtoul(ColStr.c_str(), &End, 10);

            CoveragePoint *CoveragePoint = &Points.find(PointId)->second;
            CoveragePoint->Locs.push_back(LineInfo);
          }
        }
      }
    } else {
      errs() << "Ignoring unknown key: " << Key << "\n";
    }
  }

  for (auto &KV : Points) {
    Coverage->Points.push_back(KV.second);
  }

  return Coverage;
}

// ---------- MAIN FUNCTIONALITY ----------

std::string stripPathPrefix(std::string Path) {
  if (ClStripPathPrefix.empty())
    return Path;
  size_t Pos = Path.find(ClStripPathPrefix);
  if (Pos == std::string::npos)
    return Path;
  return Path.substr(Pos + ClStripPathPrefix.size());
}

static std::unique_ptr<symbolize::LLVMSymbolizer> createSymbolizer() {
  symbolize::LLVMSymbolizer::Options SymbolizerOptions;
  SymbolizerOptions.Demangle = ClDemangle;
  SymbolizerOptions.UseSymbolTable = true;
  return std::make_unique<symbolize::LLVMSymbolizer>(SymbolizerOptions);
}

static std::string normalizeFilename(const std::string &FileName) {
  SmallString<256> S(FileName);
  sys::path::remove_dots(S, /* remove_dot_dot */ true);
  return stripPathPrefix(sys::path::convert_to_slash(std::string(S)));
}

class Ignorelists {
public:
  Ignorelists()
      : DefaultIgnorelist(createDefaultIgnorelist()),
        UserIgnorelist(createUserIgnorelist()) {}

  bool isIgnorelisted(const DILineInfo &I) {
    if (DefaultIgnorelist &&
        DefaultIgnorelist->inSection("sancov", "fun", I.FunctionName))
      return true;
    if (DefaultIgnorelist &&
        DefaultIgnorelist->inSection("sancov", "src", I.FileName))
      return true;
    if (UserIgnorelist &&
        UserIgnorelist->inSection("sancov", "fun", I.FunctionName))
      return true;
    if (UserIgnorelist &&
        UserIgnorelist->inSection("sancov", "src", I.FileName))
      return true;
    return false;
  }

private:
  static std::unique_ptr<SpecialCaseList> createDefaultIgnorelist() {
    if (!ClUseDefaultIgnorelist)
      return std::unique_ptr<SpecialCaseList>();
    std::unique_ptr<MemoryBuffer> MB =
        MemoryBuffer::getMemBuffer(DefaultIgnorelistStr);
    std::string Error;
    auto Ignorelist = SpecialCaseList::create(MB.get(), Error);
    failIfNotEmpty(Error);
    return Ignorelist;
  }

  static std::unique_ptr<SpecialCaseList> createUserIgnorelist() {
    if (ClIgnorelist.empty())
      return std::unique_ptr<SpecialCaseList>();
    return SpecialCaseList::createOrDie({{ClIgnorelist}},
                                        *vfs::getRealFileSystem());
  }
  std::unique_ptr<SpecialCaseList> DefaultIgnorelist;
  std::unique_ptr<SpecialCaseList> UserIgnorelist;
};

static std::vector<CoveragePoint>
getCoveragePoints(const std::string &ObjectFile,
                  const std::set<uint64_t> &Addrs,
                  const std::set<uint64_t> &CoveredAddrs) {
  std::vector<CoveragePoint> Result;
  auto Symbolizer(createSymbolizer());
  Ignorelists Ig;

  std::set<std::string> CoveredFiles;
  if (ClSkipDeadFiles) {
    for (auto Addr : CoveredAddrs) {
      // TODO: it would be neccessary to set proper section index here.
      // object::SectionedAddress::UndefSection works for only absolute
      // addresses.
      object::SectionedAddress ModuleAddress = {
          Addr, object::SectionedAddress::UndefSection};

      auto LineInfo = Symbolizer->symbolizeCode(ObjectFile, ModuleAddress);
      failIfError(LineInfo);
      CoveredFiles.insert(LineInfo->FileName);
      auto InliningInfo =
          Symbolizer->symbolizeInlinedCode(ObjectFile, ModuleAddress);
      failIfError(InliningInfo);
      for (uint32_t I = 0; I < InliningInfo->getNumberOfFrames(); ++I) {
        auto FrameInfo = InliningInfo->getFrame(I);
        CoveredFiles.insert(FrameInfo.FileName);
      }
    }
  }

  for (auto Addr : Addrs) {
    std::set<DILineInfo> Infos; // deduplicate debug info.

    // TODO: it would be neccessary to set proper section index here.
    // object::SectionedAddress::UndefSection works for only absolute addresses.
    object::SectionedAddress ModuleAddress = {
        Addr, object::SectionedAddress::UndefSection};

    auto LineInfo = Symbolizer->symbolizeCode(ObjectFile, ModuleAddress);
    failIfError(LineInfo);
    if (ClSkipDeadFiles &&
        CoveredFiles.find(LineInfo->FileName) == CoveredFiles.end())
      continue;
    LineInfo->FileName = normalizeFilename(LineInfo->FileName);
    if (Ig.isIgnorelisted(*LineInfo))
      continue;

    auto Id = utohexstr(Addr, true);
    auto Point = CoveragePoint(Id);
    Infos.insert(*LineInfo);
    Point.Locs.push_back(*LineInfo);

    auto InliningInfo =
        Symbolizer->symbolizeInlinedCode(ObjectFile, ModuleAddress);
    failIfError(InliningInfo);
    for (uint32_t I = 0; I < InliningInfo->getNumberOfFrames(); ++I) {
      auto FrameInfo = InliningInfo->getFrame(I);
      if (ClSkipDeadFiles &&
          CoveredFiles.find(FrameInfo.FileName) == CoveredFiles.end())
        continue;
      FrameInfo.FileName = normalizeFilename(FrameInfo.FileName);
      if (Ig.isIgnorelisted(FrameInfo))
        continue;
      if (Infos.find(FrameInfo) == Infos.end()) {
        Infos.insert(FrameInfo);
        Point.Locs.push_back(FrameInfo);
      }
    }

    Result.push_back(Point);
  }

  return Result;
}

static bool isCoveragePointSymbol(StringRef Name) {
  return Name == "__sanitizer_cov" || Name == "__sanitizer_cov_with_check" ||
         Name == "__sanitizer_cov_trace_func_enter" ||
         Name == "__sanitizer_cov_trace_pc_guard" ||
         // Mac has '___' prefix
         Name == "___sanitizer_cov" || Name == "___sanitizer_cov_with_check" ||
         Name == "___sanitizer_cov_trace_func_enter" ||
         Name == "___sanitizer_cov_trace_pc_guard";
}

// Locate __sanitizer_cov* function addresses inside the stubs table on MachO.
static void findMachOIndirectCovFunctions(const object::MachOObjectFile &O,
                                          std::set<uint64_t> *Result) {
  MachO::dysymtab_command Dysymtab = O.getDysymtabLoadCommand();
  MachO::symtab_command Symtab = O.getSymtabLoadCommand();

  for (const auto &Load : O.load_commands()) {
    if (Load.C.cmd == MachO::LC_SEGMENT_64) {
      MachO::segment_command_64 Seg = O.getSegment64LoadCommand(Load);
      for (unsigned J = 0; J < Seg.nsects; ++J) {
        MachO::section_64 Sec = O.getSection64(Load, J);

        uint32_t SectionType = Sec.flags & MachO::SECTION_TYPE;
        if (SectionType == MachO::S_SYMBOL_STUBS) {
          uint32_t Stride = Sec.reserved2;
          uint32_t Cnt = Sec.size / Stride;
          uint32_t N = Sec.reserved1;
          for (uint32_t J = 0; J < Cnt && N + J < Dysymtab.nindirectsyms; J++) {
            uint32_t IndirectSymbol =
                O.getIndirectSymbolTableEntry(Dysymtab, N + J);
            uint64_t Addr = Sec.addr + J * Stride;
            if (IndirectSymbol < Symtab.nsyms) {
              object::SymbolRef Symbol = *(O.getSymbolByIndex(IndirectSymbol));
              Expected<StringRef> Name = Symbol.getName();
              failIfError(Name);
              if (isCoveragePointSymbol(Name.get())) {
                Result->insert(Addr);
              }
            }
          }
        }
      }
    }
    if (Load.C.cmd == MachO::LC_SEGMENT) {
      errs() << "ERROR: 32 bit MachO binaries not supported\n";
    }
  }
}

// Locate __sanitizer_cov* function addresses that are used for coverage
// reporting.
static std::set<uint64_t>
findSanitizerCovFunctions(const object::ObjectFile &O) {
  std::set<uint64_t> Result;

  for (const object::SymbolRef &Symbol : O.symbols()) {
    Expected<uint64_t> AddressOrErr = Symbol.getAddress();
    failIfError(AddressOrErr);
    uint64_t Address = AddressOrErr.get();

    Expected<StringRef> NameOrErr = Symbol.getName();
    failIfError(NameOrErr);
    StringRef Name = NameOrErr.get();

    Expected<uint32_t> FlagsOrErr = Symbol.getFlags();
    // TODO: Test this error.
    failIfError(FlagsOrErr);
    uint32_t Flags = FlagsOrErr.get();

    if (!(Flags & object::BasicSymbolRef::SF_Undefined) &&
        isCoveragePointSymbol(Name)) {
      Result.insert(Address);
    }
  }

  if (const auto *CO = dyn_cast<object::COFFObjectFile>(&O)) {
    for (const object::ExportDirectoryEntryRef &Export :
         CO->export_directories()) {
      uint32_t RVA;
      failIfError(Export.getExportRVA(RVA));

      StringRef Name;
      failIfError(Export.getSymbolName(Name));

      if (isCoveragePointSymbol(Name))
        Result.insert(CO->getImageBase() + RVA);
    }
  }

  if (const auto *MO = dyn_cast<object::MachOObjectFile>(&O)) {
    findMachOIndirectCovFunctions(*MO, &Result);
  }

  return Result;
}

// Ported from
// compiler-rt/lib/sanitizer_common/sanitizer_stacktrace.h:GetPreviousInstructionPc
// GetPreviousInstructionPc.
static uint64_t getPreviousInstructionPc(uint64_t PC, Triple TheTriple) {
  if (TheTriple.isARM())
    return (PC - 3) & (~1);
  if (TheTriple.isMIPS() || TheTriple.isSPARC())
    return PC - 8;
  if (TheTriple.isRISCV())
    return PC - 2;
  if (TheTriple.isX86() || TheTriple.isSystemZ())
    return PC - 1;
  return PC - 4;
}

// Locate addresses of all coverage points in a file. Coverage point
// is defined as the 'address of instruction following __sanitizer_cov
// call - 1'.
static void getObjectCoveragePoints(const object::ObjectFile &O,
                                    std::set<uint64_t> *Addrs) {
  Triple TheTriple("unknown-unknown-unknown");
  TheTriple.setArch(Triple::ArchType(O.getArch()));
  auto TripleName = TheTriple.getTriple();

  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TripleName, Error);
  failIfNotEmpty(Error);

  std::unique_ptr<const MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, "", ""));
  failIfEmpty(STI, "no subtarget info for target " + TripleName);

  std::unique_ptr<const MCRegisterInfo> MRI(
      TheTarget->createMCRegInfo(TripleName));
  failIfEmpty(MRI, "no register info for target " + TripleName);

  MCTargetOptions MCOptions;
  std::unique_ptr<const MCAsmInfo> AsmInfo(
      TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  failIfEmpty(AsmInfo, "no asm info for target " + TripleName);

  MCContext Ctx(TheTriple, AsmInfo.get(), MRI.get(), STI.get());
  std::unique_ptr<MCDisassembler> DisAsm(
      TheTarget->createMCDisassembler(*STI, Ctx));
  failIfEmpty(DisAsm, "no disassembler info for target " + TripleName);

  std::unique_ptr<const MCInstrInfo> MII(TheTarget->createMCInstrInfo());
  failIfEmpty(MII, "no instruction info for target " + TripleName);

  std::unique_ptr<const MCInstrAnalysis> MIA(
      TheTarget->createMCInstrAnalysis(MII.get()));
  failIfEmpty(MIA, "no instruction analysis info for target " + TripleName);

  auto SanCovAddrs = findSanitizerCovFunctions(O);
  if (SanCovAddrs.empty())
    fail("__sanitizer_cov* functions not found");

  for (object::SectionRef Section : O.sections()) {
    if (Section.isVirtual() || !Section.isText()) // llvm-objdump does the same.
      continue;
    uint64_t SectionAddr = Section.getAddress();
    uint64_t SectSize = Section.getSize();
    if (!SectSize)
      continue;

    Expected<StringRef> BytesStr = Section.getContents();
    failIfError(BytesStr);
    ArrayRef<uint8_t> Bytes = arrayRefFromStringRef(*BytesStr);

    for (uint64_t Index = 0, Size = 0; Index < Section.getSize();
         Index += Size) {
      MCInst Inst;
      ArrayRef<uint8_t> ThisBytes = Bytes.slice(Index);
      uint64_t ThisAddr = SectionAddr + Index;
      if (!DisAsm->getInstruction(Inst, Size, ThisBytes, ThisAddr, nulls())) {
        if (Size == 0)
          Size = std::min<uint64_t>(
              ThisBytes.size(),
              DisAsm->suggestBytesToSkip(ThisBytes, ThisAddr));
        continue;
      }
      uint64_t Addr = Index + SectionAddr;
      // Sanitizer coverage uses the address of the next instruction - 1.
      uint64_t CovPoint = getPreviousInstructionPc(Addr + Size, TheTriple);
      uint64_t Target;
      if (MIA->isCall(Inst) &&
          MIA->evaluateBranch(Inst, SectionAddr + Index, Size, Target) &&
          SanCovAddrs.find(Target) != SanCovAddrs.end())
        Addrs->insert(CovPoint);
    }
  }
}

static void
visitObjectFiles(const object::Archive &A,
                 function_ref<void(const object::ObjectFile &)> Fn) {
  Error Err = Error::success();
  for (auto &C : A.children(Err)) {
    Expected<std::unique_ptr<object::Binary>> ChildOrErr = C.getAsBinary();
    failIfError(ChildOrErr);
    if (auto *O = dyn_cast<object::ObjectFile>(&*ChildOrErr.get()))
      Fn(*O);
    else
      failIfError(object::object_error::invalid_file_type);
  }
  failIfError(std::move(Err));
}

static void
visitObjectFiles(const std::string &FileName,
                 function_ref<void(const object::ObjectFile &)> Fn) {
  Expected<object::OwningBinary<object::Binary>> BinaryOrErr =
      object::createBinary(FileName);
  if (!BinaryOrErr)
    failIfError(BinaryOrErr);

  object::Binary &Binary = *BinaryOrErr.get().getBinary();
  if (object::Archive *A = dyn_cast<object::Archive>(&Binary))
    visitObjectFiles(*A, Fn);
  else if (object::ObjectFile *O = dyn_cast<object::ObjectFile>(&Binary))
    Fn(*O);
  else
    failIfError(object::object_error::invalid_file_type);
}

static std::set<uint64_t>
findSanitizerCovFunctions(const std::string &FileName) {
  std::set<uint64_t> Result;
  visitObjectFiles(FileName, [&](const object::ObjectFile &O) {
    auto Addrs = findSanitizerCovFunctions(O);
    Result.insert(Addrs.begin(), Addrs.end());
  });
  return Result;
}

// Locate addresses of all coverage points in a file. Coverage point
// is defined as the 'address of instruction following __sanitizer_cov
// call - 1'.
static std::set<uint64_t> findCoveragePointAddrs(const std::string &FileName) {
  std::set<uint64_t> Result;
  visitObjectFiles(FileName, [&](const object::ObjectFile &O) {
    getObjectCoveragePoints(O, &Result);
  });
  return Result;
}

static void printCovPoints(const std::string &ObjFile, raw_ostream &OS) {
  for (uint64_t Addr : findCoveragePointAddrs(ObjFile)) {
    OS << "0x";
    OS.write_hex(Addr);
    OS << "\n";
  }
}

static ErrorOr<bool> isCoverageFile(const std::string &FileName) {
  auto ShortFileName = llvm::sys::path::filename(FileName);
  if (!SancovFileRegex.match(ShortFileName))
    return false;

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(FileName);
  if (!BufOrErr) {
    errs() << "Warning: " << BufOrErr.getError().message() << "("
           << BufOrErr.getError().value()
           << "), filename: " << llvm::sys::path::filename(FileName) << "\n";
    return BufOrErr.getError();
  }
  std::unique_ptr<MemoryBuffer> Buf = std::move(BufOrErr.get());
  if (Buf->getBufferSize() < 8) {
    return false;
  }
  const FileHeader *Header =
      reinterpret_cast<const FileHeader *>(Buf->getBufferStart());
  return Header->Magic == BinCoverageMagic;
}

static bool isSymbolizedCoverageFile(const std::string &FileName) {
  auto ShortFileName = llvm::sys::path::filename(FileName);
  return SymcovFileRegex.match(ShortFileName);
}

static std::unique_ptr<SymbolizedCoverage>
symbolize(const RawCoverage &Data, const std::string ObjectFile) {
  auto Coverage = std::make_unique<SymbolizedCoverage>();

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(ObjectFile);
  failIfError(BufOrErr);
  SHA1 Hasher;
  Hasher.update((*BufOrErr)->getBuffer());
  Coverage->BinaryHash = toHex(Hasher.final());

  Ignorelists Ig;
  auto Symbolizer(createSymbolizer());

  for (uint64_t Addr : *Data.Addrs) {
    // TODO: it would be neccessary to set proper section index here.
    // object::SectionedAddress::UndefSection works for only absolute addresses.
    auto LineInfo = Symbolizer->symbolizeCode(
        ObjectFile, {Addr, object::SectionedAddress::UndefSection});
    failIfError(LineInfo);
    if (Ig.isIgnorelisted(*LineInfo))
      continue;

    Coverage->CoveredIds.insert(utohexstr(Addr, true));
  }

  std::set<uint64_t> AllAddrs = findCoveragePointAddrs(ObjectFile);
  if (!std::includes(AllAddrs.begin(), AllAddrs.end(), Data.Addrs->begin(),
                     Data.Addrs->end())) {
    fail("Coverage points in binary and .sancov file do not match.");
  }
  Coverage->Points = getCoveragePoints(ObjectFile, AllAddrs, *Data.Addrs);
  return Coverage;
}

struct FileFn {
  bool operator<(const FileFn &RHS) const {
    return std::tie(FileName, FunctionName) <
           std::tie(RHS.FileName, RHS.FunctionName);
  }

  std::string FileName;
  std::string FunctionName;
};

static std::set<FileFn>
computeFunctions(const std::vector<CoveragePoint> &Points) {
  std::set<FileFn> Fns;
  for (const auto &Point : Points) {
    for (const auto &Loc : Point.Locs) {
      Fns.insert(FileFn{Loc.FileName, Loc.FunctionName});
    }
  }
  return Fns;
}

static std::set<FileFn>
computeNotCoveredFunctions(const SymbolizedCoverage &Coverage) {
  auto Fns = computeFunctions(Coverage.Points);

  for (const auto &Point : Coverage.Points) {
    if (Coverage.CoveredIds.find(Point.Id) == Coverage.CoveredIds.end())
      continue;

    for (const auto &Loc : Point.Locs) {
      Fns.erase(FileFn{Loc.FileName, Loc.FunctionName});
    }
  }

  return Fns;
}

static std::set<FileFn>
computeCoveredFunctions(const SymbolizedCoverage &Coverage) {
  auto AllFns = computeFunctions(Coverage.Points);
  std::set<FileFn> Result;

  for (const auto &Point : Coverage.Points) {
    if (Coverage.CoveredIds.find(Point.Id) == Coverage.CoveredIds.end())
      continue;

    for (const auto &Loc : Point.Locs) {
      Result.insert(FileFn{Loc.FileName, Loc.FunctionName});
    }
  }

  return Result;
}

typedef std::map<FileFn, std::pair<uint32_t, uint32_t>> FunctionLocs;
// finds first location in a file for each function.
static FunctionLocs resolveFunctions(const SymbolizedCoverage &Coverage,
                                     const std::set<FileFn> &Fns) {
  FunctionLocs Result;
  for (const auto &Point : Coverage.Points) {
    for (const auto &Loc : Point.Locs) {
      FileFn Fn = FileFn{Loc.FileName, Loc.FunctionName};
      if (Fns.find(Fn) == Fns.end())
        continue;

      auto P = std::make_pair(Loc.Line, Loc.Column);
      auto I = Result.find(Fn);
      if (I == Result.end() || I->second > P) {
        Result[Fn] = P;
      }
    }
  }
  return Result;
}

static void printFunctionLocs(const FunctionLocs &FnLocs, raw_ostream &OS) {
  for (const auto &P : FnLocs) {
    OS << stripPathPrefix(P.first.FileName) << ":" << P.second.first << " "
       << P.first.FunctionName << "\n";
  }
}
CoverageStats computeStats(const SymbolizedCoverage &Coverage) {
  CoverageStats Stats = {Coverage.Points.size(), Coverage.CoveredIds.size(),
                         computeFunctions(Coverage.Points).size(),
                         computeCoveredFunctions(Coverage).size()};
  return Stats;
}

// Print list of covered functions.
// Line format: <file_name>:<line> <function_name>
static void printCoveredFunctions(const SymbolizedCoverage &CovData,
                                  raw_ostream &OS) {
  auto CoveredFns = computeCoveredFunctions(CovData);
  printFunctionLocs(resolveFunctions(CovData, CoveredFns), OS);
}

// Print list of not covered functions.
// Line format: <file_name>:<line> <function_name>
static void printNotCoveredFunctions(const SymbolizedCoverage &CovData,
                                     raw_ostream &OS) {
  auto NotCoveredFns = computeNotCoveredFunctions(CovData);
  printFunctionLocs(resolveFunctions(CovData, NotCoveredFns), OS);
}

// Read list of files and merges their coverage info.
static void readAndPrintRawCoverage(const std::vector<std::string> &FileNames,
                                    raw_ostream &OS) {
  std::vector<std::unique_ptr<RawCoverage>> Covs;
  for (const auto &FileName : FileNames) {
    auto Cov = RawCoverage::read(FileName);
    if (!Cov)
      continue;
    OS << *Cov.get();
  }
}

static std::unique_ptr<SymbolizedCoverage>
merge(const std::vector<std::unique_ptr<SymbolizedCoverage>> &Coverages) {
  if (Coverages.empty())
    return nullptr;

  auto Result = std::make_unique<SymbolizedCoverage>();

  for (size_t I = 0; I < Coverages.size(); ++I) {
    const SymbolizedCoverage &Coverage = *Coverages[I];
    std::string Prefix;
    if (Coverages.size() > 1) {
      // prefix is not needed when there's only one file.
      Prefix = utostr(I);
    }

    for (const auto &Id : Coverage.CoveredIds) {
      Result->CoveredIds.insert(Prefix + Id);
    }

    for (const auto &CovPoint : Coverage.Points) {
      CoveragePoint NewPoint(CovPoint);
      NewPoint.Id = Prefix + CovPoint.Id;
      Result->Points.push_back(NewPoint);
    }
  }

  if (Coverages.size() == 1) {
    Result->BinaryHash = Coverages[0]->BinaryHash;
  }

  return Result;
}

static std::unique_ptr<SymbolizedCoverage>
readSymbolizeAndMergeCmdArguments(std::vector<std::string> FileNames) {
  std::vector<std::unique_ptr<SymbolizedCoverage>> Coverages;

  {
    // Short name => file name.
    std::map<std::string, std::string> ObjFiles;
    std::string FirstObjFile;
    std::set<std::string> CovFiles;

    // Partition input values into coverage/object files.
    for (const auto &FileName : FileNames) {
      if (isSymbolizedCoverageFile(FileName)) {
        Coverages.push_back(SymbolizedCoverage::read(FileName));
      }

      auto ErrorOrIsCoverage = isCoverageFile(FileName);
      if (!ErrorOrIsCoverage)
        continue;
      if (ErrorOrIsCoverage.get()) {
        CovFiles.insert(FileName);
      } else {
        auto ShortFileName = llvm::sys::path::filename(FileName);
        if (ObjFiles.find(std::string(ShortFileName)) != ObjFiles.end()) {
          fail("Duplicate binary file with a short name: " + ShortFileName);
        }

        ObjFiles[std::string(ShortFileName)] = FileName;
        if (FirstObjFile.empty())
          FirstObjFile = FileName;
      }
    }

    SmallVector<StringRef, 2> Components;

    // Object file => list of corresponding coverage file names.
    std::map<std::string, std::vector<std::string>> CoverageByObjFile;
    for (const auto &FileName : CovFiles) {
      auto ShortFileName = llvm::sys::path::filename(FileName);
      auto Ok = SancovFileRegex.match(ShortFileName, &Components);
      if (!Ok) {
        fail("Can't match coverage file name against "
             "<module_name>.<pid>.sancov pattern: " +
             FileName);
      }

      auto Iter = ObjFiles.find(std::string(Components[1]));
      if (Iter == ObjFiles.end()) {
        fail("Object file for coverage not found: " + FileName);
      }

      CoverageByObjFile[Iter->second].push_back(FileName);
    };

    for (const auto &Pair : ObjFiles) {
      auto FileName = Pair.second;
      if (CoverageByObjFile.find(FileName) == CoverageByObjFile.end())
        errs() << "WARNING: No coverage file for " << FileName << "\n";
    }

    // Read raw coverage and symbolize it.
    for (const auto &Pair : CoverageByObjFile) {
      if (findSanitizerCovFunctions(Pair.first).empty()) {
        errs()
            << "WARNING: Ignoring " << Pair.first
            << " and its coverage because  __sanitizer_cov* functions were not "
               "found.\n";
        continue;
      }

      for (const std::string &CoverageFile : Pair.second) {
        auto DataOrError = RawCoverage::read(CoverageFile);
        failIfError(DataOrError);
        Coverages.push_back(symbolize(*DataOrError.get(), Pair.first));
      }
    }
  }

  return merge(Coverages);
}

} // namespace

static void parseArgs(int Argc, char **Argv) {
  SancovOptTable Tbl;
  llvm::BumpPtrAllocator A;
  llvm::StringSaver Saver{A};
  opt::InputArgList Args =
      Tbl.parseArgs(Argc, Argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) {
        llvm::errs() << Msg << '\n';
        std::exit(1);
      });

  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(
        llvm::outs(),
        "sancov [options] <action> <binary files...> <.sancov files...> "
        "<.symcov files...>",
        "Sanitizer Coverage Processing Tool (sancov)\n\n"
        "  This tool can extract various coverage-related information from: \n"
        "  coverage-instrumented binary files, raw .sancov files and their "
        "symbolized .symcov version.\n"
        "  Depending on chosen action the tool expects different input files:\n"
        "    -print-coverage-pcs     - coverage-instrumented binary files\n"
        "    -print-coverage         - .sancov files\n"
        "    <other actions>         - .sancov files & corresponding binary "
        "files, .symcov files\n");
    std::exit(0);
  }

  if (Args.hasArg(OPT_version)) {
    cl::PrintVersionMessage();
    std::exit(0);
  }

  if (Args.hasMultipleArgs(OPT_action_grp)) {
    fail("Only one action option is allowed");
  }

  for (const opt::Arg *A : Args.filtered(OPT_INPUT)) {
    ClInputFiles.emplace_back(A->getValue());
  }

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_action_grp)) {
    switch (A->getOption().getID()) {
    case OPT_print:
      Action = ActionType::PrintAction;
      break;
    case OPT_printCoveragePcs:
      Action = ActionType::PrintCovPointsAction;
      break;
    case OPT_coveredFunctions:
      Action = ActionType::CoveredFunctionsAction;
      break;
    case OPT_notCoveredFunctions:
      Action = ActionType::NotCoveredFunctionsAction;
      break;
    case OPT_printCoverageStats:
      Action = ActionType::StatsAction;
      break;
    case OPT_htmlReport:
      Action = ActionType::HtmlReportAction;
      break;
    case OPT_symbolize:
      Action = ActionType::SymbolizeAction;
      break;
    case OPT_merge:
      Action = ActionType::MergeAction;
      break;
    default:
      fail("Invalid Action");
    }
  }

  ClDemangle = Args.hasFlag(OPT_demangle, OPT_no_demangle, true);
  ClSkipDeadFiles = Args.hasFlag(OPT_skipDeadFiles, OPT_no_skipDeadFiles, true);
  ClUseDefaultIgnorelist =
      Args.hasFlag(OPT_useDefaultIgnoreList, OPT_no_useDefaultIgnoreList, true);

  ClStripPathPrefix = Args.getLastArgValue(OPT_stripPathPrefix_EQ);
  ClIgnorelist = Args.getLastArgValue(OPT_ignorelist_EQ);
}

int sancov_main(int Argc, char **Argv, const llvm::ToolContext &) {
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllDisassemblers();

  parseArgs(Argc, Argv);

  // -print doesn't need object files.
  if (Action == PrintAction) {
    readAndPrintRawCoverage(ClInputFiles, outs());
    return 0;
  }
  if (Action == PrintCovPointsAction) {
    // -print-coverage-points doesn't need coverage files.
    for (const std::string &ObjFile : ClInputFiles) {
      printCovPoints(ObjFile, outs());
    }
    return 0;
  }

  auto Coverage = readSymbolizeAndMergeCmdArguments(ClInputFiles);
  failIf(!Coverage, "No valid coverage files given.");

  switch (Action) {
  case CoveredFunctionsAction: {
    printCoveredFunctions(*Coverage, outs());
    return 0;
  }
  case NotCoveredFunctionsAction: {
    printNotCoveredFunctions(*Coverage, outs());
    return 0;
  }
  case StatsAction: {
    outs() << computeStats(*Coverage);
    return 0;
  }
  case MergeAction:
  case SymbolizeAction: { // merge & symbolize are synonims.
    json::OStream W(outs(), 2);
    W << *Coverage;
    return 0;
  }
  case HtmlReportAction:
    errs() << "-html-report option is removed: "
              "use -symbolize & coverage-report-server.py instead\n";
    return 1;
  case PrintAction:
  case PrintCovPointsAction:
    llvm_unreachable("unsupported action");
  }

  return 0;
}
