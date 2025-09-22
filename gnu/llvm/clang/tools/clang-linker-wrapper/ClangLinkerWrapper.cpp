//===-- clang-linker-wrapper/ClangLinkerWrapper.cpp - wrapper over linker-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This tool works as a wrapper over a linking job. This tool is used to create
// linked device images for offloading. It scans the linker's input for embedded
// device offloading data stored in sections `.llvm.offloading` and extracts it
// as a temporary file. The extracted device files will then be passed to a
// device linking job to create a final device image.
//
//===---------------------------------------------------------------------===//

#include "clang/Basic/TargetID.h"
#include "clang/Basic/Version.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Frontend/Offloading/OffloadWrapper.h"
#include "llvm/Frontend/Offloading/Utility.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LTO/LTO.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include <atomic>
#include <optional>

using namespace llvm;
using namespace llvm::opt;
using namespace llvm::object;

// Various tools (e.g., llc and opt) duplicate this series of declarations for
// options related to passes and remarks.

static cl::opt<bool> RemarksWithHotness(
    "pass-remarks-with-hotness",
    cl::desc("With PGO, include profile count in optimization remarks"),
    cl::Hidden);

static cl::opt<std::optional<uint64_t>, false, remarks::HotnessThresholdParser>
    RemarksHotnessThreshold(
        "pass-remarks-hotness-threshold",
        cl::desc("Minimum profile count required for "
                 "an optimization remark to be output. "
                 "Use 'auto' to apply the threshold from profile summary."),
        cl::value_desc("N or 'auto'"), cl::init(0), cl::Hidden);

static cl::opt<std::string>
    RemarksFilename("pass-remarks-output",
                    cl::desc("Output filename for pass remarks"),
                    cl::value_desc("filename"));

static cl::opt<std::string>
    RemarksPasses("pass-remarks-filter",
                  cl::desc("Only record optimization remarks from passes whose "
                           "names match the given regular expression"),
                  cl::value_desc("regex"));

static cl::opt<std::string> RemarksFormat(
    "pass-remarks-format",
    cl::desc("The format used for serializing remarks (default: YAML)"),
    cl::value_desc("format"), cl::init("yaml"));

static cl::list<std::string>
    PassPlugins("load-pass-plugin",
                cl::desc("Load passes from plugin library"));

static cl::opt<std::string> PassPipeline(
    "passes",
    cl::desc(
        "A textual description of the pass pipeline. To have analysis passes "
        "available before a certain pass, add 'require<foo-analysis>'. "
        "'-passes' overrides the pass pipeline (but not all effects) from "
        "specifying '--opt-level=O?' (O2 is the default) to "
        "clang-linker-wrapper.  Be sure to include the corresponding "
        "'default<O?>' in '-passes'."));
static cl::alias PassPipeline2("p", cl::aliasopt(PassPipeline),
                               cl::desc("Alias for -passes"));

/// Path of the current binary.
static const char *LinkerExecutable;

/// Ssave intermediary results.
static bool SaveTemps = false;

/// Print arguments without executing.
static bool DryRun = false;

/// Print verbose output.
static bool Verbose = false;

/// Filename of the executable being created.
static StringRef ExecutableName;

/// Binary path for the CUDA installation.
static std::string CudaBinaryPath;

/// Mutex lock to protect writes to shared TempFiles in parallel.
static std::mutex TempFilesMutex;

/// Temporary files created by the linker wrapper.
static std::list<SmallString<128>> TempFiles;

/// Codegen flags for LTO backend.
static codegen::RegisterCodeGenFlags CodeGenFlags;

/// Global flag to indicate that the LTO pipeline threw an error.
static std::atomic<bool> LTOError;

using OffloadingImage = OffloadBinary::OffloadingImage;

namespace llvm {
// Provide DenseMapInfo so that OffloadKind can be used in a DenseMap.
template <> struct DenseMapInfo<OffloadKind> {
  static inline OffloadKind getEmptyKey() { return OFK_LAST; }
  static inline OffloadKind getTombstoneKey() {
    return static_cast<OffloadKind>(OFK_LAST + 1);
  }
  static unsigned getHashValue(const OffloadKind &Val) { return Val; }

  static bool isEqual(const OffloadKind &LHS, const OffloadKind &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

namespace {
using std::error_code;

/// Must not overlap with llvm::opt::DriverFlag.
enum WrapperFlags {
  WrapperOnlyOption = (1 << 4), // Options only used by the linker wrapper.
  DeviceOnlyOption = (1 << 5),  // Options only used for device linking.
};

enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "LinkerWrapperOpts.inc"
  LastOption
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "LinkerWrapperOpts.inc"
#undef PREFIX

static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "LinkerWrapperOpts.inc"
#undef OPTION
};

class WrapperOptTable : public opt::GenericOptTable {
public:
  WrapperOptTable() : opt::GenericOptTable(InfoTable) {}
};

const OptTable &getOptTable() {
  static const WrapperOptTable *Table = []() {
    auto Result = std::make_unique<WrapperOptTable>();
    return Result.release();
  }();
  return *Table;
}

void printCommands(ArrayRef<StringRef> CmdArgs) {
  if (CmdArgs.empty())
    return;

  llvm::errs() << " \"" << CmdArgs.front() << "\" ";
  for (auto IC = std::next(CmdArgs.begin()), IE = CmdArgs.end(); IC != IE; ++IC)
    llvm::errs() << *IC << (std::next(IC) != IE ? " " : "\n");
}

[[noreturn]] void reportError(Error E) {
  outs().flush();
  logAllUnhandledErrors(std::move(E),
                        WithColor::error(errs(), LinkerExecutable));
  exit(EXIT_FAILURE);
}

/// Create an extra user-specified \p OffloadFile.
/// TODO: We should find a way to wrap these as libraries instead.
Expected<OffloadFile> getInputBitcodeLibrary(StringRef Input) {
  auto [Device, Path] = StringRef(Input).split('=');
  auto [String, Arch] = Device.rsplit('-');
  auto [Kind, Triple] = String.split('-');

  llvm::ErrorOr<std::unique_ptr<MemoryBuffer>> ImageOrError =
      llvm::MemoryBuffer::getFileOrSTDIN(Path);
  if (std::error_code EC = ImageOrError.getError())
    return createFileError(Path, EC);

  OffloadingImage Image{};
  Image.TheImageKind = IMG_Bitcode;
  Image.TheOffloadKind = getOffloadKind(Kind);
  Image.StringData["triple"] = Triple;
  Image.StringData["arch"] = Arch;
  Image.Image = std::move(*ImageOrError);

  std::unique_ptr<MemoryBuffer> Binary =
      MemoryBuffer::getMemBufferCopy(OffloadBinary::write(Image));
  auto NewBinaryOrErr = OffloadBinary::create(*Binary);
  if (!NewBinaryOrErr)
    return NewBinaryOrErr.takeError();
  return OffloadFile(std::move(*NewBinaryOrErr), std::move(Binary));
}

std::string getMainExecutable(const char *Name) {
  void *Ptr = (void *)(intptr_t)&getMainExecutable;
  auto COWPath = sys::fs::getMainExecutable(Name, Ptr);
  return sys::path::parent_path(COWPath).str();
}

/// Get a temporary filename suitable for output.
Expected<StringRef> createOutputFile(const Twine &Prefix, StringRef Extension) {
  std::scoped_lock<decltype(TempFilesMutex)> Lock(TempFilesMutex);
  SmallString<128> OutputFile;
  if (SaveTemps) {
    (Prefix + "." + Extension).toNullTerminatedStringRef(OutputFile);
  } else {
    if (std::error_code EC =
            sys::fs::createTemporaryFile(Prefix, Extension, OutputFile))
      return createFileError(OutputFile, EC);
  }

  TempFiles.emplace_back(std::move(OutputFile));
  return TempFiles.back();
}

/// Execute the command \p ExecutablePath with the arguments \p Args.
Error executeCommands(StringRef ExecutablePath, ArrayRef<StringRef> Args) {
  if (Verbose || DryRun)
    printCommands(Args);

  if (!DryRun)
    if (sys::ExecuteAndWait(ExecutablePath, Args))
      return createStringError(
          "'%s' failed", sys::path::filename(ExecutablePath).str().c_str());
  return Error::success();
}

Expected<std::string> findProgram(StringRef Name, ArrayRef<StringRef> Paths) {

  ErrorOr<std::string> Path = sys::findProgramByName(Name, Paths);
  if (!Path)
    Path = sys::findProgramByName(Name);
  if (!Path && DryRun)
    return Name.str();
  if (!Path)
    return createStringError(Path.getError(),
                             "Unable to find '" + Name + "' in path");
  return *Path;
}

/// Returns the hashed value for a constant string.
std::string getHash(StringRef Str) {
  llvm::MD5 Hasher;
  llvm::MD5::MD5Result Hash;
  Hasher.update(Str);
  Hasher.final(Hash);
  return llvm::utohexstr(Hash.low(), /*LowerCase=*/true);
}

/// Renames offloading entry sections in a relocatable link so they do not
/// conflict with a later link job.
Error relocateOffloadSection(const ArgList &Args, StringRef Output) {
  llvm::Triple Triple(
      Args.getLastArgValue(OPT_host_triple_EQ, sys::getDefaultTargetTriple()));
  if (Triple.isOSWindows())
    return createStringError(
        "Relocatable linking is not supported on COFF targets");

  Expected<std::string> ObjcopyPath =
      findProgram("llvm-objcopy", {getMainExecutable("llvm-objcopy")});
  if (!ObjcopyPath)
    return ObjcopyPath.takeError();

  // Use the linker output file to get a unique hash. This creates a unique
  // identifier to rename the sections to that is deterministic to the contents.
  auto BufferOrErr = DryRun ? MemoryBuffer::getMemBuffer("")
                            : MemoryBuffer::getFileOrSTDIN(Output);
  if (!BufferOrErr)
    return createStringError("Failed to open %s", Output.str().c_str());
  std::string Suffix = "_" + getHash((*BufferOrErr)->getBuffer());

  SmallVector<StringRef> ObjcopyArgs = {
      *ObjcopyPath,
      Output,
  };

  // Remove the old .llvm.offloading section to prevent further linking.
  ObjcopyArgs.emplace_back("--remove-section");
  ObjcopyArgs.emplace_back(".llvm.offloading");
  for (StringRef Prefix : {"omp", "cuda", "hip"}) {
    auto Section = (Prefix + "_offloading_entries").str();
    // Rename the offloading entires to make them private to this link unit.
    ObjcopyArgs.emplace_back("--rename-section");
    ObjcopyArgs.emplace_back(
        Args.MakeArgString(Section + "=" + Section + Suffix));

    // Rename the __start_ / __stop_ symbols appropriately to iterate over the
    // newly renamed section containing the offloading entries.
    ObjcopyArgs.emplace_back("--redefine-sym");
    ObjcopyArgs.emplace_back(Args.MakeArgString("__start_" + Section + "=" +
                                                "__start_" + Section + Suffix));
    ObjcopyArgs.emplace_back("--redefine-sym");
    ObjcopyArgs.emplace_back(Args.MakeArgString("__stop_" + Section + "=" +
                                                "__stop_" + Section + Suffix));
  }

  if (Error Err = executeCommands(*ObjcopyPath, ObjcopyArgs))
    return Err;

  return Error::success();
}

/// Runs the wrapped linker job with the newly created input.
Error runLinker(ArrayRef<StringRef> Files, const ArgList &Args) {
  llvm::TimeTraceScope TimeScope("Execute host linker");

  // Render the linker arguments and add the newly created image. We add it
  // after the output file to ensure it is linked with the correct libraries.
  StringRef LinkerPath = Args.getLastArgValue(OPT_linker_path_EQ);
  ArgStringList NewLinkerArgs;
  for (const opt::Arg *Arg : Args) {
    // Do not forward arguments only intended for the linker wrapper.
    if (Arg->getOption().hasFlag(WrapperOnlyOption))
      continue;

    Arg->render(Args, NewLinkerArgs);
    if (Arg->getOption().matches(OPT_o) || Arg->getOption().matches(OPT_out))
      llvm::transform(Files, std::back_inserter(NewLinkerArgs),
                      [&](StringRef Arg) { return Args.MakeArgString(Arg); });
  }

  SmallVector<StringRef> LinkerArgs({LinkerPath});
  for (StringRef Arg : NewLinkerArgs)
    LinkerArgs.push_back(Arg);
  if (Error Err = executeCommands(LinkerPath, LinkerArgs))
    return Err;

  if (Args.hasArg(OPT_relocatable))
    return relocateOffloadSection(Args, ExecutableName);

  return Error::success();
}

void printVersion(raw_ostream &OS) {
  OS << clang::getClangToolFullVersion("clang-linker-wrapper") << '\n';
}

namespace nvptx {
Expected<StringRef>
fatbinary(ArrayRef<std::pair<StringRef, StringRef>> InputFiles,
          const ArgList &Args) {
  llvm::TimeTraceScope TimeScope("NVPTX fatbinary");
  // NVPTX uses the fatbinary program to bundle the linked images.
  Expected<std::string> FatBinaryPath =
      findProgram("fatbinary", {CudaBinaryPath + "/bin"});
  if (!FatBinaryPath)
    return FatBinaryPath.takeError();

  llvm::Triple Triple(
      Args.getLastArgValue(OPT_host_triple_EQ, sys::getDefaultTargetTriple()));

  // Create a new file to write the linked device image to.
  auto TempFileOrErr =
      createOutputFile(sys::path::filename(ExecutableName), "fatbin");
  if (!TempFileOrErr)
    return TempFileOrErr.takeError();

  SmallVector<StringRef, 16> CmdArgs;
  CmdArgs.push_back(*FatBinaryPath);
  CmdArgs.push_back(Triple.isArch64Bit() ? "-64" : "-32");
  CmdArgs.push_back("--create");
  CmdArgs.push_back(*TempFileOrErr);
  for (const auto &[File, Arch] : InputFiles)
    CmdArgs.push_back(
        Args.MakeArgString("--image=profile=" + Arch + ",file=" + File));

  if (Error Err = executeCommands(*FatBinaryPath, CmdArgs))
    return std::move(Err);

  return *TempFileOrErr;
}
} // namespace nvptx

namespace amdgcn {
Expected<StringRef>
fatbinary(ArrayRef<std::pair<StringRef, StringRef>> InputFiles,
          const ArgList &Args) {
  llvm::TimeTraceScope TimeScope("AMDGPU Fatbinary");

  // AMDGPU uses the clang-offload-bundler to bundle the linked images.
  Expected<std::string> OffloadBundlerPath = findProgram(
      "clang-offload-bundler", {getMainExecutable("clang-offload-bundler")});
  if (!OffloadBundlerPath)
    return OffloadBundlerPath.takeError();

  llvm::Triple Triple(
      Args.getLastArgValue(OPT_host_triple_EQ, sys::getDefaultTargetTriple()));

  // Create a new file to write the linked device image to.
  auto TempFileOrErr =
      createOutputFile(sys::path::filename(ExecutableName), "hipfb");
  if (!TempFileOrErr)
    return TempFileOrErr.takeError();

  BumpPtrAllocator Alloc;
  StringSaver Saver(Alloc);

  SmallVector<StringRef, 16> CmdArgs;
  CmdArgs.push_back(*OffloadBundlerPath);
  CmdArgs.push_back("-type=o");
  CmdArgs.push_back("-bundle-align=4096");

  if (Args.hasArg(OPT_compress))
    CmdArgs.push_back("-compress");
  if (auto *Arg = Args.getLastArg(OPT_compression_level_eq))
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-compression-level=") + Arg->getValue()));

  SmallVector<StringRef> Targets = {"-targets=host-x86_64-unknown-linux"};
  for (const auto &[File, Arch] : InputFiles)
    Targets.push_back(Saver.save("hip-amdgcn-amd-amdhsa--" + Arch));
  CmdArgs.push_back(Saver.save(llvm::join(Targets, ",")));

#ifdef _WIN32
  CmdArgs.push_back("-input=NUL");
#else
  CmdArgs.push_back("-input=/dev/null");
#endif
  for (const auto &[File, Arch] : InputFiles)
    CmdArgs.push_back(Saver.save("-input=" + File));

  CmdArgs.push_back(Saver.save("-output=" + *TempFileOrErr));

  if (Error Err = executeCommands(*OffloadBundlerPath, CmdArgs))
    return std::move(Err);

  return *TempFileOrErr;
}
} // namespace amdgcn

namespace generic {
Expected<StringRef> clang(ArrayRef<StringRef> InputFiles, const ArgList &Args) {
  llvm::TimeTraceScope TimeScope("Clang");
  // Use `clang` to invoke the appropriate device tools.
  Expected<std::string> ClangPath =
      findProgram("clang", {getMainExecutable("clang")});
  if (!ClangPath)
    return ClangPath.takeError();

  const llvm::Triple Triple(Args.getLastArgValue(OPT_triple_EQ));
  StringRef Arch = Args.getLastArgValue(OPT_arch_EQ);
  if (Arch.empty())
    Arch = "native";
  // Create a new file to write the linked device image to. Assume that the
  // input filename already has the device and architecture.
  auto TempFileOrErr =
      createOutputFile(sys::path::filename(ExecutableName) + "." +
                           Triple.getArchName() + "." + Arch,
                       "img");
  if (!TempFileOrErr)
    return TempFileOrErr.takeError();

  StringRef OptLevel = Args.getLastArgValue(OPT_opt_level, "O2");
  SmallVector<StringRef, 16> CmdArgs{
      *ClangPath,
      "--no-default-config",
      "-o",
      *TempFileOrErr,
      Args.MakeArgString("--target=" + Triple.getTriple()),
      Triple.isAMDGPU() ? Args.MakeArgString("-mcpu=" + Arch)
                        : Args.MakeArgString("-march=" + Arch),
      Args.MakeArgString("-" + OptLevel),
  };

  if (!Triple.isNVPTX())
    CmdArgs.push_back("-Wl,--no-undefined");

  for (StringRef InputFile : InputFiles)
    CmdArgs.push_back(InputFile);

  // If this is CPU offloading we copy the input libraries.
  if (!Triple.isAMDGPU() && !Triple.isNVPTX()) {
    CmdArgs.push_back("-Wl,-Bsymbolic");
    CmdArgs.push_back("-shared");
    ArgStringList LinkerArgs;
    for (const opt::Arg *Arg :
         Args.filtered(OPT_INPUT, OPT_library, OPT_library_path, OPT_rpath,
                       OPT_whole_archive, OPT_no_whole_archive)) {
      // Sometimes needed libraries are passed by name, such as when using
      // sanitizers. We need to check the file magic for any libraries.
      if (Arg->getOption().matches(OPT_INPUT)) {
        if (!sys::fs::exists(Arg->getValue()) ||
            sys::fs::is_directory(Arg->getValue()))
          continue;

        file_magic Magic;
        if (auto EC = identify_magic(Arg->getValue(), Magic))
          return createStringError("Failed to open %s", Arg->getValue());
        if (Magic != file_magic::archive &&
            Magic != file_magic::elf_shared_object)
          continue;
      }
      if (Arg->getOption().matches(OPT_whole_archive))
        LinkerArgs.push_back(Args.MakeArgString("-Wl,--whole-archive"));
      else if (Arg->getOption().matches(OPT_no_whole_archive))
        LinkerArgs.push_back(Args.MakeArgString("-Wl,--no-whole-archive"));
      else
        Arg->render(Args, LinkerArgs);
    }
    llvm::copy(LinkerArgs, std::back_inserter(CmdArgs));
  }

  // Pass on -mllvm options to the clang invocation.
  for (const opt::Arg *Arg : Args.filtered(OPT_mllvm)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Arg->getValue());
  }

  if (Args.hasArg(OPT_debug))
    CmdArgs.push_back("-g");

  if (SaveTemps)
    CmdArgs.push_back("-save-temps");

  if (Verbose)
    CmdArgs.push_back("-v");

  if (!CudaBinaryPath.empty())
    CmdArgs.push_back(Args.MakeArgString("--cuda-path=" + CudaBinaryPath));

  for (StringRef Arg : Args.getAllArgValues(OPT_ptxas_arg))
    llvm::copy(
        SmallVector<StringRef>({"-Xcuda-ptxas", Args.MakeArgString(Arg)}),
        std::back_inserter(CmdArgs));

  for (StringRef Arg : Args.getAllArgValues(OPT_linker_arg_EQ))
    CmdArgs.push_back(Args.MakeArgString(Arg));

  for (StringRef Arg : Args.getAllArgValues(OPT_builtin_bitcode_EQ)) {
    if (llvm::Triple(Arg.split('=').first) == Triple)
      CmdArgs.append({"-Xclang", "-mlink-builtin-bitcode", "-Xclang",
                      Args.MakeArgString(Arg.split('=').second)});
  }

  // The OpenMPOpt pass can introduce new calls and is expensive, we do not want
  // this when running CodeGen through clang.
  if (Args.hasArg(OPT_clang_backend) || Args.hasArg(OPT_builtin_bitcode_EQ))
    CmdArgs.append({"-mllvm", "-openmp-opt-disable"});

  if (Error Err = executeCommands(*ClangPath, CmdArgs))
    return std::move(Err);

  return *TempFileOrErr;
}
} // namespace generic

Expected<StringRef> linkDevice(ArrayRef<StringRef> InputFiles,
                               const ArgList &Args) {
  const llvm::Triple Triple(Args.getLastArgValue(OPT_triple_EQ));
  switch (Triple.getArch()) {
  case Triple::nvptx:
  case Triple::nvptx64:
  case Triple::amdgcn:
  case Triple::x86:
  case Triple::x86_64:
  case Triple::aarch64:
  case Triple::aarch64_be:
  case Triple::ppc64:
  case Triple::ppc64le:
  case Triple::systemz:
    return generic::clang(InputFiles, Args);
  default:
    return createStringError(Triple.getArchName() +
                             " linking is not supported");
  }
}

void diagnosticHandler(const DiagnosticInfo &DI) {
  std::string ErrStorage;
  raw_string_ostream OS(ErrStorage);
  DiagnosticPrinterRawOStream DP(OS);
  DI.print(DP);

  switch (DI.getSeverity()) {
  case DS_Error:
    WithColor::error(errs(), LinkerExecutable) << ErrStorage << "\n";
    LTOError = true;
    break;
  case DS_Warning:
    WithColor::warning(errs(), LinkerExecutable) << ErrStorage << "\n";
    break;
  case DS_Note:
    WithColor::note(errs(), LinkerExecutable) << ErrStorage << "\n";
    break;
  case DS_Remark:
    WithColor::remark(errs()) << ErrStorage << "\n";
    break;
  }
}

// Get the list of target features from the input file and unify them such that
// if there are multiple +xxx or -xxx features we only keep the last one.
std::vector<std::string> getTargetFeatures(ArrayRef<OffloadFile> InputFiles) {
  SmallVector<StringRef> Features;
  for (const OffloadFile &File : InputFiles) {
    for (auto Arg : llvm::split(File.getBinary()->getString("feature"), ","))
      Features.emplace_back(Arg);
  }

  // Only add a feature if it hasn't been seen before starting from the end.
  std::vector<std::string> UnifiedFeatures;
  DenseSet<StringRef> UsedFeatures;
  for (StringRef Feature : llvm::reverse(Features)) {
    if (UsedFeatures.insert(Feature.drop_front()).second)
      UnifiedFeatures.push_back(Feature.str());
  }

  return UnifiedFeatures;
}

template <typename ModuleHook = function_ref<bool(size_t, const Module &)>>
std::unique_ptr<lto::LTO> createLTO(
    const ArgList &Args, const std::vector<std::string> &Features,
    ModuleHook Hook = [](size_t, const Module &) { return true; }) {
  const llvm::Triple Triple(Args.getLastArgValue(OPT_triple_EQ));
  // We need to remove AMD's target-id from the processor if present.
  StringRef TargetID = Args.getLastArgValue(OPT_arch_EQ);
  StringRef Arch = clang::getProcessorFromTargetID(Triple, TargetID);
  lto::Config Conf;
  lto::ThinBackend Backend;
  // TODO: Handle index-only thin-LTO
  Backend =
      lto::createInProcessThinBackend(llvm::heavyweight_hardware_concurrency());

  Conf.CPU = Arch.str();
  Conf.Options = codegen::InitTargetOptionsFromCodeGenFlags(Triple);

  Conf.RemarksFilename = RemarksFilename;
  Conf.RemarksPasses = RemarksPasses;
  Conf.RemarksWithHotness = RemarksWithHotness;
  Conf.RemarksHotnessThreshold = RemarksHotnessThreshold;
  Conf.RemarksFormat = RemarksFormat;

  StringRef OptLevel = Args.getLastArgValue(OPT_opt_level, "O2");
  Conf.MAttrs = Features;
  std::optional<CodeGenOptLevel> CGOptLevelOrNone =
      CodeGenOpt::parseLevel(OptLevel[1]);
  assert(CGOptLevelOrNone && "Invalid optimization level");
  Conf.CGOptLevel = *CGOptLevelOrNone;
  Conf.OptLevel = OptLevel[1] - '0';
  Conf.DefaultTriple = Triple.getTriple();

  // TODO: Should we complain about combining --opt-level and -passes, as opt
  // does?  That might be too limiting in clang-linker-wrapper, so for now we
  // just warn in the help entry for -passes that the default<O?> corresponding
  // to --opt-level=O? should be included there.  The problem is that
  // --opt-level produces effects in clang-linker-wrapper beyond what -passes
  // appears to be able to achieve, so rejecting the combination of --opt-level
  // and -passes would apparently make it impossible to combine those effects
  // with a custom pass pipeline.
  Conf.OptPipeline = PassPipeline;
  Conf.PassPlugins = PassPlugins;

  LTOError = false;
  Conf.DiagHandler = diagnosticHandler;

  Conf.PTO.LoopVectorization = Conf.OptLevel > 1;
  Conf.PTO.SLPVectorization = Conf.OptLevel > 1;

  if (SaveTemps) {
    std::string TempName = (sys::path::filename(ExecutableName) + "." +
                            Triple.getTriple() + "." + TargetID)
                               .str();
    Conf.PostInternalizeModuleHook = [=](size_t Task, const Module &M) {
      std::string File =
          !Task ? TempName + ".postlink.bc"
                : TempName + "." + std::to_string(Task) + ".postlink.bc";
      error_code EC;
      raw_fd_ostream LinkedBitcode(File, EC, sys::fs::OF_None);
      if (EC)
        reportError(errorCodeToError(EC));
      WriteBitcodeToFile(M, LinkedBitcode);
      return true;
    };
    Conf.PreCodeGenModuleHook = [=](size_t Task, const Module &M) {
      std::string File =
          !Task ? TempName + ".postopt.bc"
                : TempName + "." + std::to_string(Task) + ".postopt.bc";
      error_code EC;
      raw_fd_ostream LinkedBitcode(File, EC, sys::fs::OF_None);
      if (EC)
        reportError(errorCodeToError(EC));
      WriteBitcodeToFile(M, LinkedBitcode);
      return true;
    };
  }
  Conf.PostOptModuleHook = Hook;
  Conf.CGFileType = (Triple.isNVPTX() || SaveTemps)
                        ? CodeGenFileType::AssemblyFile
                        : CodeGenFileType::ObjectFile;

  // TODO: Handle remark files
  Conf.HasWholeProgramVisibility = Args.hasArg(OPT_whole_program);

  return std::make_unique<lto::LTO>(std::move(Conf), Backend);
}

// Returns true if \p S is valid as a C language identifier and will be given
// `__start_` and `__stop_` symbols.
bool isValidCIdentifier(StringRef S) {
  return !S.empty() && (isAlpha(S[0]) || S[0] == '_') &&
         llvm::all_of(llvm::drop_begin(S),
                      [](char C) { return C == '_' || isAlnum(C); });
}

Error linkBitcodeFiles(SmallVectorImpl<OffloadFile> &InputFiles,
                       SmallVectorImpl<StringRef> &OutputFiles,
                       const ArgList &Args) {
  llvm::TimeTraceScope TimeScope("Link bitcode files");
  const llvm::Triple Triple(Args.getLastArgValue(OPT_triple_EQ));
  StringRef Arch = Args.getLastArgValue(OPT_arch_EQ);

  SmallVector<OffloadFile, 4> BitcodeInputFiles;
  DenseSet<StringRef> StrongResolutions;
  DenseSet<StringRef> UsedInRegularObj;
  DenseSet<StringRef> UsedInSharedLib;
  BumpPtrAllocator Alloc;
  StringSaver Saver(Alloc);

  // Search for bitcode files in the input and create an LTO input file. If it
  // is not a bitcode file, scan its symbol table for symbols we need to save.
  for (OffloadFile &File : InputFiles) {
    MemoryBufferRef Buffer = MemoryBufferRef(File.getBinary()->getImage(), "");

    file_magic Type = identify_magic(Buffer.getBuffer());
    switch (Type) {
    case file_magic::bitcode: {
      Expected<IRSymtabFile> IRSymtabOrErr = readIRSymtab(Buffer);
      if (!IRSymtabOrErr)
        return IRSymtabOrErr.takeError();

      // Check for any strong resolutions we need to preserve.
      for (unsigned I = 0; I != IRSymtabOrErr->Mods.size(); ++I) {
        for (const auto &Sym : IRSymtabOrErr->TheReader.module_symbols(I)) {
          if (!Sym.isFormatSpecific() && Sym.isGlobal() && !Sym.isWeak() &&
              !Sym.isUndefined())
            StrongResolutions.insert(Saver.save(Sym.Name));
        }
      }
      BitcodeInputFiles.emplace_back(std::move(File));
      continue;
    }
    case file_magic::elf_relocatable:
    case file_magic::elf_shared_object: {
      Expected<std::unique_ptr<ObjectFile>> ObjFile =
          ObjectFile::createObjectFile(Buffer);
      if (!ObjFile)
        continue;

      for (SymbolRef Sym : (*ObjFile)->symbols()) {
        Expected<StringRef> Name = Sym.getName();
        if (!Name)
          return Name.takeError();

        // Record if we've seen these symbols in any object or shared libraries.
        if ((*ObjFile)->isRelocatableObject())
          UsedInRegularObj.insert(Saver.save(*Name));
        else
          UsedInSharedLib.insert(Saver.save(*Name));
      }
      continue;
    }
    default:
      continue;
    }
  }

  if (BitcodeInputFiles.empty())
    return Error::success();

  // Remove all the bitcode files that we moved from the original input.
  llvm::erase_if(InputFiles, [](OffloadFile &F) { return !F.getBinary(); });

  // LTO Module hook to output bitcode without running the backend.
  SmallVector<StringRef> BitcodeOutput;
  auto OutputBitcode = [&](size_t, const Module &M) {
    auto TempFileOrErr = createOutputFile(sys::path::filename(ExecutableName) +
                                              "-jit-" + Triple.getTriple(),
                                          "bc");
    if (!TempFileOrErr)
      reportError(TempFileOrErr.takeError());

    std::error_code EC;
    raw_fd_ostream LinkedBitcode(*TempFileOrErr, EC, sys::fs::OF_None);
    if (EC)
      reportError(errorCodeToError(EC));
    WriteBitcodeToFile(M, LinkedBitcode);
    BitcodeOutput.push_back(*TempFileOrErr);
    return false;
  };

  // We assume visibility of the whole program if every input file was bitcode.
  auto Features = getTargetFeatures(BitcodeInputFiles);
  auto LTOBackend = Args.hasArg(OPT_embed_bitcode) ||
                            Args.hasArg(OPT_builtin_bitcode_EQ) ||
                            Args.hasArg(OPT_clang_backend)
                        ? createLTO(Args, Features, OutputBitcode)
                        : createLTO(Args, Features);

  // We need to resolve the symbols so the LTO backend knows which symbols need
  // to be kept or can be internalized. This is a simplified symbol resolution
  // scheme to approximate the full resolution a linker would do.
  uint64_t Idx = 0;
  DenseSet<StringRef> PrevailingSymbols;
  for (auto &BitcodeInput : BitcodeInputFiles) {
    // Get a semi-unique buffer identifier for Thin-LTO.
    StringRef Identifier = Saver.save(
        std::to_string(Idx++) + "." +
        BitcodeInput.getBinary()->getMemoryBufferRef().getBufferIdentifier());
    MemoryBufferRef Buffer =
        MemoryBufferRef(BitcodeInput.getBinary()->getImage(), Identifier);
    Expected<std::unique_ptr<lto::InputFile>> BitcodeFileOrErr =
        llvm::lto::InputFile::create(Buffer);
    if (!BitcodeFileOrErr)
      return BitcodeFileOrErr.takeError();

    // Save the input file and the buffer associated with its memory.
    const auto Symbols = (*BitcodeFileOrErr)->symbols();
    SmallVector<lto::SymbolResolution, 16> Resolutions(Symbols.size());
    size_t Idx = 0;
    for (auto &Sym : Symbols) {
      lto::SymbolResolution &Res = Resolutions[Idx++];

      // We will use this as the prevailing symbol definition in LTO unless
      // it is undefined or another definition has already been used.
      Res.Prevailing =
          !Sym.isUndefined() &&
          !(Sym.isWeak() && StrongResolutions.contains(Sym.getName())) &&
          PrevailingSymbols.insert(Saver.save(Sym.getName())).second;

      // We need LTO to preseve the following global symbols:
      // 1) Symbols used in regular objects.
      // 2) Sections that will be given a __start/__stop symbol.
      // 3) Prevailing symbols that are needed visible to external libraries.
      Res.VisibleToRegularObj =
          UsedInRegularObj.contains(Sym.getName()) ||
          isValidCIdentifier(Sym.getSectionName()) ||
          (Res.Prevailing &&
           (Sym.getVisibility() != GlobalValue::HiddenVisibility &&
            !Sym.canBeOmittedFromSymbolTable()));

      // Identify symbols that must be exported dynamically and can be
      // referenced by other files.
      Res.ExportDynamic =
          Sym.getVisibility() != GlobalValue::HiddenVisibility &&
          (UsedInSharedLib.contains(Sym.getName()) ||
           !Sym.canBeOmittedFromSymbolTable());

      // The final definition will reside in this linkage unit if the symbol is
      // defined and local to the module. This only checks for bitcode files,
      // full assertion will require complete symbol resolution.
      Res.FinalDefinitionInLinkageUnit =
          Sym.getVisibility() != GlobalValue::DefaultVisibility &&
          (!Sym.isUndefined() && !Sym.isCommon());

      // We do not support linker redefined symbols (e.g. --wrap) for device
      // image linking, so the symbols will not be changed after LTO.
      Res.LinkerRedefined = false;
    }

    // Add the bitcode file with its resolved symbols to the LTO job.
    if (Error Err = LTOBackend->add(std::move(*BitcodeFileOrErr), Resolutions))
      return Err;
  }

  // Run the LTO job to compile the bitcode.
  size_t MaxTasks = LTOBackend->getMaxTasks();
  SmallVector<StringRef> Files(MaxTasks);
  auto AddStream =
      [&](size_t Task,
          const Twine &ModuleName) -> std::unique_ptr<CachedFileStream> {
    int FD = -1;
    auto &TempFile = Files[Task];
    StringRef Extension = (Triple.isNVPTX() || SaveTemps) ? "s" : "o";
    std::string TaskStr = Task ? "." + std::to_string(Task) : "";
    auto TempFileOrErr =
        createOutputFile(sys::path::filename(ExecutableName) + "." +
                             Triple.getTriple() + "." + Arch + TaskStr,
                         Extension);
    if (!TempFileOrErr)
      reportError(TempFileOrErr.takeError());
    TempFile = *TempFileOrErr;
    if (std::error_code EC = sys::fs::openFileForWrite(TempFile, FD))
      reportError(errorCodeToError(EC));
    return std::make_unique<CachedFileStream>(
        std::make_unique<llvm::raw_fd_ostream>(FD, true));
  };

  if (Error Err = LTOBackend->run(AddStream))
    return Err;

  if (LTOError)
    return createStringError("Errors encountered inside the LTO pipeline.");

  // If we are embedding bitcode we only need the intermediate output.
  bool SingleOutput = Files.size() == 1;
  if (Args.hasArg(OPT_embed_bitcode)) {
    if (BitcodeOutput.size() != 1 || !SingleOutput)
      return createStringError("Cannot embed bitcode with multiple files.");
    OutputFiles.push_back(Args.MakeArgString(BitcodeOutput.front()));
    return Error::success();
  }

  // Append the new inputs to the device linker input. If the user requested an
  // internalizing link we need to pass the bitcode to clang.
  for (StringRef File :
       Args.hasArg(OPT_clang_backend) || Args.hasArg(OPT_builtin_bitcode_EQ)
           ? BitcodeOutput
           : Files)
    OutputFiles.push_back(File);

  return Error::success();
}

Expected<StringRef> writeOffloadFile(const OffloadFile &File) {
  const OffloadBinary &Binary = *File.getBinary();

  StringRef Prefix =
      sys::path::stem(Binary.getMemoryBufferRef().getBufferIdentifier());
  StringRef Suffix = getImageKindName(Binary.getImageKind());

  auto TempFileOrErr = createOutputFile(
      Prefix + "-" + Binary.getTriple() + "-" + Binary.getArch(), Suffix);
  if (!TempFileOrErr)
    return TempFileOrErr.takeError();

  Expected<std::unique_ptr<FileOutputBuffer>> OutputOrErr =
      FileOutputBuffer::create(*TempFileOrErr, Binary.getImage().size());
  if (!OutputOrErr)
    return OutputOrErr.takeError();
  std::unique_ptr<FileOutputBuffer> Output = std::move(*OutputOrErr);
  llvm::copy(Binary.getImage(), Output->getBufferStart());
  if (Error E = Output->commit())
    return std::move(E);

  return *TempFileOrErr;
}

// Compile the module to an object file using the appropriate target machine for
// the host triple.
Expected<StringRef> compileModule(Module &M, OffloadKind Kind) {
  llvm::TimeTraceScope TimeScope("Compile module");
  std::string Msg;
  const Target *T = TargetRegistry::lookupTarget(M.getTargetTriple(), Msg);
  if (!T)
    return createStringError(Msg);

  auto Options =
      codegen::InitTargetOptionsFromCodeGenFlags(Triple(M.getTargetTriple()));
  StringRef CPU = "";
  StringRef Features = "";
  std::unique_ptr<TargetMachine> TM(
      T->createTargetMachine(M.getTargetTriple(), CPU, Features, Options,
                             Reloc::PIC_, M.getCodeModel()));

  if (M.getDataLayout().isDefault())
    M.setDataLayout(TM->createDataLayout());

  int FD = -1;
  auto TempFileOrErr =
      createOutputFile(sys::path::filename(ExecutableName) + "." +
                           getOffloadKindName(Kind) + ".image.wrapper",
                       "o");
  if (!TempFileOrErr)
    return TempFileOrErr.takeError();
  if (std::error_code EC = sys::fs::openFileForWrite(*TempFileOrErr, FD))
    return errorCodeToError(EC);

  auto OS = std::make_unique<llvm::raw_fd_ostream>(FD, true);

  legacy::PassManager CodeGenPasses;
  TargetLibraryInfoImpl TLII(Triple(M.getTargetTriple()));
  CodeGenPasses.add(new TargetLibraryInfoWrapperPass(TLII));
  if (TM->addPassesToEmitFile(CodeGenPasses, *OS, nullptr,
                              CodeGenFileType::ObjectFile))
    return createStringError("Failed to execute host backend");
  CodeGenPasses.run(M);

  return *TempFileOrErr;
}

/// Creates the object file containing the device image and runtime
/// registration code from the device images stored in \p Images.
Expected<StringRef>
wrapDeviceImages(ArrayRef<std::unique_ptr<MemoryBuffer>> Buffers,
                 const ArgList &Args, OffloadKind Kind) {
  llvm::TimeTraceScope TimeScope("Wrap bundled images");

  SmallVector<ArrayRef<char>, 4> BuffersToWrap;
  for (const auto &Buffer : Buffers)
    BuffersToWrap.emplace_back(
        ArrayRef<char>(Buffer->getBufferStart(), Buffer->getBufferSize()));

  LLVMContext Context;
  Module M("offload.wrapper.module", Context);
  M.setTargetTriple(
      Args.getLastArgValue(OPT_host_triple_EQ, sys::getDefaultTargetTriple()));

  switch (Kind) {
  case OFK_OpenMP:
    if (Error Err = offloading::wrapOpenMPBinaries(
            M, BuffersToWrap,
            offloading::getOffloadEntryArray(M, "omp_offloading_entries"),
            /*Suffix=*/"", /*Relocatable=*/Args.hasArg(OPT_relocatable)))
      return std::move(Err);
    break;
  case OFK_Cuda:
    if (Error Err = offloading::wrapCudaBinary(
            M, BuffersToWrap.front(),
            offloading::getOffloadEntryArray(M, "cuda_offloading_entries"),
            /*Suffix=*/"", /*EmitSurfacesAndTextures=*/false))
      return std::move(Err);
    break;
  case OFK_HIP:
    if (Error Err = offloading::wrapHIPBinary(
            M, BuffersToWrap.front(),
            offloading::getOffloadEntryArray(M, "hip_offloading_entries")))
      return std::move(Err);
    break;
  default:
    return createStringError(getOffloadKindName(Kind) +
                             " wrapping is not supported");
  }

  if (Args.hasArg(OPT_print_wrapped_module))
    errs() << M;
  if (Args.hasArg(OPT_save_temps)) {
    int FD = -1;
    auto TempFileOrErr =
        createOutputFile(sys::path::filename(ExecutableName) + "." +
                             getOffloadKindName(Kind) + ".image.wrapper",
                         "bc");
    if (!TempFileOrErr)
      return TempFileOrErr.takeError();
    if (std::error_code EC = sys::fs::openFileForWrite(*TempFileOrErr, FD))
      return errorCodeToError(EC);
    llvm::raw_fd_ostream OS(FD, true);
    WriteBitcodeToFile(M, OS);
  }

  auto FileOrErr = compileModule(M, Kind);
  if (!FileOrErr)
    return FileOrErr.takeError();
  return *FileOrErr;
}

Expected<SmallVector<std::unique_ptr<MemoryBuffer>>>
bundleOpenMP(ArrayRef<OffloadingImage> Images) {
  SmallVector<std::unique_ptr<MemoryBuffer>> Buffers;
  for (const OffloadingImage &Image : Images)
    Buffers.emplace_back(
        MemoryBuffer::getMemBufferCopy(OffloadBinary::write(Image)));

  return std::move(Buffers);
}

Expected<SmallVector<std::unique_ptr<MemoryBuffer>>>
bundleCuda(ArrayRef<OffloadingImage> Images, const ArgList &Args) {
  SmallVector<std::pair<StringRef, StringRef>, 4> InputFiles;
  for (const OffloadingImage &Image : Images)
    InputFiles.emplace_back(std::make_pair(Image.Image->getBufferIdentifier(),
                                           Image.StringData.lookup("arch")));

  Triple TheTriple = Triple(Images.front().StringData.lookup("triple"));
  auto FileOrErr = nvptx::fatbinary(InputFiles, Args);
  if (!FileOrErr)
    return FileOrErr.takeError();

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ImageOrError =
      llvm::MemoryBuffer::getFileOrSTDIN(*FileOrErr);

  SmallVector<std::unique_ptr<MemoryBuffer>> Buffers;
  if (std::error_code EC = ImageOrError.getError())
    return createFileError(*FileOrErr, EC);
  Buffers.emplace_back(std::move(*ImageOrError));

  return std::move(Buffers);
}

Expected<SmallVector<std::unique_ptr<MemoryBuffer>>>
bundleHIP(ArrayRef<OffloadingImage> Images, const ArgList &Args) {
  SmallVector<std::pair<StringRef, StringRef>, 4> InputFiles;
  for (const OffloadingImage &Image : Images)
    InputFiles.emplace_back(std::make_pair(Image.Image->getBufferIdentifier(),
                                           Image.StringData.lookup("arch")));

  Triple TheTriple = Triple(Images.front().StringData.lookup("triple"));
  auto FileOrErr = amdgcn::fatbinary(InputFiles, Args);
  if (!FileOrErr)
    return FileOrErr.takeError();

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ImageOrError =
      llvm::MemoryBuffer::getFileOrSTDIN(*FileOrErr);

  SmallVector<std::unique_ptr<MemoryBuffer>> Buffers;
  if (std::error_code EC = ImageOrError.getError())
    return createFileError(*FileOrErr, EC);
  Buffers.emplace_back(std::move(*ImageOrError));

  return std::move(Buffers);
}

/// Transforms the input \p Images into the binary format the runtime expects
/// for the given \p Kind.
Expected<SmallVector<std::unique_ptr<MemoryBuffer>>>
bundleLinkedOutput(ArrayRef<OffloadingImage> Images, const ArgList &Args,
                   OffloadKind Kind) {
  llvm::TimeTraceScope TimeScope("Bundle linked output");
  switch (Kind) {
  case OFK_OpenMP:
    return bundleOpenMP(Images);
  case OFK_Cuda:
    return bundleCuda(Images, Args);
  case OFK_HIP:
    return bundleHIP(Images, Args);
  default:
    return createStringError(getOffloadKindName(Kind) +
                             " bundling is not supported");
  }
}

/// Returns a new ArgList containg arguments used for the device linking phase.
DerivedArgList getLinkerArgs(ArrayRef<OffloadFile> Input,
                             const InputArgList &Args) {
  DerivedArgList DAL = DerivedArgList(DerivedArgList(Args));
  for (Arg *A : Args)
    DAL.append(A);

  // Set the subarchitecture and target triple for this compilation.
  const OptTable &Tbl = getOptTable();
  DAL.AddJoinedArg(nullptr, Tbl.getOption(OPT_arch_EQ),
                   Args.MakeArgString(Input.front().getBinary()->getArch()));
  DAL.AddJoinedArg(nullptr, Tbl.getOption(OPT_triple_EQ),
                   Args.MakeArgString(Input.front().getBinary()->getTriple()));

  // If every input file is bitcode we have whole program visibility as we do
  // only support static linking with bitcode.
  auto ContainsBitcode = [](const OffloadFile &F) {
    return identify_magic(F.getBinary()->getImage()) == file_magic::bitcode;
  };
  if (llvm::all_of(Input, ContainsBitcode))
    DAL.AddFlagArg(nullptr, Tbl.getOption(OPT_whole_program));

  // Forward '-Xoffload-linker' options to the appropriate backend.
  for (StringRef Arg : Args.getAllArgValues(OPT_device_linker_args_EQ)) {
    auto [Triple, Value] = Arg.split('=');
    if (Value.empty())
      DAL.AddJoinedArg(nullptr, Tbl.getOption(OPT_linker_arg_EQ),
                       Args.MakeArgString(Triple));
    else if (Triple == DAL.getLastArgValue(OPT_triple_EQ))
      DAL.AddJoinedArg(nullptr, Tbl.getOption(OPT_linker_arg_EQ),
                       Args.MakeArgString(Value));
  }

  return DAL;
}

Error handleOverrideImages(
    const InputArgList &Args,
    MapVector<OffloadKind, SmallVector<OffloadingImage, 0>> &Images) {
  for (StringRef Arg : Args.getAllArgValues(OPT_override_image)) {
    OffloadKind Kind = getOffloadKind(Arg.split("=").first);
    StringRef Filename = Arg.split("=").second;

    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
        MemoryBuffer::getFileOrSTDIN(Filename);
    if (std::error_code EC = BufferOrErr.getError())
      return createFileError(Filename, EC);

    Expected<std::unique_ptr<ObjectFile>> ElfOrErr =
        ObjectFile::createELFObjectFile(**BufferOrErr,
                                        /*InitContent=*/false);
    if (!ElfOrErr)
      return ElfOrErr.takeError();
    ObjectFile &Elf = **ElfOrErr;

    OffloadingImage TheImage{};
    TheImage.TheImageKind = IMG_Object;
    TheImage.TheOffloadKind = Kind;
    TheImage.StringData["triple"] =
        Args.MakeArgString(Elf.makeTriple().getTriple());
    if (std::optional<StringRef> CPU = Elf.tryGetCPUName())
      TheImage.StringData["arch"] = Args.MakeArgString(*CPU);
    TheImage.Image = std::move(*BufferOrErr);

    Images[Kind].emplace_back(std::move(TheImage));
  }
  return Error::success();
}

/// Transforms all the extracted offloading input files into an image that can
/// be registered by the runtime.
Expected<SmallVector<StringRef>> linkAndWrapDeviceFiles(
    SmallVectorImpl<SmallVector<OffloadFile>> &LinkerInputFiles,
    const InputArgList &Args, char **Argv, int Argc) {
  llvm::TimeTraceScope TimeScope("Handle all device input");

  std::mutex ImageMtx;
  MapVector<OffloadKind, SmallVector<OffloadingImage, 0>> Images;

  // Initialize the images with any overriding inputs.
  if (Args.hasArg(OPT_override_image))
    if (Error Err = handleOverrideImages(Args, Images))
      return std::move(Err);

  auto Err = parallelForEachError(LinkerInputFiles, [&](auto &Input) -> Error {
    llvm::TimeTraceScope TimeScope("Link device input");

    // Each thread needs its own copy of the base arguments to maintain
    // per-device argument storage of synthetic strings.
    const OptTable &Tbl = getOptTable();
    BumpPtrAllocator Alloc;
    StringSaver Saver(Alloc);
    auto BaseArgs =
        Tbl.parseArgs(Argc, Argv, OPT_INVALID, Saver, [](StringRef Err) {
          reportError(createStringError(Err));
        });
    auto LinkerArgs = getLinkerArgs(Input, BaseArgs);

    DenseSet<OffloadKind> ActiveOffloadKinds;
    for (const auto &File : Input)
      if (File.getBinary()->getOffloadKind() != OFK_None)
        ActiveOffloadKinds.insert(File.getBinary()->getOffloadKind());

    // First link and remove all the input files containing bitcode.
    SmallVector<StringRef> InputFiles;
    if (Error Err = linkBitcodeFiles(Input, InputFiles, LinkerArgs))
      return Err;

    // Write any remaining device inputs to an output file for the linker.
    for (const OffloadFile &File : Input) {
      auto FileNameOrErr = writeOffloadFile(File);
      if (!FileNameOrErr)
        return FileNameOrErr.takeError();
      InputFiles.emplace_back(*FileNameOrErr);
    }

    // Link the remaining device files using the device linker.
    auto OutputOrErr = !Args.hasArg(OPT_embed_bitcode)
                           ? linkDevice(InputFiles, LinkerArgs)
                           : InputFiles.front();
    if (!OutputOrErr)
      return OutputOrErr.takeError();

    // Store the offloading image for each linked output file.
    for (OffloadKind Kind : ActiveOffloadKinds) {
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> FileOrErr =
          llvm::MemoryBuffer::getFileOrSTDIN(*OutputOrErr);
      if (std::error_code EC = FileOrErr.getError()) {
        if (DryRun)
          FileOrErr = MemoryBuffer::getMemBuffer("");
        else
          return createFileError(*OutputOrErr, EC);
      }

      std::scoped_lock<decltype(ImageMtx)> Guard(ImageMtx);
      OffloadingImage TheImage{};
      TheImage.TheImageKind =
          Args.hasArg(OPT_embed_bitcode) ? IMG_Bitcode : IMG_Object;
      TheImage.TheOffloadKind = Kind;
      TheImage.StringData["triple"] =
          Args.MakeArgString(LinkerArgs.getLastArgValue(OPT_triple_EQ));
      TheImage.StringData["arch"] =
          Args.MakeArgString(LinkerArgs.getLastArgValue(OPT_arch_EQ));
      TheImage.Image = std::move(*FileOrErr);

      Images[Kind].emplace_back(std::move(TheImage));
    }
    return Error::success();
  });
  if (Err)
    return std::move(Err);

  // Create a binary image of each offloading image and embed it into a new
  // object file.
  SmallVector<StringRef> WrappedOutput;
  for (auto &[Kind, Input] : Images) {
    // We sort the entries before bundling so they appear in a deterministic
    // order in the final binary.
    llvm::sort(Input, [](OffloadingImage &A, OffloadingImage &B) {
      return A.StringData["triple"] > B.StringData["triple"] ||
             A.StringData["arch"] > B.StringData["arch"] ||
             A.TheOffloadKind < B.TheOffloadKind;
    });
    auto BundledImagesOrErr = bundleLinkedOutput(Input, Args, Kind);
    if (!BundledImagesOrErr)
      return BundledImagesOrErr.takeError();
    auto OutputOrErr = wrapDeviceImages(*BundledImagesOrErr, Args, Kind);
    if (!OutputOrErr)
      return OutputOrErr.takeError();
    WrappedOutput.push_back(*OutputOrErr);
  }

  return WrappedOutput;
}

std::optional<std::string> findFile(StringRef Dir, StringRef Root,
                                    const Twine &Name) {
  SmallString<128> Path;
  if (Dir.starts_with("="))
    sys::path::append(Path, Root, Dir.substr(1), Name);
  else
    sys::path::append(Path, Dir, Name);

  if (sys::fs::exists(Path))
    return static_cast<std::string>(Path);
  return std::nullopt;
}

std::optional<std::string>
findFromSearchPaths(StringRef Name, StringRef Root,
                    ArrayRef<StringRef> SearchPaths) {
  for (StringRef Dir : SearchPaths)
    if (std::optional<std::string> File = findFile(Dir, Root, Name))
      return File;
  return std::nullopt;
}

std::optional<std::string>
searchLibraryBaseName(StringRef Name, StringRef Root,
                      ArrayRef<StringRef> SearchPaths) {
  for (StringRef Dir : SearchPaths) {
    if (std::optional<std::string> File =
            findFile(Dir, Root, "lib" + Name + ".so"))
      return File;
    if (std::optional<std::string> File =
            findFile(Dir, Root, "lib" + Name + ".a"))
      return File;
  }
  return std::nullopt;
}

/// Search for static libraries in the linker's library path given input like
/// `-lfoo` or `-l:libfoo.a`.
std::optional<std::string> searchLibrary(StringRef Input, StringRef Root,
                                         ArrayRef<StringRef> SearchPaths) {
  if (Input.starts_with(":") || Input.ends_with(".lib"))
    return findFromSearchPaths(Input.drop_front(), Root, SearchPaths);
  return searchLibraryBaseName(Input, Root, SearchPaths);
}

/// Common redeclaration of needed symbol flags.
enum Symbol : uint32_t {
  Sym_None = 0,
  Sym_Undefined = 1U << 1,
  Sym_Weak = 1U << 2,
};

/// Scan the symbols from a BitcodeFile \p Buffer and record if we need to
/// extract any symbols from it.
Expected<bool> getSymbolsFromBitcode(MemoryBufferRef Buffer, OffloadKind Kind,
                                     bool IsArchive, StringSaver &Saver,
                                     DenseMap<StringRef, Symbol> &Syms) {
  Expected<IRSymtabFile> IRSymtabOrErr = readIRSymtab(Buffer);
  if (!IRSymtabOrErr)
    return IRSymtabOrErr.takeError();

  bool ShouldExtract = !IsArchive;
  DenseMap<StringRef, Symbol> TmpSyms;
  for (unsigned I = 0; I != IRSymtabOrErr->Mods.size(); ++I) {
    for (const auto &Sym : IRSymtabOrErr->TheReader.module_symbols(I)) {
      if (Sym.isFormatSpecific() || !Sym.isGlobal())
        continue;

      bool NewSymbol = Syms.count(Sym.getName()) == 0;
      auto OldSym = NewSymbol ? Sym_None : Syms[Sym.getName()];

      // We will extract if it defines a currenlty undefined non-weak symbol.
      bool ResolvesStrongReference =
          ((OldSym & Sym_Undefined && !(OldSym & Sym_Weak)) &&
           !Sym.isUndefined());
      // We will extract if it defines a new global symbol visible to the host.
      // This is only necessary for code targeting an offloading language.
      bool NewGlobalSymbol =
          ((NewSymbol || (OldSym & Sym_Undefined)) && !Sym.isUndefined() &&
           !Sym.canBeOmittedFromSymbolTable() && Kind != object::OFK_None &&
           (Sym.getVisibility() != GlobalValue::HiddenVisibility));
      ShouldExtract |= ResolvesStrongReference | NewGlobalSymbol;

      // Update this symbol in the "table" with the new information.
      if (OldSym & Sym_Undefined && !Sym.isUndefined())
        TmpSyms[Saver.save(Sym.getName())] =
            static_cast<Symbol>(OldSym & ~Sym_Undefined);
      if (Sym.isUndefined() && NewSymbol)
        TmpSyms[Saver.save(Sym.getName())] =
            static_cast<Symbol>(OldSym | Sym_Undefined);
      if (Sym.isWeak())
        TmpSyms[Saver.save(Sym.getName())] =
            static_cast<Symbol>(OldSym | Sym_Weak);
    }
  }

  // If the file gets extracted we update the table with the new symbols.
  if (ShouldExtract)
    Syms.insert(std::begin(TmpSyms), std::end(TmpSyms));

  return ShouldExtract;
}

/// Scan the symbols from an ObjectFile \p Obj and record if we need to extract
/// any symbols from it.
Expected<bool> getSymbolsFromObject(const ObjectFile &Obj, OffloadKind Kind,
                                    bool IsArchive, StringSaver &Saver,
                                    DenseMap<StringRef, Symbol> &Syms) {
  bool ShouldExtract = !IsArchive;
  DenseMap<StringRef, Symbol> TmpSyms;
  for (SymbolRef Sym : Obj.symbols()) {
    auto FlagsOrErr = Sym.getFlags();
    if (!FlagsOrErr)
      return FlagsOrErr.takeError();

    if (!(*FlagsOrErr & SymbolRef::SF_Global) ||
        (*FlagsOrErr & SymbolRef::SF_FormatSpecific))
      continue;

    auto NameOrErr = Sym.getName();
    if (!NameOrErr)
      return NameOrErr.takeError();

    bool NewSymbol = Syms.count(*NameOrErr) == 0;
    auto OldSym = NewSymbol ? Sym_None : Syms[*NameOrErr];

    // We will extract if it defines a currenlty undefined non-weak symbol.
    bool ResolvesStrongReference = (OldSym & Sym_Undefined) &&
                                   !(OldSym & Sym_Weak) &&
                                   !(*FlagsOrErr & SymbolRef::SF_Undefined);

    // We will extract if it defines a new global symbol visible to the host.
    // This is only necessary for code targeting an offloading language.
    bool NewGlobalSymbol =
        ((NewSymbol || (OldSym & Sym_Undefined)) &&
         !(*FlagsOrErr & SymbolRef::SF_Undefined) && Kind != object::OFK_None &&
         !(*FlagsOrErr & SymbolRef::SF_Hidden));
    ShouldExtract |= ResolvesStrongReference | NewGlobalSymbol;

    // Update this symbol in the "table" with the new information.
    if (OldSym & Sym_Undefined && !(*FlagsOrErr & SymbolRef::SF_Undefined))
      TmpSyms[Saver.save(*NameOrErr)] =
          static_cast<Symbol>(OldSym & ~Sym_Undefined);
    if (*FlagsOrErr & SymbolRef::SF_Undefined && NewSymbol)
      TmpSyms[Saver.save(*NameOrErr)] =
          static_cast<Symbol>(OldSym | Sym_Undefined);
    if (*FlagsOrErr & SymbolRef::SF_Weak)
      TmpSyms[Saver.save(*NameOrErr)] = static_cast<Symbol>(OldSym | Sym_Weak);
  }

  // If the file gets extracted we update the table with the new symbols.
  if (ShouldExtract)
    Syms.insert(std::begin(TmpSyms), std::end(TmpSyms));

  return ShouldExtract;
}

/// Attempt to 'resolve' symbols found in input files. We use this to
/// determine if an archive member needs to be extracted. An archive member
/// will be extracted if any of the following is true.
///   1) It defines an undefined symbol in a regular object filie.
///   2) It defines a global symbol without hidden visibility that has not
///      yet been defined.
Expected<bool> getSymbols(StringRef Image, OffloadKind Kind, bool IsArchive,
                          StringSaver &Saver,
                          DenseMap<StringRef, Symbol> &Syms) {
  MemoryBufferRef Buffer = MemoryBufferRef(Image, "");
  switch (identify_magic(Image)) {
  case file_magic::bitcode:
    return getSymbolsFromBitcode(Buffer, Kind, IsArchive, Saver, Syms);
  case file_magic::elf_relocatable: {
    Expected<std::unique_ptr<ObjectFile>> ObjFile =
        ObjectFile::createObjectFile(Buffer);
    if (!ObjFile)
      return ObjFile.takeError();
    return getSymbolsFromObject(**ObjFile, Kind, IsArchive, Saver, Syms);
  }
  default:
    return false;
  }
}

/// Search the input files and libraries for embedded device offloading code
/// and add it to the list of files to be linked. Files coming from static
/// libraries are only added to the input if they are used by an existing
/// input file. Returns a list of input files intended for a single linking job.
Expected<SmallVector<SmallVector<OffloadFile>>>
getDeviceInput(const ArgList &Args) {
  llvm::TimeTraceScope TimeScope("ExtractDeviceCode");

  // Skip all the input if the user is overriding the output.
  if (Args.hasArg(OPT_override_image))
    return SmallVector<SmallVector<OffloadFile>>();

  StringRef Root = Args.getLastArgValue(OPT_sysroot_EQ);
  SmallVector<StringRef> LibraryPaths;
  for (const opt::Arg *Arg : Args.filtered(OPT_library_path, OPT_libpath))
    LibraryPaths.push_back(Arg->getValue());

  BumpPtrAllocator Alloc;
  StringSaver Saver(Alloc);

  // Try to extract device code from the linker input files.
  bool WholeArchive = Args.hasArg(OPT_wholearchive_flag) ? true : false;
  SmallVector<OffloadFile> ObjectFilesToExtract;
  SmallVector<OffloadFile> ArchiveFilesToExtract;
  for (const opt::Arg *Arg : Args.filtered(
           OPT_INPUT, OPT_library, OPT_whole_archive, OPT_no_whole_archive)) {
    if (Arg->getOption().matches(OPT_whole_archive) ||
        Arg->getOption().matches(OPT_no_whole_archive)) {
      WholeArchive = Arg->getOption().matches(OPT_whole_archive);
      continue;
    }

    std::optional<std::string> Filename =
        Arg->getOption().matches(OPT_library)
            ? searchLibrary(Arg->getValue(), Root, LibraryPaths)
            : std::string(Arg->getValue());

    if (!Filename && Arg->getOption().matches(OPT_library))
      reportError(
          createStringError("unable to find library -l%s", Arg->getValue()));

    if (!Filename || !sys::fs::exists(*Filename) ||
        sys::fs::is_directory(*Filename))
      continue;

    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
        MemoryBuffer::getFileOrSTDIN(*Filename);
    if (std::error_code EC = BufferOrErr.getError())
      return createFileError(*Filename, EC);

    MemoryBufferRef Buffer = **BufferOrErr;
    if (identify_magic(Buffer.getBuffer()) == file_magic::elf_shared_object)
      continue;

    SmallVector<OffloadFile> Binaries;
    if (Error Err = extractOffloadBinaries(Buffer, Binaries))
      return std::move(Err);

    for (auto &OffloadFile : Binaries) {
      if (identify_magic(Buffer.getBuffer()) == file_magic::archive &&
          !WholeArchive)
        ArchiveFilesToExtract.emplace_back(std::move(OffloadFile));
      else
        ObjectFilesToExtract.emplace_back(std::move(OffloadFile));
    }
  }

  // Link all standard input files and update the list of symbols.
  MapVector<OffloadFile::TargetID, SmallVector<OffloadFile, 0>> InputFiles;
  DenseMap<OffloadFile::TargetID, DenseMap<StringRef, Symbol>> Syms;
  for (OffloadFile &Binary : ObjectFilesToExtract) {
    if (!Binary.getBinary())
      continue;

    SmallVector<OffloadFile::TargetID> CompatibleTargets = {Binary};
    for (const auto &[ID, Input] : InputFiles)
      if (object::areTargetsCompatible(Binary, ID))
        CompatibleTargets.emplace_back(ID);

    for (const auto &[Index, ID] : llvm::enumerate(CompatibleTargets)) {
      Expected<bool> ExtractOrErr = getSymbols(
          Binary.getBinary()->getImage(), Binary.getBinary()->getOffloadKind(),
          /*IsArchive=*/false, Saver, Syms[ID]);
      if (!ExtractOrErr)
        return ExtractOrErr.takeError();

      // If another target needs this binary it must be copied instead.
      if (Index == CompatibleTargets.size() - 1)
        InputFiles[ID].emplace_back(std::move(Binary));
      else
        InputFiles[ID].emplace_back(Binary.copy());
    }
  }

  // Archive members only extract if they define needed symbols. We do this
  // after every regular input file so that libraries may be included out of
  // order. This follows 'ld.lld' semantics which are more lenient.
  bool Extracted = true;
  while (Extracted) {
    Extracted = false;
    for (OffloadFile &Binary : ArchiveFilesToExtract) {
      // If the binary was previously extracted it will be set to null.
      if (!Binary.getBinary())
        continue;

      SmallVector<OffloadFile::TargetID> CompatibleTargets = {Binary};
      for (const auto &[ID, Input] : InputFiles)
        if (object::areTargetsCompatible(Binary, ID))
          CompatibleTargets.emplace_back(ID);

      for (const auto &[Index, ID] : llvm::enumerate(CompatibleTargets)) {
        // Only extract an if we have an an object matching this target.
        if (!InputFiles.count(ID))
          continue;

        Expected<bool> ExtractOrErr =
            getSymbols(Binary.getBinary()->getImage(),
                       Binary.getBinary()->getOffloadKind(), /*IsArchive=*/true,
                       Saver, Syms[ID]);
        if (!ExtractOrErr)
          return ExtractOrErr.takeError();

        Extracted = *ExtractOrErr;

        // Skip including the file if it is an archive that does not resolve
        // any symbols.
        if (!Extracted)
          continue;

        // If another target needs this binary it must be copied instead.
        if (Index == CompatibleTargets.size() - 1)
          InputFiles[ID].emplace_back(std::move(Binary));
        else
          InputFiles[ID].emplace_back(Binary.copy());
      }

      // If we extracted any files we need to check all the symbols again.
      if (Extracted)
        break;
    }
  }

  for (StringRef Library : Args.getAllArgValues(OPT_bitcode_library_EQ)) {
    auto FileOrErr = getInputBitcodeLibrary(Library);
    if (!FileOrErr)
      return FileOrErr.takeError();
    InputFiles[*FileOrErr].push_back(std::move(*FileOrErr));
  }

  SmallVector<SmallVector<OffloadFile>> InputsForTarget;
  for (auto &[ID, Input] : InputFiles)
    InputsForTarget.emplace_back(std::move(Input));

  return std::move(InputsForTarget);
}

} // namespace

int main(int Argc, char **Argv) {
  InitLLVM X(Argc, Argv);
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  LinkerExecutable = Argv[0];
  sys::PrintStackTraceOnErrorSignal(Argv[0]);

  const OptTable &Tbl = getOptTable();
  BumpPtrAllocator Alloc;
  StringSaver Saver(Alloc);
  auto Args = Tbl.parseArgs(Argc, Argv, OPT_INVALID, Saver, [&](StringRef Err) {
    reportError(createStringError(Err));
  });

  if (Args.hasArg(OPT_help) || Args.hasArg(OPT_help_hidden)) {
    Tbl.printHelp(
        outs(),
        "clang-linker-wrapper [options] -- <options to passed to the linker>",
        "\nA wrapper utility over the host linker. It scans the input files\n"
        "for sections that require additional processing prior to linking.\n"
        "The will then transparently pass all arguments and input to the\n"
        "specified host linker to create the final binary.\n",
        Args.hasArg(OPT_help_hidden), Args.hasArg(OPT_help_hidden));
    return EXIT_SUCCESS;
  }
  if (Args.hasArg(OPT_v)) {
    printVersion(outs());
    return EXIT_SUCCESS;
  }

  // This forwards '-mllvm' arguments to LLVM if present.
  SmallVector<const char *> NewArgv = {Argv[0]};
  for (const opt::Arg *Arg : Args.filtered(OPT_mllvm))
    NewArgv.push_back(Arg->getValue());
  for (const opt::Arg *Arg : Args.filtered(OPT_offload_opt_eq_minus))
    NewArgv.push_back(Args.MakeArgString(StringRef("-") + Arg->getValue()));
  SmallVector<PassPlugin, 1> PluginList;
  PassPlugins.setCallback([&](const std::string &PluginPath) {
    auto Plugin = PassPlugin::Load(PluginPath);
    if (!Plugin)
      report_fatal_error(Plugin.takeError(), /*gen_crash_diag=*/false);
    PluginList.emplace_back(Plugin.get());
  });
  cl::ParseCommandLineOptions(NewArgv.size(), &NewArgv[0]);

  Verbose = Args.hasArg(OPT_verbose);
  DryRun = Args.hasArg(OPT_dry_run);
  SaveTemps = Args.hasArg(OPT_save_temps);
  CudaBinaryPath = Args.getLastArgValue(OPT_cuda_path_EQ).str();

  llvm::Triple Triple(
      Args.getLastArgValue(OPT_host_triple_EQ, sys::getDefaultTargetTriple()));
  if (Args.hasArg(OPT_o))
    ExecutableName = Args.getLastArgValue(OPT_o, "a.out");
  else if (Args.hasArg(OPT_out))
    ExecutableName = Args.getLastArgValue(OPT_out, "a.exe");
  else
    ExecutableName = Triple.isOSWindows() ? "a.exe" : "a.out";

  parallel::strategy = hardware_concurrency(1);
  if (auto *Arg = Args.getLastArg(OPT_wrapper_jobs)) {
    unsigned Threads = 0;
    if (!llvm::to_integer(Arg->getValue(), Threads) || Threads == 0)
      reportError(createStringError("%s: expected a positive integer, got '%s'",
                                    Arg->getSpelling().data(),
                                    Arg->getValue()));
    parallel::strategy = hardware_concurrency(Threads);
  }

  if (Args.hasArg(OPT_wrapper_time_trace_eq)) {
    unsigned Granularity;
    Args.getLastArgValue(OPT_wrapper_time_trace_granularity, "500")
        .getAsInteger(10, Granularity);
    timeTraceProfilerInitialize(Granularity, Argv[0]);
  }

  {
    llvm::TimeTraceScope TimeScope("Execute linker wrapper");

    // Extract the device input files stored in the host fat binary.
    auto DeviceInputFiles = getDeviceInput(Args);
    if (!DeviceInputFiles)
      reportError(DeviceInputFiles.takeError());

    // Link and wrap the device images extracted from the linker input.
    auto FilesOrErr =
        linkAndWrapDeviceFiles(*DeviceInputFiles, Args, Argv, Argc);
    if (!FilesOrErr)
      reportError(FilesOrErr.takeError());

    // Run the host linking job with the rendered arguments.
    if (Error Err = runLinker(*FilesOrErr, Args))
      reportError(std::move(Err));
  }

  if (const opt::Arg *Arg = Args.getLastArg(OPT_wrapper_time_trace_eq)) {
    if (Error Err = timeTraceProfilerWrite(Arg->getValue(), ExecutableName))
      reportError(std::move(Err));
    timeTraceProfilerCleanup();
  }

  // Remove the temporary files created.
  if (!SaveTemps)
    for (const auto &TempFile : TempFiles)
      if (std::error_code EC = sys::fs::remove(TempFile))
        reportError(createFileError(TempFile, EC));

  return EXIT_SUCCESS;
}
