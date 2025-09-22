//===--- CommonArgs.cpp - Args handling for multiple toolchains -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommonArgs.h"
#include "Arch/AArch64.h"
#include "Arch/ARM.h"
#include "Arch/CSKY.h"
#include "Arch/LoongArch.h"
#include "Arch/M68k.h"
#include "Arch/Mips.h"
#include "Arch/PPC.h"
#include "Arch/RISCV.h"
#include "Arch/Sparc.h"
#include "Arch/SystemZ.h"
#include "Arch/VE.h"
#include "Arch/X86.h"
#include "HIPAMD.h"
#include "Hexagon.h"
#include "MSP430.h"
#include "Solaris.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Driver/Util.h"
#include "clang/Driver/XRayArgs.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/TargetParser.h"
#include <optional>

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

static bool useFramePointerForTargetByDefault(const llvm::opt::ArgList &Args,
                                              const llvm::Triple &Triple) {
  if (Args.hasArg(clang::driver::options::OPT_pg) &&
      !Args.hasArg(clang::driver::options::OPT_mfentry))
    return true;

  if (Triple.isAndroid())
    return true;

  switch (Triple.getArch()) {
  case llvm::Triple::xcore:
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
  case llvm::Triple::msp430:
    // XCore never wants frame pointers, regardless of OS.
    // WebAssembly never wants frame pointers.
    return false;
  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::sparcv9:
  case llvm::Triple::amdgcn:
  case llvm::Triple::r600:
  case llvm::Triple::csky:
  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64:
  case llvm::Triple::m68k:
    return !clang::driver::tools::areOptimizationsEnabled(Args);
  default:
    break;
  }

  if (Triple.isOSFuchsia() || Triple.isOSNetBSD()) {
    return !clang::driver::tools::areOptimizationsEnabled(Args);
  }

  if (Triple.isOSLinux() || Triple.isOSHurd()) {
    switch (Triple.getArch()) {
    // Don't use a frame pointer on linux if optimizing for certain targets.
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::systemz:
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      return !clang::driver::tools::areOptimizationsEnabled(Args);
    default:
      return true;
    }
  }

  if (Triple.isOSWindows()) {
    switch (Triple.getArch()) {
    case llvm::Triple::x86:
      return !clang::driver::tools::areOptimizationsEnabled(Args);
    case llvm::Triple::x86_64:
      return Triple.isOSBinFormatMachO();
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      // Windows on ARM builds with FPO disabled to aid fast stack walking
      return true;
    default:
      // All other supported Windows ISAs use xdata unwind information, so frame
      // pointers are not generally useful.
      return false;
    }
  }

  return true;
}

static bool useLeafFramePointerForTargetByDefault(const llvm::Triple &Triple) {
  if (Triple.isAArch64() || Triple.isPS() || Triple.isVE() ||
      (Triple.isAndroid() && !Triple.isARM()))
    return false;

  return true;
}

static bool mustUseNonLeafFramePointerForTarget(const llvm::Triple &Triple) {
  switch (Triple.getArch()) {
  default:
    return false;
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    // ARM Darwin targets require a frame pointer to be always present to aid
    // offline debugging via backtraces.
    return Triple.isOSDarwin();
  }
}

// True if a target-specific option requires the frame chain to be preserved,
// even if new frame records are not created.
static bool mustMaintainValidFrameChain(const llvm::opt::ArgList &Args,
                                        const llvm::Triple &Triple) {
  if (Triple.isARM() || Triple.isThumb()) {
    // For 32-bit Arm, the -mframe-chain=aapcs and -mframe-chain=aapcs+leaf
    // options require the frame pointer register to be reserved (or point to a
    // new AAPCS-compilant frame record), even with	-fno-omit-frame-pointer.
    if (Arg *A = Args.getLastArg(options::OPT_mframe_chain)) {
      StringRef V = A->getValue();
      return V != "none";
    }
    return false;
  }
  return false;
}

// True if a target-specific option causes -fno-omit-frame-pointer to also
// cause frame records to be created in leaf functions.
static bool framePointerImpliesLeafFramePointer(const llvm::opt::ArgList &Args,
                                                const llvm::Triple &Triple) {
  if (Triple.isARM() || Triple.isThumb()) {
    // For 32-bit Arm, the -mframe-chain=aapcs+leaf option causes the
    // -fno-omit-frame-pointer optiion to imply -mno-omit-leaf-frame-pointer,
    // but does not by itself imply either option.
    if (Arg *A = Args.getLastArg(options::OPT_mframe_chain)) {
      StringRef V = A->getValue();
      return V == "aapcs+leaf";
    }
    return false;
  }
  return false;
}

clang::CodeGenOptions::FramePointerKind
getFramePointerKind(const llvm::opt::ArgList &Args,
                    const llvm::Triple &Triple) {
  // There are three things to consider here:
  // * Should a frame record be created for non-leaf functions?
  // * Should a frame record be created for leaf functions?
  // * Is the frame pointer register reserved, i.e. must it always point to
  //   either a new, valid frame record or be un-modified?
  //
  //  Not all combinations of these are valid:
  //  * It's not useful to have leaf frame records without non-leaf ones.
  //  * It's not useful to have frame records without reserving the frame
  //    pointer.
  //
  // | Non-leaf | Leaf | Reserved |
  // | N        | N    | N        | FramePointerKind::None
  // | N        | N    | Y        | FramePointerKind::Reserved
  // | N        | Y    | N        | Invalid
  // | N        | Y    | Y        | Invalid
  // | Y        | N    | N        | Invalid
  // | Y        | N    | Y        | FramePointerKind::NonLeaf
  // | Y        | Y    | N        | Invalid
  // | Y        | Y    | Y        | FramePointerKind::All
  //
  // The FramePointerKind::Reserved case is currently only reachable for Arm,
  // which has the -mframe-chain= option which can (in combination with
  // -fno-omit-frame-pointer) specify that the frame chain must be valid,
  // without requiring new frame records to be created.

  bool DefaultFP = useFramePointerForTargetByDefault(Args, Triple);
  bool EnableFP =
      mustUseNonLeafFramePointerForTarget(Triple) ||
      Args.hasFlag(clang::driver::options::OPT_fno_omit_frame_pointer,
                   clang::driver::options::OPT_fomit_frame_pointer, DefaultFP);

  bool DefaultLeafFP =
      useLeafFramePointerForTargetByDefault(Triple) ||
      (EnableFP && framePointerImpliesLeafFramePointer(Args, Triple));
  bool EnableLeafFP = Args.hasFlag(
      clang::driver::options::OPT_mno_omit_leaf_frame_pointer,
      clang::driver::options::OPT_momit_leaf_frame_pointer, DefaultLeafFP);

  bool FPRegReserved = EnableFP || mustMaintainValidFrameChain(Args, Triple);

  if (EnableFP) {
    if (EnableLeafFP)
      return clang::CodeGenOptions::FramePointerKind::All;
    return clang::CodeGenOptions::FramePointerKind::NonLeaf;
  }
  if (FPRegReserved)
    return clang::CodeGenOptions::FramePointerKind::Reserved;
  return clang::CodeGenOptions::FramePointerKind::None;
}

static void renderRpassOptions(const ArgList &Args, ArgStringList &CmdArgs,
                               const StringRef PluginOptPrefix) {
  if (const Arg *A = Args.getLastArg(options::OPT_Rpass_EQ))
    CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                         "-pass-remarks=" + A->getValue()));

  if (const Arg *A = Args.getLastArg(options::OPT_Rpass_missed_EQ))
    CmdArgs.push_back(Args.MakeArgString(
        Twine(PluginOptPrefix) + "-pass-remarks-missed=" + A->getValue()));

  if (const Arg *A = Args.getLastArg(options::OPT_Rpass_analysis_EQ))
    CmdArgs.push_back(Args.MakeArgString(
        Twine(PluginOptPrefix) + "-pass-remarks-analysis=" + A->getValue()));
}

static void renderRemarksOptions(const ArgList &Args, ArgStringList &CmdArgs,
                                 const llvm::Triple &Triple,
                                 const InputInfo &Input,
                                 const InputInfo &Output,
                                 const StringRef PluginOptPrefix) {
  StringRef Format = "yaml";
  if (const Arg *A = Args.getLastArg(options::OPT_fsave_optimization_record_EQ))
    Format = A->getValue();

  SmallString<128> F;
  const Arg *A = Args.getLastArg(options::OPT_foptimization_record_file_EQ);
  if (A)
    F = A->getValue();
  else if (Output.isFilename())
    F = Output.getFilename();

  assert(!F.empty() && "Cannot determine remarks output name.");
  // Append "opt.ld.<format>" to the end of the file name.
  CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                       "opt-remarks-filename=" + F +
                                       ".opt.ld." + Format));

  if (const Arg *A =
          Args.getLastArg(options::OPT_foptimization_record_passes_EQ))
    CmdArgs.push_back(Args.MakeArgString(
        Twine(PluginOptPrefix) + "opt-remarks-passes=" + A->getValue()));

  CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                       "opt-remarks-format=" + Format.data()));
}

static void renderRemarksHotnessOptions(const ArgList &Args,
                                        ArgStringList &CmdArgs,
                                        const StringRef PluginOptPrefix) {
  if (Args.hasFlag(options::OPT_fdiagnostics_show_hotness,
                   options::OPT_fno_diagnostics_show_hotness, false))
    CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                         "opt-remarks-with-hotness"));

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ))
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) +
                           "opt-remarks-hotness-threshold=" + A->getValue()));
}

static bool shouldIgnoreUnsupportedTargetFeature(const Arg &TargetFeatureArg,
                                                 llvm::Triple T,
                                                 StringRef Processor) {
  // Warn no-cumode for AMDGCN processors not supporing WGP mode.
  if (!T.isAMDGPU())
    return false;
  auto GPUKind = T.isAMDGCN() ? llvm::AMDGPU::parseArchAMDGCN(Processor)
                              : llvm::AMDGPU::parseArchR600(Processor);
  auto GPUFeatures = T.isAMDGCN() ? llvm::AMDGPU::getArchAttrAMDGCN(GPUKind)
                                  : llvm::AMDGPU::getArchAttrR600(GPUKind);
  if (GPUFeatures & llvm::AMDGPU::FEATURE_WGP)
    return false;
  return TargetFeatureArg.getOption().matches(options::OPT_mno_cumode);
}

void tools::addPathIfExists(const Driver &D, const Twine &Path,
                            ToolChain::path_list &Paths) {
  if (D.getVFS().exists(Path))
    Paths.push_back(Path.str());
}

void tools::handleTargetFeaturesGroup(const Driver &D,
                                      const llvm::Triple &Triple,
                                      const ArgList &Args,
                                      std::vector<StringRef> &Features,
                                      OptSpecifier Group) {
  std::set<StringRef> Warned;
  for (const Arg *A : Args.filtered(Group)) {
    StringRef Name = A->getOption().getName();
    A->claim();

    // Skip over "-m".
    assert(Name.starts_with("m") && "Invalid feature name.");
    Name = Name.substr(1);

    auto Proc = getCPUName(D, Args, Triple);
    if (shouldIgnoreUnsupportedTargetFeature(*A, Triple, Proc)) {
      if (Warned.count(Name) == 0) {
        D.getDiags().Report(
            clang::diag::warn_drv_unsupported_option_for_processor)
            << A->getAsString(Args) << Proc;
        Warned.insert(Name);
      }
      continue;
    }

    bool IsNegative = Name.consume_front("no-");

    Features.push_back(Args.MakeArgString((IsNegative ? "-" : "+") + Name));
  }
}

SmallVector<StringRef>
tools::unifyTargetFeatures(ArrayRef<StringRef> Features) {
  // Only add a feature if it hasn't been seen before starting from the end.
  SmallVector<StringRef> UnifiedFeatures;
  llvm::DenseSet<StringRef> UsedFeatures;
  for (StringRef Feature : llvm::reverse(Features)) {
    if (UsedFeatures.insert(Feature.drop_front()).second)
      UnifiedFeatures.insert(UnifiedFeatures.begin(), Feature);
  }

  return UnifiedFeatures;
}

void tools::addDirectoryList(const ArgList &Args, ArgStringList &CmdArgs,
                             const char *ArgName, const char *EnvVar) {
  const char *DirList = ::getenv(EnvVar);
  bool CombinedArg = false;

  if (!DirList)
    return; // Nothing to do.

  StringRef Name(ArgName);
  if (Name == "-I" || Name == "-L" || Name.empty())
    CombinedArg = true;

  StringRef Dirs(DirList);
  if (Dirs.empty()) // Empty string should not add '.'.
    return;

  StringRef::size_type Delim;
  while ((Delim = Dirs.find(llvm::sys::EnvPathSeparator)) != StringRef::npos) {
    if (Delim == 0) { // Leading colon.
      if (CombinedArg) {
        CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + "."));
      } else {
        CmdArgs.push_back(ArgName);
        CmdArgs.push_back(".");
      }
    } else {
      if (CombinedArg) {
        CmdArgs.push_back(
            Args.MakeArgString(std::string(ArgName) + Dirs.substr(0, Delim)));
      } else {
        CmdArgs.push_back(ArgName);
        CmdArgs.push_back(Args.MakeArgString(Dirs.substr(0, Delim)));
      }
    }
    Dirs = Dirs.substr(Delim + 1);
  }

  if (Dirs.empty()) { // Trailing colon.
    if (CombinedArg) {
      CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + "."));
    } else {
      CmdArgs.push_back(ArgName);
      CmdArgs.push_back(".");
    }
  } else { // Add the last path.
    if (CombinedArg) {
      CmdArgs.push_back(Args.MakeArgString(std::string(ArgName) + Dirs));
    } else {
      CmdArgs.push_back(ArgName);
      CmdArgs.push_back(Args.MakeArgString(Dirs));
    }
  }
}

void tools::AddLinkerInputs(const ToolChain &TC, const InputInfoList &Inputs,
                            const ArgList &Args, ArgStringList &CmdArgs,
                            const JobAction &JA) {
  const Driver &D = TC.getDriver();

  // Add extra linker input arguments which are not treated as inputs
  // (constructed via -Xarch_).
  Args.AddAllArgValues(CmdArgs, options::OPT_Zlinker_input);

  // LIBRARY_PATH are included before user inputs and only supported on native
  // toolchains.
  if (!TC.isCrossCompiling())
    addDirectoryList(Args, CmdArgs, "-L", "LIBRARY_PATH");

  for (const auto &II : Inputs) {
    // If the current tool chain refers to an OpenMP offloading host, we
    // should ignore inputs that refer to OpenMP offloading devices -
    // they will be embedded according to a proper linker script.
    if (auto *IA = II.getAction())
      if ((JA.isHostOffloading(Action::OFK_OpenMP) &&
           IA->isDeviceOffloading(Action::OFK_OpenMP)))
        continue;

    if (!TC.HasNativeLLVMSupport() && types::isLLVMIR(II.getType()))
      // Don't try to pass LLVM inputs unless we have native support.
      D.Diag(diag::err_drv_no_linker_llvm_support) << TC.getTripleString();

    // Add filenames immediately.
    if (II.isFilename()) {
      CmdArgs.push_back(II.getFilename());
      continue;
    }

    // In some error cases, the input could be Nothing; skip those.
    if (II.isNothing())
      continue;

    // Otherwise, this is a linker input argument.
    const Arg &A = II.getInputArg();

    // Handle reserved library options.
    if (A.getOption().matches(options::OPT_Z_reserved_lib_stdcxx))
      TC.AddCXXStdlibLibArgs(Args, CmdArgs);
    else if (A.getOption().matches(options::OPT_Z_reserved_lib_cckext))
      TC.AddCCKextLibArgs(Args, CmdArgs);
    else
      A.renderAsInput(Args, CmdArgs);
  }
}

void tools::addLinkerCompressDebugSectionsOption(
    const ToolChain &TC, const llvm::opt::ArgList &Args,
    llvm::opt::ArgStringList &CmdArgs) {
  // GNU ld supports --compress-debug-sections=none|zlib|zlib-gnu|zlib-gabi
  // whereas zlib is an alias to zlib-gabi and zlib-gnu is obsoleted. Therefore
  // -gz=none|zlib are translated to --compress-debug-sections=none|zlib. -gz
  // is not translated since ld --compress-debug-sections option requires an
  // argument.
  if (const Arg *A = Args.getLastArg(options::OPT_gz_EQ)) {
    StringRef V = A->getValue();
    if (V == "none" || V == "zlib" || V == "zstd")
      CmdArgs.push_back(Args.MakeArgString("--compress-debug-sections=" + V));
    else
      TC.getDriver().Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << V;
  }
}

void tools::AddTargetFeature(const ArgList &Args,
                             std::vector<StringRef> &Features,
                             OptSpecifier OnOpt, OptSpecifier OffOpt,
                             StringRef FeatureName) {
  if (Arg *A = Args.getLastArg(OnOpt, OffOpt)) {
    if (A->getOption().matches(OnOpt))
      Features.push_back(Args.MakeArgString("+" + FeatureName));
    else
      Features.push_back(Args.MakeArgString("-" + FeatureName));
  }
}

/// Get the (LLVM) name of the AMDGPU gpu we are targeting.
static std::string getAMDGPUTargetGPU(const llvm::Triple &T,
                                      const ArgList &Args) {
  Arg *MArch = Args.getLastArg(options::OPT_march_EQ);
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    auto GPUName = getProcessorFromTargetID(T, A->getValue());
    return llvm::StringSwitch<std::string>(GPUName)
        .Cases("rv630", "rv635", "r600")
        .Cases("rv610", "rv620", "rs780", "rs880")
        .Case("rv740", "rv770")
        .Case("palm", "cedar")
        .Cases("sumo", "sumo2", "sumo")
        .Case("hemlock", "cypress")
        .Case("aruba", "cayman")
        .Default(GPUName.str());
  }
  if (MArch)
    return getProcessorFromTargetID(T, MArch->getValue()).str();
  return "";
}

static std::string getLanaiTargetCPU(const ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    return A->getValue();
  }
  return "";
}

/// Get the (LLVM) name of the WebAssembly cpu we are targeting.
static StringRef getWebAssemblyTargetCPU(const ArgList &Args) {
  // If we have -mcpu=, use that.
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    StringRef CPU = A->getValue();

#ifdef __wasm__
    // Handle "native" by examining the host. "native" isn't meaningful when
    // cross compiling, so only support this when the host is also WebAssembly.
    if (CPU == "native")
      return llvm::sys::getHostCPUName();
#endif

    return CPU;
  }

  return "generic";
}

std::string tools::getCPUName(const Driver &D, const ArgList &Args,
                              const llvm::Triple &T, bool FromAs) {
  Arg *A;

  switch (T.getArch()) {
  default:
    return "";

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
    return aarch64::getAArch64TargetCPU(Args, T, A);

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb: {
    StringRef MArch, MCPU;
    arm::getARMArchCPUFromArgs(Args, MArch, MCPU, FromAs);
    return arm::getARMTargetCPU(MCPU, MArch, T);
  }

  case llvm::Triple::avr:
    if (const Arg *A = Args.getLastArg(options::OPT_mmcu_EQ))
      return A->getValue();
    return "";

  case llvm::Triple::m68k:
    return m68k::getM68kTargetCPU(Args);

  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el: {
    StringRef CPUName;
    StringRef ABIName;
    mips::getMipsCPUAndABI(Args, T, CPUName, ABIName);
    return std::string(CPUName);
  }

  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    if (const Arg *A = Args.getLastArg(options::OPT_march_EQ))
      return A->getValue();
    return "";

  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    return ppc::getPPCTargetCPU(D, Args, T);

  case llvm::Triple::csky:
    if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
      return A->getValue();
    else if (const Arg *A = Args.getLastArg(options::OPT_march_EQ))
      return A->getValue();
    else
      return "ck810";
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    return riscv::getRISCVTargetCPU(Args, T);

  case llvm::Triple::bpfel:
  case llvm::Triple::bpfeb:
    if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
      return A->getValue();
    return "";

  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::sparcv9:
    return sparc::getSparcTargetCPU(D, Args, T);

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return x86::getX86TargetCPU(D, Args, T);

  case llvm::Triple::hexagon:
    return "hexagon" +
           toolchains::HexagonToolChain::GetTargetCPUVersion(Args).str();

  case llvm::Triple::lanai:
    return getLanaiTargetCPU(Args);

  case llvm::Triple::systemz:
    return systemz::getSystemZTargetCPU(Args);

  case llvm::Triple::r600:
  case llvm::Triple::amdgcn:
    return getAMDGPUTargetGPU(T, Args);

  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    return std::string(getWebAssemblyTargetCPU(Args));

  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64:
    return loongarch::getLoongArchTargetCPU(Args, T);
  }
}

static void getWebAssemblyTargetFeatures(const Driver &D,
                                         const llvm::Triple &Triple,
                                         const ArgList &Args,
                                         std::vector<StringRef> &Features) {
  handleTargetFeaturesGroup(D, Triple, Args, Features,
                            options::OPT_m_wasm_Features_Group);
}

void tools::getTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                              const ArgList &Args, ArgStringList &CmdArgs,
                              bool ForAS, bool IsAux) {
  std::vector<StringRef> Features;
  switch (Triple.getArch()) {
  default:
    break;
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    mips::getMIPSTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    arm::getARMTargetFeatures(D, Triple, Args, Features, ForAS);
    break;
  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    ppc::getPPCTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    riscv::getRISCVTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::systemz:
    systemz::getSystemZTargetFeatures(D, Args, Features);
    break;
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
    aarch64::getAArch64TargetFeatures(D, Triple, Args, Features, ForAS);
    break;
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    x86::getX86TargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::hexagon:
    hexagon::getHexagonTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    getWebAssemblyTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::sparcv9:
    sparc::getSparcTargetFeatures(D, Args, Features);
    break;
  case llvm::Triple::r600:
  case llvm::Triple::amdgcn:
    amdgpu::getAMDGPUTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    NVPTX::getNVPTXTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::m68k:
    m68k::getM68kTargetFeatures(D, Triple, Args, Features);
    break;
  case llvm::Triple::msp430:
    msp430::getMSP430TargetFeatures(D, Args, Features);
    break;
  case llvm::Triple::ve:
    ve::getVETargetFeatures(D, Args, Features);
    break;
  case llvm::Triple::csky:
    csky::getCSKYTargetFeatures(D, Triple, Args, CmdArgs, Features);
    break;
  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64:
    loongarch::getLoongArchTargetFeatures(D, Triple, Args, Features);
    break;
  }

  for (auto Feature : unifyTargetFeatures(Features)) {
    CmdArgs.push_back(IsAux ? "-aux-target-feature" : "-target-feature");
    CmdArgs.push_back(Feature.data());
  }
}

llvm::StringRef tools::getLTOParallelism(const ArgList &Args, const Driver &D) {
  Arg *LtoJobsArg = Args.getLastArg(options::OPT_flto_jobs_EQ);
  if (!LtoJobsArg)
    return {};
  if (!llvm::get_threadpool_strategy(LtoJobsArg->getValue()))
    D.Diag(diag::err_drv_invalid_int_value)
        << LtoJobsArg->getAsString(Args) << LtoJobsArg->getValue();
  return LtoJobsArg->getValue();
}

// PS4/PS5 uses -ffunction-sections and -fdata-sections by default.
bool tools::isUseSeparateSections(const llvm::Triple &Triple) {
  return Triple.isPS();
}

bool tools::isTLSDESCEnabled(const ToolChain &TC,
                             const llvm::opt::ArgList &Args) {
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  Arg *A = Args.getLastArg(options::OPT_mtls_dialect_EQ);
  if (!A)
    return Triple.hasDefaultTLSDESC();
  StringRef V = A->getValue();
  bool SupportedArgument = false, EnableTLSDESC = false;
  bool Unsupported = !Triple.isOSBinFormatELF();
  if (Triple.isLoongArch() || Triple.isRISCV()) {
    SupportedArgument = V == "desc" || V == "trad";
    EnableTLSDESC = V == "desc";
  } else if (Triple.isX86()) {
    SupportedArgument = V == "gnu" || V == "gnu2";
    EnableTLSDESC = V == "gnu2";
  } else {
    Unsupported = true;
  }
  if (Unsupported) {
    TC.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getSpelling() << Triple.getTriple();
  } else if (!SupportedArgument) {
    TC.getDriver().Diag(diag::err_drv_unsupported_option_argument_for_target)
        << A->getSpelling() << V << Triple.getTriple();
  }
  return EnableTLSDESC;
}

void tools::addLTOOptions(const ToolChain &ToolChain, const ArgList &Args,
                          ArgStringList &CmdArgs, const InputInfo &Output,
                          const InputInfo &Input, bool IsThinLTO) {
  const llvm::Triple &Triple = ToolChain.getTriple();
  const bool IsOSAIX = Triple.isOSAIX();
  const bool IsAMDGCN = Triple.isAMDGCN();
  const char *Linker = Args.MakeArgString(ToolChain.GetLinkerPath());
  const Driver &D = ToolChain.getDriver();
  const bool IsFatLTO = Args.hasArg(options::OPT_ffat_lto_objects);
  const bool IsUnifiedLTO = Args.hasArg(options::OPT_funified_lto);
  if (llvm::sys::path::filename(Linker) != "ld.lld" &&
      llvm::sys::path::stem(Linker) != "ld.lld" && !Triple.isOSOpenBSD()) {
    // Tell the linker to load the plugin. This has to come before
    // AddLinkerInputs as gold requires -plugin and AIX ld requires -bplugin to
    // come before any -plugin-opt/-bplugin_opt that -Wl might forward.
    const char *PluginPrefix = IsOSAIX ? "-bplugin:" : "";
    const char *PluginName = IsOSAIX ? "/libLTO" : "/LLVMgold";

    if (!IsOSAIX)
      CmdArgs.push_back("-plugin");

#if defined(_WIN32)
    const char *Suffix = ".dll";
#elif defined(__APPLE__)
    const char *Suffix = ".dylib";
#else
    const char *Suffix = ".so";
#endif

    SmallString<1024> Plugin;
    llvm::sys::path::native(Twine(D.Dir) +
                                "/../" CLANG_INSTALL_LIBDIR_BASENAME +
                                PluginName + Suffix,
                            Plugin);
    CmdArgs.push_back(Args.MakeArgString(Twine(PluginPrefix) + Plugin));
  } else {
    // Tell LLD to find and use .llvm.lto section in regular relocatable object
    // files
    if (IsFatLTO)
      CmdArgs.push_back("--fat-lto-objects");
  }

  const char *PluginOptPrefix = IsOSAIX ? "-bplugin_opt:" : "-plugin-opt=";
  const char *ExtraDash = IsOSAIX ? "-" : "";
  const char *ParallelismOpt = IsOSAIX ? "-threads=" : "jobs=";

  // Note, this solution is far from perfect, better to encode it into IR
  // metadata, but this may not be worth it, since it looks like aranges is on
  // the way out.
  if (Args.hasArg(options::OPT_gdwarf_aranges)) {
    CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                         "-generate-arange-section"));
  }

  // Pass vector library arguments to LTO.
  Arg *ArgVecLib = Args.getLastArg(options::OPT_fveclib);
  if (ArgVecLib && ArgVecLib->getNumValues() == 1) {
    // Map the vector library names from clang front-end to opt front-end. The
    // values are taken from the TargetLibraryInfo class command line options.
    std::optional<StringRef> OptVal =
        llvm::StringSwitch<std::optional<StringRef>>(ArgVecLib->getValue())
            .Case("Accelerate", "Accelerate")
            .Case("LIBMVEC", "LIBMVEC-X86")
            .Case("MASSV", "MASSV")
            .Case("SVML", "SVML")
            .Case("SLEEF", "sleefgnuabi")
            .Case("Darwin_libsystem_m", "Darwin_libsystem_m")
            .Case("ArmPL", "ArmPL")
            .Case("none", "none")
            .Default(std::nullopt);

    if (OptVal)
      CmdArgs.push_back(Args.MakeArgString(
          Twine(PluginOptPrefix) + "-vector-library=" + OptVal.value()));
  }

  // Try to pass driver level flags relevant to LTO code generation down to
  // the plugin.

  // Handle flags for selecting CPU variants.
  std::string CPU = getCPUName(D, Args, Triple);
  if (!CPU.empty())
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + ExtraDash + "mcpu=" + CPU));

  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    // The optimization level matches
    // CompilerInvocation.cpp:getOptimizationLevel().
    StringRef OOpt;
    if (A->getOption().matches(options::OPT_O4) ||
        A->getOption().matches(options::OPT_Ofast))
      OOpt = "3";
    else if (A->getOption().matches(options::OPT_O)) {
      OOpt = A->getValue();
      if (OOpt == "g")
        OOpt = "1";
      else if (OOpt == "s" || OOpt == "z")
        OOpt = "2";
    } else if (A->getOption().matches(options::OPT_O0))
      OOpt = "0";
    if (!OOpt.empty()) {
      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + ExtraDash + "O" + OOpt));
      if (IsAMDGCN)
        CmdArgs.push_back(Args.MakeArgString(Twine("--lto-CGO") + OOpt));
    }
  }

  if (Args.hasArg(options::OPT_gsplit_dwarf))
    CmdArgs.push_back(Args.MakeArgString(
        Twine(PluginOptPrefix) + "dwo_dir=" + Output.getFilename() + "_dwo"));

  if (IsThinLTO && !IsOSAIX)
    CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) + "thinlto"));
  else if (IsThinLTO && IsOSAIX)
    CmdArgs.push_back(Args.MakeArgString(Twine("-bdbg:thinlto")));

  // Matrix intrinsic lowering happens at link time with ThinLTO. Enable
  // LowerMatrixIntrinsicsPass, which is transitively called by
  // buildThinLTODefaultPipeline under EnableMatrix.
  if ((IsThinLTO || IsFatLTO || IsUnifiedLTO) &&
        Args.hasArg(options::OPT_fenable_matrix))
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "-enable-matrix"));

  StringRef Parallelism = getLTOParallelism(Args, D);
  if (!Parallelism.empty())
    CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                         ParallelismOpt + Parallelism));

  // Pass down GlobalISel options.
  if (Arg *A = Args.getLastArg(options::OPT_fglobal_isel,
                               options::OPT_fno_global_isel)) {
    // Parsing -fno-global-isel explicitly gives architectures that enable GISel
    // by default a chance to disable it.
    CmdArgs.push_back(Args.MakeArgString(
        Twine(PluginOptPrefix) + "-global-isel=" +
        (A->getOption().matches(options::OPT_fglobal_isel) ? "1" : "0")));
  }

  // If an explicit debugger tuning argument appeared, pass it along.
  if (Arg *A =
          Args.getLastArg(options::OPT_gTune_Group, options::OPT_ggdbN_Group)) {
    if (A->getOption().matches(options::OPT_glldb))
      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + "-debugger-tune=lldb"));
    else if (A->getOption().matches(options::OPT_gsce))
      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + "-debugger-tune=sce"));
    else if (A->getOption().matches(options::OPT_gdbx))
      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + "-debugger-tune=dbx"));
    else
      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + "-debugger-tune=gdb"));
  }

  if (IsOSAIX) {
    if (!ToolChain.useIntegratedAs())
      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + "-no-integrated-as=1"));

    // On AIX, clang assumes strict-dwarf is true if any debug option is
    // specified, unless it is told explicitly not to assume so.
    Arg *A = Args.getLastArg(options::OPT_g_Group);
    bool EnableDebugInfo = A && !A->getOption().matches(options::OPT_g0) &&
                           !A->getOption().matches(options::OPT_ggdb0);
    if (EnableDebugInfo && Args.hasFlag(options::OPT_gstrict_dwarf,
                                        options::OPT_gno_strict_dwarf, true))
      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + "-strict-dwarf=true"));

    for (const Arg *A : Args.filtered_reverse(options::OPT_mabi_EQ)) {
      StringRef V = A->getValue();
      if (V == "vec-default")
        break;
      if (V == "vec-extabi") {
        CmdArgs.push_back(
            Args.MakeArgString(Twine(PluginOptPrefix) + "-vec-extabi"));
        break;
      }
    }
  }

  bool UseSeparateSections =
      isUseSeparateSections(ToolChain.getEffectiveTriple());

  if (Args.hasFlag(options::OPT_ffunction_sections,
                   options::OPT_fno_function_sections, UseSeparateSections))
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "-function-sections=1"));
  else if (Args.hasArg(options::OPT_fno_function_sections))
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "-function-sections=0"));

  bool DataSectionsTurnedOff = false;
  if (Args.hasFlag(options::OPT_fdata_sections, options::OPT_fno_data_sections,
                   UseSeparateSections)) {
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "-data-sections=1"));
  } else if (Args.hasArg(options::OPT_fno_data_sections)) {
    DataSectionsTurnedOff = true;
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "-data-sections=0"));
  }

  if (Args.hasArg(options::OPT_mxcoff_roptr) ||
      Args.hasArg(options::OPT_mno_xcoff_roptr)) {
    bool HasRoptr = Args.hasFlag(options::OPT_mxcoff_roptr,
                                 options::OPT_mno_xcoff_roptr, false);
    StringRef OptStr = HasRoptr ? "-mxcoff-roptr" : "-mno-xcoff-roptr";
    if (!IsOSAIX)
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << OptStr << Triple.str();

    if (HasRoptr) {
      // The data sections option is on by default on AIX. We only need to error
      // out when -fno-data-sections is specified explicitly to turn off data
      // sections.
      if (DataSectionsTurnedOff)
        D.Diag(diag::err_roptr_requires_data_sections);

      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + "-mxcoff-roptr"));
    }
  }

  // Pass an option to enable split machine functions.
  if (auto *A = Args.getLastArg(options::OPT_fsplit_machine_functions,
                                options::OPT_fno_split_machine_functions)) {
    if (A->getOption().matches(options::OPT_fsplit_machine_functions))
      CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                           "-split-machine-functions"));
  }

  if (Arg *A = getLastProfileSampleUseArg(Args)) {
    StringRef FName = A->getValue();
    if (!llvm::sys::fs::exists(FName))
      D.Diag(diag::err_drv_no_such_file) << FName;
    else
      CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                           "sample-profile=" + FName));
  }

  if (auto *CSPGOGenerateArg = getLastCSProfileGenerateArg(Args)) {
    CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) + ExtraDash +
                                         "cs-profile-generate"));
    if (CSPGOGenerateArg->getOption().matches(
            options::OPT_fcs_profile_generate_EQ)) {
      SmallString<128> Path(CSPGOGenerateArg->getValue());
      llvm::sys::path::append(Path, "default_%m.profraw");
      CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) + ExtraDash +
                                           "cs-profile-path=" + Path));
    } else
      CmdArgs.push_back(
          Args.MakeArgString(Twine(PluginOptPrefix) + ExtraDash +
                             "cs-profile-path=default_%m.profraw"));
  } else if (auto *ProfileUseArg = getLastProfileUseArg(Args)) {
    SmallString<128> Path(
        ProfileUseArg->getNumValues() == 0 ? "" : ProfileUseArg->getValue());
    if (Path.empty() || llvm::sys::fs::is_directory(Path))
      llvm::sys::path::append(Path, "default.profdata");
    CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) + ExtraDash +
                                         "cs-profile-path=" + Path));
  }

  // This controls whether or not we perform JustMyCode instrumentation.
  if (Args.hasFlag(options::OPT_fjmc, options::OPT_fno_jmc, false)) {
    if (ToolChain.getEffectiveTriple().isOSBinFormatELF())
      CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) +
                                           "-enable-jmc-instrument"));
    else
      D.Diag(clang::diag::warn_drv_fjmc_for_elf_only);
  }

  if (Args.hasFlag(options::OPT_femulated_tls, options::OPT_fno_emulated_tls,
                   Triple.hasDefaultEmulatedTLS())) {
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "-emulated-tls"));
  }
  if (isTLSDESCEnabled(ToolChain, Args))
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "-enable-tlsdesc"));

  if (Args.hasFlag(options::OPT_fstack_size_section,
                   options::OPT_fno_stack_size_section, false))
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "-stack-size-section"));

  // Setup statistics file output.
  SmallString<128> StatsFile = getStatsFileName(Args, Output, Input, D);
  if (!StatsFile.empty())
    CmdArgs.push_back(
        Args.MakeArgString(Twine(PluginOptPrefix) + "stats-file=" + StatsFile));

  // Setup crash diagnostics dir.
  if (Arg *A = Args.getLastArg(options::OPT_fcrash_diagnostics_dir))
    CmdArgs.push_back(Args.MakeArgString(
        Twine(PluginOptPrefix) + "-crash-diagnostics-dir=" + A->getValue()));

  addX86AlignBranchArgs(D, Args, CmdArgs, /*IsLTO=*/true, PluginOptPrefix);

  // Handle remark diagnostics on screen options: '-Rpass-*'.
  renderRpassOptions(Args, CmdArgs, PluginOptPrefix);

  // Handle serialized remarks options: '-fsave-optimization-record'
  // and '-foptimization-record-*'.
  if (willEmitRemarks(Args))
    renderRemarksOptions(Args, CmdArgs, ToolChain.getEffectiveTriple(), Input,
                         Output, PluginOptPrefix);

  // Handle remarks hotness/threshold related options.
  renderRemarksHotnessOptions(Args, CmdArgs, PluginOptPrefix);

  addMachineOutlinerArgs(D, Args, CmdArgs, ToolChain.getEffectiveTriple(),
                         /*IsLTO=*/true, PluginOptPrefix);

  for (const Arg *A : Args.filtered(options::OPT_Wa_COMMA)) {
    bool Crel = false;
    for (StringRef V : A->getValues()) {
      if (V == "--crel")
        Crel = true;
      else if (V == "--no-crel")
        Crel = false;
      else
        continue;
      A->claim();
    }
    if (Crel) {
      if (Triple.isOSBinFormatELF() && !Triple.isMIPS()) {
        CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) + "-crel"));
      } else {
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << "-Wa,--crel" << D.getTargetTriple();
      }
    }
  }
}

/// Adds the '-lcgpu' and '-lmgpu' libraries to the compilation to include the
/// LLVM C library for GPUs.
static void addOpenMPDeviceLibC(const Compilation &C, const ArgList &Args,
                                ArgStringList &CmdArgs) {
  if (Args.hasArg(options::OPT_nogpulib) || Args.hasArg(options::OPT_nolibc))
    return;

  // Check the resource directory for the LLVM libc GPU declarations. If it's
  // found we can assume that LLVM was built with support for the GPU libc.
  SmallString<256> LibCDecls(C.getDriver().ResourceDir);
  llvm::sys::path::append(LibCDecls, "include", "llvm_libc_wrappers",
                          "llvm-libc-decls");
  bool HasLibC = llvm::sys::fs::exists(LibCDecls) &&
                 llvm::sys::fs::is_directory(LibCDecls);
  if (!Args.hasFlag(options::OPT_gpulibc, options::OPT_nogpulibc, HasLibC))
    return;

  SmallVector<const ToolChain *> ToolChains;
  auto TCRange = C.getOffloadToolChains(Action::OFK_OpenMP);
  for (auto TI = TCRange.first, TE = TCRange.second; TI != TE; ++TI)
    ToolChains.push_back(TI->second);

  if (llvm::any_of(ToolChains, [](const ToolChain *TC) {
        return TC->getTriple().isAMDGPU();
      })) {
    CmdArgs.push_back("-lcgpu-amdgpu");
    CmdArgs.push_back("-lmgpu-amdgpu");
  }
  if (llvm::any_of(ToolChains, [](const ToolChain *TC) {
        return TC->getTriple().isNVPTX();
      })) {
    CmdArgs.push_back("-lcgpu-nvptx");
    CmdArgs.push_back("-lmgpu-nvptx");
  }
}

void tools::addOpenMPRuntimeLibraryPath(const ToolChain &TC,
                                        const ArgList &Args,
                                        ArgStringList &CmdArgs) {
  // Default to clang lib / lib64 folder, i.e. the same location as device
  // runtime.
  SmallString<256> DefaultLibPath =
      llvm::sys::path::parent_path(TC.getDriver().Dir);
  llvm::sys::path::append(DefaultLibPath, CLANG_INSTALL_LIBDIR_BASENAME);
  CmdArgs.push_back(Args.MakeArgString("-L" + DefaultLibPath));
}

void tools::addArchSpecificRPath(const ToolChain &TC, const ArgList &Args,
                                 ArgStringList &CmdArgs) {
  if (!Args.hasFlag(options::OPT_frtlib_add_rpath,
                    options::OPT_fno_rtlib_add_rpath, false))
    return;

  SmallVector<std::string> CandidateRPaths(TC.getArchSpecificLibPaths());
  if (const auto CandidateRPath = TC.getStdlibPath())
    CandidateRPaths.emplace_back(*CandidateRPath);

  for (const auto &CandidateRPath : CandidateRPaths) {
    if (TC.getVFS().exists(CandidateRPath)) {
      CmdArgs.push_back("-rpath");
      CmdArgs.push_back(Args.MakeArgString(CandidateRPath));
    }
  }
}

bool tools::addOpenMPRuntime(const Compilation &C, ArgStringList &CmdArgs,
                             const ToolChain &TC, const ArgList &Args,
                             bool ForceStaticHostRuntime, bool IsOffloadingHost,
                             bool GompNeedsRT) {
  if (!Args.hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                    options::OPT_fno_openmp, false))
    return false;

  Driver::OpenMPRuntimeKind RTKind = TC.getDriver().getOpenMPRuntime(Args);

  if (RTKind == Driver::OMPRT_Unknown)
    // Already diagnosed.
    return false;

  if (ForceStaticHostRuntime)
    CmdArgs.push_back("-Bstatic");

  switch (RTKind) {
  case Driver::OMPRT_OMP:
    CmdArgs.push_back("-lomp");
    break;
  case Driver::OMPRT_GOMP:
    CmdArgs.push_back("-lgomp");
    break;
  case Driver::OMPRT_IOMP5:
    CmdArgs.push_back("-liomp5");
    break;
  case Driver::OMPRT_Unknown:
    break;
  }

  if (ForceStaticHostRuntime)
    CmdArgs.push_back("-Bdynamic");

  if (RTKind == Driver::OMPRT_GOMP && GompNeedsRT)
      CmdArgs.push_back("-lrt");

  if (IsOffloadingHost)
    CmdArgs.push_back("-lomptarget");

  if (IsOffloadingHost && !Args.hasArg(options::OPT_nogpulib))
    CmdArgs.push_back("-lomptarget.devicertl");

  if (IsOffloadingHost)
    addOpenMPDeviceLibC(C, Args, CmdArgs);

  addArchSpecificRPath(TC, Args, CmdArgs);
  addOpenMPRuntimeLibraryPath(TC, Args, CmdArgs);

  return true;
}

/// Add Fortran runtime libs
void tools::addFortranRuntimeLibs(const ToolChain &TC, const ArgList &Args,
                                  llvm::opt::ArgStringList &CmdArgs) {
  // Link FortranRuntime and FortranDecimal
  // These are handled earlier on Windows by telling the frontend driver to
  // add the correct libraries to link against as dependents in the object
  // file.
  if (!TC.getTriple().isKnownWindowsMSVCEnvironment()) {
    StringRef F128LibName = TC.getDriver().getFlangF128MathLibrary();
    F128LibName.consume_front_insensitive("lib");
    if (!F128LibName.empty()) {
      bool AsNeeded = !TC.getTriple().isOSAIX();
      CmdArgs.push_back("-lFortranFloat128Math");
      if (AsNeeded)
        addAsNeededOption(TC, Args, CmdArgs, /*as_needed=*/true);
      CmdArgs.push_back(Args.MakeArgString("-l" + F128LibName));
      if (AsNeeded)
        addAsNeededOption(TC, Args, CmdArgs, /*as_needed=*/false);
    }
    CmdArgs.push_back("-lFortranRuntime");
    CmdArgs.push_back("-lFortranDecimal");
  }
}

void tools::addFortranRuntimeLibraryPath(const ToolChain &TC,
                                         const llvm::opt::ArgList &Args,
                                         ArgStringList &CmdArgs) {
  // Default to the <driver-path>/../lib directory. This works fine on the
  // platforms that we have tested so far. We will probably have to re-fine
  // this in the future. In particular, on some platforms, we may need to use
  // lib64 instead of lib.
  SmallString<256> DefaultLibPath =
      llvm::sys::path::parent_path(TC.getDriver().Dir);
  llvm::sys::path::append(DefaultLibPath, "lib");
  if (TC.getTriple().isKnownWindowsMSVCEnvironment())
    CmdArgs.push_back(Args.MakeArgString("-libpath:" + DefaultLibPath));
  else
    CmdArgs.push_back(Args.MakeArgString("-L" + DefaultLibPath));
}

static void addSanitizerRuntime(const ToolChain &TC, const ArgList &Args,
                                ArgStringList &CmdArgs, StringRef Sanitizer,
                                bool IsShared, bool IsWhole) {
  // Wrap any static runtimes that must be forced into executable in
  // whole-archive.
  if (IsWhole) CmdArgs.push_back("--whole-archive");
  CmdArgs.push_back(TC.getCompilerRTArgString(
      Args, Sanitizer, IsShared ? ToolChain::FT_Shared : ToolChain::FT_Static));
  if (IsWhole) CmdArgs.push_back("--no-whole-archive");

  if (IsShared) {
    addArchSpecificRPath(TC, Args, CmdArgs);
  }
}

// Tries to use a file with the list of dynamic symbols that need to be exported
// from the runtime library. Returns true if the file was found.
static bool addSanitizerDynamicList(const ToolChain &TC, const ArgList &Args,
                                    ArgStringList &CmdArgs,
                                    StringRef Sanitizer) {
  bool LinkerIsGnuLd = solaris::isLinkerGnuLd(TC, Args);

  // Solaris ld defaults to --export-dynamic behaviour but doesn't support
  // the option, so don't try to pass it.
  if (TC.getTriple().isOSSolaris() && !LinkerIsGnuLd)
    return true;
  SmallString<128> SanRT(TC.getCompilerRT(Args, Sanitizer));
  if (llvm::sys::fs::exists(SanRT + ".syms")) {
    CmdArgs.push_back(Args.MakeArgString("--dynamic-list=" + SanRT + ".syms"));
    return true;
  }
  return false;
}

void tools::addAsNeededOption(const ToolChain &TC,
                              const llvm::opt::ArgList &Args,
                              llvm::opt::ArgStringList &CmdArgs,
                              bool as_needed) {
  assert(!TC.getTriple().isOSAIX() &&
         "AIX linker does not support any form of --as-needed option yet.");
  bool LinkerIsGnuLd = solaris::isLinkerGnuLd(TC, Args);

  // While the Solaris 11.2 ld added --as-needed/--no-as-needed as aliases
  // for the native forms -z ignore/-z record, they are missing in Illumos,
  // so always use the native form.
  // GNU ld doesn't support -z ignore/-z record, so don't use them even on
  // Solaris.
  if (TC.getTriple().isOSSolaris() && !LinkerIsGnuLd) {
    CmdArgs.push_back("-z");
    CmdArgs.push_back(as_needed ? "ignore" : "record");
  } else {
    CmdArgs.push_back(as_needed ? "--as-needed" : "--no-as-needed");
  }
}

void tools::linkSanitizerRuntimeDeps(const ToolChain &TC,
                                     const llvm::opt::ArgList &Args,
                                     ArgStringList &CmdArgs) {
  // Force linking against the system libraries sanitizers depends on
  // (see PR15823 why this is necessary).
  addAsNeededOption(TC, Args, CmdArgs, false);
  // There's no libpthread or librt on RTEMS & Android.
  if (TC.getTriple().getOS() != llvm::Triple::RTEMS &&
      !TC.getTriple().isAndroid() && !TC.getTriple().isOHOSFamily()) {
    CmdArgs.push_back("-lpthread");
    if (!TC.getTriple().isOSOpenBSD())
      CmdArgs.push_back("-lrt");
  }
  CmdArgs.push_back("-lm");
  // There's no libdl on all OSes.
  if (!TC.getTriple().isOSFreeBSD() && !TC.getTriple().isOSNetBSD() &&
      !TC.getTriple().isOSOpenBSD() &&
      TC.getTriple().getOS() != llvm::Triple::RTEMS)
    CmdArgs.push_back("-ldl");
  // Required for backtrace on some OSes
  if (TC.getTriple().isOSFreeBSD() ||
      TC.getTriple().isOSNetBSD() ||
      TC.getTriple().isOSOpenBSD())
    CmdArgs.push_back("-lexecinfo");
  // There is no libresolv on Android, FreeBSD, OpenBSD, etc. On musl
  // libresolv.a, even if exists, is an empty archive to satisfy POSIX -lresolv
  // requirement.
  if (TC.getTriple().isOSLinux() && !TC.getTriple().isAndroid() &&
      !TC.getTriple().isMusl())
    CmdArgs.push_back("-lresolv");
}

static void
collectSanitizerRuntimes(const ToolChain &TC, const ArgList &Args,
                         SmallVectorImpl<StringRef> &SharedRuntimes,
                         SmallVectorImpl<StringRef> &StaticRuntimes,
                         SmallVectorImpl<StringRef> &NonWholeStaticRuntimes,
                         SmallVectorImpl<StringRef> &HelperStaticRuntimes,
                         SmallVectorImpl<StringRef> &RequiredSymbols) {
  const SanitizerArgs &SanArgs = TC.getSanitizerArgs(Args);
  // Collect shared runtimes.
  if (SanArgs.needsSharedRt()) {
    if (SanArgs.needsAsanRt()) {
      SharedRuntimes.push_back("asan");
      if (!Args.hasArg(options::OPT_shared) && !TC.getTriple().isAndroid())
        HelperStaticRuntimes.push_back("asan-preinit");
    }
    if (SanArgs.needsMemProfRt()) {
      SharedRuntimes.push_back("memprof");
      if (!Args.hasArg(options::OPT_shared) && !TC.getTriple().isAndroid())
        HelperStaticRuntimes.push_back("memprof-preinit");
    }
    if (SanArgs.needsNsanRt())
      SharedRuntimes.push_back("nsan");
    if (SanArgs.needsUbsanRt()) {
      if (SanArgs.requiresMinimalRuntime())
        SharedRuntimes.push_back("ubsan_minimal");
      else
        SharedRuntimes.push_back("ubsan_standalone");
    }
    if (SanArgs.needsScudoRt()) {
      SharedRuntimes.push_back("scudo_standalone");
    }
    if (SanArgs.needsTsanRt())
      SharedRuntimes.push_back("tsan");
    if (SanArgs.needsHwasanRt()) {
      if (SanArgs.needsHwasanAliasesRt())
        SharedRuntimes.push_back("hwasan_aliases");
      else
        SharedRuntimes.push_back("hwasan");
      if (!Args.hasArg(options::OPT_shared))
        HelperStaticRuntimes.push_back("hwasan-preinit");
    }
  }

  // The stats_client library is also statically linked into DSOs.
  if (SanArgs.needsStatsRt())
    StaticRuntimes.push_back("stats_client");

  // Always link the static runtime regardless of DSO or executable.
  if (SanArgs.needsAsanRt())
    HelperStaticRuntimes.push_back("asan_static");

  // Collect static runtimes.
  if (Args.hasArg(options::OPT_shared)) {
    // Don't link static runtimes into DSOs.
    return;
  }

  // Each static runtime that has a DSO counterpart above is excluded below,
  // but runtimes that exist only as static are not affected by needsSharedRt.

  if (!SanArgs.needsSharedRt() && SanArgs.needsAsanRt()) {
    StaticRuntimes.push_back("asan");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("asan_cxx");
  }

  if (!SanArgs.needsSharedRt() && SanArgs.needsMemProfRt()) {
    StaticRuntimes.push_back("memprof");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("memprof_cxx");
  }

  if (!SanArgs.needsSharedRt() && SanArgs.needsHwasanRt()) {
    if (SanArgs.needsHwasanAliasesRt()) {
      StaticRuntimes.push_back("hwasan_aliases");
      if (SanArgs.linkCXXRuntimes())
        StaticRuntimes.push_back("hwasan_aliases_cxx");
    } else {
      StaticRuntimes.push_back("hwasan");
      if (SanArgs.linkCXXRuntimes())
        StaticRuntimes.push_back("hwasan_cxx");
    }
  }
  if (SanArgs.needsDfsanRt())
    StaticRuntimes.push_back("dfsan");
  if (SanArgs.needsLsanRt())
    StaticRuntimes.push_back("lsan");
  if (SanArgs.needsMsanRt()) {
    StaticRuntimes.push_back("msan");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("msan_cxx");
  }
  if (!SanArgs.needsSharedRt() && SanArgs.needsNsanRt())
    StaticRuntimes.push_back("nsan");
  if (!SanArgs.needsSharedRt() && SanArgs.needsTsanRt()) {
    StaticRuntimes.push_back("tsan");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("tsan_cxx");
  }
  if (!SanArgs.needsSharedRt() && SanArgs.needsUbsanRt()) {
    if (SanArgs.requiresMinimalRuntime()) {
      StaticRuntimes.push_back("ubsan_minimal");
    } else {
      StaticRuntimes.push_back("ubsan_standalone");
      if (SanArgs.linkCXXRuntimes())
        StaticRuntimes.push_back("ubsan_standalone_cxx");
    }
  }
  if (SanArgs.needsSafeStackRt()) {
    NonWholeStaticRuntimes.push_back("safestack");
    RequiredSymbols.push_back("__safestack_init");
  }
  if (!(SanArgs.needsSharedRt() && SanArgs.needsUbsanRt())) {
    if (SanArgs.needsCfiRt())
      StaticRuntimes.push_back("cfi");
    if (SanArgs.needsCfiDiagRt()) {
      StaticRuntimes.push_back("cfi_diag");
      if (SanArgs.linkCXXRuntimes())
        StaticRuntimes.push_back("ubsan_standalone_cxx");
    }
  }
  if (SanArgs.needsStatsRt()) {
    NonWholeStaticRuntimes.push_back("stats");
    RequiredSymbols.push_back("__sanitizer_stats_register");
  }
  if (!SanArgs.needsSharedRt() && SanArgs.needsScudoRt()) {
    StaticRuntimes.push_back("scudo_standalone");
    if (SanArgs.linkCXXRuntimes())
      StaticRuntimes.push_back("scudo_standalone_cxx");
  }
}

// Should be called before we add system libraries (C++ ABI, libstdc++/libc++,
// C runtime, etc). Returns true if sanitizer system deps need to be linked in.
bool tools::addSanitizerRuntimes(const ToolChain &TC, const ArgList &Args,
                                 ArgStringList &CmdArgs) {
  const SanitizerArgs &SanArgs = TC.getSanitizerArgs(Args);
  SmallVector<StringRef, 4> SharedRuntimes, StaticRuntimes,
      NonWholeStaticRuntimes, HelperStaticRuntimes, RequiredSymbols;
  if (SanArgs.linkRuntimes()) {
    collectSanitizerRuntimes(TC, Args, SharedRuntimes, StaticRuntimes,
                             NonWholeStaticRuntimes, HelperStaticRuntimes,
                             RequiredSymbols);
  }

  // -u options must be added before the runtime libs that resolve them.
  for (auto S : RequiredSymbols) {
    CmdArgs.push_back("-u");
    CmdArgs.push_back(Args.MakeArgString(S));
  }

  // Inject libfuzzer dependencies.
  if (SanArgs.needsFuzzer() && SanArgs.linkRuntimes() &&
      !Args.hasArg(options::OPT_shared)) {

    addSanitizerRuntime(TC, Args, CmdArgs, "fuzzer", false, true);
    if (SanArgs.needsFuzzerInterceptors())
      addSanitizerRuntime(TC, Args, CmdArgs, "fuzzer_interceptors", false,
                          true);
    if (!Args.hasArg(clang::driver::options::OPT_nostdlibxx)) {
      bool OnlyLibstdcxxStatic = Args.hasArg(options::OPT_static_libstdcxx) &&
                                 !Args.hasArg(options::OPT_static);
      if (OnlyLibstdcxxStatic)
        CmdArgs.push_back("-Bstatic");
      TC.AddCXXStdlibLibArgs(Args, CmdArgs);
      if (OnlyLibstdcxxStatic)
        CmdArgs.push_back("-Bdynamic");
    }
  }

  for (auto RT : SharedRuntimes)
    addSanitizerRuntime(TC, Args, CmdArgs, RT, true, false);
  for (auto RT : HelperStaticRuntimes)
    addSanitizerRuntime(TC, Args, CmdArgs, RT, false, true);
  bool AddExportDynamic = false;
  for (auto RT : StaticRuntimes) {
    addSanitizerRuntime(TC, Args, CmdArgs, RT, false, true);
    AddExportDynamic |= !addSanitizerDynamicList(TC, Args, CmdArgs, RT);
  }
  for (auto RT : NonWholeStaticRuntimes) {
    addSanitizerRuntime(TC, Args, CmdArgs, RT, false, false);
    AddExportDynamic |= !addSanitizerDynamicList(TC, Args, CmdArgs, RT);
  }
  // If there is a static runtime with no dynamic list, force all the symbols
  // to be dynamic to be sure we export sanitizer interface functions.
  if (AddExportDynamic)
    CmdArgs.push_back("--export-dynamic");

  if (SanArgs.hasCrossDsoCfi() && !AddExportDynamic)
    CmdArgs.push_back("--export-dynamic-symbol=__cfi_check");

  if (SanArgs.hasMemTag()) {
    if (!TC.getTriple().isAndroid()) {
      TC.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
          << "-fsanitize=memtag*" << TC.getTriple().str();
    }
    CmdArgs.push_back(
        Args.MakeArgString("--android-memtag-mode=" + SanArgs.getMemtagMode()));
    if (SanArgs.hasMemtagHeap())
      CmdArgs.push_back("--android-memtag-heap");
    if (SanArgs.hasMemtagStack())
      CmdArgs.push_back("--android-memtag-stack");
  }

  return !StaticRuntimes.empty() || !NonWholeStaticRuntimes.empty();
}

bool tools::addXRayRuntime(const ToolChain&TC, const ArgList &Args, ArgStringList &CmdArgs) {
  if (Args.hasArg(options::OPT_shared))
    return false;

  if (TC.getXRayArgs().needsXRayRt()) {
    CmdArgs.push_back("--whole-archive");
    CmdArgs.push_back(TC.getCompilerRTArgString(Args, "xray"));
    for (const auto &Mode : TC.getXRayArgs().modeList())
      CmdArgs.push_back(TC.getCompilerRTArgString(Args, Mode));
    CmdArgs.push_back("--no-whole-archive");
    return true;
  }

  return false;
}

void tools::linkXRayRuntimeDeps(const ToolChain &TC,
                                const llvm::opt::ArgList &Args,
                                ArgStringList &CmdArgs) {
  addAsNeededOption(TC, Args, CmdArgs, false);
  CmdArgs.push_back("-lpthread");
  if (!TC.getTriple().isOSOpenBSD())
    CmdArgs.push_back("-lrt");
  CmdArgs.push_back("-lm");

  if (!TC.getTriple().isOSFreeBSD() &&
      !TC.getTriple().isOSNetBSD() &&
      !TC.getTriple().isOSOpenBSD())
    CmdArgs.push_back("-ldl");
}

bool tools::areOptimizationsEnabled(const ArgList &Args) {
  // Find the last -O arg and see if it is non-zero.
  if (Arg *A = Args.getLastArg(options::OPT_O_Group))
    return !A->getOption().matches(options::OPT_O0);
  // Defaults to -O0.
  return false;
}

const char *tools::SplitDebugName(const JobAction &JA, const ArgList &Args,
                                  const InputInfo &Input,
                                  const InputInfo &Output) {
  auto AddPostfix = [JA](auto &F) {
    if (JA.getOffloadingDeviceKind() == Action::OFK_HIP)
      F += (Twine("_") + JA.getOffloadingArch()).str();
    F += ".dwo";
  };
  if (Arg *A = Args.getLastArg(options::OPT_gsplit_dwarf_EQ))
    if (StringRef(A->getValue()) == "single" && Output.isFilename())
      return Args.MakeArgString(Output.getFilename());

  SmallString<128> T;
  if (const Arg *A = Args.getLastArg(options::OPT_dumpdir)) {
    T = A->getValue();
  } else {
    Arg *FinalOutput = Args.getLastArg(options::OPT_o, options::OPT__SLASH_o);
    if (FinalOutput && Args.hasArg(options::OPT_c)) {
      T = FinalOutput->getValue();
      llvm::sys::path::remove_filename(T);
      llvm::sys::path::append(T,
                              llvm::sys::path::stem(FinalOutput->getValue()));
      AddPostfix(T);
      return Args.MakeArgString(T);
    }
  }

  T += llvm::sys::path::stem(Input.getBaseInput());
  AddPostfix(T);
  return Args.MakeArgString(T);
}

void tools::SplitDebugInfo(const ToolChain &TC, Compilation &C, const Tool &T,
                           const JobAction &JA, const ArgList &Args,
                           const InputInfo &Output, const char *OutFile) {
  ArgStringList ExtractArgs;
  ExtractArgs.push_back("--extract-dwo");

  ArgStringList StripArgs;
  StripArgs.push_back("--strip-dwo");

  // Grabbing the output of the earlier compile step.
  StripArgs.push_back(Output.getFilename());
  ExtractArgs.push_back(Output.getFilename());
  ExtractArgs.push_back(OutFile);

  const char *Exec =
      Args.MakeArgString(TC.GetProgramPath(CLANG_DEFAULT_OBJCOPY));
  InputInfo II(types::TY_Object, Output.getFilename(), Output.getFilename());

  // First extract the dwo sections.
  C.addCommand(std::make_unique<Command>(JA, T,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, ExtractArgs, II, Output));

  // Then remove them from the original .o file.
  C.addCommand(std::make_unique<Command>(
      JA, T, ResponseFileSupport::AtFileCurCP(), Exec, StripArgs, II, Output));
}

// Claim options we don't want to warn if they are unused. We do this for
// options that build systems might add but are unused when assembling or only
// running the preprocessor for example.
void tools::claimNoWarnArgs(const ArgList &Args) {
  // Don't warn about unused -f(no-)?lto.  This can happen when we're
  // preprocessing, precompiling or assembling.
  Args.ClaimAllArgs(options::OPT_flto_EQ);
  Args.ClaimAllArgs(options::OPT_flto);
  Args.ClaimAllArgs(options::OPT_fno_lto);
}

Arg *tools::getLastCSProfileGenerateArg(const ArgList &Args) {
  auto *CSPGOGenerateArg = Args.getLastArg(options::OPT_fcs_profile_generate,
                                           options::OPT_fcs_profile_generate_EQ,
                                           options::OPT_fno_profile_generate);
  if (CSPGOGenerateArg &&
      CSPGOGenerateArg->getOption().matches(options::OPT_fno_profile_generate))
    CSPGOGenerateArg = nullptr;

  return CSPGOGenerateArg;
}

Arg *tools::getLastProfileUseArg(const ArgList &Args) {
  auto *ProfileUseArg = Args.getLastArg(
      options::OPT_fprofile_instr_use, options::OPT_fprofile_instr_use_EQ,
      options::OPT_fprofile_use, options::OPT_fprofile_use_EQ,
      options::OPT_fno_profile_instr_use);

  if (ProfileUseArg &&
      ProfileUseArg->getOption().matches(options::OPT_fno_profile_instr_use))
    ProfileUseArg = nullptr;

  return ProfileUseArg;
}

Arg *tools::getLastProfileSampleUseArg(const ArgList &Args) {
  auto *ProfileSampleUseArg = Args.getLastArg(
      options::OPT_fprofile_sample_use, options::OPT_fprofile_sample_use_EQ,
      options::OPT_fauto_profile, options::OPT_fauto_profile_EQ,
      options::OPT_fno_profile_sample_use, options::OPT_fno_auto_profile);

  if (ProfileSampleUseArg &&
      (ProfileSampleUseArg->getOption().matches(
           options::OPT_fno_profile_sample_use) ||
       ProfileSampleUseArg->getOption().matches(options::OPT_fno_auto_profile)))
    return nullptr;

  return Args.getLastArg(options::OPT_fprofile_sample_use_EQ,
                         options::OPT_fauto_profile_EQ);
}

const char *tools::RelocationModelName(llvm::Reloc::Model Model) {
  switch (Model) {
  case llvm::Reloc::Static:
    return "static";
  case llvm::Reloc::PIC_:
    return "pic";
  case llvm::Reloc::DynamicNoPIC:
    return "dynamic-no-pic";
  case llvm::Reloc::ROPI:
    return "ropi";
  case llvm::Reloc::RWPI:
    return "rwpi";
  case llvm::Reloc::ROPI_RWPI:
    return "ropi-rwpi";
  }
  llvm_unreachable("Unknown Reloc::Model kind");
}

/// Parses the various -fpic/-fPIC/-fpie/-fPIE arguments.  Then,
/// smooshes them together with platform defaults, to decide whether
/// this compile should be using PIC mode or not. Returns a tuple of
/// (RelocationModel, PICLevel, IsPIE).
std::tuple<llvm::Reloc::Model, unsigned, bool>
tools::ParsePICArgs(const ToolChain &ToolChain, const ArgList &Args) {
  const llvm::Triple &EffectiveTriple = ToolChain.getEffectiveTriple();
  const llvm::Triple &Triple = ToolChain.getTriple();

  bool PIE = ToolChain.isPIEDefault(Args);
  bool PIC = PIE || ToolChain.isPICDefault();
  // The Darwin/MachO default to use PIC does not apply when using -static.
  if (Triple.isOSBinFormatMachO() && Args.hasArg(options::OPT_static))
    PIE = PIC = false;
  bool IsPICLevelTwo = PIC;

  bool KernelOrKext =
      Args.hasArg(options::OPT_mkernel, options::OPT_fapple_kext);

  // Android-specific defaults for PIC/PIE
  if (Triple.isAndroid()) {
    switch (Triple.getArch()) {
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
    case llvm::Triple::aarch64:
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
      PIC = true; // "-fpic"
      break;

    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      PIC = true; // "-fPIC"
      IsPICLevelTwo = true;
      break;

    default:
      break;
    }
  }

  // OHOS-specific defaults for PIC/PIE
  if (Triple.isOHOSFamily() && Triple.getArch() == llvm::Triple::aarch64)
    PIC = true;

  // OpenBSD-specific defaults for PIE
  if (Triple.isOSOpenBSD()) {
    switch (ToolChain.getArch()) {
    case llvm::Triple::arm:
    case llvm::Triple::aarch64:
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      IsPICLevelTwo = false; // "-fpie"
      break;

    case llvm::Triple::ppc:
    case llvm::Triple::sparcv9:
      IsPICLevelTwo = true; // "-fPIE"
      break;

    default:
      break;
    }
  }

  // The last argument relating to either PIC or PIE wins, and no
  // other argument is used. If the last argument is any flavor of the
  // '-fno-...' arguments, both PIC and PIE are disabled. Any PIE
  // option implicitly enables PIC at the same level.
  Arg *LastPICArg = Args.getLastArg(options::OPT_fPIC, options::OPT_fno_PIC,
                                    options::OPT_fpic, options::OPT_fno_pic,
                                    options::OPT_fPIE, options::OPT_fno_PIE,
                                    options::OPT_fpie, options::OPT_fno_pie);
  if (Triple.isOSWindows() && !Triple.isOSCygMing() && LastPICArg &&
      LastPICArg == Args.getLastArg(options::OPT_fPIC, options::OPT_fpic,
                                    options::OPT_fPIE, options::OPT_fpie)) {
    ToolChain.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
        << LastPICArg->getSpelling() << Triple.str();
    if (Triple.getArch() == llvm::Triple::x86_64)
      return std::make_tuple(llvm::Reloc::PIC_, 2U, false);
    return std::make_tuple(llvm::Reloc::Static, 0U, false);
  }

  // Check whether the tool chain trumps the PIC-ness decision. If the PIC-ness
  // is forced, then neither PIC nor PIE flags will have no effect.
  if (!ToolChain.isPICDefaultForced()) {
    if (LastPICArg) {
      Option O = LastPICArg->getOption();
      if (O.matches(options::OPT_fPIC) || O.matches(options::OPT_fpic) ||
          O.matches(options::OPT_fPIE) || O.matches(options::OPT_fpie)) {
        PIE = O.matches(options::OPT_fPIE) || O.matches(options::OPT_fpie);
        PIC =
            PIE || O.matches(options::OPT_fPIC) || O.matches(options::OPT_fpic);
        IsPICLevelTwo =
            O.matches(options::OPT_fPIE) || O.matches(options::OPT_fPIC);
      } else {
        PIE = PIC = false;
        if (EffectiveTriple.isPS()) {
          Arg *ModelArg = Args.getLastArg(options::OPT_mcmodel_EQ);
          StringRef Model = ModelArg ? ModelArg->getValue() : "";
          if (Model != "kernel") {
            PIC = true;
            ToolChain.getDriver().Diag(diag::warn_drv_ps_force_pic)
                << LastPICArg->getSpelling()
                << (EffectiveTriple.isPS4() ? "PS4" : "PS5");
          }
        }
      }
    }
  }

  // Introduce a Darwin and PS4/PS5-specific hack. If the default is PIC, but
  // the PIC level would've been set to level 1, force it back to level 2 PIC
  // instead.
  if (PIC && (Triple.isOSDarwin() || EffectiveTriple.isPS()))
    IsPICLevelTwo |= ToolChain.isPICDefault();

  // This kernel flags are a trump-card: they will disable PIC/PIE
  // generation, independent of the argument order.
  if (KernelOrKext &&
      ((!EffectiveTriple.isiOS() || EffectiveTriple.isOSVersionLT(6)) &&
       !EffectiveTriple.isWatchOS() && !EffectiveTriple.isDriverKit()))
    PIC = PIE = false;

  if (Arg *A = Args.getLastArg(options::OPT_mdynamic_no_pic)) {
    // This is a very special mode. It trumps the other modes, almost no one
    // uses it, and it isn't even valid on any OS but Darwin.
    if (!Triple.isOSDarwin())
      ToolChain.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << Triple.str();

    // FIXME: Warn when this flag trumps some other PIC or PIE flag.

    // Only a forced PIC mode can cause the actual compile to have PIC defines
    // etc., no flags are sufficient. This behavior was selected to closely
    // match that of llvm-gcc and Apple GCC before that.
    PIC = ToolChain.isPICDefault() && ToolChain.isPICDefaultForced();

    return std::make_tuple(llvm::Reloc::DynamicNoPIC, PIC ? 2U : 0U, false);
  }

  bool EmbeddedPISupported;
  switch (Triple.getArch()) {
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
      EmbeddedPISupported = true;
      break;
    default:
      EmbeddedPISupported = false;
      break;
  }

  bool ROPI = false, RWPI = false;
  Arg* LastROPIArg = Args.getLastArg(options::OPT_fropi, options::OPT_fno_ropi);
  if (LastROPIArg && LastROPIArg->getOption().matches(options::OPT_fropi)) {
    if (!EmbeddedPISupported)
      ToolChain.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
          << LastROPIArg->getSpelling() << Triple.str();
    ROPI = true;
  }
  Arg *LastRWPIArg = Args.getLastArg(options::OPT_frwpi, options::OPT_fno_rwpi);
  if (LastRWPIArg && LastRWPIArg->getOption().matches(options::OPT_frwpi)) {
    if (!EmbeddedPISupported)
      ToolChain.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
          << LastRWPIArg->getSpelling() << Triple.str();
    RWPI = true;
  }

  // ROPI and RWPI are not compatible with PIC or PIE.
  if ((ROPI || RWPI) && (PIC || PIE))
    ToolChain.getDriver().Diag(diag::err_drv_ropi_rwpi_incompatible_with_pic);

  if (Triple.isMIPS()) {
    StringRef CPUName;
    StringRef ABIName;
    mips::getMipsCPUAndABI(Args, Triple, CPUName, ABIName);
    // When targeting the N64 ABI, PIC is the default, except in the case
    // when the -mno-abicalls option is used. In that case we exit
    // at next check regardless of PIC being set below.
    if (ABIName == "n64")
      PIC = true;
    // When targettng MIPS with -mno-abicalls, it's always static.
    if(Args.hasArg(options::OPT_mno_abicalls))
      return std::make_tuple(llvm::Reloc::Static, 0U, false);
    // Unlike other architectures, MIPS, even with -fPIC/-mxgot/multigot,
    // does not use PIC level 2 for historical reasons.
    IsPICLevelTwo = false;
  }

  if (PIC)
    return std::make_tuple(llvm::Reloc::PIC_, IsPICLevelTwo ? 2U : 1U, PIE);

  llvm::Reloc::Model RelocM = llvm::Reloc::Static;
  if (ROPI && RWPI)
    RelocM = llvm::Reloc::ROPI_RWPI;
  else if (ROPI)
    RelocM = llvm::Reloc::ROPI;
  else if (RWPI)
    RelocM = llvm::Reloc::RWPI;

  return std::make_tuple(RelocM, 0U, false);
}

// `-falign-functions` indicates that the functions should be aligned to a
// 16-byte boundary.
//
// `-falign-functions=1` is the same as `-fno-align-functions`.
//
// The scalar `n` in `-falign-functions=n` must be an integral value between
// [0, 65536].  If the value is not a power-of-two, it will be rounded up to
// the nearest power-of-two.
//
// If we return `0`, the frontend will default to the backend's preferred
// alignment.
//
// NOTE: icc only allows values between [0, 4096].  icc uses `-falign-functions`
// to mean `-falign-functions=16`.  GCC defaults to the backend's preferred
// alignment.  For unaligned functions, we default to the backend's preferred
// alignment.
unsigned tools::ParseFunctionAlignment(const ToolChain &TC,
                                       const ArgList &Args) {
  const Arg *A = Args.getLastArg(options::OPT_falign_functions,
                                 options::OPT_falign_functions_EQ,
                                 options::OPT_fno_align_functions);
  if (!A || A->getOption().matches(options::OPT_fno_align_functions))
    return 0;

  if (A->getOption().matches(options::OPT_falign_functions))
    return 0;

  unsigned Value = 0;
  if (StringRef(A->getValue()).getAsInteger(10, Value) || Value > 65536)
    TC.getDriver().Diag(diag::err_drv_invalid_int_value)
        << A->getAsString(Args) << A->getValue();
  return Value ? llvm::Log2_32_Ceil(std::min(Value, 65536u)) : Value;
}

void tools::addDebugInfoKind(
    ArgStringList &CmdArgs, llvm::codegenoptions::DebugInfoKind DebugInfoKind) {
  switch (DebugInfoKind) {
  case llvm::codegenoptions::DebugDirectivesOnly:
    CmdArgs.push_back("-debug-info-kind=line-directives-only");
    break;
  case llvm::codegenoptions::DebugLineTablesOnly:
    CmdArgs.push_back("-debug-info-kind=line-tables-only");
    break;
  case llvm::codegenoptions::DebugInfoConstructor:
    CmdArgs.push_back("-debug-info-kind=constructor");
    break;
  case llvm::codegenoptions::LimitedDebugInfo:
    CmdArgs.push_back("-debug-info-kind=limited");
    break;
  case llvm::codegenoptions::FullDebugInfo:
    CmdArgs.push_back("-debug-info-kind=standalone");
    break;
  case llvm::codegenoptions::UnusedTypeInfo:
    CmdArgs.push_back("-debug-info-kind=unused-types");
    break;
  default:
    break;
  }
}

// Convert an arg of the form "-gN" or "-ggdbN" or one of their aliases
// to the corresponding DebugInfoKind.
llvm::codegenoptions::DebugInfoKind tools::debugLevelToInfoKind(const Arg &A) {
  assert(A.getOption().matches(options::OPT_gN_Group) &&
         "Not a -g option that specifies a debug-info level");
  if (A.getOption().matches(options::OPT_g0) ||
      A.getOption().matches(options::OPT_ggdb0))
    return llvm::codegenoptions::NoDebugInfo;
  if (A.getOption().matches(options::OPT_gline_tables_only) ||
      A.getOption().matches(options::OPT_ggdb1))
    return llvm::codegenoptions::DebugLineTablesOnly;
  if (A.getOption().matches(options::OPT_gline_directives_only))
    return llvm::codegenoptions::DebugDirectivesOnly;
  return llvm::codegenoptions::DebugInfoConstructor;
}

static unsigned ParseDebugDefaultVersion(const ToolChain &TC,
                                         const ArgList &Args) {
  const Arg *A = Args.getLastArg(options::OPT_fdebug_default_version);

  if (!A)
    return 0;

  unsigned Value = 0;
  if (StringRef(A->getValue()).getAsInteger(10, Value) || Value > 5 ||
      Value < 2)
    TC.getDriver().Diag(diag::err_drv_invalid_int_value)
        << A->getAsString(Args) << A->getValue();
  return Value;
}

unsigned tools::DwarfVersionNum(StringRef ArgValue) {
  return llvm::StringSwitch<unsigned>(ArgValue)
      .Case("-gdwarf-2", 2)
      .Case("-gdwarf-3", 3)
      .Case("-gdwarf-4", 4)
      .Case("-gdwarf-5", 5)
      .Default(0);
}

const Arg *tools::getDwarfNArg(const ArgList &Args) {
  return Args.getLastArg(options::OPT_gdwarf_2, options::OPT_gdwarf_3,
                         options::OPT_gdwarf_4, options::OPT_gdwarf_5,
                         options::OPT_gdwarf);
}

unsigned tools::getDwarfVersion(const ToolChain &TC,
                                const llvm::opt::ArgList &Args) {
  unsigned DwarfVersion = ParseDebugDefaultVersion(TC, Args);
  if (const Arg *GDwarfN = getDwarfNArg(Args))
    if (int N = DwarfVersionNum(GDwarfN->getSpelling())) {
      DwarfVersion = N;
      if (DwarfVersion == 5 && TC.getTriple().isOSAIX())
        TC.getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
            << GDwarfN->getSpelling() << TC.getTriple().str();
    }
  if (DwarfVersion == 0) {
    DwarfVersion = TC.GetDefaultDwarfVersion();
    assert(DwarfVersion && "toolchain default DWARF version must be nonzero");
  }
  return DwarfVersion;
}

void tools::AddAssemblerKPIC(const ToolChain &ToolChain, const ArgList &Args,
                             ArgStringList &CmdArgs) {
  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) = ParsePICArgs(ToolChain, Args);

  if (RelocationModel != llvm::Reloc::Static)
    CmdArgs.push_back("-KPIC");
}

/// Determine whether Objective-C automated reference counting is
/// enabled.
bool tools::isObjCAutoRefCount(const ArgList &Args) {
  return Args.hasFlag(options::OPT_fobjc_arc, options::OPT_fno_objc_arc, false);
}

enum class LibGccType { UnspecifiedLibGcc, StaticLibGcc, SharedLibGcc };

static LibGccType getLibGccType(const ToolChain &TC, const Driver &D,
                                const ArgList &Args) {
  if (Args.hasArg(options::OPT_static_libgcc) ||
      Args.hasArg(options::OPT_static) || Args.hasArg(options::OPT_static_pie) ||
      // The Android NDK only provides libunwind.a, not libunwind.so.
      TC.getTriple().isAndroid())
    return LibGccType::StaticLibGcc;
  if (Args.hasArg(options::OPT_shared_libgcc))
    return LibGccType::SharedLibGcc;
  return LibGccType::UnspecifiedLibGcc;
}

// Gcc adds libgcc arguments in various ways:
//
// gcc <none>:     -lgcc --as-needed -lgcc_s --no-as-needed
// g++ <none>:                       -lgcc_s               -lgcc
// gcc shared:                       -lgcc_s               -lgcc
// g++ shared:                       -lgcc_s               -lgcc
// gcc static:     -lgcc             -lgcc_eh
// g++ static:     -lgcc             -lgcc_eh
// gcc static-pie: -lgcc             -lgcc_eh
// g++ static-pie: -lgcc             -lgcc_eh
//
// Also, certain targets need additional adjustments.

static void AddUnwindLibrary(const ToolChain &TC, const Driver &D,
                             ArgStringList &CmdArgs, const ArgList &Args) {
  ToolChain::UnwindLibType UNW = TC.GetUnwindLibType(Args);
  // By default OHOS binaries are linked statically to libunwind.
  if (TC.getTriple().isOHOSFamily() && UNW == ToolChain::UNW_CompilerRT) {
    CmdArgs.push_back("-l:libunwind.a");
    return;
  }

  // Targets that don't use unwind libraries.
  if ((TC.getTriple().isAndroid() && UNW == ToolChain::UNW_Libgcc) ||
      TC.getTriple().isOSIAMCU() || TC.getTriple().isOSBinFormatWasm() ||
      TC.getTriple().isWindowsMSVCEnvironment() || UNW == ToolChain::UNW_None)
    return;

  LibGccType LGT = getLibGccType(TC, D, Args);
  bool AsNeeded = LGT == LibGccType::UnspecifiedLibGcc &&
                  (UNW == ToolChain::UNW_CompilerRT || !D.CCCIsCXX()) &&
                  !TC.getTriple().isAndroid() &&
                  !TC.getTriple().isOSCygMing() && !TC.getTriple().isOSAIX();
  if (AsNeeded)
    addAsNeededOption(TC, Args, CmdArgs, true);

  switch (UNW) {
  case ToolChain::UNW_None:
    return;
  case ToolChain::UNW_Libgcc: {
    if (LGT == LibGccType::StaticLibGcc)
      CmdArgs.push_back("-lgcc_eh");
    else
      CmdArgs.push_back("-lgcc_s");
    break;
  }
  case ToolChain::UNW_CompilerRT:
    if (TC.getTriple().isOSAIX()) {
      // AIX only has libunwind as a shared library. So do not pass
      // anything in if -static is specified.
      if (LGT != LibGccType::StaticLibGcc)
        CmdArgs.push_back("-lunwind");
    } else if (LGT == LibGccType::StaticLibGcc) {
      CmdArgs.push_back("-l:libunwind.a");
    } else if (LGT == LibGccType::SharedLibGcc) {
      if (TC.getTriple().isOSCygMing())
        CmdArgs.push_back("-l:libunwind.dll.a");
      else
        CmdArgs.push_back("-l:libunwind.so");
    } else {
      // Let the linker choose between libunwind.so and libunwind.a
      // depending on what's available, and depending on the -static flag
      CmdArgs.push_back("-lunwind");
    }
    break;
  }

  if (AsNeeded)
    addAsNeededOption(TC, Args, CmdArgs, false);
}

static void AddLibgcc(const ToolChain &TC, const Driver &D,
                      ArgStringList &CmdArgs, const ArgList &Args) {
  LibGccType LGT = getLibGccType(TC, D, Args);
  if (LGT == LibGccType::StaticLibGcc ||
      (LGT == LibGccType::UnspecifiedLibGcc && !D.CCCIsCXX()))
    CmdArgs.push_back("-lgcc");
  AddUnwindLibrary(TC, D, CmdArgs, Args);
  if (LGT == LibGccType::SharedLibGcc ||
      (LGT == LibGccType::UnspecifiedLibGcc && D.CCCIsCXX()))
    CmdArgs.push_back("-lgcc");
}

void tools::AddRunTimeLibs(const ToolChain &TC, const Driver &D,
                           ArgStringList &CmdArgs, const ArgList &Args) {
  // Make use of compiler-rt if --rtlib option is used
  ToolChain::RuntimeLibType RLT = TC.GetRuntimeLibType(Args);

  switch (RLT) {
  case ToolChain::RLT_CompilerRT:
    CmdArgs.push_back(TC.getCompilerRTArgString(Args, "builtins"));
    AddUnwindLibrary(TC, D, CmdArgs, Args);
    break;
  case ToolChain::RLT_Libgcc:
    // Make sure libgcc is not used under MSVC environment by default
    if (TC.getTriple().isKnownWindowsMSVCEnvironment()) {
      // Issue error diagnostic if libgcc is explicitly specified
      // through command line as --rtlib option argument.
      Arg *A = Args.getLastArg(options::OPT_rtlib_EQ);
      if (A && A->getValue() != StringRef("platform")) {
        TC.getDriver().Diag(diag::err_drv_unsupported_rtlib_for_platform)
            << A->getValue() << "MSVC";
      }
    } else
      AddLibgcc(TC, D, CmdArgs, Args);
    break;
  }

  // On Android, the unwinder uses dl_iterate_phdr (or one of
  // dl_unwind_find_exidx/__gnu_Unwind_Find_exidx on arm32) from libdl.so. For
  // statically-linked executables, these functions come from libc.a instead.
  if (TC.getTriple().isAndroid() && !Args.hasArg(options::OPT_static) &&
      !Args.hasArg(options::OPT_static_pie))
    CmdArgs.push_back("-ldl");
}

SmallString<128> tools::getStatsFileName(const llvm::opt::ArgList &Args,
                                         const InputInfo &Output,
                                         const InputInfo &Input,
                                         const Driver &D) {
  const Arg *A = Args.getLastArg(options::OPT_save_stats_EQ);
  if (!A && !D.CCPrintInternalStats)
    return {};

  SmallString<128> StatsFile;
  if (A) {
    StringRef SaveStats = A->getValue();
    if (SaveStats == "obj" && Output.isFilename()) {
      StatsFile.assign(Output.getFilename());
      llvm::sys::path::remove_filename(StatsFile);
    } else if (SaveStats != "cwd") {
      D.Diag(diag::err_drv_invalid_value) << A->getAsString(Args) << SaveStats;
      return {};
    }

    StringRef BaseName = llvm::sys::path::filename(Input.getBaseInput());
    llvm::sys::path::append(StatsFile, BaseName);
    llvm::sys::path::replace_extension(StatsFile, "stats");
  } else {
    assert(D.CCPrintInternalStats);
    StatsFile.assign(D.CCPrintInternalStatReportFilename.empty()
                         ? "-"
                         : D.CCPrintInternalStatReportFilename);
  }
  return StatsFile;
}

void tools::addMultilibFlag(bool Enabled, const StringRef Flag,
                            Multilib::flags_list &Flags) {
  assert(Flag.front() == '-');
  if (Enabled) {
    Flags.push_back(Flag.str());
  } else {
    Flags.push_back(("!" + Flag.substr(1)).str());
  }
}

void tools::addX86AlignBranchArgs(const Driver &D, const ArgList &Args,
                                  ArgStringList &CmdArgs, bool IsLTO,
                                  const StringRef PluginOptPrefix) {
  auto addArg = [&, IsLTO](const Twine &Arg) {
    if (IsLTO) {
      assert(!PluginOptPrefix.empty() && "Cannot have empty PluginOptPrefix!");
      CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) + Arg));
    } else {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back(Args.MakeArgString(Arg));
    }
  };

  if (Args.hasArg(options::OPT_mbranches_within_32B_boundaries)) {
    addArg(Twine("-x86-branches-within-32B-boundaries"));
  }
  if (const Arg *A = Args.getLastArg(options::OPT_malign_branch_boundary_EQ)) {
    StringRef Value = A->getValue();
    unsigned Boundary;
    if (Value.getAsInteger(10, Boundary) || Boundary < 16 ||
        !llvm::isPowerOf2_64(Boundary)) {
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Value << A->getOption().getName();
    } else {
      addArg("-x86-align-branch-boundary=" + Twine(Boundary));
    }
  }
  if (const Arg *A = Args.getLastArg(options::OPT_malign_branch_EQ)) {
    std::string AlignBranch;
    for (StringRef T : A->getValues()) {
      if (T != "fused" && T != "jcc" && T != "jmp" && T != "call" &&
          T != "ret" && T != "indirect")
        D.Diag(diag::err_drv_invalid_malign_branch_EQ)
            << T << "fused, jcc, jmp, call, ret, indirect";
      if (!AlignBranch.empty())
        AlignBranch += '+';
      AlignBranch += T;
    }
    addArg("-x86-align-branch=" + Twine(AlignBranch));
  }
  if (const Arg *A = Args.getLastArg(options::OPT_mpad_max_prefix_size_EQ)) {
    StringRef Value = A->getValue();
    unsigned PrefixSize;
    if (Value.getAsInteger(10, PrefixSize)) {
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Value << A->getOption().getName();
    } else {
      addArg("-x86-pad-max-prefix-size=" + Twine(PrefixSize));
    }
  }
}

/// SDLSearch: Search for Static Device Library
/// The search for SDL bitcode files is consistent with how static host
/// libraries are discovered. That is, the -l option triggers a search for
/// files in a set of directories called the LINKPATH. The host library search
/// procedure looks for a specific filename in the LINKPATH.  The filename for
/// a host library is lib<libname>.a or lib<libname>.so. For SDLs, there is an
/// ordered-set of filenames that are searched. We call this ordered-set of
/// filenames as SEARCH-ORDER. Since an SDL can either be device-type specific,
/// architecture specific, or generic across all architectures, a naming
/// convention and search order is used where the file name embeds the
/// architecture name <arch-name> (nvptx or amdgcn) and the GPU device type
/// <device-name> such as sm_30 and gfx906. <device-name> is absent in case of
/// device-independent SDLs. To reduce congestion in host library directories,
/// the search first looks for files in the “libdevice” subdirectory. SDLs that
/// are bc files begin with the prefix “lib”.
///
/// Machine-code SDLs can also be managed as an archive (*.a file). The
/// convention has been to use the prefix “lib”. To avoid confusion with host
/// archive libraries, we use prefix "libbc-" for the bitcode SDL archives.
///
static bool SDLSearch(const Driver &D, const llvm::opt::ArgList &DriverArgs,
                      llvm::opt::ArgStringList &CC1Args,
                      const SmallVectorImpl<std::string> &LibraryPaths,
                      StringRef Lib, StringRef Arch, StringRef Target,
                      bool isBitCodeSDL) {
  SmallVector<std::string, 12> SDLs;

  std::string LibDeviceLoc = "/libdevice";
  std::string LibBcPrefix = "/libbc-";
  std::string LibPrefix = "/lib";

  if (isBitCodeSDL) {
    // SEARCH-ORDER for Bitcode SDLs:
    //       libdevice/libbc-<libname>-<arch-name>-<device-type>.a
    //       libbc-<libname>-<arch-name>-<device-type>.a
    //       libdevice/libbc-<libname>-<arch-name>.a
    //       libbc-<libname>-<arch-name>.a
    //       libdevice/libbc-<libname>.a
    //       libbc-<libname>.a
    //       libdevice/lib<libname>-<arch-name>-<device-type>.bc
    //       lib<libname>-<arch-name>-<device-type>.bc
    //       libdevice/lib<libname>-<arch-name>.bc
    //       lib<libname>-<arch-name>.bc
    //       libdevice/lib<libname>.bc
    //       lib<libname>.bc

    for (StringRef Base : {LibBcPrefix, LibPrefix}) {
      const auto *Ext = Base.contains(LibBcPrefix) ? ".a" : ".bc";

      for (auto Suffix : {Twine(Lib + "-" + Arch + "-" + Target).str(),
                          Twine(Lib + "-" + Arch).str(), Twine(Lib).str()}) {
        SDLs.push_back(Twine(LibDeviceLoc + Base + Suffix + Ext).str());
        SDLs.push_back(Twine(Base + Suffix + Ext).str());
      }
    }
  } else {
    // SEARCH-ORDER for Machine-code SDLs:
    //    libdevice/lib<libname>-<arch-name>-<device-type>.a
    //    lib<libname>-<arch-name>-<device-type>.a
    //    libdevice/lib<libname>-<arch-name>.a
    //    lib<libname>-<arch-name>.a

    const auto *Ext = ".a";

    for (auto Suffix : {Twine(Lib + "-" + Arch + "-" + Target).str(),
                        Twine(Lib + "-" + Arch).str()}) {
      SDLs.push_back(Twine(LibDeviceLoc + LibPrefix + Suffix + Ext).str());
      SDLs.push_back(Twine(LibPrefix + Suffix + Ext).str());
    }
  }

  // The CUDA toolchain does not use a global device llvm-link before the LLVM
  // backend generates ptx. So currently, the use of bitcode SDL for nvptx is
  // only possible with post-clang-cc1 linking. Clang cc1 has a feature that
  // will link libraries after clang compilation while the LLVM IR is still in
  // memory. This utilizes a clang cc1 option called “-mlink-builtin-bitcode”.
  // This is a clang -cc1 option that is generated by the clang driver. The
  // option value must a full path to an existing file.
  bool FoundSDL = false;
  for (auto LPath : LibraryPaths) {
    for (auto SDL : SDLs) {
      auto FullName = Twine(LPath + SDL).str();
      if (llvm::sys::fs::exists(FullName)) {
        CC1Args.push_back(DriverArgs.MakeArgString(FullName));
        FoundSDL = true;
        break;
      }
    }
    if (FoundSDL)
      break;
  }
  return FoundSDL;
}

/// Search if a user provided archive file lib<libname>.a exists in any of
/// the library paths. If so, add a new command to clang-offload-bundler to
/// unbundle this archive and create a temporary device specific archive. Name
/// of this SDL is passed to the llvm-link tool.
static void GetSDLFromOffloadArchive(
    Compilation &C, const Driver &D, const Tool &T, const JobAction &JA,
    const InputInfoList &Inputs, const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args,
    const SmallVectorImpl<std::string> &LibraryPaths, StringRef Lib,
    StringRef Arch, StringRef Target, bool isBitCodeSDL) {

  // We don't support bitcode archive bundles for nvptx
  if (isBitCodeSDL && Arch.contains("nvptx"))
    return;

  bool FoundAOB = false;
  std::string ArchiveOfBundles;

  llvm::Triple Triple(D.getTargetTriple());
  bool IsMSVC = Triple.isWindowsMSVCEnvironment();
  auto Ext = IsMSVC ? ".lib" : ".a";
  if (!Lib.starts_with(":") && !Lib.starts_with("-l")) {
    if (llvm::sys::fs::exists(Lib)) {
      ArchiveOfBundles = Lib;
      FoundAOB = true;
    }
  } else {
    Lib.consume_front("-l");
    for (auto LPath : LibraryPaths) {
      ArchiveOfBundles.clear();
      auto LibFile = (Lib.starts_with(":") ? Lib.drop_front()
                      : IsMSVC             ? Lib + Ext
                                           : "lib" + Lib + Ext)
                         .str();
      for (auto Prefix : {"/libdevice/", "/"}) {
        auto AOB = Twine(LPath + Prefix + LibFile).str();
        if (llvm::sys::fs::exists(AOB)) {
          ArchiveOfBundles = AOB;
          FoundAOB = true;
          break;
        }
      }
      if (FoundAOB)
        break;
    }
  }

  if (!FoundAOB)
    return;

  llvm::file_magic Magic;
  auto EC = llvm::identify_magic(ArchiveOfBundles, Magic);
  if (EC || Magic != llvm::file_magic::archive)
    return;

  StringRef Prefix = isBitCodeSDL ? "libbc-" : "lib";
  std::string OutputLib =
      D.GetTemporaryPath(Twine(Prefix + llvm::sys::path::filename(Lib) + "-" +
                               Arch + "-" + Target)
                             .str(),
                         "a");

  C.addTempFile(C.getArgs().MakeArgString(OutputLib));

  ArgStringList CmdArgs;
  SmallString<128> DeviceTriple;
  DeviceTriple += Action::GetOffloadKindName(JA.getOffloadingDeviceKind());
  DeviceTriple += '-';
  std::string NormalizedTriple = T.getToolChain().getTriple().normalize();
  DeviceTriple += NormalizedTriple;
  if (!Target.empty()) {
    DeviceTriple += '-';
    DeviceTriple += Target;
  }

  std::string UnbundleArg("-unbundle");
  std::string TypeArg("-type=a");
  std::string InputArg("-input=" + ArchiveOfBundles);
  std::string OffloadArg("-targets=" + std::string(DeviceTriple));
  std::string OutputArg("-output=" + OutputLib);

  const char *UBProgram = DriverArgs.MakeArgString(
      T.getToolChain().GetProgramPath("clang-offload-bundler"));

  ArgStringList UBArgs;
  UBArgs.push_back(C.getArgs().MakeArgString(UnbundleArg));
  UBArgs.push_back(C.getArgs().MakeArgString(TypeArg));
  UBArgs.push_back(C.getArgs().MakeArgString(InputArg));
  UBArgs.push_back(C.getArgs().MakeArgString(OffloadArg));
  UBArgs.push_back(C.getArgs().MakeArgString(OutputArg));

  // Add this flag to not exit from clang-offload-bundler if no compatible
  // code object is found in heterogenous archive library.
  std::string AdditionalArgs("-allow-missing-bundles");
  UBArgs.push_back(C.getArgs().MakeArgString(AdditionalArgs));

  // Add this flag to treat hip and hipv4 offload kinds as compatible with
  // openmp offload kind while extracting code objects from a heterogenous
  // archive library. Vice versa is also considered compatible.
  std::string HipCompatibleArgs("-hip-openmp-compatible");
  UBArgs.push_back(C.getArgs().MakeArgString(HipCompatibleArgs));

  C.addCommand(std::make_unique<Command>(
      JA, T, ResponseFileSupport::AtFileCurCP(), UBProgram, UBArgs, Inputs,
      InputInfo(&JA, C.getArgs().MakeArgString(OutputLib))));

  CC1Args.push_back(DriverArgs.MakeArgString(OutputLib));

  return;
}

// Wrapper function used by driver for adding SDLs during link phase.
void tools::AddStaticDeviceLibsLinking(Compilation &C, const Tool &T,
                                       const JobAction &JA,
                                       const InputInfoList &Inputs,
                                       const llvm::opt::ArgList &DriverArgs,
                                       llvm::opt::ArgStringList &CC1Args,
                                       StringRef Arch, StringRef Target,
                                       bool isBitCodeSDL) {
  AddStaticDeviceLibs(&C, &T, &JA, &Inputs, C.getDriver(), DriverArgs, CC1Args,
                      Arch, Target, isBitCodeSDL);
}

// User defined Static Device Libraries(SDLs) can be passed to clang for
// offloading GPU compilers. Like static host libraries, the use of a SDL is
// specified with the -l command line option. The primary difference between
// host and SDLs is the filenames for SDLs (refer SEARCH-ORDER for Bitcode SDLs
// and SEARCH-ORDER for Machine-code SDLs for the naming convention).
// SDLs are of following types:
//
// * Bitcode SDLs: They can either be a *.bc file or an archive of *.bc files.
//           For NVPTX, these libraries are post-clang linked following each
//           compilation. For AMDGPU, these libraries are linked one time
//           during the application link phase.
//
// * Machine-code SDLs: They are archive files. For AMDGPU, the process for
//           machine code SDLs is still in development. But they will be linked
//           by the LLVM tool lld.
//
// * Bundled objects that contain both host and device codes: Bundled objects
//           may also contain library code compiled from source. For NVPTX, the
//           bundle contains cubin. For AMDGPU, the bundle contains bitcode.
//
// For Bitcode and Machine-code SDLs, current compiler toolchains hardcode the
// inclusion of specific SDLs such as math libraries and the OpenMP device
// library libomptarget.
void tools::AddStaticDeviceLibs(Compilation *C, const Tool *T,
                                const JobAction *JA,
                                const InputInfoList *Inputs, const Driver &D,
                                const llvm::opt::ArgList &DriverArgs,
                                llvm::opt::ArgStringList &CC1Args,
                                StringRef Arch, StringRef Target,
                                bool isBitCodeSDL) {

  SmallVector<std::string, 8> LibraryPaths;
  // Add search directories from LIBRARY_PATH env variable
  std::optional<std::string> LibPath =
      llvm::sys::Process::GetEnv("LIBRARY_PATH");
  if (LibPath) {
    SmallVector<StringRef, 8> Frags;
    const char EnvPathSeparatorStr[] = {llvm::sys::EnvPathSeparator, '\0'};
    llvm::SplitString(*LibPath, Frags, EnvPathSeparatorStr);
    for (StringRef Path : Frags)
      LibraryPaths.emplace_back(Path.trim());
  }

  // Add directories from user-specified -L options
  for (std::string Search_Dir : DriverArgs.getAllArgValues(options::OPT_L))
    LibraryPaths.emplace_back(Search_Dir);

  // Add path to lib-debug folders
  SmallString<256> DefaultLibPath = llvm::sys::path::parent_path(D.Dir);
  llvm::sys::path::append(DefaultLibPath, CLANG_INSTALL_LIBDIR_BASENAME);
  LibraryPaths.emplace_back(DefaultLibPath.c_str());

  // Build list of Static Device Libraries SDLs specified by -l option
  llvm::SmallSet<std::string, 16> SDLNames;
  static const StringRef HostOnlyArchives[] = {
      "omp", "cudart", "m", "gcc", "gcc_s", "pthread", "hip_hcc"};
  for (auto SDLName : DriverArgs.getAllArgValues(options::OPT_l)) {
    if (!llvm::is_contained(HostOnlyArchives, SDLName)) {
      SDLNames.insert(std::string("-l") + SDLName);
    }
  }

  for (auto Input : DriverArgs.getAllArgValues(options::OPT_INPUT)) {
    auto FileName = StringRef(Input);
    // Clang treats any unknown file types as archives and passes them to the
    // linker. Files with extension 'lib' are classified as TY_Object by clang
    // but they are usually archives. It is OK if the file is not really an
    // archive since GetSDLFromOffloadArchive will check the magic of the file
    // and only unbundle it if it is really an archive.
    const StringRef LibFileExt = ".lib";
    if (!llvm::sys::path::has_extension(FileName) ||
        types::lookupTypeForExtension(
            llvm::sys::path::extension(FileName).drop_front()) ==
            types::TY_INVALID ||
        llvm::sys::path::extension(FileName) == LibFileExt)
      SDLNames.insert(Input);
  }

  // The search stops as soon as an SDL file is found. The driver then provides
  // the full filename of the SDL to the llvm-link command. If no SDL is found
  // after searching each LINKPATH with SEARCH-ORDER, it is possible that an
  // archive file lib<libname>.a exists and may contain bundled object files.
  for (auto SDLName : SDLNames) {
    // This is the only call to SDLSearch
    if (!SDLSearch(D, DriverArgs, CC1Args, LibraryPaths, SDLName, Arch, Target,
                   isBitCodeSDL)) {
      GetSDLFromOffloadArchive(*C, D, *T, *JA, *Inputs, DriverArgs, CC1Args,
                               LibraryPaths, SDLName, Arch, Target,
                               isBitCodeSDL);
    }
  }
}

static llvm::opt::Arg *
getAMDGPUCodeObjectArgument(const Driver &D, const llvm::opt::ArgList &Args) {
  return Args.getLastArg(options::OPT_mcode_object_version_EQ);
}

void tools::checkAMDGPUCodeObjectVersion(const Driver &D,
                                         const llvm::opt::ArgList &Args) {
  const unsigned MinCodeObjVer = 4;
  const unsigned MaxCodeObjVer = 6;

  if (auto *CodeObjArg = getAMDGPUCodeObjectArgument(D, Args)) {
    if (CodeObjArg->getOption().getID() ==
        options::OPT_mcode_object_version_EQ) {
      unsigned CodeObjVer = MaxCodeObjVer;
      auto Remnant =
          StringRef(CodeObjArg->getValue()).getAsInteger(0, CodeObjVer);
      if (Remnant || CodeObjVer < MinCodeObjVer || CodeObjVer > MaxCodeObjVer)
        D.Diag(diag::err_drv_invalid_int_value)
            << CodeObjArg->getAsString(Args) << CodeObjArg->getValue();

      // COV6 is only supported by LLVM at the time of writing this, and it's
      // expected to take some time before all ROCm components fully
      // support it. In the meantime, make sure users are aware of this.
      if (CodeObjVer == 6)
        D.Diag(diag::warn_drv_amdgpu_cov6);
    }
  }
}

unsigned tools::getAMDGPUCodeObjectVersion(const Driver &D,
                                           const llvm::opt::ArgList &Args) {
  unsigned CodeObjVer = 4; // default
  if (auto *CodeObjArg = getAMDGPUCodeObjectArgument(D, Args))
    StringRef(CodeObjArg->getValue()).getAsInteger(0, CodeObjVer);
  return CodeObjVer;
}

bool tools::haveAMDGPUCodeObjectVersionArgument(
    const Driver &D, const llvm::opt::ArgList &Args) {
  return getAMDGPUCodeObjectArgument(D, Args) != nullptr;
}

void tools::addMachineOutlinerArgs(const Driver &D,
                                   const llvm::opt::ArgList &Args,
                                   llvm::opt::ArgStringList &CmdArgs,
                                   const llvm::Triple &Triple, bool IsLTO,
                                   const StringRef PluginOptPrefix) {
  auto addArg = [&, IsLTO](const Twine &Arg) {
    if (IsLTO) {
      assert(!PluginOptPrefix.empty() && "Cannot have empty PluginOptPrefix!");
      CmdArgs.push_back(Args.MakeArgString(Twine(PluginOptPrefix) + Arg));
    } else {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back(Args.MakeArgString(Arg));
    }
  };

  if (Arg *A = Args.getLastArg(options::OPT_moutline,
                               options::OPT_mno_outline)) {
    if (A->getOption().matches(options::OPT_moutline)) {
      // We only support -moutline in AArch64 and ARM targets right now. If
      // we're not compiling for these, emit a warning and ignore the flag.
      // Otherwise, add the proper mllvm flags.
      if (!(Triple.isARM() || Triple.isThumb() || Triple.isAArch64())) {
        D.Diag(diag::warn_drv_moutline_unsupported_opt) << Triple.getArchName();
      } else {
        addArg(Twine("-enable-machine-outliner"));
      }
    } else {
      // Disable all outlining behaviour.
      addArg(Twine("-enable-machine-outliner=never"));
    }
  }
}

void tools::addOpenMPDeviceRTL(const Driver &D,
                               const llvm::opt::ArgList &DriverArgs,
                               llvm::opt::ArgStringList &CC1Args,
                               StringRef BitcodeSuffix,
                               const llvm::Triple &Triple,
                               const ToolChain &HostTC) {
  SmallVector<StringRef, 8> LibraryPaths;

  // Add user defined library paths from LIBRARY_PATH.
  std::optional<std::string> LibPath =
      llvm::sys::Process::GetEnv("LIBRARY_PATH");
  if (LibPath) {
    SmallVector<StringRef, 8> Frags;
    const char EnvPathSeparatorStr[] = {llvm::sys::EnvPathSeparator, '\0'};
    llvm::SplitString(*LibPath, Frags, EnvPathSeparatorStr);
    for (StringRef Path : Frags)
      LibraryPaths.emplace_back(Path.trim());
  }

  // Check all of the standard library search paths used by the compiler.
  for (const auto &LibPath : HostTC.getFilePaths())
    LibraryPaths.emplace_back(LibPath);

  OptSpecifier LibomptargetBCPathOpt =
      Triple.isAMDGCN() ? options::OPT_libomptarget_amdgpu_bc_path_EQ
                        : options::OPT_libomptarget_nvptx_bc_path_EQ;

  StringRef ArchPrefix = Triple.isAMDGCN() ? "amdgpu" : "nvptx";
  std::string LibOmpTargetName =
      ("libomptarget-" + ArchPrefix + "-" + BitcodeSuffix + ".bc").str();

  // First check whether user specifies bc library
  if (const Arg *A = DriverArgs.getLastArg(LibomptargetBCPathOpt)) {
    SmallString<128> LibOmpTargetFile(A->getValue());
    if (llvm::sys::fs::exists(LibOmpTargetFile) &&
        llvm::sys::fs::is_directory(LibOmpTargetFile)) {
      llvm::sys::path::append(LibOmpTargetFile, LibOmpTargetName);
    }

    if (llvm::sys::fs::exists(LibOmpTargetFile)) {
      CC1Args.push_back("-mlink-builtin-bitcode");
      CC1Args.push_back(DriverArgs.MakeArgString(LibOmpTargetFile));
    } else {
      D.Diag(diag::err_drv_omp_offload_target_bcruntime_not_found)
          << LibOmpTargetFile;
    }
  } else {
    bool FoundBCLibrary = false;

    for (StringRef LibraryPath : LibraryPaths) {
      SmallString<128> LibOmpTargetFile(LibraryPath);
      llvm::sys::path::append(LibOmpTargetFile, LibOmpTargetName);
      if (llvm::sys::fs::exists(LibOmpTargetFile)) {
        CC1Args.push_back("-mlink-builtin-bitcode");
        CC1Args.push_back(DriverArgs.MakeArgString(LibOmpTargetFile));
        FoundBCLibrary = true;
        break;
      }
    }

    if (!FoundBCLibrary)
      D.Diag(diag::err_drv_omp_offload_target_missingbcruntime)
          << LibOmpTargetName << ArchPrefix;
  }
}
void tools::addHIPRuntimeLibArgs(const ToolChain &TC, Compilation &C,
                                 const llvm::opt::ArgList &Args,
                                 llvm::opt::ArgStringList &CmdArgs) {
  if ((C.getActiveOffloadKinds() & Action::OFK_HIP) &&
      !Args.hasArg(options::OPT_nostdlib) &&
      !Args.hasArg(options::OPT_no_hip_rt) && !Args.hasArg(options::OPT_r)) {
    TC.AddHIPRuntimeLibArgs(Args, CmdArgs);
  } else {
    // Claim "no HIP libraries" arguments if any
    for (auto *Arg : Args.filtered(options::OPT_no_hip_rt)) {
      Arg->claim();
    }
  }
}

void tools::addOutlineAtomicsArgs(const Driver &D, const ToolChain &TC,
                                  const llvm::opt::ArgList &Args,
                                  llvm::opt::ArgStringList &CmdArgs,
                                  const llvm::Triple &Triple) {
  if (Arg *A = Args.getLastArg(options::OPT_moutline_atomics,
                               options::OPT_mno_outline_atomics)) {
    // Option -moutline-atomics supported for AArch64 target only.
    if (!Triple.isAArch64()) {
      D.Diag(diag::warn_drv_moutline_atomics_unsupported_opt)
          << Triple.getArchName() << A->getOption().getName();
    } else {
      if (A->getOption().matches(options::OPT_moutline_atomics)) {
        CmdArgs.push_back("-target-feature");
        CmdArgs.push_back("+outline-atomics");
      } else {
        CmdArgs.push_back("-target-feature");
        CmdArgs.push_back("-outline-atomics");
      }
    }
  } else if (Triple.isAArch64() && TC.IsAArch64OutlineAtomicsDefault(Args)) {
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back("+outline-atomics");
  }
}

void tools::addOffloadCompressArgs(const llvm::opt::ArgList &TCArgs,
                                   llvm::opt::ArgStringList &CmdArgs) {
  if (TCArgs.hasFlag(options::OPT_offload_compress,
                     options::OPT_no_offload_compress, false))
    CmdArgs.push_back("-compress");
  if (TCArgs.hasArg(options::OPT_v))
    CmdArgs.push_back("-verbose");
  if (auto *Arg = TCArgs.getLastArg(options::OPT_offload_compression_level_EQ))
    CmdArgs.push_back(
        TCArgs.MakeArgString(Twine("-compression-level=") + Arg->getValue()));
}

void tools::addMCModel(const Driver &D, const llvm::opt::ArgList &Args,
                       const llvm::Triple &Triple,
                       const llvm::Reloc::Model &RelocationModel,
                       llvm::opt::ArgStringList &CmdArgs) {
  if (Arg *A = Args.getLastArg(options::OPT_mcmodel_EQ)) {
    StringRef CM = A->getValue();
    bool Ok = false;
    if (Triple.isOSAIX() && CM == "medium")
      CM = "large";
    if (Triple.isAArch64(64)) {
      Ok = CM == "tiny" || CM == "small" || CM == "large";
      if (CM == "large" && !Triple.isOSBinFormatMachO() &&
          RelocationModel != llvm::Reloc::Static)
        D.Diag(diag::err_drv_argument_only_allowed_with)
            << A->getAsString(Args) << "-fno-pic";
    } else if (Triple.isLoongArch()) {
      if (CM == "extreme" &&
          Args.hasFlagNoClaim(options::OPT_fplt, options::OPT_fno_plt, false))
        D.Diag(diag::err_drv_argument_not_allowed_with)
            << A->getAsString(Args) << "-fplt";
      Ok = CM == "normal" || CM == "medium" || CM == "extreme";
      // Convert to LLVM recognizable names.
      if (Ok)
        CM = llvm::StringSwitch<StringRef>(CM)
                 .Case("normal", "small")
                 .Case("extreme", "large")
                 .Default(CM);
    } else if (Triple.isPPC64() || Triple.isOSAIX()) {
      Ok = CM == "small" || CM == "medium" || CM == "large";
    } else if (Triple.isRISCV()) {
      if (CM == "medlow")
        CM = "small";
      else if (CM == "medany")
        CM = "medium";
      Ok = CM == "small" || CM == "medium";
    } else if (Triple.getArch() == llvm::Triple::x86_64) {
      Ok = llvm::is_contained({"small", "kernel", "medium", "large", "tiny"},
                              CM);
    } else if (Triple.isNVPTX() || Triple.isAMDGPU() || Triple.isSPIRV()) {
      // NVPTX/AMDGPU/SPIRV does not care about the code model and will accept
      // whatever works for the host.
      Ok = true;
    } else if (Triple.isSPARC64()) {
      if (CM == "medlow")
        CM = "small";
      else if (CM == "medmid")
        CM = "medium";
      else if (CM == "medany")
        CM = "large";
      Ok = CM == "small" || CM == "medium" || CM == "large";
    }
    if (Ok) {
      CmdArgs.push_back(Args.MakeArgString("-mcmodel=" + CM));
    } else {
      D.Diag(diag::err_drv_unsupported_option_argument_for_target)
          << A->getSpelling() << CM << Triple.getTriple();
    }
  }

  if (Triple.getArch() == llvm::Triple::x86_64) {
    bool IsMediumCM = false;
    bool IsLargeCM = false;
    if (Arg *A = Args.getLastArg(options::OPT_mcmodel_EQ)) {
      IsMediumCM = StringRef(A->getValue()) == "medium";
      IsLargeCM = StringRef(A->getValue()) == "large";
    }
    if (Arg *A = Args.getLastArg(options::OPT_mlarge_data_threshold_EQ)) {
      if (!IsMediumCM && !IsLargeCM) {
        D.Diag(diag::warn_drv_large_data_threshold_invalid_code_model)
            << A->getOption().getRenderName();
      } else {
        A->render(Args, CmdArgs);
      }
    } else if (IsMediumCM) {
      CmdArgs.push_back("-mlarge-data-threshold=65536");
    } else if (IsLargeCM) {
      CmdArgs.push_back("-mlarge-data-threshold=0");
    }
  }
}
