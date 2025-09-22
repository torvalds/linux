//===- ToolChain.cpp - Collections of tools for one platform --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/ToolChain.h"
#include "ToolChains/Arch/AArch64.h"
#include "ToolChains/Arch/ARM.h"
#include "ToolChains/Clang.h"
#include "ToolChains/CommonArgs.h"
#include "ToolChains/Flang.h"
#include "ToolChains/InterfaceStubs.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Config/config.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "clang/Driver/XRayArgs.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>

using namespace clang;
using namespace driver;
using namespace tools;
using namespace llvm;
using namespace llvm::opt;

static llvm::opt::Arg *GetRTTIArgument(const ArgList &Args) {
  return Args.getLastArg(options::OPT_mkernel, options::OPT_fapple_kext,
                         options::OPT_fno_rtti, options::OPT_frtti);
}

static ToolChain::RTTIMode CalculateRTTIMode(const ArgList &Args,
                                             const llvm::Triple &Triple,
                                             const Arg *CachedRTTIArg) {
  // Explicit rtti/no-rtti args
  if (CachedRTTIArg) {
    if (CachedRTTIArg->getOption().matches(options::OPT_frtti))
      return ToolChain::RM_Enabled;
    else
      return ToolChain::RM_Disabled;
  }

  // -frtti is default, except for the PS4/PS5 and DriverKit.
  bool NoRTTI = Triple.isPS() || Triple.isDriverKit();
  return NoRTTI ? ToolChain::RM_Disabled : ToolChain::RM_Enabled;
}

static ToolChain::ExceptionsMode CalculateExceptionsMode(const ArgList &Args) {
  if (Args.hasFlag(options::OPT_fexceptions, options::OPT_fno_exceptions,
                   true)) {
    return ToolChain::EM_Enabled;
  }
  return ToolChain::EM_Disabled;
}

ToolChain::ToolChain(const Driver &D, const llvm::Triple &T,
                     const ArgList &Args)
    : D(D), Triple(T), Args(Args), CachedRTTIArg(GetRTTIArgument(Args)),
      CachedRTTIMode(CalculateRTTIMode(Args, Triple, CachedRTTIArg)),
      CachedExceptionsMode(CalculateExceptionsMode(Args)) {
  auto addIfExists = [this](path_list &List, const std::string &Path) {
    if (getVFS().exists(Path))
      List.push_back(Path);
  };

  if (std::optional<std::string> Path = getRuntimePath())
    getLibraryPaths().push_back(*Path);
  if (std::optional<std::string> Path = getStdlibPath())
    getFilePaths().push_back(*Path);
  for (const auto &Path : getArchSpecificLibPaths())
    addIfExists(getFilePaths(), Path);
}

llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
ToolChain::executeToolChainProgram(StringRef Executable,
                                   unsigned SecondsToWait) const {
  llvm::SmallString<64> OutputFile;
  llvm::sys::fs::createTemporaryFile("toolchain-program", "txt", OutputFile);
  llvm::FileRemover OutputRemover(OutputFile.c_str());
  std::optional<llvm::StringRef> Redirects[] = {
      {""},
      OutputFile.str(),
      {""},
  };

  std::string ErrorMessage;
  if (llvm::sys::ExecuteAndWait(Executable, {}, {}, Redirects, SecondsToWait,
                                /*MemoryLimit=*/0, &ErrorMessage))
    return llvm::createStringError(std::error_code(),
                                   Executable + ": " + ErrorMessage);

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> OutputBuf =
      llvm::MemoryBuffer::getFile(OutputFile.c_str());
  if (!OutputBuf)
    return llvm::createStringError(OutputBuf.getError(),
                                   "Failed to read stdout of " + Executable +
                                       ": " + OutputBuf.getError().message());
  return std::move(*OutputBuf);
}

void ToolChain::setTripleEnvironment(llvm::Triple::EnvironmentType Env) {
  Triple.setEnvironment(Env);
  if (EffectiveTriple != llvm::Triple())
    EffectiveTriple.setEnvironment(Env);
}

ToolChain::~ToolChain() = default;

llvm::vfs::FileSystem &ToolChain::getVFS() const {
  return getDriver().getVFS();
}

bool ToolChain::useIntegratedAs() const {
  return Args.hasFlag(options::OPT_fintegrated_as,
                      options::OPT_fno_integrated_as,
                      IsIntegratedAssemblerDefault());
}

bool ToolChain::useIntegratedBackend() const {
  assert(
      ((IsIntegratedBackendDefault() && IsIntegratedBackendSupported()) ||
       (!IsIntegratedBackendDefault() || IsNonIntegratedBackendSupported())) &&
      "(Non-)integrated backend set incorrectly!");

  bool IBackend = Args.hasFlag(options::OPT_fintegrated_objemitter,
                               options::OPT_fno_integrated_objemitter,
                               IsIntegratedBackendDefault());

  // Diagnose when integrated-objemitter options are not supported by this
  // toolchain.
  unsigned DiagID;
  if ((IBackend && !IsIntegratedBackendSupported()) ||
      (!IBackend && !IsNonIntegratedBackendSupported()))
    DiagID = clang::diag::err_drv_unsupported_opt_for_target;
  else
    DiagID = clang::diag::warn_drv_unsupported_opt_for_target;
  Arg *A = Args.getLastArg(options::OPT_fno_integrated_objemitter);
  if (A && !IsNonIntegratedBackendSupported())
    D.Diag(DiagID) << A->getAsString(Args) << Triple.getTriple();
  A = Args.getLastArg(options::OPT_fintegrated_objemitter);
  if (A && !IsIntegratedBackendSupported())
    D.Diag(DiagID) << A->getAsString(Args) << Triple.getTriple();

  return IBackend;
}

bool ToolChain::useRelaxRelocations() const {
  return ENABLE_X86_RELAX_RELOCATIONS;
}

bool ToolChain::defaultToIEEELongDouble() const {
  return PPC_LINUX_DEFAULT_IEEELONGDOUBLE && getTriple().isOSLinux();
}

static void getAArch64MultilibFlags(const Driver &D,
                                          const llvm::Triple &Triple,
                                          const llvm::opt::ArgList &Args,
                                          Multilib::flags_list &Result) {
  std::vector<StringRef> Features;
  tools::aarch64::getAArch64TargetFeatures(D, Triple, Args, Features, false);
  const auto UnifiedFeatures = tools::unifyTargetFeatures(Features);
  llvm::DenseSet<StringRef> FeatureSet(UnifiedFeatures.begin(),
                                       UnifiedFeatures.end());
  std::vector<std::string> MArch;
  for (const auto &Ext : AArch64::Extensions)
    if (!Ext.UserVisibleName.empty())
      if (FeatureSet.contains(Ext.PosTargetFeature))
        MArch.push_back(Ext.UserVisibleName.str());
  for (const auto &Ext : AArch64::Extensions)
    if (!Ext.UserVisibleName.empty())
      if (FeatureSet.contains(Ext.NegTargetFeature))
        MArch.push_back(("no" + Ext.UserVisibleName).str());
  StringRef ArchName;
  for (const auto &ArchInfo : AArch64::ArchInfos)
    if (FeatureSet.contains(ArchInfo->ArchFeature))
      ArchName = ArchInfo->Name;
  assert(!ArchName.empty() && "at least one architecture should be found");
  MArch.insert(MArch.begin(), ("-march=" + ArchName).str());
  Result.push_back(llvm::join(MArch, "+"));
}

static void getARMMultilibFlags(const Driver &D,
                                      const llvm::Triple &Triple,
                                      const llvm::opt::ArgList &Args,
                                      Multilib::flags_list &Result) {
  std::vector<StringRef> Features;
  llvm::ARM::FPUKind FPUKind = tools::arm::getARMTargetFeatures(
      D, Triple, Args, Features, false /*ForAs*/, true /*ForMultilib*/);
  const auto UnifiedFeatures = tools::unifyTargetFeatures(Features);
  llvm::DenseSet<StringRef> FeatureSet(UnifiedFeatures.begin(),
                                       UnifiedFeatures.end());
  std::vector<std::string> MArch;
  for (const auto &Ext : ARM::ARCHExtNames)
    if (!Ext.Name.empty())
      if (FeatureSet.contains(Ext.Feature))
        MArch.push_back(Ext.Name.str());
  for (const auto &Ext : ARM::ARCHExtNames)
    if (!Ext.Name.empty())
      if (FeatureSet.contains(Ext.NegFeature))
        MArch.push_back(("no" + Ext.Name).str());
  MArch.insert(MArch.begin(), ("-march=" + Triple.getArchName()).str());
  Result.push_back(llvm::join(MArch, "+"));

  switch (FPUKind) {
#define ARM_FPU(NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION)                \
  case llvm::ARM::KIND:                                                        \
    Result.push_back("-mfpu=" NAME);                                           \
    break;
#include "llvm/TargetParser/ARMTargetParser.def"
  default:
    llvm_unreachable("Invalid FPUKind");
  }

  switch (arm::getARMFloatABI(D, Triple, Args)) {
  case arm::FloatABI::Soft:
    Result.push_back("-mfloat-abi=soft");
    break;
  case arm::FloatABI::SoftFP:
    Result.push_back("-mfloat-abi=softfp");
    break;
  case arm::FloatABI::Hard:
    Result.push_back("-mfloat-abi=hard");
    break;
  case arm::FloatABI::Invalid:
    llvm_unreachable("Invalid float ABI");
  }
}

Multilib::flags_list
ToolChain::getMultilibFlags(const llvm::opt::ArgList &Args) const {
  using namespace clang::driver::options;

  std::vector<std::string> Result;
  const llvm::Triple Triple(ComputeEffectiveClangTriple(Args));
  Result.push_back("--target=" + Triple.str());

  switch (Triple.getArch()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
    getAArch64MultilibFlags(D, Triple, Args, Result);
    break;
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    getARMMultilibFlags(D, Triple, Args, Result);
    break;
  default:
    break;
  }

  // Include fno-exceptions and fno-rtti
  // to improve multilib selection
  if (getRTTIMode() == ToolChain::RTTIMode::RM_Disabled)
    Result.push_back("-fno-rtti");
  else
    Result.push_back("-frtti");

  if (getExceptionsMode() == ToolChain::ExceptionsMode::EM_Disabled)
    Result.push_back("-fno-exceptions");
  else
    Result.push_back("-fexceptions");

  // Sort and remove duplicates.
  std::sort(Result.begin(), Result.end());
  Result.erase(std::unique(Result.begin(), Result.end()), Result.end());
  return Result;
}

SanitizerArgs
ToolChain::getSanitizerArgs(const llvm::opt::ArgList &JobArgs) const {
  SanitizerArgs SanArgs(*this, JobArgs, !SanitizerArgsChecked);
  SanitizerArgsChecked = true;
  return SanArgs;
}

const XRayArgs& ToolChain::getXRayArgs() const {
  if (!XRayArguments)
    XRayArguments.reset(new XRayArgs(*this, Args));
  return *XRayArguments;
}

namespace {

struct DriverSuffix {
  const char *Suffix;
  const char *ModeFlag;
};

} // namespace

static const DriverSuffix *FindDriverSuffix(StringRef ProgName, size_t &Pos) {
  // A list of known driver suffixes. Suffixes are compared against the
  // program name in order. If there is a match, the frontend type is updated as
  // necessary by applying the ModeFlag.
  static const DriverSuffix DriverSuffixes[] = {
      {"clang", nullptr},
      {"clang++", "--driver-mode=g++"},
      {"clang-c++", "--driver-mode=g++"},
      {"clang-cc", nullptr},
      {"clang-cpp", "--driver-mode=cpp"},
      {"clang-g++", "--driver-mode=g++"},
      {"clang-gcc", nullptr},
      {"clang-cl", "--driver-mode=cl"},
      {"cc", nullptr},
      {"cpp", "--driver-mode=cpp"},
      {"cl", "--driver-mode=cl"},
      {"++", "--driver-mode=g++"},
      {"flang", "--driver-mode=flang"},
      {"clang-dxc", "--driver-mode=dxc"},
  };

  for (const auto &DS : DriverSuffixes) {
    StringRef Suffix(DS.Suffix);
    if (ProgName.ends_with(Suffix)) {
      Pos = ProgName.size() - Suffix.size();
      return &DS;
    }
  }
  return nullptr;
}

/// Normalize the program name from argv[0] by stripping the file extension if
/// present and lower-casing the string on Windows.
static std::string normalizeProgramName(llvm::StringRef Argv0) {
  std::string ProgName = std::string(llvm::sys::path::filename(Argv0));
  if (is_style_windows(llvm::sys::path::Style::native)) {
    // Transform to lowercase for case insensitive file systems.
    std::transform(ProgName.begin(), ProgName.end(), ProgName.begin(),
                   ::tolower);
  }
  return ProgName;
}

static const DriverSuffix *parseDriverSuffix(StringRef ProgName, size_t &Pos) {
  // Try to infer frontend type and default target from the program name by
  // comparing it against DriverSuffixes in order.

  // If there is a match, the function tries to identify a target as prefix.
  // E.g. "x86_64-linux-clang" as interpreted as suffix "clang" with target
  // prefix "x86_64-linux". If such a target prefix is found, it may be
  // added via -target as implicit first argument.
  const DriverSuffix *DS = FindDriverSuffix(ProgName, Pos);

  if (!DS && ProgName.ends_with(".exe")) {
    // Try again after stripping the executable suffix:
    // clang++.exe -> clang++
    ProgName = ProgName.drop_back(StringRef(".exe").size());
    DS = FindDriverSuffix(ProgName, Pos);
  }

  if (!DS) {
    // Try again after stripping any trailing version number:
    // clang++3.5 -> clang++
    ProgName = ProgName.rtrim("0123456789.");
    DS = FindDriverSuffix(ProgName, Pos);
  }

  if (!DS) {
    // Try again after stripping trailing -component.
    // clang++-tot -> clang++
    ProgName = ProgName.slice(0, ProgName.rfind('-'));
    DS = FindDriverSuffix(ProgName, Pos);
  }
  return DS;
}

ParsedClangName
ToolChain::getTargetAndModeFromProgramName(StringRef PN) {
  std::string ProgName = normalizeProgramName(PN);
  size_t SuffixPos;
  const DriverSuffix *DS = parseDriverSuffix(ProgName, SuffixPos);
  if (!DS)
    return {};
  size_t SuffixEnd = SuffixPos + strlen(DS->Suffix);

  size_t LastComponent = ProgName.rfind('-', SuffixPos);
  if (LastComponent == std::string::npos)
    return ParsedClangName(ProgName.substr(0, SuffixEnd), DS->ModeFlag);
  std::string ModeSuffix = ProgName.substr(LastComponent + 1,
                                           SuffixEnd - LastComponent - 1);

  // Infer target from the prefix.
  StringRef Prefix(ProgName);
  Prefix = Prefix.slice(0, LastComponent);
  std::string IgnoredError;
  bool IsRegistered =
      llvm::TargetRegistry::lookupTarget(std::string(Prefix), IgnoredError);
  return ParsedClangName{std::string(Prefix), ModeSuffix, DS->ModeFlag,
                         IsRegistered};
}

StringRef ToolChain::getDefaultUniversalArchName() const {
  // In universal driver terms, the arch name accepted by -arch isn't exactly
  // the same as the ones that appear in the triple. Roughly speaking, this is
  // an inverse of the darwin::getArchTypeForDarwinArchName() function.
  switch (Triple.getArch()) {
  case llvm::Triple::aarch64: {
    if (getTriple().isArm64e())
      return "arm64e";
    return "arm64";
  }
  case llvm::Triple::aarch64_32:
    return "arm64_32";
  case llvm::Triple::ppc:
    return "ppc";
  case llvm::Triple::ppcle:
    return "ppcle";
  case llvm::Triple::ppc64:
    return "ppc64";
  case llvm::Triple::ppc64le:
    return "ppc64le";
  default:
    return Triple.getArchName();
  }
}

std::string ToolChain::getInputFilename(const InputInfo &Input) const {
  return Input.getFilename();
}

ToolChain::UnwindTableLevel
ToolChain::getDefaultUnwindTableLevel(const ArgList &Args) const {
  return UnwindTableLevel::None;
}

Tool *ToolChain::getClang() const {
  if (!Clang)
    Clang.reset(new tools::Clang(*this, useIntegratedBackend()));
  return Clang.get();
}

Tool *ToolChain::getFlang() const {
  if (!Flang)
    Flang.reset(new tools::Flang(*this));
  return Flang.get();
}

Tool *ToolChain::buildAssembler() const {
  return new tools::ClangAs(*this);
}

Tool *ToolChain::buildLinker() const {
  llvm_unreachable("Linking is not supported by this toolchain");
}

Tool *ToolChain::buildStaticLibTool() const {
  llvm_unreachable("Creating static lib is not supported by this toolchain");
}

Tool *ToolChain::getAssemble() const {
  if (!Assemble)
    Assemble.reset(buildAssembler());
  return Assemble.get();
}

Tool *ToolChain::getClangAs() const {
  if (!Assemble)
    Assemble.reset(new tools::ClangAs(*this));
  return Assemble.get();
}

Tool *ToolChain::getLink() const {
  if (!Link)
    Link.reset(buildLinker());
  return Link.get();
}

Tool *ToolChain::getStaticLibTool() const {
  if (!StaticLibTool)
    StaticLibTool.reset(buildStaticLibTool());
  return StaticLibTool.get();
}

Tool *ToolChain::getIfsMerge() const {
  if (!IfsMerge)
    IfsMerge.reset(new tools::ifstool::Merger(*this));
  return IfsMerge.get();
}

Tool *ToolChain::getOffloadBundler() const {
  if (!OffloadBundler)
    OffloadBundler.reset(new tools::OffloadBundler(*this));
  return OffloadBundler.get();
}

Tool *ToolChain::getOffloadPackager() const {
  if (!OffloadPackager)
    OffloadPackager.reset(new tools::OffloadPackager(*this));
  return OffloadPackager.get();
}

Tool *ToolChain::getLinkerWrapper() const {
  if (!LinkerWrapper)
    LinkerWrapper.reset(new tools::LinkerWrapper(*this, getLink()));
  return LinkerWrapper.get();
}

Tool *ToolChain::getTool(Action::ActionClass AC) const {
  switch (AC) {
  case Action::AssembleJobClass:
    return getAssemble();

  case Action::IfsMergeJobClass:
    return getIfsMerge();

  case Action::LinkJobClass:
    return getLink();

  case Action::StaticLibJobClass:
    return getStaticLibTool();

  case Action::InputClass:
  case Action::BindArchClass:
  case Action::OffloadClass:
  case Action::LipoJobClass:
  case Action::DsymutilJobClass:
  case Action::VerifyDebugInfoJobClass:
  case Action::BinaryAnalyzeJobClass:
    llvm_unreachable("Invalid tool kind.");

  case Action::CompileJobClass:
  case Action::PrecompileJobClass:
  case Action::PreprocessJobClass:
  case Action::ExtractAPIJobClass:
  case Action::AnalyzeJobClass:
  case Action::MigrateJobClass:
  case Action::VerifyPCHJobClass:
  case Action::BackendJobClass:
    return getClang();

  case Action::OffloadBundlingJobClass:
  case Action::OffloadUnbundlingJobClass:
    return getOffloadBundler();

  case Action::OffloadPackagerJobClass:
    return getOffloadPackager();
  case Action::LinkerWrapperJobClass:
    return getLinkerWrapper();
  }

  llvm_unreachable("Invalid tool kind.");
}

static StringRef getArchNameForCompilerRTLib(const ToolChain &TC,
                                             const ArgList &Args) {
  const llvm::Triple &Triple = TC.getTriple();
  bool IsWindows = Triple.isOSWindows();

  if (TC.isBareMetal())
    return Triple.getArchName();

  if (TC.getArch() == llvm::Triple::arm || TC.getArch() == llvm::Triple::armeb)
    return (arm::getARMFloatABI(TC, Args) == arm::FloatABI::Hard && !IsWindows)
               ? "armhf"
               : "arm";

  // For historic reasons, Android library is using i686 instead of i386.
  if (TC.getArch() == llvm::Triple::x86 && Triple.isAndroid())
    return "i686";

  if (TC.getArch() == llvm::Triple::x86_64 && Triple.isX32())
    return "x32";

  return llvm::Triple::getArchTypeName(TC.getArch());
}

StringRef ToolChain::getOSLibName() const {
  if (Triple.isOSDarwin())
    return "darwin";

  switch (Triple.getOS()) {
  case llvm::Triple::FreeBSD:
    return "freebsd";
  case llvm::Triple::NetBSD:
    return "netbsd";
  case llvm::Triple::OpenBSD:
    return "openbsd";
  case llvm::Triple::Solaris:
    return "sunos";
  case llvm::Triple::AIX:
    return "aix";
  default:
    return getOS();
  }
}

std::string ToolChain::getCompilerRTPath() const {
  SmallString<128> Path(getDriver().ResourceDir);
  if (isBareMetal()) {
    llvm::sys::path::append(Path, "lib", getOSLibName());
    if (!SelectedMultilibs.empty()) {
      Path += SelectedMultilibs.back().gccSuffix();
    }
  } else if (Triple.isOSUnknown()) {
    llvm::sys::path::append(Path, "lib");
  } else {
    llvm::sys::path::append(Path, "lib", getOSLibName());
  }
  return std::string(Path);
}

std::string ToolChain::getCompilerRTBasename(const ArgList &Args,
                                             StringRef Component,
                                             FileType Type) const {
  std::string CRTAbsolutePath = getCompilerRT(Args, Component, Type);
  return llvm::sys::path::filename(CRTAbsolutePath).str();
}

std::string ToolChain::buildCompilerRTBasename(const llvm::opt::ArgList &Args,
                                               StringRef Component,
                                               FileType Type,
                                               bool AddArch) const {
  const llvm::Triple &TT = getTriple();
  bool IsITANMSVCWindows =
      TT.isWindowsMSVCEnvironment() || TT.isWindowsItaniumEnvironment();

  const char *Prefix =
      IsITANMSVCWindows || Type == ToolChain::FT_Object ? "" : "lib";
  const char *Suffix;
  switch (Type) {
  case ToolChain::FT_Object:
    Suffix = IsITANMSVCWindows ? ".obj" : ".o";
    break;
  case ToolChain::FT_Static:
    Suffix = IsITANMSVCWindows ? ".lib" : ".a";
    break;
  case ToolChain::FT_Shared:
    Suffix = TT.isOSWindows()
                 ? (TT.isWindowsGNUEnvironment() ? ".dll.a" : ".lib")
                 : ".so";
    break;
  }

  std::string ArchAndEnv;
  if (AddArch) {
    StringRef Arch = getArchNameForCompilerRTLib(*this, Args);
    const char *Env = TT.isAndroid() ? "-android" : "";
    ArchAndEnv = ("-" + Arch + Env).str();
  }
  return (Prefix + Twine("clang_rt.") + Component + ArchAndEnv + Suffix).str();
}

std::string ToolChain::getCompilerRT(const ArgList &Args, StringRef Component,
                                     FileType Type) const {
  // Check for runtime files in the new layout without the architecture first.
  std::string CRTBasename =
      buildCompilerRTBasename(Args, Component, Type, /*AddArch=*/false);
  SmallString<128> Path;
  for (const auto &LibPath : getLibraryPaths()) {
    SmallString<128> P(LibPath);
    llvm::sys::path::append(P, CRTBasename);
    if (getVFS().exists(P))
      return std::string(P);
    if (Path.empty())
      Path = P;
  }
  if (getTriple().isOSAIX())
    Path.clear();

  // Check the filename for the old layout if the new one does not exist.
  CRTBasename =
      buildCompilerRTBasename(Args, Component, Type, /*AddArch=*/true);
  SmallString<128> OldPath(getCompilerRTPath());
  llvm::sys::path::append(OldPath, CRTBasename);
  if (Path.empty() || getVFS().exists(OldPath))
    return std::string(OldPath);

  // If none is found, use a file name from the new layout, which may get
  // printed in an error message, aiding users in knowing what Clang is
  // looking for.
  return std::string(Path);
}

const char *ToolChain::getCompilerRTArgString(const llvm::opt::ArgList &Args,
                                              StringRef Component,
                                              FileType Type) const {
  return Args.MakeArgString(getCompilerRT(Args, Component, Type));
}

// Android target triples contain a target version. If we don't have libraries
// for the exact target version, we should fall back to the next newest version
// or a versionless path, if any.
std::optional<std::string>
ToolChain::getFallbackAndroidTargetPath(StringRef BaseDir) const {
  llvm::Triple TripleWithoutLevel(getTriple());
  TripleWithoutLevel.setEnvironmentName("android"); // remove any version number
  const std::string &TripleWithoutLevelStr = TripleWithoutLevel.str();
  unsigned TripleVersion = getTriple().getEnvironmentVersion().getMajor();
  unsigned BestVersion = 0;

  SmallString<32> TripleDir;
  bool UsingUnversionedDir = false;
  std::error_code EC;
  for (llvm::vfs::directory_iterator LI = getVFS().dir_begin(BaseDir, EC), LE;
       !EC && LI != LE; LI = LI.increment(EC)) {
    StringRef DirName = llvm::sys::path::filename(LI->path());
    StringRef DirNameSuffix = DirName;
    if (DirNameSuffix.consume_front(TripleWithoutLevelStr)) {
      if (DirNameSuffix.empty() && TripleDir.empty()) {
        TripleDir = DirName;
        UsingUnversionedDir = true;
      } else {
        unsigned Version;
        if (!DirNameSuffix.getAsInteger(10, Version) && Version > BestVersion &&
            Version < TripleVersion) {
          BestVersion = Version;
          TripleDir = DirName;
          UsingUnversionedDir = false;
        }
      }
    }
  }

  if (TripleDir.empty())
    return {};

  SmallString<128> P(BaseDir);
  llvm::sys::path::append(P, TripleDir);
  if (UsingUnversionedDir)
    D.Diag(diag::warn_android_unversioned_fallback) << P << getTripleString();
  return std::string(P);
}

std::optional<std::string>
ToolChain::getTargetSubDirPath(StringRef BaseDir) const {
  auto getPathForTriple =
      [&](const llvm::Triple &Triple) -> std::optional<std::string> {
    SmallString<128> P(BaseDir);
    llvm::sys::path::append(P, Triple.str());
    if (getVFS().exists(P))
      return std::string(P);
    return {};
  };

  if (auto Path = getPathForTriple(getTriple()))
    return *Path;

  // When building with per target runtime directories, various ways of naming
  // the Arm architecture may have been normalised to simply "arm".
  // For example "armv8l" (Armv8 AArch32 little endian) is replaced with "arm".
  // Since an armv8l system can use libraries built for earlier architecture
  // versions assuming endian and float ABI match.
  //
  // Original triple: armv8l-unknown-linux-gnueabihf
  //  Runtime triple: arm-unknown-linux-gnueabihf
  //
  // We do not do this for armeb (big endian) because doing so could make us
  // select little endian libraries. In addition, all known armeb triples only
  // use the "armeb" architecture name.
  //
  // M profile Arm is bare metal and we know they will not be using the per
  // target runtime directory layout.
  if (getTriple().getArch() == Triple::arm && !getTriple().isArmMClass()) {
    llvm::Triple ArmTriple = getTriple();
    ArmTriple.setArch(Triple::arm);
    if (auto Path = getPathForTriple(ArmTriple))
      return *Path;
  }

  if (getTriple().isAndroid())
    return getFallbackAndroidTargetPath(BaseDir);

  return {};
}

std::optional<std::string> ToolChain::getRuntimePath() const {
  SmallString<128> P(D.ResourceDir);
  llvm::sys::path::append(P, "lib");
  if (auto Ret = getTargetSubDirPath(P))
    return Ret;
  // Darwin does not use per-target runtime directory.
  if (Triple.isOSDarwin())
    return {};
  llvm::sys::path::append(P, Triple.str());
  return std::string(P);
}

std::optional<std::string> ToolChain::getStdlibPath() const {
  SmallString<128> P(D.Dir);
  llvm::sys::path::append(P, "..", "lib");
  return getTargetSubDirPath(P);
}

std::optional<std::string> ToolChain::getStdlibIncludePath() const {
  SmallString<128> P(D.Dir);
  llvm::sys::path::append(P, "..", "include");
  return getTargetSubDirPath(P);
}

ToolChain::path_list ToolChain::getArchSpecificLibPaths() const {
  path_list Paths;

  auto AddPath = [&](const ArrayRef<StringRef> &SS) {
    SmallString<128> Path(getDriver().ResourceDir);
    llvm::sys::path::append(Path, "lib");
    for (auto &S : SS)
      llvm::sys::path::append(Path, S);
    Paths.push_back(std::string(Path));
  };

  AddPath({getTriple().str()});
  AddPath({getOSLibName(), llvm::Triple::getArchTypeName(getArch())});
  return Paths;
}

bool ToolChain::needsProfileRT(const ArgList &Args) {
  if (Args.hasArg(options::OPT_noprofilelib))
    return false;

  return Args.hasArg(options::OPT_fprofile_generate) ||
         Args.hasArg(options::OPT_fprofile_generate_EQ) ||
         Args.hasArg(options::OPT_fcs_profile_generate) ||
         Args.hasArg(options::OPT_fcs_profile_generate_EQ) ||
         Args.hasArg(options::OPT_fprofile_instr_generate) ||
         Args.hasArg(options::OPT_fprofile_instr_generate_EQ) ||
         Args.hasArg(options::OPT_fcreate_profile) ||
         Args.hasArg(options::OPT_forder_file_instrumentation);
}

bool ToolChain::needsGCovInstrumentation(const llvm::opt::ArgList &Args) {
  return Args.hasArg(options::OPT_coverage) ||
         Args.hasFlag(options::OPT_fprofile_arcs, options::OPT_fno_profile_arcs,
                      false);
}

Tool *ToolChain::SelectTool(const JobAction &JA) const {
  if (D.IsFlangMode() && getDriver().ShouldUseFlangCompiler(JA)) return getFlang();
  if (getDriver().ShouldUseClangCompiler(JA)) return getClang();
  Action::ActionClass AC = JA.getKind();
  if (AC == Action::AssembleJobClass && useIntegratedAs() &&
      !getTriple().isOSAIX())
    return getClangAs();
  return getTool(AC);
}

std::string ToolChain::GetFilePath(const char *Name) const {
  return D.GetFilePath(Name, *this);
}

std::string ToolChain::GetProgramPath(const char *Name) const {
  return D.GetProgramPath(Name, *this);
}

std::string ToolChain::GetLinkerPath(bool *LinkerIsLLD) const {
  if (LinkerIsLLD)
    *LinkerIsLLD = false;

  // Get -fuse-ld= first to prevent -Wunused-command-line-argument. -fuse-ld= is
  // considered as the linker flavor, e.g. "bfd", "gold", or "lld".
  const Arg* A = Args.getLastArg(options::OPT_fuse_ld_EQ);
  StringRef UseLinker = A ? A->getValue() : CLANG_DEFAULT_LINKER;

  // --ld-path= takes precedence over -fuse-ld= and specifies the executable
  // name. -B, COMPILER_PATH and PATH and consulted if the value does not
  // contain a path component separator.
  // -fuse-ld=lld can be used with --ld-path= to inform clang that the binary
  // that --ld-path= points to is lld.
  if (const Arg *A = Args.getLastArg(options::OPT_ld_path_EQ)) {
    std::string Path(A->getValue());
    if (!Path.empty()) {
      if (llvm::sys::path::parent_path(Path).empty())
        Path = GetProgramPath(A->getValue());
      if (llvm::sys::fs::can_execute(Path)) {
        if (LinkerIsLLD)
          *LinkerIsLLD = UseLinker == "lld";
        return std::string(Path);
      }
    }
    getDriver().Diag(diag::err_drv_invalid_linker_name) << A->getAsString(Args);
    return GetProgramPath(getDefaultLinker());
  }
  // If we're passed -fuse-ld= with no argument, or with the argument ld,
  // then use whatever the default system linker is.
  if (UseLinker.empty() || UseLinker == "ld") {
    const char *DefaultLinker = getDefaultLinker();
    if (llvm::sys::path::is_absolute(DefaultLinker))
      return std::string(DefaultLinker);
    else
      return GetProgramPath(DefaultLinker);
  }

  // Extending -fuse-ld= to an absolute or relative path is unexpected. Checking
  // for the linker flavor is brittle. In addition, prepending "ld." or "ld64."
  // to a relative path is surprising. This is more complex due to priorities
  // among -B, COMPILER_PATH and PATH. --ld-path= should be used instead.
  if (UseLinker.contains('/'))
    getDriver().Diag(diag::warn_drv_fuse_ld_path);

  if (llvm::sys::path::is_absolute(UseLinker)) {
    // If we're passed what looks like an absolute path, don't attempt to
    // second-guess that.
    if (llvm::sys::fs::can_execute(UseLinker))
      return std::string(UseLinker);
  } else {
    llvm::SmallString<8> LinkerName;
    if (Triple.isOSDarwin())
      LinkerName.append("ld64.");
    else
      LinkerName.append("ld.");
    LinkerName.append(UseLinker);

    std::string LinkerPath(GetProgramPath(LinkerName.c_str()));
    if (llvm::sys::fs::can_execute(LinkerPath)) {
      if (LinkerIsLLD)
        *LinkerIsLLD = UseLinker == "lld";
      return LinkerPath;
    }
  }

  if (A)
    getDriver().Diag(diag::err_drv_invalid_linker_name) << A->getAsString(Args);

  return GetProgramPath(getDefaultLinker());
}

std::string ToolChain::GetStaticLibToolPath() const {
  // TODO: Add support for static lib archiving on Windows
  if (Triple.isOSDarwin())
    return GetProgramPath("libtool");
  return GetProgramPath("llvm-ar");
}

types::ID ToolChain::LookupTypeForExtension(StringRef Ext) const {
  types::ID id = types::lookupTypeForExtension(Ext);

  // Flang always runs the preprocessor and has no notion of "preprocessed
  // fortran". Here, TY_PP_Fortran is coerced to TY_Fortran to avoid treating
  // them differently.
  if (D.IsFlangMode() && id == types::TY_PP_Fortran)
    id = types::TY_Fortran;

  return id;
}

bool ToolChain::HasNativeLLVMSupport() const {
  return false;
}

bool ToolChain::isCrossCompiling() const {
  llvm::Triple HostTriple(LLVM_HOST_TRIPLE);
  switch (HostTriple.getArch()) {
  // The A32/T32/T16 instruction sets are not separate architectures in this
  // context.
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    return getArch() != llvm::Triple::arm && getArch() != llvm::Triple::thumb &&
           getArch() != llvm::Triple::armeb && getArch() != llvm::Triple::thumbeb;
  default:
    return HostTriple.getArch() != getArch();
  }
}

ObjCRuntime ToolChain::getDefaultObjCRuntime(bool isNonFragile) const {
  return ObjCRuntime(isNonFragile ? ObjCRuntime::GNUstep : ObjCRuntime::GCC,
                     VersionTuple());
}

llvm::ExceptionHandling
ToolChain::GetExceptionModel(const llvm::opt::ArgList &Args) const {
  return llvm::ExceptionHandling::None;
}

bool ToolChain::isThreadModelSupported(const StringRef Model) const {
  if (Model == "single") {
    // FIXME: 'single' is only supported on ARM and WebAssembly so far.
    return Triple.getArch() == llvm::Triple::arm ||
           Triple.getArch() == llvm::Triple::armeb ||
           Triple.getArch() == llvm::Triple::thumb ||
           Triple.getArch() == llvm::Triple::thumbeb || Triple.isWasm();
  } else if (Model == "posix")
    return true;

  return false;
}

std::string ToolChain::ComputeLLVMTriple(const ArgList &Args,
                                         types::ID InputType) const {
  switch (getTriple().getArch()) {
  default:
    return getTripleString();

  case llvm::Triple::x86_64: {
    llvm::Triple Triple = getTriple();
    if (!Triple.isOSBinFormatMachO())
      return getTripleString();

    if (Arg *A = Args.getLastArg(options::OPT_march_EQ)) {
      // x86_64h goes in the triple. Other -march options just use the
      // vanilla triple we already have.
      StringRef MArch = A->getValue();
      if (MArch == "x86_64h")
        Triple.setArchName(MArch);
    }
    return Triple.getTriple();
  }
  case llvm::Triple::aarch64: {
    llvm::Triple Triple = getTriple();
    tools::aarch64::setPAuthABIInTriple(getDriver(), Args, Triple);
    if (!Triple.isOSBinFormatMachO())
      return Triple.getTriple();

    if (Triple.isArm64e())
      return Triple.getTriple();

    // FIXME: older versions of ld64 expect the "arm64" component in the actual
    // triple string and query it to determine whether an LTO file can be
    // handled. Remove this when we don't care any more.
    Triple.setArchName("arm64");
    return Triple.getTriple();
  }
  case llvm::Triple::aarch64_32:
    return getTripleString();
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb: {
    llvm::Triple Triple = getTriple();
    tools::arm::setArchNameInTriple(getDriver(), Args, InputType, Triple);
    tools::arm::setFloatABIInTriple(getDriver(), Args, Triple);
    return Triple.getTriple();
  }
  }
}

std::string ToolChain::ComputeEffectiveClangTriple(const ArgList &Args,
                                                   types::ID InputType) const {
  return ComputeLLVMTriple(Args, InputType);
}

std::string ToolChain::computeSysRoot() const {
  return D.SysRoot;
}

void ToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                          ArgStringList &CC1Args) const {
  // Each toolchain should provide the appropriate include flags.
}

void ToolChain::addClangTargetOptions(
    const ArgList &DriverArgs, ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadKind) const {}

void ToolChain::addClangCC1ASTargetOptions(const ArgList &Args,
                                           ArgStringList &CC1ASArgs) const {}

void ToolChain::addClangWarningOptions(ArgStringList &CC1Args) const {}

void ToolChain::addProfileRTLibs(const llvm::opt::ArgList &Args,
                                 llvm::opt::ArgStringList &CmdArgs) const {
  if (!needsProfileRT(Args) && !needsGCovInstrumentation(Args))
    return;

  CmdArgs.push_back(getCompilerRTArgString(Args, "profile"));
}

ToolChain::RuntimeLibType ToolChain::GetRuntimeLibType(
    const ArgList &Args) const {
  if (runtimeLibType)
    return *runtimeLibType;

  const Arg* A = Args.getLastArg(options::OPT_rtlib_EQ);
  StringRef LibName = A ? A->getValue() : CLANG_DEFAULT_RTLIB;

  // Only use "platform" in tests to override CLANG_DEFAULT_RTLIB!
  if (LibName == "compiler-rt")
    runtimeLibType = ToolChain::RLT_CompilerRT;
  else if (LibName == "libgcc")
    runtimeLibType = ToolChain::RLT_Libgcc;
  else if (LibName == "platform")
    runtimeLibType = GetDefaultRuntimeLibType();
  else {
    if (A)
      getDriver().Diag(diag::err_drv_invalid_rtlib_name)
          << A->getAsString(Args);

    runtimeLibType = GetDefaultRuntimeLibType();
  }

  return *runtimeLibType;
}

ToolChain::UnwindLibType ToolChain::GetUnwindLibType(
    const ArgList &Args) const {
  if (unwindLibType)
    return *unwindLibType;

  const Arg *A = Args.getLastArg(options::OPT_unwindlib_EQ);
  StringRef LibName = A ? A->getValue() : CLANG_DEFAULT_UNWINDLIB;

  if (LibName == "none")
    unwindLibType = ToolChain::UNW_None;
  else if (LibName == "platform" || LibName == "") {
    ToolChain::RuntimeLibType RtLibType = GetRuntimeLibType(Args);
    if (RtLibType == ToolChain::RLT_CompilerRT) {
      if (getTriple().isAndroid() || getTriple().isOSAIX())
        unwindLibType = ToolChain::UNW_CompilerRT;
      else
        unwindLibType = ToolChain::UNW_None;
    } else if (RtLibType == ToolChain::RLT_Libgcc)
      unwindLibType = ToolChain::UNW_Libgcc;
  } else if (LibName == "libunwind") {
    if (GetRuntimeLibType(Args) == RLT_Libgcc)
      getDriver().Diag(diag::err_drv_incompatible_unwindlib);
    unwindLibType = ToolChain::UNW_CompilerRT;
  } else if (LibName == "libgcc")
    unwindLibType = ToolChain::UNW_Libgcc;
  else {
    if (A)
      getDriver().Diag(diag::err_drv_invalid_unwindlib_name)
          << A->getAsString(Args);

    unwindLibType = GetDefaultUnwindLibType();
  }

  return *unwindLibType;
}

ToolChain::CXXStdlibType ToolChain::GetCXXStdlibType(const ArgList &Args) const{
  if (cxxStdlibType)
    return *cxxStdlibType;

  const Arg *A = Args.getLastArg(options::OPT_stdlib_EQ);
  StringRef LibName = A ? A->getValue() : CLANG_DEFAULT_CXX_STDLIB;

  // Only use "platform" in tests to override CLANG_DEFAULT_CXX_STDLIB!
  if (LibName == "libc++")
    cxxStdlibType = ToolChain::CST_Libcxx;
  else if (LibName == "libstdc++")
    cxxStdlibType = ToolChain::CST_Libstdcxx;
  else if (LibName == "platform")
    cxxStdlibType = GetDefaultCXXStdlibType();
  else {
    if (A)
      getDriver().Diag(diag::err_drv_invalid_stdlib_name)
          << A->getAsString(Args);

    cxxStdlibType = GetDefaultCXXStdlibType();
  }

  return *cxxStdlibType;
}

/// Utility function to add a system include directory to CC1 arguments.
/*static*/ void ToolChain::addSystemInclude(const ArgList &DriverArgs,
                                            ArgStringList &CC1Args,
                                            const Twine &Path) {
  CC1Args.push_back("-internal-isystem");
  CC1Args.push_back(DriverArgs.MakeArgString(Path));
}

/// Utility function to add a system include directory with extern "C"
/// semantics to CC1 arguments.
///
/// Note that this should be used rarely, and only for directories that
/// historically and for legacy reasons are treated as having implicit extern
/// "C" semantics. These semantics are *ignored* by and large today, but its
/// important to preserve the preprocessor changes resulting from the
/// classification.
/*static*/ void ToolChain::addExternCSystemInclude(const ArgList &DriverArgs,
                                                   ArgStringList &CC1Args,
                                                   const Twine &Path) {
  CC1Args.push_back("-internal-externc-isystem");
  CC1Args.push_back(DriverArgs.MakeArgString(Path));
}

void ToolChain::addExternCSystemIncludeIfExists(const ArgList &DriverArgs,
                                                ArgStringList &CC1Args,
                                                const Twine &Path) {
  if (llvm::sys::fs::exists(Path))
    addExternCSystemInclude(DriverArgs, CC1Args, Path);
}

/// Utility function to add a list of system include directories to CC1.
/*static*/ void ToolChain::addSystemIncludes(const ArgList &DriverArgs,
                                             ArgStringList &CC1Args,
                                             ArrayRef<StringRef> Paths) {
  for (const auto &Path : Paths) {
    CC1Args.push_back("-internal-isystem");
    CC1Args.push_back(DriverArgs.MakeArgString(Path));
  }
}

/*static*/ std::string ToolChain::concat(StringRef Path, const Twine &A,
                                         const Twine &B, const Twine &C,
                                         const Twine &D) {
  SmallString<128> Result(Path);
  llvm::sys::path::append(Result, llvm::sys::path::Style::posix, A, B, C, D);
  return std::string(Result);
}

std::string ToolChain::detectLibcxxVersion(StringRef IncludePath) const {
  std::error_code EC;
  int MaxVersion = 0;
  std::string MaxVersionString;
  SmallString<128> Path(IncludePath);
  llvm::sys::path::append(Path, "c++");
  for (llvm::vfs::directory_iterator LI = getVFS().dir_begin(Path, EC), LE;
       !EC && LI != LE; LI = LI.increment(EC)) {
    StringRef VersionText = llvm::sys::path::filename(LI->path());
    int Version;
    if (VersionText[0] == 'v' &&
        !VersionText.slice(1, StringRef::npos).getAsInteger(10, Version)) {
      if (Version > MaxVersion) {
        MaxVersion = Version;
        MaxVersionString = std::string(VersionText);
      }
    }
  }
  if (!MaxVersion)
    return "";
  return MaxVersionString;
}

void ToolChain::AddClangCXXStdlibIncludeArgs(const ArgList &DriverArgs,
                                             ArgStringList &CC1Args) const {
  // Header search paths should be handled by each of the subclasses.
  // Historically, they have not been, and instead have been handled inside of
  // the CC1-layer frontend. As the logic is hoisted out, this generic function
  // will slowly stop being called.
  //
  // While it is being called, replicate a bit of a hack to propagate the
  // '-stdlib=' flag down to CC1 so that it can in turn customize the C++
  // header search paths with it. Once all systems are overriding this
  // function, the CC1 flag and this line can be removed.
  DriverArgs.AddAllArgs(CC1Args, options::OPT_stdlib_EQ);
}

void ToolChain::AddClangCXXStdlibIsystemArgs(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  DriverArgs.ClaimAllArgs(options::OPT_stdlibxx_isystem);
  // This intentionally only looks at -nostdinc++, and not -nostdinc or
  // -nostdlibinc. The purpose of -stdlib++-isystem is to support toolchain
  // setups with non-standard search logic for the C++ headers, while still
  // allowing users of the toolchain to bring their own C++ headers. Such a
  // toolchain likely also has non-standard search logic for the C headers and
  // uses -nostdinc to suppress the default logic, but -stdlib++-isystem should
  // still work in that case and only be suppressed by an explicit -nostdinc++
  // in a project using the toolchain.
  if (!DriverArgs.hasArg(options::OPT_nostdincxx))
    for (const auto &P :
         DriverArgs.getAllArgValues(options::OPT_stdlibxx_isystem))
      addSystemInclude(DriverArgs, CC1Args, P);
}

bool ToolChain::ShouldLinkCXXStdlib(const llvm::opt::ArgList &Args) const {
  return getDriver().CCCIsCXX() &&
         !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                      options::OPT_nostdlibxx);
}

void ToolChain::AddCXXStdlibLibArgs(const ArgList &Args,
                                    ArgStringList &CmdArgs) const {
  assert(!Args.hasArg(options::OPT_nostdlibxx) &&
         "should not have called this");
  CXXStdlibType Type = GetCXXStdlibType(Args);

  switch (Type) {
  case ToolChain::CST_Libcxx:
    CmdArgs.push_back("-lc++");
    if (Args.hasArg(options::OPT_fexperimental_library))
      CmdArgs.push_back("-lc++experimental");
    break;

  case ToolChain::CST_Libstdcxx:
    CmdArgs.push_back("-lstdc++");
    break;
  }
}

void ToolChain::AddFilePathLibArgs(const ArgList &Args,
                                   ArgStringList &CmdArgs) const {
  for (const auto &LibPath : getFilePaths())
    if(LibPath.length() > 0)
      CmdArgs.push_back(Args.MakeArgString(StringRef("-L") + LibPath));
}

void ToolChain::AddCCKextLibArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  CmdArgs.push_back("-lcc_kext");
}

bool ToolChain::isFastMathRuntimeAvailable(const ArgList &Args,
                                           std::string &Path) const {
  // Don't implicitly link in mode-changing libraries in a shared library, since
  // this can have very deleterious effects. See the various links from
  // https://github.com/llvm/llvm-project/issues/57589 for more information.
  bool Default = !Args.hasArgNoClaim(options::OPT_shared);

  // Do not check for -fno-fast-math or -fno-unsafe-math when -Ofast passed
  // (to keep the linker options consistent with gcc and clang itself).
  if (Default && !isOptimizationLevelFast(Args)) {
    // Check if -ffast-math or -funsafe-math.
    Arg *A = Args.getLastArg(
        options::OPT_ffast_math, options::OPT_fno_fast_math,
        options::OPT_funsafe_math_optimizations,
        options::OPT_fno_unsafe_math_optimizations, options::OPT_ffp_model_EQ);

    if (!A || A->getOption().getID() == options::OPT_fno_fast_math ||
        A->getOption().getID() == options::OPT_fno_unsafe_math_optimizations)
      Default = false;
    if (A && A->getOption().getID() == options::OPT_ffp_model_EQ) {
      StringRef Model = A->getValue();
      if (Model != "fast")
        Default = false;
    }
  }

  // Whatever decision came as a result of the above implicit settings, either
  // -mdaz-ftz or -mno-daz-ftz is capable of overriding it.
  if (!Args.hasFlag(options::OPT_mdaz_ftz, options::OPT_mno_daz_ftz, Default))
    return false;

  // If crtfastmath.o exists add it to the arguments.
  Path = GetFilePath("crtfastmath.o");
  return (Path != "crtfastmath.o"); // Not found.
}

bool ToolChain::addFastMathRuntimeIfAvailable(const ArgList &Args,
                                              ArgStringList &CmdArgs) const {
  std::string Path;
  if (isFastMathRuntimeAvailable(Args, Path)) {
    CmdArgs.push_back(Args.MakeArgString(Path));
    return true;
  }

  return false;
}

Expected<SmallVector<std::string>>
ToolChain::getSystemGPUArchs(const llvm::opt::ArgList &Args) const {
  return SmallVector<std::string>();
}

SanitizerMask ToolChain::getSupportedSanitizers() const {
  // Return sanitizers which don't require runtime support and are not
  // platform dependent.

  SanitizerMask Res =
      (SanitizerKind::Undefined & ~SanitizerKind::Vptr) |
      (SanitizerKind::CFI & ~SanitizerKind::CFIICall) |
      SanitizerKind::CFICastStrict | SanitizerKind::FloatDivideByZero |
      SanitizerKind::KCFI | SanitizerKind::UnsignedIntegerOverflow |
      SanitizerKind::UnsignedShiftBase | SanitizerKind::ImplicitConversion |
      SanitizerKind::Nullability | SanitizerKind::LocalBounds;
  if (getTriple().getArch() == llvm::Triple::x86 ||
      getTriple().getArch() == llvm::Triple::x86_64 ||
      getTriple().getArch() == llvm::Triple::arm || getTriple().isWasm() ||
      getTriple().isAArch64() || getTriple().isRISCV() ||
      getTriple().isLoongArch64())
    Res |= SanitizerKind::CFIICall;
  if (getTriple().getArch() == llvm::Triple::x86_64 ||
      getTriple().isAArch64(64) || getTriple().isRISCV())
    Res |= SanitizerKind::ShadowCallStack;
  if (getTriple().isAArch64(64))
    Res |= SanitizerKind::MemTag;
  return Res;
}

void ToolChain::AddCudaIncludeArgs(const ArgList &DriverArgs,
                                   ArgStringList &CC1Args) const {}

void ToolChain::AddHIPIncludeArgs(const ArgList &DriverArgs,
                                  ArgStringList &CC1Args) const {}

llvm::SmallVector<ToolChain::BitCodeLibraryInfo, 12>
ToolChain::getDeviceLibs(const ArgList &DriverArgs) const {
  return {};
}

void ToolChain::AddIAMCUIncludeArgs(const ArgList &DriverArgs,
                                    ArgStringList &CC1Args) const {}

static VersionTuple separateMSVCFullVersion(unsigned Version) {
  if (Version < 100)
    return VersionTuple(Version);

  if (Version < 10000)
    return VersionTuple(Version / 100, Version % 100);

  unsigned Build = 0, Factor = 1;
  for (; Version > 10000; Version = Version / 10, Factor = Factor * 10)
    Build = Build + (Version % 10) * Factor;
  return VersionTuple(Version / 100, Version % 100, Build);
}

VersionTuple
ToolChain::computeMSVCVersion(const Driver *D,
                              const llvm::opt::ArgList &Args) const {
  const Arg *MSCVersion = Args.getLastArg(options::OPT_fmsc_version);
  const Arg *MSCompatibilityVersion =
      Args.getLastArg(options::OPT_fms_compatibility_version);

  if (MSCVersion && MSCompatibilityVersion) {
    if (D)
      D->Diag(diag::err_drv_argument_not_allowed_with)
          << MSCVersion->getAsString(Args)
          << MSCompatibilityVersion->getAsString(Args);
    return VersionTuple();
  }

  if (MSCompatibilityVersion) {
    VersionTuple MSVT;
    if (MSVT.tryParse(MSCompatibilityVersion->getValue())) {
      if (D)
        D->Diag(diag::err_drv_invalid_value)
            << MSCompatibilityVersion->getAsString(Args)
            << MSCompatibilityVersion->getValue();
    } else {
      return MSVT;
    }
  }

  if (MSCVersion) {
    unsigned Version = 0;
    if (StringRef(MSCVersion->getValue()).getAsInteger(10, Version)) {
      if (D)
        D->Diag(diag::err_drv_invalid_value)
            << MSCVersion->getAsString(Args) << MSCVersion->getValue();
    } else {
      return separateMSVCFullVersion(Version);
    }
  }

  return VersionTuple();
}

llvm::opt::DerivedArgList *ToolChain::TranslateOpenMPTargetArgs(
    const llvm::opt::DerivedArgList &Args, bool SameTripleAsHost,
    SmallVectorImpl<llvm::opt::Arg *> &AllocatedArgs) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  const OptTable &Opts = getDriver().getOpts();
  bool Modified = false;

  // Handle -Xopenmp-target flags
  for (auto *A : Args) {
    // Exclude flags which may only apply to the host toolchain.
    // Do not exclude flags when the host triple (AuxTriple)
    // matches the current toolchain triple. If it is not present
    // at all, target and host share a toolchain.
    if (A->getOption().matches(options::OPT_m_Group)) {
      // Pass code object version to device toolchain
      // to correctly set metadata in intermediate files.
      if (SameTripleAsHost ||
          A->getOption().matches(options::OPT_mcode_object_version_EQ))
        DAL->append(A);
      else
        Modified = true;
      continue;
    }

    unsigned Index;
    unsigned Prev;
    bool XOpenMPTargetNoTriple =
        A->getOption().matches(options::OPT_Xopenmp_target);

    if (A->getOption().matches(options::OPT_Xopenmp_target_EQ)) {
      llvm::Triple TT(getOpenMPTriple(A->getValue(0)));

      // Passing device args: -Xopenmp-target=<triple> -opt=val.
      if (TT.getTriple() == getTripleString())
        Index = Args.getBaseArgs().MakeIndex(A->getValue(1));
      else
        continue;
    } else if (XOpenMPTargetNoTriple) {
      // Passing device args: -Xopenmp-target -opt=val.
      Index = Args.getBaseArgs().MakeIndex(A->getValue(0));
    } else {
      DAL->append(A);
      continue;
    }

    // Parse the argument to -Xopenmp-target.
    Prev = Index;
    std::unique_ptr<Arg> XOpenMPTargetArg(Opts.ParseOneArg(Args, Index));
    if (!XOpenMPTargetArg || Index > Prev + 1) {
      getDriver().Diag(diag::err_drv_invalid_Xopenmp_target_with_args)
          << A->getAsString(Args);
      continue;
    }
    if (XOpenMPTargetNoTriple && XOpenMPTargetArg &&
        Args.getAllArgValues(options::OPT_fopenmp_targets_EQ).size() != 1) {
      getDriver().Diag(diag::err_drv_Xopenmp_target_missing_triple);
      continue;
    }
    XOpenMPTargetArg->setBaseArg(A);
    A = XOpenMPTargetArg.release();
    AllocatedArgs.push_back(A);
    DAL->append(A);
    Modified = true;
  }

  if (Modified)
    return DAL;

  delete DAL;
  return nullptr;
}

// TODO: Currently argument values separated by space e.g.
// -Xclang -mframe-pointer=no cannot be passed by -Xarch_. This should be
// fixed.
void ToolChain::TranslateXarchArgs(
    const llvm::opt::DerivedArgList &Args, llvm::opt::Arg *&A,
    llvm::opt::DerivedArgList *DAL,
    SmallVectorImpl<llvm::opt::Arg *> *AllocatedArgs) const {
  const OptTable &Opts = getDriver().getOpts();
  unsigned ValuePos = 1;
  if (A->getOption().matches(options::OPT_Xarch_device) ||
      A->getOption().matches(options::OPT_Xarch_host))
    ValuePos = 0;

  unsigned Index = Args.getBaseArgs().MakeIndex(A->getValue(ValuePos));
  unsigned Prev = Index;
  std::unique_ptr<llvm::opt::Arg> XarchArg(Opts.ParseOneArg(Args, Index));

  // If the argument parsing failed or more than one argument was
  // consumed, the -Xarch_ argument's parameter tried to consume
  // extra arguments. Emit an error and ignore.
  //
  // We also want to disallow any options which would alter the
  // driver behavior; that isn't going to work in our model. We
  // use options::NoXarchOption to control this.
  if (!XarchArg || Index > Prev + 1) {
    getDriver().Diag(diag::err_drv_invalid_Xarch_argument_with_args)
        << A->getAsString(Args);
    return;
  } else if (XarchArg->getOption().hasFlag(options::NoXarchOption)) {
    auto &Diags = getDriver().getDiags();
    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Error,
                              "invalid Xarch argument: '%0', not all driver "
                              "options can be forwared via Xarch argument");
    Diags.Report(DiagID) << A->getAsString(Args);
    return;
  }
  XarchArg->setBaseArg(A);
  A = XarchArg.release();
  if (!AllocatedArgs)
    DAL->AddSynthesizedArg(A);
  else
    AllocatedArgs->push_back(A);
}

llvm::opt::DerivedArgList *ToolChain::TranslateXarchArgs(
    const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
    Action::OffloadKind OFK,
    SmallVectorImpl<llvm::opt::Arg *> *AllocatedArgs) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  bool Modified = false;

  bool IsDevice = OFK != Action::OFK_None && OFK != Action::OFK_Host;
  for (Arg *A : Args) {
    bool NeedTrans = false;
    bool Skip = false;
    if (A->getOption().matches(options::OPT_Xarch_device)) {
      NeedTrans = IsDevice;
      Skip = !IsDevice;
    } else if (A->getOption().matches(options::OPT_Xarch_host)) {
      NeedTrans = !IsDevice;
      Skip = IsDevice;
    } else if (A->getOption().matches(options::OPT_Xarch__) && IsDevice) {
      // Do not translate -Xarch_ options for non CUDA/HIP toolchain since
      // they may need special translation.
      // Skip this argument unless the architecture matches BoundArch
      if (BoundArch.empty() || A->getValue(0) != BoundArch)
        Skip = true;
      else
        NeedTrans = true;
    }
    if (NeedTrans || Skip)
      Modified = true;
    if (NeedTrans)
      TranslateXarchArgs(Args, A, DAL, AllocatedArgs);
    if (!Skip)
      DAL->append(A);
  }

  if (Modified)
    return DAL;

  delete DAL;
  return nullptr;
}
