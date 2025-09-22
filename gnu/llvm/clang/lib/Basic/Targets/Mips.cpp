//===--- Mips.cpp - Implement Mips target feature support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Mips TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "Mips.h"
#include "Targets.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

static constexpr Builtin::Info BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define LIBBUILTIN(ID, TYPE, ATTRS, HEADER)                                    \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::HEADER, ALL_LANGUAGES},
#include "clang/Basic/BuiltinsMips.def"
};

bool MipsTargetInfo::processorSupportsGPR64() const {
  return llvm::StringSwitch<bool>(CPU)
      .Case("mips3", true)
      .Case("mips4", true)
      .Case("mips5", true)
      .Case("mips64", true)
      .Case("mips64r2", true)
      .Case("mips64r3", true)
      .Case("mips64r5", true)
      .Case("mips64r6", true)
      .Case("octeon", true)
      .Case("octeon+", true)
      .Default(false);
}

static constexpr llvm::StringLiteral ValidCPUNames[] = {
    {"mips1"},  {"mips2"},    {"mips3"},    {"mips4"},    {"mips5"},
    {"mips32"}, {"mips32r2"}, {"mips32r3"}, {"mips32r5"}, {"mips32r6"},
    {"mips64"}, {"mips64r2"}, {"mips64r3"}, {"mips64r5"}, {"mips64r6"},
    {"octeon"}, {"octeon+"}, {"p5600"}};

bool MipsTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::is_contained(ValidCPUNames, Name);
}

void MipsTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  Values.append(std::begin(ValidCPUNames), std::end(ValidCPUNames));
}

unsigned MipsTargetInfo::getISARev() const {
  return llvm::StringSwitch<unsigned>(getCPU())
             .Cases("mips32", "mips64", 1)
             .Cases("mips32r2", "mips64r2", "octeon", "octeon+", 2)
             .Cases("mips32r3", "mips64r3", 3)
             .Cases("mips32r5", "mips64r5", 5)
             .Cases("mips32r6", "mips64r6", 6)
             .Default(0);
}

void MipsTargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  if (BigEndian) {
    DefineStd(Builder, "MIPSEB", Opts);
    Builder.defineMacro("_MIPSEB");
  } else {
    DefineStd(Builder, "MIPSEL", Opts);
    Builder.defineMacro("_MIPSEL");
  }

  Builder.defineMacro("__mips__");
  Builder.defineMacro("_mips");
  if (Opts.GNUMode)
    Builder.defineMacro("mips");

  if (ABI == "o32") {
    Builder.defineMacro("__mips", "32");
    Builder.defineMacro("_MIPS_ISA", "_MIPS_ISA_MIPS32");
  } else {
    Builder.defineMacro("__mips", "64");
    Builder.defineMacro("__mips64");
    Builder.defineMacro("__mips64__");
    Builder.defineMacro("_MIPS_ISA", "_MIPS_ISA_MIPS64");
  }

  const std::string ISARev = std::to_string(getISARev());

  if (!ISARev.empty())
    Builder.defineMacro("__mips_isa_rev", ISARev);

  if (ABI == "o32") {
    Builder.defineMacro("__mips_o32");
    Builder.defineMacro("_ABIO32", "1");
    Builder.defineMacro("_MIPS_SIM", "_ABIO32");
  } else if (ABI == "n32") {
    Builder.defineMacro("__mips_n32");
    Builder.defineMacro("_ABIN32", "2");
    Builder.defineMacro("_MIPS_SIM", "_ABIN32");
  } else if (ABI == "n64") {
    Builder.defineMacro("__mips_n64");
    Builder.defineMacro("_ABI64", "3");
    Builder.defineMacro("_MIPS_SIM", "_ABI64");
  } else
    llvm_unreachable("Invalid ABI.");

  if (!IsNoABICalls) {
    Builder.defineMacro("__mips_abicalls");
    if (CanUseBSDABICalls)
      Builder.defineMacro("__ABICALLS__");
  }

  Builder.defineMacro("__REGISTER_PREFIX__", "");

  switch (FloatABI) {
  case HardFloat:
    Builder.defineMacro("__mips_hard_float", Twine(1));
    break;
  case SoftFloat:
    Builder.defineMacro("__mips_soft_float", Twine(1));
    break;
  }

  if (IsSingleFloat)
    Builder.defineMacro("__mips_single_float", Twine(1));

  switch (FPMode) {
  case FPXX:
    Builder.defineMacro("__mips_fpr", Twine(0));
    break;
  case FP32:
    Builder.defineMacro("__mips_fpr", Twine(32));
    break;
  case FP64:
    Builder.defineMacro("__mips_fpr", Twine(64));
    break;
}

  if (FPMode == FP64 || IsSingleFloat)
    Builder.defineMacro("_MIPS_FPSET", Twine(32));
  else
    Builder.defineMacro("_MIPS_FPSET", Twine(16));
  if (NoOddSpreg)
    Builder.defineMacro("_MIPS_SPFPSET", Twine(16));
  else
    Builder.defineMacro("_MIPS_SPFPSET", Twine(32));

  if (IsMips16)
    Builder.defineMacro("__mips16", Twine(1));

  if (IsMicromips)
    Builder.defineMacro("__mips_micromips", Twine(1));

  if (IsNan2008)
    Builder.defineMacro("__mips_nan2008", Twine(1));

  if (IsAbs2008)
    Builder.defineMacro("__mips_abs2008", Twine(1));

  switch (DspRev) {
  default:
    break;
  case DSP1:
    Builder.defineMacro("__mips_dsp_rev", Twine(1));
    Builder.defineMacro("__mips_dsp", Twine(1));
    break;
  case DSP2:
    Builder.defineMacro("__mips_dsp_rev", Twine(2));
    Builder.defineMacro("__mips_dspr2", Twine(1));
    Builder.defineMacro("__mips_dsp", Twine(1));
    break;
  }

  if (HasMSA)
    Builder.defineMacro("__mips_msa", Twine(1));

  if (DisableMadd4)
    Builder.defineMacro("__mips_no_madd4", Twine(1));

  Builder.defineMacro("_MIPS_SZPTR", Twine(getPointerWidth(LangAS::Default)));
  Builder.defineMacro("_MIPS_SZINT", Twine(getIntWidth()));
  Builder.defineMacro("_MIPS_SZLONG", Twine(getLongWidth()));

  Builder.defineMacro("_MIPS_ARCH", "\"" + CPU + "\"");
  if (CPU == "octeon+")
    Builder.defineMacro("_MIPS_ARCH_OCTEONP");
  else
    Builder.defineMacro("_MIPS_ARCH_" + StringRef(CPU).upper());

  if (StringRef(CPU).starts_with("octeon"))
    Builder.defineMacro("__OCTEON__");

  if (CPU != "mips1") {
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  }

  // 32-bit MIPS processors don't have the necessary lld/scd instructions
  // found in 64-bit processors. In the case of O32 on a 64-bit processor,
  // the instructions exist but using them violates the ABI since they
  // require 64-bit GPRs and O32 only supports 32-bit GPRs.
  if (ABI == "n32" || ABI == "n64")
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
}

bool MipsTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("mips", true)
      .Case("dsp", DspRev >= DSP1)
      .Case("dspr2", DspRev >= DSP2)
      .Case("fp64", FPMode == FP64)
      .Case("msa", HasMSA)
      .Default(false);
}

ArrayRef<Builtin::Info> MipsTargetInfo::getTargetBuiltins() const {
  return llvm::ArrayRef(BuiltinInfo,
                        clang::Mips::LastTSBuiltin - Builtin::FirstTSBuiltin);
}

unsigned MipsTargetInfo::getUnwindWordWidth() const {
  return llvm::StringSwitch<unsigned>(ABI)
      .Case("o32", 32)
      .Case("n32", 64)
      .Case("n64", 64)
      .Default(getPointerWidth(LangAS::Default));
}

bool MipsTargetInfo::validateTarget(DiagnosticsEngine &Diags) const {
  // microMIPS64R6 backend was removed.
  if (getTriple().isMIPS64() && IsMicromips && (ABI == "n32" || ABI == "n64")) {
    Diags.Report(diag::err_target_unsupported_cpu_for_micromips) << CPU;
    return false;
  }

  // 64-bit ABI's require 64-bit CPU's.
  if (!processorSupportsGPR64() && (ABI == "n32" || ABI == "n64")) {
    Diags.Report(diag::err_target_unsupported_abi) << ABI << CPU;
    return false;
  }

  // -fpxx is valid only for the o32 ABI
  if (FPMode == FPXX && (ABI == "n32" || ABI == "n64")) {
    Diags.Report(diag::err_unsupported_abi_for_opt) << "-mfpxx" << "o32";
    return false;
  }

  // -mfp32 and n32/n64 ABIs are incompatible
  if (FPMode != FP64 && FPMode != FPXX && !IsSingleFloat &&
      (ABI == "n32" || ABI == "n64")) {
    Diags.Report(diag::err_opt_not_valid_with_opt) << "-mfpxx" << CPU;
    return false;
  }
  // Mips revision 6 and -mfp32 are incompatible
  if (FPMode != FP64 && FPMode != FPXX && (CPU == "mips32r6" ||
      CPU == "mips64r6")) {
    Diags.Report(diag::err_opt_not_valid_with_opt) << "-mfp32" << CPU;
    return false;
  }
  // Option -mfp64 permitted on Mips32 iff revision 2 or higher is present
  if (FPMode == FP64 && (CPU == "mips1" || CPU == "mips2" ||
      getISARev() < 2) && ABI == "o32") {
    Diags.Report(diag::err_mips_fp64_req) << "-mfp64";
    return false;
  }
  // FPXX requires mips2+
  if (FPMode == FPXX && CPU == "mips1") {
    Diags.Report(diag::err_opt_not_valid_with_opt) << "-mfpxx" << CPU;
    return false;
  }
  // -mmsa with -msoft-float makes nonsense
  if (FloatABI == SoftFloat && HasMSA) {
    Diags.Report(diag::err_opt_not_valid_with_opt) << "-msoft-float"
                                                   << "-mmsa";
    return false;
  }
  // Option -mmsa permitted on Mips32 iff revision 2 or higher is present
  if (HasMSA && (CPU == "mips1" || CPU == "mips2" || getISARev() < 2) &&
      ABI == "o32") {
    Diags.Report(diag::err_mips_fp64_req) << "-mmsa";
    return false;
  }
  // MSA requires FP64
  if (FPMode == FPXX && HasMSA) {
    Diags.Report(diag::err_opt_not_valid_with_opt) << "-mfpxx"
                                                   << "-mmsa";
    return false;
  }
  if (FPMode == FP32 && HasMSA) {
    Diags.Report(diag::err_opt_not_valid_with_opt) << "-mfp32"
                                                   << "-mmsa";
    return false;
  }

  return true;
}
