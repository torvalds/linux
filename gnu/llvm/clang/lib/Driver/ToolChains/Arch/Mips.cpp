//===--- Mips.cpp - Tools Implementations -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Mips.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

// Get CPU and ABI names. They are not independent
// so we have to calculate them together.
void mips::getMipsCPUAndABI(const ArgList &Args, const llvm::Triple &Triple,
                            StringRef &CPUName, StringRef &ABIName) {
  const char *DefMips32CPU = "mips32r2";
  const char *DefMips64CPU = "mips64r2";

  // MIPS32r6 is the default for mips(el)?-img-linux-gnu and MIPS64r6 is the
  // default for mips64(el)?-img-linux-gnu.
  if (Triple.getVendor() == llvm::Triple::ImaginationTechnologies &&
      Triple.isGNUEnvironment()) {
    DefMips32CPU = "mips32r6";
    DefMips64CPU = "mips64r6";
  }

  if (Triple.getSubArch() == llvm::Triple::MipsSubArch_r6) {
    DefMips32CPU = "mips32r6";
    DefMips64CPU = "mips64r6";
  }

  // MIPS3 is the default for mips64*-unknown-openbsd.
  if (Triple.isOSOpenBSD())
    DefMips64CPU = "mips3";

  // MIPS2 is the default for mips(el)?-unknown-freebsd.
  // MIPS3 is the default for mips64(el)?-unknown-freebsd.
  if (Triple.isOSFreeBSD()) {
    DefMips32CPU = "mips2";
    DefMips64CPU = "mips3";
  }

  if (Arg *A = Args.getLastArg(clang::driver::options::OPT_march_EQ,
                               options::OPT_mcpu_EQ))
    CPUName = A->getValue();

  if (Arg *A = Args.getLastArg(options::OPT_mabi_EQ)) {
    ABIName = A->getValue();
    // Convert a GNU style Mips ABI name to the name
    // accepted by LLVM Mips backend.
    ABIName = llvm::StringSwitch<llvm::StringRef>(ABIName)
                  .Case("32", "o32")
                  .Case("64", "n64")
                  .Default(ABIName);
  }

  // Setup default CPU and ABI names.
  if (CPUName.empty() && ABIName.empty()) {
    switch (Triple.getArch()) {
    default:
      llvm_unreachable("Unexpected triple arch name");
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
      CPUName = DefMips32CPU;
      break;
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
      CPUName = DefMips64CPU;
      break;
    }
  }

  if (ABIName.empty() && (Triple.getEnvironment() == llvm::Triple::GNUABIN32))
    ABIName = "n32";

  if (ABIName.empty() &&
      (Triple.getVendor() == llvm::Triple::MipsTechnologies ||
       Triple.getVendor() == llvm::Triple::ImaginationTechnologies)) {
    ABIName = llvm::StringSwitch<const char *>(CPUName)
                  .Case("mips1", "o32")
                  .Case("mips2", "o32")
                  .Case("mips3", "n64")
                  .Case("mips4", "n64")
                  .Case("mips5", "n64")
                  .Case("mips32", "o32")
                  .Case("mips32r2", "o32")
                  .Case("mips32r3", "o32")
                  .Case("mips32r5", "o32")
                  .Case("mips32r6", "o32")
                  .Case("mips64", "n64")
                  .Case("mips64r2", "n64")
                  .Case("mips64r3", "n64")
                  .Case("mips64r5", "n64")
                  .Case("mips64r6", "n64")
                  .Case("octeon", "n64")
                  .Case("p5600", "o32")
                  .Default("");
  }

  if (ABIName.empty()) {
    // Deduce ABI name from the target triple.
    ABIName = Triple.isMIPS32() ? "o32" : "n64";
  }

  if (CPUName.empty()) {
    // Deduce CPU name from ABI name.
    CPUName = llvm::StringSwitch<const char *>(ABIName)
                  .Case("o32", DefMips32CPU)
                  .Cases("n32", "n64", DefMips64CPU)
                  .Default("");
  }

  // FIXME: Warn on inconsistent use of -march and -mabi.
}

std::string mips::getMipsABILibSuffix(const ArgList &Args,
                                      const llvm::Triple &Triple) {
  StringRef CPUName, ABIName;
  tools::mips::getMipsCPUAndABI(Args, Triple, CPUName, ABIName);
  return llvm::StringSwitch<std::string>(ABIName)
      .Case("o32", "")
      .Case("n32", "32")
      .Case("n64", "64");
}

// Convert ABI name to the GNU tools acceptable variant.
StringRef mips::getGnuCompatibleMipsABIName(StringRef ABI) {
  return llvm::StringSwitch<llvm::StringRef>(ABI)
      .Case("o32", "32")
      .Case("n64", "64")
      .Default(ABI);
}

// Select the MIPS float ABI as determined by -msoft-float, -mhard-float,
// and -mfloat-abi=.
mips::FloatABI mips::getMipsFloatABI(const Driver &D, const ArgList &Args,
                                     const llvm::Triple &Triple) {
  mips::FloatABI ABI = mips::FloatABI::Invalid;
  if (Arg *A =
          Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                          options::OPT_mfloat_abi_EQ)) {
    if (A->getOption().matches(options::OPT_msoft_float))
      ABI = mips::FloatABI::Soft;
    else if (A->getOption().matches(options::OPT_mhard_float))
      ABI = mips::FloatABI::Hard;
    else {
      ABI = llvm::StringSwitch<mips::FloatABI>(A->getValue())
                .Case("soft", mips::FloatABI::Soft)
                .Case("hard", mips::FloatABI::Hard)
                .Default(mips::FloatABI::Invalid);
      if (ABI == mips::FloatABI::Invalid && !StringRef(A->getValue()).empty()) {
        D.Diag(clang::diag::err_drv_invalid_mfloat_abi) << A->getAsString(Args);
        ABI = mips::FloatABI::Hard;
      }
    }
  }

  // If unspecified, choose the default based on the platform.
  if (ABI == mips::FloatABI::Invalid) {
    if (Triple.isOSFreeBSD()) {
      // For FreeBSD, assume "soft" on all flavors of MIPS.
      ABI = mips::FloatABI::Soft;
    } else {
      // Assume "hard", because it's a default value used by gcc.
      // When we start to recognize specific target MIPS processors,
      // we will be able to select the default more correctly.
      ABI = mips::FloatABI::Hard;
    }
  }

  assert(ABI != mips::FloatABI::Invalid && "must select an ABI");
  return ABI;
}

void mips::getMIPSTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args,
                                 std::vector<StringRef> &Features) {
  StringRef CPUName;
  StringRef ABIName;
  getMipsCPUAndABI(Args, Triple, CPUName, ABIName);
  ABIName = getGnuCompatibleMipsABIName(ABIName);

  // Historically, PIC code for MIPS was associated with -mabicalls, a.k.a
  // SVR4 abicalls. Static code does not use SVR4 calling sequences. An ABI
  // extension was developed by Richard Sandiford & Code Sourcery to support
  // static code calling PIC code (CPIC). For O32 and N32 this means we have
  // several combinations of PIC/static and abicalls. Pure static, static
  // with the CPIC extension, and pure PIC code.

  // At final link time, O32 and N32 with CPIC will have another section
  // added to the binary which contains the stub functions to perform
  // any fixups required for PIC code.

  // For N64, the situation is more regular: code can either be static
  // (non-abicalls) or PIC (abicalls). GCC has traditionally picked PIC code
  // code for N64. Since Clang has already built the relocation model portion
  // of the commandline, we pick add +noabicalls feature in the N64 static
  // case.

  // The is another case to be accounted for: -msym32, which enforces that all
  // symbols have 32 bits in size. In this case, N64 can in theory use CPIC
  // but it is unsupported.

  // The combinations for N64 are:
  // a) Static without abicalls and 64bit symbols.
  // b) Static with abicalls and 32bit symbols.
  // c) PIC with abicalls and 64bit symbols.

  // For case (a) we need to add +noabicalls for N64.

  bool IsN64 = ABIName == "64";
  bool IsPIC = false;
  bool NonPIC = false;
  bool HasNaN2008Opt = false;

  Arg *LastPICArg = Args.getLastArg(options::OPT_fPIC, options::OPT_fno_PIC,
                                    options::OPT_fpic, options::OPT_fno_pic,
                                    options::OPT_fPIE, options::OPT_fno_PIE,
                                    options::OPT_fpie, options::OPT_fno_pie);
  if (LastPICArg) {
    Option O = LastPICArg->getOption();
    NonPIC =
        (O.matches(options::OPT_fno_PIC) || O.matches(options::OPT_fno_pic) ||
         O.matches(options::OPT_fno_PIE) || O.matches(options::OPT_fno_pie));
    IsPIC =
        (O.matches(options::OPT_fPIC) || O.matches(options::OPT_fpic) ||
         O.matches(options::OPT_fPIE) || O.matches(options::OPT_fpie));
  }

  bool UseAbiCalls = false;

  Arg *ABICallsArg =
      Args.getLastArg(options::OPT_mabicalls, options::OPT_mno_abicalls);
  UseAbiCalls =
      !ABICallsArg || ABICallsArg->getOption().matches(options::OPT_mabicalls);

  if (IsN64 && NonPIC && (!ABICallsArg || UseAbiCalls)) {
    D.Diag(diag::warn_drv_unsupported_pic_with_mabicalls)
        << LastPICArg->getAsString(Args) << (!ABICallsArg ? 0 : 1);
  }

  if (ABICallsArg && !UseAbiCalls && IsPIC) {
    D.Diag(diag::err_drv_unsupported_noabicalls_pic);
  }

  if (!UseAbiCalls)
    Features.push_back("+noabicalls");
  else
    Features.push_back("-noabicalls");

  if (Arg *A = Args.getLastArg(options::OPT_mlong_calls,
                               options::OPT_mno_long_calls)) {
    if (A->getOption().matches(options::OPT_mno_long_calls))
      Features.push_back("-long-calls");
    else if (!UseAbiCalls)
      Features.push_back("+long-calls");
    else
      D.Diag(diag::warn_drv_unsupported_longcalls) << (ABICallsArg ? 0 : 1);
  }

  if (Arg *A = Args.getLastArg(options::OPT_mxgot, options::OPT_mno_xgot)) {
    if (A->getOption().matches(options::OPT_mxgot))
      Features.push_back("+xgot");
    else
      Features.push_back("-xgot");
  }

  mips::FloatABI FloatABI = mips::getMipsFloatABI(D, Args, Triple);
  if (FloatABI == mips::FloatABI::Soft) {
    // FIXME: Note, this is a hack. We need to pass the selected float
    // mode to the MipsTargetInfoBase to define appropriate macros there.
    // Now it is the only method.
    Features.push_back("+soft-float");
  }

  if (Arg *A = Args.getLastArg(options::OPT_mnan_EQ)) {
    StringRef Val = StringRef(A->getValue());
    if (Val == "2008") {
      if (mips::getIEEE754Standard(CPUName) & mips::Std2008) {
        Features.push_back("+nan2008");
        HasNaN2008Opt = true;
      } else {
        Features.push_back("-nan2008");
        D.Diag(diag::warn_target_unsupported_nan2008) << CPUName;
      }
    } else if (Val == "legacy") {
      if (mips::getIEEE754Standard(CPUName) & mips::Legacy)
        Features.push_back("-nan2008");
      else {
        Features.push_back("+nan2008");
        D.Diag(diag::warn_target_unsupported_nanlegacy) << CPUName;
      }
    } else
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
  }

  if (Arg *A = Args.getLastArg(options::OPT_mabs_EQ)) {
    StringRef Val = StringRef(A->getValue());
    if (Val == "2008") {
      if (mips::getIEEE754Standard(CPUName) & mips::Std2008) {
        Features.push_back("+abs2008");
      } else {
        Features.push_back("-abs2008");
        D.Diag(diag::warn_target_unsupported_abs2008) << CPUName;
      }
    } else if (Val == "legacy") {
      if (mips::getIEEE754Standard(CPUName) & mips::Legacy) {
        Features.push_back("-abs2008");
      } else {
        Features.push_back("+abs2008");
        D.Diag(diag::warn_target_unsupported_abslegacy) << CPUName;
      }
    } else {
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
    }
  } else if (HasNaN2008Opt) {
    Features.push_back("+abs2008");
  }

  AddTargetFeature(Args, Features, options::OPT_msingle_float,
                   options::OPT_mdouble_float, "single-float");
  AddTargetFeature(Args, Features, options::OPT_mips16, options::OPT_mno_mips16,
                   "mips16");
  AddTargetFeature(Args, Features, options::OPT_mmicromips,
                   options::OPT_mno_micromips, "micromips");
  AddTargetFeature(Args, Features, options::OPT_mdsp, options::OPT_mno_dsp,
                   "dsp");
  AddTargetFeature(Args, Features, options::OPT_mdspr2, options::OPT_mno_dspr2,
                   "dspr2");
  AddTargetFeature(Args, Features, options::OPT_mmsa, options::OPT_mno_msa,
                   "msa");
  if (Arg *A = Args.getLastArg(
          options::OPT_mstrict_align, options::OPT_mno_strict_align,
          options::OPT_mno_unaligned_access, options::OPT_munaligned_access)) {
    if (A->getOption().matches(options::OPT_mstrict_align) ||
        A->getOption().matches(options::OPT_mno_unaligned_access))
      Features.push_back(Args.MakeArgString("+strict-align"));
    else
      Features.push_back(Args.MakeArgString("-strict-align"));
  }

  // Add the last -mfp32/-mfpxx/-mfp64, if none are given and the ABI is O32
  // pass -mfpxx, or if none are given and fp64a is default, pass fp64 and
  // nooddspreg.
  if (Arg *A = Args.getLastArg(options::OPT_mfp32, options::OPT_mfpxx,
                               options::OPT_mfp64)) {
    if (A->getOption().matches(options::OPT_mfp32))
      Features.push_back("-fp64");
    else if (A->getOption().matches(options::OPT_mfpxx)) {
      Features.push_back("+fpxx");
      Features.push_back("+nooddspreg");
    } else
      Features.push_back("+fp64");
  } else if (mips::shouldUseFPXX(Args, Triple, CPUName, ABIName, FloatABI)) {
    Features.push_back("+fpxx");
    Features.push_back("+nooddspreg");
  } else if (mips::isFP64ADefault(Triple, CPUName)) {
    Features.push_back("+fp64");
    Features.push_back("+nooddspreg");
  } else if (Arg *A = Args.getLastArg(options::OPT_mmsa)) {
    if (A->getOption().matches(options::OPT_mmsa))
      Features.push_back("+fp64");
  }

  AddTargetFeature(Args, Features, options::OPT_mno_odd_spreg,
                   options::OPT_modd_spreg, "nooddspreg");
  AddTargetFeature(Args, Features, options::OPT_mno_madd4, options::OPT_mmadd4,
                   "nomadd4");
  AddTargetFeature(Args, Features, options::OPT_mmt, options::OPT_mno_mt, "mt");
  AddTargetFeature(Args, Features, options::OPT_mcrc, options::OPT_mno_crc,
                   "crc");
  AddTargetFeature(Args, Features, options::OPT_mvirt, options::OPT_mno_virt,
                   "virt");
  AddTargetFeature(Args, Features, options::OPT_mginv, options::OPT_mno_ginv,
                   "ginv");

  if (Arg *A = Args.getLastArg(options::OPT_mindirect_jump_EQ)) {
    StringRef Val = StringRef(A->getValue());
    if (Val == "hazard") {
      Arg *B =
          Args.getLastArg(options::OPT_mmicromips, options::OPT_mno_micromips);
      Arg *C = Args.getLastArg(options::OPT_mips16, options::OPT_mno_mips16);

      if (B && B->getOption().matches(options::OPT_mmicromips))
        D.Diag(diag::err_drv_unsupported_indirect_jump_opt)
            << "hazard" << "micromips";
      else if (C && C->getOption().matches(options::OPT_mips16))
        D.Diag(diag::err_drv_unsupported_indirect_jump_opt)
            << "hazard" << "mips16";
      else if (mips::supportsIndirectJumpHazardBarrier(CPUName))
        Features.push_back("+use-indirect-jump-hazard");
      else
        D.Diag(diag::err_drv_unsupported_indirect_jump_opt)
            << "hazard" << CPUName;
    } else
      D.Diag(diag::err_drv_unknown_indirect_jump_opt) << Val;
  }
}

mips::IEEE754Standard mips::getIEEE754Standard(StringRef &CPU) {
  // Strictly speaking, mips32r2 and mips64r2 do not conform to the
  // IEEE754-2008 standard. Support for this standard was first introduced
  // in Release 3. However, other compilers have traditionally allowed it
  // for Release 2 so we should do the same.
  return (IEEE754Standard)llvm::StringSwitch<int>(CPU)
      .Case("mips1", Legacy)
      .Case("mips2", Legacy)
      .Case("mips3", Legacy)
      .Case("mips4", Legacy)
      .Case("mips5", Legacy)
      .Case("mips32", Legacy)
      .Case("mips32r2", Legacy | Std2008)
      .Case("mips32r3", Legacy | Std2008)
      .Case("mips32r5", Legacy | Std2008)
      .Case("mips32r6", Std2008)
      .Case("mips64", Legacy)
      .Case("mips64r2", Legacy | Std2008)
      .Case("mips64r3", Legacy | Std2008)
      .Case("mips64r5", Legacy | Std2008)
      .Case("mips64r6", Std2008)
      .Default(Std2008);
}

bool mips::hasCompactBranches(StringRef &CPU) {
  // mips32r6 and mips64r6 have compact branches.
  return llvm::StringSwitch<bool>(CPU)
      .Case("mips32r6", true)
      .Case("mips64r6", true)
      .Default(false);
}

bool mips::hasMipsAbiArg(const ArgList &Args, const char *Value) {
  Arg *A = Args.getLastArg(options::OPT_mabi_EQ);
  return A && (A->getValue() == StringRef(Value));
}

bool mips::isUCLibc(const ArgList &Args) {
  Arg *A = Args.getLastArg(options::OPT_m_libc_Group);
  return A && A->getOption().matches(options::OPT_muclibc);
}

bool mips::isNaN2008(const Driver &D, const ArgList &Args,
                     const llvm::Triple &Triple) {
  if (Arg *NaNArg = Args.getLastArg(options::OPT_mnan_EQ))
    return llvm::StringSwitch<bool>(NaNArg->getValue())
        .Case("2008", true)
        .Case("legacy", false)
        .Default(false);

  // NaN2008 is the default for MIPS32r6/MIPS64r6.
  return llvm::StringSwitch<bool>(getCPUName(D, Args, Triple))
      .Cases("mips32r6", "mips64r6", true)
      .Default(false);
}

bool mips::isFP64ADefault(const llvm::Triple &Triple, StringRef CPUName) {
  if (!Triple.isAndroid())
    return false;

  // Android MIPS32R6 defaults to FP64A.
  return llvm::StringSwitch<bool>(CPUName)
      .Case("mips32r6", true)
      .Default(false);
}

bool mips::isFPXXDefault(const llvm::Triple &Triple, StringRef CPUName,
                         StringRef ABIName, mips::FloatABI FloatABI) {
  if (ABIName != "32")
    return false;

  // FPXX shouldn't be used if either -msoft-float or -mfloat-abi=soft is
  // present.
  if (FloatABI == mips::FloatABI::Soft)
    return false;

  return llvm::StringSwitch<bool>(CPUName)
      .Cases("mips2", "mips3", "mips4", "mips5", true)
      .Cases("mips32", "mips32r2", "mips32r3", "mips32r5", true)
      .Cases("mips64", "mips64r2", "mips64r3", "mips64r5", true)
      .Default(false);
}

bool mips::shouldUseFPXX(const ArgList &Args, const llvm::Triple &Triple,
                         StringRef CPUName, StringRef ABIName,
                         mips::FloatABI FloatABI) {
  bool UseFPXX = isFPXXDefault(Triple, CPUName, ABIName, FloatABI);

  // FPXX shouldn't be used if -msingle-float is present.
  if (Arg *A = Args.getLastArg(options::OPT_msingle_float,
                               options::OPT_mdouble_float))
    if (A->getOption().matches(options::OPT_msingle_float))
      UseFPXX = false;
  // FP64 should be used for MSA.
  if (Arg *A = Args.getLastArg(options::OPT_mmsa))
    if (A->getOption().matches(options::OPT_mmsa))
      UseFPXX = llvm::StringSwitch<bool>(CPUName)
                    .Cases("mips32r2", "mips32r3", "mips32r5", false)
                    .Cases("mips64r2", "mips64r3", "mips64r5", false)
                    .Default(UseFPXX);

  return UseFPXX;
}

bool mips::supportsIndirectJumpHazardBarrier(StringRef &CPU) {
  // Supporting the hazard barrier method of dealing with indirect
  // jumps requires MIPSR2 support.
  return llvm::StringSwitch<bool>(CPU)
      .Case("mips32r2", true)
      .Case("mips32r3", true)
      .Case("mips32r5", true)
      .Case("mips32r6", true)
      .Case("mips64r2", true)
      .Case("mips64r3", true)
      .Case("mips64r5", true)
      .Case("mips64r6", true)
      .Case("octeon", true)
      .Case("p5600", true)
      .Default(false);
}
