//===- dsymutil.cpp - Debug info dumping utility for llvm -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that aims to be a dropin replacement for Darwin's
// dsymutil.
//===----------------------------------------------------------------------===//

#include "dsymutil.h"
#include "BinaryHolder.h"
#include "CFBundle.h"
#include "DebugMap.h"
#include "DwarfLinkerForBinary.h"
#include "LinkUtils.h"
#include "MachOUtils.h"
#include "Reproducer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFVerifier.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/MachO.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/FileCollector.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/thread.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <system_error>

using namespace llvm;
using namespace llvm::dsymutil;
using namespace object;
using namespace llvm::dwarf_linker;

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

class DsymutilOptTable : public opt::GenericOptTable {
public:
  DsymutilOptTable() : opt::GenericOptTable(InfoTable) {}
};
} // namespace

enum class DWARFVerify : uint8_t {
  None = 0,
  Input = 1 << 0,
  Output = 1 << 1,
  OutputOnValidInput = 1 << 2,
  All = Input | Output,
  Auto = Input | OutputOnValidInput,
#if !defined(NDEBUG) || defined(EXPENSIVE_CHECKS)
  Default = Auto
#else
  Default = None
#endif
};

inline bool flagIsSet(DWARFVerify Flags, DWARFVerify SingleFlag) {
  return static_cast<uint8_t>(Flags) & static_cast<uint8_t>(SingleFlag);
}

struct DsymutilOptions {
  bool DumpDebugMap = false;
  bool DumpStab = false;
  bool Flat = false;
  bool InputIsYAMLDebugMap = false;
  bool ForceKeepFunctionForStatic = false;
  std::string OutputFile;
  std::string Toolchain;
  std::string ReproducerPath;
  std::vector<std::string> Archs;
  std::vector<std::string> InputFiles;
  unsigned NumThreads;
  DWARFVerify Verify = DWARFVerify::Default;
  ReproducerMode ReproMode = ReproducerMode::GenerateOnCrash;
  dsymutil::LinkOptions LinkOpts;
};

/// Return a list of input files. This function has logic for dealing with the
/// special case where we might have dSYM bundles as input. The function
/// returns an error when the directory structure doesn't match that of a dSYM
/// bundle.
static Expected<std::vector<std::string>> getInputs(opt::InputArgList &Args,
                                                    bool DsymAsInput) {
  std::vector<std::string> InputFiles;
  for (auto *File : Args.filtered(OPT_INPUT))
    InputFiles.push_back(File->getValue());

  if (!DsymAsInput)
    return InputFiles;

  // If we are updating, we might get dSYM bundles as input.
  std::vector<std::string> Inputs;
  for (const auto &Input : InputFiles) {
    if (!sys::fs::is_directory(Input)) {
      Inputs.push_back(Input);
      continue;
    }

    // Make sure that we're dealing with a dSYM bundle.
    SmallString<256> BundlePath(Input);
    sys::path::append(BundlePath, "Contents", "Resources", "DWARF");
    if (!sys::fs::is_directory(BundlePath))
      return make_error<StringError>(
          Input + " is a directory, but doesn't look like a dSYM bundle.",
          inconvertibleErrorCode());

    // Create a directory iterator to iterate over all the entries in the
    // bundle.
    std::error_code EC;
    sys::fs::directory_iterator DirIt(BundlePath, EC);
    sys::fs::directory_iterator DirEnd;
    if (EC)
      return errorCodeToError(EC);

    // Add each entry to the list of inputs.
    while (DirIt != DirEnd) {
      Inputs.push_back(DirIt->path());
      DirIt.increment(EC);
      if (EC)
        return errorCodeToError(EC);
    }
  }
  return Inputs;
}

// Verify that the given combination of options makes sense.
static Error verifyOptions(const DsymutilOptions &Options) {
  if (Options.LinkOpts.Verbose && Options.LinkOpts.Quiet) {
    return make_error<StringError>(
        "--quiet and --verbose cannot be specified together",
        errc::invalid_argument);
  }

  if (Options.InputFiles.empty()) {
    return make_error<StringError>("no input files specified",
                                   errc::invalid_argument);
  }

  if (Options.LinkOpts.Update && llvm::is_contained(Options.InputFiles, "-")) {
    // FIXME: We cannot use stdin for an update because stdin will be
    // consumed by the BinaryHolder during the debugmap parsing, and
    // then we will want to consume it again in DwarfLinker. If we
    // used a unique BinaryHolder object that could cache multiple
    // binaries this restriction would go away.
    return make_error<StringError>(
        "standard input cannot be used as input for a dSYM update.",
        errc::invalid_argument);
  }

  if (!Options.Flat && Options.OutputFile == "-")
    return make_error<StringError>(
        "cannot emit to standard output without --flat.",
        errc::invalid_argument);

  if (Options.InputFiles.size() > 1 && Options.Flat &&
      !Options.OutputFile.empty())
    return make_error<StringError>(
        "cannot use -o with multiple inputs in flat mode.",
        errc::invalid_argument);

  if (!Options.ReproducerPath.empty() &&
      Options.ReproMode != ReproducerMode::Use)
    return make_error<StringError>(
        "cannot combine --gen-reproducer and --use-reproducer.",
        errc::invalid_argument);

  return Error::success();
}

static Expected<DsymutilAccelTableKind>
getAccelTableKind(opt::InputArgList &Args) {
  if (opt::Arg *Accelerator = Args.getLastArg(OPT_accelerator)) {
    StringRef S = Accelerator->getValue();
    if (S == "Apple")
      return DsymutilAccelTableKind::Apple;
    if (S == "Dwarf")
      return DsymutilAccelTableKind::Dwarf;
    if (S == "Pub")
      return DsymutilAccelTableKind::Pub;
    if (S == "Default")
      return DsymutilAccelTableKind::Default;
    if (S == "None")
      return DsymutilAccelTableKind::None;
    return make_error<StringError>("invalid accelerator type specified: '" + S +
                                       "'. Supported values are 'Apple', "
                                       "'Dwarf', 'Pub', 'Default' and 'None'.",
                                   inconvertibleErrorCode());
  }
  return DsymutilAccelTableKind::Default;
}

static Expected<DsymutilDWARFLinkerType>
getDWARFLinkerType(opt::InputArgList &Args) {
  if (opt::Arg *LinkerType = Args.getLastArg(OPT_linker)) {
    StringRef S = LinkerType->getValue();
    if (S == "classic")
      return DsymutilDWARFLinkerType::Classic;
    if (S == "parallel")
      return DsymutilDWARFLinkerType::Parallel;
    return make_error<StringError>("invalid DWARF linker type specified: '" +
                                       S +
                                       "'. Supported values are 'classic', "
                                       "'parallel'.",
                                   inconvertibleErrorCode());
  }

  return DsymutilDWARFLinkerType::Classic;
}

static Expected<ReproducerMode> getReproducerMode(opt::InputArgList &Args) {
  if (Args.hasArg(OPT_gen_reproducer))
    return ReproducerMode::GenerateOnExit;
  if (opt::Arg *Reproducer = Args.getLastArg(OPT_reproducer)) {
    StringRef S = Reproducer->getValue();
    if (S == "GenerateOnExit")
      return ReproducerMode::GenerateOnExit;
    if (S == "GenerateOnCrash")
      return ReproducerMode::GenerateOnCrash;
    if (S == "Off")
      return ReproducerMode::Off;
    return make_error<StringError>(
        "invalid reproducer mode: '" + S +
            "'. Supported values are 'GenerateOnExit', 'GenerateOnCrash', "
            "'Off'.",
        inconvertibleErrorCode());
  }
  return ReproducerMode::GenerateOnCrash;
}

static Expected<DWARFVerify> getVerifyKind(opt::InputArgList &Args) {
  if (Args.hasArg(OPT_verify))
    return DWARFVerify::Output;
  if (opt::Arg *Verify = Args.getLastArg(OPT_verify_dwarf)) {
    StringRef S = Verify->getValue();
    if (S == "input")
      return DWARFVerify::Input;
    if (S == "output")
      return DWARFVerify::Output;
    if (S == "all")
      return DWARFVerify::All;
    if (S == "auto")
      return DWARFVerify::Auto;
    if (S == "none")
      return DWARFVerify::None;
    return make_error<StringError>("invalid verify type specified: '" + S +
                                       "'. Supported values are 'none', "
                                       "'input', 'output', 'all' and 'auto'.",
                                   inconvertibleErrorCode());
  }
  return DWARFVerify::Default;
}

/// Parses the command line options into the LinkOptions struct and performs
/// some sanity checking. Returns an error in case the latter fails.
static Expected<DsymutilOptions> getOptions(opt::InputArgList &Args) {
  DsymutilOptions Options;

  Options.DumpDebugMap = Args.hasArg(OPT_dump_debug_map);
  Options.DumpStab = Args.hasArg(OPT_symtab);
  Options.Flat = Args.hasArg(OPT_flat);
  Options.InputIsYAMLDebugMap = Args.hasArg(OPT_yaml_input);

  if (Expected<DWARFVerify> Verify = getVerifyKind(Args)) {
    Options.Verify = *Verify;
  } else {
    return Verify.takeError();
  }

  Options.LinkOpts.NoODR = Args.hasArg(OPT_no_odr);
  Options.LinkOpts.VerifyInputDWARF =
      flagIsSet(Options.Verify, DWARFVerify::Input);
  Options.LinkOpts.NoOutput = Args.hasArg(OPT_no_output);
  Options.LinkOpts.NoTimestamp = Args.hasArg(OPT_no_swiftmodule_timestamp);
  Options.LinkOpts.Update = Args.hasArg(OPT_update);
  Options.LinkOpts.Verbose = Args.hasArg(OPT_verbose);
  Options.LinkOpts.Quiet = Args.hasArg(OPT_quiet);
  Options.LinkOpts.Statistics = Args.hasArg(OPT_statistics);
  Options.LinkOpts.Fat64 = Args.hasArg(OPT_fat64);
  Options.LinkOpts.KeepFunctionForStatic =
      Args.hasArg(OPT_keep_func_for_static);

  if (opt::Arg *ReproducerPath = Args.getLastArg(OPT_use_reproducer)) {
    Options.ReproMode = ReproducerMode::Use;
    Options.ReproducerPath = ReproducerPath->getValue();
  } else {
    if (Expected<ReproducerMode> ReproMode = getReproducerMode(Args)) {
      Options.ReproMode = *ReproMode;
    } else {
      return ReproMode.takeError();
    }
  }

  if (Expected<DsymutilAccelTableKind> AccelKind = getAccelTableKind(Args)) {
    Options.LinkOpts.TheAccelTableKind = *AccelKind;
  } else {
    return AccelKind.takeError();
  }

  if (Expected<DsymutilDWARFLinkerType> DWARFLinkerType =
          getDWARFLinkerType(Args)) {
    Options.LinkOpts.DWARFLinkerType = *DWARFLinkerType;
  } else {
    return DWARFLinkerType.takeError();
  }

  if (Expected<std::vector<std::string>> InputFiles =
          getInputs(Args, Options.LinkOpts.Update)) {
    Options.InputFiles = std::move(*InputFiles);
  } else {
    return InputFiles.takeError();
  }

  for (auto *Arch : Args.filtered(OPT_arch))
    Options.Archs.push_back(Arch->getValue());

  if (opt::Arg *OsoPrependPath = Args.getLastArg(OPT_oso_prepend_path))
    Options.LinkOpts.PrependPath = OsoPrependPath->getValue();

  for (const auto &Arg : Args.getAllArgValues(OPT_object_prefix_map)) {
    auto Split = StringRef(Arg).split('=');
    Options.LinkOpts.ObjectPrefixMap.insert(
        {std::string(Split.first), std::string(Split.second)});
  }

  if (opt::Arg *OutputFile = Args.getLastArg(OPT_output))
    Options.OutputFile = OutputFile->getValue();

  if (opt::Arg *Toolchain = Args.getLastArg(OPT_toolchain))
    Options.Toolchain = Toolchain->getValue();

  if (Args.hasArg(OPT_assembly))
    Options.LinkOpts.FileType = DWARFLinkerBase::OutputFileType::Assembly;

  if (opt::Arg *NumThreads = Args.getLastArg(OPT_threads))
    Options.LinkOpts.Threads = atoi(NumThreads->getValue());
  else
    Options.LinkOpts.Threads = 0; // Use all available hardware threads

  if (Options.DumpDebugMap || Options.LinkOpts.Verbose)
    Options.LinkOpts.Threads = 1;

  if (opt::Arg *RemarksPrependPath = Args.getLastArg(OPT_remarks_prepend_path))
    Options.LinkOpts.RemarksPrependPath = RemarksPrependPath->getValue();

  if (opt::Arg *RemarksOutputFormat =
          Args.getLastArg(OPT_remarks_output_format)) {
    if (Expected<remarks::Format> FormatOrErr =
            remarks::parseFormat(RemarksOutputFormat->getValue()))
      Options.LinkOpts.RemarksFormat = *FormatOrErr;
    else
      return FormatOrErr.takeError();
  }

  Options.LinkOpts.RemarksKeepAll =
      !Args.hasArg(OPT_remarks_drop_without_debug);

  if (opt::Arg *BuildVariantSuffix = Args.getLastArg(OPT_build_variant_suffix))
    Options.LinkOpts.BuildVariantSuffix = BuildVariantSuffix->getValue();

  for (auto *SearchPath : Args.filtered(OPT_dsym_search_path))
    Options.LinkOpts.DSYMSearchPaths.push_back(SearchPath->getValue());

  if (Error E = verifyOptions(Options))
    return std::move(E);
  return Options;
}

static Error createPlistFile(StringRef Bin, StringRef BundleRoot,
                             StringRef Toolchain) {
  // Create plist file to write to.
  SmallString<128> InfoPlist(BundleRoot);
  sys::path::append(InfoPlist, "Contents/Info.plist");
  std::error_code EC;
  raw_fd_ostream PL(InfoPlist, EC, sys::fs::OF_TextWithCRLF);
  if (EC)
    return make_error<StringError>(
        "cannot create Plist: " + toString(errorCodeToError(EC)), EC);

  CFBundleInfo BI = getBundleInfo(Bin);

  if (BI.IDStr.empty()) {
    StringRef BundleID = *sys::path::rbegin(BundleRoot);
    if (sys::path::extension(BundleRoot) == ".dSYM")
      BI.IDStr = std::string(sys::path::stem(BundleID));
    else
      BI.IDStr = std::string(BundleID);
  }

  // Print out information to the plist file.
  PL << "<?xml version=\"1.0\" encoding=\"UTF-8\"\?>\n"
     << "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" "
     << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
     << "<plist version=\"1.0\">\n"
     << "\t<dict>\n"
     << "\t\t<key>CFBundleDevelopmentRegion</key>\n"
     << "\t\t<string>English</string>\n"
     << "\t\t<key>CFBundleIdentifier</key>\n"
     << "\t\t<string>com.apple.xcode.dsym.";
  printHTMLEscaped(BI.IDStr, PL);
  PL << "</string>\n"
     << "\t\t<key>CFBundleInfoDictionaryVersion</key>\n"
     << "\t\t<string>6.0</string>\n"
     << "\t\t<key>CFBundlePackageType</key>\n"
     << "\t\t<string>dSYM</string>\n"
     << "\t\t<key>CFBundleSignature</key>\n"
     << "\t\t<string>\?\?\?\?</string>\n";

  if (!BI.OmitShortVersion()) {
    PL << "\t\t<key>CFBundleShortVersionString</key>\n";
    PL << "\t\t<string>";
    printHTMLEscaped(BI.ShortVersionStr, PL);
    PL << "</string>\n";
  }

  PL << "\t\t<key>CFBundleVersion</key>\n";
  PL << "\t\t<string>";
  printHTMLEscaped(BI.VersionStr, PL);
  PL << "</string>\n";

  if (!Toolchain.empty()) {
    PL << "\t\t<key>Toolchain</key>\n";
    PL << "\t\t<string>";
    printHTMLEscaped(Toolchain, PL);
    PL << "</string>\n";
  }

  PL << "\t</dict>\n"
     << "</plist>\n";

  PL.close();
  return Error::success();
}

static Error createBundleDir(StringRef BundleBase) {
  SmallString<128> Bundle(BundleBase);
  sys::path::append(Bundle, "Contents", "Resources", "DWARF");
  if (std::error_code EC =
          create_directories(Bundle.str(), true, sys::fs::perms::all_all))
    return make_error<StringError>(
        "cannot create bundle: " + toString(errorCodeToError(EC)), EC);

  return Error::success();
}

static bool verifyOutput(StringRef OutputFile, StringRef Arch,
                         DsymutilOptions Options, std::mutex &Mutex) {

  if (OutputFile == "-") {
    if (!Options.LinkOpts.Quiet) {
      std::lock_guard<std::mutex> Guard(Mutex);
      WithColor::warning() << "verification skipped for " << Arch
                           << " because writing to stdout.\n";
    }
    return true;
  }

  if (Options.LinkOpts.NoOutput) {
    if (!Options.LinkOpts.Quiet) {
      std::lock_guard<std::mutex> Guard(Mutex);
      WithColor::warning() << "verification skipped for " << Arch
                           << " because --no-output was passed.\n";
    }
    return true;
  }

  Expected<OwningBinary<Binary>> BinOrErr = createBinary(OutputFile);
  if (!BinOrErr) {
    std::lock_guard<std::mutex> Guard(Mutex);
    WithColor::error() << OutputFile << ": " << toString(BinOrErr.takeError());
    return false;
  }

  Binary &Binary = *BinOrErr.get().getBinary();
  if (auto *Obj = dyn_cast<MachOObjectFile>(&Binary)) {
    std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(*Obj);
    if (DICtx->getMaxVersion() > 5) {
      if (!Options.LinkOpts.Quiet) {
        std::lock_guard<std::mutex> Guard(Mutex);
        WithColor::warning() << "verification skipped for " << Arch
                             << " because DWARF standard greater than v5 is "
                                "not supported yet.\n";
      }
      return true;
    }

    if (Options.LinkOpts.Verbose) {
      std::lock_guard<std::mutex> Guard(Mutex);
      errs() << "Verifying DWARF for architecture: " << Arch << "\n";
    }

    std::string Buffer;
    raw_string_ostream OS(Buffer);

    DIDumpOptions DumpOpts;
    bool success = DICtx->verify(OS, DumpOpts.noImplicitRecursion());
    if (!success) {
      std::lock_guard<std::mutex> Guard(Mutex);
      errs() << OS.str();
      WithColor::error() << "output verification failed for " << Arch << '\n';
    }
    return success;
  }

  return false;
}

namespace {
struct OutputLocation {
  OutputLocation(std::string DWARFFile,
                 std::optional<std::string> ResourceDir = {})
      : DWARFFile(DWARFFile), ResourceDir(ResourceDir) {}
  /// This method is a workaround for older compilers.
  std::optional<std::string> getResourceDir() const { return ResourceDir; }
  std::string DWARFFile;
  std::optional<std::string> ResourceDir;
};
} // namespace

static Expected<OutputLocation>
getOutputFileName(StringRef InputFile, const DsymutilOptions &Options) {
  if (Options.OutputFile == "-")
    return OutputLocation(Options.OutputFile);

  // When updating, do in place replacement.
  if (Options.OutputFile.empty() && Options.LinkOpts.Update)
    return OutputLocation(std::string(InputFile));

  // When dumping the debug map, just return an empty output location. This
  // allows us to compute the output location once.
  if (Options.DumpDebugMap)
    return OutputLocation("");

  // If a flat dSYM has been requested, things are pretty simple.
  if (Options.Flat) {
    if (Options.OutputFile.empty()) {
      if (InputFile == "-")
        return OutputLocation{"a.out.dwarf", {}};
      return OutputLocation((InputFile + ".dwarf").str());
    }

    return OutputLocation(Options.OutputFile);
  }

  // We need to create/update a dSYM bundle.
  // A bundle hierarchy looks like this:
  //   <bundle name>.dSYM/
  //       Contents/
  //          Info.plist
  //          Resources/
  //             DWARF/
  //                <DWARF file(s)>
  std::string DwarfFile =
      std::string(InputFile == "-" ? StringRef("a.out") : InputFile);
  SmallString<128> Path(Options.OutputFile);
  if (Path.empty())
    Path = DwarfFile + ".dSYM";
  if (!Options.LinkOpts.NoOutput) {
    if (auto E = createBundleDir(Path))
      return std::move(E);
    if (auto E = createPlistFile(DwarfFile, Path, Options.Toolchain))
      return std::move(E);
  }

  sys::path::append(Path, "Contents", "Resources");
  std::string ResourceDir = std::string(Path);
  sys::path::append(Path, "DWARF", sys::path::filename(DwarfFile));
  return OutputLocation(std::string(Path), ResourceDir);
}

int dsymutil_main(int argc, char **argv, const llvm::ToolContext &) {
  // Parse arguments.
  DsymutilOptTable T;
  unsigned MAI;
  unsigned MAC;
  ArrayRef<const char *> ArgsArr = ArrayRef(argv + 1, argc - 1);
  opt::InputArgList Args = T.ParseArgs(ArgsArr, MAI, MAC);

  void *P = (void *)(intptr_t)getOutputFileName;
  std::string SDKPath = sys::fs::getMainExecutable(argv[0], P);
  SDKPath = std::string(sys::path::parent_path(SDKPath));

  for (auto *Arg : Args.filtered(OPT_UNKNOWN)) {
    WithColor::warning() << "ignoring unknown option: " << Arg->getSpelling()
                         << '\n';
  }

  if (Args.hasArg(OPT_help)) {
    T.printHelp(
        outs(), (std::string(argv[0]) + " [options] <input files>").c_str(),
        "manipulate archived DWARF debug symbol files.\n\n"
        "dsymutil links the DWARF debug information found in the object files\n"
        "for the executable <input file> by using debug symbols information\n"
        "contained in its symbol table.\n",
        false);
    return EXIT_SUCCESS;
  }

  if (Args.hasArg(OPT_version)) {
    cl::PrintVersionMessage();
    return EXIT_SUCCESS;
  }

  auto OptionsOrErr = getOptions(Args);
  if (!OptionsOrErr) {
    WithColor::error() << toString(OptionsOrErr.takeError()) << '\n';
    return EXIT_FAILURE;
  }

  auto &Options = *OptionsOrErr;

  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllTargets();
  InitializeAllAsmPrinters();

  auto Repro = Reproducer::createReproducer(Options.ReproMode,
                                            Options.ReproducerPath, argc, argv);
  if (!Repro) {
    WithColor::error() << toString(Repro.takeError()) << '\n';
    return EXIT_FAILURE;
  }

  Options.LinkOpts.VFS = (*Repro)->getVFS();

  for (const auto &Arch : Options.Archs)
    if (Arch != "*" && Arch != "all" &&
        !object::MachOObjectFile::isValidArch(Arch)) {
      WithColor::error() << "unsupported cpu architecture: '" << Arch << "'\n";
      return EXIT_FAILURE;
    }

  for (auto &InputFile : Options.InputFiles) {
    // Dump the symbol table for each input file and requested arch
    if (Options.DumpStab) {
      if (!dumpStab(Options.LinkOpts.VFS, InputFile, Options.Archs,
                    Options.LinkOpts.DSYMSearchPaths,
                    Options.LinkOpts.PrependPath,
                    Options.LinkOpts.BuildVariantSuffix))
        return EXIT_FAILURE;
      continue;
    }

    auto DebugMapPtrsOrErr = parseDebugMap(
        Options.LinkOpts.VFS, InputFile, Options.Archs,
        Options.LinkOpts.DSYMSearchPaths, Options.LinkOpts.PrependPath,
        Options.LinkOpts.BuildVariantSuffix, Options.LinkOpts.Verbose,
        Options.InputIsYAMLDebugMap);

    if (auto EC = DebugMapPtrsOrErr.getError()) {
      WithColor::error() << "cannot parse the debug map for '" << InputFile
                         << "': " << EC.message() << '\n';
      return EXIT_FAILURE;
    }

    // Remember the number of debug maps that are being processed to decide how
    // to name the remark files.
    Options.LinkOpts.NumDebugMaps = DebugMapPtrsOrErr->size();

    if (Options.LinkOpts.Update) {
      // The debug map should be empty. Add one object file corresponding to
      // the input file.
      for (auto &Map : *DebugMapPtrsOrErr)
        Map->addDebugMapObject(InputFile,
                               sys::TimePoint<std::chrono::seconds>());
    }

    // Ensure that the debug map is not empty (anymore).
    if (DebugMapPtrsOrErr->empty()) {
      WithColor::error() << "no architecture to link\n";
      return EXIT_FAILURE;
    }

    // Shared a single binary holder for all the link steps.
    BinaryHolder BinHolder(Options.LinkOpts.VFS);

    // Compute the output location and update the resource directory.
    Expected<OutputLocation> OutputLocationOrErr =
        getOutputFileName(InputFile, Options);
    if (!OutputLocationOrErr) {
      WithColor::error() << toString(OutputLocationOrErr.takeError());
      return EXIT_FAILURE;
    }
    Options.LinkOpts.ResourceDir = OutputLocationOrErr->getResourceDir();

    // Statistics only require different architectures to be processed
    // sequentially, the link itself can still happen in parallel. Change the
    // thread pool strategy here instead of modifying LinkOpts.Threads.
    ThreadPoolStrategy S = hardware_concurrency(
        Options.LinkOpts.Statistics ? 1 : Options.LinkOpts.Threads);
    if (Options.LinkOpts.Threads == 0) {
      // If NumThreads is not specified, create one thread for each input, up to
      // the number of hardware threads.
      S.ThreadsRequested = DebugMapPtrsOrErr->size();
      S.Limit = true;
    }
    DefaultThreadPool Threads(S);

    // If there is more than one link to execute, we need to generate
    // temporary files.
    const bool NeedsTempFiles =
        !Options.DumpDebugMap && (Options.OutputFile != "-") &&
        (DebugMapPtrsOrErr->size() != 1 || Options.LinkOpts.Update);

    std::atomic_char AllOK(1);
    SmallVector<MachOUtils::ArchAndFile, 4> TempFiles;

    std::mutex ErrorHandlerMutex;

    // Set up a crash recovery context.
    CrashRecoveryContext::Enable();
    CrashRecoveryContext CRC;
    CRC.DumpStackAndCleanupOnFailure = true;

    const bool Crashed = !CRC.RunSafely([&]() {
      for (auto &Map : *DebugMapPtrsOrErr) {
        if (Options.LinkOpts.Verbose || Options.DumpDebugMap)
          Map->print(outs());

        if (Options.DumpDebugMap)
          continue;

        if (Map->begin() == Map->end()) {
          if (!Options.LinkOpts.Quiet) {
            std::lock_guard<std::mutex> Guard(ErrorHandlerMutex);
            WithColor::warning()
                << "no debug symbols in executable (-arch "
                << MachOUtils::getArchName(Map->getTriple().getArchName())
                << ")\n";
          }
        }

        // Using a std::shared_ptr rather than std::unique_ptr because move-only
        // types don't work with std::bind in the ThreadPool implementation.
        std::shared_ptr<raw_fd_ostream> OS;

        std::string OutputFile = OutputLocationOrErr->DWARFFile;
        if (NeedsTempFiles) {
          TempFiles.emplace_back(Map->getTriple().getArchName().str());

          auto E = TempFiles.back().createTempFile();
          if (E) {
            std::lock_guard<std::mutex> Guard(ErrorHandlerMutex);
            WithColor::error() << toString(std::move(E));
            AllOK.fetch_and(false);
            return;
          }

          MachOUtils::ArchAndFile &AF = TempFiles.back();
          OS = std::make_shared<raw_fd_ostream>(AF.getFD(),
                                                /*shouldClose*/ false);
          OutputFile = AF.getPath();
        } else {
          std::error_code EC;
          OS = std::make_shared<raw_fd_ostream>(
              Options.LinkOpts.NoOutput ? "-" : OutputFile, EC,
              sys::fs::OF_None);
          if (EC) {
            WithColor::error() << OutputFile << ": " << EC.message();
            AllOK.fetch_and(false);
            return;
          }
        }

        auto LinkLambda = [&,
                           OutputFile](std::shared_ptr<raw_fd_ostream> Stream) {
          DwarfLinkerForBinary Linker(*Stream, BinHolder, Options.LinkOpts,
                                      ErrorHandlerMutex);
          AllOK.fetch_and(Linker.link(*Map));
          Stream->flush();
          if (flagIsSet(Options.Verify, DWARFVerify::Output) ||
              (flagIsSet(Options.Verify, DWARFVerify::OutputOnValidInput) &&
               !Linker.InputVerificationFailed())) {
            AllOK.fetch_and(verifyOutput(OutputFile,
                                         Map->getTriple().getArchName(),
                                         Options, ErrorHandlerMutex));
          }
        };

        // FIXME: The DwarfLinker can have some very deep recursion that can max
        // out the (significantly smaller) stack when using threads. We don't
        // want this limitation when we only have a single thread.
        if (S.ThreadsRequested == 1)
          LinkLambda(OS);
        else
          Threads.async(LinkLambda, OS);
      }

      Threads.wait();
    });

    if (Crashed)
      (*Repro)->generate();

    if (!AllOK)
      return EXIT_FAILURE;

    if (NeedsTempFiles) {
      const bool Fat64 = Options.LinkOpts.Fat64;
      if (!Fat64) {
        // Universal Mach-O files can't have an archicture slice that starts
        // beyond the 4GB boundary. "lipo" can create a 64 bit universal
        // header, but not all tools can parse these files so we want to return
        // an error if the file can't be encoded as a file with a 32 bit
        // universal header. To detect this, we check the size of each
        // architecture's skinny Mach-O file and add up the offsets. If they
        // exceed 4GB, then we return an error.

        // First we compute the right offset where the first architecture will
        // fit followin the 32 bit universal header. The 32 bit universal header
        // starts with a uint32_t magic and a uint32_t number of architecture
        // infos. Then it is followed by 5 uint32_t values for each
        // architecture. So we set the start offset to the right value so we can
        // calculate the exact offset that the first architecture slice can
        // start at.
        constexpr uint64_t MagicAndCountSize = 2 * 4;
        constexpr uint64_t UniversalArchInfoSize = 5 * 4;
        uint64_t FileOffset =
            MagicAndCountSize + UniversalArchInfoSize * TempFiles.size();
        for (const auto &File : TempFiles) {
          ErrorOr<vfs::Status> stat =
              Options.LinkOpts.VFS->status(File.getPath());
          if (!stat)
            break;
          if (FileOffset > UINT32_MAX) {
            WithColor::error()
                << formatv("the universal binary has a slice with a starting "
                           "offset ({0:x}) that exceeds 4GB and will produce "
                           "an invalid Mach-O file. Use the -fat64 flag to "
                           "generate a universal binary with a 64-bit header "
                           "but note that not all tools support this format.",
                           FileOffset);
            return EXIT_FAILURE;
          }
          FileOffset += stat->getSize();
        }
      }
      if (!MachOUtils::generateUniversalBinary(
              TempFiles, OutputLocationOrErr->DWARFFile, Options.LinkOpts,
              SDKPath, Fat64))
        return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
