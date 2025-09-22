//===-- Clang.cpp - Clang+LLVM ToolChain Implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Clang.h"
#include "AMDGPU.h"
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
#include "CommonArgs.h"
#include "Hexagon.h"
#include "MSP430.h"
#include "PS4CPU.h"
#include "clang/Basic/CLWarnings.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/HeaderInclude.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/MakeSupport.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Distro.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "clang/Driver/Types.h"
#include "clang/Driver/XRayArgs.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/ARMTargetParserCommon.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/LoongArchTargetParser.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include "llvm/TargetParser/RISCVTargetParser.h"
#include <cctype>

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

static void CheckPreprocessingOptions(const Driver &D, const ArgList &Args) {
  if (Arg *A = Args.getLastArg(clang::driver::options::OPT_C, options::OPT_CC,
                               options::OPT_fminimize_whitespace,
                               options::OPT_fno_minimize_whitespace,
                               options::OPT_fkeep_system_includes,
                               options::OPT_fno_keep_system_includes)) {
    if (!Args.hasArg(options::OPT_E) && !Args.hasArg(options::OPT__SLASH_P) &&
        !Args.hasArg(options::OPT__SLASH_EP) && !D.CCCIsCPP()) {
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << A->getBaseArg().getAsString(Args)
          << (D.IsCLMode() ? "/E, /P or /EP" : "-E");
    }
  }
}

static void CheckCodeGenerationOptions(const Driver &D, const ArgList &Args) {
  // In gcc, only ARM checks this, but it seems reasonable to check universally.
  if (Args.hasArg(options::OPT_static))
    if (const Arg *A =
            Args.getLastArg(options::OPT_dynamic, options::OPT_mdynamic_no_pic))
      D.Diag(diag::err_drv_argument_not_allowed_with) << A->getAsString(Args)
                                                      << "-static";
}

// Add backslashes to escape spaces and other backslashes.
// This is used for the space-separated argument list specified with
// the -dwarf-debug-flags option.
static void EscapeSpacesAndBackslashes(const char *Arg,
                                       SmallVectorImpl<char> &Res) {
  for (; *Arg; ++Arg) {
    switch (*Arg) {
    default:
      break;
    case ' ':
    case '\\':
      Res.push_back('\\');
      break;
    }
    Res.push_back(*Arg);
  }
}

/// Apply \a Work on the current tool chain \a RegularToolChain and any other
/// offloading tool chain that is associated with the current action \a JA.
static void
forAllAssociatedToolChains(Compilation &C, const JobAction &JA,
                           const ToolChain &RegularToolChain,
                           llvm::function_ref<void(const ToolChain &)> Work) {
  // Apply Work on the current/regular tool chain.
  Work(RegularToolChain);

  // Apply Work on all the offloading tool chains associated with the current
  // action.
  if (JA.isHostOffloading(Action::OFK_Cuda))
    Work(*C.getSingleOffloadToolChain<Action::OFK_Cuda>());
  else if (JA.isDeviceOffloading(Action::OFK_Cuda))
    Work(*C.getSingleOffloadToolChain<Action::OFK_Host>());
  else if (JA.isHostOffloading(Action::OFK_HIP))
    Work(*C.getSingleOffloadToolChain<Action::OFK_HIP>());
  else if (JA.isDeviceOffloading(Action::OFK_HIP))
    Work(*C.getSingleOffloadToolChain<Action::OFK_Host>());

  if (JA.isHostOffloading(Action::OFK_OpenMP)) {
    auto TCs = C.getOffloadToolChains<Action::OFK_OpenMP>();
    for (auto II = TCs.first, IE = TCs.second; II != IE; ++II)
      Work(*II->second);
  } else if (JA.isDeviceOffloading(Action::OFK_OpenMP))
    Work(*C.getSingleOffloadToolChain<Action::OFK_Host>());

  //
  // TODO: Add support for other offloading programming models here.
  //
}

/// This is a helper function for validating the optional refinement step
/// parameter in reciprocal argument strings. Return false if there is an error
/// parsing the refinement step. Otherwise, return true and set the Position
/// of the refinement step in the input string.
static bool getRefinementStep(StringRef In, const Driver &D,
                              const Arg &A, size_t &Position) {
  const char RefinementStepToken = ':';
  Position = In.find(RefinementStepToken);
  if (Position != StringRef::npos) {
    StringRef Option = A.getOption().getName();
    StringRef RefStep = In.substr(Position + 1);
    // Allow exactly one numeric character for the additional refinement
    // step parameter. This is reasonable for all currently-supported
    // operations and architectures because we would expect that a larger value
    // of refinement steps would cause the estimate "optimization" to
    // under-perform the native operation. Also, if the estimate does not
    // converge quickly, it probably will not ever converge, so further
    // refinement steps will not produce a better answer.
    if (RefStep.size() != 1) {
      D.Diag(diag::err_drv_invalid_value) << Option << RefStep;
      return false;
    }
    char RefStepChar = RefStep[0];
    if (RefStepChar < '0' || RefStepChar > '9') {
      D.Diag(diag::err_drv_invalid_value) << Option << RefStep;
      return false;
    }
  }
  return true;
}

/// The -mrecip flag requires processing of many optional parameters.
static void ParseMRecip(const Driver &D, const ArgList &Args,
                        ArgStringList &OutStrings) {
  StringRef DisabledPrefixIn = "!";
  StringRef DisabledPrefixOut = "!";
  StringRef EnabledPrefixOut = "";
  StringRef Out = "-mrecip=";

  Arg *A = Args.getLastArg(options::OPT_mrecip, options::OPT_mrecip_EQ);
  if (!A)
    return;

  unsigned NumOptions = A->getNumValues();
  if (NumOptions == 0) {
    // No option is the same as "all".
    OutStrings.push_back(Args.MakeArgString(Out + "all"));
    return;
  }

  // Pass through "all", "none", or "default" with an optional refinement step.
  if (NumOptions == 1) {
    StringRef Val = A->getValue(0);
    size_t RefStepLoc;
    if (!getRefinementStep(Val, D, *A, RefStepLoc))
      return;
    StringRef ValBase = Val.slice(0, RefStepLoc);
    if (ValBase == "all" || ValBase == "none" || ValBase == "default") {
      OutStrings.push_back(Args.MakeArgString(Out + Val));
      return;
    }
  }

  // Each reciprocal type may be enabled or disabled individually.
  // Check each input value for validity, concatenate them all back together,
  // and pass through.

  llvm::StringMap<bool> OptionStrings;
  OptionStrings.insert(std::make_pair("divd", false));
  OptionStrings.insert(std::make_pair("divf", false));
  OptionStrings.insert(std::make_pair("divh", false));
  OptionStrings.insert(std::make_pair("vec-divd", false));
  OptionStrings.insert(std::make_pair("vec-divf", false));
  OptionStrings.insert(std::make_pair("vec-divh", false));
  OptionStrings.insert(std::make_pair("sqrtd", false));
  OptionStrings.insert(std::make_pair("sqrtf", false));
  OptionStrings.insert(std::make_pair("sqrth", false));
  OptionStrings.insert(std::make_pair("vec-sqrtd", false));
  OptionStrings.insert(std::make_pair("vec-sqrtf", false));
  OptionStrings.insert(std::make_pair("vec-sqrth", false));

  for (unsigned i = 0; i != NumOptions; ++i) {
    StringRef Val = A->getValue(i);

    bool IsDisabled = Val.starts_with(DisabledPrefixIn);
    // Ignore the disablement token for string matching.
    if (IsDisabled)
      Val = Val.substr(1);

    size_t RefStep;
    if (!getRefinementStep(Val, D, *A, RefStep))
      return;

    StringRef ValBase = Val.slice(0, RefStep);
    llvm::StringMap<bool>::iterator OptionIter = OptionStrings.find(ValBase);
    if (OptionIter == OptionStrings.end()) {
      // Try again specifying float suffix.
      OptionIter = OptionStrings.find(ValBase.str() + 'f');
      if (OptionIter == OptionStrings.end()) {
        // The input name did not match any known option string.
        D.Diag(diag::err_drv_unknown_argument) << Val;
        return;
      }
      // The option was specified without a half or float or double suffix.
      // Make sure that the double or half entry was not already specified.
      // The float entry will be checked below.
      if (OptionStrings[ValBase.str() + 'd'] ||
          OptionStrings[ValBase.str() + 'h']) {
        D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Val;
        return;
      }
    }

    if (OptionIter->second == true) {
      // Duplicate option specified.
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Val;
      return;
    }

    // Mark the matched option as found. Do not allow duplicate specifiers.
    OptionIter->second = true;

    // If the precision was not specified, also mark the double and half entry
    // as found.
    if (ValBase.back() != 'f' && ValBase.back() != 'd' && ValBase.back() != 'h') {
      OptionStrings[ValBase.str() + 'd'] = true;
      OptionStrings[ValBase.str() + 'h'] = true;
    }

    // Build the output string.
    StringRef Prefix = IsDisabled ? DisabledPrefixOut : EnabledPrefixOut;
    Out = Args.MakeArgString(Out + Prefix + Val);
    if (i != NumOptions - 1)
      Out = Args.MakeArgString(Out + ",");
  }

  OutStrings.push_back(Args.MakeArgString(Out));
}

/// The -mprefer-vector-width option accepts either a positive integer
/// or the string "none".
static void ParseMPreferVectorWidth(const Driver &D, const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  Arg *A = Args.getLastArg(options::OPT_mprefer_vector_width_EQ);
  if (!A)
    return;

  StringRef Value = A->getValue();
  if (Value == "none") {
    CmdArgs.push_back("-mprefer-vector-width=none");
  } else {
    unsigned Width;
    if (Value.getAsInteger(10, Width)) {
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Value;
      return;
    }
    CmdArgs.push_back(Args.MakeArgString("-mprefer-vector-width=" + Value));
  }
}

static bool
shouldUseExceptionTablesForObjCExceptions(const ObjCRuntime &runtime,
                                          const llvm::Triple &Triple) {
  // We use the zero-cost exception tables for Objective-C if the non-fragile
  // ABI is enabled or when compiling for x86_64 and ARM on Snow Leopard and
  // later.
  if (runtime.isNonFragile())
    return true;

  if (!Triple.isMacOSX())
    return false;

  return (!Triple.isMacOSXVersionLT(10, 5) &&
          (Triple.getArch() == llvm::Triple::x86_64 ||
           Triple.getArch() == llvm::Triple::arm));
}

/// Adds exception related arguments to the driver command arguments. There's a
/// main flag, -fexceptions and also language specific flags to enable/disable
/// C++ and Objective-C exceptions. This makes it possible to for example
/// disable C++ exceptions but enable Objective-C exceptions.
static bool addExceptionArgs(const ArgList &Args, types::ID InputType,
                             const ToolChain &TC, bool KernelOrKext,
                             const ObjCRuntime &objcRuntime,
                             ArgStringList &CmdArgs) {
  const llvm::Triple &Triple = TC.getTriple();

  if (KernelOrKext) {
    // -mkernel and -fapple-kext imply no exceptions, so claim exception related
    // arguments now to avoid warnings about unused arguments.
    Args.ClaimAllArgs(options::OPT_fexceptions);
    Args.ClaimAllArgs(options::OPT_fno_exceptions);
    Args.ClaimAllArgs(options::OPT_fobjc_exceptions);
    Args.ClaimAllArgs(options::OPT_fno_objc_exceptions);
    Args.ClaimAllArgs(options::OPT_fcxx_exceptions);
    Args.ClaimAllArgs(options::OPT_fno_cxx_exceptions);
    Args.ClaimAllArgs(options::OPT_fasync_exceptions);
    Args.ClaimAllArgs(options::OPT_fno_async_exceptions);
    return false;
  }

  // See if the user explicitly enabled exceptions.
  bool EH = Args.hasFlag(options::OPT_fexceptions, options::OPT_fno_exceptions,
                         false);

  // Async exceptions are Windows MSVC only.
  if (Triple.isWindowsMSVCEnvironment()) {
    bool EHa = Args.hasFlag(options::OPT_fasync_exceptions,
                            options::OPT_fno_async_exceptions, false);
    if (EHa) {
      CmdArgs.push_back("-fasync-exceptions");
      EH = true;
    }
  }

  // Obj-C exceptions are enabled by default, regardless of -fexceptions. This
  // is not necessarily sensible, but follows GCC.
  if (types::isObjC(InputType) &&
      Args.hasFlag(options::OPT_fobjc_exceptions,
                   options::OPT_fno_objc_exceptions, true)) {
    CmdArgs.push_back("-fobjc-exceptions");

    EH |= shouldUseExceptionTablesForObjCExceptions(objcRuntime, Triple);
  }

  if (types::isCXX(InputType)) {
    // Disable C++ EH by default on XCore and PS4/PS5.
    bool CXXExceptionsEnabled = Triple.getArch() != llvm::Triple::xcore &&
                                !Triple.isPS() && !Triple.isDriverKit();
    Arg *ExceptionArg = Args.getLastArg(
        options::OPT_fcxx_exceptions, options::OPT_fno_cxx_exceptions,
        options::OPT_fexceptions, options::OPT_fno_exceptions);
    if (ExceptionArg)
      CXXExceptionsEnabled =
          ExceptionArg->getOption().matches(options::OPT_fcxx_exceptions) ||
          ExceptionArg->getOption().matches(options::OPT_fexceptions);

    if (CXXExceptionsEnabled) {
      CmdArgs.push_back("-fcxx-exceptions");

      EH = true;
    }
  }

  // OPT_fignore_exceptions means exception could still be thrown,
  // but no clean up or catch would happen in current module.
  // So we do not set EH to false.
  Args.AddLastArg(CmdArgs, options::OPT_fignore_exceptions);

  Args.addOptInFlag(CmdArgs, options::OPT_fassume_nothrow_exception_dtor,
                    options::OPT_fno_assume_nothrow_exception_dtor);

  if (EH)
    CmdArgs.push_back("-fexceptions");
  return EH;
}

static bool ShouldEnableAutolink(const ArgList &Args, const ToolChain &TC,
                                 const JobAction &JA) {
  bool Default = true;
  if (TC.getTriple().isOSDarwin()) {
    // The native darwin assembler doesn't support the linker_option directives,
    // so we disable them if we think the .s file will be passed to it.
    Default = TC.useIntegratedAs();
  }
  // The linker_option directives are intended for host compilation.
  if (JA.isDeviceOffloading(Action::OFK_Cuda) ||
      JA.isDeviceOffloading(Action::OFK_HIP))
    Default = false;
  return Args.hasFlag(options::OPT_fautolink, options::OPT_fno_autolink,
                      Default);
}

/// Add a CC1 option to specify the debug compilation directory.
static const char *addDebugCompDirArg(const ArgList &Args,
                                      ArgStringList &CmdArgs,
                                      const llvm::vfs::FileSystem &VFS) {
  if (Arg *A = Args.getLastArg(options::OPT_ffile_compilation_dir_EQ,
                               options::OPT_fdebug_compilation_dir_EQ)) {
    if (A->getOption().matches(options::OPT_ffile_compilation_dir_EQ))
      CmdArgs.push_back(Args.MakeArgString(Twine("-fdebug-compilation-dir=") +
                                           A->getValue()));
    else
      A->render(Args, CmdArgs);
  } else if (llvm::ErrorOr<std::string> CWD =
                 VFS.getCurrentWorkingDirectory()) {
    CmdArgs.push_back(Args.MakeArgString("-fdebug-compilation-dir=" + *CWD));
  }
  StringRef Path(CmdArgs.back());
  return Path.substr(Path.find('=') + 1).data();
}

static void addDebugObjectName(const ArgList &Args, ArgStringList &CmdArgs,
                               const char *DebugCompilationDir,
                               const char *OutputFileName) {
  // No need to generate a value for -object-file-name if it was provided.
  for (auto *Arg : Args.filtered(options::OPT_Xclang))
    if (StringRef(Arg->getValue()).starts_with("-object-file-name"))
      return;

  if (Args.hasArg(options::OPT_object_file_name_EQ))
    return;

  SmallString<128> ObjFileNameForDebug(OutputFileName);
  if (ObjFileNameForDebug != "-" &&
      !llvm::sys::path::is_absolute(ObjFileNameForDebug) &&
      (!DebugCompilationDir ||
       llvm::sys::path::is_absolute(DebugCompilationDir))) {
    // Make the path absolute in the debug infos like MSVC does.
    llvm::sys::fs::make_absolute(ObjFileNameForDebug);
  }
  // If the object file name is a relative path, then always use Windows
  // backslash style as -object-file-name is used for embedding object file path
  // in codeview and it can only be generated when targeting on Windows.
  // Otherwise, just use native absolute path.
  llvm::sys::path::Style Style =
      llvm::sys::path::is_absolute(ObjFileNameForDebug)
          ? llvm::sys::path::Style::native
          : llvm::sys::path::Style::windows_backslash;
  llvm::sys::path::remove_dots(ObjFileNameForDebug, /*remove_dot_dot=*/true,
                               Style);
  CmdArgs.push_back(
      Args.MakeArgString(Twine("-object-file-name=") + ObjFileNameForDebug));
}

/// Add a CC1 and CC1AS option to specify the debug file path prefix map.
static void addDebugPrefixMapArg(const Driver &D, const ToolChain &TC,
                                 const ArgList &Args, ArgStringList &CmdArgs) {
  auto AddOneArg = [&](StringRef Map, StringRef Name) {
    if (!Map.contains('='))
      D.Diag(diag::err_drv_invalid_argument_to_option) << Map << Name;
    else
      CmdArgs.push_back(Args.MakeArgString("-fdebug-prefix-map=" + Map));
  };

  for (const Arg *A : Args.filtered(options::OPT_ffile_prefix_map_EQ,
                                    options::OPT_fdebug_prefix_map_EQ)) {
    AddOneArg(A->getValue(), A->getOption().getName());
    A->claim();
  }
  std::string GlobalRemapEntry = TC.GetGlobalDebugPathRemapping();
  if (GlobalRemapEntry.empty())
    return;
  AddOneArg(GlobalRemapEntry, "environment");
}

/// Add a CC1 and CC1AS option to specify the macro file path prefix map.
static void addMacroPrefixMapArg(const Driver &D, const ArgList &Args,
                                 ArgStringList &CmdArgs) {
  for (const Arg *A : Args.filtered(options::OPT_ffile_prefix_map_EQ,
                                    options::OPT_fmacro_prefix_map_EQ)) {
    StringRef Map = A->getValue();
    if (!Map.contains('='))
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Map << A->getOption().getName();
    else
      CmdArgs.push_back(Args.MakeArgString("-fmacro-prefix-map=" + Map));
    A->claim();
  }
}

/// Add a CC1 and CC1AS option to specify the coverage file path prefix map.
static void addCoveragePrefixMapArg(const Driver &D, const ArgList &Args,
                                   ArgStringList &CmdArgs) {
  for (const Arg *A : Args.filtered(options::OPT_ffile_prefix_map_EQ,
                                    options::OPT_fcoverage_prefix_map_EQ)) {
    StringRef Map = A->getValue();
    if (!Map.contains('='))
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Map << A->getOption().getName();
    else
      CmdArgs.push_back(Args.MakeArgString("-fcoverage-prefix-map=" + Map));
    A->claim();
  }
}

/// Vectorize at all optimization levels greater than 1 except for -Oz.
/// For -Oz the loop vectorizer is disabled, while the slp vectorizer is
/// enabled.
static bool shouldEnableVectorizerAtOLevel(const ArgList &Args, bool isSlpVec) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O4) ||
        A->getOption().matches(options::OPT_Ofast))
      return true;

    if (A->getOption().matches(options::OPT_O0))
      return false;

    assert(A->getOption().matches(options::OPT_O) && "Must have a -O flag");

    // Vectorize -Os.
    StringRef S(A->getValue());
    if (S == "s")
      return true;

    // Don't vectorize -Oz, unless it's the slp vectorizer.
    if (S == "z")
      return isSlpVec;

    unsigned OptLevel = 0;
    if (S.getAsInteger(10, OptLevel))
      return false;

    return OptLevel > 1;
  }

  return false;
}

/// Add -x lang to \p CmdArgs for \p Input.
static void addDashXForInput(const ArgList &Args, const InputInfo &Input,
                             ArgStringList &CmdArgs) {
  // When using -verify-pch, we don't want to provide the type
  // 'precompiled-header' if it was inferred from the file extension
  if (Args.hasArg(options::OPT_verify_pch) && Input.getType() == types::TY_PCH)
    return;

  CmdArgs.push_back("-x");
  if (Args.hasArg(options::OPT_rewrite_objc))
    CmdArgs.push_back(types::getTypeName(types::TY_PP_ObjCXX));
  else {
    // Map the driver type to the frontend type. This is mostly an identity
    // mapping, except that the distinction between module interface units
    // and other source files does not exist at the frontend layer.
    const char *ClangType;
    switch (Input.getType()) {
    case types::TY_CXXModule:
      ClangType = "c++";
      break;
    case types::TY_PP_CXXModule:
      ClangType = "c++-cpp-output";
      break;
    default:
      ClangType = types::getTypeName(Input.getType());
      break;
    }
    CmdArgs.push_back(ClangType);
  }
}

static void addPGOAndCoverageFlags(const ToolChain &TC, Compilation &C,
                                   const JobAction &JA, const InputInfo &Output,
                                   const ArgList &Args, SanitizerArgs &SanArgs,
                                   ArgStringList &CmdArgs) {
  const Driver &D = TC.getDriver();
  auto *PGOGenerateArg = Args.getLastArg(options::OPT_fprofile_generate,
                                         options::OPT_fprofile_generate_EQ,
                                         options::OPT_fno_profile_generate);
  if (PGOGenerateArg &&
      PGOGenerateArg->getOption().matches(options::OPT_fno_profile_generate))
    PGOGenerateArg = nullptr;

  auto *CSPGOGenerateArg = getLastCSProfileGenerateArg(Args);

  auto *ProfileGenerateArg = Args.getLastArg(
      options::OPT_fprofile_instr_generate,
      options::OPT_fprofile_instr_generate_EQ,
      options::OPT_fno_profile_instr_generate);
  if (ProfileGenerateArg &&
      ProfileGenerateArg->getOption().matches(
          options::OPT_fno_profile_instr_generate))
    ProfileGenerateArg = nullptr;

  if (PGOGenerateArg && ProfileGenerateArg)
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << PGOGenerateArg->getSpelling() << ProfileGenerateArg->getSpelling();

  auto *ProfileUseArg = getLastProfileUseArg(Args);

  if (PGOGenerateArg && ProfileUseArg)
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << ProfileUseArg->getSpelling() << PGOGenerateArg->getSpelling();

  if (ProfileGenerateArg && ProfileUseArg)
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << ProfileGenerateArg->getSpelling() << ProfileUseArg->getSpelling();

  if (CSPGOGenerateArg && PGOGenerateArg) {
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << CSPGOGenerateArg->getSpelling() << PGOGenerateArg->getSpelling();
    PGOGenerateArg = nullptr;
  }

  if (TC.getTriple().isOSAIX()) {
    if (Arg *ProfileSampleUseArg = getLastProfileSampleUseArg(Args))
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << ProfileSampleUseArg->getSpelling() << TC.getTriple().str();
  }

  if (ProfileGenerateArg) {
    if (ProfileGenerateArg->getOption().matches(
            options::OPT_fprofile_instr_generate_EQ))
      CmdArgs.push_back(Args.MakeArgString(Twine("-fprofile-instrument-path=") +
                                           ProfileGenerateArg->getValue()));
    // The default is to use Clang Instrumentation.
    CmdArgs.push_back("-fprofile-instrument=clang");
    if (TC.getTriple().isWindowsMSVCEnvironment() &&
        Args.hasFlag(options::OPT_frtlib_defaultlib,
                     options::OPT_fno_rtlib_defaultlib, true)) {
      // Add dependent lib for clang_rt.profile
      CmdArgs.push_back(Args.MakeArgString(
          "--dependent-lib=" + TC.getCompilerRTBasename(Args, "profile")));
    }
  }

  Arg *PGOGenArg = nullptr;
  if (PGOGenerateArg) {
    assert(!CSPGOGenerateArg);
    PGOGenArg = PGOGenerateArg;
    CmdArgs.push_back("-fprofile-instrument=llvm");
  }
  if (CSPGOGenerateArg) {
    assert(!PGOGenerateArg);
    PGOGenArg = CSPGOGenerateArg;
    CmdArgs.push_back("-fprofile-instrument=csllvm");
  }
  if (PGOGenArg) {
    if (TC.getTriple().isWindowsMSVCEnvironment() &&
        Args.hasFlag(options::OPT_frtlib_defaultlib,
                     options::OPT_fno_rtlib_defaultlib, true)) {
      // Add dependent lib for clang_rt.profile
      CmdArgs.push_back(Args.MakeArgString(
          "--dependent-lib=" + TC.getCompilerRTBasename(Args, "profile")));
    }
    if (PGOGenArg->getOption().matches(
            PGOGenerateArg ? options::OPT_fprofile_generate_EQ
                           : options::OPT_fcs_profile_generate_EQ)) {
      SmallString<128> Path(PGOGenArg->getValue());
      llvm::sys::path::append(Path, "default_%m.profraw");
      CmdArgs.push_back(
          Args.MakeArgString(Twine("-fprofile-instrument-path=") + Path));
    }
  }

  if (ProfileUseArg) {
    if (ProfileUseArg->getOption().matches(options::OPT_fprofile_instr_use_EQ))
      CmdArgs.push_back(Args.MakeArgString(
          Twine("-fprofile-instrument-use-path=") + ProfileUseArg->getValue()));
    else if ((ProfileUseArg->getOption().matches(
                  options::OPT_fprofile_use_EQ) ||
              ProfileUseArg->getOption().matches(
                  options::OPT_fprofile_instr_use))) {
      SmallString<128> Path(
          ProfileUseArg->getNumValues() == 0 ? "" : ProfileUseArg->getValue());
      if (Path.empty() || llvm::sys::fs::is_directory(Path))
        llvm::sys::path::append(Path, "default.profdata");
      CmdArgs.push_back(
          Args.MakeArgString(Twine("-fprofile-instrument-use-path=") + Path));
    }
  }

  bool EmitCovNotes = Args.hasFlag(options::OPT_ftest_coverage,
                                   options::OPT_fno_test_coverage, false) ||
                      Args.hasArg(options::OPT_coverage);
  bool EmitCovData = TC.needsGCovInstrumentation(Args);

  if (Args.hasFlag(options::OPT_fcoverage_mapping,
                   options::OPT_fno_coverage_mapping, false)) {
    if (!ProfileGenerateArg)
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "-fcoverage-mapping"
          << "-fprofile-instr-generate";

    CmdArgs.push_back("-fcoverage-mapping");
  }

  if (Args.hasFlag(options::OPT_fmcdc_coverage, options::OPT_fno_mcdc_coverage,
                   false)) {
    if (!Args.hasFlag(options::OPT_fcoverage_mapping,
                      options::OPT_fno_coverage_mapping, false))
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "-fcoverage-mcdc"
          << "-fcoverage-mapping";

    CmdArgs.push_back("-fcoverage-mcdc");
  }

  if (Arg *A = Args.getLastArg(options::OPT_ffile_compilation_dir_EQ,
                               options::OPT_fcoverage_compilation_dir_EQ)) {
    if (A->getOption().matches(options::OPT_ffile_compilation_dir_EQ))
      CmdArgs.push_back(Args.MakeArgString(
          Twine("-fcoverage-compilation-dir=") + A->getValue()));
    else
      A->render(Args, CmdArgs);
  } else if (llvm::ErrorOr<std::string> CWD =
                 D.getVFS().getCurrentWorkingDirectory()) {
    CmdArgs.push_back(Args.MakeArgString("-fcoverage-compilation-dir=" + *CWD));
  }

  if (Args.hasArg(options::OPT_fprofile_exclude_files_EQ)) {
    auto *Arg = Args.getLastArg(options::OPT_fprofile_exclude_files_EQ);
    if (!Args.hasArg(options::OPT_coverage))
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "-fprofile-exclude-files="
          << "--coverage";

    StringRef v = Arg->getValue();
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-fprofile-exclude-files=" + v)));
  }

  if (Args.hasArg(options::OPT_fprofile_filter_files_EQ)) {
    auto *Arg = Args.getLastArg(options::OPT_fprofile_filter_files_EQ);
    if (!Args.hasArg(options::OPT_coverage))
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "-fprofile-filter-files="
          << "--coverage";

    StringRef v = Arg->getValue();
    CmdArgs.push_back(Args.MakeArgString(Twine("-fprofile-filter-files=" + v)));
  }

  if (const auto *A = Args.getLastArg(options::OPT_fprofile_update_EQ)) {
    StringRef Val = A->getValue();
    if (Val == "atomic" || Val == "prefer-atomic")
      CmdArgs.push_back("-fprofile-update=atomic");
    else if (Val != "single")
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
  }

  int FunctionGroups = 1;
  int SelectedFunctionGroup = 0;
  if (const auto *A = Args.getLastArg(options::OPT_fprofile_function_groups)) {
    StringRef Val = A->getValue();
    if (Val.getAsInteger(0, FunctionGroups) || FunctionGroups < 1)
      D.Diag(diag::err_drv_invalid_int_value) << A->getAsString(Args) << Val;
  }
  if (const auto *A =
          Args.getLastArg(options::OPT_fprofile_selected_function_group)) {
    StringRef Val = A->getValue();
    if (Val.getAsInteger(0, SelectedFunctionGroup) ||
        SelectedFunctionGroup < 0 || SelectedFunctionGroup >= FunctionGroups)
      D.Diag(diag::err_drv_invalid_int_value) << A->getAsString(Args) << Val;
  }
  if (FunctionGroups != 1)
    CmdArgs.push_back(Args.MakeArgString("-fprofile-function-groups=" +
                                         Twine(FunctionGroups)));
  if (SelectedFunctionGroup != 0)
    CmdArgs.push_back(Args.MakeArgString("-fprofile-selected-function-group=" +
                                         Twine(SelectedFunctionGroup)));

  // Leave -fprofile-dir= an unused argument unless .gcda emission is
  // enabled. To be polite, with '-fprofile-arcs -fno-profile-arcs' consider
  // the flag used. There is no -fno-profile-dir, so the user has no
  // targeted way to suppress the warning.
  Arg *FProfileDir = nullptr;
  if (Args.hasArg(options::OPT_fprofile_arcs) ||
      Args.hasArg(options::OPT_coverage))
    FProfileDir = Args.getLastArg(options::OPT_fprofile_dir);

  // TODO: Don't claim -c/-S to warn about -fsyntax-only -c/-S, -E -c/-S,
  // like we warn about -fsyntax-only -E.
  (void)(Args.hasArg(options::OPT_c) || Args.hasArg(options::OPT_S));

  // Put the .gcno and .gcda files (if needed) next to the primary output file,
  // or fall back to a file in the current directory for `clang -c --coverage
  // d/a.c` in the absence of -o.
  if (EmitCovNotes || EmitCovData) {
    SmallString<128> CoverageFilename;
    if (Arg *DumpDir = Args.getLastArgNoClaim(options::OPT_dumpdir)) {
      // Form ${dumpdir}${basename}.gcno. Note that dumpdir may not end with a
      // path separator.
      CoverageFilename = DumpDir->getValue();
      CoverageFilename += llvm::sys::path::filename(Output.getBaseInput());
    } else if (Arg *FinalOutput =
                   C.getArgs().getLastArg(options::OPT__SLASH_Fo)) {
      CoverageFilename = FinalOutput->getValue();
    } else if (Arg *FinalOutput = C.getArgs().getLastArg(options::OPT_o)) {
      CoverageFilename = FinalOutput->getValue();
    } else {
      CoverageFilename = llvm::sys::path::filename(Output.getBaseInput());
    }
    if (llvm::sys::path::is_relative(CoverageFilename))
      (void)D.getVFS().makeAbsolute(CoverageFilename);
    llvm::sys::path::replace_extension(CoverageFilename, "gcno");
    if (EmitCovNotes) {
      CmdArgs.push_back(
          Args.MakeArgString("-coverage-notes-file=" + CoverageFilename));
    }

    if (EmitCovData) {
      if (FProfileDir) {
        SmallString<128> Gcno = std::move(CoverageFilename);
        CoverageFilename = FProfileDir->getValue();
        llvm::sys::path::append(CoverageFilename, Gcno);
      }
      llvm::sys::path::replace_extension(CoverageFilename, "gcda");
      CmdArgs.push_back(
          Args.MakeArgString("-coverage-data-file=" + CoverageFilename));
    }
  }
}

static void
RenderDebugEnablingArgs(const ArgList &Args, ArgStringList &CmdArgs,
                        llvm::codegenoptions::DebugInfoKind DebugInfoKind,
                        unsigned DwarfVersion,
                        llvm::DebuggerKind DebuggerTuning) {
  addDebugInfoKind(CmdArgs, DebugInfoKind);
  if (DwarfVersion > 0)
    CmdArgs.push_back(
        Args.MakeArgString("-dwarf-version=" + Twine(DwarfVersion)));
  switch (DebuggerTuning) {
  case llvm::DebuggerKind::GDB:
    CmdArgs.push_back("-debugger-tuning=gdb");
    break;
  case llvm::DebuggerKind::LLDB:
    CmdArgs.push_back("-debugger-tuning=lldb");
    break;
  case llvm::DebuggerKind::SCE:
    CmdArgs.push_back("-debugger-tuning=sce");
    break;
  case llvm::DebuggerKind::DBX:
    CmdArgs.push_back("-debugger-tuning=dbx");
    break;
  default:
    break;
  }
}

static bool checkDebugInfoOption(const Arg *A, const ArgList &Args,
                                 const Driver &D, const ToolChain &TC) {
  assert(A && "Expected non-nullptr argument.");
  if (TC.supportsDebugInfoOption(A))
    return true;
  D.Diag(diag::warn_drv_unsupported_debug_info_opt_for_target)
      << A->getAsString(Args) << TC.getTripleString();
  return false;
}

static void RenderDebugInfoCompressionArgs(const ArgList &Args,
                                           ArgStringList &CmdArgs,
                                           const Driver &D,
                                           const ToolChain &TC) {
  const Arg *A = Args.getLastArg(options::OPT_gz_EQ);
  if (!A)
    return;
  if (checkDebugInfoOption(A, Args, D, TC)) {
    StringRef Value = A->getValue();
    if (Value == "none") {
      CmdArgs.push_back("--compress-debug-sections=none");
    } else if (Value == "zlib") {
      if (llvm::compression::zlib::isAvailable()) {
        CmdArgs.push_back(
            Args.MakeArgString("--compress-debug-sections=" + Twine(Value)));
      } else {
        D.Diag(diag::warn_debug_compression_unavailable) << "zlib";
      }
    } else if (Value == "zstd") {
      if (llvm::compression::zstd::isAvailable()) {
        CmdArgs.push_back(
            Args.MakeArgString("--compress-debug-sections=" + Twine(Value)));
      } else {
        D.Diag(diag::warn_debug_compression_unavailable) << "zstd";
      }
    } else {
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    }
  }
}

static void handleAMDGPUCodeObjectVersionOptions(const Driver &D,
                                                 const ArgList &Args,
                                                 ArgStringList &CmdArgs,
                                                 bool IsCC1As = false) {
  // If no version was requested by the user, use the default value from the
  // back end. This is consistent with the value returned from
  // getAMDGPUCodeObjectVersion. This lets clang emit IR for amdgpu without
  // requiring the corresponding llvm to have the AMDGPU target enabled,
  // provided the user (e.g. front end tests) can use the default.
  if (haveAMDGPUCodeObjectVersionArgument(D, Args)) {
    unsigned CodeObjVer = getAMDGPUCodeObjectVersion(D, Args);
    CmdArgs.insert(CmdArgs.begin() + 1,
                   Args.MakeArgString(Twine("--amdhsa-code-object-version=") +
                                      Twine(CodeObjVer)));
    CmdArgs.insert(CmdArgs.begin() + 1, "-mllvm");
    // -cc1as does not accept -mcode-object-version option.
    if (!IsCC1As)
      CmdArgs.insert(CmdArgs.begin() + 1,
                     Args.MakeArgString(Twine("-mcode-object-version=") +
                                        Twine(CodeObjVer)));
  }
}

static bool maybeHasClangPchSignature(const Driver &D, StringRef Path) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> MemBuf =
      D.getVFS().getBufferForFile(Path);
  if (!MemBuf)
    return false;
  llvm::file_magic Magic = llvm::identify_magic((*MemBuf)->getBuffer());
  if (Magic == llvm::file_magic::unknown)
    return false;
  // Return true for both raw Clang AST files and object files which may
  // contain a __clangast section.
  if (Magic == llvm::file_magic::clang_ast)
    return true;
  Expected<std::unique_ptr<llvm::object::ObjectFile>> Obj =
      llvm::object::ObjectFile::createObjectFile(**MemBuf, Magic);
  return !Obj.takeError();
}

static bool gchProbe(const Driver &D, StringRef Path) {
  llvm::ErrorOr<llvm::vfs::Status> Status = D.getVFS().status(Path);
  if (!Status)
    return false;

  if (Status->isDirectory()) {
    std::error_code EC;
    for (llvm::vfs::directory_iterator DI = D.getVFS().dir_begin(Path, EC), DE;
         !EC && DI != DE; DI = DI.increment(EC)) {
      if (maybeHasClangPchSignature(D, DI->path()))
        return true;
    }
    D.Diag(diag::warn_drv_pch_ignoring_gch_dir) << Path;
    return false;
  }

  if (maybeHasClangPchSignature(D, Path))
    return true;
  D.Diag(diag::warn_drv_pch_ignoring_gch_file) << Path;
  return false;
}

void Clang::AddPreprocessingOptions(Compilation &C, const JobAction &JA,
                                    const Driver &D, const ArgList &Args,
                                    ArgStringList &CmdArgs,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs) const {
  const bool IsIAMCU = getToolChain().getTriple().isOSIAMCU();

  CheckPreprocessingOptions(D, Args);

  Args.AddLastArg(CmdArgs, options::OPT_C);
  Args.AddLastArg(CmdArgs, options::OPT_CC);

  // Handle dependency file generation.
  Arg *ArgM = Args.getLastArg(options::OPT_MM);
  if (!ArgM)
    ArgM = Args.getLastArg(options::OPT_M);
  Arg *ArgMD = Args.getLastArg(options::OPT_MMD);
  if (!ArgMD)
    ArgMD = Args.getLastArg(options::OPT_MD);

  // -M and -MM imply -w.
  if (ArgM)
    CmdArgs.push_back("-w");
  else
    ArgM = ArgMD;

  if (ArgM) {
    // Determine the output location.
    const char *DepFile;
    if (Arg *MF = Args.getLastArg(options::OPT_MF)) {
      DepFile = MF->getValue();
      C.addFailureResultFile(DepFile, &JA);
    } else if (Output.getType() == types::TY_Dependencies) {
      DepFile = Output.getFilename();
    } else if (!ArgMD) {
      DepFile = "-";
    } else {
      DepFile = getDependencyFileName(Args, Inputs);
      C.addFailureResultFile(DepFile, &JA);
    }
    CmdArgs.push_back("-dependency-file");
    CmdArgs.push_back(DepFile);

    bool HasTarget = false;
    for (const Arg *A : Args.filtered(options::OPT_MT, options::OPT_MQ)) {
      HasTarget = true;
      A->claim();
      if (A->getOption().matches(options::OPT_MT)) {
        A->render(Args, CmdArgs);
      } else {
        CmdArgs.push_back("-MT");
        SmallString<128> Quoted;
        quoteMakeTarget(A->getValue(), Quoted);
        CmdArgs.push_back(Args.MakeArgString(Quoted));
      }
    }

    // Add a default target if one wasn't specified.
    if (!HasTarget) {
      const char *DepTarget;

      // If user provided -o, that is the dependency target, except
      // when we are only generating a dependency file.
      Arg *OutputOpt = Args.getLastArg(options::OPT_o, options::OPT__SLASH_Fo);
      if (OutputOpt && Output.getType() != types::TY_Dependencies) {
        DepTarget = OutputOpt->getValue();
      } else {
        // Otherwise derive from the base input.
        //
        // FIXME: This should use the computed output file location.
        SmallString<128> P(Inputs[0].getBaseInput());
        llvm::sys::path::replace_extension(P, "o");
        DepTarget = Args.MakeArgString(llvm::sys::path::filename(P));
      }

      CmdArgs.push_back("-MT");
      SmallString<128> Quoted;
      quoteMakeTarget(DepTarget, Quoted);
      CmdArgs.push_back(Args.MakeArgString(Quoted));
    }

    if (ArgM->getOption().matches(options::OPT_M) ||
        ArgM->getOption().matches(options::OPT_MD))
      CmdArgs.push_back("-sys-header-deps");
    if ((isa<PrecompileJobAction>(JA) &&
         !Args.hasArg(options::OPT_fno_module_file_deps)) ||
        Args.hasArg(options::OPT_fmodule_file_deps))
      CmdArgs.push_back("-module-file-deps");
  }

  if (Args.hasArg(options::OPT_MG)) {
    if (!ArgM || ArgM->getOption().matches(options::OPT_MD) ||
        ArgM->getOption().matches(options::OPT_MMD))
      D.Diag(diag::err_drv_mg_requires_m_or_mm);
    CmdArgs.push_back("-MG");
  }

  Args.AddLastArg(CmdArgs, options::OPT_MP);
  Args.AddLastArg(CmdArgs, options::OPT_MV);

  // Add offload include arguments specific for CUDA/HIP.  This must happen
  // before we -I or -include anything else, because we must pick up the
  // CUDA/HIP headers from the particular CUDA/ROCm installation, rather than
  // from e.g. /usr/local/include.
  if (JA.isOffloading(Action::OFK_Cuda))
    getToolChain().AddCudaIncludeArgs(Args, CmdArgs);
  if (JA.isOffloading(Action::OFK_HIP))
    getToolChain().AddHIPIncludeArgs(Args, CmdArgs);

  // If we are offloading to a target via OpenMP we need to include the
  // openmp_wrappers folder which contains alternative system headers.
  if (JA.isDeviceOffloading(Action::OFK_OpenMP) &&
      !Args.hasArg(options::OPT_nostdinc) &&
      !Args.hasArg(options::OPT_nogpuinc) &&
      (getToolChain().getTriple().isNVPTX() ||
       getToolChain().getTriple().isAMDGCN())) {
    if (!Args.hasArg(options::OPT_nobuiltininc)) {
      // Add openmp_wrappers/* to our system include path.  This lets us wrap
      // standard library headers.
      SmallString<128> P(D.ResourceDir);
      llvm::sys::path::append(P, "include");
      llvm::sys::path::append(P, "openmp_wrappers");
      CmdArgs.push_back("-internal-isystem");
      CmdArgs.push_back(Args.MakeArgString(P));
    }

    CmdArgs.push_back("-include");
    CmdArgs.push_back("__clang_openmp_device_functions.h");
  }

  // Add -i* options, and automatically translate to
  // -include-pch/-include-pth for transparent PCH support. It's
  // wonky, but we include looking for .gch so we can support seamless
  // replacement into a build system already set up to be generating
  // .gch files.

  if (getToolChain().getDriver().IsCLMode()) {
    const Arg *YcArg = Args.getLastArg(options::OPT__SLASH_Yc);
    const Arg *YuArg = Args.getLastArg(options::OPT__SLASH_Yu);
    if (YcArg && JA.getKind() >= Action::PrecompileJobClass &&
        JA.getKind() <= Action::AssembleJobClass) {
      CmdArgs.push_back(Args.MakeArgString("-building-pch-with-obj"));
      // -fpch-instantiate-templates is the default when creating
      // precomp using /Yc
      if (Args.hasFlag(options::OPT_fpch_instantiate_templates,
                       options::OPT_fno_pch_instantiate_templates, true))
        CmdArgs.push_back(Args.MakeArgString("-fpch-instantiate-templates"));
    }
    if (YcArg || YuArg) {
      StringRef ThroughHeader = YcArg ? YcArg->getValue() : YuArg->getValue();
      if (!isa<PrecompileJobAction>(JA)) {
        CmdArgs.push_back("-include-pch");
        CmdArgs.push_back(Args.MakeArgString(D.GetClPchPath(
            C, !ThroughHeader.empty()
                   ? ThroughHeader
                   : llvm::sys::path::filename(Inputs[0].getBaseInput()))));
      }

      if (ThroughHeader.empty()) {
        CmdArgs.push_back(Args.MakeArgString(
            Twine("-pch-through-hdrstop-") + (YcArg ? "create" : "use")));
      } else {
        CmdArgs.push_back(
            Args.MakeArgString(Twine("-pch-through-header=") + ThroughHeader));
      }
    }
  }

  bool RenderedImplicitInclude = false;
  for (const Arg *A : Args.filtered(options::OPT_clang_i_Group)) {
    if (A->getOption().matches(options::OPT_include) &&
        D.getProbePrecompiled()) {
      // Handling of gcc-style gch precompiled headers.
      bool IsFirstImplicitInclude = !RenderedImplicitInclude;
      RenderedImplicitInclude = true;

      bool FoundPCH = false;
      SmallString<128> P(A->getValue());
      // We want the files to have a name like foo.h.pch. Add a dummy extension
      // so that replace_extension does the right thing.
      P += ".dummy";
      llvm::sys::path::replace_extension(P, "pch");
      if (D.getVFS().exists(P))
        FoundPCH = true;

      if (!FoundPCH) {
        // For GCC compat, probe for a file or directory ending in .gch instead.
        llvm::sys::path::replace_extension(P, "gch");
        FoundPCH = gchProbe(D, P.str());
      }

      if (FoundPCH) {
        if (IsFirstImplicitInclude) {
          A->claim();
          CmdArgs.push_back("-include-pch");
          CmdArgs.push_back(Args.MakeArgString(P));
          continue;
        } else {
          // Ignore the PCH if not first on command line and emit warning.
          D.Diag(diag::warn_drv_pch_not_first_include) << P
                                                       << A->getAsString(Args);
        }
      }
    } else if (A->getOption().matches(options::OPT_isystem_after)) {
      // Handling of paths which must come late.  These entries are handled by
      // the toolchain itself after the resource dir is inserted in the right
      // search order.
      // Do not claim the argument so that the use of the argument does not
      // silently go unnoticed on toolchains which do not honour the option.
      continue;
    } else if (A->getOption().matches(options::OPT_stdlibxx_isystem)) {
      // Translated to -internal-isystem by the driver, no need to pass to cc1.
      continue;
    } else if (A->getOption().matches(options::OPT_ibuiltininc)) {
      // This is used only by the driver. No need to pass to cc1.
      continue;
    }

    // Not translated, render as usual.
    A->claim();
    A->render(Args, CmdArgs);
  }

  Args.addAllArgs(CmdArgs,
                  {options::OPT_D, options::OPT_U, options::OPT_I_Group,
                   options::OPT_F, options::OPT_index_header_map,
                   options::OPT_embed_dir_EQ});

  // Add -Wp, and -Xpreprocessor if using the preprocessor.

  // FIXME: There is a very unfortunate problem here, some troubled
  // souls abuse -Wp, to pass preprocessor options in gcc syntax. To
  // really support that we would have to parse and then translate
  // those options. :(
  Args.AddAllArgValues(CmdArgs, options::OPT_Wp_COMMA,
                       options::OPT_Xpreprocessor);

  // -I- is a deprecated GCC feature, reject it.
  if (Arg *A = Args.getLastArg(options::OPT_I_))
    D.Diag(diag::err_drv_I_dash_not_supported) << A->getAsString(Args);

  // If we have a --sysroot, and don't have an explicit -isysroot flag, add an
  // -isysroot to the CC1 invocation.
  StringRef sysroot = C.getSysRoot();
  if (sysroot != "") {
    if (!Args.hasArg(options::OPT_isysroot)) {
      CmdArgs.push_back("-isysroot");
      CmdArgs.push_back(C.getArgs().MakeArgString(sysroot));
    }
  }

  // Parse additional include paths from environment variables.
  // FIXME: We should probably sink the logic for handling these from the
  // frontend into the driver. It will allow deleting 4 otherwise unused flags.
  // CPATH - included following the user specified includes (but prior to
  // builtin and standard includes).
  addDirectoryList(Args, CmdArgs, "-I", "CPATH");
  // C_INCLUDE_PATH - system includes enabled when compiling C.
  addDirectoryList(Args, CmdArgs, "-c-isystem", "C_INCLUDE_PATH");
  // CPLUS_INCLUDE_PATH - system includes enabled when compiling C++.
  addDirectoryList(Args, CmdArgs, "-cxx-isystem", "CPLUS_INCLUDE_PATH");
  // OBJC_INCLUDE_PATH - system includes enabled when compiling ObjC.
  addDirectoryList(Args, CmdArgs, "-objc-isystem", "OBJC_INCLUDE_PATH");
  // OBJCPLUS_INCLUDE_PATH - system includes enabled when compiling ObjC++.
  addDirectoryList(Args, CmdArgs, "-objcxx-isystem", "OBJCPLUS_INCLUDE_PATH");

  // While adding the include arguments, we also attempt to retrieve the
  // arguments of related offloading toolchains or arguments that are specific
  // of an offloading programming model.

  // Add C++ include arguments, if needed.
  if (types::isCXX(Inputs[0].getType())) {
    bool HasStdlibxxIsystem = Args.hasArg(options::OPT_stdlibxx_isystem);
    forAllAssociatedToolChains(
        C, JA, getToolChain(),
        [&Args, &CmdArgs, HasStdlibxxIsystem](const ToolChain &TC) {
          HasStdlibxxIsystem ? TC.AddClangCXXStdlibIsystemArgs(Args, CmdArgs)
                             : TC.AddClangCXXStdlibIncludeArgs(Args, CmdArgs);
        });
  }

  // If we are compiling for a GPU target we want to override the system headers
  // with ones created by the 'libc' project if present.
  // TODO: This should be moved to `AddClangSystemIncludeArgs` by passing the
  //       OffloadKind as an argument.
  if (!Args.hasArg(options::OPT_nostdinc) &&
      !Args.hasArg(options::OPT_nogpuinc) &&
      !Args.hasArg(options::OPT_nobuiltininc)) {
    // Without an offloading language we will include these headers directly.
    // Offloading languages will instead only use the declarations stored in
    // the resource directory at clang/lib/Headers/llvm_libc_wrappers.
    if ((getToolChain().getTriple().isNVPTX() ||
         getToolChain().getTriple().isAMDGCN()) &&
        C.getActiveOffloadKinds() == Action::OFK_None) {
      SmallString<128> P(llvm::sys::path::parent_path(D.Dir));
      llvm::sys::path::append(P, "include");
      llvm::sys::path::append(P, getToolChain().getTripleString());
      CmdArgs.push_back("-internal-isystem");
      CmdArgs.push_back(Args.MakeArgString(P));
    } else if (C.getActiveOffloadKinds() == Action::OFK_OpenMP) {
      // TODO: CUDA / HIP include their own headers for some common functions
      // implemented here. We'll need to clean those up so they do not conflict.
      SmallString<128> P(D.ResourceDir);
      llvm::sys::path::append(P, "include");
      llvm::sys::path::append(P, "llvm_libc_wrappers");
      CmdArgs.push_back("-internal-isystem");
      CmdArgs.push_back(Args.MakeArgString(P));
    }
  }

  // Add system include arguments for all targets but IAMCU.
  if (!IsIAMCU)
    forAllAssociatedToolChains(C, JA, getToolChain(),
                               [&Args, &CmdArgs](const ToolChain &TC) {
                                 TC.AddClangSystemIncludeArgs(Args, CmdArgs);
                               });
  else {
    // For IAMCU add special include arguments.
    getToolChain().AddIAMCUIncludeArgs(Args, CmdArgs);
  }

  addMacroPrefixMapArg(D, Args, CmdArgs);
  addCoveragePrefixMapArg(D, Args, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_ffile_reproducible,
                  options::OPT_fno_file_reproducible);

  if (const char *Epoch = std::getenv("SOURCE_DATE_EPOCH")) {
    CmdArgs.push_back("-source-date-epoch");
    CmdArgs.push_back(Args.MakeArgString(Epoch));
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fdefine_target_os_macros,
                    options::OPT_fno_define_target_os_macros);
}

// FIXME: Move to target hook.
static bool isSignedCharDefault(const llvm::Triple &Triple) {
  switch (Triple.getArch()) {
  default:
    return true;

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    if (Triple.isOSDarwin() || Triple.isOSWindows())
      return true;
    return false;

  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
    if (Triple.isOSDarwin())
      return true;
    return false;

  case llvm::Triple::hexagon:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
  case llvm::Triple::systemz:
  case llvm::Triple::xcore:
    return false;
  }
}

static bool hasMultipleInvocations(const llvm::Triple &Triple,
                                   const ArgList &Args) {
  // Supported only on Darwin where we invoke the compiler multiple times
  // followed by an invocation to lipo.
  if (!Triple.isOSDarwin())
    return false;
  // If more than one "-arch <arch>" is specified, we're targeting multiple
  // architectures resulting in a fat binary.
  return Args.getAllArgValues(options::OPT_arch).size() > 1;
}

static bool checkRemarksOptions(const Driver &D, const ArgList &Args,
                                const llvm::Triple &Triple) {
  // When enabling remarks, we need to error if:
  // * The remark file is specified but we're targeting multiple architectures,
  // which means more than one remark file is being generated.
  bool hasMultipleInvocations = ::hasMultipleInvocations(Triple, Args);
  bool hasExplicitOutputFile =
      Args.getLastArg(options::OPT_foptimization_record_file_EQ);
  if (hasMultipleInvocations && hasExplicitOutputFile) {
    D.Diag(diag::err_drv_invalid_output_with_multiple_archs)
        << "-foptimization-record-file";
    return false;
  }
  return true;
}

static void renderRemarksOptions(const ArgList &Args, ArgStringList &CmdArgs,
                                 const llvm::Triple &Triple,
                                 const InputInfo &Input,
                                 const InputInfo &Output, const JobAction &JA) {
  StringRef Format = "yaml";
  if (const Arg *A = Args.getLastArg(options::OPT_fsave_optimization_record_EQ))
    Format = A->getValue();

  CmdArgs.push_back("-opt-record-file");

  const Arg *A = Args.getLastArg(options::OPT_foptimization_record_file_EQ);
  if (A) {
    CmdArgs.push_back(A->getValue());
  } else {
    bool hasMultipleArchs =
        Triple.isOSDarwin() && // Only supported on Darwin platforms.
        Args.getAllArgValues(options::OPT_arch).size() > 1;

    SmallString<128> F;

    if (Args.hasArg(options::OPT_c) || Args.hasArg(options::OPT_S)) {
      if (Arg *FinalOutput = Args.getLastArg(options::OPT_o))
        F = FinalOutput->getValue();
    } else {
      if (Format != "yaml" && // For YAML, keep the original behavior.
          Triple.isOSDarwin() && // Enable this only on darwin, since it's the only platform supporting .dSYM bundles.
          Output.isFilename())
        F = Output.getFilename();
    }

    if (F.empty()) {
      // Use the input filename.
      F = llvm::sys::path::stem(Input.getBaseInput());

      // If we're compiling for an offload architecture (i.e. a CUDA device),
      // we need to make the file name for the device compilation different
      // from the host compilation.
      if (!JA.isDeviceOffloading(Action::OFK_None) &&
          !JA.isDeviceOffloading(Action::OFK_Host)) {
        llvm::sys::path::replace_extension(F, "");
        F += Action::GetOffloadingFileNamePrefix(JA.getOffloadingDeviceKind(),
                                                 Triple.normalize());
        F += "-";
        F += JA.getOffloadingArch();
      }
    }

    // If we're having more than one "-arch", we should name the files
    // differently so that every cc1 invocation writes to a different file.
    // We're doing that by appending "-<arch>" with "<arch>" being the arch
    // name from the triple.
    if (hasMultipleArchs) {
      // First, remember the extension.
      SmallString<64> OldExtension = llvm::sys::path::extension(F);
      // then, remove it.
      llvm::sys::path::replace_extension(F, "");
      // attach -<arch> to it.
      F += "-";
      F += Triple.getArchName();
      // put back the extension.
      llvm::sys::path::replace_extension(F, OldExtension);
    }

    SmallString<32> Extension;
    Extension += "opt.";
    Extension += Format;

    llvm::sys::path::replace_extension(F, Extension);
    CmdArgs.push_back(Args.MakeArgString(F));
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_foptimization_record_passes_EQ)) {
    CmdArgs.push_back("-opt-record-passes");
    CmdArgs.push_back(A->getValue());
  }

  if (!Format.empty()) {
    CmdArgs.push_back("-opt-record-format");
    CmdArgs.push_back(Format.data());
  }
}

void AddAAPCSVolatileBitfieldArgs(const ArgList &Args, ArgStringList &CmdArgs) {
  if (!Args.hasFlag(options::OPT_faapcs_bitfield_width,
                    options::OPT_fno_aapcs_bitfield_width, true))
    CmdArgs.push_back("-fno-aapcs-bitfield-width");

  if (Args.getLastArg(options::OPT_ForceAAPCSBitfieldLoad))
    CmdArgs.push_back("-faapcs-bitfield-load");
}

namespace {
void RenderARMABI(const Driver &D, const llvm::Triple &Triple,
                  const ArgList &Args, ArgStringList &CmdArgs) {
  // Select the ABI to use.
  // FIXME: Support -meabi.
  // FIXME: Parts of this are duplicated in the backend, unify this somehow.
  const char *ABIName = nullptr;
  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ)) {
    ABIName = A->getValue();
  } else {
    std::string CPU = getCPUName(D, Args, Triple, /*FromAs*/ false);
    ABIName = llvm::ARM::computeDefaultTargetABI(Triple, CPU).data();
  }

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName);
}

void AddUnalignedAccessWarning(ArgStringList &CmdArgs) {
  auto StrictAlignIter =
      llvm::find_if(llvm::reverse(CmdArgs), [](StringRef Arg) {
        return Arg == "+strict-align" || Arg == "-strict-align";
      });
  if (StrictAlignIter != CmdArgs.rend() &&
      StringRef(*StrictAlignIter) == "+strict-align")
    CmdArgs.push_back("-Wunaligned-access");
}
}

// Each combination of options here forms a signing schema, and in most cases
// each signing schema is its own incompatible ABI. The default values of the
// options represent the default signing schema.
static void handlePAuthABI(const ArgList &DriverArgs, ArgStringList &CC1Args) {
  if (!DriverArgs.hasArg(options::OPT_fptrauth_intrinsics,
                         options::OPT_fno_ptrauth_intrinsics))
    CC1Args.push_back("-fptrauth-intrinsics");

  if (!DriverArgs.hasArg(options::OPT_fptrauth_calls,
                         options::OPT_fno_ptrauth_calls))
    CC1Args.push_back("-fptrauth-calls");

  if (!DriverArgs.hasArg(options::OPT_fptrauth_returns,
                         options::OPT_fno_ptrauth_returns))
    CC1Args.push_back("-fptrauth-returns");

  if (!DriverArgs.hasArg(options::OPT_fptrauth_auth_traps,
                         options::OPT_fno_ptrauth_auth_traps))
    CC1Args.push_back("-fptrauth-auth-traps");

  if (!DriverArgs.hasArg(
          options::OPT_fptrauth_vtable_pointer_address_discrimination,
          options::OPT_fno_ptrauth_vtable_pointer_address_discrimination))
    CC1Args.push_back("-fptrauth-vtable-pointer-address-discrimination");

  if (!DriverArgs.hasArg(
          options::OPT_fptrauth_vtable_pointer_type_discrimination,
          options::OPT_fno_ptrauth_vtable_pointer_type_discrimination))
    CC1Args.push_back("-fptrauth-vtable-pointer-type-discrimination");

  if (!DriverArgs.hasArg(options::OPT_fptrauth_indirect_gotos,
                         options::OPT_fno_ptrauth_indirect_gotos))
    CC1Args.push_back("-fptrauth-indirect-gotos");

  if (!DriverArgs.hasArg(options::OPT_fptrauth_init_fini,
                         options::OPT_fno_ptrauth_init_fini))
    CC1Args.push_back("-fptrauth-init-fini");
}

static void CollectARMPACBTIOptions(const ToolChain &TC, const ArgList &Args,
                                    ArgStringList &CmdArgs, bool isAArch64) {
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  const Arg *A = isAArch64
                     ? Args.getLastArg(options::OPT_msign_return_address_EQ,
                                       options::OPT_mbranch_protection_EQ)
                     : Args.getLastArg(options::OPT_mbranch_protection_EQ);
  if (!A) {
    if (Triple.isOSOpenBSD() && isAArch64) {
      CmdArgs.push_back("-msign-return-address=non-leaf");
      CmdArgs.push_back("-msign-return-address-key=a_key");
      CmdArgs.push_back("-mbranch-target-enforce");
    }
    return;
  }

  const Driver &D = TC.getDriver();
  if (!(isAArch64 || (Triple.isArmT32() && Triple.isArmMClass())))
    D.Diag(diag::warn_incompatible_branch_protection_option)
        << Triple.getArchName();

  StringRef Scope, Key;
  bool IndirectBranches, BranchProtectionPAuthLR, GuardedControlStack;

  if (A->getOption().matches(options::OPT_msign_return_address_EQ)) {
    Scope = A->getValue();
    if (Scope != "none" && Scope != "non-leaf" && Scope != "all")
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Scope;
    Key = "a_key";
    if (Triple.isOSOpenBSD() && isAArch64)
      IndirectBranches = true;
    else
      IndirectBranches = false;
    BranchProtectionPAuthLR = false;
    GuardedControlStack = false;
  } else {
    StringRef DiagMsg;
    llvm::ARM::ParsedBranchProtection PBP;
    bool EnablePAuthLR = false;

    // To know if we need to enable PAuth-LR As part of the standard branch
    // protection option, it needs to be determined if the feature has been
    // activated in the `march` argument. This information is stored within the
    // CmdArgs variable and can be found using a search.
    if (isAArch64) {
      auto isPAuthLR = [](const char *member) {
        llvm::AArch64::ExtensionInfo pauthlr_extension =
            llvm::AArch64::getExtensionByID(llvm::AArch64::AEK_PAUTHLR);
        return pauthlr_extension.PosTargetFeature == member;
      };

      if (std::any_of(CmdArgs.begin(), CmdArgs.end(), isPAuthLR))
        EnablePAuthLR = true;
    }
    if (!llvm::ARM::parseBranchProtection(A->getValue(), PBP, DiagMsg,
                                          EnablePAuthLR))
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << DiagMsg;
    if (!isAArch64 && PBP.Key == "b_key")
      D.Diag(diag::warn_unsupported_branch_protection)
          << "b-key" << A->getAsString(Args);
    Scope = PBP.Scope;
    Key = PBP.Key;
    BranchProtectionPAuthLR = PBP.BranchProtectionPAuthLR;
    IndirectBranches = PBP.BranchTargetEnforcement;
    GuardedControlStack = PBP.GuardedControlStack;
  }

  CmdArgs.push_back(
      Args.MakeArgString(Twine("-msign-return-address=") + Scope));
  if (Scope != "none") {
    if (Triple.getEnvironment() == llvm::Triple::PAuthTest)
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << Triple.getTriple();
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-msign-return-address-key=") + Key));
  }
  if (BranchProtectionPAuthLR) {
    if (Triple.getEnvironment() == llvm::Triple::PAuthTest)
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << Triple.getTriple();
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-mbranch-protection-pauth-lr")));
  }
  if (IndirectBranches)
    CmdArgs.push_back("-mbranch-target-enforce");
  // GCS is currently untested with PAuthABI, but enabling this could be allowed
  // in future after testing with a suitable system.
  if (GuardedControlStack) {
    if (Triple.getEnvironment() == llvm::Triple::PAuthTest)
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << Triple.getTriple();
    CmdArgs.push_back("-mguarded-control-stack");
  }
}

void Clang::AddARMTargetArgs(const llvm::Triple &Triple, const ArgList &Args,
                             ArgStringList &CmdArgs, bool KernelOrKext) const {
  RenderARMABI(getToolChain().getDriver(), Triple, Args, CmdArgs);

  // Determine floating point ABI from the options & target defaults.
  arm::FloatABI ABI = arm::getARMFloatABI(getToolChain(), Args);
  if (ABI == arm::FloatABI::Soft) {
    // Floating point operations and argument passing are soft.
    // FIXME: This changes CPP defines, we need -target-soft-float.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else if (ABI == arm::FloatABI::SoftFP) {
    // Floating point operations are hard, but argument passing is soft.
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else {
    // Floating point operations and argument passing are hard.
    assert(ABI == arm::FloatABI::Hard && "Invalid float abi!");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("hard");
  }

  // Forward the -mglobal-merge option for explicit control over the pass.
  if (Arg *A = Args.getLastArg(options::OPT_mglobal_merge,
                               options::OPT_mno_global_merge)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_mno_global_merge))
      CmdArgs.push_back("-arm-global-merge=false");
    else
      CmdArgs.push_back("-arm-global-merge=true");
  }

  if (!Args.hasFlag(options::OPT_mimplicit_float,
                    options::OPT_mno_implicit_float, true))
    CmdArgs.push_back("-no-implicit-float");

  if (Args.getLastArg(options::OPT_mcmse))
    CmdArgs.push_back("-mcmse");

  AddAAPCSVolatileBitfieldArgs(Args, CmdArgs);

  // Enable/disable return address signing and indirect branch targets.
  CollectARMPACBTIOptions(getToolChain(), Args, CmdArgs, false /*isAArch64*/);

  AddUnalignedAccessWarning(CmdArgs);
}

void Clang::RenderTargetOptions(const llvm::Triple &EffectiveTriple,
                                const ArgList &Args, bool KernelOrKext,
                                ArgStringList &CmdArgs) const {
  const ToolChain &TC = getToolChain();

  // Add the target features
  getTargetFeatures(TC.getDriver(), EffectiveTriple, Args, CmdArgs, false);

  // Add target specific flags.
  switch (TC.getArch()) {
  default:
    break;

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    // Use the effective triple, which takes into account the deployment target.
    AddARMTargetArgs(EffectiveTriple, Args, CmdArgs, KernelOrKext);
    break;

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
    AddAArch64TargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64:
    AddLoongArchTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    AddMIPSTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    AddPPCTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    AddRISCVTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::sparcv9:
    AddSparcTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::systemz:
    AddSystemZTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    AddX86TargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::lanai:
    AddLanaiTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::hexagon:
    AddHexagonTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    AddWebAssemblyTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::ve:
    AddVETargetArgs(Args, CmdArgs);
    break;
  }
}

namespace {
void RenderAArch64ABI(const llvm::Triple &Triple, const ArgList &Args,
                      ArgStringList &CmdArgs) {
  const char *ABIName = nullptr;
  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ))
    ABIName = A->getValue();
  else if (Triple.isOSDarwin())
    ABIName = "darwinpcs";
  else if (Triple.getEnvironment() == llvm::Triple::PAuthTest)
    ABIName = "pauthtest";
  else
    ABIName = "aapcs";

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName);
}
}

void Clang::AddAArch64TargetArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();

  if (!Args.hasFlag(options::OPT_mred_zone, options::OPT_mno_red_zone, true) ||
      Args.hasArg(options::OPT_mkernel) ||
      Args.hasArg(options::OPT_fapple_kext))
    CmdArgs.push_back("-disable-red-zone");

  if (!Args.hasFlag(options::OPT_mimplicit_float,
                    options::OPT_mno_implicit_float, true))
    CmdArgs.push_back("-no-implicit-float");

  RenderAArch64ABI(Triple, Args, CmdArgs);

  // Forward the -mglobal-merge option for explicit control over the pass.
  if (Arg *A = Args.getLastArg(options::OPT_mglobal_merge,
                               options::OPT_mno_global_merge)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_mno_global_merge))
      CmdArgs.push_back("-aarch64-enable-global-merge=false");
    else
      CmdArgs.push_back("-aarch64-enable-global-merge=true");
  }

  // Enable/disable return address signing and indirect branch targets.
  CollectARMPACBTIOptions(getToolChain(), Args, CmdArgs, true /*isAArch64*/);

  if (Triple.getEnvironment() == llvm::Triple::PAuthTest)
    handlePAuthABI(Args, CmdArgs);

  // Handle -msve_vector_bits=<bits>
  if (Arg *A = Args.getLastArg(options::OPT_msve_vector_bits_EQ)) {
    StringRef Val = A->getValue();
    const Driver &D = getToolChain().getDriver();
    if (Val == "128" || Val == "256" || Val == "512" || Val == "1024" ||
        Val == "2048" || Val == "128+" || Val == "256+" || Val == "512+" ||
        Val == "1024+" || Val == "2048+") {
      unsigned Bits = 0;
      if (!Val.consume_back("+")) {
        bool Invalid = Val.getAsInteger(10, Bits); (void)Invalid;
        assert(!Invalid && "Failed to parse value");
        CmdArgs.push_back(
            Args.MakeArgString("-mvscale-max=" + llvm::Twine(Bits / 128)));
      }

      bool Invalid = Val.getAsInteger(10, Bits); (void)Invalid;
      assert(!Invalid && "Failed to parse value");
      CmdArgs.push_back(
          Args.MakeArgString("-mvscale-min=" + llvm::Twine(Bits / 128)));
    // Silently drop requests for vector-length agnostic code as it's implied.
    } else if (Val != "scalable")
      // Handle the unsupported values passed to msve-vector-bits.
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
  }

  AddAAPCSVolatileBitfieldArgs(Args, CmdArgs);

  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_mtune_EQ)) {
    CmdArgs.push_back("-tune-cpu");
    if (strcmp(A->getValue(), "native") == 0)
      CmdArgs.push_back(Args.MakeArgString(llvm::sys::getHostCPUName()));
    else
      CmdArgs.push_back(A->getValue());
  }

  AddUnalignedAccessWarning(CmdArgs);

  Args.addOptInFlag(CmdArgs, options::OPT_fptrauth_intrinsics,
                    options::OPT_fno_ptrauth_intrinsics);
  Args.addOptInFlag(CmdArgs, options::OPT_fptrauth_calls,
                    options::OPT_fno_ptrauth_calls);
  Args.addOptInFlag(CmdArgs, options::OPT_fptrauth_returns,
                    options::OPT_fno_ptrauth_returns);
  Args.addOptInFlag(CmdArgs, options::OPT_fptrauth_auth_traps,
                    options::OPT_fno_ptrauth_auth_traps);
  Args.addOptInFlag(
      CmdArgs, options::OPT_fptrauth_vtable_pointer_address_discrimination,
      options::OPT_fno_ptrauth_vtable_pointer_address_discrimination);
  Args.addOptInFlag(
      CmdArgs, options::OPT_fptrauth_vtable_pointer_type_discrimination,
      options::OPT_fno_ptrauth_vtable_pointer_type_discrimination);
  Args.addOptInFlag(
      CmdArgs, options::OPT_fptrauth_type_info_vtable_pointer_discrimination,
      options::OPT_fno_ptrauth_type_info_vtable_pointer_discrimination);
  Args.addOptInFlag(CmdArgs, options::OPT_fptrauth_init_fini,
                    options::OPT_fno_ptrauth_init_fini);
  Args.addOptInFlag(
      CmdArgs, options::OPT_fptrauth_function_pointer_type_discrimination,
      options::OPT_fno_ptrauth_function_pointer_type_discrimination);

  Args.addOptInFlag(CmdArgs, options::OPT_fptrauth_indirect_gotos,
                    options::OPT_fno_ptrauth_indirect_gotos);
}

void Clang::AddLoongArchTargetArgs(const ArgList &Args,
                                   ArgStringList &CmdArgs) const {
  const llvm::Triple &Triple = getToolChain().getTriple();

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(
      loongarch::getLoongArchABI(getToolChain().getDriver(), Args, Triple)
          .data());

  // Handle -mtune.
  if (const Arg *A = Args.getLastArg(options::OPT_mtune_EQ)) {
    std::string TuneCPU = A->getValue();
    TuneCPU = loongarch::postProcessTargetCPUString(TuneCPU, Triple);
    CmdArgs.push_back("-tune-cpu");
    CmdArgs.push_back(Args.MakeArgString(TuneCPU));
  }
}

void Clang::AddMIPSTargetArgs(const ArgList &Args,
                              ArgStringList &CmdArgs) const {
  const Driver &D = getToolChain().getDriver();
  StringRef CPUName;
  StringRef ABIName;
  const llvm::Triple &Triple = getToolChain().getTriple();
  mips::getMipsCPUAndABI(Args, Triple, CPUName, ABIName);

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName.data());

  mips::FloatABI ABI = mips::getMipsFloatABI(D, Args, Triple);
  if (ABI == mips::FloatABI::Soft) {
    // Floating point operations and argument passing are soft.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else {
    // Floating point operations and argument passing are hard.
    assert(ABI == mips::FloatABI::Hard && "Invalid float abi!");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("hard");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mldc1_sdc1,
                               options::OPT_mno_ldc1_sdc1)) {
    if (A->getOption().matches(options::OPT_mno_ldc1_sdc1)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-mno-ldc1-sdc1");
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_mcheck_zero_division,
                               options::OPT_mno_check_zero_division)) {
    if (A->getOption().matches(options::OPT_mno_check_zero_division)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-mno-check-zero-division");
    }
  }

  if (Args.getLastArg(options::OPT_mfix4300)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-mfix4300");
  }

  if (Arg *A = Args.getLastArg(options::OPT_G)) {
    StringRef v = A->getValue();
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString("-mips-ssection-threshold=" + v));
    A->claim();
  }

  Arg *GPOpt = Args.getLastArg(options::OPT_mgpopt, options::OPT_mno_gpopt);
  Arg *ABICalls =
      Args.getLastArg(options::OPT_mabicalls, options::OPT_mno_abicalls);

  // -mabicalls is the default for many MIPS environments, even with -fno-pic.
  // -mgpopt is the default for static, -fno-pic environments but these two
  // options conflict. We want to be certain that -mno-abicalls -mgpopt is
  // the only case where -mllvm -mgpopt is passed.
  // NOTE: We need a warning here or in the backend to warn when -mgpopt is
  //       passed explicitly when compiling something with -mabicalls
  //       (implictly) in affect. Currently the warning is in the backend.
  //
  // When the ABI in use is  N64, we also need to determine the PIC mode that
  // is in use, as -fno-pic for N64 implies -mno-abicalls.
  bool NoABICalls =
      ABICalls && ABICalls->getOption().matches(options::OPT_mno_abicalls);

  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) =
      ParsePICArgs(getToolChain(), Args);

  NoABICalls = NoABICalls ||
               (RelocationModel == llvm::Reloc::Static && ABIName == "n64");

  bool WantGPOpt = GPOpt && GPOpt->getOption().matches(options::OPT_mgpopt);
  // We quietly ignore -mno-gpopt as the backend defaults to -mno-gpopt.
  if (NoABICalls && (!GPOpt || WantGPOpt)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-mgpopt");

    Arg *LocalSData = Args.getLastArg(options::OPT_mlocal_sdata,
                                      options::OPT_mno_local_sdata);
    Arg *ExternSData = Args.getLastArg(options::OPT_mextern_sdata,
                                       options::OPT_mno_extern_sdata);
    Arg *EmbeddedData = Args.getLastArg(options::OPT_membedded_data,
                                        options::OPT_mno_embedded_data);
    if (LocalSData) {
      CmdArgs.push_back("-mllvm");
      if (LocalSData->getOption().matches(options::OPT_mlocal_sdata)) {
        CmdArgs.push_back("-mlocal-sdata=1");
      } else {
        CmdArgs.push_back("-mlocal-sdata=0");
      }
      LocalSData->claim();
    }

    if (ExternSData) {
      CmdArgs.push_back("-mllvm");
      if (ExternSData->getOption().matches(options::OPT_mextern_sdata)) {
        CmdArgs.push_back("-mextern-sdata=1");
      } else {
        CmdArgs.push_back("-mextern-sdata=0");
      }
      ExternSData->claim();
    }

    if (EmbeddedData) {
      CmdArgs.push_back("-mllvm");
      if (EmbeddedData->getOption().matches(options::OPT_membedded_data)) {
        CmdArgs.push_back("-membedded-data=1");
      } else {
        CmdArgs.push_back("-membedded-data=0");
      }
      EmbeddedData->claim();
    }

  } else if ((!ABICalls || (!NoABICalls && ABICalls)) && WantGPOpt)
    D.Diag(diag::warn_drv_unsupported_gpopt) << (ABICalls ? 0 : 1);

  if (GPOpt)
    GPOpt->claim();

  if (Arg *A = Args.getLastArg(options::OPT_mcompact_branches_EQ)) {
    StringRef Val = StringRef(A->getValue());
    if (mips::hasCompactBranches(CPUName)) {
      if (Val == "never" || Val == "always" || Val == "optimal") {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back(Args.MakeArgString("-mips-compact-branches=" + Val));
      } else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
    } else
      D.Diag(diag::warn_target_unsupported_compact_branches) << CPUName;
  }

  if (Arg *A = Args.getLastArg(options::OPT_mrelax_pic_calls,
                               options::OPT_mno_relax_pic_calls)) {
    if (A->getOption().matches(options::OPT_mno_relax_pic_calls)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-mips-jalr-reloc=0");
    }
  }
}

void Clang::AddPPCTargetArgs(const ArgList &Args,
                             ArgStringList &CmdArgs) const {
  const Driver &D = getToolChain().getDriver();
  const llvm::Triple &T = getToolChain().getTriple();
  if (Args.getLastArg(options::OPT_mtune_EQ)) {
    CmdArgs.push_back("-tune-cpu");
    std::string CPU = ppc::getPPCTuneCPU(Args, T);
    CmdArgs.push_back(Args.MakeArgString(CPU));
  }

  // Select the ABI to use.
  const char *ABIName = nullptr;
  if (T.isOSBinFormatELF()) {
    switch (getToolChain().getArch()) {
    case llvm::Triple::ppc64: {
      if (T.isPPC64ELFv2ABI())
        ABIName = "elfv2";
      else
        ABIName = "elfv1";
      break;
    }
    case llvm::Triple::ppc64le:
      ABIName = "elfv2";
      break;
    default:
      break;
    }
  }

  bool IEEELongDouble = getToolChain().defaultToIEEELongDouble();
  bool VecExtabi = false;
  for (const Arg *A : Args.filtered(options::OPT_mabi_EQ)) {
    StringRef V = A->getValue();
    if (V == "ieeelongdouble") {
      IEEELongDouble = true;
      A->claim();
    } else if (V == "ibmlongdouble") {
      IEEELongDouble = false;
      A->claim();
    } else if (V == "vec-default") {
      VecExtabi = false;
      A->claim();
    } else if (V == "vec-extabi") {
      VecExtabi = true;
      A->claim();
    } else if (V == "elfv1") {
      ABIName = "elfv1";
      A->claim();
    } else if (V == "elfv2") {
      ABIName = "elfv2";
      A->claim();
    } else if (V != "altivec")
      // The ppc64 linux abis are all "altivec" abis by default. Accept and ignore
      // the option if given as we don't have backend support for any targets
      // that don't use the altivec abi.
      ABIName = A->getValue();
  }
  if (IEEELongDouble)
    CmdArgs.push_back("-mabi=ieeelongdouble");
  if (VecExtabi) {
    if (!T.isOSAIX())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << "-mabi=vec-extabi" << T.str();
    CmdArgs.push_back("-mabi=vec-extabi");
  }

  ppc::FloatABI FloatABI = ppc::getPPCFloatABI(D, Args);
  if (FloatABI == ppc::FloatABI::Soft) {
    // Floating point operations and argument passing are soft.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else {
    // Floating point operations and argument passing are hard.
    assert(FloatABI == ppc::FloatABI::Hard && "Invalid float abi!");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("hard");
  }

  if (ABIName) {
    CmdArgs.push_back("-target-abi");
    CmdArgs.push_back(ABIName);
  }
}

static void SetRISCVSmallDataLimit(const ToolChain &TC, const ArgList &Args,
                                   ArgStringList &CmdArgs) {
  const Driver &D = TC.getDriver();
  const llvm::Triple &Triple = TC.getTriple();
  // Default small data limitation is eight.
  const char *SmallDataLimit = "8";
  // Get small data limitation.
  if (Args.getLastArg(options::OPT_shared, options::OPT_fpic,
                      options::OPT_fPIC)) {
    // Not support linker relaxation for PIC.
    SmallDataLimit = "0";
    if (Args.hasArg(options::OPT_G)) {
      D.Diag(diag::warn_drv_unsupported_sdata);
    }
  } else if (Args.getLastArgValue(options::OPT_mcmodel_EQ)
                 .equals_insensitive("large") &&
             (Triple.getArch() == llvm::Triple::riscv64)) {
    // Not support linker relaxation for RV64 with large code model.
    SmallDataLimit = "0";
    if (Args.hasArg(options::OPT_G)) {
      D.Diag(diag::warn_drv_unsupported_sdata);
    }
  } else if (Triple.isAndroid()) {
    // GP relaxation is not supported on Android.
    SmallDataLimit = "0";
    if (Args.hasArg(options::OPT_G)) {
      D.Diag(diag::warn_drv_unsupported_sdata);
    }
  } else if (Arg *A = Args.getLastArg(options::OPT_G)) {
    SmallDataLimit = A->getValue();
  }
  // Forward the -msmall-data-limit= option.
  CmdArgs.push_back("-msmall-data-limit");
  CmdArgs.push_back(SmallDataLimit);
}

void Clang::AddRISCVTargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  const llvm::Triple &Triple = getToolChain().getTriple();
  StringRef ABIName = riscv::getRISCVABI(Args, Triple);

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName.data());

  SetRISCVSmallDataLimit(getToolChain(), Args, CmdArgs);

  if (!Args.hasFlag(options::OPT_mimplicit_float,
                    options::OPT_mno_implicit_float, true))
    CmdArgs.push_back("-no-implicit-float");

  if (const Arg *A = Args.getLastArg(options::OPT_mtune_EQ)) {
    CmdArgs.push_back("-tune-cpu");
    if (strcmp(A->getValue(), "native") == 0)
      CmdArgs.push_back(Args.MakeArgString(llvm::sys::getHostCPUName()));
    else
      CmdArgs.push_back(A->getValue());
  }

  // Handle -mrvv-vector-bits=<bits>
  if (Arg *A = Args.getLastArg(options::OPT_mrvv_vector_bits_EQ)) {
    StringRef Val = A->getValue();
    const Driver &D = getToolChain().getDriver();

    // Get minimum VLen from march.
    unsigned MinVLen = 0;
    std::string Arch = riscv::getRISCVArch(Args, Triple);
    auto ISAInfo = llvm::RISCVISAInfo::parseArchString(
        Arch, /*EnableExperimentalExtensions*/ true);
    // Ignore parsing error.
    if (!errorToBool(ISAInfo.takeError()))
      MinVLen = (*ISAInfo)->getMinVLen();

    // If the value is "zvl", use MinVLen from march. Otherwise, try to parse
    // as integer as long as we have a MinVLen.
    unsigned Bits = 0;
    if (Val == "zvl" && MinVLen >= llvm::RISCV::RVVBitsPerBlock) {
      Bits = MinVLen;
    } else if (!Val.getAsInteger(10, Bits)) {
      // Only accept power of 2 values beteen RVVBitsPerBlock and 65536 that
      // at least MinVLen.
      if (Bits < MinVLen || Bits < llvm::RISCV::RVVBitsPerBlock ||
          Bits > 65536 || !llvm::isPowerOf2_32(Bits))
        Bits = 0;
    }

    // If we got a valid value try to use it.
    if (Bits != 0) {
      unsigned VScaleMin = Bits / llvm::RISCV::RVVBitsPerBlock;
      CmdArgs.push_back(
          Args.MakeArgString("-mvscale-max=" + llvm::Twine(VScaleMin)));
      CmdArgs.push_back(
          Args.MakeArgString("-mvscale-min=" + llvm::Twine(VScaleMin)));
    } else if (Val != "scalable") {
      // Handle the unsupported values passed to mrvv-vector-bits.
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
    }
  }
}

void Clang::AddSparcTargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  sparc::FloatABI FloatABI =
      sparc::getSparcFloatABI(getToolChain().getDriver(), Args);

  if (FloatABI == sparc::FloatABI::Soft) {
    // Floating point operations and argument passing are soft.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  } else {
    // Floating point operations and argument passing are hard.
    assert(FloatABI == sparc::FloatABI::Hard && "Invalid float abi!");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("hard");
  }

  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_mtune_EQ)) {
    StringRef Name = A->getValue();
    std::string TuneCPU;
    if (Name == "native")
      TuneCPU = std::string(llvm::sys::getHostCPUName());
    else
      TuneCPU = std::string(Name);

    CmdArgs.push_back("-tune-cpu");
    CmdArgs.push_back(Args.MakeArgString(TuneCPU));
  }
}

void Clang::AddSystemZTargetArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  if (const Arg *A = Args.getLastArg(options::OPT_mtune_EQ)) {
    CmdArgs.push_back("-tune-cpu");
    if (strcmp(A->getValue(), "native") == 0)
      CmdArgs.push_back(Args.MakeArgString(llvm::sys::getHostCPUName()));
    else
      CmdArgs.push_back(A->getValue());
  }

  bool HasBackchain =
      Args.hasFlag(options::OPT_mbackchain, options::OPT_mno_backchain, false);
  bool HasPackedStack = Args.hasFlag(options::OPT_mpacked_stack,
                                     options::OPT_mno_packed_stack, false);
  systemz::FloatABI FloatABI =
      systemz::getSystemZFloatABI(getToolChain().getDriver(), Args);
  bool HasSoftFloat = (FloatABI == systemz::FloatABI::Soft);
  if (HasBackchain && HasPackedStack && !HasSoftFloat) {
    const Driver &D = getToolChain().getDriver();
    D.Diag(diag::err_drv_unsupported_opt)
      << "-mpacked-stack -mbackchain -mhard-float";
  }
  if (HasBackchain)
    CmdArgs.push_back("-mbackchain");
  if (HasPackedStack)
    CmdArgs.push_back("-mpacked-stack");
  if (HasSoftFloat) {
    // Floating point operations and argument passing are soft.
    CmdArgs.push_back("-msoft-float");
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
  }
}

void Clang::AddX86TargetArgs(const ArgList &Args,
                             ArgStringList &CmdArgs) const {
  const Driver &D = getToolChain().getDriver();
  addX86AlignBranchArgs(D, Args, CmdArgs, /*IsLTO=*/false);

  if (!Args.hasFlag(options::OPT_mred_zone, options::OPT_mno_red_zone, true) ||
      Args.hasArg(options::OPT_mkernel) ||
      Args.hasArg(options::OPT_fapple_kext))
    CmdArgs.push_back("-disable-red-zone");

  if (!Args.hasFlag(options::OPT_mtls_direct_seg_refs,
                    options::OPT_mno_tls_direct_seg_refs, true))
    CmdArgs.push_back("-mno-tls-direct-seg-refs");

  // Default to avoid implicit floating-point for kernel/kext code, but allow
  // that to be overridden with -mno-soft-float.
  bool NoImplicitFloat = (Args.hasArg(options::OPT_mkernel) ||
                          Args.hasArg(options::OPT_fapple_kext));
  if (Arg *A = Args.getLastArg(
          options::OPT_msoft_float, options::OPT_mno_soft_float,
          options::OPT_mimplicit_float, options::OPT_mno_implicit_float)) {
    const Option &O = A->getOption();
    NoImplicitFloat = (O.matches(options::OPT_mno_implicit_float) ||
                       O.matches(options::OPT_msoft_float));
  }
  if (NoImplicitFloat)
    CmdArgs.push_back("-no-implicit-float");

  if (Arg *A = Args.getLastArg(options::OPT_masm_EQ)) {
    StringRef Value = A->getValue();
    if (Value == "intel" || Value == "att") {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back(Args.MakeArgString("-x86-asm-syntax=" + Value));
      CmdArgs.push_back(Args.MakeArgString("-inline-asm=" + Value));
    } else {
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    }
  } else if (D.IsCLMode()) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-x86-asm-syntax=intel");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mskip_rax_setup,
                               options::OPT_mno_skip_rax_setup))
    if (A->getOption().matches(options::OPT_mskip_rax_setup))
      CmdArgs.push_back(Args.MakeArgString("-mskip-rax-setup"));

  // Set flags to support MCU ABI.
  if (Args.hasFlag(options::OPT_miamcu, options::OPT_mno_iamcu, false)) {
    CmdArgs.push_back("-mfloat-abi");
    CmdArgs.push_back("soft");
    CmdArgs.push_back("-mstack-alignment=4");
  }

  // Handle -mtune.

  // Default to "generic" unless -march is present or targetting the PS4/PS5.
  std::string TuneCPU;
  if (!Args.hasArg(clang::driver::options::OPT_march_EQ) &&
      !getToolChain().getTriple().isPS())
    TuneCPU = "generic";

  // Override based on -mtune.
  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_mtune_EQ)) {
    StringRef Name = A->getValue();

    if (Name == "native") {
      Name = llvm::sys::getHostCPUName();
      if (!Name.empty())
        TuneCPU = std::string(Name);
    } else
      TuneCPU = std::string(Name);
  }

  if (!TuneCPU.empty()) {
    CmdArgs.push_back("-tune-cpu");
    CmdArgs.push_back(Args.MakeArgString(TuneCPU));
  }
}

void Clang::AddHexagonTargetArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  CmdArgs.push_back("-mqdsp6-compat");
  CmdArgs.push_back("-Wreturn-type");

  if (auto G = toolchains::HexagonToolChain::getSmallDataThreshold(Args)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(
        Args.MakeArgString("-hexagon-small-data-threshold=" + Twine(*G)));
  }

  if (!Args.hasArg(options::OPT_fno_short_enums))
    CmdArgs.push_back("-fshort-enums");
  if (Args.getLastArg(options::OPT_mieee_rnd_near)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-enable-hexagon-ieee-rnd-near");
  }
  CmdArgs.push_back("-mllvm");
  CmdArgs.push_back("-machine-sink-split=0");
}

void Clang::AddLanaiTargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    StringRef CPUName = A->getValue();

    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPUName));
  }
  if (Arg *A = Args.getLastArg(options::OPT_mregparm_EQ)) {
    StringRef Value = A->getValue();
    // Only support mregparm=4 to support old usage. Report error for all other
    // cases.
    int Mregparm;
    if (Value.getAsInteger(10, Mregparm)) {
      if (Mregparm != 4) {
        getToolChain().getDriver().Diag(
            diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Value;
      }
    }
  }
}

void Clang::AddWebAssemblyTargetArgs(const ArgList &Args,
                                     ArgStringList &CmdArgs) const {
  // Default to "hidden" visibility.
  if (!Args.hasArg(options::OPT_fvisibility_EQ,
                   options::OPT_fvisibility_ms_compat))
    CmdArgs.push_back("-fvisibility=hidden");
}

void Clang::AddVETargetArgs(const ArgList &Args, ArgStringList &CmdArgs) const {
  // Floating point operations and argument passing are hard.
  CmdArgs.push_back("-mfloat-abi");
  CmdArgs.push_back("hard");
}

void Clang::DumpCompilationDatabase(Compilation &C, StringRef Filename,
                                    StringRef Target, const InputInfo &Output,
                                    const InputInfo &Input, const ArgList &Args) const {
  // If this is a dry run, do not create the compilation database file.
  if (C.getArgs().hasArg(options::OPT__HASH_HASH_HASH))
    return;

  using llvm::yaml::escape;
  const Driver &D = getToolChain().getDriver();

  if (!CompilationDatabase) {
    std::error_code EC;
    auto File = std::make_unique<llvm::raw_fd_ostream>(
        Filename, EC,
        llvm::sys::fs::OF_TextWithCRLF | llvm::sys::fs::OF_Append);
    if (EC) {
      D.Diag(clang::diag::err_drv_compilationdatabase) << Filename
                                                       << EC.message();
      return;
    }
    CompilationDatabase = std::move(File);
  }
  auto &CDB = *CompilationDatabase;
  auto CWD = D.getVFS().getCurrentWorkingDirectory();
  if (!CWD)
    CWD = ".";
  CDB << "{ \"directory\": \"" << escape(*CWD) << "\"";
  CDB << ", \"file\": \"" << escape(Input.getFilename()) << "\"";
  if (Output.isFilename())
    CDB << ", \"output\": \"" << escape(Output.getFilename()) << "\"";
  CDB << ", \"arguments\": [\"" << escape(D.ClangExecutable) << "\"";
  SmallString<128> Buf;
  Buf = "-x";
  Buf += types::getTypeName(Input.getType());
  CDB << ", \"" << escape(Buf) << "\"";
  if (!D.SysRoot.empty() && !Args.hasArg(options::OPT__sysroot_EQ)) {
    Buf = "--sysroot=";
    Buf += D.SysRoot;
    CDB << ", \"" << escape(Buf) << "\"";
  }
  CDB << ", \"" << escape(Input.getFilename()) << "\"";
  if (Output.isFilename())
    CDB << ", \"-o\", \"" << escape(Output.getFilename()) << "\"";
  for (auto &A: Args) {
    auto &O = A->getOption();
    // Skip language selection, which is positional.
    if (O.getID() == options::OPT_x)
      continue;
    // Skip writing dependency output and the compilation database itself.
    if (O.getGroup().isValid() && O.getGroup().getID() == options::OPT_M_Group)
      continue;
    if (O.getID() == options::OPT_gen_cdb_fragment_path)
      continue;
    // Skip inputs.
    if (O.getKind() == Option::InputClass)
      continue;
    // Skip output.
    if (O.getID() == options::OPT_o)
      continue;
    // All other arguments are quoted and appended.
    ArgStringList ASL;
    A->render(Args, ASL);
    for (auto &it: ASL)
      CDB << ", \"" << escape(it) << "\"";
  }
  Buf = "--target=";
  Buf += Target;
  CDB << ", \"" << escape(Buf) << "\"]},\n";
}

void Clang::DumpCompilationDatabaseFragmentToDir(
    StringRef Dir, Compilation &C, StringRef Target, const InputInfo &Output,
    const InputInfo &Input, const llvm::opt::ArgList &Args) const {
  // If this is a dry run, do not create the compilation database file.
  if (C.getArgs().hasArg(options::OPT__HASH_HASH_HASH))
    return;

  if (CompilationDatabase)
    DumpCompilationDatabase(C, "", Target, Output, Input, Args);

  SmallString<256> Path = Dir;
  const auto &Driver = C.getDriver();
  Driver.getVFS().makeAbsolute(Path);
  auto Err = llvm::sys::fs::create_directory(Path, /*IgnoreExisting=*/true);
  if (Err) {
    Driver.Diag(diag::err_drv_compilationdatabase) << Dir << Err.message();
    return;
  }

  llvm::sys::path::append(
      Path,
      Twine(llvm::sys::path::filename(Input.getFilename())) + ".%%%%.json");
  int FD;
  SmallString<256> TempPath;
  Err = llvm::sys::fs::createUniqueFile(Path, FD, TempPath,
                                        llvm::sys::fs::OF_Text);
  if (Err) {
    Driver.Diag(diag::err_drv_compilationdatabase) << Path << Err.message();
    return;
  }
  CompilationDatabase =
      std::make_unique<llvm::raw_fd_ostream>(FD, /*shouldClose=*/true);
  DumpCompilationDatabase(C, "", Target, Output, Input, Args);
}

static bool CheckARMImplicitITArg(StringRef Value) {
  return Value == "always" || Value == "never" || Value == "arm" ||
         Value == "thumb";
}

static void AddARMImplicitITArgs(const ArgList &Args, ArgStringList &CmdArgs,
                                 StringRef Value) {
  CmdArgs.push_back("-mllvm");
  CmdArgs.push_back(Args.MakeArgString("-arm-implicit-it=" + Value));
}

static void CollectArgsForIntegratedAssembler(Compilation &C,
                                              const ArgList &Args,
                                              ArgStringList &CmdArgs,
                                              const Driver &D) {
  // Default to -mno-relax-all.
  //
  // Note: RISC-V requires an indirect jump for offsets larger than 1MiB. This
  // cannot be done by assembler branch relaxation as it needs a free temporary
  // register. Because of this, branch relaxation is handled by a MachineIR pass
  // before the assembler. Forcing assembler branch relaxation for -O0 makes the
  // MachineIR branch relaxation inaccurate and it will miss cases where an
  // indirect branch is necessary.
  Args.addOptInFlag(CmdArgs, options::OPT_mrelax_all,
                    options::OPT_mno_relax_all);

  // Only default to -mincremental-linker-compatible if we think we are
  // targeting the MSVC linker.
  bool DefaultIncrementalLinkerCompatible =
      C.getDefaultToolChain().getTriple().isWindowsMSVCEnvironment();
  if (Args.hasFlag(options::OPT_mincremental_linker_compatible,
                   options::OPT_mno_incremental_linker_compatible,
                   DefaultIncrementalLinkerCompatible))
    CmdArgs.push_back("-mincremental-linker-compatible");

  Args.AddLastArg(CmdArgs, options::OPT_femit_dwarf_unwind_EQ);

  Args.addOptInFlag(CmdArgs, options::OPT_femit_compact_unwind_non_canonical,
                    options::OPT_fno_emit_compact_unwind_non_canonical);

  // If you add more args here, also add them to the block below that
  // starts with "// If CollectArgsForIntegratedAssembler() isn't called below".

  // When passing -I arguments to the assembler we sometimes need to
  // unconditionally take the next argument.  For example, when parsing
  // '-Wa,-I -Wa,foo' we need to accept the -Wa,foo arg after seeing the
  // -Wa,-I arg and when parsing '-Wa,-I,foo' we need to accept the 'foo'
  // arg after parsing the '-I' arg.
  bool TakeNextArg = false;

  const llvm::Triple &Triple = C.getDefaultToolChain().getTriple();
  bool Crel = false, ExperimentalCrel = false;
  bool UseRelaxRelocations = C.getDefaultToolChain().useRelaxRelocations();
  bool UseNoExecStack = false;
  const char *MipsTargetFeature = nullptr;
  llvm::SmallVector<const char *> SparcTargetFeatures;
  StringRef ImplicitIt;
  for (const Arg *A :
       Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler,
                     options::OPT_mimplicit_it_EQ)) {
    A->claim();

    if (A->getOption().getID() == options::OPT_mimplicit_it_EQ) {
      switch (C.getDefaultToolChain().getArch()) {
      case llvm::Triple::arm:
      case llvm::Triple::armeb:
      case llvm::Triple::thumb:
      case llvm::Triple::thumbeb:
        // Only store the value; the last value set takes effect.
        ImplicitIt = A->getValue();
        if (!CheckARMImplicitITArg(ImplicitIt))
          D.Diag(diag::err_drv_unsupported_option_argument)
              << A->getSpelling() << ImplicitIt;
        continue;
      default:
        break;
      }
    }

    for (StringRef Value : A->getValues()) {
      if (TakeNextArg) {
        CmdArgs.push_back(Value.data());
        TakeNextArg = false;
        continue;
      }

      if (C.getDefaultToolChain().getTriple().isOSBinFormatCOFF() &&
          Value == "-mbig-obj")
        continue; // LLVM handles bigobj automatically

      switch (C.getDefaultToolChain().getArch()) {
      default:
        break;
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        if (Value == "-msse2avx") {
          CmdArgs.push_back("-msse2avx");
          continue;
        }
        break;
      case llvm::Triple::wasm32:
      case llvm::Triple::wasm64:
        if (Value == "--no-type-check") {
          CmdArgs.push_back("-mno-type-check");
          continue;
        }
        break;
      case llvm::Triple::thumb:
      case llvm::Triple::thumbeb:
      case llvm::Triple::arm:
      case llvm::Triple::armeb:
        if (Value.starts_with("-mimplicit-it=")) {
          // Only store the value; the last value set takes effect.
          ImplicitIt = Value.split("=").second;
          if (CheckARMImplicitITArg(ImplicitIt))
            continue;
        }
        if (Value == "-mthumb")
          // -mthumb has already been processed in ComputeLLVMTriple()
          // recognize but skip over here.
          continue;
        break;
      case llvm::Triple::mips:
      case llvm::Triple::mipsel:
      case llvm::Triple::mips64:
      case llvm::Triple::mips64el:
        if (Value == "--trap") {
          CmdArgs.push_back("-target-feature");
          CmdArgs.push_back("+use-tcc-in-div");
          continue;
        }
        if (Value == "--break") {
          CmdArgs.push_back("-target-feature");
          CmdArgs.push_back("-use-tcc-in-div");
          continue;
        }
        if (Value.starts_with("-msoft-float")) {
          CmdArgs.push_back("-target-feature");
          CmdArgs.push_back("+soft-float");
          continue;
        }
        if (Value.starts_with("-mhard-float")) {
          CmdArgs.push_back("-target-feature");
          CmdArgs.push_back("-soft-float");
          continue;
        }
        if (Value.starts_with("-mfix-loongson2f-btb")) {
          CmdArgs.push_back("-mllvm");
          CmdArgs.push_back("-fix-loongson2f-btb");
          continue;
        }

        MipsTargetFeature = llvm::StringSwitch<const char *>(Value)
                                .Case("-mips1", "+mips1")
                                .Case("-mips2", "+mips2")
                                .Case("-mips3", "+mips3")
                                .Case("-mips4", "+mips4")
                                .Case("-mips5", "+mips5")
                                .Case("-mips32", "+mips32")
                                .Case("-mips32r2", "+mips32r2")
                                .Case("-mips32r3", "+mips32r3")
                                .Case("-mips32r5", "+mips32r5")
                                .Case("-mips32r6", "+mips32r6")
                                .Case("-mips64", "+mips64")
                                .Case("-mips64r2", "+mips64r2")
                                .Case("-mips64r3", "+mips64r3")
                                .Case("-mips64r5", "+mips64r5")
                                .Case("-mips64r6", "+mips64r6")
                                .Default(nullptr);
        if (MipsTargetFeature)
          continue;
        break;

      case llvm::Triple::sparc:
      case llvm::Triple::sparcel:
      case llvm::Triple::sparcv9:
        if (Value == "--undeclared-regs") {
          // LLVM already allows undeclared use of G registers, so this option
          // becomes a no-op. This solely exists for GNU compatibility.
          // TODO implement --no-undeclared-regs
          continue;
        }
        SparcTargetFeatures =
            llvm::StringSwitch<llvm::SmallVector<const char *>>(Value)
                .Case("-Av8", {"-v8plus"})
                .Case("-Av8plus", {"+v8plus", "+v9"})
                .Case("-Av8plusa", {"+v8plus", "+v9", "+vis"})
                .Case("-Av8plusb", {"+v8plus", "+v9", "+vis", "+vis2"})
                .Case("-Av8plusd", {"+v8plus", "+v9", "+vis", "+vis2", "+vis3"})
                .Case("-Av9", {"+v9"})
                .Case("-Av9a", {"+v9", "+vis"})
                .Case("-Av9b", {"+v9", "+vis", "+vis2"})
                .Case("-Av9d", {"+v9", "+vis", "+vis2", "+vis3"})
                .Default({});
        if (!SparcTargetFeatures.empty())
          continue;
        break;
      }

      if (Value == "-force_cpusubtype_ALL") {
        // Do nothing, this is the default and we don't support anything else.
      } else if (Value == "-L") {
        CmdArgs.push_back("-msave-temp-labels");
      } else if (Value == "--fatal-warnings") {
        CmdArgs.push_back("-massembler-fatal-warnings");
      } else if (Value == "--no-warn" || Value == "-W") {
        CmdArgs.push_back("-massembler-no-warn");
      } else if (Value == "--noexecstack") {
        UseNoExecStack = true;
      } else if (Value.starts_with("-compress-debug-sections") ||
                 Value.starts_with("--compress-debug-sections") ||
                 Value == "-nocompress-debug-sections" ||
                 Value == "--nocompress-debug-sections") {
        CmdArgs.push_back(Value.data());
      } else if (Value == "--crel") {
        Crel = true;
      } else if (Value == "--no-crel") {
        Crel = false;
      } else if (Value == "--allow-experimental-crel") {
        ExperimentalCrel = true;
      } else if (Value == "-mrelax-relocations=yes" ||
                 Value == "--mrelax-relocations=yes") {
        UseRelaxRelocations = true;
      } else if (Value == "-mrelax-relocations=no" ||
                 Value == "--mrelax-relocations=no") {
        UseRelaxRelocations = false;
      } else if (Value.starts_with("-I")) {
        CmdArgs.push_back(Value.data());
        // We need to consume the next argument if the current arg is a plain
        // -I. The next arg will be the include directory.
        if (Value == "-I")
          TakeNextArg = true;
      } else if (Value.starts_with("-gdwarf-")) {
        // "-gdwarf-N" options are not cc1as options.
        unsigned DwarfVersion = DwarfVersionNum(Value);
        if (DwarfVersion == 0) { // Send it onward, and let cc1as complain.
          CmdArgs.push_back(Value.data());
        } else {
          RenderDebugEnablingArgs(Args, CmdArgs,
                                  llvm::codegenoptions::DebugInfoConstructor,
                                  DwarfVersion, llvm::DebuggerKind::Default);
        }
      } else if (Value.starts_with("-mcpu") || Value.starts_with("-mfpu") ||
                 Value.starts_with("-mhwdiv") || Value.starts_with("-march")) {
        // Do nothing, we'll validate it later.
      } else if (Value == "-defsym" || Value == "--defsym") {
        if (A->getNumValues() != 2) {
          D.Diag(diag::err_drv_defsym_invalid_format) << Value;
          break;
        }
        const char *S = A->getValue(1);
        auto Pair = StringRef(S).split('=');
        auto Sym = Pair.first;
        auto SVal = Pair.second;

        if (Sym.empty() || SVal.empty()) {
          D.Diag(diag::err_drv_defsym_invalid_format) << S;
          break;
        }
        int64_t IVal;
        if (SVal.getAsInteger(0, IVal)) {
          D.Diag(diag::err_drv_defsym_invalid_symval) << SVal;
          break;
        }
        CmdArgs.push_back("--defsym");
        TakeNextArg = true;
      } else if (Value == "-fdebug-compilation-dir") {
        CmdArgs.push_back("-fdebug-compilation-dir");
        TakeNextArg = true;
      } else if (Value.consume_front("-fdebug-compilation-dir=")) {
        // The flag is a -Wa / -Xassembler argument and Options doesn't
        // parse the argument, so this isn't automatically aliased to
        // -fdebug-compilation-dir (without '=') here.
        CmdArgs.push_back("-fdebug-compilation-dir");
        CmdArgs.push_back(Value.data());
      } else if (Value == "--version") {
        D.PrintVersion(C, llvm::outs());
      } else {
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Value;
      }
    }
  }
  if (ImplicitIt.size())
    AddARMImplicitITArgs(Args, CmdArgs, ImplicitIt);
  if (Crel) {
    if (!ExperimentalCrel)
      D.Diag(diag::err_drv_experimental_crel);
    if (Triple.isOSBinFormatELF() && !Triple.isMIPS()) {
      CmdArgs.push_back("--crel");
    } else {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << "-Wa,--crel" << D.getTargetTriple();
    }
  }
  if (!UseRelaxRelocations)
    CmdArgs.push_back("-mrelax-relocations=no");
  if (UseNoExecStack)
    CmdArgs.push_back("-mnoexecstack");
  if (MipsTargetFeature != nullptr) {
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back(MipsTargetFeature);
  }

  // Those OSes default to enabling VIS on 64-bit SPARC.
  // See also the corresponding code for external assemblers in
  // sparc::getSparcAsmModeForCPU().
  bool IsSparcV9ATarget =
      (C.getDefaultToolChain().getArch() == llvm::Triple::sparcv9) &&
      (Triple.isOSLinux() || Triple.isOSFreeBSD() || Triple.isOSOpenBSD());
  if (IsSparcV9ATarget && SparcTargetFeatures.empty()) {
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back("+vis");
  }
  for (const char *Feature : SparcTargetFeatures) {
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back(Feature);
  }

  // forward -fembed-bitcode to assmebler
  if (C.getDriver().embedBitcodeEnabled() ||
      C.getDriver().embedBitcodeMarkerOnly())
    Args.AddLastArg(CmdArgs, options::OPT_fembed_bitcode_EQ);

  if (const char *AsSecureLogFile = getenv("AS_SECURE_LOG_FILE")) {
    CmdArgs.push_back("-as-secure-log-file");
    CmdArgs.push_back(Args.MakeArgString(AsSecureLogFile));
  }
}

static std::string ComplexRangeKindToStr(LangOptions::ComplexRangeKind Range) {
  switch (Range) {
  case LangOptions::ComplexRangeKind::CX_Full:
    return "full";
    break;
  case LangOptions::ComplexRangeKind::CX_Basic:
    return "basic";
    break;
  case LangOptions::ComplexRangeKind::CX_Improved:
    return "improved";
    break;
  case LangOptions::ComplexRangeKind::CX_Promoted:
    return "promoted";
    break;
  default:
    return "";
  }
}

static std::string ComplexArithmeticStr(LangOptions::ComplexRangeKind Range) {
  return (Range == LangOptions::ComplexRangeKind::CX_None)
             ? ""
             : "-fcomplex-arithmetic=" + ComplexRangeKindToStr(Range);
}

static void EmitComplexRangeDiag(const Driver &D, std::string str1,
                                 std::string str2) {
  if ((str1.compare(str2) != 0) && !str2.empty() && !str1.empty()) {
    D.Diag(clang::diag::warn_drv_overriding_option) << str1 << str2;
  }
}

static std::string
RenderComplexRangeOption(LangOptions::ComplexRangeKind Range) {
  std::string ComplexRangeStr = ComplexRangeKindToStr(Range);
  if (!ComplexRangeStr.empty())
    return "-complex-range=" + ComplexRangeStr;
  return ComplexRangeStr;
}

static void RenderFloatingPointOptions(const ToolChain &TC, const Driver &D,
                                       bool OFastEnabled, const ArgList &Args,
                                       ArgStringList &CmdArgs,
                                       const JobAction &JA) {
  // Handle various floating point optimization flags, mapping them to the
  // appropriate LLVM code generation flags. This is complicated by several
  // "umbrella" flags, so we do this by stepping through the flags incrementally
  // adjusting what we think is enabled/disabled, then at the end setting the
  // LLVM flags based on the final state.
  bool HonorINFs = true;
  bool HonorNaNs = true;
  bool ApproxFunc = false;
  // -fmath-errno is the default on some platforms, e.g. BSD-derived OSes.
  bool MathErrno = TC.IsMathErrnoDefault();
  bool AssociativeMath = false;
  bool ReciprocalMath = false;
  bool SignedZeros = true;
  bool TrappingMath = false; // Implemented via -ffp-exception-behavior
  bool TrappingMathPresent = false; // Is trapping-math in args, and not
                                    // overriden by ffp-exception-behavior?
  bool RoundingFPMath = false;
  // -ffp-model values: strict, fast, precise
  StringRef FPModel = "";
  // -ffp-exception-behavior options: strict, maytrap, ignore
  StringRef FPExceptionBehavior = "";
  // -ffp-eval-method options: double, extended, source
  StringRef FPEvalMethod = "";
  llvm::DenormalMode DenormalFPMath =
      TC.getDefaultDenormalModeForType(Args, JA);
  llvm::DenormalMode DenormalFP32Math =
      TC.getDefaultDenormalModeForType(Args, JA, &llvm::APFloat::IEEEsingle());

  // CUDA and HIP don't rely on the frontend to pass an ffp-contract option.
  // If one wasn't given by the user, don't pass it here.
  StringRef FPContract;
  StringRef LastSeenFfpContractOption;
  bool SeenUnsafeMathModeOption = false;
  if (!JA.isDeviceOffloading(Action::OFK_Cuda) &&
      !JA.isOffloading(Action::OFK_HIP))
    FPContract = "on";
  bool StrictFPModel = false;
  StringRef Float16ExcessPrecision = "";
  StringRef BFloat16ExcessPrecision = "";
  LangOptions::ComplexRangeKind Range = LangOptions::ComplexRangeKind::CX_None;
  std::string ComplexRangeStr = "";
  std::string GccRangeComplexOption = "";

  // Lambda to set fast-math options. This is also used by -ffp-model=fast
  auto applyFastMath = [&]() {
    HonorINFs = false;
    HonorNaNs = false;
    MathErrno = false;
    AssociativeMath = true;
    ReciprocalMath = true;
    ApproxFunc = true;
    SignedZeros = false;
    TrappingMath = false;
    RoundingFPMath = false;
    FPExceptionBehavior = "";
    // If fast-math is set then set the fp-contract mode to fast.
    FPContract = "fast";
    // ffast-math enables basic range rules for complex multiplication and
    // division.
    // Warn if user expects to perform full implementation of complex
    // multiplication or division in the presence of nan or ninf flags.
    if (Range == LangOptions::ComplexRangeKind::CX_Full ||
        Range == LangOptions::ComplexRangeKind::CX_Improved ||
        Range == LangOptions::ComplexRangeKind::CX_Promoted)
      EmitComplexRangeDiag(
          D, ComplexArithmeticStr(Range),
          !GccRangeComplexOption.empty()
              ? GccRangeComplexOption
              : ComplexArithmeticStr(LangOptions::ComplexRangeKind::CX_Basic));
    Range = LangOptions::ComplexRangeKind::CX_Basic;
    SeenUnsafeMathModeOption = true;
  };

  if (const Arg *A = Args.getLastArg(options::OPT_flimited_precision_EQ)) {
    CmdArgs.push_back("-mlimit-float-precision");
    CmdArgs.push_back(A->getValue());
  }

  for (const Arg *A : Args) {
    switch (A->getOption().getID()) {
    // If this isn't an FP option skip the claim below
    default: continue;

    case options::OPT_fcx_limited_range:
      if (GccRangeComplexOption.empty()) {
        if (Range != LangOptions::ComplexRangeKind::CX_Basic)
          EmitComplexRangeDiag(D, RenderComplexRangeOption(Range),
                               "-fcx-limited-range");
      } else {
        if (GccRangeComplexOption != "-fno-cx-limited-range")
          EmitComplexRangeDiag(D, GccRangeComplexOption, "-fcx-limited-range");
      }
      GccRangeComplexOption = "-fcx-limited-range";
      Range = LangOptions::ComplexRangeKind::CX_Basic;
      break;
    case options::OPT_fno_cx_limited_range:
      if (GccRangeComplexOption.empty()) {
        EmitComplexRangeDiag(D, RenderComplexRangeOption(Range),
                             "-fno-cx-limited-range");
      } else {
        if (GccRangeComplexOption.compare("-fcx-limited-range") != 0 &&
            GccRangeComplexOption.compare("-fno-cx-fortran-rules") != 0)
          EmitComplexRangeDiag(D, GccRangeComplexOption,
                               "-fno-cx-limited-range");
      }
      GccRangeComplexOption = "-fno-cx-limited-range";
      Range = LangOptions::ComplexRangeKind::CX_Full;
      break;
    case options::OPT_fcx_fortran_rules:
      if (GccRangeComplexOption.empty())
        EmitComplexRangeDiag(D, RenderComplexRangeOption(Range),
                             "-fcx-fortran-rules");
      else
        EmitComplexRangeDiag(D, GccRangeComplexOption, "-fcx-fortran-rules");
      GccRangeComplexOption = "-fcx-fortran-rules";
      Range = LangOptions::ComplexRangeKind::CX_Improved;
      break;
    case options::OPT_fno_cx_fortran_rules:
      if (GccRangeComplexOption.empty()) {
        EmitComplexRangeDiag(D, RenderComplexRangeOption(Range),
                             "-fno-cx-fortran-rules");
      } else {
        if (GccRangeComplexOption != "-fno-cx-limited-range")
          EmitComplexRangeDiag(D, GccRangeComplexOption,
                               "-fno-cx-fortran-rules");
      }
      GccRangeComplexOption = "-fno-cx-fortran-rules";
      Range = LangOptions::ComplexRangeKind::CX_Full;
      break;
    case options::OPT_fcomplex_arithmetic_EQ: {
      LangOptions::ComplexRangeKind RangeVal;
      StringRef Val = A->getValue();
      if (Val == "full")
        RangeVal = LangOptions::ComplexRangeKind::CX_Full;
      else if (Val == "improved")
        RangeVal = LangOptions::ComplexRangeKind::CX_Improved;
      else if (Val == "promoted")
        RangeVal = LangOptions::ComplexRangeKind::CX_Promoted;
      else if (Val == "basic")
        RangeVal = LangOptions::ComplexRangeKind::CX_Basic;
      else {
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
        break;
      }
      if (!GccRangeComplexOption.empty()) {
        if (GccRangeComplexOption.compare("-fcx-limited-range") != 0) {
          if (GccRangeComplexOption.compare("-fcx-fortran-rules") != 0) {
            if (RangeVal != LangOptions::ComplexRangeKind::CX_Improved)
              EmitComplexRangeDiag(D, GccRangeComplexOption,
                                   ComplexArithmeticStr(RangeVal));
          } else {
            EmitComplexRangeDiag(D, GccRangeComplexOption,
                                 ComplexArithmeticStr(RangeVal));
          }
        } else {
          if (RangeVal != LangOptions::ComplexRangeKind::CX_Basic)
            EmitComplexRangeDiag(D, GccRangeComplexOption,
                                 ComplexArithmeticStr(RangeVal));
        }
      }
      Range = RangeVal;
      break;
    }
    case options::OPT_ffp_model_EQ: {
      // If -ffp-model= is seen, reset to fno-fast-math
      HonorINFs = true;
      HonorNaNs = true;
      ApproxFunc = false;
      // Turning *off* -ffast-math restores the toolchain default.
      MathErrno = TC.IsMathErrnoDefault();
      AssociativeMath = false;
      ReciprocalMath = false;
      SignedZeros = true;
      FPContract = "on";

      StringRef Val = A->getValue();
      if (OFastEnabled && Val != "fast") {
        // Only -ffp-model=fast is compatible with OFast, ignore.
        D.Diag(clang::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-model=" + Val) << "-Ofast";
        break;
      }
      StrictFPModel = false;
      if (!FPModel.empty() && FPModel != Val)
        D.Diag(clang::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-model=" + FPModel)
            << Args.MakeArgString("-ffp-model=" + Val);
      if (Val == "fast") {
        FPModel = Val;
        applyFastMath();
      } else if (Val == "precise") {
        FPModel = Val;
        FPContract = "on";
      } else if (Val == "strict") {
        StrictFPModel = true;
        FPExceptionBehavior = "strict";
        FPModel = Val;
        FPContract = "off";
        TrappingMath = true;
        RoundingFPMath = true;
      } else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }

    // Options controlling individual features
    case options::OPT_fhonor_infinities:    HonorINFs = true;         break;
    case options::OPT_fno_honor_infinities: HonorINFs = false;        break;
    case options::OPT_fhonor_nans:          HonorNaNs = true;         break;
    case options::OPT_fno_honor_nans:       HonorNaNs = false;        break;
    case options::OPT_fapprox_func:         ApproxFunc = true;        break;
    case options::OPT_fno_approx_func:      ApproxFunc = false;       break;
    case options::OPT_fmath_errno:          MathErrno = true;         break;
    case options::OPT_fno_math_errno:       MathErrno = false;        break;
    case options::OPT_fassociative_math:    AssociativeMath = true;   break;
    case options::OPT_fno_associative_math: AssociativeMath = false;  break;
    case options::OPT_freciprocal_math:     ReciprocalMath = true;    break;
    case options::OPT_fno_reciprocal_math:  ReciprocalMath = false;   break;
    case options::OPT_fsigned_zeros:        SignedZeros = true;       break;
    case options::OPT_fno_signed_zeros:     SignedZeros = false;      break;
    case options::OPT_ftrapping_math:
      if (!TrappingMathPresent && !FPExceptionBehavior.empty() &&
          FPExceptionBehavior != "strict")
        // Warn that previous value of option is overridden.
        D.Diag(clang::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-exception-behavior=" +
                                  FPExceptionBehavior)
            << "-ftrapping-math";
      TrappingMath = true;
      TrappingMathPresent = true;
      FPExceptionBehavior = "strict";
      break;
    case options::OPT_fno_trapping_math:
      if (!TrappingMathPresent && !FPExceptionBehavior.empty() &&
          FPExceptionBehavior != "ignore")
        // Warn that previous value of option is overridden.
        D.Diag(clang::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-exception-behavior=" +
                                  FPExceptionBehavior)
            << "-fno-trapping-math";
      TrappingMath = false;
      TrappingMathPresent = true;
      FPExceptionBehavior = "ignore";
      break;

    case options::OPT_frounding_math:
      RoundingFPMath = true;
      break;

    case options::OPT_fno_rounding_math:
      RoundingFPMath = false;
      break;

    case options::OPT_fdenormal_fp_math_EQ:
      DenormalFPMath = llvm::parseDenormalFPAttribute(A->getValue());
      DenormalFP32Math = DenormalFPMath;
      if (!DenormalFPMath.isValid()) {
        D.Diag(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
      }
      break;

    case options::OPT_fdenormal_fp_math_f32_EQ:
      DenormalFP32Math = llvm::parseDenormalFPAttribute(A->getValue());
      if (!DenormalFP32Math.isValid()) {
        D.Diag(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
      }
      break;

    // Validate and pass through -ffp-contract option.
    case options::OPT_ffp_contract: {
      StringRef Val = A->getValue();
      if (Val == "fast" || Val == "on" || Val == "off" ||
          Val == "fast-honor-pragmas") {
        FPContract = Val;
        LastSeenFfpContractOption = Val;
      } else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }

    // Validate and pass through -ffp-exception-behavior option.
    case options::OPT_ffp_exception_behavior_EQ: {
      StringRef Val = A->getValue();
      if (!TrappingMathPresent && !FPExceptionBehavior.empty() &&
          FPExceptionBehavior != Val)
        // Warn that previous value of option is overridden.
        D.Diag(clang::diag::warn_drv_overriding_option)
            << Args.MakeArgString("-ffp-exception-behavior=" +
                                  FPExceptionBehavior)
            << Args.MakeArgString("-ffp-exception-behavior=" + Val);
      TrappingMath = TrappingMathPresent = false;
      if (Val == "ignore" || Val == "maytrap")
        FPExceptionBehavior = Val;
      else if (Val == "strict") {
        FPExceptionBehavior = Val;
        TrappingMath = TrappingMathPresent = true;
      } else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }

    // Validate and pass through -ffp-eval-method option.
    case options::OPT_ffp_eval_method_EQ: {
      StringRef Val = A->getValue();
      if (Val == "double" || Val == "extended" || Val == "source")
        FPEvalMethod = Val;
      else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }

    case options::OPT_fexcess_precision_EQ: {
      StringRef Val = A->getValue();
      const llvm::Triple::ArchType Arch = TC.getArch();
      if (Arch == llvm::Triple::x86 || Arch == llvm::Triple::x86_64) {
        if (Val == "standard" || Val == "fast")
          Float16ExcessPrecision = Val;
        // To make it GCC compatible, allow the value of "16" which
        // means disable excess precision, the same meaning than clang's
        // equivalent value "none".
        else if (Val == "16")
          Float16ExcessPrecision = "none";
        else
          D.Diag(diag::err_drv_unsupported_option_argument)
              << A->getSpelling() << Val;
      } else {
        if (!(Val == "standard" || Val == "fast"))
          D.Diag(diag::err_drv_unsupported_option_argument)
              << A->getSpelling() << Val;
      }
      BFloat16ExcessPrecision = Float16ExcessPrecision;
      break;
    }
    case options::OPT_ffinite_math_only:
      HonorINFs = false;
      HonorNaNs = false;
      break;
    case options::OPT_fno_finite_math_only:
      HonorINFs = true;
      HonorNaNs = true;
      break;

    case options::OPT_funsafe_math_optimizations:
      AssociativeMath = true;
      ReciprocalMath = true;
      SignedZeros = false;
      ApproxFunc = true;
      TrappingMath = false;
      FPExceptionBehavior = "";
      FPContract = "fast";
      SeenUnsafeMathModeOption = true;
      break;
    case options::OPT_fno_unsafe_math_optimizations:
      AssociativeMath = false;
      ReciprocalMath = false;
      SignedZeros = true;
      ApproxFunc = false;

      if (!JA.isDeviceOffloading(Action::OFK_Cuda) &&
          !JA.isOffloading(Action::OFK_HIP)) {
        if (LastSeenFfpContractOption != "") {
          FPContract = LastSeenFfpContractOption;
        } else if (SeenUnsafeMathModeOption)
          FPContract = "on";
      }
      break;

    case options::OPT_Ofast:
      // If -Ofast is the optimization level, then -ffast-math should be enabled
      if (!OFastEnabled)
        continue;
      [[fallthrough]];
    case options::OPT_ffast_math: {
      applyFastMath();
      break;
    }
    case options::OPT_fno_fast_math:
      HonorINFs = true;
      HonorNaNs = true;
      // Turning on -ffast-math (with either flag) removes the need for
      // MathErrno. However, turning *off* -ffast-math merely restores the
      // toolchain default (which may be false).
      MathErrno = TC.IsMathErrnoDefault();
      AssociativeMath = false;
      ReciprocalMath = false;
      ApproxFunc = false;
      SignedZeros = true;
      // -fno_fast_math restores default fpcontract handling
      if (!JA.isDeviceOffloading(Action::OFK_Cuda) &&
          !JA.isOffloading(Action::OFK_HIP)) {
        if (LastSeenFfpContractOption != "") {
          FPContract = LastSeenFfpContractOption;
        } else if (SeenUnsafeMathModeOption)
          FPContract = "on";
      }
      break;
    }
    // The StrictFPModel local variable is needed to report warnings
    // in the way we intend. If -ffp-model=strict has been used, we
    // want to report a warning for the next option encountered that
    // takes us out of the settings described by fp-model=strict, but
    // we don't want to continue issuing warnings for other conflicting
    // options after that.
    if (StrictFPModel) {
      // If -ffp-model=strict has been specified on command line but
      // subsequent options conflict then emit warning diagnostic.
      if (HonorINFs && HonorNaNs && !AssociativeMath && !ReciprocalMath &&
          SignedZeros && TrappingMath && RoundingFPMath && !ApproxFunc &&
          FPContract == "off")
        // OK: Current Arg doesn't conflict with -ffp-model=strict
        ;
      else {
        StrictFPModel = false;
        FPModel = "";
        auto RHS = (A->getNumValues() == 0)
                       ? A->getSpelling()
                       : Args.MakeArgString(A->getSpelling() + A->getValue());
        if (RHS != "-ffp-model=strict")
          D.Diag(clang::diag::warn_drv_overriding_option)
              << "-ffp-model=strict" << RHS;
      }
    }

    // If we handled this option claim it
    A->claim();
  }

  if (!HonorINFs)
    CmdArgs.push_back("-menable-no-infs");

  if (!HonorNaNs)
    CmdArgs.push_back("-menable-no-nans");

  if (ApproxFunc)
    CmdArgs.push_back("-fapprox-func");

  if (MathErrno)
    CmdArgs.push_back("-fmath-errno");

 if (AssociativeMath && ReciprocalMath && !SignedZeros && ApproxFunc &&
     !TrappingMath)
    CmdArgs.push_back("-funsafe-math-optimizations");

  if (!SignedZeros)
    CmdArgs.push_back("-fno-signed-zeros");

  if (AssociativeMath && !SignedZeros && !TrappingMath)
    CmdArgs.push_back("-mreassociate");

  if (ReciprocalMath)
    CmdArgs.push_back("-freciprocal-math");

  if (TrappingMath) {
    // FP Exception Behavior is also set to strict
    assert(FPExceptionBehavior == "strict");
  }

  // The default is IEEE.
  if (DenormalFPMath != llvm::DenormalMode::getIEEE()) {
    llvm::SmallString<64> DenormFlag;
    llvm::raw_svector_ostream ArgStr(DenormFlag);
    ArgStr << "-fdenormal-fp-math=" << DenormalFPMath;
    CmdArgs.push_back(Args.MakeArgString(ArgStr.str()));
  }

  // Add f32 specific denormal mode flag if it's different.
  if (DenormalFP32Math != DenormalFPMath) {
    llvm::SmallString<64> DenormFlag;
    llvm::raw_svector_ostream ArgStr(DenormFlag);
    ArgStr << "-fdenormal-fp-math-f32=" << DenormalFP32Math;
    CmdArgs.push_back(Args.MakeArgString(ArgStr.str()));
  }

  if (!FPContract.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffp-contract=" + FPContract));

  if (RoundingFPMath)
    CmdArgs.push_back(Args.MakeArgString("-frounding-math"));
  else
    CmdArgs.push_back(Args.MakeArgString("-fno-rounding-math"));

  if (!FPExceptionBehavior.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffp-exception-behavior=" +
                      FPExceptionBehavior));

  if (!FPEvalMethod.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffp-eval-method=" + FPEvalMethod));

  if (!Float16ExcessPrecision.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffloat16-excess-precision=" +
                                         Float16ExcessPrecision));
  if (!BFloat16ExcessPrecision.empty())
    CmdArgs.push_back(Args.MakeArgString("-fbfloat16-excess-precision=" +
                                         BFloat16ExcessPrecision));

  ParseMRecip(D, Args, CmdArgs);

  // -ffast-math enables the __FAST_MATH__ preprocessor macro, but check for the
  // individual features enabled by -ffast-math instead of the option itself as
  // that's consistent with gcc's behaviour.
  if (!HonorINFs && !HonorNaNs && !MathErrno && AssociativeMath && ApproxFunc &&
      ReciprocalMath && !SignedZeros && !TrappingMath && !RoundingFPMath) {
    CmdArgs.push_back("-ffast-math");
    if (FPModel == "fast") {
      if (FPContract == "fast")
        // All set, do nothing.
        ;
      else if (FPContract.empty())
        // Enable -ffp-contract=fast
        CmdArgs.push_back(Args.MakeArgString("-ffp-contract=fast"));
      else
        D.Diag(clang::diag::warn_drv_overriding_option)
            << "-ffp-model=fast"
            << Args.MakeArgString("-ffp-contract=" + FPContract);
    }
  }

  // Handle __FINITE_MATH_ONLY__ similarly.
  if (!HonorINFs && !HonorNaNs)
    CmdArgs.push_back("-ffinite-math-only");

  if (const Arg *A = Args.getLastArg(options::OPT_mfpmath_EQ)) {
    CmdArgs.push_back("-mfpmath");
    CmdArgs.push_back(A->getValue());
  }

  // Disable a codegen optimization for floating-point casts.
  if (Args.hasFlag(options::OPT_fno_strict_float_cast_overflow,
                   options::OPT_fstrict_float_cast_overflow, false))
    CmdArgs.push_back("-fno-strict-float-cast-overflow");

  if (Range != LangOptions::ComplexRangeKind::CX_None)
    ComplexRangeStr = RenderComplexRangeOption(Range);
  if (!ComplexRangeStr.empty()) {
    CmdArgs.push_back(Args.MakeArgString(ComplexRangeStr));
    if (Args.hasArg(options::OPT_fcomplex_arithmetic_EQ))
      CmdArgs.push_back(Args.MakeArgString("-fcomplex-arithmetic=" +
                                           ComplexRangeKindToStr(Range)));
  }
  if (Args.hasArg(options::OPT_fcx_limited_range))
    CmdArgs.push_back("-fcx-limited-range");
  if (Args.hasArg(options::OPT_fcx_fortran_rules))
    CmdArgs.push_back("-fcx-fortran-rules");
  if (Args.hasArg(options::OPT_fno_cx_limited_range))
    CmdArgs.push_back("-fno-cx-limited-range");
  if (Args.hasArg(options::OPT_fno_cx_fortran_rules))
    CmdArgs.push_back("-fno-cx-fortran-rules");
}

static void RenderAnalyzerOptions(const ArgList &Args, ArgStringList &CmdArgs,
                                  const llvm::Triple &Triple,
                                  const InputInfo &Input) {
  // Add default argument set.
  if (!Args.hasArg(options::OPT__analyzer_no_default_checks)) {
    CmdArgs.push_back("-analyzer-checker=core");
    CmdArgs.push_back("-analyzer-checker=apiModeling");

    if (!Triple.isWindowsMSVCEnvironment()) {
      CmdArgs.push_back("-analyzer-checker=unix");
    } else {
      // Enable "unix" checkers that also work on Windows.
      CmdArgs.push_back("-analyzer-checker=unix.API");
      CmdArgs.push_back("-analyzer-checker=unix.Malloc");
      CmdArgs.push_back("-analyzer-checker=unix.MallocSizeof");
      CmdArgs.push_back("-analyzer-checker=unix.MismatchedDeallocator");
      CmdArgs.push_back("-analyzer-checker=unix.cstring.BadSizeArg");
      CmdArgs.push_back("-analyzer-checker=unix.cstring.NullArg");
    }

    // Disable some unix checkers for PS4/PS5.
    if (Triple.isPS()) {
      CmdArgs.push_back("-analyzer-disable-checker=unix.API");
      CmdArgs.push_back("-analyzer-disable-checker=unix.Vfork");
    }

    if (Triple.isOSDarwin()) {
      CmdArgs.push_back("-analyzer-checker=osx");
      CmdArgs.push_back(
          "-analyzer-checker=security.insecureAPI.decodeValueOfObjCType");
    }
    else if (Triple.isOSFuchsia())
      CmdArgs.push_back("-analyzer-checker=fuchsia");

    CmdArgs.push_back("-analyzer-checker=deadcode");

    if (types::isCXX(Input.getType()))
      CmdArgs.push_back("-analyzer-checker=cplusplus");

    if (!Triple.isPS()) {
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.UncheckedReturn");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.getpw");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.gets");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.mktemp");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.mkstemp");
      CmdArgs.push_back("-analyzer-checker=security.insecureAPI.vfork");
    }

    // Default nullability checks.
    CmdArgs.push_back("-analyzer-checker=nullability.NullPassedToNonnull");
    CmdArgs.push_back("-analyzer-checker=nullability.NullReturnedFromNonnull");
  }

  // Set the output format. The default is plist, for (lame) historical reasons.
  CmdArgs.push_back("-analyzer-output");
  if (Arg *A = Args.getLastArg(options::OPT__analyzer_output))
    CmdArgs.push_back(A->getValue());
  else
    CmdArgs.push_back("plist");

  // Disable the presentation of standard compiler warnings when using
  // --analyze.  We only want to show static analyzer diagnostics or frontend
  // errors.
  CmdArgs.push_back("-w");

  // Add -Xanalyzer arguments when running as analyzer.
  Args.AddAllArgValues(CmdArgs, options::OPT_Xanalyzer);
}

static bool isValidSymbolName(StringRef S) {
  if (S.empty())
    return false;

  if (std::isdigit(S[0]))
    return false;

  return llvm::all_of(S, [](char C) { return std::isalnum(C) || C == '_'; });
}

static void RenderSSPOptions(const Driver &D, const ToolChain &TC,
                             const ArgList &Args, ArgStringList &CmdArgs,
                             bool KernelOrKext) {
  const llvm::Triple &EffectiveTriple = TC.getEffectiveTriple();

  // NVPTX doesn't support stack protectors; from the compiler's perspective, it
  // doesn't even have a stack!
  if (EffectiveTriple.isNVPTX())
    return;

  // -stack-protector=0 is default.
  LangOptions::StackProtectorMode StackProtectorLevel = LangOptions::SSPOff;
  LangOptions::StackProtectorMode DefaultStackProtectorLevel =
      TC.GetDefaultStackProtectorLevel(KernelOrKext);

  if (Arg *A = Args.getLastArg(options::OPT_fno_stack_protector,
                               options::OPT_fstack_protector_all,
                               options::OPT_fstack_protector_strong,
                               options::OPT_fstack_protector)) {
    if (A->getOption().matches(options::OPT_fstack_protector))
      StackProtectorLevel =
          std::max<>(LangOptions::SSPOn, DefaultStackProtectorLevel);
    else if (A->getOption().matches(options::OPT_fstack_protector_strong))
      StackProtectorLevel = LangOptions::SSPStrong;
    else if (A->getOption().matches(options::OPT_fstack_protector_all))
      StackProtectorLevel = LangOptions::SSPReq;

    if (EffectiveTriple.isBPF() && StackProtectorLevel != LangOptions::SSPOff) {
      D.Diag(diag::warn_drv_unsupported_option_for_target)
          << A->getSpelling() << EffectiveTriple.getTriple();
      StackProtectorLevel = DefaultStackProtectorLevel;
    }
  } else {
    StackProtectorLevel = DefaultStackProtectorLevel;
  }

  if (StackProtectorLevel) {
    CmdArgs.push_back("-stack-protector");
    CmdArgs.push_back(Args.MakeArgString(Twine(StackProtectorLevel)));
  }

  // --param ssp-buffer-size=
  for (const Arg *A : Args.filtered(options::OPT__param)) {
    StringRef Str(A->getValue());
    if (Str.starts_with("ssp-buffer-size=")) {
      if (StackProtectorLevel) {
        CmdArgs.push_back("-stack-protector-buffer-size");
        // FIXME: Verify the argument is a valid integer.
        CmdArgs.push_back(Args.MakeArgString(Str.drop_front(16)));
      }
      A->claim();
    }
  }

  const std::string &TripleStr = EffectiveTriple.getTriple();
  if (Arg *A = Args.getLastArg(options::OPT_mstack_protector_guard_EQ)) {
    StringRef Value = A->getValue();
    if (!EffectiveTriple.isX86() && !EffectiveTriple.isAArch64() &&
        !EffectiveTriple.isARM() && !EffectiveTriple.isThumb())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    if ((EffectiveTriple.isX86() || EffectiveTriple.isARM() ||
         EffectiveTriple.isThumb()) &&
        Value != "tls" && Value != "global") {
      D.Diag(diag::err_drv_invalid_value_with_suggestion)
          << A->getOption().getName() << Value << "tls global";
      return;
    }
    if ((EffectiveTriple.isARM() || EffectiveTriple.isThumb()) &&
        Value == "tls") {
      if (!Args.hasArg(options::OPT_mstack_protector_guard_offset_EQ)) {
        D.Diag(diag::err_drv_ssp_missing_offset_argument)
            << A->getAsString(Args);
        return;
      }
      // Check whether the target subarch supports the hardware TLS register
      if (!arm::isHardTPSupported(EffectiveTriple)) {
        D.Diag(diag::err_target_unsupported_tp_hard)
            << EffectiveTriple.getArchName();
        return;
      }
      // Check whether the user asked for something other than -mtp=cp15
      if (Arg *A = Args.getLastArg(options::OPT_mtp_mode_EQ)) {
        StringRef Value = A->getValue();
        if (Value != "cp15") {
          D.Diag(diag::err_drv_argument_not_allowed_with)
              << A->getAsString(Args) << "-mstack-protector-guard=tls";
          return;
        }
      }
      CmdArgs.push_back("-target-feature");
      CmdArgs.push_back("+read-tp-tpidruro");
    }
    if (EffectiveTriple.isAArch64() && Value != "sysreg" && Value != "global") {
      D.Diag(diag::err_drv_invalid_value_with_suggestion)
          << A->getOption().getName() << Value << "sysreg global";
      return;
    }
    A->render(Args, CmdArgs);
  }

  if (Arg *A = Args.getLastArg(options::OPT_mstack_protector_guard_offset_EQ)) {
    StringRef Value = A->getValue();
    if (!EffectiveTriple.isX86() && !EffectiveTriple.isAArch64() &&
        !EffectiveTriple.isARM() && !EffectiveTriple.isThumb())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    int Offset;
    if (Value.getAsInteger(10, Offset)) {
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Value;
      return;
    }
    if ((EffectiveTriple.isARM() || EffectiveTriple.isThumb()) &&
        (Offset < 0 || Offset > 0xfffff)) {
      D.Diag(diag::err_drv_invalid_int_value)
          << A->getOption().getName() << Value;
      return;
    }
    A->render(Args, CmdArgs);
  }

  if (Arg *A = Args.getLastArg(options::OPT_mstack_protector_guard_reg_EQ)) {
    StringRef Value = A->getValue();
    if (!EffectiveTriple.isX86() && !EffectiveTriple.isAArch64())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    if (EffectiveTriple.isX86() && (Value != "fs" && Value != "gs")) {
      D.Diag(diag::err_drv_invalid_value_with_suggestion)
          << A->getOption().getName() << Value << "fs gs";
      return;
    }
    if (EffectiveTriple.isAArch64() && Value != "sp_el0") {
      D.Diag(diag::err_drv_invalid_value) << A->getOption().getName() << Value;
      return;
    }
    A->render(Args, CmdArgs);
  }

  if (Arg *A = Args.getLastArg(options::OPT_mstack_protector_guard_symbol_EQ)) {
    StringRef Value = A->getValue();
    if (!isValidSymbolName(Value)) {
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << A->getOption().getName() << "legal symbol name";
      return;
    }
    A->render(Args, CmdArgs);
  }
}

static void RenderSCPOptions(const ToolChain &TC, const ArgList &Args,
                             ArgStringList &CmdArgs) {
  const llvm::Triple &EffectiveTriple = TC.getEffectiveTriple();

  if (!EffectiveTriple.isOSFreeBSD() && !EffectiveTriple.isOSLinux())
    return;

  if (!EffectiveTriple.isX86() && !EffectiveTriple.isSystemZ() &&
      !EffectiveTriple.isPPC64() && !EffectiveTriple.isAArch64())
    return;

  Args.addOptInFlag(CmdArgs, options::OPT_fstack_clash_protection,
                    options::OPT_fno_stack_clash_protection);
}

static void RenderTrivialAutoVarInitOptions(const Driver &D,
                                            const ToolChain &TC,
                                            const ArgList &Args,
                                            ArgStringList &CmdArgs) {
  auto DefaultTrivialAutoVarInit = TC.GetDefaultTrivialAutoVarInit();
  StringRef TrivialAutoVarInit = "";

  for (const Arg *A : Args) {
    switch (A->getOption().getID()) {
    default:
      continue;
    case options::OPT_ftrivial_auto_var_init: {
      A->claim();
      StringRef Val = A->getValue();
      if (Val == "uninitialized" || Val == "zero" || Val == "pattern")
        TrivialAutoVarInit = Val;
      else
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Val;
      break;
    }
    }
  }

  if (TrivialAutoVarInit.empty())
    switch (DefaultTrivialAutoVarInit) {
    case LangOptions::TrivialAutoVarInitKind::Uninitialized:
      break;
    case LangOptions::TrivialAutoVarInitKind::Pattern:
      TrivialAutoVarInit = "pattern";
      break;
    case LangOptions::TrivialAutoVarInitKind::Zero:
      TrivialAutoVarInit = "zero";
      break;
    }

  if (!TrivialAutoVarInit.empty()) {
    CmdArgs.push_back(
        Args.MakeArgString("-ftrivial-auto-var-init=" + TrivialAutoVarInit));
  }

  if (Arg *A =
          Args.getLastArg(options::OPT_ftrivial_auto_var_init_stop_after)) {
    if (!Args.hasArg(options::OPT_ftrivial_auto_var_init) ||
        StringRef(
            Args.getLastArg(options::OPT_ftrivial_auto_var_init)->getValue()) ==
            "uninitialized")
      D.Diag(diag::err_drv_trivial_auto_var_init_stop_after_missing_dependency);
    A->claim();
    StringRef Val = A->getValue();
    if (std::stoi(Val.str()) <= 0)
      D.Diag(diag::err_drv_trivial_auto_var_init_stop_after_invalid_value);
    CmdArgs.push_back(
        Args.MakeArgString("-ftrivial-auto-var-init-stop-after=" + Val));
  }

  if (Arg *A = Args.getLastArg(options::OPT_ftrivial_auto_var_init_max_size)) {
    if (!Args.hasArg(options::OPT_ftrivial_auto_var_init) ||
        StringRef(
            Args.getLastArg(options::OPT_ftrivial_auto_var_init)->getValue()) ==
            "uninitialized")
      D.Diag(diag::err_drv_trivial_auto_var_init_max_size_missing_dependency);
    A->claim();
    StringRef Val = A->getValue();
    if (std::stoi(Val.str()) <= 0)
      D.Diag(diag::err_drv_trivial_auto_var_init_max_size_invalid_value);
    CmdArgs.push_back(
        Args.MakeArgString("-ftrivial-auto-var-init-max-size=" + Val));
  }
}

static void RenderOpenCLOptions(const ArgList &Args, ArgStringList &CmdArgs,
                                types::ID InputType) {
  // cl-denorms-are-zero is not forwarded. It is translated into a generic flag
  // for denormal flushing handling based on the target.
  const unsigned ForwardedArguments[] = {
      options::OPT_cl_opt_disable,
      options::OPT_cl_strict_aliasing,
      options::OPT_cl_single_precision_constant,
      options::OPT_cl_finite_math_only,
      options::OPT_cl_kernel_arg_info,
      options::OPT_cl_unsafe_math_optimizations,
      options::OPT_cl_fast_relaxed_math,
      options::OPT_cl_mad_enable,
      options::OPT_cl_no_signed_zeros,
      options::OPT_cl_fp32_correctly_rounded_divide_sqrt,
      options::OPT_cl_uniform_work_group_size
  };

  if (Arg *A = Args.getLastArg(options::OPT_cl_std_EQ)) {
    std::string CLStdStr = std::string("-cl-std=") + A->getValue();
    CmdArgs.push_back(Args.MakeArgString(CLStdStr));
  } else if (Arg *A = Args.getLastArg(options::OPT_cl_ext_EQ)) {
    std::string CLExtStr = std::string("-cl-ext=") + A->getValue();
    CmdArgs.push_back(Args.MakeArgString(CLExtStr));
  }

  for (const auto &Arg : ForwardedArguments)
    if (const auto *A = Args.getLastArg(Arg))
      CmdArgs.push_back(Args.MakeArgString(A->getOption().getPrefixedName()));

  // Only add the default headers if we are compiling OpenCL sources.
  if ((types::isOpenCL(InputType) ||
       (Args.hasArg(options::OPT_cl_std_EQ) && types::isSrcFile(InputType))) &&
      !Args.hasArg(options::OPT_cl_no_stdinc)) {
    CmdArgs.push_back("-finclude-default-header");
    CmdArgs.push_back("-fdeclare-opencl-builtins");
  }
}

static void RenderHLSLOptions(const ArgList &Args, ArgStringList &CmdArgs,
                              types::ID InputType) {
  const unsigned ForwardedArguments[] = {options::OPT_dxil_validator_version,
                                         options::OPT_D,
                                         options::OPT_I,
                                         options::OPT_O,
                                         options::OPT_emit_llvm,
                                         options::OPT_emit_obj,
                                         options::OPT_disable_llvm_passes,
                                         options::OPT_fnative_half_type,
                                         options::OPT_hlsl_entrypoint};
  if (!types::isHLSL(InputType))
    return;
  for (const auto &Arg : ForwardedArguments)
    if (const auto *A = Args.getLastArg(Arg))
      A->renderAsInput(Args, CmdArgs);
  // Add the default headers if dxc_no_stdinc is not set.
  if (!Args.hasArg(options::OPT_dxc_no_stdinc) &&
      !Args.hasArg(options::OPT_nostdinc))
    CmdArgs.push_back("-finclude-default-header");
}

static void RenderOpenACCOptions(const Driver &D, const ArgList &Args,
                                 ArgStringList &CmdArgs, types::ID InputType) {
  if (!Args.hasArg(options::OPT_fopenacc))
    return;

  CmdArgs.push_back("-fopenacc");

  if (Arg *A = Args.getLastArg(options::OPT_openacc_macro_override)) {
    StringRef Value = A->getValue();
    int Version;
    if (!Value.getAsInteger(10, Version))
      A->renderAsInput(Args, CmdArgs);
    else
      D.Diag(diag::err_drv_clang_unsupported) << Value;
  }
}

static void RenderARCMigrateToolOptions(const Driver &D, const ArgList &Args,
                                        ArgStringList &CmdArgs) {
  bool ARCMTEnabled = false;
  if (!Args.hasArg(options::OPT_fno_objc_arc, options::OPT_fobjc_arc)) {
    if (const Arg *A = Args.getLastArg(options::OPT_ccc_arcmt_check,
                                       options::OPT_ccc_arcmt_modify,
                                       options::OPT_ccc_arcmt_migrate)) {
      ARCMTEnabled = true;
      switch (A->getOption().getID()) {
      default: llvm_unreachable("missed a case");
      case options::OPT_ccc_arcmt_check:
        CmdArgs.push_back("-arcmt-action=check");
        break;
      case options::OPT_ccc_arcmt_modify:
        CmdArgs.push_back("-arcmt-action=modify");
        break;
      case options::OPT_ccc_arcmt_migrate:
        CmdArgs.push_back("-arcmt-action=migrate");
        CmdArgs.push_back("-mt-migrate-directory");
        CmdArgs.push_back(A->getValue());

        Args.AddLastArg(CmdArgs, options::OPT_arcmt_migrate_report_output);
        Args.AddLastArg(CmdArgs, options::OPT_arcmt_migrate_emit_arc_errors);
        break;
      }
    }
  } else {
    Args.ClaimAllArgs(options::OPT_ccc_arcmt_check);
    Args.ClaimAllArgs(options::OPT_ccc_arcmt_modify);
    Args.ClaimAllArgs(options::OPT_ccc_arcmt_migrate);
  }

  if (const Arg *A = Args.getLastArg(options::OPT_ccc_objcmt_migrate)) {
    if (ARCMTEnabled)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args) << "-ccc-arcmt-migrate";

    CmdArgs.push_back("-mt-migrate-directory");
    CmdArgs.push_back(A->getValue());

    if (!Args.hasArg(options::OPT_objcmt_migrate_literals,
                     options::OPT_objcmt_migrate_subscripting,
                     options::OPT_objcmt_migrate_property)) {
      // None specified, means enable them all.
      CmdArgs.push_back("-objcmt-migrate-literals");
      CmdArgs.push_back("-objcmt-migrate-subscripting");
      CmdArgs.push_back("-objcmt-migrate-property");
    } else {
      Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_literals);
      Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_subscripting);
      Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_property);
    }
  } else {
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_literals);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_subscripting);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_all);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_readonly_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_readwrite_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_property_dot_syntax);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_annotation);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_instancetype);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_nsmacros);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_protocol_conformance);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_atomic_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_returns_innerpointer_property);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_ns_nonatomic_iosonly);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_migrate_designated_init);
    Args.AddLastArg(CmdArgs, options::OPT_objcmt_allowlist_dir_path);
  }
}

static void RenderBuiltinOptions(const ToolChain &TC, const llvm::Triple &T,
                                 const ArgList &Args, ArgStringList &CmdArgs) {
  // -fbuiltin is default unless -mkernel is used.
  bool UseBuiltins =
      Args.hasFlag(options::OPT_fbuiltin, options::OPT_fno_builtin,
                   !Args.hasArg(options::OPT_mkernel));
  if (!UseBuiltins)
    CmdArgs.push_back("-fno-builtin");

  // -ffreestanding implies -fno-builtin.
  if (Args.hasArg(options::OPT_ffreestanding))
    UseBuiltins = false;

  // Process the -fno-builtin-* options.
  for (const Arg *A : Args.filtered(options::OPT_fno_builtin_)) {
    A->claim();

    // If -fno-builtin is specified, then there's no need to pass the option to
    // the frontend.
    if (UseBuiltins)
      A->render(Args, CmdArgs);
  }

  // le32-specific flags:
  //  -fno-math-builtin: clang should not convert math builtins to intrinsics
  //                     by default.
  if (TC.getArch() == llvm::Triple::le32)
    CmdArgs.push_back("-fno-math-builtin");
}

bool Driver::getDefaultModuleCachePath(SmallVectorImpl<char> &Result) {
  if (const char *Str = std::getenv("CLANG_MODULE_CACHE_PATH")) {
    Twine Path{Str};
    Path.toVector(Result);
    return Path.getSingleStringRef() != "";
  }
  if (llvm::sys::path::cache_directory(Result)) {
    llvm::sys::path::append(Result, "clang");
    llvm::sys::path::append(Result, "ModuleCache");
    return true;
  }
  return false;
}

llvm::SmallString<256>
clang::driver::tools::getCXX20NamedModuleOutputPath(const ArgList &Args,
                                                    const char *BaseInput) {
  if (Arg *ModuleOutputEQ = Args.getLastArg(options::OPT_fmodule_output_EQ))
    return StringRef(ModuleOutputEQ->getValue());

  SmallString<256> OutputPath;
  if (Arg *FinalOutput = Args.getLastArg(options::OPT_o);
      FinalOutput && Args.hasArg(options::OPT_c))
    OutputPath = FinalOutput->getValue();
  else
    OutputPath = BaseInput;

  const char *Extension = types::getTypeTempSuffix(types::TY_ModuleFile);
  llvm::sys::path::replace_extension(OutputPath, Extension);
  return OutputPath;
}

static bool RenderModulesOptions(Compilation &C, const Driver &D,
                                 const ArgList &Args, const InputInfo &Input,
                                 const InputInfo &Output, bool HaveStd20,
                                 ArgStringList &CmdArgs) {
  bool IsCXX = types::isCXX(Input.getType());
  bool HaveStdCXXModules = IsCXX && HaveStd20;
  bool HaveModules = HaveStdCXXModules;

  // -fmodules enables the use of precompiled modules (off by default).
  // Users can pass -fno-cxx-modules to turn off modules support for
  // C++/Objective-C++ programs.
  bool HaveClangModules = false;
  if (Args.hasFlag(options::OPT_fmodules, options::OPT_fno_modules, false)) {
    bool AllowedInCXX = Args.hasFlag(options::OPT_fcxx_modules,
                                     options::OPT_fno_cxx_modules, true);
    if (AllowedInCXX || !IsCXX) {
      CmdArgs.push_back("-fmodules");
      HaveClangModules = true;
    }
  }

  HaveModules |= HaveClangModules;

  // -fmodule-maps enables implicit reading of module map files. By default,
  // this is enabled if we are using Clang's flavor of precompiled modules.
  if (Args.hasFlag(options::OPT_fimplicit_module_maps,
                   options::OPT_fno_implicit_module_maps, HaveClangModules))
    CmdArgs.push_back("-fimplicit-module-maps");

  // -fmodules-decluse checks that modules used are declared so (off by default)
  Args.addOptInFlag(CmdArgs, options::OPT_fmodules_decluse,
                    options::OPT_fno_modules_decluse);

  // -fmodules-strict-decluse is like -fmodule-decluse, but also checks that
  // all #included headers are part of modules.
  if (Args.hasFlag(options::OPT_fmodules_strict_decluse,
                   options::OPT_fno_modules_strict_decluse, false))
    CmdArgs.push_back("-fmodules-strict-decluse");

  // -fno-implicit-modules turns off implicitly compiling modules on demand.
  bool ImplicitModules = false;
  if (!Args.hasFlag(options::OPT_fimplicit_modules,
                    options::OPT_fno_implicit_modules, HaveClangModules)) {
    if (HaveModules)
      CmdArgs.push_back("-fno-implicit-modules");
  } else if (HaveModules) {
    ImplicitModules = true;
    // -fmodule-cache-path specifies where our implicitly-built module files
    // should be written.
    SmallString<128> Path;
    if (Arg *A = Args.getLastArg(options::OPT_fmodules_cache_path))
      Path = A->getValue();

    bool HasPath = true;
    if (C.isForDiagnostics()) {
      // When generating crash reports, we want to emit the modules along with
      // the reproduction sources, so we ignore any provided module path.
      Path = Output.getFilename();
      llvm::sys::path::replace_extension(Path, ".cache");
      llvm::sys::path::append(Path, "modules");
    } else if (Path.empty()) {
      // No module path was provided: use the default.
      HasPath = Driver::getDefaultModuleCachePath(Path);
    }

    // `HasPath` will only be false if getDefaultModuleCachePath() fails.
    // That being said, that failure is unlikely and not caching is harmless.
    if (HasPath) {
      const char Arg[] = "-fmodules-cache-path=";
      Path.insert(Path.begin(), Arg, Arg + strlen(Arg));
      CmdArgs.push_back(Args.MakeArgString(Path));
    }
  }

  if (HaveModules) {
    if (Args.hasFlag(options::OPT_fprebuilt_implicit_modules,
                     options::OPT_fno_prebuilt_implicit_modules, false))
      CmdArgs.push_back("-fprebuilt-implicit-modules");
    if (Args.hasFlag(options::OPT_fmodules_validate_input_files_content,
                     options::OPT_fno_modules_validate_input_files_content,
                     false))
      CmdArgs.push_back("-fvalidate-ast-input-files-content");
  }

  // -fmodule-name specifies the module that is currently being built (or
  // used for header checking by -fmodule-maps).
  Args.AddLastArg(CmdArgs, options::OPT_fmodule_name_EQ);

  // -fmodule-map-file can be used to specify files containing module
  // definitions.
  Args.AddAllArgs(CmdArgs, options::OPT_fmodule_map_file);

  // -fbuiltin-module-map can be used to load the clang
  // builtin headers modulemap file.
  if (Args.hasArg(options::OPT_fbuiltin_module_map)) {
    SmallString<128> BuiltinModuleMap(D.ResourceDir);
    llvm::sys::path::append(BuiltinModuleMap, "include");
    llvm::sys::path::append(BuiltinModuleMap, "module.modulemap");
    if (llvm::sys::fs::exists(BuiltinModuleMap))
      CmdArgs.push_back(
          Args.MakeArgString("-fmodule-map-file=" + BuiltinModuleMap));
  }

  // The -fmodule-file=<name>=<file> form specifies the mapping of module
  // names to precompiled module files (the module is loaded only if used).
  // The -fmodule-file=<file> form can be used to unconditionally load
  // precompiled module files (whether used or not).
  if (HaveModules || Input.getType() == clang::driver::types::TY_ModuleFile) {
    Args.AddAllArgs(CmdArgs, options::OPT_fmodule_file);

    // -fprebuilt-module-path specifies where to load the prebuilt module files.
    for (const Arg *A : Args.filtered(options::OPT_fprebuilt_module_path)) {
      CmdArgs.push_back(Args.MakeArgString(
          std::string("-fprebuilt-module-path=") + A->getValue()));
      A->claim();
    }
  } else
    Args.ClaimAllArgs(options::OPT_fmodule_file);

  // When building modules and generating crashdumps, we need to dump a module
  // dependency VFS alongside the output.
  if (HaveClangModules && C.isForDiagnostics()) {
    SmallString<128> VFSDir(Output.getFilename());
    llvm::sys::path::replace_extension(VFSDir, ".cache");
    // Add the cache directory as a temp so the crash diagnostics pick it up.
    C.addTempFile(Args.MakeArgString(VFSDir));

    llvm::sys::path::append(VFSDir, "vfs");
    CmdArgs.push_back("-module-dependency-dir");
    CmdArgs.push_back(Args.MakeArgString(VFSDir));
  }

  if (HaveClangModules)
    Args.AddLastArg(CmdArgs, options::OPT_fmodules_user_build_path);

  // Pass through all -fmodules-ignore-macro arguments.
  Args.AddAllArgs(CmdArgs, options::OPT_fmodules_ignore_macro);
  Args.AddLastArg(CmdArgs, options::OPT_fmodules_prune_interval);
  Args.AddLastArg(CmdArgs, options::OPT_fmodules_prune_after);

  if (HaveClangModules) {
    Args.AddLastArg(CmdArgs, options::OPT_fbuild_session_timestamp);

    if (Arg *A = Args.getLastArg(options::OPT_fbuild_session_file)) {
      if (Args.hasArg(options::OPT_fbuild_session_timestamp))
        D.Diag(diag::err_drv_argument_not_allowed_with)
            << A->getAsString(Args) << "-fbuild-session-timestamp";

      llvm::sys::fs::file_status Status;
      if (llvm::sys::fs::status(A->getValue(), Status))
        D.Diag(diag::err_drv_no_such_file) << A->getValue();
      CmdArgs.push_back(Args.MakeArgString(
          "-fbuild-session-timestamp=" +
          Twine((uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
                    Status.getLastModificationTime().time_since_epoch())
                    .count())));
    }

    if (Args.getLastArg(
            options::OPT_fmodules_validate_once_per_build_session)) {
      if (!Args.getLastArg(options::OPT_fbuild_session_timestamp,
                           options::OPT_fbuild_session_file))
        D.Diag(diag::err_drv_modules_validate_once_requires_timestamp);

      Args.AddLastArg(CmdArgs,
                      options::OPT_fmodules_validate_once_per_build_session);
    }

    if (Args.hasFlag(options::OPT_fmodules_validate_system_headers,
                     options::OPT_fno_modules_validate_system_headers,
                     ImplicitModules))
      CmdArgs.push_back("-fmodules-validate-system-headers");

    Args.AddLastArg(CmdArgs,
                    options::OPT_fmodules_disable_diagnostic_validation);
  } else {
    Args.ClaimAllArgs(options::OPT_fbuild_session_timestamp);
    Args.ClaimAllArgs(options::OPT_fbuild_session_file);
    Args.ClaimAllArgs(options::OPT_fmodules_validate_once_per_build_session);
    Args.ClaimAllArgs(options::OPT_fmodules_validate_system_headers);
    Args.ClaimAllArgs(options::OPT_fno_modules_validate_system_headers);
    Args.ClaimAllArgs(options::OPT_fmodules_disable_diagnostic_validation);
  }

  // FIXME: We provisionally don't check ODR violations for decls in the global
  // module fragment.
  CmdArgs.push_back("-fskip-odr-check-in-gmf");

  if (Args.hasArg(options::OPT_modules_reduced_bmi) &&
      (Input.getType() == driver::types::TY_CXXModule ||
       Input.getType() == driver::types::TY_PP_CXXModule)) {
    CmdArgs.push_back("-fexperimental-modules-reduced-bmi");

    if (Args.hasArg(options::OPT_fmodule_output_EQ))
      Args.AddLastArg(CmdArgs, options::OPT_fmodule_output_EQ);
    else
      CmdArgs.push_back(Args.MakeArgString(
          "-fmodule-output=" +
          getCXX20NamedModuleOutputPath(Args, Input.getBaseInput())));
  }

  // Noop if we see '-fexperimental-modules-reduced-bmi' with other translation
  // units than module units. This is more user friendly to allow end uers to
  // enable this feature without asking for help from build systems.
  Args.ClaimAllArgs(options::OPT_modules_reduced_bmi);

  // We need to include the case the input file is a module file here.
  // Since the default compilation model for C++ module interface unit will
  // create temporary module file and compile the temporary module file
  // to get the object file. Then the `-fmodule-output` flag will be
  // brought to the second compilation process. So we have to claim it for
  // the case too.
  if (Input.getType() == driver::types::TY_CXXModule ||
      Input.getType() == driver::types::TY_PP_CXXModule ||
      Input.getType() == driver::types::TY_ModuleFile) {
    Args.ClaimAllArgs(options::OPT_fmodule_output);
    Args.ClaimAllArgs(options::OPT_fmodule_output_EQ);
  }

  return HaveModules;
}

static void RenderCharacterOptions(const ArgList &Args, const llvm::Triple &T,
                                   ArgStringList &CmdArgs) {
  // -fsigned-char is default.
  if (const Arg *A = Args.getLastArg(options::OPT_fsigned_char,
                                     options::OPT_fno_signed_char,
                                     options::OPT_funsigned_char,
                                     options::OPT_fno_unsigned_char)) {
    if (A->getOption().matches(options::OPT_funsigned_char) ||
        A->getOption().matches(options::OPT_fno_signed_char)) {
      CmdArgs.push_back("-fno-signed-char");
    }
  } else if (!isSignedCharDefault(T)) {
    CmdArgs.push_back("-fno-signed-char");
  }

  // The default depends on the language standard.
  Args.AddLastArg(CmdArgs, options::OPT_fchar8__t, options::OPT_fno_char8__t);

  if (const Arg *A = Args.getLastArg(options::OPT_fshort_wchar,
                                     options::OPT_fno_short_wchar)) {
    if (A->getOption().matches(options::OPT_fshort_wchar)) {
      CmdArgs.push_back("-fwchar-type=short");
      CmdArgs.push_back("-fno-signed-wchar");
    } else {
      bool IsARM = T.isARM() || T.isThumb() || T.isAArch64();
      CmdArgs.push_back("-fwchar-type=int");
      if (T.isOSzOS() ||
          (IsARM && !(T.isOSWindows() || T.isOSNetBSD() || T.isOSOpenBSD())))
        CmdArgs.push_back("-fno-signed-wchar");
      else
        CmdArgs.push_back("-fsigned-wchar");
    }
  } else if (T.isOSzOS())
    CmdArgs.push_back("-fno-signed-wchar");
}

static void RenderObjCOptions(const ToolChain &TC, const Driver &D,
                              const llvm::Triple &T, const ArgList &Args,
                              ObjCRuntime &Runtime, bool InferCovariantReturns,
                              const InputInfo &Input, ArgStringList &CmdArgs) {
  const llvm::Triple::ArchType Arch = TC.getArch();

  // -fobjc-dispatch-method is only relevant with the nonfragile-abi, and legacy
  // is the default. Except for deployment target of 10.5, next runtime is
  // always legacy dispatch and -fno-objc-legacy-dispatch gets ignored silently.
  if (Runtime.isNonFragile()) {
    if (!Args.hasFlag(options::OPT_fobjc_legacy_dispatch,
                      options::OPT_fno_objc_legacy_dispatch,
                      Runtime.isLegacyDispatchDefaultForArch(Arch))) {
      if (TC.UseObjCMixedDispatch())
        CmdArgs.push_back("-fobjc-dispatch-method=mixed");
      else
        CmdArgs.push_back("-fobjc-dispatch-method=non-legacy");
    }
  }

  // When ObjectiveC legacy runtime is in effect on MacOSX, turn on the option
  // to do Array/Dictionary subscripting by default.
  if (Arch == llvm::Triple::x86 && T.isMacOSX() &&
      Runtime.getKind() == ObjCRuntime::FragileMacOSX && Runtime.isNeXTFamily())
    CmdArgs.push_back("-fobjc-subscripting-legacy-runtime");

  // Allow -fno-objc-arr to trump -fobjc-arr/-fobjc-arc.
  // NOTE: This logic is duplicated in ToolChains.cpp.
  if (isObjCAutoRefCount(Args)) {
    TC.CheckObjCARC();

    CmdArgs.push_back("-fobjc-arc");

    // FIXME: It seems like this entire block, and several around it should be
    // wrapped in isObjC, but for now we just use it here as this is where it
    // was being used previously.
    if (types::isCXX(Input.getType()) && types::isObjC(Input.getType())) {
      if (TC.GetCXXStdlibType(Args) == ToolChain::CST_Libcxx)
        CmdArgs.push_back("-fobjc-arc-cxxlib=libc++");
      else
        CmdArgs.push_back("-fobjc-arc-cxxlib=libstdc++");
    }

    // Allow the user to enable full exceptions code emission.
    // We default off for Objective-C, on for Objective-C++.
    if (Args.hasFlag(options::OPT_fobjc_arc_exceptions,
                     options::OPT_fno_objc_arc_exceptions,
                     /*Default=*/types::isCXX(Input.getType())))
      CmdArgs.push_back("-fobjc-arc-exceptions");
  }

  // Silence warning for full exception code emission options when explicitly
  // set to use no ARC.
  if (Args.hasArg(options::OPT_fno_objc_arc)) {
    Args.ClaimAllArgs(options::OPT_fobjc_arc_exceptions);
    Args.ClaimAllArgs(options::OPT_fno_objc_arc_exceptions);
  }

  // Allow the user to control whether messages can be converted to runtime
  // functions.
  if (types::isObjC(Input.getType())) {
    auto *Arg = Args.getLastArg(
        options::OPT_fobjc_convert_messages_to_runtime_calls,
        options::OPT_fno_objc_convert_messages_to_runtime_calls);
    if (Arg &&
        Arg->getOption().matches(
            options::OPT_fno_objc_convert_messages_to_runtime_calls))
      CmdArgs.push_back("-fno-objc-convert-messages-to-runtime-calls");
  }

  // -fobjc-infer-related-result-type is the default, except in the Objective-C
  // rewriter.
  if (InferCovariantReturns)
    CmdArgs.push_back("-fno-objc-infer-related-result-type");

  // Pass down -fobjc-weak or -fno-objc-weak if present.
  if (types::isObjC(Input.getType())) {
    auto WeakArg =
        Args.getLastArg(options::OPT_fobjc_weak, options::OPT_fno_objc_weak);
    if (!WeakArg) {
      // nothing to do
    } else if (!Runtime.allowsWeak()) {
      if (WeakArg->getOption().matches(options::OPT_fobjc_weak))
        D.Diag(diag::err_objc_weak_unsupported);
    } else {
      WeakArg->render(Args, CmdArgs);
    }
  }

  if (Args.hasArg(options::OPT_fobjc_disable_direct_methods_for_testing))
    CmdArgs.push_back("-fobjc-disable-direct-methods-for-testing");
}

static void RenderDiagnosticsOptions(const Driver &D, const ArgList &Args,
                                     ArgStringList &CmdArgs) {
  bool CaretDefault = true;
  bool ColumnDefault = true;

  if (const Arg *A = Args.getLastArg(options::OPT__SLASH_diagnostics_classic,
                                     options::OPT__SLASH_diagnostics_column,
                                     options::OPT__SLASH_diagnostics_caret)) {
    switch (A->getOption().getID()) {
    case options::OPT__SLASH_diagnostics_caret:
      CaretDefault = true;
      ColumnDefault = true;
      break;
    case options::OPT__SLASH_diagnostics_column:
      CaretDefault = false;
      ColumnDefault = true;
      break;
    case options::OPT__SLASH_diagnostics_classic:
      CaretDefault = false;
      ColumnDefault = false;
      break;
    }
  }

  // -fcaret-diagnostics is default.
  if (!Args.hasFlag(options::OPT_fcaret_diagnostics,
                    options::OPT_fno_caret_diagnostics, CaretDefault))
    CmdArgs.push_back("-fno-caret-diagnostics");

  Args.addOptOutFlag(CmdArgs, options::OPT_fdiagnostics_fixit_info,
                     options::OPT_fno_diagnostics_fixit_info);
  Args.addOptOutFlag(CmdArgs, options::OPT_fdiagnostics_show_option,
                     options::OPT_fno_diagnostics_show_option);

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_show_category_EQ)) {
    CmdArgs.push_back("-fdiagnostics-show-category");
    CmdArgs.push_back(A->getValue());
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fdiagnostics_show_hotness,
                    options::OPT_fno_diagnostics_show_hotness);

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ)) {
    std::string Opt =
        std::string("-fdiagnostics-hotness-threshold=") + A->getValue();
    CmdArgs.push_back(Args.MakeArgString(Opt));
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_fdiagnostics_misexpect_tolerance_EQ)) {
    std::string Opt =
        std::string("-fdiagnostics-misexpect-tolerance=") + A->getValue();
    CmdArgs.push_back(Args.MakeArgString(Opt));
  }

  if (const Arg *A = Args.getLastArg(options::OPT_fdiagnostics_format_EQ)) {
    CmdArgs.push_back("-fdiagnostics-format");
    CmdArgs.push_back(A->getValue());
    if (StringRef(A->getValue()) == "sarif" ||
        StringRef(A->getValue()) == "SARIF")
      D.Diag(diag::warn_drv_sarif_format_unstable);
  }

  if (const Arg *A = Args.getLastArg(
          options::OPT_fdiagnostics_show_note_include_stack,
          options::OPT_fno_diagnostics_show_note_include_stack)) {
    const Option &O = A->getOption();
    if (O.matches(options::OPT_fdiagnostics_show_note_include_stack))
      CmdArgs.push_back("-fdiagnostics-show-note-include-stack");
    else
      CmdArgs.push_back("-fno-diagnostics-show-note-include-stack");
  }

  // Color diagnostics are parsed by the driver directly from argv and later
  // re-parsed to construct this job; claim any possible color diagnostic here
  // to avoid warn_drv_unused_argument and diagnose bad
  // OPT_fdiagnostics_color_EQ values.
  Args.getLastArg(options::OPT_fcolor_diagnostics,
                  options::OPT_fno_color_diagnostics);
  if (const Arg *A = Args.getLastArg(options::OPT_fdiagnostics_color_EQ)) {
    StringRef Value(A->getValue());
    if (Value != "always" && Value != "never" && Value != "auto")
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Value << A->getOption().getName();
  }

  if (D.getDiags().getDiagnosticOptions().ShowColors)
    CmdArgs.push_back("-fcolor-diagnostics");

  if (Args.hasArg(options::OPT_fansi_escape_codes))
    CmdArgs.push_back("-fansi-escape-codes");

  Args.addOptOutFlag(CmdArgs, options::OPT_fshow_source_location,
                     options::OPT_fno_show_source_location);

  Args.addOptOutFlag(CmdArgs, options::OPT_fdiagnostics_show_line_numbers,
                     options::OPT_fno_diagnostics_show_line_numbers);

  if (Args.hasArg(options::OPT_fdiagnostics_absolute_paths))
    CmdArgs.push_back("-fdiagnostics-absolute-paths");

  if (!Args.hasFlag(options::OPT_fshow_column, options::OPT_fno_show_column,
                    ColumnDefault))
    CmdArgs.push_back("-fno-show-column");

  Args.addOptOutFlag(CmdArgs, options::OPT_fspell_checking,
                     options::OPT_fno_spell_checking);
}

DwarfFissionKind tools::getDebugFissionKind(const Driver &D,
                                            const ArgList &Args, Arg *&Arg) {
  Arg = Args.getLastArg(options::OPT_gsplit_dwarf, options::OPT_gsplit_dwarf_EQ,
                        options::OPT_gno_split_dwarf);
  if (!Arg || Arg->getOption().matches(options::OPT_gno_split_dwarf))
    return DwarfFissionKind::None;

  if (Arg->getOption().matches(options::OPT_gsplit_dwarf))
    return DwarfFissionKind::Split;

  StringRef Value = Arg->getValue();
  if (Value == "split")
    return DwarfFissionKind::Split;
  if (Value == "single")
    return DwarfFissionKind::Single;

  D.Diag(diag::err_drv_unsupported_option_argument)
      << Arg->getSpelling() << Arg->getValue();
  return DwarfFissionKind::None;
}

static void renderDwarfFormat(const Driver &D, const llvm::Triple &T,
                              const ArgList &Args, ArgStringList &CmdArgs,
                              unsigned DwarfVersion) {
  auto *DwarfFormatArg =
      Args.getLastArg(options::OPT_gdwarf64, options::OPT_gdwarf32);
  if (!DwarfFormatArg)
    return;

  if (DwarfFormatArg->getOption().matches(options::OPT_gdwarf64)) {
    if (DwarfVersion < 3)
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << DwarfFormatArg->getAsString(Args) << "DWARFv3 or greater";
    else if (!T.isArch64Bit())
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << DwarfFormatArg->getAsString(Args) << "64 bit architecture";
    else if (!T.isOSBinFormatELF())
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << DwarfFormatArg->getAsString(Args) << "ELF platforms";
  }

  DwarfFormatArg->render(Args, CmdArgs);
}

static void
renderDebugOptions(const ToolChain &TC, const Driver &D, const llvm::Triple &T,
                   const ArgList &Args, bool IRInput, ArgStringList &CmdArgs,
                   const InputInfo &Output,
                   llvm::codegenoptions::DebugInfoKind &DebugInfoKind,
                   DwarfFissionKind &DwarfFission) {
  if (Args.hasFlag(options::OPT_fdebug_info_for_profiling,
                   options::OPT_fno_debug_info_for_profiling, false) &&
      checkDebugInfoOption(
          Args.getLastArg(options::OPT_fdebug_info_for_profiling), Args, D, TC))
    CmdArgs.push_back("-fdebug-info-for-profiling");

  // The 'g' groups options involve a somewhat intricate sequence of decisions
  // about what to pass from the driver to the frontend, but by the time they
  // reach cc1 they've been factored into three well-defined orthogonal choices:
  //  * what level of debug info to generate
  //  * what dwarf version to write
  //  * what debugger tuning to use
  // This avoids having to monkey around further in cc1 other than to disable
  // codeview if not running in a Windows environment. Perhaps even that
  // decision should be made in the driver as well though.
  llvm::DebuggerKind DebuggerTuning = TC.getDefaultDebuggerTuning();

  bool SplitDWARFInlining =
      Args.hasFlag(options::OPT_fsplit_dwarf_inlining,
                   options::OPT_fno_split_dwarf_inlining, false);

  // Normally -gsplit-dwarf is only useful with -gN. For IR input, Clang does
  // object file generation and no IR generation, -gN should not be needed. So
  // allow -gsplit-dwarf with either -gN or IR input.
  if (IRInput || Args.hasArg(options::OPT_g_Group)) {
    Arg *SplitDWARFArg;
    DwarfFission = getDebugFissionKind(D, Args, SplitDWARFArg);
    if (DwarfFission != DwarfFissionKind::None &&
        !checkDebugInfoOption(SplitDWARFArg, Args, D, TC)) {
      DwarfFission = DwarfFissionKind::None;
      SplitDWARFInlining = false;
    }
  }
  if (const Arg *A = Args.getLastArg(options::OPT_g_Group)) {
    DebugInfoKind = llvm::codegenoptions::DebugInfoConstructor;

    // If the last option explicitly specified a debug-info level, use it.
    if (checkDebugInfoOption(A, Args, D, TC) &&
        A->getOption().matches(options::OPT_gN_Group)) {
      DebugInfoKind = debugLevelToInfoKind(*A);
      // For -g0 or -gline-tables-only, drop -gsplit-dwarf. This gets a bit more
      // complicated if you've disabled inline info in the skeleton CUs
      // (SplitDWARFInlining) - then there's value in composing split-dwarf and
      // line-tables-only, so let those compose naturally in that case.
      if (DebugInfoKind == llvm::codegenoptions::NoDebugInfo ||
          DebugInfoKind == llvm::codegenoptions::DebugDirectivesOnly ||
          (DebugInfoKind == llvm::codegenoptions::DebugLineTablesOnly &&
           SplitDWARFInlining))
        DwarfFission = DwarfFissionKind::None;
    }
  }

  // If a debugger tuning argument appeared, remember it.
  bool HasDebuggerTuning = false;
  if (const Arg *A =
          Args.getLastArg(options::OPT_gTune_Group, options::OPT_ggdbN_Group)) {
    HasDebuggerTuning = true;
    if (checkDebugInfoOption(A, Args, D, TC)) {
      if (A->getOption().matches(options::OPT_glldb))
        DebuggerTuning = llvm::DebuggerKind::LLDB;
      else if (A->getOption().matches(options::OPT_gsce))
        DebuggerTuning = llvm::DebuggerKind::SCE;
      else if (A->getOption().matches(options::OPT_gdbx))
        DebuggerTuning = llvm::DebuggerKind::DBX;
      else
        DebuggerTuning = llvm::DebuggerKind::GDB;
    }
  }

  // If a -gdwarf argument appeared, remember it.
  bool EmitDwarf = false;
  if (const Arg *A = getDwarfNArg(Args))
    EmitDwarf = checkDebugInfoOption(A, Args, D, TC);

  bool EmitCodeView = false;
  if (const Arg *A = Args.getLastArg(options::OPT_gcodeview))
    EmitCodeView = checkDebugInfoOption(A, Args, D, TC);

  // If the user asked for debug info but did not explicitly specify -gcodeview
  // or -gdwarf, ask the toolchain for the default format.
  if (!EmitCodeView && !EmitDwarf &&
      DebugInfoKind != llvm::codegenoptions::NoDebugInfo) {
    switch (TC.getDefaultDebugFormat()) {
    case llvm::codegenoptions::DIF_CodeView:
      EmitCodeView = true;
      break;
    case llvm::codegenoptions::DIF_DWARF:
      EmitDwarf = true;
      break;
    }
  }

  unsigned RequestedDWARFVersion = 0; // DWARF version requested by the user
  unsigned EffectiveDWARFVersion = 0; // DWARF version TC can generate. It may
                                      // be lower than what the user wanted.
  if (EmitDwarf) {
    RequestedDWARFVersion = getDwarfVersion(TC, Args);
    // Clamp effective DWARF version to the max supported by the toolchain.
    EffectiveDWARFVersion =
        std::min(RequestedDWARFVersion, TC.getMaxDwarfVersion());
  } else {
    Args.ClaimAllArgs(options::OPT_fdebug_default_version);
  }

  // -gline-directives-only supported only for the DWARF debug info.
  if (RequestedDWARFVersion == 0 &&
      DebugInfoKind == llvm::codegenoptions::DebugDirectivesOnly)
    DebugInfoKind = llvm::codegenoptions::NoDebugInfo;

  // strict DWARF is set to false by default. But for DBX, we need it to be set
  // as true by default.
  if (const Arg *A = Args.getLastArg(options::OPT_gstrict_dwarf))
    (void)checkDebugInfoOption(A, Args, D, TC);
  if (Args.hasFlag(options::OPT_gstrict_dwarf, options::OPT_gno_strict_dwarf,
                   DebuggerTuning == llvm::DebuggerKind::DBX))
    CmdArgs.push_back("-gstrict-dwarf");

  // And we handle flag -grecord-gcc-switches later with DWARFDebugFlags.
  Args.ClaimAllArgs(options::OPT_g_flags_Group);

  // Column info is included by default for everything except SCE and
  // CodeView. Clang doesn't track end columns, just starting columns, which,
  // in theory, is fine for CodeView (and PDB).  In practice, however, the
  // Microsoft debuggers don't handle missing end columns well, and the AIX
  // debugger DBX also doesn't handle the columns well, so it's better not to
  // include any column info.
  if (const Arg *A = Args.getLastArg(options::OPT_gcolumn_info))
    (void)checkDebugInfoOption(A, Args, D, TC);
  if (!Args.hasFlag(options::OPT_gcolumn_info, options::OPT_gno_column_info,
                    !EmitCodeView &&
                        (DebuggerTuning != llvm::DebuggerKind::SCE &&
                         DebuggerTuning != llvm::DebuggerKind::DBX)))
    CmdArgs.push_back("-gno-column-info");

  // FIXME: Move backend command line options to the module.
  if (Args.hasFlag(options::OPT_gmodules, options::OPT_gno_modules, false)) {
    // If -gline-tables-only or -gline-directives-only is the last option it
    // wins.
    if (checkDebugInfoOption(Args.getLastArg(options::OPT_gmodules), Args, D,
                             TC)) {
      if (DebugInfoKind != llvm::codegenoptions::DebugLineTablesOnly &&
          DebugInfoKind != llvm::codegenoptions::DebugDirectivesOnly) {
        DebugInfoKind = llvm::codegenoptions::DebugInfoConstructor;
        CmdArgs.push_back("-dwarf-ext-refs");
        CmdArgs.push_back("-fmodule-format=obj");
      }
    }
  }

  if (T.isOSBinFormatELF() && SplitDWARFInlining)
    CmdArgs.push_back("-fsplit-dwarf-inlining");

  // After we've dealt with all combinations of things that could
  // make DebugInfoKind be other than None or DebugLineTablesOnly,
  // figure out if we need to "upgrade" it to standalone debug info.
  // We parse these two '-f' options whether or not they will be used,
  // to claim them even if you wrote "-fstandalone-debug -gline-tables-only"
  bool NeedFullDebug = Args.hasFlag(
      options::OPT_fstandalone_debug, options::OPT_fno_standalone_debug,
      DebuggerTuning == llvm::DebuggerKind::LLDB ||
          TC.GetDefaultStandaloneDebug());
  if (const Arg *A = Args.getLastArg(options::OPT_fstandalone_debug))
    (void)checkDebugInfoOption(A, Args, D, TC);

  if (DebugInfoKind == llvm::codegenoptions::LimitedDebugInfo ||
      DebugInfoKind == llvm::codegenoptions::DebugInfoConstructor) {
    if (Args.hasFlag(options::OPT_fno_eliminate_unused_debug_types,
                     options::OPT_feliminate_unused_debug_types, false))
      DebugInfoKind = llvm::codegenoptions::UnusedTypeInfo;
    else if (NeedFullDebug)
      DebugInfoKind = llvm::codegenoptions::FullDebugInfo;
  }

  if (Args.hasFlag(options::OPT_gembed_source, options::OPT_gno_embed_source,
                   false)) {
    // Source embedding is a vendor extension to DWARF v5. By now we have
    // checked if a DWARF version was stated explicitly, and have otherwise
    // fallen back to the target default, so if this is still not at least 5
    // we emit an error.
    const Arg *A = Args.getLastArg(options::OPT_gembed_source);
    if (RequestedDWARFVersion < 5)
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << A->getAsString(Args) << "-gdwarf-5";
    else if (EffectiveDWARFVersion < 5)
      // The toolchain has reduced allowed dwarf version, so we can't enable
      // -gembed-source.
      D.Diag(diag::warn_drv_dwarf_version_limited_by_target)
          << A->getAsString(Args) << TC.getTripleString() << 5
          << EffectiveDWARFVersion;
    else if (checkDebugInfoOption(A, Args, D, TC))
      CmdArgs.push_back("-gembed-source");
  }

  if (EmitCodeView) {
    CmdArgs.push_back("-gcodeview");

    Args.addOptInFlag(CmdArgs, options::OPT_gcodeview_ghash,
                      options::OPT_gno_codeview_ghash);

    Args.addOptOutFlag(CmdArgs, options::OPT_gcodeview_command_line,
                       options::OPT_gno_codeview_command_line);
  }

  Args.addOptOutFlag(CmdArgs, options::OPT_ginline_line_tables,
                     options::OPT_gno_inline_line_tables);

  // When emitting remarks, we need at least debug lines in the output.
  if (willEmitRemarks(Args) &&
      DebugInfoKind <= llvm::codegenoptions::DebugDirectivesOnly)
    DebugInfoKind = llvm::codegenoptions::DebugLineTablesOnly;

  // Adjust the debug info kind for the given toolchain.
  TC.adjustDebugInfoKind(DebugInfoKind, Args);

  // On AIX, the debugger tuning option can be omitted if it is not explicitly
  // set.
  RenderDebugEnablingArgs(Args, CmdArgs, DebugInfoKind, EffectiveDWARFVersion,
                          T.isOSAIX() && !HasDebuggerTuning
                              ? llvm::DebuggerKind::Default
                              : DebuggerTuning);

  // -fdebug-macro turns on macro debug info generation.
  if (Args.hasFlag(options::OPT_fdebug_macro, options::OPT_fno_debug_macro,
                   false))
    if (checkDebugInfoOption(Args.getLastArg(options::OPT_fdebug_macro), Args,
                             D, TC))
      CmdArgs.push_back("-debug-info-macro");

  // -ggnu-pubnames turns on gnu style pubnames in the backend.
  const auto *PubnamesArg =
      Args.getLastArg(options::OPT_ggnu_pubnames, options::OPT_gno_gnu_pubnames,
                      options::OPT_gpubnames, options::OPT_gno_pubnames);
  if (DwarfFission != DwarfFissionKind::None ||
      (PubnamesArg && checkDebugInfoOption(PubnamesArg, Args, D, TC))) {
    const bool OptionSet =
        (PubnamesArg &&
         (PubnamesArg->getOption().matches(options::OPT_gpubnames) ||
          PubnamesArg->getOption().matches(options::OPT_ggnu_pubnames)));
    if ((DebuggerTuning != llvm::DebuggerKind::LLDB || OptionSet) &&
        (!PubnamesArg ||
         (!PubnamesArg->getOption().matches(options::OPT_gno_gnu_pubnames) &&
          !PubnamesArg->getOption().matches(options::OPT_gno_pubnames))))
      CmdArgs.push_back(PubnamesArg && PubnamesArg->getOption().matches(
                                           options::OPT_gpubnames)
                            ? "-gpubnames"
                            : "-ggnu-pubnames");
  }
  const auto *SimpleTemplateNamesArg =
      Args.getLastArg(options::OPT_gsimple_template_names,
                      options::OPT_gno_simple_template_names);
  bool ForwardTemplateParams = DebuggerTuning == llvm::DebuggerKind::SCE;
  if (SimpleTemplateNamesArg &&
      checkDebugInfoOption(SimpleTemplateNamesArg, Args, D, TC)) {
    const auto &Opt = SimpleTemplateNamesArg->getOption();
    if (Opt.matches(options::OPT_gsimple_template_names)) {
      ForwardTemplateParams = true;
      CmdArgs.push_back("-gsimple-template-names=simple");
    }
  }

  // Emit DW_TAG_template_alias for template aliases? True by default for SCE.
  bool UseDebugTemplateAlias =
      DebuggerTuning == llvm::DebuggerKind::SCE && RequestedDWARFVersion >= 4;
  if (const auto *DebugTemplateAlias = Args.getLastArg(
          options::OPT_gtemplate_alias, options::OPT_gno_template_alias)) {
    // DW_TAG_template_alias is only supported from DWARFv5 but if a user
    // asks for it we should let them have it (if the target supports it).
    if (checkDebugInfoOption(DebugTemplateAlias, Args, D, TC)) {
      const auto &Opt = DebugTemplateAlias->getOption();
      UseDebugTemplateAlias = Opt.matches(options::OPT_gtemplate_alias);
    }
  }
  if (UseDebugTemplateAlias)
    CmdArgs.push_back("-gtemplate-alias");

  if (const Arg *A = Args.getLastArg(options::OPT_gsrc_hash_EQ)) {
    StringRef v = A->getValue();
    CmdArgs.push_back(Args.MakeArgString("-gsrc-hash=" + v));
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fdebug_ranges_base_address,
                    options::OPT_fno_debug_ranges_base_address);

  // -gdwarf-aranges turns on the emission of the aranges section in the
  // backend.
  // Always enabled for SCE tuning.
  bool NeedAranges = DebuggerTuning == llvm::DebuggerKind::SCE;
  if (const Arg *A = Args.getLastArg(options::OPT_gdwarf_aranges))
    NeedAranges = checkDebugInfoOption(A, Args, D, TC) || NeedAranges;
  if (NeedAranges) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-generate-arange-section");
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fforce_dwarf_frame,
                    options::OPT_fno_force_dwarf_frame);

  bool EnableTypeUnits = false;
  if (Args.hasFlag(options::OPT_fdebug_types_section,
                   options::OPT_fno_debug_types_section, false)) {
    if (!(T.isOSBinFormatELF() || T.isOSBinFormatWasm())) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << Args.getLastArg(options::OPT_fdebug_types_section)
                 ->getAsString(Args)
          << T.getTriple();
    } else if (checkDebugInfoOption(
                   Args.getLastArg(options::OPT_fdebug_types_section), Args, D,
                   TC)) {
      EnableTypeUnits = true;
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-generate-type-units");
    }
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_gomit_unreferenced_methods,
                          options::OPT_gno_omit_unreferenced_methods))
    (void)checkDebugInfoOption(A, Args, D, TC);
  if (Args.hasFlag(options::OPT_gomit_unreferenced_methods,
                   options::OPT_gno_omit_unreferenced_methods, false) &&
      (DebugInfoKind == llvm::codegenoptions::DebugInfoConstructor ||
       DebugInfoKind == llvm::codegenoptions::LimitedDebugInfo) &&
      !EnableTypeUnits) {
    CmdArgs.push_back("-gomit-unreferenced-methods");
  }

  // To avoid join/split of directory+filename, the integrated assembler prefers
  // the directory form of .file on all DWARF versions. GNU as doesn't allow the
  // form before DWARF v5.
  if (!Args.hasFlag(options::OPT_fdwarf_directory_asm,
                    options::OPT_fno_dwarf_directory_asm,
                    TC.useIntegratedAs() || EffectiveDWARFVersion >= 5))
    CmdArgs.push_back("-fno-dwarf-directory-asm");

  // Decide how to render forward declarations of template instantiations.
  // SCE wants full descriptions, others just get them in the name.
  if (ForwardTemplateParams)
    CmdArgs.push_back("-debug-forward-template-params");

  // Do we need to explicitly import anonymous namespaces into the parent
  // scope?
  if (DebuggerTuning == llvm::DebuggerKind::SCE)
    CmdArgs.push_back("-dwarf-explicit-import");

  renderDwarfFormat(D, T, Args, CmdArgs, EffectiveDWARFVersion);
  RenderDebugInfoCompressionArgs(Args, CmdArgs, D, TC);

  // This controls whether or not we perform JustMyCode instrumentation.
  if (Args.hasFlag(options::OPT_fjmc, options::OPT_fno_jmc, false)) {
    if (TC.getTriple().isOSBinFormatELF() || D.IsCLMode()) {
      if (DebugInfoKind >= llvm::codegenoptions::DebugInfoConstructor)
        CmdArgs.push_back("-fjmc");
      else if (D.IsCLMode())
        D.Diag(clang::diag::warn_drv_jmc_requires_debuginfo) << "/JMC"
                                                             << "'/Zi', '/Z7'";
      else
        D.Diag(clang::diag::warn_drv_jmc_requires_debuginfo) << "-fjmc"
                                                             << "-g";
    } else {
      D.Diag(clang::diag::warn_drv_fjmc_for_elf_only);
    }
  }

  // Add in -fdebug-compilation-dir if necessary.
  const char *DebugCompilationDir =
      addDebugCompDirArg(Args, CmdArgs, D.getVFS());

  addDebugPrefixMapArg(D, TC, Args, CmdArgs);

  // Add the output path to the object file for CodeView debug infos.
  if (EmitCodeView && Output.isFilename())
    addDebugObjectName(Args, CmdArgs, DebugCompilationDir,
                       Output.getFilename());
}

static void ProcessVSRuntimeLibrary(const ToolChain &TC, const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  unsigned RTOptionID = options::OPT__SLASH_MT;

  if (Args.hasArg(options::OPT__SLASH_LDd))
    // The /LDd option implies /MTd. The dependent lib part can be overridden,
    // but defining _DEBUG is sticky.
    RTOptionID = options::OPT__SLASH_MTd;

  if (Arg *A = Args.getLastArg(options::OPT__SLASH_M_Group))
    RTOptionID = A->getOption().getID();

  if (Arg *A = Args.getLastArg(options::OPT_fms_runtime_lib_EQ)) {
    RTOptionID = llvm::StringSwitch<unsigned>(A->getValue())
                     .Case("static", options::OPT__SLASH_MT)
                     .Case("static_dbg", options::OPT__SLASH_MTd)
                     .Case("dll", options::OPT__SLASH_MD)
                     .Case("dll_dbg", options::OPT__SLASH_MDd)
                     .Default(options::OPT__SLASH_MT);
  }

  StringRef FlagForCRT;
  switch (RTOptionID) {
  case options::OPT__SLASH_MD:
    if (Args.hasArg(options::OPT__SLASH_LDd))
      CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DLL");
    FlagForCRT = "--dependent-lib=msvcrt";
    break;
  case options::OPT__SLASH_MDd:
    CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DLL");
    FlagForCRT = "--dependent-lib=msvcrtd";
    break;
  case options::OPT__SLASH_MT:
    if (Args.hasArg(options::OPT__SLASH_LDd))
      CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-flto-visibility-public-std");
    FlagForCRT = "--dependent-lib=libcmt";
    break;
  case options::OPT__SLASH_MTd:
    CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-flto-visibility-public-std");
    FlagForCRT = "--dependent-lib=libcmtd";
    break;
  default:
    llvm_unreachable("Unexpected option ID.");
  }

  if (Args.hasArg(options::OPT_fms_omit_default_lib)) {
    CmdArgs.push_back("-D_VC_NODEFAULTLIB");
  } else {
    CmdArgs.push_back(FlagForCRT.data());

    // This provides POSIX compatibility (maps 'open' to '_open'), which most
    // users want.  The /Za flag to cl.exe turns this off, but it's not
    // implemented in clang.
    CmdArgs.push_back("--dependent-lib=oldnames");
  }

  // All Arm64EC object files implicitly add softintrin.lib. This is necessary
  // even if the file doesn't actually refer to any of the routines because
  // the CRT itself has incomplete dependency markings.
  if (TC.getTriple().isWindowsArm64EC())
    CmdArgs.push_back("--dependent-lib=softintrin");
}

void Clang::ConstructJob(Compilation &C, const JobAction &JA,
                         const InputInfo &Output, const InputInfoList &Inputs,
                         const ArgList &Args, const char *LinkingOutput) const {
  const auto &TC = getToolChain();
  const llvm::Triple &RawTriple = TC.getTriple();
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  const std::string &TripleStr = Triple.getTriple();

  bool KernelOrKext =
      Args.hasArg(options::OPT_mkernel, options::OPT_fapple_kext);
  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;

  assert(Inputs.size() >= 1 && "Must have at least one input.");
  // CUDA/HIP compilation may have multiple inputs (source file + results of
  // device-side compilations). OpenMP device jobs also take the host IR as a
  // second input. Module precompilation accepts a list of header files to
  // include as part of the module. API extraction accepts a list of header
  // files whose API information is emitted in the output. All other jobs are
  // expected to have exactly one input.
  bool IsCuda = JA.isOffloading(Action::OFK_Cuda);
  bool IsCudaDevice = JA.isDeviceOffloading(Action::OFK_Cuda);
  bool IsHIP = JA.isOffloading(Action::OFK_HIP);
  bool IsHIPDevice = JA.isDeviceOffloading(Action::OFK_HIP);
  bool IsOpenMPDevice = JA.isDeviceOffloading(Action::OFK_OpenMP);
  bool IsExtractAPI = isa<ExtractAPIJobAction>(JA);
  bool IsDeviceOffloadAction = !(JA.isDeviceOffloading(Action::OFK_None) ||
                                 JA.isDeviceOffloading(Action::OFK_Host));
  bool IsHostOffloadingAction =
      JA.isHostOffloading(Action::OFK_OpenMP) ||
      (JA.isHostOffloading(C.getActiveOffloadKinds()) &&
       Args.hasFlag(options::OPT_offload_new_driver,
                    options::OPT_no_offload_new_driver, false));

  bool IsRDCMode =
      Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc, false);
  bool IsUsingLTO = D.isUsingLTO(IsDeviceOffloadAction);
  auto LTOMode = D.getLTOMode(IsDeviceOffloadAction);

  // Extract API doesn't have a main input file, so invent a fake one as a
  // placeholder.
  InputInfo ExtractAPIPlaceholderInput(Inputs[0].getType(), "extract-api",
                                       "extract-api");

  const InputInfo &Input =
      IsExtractAPI ? ExtractAPIPlaceholderInput : Inputs[0];

  InputInfoList ExtractAPIInputs;
  InputInfoList HostOffloadingInputs;
  const InputInfo *CudaDeviceInput = nullptr;
  const InputInfo *OpenMPDeviceInput = nullptr;
  for (const InputInfo &I : Inputs) {
    if (&I == &Input || I.getType() == types::TY_Nothing) {
      // This is the primary input or contains nothing.
    } else if (IsExtractAPI) {
      auto ExpectedInputType = ExtractAPIPlaceholderInput.getType();
      if (I.getType() != ExpectedInputType) {
        D.Diag(diag::err_drv_extract_api_wrong_kind)
            << I.getFilename() << types::getTypeName(I.getType())
            << types::getTypeName(ExpectedInputType);
      }
      ExtractAPIInputs.push_back(I);
    } else if (IsHostOffloadingAction) {
      HostOffloadingInputs.push_back(I);
    } else if ((IsCuda || IsHIP) && !CudaDeviceInput) {
      CudaDeviceInput = &I;
    } else if (IsOpenMPDevice && !OpenMPDeviceInput) {
      OpenMPDeviceInput = &I;
    } else {
      llvm_unreachable("unexpectedly given multiple inputs");
    }
  }

  const llvm::Triple *AuxTriple =
      (IsCuda || IsHIP) ? TC.getAuxTriple() : nullptr;
  bool IsWindowsMSVC = RawTriple.isWindowsMSVCEnvironment();
  bool IsIAMCU = RawTriple.isOSIAMCU();

  // Adjust IsWindowsXYZ for CUDA/HIP compilations.  Even when compiling in
  // device mode (i.e., getToolchain().getTriple() is NVPTX/AMDGCN, not
  // Windows), we need to pass Windows-specific flags to cc1.
  if (IsCuda || IsHIP)
    IsWindowsMSVC |= AuxTriple && AuxTriple->isWindowsMSVCEnvironment();

  // C++ is not supported for IAMCU.
  if (IsIAMCU && types::isCXX(Input.getType()))
    D.Diag(diag::err_drv_clang_unsupported) << "C++ for IAMCU";

  // Invoke ourselves in -cc1 mode.
  //
  // FIXME: Implement custom jobs for internal actions.
  CmdArgs.push_back("-cc1");

  // Add the "effective" target triple.
  CmdArgs.push_back("-triple");
  CmdArgs.push_back(Args.MakeArgString(TripleStr));

  if (const Arg *MJ = Args.getLastArg(options::OPT_MJ)) {
    DumpCompilationDatabase(C, MJ->getValue(), TripleStr, Output, Input, Args);
    Args.ClaimAllArgs(options::OPT_MJ);
  } else if (const Arg *GenCDBFragment =
                 Args.getLastArg(options::OPT_gen_cdb_fragment_path)) {
    DumpCompilationDatabaseFragmentToDir(GenCDBFragment->getValue(), C,
                                         TripleStr, Output, Input, Args);
    Args.ClaimAllArgs(options::OPT_gen_cdb_fragment_path);
  }

  if (IsCuda || IsHIP) {
    // We have to pass the triple of the host if compiling for a CUDA/HIP device
    // and vice-versa.
    std::string NormalizedTriple;
    if (JA.isDeviceOffloading(Action::OFK_Cuda) ||
        JA.isDeviceOffloading(Action::OFK_HIP))
      NormalizedTriple = C.getSingleOffloadToolChain<Action::OFK_Host>()
                             ->getTriple()
                             .normalize();
    else {
      // Host-side compilation.
      NormalizedTriple =
          (IsCuda ? C.getSingleOffloadToolChain<Action::OFK_Cuda>()
                  : C.getSingleOffloadToolChain<Action::OFK_HIP>())
              ->getTriple()
              .normalize();
      if (IsCuda) {
        // We need to figure out which CUDA version we're compiling for, as that
        // determines how we load and launch GPU kernels.
        auto *CTC = static_cast<const toolchains::CudaToolChain *>(
            C.getSingleOffloadToolChain<Action::OFK_Cuda>());
        assert(CTC && "Expected valid CUDA Toolchain.");
        if (CTC && CTC->CudaInstallation.version() != CudaVersion::UNKNOWN)
          CmdArgs.push_back(Args.MakeArgString(
              Twine("-target-sdk-version=") +
              CudaVersionToString(CTC->CudaInstallation.version())));
        // Unsized function arguments used for variadics were introduced in
        // CUDA-9.0. We still do not support generating code that actually uses
        // variadic arguments yet, but we do need to allow parsing them as
        // recent CUDA headers rely on that.
        // https://github.com/llvm/llvm-project/issues/58410
        if (CTC->CudaInstallation.version() >= CudaVersion::CUDA_90)
          CmdArgs.push_back("-fcuda-allow-variadic-functions");
      }
    }
    CmdArgs.push_back("-aux-triple");
    CmdArgs.push_back(Args.MakeArgString(NormalizedTriple));

    if (JA.isDeviceOffloading(Action::OFK_HIP) &&
        (getToolChain().getTriple().isAMDGPU() ||
         (getToolChain().getTriple().isSPIRV() &&
          getToolChain().getTriple().getVendor() == llvm::Triple::AMD))) {
      // Device side compilation printf
      if (Args.getLastArg(options::OPT_mprintf_kind_EQ)) {
        CmdArgs.push_back(Args.MakeArgString(
            "-mprintf-kind=" +
            Args.getLastArgValue(options::OPT_mprintf_kind_EQ)));
        // Force compiler error on invalid conversion specifiers
        CmdArgs.push_back(
            Args.MakeArgString("-Werror=format-invalid-specifier"));
      }
    }
  }

  // Unconditionally claim the printf option now to avoid unused diagnostic.
  if (const Arg *PF = Args.getLastArg(options::OPT_mprintf_kind_EQ))
    PF->claim();

  if (Args.hasFlag(options::OPT_fsycl, options::OPT_fno_sycl, false)) {
    CmdArgs.push_back("-fsycl-is-device");

    if (Arg *A = Args.getLastArg(options::OPT_sycl_std_EQ)) {
      A->render(Args, CmdArgs);
    } else {
      // Ensure the default version in SYCL mode is 2020.
      CmdArgs.push_back("-sycl-std=2020");
    }
  }

  if (IsOpenMPDevice) {
    // We have to pass the triple of the host if compiling for an OpenMP device.
    std::string NormalizedTriple =
        C.getSingleOffloadToolChain<Action::OFK_Host>()
            ->getTriple()
            .normalize();
    CmdArgs.push_back("-aux-triple");
    CmdArgs.push_back(Args.MakeArgString(NormalizedTriple));
  }

  if (Triple.isOSWindows() && (Triple.getArch() == llvm::Triple::arm ||
                               Triple.getArch() == llvm::Triple::thumb)) {
    unsigned Offset = Triple.getArch() == llvm::Triple::arm ? 4 : 6;
    unsigned Version = 0;
    bool Failure =
        Triple.getArchName().substr(Offset).consumeInteger(10, Version);
    if (Failure || Version < 7)
      D.Diag(diag::err_target_unsupported_arch) << Triple.getArchName()
                                                << TripleStr;
  }

  // Push all default warning arguments that are specific to
  // the given target.  These come before user provided warning options
  // are provided.
  TC.addClangWarningOptions(CmdArgs);

  // FIXME: Subclass ToolChain for SPIR and move this to addClangWarningOptions.
  if (Triple.isSPIR() || Triple.isSPIRV())
    CmdArgs.push_back("-Wspir-compat");

  // Select the appropriate action.
  RewriteKind rewriteKind = RK_None;

  bool UnifiedLTO = false;
  if (IsUsingLTO) {
    UnifiedLTO = Args.hasFlag(options::OPT_funified_lto,
                              options::OPT_fno_unified_lto, Triple.isPS());
    if (UnifiedLTO)
      CmdArgs.push_back("-funified-lto");
  }

  // If CollectArgsForIntegratedAssembler() isn't called below, claim the args
  // it claims when not running an assembler. Otherwise, clang would emit
  // "argument unused" warnings for assembler flags when e.g. adding "-E" to
  // flags while debugging something. That'd be somewhat inconvenient, and it's
  // also inconsistent with most other flags -- we don't warn on
  // -ffunction-sections not being used in -E mode either for example, even
  // though it's not really used either.
  if (!isa<AssembleJobAction>(JA)) {
    // The args claimed here should match the args used in
    // CollectArgsForIntegratedAssembler().
    if (TC.useIntegratedAs()) {
      Args.ClaimAllArgs(options::OPT_mrelax_all);
      Args.ClaimAllArgs(options::OPT_mno_relax_all);
      Args.ClaimAllArgs(options::OPT_mincremental_linker_compatible);
      Args.ClaimAllArgs(options::OPT_mno_incremental_linker_compatible);
      switch (C.getDefaultToolChain().getArch()) {
      case llvm::Triple::arm:
      case llvm::Triple::armeb:
      case llvm::Triple::thumb:
      case llvm::Triple::thumbeb:
        Args.ClaimAllArgs(options::OPT_mimplicit_it_EQ);
        break;
      default:
        break;
      }
    }
    Args.ClaimAllArgs(options::OPT_Wa_COMMA);
    Args.ClaimAllArgs(options::OPT_Xassembler);
    Args.ClaimAllArgs(options::OPT_femit_dwarf_unwind_EQ);
  }

  if (isa<AnalyzeJobAction>(JA)) {
    assert(JA.getType() == types::TY_Plist && "Invalid output type.");
    CmdArgs.push_back("-analyze");
  } else if (isa<MigrateJobAction>(JA)) {
    CmdArgs.push_back("-migrate");
  } else if (isa<PreprocessJobAction>(JA)) {
    if (Output.getType() == types::TY_Dependencies)
      CmdArgs.push_back("-Eonly");
    else {
      CmdArgs.push_back("-E");
      if (Args.hasArg(options::OPT_rewrite_objc) &&
          !Args.hasArg(options::OPT_g_Group))
        CmdArgs.push_back("-P");
      else if (JA.getType() == types::TY_PP_CXXHeaderUnit)
        CmdArgs.push_back("-fdirectives-only");
    }
  } else if (isa<AssembleJobAction>(JA)) {
    CmdArgs.push_back("-emit-obj");

    CollectArgsForIntegratedAssembler(C, Args, CmdArgs, D);

    // Also ignore explicit -force_cpusubtype_ALL option.
    (void)Args.hasArg(options::OPT_force__cpusubtype__ALL);
  } else if (isa<PrecompileJobAction>(JA)) {
    if (JA.getType() == types::TY_Nothing)
      CmdArgs.push_back("-fsyntax-only");
    else if (JA.getType() == types::TY_ModuleFile)
      CmdArgs.push_back("-emit-module-interface");
    else if (JA.getType() == types::TY_HeaderUnit)
      CmdArgs.push_back("-emit-header-unit");
    else
      CmdArgs.push_back("-emit-pch");
  } else if (isa<VerifyPCHJobAction>(JA)) {
    CmdArgs.push_back("-verify-pch");
  } else if (isa<ExtractAPIJobAction>(JA)) {
    assert(JA.getType() == types::TY_API_INFO &&
           "Extract API actions must generate a API information.");
    CmdArgs.push_back("-extract-api");

    if (Arg *PrettySGFArg = Args.getLastArg(options::OPT_emit_pretty_sgf))
      PrettySGFArg->render(Args, CmdArgs);

    Arg *SymbolGraphDirArg = Args.getLastArg(options::OPT_symbol_graph_dir_EQ);

    if (Arg *ProductNameArg = Args.getLastArg(options::OPT_product_name_EQ))
      ProductNameArg->render(Args, CmdArgs);
    if (Arg *ExtractAPIIgnoresFileArg =
            Args.getLastArg(options::OPT_extract_api_ignores_EQ))
      ExtractAPIIgnoresFileArg->render(Args, CmdArgs);
    if (Arg *EmitExtensionSymbolGraphs =
            Args.getLastArg(options::OPT_emit_extension_symbol_graphs)) {
      if (!SymbolGraphDirArg)
        D.Diag(diag::err_drv_missing_symbol_graph_dir);

      EmitExtensionSymbolGraphs->render(Args, CmdArgs);
    }
    if (SymbolGraphDirArg)
      SymbolGraphDirArg->render(Args, CmdArgs);
  } else {
    assert((isa<CompileJobAction>(JA) || isa<BackendJobAction>(JA)) &&
           "Invalid action for clang tool.");
    if (JA.getType() == types::TY_Nothing) {
      CmdArgs.push_back("-fsyntax-only");
    } else if (JA.getType() == types::TY_LLVM_IR ||
               JA.getType() == types::TY_LTO_IR) {
      CmdArgs.push_back("-emit-llvm");
    } else if (JA.getType() == types::TY_LLVM_BC ||
               JA.getType() == types::TY_LTO_BC) {
      // Emit textual llvm IR for AMDGPU offloading for -emit-llvm -S
      if (Triple.isAMDGCN() && IsOpenMPDevice && Args.hasArg(options::OPT_S) &&
          Args.hasArg(options::OPT_emit_llvm)) {
        CmdArgs.push_back("-emit-llvm");
      } else {
        CmdArgs.push_back("-emit-llvm-bc");
      }
    } else if (JA.getType() == types::TY_IFS ||
               JA.getType() == types::TY_IFS_CPP) {
      StringRef ArgStr =
          Args.hasArg(options::OPT_interface_stub_version_EQ)
              ? Args.getLastArgValue(options::OPT_interface_stub_version_EQ)
              : "ifs-v1";
      CmdArgs.push_back("-emit-interface-stubs");
      CmdArgs.push_back(
          Args.MakeArgString(Twine("-interface-stub-version=") + ArgStr.str()));
    } else if (JA.getType() == types::TY_PP_Asm) {
      CmdArgs.push_back("-S");
    } else if (JA.getType() == types::TY_AST) {
      CmdArgs.push_back("-emit-pch");
    } else if (JA.getType() == types::TY_ModuleFile) {
      CmdArgs.push_back("-module-file-info");
    } else if (JA.getType() == types::TY_RewrittenObjC) {
      CmdArgs.push_back("-rewrite-objc");
      rewriteKind = RK_NonFragile;
    } else if (JA.getType() == types::TY_RewrittenLegacyObjC) {
      CmdArgs.push_back("-rewrite-objc");
      rewriteKind = RK_Fragile;
    } else {
      assert(JA.getType() == types::TY_PP_Asm && "Unexpected output type!");
    }

    // Preserve use-list order by default when emitting bitcode, so that
    // loading the bitcode up in 'opt' or 'llc' and running passes gives the
    // same result as running passes here.  For LTO, we don't need to preserve
    // the use-list order, since serialization to bitcode is part of the flow.
    if (JA.getType() == types::TY_LLVM_BC)
      CmdArgs.push_back("-emit-llvm-uselists");

    if (IsUsingLTO) {
      if (IsDeviceOffloadAction && !JA.isDeviceOffloading(Action::OFK_OpenMP) &&
          !Args.hasFlag(options::OPT_offload_new_driver,
                        options::OPT_no_offload_new_driver, false) &&
          !Triple.isAMDGPU()) {
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Args.getLastArg(options::OPT_foffload_lto,
                               options::OPT_foffload_lto_EQ)
                   ->getAsString(Args)
            << Triple.getTriple();
      } else if (Triple.isNVPTX() && !IsRDCMode &&
                 JA.isDeviceOffloading(Action::OFK_Cuda)) {
        D.Diag(diag::err_drv_unsupported_opt_for_language_mode)
            << Args.getLastArg(options::OPT_foffload_lto,
                               options::OPT_foffload_lto_EQ)
                   ->getAsString(Args)
            << "-fno-gpu-rdc";
      } else {
        assert(LTOMode == LTOK_Full || LTOMode == LTOK_Thin);
        CmdArgs.push_back(Args.MakeArgString(
            Twine("-flto=") + (LTOMode == LTOK_Thin ? "thin" : "full")));
        // PS4 uses the legacy LTO API, which does not support some of the
        // features enabled by -flto-unit.
        if (!RawTriple.isPS4() ||
            (D.getLTOMode() == LTOK_Full) || !UnifiedLTO)
          CmdArgs.push_back("-flto-unit");
      }
    }
  }

  Args.AddLastArg(CmdArgs, options::OPT_dumpdir);

  if (const Arg *A = Args.getLastArg(options::OPT_fthinlto_index_EQ)) {
    if (!types::isLLVMIR(Input.getType()))
      D.Diag(diag::err_drv_arg_requires_bitcode_input) << A->getAsString(Args);
    Args.AddLastArg(CmdArgs, options::OPT_fthinlto_index_EQ);
  }

  if (Triple.isPPC())
    Args.addOptInFlag(CmdArgs, options::OPT_mregnames,
                      options::OPT_mno_regnames);

  if (Args.getLastArg(options::OPT_fthin_link_bitcode_EQ))
    Args.AddLastArg(CmdArgs, options::OPT_fthin_link_bitcode_EQ);

  if (Args.getLastArg(options::OPT_save_temps_EQ))
    Args.AddLastArg(CmdArgs, options::OPT_save_temps_EQ);

  auto *MemProfArg = Args.getLastArg(options::OPT_fmemory_profile,
                                     options::OPT_fmemory_profile_EQ,
                                     options::OPT_fno_memory_profile);
  if (MemProfArg &&
      !MemProfArg->getOption().matches(options::OPT_fno_memory_profile))
    MemProfArg->render(Args, CmdArgs);

  if (auto *MemProfUseArg =
          Args.getLastArg(options::OPT_fmemory_profile_use_EQ)) {
    if (MemProfArg)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << MemProfUseArg->getAsString(Args) << MemProfArg->getAsString(Args);
    if (auto *PGOInstrArg = Args.getLastArg(options::OPT_fprofile_generate,
                                            options::OPT_fprofile_generate_EQ))
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << MemProfUseArg->getAsString(Args) << PGOInstrArg->getAsString(Args);
    MemProfUseArg->render(Args, CmdArgs);
  }

  // Embed-bitcode option.
  // Only white-listed flags below are allowed to be embedded.
  if (C.getDriver().embedBitcodeInObject() && !IsUsingLTO &&
      (isa<BackendJobAction>(JA) || isa<AssembleJobAction>(JA))) {
    // Add flags implied by -fembed-bitcode.
    Args.AddLastArg(CmdArgs, options::OPT_fembed_bitcode_EQ);
    // Disable all llvm IR level optimizations.
    CmdArgs.push_back("-disable-llvm-passes");

    // Render target options.
    TC.addClangTargetOptions(Args, CmdArgs, JA.getOffloadingDeviceKind());

    // reject options that shouldn't be supported in bitcode
    // also reject kernel/kext
    static const constexpr unsigned kBitcodeOptionIgnorelist[] = {
        options::OPT_mkernel,
        options::OPT_fapple_kext,
        options::OPT_ffunction_sections,
        options::OPT_fno_function_sections,
        options::OPT_fdata_sections,
        options::OPT_fno_data_sections,
        options::OPT_fbasic_block_sections_EQ,
        options::OPT_funique_internal_linkage_names,
        options::OPT_fno_unique_internal_linkage_names,
        options::OPT_funique_section_names,
        options::OPT_fno_unique_section_names,
        options::OPT_funique_basic_block_section_names,
        options::OPT_fno_unique_basic_block_section_names,
        options::OPT_mrestrict_it,
        options::OPT_mno_restrict_it,
        options::OPT_mstackrealign,
        options::OPT_mno_stackrealign,
        options::OPT_mstack_alignment,
        options::OPT_mcmodel_EQ,
        options::OPT_mlong_calls,
        options::OPT_mno_long_calls,
        options::OPT_ggnu_pubnames,
        options::OPT_gdwarf_aranges,
        options::OPT_fdebug_types_section,
        options::OPT_fno_debug_types_section,
        options::OPT_fdwarf_directory_asm,
        options::OPT_fno_dwarf_directory_asm,
        options::OPT_mrelax_all,
        options::OPT_mno_relax_all,
        options::OPT_ftrap_function_EQ,
        options::OPT_ffixed_r9,
        options::OPT_mfix_cortex_a53_835769,
        options::OPT_mno_fix_cortex_a53_835769,
        options::OPT_ffixed_x18,
        options::OPT_mglobal_merge,
        options::OPT_mno_global_merge,
        options::OPT_mred_zone,
        options::OPT_mno_red_zone,
        options::OPT_Wa_COMMA,
        options::OPT_Xassembler,
        options::OPT_mllvm,
    };
    for (const auto &A : Args)
      if (llvm::is_contained(kBitcodeOptionIgnorelist, A->getOption().getID()))
        D.Diag(diag::err_drv_unsupported_embed_bitcode) << A->getSpelling();

    // Render the CodeGen options that need to be passed.
    Args.addOptOutFlag(CmdArgs, options::OPT_foptimize_sibling_calls,
                       options::OPT_fno_optimize_sibling_calls);

    RenderFloatingPointOptions(TC, D, isOptimizationLevelFast(Args), Args,
                               CmdArgs, JA);

    // Render ABI arguments
    switch (TC.getArch()) {
    default: break;
    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumbeb:
      RenderARMABI(D, Triple, Args, CmdArgs);
      break;
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_32:
    case llvm::Triple::aarch64_be:
      RenderAArch64ABI(Triple, Args, CmdArgs);
      break;
    }

    // Optimization level for CodeGen.
    if (const Arg *A = Args.getLastArg(options::OPT_O_Group)) {
      if (A->getOption().matches(options::OPT_O4)) {
        CmdArgs.push_back("-O3");
        D.Diag(diag::warn_O4_is_O3);
      } else {
        A->render(Args, CmdArgs);
      }
    }

    // Input/Output file.
    if (Output.getType() == types::TY_Dependencies) {
      // Handled with other dependency code.
    } else if (Output.isFilename()) {
      CmdArgs.push_back("-o");
      CmdArgs.push_back(Output.getFilename());
    } else {
      assert(Output.isNothing() && "Input output.");
    }

    for (const auto &II : Inputs) {
      addDashXForInput(Args, II, CmdArgs);
      if (II.isFilename())
        CmdArgs.push_back(II.getFilename());
      else
        II.getInputArg().renderAsInput(Args, CmdArgs);
    }

    C.addCommand(std::make_unique<Command>(
        JA, *this, ResponseFileSupport::AtFileUTF8(), D.getClangProgramPath(),
        CmdArgs, Inputs, Output, D.getPrependArg()));
    return;
  }

  if (C.getDriver().embedBitcodeMarkerOnly() && !IsUsingLTO)
    CmdArgs.push_back("-fembed-bitcode=marker");

  // We normally speed up the clang process a bit by skipping destructors at
  // exit, but when we're generating diagnostics we can rely on some of the
  // cleanup.
  if (!C.isForDiagnostics())
    CmdArgs.push_back("-disable-free");
  CmdArgs.push_back("-clear-ast-before-backend");

#ifdef NDEBUG
  const bool IsAssertBuild = false;
#else
  const bool IsAssertBuild = true;
#endif

  // Disable the verification pass in asserts builds unless otherwise specified.
  if (Args.hasFlag(options::OPT_fno_verify_intermediate_code,
                   options::OPT_fverify_intermediate_code, !IsAssertBuild)) {
    CmdArgs.push_back("-disable-llvm-verifier");
  }

  // Discard value names in assert builds unless otherwise specified.
  if (Args.hasFlag(options::OPT_fdiscard_value_names,
                   options::OPT_fno_discard_value_names, !IsAssertBuild)) {
    if (Args.hasArg(options::OPT_fdiscard_value_names) &&
        llvm::any_of(Inputs, [](const clang::driver::InputInfo &II) {
          return types::isLLVMIR(II.getType());
        })) {
      D.Diag(diag::warn_ignoring_fdiscard_for_bitcode);
    }
    CmdArgs.push_back("-discard-value-names");
  }

  // Set the main file name, so that debug info works even with
  // -save-temps.
  CmdArgs.push_back("-main-file-name");
  CmdArgs.push_back(getBaseInputName(Args, Input));

  // Some flags which affect the language (via preprocessor
  // defines).
  if (Args.hasArg(options::OPT_static))
    CmdArgs.push_back("-static-define");

  if (Args.hasArg(options::OPT_municode))
    CmdArgs.push_back("-DUNICODE");

  if (isa<AnalyzeJobAction>(JA))
    RenderAnalyzerOptions(Args, CmdArgs, Triple, Input);

  if (isa<AnalyzeJobAction>(JA) ||
      (isa<PreprocessJobAction>(JA) && Args.hasArg(options::OPT__analyze)))
    CmdArgs.push_back("-setup-static-analyzer");

  // Enable compatilibily mode to avoid analyzer-config related errors.
  // Since we can't access frontend flags through hasArg, let's manually iterate
  // through them.
  bool FoundAnalyzerConfig = false;
  for (auto *Arg : Args.filtered(options::OPT_Xclang))
    if (StringRef(Arg->getValue()) == "-analyzer-config") {
      FoundAnalyzerConfig = true;
      break;
    }
  if (!FoundAnalyzerConfig)
    for (auto *Arg : Args.filtered(options::OPT_Xanalyzer))
      if (StringRef(Arg->getValue()) == "-analyzer-config") {
        FoundAnalyzerConfig = true;
        break;
      }
  if (FoundAnalyzerConfig)
    CmdArgs.push_back("-analyzer-config-compatibility-mode=true");

  CheckCodeGenerationOptions(D, Args);

  unsigned FunctionAlignment = ParseFunctionAlignment(TC, Args);
  assert(FunctionAlignment <= 31 && "function alignment will be truncated!");
  if (FunctionAlignment) {
    CmdArgs.push_back("-function-alignment");
    CmdArgs.push_back(Args.MakeArgString(std::to_string(FunctionAlignment)));
  }

  // We support -falign-loops=N where N is a power of 2. GCC supports more
  // forms.
  if (const Arg *A = Args.getLastArg(options::OPT_falign_loops_EQ)) {
    unsigned Value = 0;
    if (StringRef(A->getValue()).getAsInteger(10, Value) || Value > 65536)
      TC.getDriver().Diag(diag::err_drv_invalid_int_value)
          << A->getAsString(Args) << A->getValue();
    else if (Value & (Value - 1))
      TC.getDriver().Diag(diag::err_drv_alignment_not_power_of_two)
          << A->getAsString(Args) << A->getValue();
    // Treat =0 as unspecified (use the target preference).
    if (Value)
      CmdArgs.push_back(Args.MakeArgString("-falign-loops=" +
                                           Twine(std::min(Value, 65536u))));
  }

  if (Triple.isOSzOS()) {
    // On z/OS some of the system header feature macros need to
    // be defined to enable most cross platform projects to build
    // successfully.  Ths include the libc++ library.  A
    // complicating factor is that users can define these
    // macros to the same or different values.  We need to add
    // the definition for these macros to the compilation command
    // if the user hasn't already defined them.

    auto findMacroDefinition = [&](const std::string &Macro) {
      auto MacroDefs = Args.getAllArgValues(options::OPT_D);
      return llvm::any_of(MacroDefs, [&](const std::string &M) {
        return M == Macro || M.find(Macro + '=') != std::string::npos;
      });
    };

    // _UNIX03_WITHDRAWN is required for libcxx & porting.
    if (!findMacroDefinition("_UNIX03_WITHDRAWN"))
      CmdArgs.push_back("-D_UNIX03_WITHDRAWN");
    // _OPEN_DEFAULT is required for XL compat
    if (!findMacroDefinition("_OPEN_DEFAULT"))
      CmdArgs.push_back("-D_OPEN_DEFAULT");
    if (D.CCCIsCXX() || types::isCXX(Input.getType())) {
      // _XOPEN_SOURCE=600 is required for libcxx.
      if (!findMacroDefinition("_XOPEN_SOURCE"))
        CmdArgs.push_back("-D_XOPEN_SOURCE=600");
    }
  }

  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) = ParsePICArgs(TC, Args);
  Arg *LastPICDataRelArg =
      Args.getLastArg(options::OPT_mno_pic_data_is_text_relative,
                      options::OPT_mpic_data_is_text_relative);
  bool NoPICDataIsTextRelative = false;
  if (LastPICDataRelArg) {
    if (LastPICDataRelArg->getOption().matches(
            options::OPT_mno_pic_data_is_text_relative)) {
      NoPICDataIsTextRelative = true;
      if (!PICLevel)
        D.Diag(diag::err_drv_argument_only_allowed_with)
            << "-mno-pic-data-is-text-relative"
            << "-fpic/-fpie";
    }
    if (!Triple.isSystemZ())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << (NoPICDataIsTextRelative ? "-mno-pic-data-is-text-relative"
                                      : "-mpic-data-is-text-relative")
          << RawTriple.str();
  }

  bool IsROPI = RelocationModel == llvm::Reloc::ROPI ||
                RelocationModel == llvm::Reloc::ROPI_RWPI;
  bool IsRWPI = RelocationModel == llvm::Reloc::RWPI ||
                RelocationModel == llvm::Reloc::ROPI_RWPI;

  if (Args.hasArg(options::OPT_mcmse) &&
      !Args.hasArg(options::OPT_fallow_unsupported)) {
    if (IsROPI)
      D.Diag(diag::err_cmse_pi_are_incompatible) << IsROPI;
    if (IsRWPI)
      D.Diag(diag::err_cmse_pi_are_incompatible) << !IsRWPI;
  }

  if (IsROPI && types::isCXX(Input.getType()) &&
      !Args.hasArg(options::OPT_fallow_unsupported))
    D.Diag(diag::err_drv_ropi_incompatible_with_cxx);

  const char *RMName = RelocationModelName(RelocationModel);
  if (RMName) {
    CmdArgs.push_back("-mrelocation-model");
    CmdArgs.push_back(RMName);
  }
  if (PICLevel > 0) {
    CmdArgs.push_back("-pic-level");
    CmdArgs.push_back(PICLevel == 1 ? "1" : "2");
    if (IsPIE)
      CmdArgs.push_back("-pic-is-pie");
    if (NoPICDataIsTextRelative)
      CmdArgs.push_back("-mcmodel=medium");
  }

  if (RelocationModel == llvm::Reloc::ROPI ||
      RelocationModel == llvm::Reloc::ROPI_RWPI)
    CmdArgs.push_back("-fropi");
  if (RelocationModel == llvm::Reloc::RWPI ||
      RelocationModel == llvm::Reloc::ROPI_RWPI)
    CmdArgs.push_back("-frwpi");

  if (Arg *A = Args.getLastArg(options::OPT_meabi)) {
    CmdArgs.push_back("-meabi");
    CmdArgs.push_back(A->getValue());
  }

  // -fsemantic-interposition is forwarded to CC1: set the
  // "SemanticInterposition" metadata to 1 (make some linkages interposable) and
  // make default visibility external linkage definitions dso_preemptable.
  //
  // -fno-semantic-interposition: if the target supports .Lfoo$local local
  // aliases (make default visibility external linkage definitions dso_local).
  // This is the CC1 default for ELF to match COFF/Mach-O.
  //
  // Otherwise use Clang's traditional behavior: like
  // -fno-semantic-interposition but local aliases are not used. So references
  // can be interposed if not optimized out.
  if (Triple.isOSBinFormatELF()) {
    Arg *A = Args.getLastArg(options::OPT_fsemantic_interposition,
                             options::OPT_fno_semantic_interposition);
    if (RelocationModel != llvm::Reloc::Static && !IsPIE) {
      // The supported targets need to call AsmPrinter::getSymbolPreferLocal.
      bool SupportsLocalAlias =
          Triple.isAArch64() || Triple.isRISCV() || Triple.isX86();
      if (!A)
        CmdArgs.push_back("-fhalf-no-semantic-interposition");
      else if (A->getOption().matches(options::OPT_fsemantic_interposition))
        A->render(Args, CmdArgs);
      else if (!SupportsLocalAlias)
        CmdArgs.push_back("-fhalf-no-semantic-interposition");
    }
  }

  {
    std::string Model;
    if (Arg *A = Args.getLastArg(options::OPT_mthread_model)) {
      if (!TC.isThreadModelSupported(A->getValue()))
        D.Diag(diag::err_drv_invalid_thread_model_for_target)
            << A->getValue() << A->getAsString(Args);
      Model = A->getValue();
    } else
      Model = TC.getThreadModel();
    if (Model != "posix") {
      CmdArgs.push_back("-mthread-model");
      CmdArgs.push_back(Args.MakeArgString(Model));
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fveclib)) {
    StringRef Name = A->getValue();
    if (Name == "SVML") {
      if (Triple.getArch() != llvm::Triple::x86 &&
          Triple.getArch() != llvm::Triple::x86_64)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    } else if (Name == "LIBMVEC-X86") {
      if (Triple.getArch() != llvm::Triple::x86 &&
          Triple.getArch() != llvm::Triple::x86_64)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    } else if (Name == "SLEEF" || Name == "ArmPL") {
      if (Triple.getArch() != llvm::Triple::aarch64 &&
          Triple.getArch() != llvm::Triple::aarch64_be)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    }
    A->render(Args, CmdArgs);
  }

  if (Args.hasFlag(options::OPT_fmerge_all_constants,
                   options::OPT_fno_merge_all_constants, false))
    CmdArgs.push_back("-fmerge-all-constants");

  Args.addOptOutFlag(CmdArgs, options::OPT_fdelete_null_pointer_checks,
                     options::OPT_fno_delete_null_pointer_checks);

  // LLVM Code Generator Options.

  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ_quadword_atomics)) {
    if (!Triple.isOSAIX() || Triple.isPPC32())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getSpelling() << RawTriple.str();
    CmdArgs.push_back("-mabi=quadword-atomics");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mlong_double_128)) {
    // Emit the unsupported option error until the Clang's library integration
    // support for 128-bit long double is available for AIX.
    if (Triple.isOSAIX())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << RawTriple.str();
  }

  if (Arg *A = Args.getLastArg(options::OPT_Wframe_larger_than_EQ)) {
    StringRef V = A->getValue(), V1 = V;
    unsigned Size;
    if (V1.consumeInteger(10, Size) || !V1.empty())
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << V << A->getOption().getName();
    else
      CmdArgs.push_back(Args.MakeArgString("-fwarn-stack-size=" + V));
  }

  Args.addOptOutFlag(CmdArgs, options::OPT_fjump_tables,
                     options::OPT_fno_jump_tables);
  Args.addOptInFlag(CmdArgs, options::OPT_fprofile_sample_accurate,
                    options::OPT_fno_profile_sample_accurate);
  Args.addOptOutFlag(CmdArgs, options::OPT_fpreserve_as_comments,
                     options::OPT_fno_preserve_as_comments);

  if (Arg *A = Args.getLastArg(options::OPT_mregparm_EQ)) {
    CmdArgs.push_back("-mregparm");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_maix_struct_return,
                               options::OPT_msvr4_struct_return)) {
    if (!TC.getTriple().isPPC32()) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << RawTriple.str();
    } else if (A->getOption().matches(options::OPT_maix_struct_return)) {
      CmdArgs.push_back("-maix-struct-return");
    } else {
      assert(A->getOption().matches(options::OPT_msvr4_struct_return));
      CmdArgs.push_back("-msvr4-struct-return");
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fpcc_struct_return,
                               options::OPT_freg_struct_return)) {
    if (TC.getArch() != llvm::Triple::x86) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << RawTriple.str();
    } else if (A->getOption().matches(options::OPT_fpcc_struct_return)) {
      CmdArgs.push_back("-fpcc-struct-return");
    } else {
      assert(A->getOption().matches(options::OPT_freg_struct_return));
      CmdArgs.push_back("-freg-struct-return");
    }
  }

  if (Args.hasFlag(options::OPT_mrtd, options::OPT_mno_rtd, false)) {
    if (Triple.getArch() == llvm::Triple::m68k)
      CmdArgs.push_back("-fdefault-calling-conv=rtdcall");
    else
      CmdArgs.push_back("-fdefault-calling-conv=stdcall");
  }

  if (Args.hasArg(options::OPT_fenable_matrix)) {
    // enable-matrix is needed by both the LangOpts and by LLVM.
    CmdArgs.push_back("-fenable-matrix");
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-enable-matrix");
  }

  CodeGenOptions::FramePointerKind FPKeepKind =
                  getFramePointerKind(Args, RawTriple);
  const char *FPKeepKindStr = nullptr;
  switch (FPKeepKind) {
  case CodeGenOptions::FramePointerKind::None:
    FPKeepKindStr = "-mframe-pointer=none";
    break;
  case CodeGenOptions::FramePointerKind::Reserved:
    FPKeepKindStr = "-mframe-pointer=reserved";
    break;
  case CodeGenOptions::FramePointerKind::NonLeaf:
    FPKeepKindStr = "-mframe-pointer=non-leaf";
    break;
  case CodeGenOptions::FramePointerKind::All:
    FPKeepKindStr = "-mframe-pointer=all";
    break;
  }
  assert(FPKeepKindStr && "unknown FramePointerKind");
  CmdArgs.push_back(FPKeepKindStr);

  Args.addOptOutFlag(CmdArgs, options::OPT_fzero_initialized_in_bss,
                     options::OPT_fno_zero_initialized_in_bss);

  bool OFastEnabled = isOptimizationLevelFast(Args);
  if (OFastEnabled)
    D.Diag(diag::warn_drv_deprecated_arg_ofast);
  // If -Ofast is the optimization level, then -fstrict-aliasing should be
  // enabled.  This alias option is being used to simplify the hasFlag logic.
  OptSpecifier StrictAliasingAliasOption =
      OFastEnabled ? options::OPT_Ofast : options::OPT_fstrict_aliasing;
  // We turn strict aliasing off by default if we're Windows MSVC since MSVC
  // doesn't do any TBAA.
  bool StrictAliasingDefault = !IsWindowsMSVC;
  // We also turn off strict aliasing on OpenBSD.
  if (getToolChain().getTriple().isOSOpenBSD())
    StrictAliasingDefault = false;
  if (!Args.hasFlag(options::OPT_fstrict_aliasing, StrictAliasingAliasOption,
                    options::OPT_fno_strict_aliasing, StrictAliasingDefault))
    CmdArgs.push_back("-relaxed-aliasing");
  if (Args.hasFlag(options::OPT_fpointer_tbaa, options::OPT_fno_pointer_tbaa,
                   false))
    CmdArgs.push_back("-pointer-tbaa");
  if (!Args.hasFlag(options::OPT_fstruct_path_tbaa,
                    options::OPT_fno_struct_path_tbaa, true))
    CmdArgs.push_back("-no-struct-path-tbaa");
  Args.addOptInFlag(CmdArgs, options::OPT_fstrict_enums,
                    options::OPT_fno_strict_enums);
  Args.addOptOutFlag(CmdArgs, options::OPT_fstrict_return,
                     options::OPT_fno_strict_return);
  Args.addOptInFlag(CmdArgs, options::OPT_fallow_editor_placeholders,
                    options::OPT_fno_allow_editor_placeholders);
  Args.addOptInFlag(CmdArgs, options::OPT_fstrict_vtable_pointers,
                    options::OPT_fno_strict_vtable_pointers);
  Args.addOptInFlag(CmdArgs, options::OPT_fforce_emit_vtables,
                    options::OPT_fno_force_emit_vtables);
  Args.addOptOutFlag(CmdArgs, options::OPT_foptimize_sibling_calls,
                     options::OPT_fno_optimize_sibling_calls);
  Args.addOptOutFlag(CmdArgs, options::OPT_fescaping_block_tail_calls,
                     options::OPT_fno_escaping_block_tail_calls);

  Args.AddLastArg(CmdArgs, options::OPT_ffine_grained_bitfield_accesses,
                  options::OPT_fno_fine_grained_bitfield_accesses);

  Args.AddLastArg(CmdArgs, options::OPT_fexperimental_relative_cxx_abi_vtables,
                  options::OPT_fno_experimental_relative_cxx_abi_vtables);

  Args.AddLastArg(CmdArgs, options::OPT_fexperimental_omit_vtable_rtti,
                  options::OPT_fno_experimental_omit_vtable_rtti);

  Args.AddLastArg(CmdArgs, options::OPT_fdisable_block_signature_string,
                  options::OPT_fno_disable_block_signature_string);

  // Handle segmented stacks.
  Args.addOptInFlag(CmdArgs, options::OPT_fsplit_stack,
                    options::OPT_fno_split_stack);

  // -fprotect-parens=0 is default.
  if (Args.hasFlag(options::OPT_fprotect_parens,
                   options::OPT_fno_protect_parens, false))
    CmdArgs.push_back("-fprotect-parens");

  RenderFloatingPointOptions(TC, D, OFastEnabled, Args, CmdArgs, JA);

  if (Arg *A = Args.getLastArg(options::OPT_fextend_args_EQ)) {
    const llvm::Triple::ArchType Arch = TC.getArch();
    if (Arch == llvm::Triple::x86 || Arch == llvm::Triple::x86_64) {
      StringRef V = A->getValue();
      if (V == "64")
        CmdArgs.push_back("-fextend-arguments=64");
      else if (V != "32")
        D.Diag(diag::err_drv_invalid_argument_to_option)
            << A->getValue() << A->getOption().getName();
    } else
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getOption().getName() << TripleStr;
  }

  if (Arg *A = Args.getLastArg(options::OPT_mdouble_EQ)) {
    if (TC.getArch() == llvm::Triple::avr)
      A->render(Args, CmdArgs);
    else
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
  }

  if (Arg *A = Args.getLastArg(options::OPT_LongDouble_Group)) {
    if (TC.getTriple().isX86())
      A->render(Args, CmdArgs);
    else if (TC.getTriple().isPPC() &&
             (A->getOption().getID() != options::OPT_mlong_double_80))
      A->render(Args, CmdArgs);
    else
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
  }

  // Decide whether to use verbose asm. Verbose assembly is the default on
  // toolchains which have the integrated assembler on by default.
  bool IsIntegratedAssemblerDefault = TC.IsIntegratedAssemblerDefault();
  if (!Args.hasFlag(options::OPT_fverbose_asm, options::OPT_fno_verbose_asm,
                    IsIntegratedAssemblerDefault))
    CmdArgs.push_back("-fno-verbose-asm");

  // Parse 'none' or '$major.$minor'. Disallow -fbinutils-version=0 because we
  // use that to indicate the MC default in the backend.
  if (Arg *A = Args.getLastArg(options::OPT_fbinutils_version_EQ)) {
    StringRef V = A->getValue();
    unsigned Num;
    if (V == "none")
      A->render(Args, CmdArgs);
    else if (!V.consumeInteger(10, Num) && Num > 0 &&
             (V.empty() || (V.consume_front(".") &&
                            !V.consumeInteger(10, Num) && V.empty())))
      A->render(Args, CmdArgs);
    else
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << A->getValue() << A->getOption().getName();
  }

  // If toolchain choose to use MCAsmParser for inline asm don't pass the
  // option to disable integrated-as explicitly.
  if (!TC.useIntegratedAs() && !TC.parseInlineAsmUsingAsmParser())
    CmdArgs.push_back("-no-integrated-as");

  if (Args.hasArg(options::OPT_fdebug_pass_structure)) {
    CmdArgs.push_back("-mdebug-pass");
    CmdArgs.push_back("Structure");
  }
  if (Args.hasArg(options::OPT_fdebug_pass_arguments)) {
    CmdArgs.push_back("-mdebug-pass");
    CmdArgs.push_back("Arguments");
  }

  // Enable -mconstructor-aliases except on darwin, where we have to work around
  // a linker bug (see https://openradar.appspot.com/7198997), and CUDA device
  // code, where aliases aren't supported.
  if (!RawTriple.isOSDarwin() && !RawTriple.isNVPTX())
    CmdArgs.push_back("-mconstructor-aliases");

  // Darwin's kernel doesn't support guard variables; just die if we
  // try to use them.
  if (KernelOrKext && RawTriple.isOSDarwin())
    CmdArgs.push_back("-fforbid-guard-variables");

  if (Args.hasFlag(options::OPT_mms_bitfields, options::OPT_mno_ms_bitfields,
                   Triple.isWindowsGNUEnvironment())) {
    CmdArgs.push_back("-mms-bitfields");
  }

  if (Triple.isWindowsGNUEnvironment()) {
    Args.addOptOutFlag(CmdArgs, options::OPT_fauto_import,
                       options::OPT_fno_auto_import);
  }

  if (Args.hasFlag(options::OPT_fms_volatile, options::OPT_fno_ms_volatile,
                   Triple.isX86() && D.IsCLMode()))
    CmdArgs.push_back("-fms-volatile");

  // Non-PIC code defaults to -fdirect-access-external-data while PIC code
  // defaults to -fno-direct-access-external-data. Pass the option if different
  // from the default.
  if (Arg *A = Args.getLastArg(options::OPT_fdirect_access_external_data,
                               options::OPT_fno_direct_access_external_data)) {
    if (A->getOption().matches(options::OPT_fdirect_access_external_data) !=
        (PICLevel == 0))
      A->render(Args, CmdArgs);
  } else if (PICLevel == 0 && Triple.isLoongArch()) {
    // Some targets default to -fno-direct-access-external-data even for
    // -fno-pic.
    CmdArgs.push_back("-fno-direct-access-external-data");
  }

  if (Args.hasFlag(options::OPT_fno_plt, options::OPT_fplt, false)) {
    CmdArgs.push_back("-fno-plt");
  }

  // -fhosted is default.
  // TODO: Audit uses of KernelOrKext and see where it'd be more appropriate to
  // use Freestanding.
  bool Freestanding =
      Args.hasFlag(options::OPT_ffreestanding, options::OPT_fhosted, false) ||
      KernelOrKext;
  if (Freestanding)
    CmdArgs.push_back("-ffreestanding");

  Args.AddLastArg(CmdArgs, options::OPT_fno_knr_functions);

  // This is a coarse approximation of what llvm-gcc actually does, both
  // -fasynchronous-unwind-tables and -fnon-call-exceptions interact in more
  // complicated ways.
  auto SanitizeArgs = TC.getSanitizerArgs(Args);

  bool IsAsyncUnwindTablesDefault =
      TC.getDefaultUnwindTableLevel(Args) == ToolChain::UnwindTableLevel::Asynchronous;
  bool IsSyncUnwindTablesDefault =
      TC.getDefaultUnwindTableLevel(Args) == ToolChain::UnwindTableLevel::Synchronous;

  bool AsyncUnwindTables = Args.hasFlag(
      options::OPT_fasynchronous_unwind_tables,
      options::OPT_fno_asynchronous_unwind_tables,
      (IsAsyncUnwindTablesDefault || SanitizeArgs.needsUnwindTables()) &&
          !Freestanding);
  bool UnwindTables =
      Args.hasFlag(options::OPT_funwind_tables, options::OPT_fno_unwind_tables,
                   IsSyncUnwindTablesDefault && !Freestanding);
  if (AsyncUnwindTables)
    CmdArgs.push_back("-funwind-tables=2");
  else if (UnwindTables)
     CmdArgs.push_back("-funwind-tables=1");

  // Prepare `-aux-target-cpu` and `-aux-target-feature` unless
  // `--gpu-use-aux-triple-only` is specified.
  if (!Args.getLastArg(options::OPT_gpu_use_aux_triple_only) &&
      (IsCudaDevice || IsHIPDevice)) {
    const ArgList &HostArgs =
        C.getArgsForToolChain(nullptr, StringRef(), Action::OFK_None);
    std::string HostCPU =
        getCPUName(D, HostArgs, *TC.getAuxTriple(), /*FromAs*/ false);
    if (!HostCPU.empty()) {
      CmdArgs.push_back("-aux-target-cpu");
      CmdArgs.push_back(Args.MakeArgString(HostCPU));
    }
    getTargetFeatures(D, *TC.getAuxTriple(), HostArgs, CmdArgs,
                      /*ForAS*/ false, /*IsAux*/ true);
  }

  TC.addClangTargetOptions(Args, CmdArgs, JA.getOffloadingDeviceKind());

  addMCModel(D, Args, Triple, RelocationModel, CmdArgs);

  if (Arg *A = Args.getLastArg(options::OPT_mtls_size_EQ)) {
    StringRef Value = A->getValue();
    unsigned TLSSize = 0;
    Value.getAsInteger(10, TLSSize);
    if (!Triple.isAArch64() || !Triple.isOSBinFormatELF())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getOption().getName() << TripleStr;
    if (TLSSize != 12 && TLSSize != 24 && TLSSize != 32 && TLSSize != 48)
      D.Diag(diag::err_drv_invalid_int_value)
          << A->getOption().getName() << Value;
    Args.AddLastArg(CmdArgs, options::OPT_mtls_size_EQ);
  }

  if (isTLSDESCEnabled(TC, Args))
    CmdArgs.push_back("-enable-tlsdesc");

  // Add the target cpu
  std::string CPU = getCPUName(D, Args, Triple, /*FromAs*/ false);
  if (!CPU.empty()) {
    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPU));
  }

  RenderTargetOptions(Triple, Args, KernelOrKext, CmdArgs);

  // Add clang-cl arguments.
  types::ID InputType = Input.getType();
  if (D.IsCLMode())
    AddClangCLArgs(Args, InputType, CmdArgs);

  llvm::codegenoptions::DebugInfoKind DebugInfoKind =
      llvm::codegenoptions::NoDebugInfo;
  DwarfFissionKind DwarfFission = DwarfFissionKind::None;
  renderDebugOptions(TC, D, RawTriple, Args, types::isLLVMIR(InputType),
                     CmdArgs, Output, DebugInfoKind, DwarfFission);

  // Add the split debug info name to the command lines here so we
  // can propagate it to the backend.
  bool SplitDWARF = (DwarfFission != DwarfFissionKind::None) &&
                    (TC.getTriple().isOSBinFormatELF() ||
                     TC.getTriple().isOSBinFormatWasm() ||
                     TC.getTriple().isOSBinFormatCOFF()) &&
                    (isa<AssembleJobAction>(JA) || isa<CompileJobAction>(JA) ||
                     isa<BackendJobAction>(JA));
  if (SplitDWARF) {
    const char *SplitDWARFOut = SplitDebugName(JA, Args, Input, Output);
    CmdArgs.push_back("-split-dwarf-file");
    CmdArgs.push_back(SplitDWARFOut);
    if (DwarfFission == DwarfFissionKind::Split) {
      CmdArgs.push_back("-split-dwarf-output");
      CmdArgs.push_back(SplitDWARFOut);
    }
  }

  // Pass the linker version in use.
  if (Arg *A = Args.getLastArg(options::OPT_mlinker_version_EQ)) {
    CmdArgs.push_back("-target-linker-version");
    CmdArgs.push_back(A->getValue());
  }

  // Explicitly error on some things we know we don't support and can't just
  // ignore.
  if (!Args.hasArg(options::OPT_fallow_unsupported)) {
    Arg *Unsupported;
    if (types::isCXX(InputType) && RawTriple.isOSDarwin() &&
        TC.getArch() == llvm::Triple::x86) {
      if ((Unsupported = Args.getLastArg(options::OPT_fapple_kext)) ||
          (Unsupported = Args.getLastArg(options::OPT_mkernel)))
        D.Diag(diag::err_drv_clang_unsupported_opt_cxx_darwin_i386)
            << Unsupported->getOption().getName();
    }
    // The faltivec option has been superseded by the maltivec option.
    if ((Unsupported = Args.getLastArg(options::OPT_faltivec)))
      D.Diag(diag::err_drv_clang_unsupported_opt_faltivec)
          << Unsupported->getOption().getName()
          << "please use -maltivec and include altivec.h explicitly";
    if ((Unsupported = Args.getLastArg(options::OPT_fno_altivec)))
      D.Diag(diag::err_drv_clang_unsupported_opt_faltivec)
          << Unsupported->getOption().getName() << "please use -mno-altivec";
  }

  Args.AddAllArgs(CmdArgs, options::OPT_v);

  if (Args.getLastArg(options::OPT_H)) {
    CmdArgs.push_back("-H");
    CmdArgs.push_back("-sys-header-deps");
  }
  Args.AddAllArgs(CmdArgs, options::OPT_fshow_skipped_includes);

  if (D.CCPrintHeadersFormat && !D.CCGenDiagnostics) {
    CmdArgs.push_back("-header-include-file");
    CmdArgs.push_back(!D.CCPrintHeadersFilename.empty()
                          ? D.CCPrintHeadersFilename.c_str()
                          : "-");
    CmdArgs.push_back("-sys-header-deps");
    CmdArgs.push_back(Args.MakeArgString(
        "-header-include-format=" +
        std::string(headerIncludeFormatKindToString(D.CCPrintHeadersFormat))));
    CmdArgs.push_back(
        Args.MakeArgString("-header-include-filtering=" +
                           std::string(headerIncludeFilteringKindToString(
                               D.CCPrintHeadersFiltering))));
  }
  Args.AddLastArg(CmdArgs, options::OPT_P);
  Args.AddLastArg(CmdArgs, options::OPT_print_ivar_layout);

  if (D.CCLogDiagnostics && !D.CCGenDiagnostics) {
    CmdArgs.push_back("-diagnostic-log-file");
    CmdArgs.push_back(!D.CCLogDiagnosticsFilename.empty()
                          ? D.CCLogDiagnosticsFilename.c_str()
                          : "-");
  }

  // Give the gen diagnostics more chances to succeed, by avoiding intentional
  // crashes.
  if (D.CCGenDiagnostics)
    CmdArgs.push_back("-disable-pragma-debug-crash");

  // Allow backend to put its diagnostic files in the same place as frontend
  // crash diagnostics files.
  if (Args.hasArg(options::OPT_fcrash_diagnostics_dir)) {
    StringRef Dir = Args.getLastArgValue(options::OPT_fcrash_diagnostics_dir);
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString("-crash-diagnostics-dir=" + Dir));
  }

  bool UseSeparateSections = isUseSeparateSections(Triple);

  if (Args.hasFlag(options::OPT_ffunction_sections,
                   options::OPT_fno_function_sections, UseSeparateSections)) {
    CmdArgs.push_back("-ffunction-sections");
  }

  if (Arg *A = Args.getLastArg(options::OPT_fbasic_block_address_map,
                               options::OPT_fno_basic_block_address_map)) {
    if ((Triple.isX86() || Triple.isAArch64()) && Triple.isOSBinFormatELF()) {
      if (A->getOption().matches(options::OPT_fbasic_block_address_map))
        A->render(Args, CmdArgs);
    } else {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fbasic_block_sections_EQ)) {
    StringRef Val = A->getValue();
    if (Triple.isX86() && Triple.isOSBinFormatELF()) {
      if (Val != "all" && Val != "labels" && Val != "none" &&
          !Val.starts_with("list="))
        D.Diag(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
      else
        A->render(Args, CmdArgs);
    } else if (Triple.isAArch64() && Triple.isOSBinFormatELF()) {
      // "all" is not supported on AArch64 since branch relaxation creates new
      // basic blocks for some cross-section branches.
      if (Val != "labels" && Val != "none" && !Val.starts_with("list="))
        D.Diag(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
      else
        A->render(Args, CmdArgs);
    } else if (Triple.isNVPTX()) {
      // Do not pass the option to the GPU compilation. We still want it enabled
      // for the host-side compilation, so seeing it here is not an error.
    } else if (Val != "none") {
      // =none is allowed everywhere. It's useful for overriding the option
      // and is the same as not specifying the option.
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    }
  }

  bool HasDefaultDataSections = Triple.isOSBinFormatXCOFF();
  if (Args.hasFlag(options::OPT_fdata_sections, options::OPT_fno_data_sections,
                   UseSeparateSections || HasDefaultDataSections)) {
    CmdArgs.push_back("-fdata-sections");
  }

  Args.addOptOutFlag(CmdArgs, options::OPT_funique_section_names,
                     options::OPT_fno_unique_section_names);
  Args.addOptInFlag(CmdArgs, options::OPT_fseparate_named_sections,
                    options::OPT_fno_separate_named_sections);
  Args.addOptInFlag(CmdArgs, options::OPT_funique_internal_linkage_names,
                    options::OPT_fno_unique_internal_linkage_names);
  Args.addOptInFlag(CmdArgs, options::OPT_funique_basic_block_section_names,
                    options::OPT_fno_unique_basic_block_section_names);
  Args.addOptInFlag(CmdArgs, options::OPT_fconvergent_functions,
                    options::OPT_fno_convergent_functions);

  if (Arg *A = Args.getLastArg(options::OPT_fsplit_machine_functions,
                               options::OPT_fno_split_machine_functions)) {
    if (!A->getOption().matches(options::OPT_fno_split_machine_functions)) {
      // This codegen pass is only available on x86 and AArch64 ELF targets.
      if ((Triple.isX86() || Triple.isAArch64()) && Triple.isOSBinFormatELF())
        A->render(Args, CmdArgs);
      else
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << A->getAsString(Args) << TripleStr;
    }
  }

  Args.AddLastArg(CmdArgs, options::OPT_finstrument_functions,
                  options::OPT_finstrument_functions_after_inlining,
                  options::OPT_finstrument_function_entry_bare);

  // NVPTX/AMDGCN doesn't support PGO or coverage. There's no runtime support
  // for sampling, overhead of call arc collection is way too high and there's
  // no way to collect the output.
  if (!Triple.isNVPTX() && !Triple.isAMDGCN())
    addPGOAndCoverageFlags(TC, C, JA, Output, Args, SanitizeArgs, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_fclang_abi_compat_EQ);

  if (getLastProfileSampleUseArg(Args) &&
      Args.hasArg(options::OPT_fsample_profile_use_profi)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-sample-profile-use-profi");
  }

  // Add runtime flag for PS4/PS5 when PGO, coverage, or sanitizers are enabled.
  if (RawTriple.isPS() &&
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    PScpu::addProfileRTArgs(TC, Args, CmdArgs);
    PScpu::addSanitizerArgs(TC, Args, CmdArgs);
  }

  // Pass options for controlling the default header search paths.
  if (Args.hasArg(options::OPT_nostdinc)) {
    CmdArgs.push_back("-nostdsysteminc");
    CmdArgs.push_back("-nobuiltininc");
  } else {
    if (Args.hasArg(options::OPT_nostdlibinc))
      CmdArgs.push_back("-nostdsysteminc");
    Args.AddLastArg(CmdArgs, options::OPT_nostdincxx);
    Args.AddLastArg(CmdArgs, options::OPT_nobuiltininc);
  }

  // Pass the path to compiler resource files.
  CmdArgs.push_back("-resource-dir");
  CmdArgs.push_back(D.ResourceDir.c_str());

  Args.AddLastArg(CmdArgs, options::OPT_working_directory);

  RenderARCMigrateToolOptions(D, Args, CmdArgs);

  // Add preprocessing options like -I, -D, etc. if we are using the
  // preprocessor.
  //
  // FIXME: Support -fpreprocessed
  if (types::getPreprocessedType(InputType) != types::TY_INVALID)
    AddPreprocessingOptions(C, JA, D, Args, CmdArgs, Output, Inputs);

  // Don't warn about "clang -c -DPIC -fPIC test.i" because libtool.m4 assumes
  // that "The compiler can only warn and ignore the option if not recognized".
  // When building with ccache, it will pass -D options to clang even on
  // preprocessed inputs and configure concludes that -fPIC is not supported.
  Args.ClaimAllArgs(options::OPT_D);

  // Manually translate -O4 to -O3; let clang reject others.
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O4)) {
      CmdArgs.push_back("-O3");
      D.Diag(diag::warn_O4_is_O3);
    } else {
      A->render(Args, CmdArgs);
    }
  }

  // Warn about ignored options to clang.
  for (const Arg *A :
       Args.filtered(options::OPT_clang_ignored_gcc_optimization_f_Group)) {
    D.Diag(diag::warn_ignored_gcc_optimization) << A->getAsString(Args);
    A->claim();
  }

  for (const Arg *A :
       Args.filtered(options::OPT_clang_ignored_legacy_options_Group)) {
    D.Diag(diag::warn_ignored_clang_option) << A->getAsString(Args);
    A->claim();
  }

  claimNoWarnArgs(Args);

  Args.AddAllArgs(CmdArgs, options::OPT_R_Group);

  for (const Arg *A :
       Args.filtered(options::OPT_W_Group, options::OPT__SLASH_wd)) {
    A->claim();
    if (A->getOption().getID() == options::OPT__SLASH_wd) {
      unsigned WarningNumber;
      if (StringRef(A->getValue()).getAsInteger(10, WarningNumber)) {
        D.Diag(diag::err_drv_invalid_int_value)
            << A->getAsString(Args) << A->getValue();
        continue;
      }

      if (auto Group = diagGroupFromCLWarningID(WarningNumber)) {
        CmdArgs.push_back(Args.MakeArgString(
            "-Wno-" + DiagnosticIDs::getWarningOptionForGroup(*Group)));
      }
      continue;
    }
    A->render(Args, CmdArgs);
  }

  Args.AddAllArgs(CmdArgs, options::OPT_Wsystem_headers_in_module_EQ);

  if (Args.hasFlag(options::OPT_pedantic, options::OPT_no_pedantic, false))
    CmdArgs.push_back("-pedantic");
  Args.AddLastArg(CmdArgs, options::OPT_pedantic_errors);
  Args.AddLastArg(CmdArgs, options::OPT_w);

  Args.addOptInFlag(CmdArgs, options::OPT_ffixed_point,
                    options::OPT_fno_fixed_point);

  if (Arg *A = Args.getLastArg(options::OPT_fcxx_abi_EQ))
    A->render(Args, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_fexperimental_relative_cxx_abi_vtables,
                  options::OPT_fno_experimental_relative_cxx_abi_vtables);

  Args.AddLastArg(CmdArgs, options::OPT_fexperimental_omit_vtable_rtti,
                  options::OPT_fno_experimental_omit_vtable_rtti);

  if (Arg *A = Args.getLastArg(options::OPT_ffuchsia_api_level_EQ))
    A->render(Args, CmdArgs);

  // Handle -{std, ansi, trigraphs} -- take the last of -{std, ansi}
  // (-ansi is equivalent to -std=c89 or -std=c++98).
  //
  // If a std is supplied, only add -trigraphs if it follows the
  // option.
  bool ImplyVCPPCVer = false;
  bool ImplyVCPPCXXVer = false;
  const Arg *Std = Args.getLastArg(options::OPT_std_EQ, options::OPT_ansi);
  if (Std) {
    if (Std->getOption().matches(options::OPT_ansi))
      if (types::isCXX(InputType))
        CmdArgs.push_back("-std=c++98");
      else
        CmdArgs.push_back("-std=c89");
    else
      Std->render(Args, CmdArgs);

    // If -f(no-)trigraphs appears after the language standard flag, honor it.
    if (Arg *A = Args.getLastArg(options::OPT_std_EQ, options::OPT_ansi,
                                 options::OPT_ftrigraphs,
                                 options::OPT_fno_trigraphs))
      if (A != Std)
        A->render(Args, CmdArgs);
  } else {
    // Honor -std-default.
    //
    // FIXME: Clang doesn't correctly handle -std= when the input language
    // doesn't match. For the time being just ignore this for C++ inputs;
    // eventually we want to do all the standard defaulting here instead of
    // splitting it between the driver and clang -cc1.
    if (!types::isCXX(InputType)) {
      if (!Args.hasArg(options::OPT__SLASH_std)) {
        Args.AddAllArgsTranslated(CmdArgs, options::OPT_std_default_EQ, "-std=",
                                  /*Joined=*/true);
      } else
        ImplyVCPPCVer = true;
    }
    else if (IsWindowsMSVC)
      ImplyVCPPCXXVer = true;

    Args.AddLastArg(CmdArgs, options::OPT_ftrigraphs,
                    options::OPT_fno_trigraphs);
  }

  // GCC's behavior for -Wwrite-strings is a bit strange:
  //  * In C, this "warning flag" changes the types of string literals from
  //    'char[N]' to 'const char[N]', and thus triggers an unrelated warning
  //    for the discarded qualifier.
  //  * In C++, this is just a normal warning flag.
  //
  // Implementing this warning correctly in C is hard, so we follow GCC's
  // behavior for now. FIXME: Directly diagnose uses of a string literal as
  // a non-const char* in C, rather than using this crude hack.
  if (!types::isCXX(InputType)) {
    // FIXME: This should behave just like a warning flag, and thus should also
    // respect -Weverything, -Wno-everything, -Werror=write-strings, and so on.
    Arg *WriteStrings =
        Args.getLastArg(options::OPT_Wwrite_strings,
                        options::OPT_Wno_write_strings, options::OPT_w);
    if (WriteStrings &&
        WriteStrings->getOption().matches(options::OPT_Wwrite_strings))
      CmdArgs.push_back("-fconst-strings");
  }

  // GCC provides a macro definition '__DEPRECATED' when -Wdeprecated is active
  // during C++ compilation, which it is by default. GCC keeps this define even
  // in the presence of '-w', match this behavior bug-for-bug.
  if (types::isCXX(InputType) &&
      Args.hasFlag(options::OPT_Wdeprecated, options::OPT_Wno_deprecated,
                   true)) {
    CmdArgs.push_back("-fdeprecated-macro");
  }

  // Translate GCC's misnamer '-fasm' arguments to '-fgnu-keywords'.
  if (Arg *Asm = Args.getLastArg(options::OPT_fasm, options::OPT_fno_asm)) {
    if (Asm->getOption().matches(options::OPT_fasm))
      CmdArgs.push_back("-fgnu-keywords");
    else
      CmdArgs.push_back("-fno-gnu-keywords");
  }

  if (!ShouldEnableAutolink(Args, TC, JA))
    CmdArgs.push_back("-fno-autolink");

  Args.AddLastArg(CmdArgs, options::OPT_ftemplate_depth_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_foperator_arrow_depth_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fconstexpr_depth_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fconstexpr_steps_EQ);

  Args.AddLastArg(CmdArgs, options::OPT_fexperimental_library);

  if (Args.hasArg(options::OPT_fexperimental_new_constant_interpreter))
    CmdArgs.push_back("-fexperimental-new-constant-interpreter");

  if (Arg *A = Args.getLastArg(options::OPT_fbracket_depth_EQ)) {
    CmdArgs.push_back("-fbracket-depth");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_Wlarge_by_value_copy_EQ,
                               options::OPT_Wlarge_by_value_copy_def)) {
    if (A->getNumValues()) {
      StringRef bytes = A->getValue();
      CmdArgs.push_back(Args.MakeArgString("-Wlarge-by-value-copy=" + bytes));
    } else
      CmdArgs.push_back("-Wlarge-by-value-copy=64"); // default value
  }

  if (Args.hasArg(options::OPT_relocatable_pch))
    CmdArgs.push_back("-relocatable-pch");

  if (const Arg *A = Args.getLastArg(options::OPT_fcf_runtime_abi_EQ)) {
    static const char *kCFABIs[] = {
      "standalone", "objc", "swift", "swift-5.0", "swift-4.2", "swift-4.1",
    };

    if (!llvm::is_contained(kCFABIs, StringRef(A->getValue())))
      D.Diag(diag::err_drv_invalid_cf_runtime_abi) << A->getValue();
    else
      A->render(Args, CmdArgs);
  }

  if (Arg *A = Args.getLastArg(options::OPT_fconstant_string_class_EQ)) {
    CmdArgs.push_back("-fconstant-string-class");
    CmdArgs.push_back(A->getValue());
  }

  if (Arg *A = Args.getLastArg(options::OPT_ftabstop_EQ)) {
    CmdArgs.push_back("-ftabstop");
    CmdArgs.push_back(A->getValue());
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fstack_size_section,
                    options::OPT_fno_stack_size_section);

  if (Args.hasArg(options::OPT_fstack_usage)) {
    CmdArgs.push_back("-stack-usage-file");

    if (Arg *OutputOpt = Args.getLastArg(options::OPT_o)) {
      SmallString<128> OutputFilename(OutputOpt->getValue());
      llvm::sys::path::replace_extension(OutputFilename, "su");
      CmdArgs.push_back(Args.MakeArgString(OutputFilename));
    } else
      CmdArgs.push_back(
          Args.MakeArgString(Twine(getBaseInputStem(Args, Inputs)) + ".su"));
  }

  CmdArgs.push_back("-ferror-limit");
  if (Arg *A = Args.getLastArg(options::OPT_ferror_limit_EQ))
    CmdArgs.push_back(A->getValue());
  else
    CmdArgs.push_back("19");

  Args.AddLastArg(CmdArgs, options::OPT_fconstexpr_backtrace_limit_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fmacro_backtrace_limit_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_ftemplate_backtrace_limit_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fspell_checking_limit_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fcaret_diagnostics_max_lines_EQ);

  // Pass -fmessage-length=.
  unsigned MessageLength = 0;
  if (Arg *A = Args.getLastArg(options::OPT_fmessage_length_EQ)) {
    StringRef V(A->getValue());
    if (V.getAsInteger(0, MessageLength))
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << V << A->getOption().getName();
  } else {
    // If -fmessage-length=N was not specified, determine whether this is a
    // terminal and, if so, implicitly define -fmessage-length appropriately.
    MessageLength = llvm::sys::Process::StandardErrColumns();
  }
  if (MessageLength != 0)
    CmdArgs.push_back(
        Args.MakeArgString("-fmessage-length=" + Twine(MessageLength)));

  if (Arg *A = Args.getLastArg(options::OPT_frandomize_layout_seed_EQ))
    CmdArgs.push_back(
        Args.MakeArgString("-frandomize-layout-seed=" + Twine(A->getValue(0))));

  if (Arg *A = Args.getLastArg(options::OPT_frandomize_layout_seed_file_EQ))
    CmdArgs.push_back(Args.MakeArgString("-frandomize-layout-seed-file=" +
                                         Twine(A->getValue(0))));

  // -fvisibility= and -fvisibility-ms-compat are of a piece.
  if (const Arg *A = Args.getLastArg(options::OPT_fvisibility_EQ,
                                     options::OPT_fvisibility_ms_compat)) {
    if (A->getOption().matches(options::OPT_fvisibility_EQ)) {
      A->render(Args, CmdArgs);
    } else {
      assert(A->getOption().matches(options::OPT_fvisibility_ms_compat));
      CmdArgs.push_back("-fvisibility=hidden");
      CmdArgs.push_back("-ftype-visibility=default");
    }
  } else if (IsOpenMPDevice) {
    // When compiling for the OpenMP device we want protected visibility by
    // default. This prevents the device from accidentally preempting code on
    // the host, makes the system more robust, and improves performance.
    CmdArgs.push_back("-fvisibility=protected");
  }

  // PS4/PS5 process these options in addClangTargetOptions.
  if (!RawTriple.isPS()) {
    if (const Arg *A =
            Args.getLastArg(options::OPT_fvisibility_from_dllstorageclass,
                            options::OPT_fno_visibility_from_dllstorageclass)) {
      if (A->getOption().matches(
              options::OPT_fvisibility_from_dllstorageclass)) {
        CmdArgs.push_back("-fvisibility-from-dllstorageclass");
        Args.AddLastArg(CmdArgs, options::OPT_fvisibility_dllexport_EQ);
        Args.AddLastArg(CmdArgs, options::OPT_fvisibility_nodllstorageclass_EQ);
        Args.AddLastArg(CmdArgs, options::OPT_fvisibility_externs_dllimport_EQ);
        Args.AddLastArg(CmdArgs,
                        options::OPT_fvisibility_externs_nodllstorageclass_EQ);
      }
    }
  }

  if (Args.hasFlag(options::OPT_fvisibility_inlines_hidden,
                    options::OPT_fno_visibility_inlines_hidden, false))
    CmdArgs.push_back("-fvisibility-inlines-hidden");

  Args.AddLastArg(CmdArgs, options::OPT_fvisibility_inlines_hidden_static_local_var,
                           options::OPT_fno_visibility_inlines_hidden_static_local_var);

  // -fvisibility-global-new-delete-hidden is a deprecated spelling of
  // -fvisibility-global-new-delete=force-hidden.
  if (const Arg *A =
          Args.getLastArg(options::OPT_fvisibility_global_new_delete_hidden)) {
    D.Diag(diag::warn_drv_deprecated_arg)
        << A->getAsString(Args) << /*hasReplacement=*/true
        << "-fvisibility-global-new-delete=force-hidden";
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_fvisibility_global_new_delete_EQ,
                          options::OPT_fvisibility_global_new_delete_hidden)) {
    if (A->getOption().matches(options::OPT_fvisibility_global_new_delete_EQ)) {
      A->render(Args, CmdArgs);
    } else {
      assert(A->getOption().matches(
          options::OPT_fvisibility_global_new_delete_hidden));
      CmdArgs.push_back("-fvisibility-global-new-delete=force-hidden");
    }
  }

  Args.AddLastArg(CmdArgs, options::OPT_ftlsmodel_EQ);

  if (Args.hasFlag(options::OPT_fnew_infallible,
                   options::OPT_fno_new_infallible, false))
    CmdArgs.push_back("-fnew-infallible");

  if (Args.hasFlag(options::OPT_fno_operator_names,
                   options::OPT_foperator_names, false))
    CmdArgs.push_back("-fno-operator-names");

  // Forward -f (flag) options which we can pass directly.
  Args.AddLastArg(CmdArgs, options::OPT_femit_all_decls);
  Args.AddLastArg(CmdArgs, options::OPT_fheinous_gnu_extensions);
  Args.AddLastArg(CmdArgs, options::OPT_fdigraphs, options::OPT_fno_digraphs);
  Args.AddLastArg(CmdArgs, options::OPT_fzero_call_used_regs_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fraw_string_literals,
                  options::OPT_fno_raw_string_literals);

  if (Args.hasFlag(options::OPT_femulated_tls, options::OPT_fno_emulated_tls,
                   Triple.hasDefaultEmulatedTLS()))
    CmdArgs.push_back("-femulated-tls");

  Args.addOptInFlag(CmdArgs, options::OPT_fcheck_new,
                    options::OPT_fno_check_new);

  if (Arg *A = Args.getLastArg(options::OPT_fzero_call_used_regs_EQ)) {
    // FIXME: There's no reason for this to be restricted to X86. The backend
    // code needs to be changed to include the appropriate function calls
    // automatically.
    if (!Triple.isX86() && !Triple.isAArch64())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
  }

  // AltiVec-like language extensions aren't relevant for assembling.
  if (!isa<PreprocessJobAction>(JA) || Output.getType() != types::TY_PP_Asm)
    Args.AddLastArg(CmdArgs, options::OPT_fzvector);

  Args.AddLastArg(CmdArgs, options::OPT_fdiagnostics_show_template_tree);
  Args.AddLastArg(CmdArgs, options::OPT_fno_elide_type);

  // Forward flags for OpenMP. We don't do this if the current action is an
  // device offloading action other than OpenMP.
  if (Args.hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                   options::OPT_fno_openmp, false) &&
      (JA.isDeviceOffloading(Action::OFK_None) ||
       JA.isDeviceOffloading(Action::OFK_OpenMP))) {
    switch (D.getOpenMPRuntime(Args)) {
    case Driver::OMPRT_OMP:
    case Driver::OMPRT_IOMP5:
      // Clang can generate useful OpenMP code for these two runtime libraries.
      CmdArgs.push_back("-fopenmp");

      // If no option regarding the use of TLS in OpenMP codegeneration is
      // given, decide a default based on the target. Otherwise rely on the
      // options and pass the right information to the frontend.
      if (!Args.hasFlag(options::OPT_fopenmp_use_tls,
                        options::OPT_fnoopenmp_use_tls, /*Default=*/true))
        CmdArgs.push_back("-fnoopenmp-use-tls");
      Args.AddLastArg(CmdArgs, options::OPT_fopenmp_simd,
                      options::OPT_fno_openmp_simd);
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_enable_irbuilder);
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_version_EQ);
      if (!Args.hasFlag(options::OPT_fopenmp_extensions,
                        options::OPT_fno_openmp_extensions, /*Default=*/true))
        CmdArgs.push_back("-fno-openmp-extensions");
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_cuda_number_of_sm_EQ);
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_cuda_blocks_per_sm_EQ);
      Args.AddAllArgs(CmdArgs,
                      options::OPT_fopenmp_cuda_teams_reduction_recs_num_EQ);
      if (Args.hasFlag(options::OPT_fopenmp_optimistic_collapse,
                       options::OPT_fno_openmp_optimistic_collapse,
                       /*Default=*/false))
        CmdArgs.push_back("-fopenmp-optimistic-collapse");

      // When in OpenMP offloading mode with NVPTX target, forward
      // cuda-mode flag
      if (Args.hasFlag(options::OPT_fopenmp_cuda_mode,
                       options::OPT_fno_openmp_cuda_mode, /*Default=*/false))
        CmdArgs.push_back("-fopenmp-cuda-mode");

      // When in OpenMP offloading mode, enable debugging on the device.
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_target_debug_EQ);
      if (Args.hasFlag(options::OPT_fopenmp_target_debug,
                       options::OPT_fno_openmp_target_debug, /*Default=*/false))
        CmdArgs.push_back("-fopenmp-target-debug");

      // When in OpenMP offloading mode, forward assumptions information about
      // thread and team counts in the device.
      if (Args.hasFlag(options::OPT_fopenmp_assume_teams_oversubscription,
                       options::OPT_fno_openmp_assume_teams_oversubscription,
                       /*Default=*/false))
        CmdArgs.push_back("-fopenmp-assume-teams-oversubscription");
      if (Args.hasFlag(options::OPT_fopenmp_assume_threads_oversubscription,
                       options::OPT_fno_openmp_assume_threads_oversubscription,
                       /*Default=*/false))
        CmdArgs.push_back("-fopenmp-assume-threads-oversubscription");
      if (Args.hasArg(options::OPT_fopenmp_assume_no_thread_state))
        CmdArgs.push_back("-fopenmp-assume-no-thread-state");
      if (Args.hasArg(options::OPT_fopenmp_assume_no_nested_parallelism))
        CmdArgs.push_back("-fopenmp-assume-no-nested-parallelism");
      if (Args.hasArg(options::OPT_fopenmp_offload_mandatory))
        CmdArgs.push_back("-fopenmp-offload-mandatory");
      if (Args.hasArg(options::OPT_fopenmp_force_usm))
        CmdArgs.push_back("-fopenmp-force-usm");
      break;
    default:
      // By default, if Clang doesn't know how to generate useful OpenMP code
      // for a specific runtime library, we just don't pass the '-fopenmp' flag
      // down to the actual compilation.
      // FIXME: It would be better to have a mode which *only* omits IR
      // generation based on the OpenMP support so that we get consistent
      // semantic analysis, etc.
      break;
    }
  } else {
    Args.AddLastArg(CmdArgs, options::OPT_fopenmp_simd,
                    options::OPT_fno_openmp_simd);
    Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_version_EQ);
    Args.addOptOutFlag(CmdArgs, options::OPT_fopenmp_extensions,
                       options::OPT_fno_openmp_extensions);
  }

  // Forward the new driver to change offloading code generation.
  if (Args.hasFlag(options::OPT_offload_new_driver,
                   options::OPT_no_offload_new_driver, false))
    CmdArgs.push_back("--offload-new-driver");

  SanitizeArgs.addArgs(TC, Args, CmdArgs, InputType);

  const XRayArgs &XRay = TC.getXRayArgs();
  XRay.addArgs(TC, Args, CmdArgs, InputType);

  for (const auto &Filename :
       Args.getAllArgValues(options::OPT_fprofile_list_EQ)) {
    if (D.getVFS().exists(Filename))
      CmdArgs.push_back(Args.MakeArgString("-fprofile-list=" + Filename));
    else
      D.Diag(clang::diag::err_drv_no_such_file) << Filename;
  }

  if (Arg *A = Args.getLastArg(options::OPT_fpatchable_function_entry_EQ)) {
    StringRef S0 = A->getValue(), S = S0;
    unsigned Size, Offset = 0;
    if (!Triple.isAArch64() && !Triple.isLoongArch() && !Triple.isRISCV() &&
        !Triple.isX86() &&
        !(!Triple.isOSAIX() && (Triple.getArch() == llvm::Triple::ppc ||
                                Triple.getArch() == llvm::Triple::ppc64)))
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    else if (S.consumeInteger(10, Size) ||
             (!S.empty() && (!S.consume_front(",") ||
                             S.consumeInteger(10, Offset) || !S.empty())))
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << S0 << A->getOption().getName();
    else if (Size < Offset)
      D.Diag(diag::err_drv_unsupported_fpatchable_function_entry_argument);
    else {
      CmdArgs.push_back(Args.MakeArgString(A->getSpelling() + Twine(Size)));
      CmdArgs.push_back(Args.MakeArgString(
          "-fpatchable-function-entry-offset=" + Twine(Offset)));
    }
  }

  Args.AddLastArg(CmdArgs, options::OPT_fms_hotpatch);

  if (TC.SupportsProfiling()) {
    Args.AddLastArg(CmdArgs, options::OPT_pg);

    llvm::Triple::ArchType Arch = TC.getArch();
    if (Arg *A = Args.getLastArg(options::OPT_mfentry)) {
      if (Arch == llvm::Triple::systemz || TC.getTriple().isX86())
        A->render(Args, CmdArgs);
      else
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << A->getAsString(Args) << TripleStr;
    }
    if (Arg *A = Args.getLastArg(options::OPT_mnop_mcount)) {
      if (Arch == llvm::Triple::systemz)
        A->render(Args, CmdArgs);
      else
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << A->getAsString(Args) << TripleStr;
    }
    if (Arg *A = Args.getLastArg(options::OPT_mrecord_mcount)) {
      if (Arch == llvm::Triple::systemz)
        A->render(Args, CmdArgs);
      else
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << A->getAsString(Args) << TripleStr;
    }
  }

  if (Arg *A = Args.getLastArgNoClaim(options::OPT_pg)) {
    if (TC.getTriple().isOSzOS()) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    }
  }
  if (Arg *A = Args.getLastArgNoClaim(options::OPT_p)) {
    if (!(TC.getTriple().isOSAIX() || TC.getTriple().isOSOpenBSD())) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << TripleStr;
    }
  }
  if (Arg *A = Args.getLastArgNoClaim(options::OPT_p, options::OPT_pg)) {
    if (A->getOption().matches(options::OPT_p)) {
      A->claim();
      if (TC.getTriple().isOSAIX() && !Args.hasArgNoClaim(options::OPT_pg))
        CmdArgs.push_back("-pg");
    }
  }

  // Reject AIX-specific link options on other targets.
  if (!TC.getTriple().isOSAIX()) {
    for (const Arg *A : Args.filtered(options::OPT_b, options::OPT_K,
                                      options::OPT_mxcoff_build_id_EQ)) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << TripleStr;
    }
  }

  if (Args.getLastArg(options::OPT_fapple_kext) ||
      (Args.hasArg(options::OPT_mkernel) && types::isCXX(InputType)))
    CmdArgs.push_back("-fapple-kext");

  Args.AddLastArg(CmdArgs, options::OPT_altivec_src_compat);
  Args.AddLastArg(CmdArgs, options::OPT_flax_vector_conversions_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_fobjc_sender_dependent_dispatch);
  Args.AddLastArg(CmdArgs, options::OPT_fdiagnostics_print_source_range_info);
  Args.AddLastArg(CmdArgs, options::OPT_fdiagnostics_parseable_fixits);
  Args.AddLastArg(CmdArgs, options::OPT_ftime_report);
  Args.AddLastArg(CmdArgs, options::OPT_ftime_report_EQ);
  Args.AddLastArg(CmdArgs, options::OPT_ftrapv);
  Args.AddLastArg(CmdArgs, options::OPT_malign_double);
  Args.AddLastArg(CmdArgs, options::OPT_fno_temp_file);

  if (const char *Name = C.getTimeTraceFile(&JA)) {
    CmdArgs.push_back(Args.MakeArgString("-ftime-trace=" + Twine(Name)));
    Args.AddLastArg(CmdArgs, options::OPT_ftime_trace_granularity_EQ);
    Args.AddLastArg(CmdArgs, options::OPT_ftime_trace_verbose);
  }

  if (Arg *A = Args.getLastArg(options::OPT_ftrapv_handler_EQ)) {
    CmdArgs.push_back("-ftrapv-handler");
    CmdArgs.push_back(A->getValue());
  }

  Args.AddLastArg(CmdArgs, options::OPT_ftrap_function_EQ);

  // -fno-strict-overflow implies -fwrapv if it isn't disabled, but
  // -fstrict-overflow won't turn off an explicitly enabled -fwrapv.
  if (Arg *A = Args.getLastArg(options::OPT_fwrapv, options::OPT_fno_wrapv)) {
    if (A->getOption().matches(options::OPT_fwrapv))
      CmdArgs.push_back("-fwrapv");
  } else if (Arg *A = Args.getLastArg(options::OPT_fstrict_overflow,
                                      options::OPT_fno_strict_overflow)) {
    if (A->getOption().matches(options::OPT_fno_strict_overflow))
      CmdArgs.push_back("-fwrapv");
  } else if (getToolChain().getTriple().isOSOpenBSD())
    CmdArgs.push_back("-fwrapv");

  Args.AddLastArg(CmdArgs, options::OPT_ffinite_loops,
                  options::OPT_fno_finite_loops);

  Args.AddLastArg(CmdArgs, options::OPT_fwritable_strings);
  Args.AddLastArg(CmdArgs, options::OPT_funroll_loops,
                  options::OPT_fno_unroll_loops);

  Args.AddLastArg(CmdArgs, options::OPT_fstrict_flex_arrays_EQ);

  Args.AddLastArg(CmdArgs, options::OPT_pthread);

  Args.addOptInFlag(CmdArgs, options::OPT_mspeculative_load_hardening,
                    options::OPT_mno_speculative_load_hardening);

  // -ret-protector
  unsigned RetProtector = 1;
  if (Arg *A = Args.getLastArg(options::OPT_fno_ret_protector,
        options::OPT_fret_protector)) {
    if (A->getOption().matches(options::OPT_fno_ret_protector))
      RetProtector = 0;
    else if (A->getOption().matches(options::OPT_fret_protector))
      RetProtector = 1;
  }

  if (RetProtector &&
      ((getToolChain().getArch() == llvm::Triple::x86_64) ||
       (getToolChain().getArch() == llvm::Triple::mips64) ||
       (getToolChain().getArch() == llvm::Triple::mips64el) ||
       (getToolChain().getArch() == llvm::Triple::ppc) ||
       (getToolChain().getArch() == llvm::Triple::ppc64) ||
       (getToolChain().getArch() == llvm::Triple::ppc64le) ||
       (getToolChain().getArch() == llvm::Triple::aarch64)) &&
      !Args.hasArg(options::OPT_fno_stack_protector) &&
      !Args.hasArg(options::OPT_pg)) {
    CmdArgs.push_back(Args.MakeArgString("-D_RET_PROTECTOR"));
    CmdArgs.push_back(Args.MakeArgString("-ret-protector"));
    // Consume the stack protector arguments to prevent warning
    Args.getLastArg(options::OPT_fstack_protector_all,
        options::OPT_fstack_protector_strong,
        options::OPT_fstack_protector,
        options::OPT__param); // ssp-buffer-size
  } else {
    // If we're not using retguard, then do the usual stack protector
    RenderSSPOptions(D, TC, Args, CmdArgs, KernelOrKext);
  }

  // -fixup-gadgets
  if (Arg *A = Args.getLastArg(options::OPT_fno_fixup_gadgets,
                               options::OPT_ffixup_gadgets)) {
    CmdArgs.push_back(Args.MakeArgString(Twine("-mllvm")));
    if (A->getOption().matches(options::OPT_fno_fixup_gadgets))
      CmdArgs.push_back(Args.MakeArgString(Twine("-x86-fixup-gadgets=false")));
    else if (A->getOption().matches(options::OPT_ffixup_gadgets))
      CmdArgs.push_back(Args.MakeArgString(Twine("-x86-fixup-gadgets=true")));
  }

  // -ret-clean
  if (Arg *A = Args.getLastArg(options::OPT_fno_ret_clean,
                               options::OPT_fret_clean)) {
    CmdArgs.push_back(Args.MakeArgString(Twine("-mllvm")));
    if (A->getOption().matches(options::OPT_fno_ret_clean))
      CmdArgs.push_back(Args.MakeArgString(Twine("-x86-ret-clean=false")));
    else if (A->getOption().matches(options::OPT_fret_clean))
      CmdArgs.push_back(Args.MakeArgString(Twine("-x86-ret-clean=true")));
  }

  RenderSCPOptions(TC, Args, CmdArgs);
  RenderTrivialAutoVarInitOptions(D, TC, Args, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_fswift_async_fp_EQ);

  Args.addOptInFlag(CmdArgs, options::OPT_mstackrealign,
                    options::OPT_mno_stackrealign);

  if (Args.hasArg(options::OPT_mstack_alignment)) {
    StringRef alignment = Args.getLastArgValue(options::OPT_mstack_alignment);
    CmdArgs.push_back(Args.MakeArgString("-mstack-alignment=" + alignment));
  }

  if (Args.hasArg(options::OPT_mstack_probe_size)) {
    StringRef Size = Args.getLastArgValue(options::OPT_mstack_probe_size);

    if (!Size.empty())
      CmdArgs.push_back(Args.MakeArgString("-mstack-probe-size=" + Size));
    else
      CmdArgs.push_back("-mstack-probe-size=0");
  }

  Args.addOptOutFlag(CmdArgs, options::OPT_mstack_arg_probe,
                     options::OPT_mno_stack_arg_probe);

  if (Arg *A = Args.getLastArg(options::OPT_mrestrict_it,
                               options::OPT_mno_restrict_it)) {
    if (A->getOption().matches(options::OPT_mrestrict_it)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-arm-restrict-it");
    } else {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-arm-default-it");
    }
  }

  // Forward -cl options to -cc1
  RenderOpenCLOptions(Args, CmdArgs, InputType);

  // Forward hlsl options to -cc1
  RenderHLSLOptions(Args, CmdArgs, InputType);

  // Forward OpenACC options to -cc1
  RenderOpenACCOptions(D, Args, CmdArgs, InputType);

  if (IsHIP) {
    if (Args.hasFlag(options::OPT_fhip_new_launch_api,
                     options::OPT_fno_hip_new_launch_api, true))
      CmdArgs.push_back("-fhip-new-launch-api");
    Args.addOptInFlag(CmdArgs, options::OPT_fgpu_allow_device_init,
                      options::OPT_fno_gpu_allow_device_init);
    Args.AddLastArg(CmdArgs, options::OPT_hipstdpar);
    Args.AddLastArg(CmdArgs, options::OPT_hipstdpar_interpose_alloc);
    Args.addOptInFlag(CmdArgs, options::OPT_fhip_kernel_arg_name,
                      options::OPT_fno_hip_kernel_arg_name);
  }

  if (IsCuda || IsHIP) {
    if (IsRDCMode)
      CmdArgs.push_back("-fgpu-rdc");
    Args.addOptInFlag(CmdArgs, options::OPT_fgpu_defer_diag,
                      options::OPT_fno_gpu_defer_diag);
    if (Args.hasFlag(options::OPT_fgpu_exclude_wrong_side_overloads,
                     options::OPT_fno_gpu_exclude_wrong_side_overloads,
                     false)) {
      CmdArgs.push_back("-fgpu-exclude-wrong-side-overloads");
      CmdArgs.push_back("-fgpu-defer-diag");
    }
  }

  // Forward -nogpulib to -cc1.
  if (Args.hasArg(options::OPT_nogpulib))
    CmdArgs.push_back("-nogpulib");

  if (Arg *A = Args.getLastArg(options::OPT_fcf_protection_EQ)) {
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-fcf-protection=") + A->getValue()));
  } else if (Triple.isOSOpenBSD() && Triple.getArch() == llvm::Triple::x86_64) {
    // Emit IBT endbr64 instructions by default
    CmdArgs.push_back("-fcf-protection=branch");
    // jump-table can generate indirect jumps, which are not permitted
    CmdArgs.push_back("-fno-jump-tables");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mfunction_return_EQ))
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-mfunction-return=") + A->getValue()));

  Args.AddLastArg(CmdArgs, options::OPT_mindirect_branch_cs_prefix);

  // Forward -f options with positive and negative forms; we translate these by
  // hand.  Do not propagate PGO options to the GPU-side compilations as the
  // profile info is for the host-side compilation only.
  if (!(IsCudaDevice || IsHIPDevice)) {
    if (Arg *A = getLastProfileSampleUseArg(Args)) {
      auto *PGOArg = Args.getLastArg(
          options::OPT_fprofile_generate, options::OPT_fprofile_generate_EQ,
          options::OPT_fcs_profile_generate,
          options::OPT_fcs_profile_generate_EQ, options::OPT_fprofile_use,
          options::OPT_fprofile_use_EQ);
      if (PGOArg)
        D.Diag(diag::err_drv_argument_not_allowed_with)
            << "SampleUse with PGO options";

      StringRef fname = A->getValue();
      if (!llvm::sys::fs::exists(fname))
        D.Diag(diag::err_drv_no_such_file) << fname;
      else
        A->render(Args, CmdArgs);
    }
    Args.AddLastArg(CmdArgs, options::OPT_fprofile_remapping_file_EQ);

    if (Args.hasFlag(options::OPT_fpseudo_probe_for_profiling,
                     options::OPT_fno_pseudo_probe_for_profiling, false)) {
      CmdArgs.push_back("-fpseudo-probe-for-profiling");
      // Enforce -funique-internal-linkage-names if it's not explicitly turned
      // off.
      if (Args.hasFlag(options::OPT_funique_internal_linkage_names,
                       options::OPT_fno_unique_internal_linkage_names, true))
        CmdArgs.push_back("-funique-internal-linkage-names");
    }
  }
  RenderBuiltinOptions(TC, RawTriple, Args, CmdArgs);

  Args.addOptOutFlag(CmdArgs, options::OPT_fassume_sane_operator_new,
                     options::OPT_fno_assume_sane_operator_new);

  if (Args.hasFlag(options::OPT_fapinotes, options::OPT_fno_apinotes, false))
    CmdArgs.push_back("-fapinotes");
  if (Args.hasFlag(options::OPT_fapinotes_modules,
                   options::OPT_fno_apinotes_modules, false))
    CmdArgs.push_back("-fapinotes-modules");
  Args.AddLastArg(CmdArgs, options::OPT_fapinotes_swift_version);

  // -fblocks=0 is default.
  if (Args.hasFlag(options::OPT_fblocks, options::OPT_fno_blocks,
                   TC.IsBlocksDefault()) ||
      (Args.hasArg(options::OPT_fgnu_runtime) &&
       Args.hasArg(options::OPT_fobjc_nonfragile_abi) &&
       !Args.hasArg(options::OPT_fno_blocks))) {
    CmdArgs.push_back("-fblocks");

    if (!Args.hasArg(options::OPT_fgnu_runtime) && !TC.hasBlocksRuntime())
      CmdArgs.push_back("-fblocks-runtime-optional");
  }

  // -fencode-extended-block-signature=1 is default.
  if (TC.IsEncodeExtendedBlockSignatureDefault())
    CmdArgs.push_back("-fencode-extended-block-signature");

  if (Args.hasFlag(options::OPT_fcoro_aligned_allocation,
                   options::OPT_fno_coro_aligned_allocation, false) &&
      types::isCXX(InputType))
    CmdArgs.push_back("-fcoro-aligned-allocation");

  Args.AddLastArg(CmdArgs, options::OPT_fdouble_square_bracket_attributes,
                  options::OPT_fno_double_square_bracket_attributes);

  Args.addOptOutFlag(CmdArgs, options::OPT_faccess_control,
                     options::OPT_fno_access_control);
  Args.addOptOutFlag(CmdArgs, options::OPT_felide_constructors,
                     options::OPT_fno_elide_constructors);

  ToolChain::RTTIMode RTTIMode = TC.getRTTIMode();

  if (KernelOrKext || (types::isCXX(InputType) &&
                       (RTTIMode == ToolChain::RM_Disabled)))
    CmdArgs.push_back("-fno-rtti");

  // -fshort-enums=0 is default for all architectures except Hexagon and z/OS.
  if (Args.hasFlag(options::OPT_fshort_enums, options::OPT_fno_short_enums,
                   TC.getArch() == llvm::Triple::hexagon || Triple.isOSzOS()))
    CmdArgs.push_back("-fshort-enums");

  RenderCharacterOptions(Args, AuxTriple ? *AuxTriple : RawTriple, CmdArgs);

  // -fuse-cxa-atexit is default.
  if (!Args.hasFlag(
          options::OPT_fuse_cxa_atexit, options::OPT_fno_use_cxa_atexit,
          !RawTriple.isOSAIX() && !RawTriple.isOSWindows() &&
              ((RawTriple.getVendor() != llvm::Triple::MipsTechnologies) ||
               RawTriple.hasEnvironment())) ||
      KernelOrKext)
    CmdArgs.push_back("-fno-use-cxa-atexit");

  if (Args.hasFlag(options::OPT_fregister_global_dtors_with_atexit,
                   options::OPT_fno_register_global_dtors_with_atexit,
                   RawTriple.isOSDarwin() && !KernelOrKext))
    CmdArgs.push_back("-fregister-global-dtors-with-atexit");

  Args.addOptInFlag(CmdArgs, options::OPT_fuse_line_directives,
                    options::OPT_fno_use_line_directives);

  // -fno-minimize-whitespace is default.
  if (Args.hasFlag(options::OPT_fminimize_whitespace,
                   options::OPT_fno_minimize_whitespace, false)) {
    types::ID InputType = Inputs[0].getType();
    if (!isDerivedFromC(InputType))
      D.Diag(diag::err_drv_opt_unsupported_input_type)
          << "-fminimize-whitespace" << types::getTypeName(InputType);
    CmdArgs.push_back("-fminimize-whitespace");
  }

  // -fno-keep-system-includes is default.
  if (Args.hasFlag(options::OPT_fkeep_system_includes,
                   options::OPT_fno_keep_system_includes, false)) {
    types::ID InputType = Inputs[0].getType();
    if (!isDerivedFromC(InputType))
      D.Diag(diag::err_drv_opt_unsupported_input_type)
          << "-fkeep-system-includes" << types::getTypeName(InputType);
    CmdArgs.push_back("-fkeep-system-includes");
  }

  // -fms-extensions=0 is default.
  if (Args.hasFlag(options::OPT_fms_extensions, options::OPT_fno_ms_extensions,
                   IsWindowsMSVC))
    CmdArgs.push_back("-fms-extensions");

  // -fms-compatibility=0 is default.
  bool IsMSVCCompat = Args.hasFlag(
      options::OPT_fms_compatibility, options::OPT_fno_ms_compatibility,
      (IsWindowsMSVC && Args.hasFlag(options::OPT_fms_extensions,
                                     options::OPT_fno_ms_extensions, true)));
  if (IsMSVCCompat) {
    CmdArgs.push_back("-fms-compatibility");
    if (!types::isCXX(Input.getType()) &&
        Args.hasArg(options::OPT_fms_define_stdc))
      CmdArgs.push_back("-fms-define-stdc");
  }

  if (Triple.isWindowsMSVCEnvironment() && !D.IsCLMode() &&
      Args.hasArg(options::OPT_fms_runtime_lib_EQ))
    ProcessVSRuntimeLibrary(getToolChain(), Args, CmdArgs);

  // Handle -fgcc-version, if present.
  VersionTuple GNUCVer;
  if (Arg *A = Args.getLastArg(options::OPT_fgnuc_version_EQ)) {
    // Check that the version has 1 to 3 components and the minor and patch
    // versions fit in two decimal digits.
    StringRef Val = A->getValue();
    Val = Val.empty() ? "0" : Val; // Treat "" as 0 or disable.
    bool Invalid = GNUCVer.tryParse(Val);
    unsigned Minor = GNUCVer.getMinor().value_or(0);
    unsigned Patch = GNUCVer.getSubminor().value_or(0);
    if (Invalid || GNUCVer.getBuild() || Minor >= 100 || Patch >= 100) {
      D.Diag(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    }
  } else if (!IsMSVCCompat) {
    // Imitate GCC 4.2.1 by default if -fms-compatibility is not in effect.
    GNUCVer = VersionTuple(4, 2, 1);
  }
  if (!GNUCVer.empty()) {
    CmdArgs.push_back(
        Args.MakeArgString("-fgnuc-version=" + GNUCVer.getAsString()));
  }

  VersionTuple MSVT = TC.computeMSVCVersion(&D, Args);
  if (!MSVT.empty())
    CmdArgs.push_back(
        Args.MakeArgString("-fms-compatibility-version=" + MSVT.getAsString()));

  bool IsMSVC2015Compatible = MSVT.getMajor() >= 19;
  if (ImplyVCPPCVer) {
    StringRef LanguageStandard;
    if (const Arg *StdArg = Args.getLastArg(options::OPT__SLASH_std)) {
      Std = StdArg;
      LanguageStandard = llvm::StringSwitch<StringRef>(StdArg->getValue())
                             .Case("c11", "-std=c11")
                             .Case("c17", "-std=c17")
                             .Default("");
      if (LanguageStandard.empty())
        D.Diag(clang::diag::warn_drv_unused_argument)
            << StdArg->getAsString(Args);
    }
    CmdArgs.push_back(LanguageStandard.data());
  }
  if (ImplyVCPPCXXVer) {
    StringRef LanguageStandard;
    if (const Arg *StdArg = Args.getLastArg(options::OPT__SLASH_std)) {
      Std = StdArg;
      LanguageStandard = llvm::StringSwitch<StringRef>(StdArg->getValue())
                             .Case("c++14", "-std=c++14")
                             .Case("c++17", "-std=c++17")
                             .Case("c++20", "-std=c++20")
                             // TODO add c++23 and c++26 when MSVC supports it.
                             .Case("c++latest", "-std=c++26")
                             .Default("");
      if (LanguageStandard.empty())
        D.Diag(clang::diag::warn_drv_unused_argument)
            << StdArg->getAsString(Args);
    }

    if (LanguageStandard.empty()) {
      if (IsMSVC2015Compatible)
        LanguageStandard = "-std=c++14";
      else
        LanguageStandard = "-std=c++11";
    }

    CmdArgs.push_back(LanguageStandard.data());
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fborland_extensions,
                    options::OPT_fno_borland_extensions);

  // -fno-declspec is default, except for PS4/PS5.
  if (Args.hasFlag(options::OPT_fdeclspec, options::OPT_fno_declspec,
                   RawTriple.isPS()))
    CmdArgs.push_back("-fdeclspec");
  else if (Args.hasArg(options::OPT_fno_declspec))
    CmdArgs.push_back("-fno-declspec"); // Explicitly disabling __declspec.

  // -fthreadsafe-static is default, except for MSVC compatibility versions less
  // than 19.
  if (!Args.hasFlag(options::OPT_fthreadsafe_statics,
                    options::OPT_fno_threadsafe_statics,
                    !types::isOpenCL(InputType) &&
                        (!IsWindowsMSVC || IsMSVC2015Compatible)))
    CmdArgs.push_back("-fno-threadsafe-statics");

  // Add -fno-assumptions, if it was specified.
  if (!Args.hasFlag(options::OPT_fassumptions, options::OPT_fno_assumptions,
                    true))
    CmdArgs.push_back("-fno-assumptions");

  // -fgnu-keywords default varies depending on language; only pass if
  // specified.
  Args.AddLastArg(CmdArgs, options::OPT_fgnu_keywords,
                  options::OPT_fno_gnu_keywords);

  Args.addOptInFlag(CmdArgs, options::OPT_fgnu89_inline,
                    options::OPT_fno_gnu89_inline);

  const Arg *InlineArg = Args.getLastArg(options::OPT_finline_functions,
                                         options::OPT_finline_hint_functions,
                                         options::OPT_fno_inline_functions);
  if (Arg *A = Args.getLastArg(options::OPT_finline, options::OPT_fno_inline)) {
    if (A->getOption().matches(options::OPT_fno_inline))
      A->render(Args, CmdArgs);
  } else if (InlineArg) {
    InlineArg->render(Args, CmdArgs);
  }

  Args.AddLastArg(CmdArgs, options::OPT_finline_max_stacksize_EQ);

  // FIXME: Find a better way to determine whether we are in C++20.
  bool HaveCxx20 =
      Std &&
      (Std->containsValue("c++2a") || Std->containsValue("gnu++2a") ||
       Std->containsValue("c++20") || Std->containsValue("gnu++20") ||
       Std->containsValue("c++2b") || Std->containsValue("gnu++2b") ||
       Std->containsValue("c++23") || Std->containsValue("gnu++23") ||
       Std->containsValue("c++2c") || Std->containsValue("gnu++2c") ||
       Std->containsValue("c++26") || Std->containsValue("gnu++26") ||
       Std->containsValue("c++latest") || Std->containsValue("gnu++latest"));
  bool HaveModules =
      RenderModulesOptions(C, D, Args, Input, Output, HaveCxx20, CmdArgs);

  // -fdelayed-template-parsing is default when targeting MSVC.
  // Many old Windows SDK versions require this to parse.
  //
  // According to
  // https://learn.microsoft.com/en-us/cpp/build/reference/permissive-standards-conformance?view=msvc-170,
  // MSVC actually defaults to -fno-delayed-template-parsing (/Zc:twoPhase-
  // with MSVC CLI) if using C++20. So we match the behavior with MSVC here to
  // not enable -fdelayed-template-parsing by default after C++20.
  //
  // FIXME: Given -fdelayed-template-parsing is a source of bugs, we should be
  // able to disable this by default at some point.
  if (Args.hasFlag(options::OPT_fdelayed_template_parsing,
                   options::OPT_fno_delayed_template_parsing,
                   IsWindowsMSVC && !HaveCxx20)) {
    if (HaveCxx20)
      D.Diag(clang::diag::warn_drv_delayed_template_parsing_after_cxx20);

    CmdArgs.push_back("-fdelayed-template-parsing");
  }

  if (Args.hasFlag(options::OPT_fpch_validate_input_files_content,
                   options::OPT_fno_pch_validate_input_files_content, false))
    CmdArgs.push_back("-fvalidate-ast-input-files-content");
  if (Args.hasFlag(options::OPT_fpch_instantiate_templates,
                   options::OPT_fno_pch_instantiate_templates, false))
    CmdArgs.push_back("-fpch-instantiate-templates");
  if (Args.hasFlag(options::OPT_fpch_codegen, options::OPT_fno_pch_codegen,
                   false))
    CmdArgs.push_back("-fmodules-codegen");
  if (Args.hasFlag(options::OPT_fpch_debuginfo, options::OPT_fno_pch_debuginfo,
                   false))
    CmdArgs.push_back("-fmodules-debuginfo");

  ObjCRuntime Runtime = AddObjCRuntimeArgs(Args, Inputs, CmdArgs, rewriteKind);
  RenderObjCOptions(TC, D, RawTriple, Args, Runtime, rewriteKind != RK_None,
                    Input, CmdArgs);

  if (types::isObjC(Input.getType()) &&
      Args.hasFlag(options::OPT_fobjc_encode_cxx_class_template_spec,
                   options::OPT_fno_objc_encode_cxx_class_template_spec,
                   !Runtime.isNeXTFamily()))
    CmdArgs.push_back("-fobjc-encode-cxx-class-template-spec");

  if (Args.hasFlag(options::OPT_fapplication_extension,
                   options::OPT_fno_application_extension, false))
    CmdArgs.push_back("-fapplication-extension");

  // Handle GCC-style exception args.
  bool EH = false;
  if (!C.getDriver().IsCLMode())
    EH = addExceptionArgs(Args, InputType, TC, KernelOrKext, Runtime, CmdArgs);

  // Handle exception personalities
  Arg *A = Args.getLastArg(
      options::OPT_fsjlj_exceptions, options::OPT_fseh_exceptions,
      options::OPT_fdwarf_exceptions, options::OPT_fwasm_exceptions);
  if (A) {
    const Option &Opt = A->getOption();
    if (Opt.matches(options::OPT_fsjlj_exceptions))
      CmdArgs.push_back("-exception-model=sjlj");
    if (Opt.matches(options::OPT_fseh_exceptions))
      CmdArgs.push_back("-exception-model=seh");
    if (Opt.matches(options::OPT_fdwarf_exceptions))
      CmdArgs.push_back("-exception-model=dwarf");
    if (Opt.matches(options::OPT_fwasm_exceptions))
      CmdArgs.push_back("-exception-model=wasm");
  } else {
    switch (TC.GetExceptionModel(Args)) {
    default:
      break;
    case llvm::ExceptionHandling::DwarfCFI:
      CmdArgs.push_back("-exception-model=dwarf");
      break;
    case llvm::ExceptionHandling::SjLj:
      CmdArgs.push_back("-exception-model=sjlj");
      break;
    case llvm::ExceptionHandling::WinEH:
      CmdArgs.push_back("-exception-model=seh");
      break;
    }
  }

  // C++ "sane" operator new.
  Args.addOptOutFlag(CmdArgs, options::OPT_fassume_sane_operator_new,
                     options::OPT_fno_assume_sane_operator_new);

  // -fassume-unique-vtables is on by default.
  Args.addOptOutFlag(CmdArgs, options::OPT_fassume_unique_vtables,
                     options::OPT_fno_assume_unique_vtables);

  // -frelaxed-template-template-args is deprecated.
  if (Arg *A =
          Args.getLastArg(options::OPT_frelaxed_template_template_args,
                          options::OPT_fno_relaxed_template_template_args)) {
    if (A->getOption().matches(
            options::OPT_fno_relaxed_template_template_args)) {
      D.Diag(diag::warn_drv_deprecated_arg_no_relaxed_template_template_args);
      CmdArgs.push_back("-fno-relaxed-template-template-args");
    } else {
      D.Diag(diag::warn_drv_deprecated_arg)
          << A->getAsString(Args) << /*hasReplacement=*/false;
    }
  }

  // -fsized-deallocation is on by default in C++14 onwards and otherwise off
  // by default.
  Args.addLastArg(CmdArgs, options::OPT_fsized_deallocation,
                  options::OPT_fno_sized_deallocation);

  // -faligned-allocation is on by default in C++17 onwards and otherwise off
  // by default.
  if (Arg *A = Args.getLastArg(options::OPT_faligned_allocation,
                               options::OPT_fno_aligned_allocation,
                               options::OPT_faligned_new_EQ)) {
    if (A->getOption().matches(options::OPT_fno_aligned_allocation))
      CmdArgs.push_back("-fno-aligned-allocation");
    else
      CmdArgs.push_back("-faligned-allocation");
  }

  // The default new alignment can be specified using a dedicated option or via
  // a GCC-compatible option that also turns on aligned allocation.
  if (Arg *A = Args.getLastArg(options::OPT_fnew_alignment_EQ,
                               options::OPT_faligned_new_EQ))
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-fnew-alignment=") + A->getValue()));

  // -fconstant-cfstrings is default, and may be subject to argument translation
  // on Darwin.
  if (!Args.hasFlag(options::OPT_fconstant_cfstrings,
                    options::OPT_fno_constant_cfstrings, true) ||
      !Args.hasFlag(options::OPT_mconstant_cfstrings,
                    options::OPT_mno_constant_cfstrings, true))
    CmdArgs.push_back("-fno-constant-cfstrings");

  Args.addOptInFlag(CmdArgs, options::OPT_fpascal_strings,
                    options::OPT_fno_pascal_strings);

  // Honor -fpack-struct= and -fpack-struct, if given. Note that
  // -fno-pack-struct doesn't apply to -fpack-struct=.
  if (Arg *A = Args.getLastArg(options::OPT_fpack_struct_EQ)) {
    std::string PackStructStr = "-fpack-struct=";
    PackStructStr += A->getValue();
    CmdArgs.push_back(Args.MakeArgString(PackStructStr));
  } else if (Args.hasFlag(options::OPT_fpack_struct,
                          options::OPT_fno_pack_struct, false)) {
    CmdArgs.push_back("-fpack-struct=1");
  }

  // Handle -fmax-type-align=N and -fno-type-align
  bool SkipMaxTypeAlign = Args.hasArg(options::OPT_fno_max_type_align);
  if (Arg *A = Args.getLastArg(options::OPT_fmax_type_align_EQ)) {
    if (!SkipMaxTypeAlign) {
      std::string MaxTypeAlignStr = "-fmax-type-align=";
      MaxTypeAlignStr += A->getValue();
      CmdArgs.push_back(Args.MakeArgString(MaxTypeAlignStr));
    }
  } else if (RawTriple.isOSDarwin()) {
    if (!SkipMaxTypeAlign) {
      std::string MaxTypeAlignStr = "-fmax-type-align=16";
      CmdArgs.push_back(Args.MakeArgString(MaxTypeAlignStr));
    }
  }

  if (!Args.hasFlag(options::OPT_Qy, options::OPT_Qn, true))
    CmdArgs.push_back("-Qn");

  // -fno-common is the default, set -fcommon only when that flag is set.
  Args.addOptInFlag(CmdArgs, options::OPT_fcommon, options::OPT_fno_common);

  // -fsigned-bitfields is default, and clang doesn't yet support
  // -funsigned-bitfields.
  if (!Args.hasFlag(options::OPT_fsigned_bitfields,
                    options::OPT_funsigned_bitfields, true))
    D.Diag(diag::warn_drv_clang_unsupported)
        << Args.getLastArg(options::OPT_funsigned_bitfields)->getAsString(Args);

  // -fsigned-bitfields is default, and clang doesn't support -fno-for-scope.
  if (!Args.hasFlag(options::OPT_ffor_scope, options::OPT_fno_for_scope, true))
    D.Diag(diag::err_drv_clang_unsupported)
        << Args.getLastArg(options::OPT_fno_for_scope)->getAsString(Args);

  // -finput_charset=UTF-8 is default. Reject others
  if (Arg *inputCharset = Args.getLastArg(options::OPT_finput_charset_EQ)) {
    StringRef value = inputCharset->getValue();
    if (!value.equals_insensitive("utf-8"))
      D.Diag(diag::err_drv_invalid_value) << inputCharset->getAsString(Args)
                                          << value;
  }

  // -fexec_charset=UTF-8 is default. Reject others
  if (Arg *execCharset = Args.getLastArg(options::OPT_fexec_charset_EQ)) {
    StringRef value = execCharset->getValue();
    if (!value.equals_insensitive("utf-8"))
      D.Diag(diag::err_drv_invalid_value) << execCharset->getAsString(Args)
                                          << value;
  }

  RenderDiagnosticsOptions(D, Args, CmdArgs);

  Args.addOptInFlag(CmdArgs, options::OPT_fasm_blocks,
                    options::OPT_fno_asm_blocks);

  Args.addOptOutFlag(CmdArgs, options::OPT_fgnu_inline_asm,
                     options::OPT_fno_gnu_inline_asm);

  // Enable vectorization per default according to the optimization level
  // selected. For optimization levels that want vectorization we use the alias
  // option to simplify the hasFlag logic.
  bool EnableVec = shouldEnableVectorizerAtOLevel(Args, false);
  OptSpecifier VectorizeAliasOption =
      EnableVec ? options::OPT_O_Group : options::OPT_fvectorize;
  if (Args.hasFlag(options::OPT_fvectorize, VectorizeAliasOption,
                   options::OPT_fno_vectorize, EnableVec))
    CmdArgs.push_back("-vectorize-loops");

  // -fslp-vectorize is enabled based on the optimization level selected.
  bool EnableSLPVec = shouldEnableVectorizerAtOLevel(Args, true);
  OptSpecifier SLPVectAliasOption =
      EnableSLPVec ? options::OPT_O_Group : options::OPT_fslp_vectorize;
  if (Args.hasFlag(options::OPT_fslp_vectorize, SLPVectAliasOption,
                   options::OPT_fno_slp_vectorize, EnableSLPVec))
    CmdArgs.push_back("-vectorize-slp");

  ParseMPreferVectorWidth(D, Args, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_fshow_overloads_EQ);
  Args.AddLastArg(CmdArgs,
                  options::OPT_fsanitize_undefined_strip_path_components_EQ);

  // -fdollars-in-identifiers default varies depending on platform and
  // language; only pass if specified.
  if (Arg *A = Args.getLastArg(options::OPT_fdollars_in_identifiers,
                               options::OPT_fno_dollars_in_identifiers)) {
    if (A->getOption().matches(options::OPT_fdollars_in_identifiers))
      CmdArgs.push_back("-fdollars-in-identifiers");
    else
      CmdArgs.push_back("-fno-dollars-in-identifiers");
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fapple_pragma_pack,
                    options::OPT_fno_apple_pragma_pack);

  // Remarks can be enabled with any of the `-f.*optimization-record.*` flags.
  if (willEmitRemarks(Args) && checkRemarksOptions(D, Args, Triple))
    renderRemarksOptions(Args, CmdArgs, Triple, Input, Output, JA);

  bool RewriteImports = Args.hasFlag(options::OPT_frewrite_imports,
                                     options::OPT_fno_rewrite_imports, false);
  if (RewriteImports)
    CmdArgs.push_back("-frewrite-imports");

  // Disable some builtins on OpenBSD because they are just not
  // right...
  if (getToolChain().getTriple().isOSOpenBSD()) {
    CmdArgs.push_back("-fno-builtin-malloc");
    CmdArgs.push_back("-fno-builtin-calloc");
    CmdArgs.push_back("-fno-builtin-realloc");
    CmdArgs.push_back("-fno-builtin-valloc");
    CmdArgs.push_back("-fno-builtin-free");
    CmdArgs.push_back("-fno-builtin-strdup");
    CmdArgs.push_back("-fno-builtin-strndup");
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fdirectives_only,
                    options::OPT_fno_directives_only);

  // Enable rewrite includes if the user's asked for it or if we're generating
  // diagnostics.
  // TODO: Once -module-dependency-dir works with -frewrite-includes it'd be
  // nice to enable this when doing a crashdump for modules as well.
  if (Args.hasFlag(options::OPT_frewrite_includes,
                   options::OPT_fno_rewrite_includes, false) ||
      (C.isForDiagnostics() && !HaveModules))
    CmdArgs.push_back("-frewrite-includes");

  // Only allow -traditional or -traditional-cpp outside in preprocessing modes.
  if (Arg *A = Args.getLastArg(options::OPT_traditional,
                               options::OPT_traditional_cpp)) {
    if (isa<PreprocessJobAction>(JA))
      CmdArgs.push_back("-traditional-cpp");
    else
      D.Diag(diag::err_drv_clang_unsupported) << A->getAsString(Args);
  }

  Args.AddLastArg(CmdArgs, options::OPT_dM);
  Args.AddLastArg(CmdArgs, options::OPT_dD);
  Args.AddLastArg(CmdArgs, options::OPT_dI);

  Args.AddLastArg(CmdArgs, options::OPT_fmax_tokens_EQ);

  // Handle serialized diagnostics.
  if (Arg *A = Args.getLastArg(options::OPT__serialize_diags)) {
    CmdArgs.push_back("-serialize-diagnostic-file");
    CmdArgs.push_back(Args.MakeArgString(A->getValue()));
  }

  if (Args.hasArg(options::OPT_fretain_comments_from_system_headers))
    CmdArgs.push_back("-fretain-comments-from-system-headers");

  // Forward -fcomment-block-commands to -cc1.
  Args.AddAllArgs(CmdArgs, options::OPT_fcomment_block_commands);
  // Forward -fparse-all-comments to -cc1.
  Args.AddAllArgs(CmdArgs, options::OPT_fparse_all_comments);

  // Turn -fplugin=name.so into -load name.so
  for (const Arg *A : Args.filtered(options::OPT_fplugin_EQ)) {
    CmdArgs.push_back("-load");
    CmdArgs.push_back(A->getValue());
    A->claim();
  }

  // Turn -fplugin-arg-pluginname-key=value into
  // -plugin-arg-pluginname key=value
  // GCC has an actual plugin_argument struct with key/value pairs that it
  // passes to its plugins, but we don't, so just pass it on as-is.
  //
  // The syntax for -fplugin-arg- is ambiguous if both plugin name and
  // argument key are allowed to contain dashes. GCC therefore only
  // allows dashes in the key. We do the same.
  for (const Arg *A : Args.filtered(options::OPT_fplugin_arg)) {
    auto ArgValue = StringRef(A->getValue());
    auto FirstDashIndex = ArgValue.find('-');
    StringRef PluginName = ArgValue.substr(0, FirstDashIndex);
    StringRef Arg = ArgValue.substr(FirstDashIndex + 1);

    A->claim();
    if (FirstDashIndex == StringRef::npos || Arg.empty()) {
      if (PluginName.empty()) {
        D.Diag(diag::warn_drv_missing_plugin_name) << A->getAsString(Args);
      } else {
        D.Diag(diag::warn_drv_missing_plugin_arg)
            << PluginName << A->getAsString(Args);
      }
      continue;
    }

    CmdArgs.push_back(Args.MakeArgString(Twine("-plugin-arg-") + PluginName));
    CmdArgs.push_back(Args.MakeArgString(Arg));
  }

  // Forward -fpass-plugin=name.so to -cc1.
  for (const Arg *A : Args.filtered(options::OPT_fpass_plugin_EQ)) {
    CmdArgs.push_back(
        Args.MakeArgString(Twine("-fpass-plugin=") + A->getValue()));
    A->claim();
  }

  // Forward --vfsoverlay to -cc1.
  for (const Arg *A : Args.filtered(options::OPT_vfsoverlay)) {
    CmdArgs.push_back("--vfsoverlay");
    CmdArgs.push_back(A->getValue());
    A->claim();
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fsafe_buffer_usage_suggestions,
                    options::OPT_fno_safe_buffer_usage_suggestions);

  Args.addOptInFlag(CmdArgs, options::OPT_fexperimental_late_parse_attributes,
                    options::OPT_fno_experimental_late_parse_attributes);

  // Setup statistics file output.
  SmallString<128> StatsFile = getStatsFileName(Args, Output, Input, D);
  if (!StatsFile.empty()) {
    CmdArgs.push_back(Args.MakeArgString(Twine("-stats-file=") + StatsFile));
    if (D.CCPrintInternalStats)
      CmdArgs.push_back("-stats-file-append");
  }

  // Forward -Xclang arguments to -cc1, and -mllvm arguments to the LLVM option
  // parser.
  for (auto Arg : Args.filtered(options::OPT_Xclang)) {
    Arg->claim();
    // -finclude-default-header flag is for preprocessor,
    // do not pass it to other cc1 commands when save-temps is enabled
    if (C.getDriver().isSaveTempsEnabled() &&
        !isa<PreprocessJobAction>(JA)) {
      if (StringRef(Arg->getValue()) == "-finclude-default-header")
        continue;
    }
    CmdArgs.push_back(Arg->getValue());
  }
  for (const Arg *A : Args.filtered(options::OPT_mllvm)) {
    A->claim();

    // We translate this by hand to the -cc1 argument, since nightly test uses
    // it and developers have been trained to spell it with -mllvm. Both
    // spellings are now deprecated and should be removed.
    if (StringRef(A->getValue(0)) == "-disable-llvm-optzns") {
      CmdArgs.push_back("-disable-llvm-optzns");
    } else {
      A->render(Args, CmdArgs);
    }
  }

  // With -save-temps, we want to save the unoptimized bitcode output from the
  // CompileJobAction, use -disable-llvm-passes to get pristine IR generated
  // by the frontend.
  // When -fembed-bitcode is enabled, optimized bitcode is emitted because it
  // has slightly different breakdown between stages.
  // FIXME: -fembed-bitcode -save-temps will save optimized bitcode instead of
  // pristine IR generated by the frontend. Ideally, a new compile action should
  // be added so both IR can be captured.
  if ((C.getDriver().isSaveTempsEnabled() ||
       JA.isHostOffloading(Action::OFK_OpenMP)) &&
      !(C.getDriver().embedBitcodeInObject() && !IsUsingLTO) &&
      isa<CompileJobAction>(JA))
    CmdArgs.push_back("-disable-llvm-passes");

  Args.AddAllArgs(CmdArgs, options::OPT_undef);

  const char *Exec = D.getClangProgramPath();

  // Optionally embed the -cc1 level arguments into the debug info or a
  // section, for build analysis.
  // Also record command line arguments into the debug info if
  // -grecord-gcc-switches options is set on.
  // By default, -gno-record-gcc-switches is set on and no recording.
  auto GRecordSwitches =
      Args.hasFlag(options::OPT_grecord_command_line,
                   options::OPT_gno_record_command_line, false);
  auto FRecordSwitches =
      Args.hasFlag(options::OPT_frecord_command_line,
                   options::OPT_fno_record_command_line, false);
  if (FRecordSwitches && !Triple.isOSBinFormatELF() &&
      !Triple.isOSBinFormatXCOFF() && !Triple.isOSBinFormatMachO())
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << Args.getLastArg(options::OPT_frecord_command_line)->getAsString(Args)
        << TripleStr;
  if (TC.UseDwarfDebugFlags() || GRecordSwitches || FRecordSwitches) {
    ArgStringList OriginalArgs;
    for (const auto &Arg : Args)
      Arg->render(Args, OriginalArgs);

    SmallString<256> Flags;
    EscapeSpacesAndBackslashes(Exec, Flags);
    for (const char *OriginalArg : OriginalArgs) {
      SmallString<128> EscapedArg;
      EscapeSpacesAndBackslashes(OriginalArg, EscapedArg);
      Flags += " ";
      Flags += EscapedArg;
    }
    auto FlagsArgString = Args.MakeArgString(Flags);
    if (TC.UseDwarfDebugFlags() || GRecordSwitches) {
      CmdArgs.push_back("-dwarf-debug-flags");
      CmdArgs.push_back(FlagsArgString);
    }
    if (FRecordSwitches) {
      CmdArgs.push_back("-record-command-line");
      CmdArgs.push_back(FlagsArgString);
    }
  }

  // Host-side offloading compilation receives all device-side outputs. Include
  // them in the host compilation depending on the target. If the host inputs
  // are not empty we use the new-driver scheme, otherwise use the old scheme.
  if ((IsCuda || IsHIP) && CudaDeviceInput) {
    CmdArgs.push_back("-fcuda-include-gpubinary");
    CmdArgs.push_back(CudaDeviceInput->getFilename());
  } else if (!HostOffloadingInputs.empty()) {
    if ((IsCuda || IsHIP) && !IsRDCMode) {
      assert(HostOffloadingInputs.size() == 1 && "Only one input expected");
      CmdArgs.push_back("-fcuda-include-gpubinary");
      CmdArgs.push_back(HostOffloadingInputs.front().getFilename());
    } else {
      for (const InputInfo Input : HostOffloadingInputs)
        CmdArgs.push_back(Args.MakeArgString("-fembed-offload-object=" +
                                             TC.getInputFilename(Input)));
    }
  }

  if (IsCuda) {
    if (Args.hasFlag(options::OPT_fcuda_short_ptr,
                     options::OPT_fno_cuda_short_ptr, false))
      CmdArgs.push_back("-fcuda-short-ptr");
  }

  if (IsCuda || IsHIP) {
    // Determine the original source input.
    const Action *SourceAction = &JA;
    while (SourceAction->getKind() != Action::InputClass) {
      assert(!SourceAction->getInputs().empty() && "unexpected root action!");
      SourceAction = SourceAction->getInputs()[0];
    }
    auto CUID = cast<InputAction>(SourceAction)->getId();
    if (!CUID.empty())
      CmdArgs.push_back(Args.MakeArgString(Twine("-cuid=") + Twine(CUID)));

    // -ffast-math turns on -fgpu-approx-transcendentals implicitly, but will
    // be overriden by -fno-gpu-approx-transcendentals.
    bool UseApproxTranscendentals = Args.hasFlag(
        options::OPT_ffast_math, options::OPT_fno_fast_math, false);
    if (Args.hasFlag(options::OPT_fgpu_approx_transcendentals,
                     options::OPT_fno_gpu_approx_transcendentals,
                     UseApproxTranscendentals))
      CmdArgs.push_back("-fgpu-approx-transcendentals");
  } else {
    Args.claimAllArgs(options::OPT_fgpu_approx_transcendentals,
                      options::OPT_fno_gpu_approx_transcendentals);
  }

  if (IsHIP) {
    CmdArgs.push_back("-fcuda-allow-variadic-functions");
    Args.AddLastArg(CmdArgs, options::OPT_fgpu_default_stream_EQ);
  }

  Args.AddLastArg(CmdArgs, options::OPT_foffload_uniform_block,
                  options::OPT_fno_offload_uniform_block);

  Args.AddLastArg(CmdArgs, options::OPT_foffload_implicit_host_device_templates,
                  options::OPT_fno_offload_implicit_host_device_templates);

  if (IsCudaDevice || IsHIPDevice) {
    StringRef InlineThresh =
        Args.getLastArgValue(options::OPT_fgpu_inline_threshold_EQ);
    if (!InlineThresh.empty()) {
      std::string ArgStr =
          std::string("-inline-threshold=") + InlineThresh.str();
      CmdArgs.append({"-mllvm", Args.MakeArgStringRef(ArgStr)});
    }
  }

  if (IsHIPDevice)
    Args.addOptOutFlag(CmdArgs,
                       options::OPT_fhip_fp32_correctly_rounded_divide_sqrt,
                       options::OPT_fno_hip_fp32_correctly_rounded_divide_sqrt);

  // OpenMP offloading device jobs take the argument -fopenmp-host-ir-file-path
  // to specify the result of the compile phase on the host, so the meaningful
  // device declarations can be identified. Also, -fopenmp-is-target-device is
  // passed along to tell the frontend that it is generating code for a device,
  // so that only the relevant declarations are emitted.
  if (IsOpenMPDevice) {
    CmdArgs.push_back("-fopenmp-is-target-device");
    if (OpenMPDeviceInput) {
      CmdArgs.push_back("-fopenmp-host-ir-file-path");
      CmdArgs.push_back(Args.MakeArgString(OpenMPDeviceInput->getFilename()));
    }
  }

  if (Triple.isAMDGPU()) {
    handleAMDGPUCodeObjectVersionOptions(D, Args, CmdArgs);

    Args.addOptInFlag(CmdArgs, options::OPT_munsafe_fp_atomics,
                      options::OPT_mno_unsafe_fp_atomics);
    Args.addOptOutFlag(CmdArgs, options::OPT_mamdgpu_ieee,
                       options::OPT_mno_amdgpu_ieee);
  }

  // For all the host OpenMP offloading compile jobs we need to pass the targets
  // information using -fopenmp-targets= option.
  if (JA.isHostOffloading(Action::OFK_OpenMP)) {
    SmallString<128> Targets("-fopenmp-targets=");

    SmallVector<std::string, 4> Triples;
    auto TCRange = C.getOffloadToolChains<Action::OFK_OpenMP>();
    std::transform(TCRange.first, TCRange.second, std::back_inserter(Triples),
                   [](auto TC) { return TC.second->getTripleString(); });
    CmdArgs.push_back(Args.MakeArgString(Targets + llvm::join(Triples, ",")));
  }

  bool VirtualFunctionElimination =
      Args.hasFlag(options::OPT_fvirtual_function_elimination,
                   options::OPT_fno_virtual_function_elimination, false);
  if (VirtualFunctionElimination) {
    // VFE requires full LTO (currently, this might be relaxed to allow ThinLTO
    // in the future).
    if (LTOMode != LTOK_Full)
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << "-fvirtual-function-elimination"
          << "-flto=full";

    CmdArgs.push_back("-fvirtual-function-elimination");
  }

  // VFE requires whole-program-vtables, and enables it by default.
  bool WholeProgramVTables = Args.hasFlag(
      options::OPT_fwhole_program_vtables,
      options::OPT_fno_whole_program_vtables, VirtualFunctionElimination);
  if (VirtualFunctionElimination && !WholeProgramVTables) {
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << "-fno-whole-program-vtables"
        << "-fvirtual-function-elimination";
  }

  if (WholeProgramVTables) {
    // PS4 uses the legacy LTO API, which does not support this feature in
    // ThinLTO mode.
    bool IsPS4 = getToolChain().getTriple().isPS4();

    // Check if we passed LTO options but they were suppressed because this is a
    // device offloading action, or we passed device offload LTO options which
    // were suppressed because this is not the device offload action.
    // Check if we are using PS4 in regular LTO mode.
    // Otherwise, issue an error.
    if ((!IsUsingLTO && !D.isUsingLTO(!IsDeviceOffloadAction)) ||
        (IsPS4 && !UnifiedLTO && (D.getLTOMode() != LTOK_Full)))
      D.Diag(diag::err_drv_argument_only_allowed_with)
          << "-fwhole-program-vtables"
          << ((IsPS4 && !UnifiedLTO) ? "-flto=full" : "-flto");

    // Propagate -fwhole-program-vtables if this is an LTO compile.
    if (IsUsingLTO)
      CmdArgs.push_back("-fwhole-program-vtables");
  }

  bool DefaultsSplitLTOUnit =
      ((WholeProgramVTables || SanitizeArgs.needsLTO()) &&
          (LTOMode == LTOK_Full || TC.canSplitThinLTOUnit())) ||
      (!Triple.isPS4() && UnifiedLTO);
  bool SplitLTOUnit =
      Args.hasFlag(options::OPT_fsplit_lto_unit,
                   options::OPT_fno_split_lto_unit, DefaultsSplitLTOUnit);
  if (SanitizeArgs.needsLTO() && !SplitLTOUnit)
    D.Diag(diag::err_drv_argument_not_allowed_with) << "-fno-split-lto-unit"
                                                    << "-fsanitize=cfi";
  if (SplitLTOUnit)
    CmdArgs.push_back("-fsplit-lto-unit");

  if (Arg *A = Args.getLastArg(options::OPT_ffat_lto_objects,
                               options::OPT_fno_fat_lto_objects)) {
    if (IsUsingLTO && A->getOption().matches(options::OPT_ffat_lto_objects)) {
      assert(LTOMode == LTOK_Full || LTOMode == LTOK_Thin);
      if (!Triple.isOSBinFormatELF()) {
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << A->getAsString(Args) << TC.getTripleString();
      }
      CmdArgs.push_back(Args.MakeArgString(
          Twine("-flto=") + (LTOMode == LTOK_Thin ? "thin" : "full")));
      CmdArgs.push_back("-flto-unit");
      CmdArgs.push_back("-ffat-lto-objects");
      A->render(Args, CmdArgs);
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fglobal_isel,
                               options::OPT_fno_global_isel)) {
    CmdArgs.push_back("-mllvm");
    if (A->getOption().matches(options::OPT_fglobal_isel)) {
      CmdArgs.push_back("-global-isel=1");

      // GISel is on by default on AArch64 -O0, so don't bother adding
      // the fallback remarks for it. Other combinations will add a warning of
      // some kind.
      bool IsArchSupported = Triple.getArch() == llvm::Triple::aarch64;
      bool IsOptLevelSupported = false;

      Arg *A = Args.getLastArg(options::OPT_O_Group);
      if (Triple.getArch() == llvm::Triple::aarch64) {
        if (!A || A->getOption().matches(options::OPT_O0))
          IsOptLevelSupported = true;
      }
      if (!IsArchSupported || !IsOptLevelSupported) {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back("-global-isel-abort=2");

        if (!IsArchSupported)
          D.Diag(diag::warn_drv_global_isel_incomplete) << Triple.getArchName();
        else
          D.Diag(diag::warn_drv_global_isel_incomplete_opt);
      }
    } else {
      CmdArgs.push_back("-global-isel=0");
    }
  }

  if (Args.hasArg(options::OPT_forder_file_instrumentation)) {
     CmdArgs.push_back("-forder-file-instrumentation");
     // Enable order file instrumentation when ThinLTO is not on. When ThinLTO is
     // on, we need to pass these flags as linker flags and that will be handled
     // outside of the compiler.
     if (!IsUsingLTO) {
       CmdArgs.push_back("-mllvm");
       CmdArgs.push_back("-enable-order-file-instrumentation");
     }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fforce_enable_int128,
                               options::OPT_fno_force_enable_int128)) {
    if (A->getOption().matches(options::OPT_fforce_enable_int128))
      CmdArgs.push_back("-fforce-enable-int128");
  }

  Args.addOptInFlag(CmdArgs, options::OPT_fkeep_static_consts,
                    options::OPT_fno_keep_static_consts);
  Args.addOptInFlag(CmdArgs, options::OPT_fkeep_persistent_storage_variables,
                    options::OPT_fno_keep_persistent_storage_variables);
  Args.addOptInFlag(CmdArgs, options::OPT_fcomplete_member_pointers,
                    options::OPT_fno_complete_member_pointers);
  Args.addOptOutFlag(CmdArgs, options::OPT_fcxx_static_destructors,
                     options::OPT_fno_cxx_static_destructors);

  addMachineOutlinerArgs(D, Args, CmdArgs, Triple, /*IsLTO=*/false);

  addOutlineAtomicsArgs(D, getToolChain(), Args, CmdArgs, Triple);

  if (Triple.isAArch64() &&
      (Args.hasArg(options::OPT_mno_fmv) ||
       (Triple.isAndroid() && Triple.isAndroidVersionLT(23)) ||
       getToolChain().GetRuntimeLibType(Args) != ToolChain::RLT_CompilerRT)) {
    // Disable Function Multiversioning on AArch64 target.
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back("-fmv");
  }

  if (Args.hasFlag(options::OPT_faddrsig, options::OPT_fno_addrsig,
                   (TC.getTriple().isOSBinFormatELF() ||
                    TC.getTriple().isOSBinFormatCOFF()) &&
                       !TC.getTriple().isPS4() && !TC.getTriple().isVE() &&
                       !TC.getTriple().isOSNetBSD() &&
                       !Distro(D.getVFS(), TC.getTriple()).IsGentoo() &&
                       !TC.getTriple().isAndroid() && TC.useIntegratedAs()))
    CmdArgs.push_back("-faddrsig");

  if ((Triple.isOSBinFormatELF() || Triple.isOSBinFormatMachO()) &&
      (EH || UnwindTables || AsyncUnwindTables ||
       DebugInfoKind != llvm::codegenoptions::NoDebugInfo))
    CmdArgs.push_back("-D__GCC_HAVE_DWARF2_CFI_ASM=1");

  if (Arg *A = Args.getLastArg(options::OPT_fsymbol_partition_EQ)) {
    std::string Str = A->getAsString(Args);
    if (!TC.getTriple().isOSBinFormatELF())
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << Str << TC.getTripleString();
    CmdArgs.push_back(Args.MakeArgString(Str));
  }

  // Add the "-o out -x type src.c" flags last. This is done primarily to make
  // the -cc1 command easier to edit when reproducing compiler crashes.
  if (Output.getType() == types::TY_Dependencies) {
    // Handled with other dependency code.
  } else if (Output.isFilename()) {
    if (Output.getType() == clang::driver::types::TY_IFS_CPP ||
        Output.getType() == clang::driver::types::TY_IFS) {
      SmallString<128> OutputFilename(Output.getFilename());
      llvm::sys::path::replace_extension(OutputFilename, "ifs");
      CmdArgs.push_back("-o");
      CmdArgs.push_back(Args.MakeArgString(OutputFilename));
    } else {
      CmdArgs.push_back("-o");
      CmdArgs.push_back(Output.getFilename());
    }
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  addDashXForInput(Args, Input, CmdArgs);

  ArrayRef<InputInfo> FrontendInputs = Input;
  if (IsExtractAPI)
    FrontendInputs = ExtractAPIInputs;
  else if (Input.isNothing())
    FrontendInputs = {};

  for (const InputInfo &Input : FrontendInputs) {
    if (Input.isFilename())
      CmdArgs.push_back(Input.getFilename());
    else
      Input.getInputArg().renderAsInput(Args, CmdArgs);
  }

  if (D.CC1Main && !D.CCGenDiagnostics) {
    // Invoke the CC1 directly in this process
    C.addCommand(std::make_unique<CC1Command>(
        JA, *this, ResponseFileSupport::AtFileUTF8(), Exec, CmdArgs, Inputs,
        Output, D.getPrependArg()));
  } else {
    C.addCommand(std::make_unique<Command>(
        JA, *this, ResponseFileSupport::AtFileUTF8(), Exec, CmdArgs, Inputs,
        Output, D.getPrependArg()));
  }

  // Make the compile command echo its inputs for /showFilenames.
  if (Output.getType() == types::TY_Object &&
      Args.hasFlag(options::OPT__SLASH_showFilenames,
                   options::OPT__SLASH_showFilenames_, false)) {
    C.getJobs().getJobs().back()->PrintInputFilenames = true;
  }

  if (Arg *A = Args.getLastArg(options::OPT_pg))
    if (FPKeepKind == CodeGenOptions::FramePointerKind::None &&
        !Args.hasArg(options::OPT_mfentry))
      D.Diag(diag::err_drv_argument_not_allowed_with) << "-fomit-frame-pointer"
                                                      << A->getAsString(Args);

  // Claim some arguments which clang supports automatically.

  // -fpch-preprocess is used with gcc to add a special marker in the output to
  // include the PCH file.
  Args.ClaimAllArgs(options::OPT_fpch_preprocess);

  // Claim some arguments which clang doesn't support, but we don't
  // care to warn the user about.
  Args.ClaimAllArgs(options::OPT_clang_ignored_f_Group);
  Args.ClaimAllArgs(options::OPT_clang_ignored_m_Group);

  // Disable warnings for clang -E -emit-llvm foo.c
  Args.ClaimAllArgs(options::OPT_emit_llvm);
}

Clang::Clang(const ToolChain &TC, bool HasIntegratedBackend)
    // CAUTION! The first constructor argument ("clang") is not arbitrary,
    // as it is for other tools. Some operations on a Tool actually test
    // whether that tool is Clang based on the Tool's Name as a string.
    : Tool("clang", "clang frontend", TC), HasBackend(HasIntegratedBackend) {}

Clang::~Clang() {}

/// Add options related to the Objective-C runtime/ABI.
///
/// Returns true if the runtime is non-fragile.
ObjCRuntime Clang::AddObjCRuntimeArgs(const ArgList &args,
                                      const InputInfoList &inputs,
                                      ArgStringList &cmdArgs,
                                      RewriteKind rewriteKind) const {
  // Look for the controlling runtime option.
  Arg *runtimeArg =
      args.getLastArg(options::OPT_fnext_runtime, options::OPT_fgnu_runtime,
                      options::OPT_fobjc_runtime_EQ);

  // Just forward -fobjc-runtime= to the frontend.  This supercedes
  // options about fragility.
  if (runtimeArg &&
      runtimeArg->getOption().matches(options::OPT_fobjc_runtime_EQ)) {
    ObjCRuntime runtime;
    StringRef value = runtimeArg->getValue();
    if (runtime.tryParse(value)) {
      getToolChain().getDriver().Diag(diag::err_drv_unknown_objc_runtime)
          << value;
    }
    if ((runtime.getKind() == ObjCRuntime::GNUstep) &&
        (runtime.getVersion() >= VersionTuple(2, 0)))
      if (!getToolChain().getTriple().isOSBinFormatELF() &&
          !getToolChain().getTriple().isOSBinFormatCOFF()) {
        getToolChain().getDriver().Diag(
            diag::err_drv_gnustep_objc_runtime_incompatible_binary)
          << runtime.getVersion().getMajor();
      }

    runtimeArg->render(args, cmdArgs);
    return runtime;
  }

  // Otherwise, we'll need the ABI "version".  Version numbers are
  // slightly confusing for historical reasons:
  //   1 - Traditional "fragile" ABI
  //   2 - Non-fragile ABI, version 1
  //   3 - Non-fragile ABI, version 2
  unsigned objcABIVersion = 1;
  // If -fobjc-abi-version= is present, use that to set the version.
  if (Arg *abiArg = args.getLastArg(options::OPT_fobjc_abi_version_EQ)) {
    StringRef value = abiArg->getValue();
    if (value == "1")
      objcABIVersion = 1;
    else if (value == "2")
      objcABIVersion = 2;
    else if (value == "3")
      objcABIVersion = 3;
    else
      getToolChain().getDriver().Diag(diag::err_drv_clang_unsupported) << value;
  } else {
    // Otherwise, determine if we are using the non-fragile ABI.
    bool nonFragileABIIsDefault =
        (rewriteKind == RK_NonFragile ||
         (rewriteKind == RK_None &&
          getToolChain().IsObjCNonFragileABIDefault()));
    if (args.hasFlag(options::OPT_fobjc_nonfragile_abi,
                     options::OPT_fno_objc_nonfragile_abi,
                     nonFragileABIIsDefault)) {
// Determine the non-fragile ABI version to use.
#ifdef DISABLE_DEFAULT_NONFRAGILEABI_TWO
      unsigned nonFragileABIVersion = 1;
#else
      unsigned nonFragileABIVersion = 2;
#endif

      if (Arg *abiArg =
              args.getLastArg(options::OPT_fobjc_nonfragile_abi_version_EQ)) {
        StringRef value = abiArg->getValue();
        if (value == "1")
          nonFragileABIVersion = 1;
        else if (value == "2")
          nonFragileABIVersion = 2;
        else
          getToolChain().getDriver().Diag(diag::err_drv_clang_unsupported)
              << value;
      }

      objcABIVersion = 1 + nonFragileABIVersion;
    } else {
      objcABIVersion = 1;
    }
  }

  // We don't actually care about the ABI version other than whether
  // it's non-fragile.
  bool isNonFragile = objcABIVersion != 1;

  // If we have no runtime argument, ask the toolchain for its default runtime.
  // However, the rewriter only really supports the Mac runtime, so assume that.
  ObjCRuntime runtime;
  if (!runtimeArg) {
    switch (rewriteKind) {
    case RK_None:
      runtime = getToolChain().getDefaultObjCRuntime(isNonFragile);
      break;
    case RK_Fragile:
      runtime = ObjCRuntime(ObjCRuntime::FragileMacOSX, VersionTuple());
      break;
    case RK_NonFragile:
      runtime = ObjCRuntime(ObjCRuntime::MacOSX, VersionTuple());
      break;
    }

    // -fnext-runtime
  } else if (runtimeArg->getOption().matches(options::OPT_fnext_runtime)) {
    // On Darwin, make this use the default behavior for the toolchain.
    if (getToolChain().getTriple().isOSDarwin()) {
      runtime = getToolChain().getDefaultObjCRuntime(isNonFragile);

      // Otherwise, build for a generic macosx port.
    } else {
      runtime = ObjCRuntime(ObjCRuntime::MacOSX, VersionTuple());
    }

    // -fgnu-runtime
  } else {
    assert(runtimeArg->getOption().matches(options::OPT_fgnu_runtime));
    // Legacy behaviour is to target the gnustep runtime if we are in
    // non-fragile mode or the GCC runtime in fragile mode.
    if (isNonFragile)
      runtime = ObjCRuntime(ObjCRuntime::GNUstep, VersionTuple(2, 0));
    else
      runtime = ObjCRuntime(ObjCRuntime::GCC, VersionTuple());
  }

  if (llvm::any_of(inputs, [](const InputInfo &input) {
        return types::isObjC(input.getType());
      }))
    cmdArgs.push_back(
        args.MakeArgString("-fobjc-runtime=" + runtime.getAsString()));
  return runtime;
}

static bool maybeConsumeDash(const std::string &EH, size_t &I) {
  bool HaveDash = (I + 1 < EH.size() && EH[I + 1] == '-');
  I += HaveDash;
  return !HaveDash;
}

namespace {
struct EHFlags {
  bool Synch = false;
  bool Asynch = false;
  bool NoUnwindC = false;
};
} // end anonymous namespace

/// /EH controls whether to run destructor cleanups when exceptions are
/// thrown.  There are three modifiers:
/// - s: Cleanup after "synchronous" exceptions, aka C++ exceptions.
/// - a: Cleanup after "asynchronous" exceptions, aka structured exceptions.
///      The 'a' modifier is unimplemented and fundamentally hard in LLVM IR.
/// - c: Assume that extern "C" functions are implicitly nounwind.
/// The default is /EHs-c-, meaning cleanups are disabled.
static EHFlags parseClangCLEHFlags(const Driver &D, const ArgList &Args,
                                   bool isWindowsMSVC) {
  EHFlags EH;

  std::vector<std::string> EHArgs =
      Args.getAllArgValues(options::OPT__SLASH_EH);
  for (const auto &EHVal : EHArgs) {
    for (size_t I = 0, E = EHVal.size(); I != E; ++I) {
      switch (EHVal[I]) {
      case 'a':
        EH.Asynch = maybeConsumeDash(EHVal, I);
        if (EH.Asynch) {
          // Async exceptions are Windows MSVC only.
          if (!isWindowsMSVC) {
            EH.Asynch = false;
            D.Diag(clang::diag::warn_drv_unused_argument) << "/EHa" << EHVal;
            continue;
          }
          EH.Synch = false;
        }
        continue;
      case 'c':
        EH.NoUnwindC = maybeConsumeDash(EHVal, I);
        continue;
      case 's':
        EH.Synch = maybeConsumeDash(EHVal, I);
        if (EH.Synch)
          EH.Asynch = false;
        continue;
      default:
        break;
      }
      D.Diag(clang::diag::err_drv_invalid_value) << "/EH" << EHVal;
      break;
    }
  }
  // The /GX, /GX- flags are only processed if there are not /EH flags.
  // The default is that /GX is not specified.
  if (EHArgs.empty() &&
      Args.hasFlag(options::OPT__SLASH_GX, options::OPT__SLASH_GX_,
                   /*Default=*/false)) {
    EH.Synch = true;
    EH.NoUnwindC = true;
  }

  if (Args.hasArg(options::OPT__SLASH_kernel)) {
    EH.Synch = false;
    EH.NoUnwindC = false;
    EH.Asynch = false;
  }

  return EH;
}

void Clang::AddClangCLArgs(const ArgList &Args, types::ID InputType,
                           ArgStringList &CmdArgs) const {
  bool isNVPTX = getToolChain().getTriple().isNVPTX();

  ProcessVSRuntimeLibrary(getToolChain(), Args, CmdArgs);

  if (Arg *ShowIncludes =
          Args.getLastArg(options::OPT__SLASH_showIncludes,
                          options::OPT__SLASH_showIncludes_user)) {
    CmdArgs.push_back("--show-includes");
    if (ShowIncludes->getOption().matches(options::OPT__SLASH_showIncludes))
      CmdArgs.push_back("-sys-header-deps");
  }

  // This controls whether or not we emit RTTI data for polymorphic types.
  if (Args.hasFlag(options::OPT__SLASH_GR_, options::OPT__SLASH_GR,
                   /*Default=*/false))
    CmdArgs.push_back("-fno-rtti-data");

  // This controls whether or not we emit stack-protector instrumentation.
  // In MSVC, Buffer Security Check (/GS) is on by default.
  if (!isNVPTX && Args.hasFlag(options::OPT__SLASH_GS, options::OPT__SLASH_GS_,
                               /*Default=*/true)) {
    CmdArgs.push_back("-stack-protector");
    CmdArgs.push_back(Args.MakeArgString(Twine(LangOptions::SSPStrong)));
  }

  const Driver &D = getToolChain().getDriver();

  bool IsWindowsMSVC = getToolChain().getTriple().isWindowsMSVCEnvironment();
  EHFlags EH = parseClangCLEHFlags(D, Args, IsWindowsMSVC);
  if (!isNVPTX && (EH.Synch || EH.Asynch)) {
    if (types::isCXX(InputType))
      CmdArgs.push_back("-fcxx-exceptions");
    CmdArgs.push_back("-fexceptions");
    if (EH.Asynch)
      CmdArgs.push_back("-fasync-exceptions");
  }
  if (types::isCXX(InputType) && EH.Synch && EH.NoUnwindC)
    CmdArgs.push_back("-fexternc-nounwind");

  // /EP should expand to -E -P.
  if (Args.hasArg(options::OPT__SLASH_EP)) {
    CmdArgs.push_back("-E");
    CmdArgs.push_back("-P");
  }

 if (Args.hasFlag(options::OPT__SLASH_Zc_dllexportInlines_,
                  options::OPT__SLASH_Zc_dllexportInlines,
                  false)) {
  CmdArgs.push_back("-fno-dllexport-inlines");
 }

 if (Args.hasFlag(options::OPT__SLASH_Zc_wchar_t_,
                  options::OPT__SLASH_Zc_wchar_t, false)) {
   CmdArgs.push_back("-fno-wchar");
 }

 if (Args.hasArg(options::OPT__SLASH_kernel)) {
   llvm::Triple::ArchType Arch = getToolChain().getArch();
   std::vector<std::string> Values =
       Args.getAllArgValues(options::OPT__SLASH_arch);
   if (!Values.empty()) {
     llvm::SmallSet<std::string, 4> SupportedArches;
     if (Arch == llvm::Triple::x86)
       SupportedArches.insert("IA32");

     for (auto &V : Values)
       if (!SupportedArches.contains(V))
         D.Diag(diag::err_drv_argument_not_allowed_with)
             << std::string("/arch:").append(V) << "/kernel";
   }

   CmdArgs.push_back("-fno-rtti");
   if (Args.hasFlag(options::OPT__SLASH_GR, options::OPT__SLASH_GR_, false))
     D.Diag(diag::err_drv_argument_not_allowed_with) << "/GR"
                                                     << "/kernel";
 }

  Arg *MostGeneralArg = Args.getLastArg(options::OPT__SLASH_vmg);
  Arg *BestCaseArg = Args.getLastArg(options::OPT__SLASH_vmb);
  if (MostGeneralArg && BestCaseArg)
    D.Diag(clang::diag::err_drv_argument_not_allowed_with)
        << MostGeneralArg->getAsString(Args) << BestCaseArg->getAsString(Args);

  if (MostGeneralArg) {
    Arg *SingleArg = Args.getLastArg(options::OPT__SLASH_vms);
    Arg *MultipleArg = Args.getLastArg(options::OPT__SLASH_vmm);
    Arg *VirtualArg = Args.getLastArg(options::OPT__SLASH_vmv);

    Arg *FirstConflict = SingleArg ? SingleArg : MultipleArg;
    Arg *SecondConflict = VirtualArg ? VirtualArg : MultipleArg;
    if (FirstConflict && SecondConflict && FirstConflict != SecondConflict)
      D.Diag(clang::diag::err_drv_argument_not_allowed_with)
          << FirstConflict->getAsString(Args)
          << SecondConflict->getAsString(Args);

    if (SingleArg)
      CmdArgs.push_back("-fms-memptr-rep=single");
    else if (MultipleArg)
      CmdArgs.push_back("-fms-memptr-rep=multiple");
    else
      CmdArgs.push_back("-fms-memptr-rep=virtual");
  }

  if (Args.hasArg(options::OPT_regcall4))
    CmdArgs.push_back("-regcall4");

  // Parse the default calling convention options.
  if (Arg *CCArg =
          Args.getLastArg(options::OPT__SLASH_Gd, options::OPT__SLASH_Gr,
                          options::OPT__SLASH_Gz, options::OPT__SLASH_Gv,
                          options::OPT__SLASH_Gregcall)) {
    unsigned DCCOptId = CCArg->getOption().getID();
    const char *DCCFlag = nullptr;
    bool ArchSupported = !isNVPTX;
    llvm::Triple::ArchType Arch = getToolChain().getArch();
    switch (DCCOptId) {
    case options::OPT__SLASH_Gd:
      DCCFlag = "-fdefault-calling-conv=cdecl";
      break;
    case options::OPT__SLASH_Gr:
      ArchSupported = Arch == llvm::Triple::x86;
      DCCFlag = "-fdefault-calling-conv=fastcall";
      break;
    case options::OPT__SLASH_Gz:
      ArchSupported = Arch == llvm::Triple::x86;
      DCCFlag = "-fdefault-calling-conv=stdcall";
      break;
    case options::OPT__SLASH_Gv:
      ArchSupported = Arch == llvm::Triple::x86 || Arch == llvm::Triple::x86_64;
      DCCFlag = "-fdefault-calling-conv=vectorcall";
      break;
    case options::OPT__SLASH_Gregcall:
      ArchSupported = Arch == llvm::Triple::x86 || Arch == llvm::Triple::x86_64;
      DCCFlag = "-fdefault-calling-conv=regcall";
      break;
    }

    // MSVC doesn't warn if /Gr or /Gz is used on x64, so we don't either.
    if (ArchSupported && DCCFlag)
      CmdArgs.push_back(DCCFlag);
  }

  if (Args.hasArg(options::OPT__SLASH_Gregcall4))
    CmdArgs.push_back("-regcall4");

  Args.AddLastArg(CmdArgs, options::OPT_vtordisp_mode_EQ);

  if (!Args.hasArg(options::OPT_fdiagnostics_format_EQ)) {
    CmdArgs.push_back("-fdiagnostics-format");
    CmdArgs.push_back("msvc");
  }

  if (Args.hasArg(options::OPT__SLASH_kernel))
    CmdArgs.push_back("-fms-kernel");

  for (const Arg *A : Args.filtered(options::OPT__SLASH_guard)) {
    StringRef GuardArgs = A->getValue();
    // The only valid options are "cf", "cf,nochecks", "cf-", "ehcont" and
    // "ehcont-".
    if (GuardArgs.equals_insensitive("cf")) {
      // Emit CFG instrumentation and the table of address-taken functions.
      CmdArgs.push_back("-cfguard");
    } else if (GuardArgs.equals_insensitive("cf,nochecks")) {
      // Emit only the table of address-taken functions.
      CmdArgs.push_back("-cfguard-no-checks");
    } else if (GuardArgs.equals_insensitive("ehcont")) {
      // Emit EH continuation table.
      CmdArgs.push_back("-ehcontguard");
    } else if (GuardArgs.equals_insensitive("cf-") ||
               GuardArgs.equals_insensitive("ehcont-")) {
      // Do nothing, but we might want to emit a security warning in future.
    } else {
      D.Diag(diag::err_drv_invalid_value) << A->getSpelling() << GuardArgs;
    }
    A->claim();
  }
}

const char *Clang::getBaseInputName(const ArgList &Args,
                                    const InputInfo &Input) {
  return Args.MakeArgString(llvm::sys::path::filename(Input.getBaseInput()));
}

const char *Clang::getBaseInputStem(const ArgList &Args,
                                    const InputInfoList &Inputs) {
  const char *Str = getBaseInputName(Args, Inputs[0]);

  if (const char *End = strrchr(Str, '.'))
    return Args.MakeArgString(std::string(Str, End));

  return Str;
}

const char *Clang::getDependencyFileName(const ArgList &Args,
                                         const InputInfoList &Inputs) {
  // FIXME: Think about this more.

  if (Arg *OutputOpt = Args.getLastArg(options::OPT_o)) {
    SmallString<128> OutputFilename(OutputOpt->getValue());
    llvm::sys::path::replace_extension(OutputFilename, llvm::Twine('d'));
    return Args.MakeArgString(OutputFilename);
  }

  return Args.MakeArgString(Twine(getBaseInputStem(Args, Inputs)) + ".d");
}

// Begin ClangAs

void ClangAs::AddMIPSTargetArgs(const ArgList &Args,
                                ArgStringList &CmdArgs) const {
  StringRef CPUName;
  StringRef ABIName;
  const llvm::Triple &Triple = getToolChain().getTriple();
  mips::getMipsCPUAndABI(Args, Triple, CPUName, ABIName);

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName.data());
}

void ClangAs::AddX86TargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  addX86AlignBranchArgs(getToolChain().getDriver(), Args, CmdArgs,
                        /*IsLTO=*/false);

  if (Arg *A = Args.getLastArg(options::OPT_masm_EQ)) {
    StringRef Value = A->getValue();
    if (Value == "intel" || Value == "att") {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back(Args.MakeArgString("-x86-asm-syntax=" + Value));
    } else {
      getToolChain().getDriver().Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    }
  }
}

void ClangAs::AddLoongArchTargetArgs(const ArgList &Args,
                                     ArgStringList &CmdArgs) const {
  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(loongarch::getLoongArchABI(getToolChain().getDriver(), Args,
                                               getToolChain().getTriple())
                        .data());
}

void ClangAs::AddRISCVTargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  const llvm::Triple &Triple = getToolChain().getTriple();
  StringRef ABIName = riscv::getRISCVABI(Args, Triple);

  CmdArgs.push_back("-target-abi");
  CmdArgs.push_back(ABIName.data());

  if (Args.hasFlag(options::OPT_mdefault_build_attributes,
                   options::OPT_mno_default_build_attributes, true)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-riscv-add-build-attributes");
  }
}

void ClangAs::ConstructJob(Compilation &C, const JobAction &JA,
                           const InputInfo &Output, const InputInfoList &Inputs,
                           const ArgList &Args,
                           const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  assert(Inputs.size() == 1 && "Unexpected number of inputs.");
  const InputInfo &Input = Inputs[0];

  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();
  const std::string &TripleStr = Triple.getTriple();
  const auto &D = getToolChain().getDriver();

  // Don't warn about "clang -w -c foo.s"
  Args.ClaimAllArgs(options::OPT_w);
  // and "clang -emit-llvm -c foo.s"
  Args.ClaimAllArgs(options::OPT_emit_llvm);

  claimNoWarnArgs(Args);

  // Invoke ourselves in -cc1as mode.
  //
  // FIXME: Implement custom jobs for internal actions.
  CmdArgs.push_back("-cc1as");

  // Add the "effective" target triple.
  CmdArgs.push_back("-triple");
  CmdArgs.push_back(Args.MakeArgString(TripleStr));

  getToolChain().addClangCC1ASTargetOptions(Args, CmdArgs);

  // Set the output mode, we currently only expect to be used as a real
  // assembler.
  CmdArgs.push_back("-filetype");
  CmdArgs.push_back("obj");

  // Set the main file name, so that debug info works even with
  // -save-temps or preprocessed assembly.
  CmdArgs.push_back("-main-file-name");
  CmdArgs.push_back(Clang::getBaseInputName(Args, Input));

  // Add the target cpu
  std::string CPU = getCPUName(D, Args, Triple, /*FromAs*/ true);
  if (!CPU.empty()) {
    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPU));
  }

  // Add the target features
  getTargetFeatures(D, Triple, Args, CmdArgs, true);

  // Ignore explicit -force_cpusubtype_ALL option.
  (void)Args.hasArg(options::OPT_force__cpusubtype__ALL);

  // Pass along any -I options so we get proper .include search paths.
  Args.AddAllArgs(CmdArgs, options::OPT_I_Group);

  // Pass along any --embed-dir or similar options so we get proper embed paths.
  Args.AddAllArgs(CmdArgs, options::OPT_embed_dir_EQ);

  // Determine the original source input.
  auto FindSource = [](const Action *S) -> const Action * {
    while (S->getKind() != Action::InputClass) {
      assert(!S->getInputs().empty() && "unexpected root action!");
      S = S->getInputs()[0];
    }
    return S;
  };
  const Action *SourceAction = FindSource(&JA);

  // Forward -g and handle debug info related flags, assuming we are dealing
  // with an actual assembly file.
  bool WantDebug = false;
  Args.ClaimAllArgs(options::OPT_g_Group);
  if (Arg *A = Args.getLastArg(options::OPT_g_Group))
    WantDebug = !A->getOption().matches(options::OPT_g0) &&
                !A->getOption().matches(options::OPT_ggdb0);

  // If a -gdwarf argument appeared, remember it.
  bool EmitDwarf = false;
  if (const Arg *A = getDwarfNArg(Args))
    EmitDwarf = checkDebugInfoOption(A, Args, D, getToolChain());

  bool EmitCodeView = false;
  if (const Arg *A = Args.getLastArg(options::OPT_gcodeview))
    EmitCodeView = checkDebugInfoOption(A, Args, D, getToolChain());

  // If the user asked for debug info but did not explicitly specify -gcodeview
  // or -gdwarf, ask the toolchain for the default format.
  if (!EmitCodeView && !EmitDwarf && WantDebug) {
    switch (getToolChain().getDefaultDebugFormat()) {
    case llvm::codegenoptions::DIF_CodeView:
      EmitCodeView = true;
      break;
    case llvm::codegenoptions::DIF_DWARF:
      EmitDwarf = true;
      break;
    }
  }

  // If the arguments don't imply DWARF, don't emit any debug info here.
  if (!EmitDwarf)
    WantDebug = false;

  llvm::codegenoptions::DebugInfoKind DebugInfoKind =
      llvm::codegenoptions::NoDebugInfo;

  // Add the -fdebug-compilation-dir flag if needed.
  const char *DebugCompilationDir =
      addDebugCompDirArg(Args, CmdArgs, C.getDriver().getVFS());

  if (SourceAction->getType() == types::TY_Asm ||
      SourceAction->getType() == types::TY_PP_Asm) {
    // You might think that it would be ok to set DebugInfoKind outside of
    // the guard for source type, however there is a test which asserts
    // that some assembler invocation receives no -debug-info-kind,
    // and it's not clear whether that test is just overly restrictive.
    DebugInfoKind = (WantDebug ? llvm::codegenoptions::DebugInfoConstructor
                               : llvm::codegenoptions::NoDebugInfo);

    addDebugPrefixMapArg(getToolChain().getDriver(), getToolChain(), Args,
                         CmdArgs);

    // Set the AT_producer to the clang version when using the integrated
    // assembler on assembly source files.
    CmdArgs.push_back("-dwarf-debug-producer");
    CmdArgs.push_back(Args.MakeArgString(getClangFullVersion()));

    // And pass along -I options
    Args.AddAllArgs(CmdArgs, options::OPT_I);
  }
  const unsigned DwarfVersion = getDwarfVersion(getToolChain(), Args);
  RenderDebugEnablingArgs(Args, CmdArgs, DebugInfoKind, DwarfVersion,
                          llvm::DebuggerKind::Default);
  renderDwarfFormat(D, Triple, Args, CmdArgs, DwarfVersion);
  RenderDebugInfoCompressionArgs(Args, CmdArgs, D, getToolChain());

  // Handle -fPIC et al -- the relocation-model affects the assembler
  // for some targets.
  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) =
      ParsePICArgs(getToolChain(), Args);

  const char *RMName = RelocationModelName(RelocationModel);
  if (RMName) {
    CmdArgs.push_back("-mrelocation-model");
    CmdArgs.push_back(RMName);
  }

  // Optionally embed the -cc1as level arguments into the debug info, for build
  // analysis.
  if (getToolChain().UseDwarfDebugFlags()) {
    ArgStringList OriginalArgs;
    for (const auto &Arg : Args)
      Arg->render(Args, OriginalArgs);

    SmallString<256> Flags;
    const char *Exec = getToolChain().getDriver().getClangProgramPath();
    EscapeSpacesAndBackslashes(Exec, Flags);
    for (const char *OriginalArg : OriginalArgs) {
      SmallString<128> EscapedArg;
      EscapeSpacesAndBackslashes(OriginalArg, EscapedArg);
      Flags += " ";
      Flags += EscapedArg;
    }
    CmdArgs.push_back("-dwarf-debug-flags");
    CmdArgs.push_back(Args.MakeArgString(Flags));
  }

  // FIXME: Add -static support, once we have it.

  // Add target specific flags.
  switch (getToolChain().getArch()) {
  default:
    break;

  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    AddMIPSTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    AddX86TargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    // This isn't in AddARMTargetArgs because we want to do this for assembly
    // only, not C/C++.
    if (Args.hasFlag(options::OPT_mdefault_build_attributes,
                     options::OPT_mno_default_build_attributes, true)) {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back("-arm-add-build-attributes");
    }
    break;

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
    if (Args.hasArg(options::OPT_mmark_bti_property)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-aarch64-mark-bti-property");
    }
    break;

  case llvm::Triple::loongarch32:
  case llvm::Triple::loongarch64:
    AddLoongArchTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    AddRISCVTargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::hexagon:
    if (Args.hasFlag(options::OPT_mdefault_build_attributes,
                     options::OPT_mno_default_build_attributes, true)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-hexagon-add-build-attributes");
    }
    break;
  }

  // Consume all the warning flags. Usually this would be handled more
  // gracefully by -cc1 (warning about unknown warning flags, etc) but -cc1as
  // doesn't handle that so rather than warning about unused flags that are
  // actually used, we'll lie by omission instead.
  // FIXME: Stop lying and consume only the appropriate driver flags
  Args.ClaimAllArgs(options::OPT_W_Group);

  CollectArgsForIntegratedAssembler(C, Args, CmdArgs,
                                    getToolChain().getDriver());

  Args.AddAllArgs(CmdArgs, options::OPT_mllvm);

  if (DebugInfoKind > llvm::codegenoptions::NoDebugInfo && Output.isFilename())
    addDebugObjectName(Args, CmdArgs, DebugCompilationDir,
                       Output.getFilename());

  // Fixup any previous commands that use -object-file-name because when we
  // generated them, the final .obj name wasn't yet known.
  for (Command &J : C.getJobs()) {
    if (SourceAction != FindSource(&J.getSource()))
      continue;
    auto &JArgs = J.getArguments();
    for (unsigned I = 0; I < JArgs.size(); ++I) {
      if (StringRef(JArgs[I]).starts_with("-object-file-name=") &&
          Output.isFilename()) {
       ArgStringList NewArgs(JArgs.begin(), JArgs.begin() + I);
       addDebugObjectName(Args, NewArgs, DebugCompilationDir,
                          Output.getFilename());
       NewArgs.append(JArgs.begin() + I + 1, JArgs.end());
       J.replaceArguments(NewArgs);
       break;
      }
    }
  }

  assert(Output.isFilename() && "Unexpected lipo output.");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  const llvm::Triple &T = getToolChain().getTriple();
  Arg *A;
  if (getDebugFissionKind(D, Args, A) == DwarfFissionKind::Split &&
      T.isOSBinFormatELF()) {
    CmdArgs.push_back("-split-dwarf-output");
    CmdArgs.push_back(SplitDebugName(JA, Args, Input, Output));
  }

  if (Triple.isAMDGPU())
    handleAMDGPUCodeObjectVersionOptions(D, Args, CmdArgs, /*IsCC1As=*/true);

  assert(Input.isFilename() && "Invalid input.");
  CmdArgs.push_back(Input.getFilename());

  const char *Exec = getToolChain().getDriver().getClangProgramPath();
  if (D.CC1Main && !D.CCGenDiagnostics) {
    // Invoke cc1as directly in this process.
    C.addCommand(std::make_unique<CC1Command>(
        JA, *this, ResponseFileSupport::AtFileUTF8(), Exec, CmdArgs, Inputs,
        Output, D.getPrependArg()));
  } else {
    C.addCommand(std::make_unique<Command>(
        JA, *this, ResponseFileSupport::AtFileUTF8(), Exec, CmdArgs, Inputs,
        Output, D.getPrependArg()));
  }
}

// Begin OffloadBundler
void OffloadBundler::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const llvm::opt::ArgList &TCArgs,
                                  const char *LinkingOutput) const {
  // The version with only one output is expected to refer to a bundling job.
  assert(isa<OffloadBundlingJobAction>(JA) && "Expecting bundling job!");

  // The bundling command looks like this:
  // clang-offload-bundler -type=bc
  //   -targets=host-triple,openmp-triple1,openmp-triple2
  //   -output=output_file
  //   -input=unbundle_file_host
  //   -input=unbundle_file_tgt1
  //   -input=unbundle_file_tgt2

  ArgStringList CmdArgs;

  // Get the type.
  CmdArgs.push_back(TCArgs.MakeArgString(
      Twine("-type=") + types::getTypeTempSuffix(Output.getType())));

  assert(JA.getInputs().size() == Inputs.size() &&
         "Not have inputs for all dependence actions??");

  // Get the targets.
  SmallString<128> Triples;
  Triples += "-targets=";
  for (unsigned I = 0; I < Inputs.size(); ++I) {
    if (I)
      Triples += ',';

    // Find ToolChain for this input.
    Action::OffloadKind CurKind = Action::OFK_Host;
    const ToolChain *CurTC = &getToolChain();
    const Action *CurDep = JA.getInputs()[I];

    if (const auto *OA = dyn_cast<OffloadAction>(CurDep)) {
      CurTC = nullptr;
      OA->doOnEachDependence([&](Action *A, const ToolChain *TC, const char *) {
        assert(CurTC == nullptr && "Expected one dependence!");
        CurKind = A->getOffloadingDeviceKind();
        CurTC = TC;
      });
    }
    Triples += Action::GetOffloadKindName(CurKind);
    Triples += '-';
    Triples += CurTC->getTriple().normalize();
    if ((CurKind == Action::OFK_HIP || CurKind == Action::OFK_Cuda) &&
        !StringRef(CurDep->getOffloadingArch()).empty()) {
      Triples += '-';
      Triples += CurDep->getOffloadingArch();
    }

    // TODO: Replace parsing of -march flag. Can be done by storing GPUArch
    //       with each toolchain.
    StringRef GPUArchName;
    if (CurKind == Action::OFK_OpenMP) {
      // Extract GPUArch from -march argument in TC argument list.
      for (unsigned ArgIndex = 0; ArgIndex < TCArgs.size(); ArgIndex++) {
        auto ArchStr = StringRef(TCArgs.getArgString(ArgIndex));
        auto Arch = ArchStr.starts_with_insensitive("-march=");
        if (Arch) {
          GPUArchName = ArchStr.substr(7);
          Triples += "-";
          break;
        }
      }
      Triples += GPUArchName.str();
    }
  }
  CmdArgs.push_back(TCArgs.MakeArgString(Triples));

  // Get bundled file command.
  CmdArgs.push_back(
      TCArgs.MakeArgString(Twine("-output=") + Output.getFilename()));

  // Get unbundled files command.
  for (unsigned I = 0; I < Inputs.size(); ++I) {
    SmallString<128> UB;
    UB += "-input=";

    // Find ToolChain for this input.
    const ToolChain *CurTC = &getToolChain();
    if (const auto *OA = dyn_cast<OffloadAction>(JA.getInputs()[I])) {
      CurTC = nullptr;
      OA->doOnEachDependence([&](Action *, const ToolChain *TC, const char *) {
        assert(CurTC == nullptr && "Expected one dependence!");
        CurTC = TC;
      });
      UB += C.addTempFile(
          C.getArgs().MakeArgString(CurTC->getInputFilename(Inputs[I])));
    } else {
      UB += CurTC->getInputFilename(Inputs[I]);
    }
    CmdArgs.push_back(TCArgs.MakeArgString(UB));
  }
  addOffloadCompressArgs(TCArgs, CmdArgs);
  // All the inputs are encoded as commands.
  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::None(),
      TCArgs.MakeArgString(getToolChain().GetProgramPath(getShortName())),
      CmdArgs, std::nullopt, Output));
}

void OffloadBundler::ConstructJobMultipleOutputs(
    Compilation &C, const JobAction &JA, const InputInfoList &Outputs,
    const InputInfoList &Inputs, const llvm::opt::ArgList &TCArgs,
    const char *LinkingOutput) const {
  // The version with multiple outputs is expected to refer to a unbundling job.
  auto &UA = cast<OffloadUnbundlingJobAction>(JA);

  // The unbundling command looks like this:
  // clang-offload-bundler -type=bc
  //   -targets=host-triple,openmp-triple1,openmp-triple2
  //   -input=input_file
  //   -output=unbundle_file_host
  //   -output=unbundle_file_tgt1
  //   -output=unbundle_file_tgt2
  //   -unbundle

  ArgStringList CmdArgs;

  assert(Inputs.size() == 1 && "Expecting to unbundle a single file!");
  InputInfo Input = Inputs.front();

  // Get the type.
  CmdArgs.push_back(TCArgs.MakeArgString(
      Twine("-type=") + types::getTypeTempSuffix(Input.getType())));

  // Get the targets.
  SmallString<128> Triples;
  Triples += "-targets=";
  auto DepInfo = UA.getDependentActionsInfo();
  for (unsigned I = 0; I < DepInfo.size(); ++I) {
    if (I)
      Triples += ',';

    auto &Dep = DepInfo[I];
    Triples += Action::GetOffloadKindName(Dep.DependentOffloadKind);
    Triples += '-';
    Triples += Dep.DependentToolChain->getTriple().normalize();
    if ((Dep.DependentOffloadKind == Action::OFK_HIP ||
         Dep.DependentOffloadKind == Action::OFK_Cuda) &&
        !Dep.DependentBoundArch.empty()) {
      Triples += '-';
      Triples += Dep.DependentBoundArch;
    }
    // TODO: Replace parsing of -march flag. Can be done by storing GPUArch
    //       with each toolchain.
    StringRef GPUArchName;
    if (Dep.DependentOffloadKind == Action::OFK_OpenMP) {
      // Extract GPUArch from -march argument in TC argument list.
      for (unsigned ArgIndex = 0; ArgIndex < TCArgs.size(); ArgIndex++) {
        StringRef ArchStr = StringRef(TCArgs.getArgString(ArgIndex));
        auto Arch = ArchStr.starts_with_insensitive("-march=");
        if (Arch) {
          GPUArchName = ArchStr.substr(7);
          Triples += "-";
          break;
        }
      }
      Triples += GPUArchName.str();
    }
  }

  CmdArgs.push_back(TCArgs.MakeArgString(Triples));

  // Get bundled file command.
  CmdArgs.push_back(
      TCArgs.MakeArgString(Twine("-input=") + Input.getFilename()));

  // Get unbundled files command.
  for (unsigned I = 0; I < Outputs.size(); ++I) {
    SmallString<128> UB;
    UB += "-output=";
    UB += DepInfo[I].DependentToolChain->getInputFilename(Outputs[I]);
    CmdArgs.push_back(TCArgs.MakeArgString(UB));
  }
  CmdArgs.push_back("-unbundle");
  CmdArgs.push_back("-allow-missing-bundles");
  if (TCArgs.hasArg(options::OPT_v))
    CmdArgs.push_back("-verbose");

  // All the inputs are encoded as commands.
  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::None(),
      TCArgs.MakeArgString(getToolChain().GetProgramPath(getShortName())),
      CmdArgs, std::nullopt, Outputs));
}

void OffloadPackager::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const llvm::opt::ArgList &Args,
                                   const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  // Add the output file name.
  assert(Output.isFilename() && "Invalid output.");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  // Create the inputs to bundle the needed metadata.
  for (const InputInfo &Input : Inputs) {
    const Action *OffloadAction = Input.getAction();
    const ToolChain *TC = OffloadAction->getOffloadingToolChain();
    const ArgList &TCArgs =
        C.getArgsForToolChain(TC, OffloadAction->getOffloadingArch(),
                              OffloadAction->getOffloadingDeviceKind());
    StringRef File = C.getArgs().MakeArgString(TC->getInputFilename(Input));
    StringRef Arch = OffloadAction->getOffloadingArch()
                         ? OffloadAction->getOffloadingArch()
                         : TCArgs.getLastArgValue(options::OPT_march_EQ);
    StringRef Kind =
      Action::GetOffloadKindName(OffloadAction->getOffloadingDeviceKind());

    ArgStringList Features;
    SmallVector<StringRef> FeatureArgs;
    getTargetFeatures(TC->getDriver(), TC->getTriple(), TCArgs, Features,
                      false);
    llvm::copy_if(Features, std::back_inserter(FeatureArgs),
                  [](StringRef Arg) { return !Arg.starts_with("-target"); });

    if (TC->getTriple().isAMDGPU()) {
      for (StringRef Feature : llvm::split(Arch.split(':').second, ':')) {
        FeatureArgs.emplace_back(
            Args.MakeArgString(Feature.take_back() + Feature.drop_back()));
      }
    }

    // TODO: We need to pass in the full target-id and handle it properly in the
    // linker wrapper.
    SmallVector<std::string> Parts{
        "file=" + File.str(),
        "triple=" + TC->getTripleString(),
        "arch=" + Arch.str(),
        "kind=" + Kind.str(),
    };

    if (TC->getDriver().isUsingLTO(/* IsOffload */ true) ||
        TC->getTriple().isAMDGPU())
      for (StringRef Feature : FeatureArgs)
        Parts.emplace_back("feature=" + Feature.str());

    CmdArgs.push_back(Args.MakeArgString("--image=" + llvm::join(Parts, ",")));
  }

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::None(),
      Args.MakeArgString(getToolChain().GetProgramPath(getShortName())),
      CmdArgs, Inputs, Output));
}

void LinkerWrapper::ConstructJob(Compilation &C, const JobAction &JA,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) const {
  const Driver &D = getToolChain().getDriver();
  const llvm::Triple TheTriple = getToolChain().getTriple();
  ArgStringList CmdArgs;

  // Pass the CUDA path to the linker wrapper tool.
  for (Action::OffloadKind Kind : {Action::OFK_Cuda, Action::OFK_OpenMP}) {
    auto TCRange = C.getOffloadToolChains(Kind);
    for (auto &I : llvm::make_range(TCRange.first, TCRange.second)) {
      const ToolChain *TC = I.second;
      if (TC->getTriple().isNVPTX()) {
        CudaInstallationDetector CudaInstallation(D, TheTriple, Args);
        if (CudaInstallation.isValid())
          CmdArgs.push_back(Args.MakeArgString(
              "--cuda-path=" + CudaInstallation.getInstallPath()));
        break;
      }
    }
  }

  // Pass in the optimization level to use for LTO.
  if (const Arg *A = Args.getLastArg(options::OPT_O_Group)) {
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
    if (!OOpt.empty())
      CmdArgs.push_back(Args.MakeArgString(Twine("--opt-level=O") + OOpt));
  }

  CmdArgs.push_back(
      Args.MakeArgString("--host-triple=" + TheTriple.getTriple()));
  if (Args.hasArg(options::OPT_v))
    CmdArgs.push_back("--wrapper-verbose");

  if (const Arg *A = Args.getLastArg(options::OPT_g_Group)) {
    if (!A->getOption().matches(options::OPT_g0))
      CmdArgs.push_back("--device-debug");
  }

  // code-object-version=X needs to be passed to clang-linker-wrapper to ensure
  // that it is used by lld.
  if (const Arg *A = Args.getLastArg(options::OPT_mcode_object_version_EQ)) {
    CmdArgs.push_back(Args.MakeArgString("-mllvm"));
    CmdArgs.push_back(Args.MakeArgString(
        Twine("--amdhsa-code-object-version=") + A->getValue()));
  }

  for (const auto &A : Args.getAllArgValues(options::OPT_Xcuda_ptxas))
    CmdArgs.push_back(Args.MakeArgString("--ptxas-arg=" + A));

  // Forward remarks passes to the LLVM backend in the wrapper.
  if (const Arg *A = Args.getLastArg(options::OPT_Rpass_EQ))
    CmdArgs.push_back(Args.MakeArgString(Twine("--offload-opt=-pass-remarks=") +
                                         A->getValue()));
  if (const Arg *A = Args.getLastArg(options::OPT_Rpass_missed_EQ))
    CmdArgs.push_back(Args.MakeArgString(
        Twine("--offload-opt=-pass-remarks-missed=") + A->getValue()));
  if (const Arg *A = Args.getLastArg(options::OPT_Rpass_analysis_EQ))
    CmdArgs.push_back(Args.MakeArgString(
        Twine("--offload-opt=-pass-remarks-analysis=") + A->getValue()));
  if (Args.getLastArg(options::OPT_save_temps_EQ))
    CmdArgs.push_back("--save-temps");

  // Construct the link job so we can wrap around it.
  Linker->ConstructJob(C, JA, Output, Inputs, Args, LinkingOutput);
  const auto &LinkCommand = C.getJobs().getJobs().back();

  // Forward -Xoffload-linker<-triple> arguments to the device link job.
  for (Arg *A : Args.filtered(options::OPT_Xoffload_linker)) {
    StringRef Val = A->getValue(0);
    if (Val.empty())
      CmdArgs.push_back(
          Args.MakeArgString(Twine("--device-linker=") + A->getValue(1)));
    else
      CmdArgs.push_back(Args.MakeArgString(
          "--device-linker=" +
          ToolChain::getOpenMPTriple(Val.drop_front()).getTriple() + "=" +
          A->getValue(1)));
  }
  Args.ClaimAllArgs(options::OPT_Xoffload_linker);

  // Embed bitcode instead of an object in JIT mode.
  if (Args.hasFlag(options::OPT_fopenmp_target_jit,
                   options::OPT_fno_openmp_target_jit, false))
    CmdArgs.push_back("--embed-bitcode");

  // Forward `-mllvm` arguments to the LLVM invocations if present.
  for (Arg *A : Args.filtered(options::OPT_mllvm)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(A->getValue());
    A->claim();
  }

  // Add the linker arguments to be forwarded by the wrapper.
  CmdArgs.push_back(Args.MakeArgString(Twine("--linker-path=") +
                                       LinkCommand->getExecutable()));
  for (const char *LinkArg : LinkCommand->getArguments())
    CmdArgs.push_back(LinkArg);

  addOffloadCompressArgs(Args, CmdArgs);

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("clang-linker-wrapper"));

  // Replace the executable and arguments of the link job with the
  // wrapper.
  LinkCommand->replaceExecutable(Exec);
  LinkCommand->replaceArguments(CmdArgs);
}
