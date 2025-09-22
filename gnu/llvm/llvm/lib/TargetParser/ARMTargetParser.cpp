//===-- ARMTargetParser - Parser for ARM target features --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise ARM hardware features
// such as FPU/CPU/ARCH/extensions and specific support such as HWDIV.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/ARMTargetParser.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/ARMTargetParserCommon.h"
#include "llvm/TargetParser/Triple.h"
#include <cctype>

using namespace llvm;

static StringRef getHWDivSynonym(StringRef HWDiv) {
  return StringSwitch<StringRef>(HWDiv)
      .Case("thumb,arm", "arm,thumb")
      .Default(HWDiv);
}

// Allows partial match, ex. "v7a" matches "armv7a".
ARM::ArchKind ARM::parseArch(StringRef Arch) {
  Arch = getCanonicalArchName(Arch);
  StringRef Syn = getArchSynonym(Arch);
  for (const auto &A : ARMArchNames) {
    if (A.Name.ends_with(Syn))
      return A.ID;
  }
  return ArchKind::INVALID;
}

// Version number (ex. v7 = 7).
unsigned ARM::parseArchVersion(StringRef Arch) {
  Arch = getCanonicalArchName(Arch);
  switch (parseArch(Arch)) {
  case ArchKind::ARMV4:
  case ArchKind::ARMV4T:
    return 4;
  case ArchKind::ARMV5T:
  case ArchKind::ARMV5TE:
  case ArchKind::IWMMXT:
  case ArchKind::IWMMXT2:
  case ArchKind::XSCALE:
  case ArchKind::ARMV5TEJ:
    return 5;
  case ArchKind::ARMV6:
  case ArchKind::ARMV6K:
  case ArchKind::ARMV6T2:
  case ArchKind::ARMV6KZ:
  case ArchKind::ARMV6M:
    return 6;
  case ArchKind::ARMV7A:
  case ArchKind::ARMV7VE:
  case ArchKind::ARMV7R:
  case ArchKind::ARMV7M:
  case ArchKind::ARMV7S:
  case ArchKind::ARMV7EM:
  case ArchKind::ARMV7K:
    return 7;
  case ArchKind::ARMV8A:
  case ArchKind::ARMV8_1A:
  case ArchKind::ARMV8_2A:
  case ArchKind::ARMV8_3A:
  case ArchKind::ARMV8_4A:
  case ArchKind::ARMV8_5A:
  case ArchKind::ARMV8_6A:
  case ArchKind::ARMV8_7A:
  case ArchKind::ARMV8_8A:
  case ArchKind::ARMV8_9A:
  case ArchKind::ARMV8R:
  case ArchKind::ARMV8MBaseline:
  case ArchKind::ARMV8MMainline:
  case ArchKind::ARMV8_1MMainline:
    return 8;
  case ArchKind::ARMV9A:
  case ArchKind::ARMV9_1A:
  case ArchKind::ARMV9_2A:
  case ArchKind::ARMV9_3A:
  case ArchKind::ARMV9_4A:
  case ArchKind::ARMV9_5A:
    return 9;
  case ArchKind::INVALID:
    return 0;
  }
  llvm_unreachable("Unhandled architecture");
}

static ARM::ProfileKind getProfileKind(ARM::ArchKind AK) {
  switch (AK) {
  case ARM::ArchKind::ARMV6M:
  case ARM::ArchKind::ARMV7M:
  case ARM::ArchKind::ARMV7EM:
  case ARM::ArchKind::ARMV8MMainline:
  case ARM::ArchKind::ARMV8MBaseline:
  case ARM::ArchKind::ARMV8_1MMainline:
    return ARM::ProfileKind::M;
  case ARM::ArchKind::ARMV7R:
  case ARM::ArchKind::ARMV8R:
    return ARM::ProfileKind::R;
  case ARM::ArchKind::ARMV7A:
  case ARM::ArchKind::ARMV7VE:
  case ARM::ArchKind::ARMV7K:
  case ARM::ArchKind::ARMV8A:
  case ARM::ArchKind::ARMV8_1A:
  case ARM::ArchKind::ARMV8_2A:
  case ARM::ArchKind::ARMV8_3A:
  case ARM::ArchKind::ARMV8_4A:
  case ARM::ArchKind::ARMV8_5A:
  case ARM::ArchKind::ARMV8_6A:
  case ARM::ArchKind::ARMV8_7A:
  case ARM::ArchKind::ARMV8_8A:
  case ARM::ArchKind::ARMV8_9A:
  case ARM::ArchKind::ARMV9A:
  case ARM::ArchKind::ARMV9_1A:
  case ARM::ArchKind::ARMV9_2A:
  case ARM::ArchKind::ARMV9_3A:
  case ARM::ArchKind::ARMV9_4A:
  case ARM::ArchKind::ARMV9_5A:
    return ARM::ProfileKind::A;
  case ARM::ArchKind::ARMV4:
  case ARM::ArchKind::ARMV4T:
  case ARM::ArchKind::ARMV5T:
  case ARM::ArchKind::ARMV5TE:
  case ARM::ArchKind::ARMV5TEJ:
  case ARM::ArchKind::ARMV6:
  case ARM::ArchKind::ARMV6K:
  case ARM::ArchKind::ARMV6T2:
  case ARM::ArchKind::ARMV6KZ:
  case ARM::ArchKind::ARMV7S:
  case ARM::ArchKind::IWMMXT:
  case ARM::ArchKind::IWMMXT2:
  case ARM::ArchKind::XSCALE:
  case ARM::ArchKind::INVALID:
    return ARM::ProfileKind::INVALID;
  }
  llvm_unreachable("Unhandled architecture");
}

// Profile A/R/M
ARM::ProfileKind ARM::parseArchProfile(StringRef Arch) {
  Arch = getCanonicalArchName(Arch);
  return getProfileKind(parseArch(Arch));
}

bool ARM::getFPUFeatures(ARM::FPUKind FPUKind,
                         std::vector<StringRef> &Features) {

  if (FPUKind >= FK_LAST || FPUKind == FK_INVALID)
    return false;

  static const struct FPUFeatureNameInfo {
    const char *PlusName, *MinusName;
    FPUVersion MinVersion;
    FPURestriction MaxRestriction;
  } FPUFeatureInfoList[] = {
    // We have to specify the + and - versions of the name in full so
    // that we can return them as static StringRefs.
    //
    // Also, the SubtargetFeatures ending in just "sp" are listed here
    // under FPURestriction::None, which is the only FPURestriction in
    // which they would be valid (since FPURestriction::SP doesn't
    // exist).
    {"+vfp2", "-vfp2", FPUVersion::VFPV2, FPURestriction::D16},
    {"+vfp2sp", "-vfp2sp", FPUVersion::VFPV2, FPURestriction::SP_D16},
    {"+vfp3", "-vfp3", FPUVersion::VFPV3, FPURestriction::None},
    {"+vfp3d16", "-vfp3d16", FPUVersion::VFPV3, FPURestriction::D16},
    {"+vfp3d16sp", "-vfp3d16sp", FPUVersion::VFPV3, FPURestriction::SP_D16},
    {"+vfp3sp", "-vfp3sp", FPUVersion::VFPV3, FPURestriction::None},
    {"+fp16", "-fp16", FPUVersion::VFPV3_FP16, FPURestriction::SP_D16},
    {"+vfp4", "-vfp4", FPUVersion::VFPV4, FPURestriction::None},
    {"+vfp4d16", "-vfp4d16", FPUVersion::VFPV4, FPURestriction::D16},
    {"+vfp4d16sp", "-vfp4d16sp", FPUVersion::VFPV4, FPURestriction::SP_D16},
    {"+vfp4sp", "-vfp4sp", FPUVersion::VFPV4, FPURestriction::None},
    {"+fp-armv8", "-fp-armv8", FPUVersion::VFPV5, FPURestriction::None},
    {"+fp-armv8d16", "-fp-armv8d16", FPUVersion::VFPV5, FPURestriction::D16},
    {"+fp-armv8d16sp", "-fp-armv8d16sp", FPUVersion::VFPV5, FPURestriction::SP_D16},
    {"+fp-armv8sp", "-fp-armv8sp", FPUVersion::VFPV5, FPURestriction::None},
    {"+fullfp16", "-fullfp16", FPUVersion::VFPV5_FULLFP16, FPURestriction::SP_D16},
    {"+fp64", "-fp64", FPUVersion::VFPV2, FPURestriction::D16},
    {"+d32", "-d32", FPUVersion::VFPV3, FPURestriction::None},
  };

  for (const auto &Info: FPUFeatureInfoList) {
    if (FPUNames[FPUKind].FPUVer >= Info.MinVersion &&
        FPUNames[FPUKind].Restriction <= Info.MaxRestriction)
      Features.push_back(Info.PlusName);
    else
      Features.push_back(Info.MinusName);
  }

  static const struct NeonFeatureNameInfo {
    const char *PlusName, *MinusName;
    NeonSupportLevel MinSupportLevel;
  } NeonFeatureInfoList[] = {
      {"+neon", "-neon", NeonSupportLevel::Neon},
      {"+sha2", "-sha2", NeonSupportLevel::Crypto},
      {"+aes", "-aes", NeonSupportLevel::Crypto},
  };

  for (const auto &Info: NeonFeatureInfoList) {
    if (FPUNames[FPUKind].NeonSupport >= Info.MinSupportLevel)
      Features.push_back(Info.PlusName);
    else
      Features.push_back(Info.MinusName);
  }

  return true;
}

ARM::FPUKind ARM::parseFPU(StringRef FPU) {
  StringRef Syn = getFPUSynonym(FPU);
  for (const auto &F : FPUNames) {
    if (Syn == F.Name)
      return F.ID;
  }
  return FK_INVALID;
}

ARM::NeonSupportLevel ARM::getFPUNeonSupportLevel(ARM::FPUKind FPUKind) {
  if (FPUKind >= FK_LAST)
    return NeonSupportLevel::None;
  return FPUNames[FPUKind].NeonSupport;
}

StringRef ARM::getFPUSynonym(StringRef FPU) {
  return StringSwitch<StringRef>(FPU)
      .Cases("fpa", "fpe2", "fpe3", "maverick", "invalid") // Unsupported
      .Case("vfp2", "vfpv2")
      .Case("vfp3", "vfpv3")
      .Case("vfp4", "vfpv4")
      .Case("vfp3-d16", "vfpv3-d16")
      .Case("vfp4-d16", "vfpv4-d16")
      .Cases("fp4-sp-d16", "vfpv4-sp-d16", "fpv4-sp-d16")
      .Cases("fp4-dp-d16", "fpv4-dp-d16", "vfpv4-d16")
      .Case("fp5-sp-d16", "fpv5-sp-d16")
      .Cases("fp5-dp-d16", "fpv5-dp-d16", "fpv5-d16")
      // FIXME: Clang uses it, but it's bogus, since neon defaults to vfpv3.
      .Case("neon-vfpv3", "neon")
      .Default(FPU);
}

StringRef ARM::getFPUName(ARM::FPUKind FPUKind) {
  if (FPUKind >= FK_LAST)
    return StringRef();
  return FPUNames[FPUKind].Name;
}

ARM::FPUVersion ARM::getFPUVersion(ARM::FPUKind FPUKind) {
  if (FPUKind >= FK_LAST)
    return FPUVersion::NONE;
  return FPUNames[FPUKind].FPUVer;
}

ARM::FPURestriction ARM::getFPURestriction(ARM::FPUKind FPUKind) {
  if (FPUKind >= FK_LAST)
    return FPURestriction::None;
  return FPUNames[FPUKind].Restriction;
}

ARM::FPUKind ARM::getDefaultFPU(StringRef CPU, ARM::ArchKind AK) {
  if (CPU == "generic")
    return ARM::ARMArchNames[static_cast<unsigned>(AK)].DefaultFPU;

  return StringSwitch<ARM::FPUKind>(CPU)
#define ARM_CPU_NAME(NAME, ID, DEFAULT_FPU, IS_DEFAULT, DEFAULT_EXT)           \
  .Case(NAME, DEFAULT_FPU)
#include "llvm/TargetParser/ARMTargetParser.def"
      .Default(ARM::FK_INVALID);
}

uint64_t ARM::getDefaultExtensions(StringRef CPU, ARM::ArchKind AK) {
  if (CPU == "generic")
    return ARM::ARMArchNames[static_cast<unsigned>(AK)].ArchBaseExtensions;

  return StringSwitch<uint64_t>(CPU)
#define ARM_CPU_NAME(NAME, ID, DEFAULT_FPU, IS_DEFAULT, DEFAULT_EXT)           \
  .Case(NAME,                                                                  \
        ARMArchNames[static_cast<unsigned>(ArchKind::ID)].ArchBaseExtensions | \
            DEFAULT_EXT)
#include "llvm/TargetParser/ARMTargetParser.def"
  .Default(ARM::AEK_INVALID);
}

bool ARM::getHWDivFeatures(uint64_t HWDivKind,
                           std::vector<StringRef> &Features) {

  if (HWDivKind == AEK_INVALID)
    return false;

  if (HWDivKind & AEK_HWDIVARM)
    Features.push_back("+hwdiv-arm");
  else
    Features.push_back("-hwdiv-arm");

  if (HWDivKind & AEK_HWDIVTHUMB)
    Features.push_back("+hwdiv");
  else
    Features.push_back("-hwdiv");

  return true;
}

bool ARM::getExtensionFeatures(uint64_t Extensions,
                               std::vector<StringRef> &Features) {

  if (Extensions == AEK_INVALID)
    return false;

  for (const auto &AE : ARCHExtNames) {
    if ((Extensions & AE.ID) == AE.ID && !AE.Feature.empty())
      Features.push_back(AE.Feature);
    else if (!AE.NegFeature.empty())
      Features.push_back(AE.NegFeature);
  }

  return getHWDivFeatures(Extensions, Features);
}

StringRef ARM::getArchName(ARM::ArchKind AK) {
  return ARMArchNames[static_cast<unsigned>(AK)].Name;
}

StringRef ARM::getCPUAttr(ARM::ArchKind AK) {
  return ARMArchNames[static_cast<unsigned>(AK)].CPUAttr;
}

StringRef ARM::getSubArch(ARM::ArchKind AK) {
  return ARMArchNames[static_cast<unsigned>(AK)].getSubArch();
}

unsigned ARM::getArchAttr(ARM::ArchKind AK) {
  return ARMArchNames[static_cast<unsigned>(AK)].ArchAttr;
}

StringRef ARM::getArchExtName(uint64_t ArchExtKind) {
  for (const auto &AE : ARCHExtNames) {
    if (ArchExtKind == AE.ID)
      return AE.Name;
  }
  return StringRef();
}

static bool stripNegationPrefix(StringRef &Name) {
  return Name.consume_front("no");
}

StringRef ARM::getArchExtFeature(StringRef ArchExt) {
  bool Negated = stripNegationPrefix(ArchExt);
  for (const auto &AE : ARCHExtNames) {
    if (!AE.Feature.empty() && ArchExt == AE.Name)
      return StringRef(Negated ? AE.NegFeature : AE.Feature);
  }

  return StringRef();
}

static ARM::FPUKind findDoublePrecisionFPU(ARM::FPUKind InputFPUKind) {
  if (InputFPUKind == ARM::FK_INVALID || InputFPUKind == ARM::FK_NONE)
    return ARM::FK_INVALID;

  const ARM::FPUName &InputFPU = ARM::FPUNames[InputFPUKind];

  // If the input FPU already supports double-precision, then there
  // isn't any different FPU we can return here.
  if (ARM::isDoublePrecision(InputFPU.Restriction))
    return InputFPUKind;

  // Otherwise, look for an FPU entry with all the same fields, except
  // that it supports double precision.
  for (const ARM::FPUName &CandidateFPU : ARM::FPUNames) {
    if (CandidateFPU.FPUVer == InputFPU.FPUVer &&
        CandidateFPU.NeonSupport == InputFPU.NeonSupport &&
        ARM::has32Regs(CandidateFPU.Restriction) ==
            ARM::has32Regs(InputFPU.Restriction) &&
        ARM::isDoublePrecision(CandidateFPU.Restriction)) {
      return CandidateFPU.ID;
    }
  }

  // nothing found
  return ARM::FK_INVALID;
}

static ARM::FPUKind findSinglePrecisionFPU(ARM::FPUKind InputFPUKind) {
  if (InputFPUKind == ARM::FK_INVALID || InputFPUKind == ARM::FK_NONE)
    return ARM::FK_INVALID;

  const ARM::FPUName &InputFPU = ARM::FPUNames[InputFPUKind];

  // If the input FPU already is single-precision only, then there
  // isn't any different FPU we can return here.
  if (!ARM::isDoublePrecision(InputFPU.Restriction))
    return InputFPUKind;

  // Otherwise, look for an FPU entry with all the same fields, except
  // that it does not support double precision.
  for (const ARM::FPUName &CandidateFPU : ARM::FPUNames) {
    if (CandidateFPU.FPUVer == InputFPU.FPUVer &&
        CandidateFPU.NeonSupport == InputFPU.NeonSupport &&
        ARM::has32Regs(CandidateFPU.Restriction) ==
            ARM::has32Regs(InputFPU.Restriction) &&
        !ARM::isDoublePrecision(CandidateFPU.Restriction)) {
      return CandidateFPU.ID;
    }
  }

  // nothing found
  return ARM::FK_INVALID;
}

bool ARM::appendArchExtFeatures(StringRef CPU, ARM::ArchKind AK,
                                StringRef ArchExt,
                                std::vector<StringRef> &Features,
                                ARM::FPUKind &ArgFPUKind) {

  size_t StartingNumFeatures = Features.size();
  const bool Negated = stripNegationPrefix(ArchExt);
  uint64_t ID = parseArchExt(ArchExt);

  if (ID == AEK_INVALID)
    return false;

  for (const auto &AE : ARCHExtNames) {
    if (Negated) {
      if ((AE.ID & ID) == ID && !AE.NegFeature.empty())
        Features.push_back(AE.NegFeature);
    } else {
      if ((AE.ID & ID) == AE.ID && !AE.Feature.empty())
        Features.push_back(AE.Feature);
    }
  }

  if (CPU == "")
    CPU = "generic";

  if (ArchExt == "fp" || ArchExt == "fp.dp") {
    const ARM::FPUKind DefaultFPU = getDefaultFPU(CPU, AK);
    ARM::FPUKind FPUKind;
    if (ArchExt == "fp.dp") {
      const bool IsDP = ArgFPUKind != ARM::FK_INVALID &&
                        ArgFPUKind != ARM::FK_NONE &&
                        isDoublePrecision(getFPURestriction(ArgFPUKind));
      if (Negated) {
        /* If there is no FPU selected yet, we still need to set ArgFPUKind, as
         * leaving it as FK_INVALID, would cause default FPU to be selected
         * later and that could be double precision one. */
        if (ArgFPUKind != ARM::FK_INVALID && !IsDP)
          return true;
        FPUKind = findSinglePrecisionFPU(DefaultFPU);
        if (FPUKind == ARM::FK_INVALID)
          FPUKind = ARM::FK_NONE;
      } else {
        if (IsDP)
          return true;
        FPUKind = findDoublePrecisionFPU(DefaultFPU);
        if (FPUKind == ARM::FK_INVALID)
          return false;
      }
    } else if (Negated) {
      FPUKind = ARM::FK_NONE;
    } else {
      FPUKind = DefaultFPU;
    }
    ArgFPUKind = FPUKind;
    return true;
  }
  return StartingNumFeatures != Features.size();
}

ARM::ArchKind ARM::convertV9toV8(ARM::ArchKind AK) {
  if (getProfileKind(AK) != ProfileKind::A)
    return ARM::ArchKind::INVALID;
  if (AK < ARM::ArchKind::ARMV9A || AK > ARM::ArchKind::ARMV9_3A)
    return ARM::ArchKind::INVALID;
  unsigned AK_v8 = static_cast<unsigned>(ARM::ArchKind::ARMV8_5A);
  AK_v8 += static_cast<unsigned>(AK) -
           static_cast<unsigned>(ARM::ArchKind::ARMV9A);
  return static_cast<ARM::ArchKind>(AK_v8);
}

StringRef ARM::getDefaultCPU(StringRef Arch) {
  ArchKind AK = parseArch(Arch);
  if (AK == ArchKind::INVALID)
    return StringRef();

  // Look for multiple AKs to find the default for pair AK+Name.
  for (const auto &CPU : CPUNames) {
    if (CPU.ArchID == AK && CPU.Default)
      return CPU.Name;
  }

  // If we can't find a default then target the architecture instead
  return "generic";
}

uint64_t ARM::parseHWDiv(StringRef HWDiv) {
  StringRef Syn = getHWDivSynonym(HWDiv);
  for (const auto &D : HWDivNames) {
    if (Syn == D.Name)
      return D.ID;
  }
  return AEK_INVALID;
}

uint64_t ARM::parseArchExt(StringRef ArchExt) {
  for (const auto &A : ARCHExtNames) {
    if (ArchExt == A.Name)
      return A.ID;
  }
  return AEK_INVALID;
}

ARM::ArchKind ARM::parseCPUArch(StringRef CPU) {
  for (const auto &C : CPUNames) {
    if (CPU == C.Name)
      return C.ArchID;
  }
  return ArchKind::INVALID;
}

void ARM::fillValidCPUArchList(SmallVectorImpl<StringRef> &Values) {
  for (const auto &Arch : CPUNames) {
    if (Arch.ArchID != ArchKind::INVALID)
      Values.push_back(Arch.Name);
  }
}

StringRef ARM::computeDefaultTargetABI(const Triple &TT, StringRef CPU) {
  StringRef ArchName =
      CPU.empty() ? TT.getArchName() : getArchName(parseCPUArch(CPU));

  if (TT.isOSBinFormatMachO()) {
    if (TT.getEnvironment() == Triple::EABI ||
        TT.getOS() == Triple::UnknownOS ||
        parseArchProfile(ArchName) == ProfileKind::M)
      return "aapcs";
    if (TT.isWatchABI())
      return "aapcs16";
    return "apcs-gnu";
  } else if (TT.isOSWindows())
    // FIXME: this is invalid for WindowsCE.
    return "aapcs";

  // Select the default based on the platform.
  switch (TT.getEnvironment()) {
  case Triple::Android:
  case Triple::GNUEABI:
  case Triple::GNUEABIT64:
  case Triple::GNUEABIHF:
  case Triple::GNUEABIHFT64:
  case Triple::MuslEABI:
  case Triple::MuslEABIHF:
  case Triple::OpenHOS:
    return "aapcs-linux";
  case Triple::EABIHF:
  case Triple::EABI:
    return "aapcs";
  default:
    if (TT.isOSNetBSD())
      return "apcs-gnu";
    if (TT.isOSFreeBSD() || TT.isOSOpenBSD() || TT.isOSHaiku() ||
        TT.isOHOSFamily())
      return "aapcs-linux";
    return "aapcs";
  }
}

StringRef ARM::getARMCPUForArch(const llvm::Triple &Triple, StringRef MArch) {
  if (MArch.empty())
    MArch = Triple.getArchName();
  MArch = llvm::ARM::getCanonicalArchName(MArch);

  // Some defaults are forced.
  switch (Triple.getOS()) {
  case llvm::Triple::FreeBSD:
  case llvm::Triple::NetBSD:
  case llvm::Triple::OpenBSD:
  case llvm::Triple::Haiku:
    if (!MArch.empty() && MArch == "v6")
      return "arm1176jzf-s";
    if (!MArch.empty() && MArch == "v7")
      return "cortex-a8";
    break;
  case llvm::Triple::Win32:
    // FIXME: this is invalid for WindowsCE
    if (llvm::ARM::parseArchVersion(MArch) <= 7)
      return "cortex-a9";
    break;
  case llvm::Triple::IOS:
  case llvm::Triple::MacOSX:
  case llvm::Triple::TvOS:
  case llvm::Triple::WatchOS:
  case llvm::Triple::DriverKit:
  case llvm::Triple::XROS:
    if (MArch == "v7k")
      return "cortex-a7";
    break;
  default:
    break;
  }

  if (MArch.empty())
    return StringRef();

  StringRef CPU = llvm::ARM::getDefaultCPU(MArch);
  if (!CPU.empty() && CPU != "invalid")
    return CPU;

  // If no specific architecture version is requested, return the minimum CPU
  // required by the OS and environment.
  switch (Triple.getOS()) {
  case llvm::Triple::Haiku:
    return "arm1176jzf-s";
  case llvm::Triple::NetBSD:
    switch (Triple.getEnvironment()) {
    case llvm::Triple::EABI:
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABI:
    case llvm::Triple::GNUEABIHF:
      return "arm926ej-s";
    default:
      return "strongarm";
    }
  case llvm::Triple::NaCl:
  case llvm::Triple::OpenBSD:
    return "cortex-a8";
  default:
    switch (Triple.getEnvironment()) {
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::GNUEABIHFT64:
    case llvm::Triple::MuslEABIHF:
      return "arm1176jzf-s";
    default:
      return "arm7tdmi";
    }
  }

  llvm_unreachable("invalid arch name");
}

void ARM::PrintSupportedExtensions(StringMap<StringRef> DescMap) {
  outs() << "All available -march extensions for ARM\n\n"
         << "    " << left_justify("Name", 20)
         << (DescMap.empty() ? "\n" : "Description\n");
  for (const auto &Ext : ARCHExtNames) {
    // Extensions without a feature cannot be used with -march.
    if (!Ext.Feature.empty()) {
      std::string Description = DescMap[Ext.Name].str();
      outs() << "    "
             << format(Description.empty() ? "%s\n" : "%-20s%s\n",
                       Ext.Name.str().c_str(), Description.c_str());
    }
  }
}
