//===--- ARM.cpp - ARM (not AArch64) Helpers for Tools ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/TargetParser/ARMTargetParser.h"
#include "llvm/TargetParser/Host.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

// Get SubArch (vN).
int arm::getARMSubArchVersionNumber(const llvm::Triple &Triple) {
  llvm::StringRef Arch = Triple.getArchName();
  return llvm::ARM::parseArchVersion(Arch);
}

// True if M-profile.
bool arm::isARMMProfile(const llvm::Triple &Triple) {
  llvm::StringRef Arch = Triple.getArchName();
  return llvm::ARM::parseArchProfile(Arch) == llvm::ARM::ProfileKind::M;
}

// On Arm the endianness of the output file is determined by the target and
// can be overridden by the pseudo-target flags '-mlittle-endian'/'-EL' and
// '-mbig-endian'/'-EB'. Unlike other targets the flag does not result in a
// normalized triple so we must handle the flag here.
bool arm::isARMBigEndian(const llvm::Triple &Triple, const ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_mlittle_endian,
                               options::OPT_mbig_endian)) {
    return !A->getOption().matches(options::OPT_mlittle_endian);
  }

  return Triple.getArch() == llvm::Triple::armeb ||
         Triple.getArch() == llvm::Triple::thumbeb;
}

// True if A-profile.
bool arm::isARMAProfile(const llvm::Triple &Triple) {
  llvm::StringRef Arch = Triple.getArchName();
  return llvm::ARM::parseArchProfile(Arch) == llvm::ARM::ProfileKind::A;
}

// Get Arch/CPU from args.
void arm::getARMArchCPUFromArgs(const ArgList &Args, llvm::StringRef &Arch,
                                llvm::StringRef &CPU, bool FromAs) {
  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_mcpu_EQ))
    CPU = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT_march_EQ))
    Arch = A->getValue();
  if (!FromAs)
    return;

  for (const Arg *A :
       Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler)) {
    // Use getValues because -Wa can have multiple arguments
    // e.g. -Wa,-mcpu=foo,-mcpu=bar
    for (StringRef Value : A->getValues()) {
      if (Value.starts_with("-mcpu="))
        CPU = Value.substr(6);
      if (Value.starts_with("-march="))
        Arch = Value.substr(7);
    }
  }
}

// Handle -mhwdiv=.
// FIXME: Use ARMTargetParser.
static void getARMHWDivFeatures(const Driver &D, const Arg *A,
                                const ArgList &Args, StringRef HWDiv,
                                std::vector<StringRef> &Features) {
  uint64_t HWDivID = llvm::ARM::parseHWDiv(HWDiv);
  if (!llvm::ARM::getHWDivFeatures(HWDivID, Features))
    D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
}

// Handle -mfpu=.
static llvm::ARM::FPUKind getARMFPUFeatures(const Driver &D, const Arg *A,
                                            const ArgList &Args, StringRef FPU,
                                            std::vector<StringRef> &Features) {
  llvm::ARM::FPUKind FPUKind = llvm::ARM::parseFPU(FPU);
  if (!llvm::ARM::getFPUFeatures(FPUKind, Features))
    D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
  return FPUKind;
}

// Decode ARM features from string like +[no]featureA+[no]featureB+...
static bool DecodeARMFeatures(const Driver &D, StringRef text, StringRef CPU,
                              llvm::ARM::ArchKind ArchKind,
                              std::vector<StringRef> &Features,
                              llvm::ARM::FPUKind &ArgFPUKind) {
  SmallVector<StringRef, 8> Split;
  text.split(Split, StringRef("+"), -1, false);

  for (StringRef Feature : Split) {
    if (!appendArchExtFeatures(CPU, ArchKind, Feature, Features, ArgFPUKind))
      return false;
  }
  return true;
}

static void DecodeARMFeaturesFromCPU(const Driver &D, StringRef CPU,
                                     std::vector<StringRef> &Features) {
  CPU = CPU.split("+").first;
  if (CPU != "generic") {
    llvm::ARM::ArchKind ArchKind = llvm::ARM::parseCPUArch(CPU);
    uint64_t Extension = llvm::ARM::getDefaultExtensions(CPU, ArchKind);
    llvm::ARM::getExtensionFeatures(Extension, Features);
  }
}

// Check if -march is valid by checking if it can be canonicalised and parsed.
// getARMArch is used here instead of just checking the -march value in order
// to handle -march=native correctly.
static void checkARMArchName(const Driver &D, const Arg *A, const ArgList &Args,
                             llvm::StringRef ArchName, llvm::StringRef CPUName,
                             std::vector<StringRef> &Features,
                             const llvm::Triple &Triple,
                             llvm::ARM::FPUKind &ArgFPUKind) {
  std::pair<StringRef, StringRef> Split = ArchName.split("+");

  std::string MArch = arm::getARMArch(ArchName, Triple);
  llvm::ARM::ArchKind ArchKind = llvm::ARM::parseArch(MArch);
  if (ArchKind == llvm::ARM::ArchKind::INVALID ||
      (Split.second.size() &&
       !DecodeARMFeatures(D, Split.second, CPUName, ArchKind, Features,
                          ArgFPUKind)))
    D.Diag(clang::diag::err_drv_unsupported_option_argument)
        << A->getSpelling() << A->getValue();
}

// Check -mcpu=. Needs ArchName to handle -mcpu=generic.
static void checkARMCPUName(const Driver &D, const Arg *A, const ArgList &Args,
                            llvm::StringRef CPUName, llvm::StringRef ArchName,
                            std::vector<StringRef> &Features,
                            const llvm::Triple &Triple,
                            llvm::ARM::FPUKind &ArgFPUKind) {
  std::pair<StringRef, StringRef> Split = CPUName.split("+");

  std::string CPU = arm::getARMTargetCPU(CPUName, ArchName, Triple);
  llvm::ARM::ArchKind ArchKind =
    arm::getLLVMArchKindForARM(CPU, ArchName, Triple);
  if (ArchKind == llvm::ARM::ArchKind::INVALID ||
      (Split.second.size() && !DecodeARMFeatures(D, Split.second, CPU, ArchKind,
                                                 Features, ArgFPUKind)))
    D.Diag(clang::diag::err_drv_unsupported_option_argument)
        << A->getSpelling() << A->getValue();
}

// If -mfloat-abi=hard or -mhard-float are specified explicitly then check that
// floating point registers are available on the target CPU.
static void checkARMFloatABI(const Driver &D, const ArgList &Args,
                             bool HasFPRegs) {
  if (HasFPRegs)
    return;
  const Arg *A =
      Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                      options::OPT_mfloat_abi_EQ);
  if (A && (A->getOption().matches(options::OPT_mhard_float) ||
            (A->getOption().matches(options::OPT_mfloat_abi_EQ) &&
             A->getValue() == StringRef("hard"))))
    D.Diag(clang::diag::warn_drv_no_floating_point_registers)
        << A->getAsString(Args);
}

bool arm::useAAPCSForMachO(const llvm::Triple &T) {
  // The backend is hardwired to assume AAPCS for M-class processors, ensure
  // the frontend matches that.
  return T.getEnvironment() == llvm::Triple::EABI ||
         T.getEnvironment() == llvm::Triple::EABIHF ||
         T.getOS() == llvm::Triple::UnknownOS || isARMMProfile(T);
}

// We follow GCC and support when the backend has support for the MRC/MCR
// instructions that are used to set the hard thread pointer ("CP15 C13
// Thread id").
bool arm::isHardTPSupported(const llvm::Triple &Triple) {
  int Ver = getARMSubArchVersionNumber(Triple);
  llvm::ARM::ArchKind AK = llvm::ARM::parseArch(Triple.getArchName());
  return Triple.isARM() || AK == llvm::ARM::ArchKind::ARMV6T2 ||
         (Ver >= 7 && AK != llvm::ARM::ArchKind::ARMV8MBaseline);
}

// Select mode for reading thread pointer (-mtp=soft/cp15).
arm::ReadTPMode arm::getReadTPMode(const Driver &D, const ArgList &Args,
                                   const llvm::Triple &Triple, bool ForAS) {
  if (Arg *A = Args.getLastArg(options::OPT_mtp_mode_EQ)) {
    arm::ReadTPMode ThreadPointer =
        llvm::StringSwitch<arm::ReadTPMode>(A->getValue())
            .Case("cp15", ReadTPMode::TPIDRURO)
            .Case("tpidrurw", ReadTPMode::TPIDRURW)
            .Case("tpidruro", ReadTPMode::TPIDRURO)
            .Case("tpidrprw", ReadTPMode::TPIDRPRW)
            .Case("soft", ReadTPMode::Soft)
            .Default(ReadTPMode::Invalid);
    if ((ThreadPointer == ReadTPMode::TPIDRURW ||
         ThreadPointer == ReadTPMode::TPIDRURO ||
         ThreadPointer == ReadTPMode::TPIDRPRW) &&
        !isHardTPSupported(Triple) && !ForAS) {
      D.Diag(diag::err_target_unsupported_tp_hard) << Triple.getArchName();
      return ReadTPMode::Invalid;
    }
    if (ThreadPointer != ReadTPMode::Invalid)
      return ThreadPointer;
    if (StringRef(A->getValue()).empty())
      D.Diag(diag::err_drv_missing_arg_mtp) << A->getAsString(Args);
    else
      D.Diag(diag::err_drv_invalid_mtp) << A->getAsString(Args);
    return ReadTPMode::Invalid;
  }
  return ReadTPMode::Soft;
}

void arm::setArchNameInTriple(const Driver &D, const ArgList &Args,
                              types::ID InputType, llvm::Triple &Triple) {
  StringRef MCPU, MArch;
  if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
    MCPU = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT_march_EQ))
    MArch = A->getValue();

  std::string CPU = Triple.isOSBinFormatMachO()
                        ? tools::arm::getARMCPUForMArch(MArch, Triple).str()
                        : tools::arm::getARMTargetCPU(MCPU, MArch, Triple);
  StringRef Suffix = tools::arm::getLLVMArchSuffixForARM(CPU, MArch, Triple);

  bool IsBigEndian = Triple.getArch() == llvm::Triple::armeb ||
                     Triple.getArch() == llvm::Triple::thumbeb;
  // Handle pseudo-target flags '-mlittle-endian'/'-EL' and
  // '-mbig-endian'/'-EB'.
  if (Arg *A = Args.getLastArg(options::OPT_mlittle_endian,
                               options::OPT_mbig_endian)) {
    IsBigEndian = !A->getOption().matches(options::OPT_mlittle_endian);
  }
  std::string ArchName = IsBigEndian ? "armeb" : "arm";

  // FIXME: Thumb should just be another -target-feaure, not in the triple.
  bool IsMProfile =
      llvm::ARM::parseArchProfile(Suffix) == llvm::ARM::ProfileKind::M;
  bool ThumbDefault = IsMProfile ||
                      // Thumb2 is the default for V7 on Darwin.
                      (llvm::ARM::parseArchVersion(Suffix) == 7 &&
                       Triple.isOSBinFormatMachO()) ||
                      // FIXME: this is invalid for WindowsCE
                      Triple.isOSWindows();

  // Check if ARM ISA was explicitly selected (using -mno-thumb or -marm) for
  // M-Class CPUs/architecture variants, which is not supported.
  bool ARMModeRequested =
      !Args.hasFlag(options::OPT_mthumb, options::OPT_mno_thumb, ThumbDefault);
  if (IsMProfile && ARMModeRequested) {
    if (MCPU.size())
      D.Diag(diag::err_cpu_unsupported_isa) << CPU << "ARM";
    else
      D.Diag(diag::err_arch_unsupported_isa)
          << tools::arm::getARMArch(MArch, Triple) << "ARM";
  }

  // Check to see if an explicit choice to use thumb has been made via
  // -mthumb. For assembler files we must check for -mthumb in the options
  // passed to the assembler via -Wa or -Xassembler.
  bool IsThumb = false;
  if (InputType != types::TY_PP_Asm)
    IsThumb =
        Args.hasFlag(options::OPT_mthumb, options::OPT_mno_thumb, ThumbDefault);
  else {
    // Ideally we would check for these flags in
    // CollectArgsForIntegratedAssembler but we can't change the ArchName at
    // that point.
    llvm::StringRef WaMArch, WaMCPU;
    for (const auto *A :
         Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler)) {
      for (StringRef Value : A->getValues()) {
        // There is no assembler equivalent of -mno-thumb, -marm, or -mno-arm.
        if (Value == "-mthumb")
          IsThumb = true;
        else if (Value.starts_with("-march="))
          WaMArch = Value.substr(7);
        else if (Value.starts_with("-mcpu="))
          WaMCPU = Value.substr(6);
      }
    }

    if (WaMCPU.size() || WaMArch.size()) {
      // The way this works means that we prefer -Wa,-mcpu's architecture
      // over -Wa,-march. Which matches the compiler behaviour.
      Suffix = tools::arm::getLLVMArchSuffixForARM(WaMCPU, WaMArch, Triple);
    }
  }

  // Assembly files should start in ARM mode, unless arch is M-profile, or
  // -mthumb has been passed explicitly to the assembler. Windows is always
  // thumb.
  if (IsThumb || IsMProfile || Triple.isOSWindows()) {
    if (IsBigEndian)
      ArchName = "thumbeb";
    else
      ArchName = "thumb";
  }
  Triple.setArchName(ArchName + Suffix.str());
}

void arm::setFloatABIInTriple(const Driver &D, const ArgList &Args,
                              llvm::Triple &Triple) {
  if (Triple.isOSLiteOS()) {
    Triple.setEnvironment(llvm::Triple::OpenHOS);
    return;
  }

  bool isHardFloat =
      (arm::getARMFloatABI(D, Triple, Args) == arm::FloatABI::Hard);

  switch (Triple.getEnvironment()) {
  case llvm::Triple::GNUEABI:
  case llvm::Triple::GNUEABIHF:
    Triple.setEnvironment(isHardFloat ? llvm::Triple::GNUEABIHF
                                      : llvm::Triple::GNUEABI);
    break;
  case llvm::Triple::GNUEABIT64:
  case llvm::Triple::GNUEABIHFT64:
    Triple.setEnvironment(isHardFloat ? llvm::Triple::GNUEABIHFT64
                                      : llvm::Triple::GNUEABIT64);
    break;
  case llvm::Triple::EABI:
  case llvm::Triple::EABIHF:
    Triple.setEnvironment(isHardFloat ? llvm::Triple::EABIHF
                                      : llvm::Triple::EABI);
    break;
  case llvm::Triple::MuslEABI:
  case llvm::Triple::MuslEABIHF:
    Triple.setEnvironment(isHardFloat ? llvm::Triple::MuslEABIHF
                                      : llvm::Triple::MuslEABI);
    break;
  case llvm::Triple::OpenHOS:
    break;
  default: {
    arm::FloatABI DefaultABI = arm::getDefaultFloatABI(Triple);
    if (DefaultABI != arm::FloatABI::Invalid &&
        isHardFloat != (DefaultABI == arm::FloatABI::Hard)) {
      Arg *ABIArg =
          Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                          options::OPT_mfloat_abi_EQ);
      assert(ABIArg && "Non-default float abi expected to be from arg");
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << ABIArg->getAsString(Args) << Triple.getTriple();
    }
    break;
  }
  }
}

arm::FloatABI arm::getARMFloatABI(const ToolChain &TC, const ArgList &Args) {
  return arm::getARMFloatABI(TC.getDriver(), TC.getEffectiveTriple(), Args);
}

arm::FloatABI arm::getDefaultFloatABI(const llvm::Triple &Triple) {
  auto SubArch = getARMSubArchVersionNumber(Triple);
  switch (Triple.getOS()) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS:
  case llvm::Triple::DriverKit:
  case llvm::Triple::XROS:
    // Darwin defaults to "softfp" for v6 and v7.
    if (Triple.isWatchABI())
      return FloatABI::Hard;
    else
      return (SubArch == 6 || SubArch == 7) ? FloatABI::SoftFP : FloatABI::Soft;

  case llvm::Triple::WatchOS:
    return FloatABI::Hard;

  // FIXME: this is invalid for WindowsCE
  case llvm::Triple::Win32:
    // It is incorrect to select hard float ABI on MachO platforms if the ABI is
    // "apcs-gnu".
    if (Triple.isOSBinFormatMachO() && !useAAPCSForMachO(Triple))
      return FloatABI::Soft;
    return FloatABI::Hard;

  case llvm::Triple::NetBSD:
    switch (Triple.getEnvironment()) {
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABIHF:
      return FloatABI::Hard;
    default:
      return FloatABI::Soft;
    }
    break;

  case llvm::Triple::FreeBSD:
    switch (Triple.getEnvironment()) {
    case llvm::Triple::GNUEABIHF:
      return FloatABI::Hard;
    default:
      // FreeBSD defaults to soft float
      return FloatABI::Soft;
    }
    break;

  case llvm::Triple::Haiku:
  case llvm::Triple::OpenBSD:
    return FloatABI::SoftFP;

  default:
    if (Triple.isOHOSFamily())
      return FloatABI::Soft;
    switch (Triple.getEnvironment()) {
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::GNUEABIHFT64:
    case llvm::Triple::MuslEABIHF:
    case llvm::Triple::EABIHF:
      return FloatABI::Hard;
    case llvm::Triple::GNUEABI:
    case llvm::Triple::GNUEABIT64:
    case llvm::Triple::MuslEABI:
    case llvm::Triple::EABI:
      // EABI is always AAPCS, and if it was not marked 'hard', it's softfp
      return FloatABI::SoftFP;
    case llvm::Triple::Android:
      return (SubArch >= 7) ? FloatABI::SoftFP : FloatABI::Soft;
    default:
      return FloatABI::Invalid;
    }
  }
  return FloatABI::Invalid;
}

// Select the float ABI as determined by -msoft-float, -mhard-float, and
// -mfloat-abi=.
arm::FloatABI arm::getARMFloatABI(const Driver &D, const llvm::Triple &Triple,
                                  const ArgList &Args) {
  arm::FloatABI ABI = FloatABI::Invalid;
  if (Arg *A =
          Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                          options::OPT_mfloat_abi_EQ)) {
    if (A->getOption().matches(options::OPT_msoft_float)) {
      ABI = FloatABI::Soft;
    } else if (A->getOption().matches(options::OPT_mhard_float)) {
      ABI = FloatABI::Hard;
    } else {
      ABI = llvm::StringSwitch<arm::FloatABI>(A->getValue())
                .Case("soft", FloatABI::Soft)
                .Case("softfp", FloatABI::SoftFP)
                .Case("hard", FloatABI::Hard)
                .Default(FloatABI::Invalid);
      if (ABI == FloatABI::Invalid && !StringRef(A->getValue()).empty()) {
        D.Diag(diag::err_drv_invalid_mfloat_abi) << A->getAsString(Args);
        ABI = FloatABI::Soft;
      }
    }
  }

  // If unspecified, choose the default based on the platform.
  if (ABI == FloatABI::Invalid)
    ABI = arm::getDefaultFloatABI(Triple);

  if (ABI == FloatABI::Invalid) {
    // Assume "soft", but warn the user we are guessing.
    if (Triple.isOSBinFormatMachO() &&
        Triple.getSubArch() == llvm::Triple::ARMSubArch_v7em)
      ABI = FloatABI::Hard;
    else
      ABI = FloatABI::Soft;

    if (Triple.getOS() != llvm::Triple::UnknownOS ||
        !Triple.isOSBinFormatMachO())
      D.Diag(diag::warn_drv_assuming_mfloat_abi_is) << "soft";
  }

  assert(ABI != FloatABI::Invalid && "must select an ABI");
  return ABI;
}

static bool hasIntegerMVE(const std::vector<StringRef> &F) {
  auto MVE = llvm::find(llvm::reverse(F), "+mve");
  auto NoMVE = llvm::find(llvm::reverse(F), "-mve");
  return MVE != F.rend() &&
         (NoMVE == F.rend() || std::distance(MVE, NoMVE) > 0);
}

llvm::ARM::FPUKind arm::getARMTargetFeatures(const Driver &D,
                                             const llvm::Triple &Triple,
                                             const ArgList &Args,
                                             std::vector<StringRef> &Features,
                                             bool ForAS, bool ForMultilib) {
  bool KernelOrKext =
      Args.hasArg(options::OPT_mkernel, options::OPT_fapple_kext);
  arm::FloatABI ABI = arm::getARMFloatABI(D, Triple, Args);
  std::optional<std::pair<const Arg *, StringRef>> WaCPU, WaFPU, WaHDiv, WaArch;

  // This vector will accumulate features from the architecture
  // extension suffixes on -mcpu and -march (e.g. the 'bar' in
  // -mcpu=foo+bar). We want to apply those after the features derived
  // from the FPU, in case -mfpu generates a negative feature which
  // the +bar is supposed to override.
  std::vector<StringRef> ExtensionFeatures;

  if (!ForAS) {
    // FIXME: Note, this is a hack, the LLVM backend doesn't actually use these
    // yet (it uses the -mfloat-abi and -msoft-float options), and it is
    // stripped out by the ARM target. We should probably pass this a new
    // -target-option, which is handled by the -cc1/-cc1as invocation.
    //
    // FIXME2:  For consistency, it would be ideal if we set up the target
    // machine state the same when using the frontend or the assembler. We don't
    // currently do that for the assembler, we pass the options directly to the
    // backend and never even instantiate the frontend TargetInfo. If we did,
    // and used its handleTargetFeatures hook, then we could ensure the
    // assembler and the frontend behave the same.

    // Use software floating point operations?
    if (ABI == arm::FloatABI::Soft)
      Features.push_back("+soft-float");

    // Use software floating point argument passing?
    if (ABI != arm::FloatABI::Hard)
      Features.push_back("+soft-float-abi");
  } else {
    // Here, we make sure that -Wa,-mfpu/cpu/arch/hwdiv will be passed down
    // to the assembler correctly.
    for (const Arg *A :
         Args.filtered(options::OPT_Wa_COMMA, options::OPT_Xassembler)) {
      // We use getValues here because you can have many options per -Wa
      // We will keep the last one we find for each of these
      for (StringRef Value : A->getValues()) {
        if (Value.starts_with("-mfpu=")) {
          WaFPU = std::make_pair(A, Value.substr(6));
        } else if (Value.starts_with("-mcpu=")) {
          WaCPU = std::make_pair(A, Value.substr(6));
        } else if (Value.starts_with("-mhwdiv=")) {
          WaHDiv = std::make_pair(A, Value.substr(8));
        } else if (Value.starts_with("-march=")) {
          WaArch = std::make_pair(A, Value.substr(7));
        }
      }
    }

    // The integrated assembler doesn't implement e_flags setting behavior for
    // -meabi=gnu (gcc -mabi={apcs-gnu,atpcs} passes -meabi=gnu to gas). For
    // compatibility we accept but warn.
    if (Arg *A = Args.getLastArgNoClaim(options::OPT_mabi_EQ))
      A->ignoreTargetSpecific();
  }

  if (getReadTPMode(D, Args, Triple, ForAS) == ReadTPMode::TPIDRURW)
    Features.push_back("+read-tp-tpidrurw");
  if (getReadTPMode(D, Args, Triple, ForAS) == ReadTPMode::TPIDRURO)
    Features.push_back("+read-tp-tpidruro");
  if (getReadTPMode(D, Args, Triple, ForAS) == ReadTPMode::TPIDRPRW)
    Features.push_back("+read-tp-tpidrprw");

  const Arg *ArchArg = Args.getLastArg(options::OPT_march_EQ);
  const Arg *CPUArg = Args.getLastArg(options::OPT_mcpu_EQ);
  StringRef ArchName;
  StringRef CPUName;
  llvm::ARM::FPUKind ArchArgFPUKind = llvm::ARM::FK_INVALID;
  llvm::ARM::FPUKind CPUArgFPUKind = llvm::ARM::FK_INVALID;

  // Check -mcpu. ClangAs gives preference to -Wa,-mcpu=.
  if (WaCPU) {
    if (CPUArg)
      D.Diag(clang::diag::warn_drv_unused_argument)
          << CPUArg->getAsString(Args);
    CPUName = WaCPU->second;
    CPUArg = WaCPU->first;
  } else if (CPUArg)
    CPUName = CPUArg->getValue();

  // Check -march. ClangAs gives preference to -Wa,-march=.
  if (WaArch) {
    if (ArchArg)
      D.Diag(clang::diag::warn_drv_unused_argument)
          << ArchArg->getAsString(Args);
    ArchName = WaArch->second;
    // This will set any features after the base architecture.
    checkARMArchName(D, WaArch->first, Args, ArchName, CPUName,
                     ExtensionFeatures, Triple, ArchArgFPUKind);
    // The base architecture was handled in ToolChain::ComputeLLVMTriple because
    // triple is read only by this point.
  } else if (ArchArg) {
    ArchName = ArchArg->getValue();
    checkARMArchName(D, ArchArg, Args, ArchName, CPUName, ExtensionFeatures,
                     Triple, ArchArgFPUKind);
  }

  // Add CPU features for generic CPUs
  if (CPUName == "native") {
    for (auto &F : llvm::sys::getHostCPUFeatures())
      Features.push_back(
          Args.MakeArgString((F.second ? "+" : "-") + F.first()));
  } else if (!CPUName.empty()) {
    // This sets the default features for the specified CPU. We certainly don't
    // want to override the features that have been explicitly specified on the
    // command line. Therefore, process them directly instead of appending them
    // at the end later.
    DecodeARMFeaturesFromCPU(D, CPUName, Features);
  }

  if (CPUArg)
    checkARMCPUName(D, CPUArg, Args, CPUName, ArchName, ExtensionFeatures,
                    Triple, CPUArgFPUKind);

  // TODO Handle -mtune=. Suppress -Wunused-command-line-argument as a
  // longstanding behavior.
  (void)Args.getLastArg(options::OPT_mtune_EQ);

  // Honor -mfpu=. ClangAs gives preference to -Wa,-mfpu=.
  llvm::ARM::FPUKind FPUKind = llvm::ARM::FK_INVALID;
  const Arg *FPUArg = Args.getLastArg(options::OPT_mfpu_EQ);
  if (WaFPU) {
    if (FPUArg)
      D.Diag(clang::diag::warn_drv_unused_argument)
          << FPUArg->getAsString(Args);
    (void)getARMFPUFeatures(D, WaFPU->first, Args, WaFPU->second, Features);
  } else if (FPUArg) {
    FPUKind = getARMFPUFeatures(D, FPUArg, Args, FPUArg->getValue(), Features);
  } else if (Triple.isAndroid() && getARMSubArchVersionNumber(Triple) >= 7) {
    const char *AndroidFPU = "neon";
    FPUKind = llvm::ARM::parseFPU(AndroidFPU);
    if (!llvm::ARM::getFPUFeatures(FPUKind, Features))
      D.Diag(clang::diag::err_drv_clang_unsupported)
          << std::string("-mfpu=") + AndroidFPU;
  } else if (ArchArgFPUKind != llvm::ARM::FK_INVALID ||
             CPUArgFPUKind != llvm::ARM::FK_INVALID) {
    FPUKind =
        CPUArgFPUKind != llvm::ARM::FK_INVALID ? CPUArgFPUKind : ArchArgFPUKind;
    (void)llvm::ARM::getFPUFeatures(FPUKind, Features);
  } else {
    if (!ForAS) {
      std::string CPU = arm::getARMTargetCPU(CPUName, ArchName, Triple);
      llvm::ARM::ArchKind ArchKind =
          arm::getLLVMArchKindForARM(CPU, ArchName, Triple);
      FPUKind = llvm::ARM::getDefaultFPU(CPU, ArchKind);
      (void)llvm::ARM::getFPUFeatures(FPUKind, Features);
    }
  }

  // Now we've finished accumulating features from arch, cpu and fpu,
  // we can append the ones for architecture extensions that we
  // collected separately.
  Features.insert(std::end(Features),
                  std::begin(ExtensionFeatures), std::end(ExtensionFeatures));

  // Honor -mhwdiv=. ClangAs gives preference to -Wa,-mhwdiv=.
  const Arg *HDivArg = Args.getLastArg(options::OPT_mhwdiv_EQ);
  if (WaHDiv) {
    if (HDivArg)
      D.Diag(clang::diag::warn_drv_unused_argument)
          << HDivArg->getAsString(Args);
    getARMHWDivFeatures(D, WaHDiv->first, Args, WaHDiv->second, Features);
  } else if (HDivArg)
    getARMHWDivFeatures(D, HDivArg, Args, HDivArg->getValue(), Features);

  // Handle (arch-dependent) fp16fml/fullfp16 relationship.
  // Must happen before any features are disabled due to soft-float.
  // FIXME: this fp16fml option handling will be reimplemented after the
  // TargetParser rewrite.
  const auto ItRNoFullFP16 = std::find(Features.rbegin(), Features.rend(), "-fullfp16");
  const auto ItRFP16FML = std::find(Features.rbegin(), Features.rend(), "+fp16fml");
  if (Triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v8_4a) {
    const auto ItRFullFP16  = std::find(Features.rbegin(), Features.rend(), "+fullfp16");
    if (ItRFullFP16 < ItRNoFullFP16 && ItRFullFP16 < ItRFP16FML) {
      // Only entangled feature that can be to the right of this +fullfp16 is -fp16fml.
      // Only append the +fp16fml if there is no -fp16fml after the +fullfp16.
      if (std::find(Features.rbegin(), ItRFullFP16, "-fp16fml") == ItRFullFP16)
        Features.push_back("+fp16fml");
    }
    else
      goto fp16_fml_fallthrough;
  }
  else {
fp16_fml_fallthrough:
    // In both of these cases, putting the 'other' feature on the end of the vector will
    // result in the same effect as placing it immediately after the current feature.
    if (ItRNoFullFP16 < ItRFP16FML)
      Features.push_back("-fp16fml");
    else if (ItRNoFullFP16 > ItRFP16FML)
      Features.push_back("+fullfp16");
  }

  // Setting -msoft-float/-mfloat-abi=soft, -mfpu=none, or adding +nofp to
  // -march/-mcpu effectively disables the FPU (GCC ignores the -mfpu options in
  // this case). Note that the ABI can also be set implicitly by the target
  // selected.
  bool HasFPRegs = true;
  if (ABI == arm::FloatABI::Soft) {
    llvm::ARM::getFPUFeatures(llvm::ARM::FK_NONE, Features);

    // Disable all features relating to hardware FP, not already disabled by the
    // above call.
    Features.insert(Features.end(),
                    {"-dotprod", "-fp16fml", "-bf16", "-mve", "-mve.fp"});
    HasFPRegs = false;
    FPUKind = llvm::ARM::FK_NONE;
  } else if (FPUKind == llvm::ARM::FK_NONE ||
             ArchArgFPUKind == llvm::ARM::FK_NONE ||
             CPUArgFPUKind == llvm::ARM::FK_NONE) {
    // -mfpu=none, -march=armvX+nofp or -mcpu=X+nofp is *very* similar to
    // -mfloat-abi=soft, only that it should not disable MVE-I. They disable the
    // FPU, but not the FPU registers, thus MVE-I, which depends only on the
    // latter, is still supported.
    Features.insert(Features.end(),
                    {"-dotprod", "-fp16fml", "-bf16", "-mve.fp"});
    HasFPRegs = hasIntegerMVE(Features);
    FPUKind = llvm::ARM::FK_NONE;
  }
  if (!HasFPRegs)
    Features.emplace_back("-fpregs");

  // En/disable crc code generation.
  if (Arg *A = Args.getLastArg(options::OPT_mcrc, options::OPT_mnocrc)) {
    if (A->getOption().matches(options::OPT_mcrc))
      Features.push_back("+crc");
    else
      Features.push_back("-crc");
  }

  // For Arch >= ARMv8.0 && A or R profile:  crypto = sha2 + aes
  // Rather than replace within the feature vector, determine whether each
  // algorithm is enabled and append this to the end of the vector.
  // The algorithms can be controlled by their specific feature or the crypto
  // feature, so their status can be determined by the last occurance of
  // either in the vector. This allows one to supercede the other.
  // e.g. +crypto+noaes in -march/-mcpu should enable sha2, but not aes
  // FIXME: this needs reimplementation after the TargetParser rewrite
  bool HasSHA2 = false;
  bool HasAES = false;
  const auto ItCrypto =
      llvm::find_if(llvm::reverse(Features), [](const StringRef F) {
        return F.contains("crypto");
      });
  const auto ItSHA2 =
      llvm::find_if(llvm::reverse(Features), [](const StringRef F) {
        return F.contains("crypto") || F.contains("sha2");
      });
  const auto ItAES =
      llvm::find_if(llvm::reverse(Features), [](const StringRef F) {
        return F.contains("crypto") || F.contains("aes");
      });
  const bool FoundSHA2 = ItSHA2 != Features.rend();
  const bool FoundAES = ItAES != Features.rend();
  if (FoundSHA2)
    HasSHA2 = ItSHA2->take_front() == "+";
  if (FoundAES)
    HasAES = ItAES->take_front() == "+";
  if (ItCrypto != Features.rend()) {
    if (HasSHA2 && HasAES)
      Features.push_back("+crypto");
    else
      Features.push_back("-crypto");
    if (HasSHA2)
      Features.push_back("+sha2");
    else
      Features.push_back("-sha2");
    if (HasAES)
      Features.push_back("+aes");
    else
      Features.push_back("-aes");
  }

  if (HasSHA2 || HasAES) {
    StringRef ArchSuffix = arm::getLLVMArchSuffixForARM(
        arm::getARMTargetCPU(CPUName, ArchName, Triple), ArchName, Triple);
    llvm::ARM::ProfileKind ArchProfile =
        llvm::ARM::parseArchProfile(ArchSuffix);
    if (!((llvm::ARM::parseArchVersion(ArchSuffix) >= 8) &&
          (ArchProfile == llvm::ARM::ProfileKind::A ||
           ArchProfile == llvm::ARM::ProfileKind::R))) {
      if (HasSHA2)
        D.Diag(clang::diag::warn_target_unsupported_extension)
            << "sha2"
            << llvm::ARM::getArchName(llvm::ARM::parseArch(ArchSuffix));
      if (HasAES)
        D.Diag(clang::diag::warn_target_unsupported_extension)
            << "aes"
            << llvm::ARM::getArchName(llvm::ARM::parseArch(ArchSuffix));
      // With -fno-integrated-as -mfpu=crypto-neon-fp-armv8 some assemblers such
      // as the GNU assembler will permit the use of crypto instructions as the
      // fpu will override the architecture. We keep the crypto feature in this
      // case to preserve compatibility. In all other cases we remove the crypto
      // feature.
      if (!Args.hasArg(options::OPT_fno_integrated_as)) {
        Features.push_back("-sha2");
        Features.push_back("-aes");
      }
    }
  }

  // Propagate frame-chain model selection
  if (Arg *A = Args.getLastArg(options::OPT_mframe_chain)) {
    StringRef FrameChainOption = A->getValue();
    if (FrameChainOption.starts_with("aapcs"))
      Features.push_back("+aapcs-frame-chain");
  }

  // CMSE: Check for target 8M (for -mcmse to be applicable) is performed later.
  if (Args.getLastArg(options::OPT_mcmse))
    Features.push_back("+8msecext");

  if (Arg *A = Args.getLastArg(options::OPT_mfix_cmse_cve_2021_35465,
                               options::OPT_mno_fix_cmse_cve_2021_35465)) {
    if (!Args.getLastArg(options::OPT_mcmse))
      D.Diag(diag::err_opt_not_valid_without_opt)
          << A->getOption().getName() << "-mcmse";

    if (A->getOption().matches(options::OPT_mfix_cmse_cve_2021_35465))
      Features.push_back("+fix-cmse-cve-2021-35465");
    else
      Features.push_back("-fix-cmse-cve-2021-35465");
  }

  // This also handles the -m(no-)fix-cortex-a72-1655431 arguments via aliases.
  if (Arg *A = Args.getLastArg(options::OPT_mfix_cortex_a57_aes_1742098,
                               options::OPT_mno_fix_cortex_a57_aes_1742098)) {
    if (A->getOption().matches(options::OPT_mfix_cortex_a57_aes_1742098)) {
      Features.push_back("+fix-cortex-a57-aes-1742098");
    } else {
      Features.push_back("-fix-cortex-a57-aes-1742098");
    }
  }

  // Look for the last occurrence of -mlong-calls or -mno-long-calls. If
  // neither options are specified, see if we are compiling for kernel/kext and
  // decide whether to pass "+long-calls" based on the OS and its version.
  if (Arg *A = Args.getLastArg(options::OPT_mlong_calls,
                               options::OPT_mno_long_calls)) {
    if (A->getOption().matches(options::OPT_mlong_calls))
      Features.push_back("+long-calls");
  } else if (KernelOrKext && (!Triple.isiOS() || Triple.isOSVersionLT(6)) &&
             !Triple.isWatchOS() && !Triple.isXROS()) {
    Features.push_back("+long-calls");
  }

  // Generate execute-only output (no data access to code sections).
  // This only makes sense for the compiler, not for the assembler.
  // It's not needed for multilib selection and may hide an unused
  // argument diagnostic if the code is always run.
  if (!ForAS && !ForMultilib) {
    // Supported only on ARMv6T2 and ARMv7 and above.
    // Cannot be combined with -mno-movt.
    if (Arg *A = Args.getLastArg(options::OPT_mexecute_only, options::OPT_mno_execute_only)) {
      if (A->getOption().matches(options::OPT_mexecute_only)) {
        if (getARMSubArchVersionNumber(Triple) < 7 &&
            llvm::ARM::parseArch(Triple.getArchName()) != llvm::ARM::ArchKind::ARMV6T2 &&
            llvm::ARM::parseArch(Triple.getArchName()) != llvm::ARM::ArchKind::ARMV6M)
              D.Diag(diag::err_target_unsupported_execute_only) << Triple.getArchName();
        else if (llvm::ARM::parseArch(Triple.getArchName()) == llvm::ARM::ArchKind::ARMV6M) {
          if (Arg *PIArg = Args.getLastArg(options::OPT_fropi, options::OPT_frwpi,
                                           options::OPT_fpic, options::OPT_fpie,
                                           options::OPT_fPIC, options::OPT_fPIE))
            D.Diag(diag::err_opt_not_valid_with_opt_on_target)
                << A->getAsString(Args) << PIArg->getAsString(Args) << Triple.getArchName();
        } else if (Arg *B = Args.getLastArg(options::OPT_mno_movt))
          D.Diag(diag::err_opt_not_valid_with_opt)
              << A->getAsString(Args) << B->getAsString(Args);
        Features.push_back("+execute-only");
      }
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_mno_unaligned_access,
                                      options::OPT_munaligned_access,
                                      options::OPT_mstrict_align,
                                      options::OPT_mno_strict_align)) {
    // Kernel code has more strict alignment requirements.
    if (KernelOrKext ||
        A->getOption().matches(options::OPT_mno_unaligned_access) ||
        A->getOption().matches(options::OPT_mstrict_align)) {
      Features.push_back("+strict-align");
    } else {
      // No v6M core supports unaligned memory access (v6M ARM ARM A3.2).
      if (Triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v6m)
        D.Diag(diag::err_target_unsupported_unaligned) << "v6m";
      // v8M Baseline follows on from v6M, so doesn't support unaligned memory
      // access either.
      else if (Triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v8m_baseline)
        D.Diag(diag::err_target_unsupported_unaligned) << "v8m.base";
    }
  } else {
    // Assume pre-ARMv6 doesn't support unaligned accesses.
    //
    // ARMv6 may or may not support unaligned accesses depending on the
    // SCTLR.U bit, which is architecture-specific. We assume ARMv6
    // Darwin and NetBSD targets support unaligned accesses, and others don't.
    //
    // ARMv7 always has SCTLR.U set to 1, but it has a new SCTLR.A bit which
    // raises an alignment fault on unaligned accesses. Assume ARMv7+ supports
    // unaligned accesses, except ARMv6-M, and ARMv8-M without the Main
    // Extension. This aligns with the default behavior of ARM's downstream
    // versions of GCC and Clang.
    //
    // Users can change the default behavior via -m[no-]unaliged-access.
    int VersionNum = getARMSubArchVersionNumber(Triple);
    if (Triple.isOSDarwin() || Triple.isOSNetBSD()) {
      if (VersionNum < 6 ||
          Triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v6m)
        Features.push_back("+strict-align");
    } else if (VersionNum < 7 ||
               Triple.getSubArch() ==
                   llvm::Triple::SubArchType::ARMSubArch_v6m ||
               Triple.getSubArch() ==
                   llvm::Triple::SubArchType::ARMSubArch_v8m_baseline) {
      Features.push_back("+strict-align");
    }
  }

  // llvm does not support reserving registers in general. There is support
  // for reserving r9 on ARM though (defined as a platform-specific register
  // in ARM EABI).
  if (Args.hasArg(options::OPT_ffixed_r9))
    Features.push_back("+reserve-r9");

  // The kext linker doesn't know how to deal with movw/movt.
  if (KernelOrKext || Args.hasArg(options::OPT_mno_movt))
    Features.push_back("+no-movt");

  if (Args.hasArg(options::OPT_mno_neg_immediates))
    Features.push_back("+no-neg-immediates");

  // Enable/disable straight line speculation hardening.
  if (Arg *A = Args.getLastArg(options::OPT_mharden_sls_EQ)) {
    StringRef Scope = A->getValue();
    bool EnableRetBr = false;
    bool EnableBlr = false;
    bool DisableComdat = false;
    if (Scope != "none") {
      SmallVector<StringRef, 4> Opts;
      Scope.split(Opts, ",");
      for (auto Opt : Opts) {
        Opt = Opt.trim();
        if (Opt == "all") {
          EnableBlr = true;
          EnableRetBr = true;
          continue;
        }
        if (Opt == "retbr") {
          EnableRetBr = true;
          continue;
        }
        if (Opt == "blr") {
          EnableBlr = true;
          continue;
        }
        if (Opt == "comdat") {
          DisableComdat = false;
          continue;
        }
        if (Opt == "nocomdat") {
          DisableComdat = true;
          continue;
        }
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Scope;
        break;
      }
    }

    if (EnableRetBr || EnableBlr)
      if (!(isARMAProfile(Triple) && getARMSubArchVersionNumber(Triple) >= 7))
        D.Diag(diag::err_sls_hardening_arm_not_supported)
            << Scope << A->getAsString(Args);

    if (EnableRetBr)
      Features.push_back("+harden-sls-retbr");
    if (EnableBlr)
      Features.push_back("+harden-sls-blr");
    if (DisableComdat) {
      Features.push_back("+harden-sls-nocomdat");
    }
  }

  if (Args.getLastArg(options::OPT_mno_bti_at_return_twice))
    Features.push_back("+no-bti-at-return-twice");

  checkARMFloatABI(D, Args, HasFPRegs);

  return FPUKind;
}

std::string arm::getARMArch(StringRef Arch, const llvm::Triple &Triple) {
  std::string MArch;
  if (!Arch.empty())
    MArch = std::string(Arch);
  else
    MArch = std::string(Triple.getArchName());
  MArch = StringRef(MArch).split("+").first.lower();

  // Handle -march=native.
  if (MArch == "native") {
    std::string CPU = std::string(llvm::sys::getHostCPUName());
    if (CPU != "generic") {
      // Translate the native cpu into the architecture suffix for that CPU.
      StringRef Suffix = arm::getLLVMArchSuffixForARM(CPU, MArch, Triple);
      // If there is no valid architecture suffix for this CPU we don't know how
      // to handle it, so return no architecture.
      if (Suffix.empty())
        MArch = "";
      else
        MArch = std::string("arm") + Suffix.str();
    }
  }

  return MArch;
}

/// Get the (LLVM) name of the minimum ARM CPU for the arch we are targeting.
StringRef arm::getARMCPUForMArch(StringRef Arch, const llvm::Triple &Triple) {
  std::string MArch = getARMArch(Arch, Triple);
  // getARMCPUForArch defaults to the triple if MArch is empty, but empty MArch
  // here means an -march=native that we can't handle, so instead return no CPU.
  if (MArch.empty())
    return StringRef();

  // We need to return an empty string here on invalid MArch values as the
  // various places that call this function can't cope with a null result.
  return llvm::ARM::getARMCPUForArch(Triple, MArch);
}

/// getARMTargetCPU - Get the (LLVM) name of the ARM cpu we are targeting.
std::string arm::getARMTargetCPU(StringRef CPU, StringRef Arch,
                                 const llvm::Triple &Triple) {
  // FIXME: Warn on inconsistent use of -mcpu and -march.
  // If we have -mcpu=, use that.
  if (!CPU.empty()) {
    std::string MCPU = StringRef(CPU).split("+").first.lower();
    // Handle -mcpu=native.
    if (MCPU == "native")
      return std::string(llvm::sys::getHostCPUName());
    else
      return MCPU;
  }

  return std::string(getARMCPUForMArch(Arch, Triple));
}

/// getLLVMArchSuffixForARM - Get the LLVM ArchKind value to use for a
/// particular CPU (or Arch, if CPU is generic). This is needed to
/// pass to functions like llvm::ARM::getDefaultFPU which need an
/// ArchKind as well as a CPU name.
llvm::ARM::ArchKind arm::getLLVMArchKindForARM(StringRef CPU, StringRef Arch,
                                               const llvm::Triple &Triple) {
  llvm::ARM::ArchKind ArchKind;
  if (CPU == "generic" || CPU.empty()) {
    std::string ARMArch = tools::arm::getARMArch(Arch, Triple);
    ArchKind = llvm::ARM::parseArch(ARMArch);
    if (ArchKind == llvm::ARM::ArchKind::INVALID)
      // In case of generic Arch, i.e. "arm",
      // extract arch from default cpu of the Triple
      ArchKind =
          llvm::ARM::parseCPUArch(llvm::ARM::getARMCPUForArch(Triple, ARMArch));
  } else {
    // FIXME: horrible hack to get around the fact that Cortex-A7 is only an
    // armv7k triple if it's actually been specified via "-arch armv7k".
    ArchKind = (Arch == "armv7k" || Arch == "thumbv7k")
                          ? llvm::ARM::ArchKind::ARMV7K
                          : llvm::ARM::parseCPUArch(CPU);
  }
  return ArchKind;
}

/// getLLVMArchSuffixForARM - Get the LLVM arch name to use for a particular
/// CPU  (or Arch, if CPU is generic).
// FIXME: This is redundant with -mcpu, why does LLVM use this.
StringRef arm::getLLVMArchSuffixForARM(StringRef CPU, StringRef Arch,
                                       const llvm::Triple &Triple) {
  llvm::ARM::ArchKind ArchKind = getLLVMArchKindForARM(CPU, Arch, Triple);
  if (ArchKind == llvm::ARM::ArchKind::INVALID)
    return "";
  return llvm::ARM::getSubArch(ArchKind);
}

void arm::appendBE8LinkFlag(const ArgList &Args, ArgStringList &CmdArgs,
                            const llvm::Triple &Triple) {
  if (Args.hasArg(options::OPT_r))
    return;

  // ARMv7 (and later) and ARMv6-M do not support BE-32, so instruct the linker
  // to generate BE-8 executables.
  if (arm::getARMSubArchVersionNumber(Triple) >= 7 || arm::isARMMProfile(Triple))
    CmdArgs.push_back("--be8");
}
