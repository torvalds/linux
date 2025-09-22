//=== llvm-dwarfutil.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DebugInfoLinker.h"
#include "Error.h"
#include "Options.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFVerifier.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/ObjCopy/ConfigManager.h"
#include "llvm/ObjCopy/ObjCopy.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm;
using namespace object;

namespace {
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Options.inc"
#undef PREFIX

using namespace llvm::opt;
static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

class DwarfutilOptTable : public opt::GenericOptTable {
public:
  DwarfutilOptTable() : opt::GenericOptTable(InfoTable) {}
};
} // namespace

namespace llvm {
namespace dwarfutil {

std::string ToolName;

static mc::RegisterMCTargetOptionsFlags MOF;

static Error validateAndSetOptions(opt::InputArgList &Args, Options &Options) {
  auto UnknownArgs = Args.filtered(OPT_UNKNOWN);
  if (!UnknownArgs.empty())
    return createStringError(
        std::errc::invalid_argument,
        formatv("unknown option: {0}", (*UnknownArgs.begin())->getSpelling())
            .str()
            .c_str());

  std::vector<std::string> InputFiles = Args.getAllArgValues(OPT_INPUT);
  if (InputFiles.size() != 2)
    return createStringError(
        std::errc::invalid_argument,
        formatv("exactly two positional arguments expected, {0} provided",
                InputFiles.size())
            .str()
            .c_str());

  Options.InputFileName = InputFiles[0];
  Options.OutputFileName = InputFiles[1];

  Options.BuildSeparateDebugFile =
      Args.hasFlag(OPT_separate_debug_file, OPT_no_separate_debug_file, false);
  Options.DoODRDeduplication =
      Args.hasFlag(OPT_odr_deduplication, OPT_no_odr_deduplication, true);
  Options.DoGarbageCollection =
      Args.hasFlag(OPT_garbage_collection, OPT_no_garbage_collection, true);
  Options.Verbose = Args.hasArg(OPT_verbose);
  Options.Verify = Args.hasArg(OPT_verify);

  if (opt::Arg *NumThreads = Args.getLastArg(OPT_threads))
    Options.NumThreads = atoi(NumThreads->getValue());
  else
    Options.NumThreads = 0; // Use all available hardware threads

  if (opt::Arg *Tombstone = Args.getLastArg(OPT_tombstone)) {
    StringRef S = Tombstone->getValue();
    if (S == "bfd")
      Options.Tombstone = TombstoneKind::BFD;
    else if (S == "maxpc")
      Options.Tombstone = TombstoneKind::MaxPC;
    else if (S == "universal")
      Options.Tombstone = TombstoneKind::Universal;
    else if (S == "exec")
      Options.Tombstone = TombstoneKind::Exec;
    else
      return createStringError(
          std::errc::invalid_argument,
          formatv("unknown tombstone value: '{0}'", S).str().c_str());
  }

  if (opt::Arg *LinkerKind = Args.getLastArg(OPT_linker)) {
    StringRef S = LinkerKind->getValue();
    if (S == "classic")
      Options.UseDWARFLinkerParallel = false;
    else if (S == "parallel")
      Options.UseDWARFLinkerParallel = true;
    else
      return createStringError(
          std::errc::invalid_argument,
          formatv("unknown linker kind value: '{0}'", S).str().c_str());
  }

  if (opt::Arg *BuildAccelerator = Args.getLastArg(OPT_build_accelerator)) {
    StringRef S = BuildAccelerator->getValue();

    if (S == "none")
      Options.AccelTableKind = DwarfUtilAccelKind::None;
    else if (S == "DWARF")
      Options.AccelTableKind = DwarfUtilAccelKind::DWARF;
    else
      return createStringError(
          std::errc::invalid_argument,
          formatv("unknown build-accelerator value: '{0}'", S).str().c_str());
  }

  if (Options.Verbose) {
    if (Options.NumThreads != 1 && Args.hasArg(OPT_threads))
      warning("--num-threads set to 1 because verbose mode is specified");

    Options.NumThreads = 1;
  }

  if (Options.DoODRDeduplication && Args.hasArg(OPT_odr_deduplication) &&
      !Options.DoGarbageCollection)
    return createStringError(
        std::errc::invalid_argument,
        "cannot use --odr-deduplication without --garbage-collection");

  if (Options.BuildSeparateDebugFile && Options.OutputFileName == "-")
    return createStringError(
        std::errc::invalid_argument,
        "unable to write to stdout when --separate-debug-file specified");

  return Error::success();
}

static Error setConfigToAddNewDebugSections(objcopy::ConfigManager &Config,
                                            ObjectFile &ObjFile) {
  // Add new debug sections.
  for (SectionRef Sec : ObjFile.sections()) {
    Expected<StringRef> SecName = Sec.getName();
    if (!SecName)
      return SecName.takeError();

    if (isDebugSection(*SecName)) {
      Expected<StringRef> SecData = Sec.getContents();
      if (!SecData)
        return SecData.takeError();

      Config.Common.AddSection.emplace_back(objcopy::NewSectionInfo(
          *SecName, MemoryBuffer::getMemBuffer(*SecData, *SecName, false)));
    }
  }

  return Error::success();
}

static Error verifyOutput(const Options &Opts) {
  if (Opts.OutputFileName == "-") {
    warning("verification skipped because writing to stdout");
    return Error::success();
  }

  std::string FileName = Opts.BuildSeparateDebugFile
                             ? Opts.getSeparateDebugFileName()
                             : Opts.OutputFileName;
  Expected<OwningBinary<Binary>> BinOrErr = createBinary(FileName);
  if (!BinOrErr)
    return createFileError(FileName, BinOrErr.takeError());

  if (BinOrErr->getBinary()->isObject()) {
    if (ObjectFile *Obj = static_cast<ObjectFile *>(BinOrErr->getBinary())) {
      verbose("Verifying DWARF...", Opts.Verbose);
      std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(*Obj);
      DIDumpOptions DumpOpts;
      if (!DICtx->verify(Opts.Verbose ? outs() : nulls(),
                         DumpOpts.noImplicitRecursion()))
        return createFileError(FileName,
                               createError("output verification failed"));

      return Error::success();
    }
  }

  // The file "FileName" was created by this utility in the previous steps
  // (i.e. it is already known that it should pass the isObject check).
  // If the createBinary() function does not return an error, the isObject
  // check should also be successful.
  llvm_unreachable(
      formatv("tool unexpectedly did not emit a supported object file: '{0}'",
              FileName)
          .str()
          .c_str());
}

class raw_crc_ostream : public raw_ostream {
public:
  explicit raw_crc_ostream(raw_ostream &O) : OS(O) { SetUnbuffered(); }

  void reserveExtraSpace(uint64_t ExtraSize) override {
    OS.reserveExtraSpace(ExtraSize);
  }

  uint32_t getCRC32() { return CRC32; }

protected:
  raw_ostream &OS;
  uint32_t CRC32 = 0;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override {
    CRC32 = crc32(
        CRC32, ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Ptr), Size));
    OS.write(Ptr, Size);
  }

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override { return OS.tell(); }
};

static Expected<uint32_t> saveSeparateDebugInfo(const Options &Opts,
                                                ObjectFile &InputFile) {
  objcopy::ConfigManager Config;
  std::string OutputFilename = Opts.getSeparateDebugFileName();
  Config.Common.InputFilename = Opts.InputFileName;
  Config.Common.OutputFilename = OutputFilename;
  Config.Common.OnlyKeepDebug = true;
  uint32_t WrittenFileCRC32 = 0;

  if (Error Err = writeToOutput(
          Config.Common.OutputFilename, [&](raw_ostream &OutFile) -> Error {
            raw_crc_ostream CRCBuffer(OutFile);
            if (Error Err = objcopy::executeObjcopyOnBinary(Config, InputFile,
                                                            CRCBuffer))
              return Err;

            WrittenFileCRC32 = CRCBuffer.getCRC32();
            return Error::success();
          }))
    return std::move(Err);

  return WrittenFileCRC32;
}

static Error saveNonDebugInfo(const Options &Opts, ObjectFile &InputFile,
                              uint32_t GnuDebugLinkCRC32) {
  objcopy::ConfigManager Config;
  Config.Common.InputFilename = Opts.InputFileName;
  Config.Common.OutputFilename = Opts.OutputFileName;
  Config.Common.StripDebug = true;
  std::string SeparateDebugFileName = Opts.getSeparateDebugFileName();
  Config.Common.AddGnuDebugLink = sys::path::filename(SeparateDebugFileName);
  Config.Common.GnuDebugLinkCRC32 = GnuDebugLinkCRC32;

  if (Error Err = writeToOutput(
          Config.Common.OutputFilename, [&](raw_ostream &OutFile) -> Error {
            if (Error Err =
                    objcopy::executeObjcopyOnBinary(Config, InputFile, OutFile))
              return Err;

            return Error::success();
          }))
    return Err;

  return Error::success();
}

static Error splitDebugIntoSeparateFile(const Options &Opts,
                                        ObjectFile &InputFile) {
  Expected<uint32_t> SeparateDebugFileCRC32OrErr =
      saveSeparateDebugInfo(Opts, InputFile);
  if (!SeparateDebugFileCRC32OrErr)
    return SeparateDebugFileCRC32OrErr.takeError();

  if (Error Err =
          saveNonDebugInfo(Opts, InputFile, *SeparateDebugFileCRC32OrErr))
    return Err;

  return Error::success();
}

using DebugInfoBits = SmallString<10000>;

static Error addSectionsFromLinkedData(objcopy::ConfigManager &Config,
                                       ObjectFile &InputFile,
                                       DebugInfoBits &LinkedDebugInfoBits) {
  if (isa<ELFObjectFile<ELF32LE>>(&InputFile)) {
    Expected<ELFObjectFile<ELF32LE>> MemFile = ELFObjectFile<ELF32LE>::create(
        MemoryBufferRef(LinkedDebugInfoBits, ""));
    if (!MemFile)
      return MemFile.takeError();

    if (Error Err = setConfigToAddNewDebugSections(Config, *MemFile))
      return Err;
  } else if (isa<ELFObjectFile<ELF64LE>>(&InputFile)) {
    Expected<ELFObjectFile<ELF64LE>> MemFile = ELFObjectFile<ELF64LE>::create(
        MemoryBufferRef(LinkedDebugInfoBits, ""));
    if (!MemFile)
      return MemFile.takeError();

    if (Error Err = setConfigToAddNewDebugSections(Config, *MemFile))
      return Err;
  } else if (isa<ELFObjectFile<ELF32BE>>(&InputFile)) {
    Expected<ELFObjectFile<ELF32BE>> MemFile = ELFObjectFile<ELF32BE>::create(
        MemoryBufferRef(LinkedDebugInfoBits, ""));
    if (!MemFile)
      return MemFile.takeError();

    if (Error Err = setConfigToAddNewDebugSections(Config, *MemFile))
      return Err;
  } else if (isa<ELFObjectFile<ELF64BE>>(&InputFile)) {
    Expected<ELFObjectFile<ELF64BE>> MemFile = ELFObjectFile<ELF64BE>::create(
        MemoryBufferRef(LinkedDebugInfoBits, ""));
    if (!MemFile)
      return MemFile.takeError();

    if (Error Err = setConfigToAddNewDebugSections(Config, *MemFile))
      return Err;
  } else
    return createStringError(std::errc::invalid_argument,
                             "unsupported file format");

  return Error::success();
}

static Expected<uint32_t>
saveSeparateLinkedDebugInfo(const Options &Opts, ObjectFile &InputFile,
                            DebugInfoBits LinkedDebugInfoBits) {
  objcopy::ConfigManager Config;
  std::string OutputFilename = Opts.getSeparateDebugFileName();
  Config.Common.InputFilename = Opts.InputFileName;
  Config.Common.OutputFilename = OutputFilename;
  Config.Common.StripDebug = true;
  Config.Common.OnlyKeepDebug = true;
  uint32_t WrittenFileCRC32 = 0;

  if (Error Err =
          addSectionsFromLinkedData(Config, InputFile, LinkedDebugInfoBits))
    return std::move(Err);

  if (Error Err = writeToOutput(
          Config.Common.OutputFilename, [&](raw_ostream &OutFile) -> Error {
            raw_crc_ostream CRCBuffer(OutFile);

            if (Error Err = objcopy::executeObjcopyOnBinary(Config, InputFile,
                                                            CRCBuffer))
              return Err;

            WrittenFileCRC32 = CRCBuffer.getCRC32();
            return Error::success();
          }))
    return std::move(Err);

  return WrittenFileCRC32;
}

static Error saveSingleLinkedDebugInfo(const Options &Opts,
                                       ObjectFile &InputFile,
                                       DebugInfoBits LinkedDebugInfoBits) {
  objcopy::ConfigManager Config;

  Config.Common.InputFilename = Opts.InputFileName;
  Config.Common.OutputFilename = Opts.OutputFileName;
  Config.Common.StripDebug = true;
  if (Error Err =
          addSectionsFromLinkedData(Config, InputFile, LinkedDebugInfoBits))
    return Err;

  if (Error Err = writeToOutput(
          Config.Common.OutputFilename, [&](raw_ostream &OutFile) -> Error {
            return objcopy::executeObjcopyOnBinary(Config, InputFile, OutFile);
          }))
    return Err;

  return Error::success();
}

static Error saveLinkedDebugInfo(const Options &Opts, ObjectFile &InputFile,
                                 DebugInfoBits LinkedDebugInfoBits) {
  if (Opts.BuildSeparateDebugFile) {
    Expected<uint32_t> SeparateDebugFileCRC32OrErr =
        saveSeparateLinkedDebugInfo(Opts, InputFile,
                                    std::move(LinkedDebugInfoBits));
    if (!SeparateDebugFileCRC32OrErr)
      return SeparateDebugFileCRC32OrErr.takeError();

    if (Error Err =
            saveNonDebugInfo(Opts, InputFile, *SeparateDebugFileCRC32OrErr))
      return Err;
  } else {
    if (Error Err = saveSingleLinkedDebugInfo(Opts, InputFile,
                                              std::move(LinkedDebugInfoBits)))
      return Err;
  }

  return Error::success();
}

static Error saveCopyOfFile(const Options &Opts, ObjectFile &InputFile) {
  objcopy::ConfigManager Config;

  Config.Common.InputFilename = Opts.InputFileName;
  Config.Common.OutputFilename = Opts.OutputFileName;

  if (Error Err = writeToOutput(
          Config.Common.OutputFilename, [&](raw_ostream &OutFile) -> Error {
            return objcopy::executeObjcopyOnBinary(Config, InputFile, OutFile);
          }))
    return Err;

  return Error::success();
}

static Error applyCLOptions(const struct Options &Opts, ObjectFile &InputFile) {
  if (Opts.DoGarbageCollection ||
      Opts.AccelTableKind != DwarfUtilAccelKind::None) {
    verbose("Do debug info linking...", Opts.Verbose);

    DebugInfoBits LinkedDebugInfo;
    raw_svector_ostream OutStream(LinkedDebugInfo);

    if (Error Err = linkDebugInfo(InputFile, Opts, OutStream))
      return Err;

    if (Error Err =
            saveLinkedDebugInfo(Opts, InputFile, std::move(LinkedDebugInfo)))
      return Err;

    return Error::success();
  } else if (Opts.BuildSeparateDebugFile) {
    if (Error Err = splitDebugIntoSeparateFile(Opts, InputFile))
      return Err;
  } else {
    if (Error Err = saveCopyOfFile(Opts, InputFile))
      return Err;
  }

  return Error::success();
}

} // end of namespace dwarfutil
} // end of namespace llvm

int main(int Argc, char const *Argv[]) {
  using namespace dwarfutil;

  InitLLVM X(Argc, Argv);
  ToolName = Argv[0];

  // Parse arguments.
  DwarfutilOptTable T;
  unsigned MAI;
  unsigned MAC;
  ArrayRef<const char *> ArgsArr = ArrayRef(Argv + 1, Argc - 1);
  opt::InputArgList Args = T.ParseArgs(ArgsArr, MAI, MAC);

  if (Args.hasArg(OPT_help) || Args.size() == 0) {
    T.printHelp(
        outs(), (ToolName + " [options] <input file> <output file>").c_str(),
        "llvm-dwarfutil is a tool to copy and manipulate debug info", false);
    return EXIT_SUCCESS;
  }

  if (Args.hasArg(OPT_version)) {
    cl::PrintVersionMessage();
    return EXIT_SUCCESS;
  }

  Options Opts;
  if (Error Err = validateAndSetOptions(Args, Opts))
    error(std::move(Err), dwarfutil::ToolName);

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllTargetInfos();
  InitializeAllAsmPrinters();

  ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
      MemoryBuffer::getFileOrSTDIN(Opts.InputFileName);
  if (BuffOrErr.getError())
    error(createFileError(Opts.InputFileName, BuffOrErr.getError()));

  Expected<std::unique_ptr<Binary>> BinOrErr =
      object::createBinary(**BuffOrErr);
  if (!BinOrErr)
    error(createFileError(Opts.InputFileName, BinOrErr.takeError()));

  Expected<FilePermissionsApplier> PermsApplierOrErr =
      FilePermissionsApplier::create(Opts.InputFileName);
  if (!PermsApplierOrErr)
    error(createFileError(Opts.InputFileName, PermsApplierOrErr.takeError()));

  if (!(*BinOrErr)->isObject())
    error(createFileError(Opts.InputFileName,
                          createError("unsupported input file")));

  if (Error Err =
          applyCLOptions(Opts, *static_cast<ObjectFile *>((*BinOrErr).get())))
    error(createFileError(Opts.InputFileName, std::move(Err)));

  BinOrErr->reset();
  BuffOrErr->reset();

  if (Error Err = PermsApplierOrErr->apply(Opts.OutputFileName))
    error(std::move(Err));

  if (Opts.BuildSeparateDebugFile)
    if (Error Err = PermsApplierOrErr->apply(Opts.getSeparateDebugFileName()))
      error(std::move(Err));

  if (Opts.Verify) {
    if (Error Err = verifyOutput(Opts))
      error(std::move(Err));
  }

  return EXIT_SUCCESS;
}
