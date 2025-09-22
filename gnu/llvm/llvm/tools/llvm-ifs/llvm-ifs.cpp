//===- llvm-ifs.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/

#include "ErrorCollector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/InterfaceStub/ELFObjHandler.h"
#include "llvm/InterfaceStub/IFSHandler.h"
#include "llvm/InterfaceStub/IFSStub.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/TextAPIReader.h"
#include "llvm/TextAPI/TextAPIWriter.h"
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::yaml;
using namespace llvm::MachO;
using namespace llvm::ifs;

#define DEBUG_TYPE "llvm-ifs"

namespace {
const VersionTuple IfsVersionCurrent(3, 0);

enum class FileFormat { IFS, ELF, TBD };
} // end anonymous namespace

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

class IFSOptTable : public opt::GenericOptTable {
public:
  IFSOptTable() : opt::GenericOptTable(InfoTable) {
    setGroupedShortOptions(true);
  }
};

struct DriverConfig {
  std::vector<std::string> InputFilePaths;

  std::optional<FileFormat> InputFormat;
  std::optional<FileFormat> OutputFormat;

  std::optional<std::string> HintIfsTarget;
  std::optional<std::string> OptTargetTriple;
  std::optional<IFSArch> OverrideArch;
  std::optional<IFSBitWidthType> OverrideBitWidth;
  std::optional<IFSEndiannessType> OverrideEndianness;

  bool StripIfsArch = false;
  bool StripIfsBitwidth = false;
  bool StripIfsEndianness = false;
  bool StripIfsTarget = false;
  bool StripNeeded = false;
  bool StripSize = false;
  bool StripUndefined = false;

  std::vector<std::string> Exclude;

  std::optional<std::string> SoName;

  std::optional<std::string> Output;
  std::optional<std::string> OutputElf;
  std::optional<std::string> OutputIfs;
  std::optional<std::string> OutputTbd;

  bool WriteIfChanged = false;
};

static std::string getTypeName(IFSSymbolType Type) {
  switch (Type) {
  case IFSSymbolType::NoType:
    return "NoType";
  case IFSSymbolType::Func:
    return "Func";
  case IFSSymbolType::Object:
    return "Object";
  case IFSSymbolType::TLS:
    return "TLS";
  case IFSSymbolType::Unknown:
    return "Unknown";
  }
  llvm_unreachable("Unexpected ifs symbol type.");
}

static Expected<std::unique_ptr<IFSStub>>
readInputFile(std::optional<FileFormat> &InputFormat, StringRef FilePath) {
  // Read in file.
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrError =
      MemoryBuffer::getFileOrSTDIN(FilePath, /*IsText=*/true);
  if (!BufOrError)
    return createStringError(BufOrError.getError(), "Could not open `%s`",
                             FilePath.data());

  std::unique_ptr<MemoryBuffer> FileReadBuffer = std::move(*BufOrError);
  ErrorCollector EC(/*UseFatalErrors=*/false);

  // First try to read as a binary (fails fast if not binary).
  if (!InputFormat || *InputFormat == FileFormat::ELF) {
    Expected<std::unique_ptr<IFSStub>> StubFromELF =
        readELFFile(FileReadBuffer->getMemBufferRef());
    if (StubFromELF) {
      InputFormat = FileFormat::ELF;
      (*StubFromELF)->IfsVersion = IfsVersionCurrent;
      return std::move(*StubFromELF);
    }
    EC.addError(StubFromELF.takeError(), "BinaryRead");
  }

  // Fall back to reading as a ifs.
  if (!InputFormat || *InputFormat == FileFormat::IFS) {
    Expected<std::unique_ptr<IFSStub>> StubFromIFS =
        readIFSFromBuffer(FileReadBuffer->getBuffer());
    if (StubFromIFS) {
      InputFormat = FileFormat::IFS;
      if ((*StubFromIFS)->IfsVersion > IfsVersionCurrent)
        EC.addError(
            createStringError(errc::not_supported,
                              "IFS version " +
                                  (*StubFromIFS)->IfsVersion.getAsString() +
                                  " is unsupported."),
            "ReadInputFile");
      else
        return std::move(*StubFromIFS);
    } else {
      EC.addError(StubFromIFS.takeError(), "YamlParse");
    }
  }

  // If both readers fail, build a new error that includes all information.
  EC.addError(createStringError(errc::not_supported,
                                "No file readers succeeded reading `%s` "
                                "(unsupported/malformed file?)",
                                FilePath.data()),
              "ReadInputFile");
  EC.escalateToFatal();
  return EC.makeError();
}

static int writeTbdStub(const Triple &T, const std::vector<IFSSymbol> &Symbols,
                        const StringRef Format, raw_ostream &Out) {

  auto PlatformTypeOrError =
      [](const llvm::Triple &T) -> llvm::Expected<llvm::MachO::PlatformType> {
    if (T.isMacOSX())
      return llvm::MachO::PLATFORM_MACOS;
    if (T.isTvOS())
      return llvm::MachO::PLATFORM_TVOS;
    if (T.isWatchOS())
      return llvm::MachO::PLATFORM_WATCHOS;
    // Note: put isiOS last because tvOS and watchOS are also iOS according
    // to the Triple.
    if (T.isiOS())
      return llvm::MachO::PLATFORM_IOS;

    return createStringError(errc::not_supported, "Invalid Platform.\n");
  }(T);

  if (!PlatformTypeOrError)
    return -1;

  PlatformType Plat = PlatformTypeOrError.get();
  TargetList Targets({Target(llvm::MachO::mapToArchitecture(T), Plat)});

  InterfaceFile File;
  File.setFileType(FileType::TBD_V3); // Only supporting v3 for now.
  File.addTargets(Targets);

  for (const auto &Symbol : Symbols) {
    auto Name = Symbol.Name;
    auto Kind = EncodeKind::GlobalSymbol;
    switch (Symbol.Type) {
    default:
    case IFSSymbolType::NoType:
      Kind = EncodeKind::GlobalSymbol;
      break;
    case IFSSymbolType::Object:
      Kind = EncodeKind::GlobalSymbol;
      break;
    case IFSSymbolType::Func:
      Kind = EncodeKind::GlobalSymbol;
      break;
    }
    if (Symbol.Weak)
      File.addSymbol(Kind, Name, Targets, SymbolFlags::WeakDefined);
    else
      File.addSymbol(Kind, Name, Targets);
  }

  SmallString<4096> Buffer;
  raw_svector_ostream OS(Buffer);
  if (Error Result = TextAPIWriter::writeToStream(OS, File))
    return -1;
  Out << OS.str();
  return 0;
}

static void fatalError(Error Err) {
  WithColor::defaultErrorHandler(std::move(Err));
  exit(1);
}

static void fatalError(Twine T) {
  WithColor::error() << T.str() << '\n';
  exit(1);
}

/// writeIFS() writes a Text-Based ELF stub to a file using the latest version
/// of the YAML parser.
static Error writeIFS(StringRef FilePath, IFSStub &Stub, bool WriteIfChanged) {
  // Write IFS to memory first.
  std::string IFSStr;
  raw_string_ostream OutStr(IFSStr);
  Error YAMLErr = writeIFSToOutputStream(OutStr, Stub);
  if (YAMLErr)
    return YAMLErr;
  OutStr.flush();

  if (WriteIfChanged) {
    if (ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrError =
            MemoryBuffer::getFile(FilePath)) {
      // Compare IFS output with the existing IFS file. If unchanged, avoid
      // changing the file.
      if ((*BufOrError)->getBuffer() == IFSStr)
        return Error::success();
    }
  }
  // Open IFS file for writing.
  std::error_code SysErr;
  raw_fd_ostream Out(FilePath, SysErr);
  if (SysErr)
    return createStringError(SysErr, "Couldn't open `%s` for writing",
                             FilePath.data());
  Out << IFSStr;
  return Error::success();
}

static DriverConfig parseArgs(int argc, char *const *argv) {
  BumpPtrAllocator A;
  StringSaver Saver(A);
  IFSOptTable Tbl;
  StringRef ToolName = argv[0];
  llvm::opt::InputArgList Args = Tbl.parseArgs(
      argc, argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) { fatalError(Msg); });
  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(llvm::outs(),
                  (Twine(ToolName) + " <input_file> <output_file> [options]")
                      .str()
                      .c_str(),
                  "shared object stubbing tool");
    std::exit(0);
  }
  if (Args.hasArg(OPT_version)) {
    llvm::outs() << ToolName << '\n';
    cl::PrintVersionMessage();
    std::exit(0);
  }

  DriverConfig Config;
  for (const opt::Arg *A : Args.filtered(OPT_INPUT))
    Config.InputFilePaths.push_back(A->getValue());
  if (const opt::Arg *A = Args.getLastArg(OPT_input_format_EQ)) {
    Config.InputFormat = StringSwitch<std::optional<FileFormat>>(A->getValue())
                             .Case("IFS", FileFormat::IFS)
                             .Case("ELF", FileFormat::ELF)
                             .Default(std::nullopt);
    if (!Config.InputFormat)
      fatalError(Twine("invalid argument '") + A->getValue());
  }

  auto OptionNotFound = [ToolName](StringRef FlagName, StringRef OptionName) {
    fatalError(Twine(ToolName) + ": for the " + FlagName +
               " option: Cannot find option named '" + OptionName + "'!");
  };
  if (const opt::Arg *A = Args.getLastArg(OPT_output_format_EQ)) {
    Config.OutputFormat = StringSwitch<std::optional<FileFormat>>(A->getValue())
                              .Case("IFS", FileFormat::IFS)
                              .Case("ELF", FileFormat::ELF)
                              .Case("TBD", FileFormat::TBD)
                              .Default(std::nullopt);
    if (!Config.OutputFormat)
      OptionNotFound("--output-format", A->getValue());
  }
  if (const opt::Arg *A = Args.getLastArg(OPT_arch_EQ)) {
    uint16_t eMachine = ELF::convertArchNameToEMachine(A->getValue());
    if (eMachine == ELF::EM_NONE) {
      fatalError(Twine("unknown arch '") + A->getValue() + "'");
    }
    Config.OverrideArch = eMachine;
  }
  if (const opt::Arg *A = Args.getLastArg(OPT_bitwidth_EQ)) {
    size_t Width;
    llvm::StringRef S(A->getValue());
    if (!S.getAsInteger<size_t>(10, Width) || Width == 64 || Width == 32)
      Config.OverrideBitWidth =
          Width == 64 ? IFSBitWidthType::IFS64 : IFSBitWidthType::IFS32;
    else
      OptionNotFound("--bitwidth", A->getValue());
  }
  if (const opt::Arg *A = Args.getLastArg(OPT_endianness_EQ)) {
    Config.OverrideEndianness =
        StringSwitch<std::optional<IFSEndiannessType>>(A->getValue())
            .Case("little", IFSEndiannessType::Little)
            .Case("big", IFSEndiannessType::Big)
            .Default(std::nullopt);
    if (!Config.OverrideEndianness)
      OptionNotFound("--endianness", A->getValue());
  }
  if (const opt::Arg *A = Args.getLastArg(OPT_target_EQ))
    Config.OptTargetTriple = A->getValue();
  if (const opt::Arg *A = Args.getLastArg(OPT_hint_ifs_target_EQ))
    Config.HintIfsTarget = A->getValue();

  Config.StripIfsArch = Args.hasArg(OPT_strip_ifs_arch);
  Config.StripIfsBitwidth = Args.hasArg(OPT_strip_ifs_bitwidth);
  Config.StripIfsEndianness = Args.hasArg(OPT_strip_ifs_endianness);
  Config.StripIfsTarget = Args.hasArg(OPT_strip_ifs_target);
  Config.StripUndefined = Args.hasArg(OPT_strip_undefined);
  Config.StripNeeded = Args.hasArg(OPT_strip_needed);
  Config.StripSize = Args.hasArg(OPT_strip_size);

  for (const opt::Arg *A : Args.filtered(OPT_exclude_EQ))
    Config.Exclude.push_back(A->getValue());
  if (const opt::Arg *A = Args.getLastArg(OPT_soname_EQ))
    Config.SoName = A->getValue();
  if (const opt::Arg *A = Args.getLastArg(OPT_output_EQ))
    Config.Output = A->getValue();
  if (const opt::Arg *A = Args.getLastArg(OPT_output_elf_EQ))
    Config.OutputElf = A->getValue();
  if (const opt::Arg *A = Args.getLastArg(OPT_output_ifs_EQ))
    Config.OutputIfs = A->getValue();
  if (const opt::Arg *A = Args.getLastArg(OPT_output_tbd_EQ))
    Config.OutputTbd = A->getValue();
  Config.WriteIfChanged = Args.hasArg(OPT_write_if_changed);
  return Config;
}

int llvm_ifs_main(int argc, char **argv, const llvm::ToolContext &) {
  DriverConfig Config = parseArgs(argc, argv);

  if (Config.InputFilePaths.empty())
    Config.InputFilePaths.push_back("-");

  // If input files are more than one, they can only be IFS files.
  if (Config.InputFilePaths.size() > 1)
    Config.InputFormat = FileFormat::IFS;

  // Attempt to merge input.
  IFSStub Stub;
  std::map<std::string, IFSSymbol> SymbolMap;
  std::string PreviousInputFilePath;
  for (const std::string &InputFilePath : Config.InputFilePaths) {
    Expected<std::unique_ptr<IFSStub>> StubOrErr =
        readInputFile(Config.InputFormat, InputFilePath);
    if (!StubOrErr)
      fatalError(StubOrErr.takeError());

    std::unique_ptr<IFSStub> TargetStub = std::move(StubOrErr.get());
    if (PreviousInputFilePath.empty()) {
      Stub.IfsVersion = TargetStub->IfsVersion;
      Stub.Target = TargetStub->Target;
      Stub.SoName = TargetStub->SoName;
      Stub.NeededLibs = TargetStub->NeededLibs;
    } else {
      if (Stub.IfsVersion != TargetStub->IfsVersion) {
        if (Stub.IfsVersion.getMajor() != IfsVersionCurrent.getMajor()) {
          WithColor::error()
              << "Interface Stub: IfsVersion Mismatch."
              << "\nFilenames: " << PreviousInputFilePath << " "
              << InputFilePath << "\nIfsVersion Values: " << Stub.IfsVersion
              << " " << TargetStub->IfsVersion << "\n";
          return -1;
        }
        if (TargetStub->IfsVersion > Stub.IfsVersion)
          Stub.IfsVersion = TargetStub->IfsVersion;
      }
      if (Stub.Target != TargetStub->Target && !TargetStub->Target.empty()) {
        WithColor::error() << "Interface Stub: Target Mismatch."
                           << "\nFilenames: " << PreviousInputFilePath << " "
                           << InputFilePath;
        return -1;
      }
      if (Stub.SoName != TargetStub->SoName) {
        WithColor::error() << "Interface Stub: SoName Mismatch."
                           << "\nFilenames: " << PreviousInputFilePath << " "
                           << InputFilePath
                           << "\nSoName Values: " << Stub.SoName << " "
                           << TargetStub->SoName << "\n";
        return -1;
      }
      if (Stub.NeededLibs != TargetStub->NeededLibs) {
        WithColor::error() << "Interface Stub: NeededLibs Mismatch."
                           << "\nFilenames: " << PreviousInputFilePath << " "
                           << InputFilePath << "\n";
        return -1;
      }
    }

    for (auto Symbol : TargetStub->Symbols) {
      auto SI = SymbolMap.find(Symbol.Name);
      if (SI == SymbolMap.end()) {
        SymbolMap.insert(
            std::pair<std::string, IFSSymbol>(Symbol.Name, Symbol));
        continue;
      }

      assert(Symbol.Name == SI->second.Name && "Symbol Names Must Match.");

      // Check conflicts:
      if (Symbol.Type != SI->second.Type) {
        WithColor::error() << "Interface Stub: Type Mismatch for "
                           << Symbol.Name << ".\nFilename: " << InputFilePath
                           << "\nType Values: " << getTypeName(SI->second.Type)
                           << " " << getTypeName(Symbol.Type) << "\n";

        return -1;
      }
      if (Symbol.Size != SI->second.Size) {
        WithColor::error() << "Interface Stub: Size Mismatch for "
                           << Symbol.Name << ".\nFilename: " << InputFilePath
                           << "\nSize Values: " << SI->second.Size << " "
                           << Symbol.Size << "\n";

        return -1;
      }
      if (Symbol.Weak != SI->second.Weak) {
        Symbol.Weak = false;
        continue;
      }
      // TODO: Not checking Warning. Will be dropped.
    }

    PreviousInputFilePath = InputFilePath;
  }

  if (Stub.IfsVersion != IfsVersionCurrent)
    if (Stub.IfsVersion.getMajor() != IfsVersionCurrent.getMajor()) {
      WithColor::error() << "Interface Stub: Bad IfsVersion: "
                         << Stub.IfsVersion << ", llvm-ifs supported version: "
                         << IfsVersionCurrent << ".\n";
      return -1;
    }

  for (auto &Entry : SymbolMap)
    Stub.Symbols.push_back(Entry.second);

  // Change SoName before emitting stubs.
  if (Config.SoName)
    Stub.SoName = *Config.SoName;

  Error OverrideError =
      overrideIFSTarget(Stub, Config.OverrideArch, Config.OverrideEndianness,
                        Config.OverrideBitWidth, Config.OptTargetTriple);
  if (OverrideError)
    fatalError(std::move(OverrideError));

  if (Config.StripNeeded)
    Stub.NeededLibs.clear();

  if (Error E = filterIFSSyms(Stub, Config.StripUndefined, Config.Exclude))
    fatalError(std::move(E));

  if (Config.StripSize)
    for (IFSSymbol &Sym : Stub.Symbols)
      Sym.Size.reset();

  if (!Config.OutputElf && !Config.OutputIfs && !Config.OutputTbd) {
    if (!Config.OutputFormat) {
      WithColor::error() << "at least one output should be specified.";
      return -1;
    }
  } else if (Config.OutputFormat) {
    WithColor::error() << "'--output-format' cannot be used with "
                          "'--output-{FILE_FORMAT}' options at the same time";
    return -1;
  }
  if (Config.OutputFormat) {
    // TODO: Remove OutputFormat flag in the next revision.
    WithColor::warning() << "--output-format option is deprecated, please use "
                            "--output-{FILE_FORMAT} options instead\n";
    switch (*Config.OutputFormat) {
    case FileFormat::TBD: {
      std::error_code SysErr;
      raw_fd_ostream Out(*Config.Output, SysErr);
      if (SysErr) {
        WithColor::error() << "Couldn't open " << *Config.Output
                           << " for writing.\n";
        return -1;
      }
      if (!Stub.Target.Triple) {
        WithColor::error()
            << "Triple should be defined when output format is TBD";
        return -1;
      }
      return writeTbdStub(llvm::Triple(*Stub.Target.Triple), Stub.Symbols,
                          "TBD", Out);
    }
    case FileFormat::IFS: {
      Stub.IfsVersion = IfsVersionCurrent;
      if (*Config.InputFormat == FileFormat::ELF && Config.HintIfsTarget) {
        std::error_code HintEC(1, std::generic_category());
        IFSTarget HintTarget = parseTriple(*Config.HintIfsTarget);
        if (*Stub.Target.Arch != *HintTarget.Arch)
          fatalError(make_error<StringError>(
              "Triple hint does not match the actual architecture", HintEC));
        if (*Stub.Target.Endianness != *HintTarget.Endianness)
          fatalError(make_error<StringError>(
              "Triple hint does not match the actual endianness", HintEC));
        if (*Stub.Target.BitWidth != *HintTarget.BitWidth)
          fatalError(make_error<StringError>(
              "Triple hint does not match the actual bit width", HintEC));

        stripIFSTarget(Stub, true, false, false, false);
        Stub.Target.Triple = *Config.HintIfsTarget;
      } else {
        stripIFSTarget(Stub, Config.StripIfsTarget, Config.StripIfsArch,
                       Config.StripIfsEndianness, Config.StripIfsBitwidth);
      }
      Error IFSWriteError =
          writeIFS(*Config.Output, Stub, Config.WriteIfChanged);
      if (IFSWriteError)
        fatalError(std::move(IFSWriteError));
      break;
    }
    case FileFormat::ELF: {
      Error TargetError = validateIFSTarget(Stub, true);
      if (TargetError)
        fatalError(std::move(TargetError));
      Error BinaryWriteError =
          writeBinaryStub(*Config.Output, Stub, Config.WriteIfChanged);
      if (BinaryWriteError)
        fatalError(std::move(BinaryWriteError));
      break;
    }
    }
  } else {
    // Check if output path for individual format.
    if (Config.OutputElf) {
      Error TargetError = validateIFSTarget(Stub, true);
      if (TargetError)
        fatalError(std::move(TargetError));
      Error BinaryWriteError =
          writeBinaryStub(*Config.OutputElf, Stub, Config.WriteIfChanged);
      if (BinaryWriteError)
        fatalError(std::move(BinaryWriteError));
    }
    if (Config.OutputIfs) {
      Stub.IfsVersion = IfsVersionCurrent;
      if (*Config.InputFormat == FileFormat::ELF && Config.HintIfsTarget) {
        std::error_code HintEC(1, std::generic_category());
        IFSTarget HintTarget = parseTriple(*Config.HintIfsTarget);
        if (*Stub.Target.Arch != *HintTarget.Arch)
          fatalError(make_error<StringError>(
              "Triple hint does not match the actual architecture", HintEC));
        if (*Stub.Target.Endianness != *HintTarget.Endianness)
          fatalError(make_error<StringError>(
              "Triple hint does not match the actual endianness", HintEC));
        if (*Stub.Target.BitWidth != *HintTarget.BitWidth)
          fatalError(make_error<StringError>(
              "Triple hint does not match the actual bit width", HintEC));

        stripIFSTarget(Stub, true, false, false, false);
        Stub.Target.Triple = *Config.HintIfsTarget;
      } else {
        stripIFSTarget(Stub, Config.StripIfsTarget, Config.StripIfsArch,
                       Config.StripIfsEndianness, Config.StripIfsBitwidth);
      }
      Error IFSWriteError =
          writeIFS(*Config.OutputIfs, Stub, Config.WriteIfChanged);
      if (IFSWriteError)
        fatalError(std::move(IFSWriteError));
    }
    if (Config.OutputTbd) {
      std::error_code SysErr;
      raw_fd_ostream Out(*Config.OutputTbd, SysErr);
      if (SysErr) {
        WithColor::error() << "Couldn't open " << *Config.OutputTbd
                           << " for writing.\n";
        return -1;
      }
      if (!Stub.Target.Triple) {
        WithColor::error()
            << "Triple should be defined when output format is TBD";
        return -1;
      }
      return writeTbdStub(llvm::Triple(*Stub.Target.Triple), Stub.Symbols,
                          "TBD", Out);
    }
  }
  return 0;
}
