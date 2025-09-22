//===--- OSTargets.cpp - Implement OS target feature support --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements OS specific TargetInfo types.
//===----------------------------------------------------------------------===//

#include "OSTargets.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::targets;

namespace clang {
namespace targets {

void getDarwinDefines(MacroBuilder &Builder, const LangOptions &Opts,
                      const llvm::Triple &Triple, StringRef &PlatformName,
                      VersionTuple &PlatformMinVersion) {
  Builder.defineMacro("__APPLE_CC__", "6000");
  Builder.defineMacro("__APPLE__");
  Builder.defineMacro("__STDC_NO_THREADS__");

  // AddressSanitizer doesn't play well with source fortification, which is on
  // by default on Darwin.
  if (Opts.Sanitize.has(SanitizerKind::Address))
    Builder.defineMacro("_FORTIFY_SOURCE", "0");

  // Darwin defines __weak, __strong, and __unsafe_unretained even in C mode.
  if (!Opts.ObjC) {
    // __weak is always defined, for use in blocks and with objc pointers.
    Builder.defineMacro("__weak", "__attribute__((objc_gc(weak)))");
    Builder.defineMacro("__strong", "");
    Builder.defineMacro("__unsafe_unretained", "");
  }

  if (Opts.Static)
    Builder.defineMacro("__STATIC__");
  else
    Builder.defineMacro("__DYNAMIC__");

  if (Opts.POSIXThreads)
    Builder.defineMacro("_REENTRANT");

  // Get the platform type and version number from the triple.
  VersionTuple OsVersion;
  if (Triple.isMacOSX()) {
    Triple.getMacOSXVersion(OsVersion);
    PlatformName = "macos";
  } else {
    OsVersion = Triple.getOSVersion();
    PlatformName = llvm::Triple::getOSTypeName(Triple.getOS());
    if (PlatformName == "ios" && Triple.isMacCatalystEnvironment())
      PlatformName = "maccatalyst";
  }

  // If -target arch-pc-win32-macho option specified, we're
  // generating code for Win32 ABI. No need to emit
  // __ENVIRONMENT_XX_OS_VERSION_MIN_REQUIRED__.
  if (PlatformName == "win32") {
    PlatformMinVersion = OsVersion;
    return;
  }

  assert(OsVersion < VersionTuple(100) && "Invalid version!");
  char Str[7];
  if (Triple.isMacOSX() && OsVersion < VersionTuple(10, 10)) {
    Str[0] = '0' + (OsVersion.getMajor() / 10);
    Str[1] = '0' + (OsVersion.getMajor() % 10);
    Str[2] = '0' + std::min(OsVersion.getMinor().value_or(0), 9U);
    Str[3] = '0' + std::min(OsVersion.getSubminor().value_or(0), 9U);
    Str[4] = '\0';
  } else if (!Triple.isMacOSX() && OsVersion.getMajor() < 10) {
    Str[0] = '0' + OsVersion.getMajor();
    Str[1] = '0' + (OsVersion.getMinor().value_or(0) / 10);
    Str[2] = '0' + (OsVersion.getMinor().value_or(0) % 10);
    Str[3] = '0' + (OsVersion.getSubminor().value_or(0) / 10);
    Str[4] = '0' + (OsVersion.getSubminor().value_or(0) % 10);
    Str[5] = '\0';
  } else {
    // Handle versions >= 10.
    Str[0] = '0' + (OsVersion.getMajor() / 10);
    Str[1] = '0' + (OsVersion.getMajor() % 10);
    Str[2] = '0' + (OsVersion.getMinor().value_or(0) / 10);
    Str[3] = '0' + (OsVersion.getMinor().value_or(0) % 10);
    Str[4] = '0' + (OsVersion.getSubminor().value_or(0) / 10);
    Str[5] = '0' + (OsVersion.getSubminor().value_or(0) % 10);
    Str[6] = '\0';
  }

  // Set the appropriate OS version define.
  if (Triple.isTvOS()) {
    Builder.defineMacro("__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__", Str);
  } else if (Triple.isiOS()) {
    Builder.defineMacro("__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__", Str);
  } else if (Triple.isWatchOS()) {
    Builder.defineMacro("__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__", Str);
  } else if (Triple.isDriverKit()) {
    assert(OsVersion.getMinor().value_or(0) < 100 &&
           OsVersion.getSubminor().value_or(0) < 100 && "Invalid version!");
    Builder.defineMacro("__ENVIRONMENT_DRIVERKIT_VERSION_MIN_REQUIRED__", Str);
  } else if (Triple.isMacOSX()) {
    Builder.defineMacro("__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__", Str);
  }

  if (Triple.isOSDarwin()) {
    // Any darwin OS defines a general darwin OS version macro in addition
    // to the other OS specific macros.
    assert(OsVersion.getMinor().value_or(0) < 100 &&
           OsVersion.getSubminor().value_or(0) < 100 && "Invalid version!");
    Builder.defineMacro("__ENVIRONMENT_OS_VERSION_MIN_REQUIRED__", Str);

    // Tell users about the kernel if there is one.
    Builder.defineMacro("__MACH__");
  }

  PlatformMinVersion = OsVersion;
}

static void addMinGWDefines(const llvm::Triple &Triple, const LangOptions &Opts,
                            MacroBuilder &Builder) {
  DefineStd(Builder, "WIN32", Opts);
  DefineStd(Builder, "WINNT", Opts);
  if (Triple.isArch64Bit()) {
    DefineStd(Builder, "WIN64", Opts);
    Builder.defineMacro("__MINGW64__");
  }
  Builder.defineMacro("__MSVCRT__");
  Builder.defineMacro("__MINGW32__");
  addCygMingDefines(Opts, Builder);
}

static void addVisualCDefines(const LangOptions &Opts, MacroBuilder &Builder) {
  if (Opts.CPlusPlus) {
    if (Opts.RTTIData)
      Builder.defineMacro("_CPPRTTI");

    if (Opts.CXXExceptions)
      Builder.defineMacro("_CPPUNWIND");
  }

  if (Opts.Bool)
    Builder.defineMacro("__BOOL_DEFINED");

  if (!Opts.CharIsSigned)
    Builder.defineMacro("_CHAR_UNSIGNED");

  // "The /fp:contract option allows the compiler to generate floating-point
  // contractions [...]"
  if (Opts.getDefaultFPContractMode() != LangOptions::FPModeKind::FPM_Off)
    Builder.defineMacro("_M_FP_CONTRACT");

  // "The /fp:except option generates code to ensures that any unmasked
  // floating-point exceptions are raised at the exact point at which they
  // occur, and that no other floating-point exceptions are raised."
  if (Opts.getDefaultExceptionMode() ==
      LangOptions::FPExceptionModeKind::FPE_Strict)
    Builder.defineMacro("_M_FP_EXCEPT");

  // "The /fp:fast option allows the compiler to reorder, combine, or simplify
  // floating-point operations to optimize floating-point code for speed and
  // space. The compiler may omit rounding at assignment statements,
  // typecasts, or function calls. It may reorder operations or make algebraic
  // transforms, for example, by use of associative and distributive laws. It
  // may reorder code even if such transformations result in observably
  // different rounding behavior."
  //
  // "Under /fp:precise and /fp:strict, the compiler doesn't do any mathematical
  // transformation unless the transformation is guaranteed to produce a bitwise
  // identical result."
  const bool any_imprecise_flags =
      Opts.FastMath || Opts.FiniteMathOnly || Opts.UnsafeFPMath ||
      Opts.AllowFPReassoc || Opts.NoHonorNaNs || Opts.NoHonorInfs ||
      Opts.NoSignedZero || Opts.AllowRecip || Opts.ApproxFunc;

  // "Under both /fp:precise and /fp:fast, the compiler generates code intended
  // to run in the default floating-point environment."
  //
  // "[The] default floating point environment [...] sets the rounding mode
  // to round to nearest."
  if (Opts.getDefaultRoundingMode() ==
      LangOptions::RoundingMode::NearestTiesToEven) {
    if (any_imprecise_flags) {
      Builder.defineMacro("_M_FP_FAST");
    } else {
      Builder.defineMacro("_M_FP_PRECISE");
    }
  } else if (!any_imprecise_flags && Opts.getDefaultRoundingMode() ==
                                         LangOptions::RoundingMode::Dynamic) {
    // "Under /fp:strict, the compiler generates code that allows the
    // program to safely unmask floating-point exceptions, read or write
    // floating-point status registers, or change rounding modes."
    Builder.defineMacro("_M_FP_STRICT");
  }

  // FIXME: POSIXThreads isn't exactly the option this should be defined for,
  //        but it works for now.
  if (Opts.POSIXThreads)
    Builder.defineMacro("_MT");

  if (Opts.MSCompatibilityVersion) {
    Builder.defineMacro("_MSC_VER",
                        Twine(Opts.MSCompatibilityVersion / 100000));
    Builder.defineMacro("_MSC_FULL_VER", Twine(Opts.MSCompatibilityVersion));
    // FIXME We cannot encode the revision information into 32-bits
    Builder.defineMacro("_MSC_BUILD", Twine(1));

    if (Opts.CPlusPlus11 && Opts.isCompatibleWithMSVC(LangOptions::MSVC2015))
      Builder.defineMacro("_HAS_CHAR16_T_LANGUAGE_SUPPORT", Twine(1));

    if (Opts.isCompatibleWithMSVC(LangOptions::MSVC2015)) {
      if (Opts.CPlusPlus23)
        // TODO update to the proper value.
        Builder.defineMacro("_MSVC_LANG", "202004L");
      else if (Opts.CPlusPlus20)
        Builder.defineMacro("_MSVC_LANG", "202002L");
      else if (Opts.CPlusPlus17)
        Builder.defineMacro("_MSVC_LANG", "201703L");
      else if (Opts.CPlusPlus14)
        Builder.defineMacro("_MSVC_LANG", "201402L");
    }

    if (Opts.isCompatibleWithMSVC(LangOptions::MSVC2022_3))
      Builder.defineMacro("_MSVC_CONSTEXPR_ATTRIBUTE");
  }

  if (Opts.MicrosoftExt) {
    Builder.defineMacro("_MSC_EXTENSIONS");

    if (Opts.CPlusPlus11) {
      Builder.defineMacro("_RVALUE_REFERENCES_V2_SUPPORTED");
      Builder.defineMacro("_RVALUE_REFERENCES_SUPPORTED");
      Builder.defineMacro("_NATIVE_NULLPTR_SUPPORTED");
    }
  }

  if (!Opts.MSVolatile)
    Builder.defineMacro("_ISO_VOLATILE");

  if (Opts.Kernel)
    Builder.defineMacro("_KERNEL_MODE");

  Builder.defineMacro("_INTEGRAL_MAX_BITS", "64");
  Builder.defineMacro("__STDC_NO_THREADS__");

  // Starting with VS 2022 17.1, MSVC predefines the below macro to inform
  // users of the execution character set defined at compile time.
  // The value given is the Windows Code Page Identifier:
  // https://docs.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
  //
  // Clang currently only supports UTF-8, so we'll use 65001
  Builder.defineMacro("_MSVC_EXECUTION_CHARACTER_SET", "65001");
}

void addWindowsDefines(const llvm::Triple &Triple, const LangOptions &Opts,
                       MacroBuilder &Builder) {
  Builder.defineMacro("_WIN32");
  if (Triple.isArch64Bit())
    Builder.defineMacro("_WIN64");
  if (Triple.isWindowsGNUEnvironment())
    addMinGWDefines(Triple, Opts, Builder);
  else if (Triple.isKnownWindowsMSVCEnvironment() ||
           (Triple.isWindowsItaniumEnvironment() && Opts.MSVCCompat))
    addVisualCDefines(Opts, Builder);
}

} // namespace targets
} // namespace clang
