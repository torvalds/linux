//===--- Driver.cpp - Clang GCC Compatible Driver -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Driver.h"
#include "ToolChains/AIX.h"
#include "ToolChains/AMDGPU.h"
#include "ToolChains/AMDGPUOpenMP.h"
#include "ToolChains/AVR.h"
#include "ToolChains/Arch/RISCV.h"
#include "ToolChains/BareMetal.h"
#include "ToolChains/CSKYToolChain.h"
#include "ToolChains/Clang.h"
#include "ToolChains/CrossWindows.h"
#include "ToolChains/Cuda.h"
#include "ToolChains/Darwin.h"
#include "ToolChains/DragonFly.h"
#include "ToolChains/FreeBSD.h"
#include "ToolChains/Fuchsia.h"
#include "ToolChains/Gnu.h"
#include "ToolChains/HIPAMD.h"
#include "ToolChains/HIPSPV.h"
#include "ToolChains/HLSL.h"
#include "ToolChains/Haiku.h"
#include "ToolChains/Hexagon.h"
#include "ToolChains/Hurd.h"
#include "ToolChains/Lanai.h"
#include "ToolChains/Linux.h"
#include "ToolChains/MSP430.h"
#include "ToolChains/MSVC.h"
#include "ToolChains/MinGW.h"
#include "ToolChains/MipsLinux.h"
#include "ToolChains/NaCl.h"
#include "ToolChains/NetBSD.h"
#include "ToolChains/OHOS.h"
#include "ToolChains/OpenBSD.h"
#include "ToolChains/PPCFreeBSD.h"
#include "ToolChains/PPCLinux.h"
#include "ToolChains/PS4CPU.h"
#include "ToolChains/RISCVToolchain.h"
#include "ToolChains/SPIRV.h"
#include "ToolChains/Solaris.h"
#include "ToolChains/TCE.h"
#include "ToolChains/VEToolchain.h"
#include "ToolChains/WebAssembly.h"
#include "ToolChains/XCore.h"
#include "ToolChains/ZOS.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Basic/TargetID.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Phases.h"
#include "clang/Driver/SanitizerArgs.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Driver/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ExitCodes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include <cstdlib> // ::getenv
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#if LLVM_ON_UNIX
#include <unistd.h> // getpid
#endif

using namespace clang::driver;
using namespace clang;
using namespace llvm::opt;

static std::optional<llvm::Triple> getOffloadTargetTriple(const Driver &D,
                                                          const ArgList &Args) {
  auto OffloadTargets = Args.getAllArgValues(options::OPT_offload_EQ);
  // Offload compilation flow does not support multiple targets for now. We
  // need the HIPActionBuilder (and possibly the CudaActionBuilder{,Base}too)
  // to support multiple tool chains first.
  switch (OffloadTargets.size()) {
  default:
    D.Diag(diag::err_drv_only_one_offload_target_supported);
    return std::nullopt;
  case 0:
    D.Diag(diag::err_drv_invalid_or_unsupported_offload_target) << "";
    return std::nullopt;
  case 1:
    break;
  }
  return llvm::Triple(OffloadTargets[0]);
}

static std::optional<llvm::Triple>
getNVIDIAOffloadTargetTriple(const Driver &D, const ArgList &Args,
                             const llvm::Triple &HostTriple) {
  if (!Args.hasArg(options::OPT_offload_EQ)) {
    return llvm::Triple(HostTriple.isArch64Bit() ? "nvptx64-nvidia-cuda"
                                                 : "nvptx-nvidia-cuda");
  }
  auto TT = getOffloadTargetTriple(D, Args);
  if (TT && (TT->getArch() == llvm::Triple::spirv32 ||
             TT->getArch() == llvm::Triple::spirv64)) {
    if (Args.hasArg(options::OPT_emit_llvm))
      return TT;
    D.Diag(diag::err_drv_cuda_offload_only_emit_bc);
    return std::nullopt;
  }
  D.Diag(diag::err_drv_invalid_or_unsupported_offload_target) << TT->str();
  return std::nullopt;
}
static std::optional<llvm::Triple>
getHIPOffloadTargetTriple(const Driver &D, const ArgList &Args) {
  if (!Args.hasArg(options::OPT_offload_EQ)) {
    auto OffloadArchs = Args.getAllArgValues(options::OPT_offload_arch_EQ);
    if (llvm::find(OffloadArchs, "amdgcnspirv") != OffloadArchs.cend()) {
      if (OffloadArchs.size() == 1)
        return llvm::Triple("spirv64-amd-amdhsa");
      // Mixing specific & SPIR-V compilation is not supported for now.
      D.Diag(diag::err_drv_only_one_offload_target_supported);
      return std::nullopt;
    }
    return llvm::Triple("amdgcn-amd-amdhsa"); // Default HIP triple.
  }
  auto TT = getOffloadTargetTriple(D, Args);
  if (!TT)
    return std::nullopt;
  if (TT->getArch() == llvm::Triple::amdgcn &&
      TT->getVendor() == llvm::Triple::AMD &&
      TT->getOS() == llvm::Triple::AMDHSA)
    return TT;
  if (TT->getArch() == llvm::Triple::spirv64)
    return TT;
  D.Diag(diag::err_drv_invalid_or_unsupported_offload_target) << TT->str();
  return std::nullopt;
}

// static
std::string Driver::GetResourcesPath(StringRef BinaryPath,
                                     StringRef CustomResourceDir) {
  // Since the resource directory is embedded in the module hash, it's important
  // that all places that need it call this function, so that they get the
  // exact same string ("a/../b/" and "b/" get different hashes, for example).

  // Dir is bin/ or lib/, depending on where BinaryPath is.
  std::string Dir = std::string(llvm::sys::path::parent_path(BinaryPath));

  SmallString<128> P(Dir);
  if (CustomResourceDir != "") {
    llvm::sys::path::append(P, CustomResourceDir);
  } else {
    // On Windows, libclang.dll is in bin/.
    // On non-Windows, libclang.so/.dylib is in lib/.
    // With a static-library build of libclang, LibClangPath will contain the
    // path of the embedding binary, which for LLVM binaries will be in bin/.
    // ../lib gets us to lib/ in both cases.
    P = llvm::sys::path::parent_path(Dir);
    // This search path is also created in the COFF driver of lld, so any
    // changes here also needs to happen in lld/COFF/Driver.cpp
    llvm::sys::path::append(P, CLANG_INSTALL_LIBDIR_BASENAME, "clang",
                            CLANG_VERSION_MAJOR_STRING);
  }

  return std::string(P);
}

Driver::Driver(StringRef ClangExecutable, StringRef TargetTriple,
               DiagnosticsEngine &Diags, std::string Title,
               IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS)
    : Diags(Diags), VFS(std::move(VFS)), Mode(GCCMode),
      SaveTemps(SaveTempsNone), BitcodeEmbed(EmbedNone),
      Offload(OffloadHostDevice), CXX20HeaderType(HeaderMode_None),
      ModulesModeCXX20(false), LTOMode(LTOK_None),
      ClangExecutable(ClangExecutable), SysRoot(DEFAULT_SYSROOT),
      DriverTitle(Title), CCCPrintBindings(false), CCPrintOptions(false),
      CCLogDiagnostics(false), CCGenDiagnostics(false),
      CCPrintProcessStats(false), CCPrintInternalStats(false),
      TargetTriple(TargetTriple), Saver(Alloc), PrependArg(nullptr),
      CheckInputsExist(true), ProbePrecompiled(true),
      SuppressMissingInputWarning(false) {
  // Provide a sane fallback if no VFS is specified.
  if (!this->VFS)
    this->VFS = llvm::vfs::getRealFileSystem();

  Name = std::string(llvm::sys::path::filename(ClangExecutable));
  Dir = std::string(llvm::sys::path::parent_path(ClangExecutable));

  if ((!SysRoot.empty()) && llvm::sys::path::is_relative(SysRoot)) {
    // Prepend InstalledDir if SysRoot is relative
    SmallString<128> P(Dir);
    llvm::sys::path::append(P, SysRoot);
    SysRoot = std::string(P);
  }

#if defined(CLANG_CONFIG_FILE_SYSTEM_DIR)
  SystemConfigDir = CLANG_CONFIG_FILE_SYSTEM_DIR;
#endif
#if defined(CLANG_CONFIG_FILE_USER_DIR)
  {
    SmallString<128> P;
    llvm::sys::fs::expand_tilde(CLANG_CONFIG_FILE_USER_DIR, P);
    UserConfigDir = static_cast<std::string>(P);
  }
#endif

  // Compute the path to the resource directory.
  ResourceDir = GetResourcesPath(ClangExecutable, CLANG_RESOURCE_DIR);
}

void Driver::setDriverMode(StringRef Value) {
  static StringRef OptName =
      getOpts().getOption(options::OPT_driver_mode).getPrefixedName();
  if (auto M = llvm::StringSwitch<std::optional<DriverMode>>(Value)
                   .Case("gcc", GCCMode)
                   .Case("g++", GXXMode)
                   .Case("cpp", CPPMode)
                   .Case("cl", CLMode)
                   .Case("flang", FlangMode)
                   .Case("dxc", DXCMode)
                   .Default(std::nullopt))
    Mode = *M;
  else
    Diag(diag::err_drv_unsupported_option_argument) << OptName << Value;
}

InputArgList Driver::ParseArgStrings(ArrayRef<const char *> ArgStrings,
                                     bool UseDriverMode, bool &ContainsError) {
  llvm::PrettyStackTraceString CrashInfo("Command line argument parsing");
  ContainsError = false;

  llvm::opt::Visibility VisibilityMask = getOptionVisibilityMask(UseDriverMode);
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = getOpts().ParseArgs(ArgStrings, MissingArgIndex,
                                          MissingArgCount, VisibilityMask);

  // Check for missing argument error.
  if (MissingArgCount) {
    Diag(diag::err_drv_missing_argument)
        << Args.getArgString(MissingArgIndex) << MissingArgCount;
    ContainsError |=
        Diags.getDiagnosticLevel(diag::err_drv_missing_argument,
                                 SourceLocation()) > DiagnosticsEngine::Warning;
  }

  // Check for unsupported options.
  for (const Arg *A : Args) {
    if (A->getOption().hasFlag(options::Unsupported)) {
      Diag(diag::err_drv_unsupported_opt) << A->getAsString(Args);
      ContainsError |= Diags.getDiagnosticLevel(diag::err_drv_unsupported_opt,
                                                SourceLocation()) >
                       DiagnosticsEngine::Warning;
      continue;
    }

    // Warn about -mcpu= without an argument.
    if (A->getOption().matches(options::OPT_mcpu_EQ) && A->containsValue("")) {
      Diag(diag::warn_drv_empty_joined_argument) << A->getAsString(Args);
      ContainsError |= Diags.getDiagnosticLevel(
                           diag::warn_drv_empty_joined_argument,
                           SourceLocation()) > DiagnosticsEngine::Warning;
    }
  }

  for (const Arg *A : Args.filtered(options::OPT_UNKNOWN)) {
    unsigned DiagID;
    auto ArgString = A->getAsString(Args);
    std::string Nearest;
    if (getOpts().findNearest(ArgString, Nearest, VisibilityMask) > 1) {
      if (!IsCLMode() &&
          getOpts().findExact(ArgString, Nearest,
                              llvm::opt::Visibility(options::CC1Option))) {
        DiagID = diag::err_drv_unknown_argument_with_suggestion;
        Diags.Report(DiagID) << ArgString << "-Xclang " + Nearest;
      } else {
        DiagID = IsCLMode() ? diag::warn_drv_unknown_argument_clang_cl
                            : diag::err_drv_unknown_argument;
        Diags.Report(DiagID) << ArgString;
      }
    } else {
      DiagID = IsCLMode()
                   ? diag::warn_drv_unknown_argument_clang_cl_with_suggestion
                   : diag::err_drv_unknown_argument_with_suggestion;
      Diags.Report(DiagID) << ArgString << Nearest;
    }
    ContainsError |= Diags.getDiagnosticLevel(DiagID, SourceLocation()) >
                     DiagnosticsEngine::Warning;
  }

  for (const Arg *A : Args.filtered(options::OPT_o)) {
    if (ArgStrings[A->getIndex()] == A->getSpelling())
      continue;

    // Warn on joined arguments that are similar to a long argument.
    std::string ArgString = ArgStrings[A->getIndex()];
    std::string Nearest;
    if (getOpts().findExact("-" + ArgString, Nearest, VisibilityMask))
      Diags.Report(diag::warn_drv_potentially_misspelled_joined_argument)
          << A->getAsString(Args) << Nearest;
  }

  return Args;
}

// Determine which compilation mode we are in. We look for options which
// affect the phase, starting with the earliest phases, and record which
// option we used to determine the final phase.
phases::ID Driver::getFinalPhase(const DerivedArgList &DAL,
                                 Arg **FinalPhaseArg) const {
  Arg *PhaseArg = nullptr;
  phases::ID FinalPhase;

  // -{E,EP,P,M,MM} only run the preprocessor.
  if (CCCIsCPP() || (PhaseArg = DAL.getLastArg(options::OPT_E)) ||
      (PhaseArg = DAL.getLastArg(options::OPT__SLASH_EP)) ||
      (PhaseArg = DAL.getLastArg(options::OPT_M, options::OPT_MM)) ||
      (PhaseArg = DAL.getLastArg(options::OPT__SLASH_P)) ||
      CCGenDiagnostics) {
    FinalPhase = phases::Preprocess;

    // --precompile only runs up to precompilation.
    // Options that cause the output of C++20 compiled module interfaces or
    // header units have the same effect.
  } else if ((PhaseArg = DAL.getLastArg(options::OPT__precompile)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_extract_api)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_fmodule_header,
                                        options::OPT_fmodule_header_EQ))) {
    FinalPhase = phases::Precompile;
    // -{fsyntax-only,-analyze,emit-ast} only run up to the compiler.
  } else if ((PhaseArg = DAL.getLastArg(options::OPT_fsyntax_only)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_print_supported_cpus)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_print_enabled_extensions)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_module_file_info)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_verify_pch)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_rewrite_objc)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_rewrite_legacy_objc)) ||
             (PhaseArg = DAL.getLastArg(options::OPT__migrate)) ||
             (PhaseArg = DAL.getLastArg(options::OPT__analyze)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_emit_cir)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_emit_ast))) {
    FinalPhase = phases::Compile;

  // -S only runs up to the backend.
  } else if ((PhaseArg = DAL.getLastArg(options::OPT_S))) {
    FinalPhase = phases::Backend;

  // -c compilation only runs up to the assembler.
  } else if ((PhaseArg = DAL.getLastArg(options::OPT_c))) {
    FinalPhase = phases::Assemble;

  } else if ((PhaseArg = DAL.getLastArg(options::OPT_emit_interface_stubs))) {
    FinalPhase = phases::IfsMerge;

  // Otherwise do everything.
  } else
    FinalPhase = phases::Link;

  if (FinalPhaseArg)
    *FinalPhaseArg = PhaseArg;

  return FinalPhase;
}

static Arg *MakeInputArg(DerivedArgList &Args, const OptTable &Opts,
                         StringRef Value, bool Claim = true) {
  Arg *A = new Arg(Opts.getOption(options::OPT_INPUT), Value,
                   Args.getBaseArgs().MakeIndex(Value), Value.data());
  Args.AddSynthesizedArg(A);
  if (Claim)
    A->claim();
  return A;
}

DerivedArgList *Driver::TranslateInputArgs(const InputArgList &Args) const {
  const llvm::opt::OptTable &Opts = getOpts();
  DerivedArgList *DAL = new DerivedArgList(Args);

  bool HasNostdlib = Args.hasArg(options::OPT_nostdlib);
  bool HasNostdlibxx = Args.hasArg(options::OPT_nostdlibxx);
  bool HasNodefaultlib = Args.hasArg(options::OPT_nodefaultlibs);
  bool IgnoreUnused = false;
  for (Arg *A : Args) {
    if (IgnoreUnused)
      A->claim();

    if (A->getOption().matches(options::OPT_start_no_unused_arguments)) {
      IgnoreUnused = true;
      continue;
    }
    if (A->getOption().matches(options::OPT_end_no_unused_arguments)) {
      IgnoreUnused = false;
      continue;
    }

    // Unfortunately, we have to parse some forwarding options (-Xassembler,
    // -Xlinker, -Xpreprocessor) because we either integrate their functionality
    // (assembler and preprocessor), or bypass a previous driver ('collect2').

    // Rewrite linker options, to replace --no-demangle with a custom internal
    // option.
    if ((A->getOption().matches(options::OPT_Wl_COMMA) ||
         A->getOption().matches(options::OPT_Xlinker)) &&
        A->containsValue("--no-demangle")) {
      // Add the rewritten no-demangle argument.
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_Z_Xlinker__no_demangle));

      // Add the remaining values as Xlinker arguments.
      for (StringRef Val : A->getValues())
        if (Val != "--no-demangle")
          DAL->AddSeparateArg(A, Opts.getOption(options::OPT_Xlinker), Val);

      continue;
    }

    // Rewrite preprocessor options, to replace -Wp,-MD,FOO which is used by
    // some build systems. We don't try to be complete here because we don't
    // care to encourage this usage model.
    if (A->getOption().matches(options::OPT_Wp_COMMA) &&
        (A->getValue(0) == StringRef("-MD") ||
         A->getValue(0) == StringRef("-MMD"))) {
      // Rewrite to -MD/-MMD along with -MF.
      if (A->getValue(0) == StringRef("-MD"))
        DAL->AddFlagArg(A, Opts.getOption(options::OPT_MD));
      else
        DAL->AddFlagArg(A, Opts.getOption(options::OPT_MMD));
      if (A->getNumValues() == 2)
        DAL->AddSeparateArg(A, Opts.getOption(options::OPT_MF), A->getValue(1));
      continue;
    }

    // Rewrite reserved library names.
    if (A->getOption().matches(options::OPT_l)) {
      StringRef Value = A->getValue();

      // Rewrite unless -nostdlib is present.
      if (!HasNostdlib && !HasNodefaultlib && !HasNostdlibxx &&
          Value == "stdc++") {
        DAL->AddFlagArg(A, Opts.getOption(options::OPT_Z_reserved_lib_stdcxx));
        continue;
      }

      // Rewrite unconditionally.
      if (Value == "cc_kext") {
        DAL->AddFlagArg(A, Opts.getOption(options::OPT_Z_reserved_lib_cckext));
        continue;
      }
    }

    // Pick up inputs via the -- option.
    if (A->getOption().matches(options::OPT__DASH_DASH)) {
      A->claim();
      for (StringRef Val : A->getValues())
        DAL->append(MakeInputArg(*DAL, Opts, Val, false));
      continue;
    }

    DAL->append(A);
  }

  // DXC mode quits before assembly if an output object file isn't specified.
  if (IsDXCMode() && !Args.hasArg(options::OPT_dxc_Fo))
    DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_S));

  // Enforce -static if -miamcu is present.
  if (Args.hasFlag(options::OPT_miamcu, options::OPT_mno_iamcu, false))
    DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_static));

// Add a default value of -mlinker-version=, if one was given and the user
// didn't specify one.
#if defined(HOST_LINK_VERSION)
  if (!Args.hasArg(options::OPT_mlinker_version_EQ) &&
      strlen(HOST_LINK_VERSION) > 0) {
    DAL->AddJoinedArg(0, Opts.getOption(options::OPT_mlinker_version_EQ),
                      HOST_LINK_VERSION);
    DAL->getLastArg(options::OPT_mlinker_version_EQ)->claim();
  }
#endif

  return DAL;
}

/// Compute target triple from args.
///
/// This routine provides the logic to compute a target triple from various
/// args passed to the driver and the default triple string.
static llvm::Triple computeTargetTriple(const Driver &D,
                                        StringRef TargetTriple,
                                        const ArgList &Args,
                                        StringRef DarwinArchName = "") {
  // FIXME: Already done in Compilation *Driver::BuildCompilation
  if (const Arg *A = Args.getLastArg(options::OPT_target))
    TargetTriple = A->getValue();

  llvm::Triple Target(llvm::Triple::normalize(TargetTriple));

  // GNU/Hurd's triples should have been -hurd-gnu*, but were historically made
  // -gnu* only, and we can not change this, so we have to detect that case as
  // being the Hurd OS.
  if (TargetTriple.contains("-unknown-gnu") || TargetTriple.contains("-pc-gnu"))
    Target.setOSName("hurd");

  // Handle Apple-specific options available here.
  if (Target.isOSBinFormatMachO()) {
    // If an explicit Darwin arch name is given, that trumps all.
    if (!DarwinArchName.empty()) {
      tools::darwin::setTripleTypeForMachOArchName(Target, DarwinArchName,
                                                   Args);
      return Target;
    }

    // Handle the Darwin '-arch' flag.
    if (Arg *A = Args.getLastArg(options::OPT_arch)) {
      StringRef ArchName = A->getValue();
      tools::darwin::setTripleTypeForMachOArchName(Target, ArchName, Args);
    }
  }

  // Handle pseudo-target flags '-mlittle-endian'/'-EL' and
  // '-mbig-endian'/'-EB'.
  if (Arg *A = Args.getLastArgNoClaim(options::OPT_mlittle_endian,
                                      options::OPT_mbig_endian)) {
    llvm::Triple T = A->getOption().matches(options::OPT_mlittle_endian)
                         ? Target.getLittleEndianArchVariant()
                         : Target.getBigEndianArchVariant();
    if (T.getArch() != llvm::Triple::UnknownArch) {
      Target = std::move(T);
      Args.claimAllArgs(options::OPT_mlittle_endian, options::OPT_mbig_endian);
    }
  }

  // Skip further flag support on OSes which don't support '-m32' or '-m64'.
  if (Target.getArch() == llvm::Triple::tce)
    return Target;

  // On AIX, the env OBJECT_MODE may affect the resulting arch variant.
  if (Target.isOSAIX()) {
    if (std::optional<std::string> ObjectModeValue =
            llvm::sys::Process::GetEnv("OBJECT_MODE")) {
      StringRef ObjectMode = *ObjectModeValue;
      llvm::Triple::ArchType AT = llvm::Triple::UnknownArch;

      if (ObjectMode == "64") {
        AT = Target.get64BitArchVariant().getArch();
      } else if (ObjectMode == "32") {
        AT = Target.get32BitArchVariant().getArch();
      } else {
        D.Diag(diag::err_drv_invalid_object_mode) << ObjectMode;
      }

      if (AT != llvm::Triple::UnknownArch && AT != Target.getArch())
        Target.setArch(AT);
    }
  }

  // The `-maix[32|64]` flags are only valid for AIX targets.
  if (Arg *A = Args.getLastArgNoClaim(options::OPT_maix32, options::OPT_maix64);
      A && !Target.isOSAIX())
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(Args) << Target.str();

  // Handle pseudo-target flags '-m64', '-mx32', '-m32' and '-m16'.
  Arg *A = Args.getLastArg(options::OPT_m64, options::OPT_mx32,
                           options::OPT_m32, options::OPT_m16,
                           options::OPT_maix32, options::OPT_maix64);
  if (A) {
    llvm::Triple::ArchType AT = llvm::Triple::UnknownArch;

    if (A->getOption().matches(options::OPT_m64) ||
        A->getOption().matches(options::OPT_maix64)) {
      AT = Target.get64BitArchVariant().getArch();
      if (Target.getEnvironment() == llvm::Triple::GNUX32 ||
          Target.getEnvironment() == llvm::Triple::GNUT64)
        Target.setEnvironment(llvm::Triple::GNU);
      else if (Target.getEnvironment() == llvm::Triple::MuslX32)
        Target.setEnvironment(llvm::Triple::Musl);
    } else if (A->getOption().matches(options::OPT_mx32) &&
               Target.get64BitArchVariant().getArch() == llvm::Triple::x86_64) {
      AT = llvm::Triple::x86_64;
      if (Target.getEnvironment() == llvm::Triple::Musl)
        Target.setEnvironment(llvm::Triple::MuslX32);
      else
        Target.setEnvironment(llvm::Triple::GNUX32);
    } else if (A->getOption().matches(options::OPT_m32) ||
               A->getOption().matches(options::OPT_maix32)) {
      AT = Target.get32BitArchVariant().getArch();
      if (Target.getEnvironment() == llvm::Triple::GNUX32)
        Target.setEnvironment(llvm::Triple::GNU);
      else if (Target.getEnvironment() == llvm::Triple::MuslX32)
        Target.setEnvironment(llvm::Triple::Musl);
    } else if (A->getOption().matches(options::OPT_m16) &&
               Target.get32BitArchVariant().getArch() == llvm::Triple::x86) {
      AT = llvm::Triple::x86;
      Target.setEnvironment(llvm::Triple::CODE16);
    }

    if (AT != llvm::Triple::UnknownArch && AT != Target.getArch()) {
      Target.setArch(AT);
      if (Target.isWindowsGNUEnvironment())
        toolchains::MinGW::fixTripleArch(D, Target, Args);
    }
  }

  // Handle -miamcu flag.
  if (Args.hasFlag(options::OPT_miamcu, options::OPT_mno_iamcu, false)) {
    if (Target.get32BitArchVariant().getArch() != llvm::Triple::x86)
      D.Diag(diag::err_drv_unsupported_opt_for_target) << "-miamcu"
                                                       << Target.str();

    if (A && !A->getOption().matches(options::OPT_m32))
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << "-miamcu" << A->getBaseArg().getAsString(Args);

    Target.setArch(llvm::Triple::x86);
    Target.setArchName("i586");
    Target.setEnvironment(llvm::Triple::UnknownEnvironment);
    Target.setEnvironmentName("");
    Target.setOS(llvm::Triple::ELFIAMCU);
    Target.setVendor(llvm::Triple::UnknownVendor);
    Target.setVendorName("intel");
  }

  // If target is MIPS adjust the target triple
  // accordingly to provided ABI name.
  if (Target.isMIPS()) {
    if ((A = Args.getLastArg(options::OPT_mabi_EQ))) {
      StringRef ABIName = A->getValue();
      if (ABIName == "32") {
        Target = Target.get32BitArchVariant();
        if (Target.getEnvironment() == llvm::Triple::GNUABI64 ||
            Target.getEnvironment() == llvm::Triple::GNUABIN32)
          Target.setEnvironment(llvm::Triple::GNU);
      } else if (ABIName == "n32") {
        Target = Target.get64BitArchVariant();
        if (Target.getEnvironment() == llvm::Triple::GNU ||
            Target.getEnvironment() == llvm::Triple::GNUT64 ||
            Target.getEnvironment() == llvm::Triple::GNUABI64)
          Target.setEnvironment(llvm::Triple::GNUABIN32);
      } else if (ABIName == "64") {
        Target = Target.get64BitArchVariant();
        if (Target.getEnvironment() == llvm::Triple::GNU ||
            Target.getEnvironment() == llvm::Triple::GNUT64 ||
            Target.getEnvironment() == llvm::Triple::GNUABIN32)
          Target.setEnvironment(llvm::Triple::GNUABI64);
      }
    }
  }

  // If target is RISC-V adjust the target triple according to
  // provided architecture name
  if (Target.isRISCV()) {
    if (Args.hasArg(options::OPT_march_EQ) ||
        Args.hasArg(options::OPT_mcpu_EQ)) {
      std::string ArchName = tools::riscv::getRISCVArch(Args, Target);
      auto ISAInfo = llvm::RISCVISAInfo::parseArchString(
          ArchName, /*EnableExperimentalExtensions=*/true);
      if (!llvm::errorToBool(ISAInfo.takeError())) {
        unsigned XLen = (*ISAInfo)->getXLen();
        if (XLen == 32)
          Target.setArch(llvm::Triple::riscv32);
        else if (XLen == 64)
          Target.setArch(llvm::Triple::riscv64);
      }
    }
  }

  return Target;
}

// Parse the LTO options and record the type of LTO compilation
// based on which -f(no-)?lto(=.*)? or -f(no-)?offload-lto(=.*)?
// option occurs last.
static driver::LTOKind parseLTOMode(Driver &D, const llvm::opt::ArgList &Args,
                                    OptSpecifier OptEq, OptSpecifier OptNeg) {
  if (!Args.hasFlag(OptEq, OptNeg, false))
    return LTOK_None;

  const Arg *A = Args.getLastArg(OptEq);
  StringRef LTOName = A->getValue();

  driver::LTOKind LTOMode = llvm::StringSwitch<LTOKind>(LTOName)
                                .Case("full", LTOK_Full)
                                .Case("thin", LTOK_Thin)
                                .Default(LTOK_Unknown);

  if (LTOMode == LTOK_Unknown) {
    D.Diag(diag::err_drv_unsupported_option_argument)
        << A->getSpelling() << A->getValue();
    return LTOK_None;
  }
  return LTOMode;
}

// Parse the LTO options.
void Driver::setLTOMode(const llvm::opt::ArgList &Args) {
  LTOMode =
      parseLTOMode(*this, Args, options::OPT_flto_EQ, options::OPT_fno_lto);

  OffloadLTOMode = parseLTOMode(*this, Args, options::OPT_foffload_lto_EQ,
                                options::OPT_fno_offload_lto);

  // Try to enable `-foffload-lto=full` if `-fopenmp-target-jit` is on.
  if (Args.hasFlag(options::OPT_fopenmp_target_jit,
                   options::OPT_fno_openmp_target_jit, false)) {
    if (Arg *A = Args.getLastArg(options::OPT_foffload_lto_EQ,
                                 options::OPT_fno_offload_lto))
      if (OffloadLTOMode != LTOK_Full)
        Diag(diag::err_drv_incompatible_options)
            << A->getSpelling() << "-fopenmp-target-jit";
    OffloadLTOMode = LTOK_Full;
  }
}

/// Compute the desired OpenMP runtime from the flags provided.
Driver::OpenMPRuntimeKind Driver::getOpenMPRuntime(const ArgList &Args) const {
  StringRef RuntimeName(CLANG_DEFAULT_OPENMP_RUNTIME);

  const Arg *A = Args.getLastArg(options::OPT_fopenmp_EQ);
  if (A)
    RuntimeName = A->getValue();

  auto RT = llvm::StringSwitch<OpenMPRuntimeKind>(RuntimeName)
                .Case("libomp", OMPRT_OMP)
                .Case("libgomp", OMPRT_GOMP)
                .Case("libiomp5", OMPRT_IOMP5)
                .Default(OMPRT_Unknown);

  if (RT == OMPRT_Unknown) {
    if (A)
      Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << A->getValue();
    else
      // FIXME: We could use a nicer diagnostic here.
      Diag(diag::err_drv_unsupported_opt) << "-fopenmp";
  }

  return RT;
}

void Driver::CreateOffloadingDeviceToolChains(Compilation &C,
                                              InputList &Inputs) {

  //
  // CUDA/HIP
  //
  // We need to generate a CUDA/HIP toolchain if any of the inputs has a CUDA
  // or HIP type. However, mixed CUDA/HIP compilation is not supported.
  bool IsCuda =
      llvm::any_of(Inputs, [](std::pair<types::ID, const llvm::opt::Arg *> &I) {
        return types::isCuda(I.first);
      });
  bool IsHIP =
      llvm::any_of(Inputs,
                   [](std::pair<types::ID, const llvm::opt::Arg *> &I) {
                     return types::isHIP(I.first);
                   }) ||
      C.getInputArgs().hasArg(options::OPT_hip_link) ||
      C.getInputArgs().hasArg(options::OPT_hipstdpar);
  if (IsCuda && IsHIP) {
    Diag(clang::diag::err_drv_mix_cuda_hip);
    return;
  }
  if (IsCuda) {
    const ToolChain *HostTC = C.getSingleOffloadToolChain<Action::OFK_Host>();
    const llvm::Triple &HostTriple = HostTC->getTriple();
    auto OFK = Action::OFK_Cuda;
    auto CudaTriple =
        getNVIDIAOffloadTargetTriple(*this, C.getInputArgs(), HostTriple);
    if (!CudaTriple)
      return;
    // Use the CUDA and host triples as the key into the ToolChains map,
    // because the device toolchain we create depends on both.
    auto &CudaTC = ToolChains[CudaTriple->str() + "/" + HostTriple.str()];
    if (!CudaTC) {
      CudaTC = std::make_unique<toolchains::CudaToolChain>(
          *this, *CudaTriple, *HostTC, C.getInputArgs());

      // Emit a warning if the detected CUDA version is too new.
      CudaInstallationDetector &CudaInstallation =
          static_cast<toolchains::CudaToolChain &>(*CudaTC).CudaInstallation;
      if (CudaInstallation.isValid())
        CudaInstallation.WarnIfUnsupportedVersion();
    }
    C.addOffloadDeviceToolChain(CudaTC.get(), OFK);
  } else if (IsHIP) {
    if (auto *OMPTargetArg =
            C.getInputArgs().getLastArg(options::OPT_fopenmp_targets_EQ)) {
      Diag(clang::diag::err_drv_unsupported_opt_for_language_mode)
          << OMPTargetArg->getSpelling() << "HIP";
      return;
    }
    const ToolChain *HostTC = C.getSingleOffloadToolChain<Action::OFK_Host>();
    auto OFK = Action::OFK_HIP;
    auto HIPTriple = getHIPOffloadTargetTriple(*this, C.getInputArgs());
    if (!HIPTriple)
      return;
    auto *HIPTC = &getOffloadingDeviceToolChain(C.getInputArgs(), *HIPTriple,
                                                *HostTC, OFK);
    assert(HIPTC && "Could not create offloading device tool chain.");
    C.addOffloadDeviceToolChain(HIPTC, OFK);
  }

  //
  // OpenMP
  //
  // We need to generate an OpenMP toolchain if the user specified targets with
  // the -fopenmp-targets option or used --offload-arch with OpenMP enabled.
  bool IsOpenMPOffloading =
      C.getInputArgs().hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                               options::OPT_fno_openmp, false) &&
      (C.getInputArgs().hasArg(options::OPT_fopenmp_targets_EQ) ||
       C.getInputArgs().hasArg(options::OPT_offload_arch_EQ));
  if (IsOpenMPOffloading) {
    // We expect that -fopenmp-targets is always used in conjunction with the
    // option -fopenmp specifying a valid runtime with offloading support, i.e.
    // libomp or libiomp.
    OpenMPRuntimeKind RuntimeKind = getOpenMPRuntime(C.getInputArgs());
    if (RuntimeKind != OMPRT_OMP && RuntimeKind != OMPRT_IOMP5) {
      Diag(clang::diag::err_drv_expecting_fopenmp_with_fopenmp_targets);
      return;
    }

    llvm::StringMap<llvm::DenseSet<StringRef>> DerivedArchs;
    llvm::StringMap<StringRef> FoundNormalizedTriples;
    std::multiset<StringRef> OpenMPTriples;

    // If the user specified -fopenmp-targets= we create a toolchain for each
    // valid triple. Otherwise, if only --offload-arch= was specified we instead
    // attempt to derive the appropriate toolchains from the arguments.
    if (Arg *OpenMPTargets =
            C.getInputArgs().getLastArg(options::OPT_fopenmp_targets_EQ)) {
      if (OpenMPTargets && !OpenMPTargets->getNumValues()) {
        Diag(clang::diag::warn_drv_empty_joined_argument)
            << OpenMPTargets->getAsString(C.getInputArgs());
        return;
      }
      for (StringRef T : OpenMPTargets->getValues())
        OpenMPTriples.insert(T);
    } else if (C.getInputArgs().hasArg(options::OPT_offload_arch_EQ) &&
               !IsHIP && !IsCuda) {
      const ToolChain *HostTC = C.getSingleOffloadToolChain<Action::OFK_Host>();
      auto AMDTriple = getHIPOffloadTargetTriple(*this, C.getInputArgs());
      auto NVPTXTriple = getNVIDIAOffloadTargetTriple(*this, C.getInputArgs(),
                                                      HostTC->getTriple());

      // Attempt to deduce the offloading triple from the set of architectures.
      // We can only correctly deduce NVPTX / AMDGPU triples currently. We need
      // to temporarily create these toolchains so that we can access tools for
      // inferring architectures.
      llvm::DenseSet<StringRef> Archs;
      if (NVPTXTriple) {
        auto TempTC = std::make_unique<toolchains::CudaToolChain>(
            *this, *NVPTXTriple, *HostTC, C.getInputArgs());
        for (StringRef Arch : getOffloadArchs(
                 C, C.getArgs(), Action::OFK_OpenMP, &*TempTC, true))
          Archs.insert(Arch);
      }
      if (AMDTriple) {
        auto TempTC = std::make_unique<toolchains::AMDGPUOpenMPToolChain>(
            *this, *AMDTriple, *HostTC, C.getInputArgs());
        for (StringRef Arch : getOffloadArchs(
                 C, C.getArgs(), Action::OFK_OpenMP, &*TempTC, true))
          Archs.insert(Arch);
      }
      if (!AMDTriple && !NVPTXTriple) {
        for (StringRef Arch :
             getOffloadArchs(C, C.getArgs(), Action::OFK_OpenMP, nullptr, true))
          Archs.insert(Arch);
      }

      for (StringRef Arch : Archs) {
        if (NVPTXTriple && IsNVIDIAOffloadArch(StringToOffloadArch(
                               getProcessorFromTargetID(*NVPTXTriple, Arch)))) {
          DerivedArchs[NVPTXTriple->getTriple()].insert(Arch);
        } else if (AMDTriple &&
                   IsAMDOffloadArch(StringToOffloadArch(
                       getProcessorFromTargetID(*AMDTriple, Arch)))) {
          DerivedArchs[AMDTriple->getTriple()].insert(Arch);
        } else {
          Diag(clang::diag::err_drv_failed_to_deduce_target_from_arch) << Arch;
          return;
        }
      }

      // If the set is empty then we failed to find a native architecture.
      if (Archs.empty()) {
        Diag(clang::diag::err_drv_failed_to_deduce_target_from_arch)
            << "native";
        return;
      }

      for (const auto &TripleAndArchs : DerivedArchs)
        OpenMPTriples.insert(TripleAndArchs.first());
    }

    for (StringRef Val : OpenMPTriples) {
      llvm::Triple TT(ToolChain::getOpenMPTriple(Val));
      std::string NormalizedName = TT.normalize();

      // Make sure we don't have a duplicate triple.
      auto Duplicate = FoundNormalizedTriples.find(NormalizedName);
      if (Duplicate != FoundNormalizedTriples.end()) {
        Diag(clang::diag::warn_drv_omp_offload_target_duplicate)
            << Val << Duplicate->second;
        continue;
      }

      // Store the current triple so that we can check for duplicates in the
      // following iterations.
      FoundNormalizedTriples[NormalizedName] = Val;

      // If the specified target is invalid, emit a diagnostic.
      if (TT.getArch() == llvm::Triple::UnknownArch)
        Diag(clang::diag::err_drv_invalid_omp_target) << Val;
      else {
        const ToolChain *TC;
        // Device toolchains have to be selected differently. They pair host
        // and device in their implementation.
        if (TT.isNVPTX() || TT.isAMDGCN()) {
          const ToolChain *HostTC =
              C.getSingleOffloadToolChain<Action::OFK_Host>();
          assert(HostTC && "Host toolchain should be always defined.");
          auto &DeviceTC =
              ToolChains[TT.str() + "/" + HostTC->getTriple().normalize()];
          if (!DeviceTC) {
            if (TT.isNVPTX())
              DeviceTC = std::make_unique<toolchains::CudaToolChain>(
                  *this, TT, *HostTC, C.getInputArgs());
            else if (TT.isAMDGCN())
              DeviceTC = std::make_unique<toolchains::AMDGPUOpenMPToolChain>(
                  *this, TT, *HostTC, C.getInputArgs());
            else
              assert(DeviceTC && "Device toolchain not defined.");
          }

          TC = DeviceTC.get();
        } else
          TC = &getToolChain(C.getInputArgs(), TT);
        C.addOffloadDeviceToolChain(TC, Action::OFK_OpenMP);
        if (DerivedArchs.contains(TT.getTriple()))
          KnownArchs[TC] = DerivedArchs[TT.getTriple()];
      }
    }
  } else if (C.getInputArgs().hasArg(options::OPT_fopenmp_targets_EQ)) {
    Diag(clang::diag::err_drv_expecting_fopenmp_with_fopenmp_targets);
    return;
  }

  //
  // TODO: Add support for other offloading programming models here.
  //
}

static void appendOneArg(InputArgList &Args, const Arg *Opt,
                         const Arg *BaseArg) {
  // The args for config files or /clang: flags belong to different InputArgList
  // objects than Args. This copies an Arg from one of those other InputArgLists
  // to the ownership of Args.
  unsigned Index = Args.MakeIndex(Opt->getSpelling());
  Arg *Copy = new llvm::opt::Arg(Opt->getOption(), Args.getArgString(Index),
                                 Index, BaseArg);
  Copy->getValues() = Opt->getValues();
  if (Opt->isClaimed())
    Copy->claim();
  Copy->setOwnsValues(Opt->getOwnsValues());
  Opt->setOwnsValues(false);
  Args.append(Copy);
}

bool Driver::readConfigFile(StringRef FileName,
                            llvm::cl::ExpansionContext &ExpCtx) {
  // Try opening the given file.
  auto Status = getVFS().status(FileName);
  if (!Status) {
    Diag(diag::err_drv_cannot_open_config_file)
        << FileName << Status.getError().message();
    return true;
  }
  if (Status->getType() != llvm::sys::fs::file_type::regular_file) {
    Diag(diag::err_drv_cannot_open_config_file)
        << FileName << "not a regular file";
    return true;
  }

  // Try reading the given file.
  SmallVector<const char *, 32> NewCfgArgs;
  if (llvm::Error Err = ExpCtx.readConfigFile(FileName, NewCfgArgs)) {
    Diag(diag::err_drv_cannot_read_config_file)
        << FileName << toString(std::move(Err));
    return true;
  }

  // Read options from config file.
  llvm::SmallString<128> CfgFileName(FileName);
  llvm::sys::path::native(CfgFileName);
  bool ContainErrors;
  std::unique_ptr<InputArgList> NewOptions = std::make_unique<InputArgList>(
      ParseArgStrings(NewCfgArgs, /*UseDriverMode=*/true, ContainErrors));
  if (ContainErrors)
    return true;

  // Claim all arguments that come from a configuration file so that the driver
  // does not warn on any that is unused.
  for (Arg *A : *NewOptions)
    A->claim();

  if (!CfgOptions)
    CfgOptions = std::move(NewOptions);
  else {
    // If this is a subsequent config file, append options to the previous one.
    for (auto *Opt : *NewOptions) {
      const Arg *BaseArg = &Opt->getBaseArg();
      if (BaseArg == Opt)
        BaseArg = nullptr;
      appendOneArg(*CfgOptions, Opt, BaseArg);
    }
  }
  ConfigFiles.push_back(std::string(CfgFileName));
  return false;
}

bool Driver::loadConfigFiles() {
  llvm::cl::ExpansionContext ExpCtx(Saver.getAllocator(),
                                    llvm::cl::tokenizeConfigFile);
  ExpCtx.setVFS(&getVFS());

  // Process options that change search path for config files.
  if (CLOptions) {
    if (CLOptions->hasArg(options::OPT_config_system_dir_EQ)) {
      SmallString<128> CfgDir;
      CfgDir.append(
          CLOptions->getLastArgValue(options::OPT_config_system_dir_EQ));
      if (CfgDir.empty() || getVFS().makeAbsolute(CfgDir))
        SystemConfigDir.clear();
      else
        SystemConfigDir = static_cast<std::string>(CfgDir);
    }
    if (CLOptions->hasArg(options::OPT_config_user_dir_EQ)) {
      SmallString<128> CfgDir;
      llvm::sys::fs::expand_tilde(
          CLOptions->getLastArgValue(options::OPT_config_user_dir_EQ), CfgDir);
      if (CfgDir.empty() || getVFS().makeAbsolute(CfgDir))
        UserConfigDir.clear();
      else
        UserConfigDir = static_cast<std::string>(CfgDir);
    }
  }

  // Prepare list of directories where config file is searched for.
  StringRef CfgFileSearchDirs[] = {UserConfigDir, SystemConfigDir, Dir};
  ExpCtx.setSearchDirs(CfgFileSearchDirs);

  // First try to load configuration from the default files, return on error.
  if (loadDefaultConfigFiles(ExpCtx))
    return true;

  // Then load configuration files specified explicitly.
  SmallString<128> CfgFilePath;
  if (CLOptions) {
    for (auto CfgFileName : CLOptions->getAllArgValues(options::OPT_config)) {
      // If argument contains directory separator, treat it as a path to
      // configuration file.
      if (llvm::sys::path::has_parent_path(CfgFileName)) {
        CfgFilePath.assign(CfgFileName);
        if (llvm::sys::path::is_relative(CfgFilePath)) {
          if (getVFS().makeAbsolute(CfgFilePath)) {
            Diag(diag::err_drv_cannot_open_config_file)
                << CfgFilePath << "cannot get absolute path";
            return true;
          }
        }
      } else if (!ExpCtx.findConfigFile(CfgFileName, CfgFilePath)) {
        // Report an error that the config file could not be found.
        Diag(diag::err_drv_config_file_not_found) << CfgFileName;
        for (const StringRef &SearchDir : CfgFileSearchDirs)
          if (!SearchDir.empty())
            Diag(diag::note_drv_config_file_searched_in) << SearchDir;
        return true;
      }

      // Try to read the config file, return on error.
      if (readConfigFile(CfgFilePath, ExpCtx))
        return true;
    }
  }

  // No error occurred.
  return false;
}

bool Driver::loadDefaultConfigFiles(llvm::cl::ExpansionContext &ExpCtx) {
  // Disable default config if CLANG_NO_DEFAULT_CONFIG is set to a non-empty
  // value.
  if (const char *NoConfigEnv = ::getenv("CLANG_NO_DEFAULT_CONFIG")) {
    if (*NoConfigEnv)
      return false;
  }
  if (CLOptions && CLOptions->hasArg(options::OPT_no_default_config))
    return false;

  std::string RealMode = getExecutableForDriverMode(Mode);
  std::string Triple;

  // If name prefix is present, no --target= override was passed via CLOptions
  // and the name prefix is not a valid triple, force it for backwards
  // compatibility.
  if (!ClangNameParts.TargetPrefix.empty() &&
      computeTargetTriple(*this, "/invalid/", *CLOptions).str() ==
          "/invalid/") {
    llvm::Triple PrefixTriple{ClangNameParts.TargetPrefix};
    if (PrefixTriple.getArch() == llvm::Triple::UnknownArch ||
        PrefixTriple.isOSUnknown())
      Triple = PrefixTriple.str();
  }

  // Otherwise, use the real triple as used by the driver.
  if (Triple.empty()) {
    llvm::Triple RealTriple =
        computeTargetTriple(*this, TargetTriple, *CLOptions);
    Triple = RealTriple.str();
    assert(!Triple.empty());
  }

  // Search for config files in the following order:
  // 1. <triple>-<mode>.cfg using real driver mode
  //    (e.g. i386-pc-linux-gnu-clang++.cfg).
  // 2. <triple>-<mode>.cfg using executable suffix
  //    (e.g. i386-pc-linux-gnu-clang-g++.cfg for *clang-g++).
  // 3. <triple>.cfg + <mode>.cfg using real driver mode
  //    (e.g. i386-pc-linux-gnu.cfg + clang++.cfg).
  // 4. <triple>.cfg + <mode>.cfg using executable suffix
  //    (e.g. i386-pc-linux-gnu.cfg + clang-g++.cfg for *clang-g++).

  // Try loading <triple>-<mode>.cfg, and return if we find a match.
  SmallString<128> CfgFilePath;
  std::string CfgFileName = Triple + '-' + RealMode + ".cfg";
  if (ExpCtx.findConfigFile(CfgFileName, CfgFilePath))
    return readConfigFile(CfgFilePath, ExpCtx);

  bool TryModeSuffix = !ClangNameParts.ModeSuffix.empty() &&
                       ClangNameParts.ModeSuffix != RealMode;
  if (TryModeSuffix) {
    CfgFileName = Triple + '-' + ClangNameParts.ModeSuffix + ".cfg";
    if (ExpCtx.findConfigFile(CfgFileName, CfgFilePath))
      return readConfigFile(CfgFilePath, ExpCtx);
  }

  // Try loading <mode>.cfg, and return if loading failed.  If a matching file
  // was not found, still proceed on to try <triple>.cfg.
  CfgFileName = RealMode + ".cfg";
  if (ExpCtx.findConfigFile(CfgFileName, CfgFilePath)) {
    if (readConfigFile(CfgFilePath, ExpCtx))
      return true;
  } else if (TryModeSuffix) {
    CfgFileName = ClangNameParts.ModeSuffix + ".cfg";
    if (ExpCtx.findConfigFile(CfgFileName, CfgFilePath) &&
        readConfigFile(CfgFilePath, ExpCtx))
      return true;
  }

  // Try loading <triple>.cfg and return if we find a match.
  CfgFileName = Triple + ".cfg";
  if (ExpCtx.findConfigFile(CfgFileName, CfgFilePath))
    return readConfigFile(CfgFilePath, ExpCtx);

  // If we were unable to find a config file deduced from executable name,
  // that is not an error.
  return false;
}

Compilation *Driver::BuildCompilation(ArrayRef<const char *> ArgList) {
  llvm::PrettyStackTraceString CrashInfo("Compilation construction");

  // FIXME: Handle environment options which affect driver behavior, somewhere
  // (client?). GCC_EXEC_PREFIX, LPATH, CC_PRINT_OPTIONS.

  // We look for the driver mode option early, because the mode can affect
  // how other options are parsed.

  auto DriverMode = getDriverMode(ClangExecutable, ArgList.slice(1));
  if (!DriverMode.empty())
    setDriverMode(DriverMode);

  // FIXME: What are we going to do with -V and -b?

  // Arguments specified in command line.
  bool ContainsError;
  CLOptions = std::make_unique<InputArgList>(
      ParseArgStrings(ArgList.slice(1), /*UseDriverMode=*/true, ContainsError));

  // Try parsing configuration file.
  if (!ContainsError)
    ContainsError = loadConfigFiles();
  bool HasConfigFile = !ContainsError && (CfgOptions.get() != nullptr);

  // All arguments, from both config file and command line.
  InputArgList Args = std::move(HasConfigFile ? std::move(*CfgOptions)
                                              : std::move(*CLOptions));

  if (HasConfigFile)
    for (auto *Opt : *CLOptions) {
      if (Opt->getOption().matches(options::OPT_config))
        continue;
      const Arg *BaseArg = &Opt->getBaseArg();
      if (BaseArg == Opt)
        BaseArg = nullptr;
      appendOneArg(Args, Opt, BaseArg);
    }

  // In CL mode, look for any pass-through arguments
  if (IsCLMode() && !ContainsError) {
    SmallVector<const char *, 16> CLModePassThroughArgList;
    for (const auto *A : Args.filtered(options::OPT__SLASH_clang)) {
      A->claim();
      CLModePassThroughArgList.push_back(A->getValue());
    }

    if (!CLModePassThroughArgList.empty()) {
      // Parse any pass through args using default clang processing rather
      // than clang-cl processing.
      auto CLModePassThroughOptions = std::make_unique<InputArgList>(
          ParseArgStrings(CLModePassThroughArgList, /*UseDriverMode=*/false,
                          ContainsError));

      if (!ContainsError)
        for (auto *Opt : *CLModePassThroughOptions) {
          appendOneArg(Args, Opt, nullptr);
        }
    }
  }

  // Check for working directory option before accessing any files
  if (Arg *WD = Args.getLastArg(options::OPT_working_directory))
    if (VFS->setCurrentWorkingDirectory(WD->getValue()))
      Diag(diag::err_drv_unable_to_set_working_directory) << WD->getValue();

  // Check for missing include directories.
  if (!Diags.isIgnored(diag::warn_missing_include_dirs, SourceLocation())) {
    for (auto IncludeDir : Args.getAllArgValues(options::OPT_I_Group)) {
      if (!VFS->exists(IncludeDir))
        Diag(diag::warn_missing_include_dirs) << IncludeDir;
    }
  }

  // FIXME: This stuff needs to go into the Compilation, not the driver.
  bool CCCPrintPhases;

  // -canonical-prefixes, -no-canonical-prefixes are used very early in main.
  Args.ClaimAllArgs(options::OPT_canonical_prefixes);
  Args.ClaimAllArgs(options::OPT_no_canonical_prefixes);

  // f(no-)integated-cc1 is also used very early in main.
  Args.ClaimAllArgs(options::OPT_fintegrated_cc1);
  Args.ClaimAllArgs(options::OPT_fno_integrated_cc1);

  // Ignore -pipe.
  Args.ClaimAllArgs(options::OPT_pipe);

  // Extract -ccc args.
  //
  // FIXME: We need to figure out where this behavior should live. Most of it
  // should be outside in the client; the parts that aren't should have proper
  // options, either by introducing new ones or by overloading gcc ones like -V
  // or -b.
  CCCPrintPhases = Args.hasArg(options::OPT_ccc_print_phases);
  CCCPrintBindings = Args.hasArg(options::OPT_ccc_print_bindings);
  if (const Arg *A = Args.getLastArg(options::OPT_ccc_gcc_name))
    CCCGenericGCCName = A->getValue();

  // Process -fproc-stat-report options.
  if (const Arg *A = Args.getLastArg(options::OPT_fproc_stat_report_EQ)) {
    CCPrintProcessStats = true;
    CCPrintStatReportFilename = A->getValue();
  }
  if (Args.hasArg(options::OPT_fproc_stat_report))
    CCPrintProcessStats = true;

  // FIXME: TargetTriple is used by the target-prefixed calls to as/ld
  // and getToolChain is const.
  if (IsCLMode()) {
    // clang-cl targets MSVC-style Win32.
    llvm::Triple T(TargetTriple);
    T.setOS(llvm::Triple::Win32);
    T.setVendor(llvm::Triple::PC);
    T.setEnvironment(llvm::Triple::MSVC);
    T.setObjectFormat(llvm::Triple::COFF);
    if (Args.hasArg(options::OPT__SLASH_arm64EC))
      T.setArch(llvm::Triple::aarch64, llvm::Triple::AArch64SubArch_arm64ec);
    TargetTriple = T.str();
  } else if (IsDXCMode()) {
    // Build TargetTriple from target_profile option for clang-dxc.
    if (const Arg *A = Args.getLastArg(options::OPT_target_profile)) {
      StringRef TargetProfile = A->getValue();
      if (auto Triple =
              toolchains::HLSLToolChain::parseTargetProfile(TargetProfile))
        TargetTriple = *Triple;
      else
        Diag(diag::err_drv_invalid_directx_shader_module) << TargetProfile;

      A->claim();

      if (Args.hasArg(options::OPT_spirv)) {
        llvm::Triple T(TargetTriple);
        T.setArch(llvm::Triple::spirv);
        T.setOS(llvm::Triple::Vulkan);

        // Set specific Vulkan version if applicable.
        if (const Arg *A = Args.getLastArg(options::OPT_fspv_target_env_EQ)) {
          const llvm::StringSet<> ValidValues = {"vulkan1.2", "vulkan1.3"};
          if (ValidValues.contains(A->getValue())) {
            T.setOSName(A->getValue());
          } else {
            Diag(diag::err_drv_invalid_value)
                << A->getAsString(Args) << A->getValue();
          }
          A->claim();
        }

        TargetTriple = T.str();
      }
    } else {
      Diag(diag::err_drv_dxc_missing_target_profile);
    }
  }

  if (const Arg *A = Args.getLastArg(options::OPT_target))
    TargetTriple = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT_ccc_install_dir))
    Dir = Dir = A->getValue();
  for (const Arg *A : Args.filtered(options::OPT_B)) {
    A->claim();
    PrefixDirs.push_back(A->getValue(0));
  }
  if (std::optional<std::string> CompilerPathValue =
          llvm::sys::Process::GetEnv("COMPILER_PATH")) {
    StringRef CompilerPath = *CompilerPathValue;
    while (!CompilerPath.empty()) {
      std::pair<StringRef, StringRef> Split =
          CompilerPath.split(llvm::sys::EnvPathSeparator);
      PrefixDirs.push_back(std::string(Split.first));
      CompilerPath = Split.second;
    }
  }
  if (const Arg *A = Args.getLastArg(options::OPT__sysroot_EQ))
    SysRoot = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT__dyld_prefix_EQ))
    DyldPrefix = A->getValue();

  if (const Arg *A = Args.getLastArg(options::OPT_resource_dir))
    ResourceDir = A->getValue();

  if (const Arg *A = Args.getLastArg(options::OPT_save_temps_EQ)) {
    SaveTemps = llvm::StringSwitch<SaveTempsMode>(A->getValue())
                    .Case("cwd", SaveTempsCwd)
                    .Case("obj", SaveTempsObj)
                    .Default(SaveTempsCwd);
  }

  if (const Arg *A = Args.getLastArg(options::OPT_offload_host_only,
                                     options::OPT_offload_device_only,
                                     options::OPT_offload_host_device)) {
    if (A->getOption().matches(options::OPT_offload_host_only))
      Offload = OffloadHost;
    else if (A->getOption().matches(options::OPT_offload_device_only))
      Offload = OffloadDevice;
    else
      Offload = OffloadHostDevice;
  }

  setLTOMode(Args);

  // Process -fembed-bitcode= flags.
  if (Arg *A = Args.getLastArg(options::OPT_fembed_bitcode_EQ)) {
    StringRef Name = A->getValue();
    unsigned Model = llvm::StringSwitch<unsigned>(Name)
        .Case("off", EmbedNone)
        .Case("all", EmbedBitcode)
        .Case("bitcode", EmbedBitcode)
        .Case("marker", EmbedMarker)
        .Default(~0U);
    if (Model == ~0U) {
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args)
                                                << Name;
    } else
      BitcodeEmbed = static_cast<BitcodeEmbedMode>(Model);
  }

  // Remove existing compilation database so that each job can append to it.
  if (Arg *A = Args.getLastArg(options::OPT_MJ))
    llvm::sys::fs::remove(A->getValue());

  // Setting up the jobs for some precompile cases depends on whether we are
  // treating them as PCH, implicit modules or C++20 ones.
  // TODO: inferring the mode like this seems fragile (it meets the objective
  // of not requiring anything new for operation, however).
  const Arg *Std = Args.getLastArg(options::OPT_std_EQ);
  ModulesModeCXX20 =
      !Args.hasArg(options::OPT_fmodules) && Std &&
      (Std->containsValue("c++20") || Std->containsValue("c++2a") ||
       Std->containsValue("c++23") || Std->containsValue("c++2b") ||
       Std->containsValue("c++26") || Std->containsValue("c++2c") ||
       Std->containsValue("c++latest"));

  // Process -fmodule-header{=} flags.
  if (Arg *A = Args.getLastArg(options::OPT_fmodule_header_EQ,
                               options::OPT_fmodule_header)) {
    // These flags force C++20 handling of headers.
    ModulesModeCXX20 = true;
    if (A->getOption().matches(options::OPT_fmodule_header))
      CXX20HeaderType = HeaderMode_Default;
    else {
      StringRef ArgName = A->getValue();
      unsigned Kind = llvm::StringSwitch<unsigned>(ArgName)
                          .Case("user", HeaderMode_User)
                          .Case("system", HeaderMode_System)
                          .Default(~0U);
      if (Kind == ~0U) {
        Diags.Report(diag::err_drv_invalid_value)
            << A->getAsString(Args) << ArgName;
      } else
        CXX20HeaderType = static_cast<ModuleHeaderMode>(Kind);
    }
  }

  std::unique_ptr<llvm::opt::InputArgList> UArgs =
      std::make_unique<InputArgList>(std::move(Args));

  // Perform the default argument translations.
  DerivedArgList *TranslatedArgs = TranslateInputArgs(*UArgs);

  // Owned by the host.
  const ToolChain &TC = getToolChain(
      *UArgs, computeTargetTriple(*this, TargetTriple, *UArgs));

  // Check if the environment version is valid except wasm case.
  llvm::Triple Triple = TC.getTriple();
  if (!Triple.isWasm()) {
    StringRef TripleVersionName = Triple.getEnvironmentVersionString();
    StringRef TripleObjectFormat =
        Triple.getObjectFormatTypeName(Triple.getObjectFormat());
    if (Triple.getEnvironmentVersion().empty() && TripleVersionName != "" &&
        TripleVersionName != TripleObjectFormat) {
      Diags.Report(diag::err_drv_triple_version_invalid)
          << TripleVersionName << TC.getTripleString();
      ContainsError = true;
    }
  }

  // Report warning when arm64EC option is overridden by specified target
  if ((TC.getTriple().getArch() != llvm::Triple::aarch64 ||
       TC.getTriple().getSubArch() != llvm::Triple::AArch64SubArch_arm64ec) &&
      UArgs->hasArg(options::OPT__SLASH_arm64EC)) {
    getDiags().Report(clang::diag::warn_target_override_arm64ec)
        << TC.getTriple().str();
  }

  // A common user mistake is specifying a target of aarch64-none-eabi or
  // arm-none-elf whereas the correct names are aarch64-none-elf &
  // arm-none-eabi. Detect these cases and issue a warning.
  if (TC.getTriple().getOS() == llvm::Triple::UnknownOS &&
      TC.getTriple().getVendor() == llvm::Triple::UnknownVendor) {
    switch (TC.getTriple().getArch()) {
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
      if (TC.getTriple().getEnvironmentName() == "elf") {
        Diag(diag::warn_target_unrecognized_env)
            << TargetTriple
            << (TC.getTriple().getArchName().str() + "-none-eabi");
      }
      break;
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_be:
    case llvm::Triple::aarch64_32:
      if (TC.getTriple().getEnvironmentName().starts_with("eabi")) {
        Diag(diag::warn_target_unrecognized_env)
            << TargetTriple
            << (TC.getTriple().getArchName().str() + "-none-elf");
      }
      break;
    default:
      break;
    }
  }

  // The compilation takes ownership of Args.
  Compilation *C = new Compilation(*this, TC, UArgs.release(), TranslatedArgs,
                                   ContainsError);

  if (!HandleImmediateArgs(*C))
    return C;

  // Construct the list of inputs.
  InputList Inputs;
  BuildInputs(C->getDefaultToolChain(), *TranslatedArgs, Inputs);

  // Populate the tool chains for the offloading devices, if any.
  CreateOffloadingDeviceToolChains(*C, Inputs);

  // Construct the list of abstract actions to perform for this compilation. On
  // MachO targets this uses the driver-driver and universal actions.
  if (TC.getTriple().isOSBinFormatMachO())
    BuildUniversalActions(*C, C->getDefaultToolChain(), Inputs);
  else
    BuildActions(*C, C->getArgs(), Inputs, C->getActions());

  if (CCCPrintPhases) {
    PrintActions(*C);
    return C;
  }

  BuildJobs(*C);

  return C;
}

static void printArgList(raw_ostream &OS, const llvm::opt::ArgList &Args) {
  llvm::opt::ArgStringList ASL;
  for (const auto *A : Args) {
    // Use user's original spelling of flags. For example, use
    // `/source-charset:utf-8` instead of `-finput-charset=utf-8` if the user
    // wrote the former.
    while (A->getAlias())
      A = A->getAlias();
    A->render(Args, ASL);
  }

  for (auto I = ASL.begin(), E = ASL.end(); I != E; ++I) {
    if (I != ASL.begin())
      OS << ' ';
    llvm::sys::printArg(OS, *I, true);
  }
  OS << '\n';
}

bool Driver::getCrashDiagnosticFile(StringRef ReproCrashFilename,
                                    SmallString<128> &CrashDiagDir) {
  using namespace llvm::sys;
  assert(llvm::Triple(llvm::sys::getProcessTriple()).isOSDarwin() &&
         "Only knows about .crash files on Darwin");

  // The .crash file can be found on at ~/Library/Logs/DiagnosticReports/
  // (or /Library/Logs/DiagnosticReports for root) and has the filename pattern
  // clang-<VERSION>_<YYYY-MM-DD-HHMMSS>_<hostname>.crash.
  path::home_directory(CrashDiagDir);
  if (CrashDiagDir.starts_with("/var/root"))
    CrashDiagDir = "/";
  path::append(CrashDiagDir, "Library/Logs/DiagnosticReports");
  int PID =
#if LLVM_ON_UNIX
      getpid();
#else
      0;
#endif
  std::error_code EC;
  fs::file_status FileStatus;
  TimePoint<> LastAccessTime;
  SmallString<128> CrashFilePath;
  // Lookup the .crash files and get the one generated by a subprocess spawned
  // by this driver invocation.
  for (fs::directory_iterator File(CrashDiagDir, EC), FileEnd;
       File != FileEnd && !EC; File.increment(EC)) {
    StringRef FileName = path::filename(File->path());
    if (!FileName.starts_with(Name))
      continue;
    if (fs::status(File->path(), FileStatus))
      continue;
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> CrashFile =
        llvm::MemoryBuffer::getFile(File->path());
    if (!CrashFile)
      continue;
    // The first line should start with "Process:", otherwise this isn't a real
    // .crash file.
    StringRef Data = CrashFile.get()->getBuffer();
    if (!Data.starts_with("Process:"))
      continue;
    // Parse parent process pid line, e.g: "Parent Process: clang-4.0 [79141]"
    size_t ParentProcPos = Data.find("Parent Process:");
    if (ParentProcPos == StringRef::npos)
      continue;
    size_t LineEnd = Data.find_first_of("\n", ParentProcPos);
    if (LineEnd == StringRef::npos)
      continue;
    StringRef ParentProcess = Data.slice(ParentProcPos+15, LineEnd).trim();
    int OpenBracket = -1, CloseBracket = -1;
    for (size_t i = 0, e = ParentProcess.size(); i < e; ++i) {
      if (ParentProcess[i] == '[')
        OpenBracket = i;
      if (ParentProcess[i] == ']')
        CloseBracket = i;
    }
    // Extract the parent process PID from the .crash file and check whether
    // it matches this driver invocation pid.
    int CrashPID;
    if (OpenBracket < 0 || CloseBracket < 0 ||
        ParentProcess.slice(OpenBracket + 1, CloseBracket)
            .getAsInteger(10, CrashPID) || CrashPID != PID) {
      continue;
    }

    // Found a .crash file matching the driver pid. To avoid getting an older
    // and misleading crash file, continue looking for the most recent.
    // FIXME: the driver can dispatch multiple cc1 invocations, leading to
    // multiple crashes poiting to the same parent process. Since the driver
    // does not collect pid information for the dispatched invocation there's
    // currently no way to distinguish among them.
    const auto FileAccessTime = FileStatus.getLastModificationTime();
    if (FileAccessTime > LastAccessTime) {
      CrashFilePath.assign(File->path());
      LastAccessTime = FileAccessTime;
    }
  }

  // If found, copy it over to the location of other reproducer files.
  if (!CrashFilePath.empty()) {
    EC = fs::copy_file(CrashFilePath, ReproCrashFilename);
    if (EC)
      return false;
    return true;
  }

  return false;
}

static const char BugReporMsg[] =
    "\n********************\n\n"
    "PLEASE ATTACH THE FOLLOWING FILES TO THE BUG REPORT:\n"
    "Preprocessed source(s) and associated run script(s) are located at:";

// When clang crashes, produce diagnostic information including the fully
// preprocessed source file(s).  Request that the developer attach the
// diagnostic information to a bug report.
void Driver::generateCompilationDiagnostics(
    Compilation &C, const Command &FailingCommand,
    StringRef AdditionalInformation, CompilationDiagnosticReport *Report) {
  if (C.getArgs().hasArg(options::OPT_fno_crash_diagnostics))
    return;

  unsigned Level = 1;
  if (Arg *A = C.getArgs().getLastArg(options::OPT_fcrash_diagnostics_EQ)) {
    Level = llvm::StringSwitch<unsigned>(A->getValue())
                .Case("off", 0)
                .Case("compiler", 1)
                .Case("all", 2)
                .Default(1);
  }
  if (!Level)
    return;

  // Don't try to generate diagnostics for dsymutil jobs.
  if (FailingCommand.getCreator().isDsymutilJob())
    return;

  bool IsLLD = false;
  ArgStringList SavedTemps;
  if (FailingCommand.getCreator().isLinkJob()) {
    C.getDefaultToolChain().GetLinkerPath(&IsLLD);
    if (!IsLLD || Level < 2)
      return;

    // If lld crashed, we will re-run the same command with the input it used
    // to have. In that case we should not remove temp files in
    // initCompilationForDiagnostics yet. They will be added back and removed
    // later.
    SavedTemps = std::move(C.getTempFiles());
    assert(!C.getTempFiles().size());
  }

  // Print the version of the compiler.
  PrintVersion(C, llvm::errs());

  // Suppress driver output and emit preprocessor output to temp file.
  CCGenDiagnostics = true;

  // Save the original job command(s).
  Command Cmd = FailingCommand;

  // Keep track of whether we produce any errors while trying to produce
  // preprocessed sources.
  DiagnosticErrorTrap Trap(Diags);

  // Suppress tool output.
  C.initCompilationForDiagnostics();

  // If lld failed, rerun it again with --reproduce.
  if (IsLLD) {
    const char *TmpName = CreateTempFile(C, "linker-crash", "tar");
    Command NewLLDInvocation = Cmd;
    llvm::opt::ArgStringList ArgList = NewLLDInvocation.getArguments();
    StringRef ReproduceOption =
        C.getDefaultToolChain().getTriple().isWindowsMSVCEnvironment()
            ? "/reproduce:"
            : "--reproduce=";
    ArgList.push_back(Saver.save(Twine(ReproduceOption) + TmpName).data());
    NewLLDInvocation.replaceArguments(std::move(ArgList));

    // Redirect stdout/stderr to /dev/null.
    NewLLDInvocation.Execute({std::nullopt, {""}, {""}}, nullptr, nullptr);
    Diag(clang::diag::note_drv_command_failed_diag_msg) << BugReporMsg;
    Diag(clang::diag::note_drv_command_failed_diag_msg) << TmpName;
    Diag(clang::diag::note_drv_command_failed_diag_msg)
        << "\n\n********************";
    if (Report)
      Report->TemporaryFiles.push_back(TmpName);
    return;
  }

  // Construct the list of inputs.
  InputList Inputs;
  BuildInputs(C.getDefaultToolChain(), C.getArgs(), Inputs);

  for (InputList::iterator it = Inputs.begin(), ie = Inputs.end(); it != ie;) {
    bool IgnoreInput = false;

    // Ignore input from stdin or any inputs that cannot be preprocessed.
    // Check type first as not all linker inputs have a value.
    if (types::getPreprocessedType(it->first) == types::TY_INVALID) {
      IgnoreInput = true;
    } else if (!strcmp(it->second->getValue(), "-")) {
      Diag(clang::diag::note_drv_command_failed_diag_msg)
          << "Error generating preprocessed source(s) - "
             "ignoring input from stdin.";
      IgnoreInput = true;
    }

    if (IgnoreInput) {
      it = Inputs.erase(it);
      ie = Inputs.end();
    } else {
      ++it;
    }
  }

  if (Inputs.empty()) {
    Diag(clang::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s) - "
           "no preprocessable inputs.";
    return;
  }

  // Don't attempt to generate preprocessed files if multiple -arch options are
  // used, unless they're all duplicates.
  llvm::StringSet<> ArchNames;
  for (const Arg *A : C.getArgs()) {
    if (A->getOption().matches(options::OPT_arch)) {
      StringRef ArchName = A->getValue();
      ArchNames.insert(ArchName);
    }
  }
  if (ArchNames.size() > 1) {
    Diag(clang::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s) - cannot generate "
           "preprocessed source with multiple -arch options.";
    return;
  }

  // Construct the list of abstract actions to perform for this compilation. On
  // Darwin OSes this uses the driver-driver and builds universal actions.
  const ToolChain &TC = C.getDefaultToolChain();
  if (TC.getTriple().isOSBinFormatMachO())
    BuildUniversalActions(C, TC, Inputs);
  else
    BuildActions(C, C.getArgs(), Inputs, C.getActions());

  BuildJobs(C);

  // If there were errors building the compilation, quit now.
  if (Trap.hasErrorOccurred()) {
    Diag(clang::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s).";
    return;
  }

  // Generate preprocessed output.
  SmallVector<std::pair<int, const Command *>, 4> FailingCommands;
  C.ExecuteJobs(C.getJobs(), FailingCommands);

  // If any of the preprocessing commands failed, clean up and exit.
  if (!FailingCommands.empty()) {
    Diag(clang::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s).";
    return;
  }

  const ArgStringList &TempFiles = C.getTempFiles();
  if (TempFiles.empty()) {
    Diag(clang::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s).";
    return;
  }

  Diag(clang::diag::note_drv_command_failed_diag_msg) << BugReporMsg;

  SmallString<128> VFS;
  SmallString<128> ReproCrashFilename;
  for (const char *TempFile : TempFiles) {
    Diag(clang::diag::note_drv_command_failed_diag_msg) << TempFile;
    if (Report)
      Report->TemporaryFiles.push_back(TempFile);
    if (ReproCrashFilename.empty()) {
      ReproCrashFilename = TempFile;
      llvm::sys::path::replace_extension(ReproCrashFilename, ".crash");
    }
    if (StringRef(TempFile).ends_with(".cache")) {
      // In some cases (modules) we'll dump extra data to help with reproducing
      // the crash into a directory next to the output.
      VFS = llvm::sys::path::filename(TempFile);
      llvm::sys::path::append(VFS, "vfs", "vfs.yaml");
    }
  }

  for (const char *TempFile : SavedTemps)
    C.addTempFile(TempFile);

  // Assume associated files are based off of the first temporary file.
  CrashReportInfo CrashInfo(TempFiles[0], VFS);

  llvm::SmallString<128> Script(CrashInfo.Filename);
  llvm::sys::path::replace_extension(Script, "sh");
  std::error_code EC;
  llvm::raw_fd_ostream ScriptOS(Script, EC, llvm::sys::fs::CD_CreateNew,
                                llvm::sys::fs::FA_Write,
                                llvm::sys::fs::OF_Text);
  if (EC) {
    Diag(clang::diag::note_drv_command_failed_diag_msg)
        << "Error generating run script: " << Script << " " << EC.message();
  } else {
    ScriptOS << "# Crash reproducer for " << getClangFullVersion() << "\n"
             << "# Driver args: ";
    printArgList(ScriptOS, C.getInputArgs());
    ScriptOS << "# Original command: ";
    Cmd.Print(ScriptOS, "\n", /*Quote=*/true);
    Cmd.Print(ScriptOS, "\n", /*Quote=*/true, &CrashInfo);
    if (!AdditionalInformation.empty())
      ScriptOS << "\n# Additional information: " << AdditionalInformation
               << "\n";
    if (Report)
      Report->TemporaryFiles.push_back(std::string(Script));
    Diag(clang::diag::note_drv_command_failed_diag_msg) << Script;
  }

  // On darwin, provide information about the .crash diagnostic report.
  if (llvm::Triple(llvm::sys::getProcessTriple()).isOSDarwin()) {
    SmallString<128> CrashDiagDir;
    if (getCrashDiagnosticFile(ReproCrashFilename, CrashDiagDir)) {
      Diag(clang::diag::note_drv_command_failed_diag_msg)
          << ReproCrashFilename.str();
    } else { // Suggest a directory for the user to look for .crash files.
      llvm::sys::path::append(CrashDiagDir, Name);
      CrashDiagDir += "_<YYYY-MM-DD-HHMMSS>_<hostname>.crash";
      Diag(clang::diag::note_drv_command_failed_diag_msg)
          << "Crash backtrace is located in";
      Diag(clang::diag::note_drv_command_failed_diag_msg)
          << CrashDiagDir.str();
      Diag(clang::diag::note_drv_command_failed_diag_msg)
          << "(choose the .crash file that corresponds to your crash)";
    }
  }

  Diag(clang::diag::note_drv_command_failed_diag_msg)
      << "\n\n********************";
}

void Driver::setUpResponseFiles(Compilation &C, Command &Cmd) {
  // Since commandLineFitsWithinSystemLimits() may underestimate system's
  // capacity if the tool does not support response files, there is a chance/
  // that things will just work without a response file, so we silently just
  // skip it.
  if (Cmd.getResponseFileSupport().ResponseKind ==
          ResponseFileSupport::RF_None ||
      llvm::sys::commandLineFitsWithinSystemLimits(Cmd.getExecutable(),
                                                   Cmd.getArguments()))
    return;

  std::string TmpName = GetTemporaryPath("response", "txt");
  Cmd.setResponseFile(C.addTempFile(C.getArgs().MakeArgString(TmpName)));
}

int Driver::ExecuteCompilation(
    Compilation &C,
    SmallVectorImpl<std::pair<int, const Command *>> &FailingCommands) {
  if (C.getArgs().hasArg(options::OPT_fdriver_only)) {
    if (C.getArgs().hasArg(options::OPT_v))
      C.getJobs().Print(llvm::errs(), "\n", true);

    C.ExecuteJobs(C.getJobs(), FailingCommands, /*LogOnly=*/true);

    // If there were errors building the compilation, quit now.
    if (!FailingCommands.empty() || Diags.hasErrorOccurred())
      return 1;

    return 0;
  }

  // Just print if -### was present.
  if (C.getArgs().hasArg(options::OPT__HASH_HASH_HASH)) {
    C.getJobs().Print(llvm::errs(), "\n", true);
    return Diags.hasErrorOccurred() ? 1 : 0;
  }

  // If there were errors building the compilation, quit now.
  if (Diags.hasErrorOccurred())
    return 1;

  // Set up response file names for each command, if necessary.
  for (auto &Job : C.getJobs())
    setUpResponseFiles(C, Job);

  C.ExecuteJobs(C.getJobs(), FailingCommands);

  // If the command succeeded, we are done.
  if (FailingCommands.empty())
    return 0;

  // Otherwise, remove result files and print extra information about abnormal
  // failures.
  int Res = 0;
  for (const auto &CmdPair : FailingCommands) {
    int CommandRes = CmdPair.first;
    const Command *FailingCommand = CmdPair.second;

    // Remove result files if we're not saving temps.
    if (!isSaveTempsEnabled()) {
      const JobAction *JA = cast<JobAction>(&FailingCommand->getSource());
      C.CleanupFileMap(C.getResultFiles(), JA, true);

      // Failure result files are valid unless we crashed.
      if (CommandRes < 0)
        C.CleanupFileMap(C.getFailureResultFiles(), JA, true);
    }

    // llvm/lib/Support/*/Signals.inc will exit with a special return code
    // for SIGPIPE. Do not print diagnostics for this case.
    if (CommandRes == EX_IOERR) {
      Res = CommandRes;
      continue;
    }

    // Print extra information about abnormal failures, if possible.
    //
    // This is ad-hoc, but we don't want to be excessively noisy. If the result
    // status was 1, assume the command failed normally. In particular, if it
    // was the compiler then assume it gave a reasonable error code. Failures
    // in other tools are less common, and they generally have worse
    // diagnostics, so always print the diagnostic there.
    const Tool &FailingTool = FailingCommand->getCreator();

    if (!FailingCommand->getCreator().hasGoodDiagnostics() || CommandRes != 1) {
      // FIXME: See FIXME above regarding result code interpretation.
      if (CommandRes < 0)
        Diag(clang::diag::err_drv_command_signalled)
            << FailingTool.getShortName();
      else
        Diag(clang::diag::err_drv_command_failed)
            << FailingTool.getShortName() << CommandRes;
    }
  }
  return Res;
}

void Driver::PrintHelp(bool ShowHidden) const {
  llvm::opt::Visibility VisibilityMask = getOptionVisibilityMask();

  std::string Usage = llvm::formatv("{0} [options] file...", Name).str();
  getOpts().printHelp(llvm::outs(), Usage.c_str(), DriverTitle.c_str(),
                      ShowHidden, /*ShowAllAliases=*/false,
                      VisibilityMask);
}

void Driver::PrintVersion(const Compilation &C, raw_ostream &OS) const {
  if (IsFlangMode()) {
    OS << getClangToolFullVersion("flang-new") << '\n';
  } else {
    // FIXME: The following handlers should use a callback mechanism, we don't
    // know what the client would like to do.
    OS << getClangFullVersion() << '\n';
  }
  const ToolChain &TC = C.getDefaultToolChain();
  OS << "Target: " << TC.getTripleString() << '\n';

  // Print the threading model.
  if (Arg *A = C.getArgs().getLastArg(options::OPT_mthread_model)) {
    // Don't print if the ToolChain would have barfed on it already
    if (TC.isThreadModelSupported(A->getValue()))
      OS << "Thread model: " << A->getValue();
  } else
    OS << "Thread model: " << TC.getThreadModel();
  OS << '\n';

  // Print out the install directory.
  OS << "InstalledDir: " << Dir << '\n';

  // Print the build config if it's non-default.
  // Intended to help LLVM developers understand the configs of compilers
  // they're investigating.
  if (!llvm::cl::getCompilerBuildConfig().empty())
    llvm::cl::printBuildConfig(OS);

  // If configuration files were used, print their paths.
  for (auto ConfigFile : ConfigFiles)
    OS << "Configuration file: " << ConfigFile << '\n';
}

/// PrintDiagnosticCategories - Implement the --print-diagnostic-categories
/// option.
static void PrintDiagnosticCategories(raw_ostream &OS) {
  // Skip the empty category.
  for (unsigned i = 1, max = DiagnosticIDs::getNumberOfCategories(); i != max;
       ++i)
    OS << i << ',' << DiagnosticIDs::getCategoryNameFromID(i) << '\n';
}

void Driver::HandleAutocompletions(StringRef PassedFlags) const {
  if (PassedFlags == "")
    return;
  // Print out all options that start with a given argument. This is used for
  // shell autocompletion.
  std::vector<std::string> SuggestedCompletions;
  std::vector<std::string> Flags;

  llvm::opt::Visibility VisibilityMask(options::ClangOption);

  // Make sure that Flang-only options don't pollute the Clang output
  // TODO: Make sure that Clang-only options don't pollute Flang output
  if (IsFlangMode())
    VisibilityMask = llvm::opt::Visibility(options::FlangOption);

  // Distinguish "--autocomplete=-someflag" and "--autocomplete=-someflag,"
  // because the latter indicates that the user put space before pushing tab
  // which should end up in a file completion.
  const bool HasSpace = PassedFlags.ends_with(",");

  // Parse PassedFlags by "," as all the command-line flags are passed to this
  // function separated by ","
  StringRef TargetFlags = PassedFlags;
  while (TargetFlags != "") {
    StringRef CurFlag;
    std::tie(CurFlag, TargetFlags) = TargetFlags.split(",");
    Flags.push_back(std::string(CurFlag));
  }

  // We want to show cc1-only options only when clang is invoked with -cc1 or
  // -Xclang.
  if (llvm::is_contained(Flags, "-Xclang") || llvm::is_contained(Flags, "-cc1"))
    VisibilityMask = llvm::opt::Visibility(options::CC1Option);

  const llvm::opt::OptTable &Opts = getOpts();
  StringRef Cur;
  Cur = Flags.at(Flags.size() - 1);
  StringRef Prev;
  if (Flags.size() >= 2) {
    Prev = Flags.at(Flags.size() - 2);
    SuggestedCompletions = Opts.suggestValueCompletions(Prev, Cur);
  }

  if (SuggestedCompletions.empty())
    SuggestedCompletions = Opts.suggestValueCompletions(Cur, "");

  // If Flags were empty, it means the user typed `clang [tab]` where we should
  // list all possible flags. If there was no value completion and the user
  // pressed tab after a space, we should fall back to a file completion.
  // We're printing a newline to be consistent with what we print at the end of
  // this function.
  if (SuggestedCompletions.empty() && HasSpace && !Flags.empty()) {
    llvm::outs() << '\n';
    return;
  }

  // When flag ends with '=' and there was no value completion, return empty
  // string and fall back to the file autocompletion.
  if (SuggestedCompletions.empty() && !Cur.ends_with("=")) {
    // If the flag is in the form of "--autocomplete=-foo",
    // we were requested to print out all option names that start with "-foo".
    // For example, "--autocomplete=-fsyn" is expanded to "-fsyntax-only".
    SuggestedCompletions = Opts.findByPrefix(
        Cur, VisibilityMask,
        /*DisableFlags=*/options::Unsupported | options::Ignored);

    // We have to query the -W flags manually as they're not in the OptTable.
    // TODO: Find a good way to add them to OptTable instead and them remove
    // this code.
    for (StringRef S : DiagnosticIDs::getDiagnosticFlags())
      if (S.starts_with(Cur))
        SuggestedCompletions.push_back(std::string(S));
  }

  // Sort the autocomplete candidates so that shells print them out in a
  // deterministic order. We could sort in any way, but we chose
  // case-insensitive sorting for consistency with the -help option
  // which prints out options in the case-insensitive alphabetical order.
  llvm::sort(SuggestedCompletions, [](StringRef A, StringRef B) {
    if (int X = A.compare_insensitive(B))
      return X < 0;
    return A.compare(B) > 0;
  });

  llvm::outs() << llvm::join(SuggestedCompletions, "\n") << '\n';
}

bool Driver::HandleImmediateArgs(Compilation &C) {
  // The order these options are handled in gcc is all over the place, but we
  // don't expect inconsistencies w.r.t. that to matter in practice.

  if (C.getArgs().hasArg(options::OPT_dumpmachine)) {
    llvm::outs() << C.getDefaultToolChain().getTripleString() << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_dumpversion)) {
    // Since -dumpversion is only implemented for pedantic GCC compatibility, we
    // return an answer which matches our definition of __VERSION__.
    llvm::outs() << CLANG_VERSION_STRING << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT__print_diagnostic_categories)) {
    PrintDiagnosticCategories(llvm::outs());
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_help) ||
      C.getArgs().hasArg(options::OPT__help_hidden)) {
    PrintHelp(C.getArgs().hasArg(options::OPT__help_hidden));
    return false;
  }

  if (C.getArgs().hasArg(options::OPT__version)) {
    // Follow gcc behavior and use stdout for --version and stderr for -v.
    PrintVersion(C, llvm::outs());
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_v) ||
      C.getArgs().hasArg(options::OPT__HASH_HASH_HASH) ||
      C.getArgs().hasArg(options::OPT_print_supported_cpus) ||
      C.getArgs().hasArg(options::OPT_print_supported_extensions) ||
      C.getArgs().hasArg(options::OPT_print_enabled_extensions)) {
    PrintVersion(C, llvm::errs());
    SuppressMissingInputWarning = true;
  }

  if (C.getArgs().hasArg(options::OPT_v)) {
    if (!SystemConfigDir.empty())
      llvm::errs() << "System configuration file directory: "
                   << SystemConfigDir << "\n";
    if (!UserConfigDir.empty())
      llvm::errs() << "User configuration file directory: "
                   << UserConfigDir << "\n";
  }

  const ToolChain &TC = C.getDefaultToolChain();

  if (C.getArgs().hasArg(options::OPT_v))
    TC.printVerboseInfo(llvm::errs());

  if (C.getArgs().hasArg(options::OPT_print_resource_dir)) {
    llvm::outs() << ResourceDir << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_search_dirs)) {
    llvm::outs() << "programs: =";
    bool separator = false;
    // Print -B and COMPILER_PATH.
    for (const std::string &Path : PrefixDirs) {
      if (separator)
        llvm::outs() << llvm::sys::EnvPathSeparator;
      llvm::outs() << Path;
      separator = true;
    }
    for (const std::string &Path : TC.getProgramPaths()) {
      if (separator)
        llvm::outs() << llvm::sys::EnvPathSeparator;
      llvm::outs() << Path;
      separator = true;
    }
    llvm::outs() << "\n";
    llvm::outs() << "libraries: =" << ResourceDir;

    StringRef sysroot = C.getSysRoot();

    for (const std::string &Path : TC.getFilePaths()) {
      // Always print a separator. ResourceDir was the first item shown.
      llvm::outs() << llvm::sys::EnvPathSeparator;
      // Interpretation of leading '=' is needed only for NetBSD.
      if (Path[0] == '=')
        llvm::outs() << sysroot << Path.substr(1);
      else
        llvm::outs() << Path;
    }
    llvm::outs() << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_std_module_manifest_path)) {
    llvm::outs() << GetStdModuleManifestPath(C, C.getDefaultToolChain())
                 << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_runtime_dir)) {
    if (std::optional<std::string> RuntimePath = TC.getRuntimePath())
      llvm::outs() << *RuntimePath << '\n';
    else
      llvm::outs() << TC.getCompilerRTPath() << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_diagnostic_options)) {
    std::vector<std::string> Flags = DiagnosticIDs::getDiagnosticFlags();
    for (std::size_t I = 0; I != Flags.size(); I += 2)
      llvm::outs() << "  " << Flags[I] << "\n  " << Flags[I + 1] << "\n\n";
    return false;
  }

  // FIXME: The following handlers should use a callback mechanism, we don't
  // know what the client would like to do.
  if (Arg *A = C.getArgs().getLastArg(options::OPT_print_file_name_EQ)) {
    llvm::outs() << GetFilePath(A->getValue(), TC) << "\n";
    return false;
  }

  if (Arg *A = C.getArgs().getLastArg(options::OPT_print_prog_name_EQ)) {
    StringRef ProgName = A->getValue();

    // Null program name cannot have a path.
    if (! ProgName.empty())
      llvm::outs() << GetProgramPath(ProgName, TC);

    llvm::outs() << "\n";
    return false;
  }

  if (Arg *A = C.getArgs().getLastArg(options::OPT_autocomplete)) {
    StringRef PassedFlags = A->getValue();
    HandleAutocompletions(PassedFlags);
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_libgcc_file_name)) {
    ToolChain::RuntimeLibType RLT = TC.GetRuntimeLibType(C.getArgs());
    const llvm::Triple Triple(TC.ComputeEffectiveClangTriple(C.getArgs()));
    // The 'Darwin' toolchain is initialized only when its arguments are
    // computed. Get the default arguments for OFK_None to ensure that
    // initialization is performed before trying to access properties of
    // the toolchain in the functions below.
    // FIXME: Remove when darwin's toolchain is initialized during construction.
    // FIXME: For some more esoteric targets the default toolchain is not the
    //        correct one.
    C.getArgsForToolChain(&TC, Triple.getArchName(), Action::OFK_None);
    RegisterEffectiveTriple TripleRAII(TC, Triple);
    switch (RLT) {
    case ToolChain::RLT_CompilerRT:
      llvm::outs() << TC.getCompilerRT(C.getArgs(), "builtins") << "\n";
      break;
    case ToolChain::RLT_Libgcc:
      llvm::outs() << GetFilePath("libgcc.a", TC) << "\n";
      break;
    }
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_multi_lib)) {
    for (const Multilib &Multilib : TC.getMultilibs())
      llvm::outs() << Multilib << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_multi_flags)) {
    Multilib::flags_list ArgFlags = TC.getMultilibFlags(C.getArgs());
    llvm::StringSet<> ExpandedFlags = TC.getMultilibs().expandFlags(ArgFlags);
    std::set<llvm::StringRef> SortedFlags;
    for (const auto &FlagEntry : ExpandedFlags)
      SortedFlags.insert(FlagEntry.getKey());
    for (auto Flag : SortedFlags)
      llvm::outs() << Flag << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_multi_directory)) {
    for (const Multilib &Multilib : TC.getSelectedMultilibs()) {
      if (Multilib.gccSuffix().empty())
        llvm::outs() << ".\n";
      else {
        StringRef Suffix(Multilib.gccSuffix());
        assert(Suffix.front() == '/');
        llvm::outs() << Suffix.substr(1) << "\n";
      }
    }
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_target_triple)) {
    llvm::outs() << TC.getTripleString() << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_effective_triple)) {
    const llvm::Triple Triple(TC.ComputeEffectiveClangTriple(C.getArgs()));
    llvm::outs() << Triple.getTriple() << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_targets)) {
    llvm::TargetRegistry::printRegisteredTargetsForVersion(llvm::outs());
    return false;
  }

  return true;
}

enum {
  TopLevelAction = 0,
  HeadSibAction = 1,
  OtherSibAction = 2,
};

// Display an action graph human-readably.  Action A is the "sink" node
// and latest-occuring action. Traversal is in pre-order, visiting the
// inputs to each action before printing the action itself.
static unsigned PrintActions1(const Compilation &C, Action *A,
                              std::map<Action *, unsigned> &Ids,
                              Twine Indent = {}, int Kind = TopLevelAction) {
  if (Ids.count(A)) // A was already visited.
    return Ids[A];

  std::string str;
  llvm::raw_string_ostream os(str);

  auto getSibIndent = [](int K) -> Twine {
    return (K == HeadSibAction) ? "   " : (K == OtherSibAction) ? "|  " : "";
  };

  Twine SibIndent = Indent + getSibIndent(Kind);
  int SibKind = HeadSibAction;
  os << Action::getClassName(A->getKind()) << ", ";
  if (InputAction *IA = dyn_cast<InputAction>(A)) {
    os << "\"" << IA->getInputArg().getValue() << "\"";
  } else if (BindArchAction *BIA = dyn_cast<BindArchAction>(A)) {
    os << '"' << BIA->getArchName() << '"' << ", {"
       << PrintActions1(C, *BIA->input_begin(), Ids, SibIndent, SibKind) << "}";
  } else if (OffloadAction *OA = dyn_cast<OffloadAction>(A)) {
    bool IsFirst = true;
    OA->doOnEachDependence(
        [&](Action *A, const ToolChain *TC, const char *BoundArch) {
          assert(TC && "Unknown host toolchain");
          // E.g. for two CUDA device dependences whose bound arch is sm_20 and
          // sm_35 this will generate:
          // "cuda-device" (nvptx64-nvidia-cuda:sm_20) {#ID}, "cuda-device"
          // (nvptx64-nvidia-cuda:sm_35) {#ID}
          if (!IsFirst)
            os << ", ";
          os << '"';
          os << A->getOffloadingKindPrefix();
          os << " (";
          os << TC->getTriple().normalize();
          if (BoundArch)
            os << ":" << BoundArch;
          os << ")";
          os << '"';
          os << " {" << PrintActions1(C, A, Ids, SibIndent, SibKind) << "}";
          IsFirst = false;
          SibKind = OtherSibAction;
        });
  } else {
    const ActionList *AL = &A->getInputs();

    if (AL->size()) {
      const char *Prefix = "{";
      for (Action *PreRequisite : *AL) {
        os << Prefix << PrintActions1(C, PreRequisite, Ids, SibIndent, SibKind);
        Prefix = ", ";
        SibKind = OtherSibAction;
      }
      os << "}";
    } else
      os << "{}";
  }

  // Append offload info for all options other than the offloading action
  // itself (e.g. (cuda-device, sm_20) or (cuda-host)).
  std::string offload_str;
  llvm::raw_string_ostream offload_os(offload_str);
  if (!isa<OffloadAction>(A)) {
    auto S = A->getOffloadingKindPrefix();
    if (!S.empty()) {
      offload_os << ", (" << S;
      if (A->getOffloadingArch())
        offload_os << ", " << A->getOffloadingArch();
      offload_os << ")";
    }
  }

  auto getSelfIndent = [](int K) -> Twine {
    return (K == HeadSibAction) ? "+- " : (K == OtherSibAction) ? "|- " : "";
  };

  unsigned Id = Ids.size();
  Ids[A] = Id;
  llvm::errs() << Indent + getSelfIndent(Kind) << Id << ": " << os.str() << ", "
               << types::getTypeName(A->getType()) << offload_os.str() << "\n";

  return Id;
}

// Print the action graphs in a compilation C.
// For example "clang -c file1.c file2.c" is composed of two subgraphs.
void Driver::PrintActions(const Compilation &C) const {
  std::map<Action *, unsigned> Ids;
  for (Action *A : C.getActions())
    PrintActions1(C, A, Ids);
}

/// Check whether the given input tree contains any compilation or
/// assembly actions.
static bool ContainsCompileOrAssembleAction(const Action *A) {
  if (isa<CompileJobAction>(A) || isa<BackendJobAction>(A) ||
      isa<AssembleJobAction>(A))
    return true;

  return llvm::any_of(A->inputs(), ContainsCompileOrAssembleAction);
}

void Driver::BuildUniversalActions(Compilation &C, const ToolChain &TC,
                                   const InputList &BAInputs) const {
  DerivedArgList &Args = C.getArgs();
  ActionList &Actions = C.getActions();
  llvm::PrettyStackTraceString CrashInfo("Building universal build actions");
  // Collect the list of architectures. Duplicates are allowed, but should only
  // be handled once (in the order seen).
  llvm::StringSet<> ArchNames;
  SmallVector<const char *, 4> Archs;
  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_arch)) {
      // Validate the option here; we don't save the type here because its
      // particular spelling may participate in other driver choices.
      llvm::Triple::ArchType Arch =
          tools::darwin::getArchTypeForMachOArchName(A->getValue());
      if (Arch == llvm::Triple::UnknownArch) {
        Diag(clang::diag::err_drv_invalid_arch_name) << A->getAsString(Args);
        continue;
      }

      A->claim();
      if (ArchNames.insert(A->getValue()).second)
        Archs.push_back(A->getValue());
    }
  }

  // When there is no explicit arch for this platform, make sure we still bind
  // the architecture (to the default) so that -Xarch_ is handled correctly.
  if (!Archs.size())
    Archs.push_back(Args.MakeArgString(TC.getDefaultUniversalArchName()));

  ActionList SingleActions;
  BuildActions(C, Args, BAInputs, SingleActions);

  // Add in arch bindings for every top level action, as well as lipo and
  // dsymutil steps if needed.
  for (Action* Act : SingleActions) {
    // Make sure we can lipo this kind of output. If not (and it is an actual
    // output) then we disallow, since we can't create an output file with the
    // right name without overwriting it. We could remove this oddity by just
    // changing the output names to include the arch, which would also fix
    // -save-temps. Compatibility wins for now.

    if (Archs.size() > 1 && !types::canLipoType(Act->getType()))
      Diag(clang::diag::err_drv_invalid_output_with_multiple_archs)
          << types::getTypeName(Act->getType());

    ActionList Inputs;
    for (unsigned i = 0, e = Archs.size(); i != e; ++i)
      Inputs.push_back(C.MakeAction<BindArchAction>(Act, Archs[i]));

    // Lipo if necessary, we do it this way because we need to set the arch flag
    // so that -Xarch_ gets overwritten.
    if (Inputs.size() == 1 || Act->getType() == types::TY_Nothing)
      Actions.append(Inputs.begin(), Inputs.end());
    else
      Actions.push_back(C.MakeAction<LipoJobAction>(Inputs, Act->getType()));

    // Handle debug info queries.
    Arg *A = Args.getLastArg(options::OPT_g_Group);
    bool enablesDebugInfo = A && !A->getOption().matches(options::OPT_g0) &&
                            !A->getOption().matches(options::OPT_gstabs);
    if ((enablesDebugInfo || willEmitRemarks(Args)) &&
        ContainsCompileOrAssembleAction(Actions.back())) {

      // Add a 'dsymutil' step if necessary, when debug info is enabled and we
      // have a compile input. We need to run 'dsymutil' ourselves in such cases
      // because the debug info will refer to a temporary object file which
      // will be removed at the end of the compilation process.
      if (Act->getType() == types::TY_Image) {
        ActionList Inputs;
        Inputs.push_back(Actions.back());
        Actions.pop_back();
        Actions.push_back(
            C.MakeAction<DsymutilJobAction>(Inputs, types::TY_dSYM));
      }

      // Verify the debug info output.
      if (Args.hasArg(options::OPT_verify_debug_info)) {
        Action* LastAction = Actions.back();
        Actions.pop_back();
        Actions.push_back(C.MakeAction<VerifyDebugInfoJobAction>(
            LastAction, types::TY_Nothing));
      }
    }
  }
}

bool Driver::DiagnoseInputExistence(const DerivedArgList &Args, StringRef Value,
                                    types::ID Ty, bool TypoCorrect) const {
  if (!getCheckInputsExist())
    return true;

  // stdin always exists.
  if (Value == "-")
    return true;

  // If it's a header to be found in the system or user search path, then defer
  // complaints about its absence until those searches can be done.  When we
  // are definitely processing headers for C++20 header units, extend this to
  // allow the user to put "-fmodule-header -xc++-header vector" for example.
  if (Ty == types::TY_CXXSHeader || Ty == types::TY_CXXUHeader ||
      (ModulesModeCXX20 && Ty == types::TY_CXXHeader))
    return true;

  if (getVFS().exists(Value))
    return true;

  if (TypoCorrect) {
    // Check if the filename is a typo for an option flag. OptTable thinks
    // that all args that are not known options and that start with / are
    // filenames, but e.g. `/diagnostic:caret` is more likely a typo for
    // the option `/diagnostics:caret` than a reference to a file in the root
    // directory.
    std::string Nearest;
    if (getOpts().findNearest(Value, Nearest, getOptionVisibilityMask()) <= 1) {
      Diag(clang::diag::err_drv_no_such_file_with_suggestion)
          << Value << Nearest;
      return false;
    }
  }

  // In CL mode, don't error on apparently non-existent linker inputs, because
  // they can be influenced by linker flags the clang driver might not
  // understand.
  // Examples:
  // - `clang-cl main.cc ole32.lib` in a non-MSVC shell will make the driver
  //   module look for an MSVC installation in the registry. (We could ask
  //   the MSVCToolChain object if it can find `ole32.lib`, but the logic to
  //   look in the registry might move into lld-link in the future so that
  //   lld-link invocations in non-MSVC shells just work too.)
  // - `clang-cl ... /link ...` can pass arbitrary flags to the linker,
  //   including /libpath:, which is used to find .lib and .obj files.
  // So do not diagnose this on the driver level. Rely on the linker diagnosing
  // it. (If we don't end up invoking the linker, this means we'll emit a
  // "'linker' input unused [-Wunused-command-line-argument]" warning instead
  // of an error.)
  //
  // Only do this skip after the typo correction step above. `/Brepo` is treated
  // as TY_Object, but it's clearly a typo for `/Brepro`. It seems fine to emit
  // an error if we have a flag that's within an edit distance of 1 from a
  // flag. (Users can use `-Wl,` or `/linker` to launder the flag past the
  // driver in the unlikely case they run into this.)
  //
  // Don't do this for inputs that start with a '/', else we'd pass options
  // like /libpath: through to the linker silently.
  //
  // Emitting an error for linker inputs can also cause incorrect diagnostics
  // with the gcc driver. The command
  //     clang -fuse-ld=lld -Wl,--chroot,some/dir /file.o
  // will make lld look for some/dir/file.o, while we will diagnose here that
  // `/file.o` does not exist. However, configure scripts check if
  // `clang /GR-` compiles without error to see if the compiler is cl.exe,
  // so we can't downgrade diagnostics for `/GR-` from an error to a warning
  // in cc mode. (We can in cl mode because cl.exe itself only warns on
  // unknown flags.)
  if (IsCLMode() && Ty == types::TY_Object && !Value.starts_with("/"))
    return true;

  Diag(clang::diag::err_drv_no_such_file) << Value;
  return false;
}

// Get the C++20 Header Unit type corresponding to the input type.
static types::ID CXXHeaderUnitType(ModuleHeaderMode HM) {
  switch (HM) {
  case HeaderMode_User:
    return types::TY_CXXUHeader;
  case HeaderMode_System:
    return types::TY_CXXSHeader;
  case HeaderMode_Default:
    break;
  case HeaderMode_None:
    llvm_unreachable("should not be called in this case");
  }
  return types::TY_CXXHUHeader;
}

// Construct a the list of inputs and their types.
void Driver::BuildInputs(const ToolChain &TC, DerivedArgList &Args,
                         InputList &Inputs) const {
  const llvm::opt::OptTable &Opts = getOpts();
  // Track the current user specified (-x) input. We also explicitly track the
  // argument used to set the type; we only want to claim the type when we
  // actually use it, so we warn about unused -x arguments.
  types::ID InputType = types::TY_Nothing;
  Arg *InputTypeArg = nullptr;

  // The last /TC or /TP option sets the input type to C or C++ globally.
  if (Arg *TCTP = Args.getLastArgNoClaim(options::OPT__SLASH_TC,
                                         options::OPT__SLASH_TP)) {
    InputTypeArg = TCTP;
    InputType = TCTP->getOption().matches(options::OPT__SLASH_TC)
                    ? types::TY_C
                    : types::TY_CXX;

    Arg *Previous = nullptr;
    bool ShowNote = false;
    for (Arg *A :
         Args.filtered(options::OPT__SLASH_TC, options::OPT__SLASH_TP)) {
      if (Previous) {
        Diag(clang::diag::warn_drv_overriding_option)
            << Previous->getSpelling() << A->getSpelling();
        ShowNote = true;
      }
      Previous = A;
    }
    if (ShowNote)
      Diag(clang::diag::note_drv_t_option_is_global);
  }

  // Warn -x after last input file has no effect
  {
    Arg *LastXArg = Args.getLastArgNoClaim(options::OPT_x);
    Arg *LastInputArg = Args.getLastArgNoClaim(options::OPT_INPUT);
    if (LastXArg && LastInputArg &&
        LastInputArg->getIndex() < LastXArg->getIndex())
      Diag(clang::diag::warn_drv_unused_x) << LastXArg->getValue();
  }

  for (Arg *A : Args) {
    if (A->getOption().getKind() == Option::InputClass) {
      const char *Value = A->getValue();
      types::ID Ty = types::TY_INVALID;

      // Infer the input type if necessary.
      if (InputType == types::TY_Nothing) {
        // If there was an explicit arg for this, claim it.
        if (InputTypeArg)
          InputTypeArg->claim();

        // stdin must be handled specially.
        if (memcmp(Value, "-", 2) == 0) {
          if (IsFlangMode()) {
            Ty = types::TY_Fortran;
          } else if (IsDXCMode()) {
            Ty = types::TY_HLSL;
          } else {
            // If running with -E, treat as a C input (this changes the
            // builtin macros, for example). This may be overridden by -ObjC
            // below.
            //
            // Otherwise emit an error but still use a valid type to avoid
            // spurious errors (e.g., no inputs).
            assert(!CCGenDiagnostics && "stdin produces no crash reproducer");
            if (!Args.hasArgNoClaim(options::OPT_E) && !CCCIsCPP())
              Diag(IsCLMode() ? clang::diag::err_drv_unknown_stdin_type_clang_cl
                              : clang::diag::err_drv_unknown_stdin_type);
            Ty = types::TY_C;
          }
        } else {
          // Otherwise lookup by extension.
          // Fallback is C if invoked as C preprocessor, C++ if invoked with
          // clang-cl /E, or Object otherwise.
          // We use a host hook here because Darwin at least has its own
          // idea of what .s is.
          if (const char *Ext = strrchr(Value, '.'))
            Ty = TC.LookupTypeForExtension(Ext + 1);

          if (Ty == types::TY_INVALID) {
            if (IsCLMode() && (Args.hasArgNoClaim(options::OPT_E) || CCGenDiagnostics))
              Ty = types::TY_CXX;
            else if (CCCIsCPP() || CCGenDiagnostics)
              Ty = types::TY_C;
            else
              Ty = types::TY_Object;
          }

          // If the driver is invoked as C++ compiler (like clang++ or c++) it
          // should autodetect some input files as C++ for g++ compatibility.
          if (CCCIsCXX()) {
            types::ID OldTy = Ty;
            Ty = types::lookupCXXTypeForCType(Ty);

            // Do not complain about foo.h, when we are known to be processing
            // it as a C++20 header unit.
            if (Ty != OldTy && !(OldTy == types::TY_CHeader && hasHeaderMode()))
              Diag(clang::diag::warn_drv_treating_input_as_cxx)
                  << getTypeName(OldTy) << getTypeName(Ty);
          }

          // If running with -fthinlto-index=, extensions that normally identify
          // native object files actually identify LLVM bitcode files.
          if (Args.hasArgNoClaim(options::OPT_fthinlto_index_EQ) &&
              Ty == types::TY_Object)
            Ty = types::TY_LLVM_BC;
        }

        // -ObjC and -ObjC++ override the default language, but only for "source
        // files". We just treat everything that isn't a linker input as a
        // source file.
        //
        // FIXME: Clean this up if we move the phase sequence into the type.
        if (Ty != types::TY_Object) {
          if (Args.hasArg(options::OPT_ObjC))
            Ty = types::TY_ObjC;
          else if (Args.hasArg(options::OPT_ObjCXX))
            Ty = types::TY_ObjCXX;
        }

        // Disambiguate headers that are meant to be header units from those
        // intended to be PCH.  Avoid missing '.h' cases that are counted as
        // C headers by default - we know we are in C++ mode and we do not
        // want to issue a complaint about compiling things in the wrong mode.
        if ((Ty == types::TY_CXXHeader || Ty == types::TY_CHeader) &&
            hasHeaderMode())
          Ty = CXXHeaderUnitType(CXX20HeaderType);
      } else {
        assert(InputTypeArg && "InputType set w/o InputTypeArg");
        if (!InputTypeArg->getOption().matches(options::OPT_x)) {
          // If emulating cl.exe, make sure that /TC and /TP don't affect input
          // object files.
          const char *Ext = strrchr(Value, '.');
          if (Ext && TC.LookupTypeForExtension(Ext + 1) == types::TY_Object)
            Ty = types::TY_Object;
        }
        if (Ty == types::TY_INVALID) {
          Ty = InputType;
          InputTypeArg->claim();
        }
      }

      if ((Ty == types::TY_C || Ty == types::TY_CXX) &&
          Args.hasArgNoClaim(options::OPT_hipstdpar))
        Ty = types::TY_HIP;

      if (DiagnoseInputExistence(Args, Value, Ty, /*TypoCorrect=*/true))
        Inputs.push_back(std::make_pair(Ty, A));

    } else if (A->getOption().matches(options::OPT__SLASH_Tc)) {
      StringRef Value = A->getValue();
      if (DiagnoseInputExistence(Args, Value, types::TY_C,
                                 /*TypoCorrect=*/false)) {
        Arg *InputArg = MakeInputArg(Args, Opts, A->getValue());
        Inputs.push_back(std::make_pair(types::TY_C, InputArg));
      }
      A->claim();
    } else if (A->getOption().matches(options::OPT__SLASH_Tp)) {
      StringRef Value = A->getValue();
      if (DiagnoseInputExistence(Args, Value, types::TY_CXX,
                                 /*TypoCorrect=*/false)) {
        Arg *InputArg = MakeInputArg(Args, Opts, A->getValue());
        Inputs.push_back(std::make_pair(types::TY_CXX, InputArg));
      }
      A->claim();
    } else if (A->getOption().hasFlag(options::LinkerInput)) {
      // Just treat as object type, we could make a special type for this if
      // necessary.
      Inputs.push_back(std::make_pair(types::TY_Object, A));

    } else if (A->getOption().matches(options::OPT_x)) {
      InputTypeArg = A;
      InputType = types::lookupTypeForTypeSpecifier(A->getValue());
      A->claim();

      // Follow gcc behavior and treat as linker input for invalid -x
      // options. Its not clear why we shouldn't just revert to unknown; but
      // this isn't very important, we might as well be bug compatible.
      if (!InputType) {
        Diag(clang::diag::err_drv_unknown_language) << A->getValue();
        InputType = types::TY_Object;
      }

      // If the user has put -fmodule-header{,=} then we treat C++ headers as
      // header unit inputs.  So we 'promote' -xc++-header appropriately.
      if (InputType == types::TY_CXXHeader && hasHeaderMode())
        InputType = CXXHeaderUnitType(CXX20HeaderType);
    } else if (A->getOption().getID() == options::OPT_U) {
      assert(A->getNumValues() == 1 && "The /U option has one value.");
      StringRef Val = A->getValue(0);
      if (Val.find_first_of("/\\") != StringRef::npos) {
        // Warn about e.g. "/Users/me/myfile.c".
        Diag(diag::warn_slash_u_filename) << Val;
        Diag(diag::note_use_dashdash);
      }
    }
  }
  if (CCCIsCPP() && Inputs.empty()) {
    // If called as standalone preprocessor, stdin is processed
    // if no other input is present.
    Arg *A = MakeInputArg(Args, Opts, "-");
    Inputs.push_back(std::make_pair(types::TY_C, A));
  }
}

namespace {
/// Provides a convenient interface for different programming models to generate
/// the required device actions.
class OffloadingActionBuilder final {
  /// Flag used to trace errors in the builder.
  bool IsValid = false;

  /// The compilation that is using this builder.
  Compilation &C;

  /// Map between an input argument and the offload kinds used to process it.
  std::map<const Arg *, unsigned> InputArgToOffloadKindMap;

  /// Map between a host action and its originating input argument.
  std::map<Action *, const Arg *> HostActionToInputArgMap;

  /// Builder interface. It doesn't build anything or keep any state.
  class DeviceActionBuilder {
  public:
    typedef const llvm::SmallVectorImpl<phases::ID> PhasesTy;

    enum ActionBuilderReturnCode {
      // The builder acted successfully on the current action.
      ABRT_Success,
      // The builder didn't have to act on the current action.
      ABRT_Inactive,
      // The builder was successful and requested the host action to not be
      // generated.
      ABRT_Ignore_Host,
    };

  protected:
    /// Compilation associated with this builder.
    Compilation &C;

    /// Tool chains associated with this builder. The same programming
    /// model may have associated one or more tool chains.
    SmallVector<const ToolChain *, 2> ToolChains;

    /// The derived arguments associated with this builder.
    DerivedArgList &Args;

    /// The inputs associated with this builder.
    const Driver::InputList &Inputs;

    /// The associated offload kind.
    Action::OffloadKind AssociatedOffloadKind = Action::OFK_None;

  public:
    DeviceActionBuilder(Compilation &C, DerivedArgList &Args,
                        const Driver::InputList &Inputs,
                        Action::OffloadKind AssociatedOffloadKind)
        : C(C), Args(Args), Inputs(Inputs),
          AssociatedOffloadKind(AssociatedOffloadKind) {}
    virtual ~DeviceActionBuilder() {}

    /// Fill up the array \a DA with all the device dependences that should be
    /// added to the provided host action \a HostAction. By default it is
    /// inactive.
    virtual ActionBuilderReturnCode
    getDeviceDependences(OffloadAction::DeviceDependences &DA,
                         phases::ID CurPhase, phases::ID FinalPhase,
                         PhasesTy &Phases) {
      return ABRT_Inactive;
    }

    /// Update the state to include the provided host action \a HostAction as a
    /// dependency of the current device action. By default it is inactive.
    virtual ActionBuilderReturnCode addDeviceDependences(Action *HostAction) {
      return ABRT_Inactive;
    }

    /// Append top level actions generated by the builder.
    virtual void appendTopLevelActions(ActionList &AL) {}

    /// Append linker device actions generated by the builder.
    virtual void appendLinkDeviceActions(ActionList &AL) {}

    /// Append linker host action generated by the builder.
    virtual Action* appendLinkHostActions(ActionList &AL) { return nullptr; }

    /// Append linker actions generated by the builder.
    virtual void appendLinkDependences(OffloadAction::DeviceDependences &DA) {}

    /// Initialize the builder. Return true if any initialization errors are
    /// found.
    virtual bool initialize() { return false; }

    /// Return true if the builder can use bundling/unbundling.
    virtual bool canUseBundlerUnbundler() const { return false; }

    /// Return true if this builder is valid. We have a valid builder if we have
    /// associated device tool chains.
    bool isValid() { return !ToolChains.empty(); }

    /// Return the associated offload kind.
    Action::OffloadKind getAssociatedOffloadKind() {
      return AssociatedOffloadKind;
    }
  };

  /// Base class for CUDA/HIP action builder. It injects device code in
  /// the host backend action.
  class CudaActionBuilderBase : public DeviceActionBuilder {
  protected:
    /// Flags to signal if the user requested host-only or device-only
    /// compilation.
    bool CompileHostOnly = false;
    bool CompileDeviceOnly = false;
    bool EmitLLVM = false;
    bool EmitAsm = false;

    /// ID to identify each device compilation. For CUDA it is simply the
    /// GPU arch string. For HIP it is either the GPU arch string or GPU
    /// arch string plus feature strings delimited by a plus sign, e.g.
    /// gfx906+xnack.
    struct TargetID {
      /// Target ID string which is persistent throughout the compilation.
      const char *ID;
      TargetID(OffloadArch Arch) { ID = OffloadArchToString(Arch); }
      TargetID(const char *ID) : ID(ID) {}
      operator const char *() { return ID; }
      operator StringRef() { return StringRef(ID); }
    };
    /// List of GPU architectures to use in this compilation.
    SmallVector<TargetID, 4> GpuArchList;

    /// The CUDA actions for the current input.
    ActionList CudaDeviceActions;

    /// The CUDA fat binary if it was generated for the current input.
    Action *CudaFatBinary = nullptr;

    /// Flag that is set to true if this builder acted on the current input.
    bool IsActive = false;

    /// Flag for -fgpu-rdc.
    bool Relocatable = false;

    /// Default GPU architecture if there's no one specified.
    OffloadArch DefaultOffloadArch = OffloadArch::UNKNOWN;

    /// Method to generate compilation unit ID specified by option
    /// '-fuse-cuid='.
    enum UseCUIDKind { CUID_Hash, CUID_Random, CUID_None, CUID_Invalid };
    UseCUIDKind UseCUID = CUID_Hash;

    /// Compilation unit ID specified by option '-cuid='.
    StringRef FixedCUID;

  public:
    CudaActionBuilderBase(Compilation &C, DerivedArgList &Args,
                          const Driver::InputList &Inputs,
                          Action::OffloadKind OFKind)
        : DeviceActionBuilder(C, Args, Inputs, OFKind) {

      CompileDeviceOnly = C.getDriver().offloadDeviceOnly();
      Relocatable = Args.hasFlag(options::OPT_fgpu_rdc,
                                 options::OPT_fno_gpu_rdc, /*Default=*/false);
    }

    ActionBuilderReturnCode addDeviceDependences(Action *HostAction) override {
      // While generating code for CUDA, we only depend on the host input action
      // to trigger the creation of all the CUDA device actions.

      // If we are dealing with an input action, replicate it for each GPU
      // architecture. If we are in host-only mode we return 'success' so that
      // the host uses the CUDA offload kind.
      if (auto *IA = dyn_cast<InputAction>(HostAction)) {
        assert(!GpuArchList.empty() &&
               "We should have at least one GPU architecture.");

        // If the host input is not CUDA or HIP, we don't need to bother about
        // this input.
        if (!(IA->getType() == types::TY_CUDA ||
              IA->getType() == types::TY_HIP ||
              IA->getType() == types::TY_PP_HIP)) {
          // The builder will ignore this input.
          IsActive = false;
          return ABRT_Inactive;
        }

        // Set the flag to true, so that the builder acts on the current input.
        IsActive = true;

        if (CompileHostOnly)
          return ABRT_Success;

        // Replicate inputs for each GPU architecture.
        auto Ty = IA->getType() == types::TY_HIP ? types::TY_HIP_DEVICE
                                                 : types::TY_CUDA_DEVICE;
        std::string CUID = FixedCUID.str();
        if (CUID.empty()) {
          if (UseCUID == CUID_Random)
            CUID = llvm::utohexstr(llvm::sys::Process::GetRandomNumber(),
                                   /*LowerCase=*/true);
          else if (UseCUID == CUID_Hash) {
            llvm::MD5 Hasher;
            llvm::MD5::MD5Result Hash;
            SmallString<256> RealPath;
            llvm::sys::fs::real_path(IA->getInputArg().getValue(), RealPath,
                                     /*expand_tilde=*/true);
            Hasher.update(RealPath);
            for (auto *A : Args) {
              if (A->getOption().matches(options::OPT_INPUT))
                continue;
              Hasher.update(A->getAsString(Args));
            }
            Hasher.final(Hash);
            CUID = llvm::utohexstr(Hash.low(), /*LowerCase=*/true);
          }
        }
        IA->setId(CUID);

        for (unsigned I = 0, E = GpuArchList.size(); I != E; ++I) {
          CudaDeviceActions.push_back(
              C.MakeAction<InputAction>(IA->getInputArg(), Ty, IA->getId()));
        }

        return ABRT_Success;
      }

      // If this is an unbundling action use it as is for each CUDA toolchain.
      if (auto *UA = dyn_cast<OffloadUnbundlingJobAction>(HostAction)) {

        // If -fgpu-rdc is disabled, should not unbundle since there is no
        // device code to link.
        if (UA->getType() == types::TY_Object && !Relocatable)
          return ABRT_Inactive;

        CudaDeviceActions.clear();
        auto *IA = cast<InputAction>(UA->getInputs().back());
        std::string FileName = IA->getInputArg().getAsString(Args);
        // Check if the type of the file is the same as the action. Do not
        // unbundle it if it is not. Do not unbundle .so files, for example,
        // which are not object files. Files with extension ".lib" is classified
        // as TY_Object but they are actually archives, therefore should not be
        // unbundled here as objects. They will be handled at other places.
        const StringRef LibFileExt = ".lib";
        if (IA->getType() == types::TY_Object &&
            (!llvm::sys::path::has_extension(FileName) ||
             types::lookupTypeForExtension(
                 llvm::sys::path::extension(FileName).drop_front()) !=
                 types::TY_Object ||
             llvm::sys::path::extension(FileName) == LibFileExt))
          return ABRT_Inactive;

        for (auto Arch : GpuArchList) {
          CudaDeviceActions.push_back(UA);
          UA->registerDependentActionInfo(ToolChains[0], Arch,
                                          AssociatedOffloadKind);
        }
        IsActive = true;
        return ABRT_Success;
      }

      return IsActive ? ABRT_Success : ABRT_Inactive;
    }

    void appendTopLevelActions(ActionList &AL) override {
      // Utility to append actions to the top level list.
      auto AddTopLevel = [&](Action *A, TargetID TargetID) {
        OffloadAction::DeviceDependences Dep;
        Dep.add(*A, *ToolChains.front(), TargetID, AssociatedOffloadKind);
        AL.push_back(C.MakeAction<OffloadAction>(Dep, A->getType()));
      };

      // If we have a fat binary, add it to the list.
      if (CudaFatBinary) {
        AddTopLevel(CudaFatBinary, OffloadArch::UNUSED);
        CudaDeviceActions.clear();
        CudaFatBinary = nullptr;
        return;
      }

      if (CudaDeviceActions.empty())
        return;

      // If we have CUDA actions at this point, that's because we have a have
      // partial compilation, so we should have an action for each GPU
      // architecture.
      assert(CudaDeviceActions.size() == GpuArchList.size() &&
             "Expecting one action per GPU architecture.");
      assert(ToolChains.size() == 1 &&
             "Expecting to have a single CUDA toolchain.");
      for (unsigned I = 0, E = GpuArchList.size(); I != E; ++I)
        AddTopLevel(CudaDeviceActions[I], GpuArchList[I]);

      CudaDeviceActions.clear();
    }

    /// Get canonicalized offload arch option. \returns empty StringRef if the
    /// option is invalid.
    virtual StringRef getCanonicalOffloadArch(StringRef Arch) = 0;

    virtual std::optional<std::pair<llvm::StringRef, llvm::StringRef>>
    getConflictOffloadArchCombination(const std::set<StringRef> &GpuArchs) = 0;

    bool initialize() override {
      assert(AssociatedOffloadKind == Action::OFK_Cuda ||
             AssociatedOffloadKind == Action::OFK_HIP);

      // We don't need to support CUDA.
      if (AssociatedOffloadKind == Action::OFK_Cuda &&
          !C.hasOffloadToolChain<Action::OFK_Cuda>())
        return false;

      // We don't need to support HIP.
      if (AssociatedOffloadKind == Action::OFK_HIP &&
          !C.hasOffloadToolChain<Action::OFK_HIP>())
        return false;

      const ToolChain *HostTC = C.getSingleOffloadToolChain<Action::OFK_Host>();
      assert(HostTC && "No toolchain for host compilation.");
      if (HostTC->getTriple().isNVPTX() ||
          HostTC->getTriple().getArch() == llvm::Triple::amdgcn) {
        // We do not support targeting NVPTX/AMDGCN for host compilation. Throw
        // an error and abort pipeline construction early so we don't trip
        // asserts that assume device-side compilation.
        C.getDriver().Diag(diag::err_drv_cuda_host_arch)
            << HostTC->getTriple().getArchName();
        return true;
      }

      ToolChains.push_back(
          AssociatedOffloadKind == Action::OFK_Cuda
              ? C.getSingleOffloadToolChain<Action::OFK_Cuda>()
              : C.getSingleOffloadToolChain<Action::OFK_HIP>());

      CompileHostOnly = C.getDriver().offloadHostOnly();
      EmitLLVM = Args.getLastArg(options::OPT_emit_llvm);
      EmitAsm = Args.getLastArg(options::OPT_S);
      FixedCUID = Args.getLastArgValue(options::OPT_cuid_EQ);
      if (Arg *A = Args.getLastArg(options::OPT_fuse_cuid_EQ)) {
        StringRef UseCUIDStr = A->getValue();
        UseCUID = llvm::StringSwitch<UseCUIDKind>(UseCUIDStr)
                      .Case("hash", CUID_Hash)
                      .Case("random", CUID_Random)
                      .Case("none", CUID_None)
                      .Default(CUID_Invalid);
        if (UseCUID == CUID_Invalid) {
          C.getDriver().Diag(diag::err_drv_invalid_value)
              << A->getAsString(Args) << UseCUIDStr;
          C.setContainsError();
          return true;
        }
      }

      // --offload and --offload-arch options are mutually exclusive.
      if (Args.hasArgNoClaim(options::OPT_offload_EQ) &&
          Args.hasArgNoClaim(options::OPT_offload_arch_EQ,
                             options::OPT_no_offload_arch_EQ)) {
        C.getDriver().Diag(diag::err_opt_not_valid_with_opt) << "--offload-arch"
                                                             << "--offload";
      }

      // Collect all offload arch parameters, removing duplicates.
      std::set<StringRef> GpuArchs;
      bool Error = false;
      for (Arg *A : Args) {
        if (!(A->getOption().matches(options::OPT_offload_arch_EQ) ||
              A->getOption().matches(options::OPT_no_offload_arch_EQ)))
          continue;
        A->claim();

        for (StringRef ArchStr : llvm::split(A->getValue(), ",")) {
          if (A->getOption().matches(options::OPT_no_offload_arch_EQ) &&
              ArchStr == "all") {
            GpuArchs.clear();
          } else if (ArchStr == "native") {
            const ToolChain &TC = *ToolChains.front();
            auto GPUsOrErr = ToolChains.front()->getSystemGPUArchs(Args);
            if (!GPUsOrErr) {
              TC.getDriver().Diag(diag::err_drv_undetermined_gpu_arch)
                  << llvm::Triple::getArchTypeName(TC.getArch())
                  << llvm::toString(GPUsOrErr.takeError()) << "--offload-arch";
              continue;
            }

            for (auto GPU : *GPUsOrErr) {
              GpuArchs.insert(Args.MakeArgString(GPU));
            }
          } else {
            ArchStr = getCanonicalOffloadArch(ArchStr);
            if (ArchStr.empty()) {
              Error = true;
            } else if (A->getOption().matches(options::OPT_offload_arch_EQ))
              GpuArchs.insert(ArchStr);
            else if (A->getOption().matches(options::OPT_no_offload_arch_EQ))
              GpuArchs.erase(ArchStr);
            else
              llvm_unreachable("Unexpected option.");
          }
        }
      }

      auto &&ConflictingArchs = getConflictOffloadArchCombination(GpuArchs);
      if (ConflictingArchs) {
        C.getDriver().Diag(clang::diag::err_drv_bad_offload_arch_combo)
            << ConflictingArchs->first << ConflictingArchs->second;
        C.setContainsError();
        return true;
      }

      // Collect list of GPUs remaining in the set.
      for (auto Arch : GpuArchs)
        GpuArchList.push_back(Arch.data());

      // Default to sm_20 which is the lowest common denominator for
      // supported GPUs.  sm_20 code should work correctly, if
      // suboptimally, on all newer GPUs.
      if (GpuArchList.empty()) {
        if (ToolChains.front()->getTriple().isSPIRV()) {
          if (ToolChains.front()->getTriple().getVendor() == llvm::Triple::AMD)
            GpuArchList.push_back(OffloadArch::AMDGCNSPIRV);
          else
            GpuArchList.push_back(OffloadArch::Generic);
        } else {
          GpuArchList.push_back(DefaultOffloadArch);
        }
      }

      return Error;
    }
  };

  /// \brief CUDA action builder. It injects device code in the host backend
  /// action.
  class CudaActionBuilder final : public CudaActionBuilderBase {
  public:
    CudaActionBuilder(Compilation &C, DerivedArgList &Args,
                      const Driver::InputList &Inputs)
        : CudaActionBuilderBase(C, Args, Inputs, Action::OFK_Cuda) {
      DefaultOffloadArch = OffloadArch::CudaDefault;
    }

    StringRef getCanonicalOffloadArch(StringRef ArchStr) override {
      OffloadArch Arch = StringToOffloadArch(ArchStr);
      if (Arch == OffloadArch::UNKNOWN || !IsNVIDIAOffloadArch(Arch)) {
        C.getDriver().Diag(clang::diag::err_drv_cuda_bad_gpu_arch) << ArchStr;
        return StringRef();
      }
      return OffloadArchToString(Arch);
    }

    std::optional<std::pair<llvm::StringRef, llvm::StringRef>>
    getConflictOffloadArchCombination(
        const std::set<StringRef> &GpuArchs) override {
      return std::nullopt;
    }

    ActionBuilderReturnCode
    getDeviceDependences(OffloadAction::DeviceDependences &DA,
                         phases::ID CurPhase, phases::ID FinalPhase,
                         PhasesTy &Phases) override {
      if (!IsActive)
        return ABRT_Inactive;

      // If we don't have more CUDA actions, we don't have any dependences to
      // create for the host.
      if (CudaDeviceActions.empty())
        return ABRT_Success;

      assert(CudaDeviceActions.size() == GpuArchList.size() &&
             "Expecting one action per GPU architecture.");
      assert(!CompileHostOnly &&
             "Not expecting CUDA actions in host-only compilation.");

      // If we are generating code for the device or we are in a backend phase,
      // we attempt to generate the fat binary. We compile each arch to ptx and
      // assemble to cubin, then feed the cubin *and* the ptx into a device
      // "link" action, which uses fatbinary to combine these cubins into one
      // fatbin.  The fatbin is then an input to the host action if not in
      // device-only mode.
      if (CompileDeviceOnly || CurPhase == phases::Backend) {
        ActionList DeviceActions;
        for (unsigned I = 0, E = GpuArchList.size(); I != E; ++I) {
          // Produce the device action from the current phase up to the assemble
          // phase.
          for (auto Ph : Phases) {
            // Skip the phases that were already dealt with.
            if (Ph < CurPhase)
              continue;
            // We have to be consistent with the host final phase.
            if (Ph > FinalPhase)
              break;

            CudaDeviceActions[I] = C.getDriver().ConstructPhaseAction(
                C, Args, Ph, CudaDeviceActions[I], Action::OFK_Cuda);

            if (Ph == phases::Assemble)
              break;
          }

          // If we didn't reach the assemble phase, we can't generate the fat
          // binary. We don't need to generate the fat binary if we are not in
          // device-only mode.
          if (!isa<AssembleJobAction>(CudaDeviceActions[I]) ||
              CompileDeviceOnly)
            continue;

          Action *AssembleAction = CudaDeviceActions[I];
          assert(AssembleAction->getType() == types::TY_Object);
          assert(AssembleAction->getInputs().size() == 1);

          Action *BackendAction = AssembleAction->getInputs()[0];
          assert(BackendAction->getType() == types::TY_PP_Asm);

          for (auto &A : {AssembleAction, BackendAction}) {
            OffloadAction::DeviceDependences DDep;
            DDep.add(*A, *ToolChains.front(), GpuArchList[I], Action::OFK_Cuda);
            DeviceActions.push_back(
                C.MakeAction<OffloadAction>(DDep, A->getType()));
          }
        }

        // We generate the fat binary if we have device input actions.
        if (!DeviceActions.empty()) {
          CudaFatBinary =
              C.MakeAction<LinkJobAction>(DeviceActions, types::TY_CUDA_FATBIN);

          if (!CompileDeviceOnly) {
            DA.add(*CudaFatBinary, *ToolChains.front(), /*BoundArch=*/nullptr,
                   Action::OFK_Cuda);
            // Clear the fat binary, it is already a dependence to an host
            // action.
            CudaFatBinary = nullptr;
          }

          // Remove the CUDA actions as they are already connected to an host
          // action or fat binary.
          CudaDeviceActions.clear();
        }

        // We avoid creating host action in device-only mode.
        return CompileDeviceOnly ? ABRT_Ignore_Host : ABRT_Success;
      } else if (CurPhase > phases::Backend) {
        // If we are past the backend phase and still have a device action, we
        // don't have to do anything as this action is already a device
        // top-level action.
        return ABRT_Success;
      }

      assert(CurPhase < phases::Backend && "Generating single CUDA "
                                           "instructions should only occur "
                                           "before the backend phase!");

      // By default, we produce an action for each device arch.
      for (Action *&A : CudaDeviceActions)
        A = C.getDriver().ConstructPhaseAction(C, Args, CurPhase, A);

      return ABRT_Success;
    }
  };
  /// \brief HIP action builder. It injects device code in the host backend
  /// action.
  class HIPActionBuilder final : public CudaActionBuilderBase {
    /// The linker inputs obtained for each device arch.
    SmallVector<ActionList, 8> DeviceLinkerInputs;
    // The default bundling behavior depends on the type of output, therefore
    // BundleOutput needs to be tri-value: None, true, or false.
    // Bundle code objects except --no-gpu-output is specified for device
    // only compilation. Bundle other type of output files only if
    // --gpu-bundle-output is specified for device only compilation.
    std::optional<bool> BundleOutput;
    std::optional<bool> EmitReloc;

  public:
    HIPActionBuilder(Compilation &C, DerivedArgList &Args,
                     const Driver::InputList &Inputs)
        : CudaActionBuilderBase(C, Args, Inputs, Action::OFK_HIP) {

      DefaultOffloadArch = OffloadArch::HIPDefault;

      if (Args.hasArg(options::OPT_fhip_emit_relocatable,
                      options::OPT_fno_hip_emit_relocatable)) {
        EmitReloc = Args.hasFlag(options::OPT_fhip_emit_relocatable,
                                 options::OPT_fno_hip_emit_relocatable, false);

        if (*EmitReloc) {
          if (Relocatable) {
            C.getDriver().Diag(diag::err_opt_not_valid_with_opt)
                << "-fhip-emit-relocatable"
                << "-fgpu-rdc";
          }

          if (!CompileDeviceOnly) {
            C.getDriver().Diag(diag::err_opt_not_valid_without_opt)
                << "-fhip-emit-relocatable"
                << "--cuda-device-only";
          }
        }
      }

      if (Args.hasArg(options::OPT_gpu_bundle_output,
                      options::OPT_no_gpu_bundle_output))
        BundleOutput = Args.hasFlag(options::OPT_gpu_bundle_output,
                                    options::OPT_no_gpu_bundle_output, true) &&
                       (!EmitReloc || !*EmitReloc);
    }

    bool canUseBundlerUnbundler() const override { return true; }

    StringRef getCanonicalOffloadArch(StringRef IdStr) override {
      llvm::StringMap<bool> Features;
      // getHIPOffloadTargetTriple() is known to return valid value as it has
      // been called successfully in the CreateOffloadingDeviceToolChains().
      auto ArchStr = parseTargetID(
          *getHIPOffloadTargetTriple(C.getDriver(), C.getInputArgs()), IdStr,
          &Features);
      if (!ArchStr) {
        C.getDriver().Diag(clang::diag::err_drv_bad_target_id) << IdStr;
        C.setContainsError();
        return StringRef();
      }
      auto CanId = getCanonicalTargetID(*ArchStr, Features);
      return Args.MakeArgStringRef(CanId);
    };

    std::optional<std::pair<llvm::StringRef, llvm::StringRef>>
    getConflictOffloadArchCombination(
        const std::set<StringRef> &GpuArchs) override {
      return getConflictTargetIDCombination(GpuArchs);
    }

    ActionBuilderReturnCode
    getDeviceDependences(OffloadAction::DeviceDependences &DA,
                         phases::ID CurPhase, phases::ID FinalPhase,
                         PhasesTy &Phases) override {
      if (!IsActive)
        return ABRT_Inactive;

      // amdgcn does not support linking of object files, therefore we skip
      // backend and assemble phases to output LLVM IR. Except for generating
      // non-relocatable device code, where we generate fat binary for device
      // code and pass to host in Backend phase.
      if (CudaDeviceActions.empty())
        return ABRT_Success;

      assert(((CurPhase == phases::Link && Relocatable) ||
              CudaDeviceActions.size() == GpuArchList.size()) &&
             "Expecting one action per GPU architecture.");
      assert(!CompileHostOnly &&
             "Not expecting HIP actions in host-only compilation.");

      bool ShouldLink = !EmitReloc || !*EmitReloc;

      if (!Relocatable && CurPhase == phases::Backend && !EmitLLVM &&
          !EmitAsm && ShouldLink) {
        // If we are in backend phase, we attempt to generate the fat binary.
        // We compile each arch to IR and use a link action to generate code
        // object containing ISA. Then we use a special "link" action to create
        // a fat binary containing all the code objects for different GPU's.
        // The fat binary is then an input to the host action.
        for (unsigned I = 0, E = GpuArchList.size(); I != E; ++I) {
          if (C.getDriver().isUsingLTO(/*IsOffload=*/true)) {
            // When LTO is enabled, skip the backend and assemble phases and
            // use lld to link the bitcode.
            ActionList AL;
            AL.push_back(CudaDeviceActions[I]);
            // Create a link action to link device IR with device library
            // and generate ISA.
            CudaDeviceActions[I] =
                C.MakeAction<LinkJobAction>(AL, types::TY_Image);
          } else {
            // When LTO is not enabled, we follow the conventional
            // compiler phases, including backend and assemble phases.
            ActionList AL;
            Action *BackendAction = nullptr;
            if (ToolChains.front()->getTriple().isSPIRV()) {
              // Emit LLVM bitcode for SPIR-V targets. SPIR-V device tool chain
              // (HIPSPVToolChain) runs post-link LLVM IR passes.
              types::ID Output = Args.hasArg(options::OPT_S)
                                     ? types::TY_LLVM_IR
                                     : types::TY_LLVM_BC;
              BackendAction =
                  C.MakeAction<BackendJobAction>(CudaDeviceActions[I], Output);
            } else
              BackendAction = C.getDriver().ConstructPhaseAction(
                  C, Args, phases::Backend, CudaDeviceActions[I],
                  AssociatedOffloadKind);
            auto AssembleAction = C.getDriver().ConstructPhaseAction(
                C, Args, phases::Assemble, BackendAction,
                AssociatedOffloadKind);
            AL.push_back(AssembleAction);
            // Create a link action to link device IR with device library
            // and generate ISA.
            CudaDeviceActions[I] =
                C.MakeAction<LinkJobAction>(AL, types::TY_Image);
          }

          // OffloadingActionBuilder propagates device arch until an offload
          // action. Since the next action for creating fatbin does
          // not have device arch, whereas the above link action and its input
          // have device arch, an offload action is needed to stop the null
          // device arch of the next action being propagated to the above link
          // action.
          OffloadAction::DeviceDependences DDep;
          DDep.add(*CudaDeviceActions[I], *ToolChains.front(), GpuArchList[I],
                   AssociatedOffloadKind);
          CudaDeviceActions[I] = C.MakeAction<OffloadAction>(
              DDep, CudaDeviceActions[I]->getType());
        }

        if (!CompileDeviceOnly || !BundleOutput || *BundleOutput) {
          // Create HIP fat binary with a special "link" action.
          CudaFatBinary = C.MakeAction<LinkJobAction>(CudaDeviceActions,
                                                      types::TY_HIP_FATBIN);

          if (!CompileDeviceOnly) {
            DA.add(*CudaFatBinary, *ToolChains.front(), /*BoundArch=*/nullptr,
                   AssociatedOffloadKind);
            // Clear the fat binary, it is already a dependence to an host
            // action.
            CudaFatBinary = nullptr;
          }

          // Remove the CUDA actions as they are already connected to an host
          // action or fat binary.
          CudaDeviceActions.clear();
        }

        return CompileDeviceOnly ? ABRT_Ignore_Host : ABRT_Success;
      } else if (CurPhase == phases::Link) {
        if (!ShouldLink)
          return ABRT_Success;
        // Save CudaDeviceActions to DeviceLinkerInputs for each GPU subarch.
        // This happens to each device action originated from each input file.
        // Later on, device actions in DeviceLinkerInputs are used to create
        // device link actions in appendLinkDependences and the created device
        // link actions are passed to the offload action as device dependence.
        DeviceLinkerInputs.resize(CudaDeviceActions.size());
        auto LI = DeviceLinkerInputs.begin();
        for (auto *A : CudaDeviceActions) {
          LI->push_back(A);
          ++LI;
        }

        // We will pass the device action as a host dependence, so we don't
        // need to do anything else with them.
        CudaDeviceActions.clear();
        return CompileDeviceOnly ? ABRT_Ignore_Host : ABRT_Success;
      }

      // By default, we produce an action for each device arch.
      for (Action *&A : CudaDeviceActions)
        A = C.getDriver().ConstructPhaseAction(C, Args, CurPhase, A,
                                               AssociatedOffloadKind);

      if (CompileDeviceOnly && CurPhase == FinalPhase && BundleOutput &&
          *BundleOutput) {
        for (unsigned I = 0, E = GpuArchList.size(); I != E; ++I) {
          OffloadAction::DeviceDependences DDep;
          DDep.add(*CudaDeviceActions[I], *ToolChains.front(), GpuArchList[I],
                   AssociatedOffloadKind);
          CudaDeviceActions[I] = C.MakeAction<OffloadAction>(
              DDep, CudaDeviceActions[I]->getType());
        }
        CudaFatBinary =
            C.MakeAction<OffloadBundlingJobAction>(CudaDeviceActions);
        CudaDeviceActions.clear();
      }

      return (CompileDeviceOnly &&
              (CurPhase == FinalPhase ||
               (!ShouldLink && CurPhase == phases::Assemble)))
                 ? ABRT_Ignore_Host
                 : ABRT_Success;
    }

    void appendLinkDeviceActions(ActionList &AL) override {
      if (DeviceLinkerInputs.size() == 0)
        return;

      assert(DeviceLinkerInputs.size() == GpuArchList.size() &&
             "Linker inputs and GPU arch list sizes do not match.");

      ActionList Actions;
      unsigned I = 0;
      // Append a new link action for each device.
      // Each entry in DeviceLinkerInputs corresponds to a GPU arch.
      for (auto &LI : DeviceLinkerInputs) {

        types::ID Output = Args.hasArg(options::OPT_emit_llvm)
                                   ? types::TY_LLVM_BC
                                   : types::TY_Image;

        auto *DeviceLinkAction = C.MakeAction<LinkJobAction>(LI, Output);
        // Linking all inputs for the current GPU arch.
        // LI contains all the inputs for the linker.
        OffloadAction::DeviceDependences DeviceLinkDeps;
        DeviceLinkDeps.add(*DeviceLinkAction, *ToolChains[0],
            GpuArchList[I], AssociatedOffloadKind);
        Actions.push_back(C.MakeAction<OffloadAction>(
            DeviceLinkDeps, DeviceLinkAction->getType()));
        ++I;
      }
      DeviceLinkerInputs.clear();

      // If emitting LLVM, do not generate final host/device compilation action
      if (Args.hasArg(options::OPT_emit_llvm)) {
          AL.append(Actions);
          return;
      }

      // Create a host object from all the device images by embedding them
      // in a fat binary for mixed host-device compilation. For device-only
      // compilation, creates a fat binary.
      OffloadAction::DeviceDependences DDeps;
      if (!CompileDeviceOnly || !BundleOutput || *BundleOutput) {
        auto *TopDeviceLinkAction = C.MakeAction<LinkJobAction>(
            Actions,
            CompileDeviceOnly ? types::TY_HIP_FATBIN : types::TY_Object);
        DDeps.add(*TopDeviceLinkAction, *ToolChains[0], nullptr,
                  AssociatedOffloadKind);
        // Offload the host object to the host linker.
        AL.push_back(
            C.MakeAction<OffloadAction>(DDeps, TopDeviceLinkAction->getType()));
      } else {
        AL.append(Actions);
      }
    }

    Action* appendLinkHostActions(ActionList &AL) override { return AL.back(); }

    void appendLinkDependences(OffloadAction::DeviceDependences &DA) override {}
  };

  ///
  /// TODO: Add the implementation for other specialized builders here.
  ///

  /// Specialized builders being used by this offloading action builder.
  SmallVector<DeviceActionBuilder *, 4> SpecializedBuilders;

  /// Flag set to true if all valid builders allow file bundling/unbundling.
  bool CanUseBundler;

public:
  OffloadingActionBuilder(Compilation &C, DerivedArgList &Args,
                          const Driver::InputList &Inputs)
      : C(C) {
    // Create a specialized builder for each device toolchain.

    IsValid = true;

    // Create a specialized builder for CUDA.
    SpecializedBuilders.push_back(new CudaActionBuilder(C, Args, Inputs));

    // Create a specialized builder for HIP.
    SpecializedBuilders.push_back(new HIPActionBuilder(C, Args, Inputs));

    //
    // TODO: Build other specialized builders here.
    //

    // Initialize all the builders, keeping track of errors. If all valid
    // builders agree that we can use bundling, set the flag to true.
    unsigned ValidBuilders = 0u;
    unsigned ValidBuildersSupportingBundling = 0u;
    for (auto *SB : SpecializedBuilders) {
      IsValid = IsValid && !SB->initialize();

      // Update the counters if the builder is valid.
      if (SB->isValid()) {
        ++ValidBuilders;
        if (SB->canUseBundlerUnbundler())
          ++ValidBuildersSupportingBundling;
      }
    }
    CanUseBundler =
        ValidBuilders && ValidBuilders == ValidBuildersSupportingBundling;
  }

  ~OffloadingActionBuilder() {
    for (auto *SB : SpecializedBuilders)
      delete SB;
  }

  /// Record a host action and its originating input argument.
  void recordHostAction(Action *HostAction, const Arg *InputArg) {
    assert(HostAction && "Invalid host action");
    assert(InputArg && "Invalid input argument");
    auto Loc = HostActionToInputArgMap.find(HostAction);
    if (Loc == HostActionToInputArgMap.end())
      HostActionToInputArgMap[HostAction] = InputArg;
    assert(HostActionToInputArgMap[HostAction] == InputArg &&
           "host action mapped to multiple input arguments");
  }

  /// Generate an action that adds device dependences (if any) to a host action.
  /// If no device dependence actions exist, just return the host action \a
  /// HostAction. If an error is found or if no builder requires the host action
  /// to be generated, return nullptr.
  Action *
  addDeviceDependencesToHostAction(Action *HostAction, const Arg *InputArg,
                                   phases::ID CurPhase, phases::ID FinalPhase,
                                   DeviceActionBuilder::PhasesTy &Phases) {
    if (!IsValid)
      return nullptr;

    if (SpecializedBuilders.empty())
      return HostAction;

    assert(HostAction && "Invalid host action!");
    recordHostAction(HostAction, InputArg);

    OffloadAction::DeviceDependences DDeps;
    // Check if all the programming models agree we should not emit the host
    // action. Also, keep track of the offloading kinds employed.
    auto &OffloadKind = InputArgToOffloadKindMap[InputArg];
    unsigned InactiveBuilders = 0u;
    unsigned IgnoringBuilders = 0u;
    for (auto *SB : SpecializedBuilders) {
      if (!SB->isValid()) {
        ++InactiveBuilders;
        continue;
      }
      auto RetCode =
          SB->getDeviceDependences(DDeps, CurPhase, FinalPhase, Phases);

      // If the builder explicitly says the host action should be ignored,
      // we need to increment the variable that tracks the builders that request
      // the host object to be ignored.
      if (RetCode == DeviceActionBuilder::ABRT_Ignore_Host)
        ++IgnoringBuilders;

      // Unless the builder was inactive for this action, we have to record the
      // offload kind because the host will have to use it.
      if (RetCode != DeviceActionBuilder::ABRT_Inactive)
        OffloadKind |= SB->getAssociatedOffloadKind();
    }

    // If all builders agree that the host object should be ignored, just return
    // nullptr.
    if (IgnoringBuilders &&
        SpecializedBuilders.size() == (InactiveBuilders + IgnoringBuilders))
      return nullptr;

    if (DDeps.getActions().empty())
      return HostAction;

    // We have dependences we need to bundle together. We use an offload action
    // for that.
    OffloadAction::HostDependence HDep(
        *HostAction, *C.getSingleOffloadToolChain<Action::OFK_Host>(),
        /*BoundArch=*/nullptr, DDeps);
    return C.MakeAction<OffloadAction>(HDep, DDeps);
  }

  /// Generate an action that adds a host dependence to a device action. The
  /// results will be kept in this action builder. Return true if an error was
  /// found.
  bool addHostDependenceToDeviceActions(Action *&HostAction,
                                        const Arg *InputArg) {
    if (!IsValid)
      return true;

    recordHostAction(HostAction, InputArg);

    // If we are supporting bundling/unbundling and the current action is an
    // input action of non-source file, we replace the host action by the
    // unbundling action. The bundler tool has the logic to detect if an input
    // is a bundle or not and if the input is not a bundle it assumes it is a
    // host file. Therefore it is safe to create an unbundling action even if
    // the input is not a bundle.
    if (CanUseBundler && isa<InputAction>(HostAction) &&
        InputArg->getOption().getKind() == llvm::opt::Option::InputClass &&
        (!types::isSrcFile(HostAction->getType()) ||
         HostAction->getType() == types::TY_PP_HIP)) {
      auto UnbundlingHostAction =
          C.MakeAction<OffloadUnbundlingJobAction>(HostAction);
      UnbundlingHostAction->registerDependentActionInfo(
          C.getSingleOffloadToolChain<Action::OFK_Host>(),
          /*BoundArch=*/StringRef(), Action::OFK_Host);
      HostAction = UnbundlingHostAction;
      recordHostAction(HostAction, InputArg);
    }

    assert(HostAction && "Invalid host action!");

    // Register the offload kinds that are used.
    auto &OffloadKind = InputArgToOffloadKindMap[InputArg];
    for (auto *SB : SpecializedBuilders) {
      if (!SB->isValid())
        continue;

      auto RetCode = SB->addDeviceDependences(HostAction);

      // Host dependences for device actions are not compatible with that same
      // action being ignored.
      assert(RetCode != DeviceActionBuilder::ABRT_Ignore_Host &&
             "Host dependence not expected to be ignored.!");

      // Unless the builder was inactive for this action, we have to record the
      // offload kind because the host will have to use it.
      if (RetCode != DeviceActionBuilder::ABRT_Inactive)
        OffloadKind |= SB->getAssociatedOffloadKind();
    }

    // Do not use unbundler if the Host does not depend on device action.
    if (OffloadKind == Action::OFK_None && CanUseBundler)
      if (auto *UA = dyn_cast<OffloadUnbundlingJobAction>(HostAction))
        HostAction = UA->getInputs().back();

    return false;
  }

  /// Add the offloading top level actions to the provided action list. This
  /// function can replace the host action by a bundling action if the
  /// programming models allow it.
  bool appendTopLevelActions(ActionList &AL, Action *HostAction,
                             const Arg *InputArg) {
    if (HostAction)
      recordHostAction(HostAction, InputArg);

    // Get the device actions to be appended.
    ActionList OffloadAL;
    for (auto *SB : SpecializedBuilders) {
      if (!SB->isValid())
        continue;
      SB->appendTopLevelActions(OffloadAL);
    }

    // If we can use the bundler, replace the host action by the bundling one in
    // the resulting list. Otherwise, just append the device actions. For
    // device only compilation, HostAction is a null pointer, therefore only do
    // this when HostAction is not a null pointer.
    if (CanUseBundler && HostAction &&
        HostAction->getType() != types::TY_Nothing && !OffloadAL.empty()) {
      // Add the host action to the list in order to create the bundling action.
      OffloadAL.push_back(HostAction);

      // We expect that the host action was just appended to the action list
      // before this method was called.
      assert(HostAction == AL.back() && "Host action not in the list??");
      HostAction = C.MakeAction<OffloadBundlingJobAction>(OffloadAL);
      recordHostAction(HostAction, InputArg);
      AL.back() = HostAction;
    } else
      AL.append(OffloadAL.begin(), OffloadAL.end());

    // Propagate to the current host action (if any) the offload information
    // associated with the current input.
    if (HostAction)
      HostAction->propagateHostOffloadInfo(InputArgToOffloadKindMap[InputArg],
                                           /*BoundArch=*/nullptr);
    return false;
  }

  void appendDeviceLinkActions(ActionList &AL) {
    for (DeviceActionBuilder *SB : SpecializedBuilders) {
      if (!SB->isValid())
        continue;
      SB->appendLinkDeviceActions(AL);
    }
  }

  Action *makeHostLinkAction() {
    // Build a list of device linking actions.
    ActionList DeviceAL;
    appendDeviceLinkActions(DeviceAL);
    if (DeviceAL.empty())
      return nullptr;

    // Let builders add host linking actions.
    Action* HA = nullptr;
    for (DeviceActionBuilder *SB : SpecializedBuilders) {
      if (!SB->isValid())
        continue;
      HA = SB->appendLinkHostActions(DeviceAL);
      // This created host action has no originating input argument, therefore
      // needs to set its offloading kind directly.
      if (HA)
        HA->propagateHostOffloadInfo(SB->getAssociatedOffloadKind(),
                                     /*BoundArch=*/nullptr);
    }
    return HA;
  }

  /// Processes the host linker action. This currently consists of replacing it
  /// with an offload action if there are device link objects and propagate to
  /// the host action all the offload kinds used in the current compilation. The
  /// resulting action is returned.
  Action *processHostLinkAction(Action *HostAction) {
    // Add all the dependences from the device linking actions.
    OffloadAction::DeviceDependences DDeps;
    for (auto *SB : SpecializedBuilders) {
      if (!SB->isValid())
        continue;

      SB->appendLinkDependences(DDeps);
    }

    // Calculate all the offload kinds used in the current compilation.
    unsigned ActiveOffloadKinds = 0u;
    for (auto &I : InputArgToOffloadKindMap)
      ActiveOffloadKinds |= I.second;

    // If we don't have device dependencies, we don't have to create an offload
    // action.
    if (DDeps.getActions().empty()) {
      // Set all the active offloading kinds to the link action. Given that it
      // is a link action it is assumed to depend on all actions generated so
      // far.
      HostAction->setHostOffloadInfo(ActiveOffloadKinds,
                                     /*BoundArch=*/nullptr);
      // Propagate active offloading kinds for each input to the link action.
      // Each input may have different active offloading kind.
      for (auto *A : HostAction->inputs()) {
        auto ArgLoc = HostActionToInputArgMap.find(A);
        if (ArgLoc == HostActionToInputArgMap.end())
          continue;
        auto OFKLoc = InputArgToOffloadKindMap.find(ArgLoc->second);
        if (OFKLoc == InputArgToOffloadKindMap.end())
          continue;
        A->propagateHostOffloadInfo(OFKLoc->second, /*BoundArch=*/nullptr);
      }
      return HostAction;
    }

    // Create the offload action with all dependences. When an offload action
    // is created the kinds are propagated to the host action, so we don't have
    // to do that explicitly here.
    OffloadAction::HostDependence HDep(
        *HostAction, *C.getSingleOffloadToolChain<Action::OFK_Host>(),
        /*BoundArch*/ nullptr, ActiveOffloadKinds);
    return C.MakeAction<OffloadAction>(HDep, DDeps);
  }
};
} // anonymous namespace.

void Driver::handleArguments(Compilation &C, DerivedArgList &Args,
                             const InputList &Inputs,
                             ActionList &Actions) const {

  // Ignore /Yc/Yu if both /Yc and /Yu passed but with different filenames.
  Arg *YcArg = Args.getLastArg(options::OPT__SLASH_Yc);
  Arg *YuArg = Args.getLastArg(options::OPT__SLASH_Yu);
  if (YcArg && YuArg && strcmp(YcArg->getValue(), YuArg->getValue()) != 0) {
    Diag(clang::diag::warn_drv_ycyu_different_arg_clang_cl);
    Args.eraseArg(options::OPT__SLASH_Yc);
    Args.eraseArg(options::OPT__SLASH_Yu);
    YcArg = YuArg = nullptr;
  }
  if (YcArg && Inputs.size() > 1) {
    Diag(clang::diag::warn_drv_yc_multiple_inputs_clang_cl);
    Args.eraseArg(options::OPT__SLASH_Yc);
    YcArg = nullptr;
  }

  Arg *FinalPhaseArg;
  phases::ID FinalPhase = getFinalPhase(Args, &FinalPhaseArg);

  if (FinalPhase == phases::Link) {
    if (Args.hasArgNoClaim(options::OPT_hipstdpar)) {
      Args.AddFlagArg(nullptr, getOpts().getOption(options::OPT_hip_link));
      Args.AddFlagArg(nullptr,
                      getOpts().getOption(options::OPT_frtlib_add_rpath));
    }
    // Emitting LLVM while linking disabled except in HIPAMD Toolchain
    if (Args.hasArg(options::OPT_emit_llvm) && !Args.hasArg(options::OPT_hip_link))
      Diag(clang::diag::err_drv_emit_llvm_link);
    if (IsCLMode() && LTOMode != LTOK_None &&
        !Args.getLastArgValue(options::OPT_fuse_ld_EQ)
             .equals_insensitive("lld"))
      Diag(clang::diag::err_drv_lto_without_lld);

    // If -dumpdir is not specified, give a default prefix derived from the link
    // output filename. For example, `clang -g -gsplit-dwarf a.c -o x` passes
    // `-dumpdir x-` to cc1. If -o is unspecified, use
    // stem(getDefaultImageName()) (usually stem("a.out") = "a").
    if (!Args.hasArg(options::OPT_dumpdir)) {
      Arg *FinalOutput = Args.getLastArg(options::OPT_o, options::OPT__SLASH_o);
      Arg *Arg = Args.MakeSeparateArg(
          nullptr, getOpts().getOption(options::OPT_dumpdir),
          Args.MakeArgString(
              (FinalOutput ? FinalOutput->getValue()
                           : llvm::sys::path::stem(getDefaultImageName())) +
              "-"));
      Arg->claim();
      Args.append(Arg);
    }
  }

  if (FinalPhase == phases::Preprocess || Args.hasArg(options::OPT__SLASH_Y_)) {
    // If only preprocessing or /Y- is used, all pch handling is disabled.
    // Rather than check for it everywhere, just remove clang-cl pch-related
    // flags here.
    Args.eraseArg(options::OPT__SLASH_Fp);
    Args.eraseArg(options::OPT__SLASH_Yc);
    Args.eraseArg(options::OPT__SLASH_Yu);
    YcArg = YuArg = nullptr;
  }

  unsigned LastPLSize = 0;
  for (auto &I : Inputs) {
    types::ID InputType = I.first;
    const Arg *InputArg = I.second;

    auto PL = types::getCompilationPhases(InputType);
    LastPLSize = PL.size();

    // If the first step comes after the final phase we are doing as part of
    // this compilation, warn the user about it.
    phases::ID InitialPhase = PL[0];
    if (InitialPhase > FinalPhase) {
      if (InputArg->isClaimed())
        continue;

      // Claim here to avoid the more general unused warning.
      InputArg->claim();

      // Suppress all unused style warnings with -Qunused-arguments
      if (Args.hasArg(options::OPT_Qunused_arguments))
        continue;

      // Special case when final phase determined by binary name, rather than
      // by a command-line argument with a corresponding Arg.
      if (CCCIsCPP())
        Diag(clang::diag::warn_drv_input_file_unused_by_cpp)
            << InputArg->getAsString(Args) << getPhaseName(InitialPhase);
      // Special case '-E' warning on a previously preprocessed file to make
      // more sense.
      else if (InitialPhase == phases::Compile &&
               (Args.getLastArg(options::OPT__SLASH_EP,
                                options::OPT__SLASH_P) ||
                Args.getLastArg(options::OPT_E) ||
                Args.getLastArg(options::OPT_M, options::OPT_MM)) &&
               getPreprocessedType(InputType) == types::TY_INVALID)
        Diag(clang::diag::warn_drv_preprocessed_input_file_unused)
            << InputArg->getAsString(Args) << !!FinalPhaseArg
            << (FinalPhaseArg ? FinalPhaseArg->getOption().getName() : "");
      else
        Diag(clang::diag::warn_drv_input_file_unused)
            << InputArg->getAsString(Args) << getPhaseName(InitialPhase)
            << !!FinalPhaseArg
            << (FinalPhaseArg ? FinalPhaseArg->getOption().getName() : "");
      continue;
    }

    if (YcArg) {
      // Add a separate precompile phase for the compile phase.
      if (FinalPhase >= phases::Compile) {
        const types::ID HeaderType = lookupHeaderTypeForSourceType(InputType);
        // Build the pipeline for the pch file.
        Action *ClangClPch = C.MakeAction<InputAction>(*InputArg, HeaderType);
        for (phases::ID Phase : types::getCompilationPhases(HeaderType))
          ClangClPch = ConstructPhaseAction(C, Args, Phase, ClangClPch);
        assert(ClangClPch);
        Actions.push_back(ClangClPch);
        // The driver currently exits after the first failed command.  This
        // relies on that behavior, to make sure if the pch generation fails,
        // the main compilation won't run.
        // FIXME: If the main compilation fails, the PCH generation should
        // probably not be considered successful either.
      }
    }
  }

  // If we are linking, claim any options which are obviously only used for
  // compilation.
  // FIXME: Understand why the last Phase List length is used here.
  if (FinalPhase == phases::Link && LastPLSize == 1) {
    Args.ClaimAllArgs(options::OPT_CompileOnly_Group);
    Args.ClaimAllArgs(options::OPT_cl_compile_Group);
  }
}

void Driver::BuildActions(Compilation &C, DerivedArgList &Args,
                          const InputList &Inputs, ActionList &Actions) const {
  llvm::PrettyStackTraceString CrashInfo("Building compilation actions");

  if (!SuppressMissingInputWarning && Inputs.empty()) {
    Diag(clang::diag::err_drv_no_input_files);
    return;
  }

  // Diagnose misuse of /Fo.
  if (Arg *A = Args.getLastArg(options::OPT__SLASH_Fo)) {
    StringRef V = A->getValue();
    if (Inputs.size() > 1 && !V.empty() &&
        !llvm::sys::path::is_separator(V.back())) {
      // Check whether /Fo tries to name an output file for multiple inputs.
      Diag(clang::diag::err_drv_out_file_argument_with_multiple_sources)
          << A->getSpelling() << V;
      Args.eraseArg(options::OPT__SLASH_Fo);
    }
  }

  // Diagnose misuse of /Fa.
  if (Arg *A = Args.getLastArg(options::OPT__SLASH_Fa)) {
    StringRef V = A->getValue();
    if (Inputs.size() > 1 && !V.empty() &&
        !llvm::sys::path::is_separator(V.back())) {
      // Check whether /Fa tries to name an asm file for multiple inputs.
      Diag(clang::diag::err_drv_out_file_argument_with_multiple_sources)
          << A->getSpelling() << V;
      Args.eraseArg(options::OPT__SLASH_Fa);
    }
  }

  // Diagnose misuse of /o.
  if (Arg *A = Args.getLastArg(options::OPT__SLASH_o)) {
    if (A->getValue()[0] == '\0') {
      // It has to have a value.
      Diag(clang::diag::err_drv_missing_argument) << A->getSpelling() << 1;
      Args.eraseArg(options::OPT__SLASH_o);
    }
  }

  handleArguments(C, Args, Inputs, Actions);

  bool UseNewOffloadingDriver =
      C.isOffloadingHostKind(Action::OFK_OpenMP) ||
      Args.hasFlag(options::OPT_offload_new_driver,
                   options::OPT_no_offload_new_driver, false);

  // Builder to be used to build offloading actions.
  std::unique_ptr<OffloadingActionBuilder> OffloadBuilder =
      !UseNewOffloadingDriver
          ? std::make_unique<OffloadingActionBuilder>(C, Args, Inputs)
          : nullptr;

  // Construct the actions to perform.
  ExtractAPIJobAction *ExtractAPIAction = nullptr;
  ActionList LinkerInputs;
  ActionList MergerInputs;

  for (auto &I : Inputs) {
    types::ID InputType = I.first;
    const Arg *InputArg = I.second;

    auto PL = types::getCompilationPhases(*this, Args, InputType);
    if (PL.empty())
      continue;

    auto FullPL = types::getCompilationPhases(InputType);

    // Build the pipeline for this file.
    Action *Current = C.MakeAction<InputAction>(*InputArg, InputType);

    // Use the current host action in any of the offloading actions, if
    // required.
    if (!UseNewOffloadingDriver)
      if (OffloadBuilder->addHostDependenceToDeviceActions(Current, InputArg))
        break;

    for (phases::ID Phase : PL) {

      // Add any offload action the host action depends on.
      if (!UseNewOffloadingDriver)
        Current = OffloadBuilder->addDeviceDependencesToHostAction(
            Current, InputArg, Phase, PL.back(), FullPL);
      if (!Current)
        break;

      // Queue linker inputs.
      if (Phase == phases::Link) {
        assert(Phase == PL.back() && "linking must be final compilation step.");
        // We don't need to generate additional link commands if emitting AMD
        // bitcode or compiling only for the offload device
        if (!(C.getInputArgs().hasArg(options::OPT_hip_link) &&
              (C.getInputArgs().hasArg(options::OPT_emit_llvm))) &&
            !offloadDeviceOnly())
          LinkerInputs.push_back(Current);
        Current = nullptr;
        break;
      }

      // TODO: Consider removing this because the merged may not end up being
      // the final Phase in the pipeline. Perhaps the merged could just merge
      // and then pass an artifact of some sort to the Link Phase.
      // Queue merger inputs.
      if (Phase == phases::IfsMerge) {
        assert(Phase == PL.back() && "merging must be final compilation step.");
        MergerInputs.push_back(Current);
        Current = nullptr;
        break;
      }

      if (Phase == phases::Precompile && ExtractAPIAction) {
        ExtractAPIAction->addHeaderInput(Current);
        Current = nullptr;
        break;
      }

      // FIXME: Should we include any prior module file outputs as inputs of
      // later actions in the same command line?

      // Otherwise construct the appropriate action.
      Action *NewCurrent = ConstructPhaseAction(C, Args, Phase, Current);

      // We didn't create a new action, so we will just move to the next phase.
      if (NewCurrent == Current)
        continue;

      if (auto *EAA = dyn_cast<ExtractAPIJobAction>(NewCurrent))
        ExtractAPIAction = EAA;

      Current = NewCurrent;

      // Try to build the offloading actions and add the result as a dependency
      // to the host.
      if (UseNewOffloadingDriver)
        Current = BuildOffloadingActions(C, Args, I, Current);
      // Use the current host action in any of the offloading actions, if
      // required.
      else if (OffloadBuilder->addHostDependenceToDeviceActions(Current,
                                                                InputArg))
        break;

      if (Current->getType() == types::TY_Nothing)
        break;
    }

    // If we ended with something, add to the output list.
    if (Current)
      Actions.push_back(Current);

    // Add any top level actions generated for offloading.
    if (!UseNewOffloadingDriver)
      OffloadBuilder->appendTopLevelActions(Actions, Current, InputArg);
    else if (Current)
      Current->propagateHostOffloadInfo(C.getActiveOffloadKinds(),
                                        /*BoundArch=*/nullptr);
  }

  // Add a link action if necessary.

  if (LinkerInputs.empty()) {
    Arg *FinalPhaseArg;
    if (getFinalPhase(Args, &FinalPhaseArg) == phases::Link)
      if (!UseNewOffloadingDriver)
        OffloadBuilder->appendDeviceLinkActions(Actions);
  }

  if (!LinkerInputs.empty()) {
    if (!UseNewOffloadingDriver)
      if (Action *Wrapper = OffloadBuilder->makeHostLinkAction())
        LinkerInputs.push_back(Wrapper);
    Action *LA;
    // Check if this Linker Job should emit a static library.
    if (ShouldEmitStaticLibrary(Args)) {
      LA = C.MakeAction<StaticLibJobAction>(LinkerInputs, types::TY_Image);
    } else if (UseNewOffloadingDriver ||
               Args.hasArg(options::OPT_offload_link)) {
      LA = C.MakeAction<LinkerWrapperJobAction>(LinkerInputs, types::TY_Image);
      LA->propagateHostOffloadInfo(C.getActiveOffloadKinds(),
                                   /*BoundArch=*/nullptr);
    } else {
      LA = C.MakeAction<LinkJobAction>(LinkerInputs, types::TY_Image);
    }
    if (!UseNewOffloadingDriver)
      LA = OffloadBuilder->processHostLinkAction(LA);
    Actions.push_back(LA);
  }

  // Add an interface stubs merge action if necessary.
  if (!MergerInputs.empty())
    Actions.push_back(
        C.MakeAction<IfsMergeJobAction>(MergerInputs, types::TY_Image));

  if (Args.hasArg(options::OPT_emit_interface_stubs)) {
    auto PhaseList = types::getCompilationPhases(
        types::TY_IFS_CPP,
        Args.hasArg(options::OPT_c) ? phases::Compile : phases::IfsMerge);

    ActionList MergerInputs;

    for (auto &I : Inputs) {
      types::ID InputType = I.first;
      const Arg *InputArg = I.second;

      // Currently clang and the llvm assembler do not support generating symbol
      // stubs from assembly, so we skip the input on asm files. For ifs files
      // we rely on the normal pipeline setup in the pipeline setup code above.
      if (InputType == types::TY_IFS || InputType == types::TY_PP_Asm ||
          InputType == types::TY_Asm)
        continue;

      Action *Current = C.MakeAction<InputAction>(*InputArg, InputType);

      for (auto Phase : PhaseList) {
        switch (Phase) {
        default:
          llvm_unreachable(
              "IFS Pipeline can only consist of Compile followed by IfsMerge.");
        case phases::Compile: {
          // Only IfsMerge (llvm-ifs) can handle .o files by looking for ifs
          // files where the .o file is located. The compile action can not
          // handle this.
          if (InputType == types::TY_Object)
            break;

          Current = C.MakeAction<CompileJobAction>(Current, types::TY_IFS_CPP);
          break;
        }
        case phases::IfsMerge: {
          assert(Phase == PhaseList.back() &&
                 "merging must be final compilation step.");
          MergerInputs.push_back(Current);
          Current = nullptr;
          break;
        }
        }
      }

      // If we ended with something, add to the output list.
      if (Current)
        Actions.push_back(Current);
    }

    // Add an interface stubs merge action if necessary.
    if (!MergerInputs.empty())
      Actions.push_back(
          C.MakeAction<IfsMergeJobAction>(MergerInputs, types::TY_Image));
  }

  for (auto Opt : {options::OPT_print_supported_cpus,
                   options::OPT_print_supported_extensions,
                   options::OPT_print_enabled_extensions}) {
    // If --print-supported-cpus, -mcpu=? or -mtune=? is specified, build a
    // custom Compile phase that prints out supported cpu models and quits.
    //
    // If either --print-supported-extensions or --print-enabled-extensions is
    // specified, call the corresponding helper function that prints out the
    // supported/enabled extensions and quits.
    if (Arg *A = Args.getLastArg(Opt)) {
      if (Opt == options::OPT_print_supported_extensions &&
          !C.getDefaultToolChain().getTriple().isRISCV() &&
          !C.getDefaultToolChain().getTriple().isAArch64() &&
          !C.getDefaultToolChain().getTriple().isARM()) {
        C.getDriver().Diag(diag::err_opt_not_valid_on_target)
            << "--print-supported-extensions";
        return;
      }
      if (Opt == options::OPT_print_enabled_extensions &&
          !C.getDefaultToolChain().getTriple().isRISCV() &&
          !C.getDefaultToolChain().getTriple().isAArch64()) {
        C.getDriver().Diag(diag::err_opt_not_valid_on_target)
            << "--print-enabled-extensions";
        return;
      }

      // Use the -mcpu=? flag as the dummy input to cc1.
      Actions.clear();
      Action *InputAc = C.MakeAction<InputAction>(*A, types::TY_C);
      Actions.push_back(
          C.MakeAction<PrecompileJobAction>(InputAc, types::TY_Nothing));
      for (auto &I : Inputs)
        I.second->claim();
    }
  }

  // Call validator for dxil when -Vd not in Args.
  if (C.getDefaultToolChain().getTriple().isDXIL()) {
    // Only add action when needValidation.
    const auto &TC =
        static_cast<const toolchains::HLSLToolChain &>(C.getDefaultToolChain());
    if (TC.requiresValidation(Args)) {
      Action *LastAction = Actions.back();
      Actions.push_back(C.MakeAction<BinaryAnalyzeJobAction>(
          LastAction, types::TY_DX_CONTAINER));
    }
  }

  // Claim ignored clang-cl options.
  Args.ClaimAllArgs(options::OPT_cl_ignored_Group);
}

/// Returns the canonical name for the offloading architecture when using a HIP
/// or CUDA architecture.
static StringRef getCanonicalArchString(Compilation &C,
                                        const llvm::opt::DerivedArgList &Args,
                                        StringRef ArchStr,
                                        const llvm::Triple &Triple,
                                        bool SuppressError = false) {
  // Lookup the CUDA / HIP architecture string. Only report an error if we were
  // expecting the triple to be only NVPTX / AMDGPU.
  OffloadArch Arch =
      StringToOffloadArch(getProcessorFromTargetID(Triple, ArchStr));
  if (!SuppressError && Triple.isNVPTX() &&
      (Arch == OffloadArch::UNKNOWN || !IsNVIDIAOffloadArch(Arch))) {
    C.getDriver().Diag(clang::diag::err_drv_offload_bad_gpu_arch)
        << "CUDA" << ArchStr;
    return StringRef();
  } else if (!SuppressError && Triple.isAMDGPU() &&
             (Arch == OffloadArch::UNKNOWN || !IsAMDOffloadArch(Arch))) {
    C.getDriver().Diag(clang::diag::err_drv_offload_bad_gpu_arch)
        << "HIP" << ArchStr;
    return StringRef();
  }

  if (IsNVIDIAOffloadArch(Arch))
    return Args.MakeArgStringRef(OffloadArchToString(Arch));

  if (IsAMDOffloadArch(Arch)) {
    llvm::StringMap<bool> Features;
    auto HIPTriple = getHIPOffloadTargetTriple(C.getDriver(), C.getInputArgs());
    if (!HIPTriple)
      return StringRef();
    auto Arch = parseTargetID(*HIPTriple, ArchStr, &Features);
    if (!Arch) {
      C.getDriver().Diag(clang::diag::err_drv_bad_target_id) << ArchStr;
      C.setContainsError();
      return StringRef();
    }
    return Args.MakeArgStringRef(getCanonicalTargetID(*Arch, Features));
  }

  // If the input isn't CUDA or HIP just return the architecture.
  return ArchStr;
}

/// Checks if the set offloading architectures does not conflict. Returns the
/// incompatible pair if a conflict occurs.
static std::optional<std::pair<llvm::StringRef, llvm::StringRef>>
getConflictOffloadArchCombination(const llvm::DenseSet<StringRef> &Archs,
                                  llvm::Triple Triple) {
  if (!Triple.isAMDGPU())
    return std::nullopt;

  std::set<StringRef> ArchSet;
  llvm::copy(Archs, std::inserter(ArchSet, ArchSet.begin()));
  return getConflictTargetIDCombination(ArchSet);
}

llvm::DenseSet<StringRef>
Driver::getOffloadArchs(Compilation &C, const llvm::opt::DerivedArgList &Args,
                        Action::OffloadKind Kind, const ToolChain *TC,
                        bool SuppressError) const {
  if (!TC)
    TC = &C.getDefaultToolChain();

  // --offload and --offload-arch options are mutually exclusive.
  if (Args.hasArgNoClaim(options::OPT_offload_EQ) &&
      Args.hasArgNoClaim(options::OPT_offload_arch_EQ,
                         options::OPT_no_offload_arch_EQ)) {
    C.getDriver().Diag(diag::err_opt_not_valid_with_opt)
        << "--offload"
        << (Args.hasArgNoClaim(options::OPT_offload_arch_EQ)
                ? "--offload-arch"
                : "--no-offload-arch");
  }

  if (KnownArchs.contains(TC))
    return KnownArchs.lookup(TC);

  llvm::DenseSet<StringRef> Archs;
  for (auto *Arg : Args) {
    // Extract any '--[no-]offload-arch' arguments intended for this toolchain.
    std::unique_ptr<llvm::opt::Arg> ExtractedArg = nullptr;
    if (Arg->getOption().matches(options::OPT_Xopenmp_target_EQ) &&
        ToolChain::getOpenMPTriple(Arg->getValue(0)) == TC->getTriple()) {
      Arg->claim();
      unsigned Index = Args.getBaseArgs().MakeIndex(Arg->getValue(1));
      ExtractedArg = getOpts().ParseOneArg(Args, Index);
      Arg = ExtractedArg.get();
    }

    // Add or remove the seen architectures in order of appearance. If an
    // invalid architecture is given we simply exit.
    if (Arg->getOption().matches(options::OPT_offload_arch_EQ)) {
      for (StringRef Arch : llvm::split(Arg->getValue(), ",")) {
        if (Arch == "native" || Arch.empty()) {
          auto GPUsOrErr = TC->getSystemGPUArchs(Args);
          if (!GPUsOrErr) {
            if (SuppressError)
              llvm::consumeError(GPUsOrErr.takeError());
            else
              TC->getDriver().Diag(diag::err_drv_undetermined_gpu_arch)
                  << llvm::Triple::getArchTypeName(TC->getArch())
                  << llvm::toString(GPUsOrErr.takeError()) << "--offload-arch";
            continue;
          }

          for (auto ArchStr : *GPUsOrErr) {
            Archs.insert(
                getCanonicalArchString(C, Args, Args.MakeArgString(ArchStr),
                                       TC->getTriple(), SuppressError));
          }
        } else {
          StringRef ArchStr = getCanonicalArchString(
              C, Args, Arch, TC->getTriple(), SuppressError);
          if (ArchStr.empty())
            return Archs;
          Archs.insert(ArchStr);
        }
      }
    } else if (Arg->getOption().matches(options::OPT_no_offload_arch_EQ)) {
      for (StringRef Arch : llvm::split(Arg->getValue(), ",")) {
        if (Arch == "all") {
          Archs.clear();
        } else {
          StringRef ArchStr = getCanonicalArchString(
              C, Args, Arch, TC->getTriple(), SuppressError);
          if (ArchStr.empty())
            return Archs;
          Archs.erase(ArchStr);
        }
      }
    }
  }

  if (auto ConflictingArchs =
          getConflictOffloadArchCombination(Archs, TC->getTriple())) {
    C.getDriver().Diag(clang::diag::err_drv_bad_offload_arch_combo)
        << ConflictingArchs->first << ConflictingArchs->second;
    C.setContainsError();
  }

  // Skip filling defaults if we're just querying what is availible.
  if (SuppressError)
    return Archs;

  if (Archs.empty()) {
    if (Kind == Action::OFK_Cuda)
      Archs.insert(OffloadArchToString(OffloadArch::CudaDefault));
    else if (Kind == Action::OFK_HIP)
      Archs.insert(OffloadArchToString(OffloadArch::HIPDefault));
    else if (Kind == Action::OFK_OpenMP)
      Archs.insert(StringRef());
  } else {
    Args.ClaimAllArgs(options::OPT_offload_arch_EQ);
    Args.ClaimAllArgs(options::OPT_no_offload_arch_EQ);
  }

  return Archs;
}

Action *Driver::BuildOffloadingActions(Compilation &C,
                                       llvm::opt::DerivedArgList &Args,
                                       const InputTy &Input,
                                       Action *HostAction) const {
  // Don't build offloading actions if explicitly disabled or we do not have a
  // valid source input and compile action to embed it in. If preprocessing only
  // ignore embedding.
  if (offloadHostOnly() || !types::isSrcFile(Input.first) ||
      !(isa<CompileJobAction>(HostAction) ||
        getFinalPhase(Args) == phases::Preprocess))
    return HostAction;

  ActionList OffloadActions;
  OffloadAction::DeviceDependences DDeps;

  const Action::OffloadKind OffloadKinds[] = {
      Action::OFK_OpenMP, Action::OFK_Cuda, Action::OFK_HIP};

  for (Action::OffloadKind Kind : OffloadKinds) {
    SmallVector<const ToolChain *, 2> ToolChains;
    ActionList DeviceActions;

    auto TCRange = C.getOffloadToolChains(Kind);
    for (auto TI = TCRange.first, TE = TCRange.second; TI != TE; ++TI)
      ToolChains.push_back(TI->second);

    if (ToolChains.empty())
      continue;

    types::ID InputType = Input.first;
    const Arg *InputArg = Input.second;

    // The toolchain can be active for unsupported file types.
    if ((Kind == Action::OFK_Cuda && !types::isCuda(InputType)) ||
        (Kind == Action::OFK_HIP && !types::isHIP(InputType)))
      continue;

    // Get the product of all bound architectures and toolchains.
    SmallVector<std::pair<const ToolChain *, StringRef>> TCAndArchs;
    for (const ToolChain *TC : ToolChains) {
      llvm::DenseSet<StringRef> Arches = getOffloadArchs(C, Args, Kind, TC);
      SmallVector<StringRef, 0> Sorted(Arches.begin(), Arches.end());
      llvm::sort(Sorted);
      for (StringRef Arch : Sorted)
        TCAndArchs.push_back(std::make_pair(TC, Arch));
    }

    for (unsigned I = 0, E = TCAndArchs.size(); I != E; ++I)
      DeviceActions.push_back(C.MakeAction<InputAction>(*InputArg, InputType));

    if (DeviceActions.empty())
      return HostAction;

    auto PL = types::getCompilationPhases(*this, Args, InputType);

    for (phases::ID Phase : PL) {
      if (Phase == phases::Link) {
        assert(Phase == PL.back() && "linking must be final compilation step.");
        break;
      }

      auto TCAndArch = TCAndArchs.begin();
      for (Action *&A : DeviceActions) {
        if (A->getType() == types::TY_Nothing)
          continue;

        // Propagate the ToolChain so we can use it in ConstructPhaseAction.
        A->propagateDeviceOffloadInfo(Kind, TCAndArch->second.data(),
                                      TCAndArch->first);
        A = ConstructPhaseAction(C, Args, Phase, A, Kind);

        if (isa<CompileJobAction>(A) && isa<CompileJobAction>(HostAction) &&
            Kind == Action::OFK_OpenMP &&
            HostAction->getType() != types::TY_Nothing) {
          // OpenMP offloading has a dependency on the host compile action to
          // identify which declarations need to be emitted. This shouldn't be
          // collapsed with any other actions so we can use it in the device.
          HostAction->setCannotBeCollapsedWithNextDependentAction();
          OffloadAction::HostDependence HDep(
              *HostAction, *C.getSingleOffloadToolChain<Action::OFK_Host>(),
              TCAndArch->second.data(), Kind);
          OffloadAction::DeviceDependences DDep;
          DDep.add(*A, *TCAndArch->first, TCAndArch->second.data(), Kind);
          A = C.MakeAction<OffloadAction>(HDep, DDep);
        }

        ++TCAndArch;
      }
    }

    // Compiling HIP in non-RDC mode requires linking each action individually.
    for (Action *&A : DeviceActions) {
      if ((A->getType() != types::TY_Object &&
           A->getType() != types::TY_LTO_BC) ||
          Kind != Action::OFK_HIP ||
          Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc, false))
        continue;
      ActionList LinkerInput = {A};
      A = C.MakeAction<LinkJobAction>(LinkerInput, types::TY_Image);
    }

    auto TCAndArch = TCAndArchs.begin();
    for (Action *A : DeviceActions) {
      DDeps.add(*A, *TCAndArch->first, TCAndArch->second.data(), Kind);
      OffloadAction::DeviceDependences DDep;
      DDep.add(*A, *TCAndArch->first, TCAndArch->second.data(), Kind);

      // Compiling CUDA in non-RDC mode uses the PTX output if available.
      for (Action *Input : A->getInputs())
        if (Kind == Action::OFK_Cuda && A->getType() == types::TY_Object &&
            !Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc,
                          false))
          DDep.add(*Input, *TCAndArch->first, TCAndArch->second.data(), Kind);
      OffloadActions.push_back(C.MakeAction<OffloadAction>(DDep, A->getType()));

      ++TCAndArch;
    }
  }

  // HIP code in non-RDC mode will bundle the output if it invoked the linker.
  bool ShouldBundleHIP =
      C.isOffloadingHostKind(Action::OFK_HIP) &&
      Args.hasFlag(options::OPT_gpu_bundle_output,
                   options::OPT_no_gpu_bundle_output, true) &&
      !Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc, false) &&
      !llvm::any_of(OffloadActions,
                    [](Action *A) { return A->getType() != types::TY_Image; });

  // All kinds exit now in device-only mode except for non-RDC mode HIP.
  if (offloadDeviceOnly() && !ShouldBundleHIP)
    return C.MakeAction<OffloadAction>(DDeps, types::TY_Nothing);

  if (OffloadActions.empty())
    return HostAction;

  OffloadAction::DeviceDependences DDep;
  if (C.isOffloadingHostKind(Action::OFK_Cuda) &&
      !Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc, false)) {
    // If we are not in RDC-mode we just emit the final CUDA fatbinary for
    // each translation unit without requiring any linking.
    Action *FatbinAction =
        C.MakeAction<LinkJobAction>(OffloadActions, types::TY_CUDA_FATBIN);
    DDep.add(*FatbinAction, *C.getSingleOffloadToolChain<Action::OFK_Cuda>(),
             nullptr, Action::OFK_Cuda);
  } else if (C.isOffloadingHostKind(Action::OFK_HIP) &&
             !Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc,
                           false)) {
    // If we are not in RDC-mode we just emit the final HIP fatbinary for each
    // translation unit, linking each input individually.
    Action *FatbinAction =
        C.MakeAction<LinkJobAction>(OffloadActions, types::TY_HIP_FATBIN);
    DDep.add(*FatbinAction, *C.getSingleOffloadToolChain<Action::OFK_HIP>(),
             nullptr, Action::OFK_HIP);
  } else {
    // Package all the offloading actions into a single output that can be
    // embedded in the host and linked.
    Action *PackagerAction =
        C.MakeAction<OffloadPackagerJobAction>(OffloadActions, types::TY_Image);
    DDep.add(*PackagerAction, *C.getSingleOffloadToolChain<Action::OFK_Host>(),
             nullptr, C.getActiveOffloadKinds());
  }

  // HIP wants '--offload-device-only' to create a fatbinary by default.
  if (offloadDeviceOnly())
    return C.MakeAction<OffloadAction>(DDep, types::TY_Nothing);

  // If we are unable to embed a single device output into the host, we need to
  // add each device output as a host dependency to ensure they are still built.
  bool SingleDeviceOutput = !llvm::any_of(OffloadActions, [](Action *A) {
    return A->getType() == types::TY_Nothing;
  }) && isa<CompileJobAction>(HostAction);
  OffloadAction::HostDependence HDep(
      *HostAction, *C.getSingleOffloadToolChain<Action::OFK_Host>(),
      /*BoundArch=*/nullptr, SingleDeviceOutput ? DDep : DDeps);
  return C.MakeAction<OffloadAction>(HDep, SingleDeviceOutput ? DDep : DDeps);
}

Action *Driver::ConstructPhaseAction(
    Compilation &C, const ArgList &Args, phases::ID Phase, Action *Input,
    Action::OffloadKind TargetDeviceOffloadKind) const {
  llvm::PrettyStackTraceString CrashInfo("Constructing phase actions");

  // Some types skip the assembler phase (e.g., llvm-bc), but we can't
  // encode this in the steps because the intermediate type depends on
  // arguments. Just special case here.
  if (Phase == phases::Assemble && Input->getType() != types::TY_PP_Asm)
    return Input;

  // Build the appropriate action.
  switch (Phase) {
  case phases::Link:
    llvm_unreachable("link action invalid here.");
  case phases::IfsMerge:
    llvm_unreachable("ifsmerge action invalid here.");
  case phases::Preprocess: {
    types::ID OutputTy;
    // -M and -MM specify the dependency file name by altering the output type,
    // -if -MD and -MMD are not specified.
    if (Args.hasArg(options::OPT_M, options::OPT_MM) &&
        !Args.hasArg(options::OPT_MD, options::OPT_MMD)) {
      OutputTy = types::TY_Dependencies;
    } else {
      OutputTy = Input->getType();
      // For these cases, the preprocessor is only translating forms, the Output
      // still needs preprocessing.
      if (!Args.hasFlag(options::OPT_frewrite_includes,
                        options::OPT_fno_rewrite_includes, false) &&
          !Args.hasFlag(options::OPT_frewrite_imports,
                        options::OPT_fno_rewrite_imports, false) &&
          !Args.hasFlag(options::OPT_fdirectives_only,
                        options::OPT_fno_directives_only, false) &&
          !CCGenDiagnostics)
        OutputTy = types::getPreprocessedType(OutputTy);
      assert(OutputTy != types::TY_INVALID &&
             "Cannot preprocess this input type!");
    }
    return C.MakeAction<PreprocessJobAction>(Input, OutputTy);
  }
  case phases::Precompile: {
    // API extraction should not generate an actual precompilation action.
    if (Args.hasArg(options::OPT_extract_api))
      return C.MakeAction<ExtractAPIJobAction>(Input, types::TY_API_INFO);

    // With 'fexperimental-modules-reduced-bmi', we don't want to run the
    // precompile phase unless the user specified '--precompile'. In the case
    // the '--precompile' flag is enabled, we will try to emit the reduced BMI
    // as a by product in GenerateModuleInterfaceAction.
    if (Args.hasArg(options::OPT_modules_reduced_bmi) &&
        !Args.getLastArg(options::OPT__precompile))
      return Input;

    types::ID OutputTy = getPrecompiledType(Input->getType());
    assert(OutputTy != types::TY_INVALID &&
           "Cannot precompile this input type!");

    // If we're given a module name, precompile header file inputs as a
    // module, not as a precompiled header.
    const char *ModName = nullptr;
    if (OutputTy == types::TY_PCH) {
      if (Arg *A = Args.getLastArg(options::OPT_fmodule_name_EQ))
        ModName = A->getValue();
      if (ModName)
        OutputTy = types::TY_ModuleFile;
    }

    if (Args.hasArg(options::OPT_fsyntax_only)) {
      // Syntax checks should not emit a PCH file
      OutputTy = types::TY_Nothing;
    }

    return C.MakeAction<PrecompileJobAction>(Input, OutputTy);
  }
  case phases::Compile: {
    if (Args.hasArg(options::OPT_fsyntax_only))
      return C.MakeAction<CompileJobAction>(Input, types::TY_Nothing);
    if (Args.hasArg(options::OPT_rewrite_objc))
      return C.MakeAction<CompileJobAction>(Input, types::TY_RewrittenObjC);
    if (Args.hasArg(options::OPT_rewrite_legacy_objc))
      return C.MakeAction<CompileJobAction>(Input,
                                            types::TY_RewrittenLegacyObjC);
    if (Args.hasArg(options::OPT__analyze))
      return C.MakeAction<AnalyzeJobAction>(Input, types::TY_Plist);
    if (Args.hasArg(options::OPT__migrate))
      return C.MakeAction<MigrateJobAction>(Input, types::TY_Remap);
    if (Args.hasArg(options::OPT_emit_ast))
      return C.MakeAction<CompileJobAction>(Input, types::TY_AST);
    if (Args.hasArg(options::OPT_emit_cir))
      return C.MakeAction<CompileJobAction>(Input, types::TY_CIR);
    if (Args.hasArg(options::OPT_module_file_info))
      return C.MakeAction<CompileJobAction>(Input, types::TY_ModuleFile);
    if (Args.hasArg(options::OPT_verify_pch))
      return C.MakeAction<VerifyPCHJobAction>(Input, types::TY_Nothing);
    if (Args.hasArg(options::OPT_extract_api))
      return C.MakeAction<ExtractAPIJobAction>(Input, types::TY_API_INFO);
    return C.MakeAction<CompileJobAction>(Input, types::TY_LLVM_BC);
  }
  case phases::Backend: {
    if (isUsingLTO() && TargetDeviceOffloadKind == Action::OFK_None) {
      types::ID Output;
      if (Args.hasArg(options::OPT_ffat_lto_objects) &&
          !Args.hasArg(options::OPT_emit_llvm))
        Output = types::TY_PP_Asm;
      else if (Args.hasArg(options::OPT_S))
        Output = types::TY_LTO_IR;
      else
        Output = types::TY_LTO_BC;
      return C.MakeAction<BackendJobAction>(Input, Output);
    }
    if (isUsingLTO(/* IsOffload */ true) &&
        TargetDeviceOffloadKind != Action::OFK_None) {
      types::ID Output =
          Args.hasArg(options::OPT_S) ? types::TY_LTO_IR : types::TY_LTO_BC;
      return C.MakeAction<BackendJobAction>(Input, Output);
    }
    if (Args.hasArg(options::OPT_emit_llvm) ||
        (((Input->getOffloadingToolChain() &&
           Input->getOffloadingToolChain()->getTriple().isAMDGPU()) ||
          TargetDeviceOffloadKind == Action::OFK_HIP) &&
         (Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc,
                       false) ||
          TargetDeviceOffloadKind == Action::OFK_OpenMP))) {
      types::ID Output =
          Args.hasArg(options::OPT_S) &&
                  (TargetDeviceOffloadKind == Action::OFK_None ||
                   offloadDeviceOnly() ||
                   (TargetDeviceOffloadKind == Action::OFK_HIP &&
                    !Args.hasFlag(options::OPT_offload_new_driver,
                                  options::OPT_no_offload_new_driver, false)))
              ? types::TY_LLVM_IR
              : types::TY_LLVM_BC;
      return C.MakeAction<BackendJobAction>(Input, Output);
    }
    return C.MakeAction<BackendJobAction>(Input, types::TY_PP_Asm);
  }
  case phases::Assemble:
    return C.MakeAction<AssembleJobAction>(std::move(Input), types::TY_Object);
  }

  llvm_unreachable("invalid phase in ConstructPhaseAction");
}

void Driver::BuildJobs(Compilation &C) const {
  llvm::PrettyStackTraceString CrashInfo("Building compilation jobs");

  Arg *FinalOutput = C.getArgs().getLastArg(options::OPT_o);

  // It is an error to provide a -o option if we are making multiple output
  // files. There are exceptions:
  //
  // IfsMergeJob: when generating interface stubs enabled we want to be able to
  // generate the stub file at the same time that we generate the real
  // library/a.out. So when a .o, .so, etc are the output, with clang interface
  // stubs there will also be a .ifs and .ifso at the same location.
  //
  // CompileJob of type TY_IFS_CPP: when generating interface stubs is enabled
  // and -c is passed, we still want to be able to generate a .ifs file while
  // we are also generating .o files. So we allow more than one output file in
  // this case as well.
  //
  // OffloadClass of type TY_Nothing: device-only output will place many outputs
  // into a single offloading action. We should count all inputs to the action
  // as outputs. Also ignore device-only outputs if we're compiling with
  // -fsyntax-only.
  if (FinalOutput) {
    unsigned NumOutputs = 0;
    unsigned NumIfsOutputs = 0;
    for (const Action *A : C.getActions()) {
      if (A->getType() != types::TY_Nothing &&
          A->getType() != types::TY_DX_CONTAINER &&
          !(A->getKind() == Action::IfsMergeJobClass ||
            (A->getType() == clang::driver::types::TY_IFS_CPP &&
             A->getKind() == clang::driver::Action::CompileJobClass &&
             0 == NumIfsOutputs++) ||
            (A->getKind() == Action::BindArchClass && A->getInputs().size() &&
             A->getInputs().front()->getKind() == Action::IfsMergeJobClass)))
        ++NumOutputs;
      else if (A->getKind() == Action::OffloadClass &&
               A->getType() == types::TY_Nothing &&
               !C.getArgs().hasArg(options::OPT_fsyntax_only))
        NumOutputs += A->size();
    }

    if (NumOutputs > 1) {
      Diag(clang::diag::err_drv_output_argument_with_multiple_files);
      FinalOutput = nullptr;
    }
  }

  const llvm::Triple &RawTriple = C.getDefaultToolChain().getTriple();

  // Collect the list of architectures.
  llvm::StringSet<> ArchNames;
  if (RawTriple.isOSBinFormatMachO())
    for (const Arg *A : C.getArgs())
      if (A->getOption().matches(options::OPT_arch))
        ArchNames.insert(A->getValue());

  // Set of (Action, canonical ToolChain triple) pairs we've built jobs for.
  std::map<std::pair<const Action *, std::string>, InputInfoList> CachedResults;
  for (Action *A : C.getActions()) {
    // If we are linking an image for multiple archs then the linker wants
    // -arch_multiple and -final_output <final image name>. Unfortunately, this
    // doesn't fit in cleanly because we have to pass this information down.
    //
    // FIXME: This is a hack; find a cleaner way to integrate this into the
    // process.
    const char *LinkingOutput = nullptr;
    if (isa<LipoJobAction>(A)) {
      if (FinalOutput)
        LinkingOutput = FinalOutput->getValue();
      else
        LinkingOutput = getDefaultImageName();
    }

    BuildJobsForAction(C, A, &C.getDefaultToolChain(),
                       /*BoundArch*/ StringRef(),
                       /*AtTopLevel*/ true,
                       /*MultipleArchs*/ ArchNames.size() > 1,
                       /*LinkingOutput*/ LinkingOutput, CachedResults,
                       /*TargetDeviceOffloadKind*/ Action::OFK_None);
  }

  // If we have more than one job, then disable integrated-cc1 for now. Do this
  // also when we need to report process execution statistics.
  if (C.getJobs().size() > 1 || CCPrintProcessStats)
    for (auto &J : C.getJobs())
      J.InProcess = false;

  if (CCPrintProcessStats) {
    C.setPostCallback([=](const Command &Cmd, int Res) {
      std::optional<llvm::sys::ProcessStatistics> ProcStat =
          Cmd.getProcessStatistics();
      if (!ProcStat)
        return;

      const char *LinkingOutput = nullptr;
      if (FinalOutput)
        LinkingOutput = FinalOutput->getValue();
      else if (!Cmd.getOutputFilenames().empty())
        LinkingOutput = Cmd.getOutputFilenames().front().c_str();
      else
        LinkingOutput = getDefaultImageName();

      if (CCPrintStatReportFilename.empty()) {
        using namespace llvm;
        // Human readable output.
        outs() << sys::path::filename(Cmd.getExecutable()) << ": "
               << "output=" << LinkingOutput;
        outs() << ", total="
               << format("%.3f", ProcStat->TotalTime.count() / 1000.) << " ms"
               << ", user="
               << format("%.3f", ProcStat->UserTime.count() / 1000.) << " ms"
               << ", mem=" << ProcStat->PeakMemory << " Kb\n";
      } else {
        // CSV format.
        std::string Buffer;
        llvm::raw_string_ostream Out(Buffer);
        llvm::sys::printArg(Out, llvm::sys::path::filename(Cmd.getExecutable()),
                            /*Quote*/ true);
        Out << ',';
        llvm::sys::printArg(Out, LinkingOutput, true);
        Out << ',' << ProcStat->TotalTime.count() << ','
            << ProcStat->UserTime.count() << ',' << ProcStat->PeakMemory
            << '\n';
        Out.flush();
        std::error_code EC;
        llvm::raw_fd_ostream OS(CCPrintStatReportFilename, EC,
                                llvm::sys::fs::OF_Append |
                                    llvm::sys::fs::OF_Text);
        if (EC)
          return;
        auto L = OS.lock();
        if (!L) {
          llvm::errs() << "ERROR: Cannot lock file "
                       << CCPrintStatReportFilename << ": "
                       << toString(L.takeError()) << "\n";
          return;
        }
        OS << Buffer;
        OS.flush();
      }
    });
  }

  // If the user passed -Qunused-arguments or there were errors, don't warn
  // about any unused arguments.
  if (Diags.hasErrorOccurred() ||
      C.getArgs().hasArg(options::OPT_Qunused_arguments))
    return;

  // Claim -fdriver-only here.
  (void)C.getArgs().hasArg(options::OPT_fdriver_only);
  // Claim -### here.
  (void)C.getArgs().hasArg(options::OPT__HASH_HASH_HASH);

  // Claim --driver-mode, --rsp-quoting, it was handled earlier.
  (void)C.getArgs().hasArg(options::OPT_driver_mode);
  (void)C.getArgs().hasArg(options::OPT_rsp_quoting);

  bool HasAssembleJob = llvm::any_of(C.getJobs(), [](auto &J) {
    // Match ClangAs and other derived assemblers of Tool. ClangAs uses a
    // longer ShortName "clang integrated assembler" while other assemblers just
    // use "assembler".
    return strstr(J.getCreator().getShortName(), "assembler");
  });
  for (Arg *A : C.getArgs()) {
    // FIXME: It would be nice to be able to send the argument to the
    // DiagnosticsEngine, so that extra values, position, and so on could be
    // printed.
    if (!A->isClaimed()) {
      if (A->getOption().hasFlag(options::NoArgumentUnused))
        continue;

      // Suppress the warning automatically if this is just a flag, and it is an
      // instance of an argument we already claimed.
      const Option &Opt = A->getOption();
      if (Opt.getKind() == Option::FlagClass) {
        bool DuplicateClaimed = false;

        for (const Arg *AA : C.getArgs().filtered(&Opt)) {
          if (AA->isClaimed()) {
            DuplicateClaimed = true;
            break;
          }
        }

        if (DuplicateClaimed)
          continue;
      }

      // In clang-cl, don't mention unknown arguments here since they have
      // already been warned about.
      if (!IsCLMode() || !A->getOption().matches(options::OPT_UNKNOWN)) {
        if (A->getOption().hasFlag(options::TargetSpecific) &&
            !A->isIgnoredTargetSpecific() && !HasAssembleJob &&
            // When for example -### or -v is used
            // without a file, target specific options are not
            // consumed/validated.
            // Instead emitting an error emit a warning instead.
            !C.getActions().empty()) {
          Diag(diag::err_drv_unsupported_opt_for_target)
              << A->getSpelling() << getTargetTriple();
        } else {
          Diag(clang::diag::warn_drv_unused_argument)
              << A->getAsString(C.getArgs());
        }
      }
    }
  }
}

namespace {
/// Utility class to control the collapse of dependent actions and select the
/// tools accordingly.
class ToolSelector final {
  /// The tool chain this selector refers to.
  const ToolChain &TC;

  /// The compilation this selector refers to.
  const Compilation &C;

  /// The base action this selector refers to.
  const JobAction *BaseAction;

  /// Set to true if the current toolchain refers to host actions.
  bool IsHostSelector;

  /// Set to true if save-temps and embed-bitcode functionalities are active.
  bool SaveTemps;
  bool EmbedBitcode;

  /// Get previous dependent action or null if that does not exist. If
  /// \a CanBeCollapsed is false, that action must be legal to collapse or
  /// null will be returned.
  const JobAction *getPrevDependentAction(const ActionList &Inputs,
                                          ActionList &SavedOffloadAction,
                                          bool CanBeCollapsed = true) {
    // An option can be collapsed only if it has a single input.
    if (Inputs.size() != 1)
      return nullptr;

    Action *CurAction = *Inputs.begin();
    if (CanBeCollapsed &&
        !CurAction->isCollapsingWithNextDependentActionLegal())
      return nullptr;

    // If the input action is an offload action. Look through it and save any
    // offload action that can be dropped in the event of a collapse.
    if (auto *OA = dyn_cast<OffloadAction>(CurAction)) {
      // If the dependent action is a device action, we will attempt to collapse
      // only with other device actions. Otherwise, we would do the same but
      // with host actions only.
      if (!IsHostSelector) {
        if (OA->hasSingleDeviceDependence(/*DoNotConsiderHostActions=*/true)) {
          CurAction =
              OA->getSingleDeviceDependence(/*DoNotConsiderHostActions=*/true);
          if (CanBeCollapsed &&
              !CurAction->isCollapsingWithNextDependentActionLegal())
            return nullptr;
          SavedOffloadAction.push_back(OA);
          return dyn_cast<JobAction>(CurAction);
        }
      } else if (OA->hasHostDependence()) {
        CurAction = OA->getHostDependence();
        if (CanBeCollapsed &&
            !CurAction->isCollapsingWithNextDependentActionLegal())
          return nullptr;
        SavedOffloadAction.push_back(OA);
        return dyn_cast<JobAction>(CurAction);
      }
      return nullptr;
    }

    return dyn_cast<JobAction>(CurAction);
  }

  /// Return true if an assemble action can be collapsed.
  bool canCollapseAssembleAction() const {
    return TC.useIntegratedAs() && !SaveTemps &&
           !C.getArgs().hasArg(options::OPT_via_file_asm) &&
           !C.getArgs().hasArg(options::OPT__SLASH_FA) &&
           !C.getArgs().hasArg(options::OPT__SLASH_Fa) &&
           !C.getArgs().hasArg(options::OPT_dxc_Fc);
  }

  /// Return true if a preprocessor action can be collapsed.
  bool canCollapsePreprocessorAction() const {
    return !C.getArgs().hasArg(options::OPT_no_integrated_cpp) &&
           !C.getArgs().hasArg(options::OPT_traditional_cpp) && !SaveTemps &&
           !C.getArgs().hasArg(options::OPT_rewrite_objc);
  }

  /// Struct that relates an action with the offload actions that would be
  /// collapsed with it.
  struct JobActionInfo final {
    /// The action this info refers to.
    const JobAction *JA = nullptr;
    /// The offload actions we need to take care off if this action is
    /// collapsed.
    ActionList SavedOffloadAction;
  };

  /// Append collapsed offload actions from the give nnumber of elements in the
  /// action info array.
  static void AppendCollapsedOffloadAction(ActionList &CollapsedOffloadAction,
                                           ArrayRef<JobActionInfo> &ActionInfo,
                                           unsigned ElementNum) {
    assert(ElementNum <= ActionInfo.size() && "Invalid number of elements.");
    for (unsigned I = 0; I < ElementNum; ++I)
      CollapsedOffloadAction.append(ActionInfo[I].SavedOffloadAction.begin(),
                                    ActionInfo[I].SavedOffloadAction.end());
  }

  /// Functions that attempt to perform the combining. They detect if that is
  /// legal, and if so they update the inputs \a Inputs and the offload action
  /// that were collapsed in \a CollapsedOffloadAction. A tool that deals with
  /// the combined action is returned. If the combining is not legal or if the
  /// tool does not exist, null is returned.
  /// Currently three kinds of collapsing are supported:
  ///  - Assemble + Backend + Compile;
  ///  - Assemble + Backend ;
  ///  - Backend + Compile.
  const Tool *
  combineAssembleBackendCompile(ArrayRef<JobActionInfo> ActionInfo,
                                ActionList &Inputs,
                                ActionList &CollapsedOffloadAction) {
    if (ActionInfo.size() < 3 || !canCollapseAssembleAction())
      return nullptr;
    auto *AJ = dyn_cast<AssembleJobAction>(ActionInfo[0].JA);
    auto *BJ = dyn_cast<BackendJobAction>(ActionInfo[1].JA);
    auto *CJ = dyn_cast<CompileJobAction>(ActionInfo[2].JA);
    if (!AJ || !BJ || !CJ)
      return nullptr;

    // Get compiler tool.
    const Tool *T = TC.SelectTool(*CJ);
    if (!T)
      return nullptr;

    // Can't collapse if we don't have codegen support unless we are
    // emitting LLVM IR.
    bool OutputIsLLVM = types::isLLVMIR(ActionInfo[0].JA->getType());
    if (!T->hasIntegratedBackend() && !(OutputIsLLVM && T->canEmitIR()))
      return nullptr;

    // When using -fembed-bitcode, it is required to have the same tool (clang)
    // for both CompilerJA and BackendJA. Otherwise, combine two stages.
    if (EmbedBitcode) {
      const Tool *BT = TC.SelectTool(*BJ);
      if (BT == T)
        return nullptr;
    }

    if (!T->hasIntegratedAssembler())
      return nullptr;

    Inputs = CJ->getInputs();
    AppendCollapsedOffloadAction(CollapsedOffloadAction, ActionInfo,
                                 /*NumElements=*/3);
    return T;
  }
  const Tool *combineAssembleBackend(ArrayRef<JobActionInfo> ActionInfo,
                                     ActionList &Inputs,
                                     ActionList &CollapsedOffloadAction) {
    if (ActionInfo.size() < 2 || !canCollapseAssembleAction())
      return nullptr;
    auto *AJ = dyn_cast<AssembleJobAction>(ActionInfo[0].JA);
    auto *BJ = dyn_cast<BackendJobAction>(ActionInfo[1].JA);
    if (!AJ || !BJ)
      return nullptr;

    // Get backend tool.
    const Tool *T = TC.SelectTool(*BJ);
    if (!T)
      return nullptr;

    if (!T->hasIntegratedAssembler())
      return nullptr;

    Inputs = BJ->getInputs();
    AppendCollapsedOffloadAction(CollapsedOffloadAction, ActionInfo,
                                 /*NumElements=*/2);
    return T;
  }
  const Tool *combineBackendCompile(ArrayRef<JobActionInfo> ActionInfo,
                                    ActionList &Inputs,
                                    ActionList &CollapsedOffloadAction) {
    if (ActionInfo.size() < 2)
      return nullptr;
    auto *BJ = dyn_cast<BackendJobAction>(ActionInfo[0].JA);
    auto *CJ = dyn_cast<CompileJobAction>(ActionInfo[1].JA);
    if (!BJ || !CJ)
      return nullptr;

    // Check if the initial input (to the compile job or its predessor if one
    // exists) is LLVM bitcode. In that case, no preprocessor step is required
    // and we can still collapse the compile and backend jobs when we have
    // -save-temps. I.e. there is no need for a separate compile job just to
    // emit unoptimized bitcode.
    bool InputIsBitcode = true;
    for (size_t i = 1; i < ActionInfo.size(); i++)
      if (ActionInfo[i].JA->getType() != types::TY_LLVM_BC &&
          ActionInfo[i].JA->getType() != types::TY_LTO_BC) {
        InputIsBitcode = false;
        break;
      }
    if (!InputIsBitcode && !canCollapsePreprocessorAction())
      return nullptr;

    // Get compiler tool.
    const Tool *T = TC.SelectTool(*CJ);
    if (!T)
      return nullptr;

    // Can't collapse if we don't have codegen support unless we are
    // emitting LLVM IR.
    bool OutputIsLLVM = types::isLLVMIR(ActionInfo[0].JA->getType());
    if (!T->hasIntegratedBackend() && !(OutputIsLLVM && T->canEmitIR()))
      return nullptr;

    if (T->canEmitIR() && ((SaveTemps && !InputIsBitcode) || EmbedBitcode))
      return nullptr;

    Inputs = CJ->getInputs();
    AppendCollapsedOffloadAction(CollapsedOffloadAction, ActionInfo,
                                 /*NumElements=*/2);
    return T;
  }

  /// Updates the inputs if the obtained tool supports combining with
  /// preprocessor action, and the current input is indeed a preprocessor
  /// action. If combining results in the collapse of offloading actions, those
  /// are appended to \a CollapsedOffloadAction.
  void combineWithPreprocessor(const Tool *T, ActionList &Inputs,
                               ActionList &CollapsedOffloadAction) {
    if (!T || !canCollapsePreprocessorAction() || !T->hasIntegratedCPP())
      return;

    // Attempt to get a preprocessor action dependence.
    ActionList PreprocessJobOffloadActions;
    ActionList NewInputs;
    for (Action *A : Inputs) {
      auto *PJ = getPrevDependentAction({A}, PreprocessJobOffloadActions);
      if (!PJ || !isa<PreprocessJobAction>(PJ)) {
        NewInputs.push_back(A);
        continue;
      }

      // This is legal to combine. Append any offload action we found and add the
      // current input to preprocessor inputs.
      CollapsedOffloadAction.append(PreprocessJobOffloadActions.begin(),
                                    PreprocessJobOffloadActions.end());
      NewInputs.append(PJ->input_begin(), PJ->input_end());
    }
    Inputs = NewInputs;
  }

public:
  ToolSelector(const JobAction *BaseAction, const ToolChain &TC,
               const Compilation &C, bool SaveTemps, bool EmbedBitcode)
      : TC(TC), C(C), BaseAction(BaseAction), SaveTemps(SaveTemps),
        EmbedBitcode(EmbedBitcode) {
    assert(BaseAction && "Invalid base action.");
    IsHostSelector = BaseAction->getOffloadingDeviceKind() == Action::OFK_None;
  }

  /// Check if a chain of actions can be combined and return the tool that can
  /// handle the combination of actions. The pointer to the current inputs \a
  /// Inputs and the list of offload actions \a CollapsedOffloadActions
  /// connected to collapsed actions are updated accordingly. The latter enables
  /// the caller of the selector to process them afterwards instead of just
  /// dropping them. If no suitable tool is found, null will be returned.
  const Tool *getTool(ActionList &Inputs,
                      ActionList &CollapsedOffloadAction) {
    //
    // Get the largest chain of actions that we could combine.
    //

    SmallVector<JobActionInfo, 5> ActionChain(1);
    ActionChain.back().JA = BaseAction;
    while (ActionChain.back().JA) {
      const Action *CurAction = ActionChain.back().JA;

      // Grow the chain by one element.
      ActionChain.resize(ActionChain.size() + 1);
      JobActionInfo &AI = ActionChain.back();

      // Attempt to fill it with the
      AI.JA =
          getPrevDependentAction(CurAction->getInputs(), AI.SavedOffloadAction);
    }

    // Pop the last action info as it could not be filled.
    ActionChain.pop_back();

    //
    // Attempt to combine actions. If all combining attempts failed, just return
    // the tool of the provided action. At the end we attempt to combine the
    // action with any preprocessor action it may depend on.
    //

    const Tool *T = combineAssembleBackendCompile(ActionChain, Inputs,
                                                  CollapsedOffloadAction);
    if (!T)
      T = combineAssembleBackend(ActionChain, Inputs, CollapsedOffloadAction);
    if (!T)
      T = combineBackendCompile(ActionChain, Inputs, CollapsedOffloadAction);
    if (!T) {
      Inputs = BaseAction->getInputs();
      T = TC.SelectTool(*BaseAction);
    }

    combineWithPreprocessor(T, Inputs, CollapsedOffloadAction);
    return T;
  }
};
}

/// Return a string that uniquely identifies the result of a job. The bound arch
/// is not necessarily represented in the toolchain's triple -- for example,
/// armv7 and armv7s both map to the same triple -- so we need both in our map.
/// Also, we need to add the offloading device kind, as the same tool chain can
/// be used for host and device for some programming models, e.g. OpenMP.
static std::string GetTriplePlusArchString(const ToolChain *TC,
                                           StringRef BoundArch,
                                           Action::OffloadKind OffloadKind) {
  std::string TriplePlusArch = TC->getTriple().normalize();
  if (!BoundArch.empty()) {
    TriplePlusArch += "-";
    TriplePlusArch += BoundArch;
  }
  TriplePlusArch += "-";
  TriplePlusArch += Action::GetOffloadKindName(OffloadKind);
  return TriplePlusArch;
}

InputInfoList Driver::BuildJobsForAction(
    Compilation &C, const Action *A, const ToolChain *TC, StringRef BoundArch,
    bool AtTopLevel, bool MultipleArchs, const char *LinkingOutput,
    std::map<std::pair<const Action *, std::string>, InputInfoList>
        &CachedResults,
    Action::OffloadKind TargetDeviceOffloadKind) const {
  std::pair<const Action *, std::string> ActionTC = {
      A, GetTriplePlusArchString(TC, BoundArch, TargetDeviceOffloadKind)};
  auto CachedResult = CachedResults.find(ActionTC);
  if (CachedResult != CachedResults.end()) {
    return CachedResult->second;
  }
  InputInfoList Result = BuildJobsForActionNoCache(
      C, A, TC, BoundArch, AtTopLevel, MultipleArchs, LinkingOutput,
      CachedResults, TargetDeviceOffloadKind);
  CachedResults[ActionTC] = Result;
  return Result;
}

static void handleTimeTrace(Compilation &C, const ArgList &Args,
                            const JobAction *JA, const char *BaseInput,
                            const InputInfo &Result) {
  Arg *A =
      Args.getLastArg(options::OPT_ftime_trace, options::OPT_ftime_trace_EQ);
  if (!A)
    return;
  SmallString<128> Path;
  if (A->getOption().matches(options::OPT_ftime_trace_EQ)) {
    Path = A->getValue();
    if (llvm::sys::fs::is_directory(Path)) {
      SmallString<128> Tmp(Result.getFilename());
      llvm::sys::path::replace_extension(Tmp, "json");
      llvm::sys::path::append(Path, llvm::sys::path::filename(Tmp));
    }
  } else {
    if (Arg *DumpDir = Args.getLastArgNoClaim(options::OPT_dumpdir)) {
      // The trace file is ${dumpdir}${basename}.json. Note that dumpdir may not
      // end with a path separator.
      Path = DumpDir->getValue();
      Path += llvm::sys::path::filename(BaseInput);
    } else {
      Path = Result.getFilename();
    }
    llvm::sys::path::replace_extension(Path, "json");
  }
  const char *ResultFile = C.getArgs().MakeArgString(Path);
  C.addTimeTraceFile(ResultFile, JA);
  C.addResultFile(ResultFile, JA);
}

InputInfoList Driver::BuildJobsForActionNoCache(
    Compilation &C, const Action *A, const ToolChain *TC, StringRef BoundArch,
    bool AtTopLevel, bool MultipleArchs, const char *LinkingOutput,
    std::map<std::pair<const Action *, std::string>, InputInfoList>
        &CachedResults,
    Action::OffloadKind TargetDeviceOffloadKind) const {
  llvm::PrettyStackTraceString CrashInfo("Building compilation jobs");

  InputInfoList OffloadDependencesInputInfo;
  bool BuildingForOffloadDevice = TargetDeviceOffloadKind != Action::OFK_None;
  if (const OffloadAction *OA = dyn_cast<OffloadAction>(A)) {
    // The 'Darwin' toolchain is initialized only when its arguments are
    // computed. Get the default arguments for OFK_None to ensure that
    // initialization is performed before processing the offload action.
    // FIXME: Remove when darwin's toolchain is initialized during construction.
    C.getArgsForToolChain(TC, BoundArch, Action::OFK_None);

    // The offload action is expected to be used in four different situations.
    //
    // a) Set a toolchain/architecture/kind for a host action:
    //    Host Action 1 -> OffloadAction -> Host Action 2
    //
    // b) Set a toolchain/architecture/kind for a device action;
    //    Device Action 1 -> OffloadAction -> Device Action 2
    //
    // c) Specify a device dependence to a host action;
    //    Device Action 1  _
    //                      \
    //      Host Action 1  ---> OffloadAction -> Host Action 2
    //
    // d) Specify a host dependence to a device action.
    //      Host Action 1  _
    //                      \
    //    Device Action 1  ---> OffloadAction -> Device Action 2
    //
    // For a) and b), we just return the job generated for the dependences. For
    // c) and d) we override the current action with the host/device dependence
    // if the current toolchain is host/device and set the offload dependences
    // info with the jobs obtained from the device/host dependence(s).

    // If there is a single device option or has no host action, just generate
    // the job for it.
    if (OA->hasSingleDeviceDependence() || !OA->hasHostDependence()) {
      InputInfoList DevA;
      OA->doOnEachDeviceDependence([&](Action *DepA, const ToolChain *DepTC,
                                       const char *DepBoundArch) {
        DevA.append(BuildJobsForAction(C, DepA, DepTC, DepBoundArch, AtTopLevel,
                                       /*MultipleArchs*/ !!DepBoundArch,
                                       LinkingOutput, CachedResults,
                                       DepA->getOffloadingDeviceKind()));
      });
      return DevA;
    }

    // If 'Action 2' is host, we generate jobs for the device dependences and
    // override the current action with the host dependence. Otherwise, we
    // generate the host dependences and override the action with the device
    // dependence. The dependences can't therefore be a top-level action.
    OA->doOnEachDependence(
        /*IsHostDependence=*/BuildingForOffloadDevice,
        [&](Action *DepA, const ToolChain *DepTC, const char *DepBoundArch) {
          OffloadDependencesInputInfo.append(BuildJobsForAction(
              C, DepA, DepTC, DepBoundArch, /*AtTopLevel=*/false,
              /*MultipleArchs*/ !!DepBoundArch, LinkingOutput, CachedResults,
              DepA->getOffloadingDeviceKind()));
        });

    A = BuildingForOffloadDevice
            ? OA->getSingleDeviceDependence(/*DoNotConsiderHostActions=*/true)
            : OA->getHostDependence();

    // We may have already built this action as a part of the offloading
    // toolchain, return the cached input if so.
    std::pair<const Action *, std::string> ActionTC = {
        OA->getHostDependence(),
        GetTriplePlusArchString(TC, BoundArch, TargetDeviceOffloadKind)};
    if (CachedResults.find(ActionTC) != CachedResults.end()) {
      InputInfoList Inputs = CachedResults[ActionTC];
      Inputs.append(OffloadDependencesInputInfo);
      return Inputs;
    }
  }

  if (const InputAction *IA = dyn_cast<InputAction>(A)) {
    // FIXME: It would be nice to not claim this here; maybe the old scheme of
    // just using Args was better?
    const Arg &Input = IA->getInputArg();
    Input.claim();
    if (Input.getOption().matches(options::OPT_INPUT)) {
      const char *Name = Input.getValue();
      return {InputInfo(A, Name, /* _BaseInput = */ Name)};
    }
    return {InputInfo(A, &Input, /* _BaseInput = */ "")};
  }

  if (const BindArchAction *BAA = dyn_cast<BindArchAction>(A)) {
    const ToolChain *TC;
    StringRef ArchName = BAA->getArchName();

    if (!ArchName.empty())
      TC = &getToolChain(C.getArgs(),
                         computeTargetTriple(*this, TargetTriple,
                                             C.getArgs(), ArchName));
    else
      TC = &C.getDefaultToolChain();

    return BuildJobsForAction(C, *BAA->input_begin(), TC, ArchName, AtTopLevel,
                              MultipleArchs, LinkingOutput, CachedResults,
                              TargetDeviceOffloadKind);
  }


  ActionList Inputs = A->getInputs();

  const JobAction *JA = cast<JobAction>(A);
  ActionList CollapsedOffloadActions;

  ToolSelector TS(JA, *TC, C, isSaveTempsEnabled(),
                  embedBitcodeInObject() && !isUsingLTO());
  const Tool *T = TS.getTool(Inputs, CollapsedOffloadActions);

  if (!T)
    return {InputInfo()};

  // If we've collapsed action list that contained OffloadAction we
  // need to build jobs for host/device-side inputs it may have held.
  for (const auto *OA : CollapsedOffloadActions)
    cast<OffloadAction>(OA)->doOnEachDependence(
        /*IsHostDependence=*/BuildingForOffloadDevice,
        [&](Action *DepA, const ToolChain *DepTC, const char *DepBoundArch) {
          OffloadDependencesInputInfo.append(BuildJobsForAction(
              C, DepA, DepTC, DepBoundArch, /* AtTopLevel */ false,
              /*MultipleArchs=*/!!DepBoundArch, LinkingOutput, CachedResults,
              DepA->getOffloadingDeviceKind()));
        });

  // Only use pipes when there is exactly one input.
  InputInfoList InputInfos;
  for (const Action *Input : Inputs) {
    // Treat dsymutil and verify sub-jobs as being at the top-level too, they
    // shouldn't get temporary output names.
    // FIXME: Clean this up.
    bool SubJobAtTopLevel =
        AtTopLevel && (isa<DsymutilJobAction>(A) || isa<VerifyJobAction>(A));
    InputInfos.append(BuildJobsForAction(
        C, Input, TC, BoundArch, SubJobAtTopLevel, MultipleArchs, LinkingOutput,
        CachedResults, A->getOffloadingDeviceKind()));
  }

  // Always use the first file input as the base input.
  const char *BaseInput = InputInfos[0].getBaseInput();
  for (auto &Info : InputInfos) {
    if (Info.isFilename()) {
      BaseInput = Info.getBaseInput();
      break;
    }
  }

  // ... except dsymutil actions, which use their actual input as the base
  // input.
  if (JA->getType() == types::TY_dSYM)
    BaseInput = InputInfos[0].getFilename();

  // Append outputs of offload device jobs to the input list
  if (!OffloadDependencesInputInfo.empty())
    InputInfos.append(OffloadDependencesInputInfo.begin(),
                      OffloadDependencesInputInfo.end());

  // Set the effective triple of the toolchain for the duration of this job.
  llvm::Triple EffectiveTriple;
  const ToolChain &ToolTC = T->getToolChain();
  const ArgList &Args =
      C.getArgsForToolChain(TC, BoundArch, A->getOffloadingDeviceKind());
  if (InputInfos.size() != 1) {
    EffectiveTriple = llvm::Triple(ToolTC.ComputeEffectiveClangTriple(Args));
  } else {
    // Pass along the input type if it can be unambiguously determined.
    EffectiveTriple = llvm::Triple(
        ToolTC.ComputeEffectiveClangTriple(Args, InputInfos[0].getType()));
  }
  RegisterEffectiveTriple TripleRAII(ToolTC, EffectiveTriple);

  // Determine the place to write output to, if any.
  InputInfo Result;
  InputInfoList UnbundlingResults;
  if (auto *UA = dyn_cast<OffloadUnbundlingJobAction>(JA)) {
    // If we have an unbundling job, we need to create results for all the
    // outputs. We also update the results cache so that other actions using
    // this unbundling action can get the right results.
    for (auto &UI : UA->getDependentActionsInfo()) {
      assert(UI.DependentOffloadKind != Action::OFK_None &&
             "Unbundling with no offloading??");

      // Unbundling actions are never at the top level. When we generate the
      // offloading prefix, we also do that for the host file because the
      // unbundling action does not change the type of the output which can
      // cause a overwrite.
      std::string OffloadingPrefix = Action::GetOffloadingFileNamePrefix(
          UI.DependentOffloadKind,
          UI.DependentToolChain->getTriple().normalize(),
          /*CreatePrefixForHost=*/true);
      auto CurI = InputInfo(
          UA,
          GetNamedOutputPath(C, *UA, BaseInput, UI.DependentBoundArch,
                             /*AtTopLevel=*/false,
                             MultipleArchs ||
                                 UI.DependentOffloadKind == Action::OFK_HIP,
                             OffloadingPrefix),
          BaseInput);
      // Save the unbundling result.
      UnbundlingResults.push_back(CurI);

      // Get the unique string identifier for this dependence and cache the
      // result.
      StringRef Arch;
      if (TargetDeviceOffloadKind == Action::OFK_HIP) {
        if (UI.DependentOffloadKind == Action::OFK_Host)
          Arch = StringRef();
        else
          Arch = UI.DependentBoundArch;
      } else
        Arch = BoundArch;

      CachedResults[{A, GetTriplePlusArchString(UI.DependentToolChain, Arch,
                                                UI.DependentOffloadKind)}] = {
          CurI};
    }

    // Now that we have all the results generated, select the one that should be
    // returned for the current depending action.
    std::pair<const Action *, std::string> ActionTC = {
        A, GetTriplePlusArchString(TC, BoundArch, TargetDeviceOffloadKind)};
    assert(CachedResults.find(ActionTC) != CachedResults.end() &&
           "Result does not exist??");
    Result = CachedResults[ActionTC].front();
  } else if (JA->getType() == types::TY_Nothing)
    Result = {InputInfo(A, BaseInput)};
  else {
    // We only have to generate a prefix for the host if this is not a top-level
    // action.
    std::string OffloadingPrefix = Action::GetOffloadingFileNamePrefix(
        A->getOffloadingDeviceKind(), TC->getTriple().normalize(),
        /*CreatePrefixForHost=*/isa<OffloadPackagerJobAction>(A) ||
            !(A->getOffloadingHostActiveKinds() == Action::OFK_None ||
              AtTopLevel));
    Result = InputInfo(A, GetNamedOutputPath(C, *JA, BaseInput, BoundArch,
                                             AtTopLevel, MultipleArchs,
                                             OffloadingPrefix),
                       BaseInput);
    if (T->canEmitIR() && OffloadingPrefix.empty())
      handleTimeTrace(C, Args, JA, BaseInput, Result);
  }

  if (CCCPrintBindings && !CCGenDiagnostics) {
    llvm::errs() << "# \"" << T->getToolChain().getTripleString() << '"'
                 << " - \"" << T->getName() << "\", inputs: [";
    for (unsigned i = 0, e = InputInfos.size(); i != e; ++i) {
      llvm::errs() << InputInfos[i].getAsString();
      if (i + 1 != e)
        llvm::errs() << ", ";
    }
    if (UnbundlingResults.empty())
      llvm::errs() << "], output: " << Result.getAsString() << "\n";
    else {
      llvm::errs() << "], outputs: [";
      for (unsigned i = 0, e = UnbundlingResults.size(); i != e; ++i) {
        llvm::errs() << UnbundlingResults[i].getAsString();
        if (i + 1 != e)
          llvm::errs() << ", ";
      }
      llvm::errs() << "] \n";
    }
  } else {
    if (UnbundlingResults.empty())
      T->ConstructJob(
          C, *JA, Result, InputInfos,
          C.getArgsForToolChain(TC, BoundArch, JA->getOffloadingDeviceKind()),
          LinkingOutput);
    else
      T->ConstructJobMultipleOutputs(
          C, *JA, UnbundlingResults, InputInfos,
          C.getArgsForToolChain(TC, BoundArch, JA->getOffloadingDeviceKind()),
          LinkingOutput);
  }
  return {Result};
}

const char *Driver::getDefaultImageName() const {
  llvm::Triple Target(llvm::Triple::normalize(TargetTriple));
  return Target.isOSWindows() ? "a.exe" : "a.out";
}

/// Create output filename based on ArgValue, which could either be a
/// full filename, filename without extension, or a directory. If ArgValue
/// does not provide a filename, then use BaseName, and use the extension
/// suitable for FileType.
static const char *MakeCLOutputFilename(const ArgList &Args, StringRef ArgValue,
                                        StringRef BaseName,
                                        types::ID FileType) {
  SmallString<128> Filename = ArgValue;

  if (ArgValue.empty()) {
    // If the argument is empty, output to BaseName in the current dir.
    Filename = BaseName;
  } else if (llvm::sys::path::is_separator(Filename.back())) {
    // If the argument is a directory, output to BaseName in that dir.
    llvm::sys::path::append(Filename, BaseName);
  }

  if (!llvm::sys::path::has_extension(ArgValue)) {
    // If the argument didn't provide an extension, then set it.
    const char *Extension = types::getTypeTempSuffix(FileType, true);

    if (FileType == types::TY_Image &&
        Args.hasArg(options::OPT__SLASH_LD, options::OPT__SLASH_LDd)) {
      // The output file is a dll.
      Extension = "dll";
    }

    llvm::sys::path::replace_extension(Filename, Extension);
  }

  return Args.MakeArgString(Filename.c_str());
}

static bool HasPreprocessOutput(const Action &JA) {
  if (isa<PreprocessJobAction>(JA))
    return true;
  if (isa<OffloadAction>(JA) && isa<PreprocessJobAction>(JA.getInputs()[0]))
    return true;
  if (isa<OffloadBundlingJobAction>(JA) &&
      HasPreprocessOutput(*(JA.getInputs()[0])))
    return true;
  return false;
}

const char *Driver::CreateTempFile(Compilation &C, StringRef Prefix,
                                   StringRef Suffix, bool MultipleArchs,
                                   StringRef BoundArch,
                                   bool NeedUniqueDirectory) const {
  SmallString<128> TmpName;
  Arg *A = C.getArgs().getLastArg(options::OPT_fcrash_diagnostics_dir);
  std::optional<std::string> CrashDirectory =
      CCGenDiagnostics && A
          ? std::string(A->getValue())
          : llvm::sys::Process::GetEnv("CLANG_CRASH_DIAGNOSTICS_DIR");
  if (CrashDirectory) {
    if (!getVFS().exists(*CrashDirectory))
      llvm::sys::fs::create_directories(*CrashDirectory);
    SmallString<128> Path(*CrashDirectory);
    llvm::sys::path::append(Path, Prefix);
    const char *Middle = !Suffix.empty() ? "-%%%%%%." : "-%%%%%%";
    if (std::error_code EC =
            llvm::sys::fs::createUniqueFile(Path + Middle + Suffix, TmpName)) {
      Diag(clang::diag::err_unable_to_make_temp) << EC.message();
      return "";
    }
  } else {
    if (MultipleArchs && !BoundArch.empty()) {
      if (NeedUniqueDirectory) {
        TmpName = GetTemporaryDirectory(Prefix);
        llvm::sys::path::append(TmpName,
                                Twine(Prefix) + "-" + BoundArch + "." + Suffix);
      } else {
        TmpName =
            GetTemporaryPath((Twine(Prefix) + "-" + BoundArch).str(), Suffix);
      }

    } else {
      TmpName = GetTemporaryPath(Prefix, Suffix);
    }
  }
  return C.addTempFile(C.getArgs().MakeArgString(TmpName));
}

// Calculate the output path of the module file when compiling a module unit
// with the `-fmodule-output` option or `-fmodule-output=` option specified.
// The behavior is:
// - If `-fmodule-output=` is specfied, then the module file is
//   writing to the value.
// - Otherwise if the output object file of the module unit is specified, the
// output path
//   of the module file should be the same with the output object file except
//   the corresponding suffix. This requires both `-o` and `-c` are specified.
// - Otherwise, the output path of the module file will be the same with the
//   input with the corresponding suffix.
static const char *GetModuleOutputPath(Compilation &C, const JobAction &JA,
                                       const char *BaseInput) {
  assert(isa<PrecompileJobAction>(JA) && JA.getType() == types::TY_ModuleFile &&
         (C.getArgs().hasArg(options::OPT_fmodule_output) ||
          C.getArgs().hasArg(options::OPT_fmodule_output_EQ)));

  SmallString<256> OutputPath =
      tools::getCXX20NamedModuleOutputPath(C.getArgs(), BaseInput);

  return C.addResultFile(C.getArgs().MakeArgString(OutputPath.c_str()), &JA);
}

const char *Driver::GetNamedOutputPath(Compilation &C, const JobAction &JA,
                                       const char *BaseInput,
                                       StringRef OrigBoundArch, bool AtTopLevel,
                                       bool MultipleArchs,
                                       StringRef OffloadingPrefix) const {
  std::string BoundArch = OrigBoundArch.str();
  if (is_style_windows(llvm::sys::path::Style::native)) {
    // BoundArch may contains ':', which is invalid in file names on Windows,
    // therefore replace it with '%'.
    std::replace(BoundArch.begin(), BoundArch.end(), ':', '@');
  }

  llvm::PrettyStackTraceString CrashInfo("Computing output path");
  // Output to a user requested destination?
  if (AtTopLevel && !isa<DsymutilJobAction>(JA) && !isa<VerifyJobAction>(JA)) {
    if (Arg *FinalOutput = C.getArgs().getLastArg(options::OPT_o))
      return C.addResultFile(FinalOutput->getValue(), &JA);
  }

  // For /P, preprocess to file named after BaseInput.
  if (C.getArgs().hasArg(options::OPT__SLASH_P)) {
    assert(AtTopLevel && isa<PreprocessJobAction>(JA));
    StringRef BaseName = llvm::sys::path::filename(BaseInput);
    StringRef NameArg;
    if (Arg *A = C.getArgs().getLastArg(options::OPT__SLASH_Fi))
      NameArg = A->getValue();
    return C.addResultFile(
        MakeCLOutputFilename(C.getArgs(), NameArg, BaseName, types::TY_PP_C),
        &JA);
  }

  // Default to writing to stdout?
  if (AtTopLevel && !CCGenDiagnostics && HasPreprocessOutput(JA)) {
    return "-";
  }

  if (JA.getType() == types::TY_ModuleFile &&
      C.getArgs().getLastArg(options::OPT_module_file_info)) {
    return "-";
  }

  if (JA.getType() == types::TY_PP_Asm &&
      C.getArgs().hasArg(options::OPT_dxc_Fc)) {
    StringRef FcValue = C.getArgs().getLastArgValue(options::OPT_dxc_Fc);
    // TODO: Should we use `MakeCLOutputFilename` here? If so, we can probably
    // handle this as part of the SLASH_Fa handling below.
    return C.addResultFile(C.getArgs().MakeArgString(FcValue.str()), &JA);
  }

  if (JA.getType() == types::TY_Object &&
      C.getArgs().hasArg(options::OPT_dxc_Fo)) {
    StringRef FoValue = C.getArgs().getLastArgValue(options::OPT_dxc_Fo);
    // TODO: Should we use `MakeCLOutputFilename` here? If so, we can probably
    // handle this as part of the SLASH_Fo handling below.
    return C.addResultFile(C.getArgs().MakeArgString(FoValue.str()), &JA);
  }

  // Is this the assembly listing for /FA?
  if (JA.getType() == types::TY_PP_Asm &&
      (C.getArgs().hasArg(options::OPT__SLASH_FA) ||
       C.getArgs().hasArg(options::OPT__SLASH_Fa))) {
    // Use /Fa and the input filename to determine the asm file name.
    StringRef BaseName = llvm::sys::path::filename(BaseInput);
    StringRef FaValue = C.getArgs().getLastArgValue(options::OPT__SLASH_Fa);
    return C.addResultFile(
        MakeCLOutputFilename(C.getArgs(), FaValue, BaseName, JA.getType()),
        &JA);
  }

  if (JA.getType() == types::TY_API_INFO &&
      C.getArgs().hasArg(options::OPT_emit_extension_symbol_graphs) &&
      C.getArgs().hasArg(options::OPT_o))
    Diag(clang::diag::err_drv_unexpected_symbol_graph_output)
        << C.getArgs().getLastArgValue(options::OPT_o);

  // DXC defaults to standard out when generating assembly. We check this after
  // any DXC flags that might specify a file.
  if (AtTopLevel && JA.getType() == types::TY_PP_Asm && IsDXCMode())
    return "-";

  bool SpecifiedModuleOutput =
      C.getArgs().hasArg(options::OPT_fmodule_output) ||
      C.getArgs().hasArg(options::OPT_fmodule_output_EQ);
  if (MultipleArchs && SpecifiedModuleOutput)
    Diag(clang::diag::err_drv_module_output_with_multiple_arch);

  // If we're emitting a module output with the specified option
  // `-fmodule-output`.
  if (!AtTopLevel && isa<PrecompileJobAction>(JA) &&
      JA.getType() == types::TY_ModuleFile && SpecifiedModuleOutput) {
    assert(!C.getArgs().hasArg(options::OPT_modules_reduced_bmi));
    return GetModuleOutputPath(C, JA, BaseInput);
  }

  // Output to a temporary file?
  if ((!AtTopLevel && !isSaveTempsEnabled() &&
       !C.getArgs().hasArg(options::OPT__SLASH_Fo)) ||
      CCGenDiagnostics) {
    StringRef Name = llvm::sys::path::filename(BaseInput);
    std::pair<StringRef, StringRef> Split = Name.split('.');
    const char *Suffix =
        types::getTypeTempSuffix(JA.getType(), IsCLMode() || IsDXCMode());
    // The non-offloading toolchain on Darwin requires deterministic input
    // file name for binaries to be deterministic, therefore it needs unique
    // directory.
    llvm::Triple Triple(C.getDriver().getTargetTriple());
    bool NeedUniqueDirectory =
        (JA.getOffloadingDeviceKind() == Action::OFK_None ||
         JA.getOffloadingDeviceKind() == Action::OFK_Host) &&
        Triple.isOSDarwin();
    return CreateTempFile(C, Split.first, Suffix, MultipleArchs, BoundArch,
                          NeedUniqueDirectory);
  }

  SmallString<128> BasePath(BaseInput);
  SmallString<128> ExternalPath("");
  StringRef BaseName;

  // Dsymutil actions should use the full path.
  if (isa<DsymutilJobAction>(JA) && C.getArgs().hasArg(options::OPT_dsym_dir)) {
    ExternalPath += C.getArgs().getLastArg(options::OPT_dsym_dir)->getValue();
    // We use posix style here because the tests (specifically
    // darwin-dsymutil.c) demonstrate that posix style paths are acceptable
    // even on Windows and if we don't then the similar test covering this
    // fails.
    llvm::sys::path::append(ExternalPath, llvm::sys::path::Style::posix,
                            llvm::sys::path::filename(BasePath));
    BaseName = ExternalPath;
  } else if (isa<DsymutilJobAction>(JA) || isa<VerifyJobAction>(JA))
    BaseName = BasePath;
  else
    BaseName = llvm::sys::path::filename(BasePath);

  // Determine what the derived output name should be.
  const char *NamedOutput;

  if ((JA.getType() == types::TY_Object || JA.getType() == types::TY_LTO_BC) &&
      C.getArgs().hasArg(options::OPT__SLASH_Fo, options::OPT__SLASH_o)) {
    // The /Fo or /o flag decides the object filename.
    StringRef Val =
        C.getArgs()
            .getLastArg(options::OPT__SLASH_Fo, options::OPT__SLASH_o)
            ->getValue();
    NamedOutput =
        MakeCLOutputFilename(C.getArgs(), Val, BaseName, types::TY_Object);
  } else if (JA.getType() == types::TY_Image &&
             C.getArgs().hasArg(options::OPT__SLASH_Fe,
                                options::OPT__SLASH_o)) {
    // The /Fe or /o flag names the linked file.
    StringRef Val =
        C.getArgs()
            .getLastArg(options::OPT__SLASH_Fe, options::OPT__SLASH_o)
            ->getValue();
    NamedOutput =
        MakeCLOutputFilename(C.getArgs(), Val, BaseName, types::TY_Image);
  } else if (JA.getType() == types::TY_Image) {
    if (IsCLMode()) {
      // clang-cl uses BaseName for the executable name.
      NamedOutput =
          MakeCLOutputFilename(C.getArgs(), "", BaseName, types::TY_Image);
    } else {
      SmallString<128> Output(getDefaultImageName());
      // HIP image for device compilation with -fno-gpu-rdc is per compilation
      // unit.
      bool IsHIPNoRDC = JA.getOffloadingDeviceKind() == Action::OFK_HIP &&
                        !C.getArgs().hasFlag(options::OPT_fgpu_rdc,
                                             options::OPT_fno_gpu_rdc, false);
      bool UseOutExtension = IsHIPNoRDC || isa<OffloadPackagerJobAction>(JA);
      if (UseOutExtension) {
        Output = BaseName;
        llvm::sys::path::replace_extension(Output, "");
      }
      Output += OffloadingPrefix;
      if (MultipleArchs && !BoundArch.empty()) {
        Output += "-";
        Output.append(BoundArch);
      }
      if (UseOutExtension)
        Output += ".out";
      NamedOutput = C.getArgs().MakeArgString(Output.c_str());
    }
  } else if (JA.getType() == types::TY_PCH && IsCLMode()) {
    NamedOutput = C.getArgs().MakeArgString(GetClPchPath(C, BaseName));
  } else if ((JA.getType() == types::TY_Plist || JA.getType() == types::TY_AST) &&
             C.getArgs().hasArg(options::OPT__SLASH_o)) {
    StringRef Val =
        C.getArgs()
            .getLastArg(options::OPT__SLASH_o)
            ->getValue();
    NamedOutput =
        MakeCLOutputFilename(C.getArgs(), Val, BaseName, types::TY_Object);
  } else {
    const char *Suffix =
        types::getTypeTempSuffix(JA.getType(), IsCLMode() || IsDXCMode());
    assert(Suffix && "All types used for output should have a suffix.");

    std::string::size_type End = std::string::npos;
    if (!types::appendSuffixForType(JA.getType()))
      End = BaseName.rfind('.');
    SmallString<128> Suffixed(BaseName.substr(0, End));
    Suffixed += OffloadingPrefix;
    if (MultipleArchs && !BoundArch.empty()) {
      Suffixed += "-";
      Suffixed.append(BoundArch);
    }
    // When using both -save-temps and -emit-llvm, use a ".tmp.bc" suffix for
    // the unoptimized bitcode so that it does not get overwritten by the ".bc"
    // optimized bitcode output.
    auto IsAMDRDCInCompilePhase = [](const JobAction &JA,
                                     const llvm::opt::DerivedArgList &Args) {
      // The relocatable compilation in HIP and OpenMP implies -emit-llvm.
      // Similarly, use a ".tmp.bc" suffix for the unoptimized bitcode
      // (generated in the compile phase.)
      const ToolChain *TC = JA.getOffloadingToolChain();
      return isa<CompileJobAction>(JA) &&
             ((JA.getOffloadingDeviceKind() == Action::OFK_HIP &&
               Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc,
                            false)) ||
              (JA.getOffloadingDeviceKind() == Action::OFK_OpenMP && TC &&
               TC->getTriple().isAMDGPU()));
    };
    if (!AtTopLevel && JA.getType() == types::TY_LLVM_BC &&
        (C.getArgs().hasArg(options::OPT_emit_llvm) ||
         IsAMDRDCInCompilePhase(JA, C.getArgs())))
      Suffixed += ".tmp";
    Suffixed += '.';
    Suffixed += Suffix;
    NamedOutput = C.getArgs().MakeArgString(Suffixed.c_str());
  }

  // Prepend object file path if -save-temps=obj
  if (!AtTopLevel && isSaveTempsObj() && C.getArgs().hasArg(options::OPT_o) &&
      JA.getType() != types::TY_PCH) {
    Arg *FinalOutput = C.getArgs().getLastArg(options::OPT_o);
    SmallString<128> TempPath(FinalOutput->getValue());
    llvm::sys::path::remove_filename(TempPath);
    StringRef OutputFileName = llvm::sys::path::filename(NamedOutput);
    llvm::sys::path::append(TempPath, OutputFileName);
    NamedOutput = C.getArgs().MakeArgString(TempPath.c_str());
  }

  // If we're saving temps and the temp file conflicts with the input file,
  // then avoid overwriting input file.
  if (!AtTopLevel && isSaveTempsEnabled() && NamedOutput == BaseName) {
    bool SameFile = false;
    SmallString<256> Result;
    llvm::sys::fs::current_path(Result);
    llvm::sys::path::append(Result, BaseName);
    llvm::sys::fs::equivalent(BaseInput, Result.c_str(), SameFile);
    // Must share the same path to conflict.
    if (SameFile) {
      StringRef Name = llvm::sys::path::filename(BaseInput);
      std::pair<StringRef, StringRef> Split = Name.split('.');
      std::string TmpName = GetTemporaryPath(
          Split.first,
          types::getTypeTempSuffix(JA.getType(), IsCLMode() || IsDXCMode()));
      return C.addTempFile(C.getArgs().MakeArgString(TmpName));
    }
  }

  // As an annoying special case, PCH generation doesn't strip the pathname.
  if (JA.getType() == types::TY_PCH && !IsCLMode()) {
    llvm::sys::path::remove_filename(BasePath);
    if (BasePath.empty())
      BasePath = NamedOutput;
    else
      llvm::sys::path::append(BasePath, NamedOutput);
    return C.addResultFile(C.getArgs().MakeArgString(BasePath.c_str()), &JA);
  }

  return C.addResultFile(NamedOutput, &JA);
}


namespace {
static std::optional<std::string> findFile(StringRef path1, const Twine &path2) {
  SmallString<128> s;
  llvm::sys::path::append(s, path1, path2);

  if (llvm::sys::fs::exists(s))
    return std::string(s);
  return std::nullopt;
}

// Must be in sync with findMajMinShlib in lld/ELF/DriverUtils.cpp.
std::optional<std::string> findMajMinShlib(StringRef dir, const Twine& libNameSo) {
  // Handle OpenBSD-style maj/min shlib scheme
  llvm::SmallString<128> Scratch;
  const StringRef LibName = (libNameSo + ".").toStringRef(Scratch);
  int MaxMaj = -1, MaxMin = -1;
  std::error_code EC;
  for (llvm::sys::fs::directory_iterator LI(dir, EC), LE;
       LI != LE; LI = LI.increment(EC)) {
    StringRef FilePath = LI->path();
    StringRef FileName = llvm::sys::path::filename(FilePath);
    if (!(FileName.starts_with(LibName)))
      continue;
    std::pair<StringRef, StringRef> MajMin =
      FileName.substr(LibName.size()).split('.');
    int Maj, Min;
    if (MajMin.first.getAsInteger(10, Maj) || Maj < 0)
      continue;
    if (MajMin.second.getAsInteger(10, Min) || Min < 0)
      continue;
    if (Maj > MaxMaj)
      MaxMaj = Maj, MaxMin = Min;
    if (MaxMaj == Maj && Min > MaxMin)
      MaxMin = Min;
  }
  if (MaxMaj >= 0)
    return findFile(dir, LibName + Twine(MaxMaj) + "." + Twine(MaxMin));
  return std::nullopt;
}
}  // namespace

std::string Driver::GetFilePath(StringRef Name, const ToolChain &TC) const {
  const bool lookForLibDotSo = Name.starts_with("lib") && Name.ends_with(".so");
  // Search for Name in a list of paths.
  auto SearchPaths = [&](const llvm::SmallVectorImpl<std::string> &P)
      -> std::optional<std::string> {
    // Respect a limited subset of the '-Bprefix' functionality in GCC by
    // attempting to use this prefix when looking for file paths.
    for (const auto &Dir : P) {
      if (Dir.empty())
        continue;
      SmallString<128> P(Dir[0] == '=' ? SysRoot + Dir.substr(1) : Dir);
      if (!lookForLibDotSo) {
	llvm::sys::path::append(P, Name);
	if (llvm::sys::fs::exists(Twine(P)))
	  return std::string(P);
      } else {
	if (auto s = findMajMinShlib(P, Name))
	  return std::string(*s);
      }
    }
    return std::nullopt;
  };

  if (auto P = SearchPaths(PrefixDirs))
    return *P;

  SmallString<128> R(ResourceDir);
  llvm::sys::path::append(R, Name);
  if (llvm::sys::fs::exists(Twine(R)))
    return std::string(R);

  SmallString<128> P(TC.getCompilerRTPath());
  llvm::sys::path::append(P, Name);
  if (llvm::sys::fs::exists(Twine(P)))
    return std::string(P);

  SmallString<128> D(Dir);
  llvm::sys::path::append(D, "..", Name);
  if (llvm::sys::fs::exists(Twine(D)))
    return std::string(D);

  if (auto P = SearchPaths(TC.getLibraryPaths()))
    return *P;

  if (auto P = SearchPaths(TC.getFilePaths()))
    return *P;

  SmallString<128> R2(ResourceDir);
  llvm::sys::path::append(R2, "..", "..", Name);
  if (llvm::sys::fs::exists(Twine(R2)))
    return std::string(R2);

  return std::string(Name);
}

void Driver::generatePrefixedToolNames(
    StringRef Tool, const ToolChain &TC,
    SmallVectorImpl<std::string> &Names) const {
  // FIXME: Needs a better variable than TargetTriple
  Names.emplace_back((TargetTriple + "-" + Tool).str());
  Names.emplace_back(Tool);
}

static bool ScanDirForExecutable(SmallString<128> &Dir, StringRef Name) {
  llvm::sys::path::append(Dir, Name);
  if (llvm::sys::fs::can_execute(Twine(Dir)))
    return true;
  llvm::sys::path::remove_filename(Dir);
  return false;
}

std::string Driver::GetProgramPath(StringRef Name, const ToolChain &TC) const {
  SmallVector<std::string, 2> TargetSpecificExecutables;
  generatePrefixedToolNames(Name, TC, TargetSpecificExecutables);

  // Respect a limited subset of the '-Bprefix' functionality in GCC by
  // attempting to use this prefix when looking for program paths.
  for (const auto &PrefixDir : PrefixDirs) {
    if (llvm::sys::fs::is_directory(PrefixDir)) {
      SmallString<128> P(PrefixDir);
      if (ScanDirForExecutable(P, Name))
        return std::string(P);
    } else {
      SmallString<128> P((PrefixDir + Name).str());
      if (llvm::sys::fs::can_execute(Twine(P)))
        return std::string(P);
    }
  }

  const ToolChain::path_list &List = TC.getProgramPaths();
  for (const auto &TargetSpecificExecutable : TargetSpecificExecutables) {
    // For each possible name of the tool look for it in
    // program paths first, then the path.
    // Higher priority names will be first, meaning that
    // a higher priority name in the path will be found
    // instead of a lower priority name in the program path.
    // E.g. <triple>-gcc on the path will be found instead
    // of gcc in the program path
    for (const auto &Path : List) {
      SmallString<128> P(Path);
      if (ScanDirForExecutable(P, TargetSpecificExecutable))
        return std::string(P);
    }

    // Fall back to the path
    if (llvm::ErrorOr<std::string> P =
            llvm::sys::findProgramByName(TargetSpecificExecutable))
      return *P;
  }

  return std::string(Name);
}

std::string Driver::GetStdModuleManifestPath(const Compilation &C,
                                             const ToolChain &TC) const {
  std::string error = "<NOT PRESENT>";

  switch (TC.GetCXXStdlibType(C.getArgs())) {
  case ToolChain::CST_Libcxx: {
    auto evaluate = [&](const char *library) -> std::optional<std::string> {
      std::string lib = GetFilePath(library, TC);

      // Note when there are multiple flavours of libc++ the module json needs
      // to look at the command-line arguments for the proper json. These
      // flavours do not exist at the moment, but there are plans to provide a
      // variant that is built with sanitizer instrumentation enabled.

      // For example
      //  StringRef modules = [&] {
      //    const SanitizerArgs &Sanitize = TC.getSanitizerArgs(C.getArgs());
      //    if (Sanitize.needsAsanRt())
      //      return "libc++.modules-asan.json";
      //    return "libc++.modules.json";
      //  }();

      SmallString<128> path(lib.begin(), lib.end());
      llvm::sys::path::remove_filename(path);
      llvm::sys::path::append(path, "libc++.modules.json");
      if (TC.getVFS().exists(path))
        return static_cast<std::string>(path);

      return {};
    };

    if (std::optional<std::string> result = evaluate("libc++.so"); result)
      return *result;

    return evaluate("libc++.a").value_or(error);
  }

  case ToolChain::CST_Libstdcxx:
    // libstdc++ does not provide Standard library modules yet.
    return error;
  }

  return error;
}

std::string Driver::GetTemporaryPath(StringRef Prefix, StringRef Suffix) const {
  SmallString<128> Path;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(Prefix, Suffix, Path);
  if (EC) {
    Diag(clang::diag::err_unable_to_make_temp) << EC.message();
    return "";
  }

  return std::string(Path);
}

std::string Driver::GetTemporaryDirectory(StringRef Prefix) const {
  SmallString<128> Path;
  std::error_code EC = llvm::sys::fs::createUniqueDirectory(Prefix, Path);
  if (EC) {
    Diag(clang::diag::err_unable_to_make_temp) << EC.message();
    return "";
  }

  return std::string(Path);
}

std::string Driver::GetClPchPath(Compilation &C, StringRef BaseName) const {
  SmallString<128> Output;
  if (Arg *FpArg = C.getArgs().getLastArg(options::OPT__SLASH_Fp)) {
    // FIXME: If anybody needs it, implement this obscure rule:
    // "If you specify a directory without a file name, the default file name
    // is VCx0.pch., where x is the major version of Visual C++ in use."
    Output = FpArg->getValue();

    // "If you do not specify an extension as part of the path name, an
    // extension of .pch is assumed. "
    if (!llvm::sys::path::has_extension(Output))
      Output += ".pch";
  } else {
    if (Arg *YcArg = C.getArgs().getLastArg(options::OPT__SLASH_Yc))
      Output = YcArg->getValue();
    if (Output.empty())
      Output = BaseName;
    llvm::sys::path::replace_extension(Output, ".pch");
  }
  return std::string(Output);
}

const ToolChain &Driver::getToolChain(const ArgList &Args,
                                      const llvm::Triple &Target) const {

  auto &TC = ToolChains[Target.str()];
  if (!TC) {
    switch (Target.getOS()) {
    case llvm::Triple::AIX:
      TC = std::make_unique<toolchains::AIX>(*this, Target, Args);
      break;
    case llvm::Triple::Haiku:
      TC = std::make_unique<toolchains::Haiku>(*this, Target, Args);
      break;
    case llvm::Triple::Darwin:
    case llvm::Triple::MacOSX:
    case llvm::Triple::IOS:
    case llvm::Triple::TvOS:
    case llvm::Triple::WatchOS:
    case llvm::Triple::XROS:
    case llvm::Triple::DriverKit:
      TC = std::make_unique<toolchains::DarwinClang>(*this, Target, Args);
      break;
    case llvm::Triple::DragonFly:
      TC = std::make_unique<toolchains::DragonFly>(*this, Target, Args);
      break;
    case llvm::Triple::OpenBSD:
      TC = std::make_unique<toolchains::OpenBSD>(*this, Target, Args);
      break;
    case llvm::Triple::NetBSD:
      TC = std::make_unique<toolchains::NetBSD>(*this, Target, Args);
      break;
    case llvm::Triple::FreeBSD:
      if (Target.isPPC())
        TC = std::make_unique<toolchains::PPCFreeBSDToolChain>(*this, Target,
                                                               Args);
      else
        TC = std::make_unique<toolchains::FreeBSD>(*this, Target, Args);
      break;
    case llvm::Triple::Linux:
    case llvm::Triple::ELFIAMCU:
      if (Target.getArch() == llvm::Triple::hexagon)
        TC = std::make_unique<toolchains::HexagonToolChain>(*this, Target,
                                                             Args);
      else if ((Target.getVendor() == llvm::Triple::MipsTechnologies) &&
               !Target.hasEnvironment())
        TC = std::make_unique<toolchains::MipsLLVMToolChain>(*this, Target,
                                                              Args);
      else if (Target.isPPC())
        TC = std::make_unique<toolchains::PPCLinuxToolChain>(*this, Target,
                                                              Args);
      else if (Target.getArch() == llvm::Triple::ve)
        TC = std::make_unique<toolchains::VEToolChain>(*this, Target, Args);
      else if (Target.isOHOSFamily())
        TC = std::make_unique<toolchains::OHOS>(*this, Target, Args);
      else
        TC = std::make_unique<toolchains::Linux>(*this, Target, Args);
      break;
    case llvm::Triple::NaCl:
      TC = std::make_unique<toolchains::NaClToolChain>(*this, Target, Args);
      break;
    case llvm::Triple::Fuchsia:
      TC = std::make_unique<toolchains::Fuchsia>(*this, Target, Args);
      break;
    case llvm::Triple::Solaris:
      TC = std::make_unique<toolchains::Solaris>(*this, Target, Args);
      break;
    case llvm::Triple::CUDA:
      TC = std::make_unique<toolchains::NVPTXToolChain>(*this, Target, Args);
      break;
    case llvm::Triple::AMDHSA:
      TC = std::make_unique<toolchains::ROCMToolChain>(*this, Target, Args);
      break;
    case llvm::Triple::AMDPAL:
    case llvm::Triple::Mesa3D:
      TC = std::make_unique<toolchains::AMDGPUToolChain>(*this, Target, Args);
      break;
    case llvm::Triple::Win32:
      switch (Target.getEnvironment()) {
      default:
        if (Target.isOSBinFormatELF())
          TC = std::make_unique<toolchains::Generic_ELF>(*this, Target, Args);
        else if (Target.isOSBinFormatMachO())
          TC = std::make_unique<toolchains::MachO>(*this, Target, Args);
        else
          TC = std::make_unique<toolchains::Generic_GCC>(*this, Target, Args);
        break;
      case llvm::Triple::GNU:
        TC = std::make_unique<toolchains::MinGW>(*this, Target, Args);
        break;
      case llvm::Triple::Itanium:
        TC = std::make_unique<toolchains::CrossWindowsToolChain>(*this, Target,
                                                                  Args);
        break;
      case llvm::Triple::MSVC:
      case llvm::Triple::UnknownEnvironment:
        if (Args.getLastArgValue(options::OPT_fuse_ld_EQ)
                .starts_with_insensitive("bfd"))
          TC = std::make_unique<toolchains::CrossWindowsToolChain>(
              *this, Target, Args);
        else
          TC =
              std::make_unique<toolchains::MSVCToolChain>(*this, Target, Args);
        break;
      }
      break;
    case llvm::Triple::PS4:
      TC = std::make_unique<toolchains::PS4CPU>(*this, Target, Args);
      break;
    case llvm::Triple::PS5:
      TC = std::make_unique<toolchains::PS5CPU>(*this, Target, Args);
      break;
    case llvm::Triple::Hurd:
      TC = std::make_unique<toolchains::Hurd>(*this, Target, Args);
      break;
    case llvm::Triple::LiteOS:
      TC = std::make_unique<toolchains::OHOS>(*this, Target, Args);
      break;
    case llvm::Triple::ZOS:
      TC = std::make_unique<toolchains::ZOS>(*this, Target, Args);
      break;
    case llvm::Triple::ShaderModel:
      TC = std::make_unique<toolchains::HLSLToolChain>(*this, Target, Args);
      break;
    default:
      // Of these targets, Hexagon is the only one that might have
      // an OS of Linux, in which case it got handled above already.
      switch (Target.getArch()) {
      case llvm::Triple::tce:
        TC = std::make_unique<toolchains::TCEToolChain>(*this, Target, Args);
        break;
      case llvm::Triple::tcele:
        TC = std::make_unique<toolchains::TCELEToolChain>(*this, Target, Args);
        break;
      case llvm::Triple::hexagon:
        TC = std::make_unique<toolchains::HexagonToolChain>(*this, Target,
                                                             Args);
        break;
      case llvm::Triple::lanai:
        TC = std::make_unique<toolchains::LanaiToolChain>(*this, Target, Args);
        break;
      case llvm::Triple::xcore:
        TC = std::make_unique<toolchains::XCoreToolChain>(*this, Target, Args);
        break;
      case llvm::Triple::wasm32:
      case llvm::Triple::wasm64:
        TC = std::make_unique<toolchains::WebAssembly>(*this, Target, Args);
        break;
      case llvm::Triple::avr:
        TC = std::make_unique<toolchains::AVRToolChain>(*this, Target, Args);
        break;
      case llvm::Triple::msp430:
        TC =
            std::make_unique<toolchains::MSP430ToolChain>(*this, Target, Args);
        break;
      case llvm::Triple::riscv32:
      case llvm::Triple::riscv64:
        if (toolchains::RISCVToolChain::hasGCCToolchain(*this, Args))
          TC =
              std::make_unique<toolchains::RISCVToolChain>(*this, Target, Args);
        else
          TC = std::make_unique<toolchains::BareMetal>(*this, Target, Args);
        break;
      case llvm::Triple::ve:
        TC = std::make_unique<toolchains::VEToolChain>(*this, Target, Args);
        break;
      case llvm::Triple::spirv32:
      case llvm::Triple::spirv64:
        TC = std::make_unique<toolchains::SPIRVToolChain>(*this, Target, Args);
        break;
      case llvm::Triple::csky:
        TC = std::make_unique<toolchains::CSKYToolChain>(*this, Target, Args);
        break;
      default:
        if (toolchains::BareMetal::handlesTarget(Target))
          TC = std::make_unique<toolchains::BareMetal>(*this, Target, Args);
        else if (Target.isOSBinFormatELF())
          TC = std::make_unique<toolchains::Generic_ELF>(*this, Target, Args);
        else if (Target.isOSBinFormatMachO())
          TC = std::make_unique<toolchains::MachO>(*this, Target, Args);
        else
          TC = std::make_unique<toolchains::Generic_GCC>(*this, Target, Args);
      }
    }
  }

  return *TC;
}

const ToolChain &Driver::getOffloadingDeviceToolChain(
    const ArgList &Args, const llvm::Triple &Target, const ToolChain &HostTC,
    const Action::OffloadKind &TargetDeviceOffloadKind) const {
  // Use device / host triples as the key into the ToolChains map because the
  // device ToolChain we create depends on both.
  auto &TC = ToolChains[Target.str() + "/" + HostTC.getTriple().str()];
  if (!TC) {
    // Categorized by offload kind > arch rather than OS > arch like
    // the normal getToolChain call, as it seems a reasonable way to categorize
    // things.
    switch (TargetDeviceOffloadKind) {
    case Action::OFK_HIP: {
      if (((Target.getArch() == llvm::Triple::amdgcn ||
            Target.getArch() == llvm::Triple::spirv64) &&
           Target.getVendor() == llvm::Triple::AMD &&
           Target.getOS() == llvm::Triple::AMDHSA) ||
          !Args.hasArgNoClaim(options::OPT_offload_EQ))
        TC = std::make_unique<toolchains::HIPAMDToolChain>(*this, Target,
                                                           HostTC, Args);
      else if (Target.getArch() == llvm::Triple::spirv64 &&
               Target.getVendor() == llvm::Triple::UnknownVendor &&
               Target.getOS() == llvm::Triple::UnknownOS)
        TC = std::make_unique<toolchains::HIPSPVToolChain>(*this, Target,
                                                           HostTC, Args);
      break;
    }
    default:
      break;
    }
  }

  return *TC;
}

bool Driver::ShouldUseClangCompiler(const JobAction &JA) const {
  // Say "no" if there is not exactly one input of a type clang understands.
  if (JA.size() != 1 ||
      !types::isAcceptedByClang((*JA.input_begin())->getType()))
    return false;

  // And say "no" if this is not a kind of action clang understands.
  if (!isa<PreprocessJobAction>(JA) && !isa<PrecompileJobAction>(JA) &&
      !isa<CompileJobAction>(JA) && !isa<BackendJobAction>(JA) &&
      !isa<ExtractAPIJobAction>(JA))
    return false;

  return true;
}

bool Driver::ShouldUseFlangCompiler(const JobAction &JA) const {
  // Say "no" if there is not exactly one input of a type flang understands.
  if (JA.size() != 1 ||
      !types::isAcceptedByFlang((*JA.input_begin())->getType()))
    return false;

  // And say "no" if this is not a kind of action flang understands.
  if (!isa<PreprocessJobAction>(JA) && !isa<CompileJobAction>(JA) &&
      !isa<BackendJobAction>(JA))
    return false;

  return true;
}

bool Driver::ShouldEmitStaticLibrary(const ArgList &Args) const {
  // Only emit static library if the flag is set explicitly.
  if (Args.hasArg(options::OPT_emit_static_lib))
    return true;
  return false;
}

/// GetReleaseVersion - Parse (([0-9]+)(.([0-9]+)(.([0-9]+)?))?)? and return the
/// grouped values as integers. Numbers which are not provided are set to 0.
///
/// \return True if the entire string was parsed (9.2), or all groups were
/// parsed (10.3.5extrastuff).
bool Driver::GetReleaseVersion(StringRef Str, unsigned &Major, unsigned &Minor,
                               unsigned &Micro, bool &HadExtra) {
  HadExtra = false;

  Major = Minor = Micro = 0;
  if (Str.empty())
    return false;

  if (Str.consumeInteger(10, Major))
    return false;
  if (Str.empty())
    return true;
  if (!Str.consume_front("."))
    return false;

  if (Str.consumeInteger(10, Minor))
    return false;
  if (Str.empty())
    return true;
  if (!Str.consume_front("."))
    return false;

  if (Str.consumeInteger(10, Micro))
    return false;
  if (!Str.empty())
    HadExtra = true;
  return true;
}

/// Parse digits from a string \p Str and fulfill \p Digits with
/// the parsed numbers. This method assumes that the max number of
/// digits to look for is equal to Digits.size().
///
/// \return True if the entire string was parsed and there are
/// no extra characters remaining at the end.
bool Driver::GetReleaseVersion(StringRef Str,
                               MutableArrayRef<unsigned> Digits) {
  if (Str.empty())
    return false;

  unsigned CurDigit = 0;
  while (CurDigit < Digits.size()) {
    unsigned Digit;
    if (Str.consumeInteger(10, Digit))
      return false;
    Digits[CurDigit] = Digit;
    if (Str.empty())
      return true;
    if (!Str.consume_front("."))
      return false;
    CurDigit++;
  }

  // More digits than requested, bail out...
  return false;
}

llvm::opt::Visibility
Driver::getOptionVisibilityMask(bool UseDriverMode) const {
  if (!UseDriverMode)
    return llvm::opt::Visibility(options::ClangOption);
  if (IsCLMode())
    return llvm::opt::Visibility(options::CLOption);
  if (IsDXCMode())
    return llvm::opt::Visibility(options::DXCOption);
  if (IsFlangMode())  {
    return llvm::opt::Visibility(options::FlangOption);
  }
  return llvm::opt::Visibility(options::ClangOption);
}

const char *Driver::getExecutableForDriverMode(DriverMode Mode) {
  switch (Mode) {
  case GCCMode:
    return "clang";
  case GXXMode:
    return "clang++";
  case CPPMode:
    return "clang-cpp";
  case CLMode:
    return "clang-cl";
  case FlangMode:
    return "flang";
  case DXCMode:
    return "clang-dxc";
  }

  llvm_unreachable("Unhandled Mode");
}

bool clang::driver::isOptimizationLevelFast(const ArgList &Args) {
  return Args.hasFlag(options::OPT_Ofast, options::OPT_O_Group, false);
}

bool clang::driver::willEmitRemarks(const ArgList &Args) {
  // -fsave-optimization-record enables it.
  if (Args.hasFlag(options::OPT_fsave_optimization_record,
                   options::OPT_fno_save_optimization_record, false))
    return true;

  // -fsave-optimization-record=<format> enables it as well.
  if (Args.hasFlag(options::OPT_fsave_optimization_record_EQ,
                   options::OPT_fno_save_optimization_record, false))
    return true;

  // -foptimization-record-file alone enables it too.
  if (Args.hasFlag(options::OPT_foptimization_record_file_EQ,
                   options::OPT_fno_save_optimization_record, false))
    return true;

  // -foptimization-record-passes alone enables it too.
  if (Args.hasFlag(options::OPT_foptimization_record_passes_EQ,
                   options::OPT_fno_save_optimization_record, false))
    return true;
  return false;
}

llvm::StringRef clang::driver::getDriverMode(StringRef ProgName,
                                             ArrayRef<const char *> Args) {
  static StringRef OptName =
      getDriverOptTable().getOption(options::OPT_driver_mode).getPrefixedName();
  llvm::StringRef Opt;
  for (StringRef Arg : Args) {
    if (!Arg.starts_with(OptName))
      continue;
    Opt = Arg;
  }
  if (Opt.empty())
    Opt = ToolChain::getTargetAndModeFromProgramName(ProgName).DriverMode;
  return Opt.consume_front(OptName) ? Opt : "";
}

bool driver::IsClangCL(StringRef DriverMode) { return DriverMode == "cl"; }

llvm::Error driver::expandResponseFiles(SmallVectorImpl<const char *> &Args,
                                        bool ClangCLMode,
                                        llvm::BumpPtrAllocator &Alloc,
                                        llvm::vfs::FileSystem *FS) {
  // Parse response files using the GNU syntax, unless we're in CL mode. There
  // are two ways to put clang in CL compatibility mode: ProgName is either
  // clang-cl or cl, or --driver-mode=cl is on the command line. The normal
  // command line parsing can't happen until after response file parsing, so we
  // have to manually search for a --driver-mode=cl argument the hard way.
  // Finally, our -cc1 tools don't care which tokenization mode we use because
  // response files written by clang will tokenize the same way in either mode.
  enum { Default, POSIX, Windows } RSPQuoting = Default;
  for (const char *F : Args) {
    if (strcmp(F, "--rsp-quoting=posix") == 0)
      RSPQuoting = POSIX;
    else if (strcmp(F, "--rsp-quoting=windows") == 0)
      RSPQuoting = Windows;
  }

  // Determines whether we want nullptr markers in Args to indicate response
  // files end-of-lines. We only use this for the /LINK driver argument with
  // clang-cl.exe on Windows.
  bool MarkEOLs = ClangCLMode;

  llvm::cl::TokenizerCallback Tokenizer;
  if (RSPQuoting == Windows || (RSPQuoting == Default && ClangCLMode))
    Tokenizer = &llvm::cl::TokenizeWindowsCommandLine;
  else
    Tokenizer = &llvm::cl::TokenizeGNUCommandLine;

  if (MarkEOLs && Args.size() > 1 && StringRef(Args[1]).starts_with("-cc1"))
    MarkEOLs = false;

  llvm::cl::ExpansionContext ECtx(Alloc, Tokenizer);
  ECtx.setMarkEOLs(MarkEOLs);
  if (FS)
    ECtx.setVFS(FS);

  if (llvm::Error Err = ECtx.expandResponseFiles(Args))
    return Err;

  // If -cc1 came from a response file, remove the EOL sentinels.
  auto FirstArg = llvm::find_if(llvm::drop_begin(Args),
                                [](const char *A) { return A != nullptr; });
  if (FirstArg != Args.end() && StringRef(*FirstArg).starts_with("-cc1")) {
    // If -cc1 came from a response file, remove the EOL sentinels.
    if (MarkEOLs) {
      auto newEnd = std::remove(Args.begin(), Args.end(), nullptr);
      Args.resize(newEnd - Args.begin());
    }
  }

  return llvm::Error::success();
}

static const char *GetStableCStr(llvm::StringSet<> &SavedStrings, StringRef S) {
  return SavedStrings.insert(S).first->getKeyData();
}

/// Apply a list of edits to the input argument lists.
///
/// The input string is a space separated list of edits to perform,
/// they are applied in order to the input argument lists. Edits
/// should be one of the following forms:
///
///  '#': Silence information about the changes to the command line arguments.
///
///  '^': Add FOO as a new argument at the beginning of the command line.
///
///  '+': Add FOO as a new argument at the end of the command line.
///
///  's/XXX/YYY/': Substitute the regular expression XXX with YYY in the command
///  line.
///
///  'xOPTION': Removes all instances of the literal argument OPTION.
///
///  'XOPTION': Removes all instances of the literal argument OPTION,
///  and the following argument.
///
///  'Ox': Removes all flags matching 'O' or 'O[sz0-9]' and adds 'Ox'
///  at the end of the command line.
///
/// \param OS - The stream to write edit information to.
/// \param Args - The vector of command line arguments.
/// \param Edit - The override command to perform.
/// \param SavedStrings - Set to use for storing string representations.
static void applyOneOverrideOption(raw_ostream &OS,
                                   SmallVectorImpl<const char *> &Args,
                                   StringRef Edit,
                                   llvm::StringSet<> &SavedStrings) {
  // This does not need to be efficient.

  if (Edit[0] == '^') {
    const char *Str = GetStableCStr(SavedStrings, Edit.substr(1));
    OS << "### Adding argument " << Str << " at beginning\n";
    Args.insert(Args.begin() + 1, Str);
  } else if (Edit[0] == '+') {
    const char *Str = GetStableCStr(SavedStrings, Edit.substr(1));
    OS << "### Adding argument " << Str << " at end\n";
    Args.push_back(Str);
  } else if (Edit[0] == 's' && Edit[1] == '/' && Edit.ends_with("/") &&
             Edit.slice(2, Edit.size() - 1).contains('/')) {
    StringRef MatchPattern = Edit.substr(2).split('/').first;
    StringRef ReplPattern = Edit.substr(2).split('/').second;
    ReplPattern = ReplPattern.slice(0, ReplPattern.size() - 1);

    for (unsigned i = 1, e = Args.size(); i != e; ++i) {
      // Ignore end-of-line response file markers
      if (Args[i] == nullptr)
        continue;
      std::string Repl = llvm::Regex(MatchPattern).sub(ReplPattern, Args[i]);

      if (Repl != Args[i]) {
        OS << "### Replacing '" << Args[i] << "' with '" << Repl << "'\n";
        Args[i] = GetStableCStr(SavedStrings, Repl);
      }
    }
  } else if (Edit[0] == 'x' || Edit[0] == 'X') {
    auto Option = Edit.substr(1);
    for (unsigned i = 1; i < Args.size();) {
      if (Option == Args[i]) {
        OS << "### Deleting argument " << Args[i] << '\n';
        Args.erase(Args.begin() + i);
        if (Edit[0] == 'X') {
          if (i < Args.size()) {
            OS << "### Deleting argument " << Args[i] << '\n';
            Args.erase(Args.begin() + i);
          } else
            OS << "### Invalid X edit, end of command line!\n";
        }
      } else
        ++i;
    }
  } else if (Edit[0] == 'O') {
    for (unsigned i = 1; i < Args.size();) {
      const char *A = Args[i];
      // Ignore end-of-line response file markers
      if (A == nullptr)
        continue;
      if (A[0] == '-' && A[1] == 'O' &&
          (A[2] == '\0' || (A[3] == '\0' && (A[2] == 's' || A[2] == 'z' ||
                                             ('0' <= A[2] && A[2] <= '9'))))) {
        OS << "### Deleting argument " << Args[i] << '\n';
        Args.erase(Args.begin() + i);
      } else
        ++i;
    }
    OS << "### Adding argument " << Edit << " at end\n";
    Args.push_back(GetStableCStr(SavedStrings, '-' + Edit.str()));
  } else {
    OS << "### Unrecognized edit: " << Edit << "\n";
  }
}

void driver::applyOverrideOptions(SmallVectorImpl<const char *> &Args,
                                  const char *OverrideStr,
                                  llvm::StringSet<> &SavedStrings,
                                  raw_ostream *OS) {
  if (!OS)
    OS = &llvm::nulls();

  if (OverrideStr[0] == '#') {
    ++OverrideStr;
    OS = &llvm::nulls();
  }

  *OS << "### CCC_OVERRIDE_OPTIONS: " << OverrideStr << "\n";

  // This does not need to be efficient.

  const char *S = OverrideStr;
  while (*S) {
    const char *End = ::strchr(S, ' ');
    if (!End)
      End = S + strlen(S);
    if (End != S)
      applyOneOverrideOption(*OS, Args, std::string(S, End), SavedStrings);
    S = End;
    if (*S != '\0')
      ++S;
  }
}
