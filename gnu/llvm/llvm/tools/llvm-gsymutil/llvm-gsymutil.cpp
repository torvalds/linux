//===-- gsymutil.cpp - GSYM dumping and creation utility for llvm ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstring>
#include <inttypes.h>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "llvm/DebugInfo/GSYM/DwarfTransformer.h"
#include "llvm/DebugInfo/GSYM/FunctionInfo.h"
#include "llvm/DebugInfo/GSYM/GsymCreator.h"
#include "llvm/DebugInfo/GSYM/GsymReader.h"
#include "llvm/DebugInfo/GSYM/InlineInfo.h"
#include "llvm/DebugInfo/GSYM/LookupResult.h"
#include "llvm/DebugInfo/GSYM/ObjectFileTransformer.h"
#include "llvm/DebugInfo/GSYM/OutputAggregator.h"
#include <optional>

using namespace llvm;
using namespace gsym;
using namespace object;

/// @}
/// Command line options.
/// @{

using namespace llvm::opt;
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  constexpr llvm::StringLiteral NAME##_init[] = VALUE;                         \
  constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                          \
      NAME##_init, std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

const opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class GSYMUtilOptTable : public llvm::opt::GenericOptTable {
public:
  GSYMUtilOptTable() : GenericOptTable(InfoTable) {
    setGroupedShortOptions(true);
  }
};

static bool Verbose;
static std::vector<std::string> InputFilenames;
static std::string ConvertFilename;
static std::vector<std::string> ArchFilters;
static std::string OutputFilename;
static std::string JsonSummaryFile;
static bool Verify;
static unsigned NumThreads;
static uint64_t SegmentSize;
static bool Quiet;
static std::vector<uint64_t> LookupAddresses;
static bool LookupAddressesFromStdin;

static void parseArgs(int argc, char **argv) {
  GSYMUtilOptTable Tbl;
  llvm::StringRef ToolName = argv[0];
  llvm::BumpPtrAllocator A;
  llvm::StringSaver Saver{A};
  llvm::opt::InputArgList Args =
      Tbl.parseArgs(argc, argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) {
        llvm::errs() << Msg << '\n';
        std::exit(1);
      });
  if (Args.hasArg(OPT_help)) {
    const char *Overview =
        "A tool for dumping, searching and creating GSYM files.\n\n"
        "Specify one or more GSYM paths as arguments to dump all of the "
        "information in each GSYM file.\n"
        "Specify a single GSYM file along with one or more --lookup options to "
        "lookup addresses within that GSYM file.\n"
        "Use the --convert option to specify a file with option --out-file "
        "option to convert to GSYM format.\n";

    Tbl.printHelp(llvm::outs(), "llvm-gsymutil [options] <input GSYM files>",
                  Overview);
    std::exit(0);
  }
  if (Args.hasArg(OPT_version)) {
    llvm::outs() << ToolName << '\n';
    cl::PrintVersionMessage();
    std::exit(0);
  }

  Verbose = Args.hasArg(OPT_verbose);

  for (const llvm::opt::Arg *A : Args.filtered(OPT_INPUT))
    InputFilenames.emplace_back(A->getValue());

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_convert_EQ))
    ConvertFilename = A->getValue();

  for (const llvm::opt::Arg *A : Args.filtered(OPT_arch_EQ))
    ArchFilters.emplace_back(A->getValue());

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_out_file_EQ))
    OutputFilename = A->getValue();

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_json_summary_file_EQ))
    JsonSummaryFile = A->getValue();

  Verify = Args.hasArg(OPT_verify);

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_num_threads_EQ)) {
    StringRef S{A->getValue()};
    if (!llvm::to_integer(S, NumThreads, 0)) {
      llvm::errs() << ToolName << ": for the --num-threads option: '" << S
                   << "' value invalid for uint argument!\n";
      std::exit(1);
    }
  }

  if (const llvm::opt::Arg *A = Args.getLastArg(OPT_segment_size_EQ)) {
    StringRef S{A->getValue()};
    if (!llvm::to_integer(S, SegmentSize, 0)) {
      llvm::errs() << ToolName << ": for the --segment-size option: '" << S
                   << "' value invalid for uint argument!\n";
      std::exit(1);
    }
  }

  Quiet = Args.hasArg(OPT_quiet);

  for (const llvm::opt::Arg *A : Args.filtered(OPT_address_EQ)) {
    StringRef S{A->getValue()};
    if (!llvm::to_integer(S, LookupAddresses.emplace_back(), 0)) {
      llvm::errs() << ToolName << ": for the --address option: '" << S
                   << "' value invalid for uint argument!\n";
      std::exit(1);
    }
  }

  LookupAddressesFromStdin = Args.hasArg(OPT_addresses_from_stdin);
}

/// @}
//===----------------------------------------------------------------------===//

static void error(Error Err) {
  if (!Err)
    return;
  WithColor::error() << toString(std::move(Err)) << "\n";
  exit(1);
}

static void error(StringRef Prefix, llvm::Error Err) {
  if (!Err)
    return;
  errs() << Prefix << ": " << Err << "\n";
  consumeError(std::move(Err));
  exit(1);
}

static void error(StringRef Prefix, std::error_code EC) {
  if (!EC)
    return;
  errs() << Prefix << ": " << EC.message() << "\n";
  exit(1);
}

static uint32_t getCPUType(MachOObjectFile &MachO) {
  if (MachO.is64Bit())
    return MachO.getHeader64().cputype;
  else
    return MachO.getHeader().cputype;
}

/// Return true if the object file has not been filtered by an --arch option.
static bool filterArch(MachOObjectFile &Obj) {
  if (ArchFilters.empty())
    return true;

  Triple ObjTriple(Obj.getArchTriple());
  StringRef ObjArch = ObjTriple.getArchName();

  for (StringRef Arch : ArchFilters) {
    // Match name.
    if (Arch == ObjArch)
      return true;

    // Match architecture number.
    unsigned Value;
    if (!Arch.getAsInteger(0, Value))
      if (Value == getCPUType(Obj))
        return true;
  }
  return false;
}

/// Determine the virtual address that is considered the base address of an ELF
/// object file.
///
/// The base address of an ELF file is the "p_vaddr" of the first program
/// header whose "p_type" is PT_LOAD.
///
/// \param ELFFile An ELF object file we will search.
///
/// \returns A valid image base address if we are able to extract one.
template <class ELFT>
static std::optional<uint64_t>
getImageBaseAddress(const object::ELFFile<ELFT> &ELFFile) {
  auto PhdrRangeOrErr = ELFFile.program_headers();
  if (!PhdrRangeOrErr) {
    consumeError(PhdrRangeOrErr.takeError());
    return std::nullopt;
  }
  for (const typename ELFT::Phdr &Phdr : *PhdrRangeOrErr)
    if (Phdr.p_type == ELF::PT_LOAD)
      return (uint64_t)Phdr.p_vaddr;
  return std::nullopt;
}

/// Determine the virtual address that is considered the base address of mach-o
/// object file.
///
/// The base address of a mach-o file is the vmaddr of the  "__TEXT" segment.
///
/// \param MachO A mach-o object file we will search.
///
/// \returns A valid image base address if we are able to extract one.
static std::optional<uint64_t>
getImageBaseAddress(const object::MachOObjectFile *MachO) {
  for (const auto &Command : MachO->load_commands()) {
    if (Command.C.cmd == MachO::LC_SEGMENT) {
      MachO::segment_command SLC = MachO->getSegmentLoadCommand(Command);
      StringRef SegName = SLC.segname;
      if (SegName == "__TEXT")
        return SLC.vmaddr;
    } else if (Command.C.cmd == MachO::LC_SEGMENT_64) {
      MachO::segment_command_64 SLC = MachO->getSegment64LoadCommand(Command);
      StringRef SegName = SLC.segname;
      if (SegName == "__TEXT")
        return SLC.vmaddr;
    }
  }
  return std::nullopt;
}

/// Determine the virtual address that is considered the base address of an
/// object file.
///
/// Since GSYM files are used for symbolication, many clients will need to
/// easily adjust addresses they find in stack traces so the lookups happen
/// on unslid addresses from the original object file. If the base address of
/// a GSYM file is set to the base address of the image, then this address
/// adjusting is much easier.
///
/// \param Obj An object file we will search.
///
/// \returns A valid image base address if we are able to extract one.
static std::optional<uint64_t> getImageBaseAddress(object::ObjectFile &Obj) {
  if (const auto *MachO = dyn_cast<object::MachOObjectFile>(&Obj))
    return getImageBaseAddress(MachO);
  else if (const auto *ELFObj = dyn_cast<object::ELF32LEObjectFile>(&Obj))
    return getImageBaseAddress(ELFObj->getELFFile());
  else if (const auto *ELFObj = dyn_cast<object::ELF32BEObjectFile>(&Obj))
    return getImageBaseAddress(ELFObj->getELFFile());
  else if (const auto *ELFObj = dyn_cast<object::ELF64LEObjectFile>(&Obj))
    return getImageBaseAddress(ELFObj->getELFFile());
  else if (const auto *ELFObj = dyn_cast<object::ELF64BEObjectFile>(&Obj))
    return getImageBaseAddress(ELFObj->getELFFile());
  return std::nullopt;
}

static llvm::Error handleObjectFile(ObjectFile &Obj, const std::string &OutFile,
                                    OutputAggregator &Out) {
  auto ThreadCount =
      NumThreads > 0 ? NumThreads : std::thread::hardware_concurrency();

  GsymCreator Gsym(Quiet);

  // See if we can figure out the base address for a given object file, and if
  // we can, then set the base address to use to this value. This will ease
  // symbolication since clients can slide the GSYM lookup addresses by using
  // the load bias of the shared library.
  if (auto ImageBaseAddr = getImageBaseAddress(Obj))
    Gsym.setBaseAddress(*ImageBaseAddr);

  // We need to know where the valid sections are that contain instructions.
  // See header documentation for DWARFTransformer::SetValidTextRanges() for
  // defails.
  AddressRanges TextRanges;
  for (const object::SectionRef &Sect : Obj.sections()) {
    if (!Sect.isText())
      continue;
    const uint64_t Size = Sect.getSize();
    if (Size == 0)
      continue;
    const uint64_t StartAddr = Sect.getAddress();
    TextRanges.insert(AddressRange(StartAddr, StartAddr + Size));
  }

  // Make sure there is DWARF to convert first.
  std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(
      Obj,
      /*RelocAction=*/DWARFContext::ProcessDebugRelocations::Process,
      nullptr,
      /*DWPName=*/"",
      /*RecoverableErrorHandler=*/WithColor::defaultErrorHandler,
      /*WarningHandler=*/WithColor::defaultWarningHandler,
      /*ThreadSafe*/true);
  if (!DICtx)
    return createStringError(std::errc::invalid_argument,
                             "unable to create DWARF context");

  // Make a DWARF transformer object and populate the ranges of the code
  // so we don't end up adding invalid functions to GSYM data.
  DwarfTransformer DT(*DICtx, Gsym);
  if (!TextRanges.empty())
    Gsym.SetValidTextRanges(TextRanges);

  // Convert all DWARF to GSYM.
  if (auto Err = DT.convert(ThreadCount, Out))
    return Err;

  // Get the UUID and convert symbol table to GSYM.
  if (auto Err = ObjectFileTransformer::convert(Obj, Out, Gsym))
    return Err;

  // Finalize the GSYM to make it ready to save to disk. This will remove
  // duplicate FunctionInfo entries where we might have found an entry from
  // debug info and also a symbol table entry from the object file.
  if (auto Err = Gsym.finalize(Out))
    return Err;

  // Save the GSYM file to disk.
  llvm::endianness Endian = Obj.makeTriple().isLittleEndian()
                                ? llvm::endianness::little
                                : llvm::endianness::big;

  std::optional<uint64_t> OptSegmentSize;
  if (SegmentSize > 0)
    OptSegmentSize = SegmentSize;
  if (auto Err = Gsym.save(OutFile, Endian, OptSegmentSize))
    return Err;

  // Verify the DWARF if requested. This will ensure all the info in the DWARF
  // can be looked up in the GSYM and that all lookups get matching data.
  if (Verify) {
    if (auto Err = DT.verify(OutFile, Out))
      return Err;
  }

  return Error::success();
}

static llvm::Error handleBuffer(StringRef Filename, MemoryBufferRef Buffer,
                                const std::string &OutFile,
                                OutputAggregator &Out) {
  Expected<std::unique_ptr<Binary>> BinOrErr = object::createBinary(Buffer);
  error(Filename, errorToErrorCode(BinOrErr.takeError()));

  if (auto *Obj = dyn_cast<ObjectFile>(BinOrErr->get())) {
    Triple ObjTriple(Obj->makeTriple());
    auto ArchName = ObjTriple.getArchName();
    outs() << "Output file (" << ArchName << "): " << OutFile << "\n";
    if (auto Err = handleObjectFile(*Obj, OutFile, Out))
      return Err;
  } else if (auto *Fat = dyn_cast<MachOUniversalBinary>(BinOrErr->get())) {
    // Iterate over all contained architectures and filter out any that were
    // not specified with the "--arch <arch>" option. If the --arch option was
    // not specified on the command line, we will process all architectures.
    std::vector<std::unique_ptr<MachOObjectFile>> FilterObjs;
    for (auto &ObjForArch : Fat->objects()) {
      if (auto MachOOrErr = ObjForArch.getAsObjectFile()) {
        auto &Obj = **MachOOrErr;
        if (filterArch(Obj))
          FilterObjs.emplace_back(MachOOrErr->release());
      } else {
        error(Filename, MachOOrErr.takeError());
      }
    }
    if (FilterObjs.empty())
      error(Filename, createStringError(std::errc::invalid_argument,
                                        "no matching architectures found"));

    // Now handle each architecture we need to convert.
    for (auto &Obj : FilterObjs) {
      Triple ObjTriple(Obj->getArchTriple());
      auto ArchName = ObjTriple.getArchName();
      std::string ArchOutFile(OutFile);
      // If we are only handling a single architecture, then we will use the
      // normal output file. If we are handling multiple architectures append
      // the architecture name to the end of the out file path so that we
      // don't overwrite the previous architecture's gsym file.
      if (FilterObjs.size() > 1) {
        ArchOutFile.append(1, '.');
        ArchOutFile.append(ArchName.str());
      }
      outs() << "Output file (" << ArchName << "): " << ArchOutFile << "\n";
      if (auto Err = handleObjectFile(*Obj, ArchOutFile, Out))
        return Err;
    }
  }
  return Error::success();
}

static llvm::Error handleFileConversionToGSYM(StringRef Filename,
                                              const std::string &OutFile,
                                              OutputAggregator &Out) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
      MemoryBuffer::getFileOrSTDIN(Filename);
  error(Filename, BuffOrErr.getError());
  std::unique_ptr<MemoryBuffer> Buffer = std::move(BuffOrErr.get());
  return handleBuffer(Filename, *Buffer, OutFile, Out);
}

static llvm::Error convertFileToGSYM(OutputAggregator &Out) {
  // Expand any .dSYM bundles to the individual object files contained therein.
  std::vector<std::string> Objects;
  std::string OutFile = OutputFilename;
  if (OutFile.empty()) {
    OutFile = ConvertFilename;
    OutFile += ".gsym";
  }

  Out << "Input file: " << ConvertFilename << "\n";

  if (auto DsymObjectsOrErr =
          MachOObjectFile::findDsymObjectMembers(ConvertFilename)) {
    if (DsymObjectsOrErr->empty())
      Objects.push_back(ConvertFilename);
    else
      llvm::append_range(Objects, *DsymObjectsOrErr);
  } else {
    error(DsymObjectsOrErr.takeError());
  }

  for (StringRef Object : Objects)
    if (Error Err = handleFileConversionToGSYM(Object, OutFile, Out))
      return Err;
  return Error::success();
}

static void doLookup(GsymReader &Gsym, uint64_t Addr, raw_ostream &OS) {
  if (auto Result = Gsym.lookup(Addr)) {
    // If verbose is enabled dump the full function info for the address.
    if (Verbose) {
      if (auto FI = Gsym.getFunctionInfo(Addr)) {
        OS << "FunctionInfo for " << HEX64(Addr) << ":\n";
        Gsym.dump(OS, *FI);
        OS << "\nLookupResult for " << HEX64(Addr) << ":\n";
      }
    }
    OS << Result.get();
  } else {
    if (Verbose)
      OS << "\nLookupResult for " << HEX64(Addr) << ":\n";
    OS << HEX64(Addr) << ": ";
    logAllUnhandledErrors(Result.takeError(), OS, "error: ");
  }
  if (Verbose)
    OS << "\n";
}

int llvm_gsymutil_main(int argc, char **argv, const llvm::ToolContext &) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

  llvm::InitializeAllTargets();

  parseArgs(argc, argv);

  raw_ostream &OS = outs();

  OutputAggregator Aggregation(&OS);
  if (!ConvertFilename.empty()) {
    // Convert DWARF to GSYM
    if (!InputFilenames.empty()) {
      OS << "error: no input files can be specified when using the --convert "
            "option.\n";
      return 1;
    }
    // Call error() if we have an error and it will exit with a status of 1
    if (auto Err = convertFileToGSYM(Aggregation))
      error("DWARF conversion failed: ", std::move(Err));

    // Report the errors from aggregator:
    Aggregation.EnumerateResults([&](StringRef category, unsigned count) {
      OS << category << " occurred " << count << " time(s)\n";
    });
    if (!JsonSummaryFile.empty()) {
      std::error_code EC;
      raw_fd_ostream JsonStream(JsonSummaryFile, EC, sys::fs::OF_Text);
      if (EC) {
        OS << "error opening aggregate error json file '" << JsonSummaryFile
           << "' for writing: " << EC.message() << '\n';
        return 1;
      }

      llvm::json::Object Categories;
      uint64_t ErrorCount = 0;
      Aggregation.EnumerateResults([&](StringRef Category, unsigned Count) {
        llvm::json::Object Val;
        Val.try_emplace("count", Count);
        Categories.try_emplace(Category, std::move(Val));
        ErrorCount += Count;
      });
      llvm::json::Object RootNode;
      RootNode.try_emplace("error-categories", std::move(Categories));
      RootNode.try_emplace("error-count", ErrorCount);

      JsonStream << llvm::json::Value(std::move(RootNode));
    }
    return 0;
  }

  if (LookupAddressesFromStdin) {
    if (!LookupAddresses.empty() || !InputFilenames.empty()) {
      OS << "error: no input files or addresses can be specified when using "
            "the --addresses-from-stdin "
            "option.\n";
      return 1;
    }

    std::string InputLine;
    std::string CurrentGSYMPath;
    std::optional<Expected<GsymReader>> CurrentGsym;

    while (std::getline(std::cin, InputLine)) {
      // Strip newline characters.
      std::string StrippedInputLine(InputLine);
      llvm::erase_if(StrippedInputLine,
                     [](char c) { return c == '\r' || c == '\n'; });

      StringRef AddrStr, GSYMPath;
      std::tie(AddrStr, GSYMPath) =
          llvm::StringRef{StrippedInputLine}.split(' ');

      if (GSYMPath != CurrentGSYMPath) {
        CurrentGsym = GsymReader::openFile(GSYMPath);
        if (!*CurrentGsym)
          error(GSYMPath, CurrentGsym->takeError());
        CurrentGSYMPath = GSYMPath;
      }

      uint64_t Addr;
      if (AddrStr.getAsInteger(0, Addr)) {
        OS << "error: invalid address " << AddrStr
           << ", expected: Address GsymFile.\n";
        return 1;
      }

      doLookup(**CurrentGsym, Addr, OS);

      OS << "\n";
      OS.flush();
    }

    return EXIT_SUCCESS;
  }

  // Dump or access data inside GSYM files
  for (const auto &GSYMPath : InputFilenames) {
    auto Gsym = GsymReader::openFile(GSYMPath);
    if (!Gsym)
      error(GSYMPath, Gsym.takeError());

    if (LookupAddresses.empty()) {
      Gsym->dump(outs());
      continue;
    }

    // Lookup an address in a GSYM file and print any matches.
    OS << "Looking up addresses in \"" << GSYMPath << "\":\n";
    for (auto Addr : LookupAddresses) {
      doLookup(*Gsym, Addr, OS);
    }
  }
  return EXIT_SUCCESS;
}
