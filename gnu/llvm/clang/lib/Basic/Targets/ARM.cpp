//===--- ARM.cpp - Implement ARM target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements ARM TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/TargetParser/ARMTargetParser.h"

using namespace clang;
using namespace clang::targets;

void ARMTargetInfo::setABIAAPCS() {
  IsAAPCS = true;

  DoubleAlign = LongLongAlign = LongDoubleAlign = SuitableAlign = 64;
  BFloat16Width = BFloat16Align = 16;
  BFloat16Format = &llvm::APFloat::BFloat();

  const llvm::Triple &T = getTriple();

  bool IsNetBSD = T.isOSNetBSD();
  bool IsOpenBSD = T.isOSOpenBSD();
  if (!T.isOSWindows() && !IsNetBSD && !IsOpenBSD)
    WCharType = UnsignedInt;

  UseBitFieldTypeAlignment = true;

  ZeroLengthBitfieldBoundary = 0;

  // Thumb1 add sp, #imm requires the immediate value be multiple of 4,
  // so set preferred for small types to 32.
  if (T.isOSBinFormatMachO()) {
    resetDataLayout(BigEndian
                        ? "E-m:o-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
                        : "e-m:o-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64",
                    "_");
  } else if (T.isOSWindows()) {
    assert(!BigEndian && "Windows on ARM does not support big endian");
    resetDataLayout("e"
                    "-m:w"
                    "-p:32:32"
                    "-Fi8"
                    "-i64:64"
                    "-v128:64:128"
                    "-a:0:32"
                    "-n32"
                    "-S64");
  } else if (T.isOSNaCl()) {
    assert(!BigEndian && "NaCl on ARM does not support big endian");
    resetDataLayout("e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S128");
  } else {
    resetDataLayout(BigEndian
                        ? "E-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
                        : "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64");
  }

  // FIXME: Enumerated types are variable width in straight AAPCS.
}

void ARMTargetInfo::setABIAPCS(bool IsAAPCS16) {
  const llvm::Triple &T = getTriple();

  IsAAPCS = false;

  if (IsAAPCS16)
    DoubleAlign = LongLongAlign = LongDoubleAlign = SuitableAlign = 64;
  else
    DoubleAlign = LongLongAlign = LongDoubleAlign = SuitableAlign = 32;
  BFloat16Width = BFloat16Align = 16;
  BFloat16Format = &llvm::APFloat::BFloat();

  WCharType = SignedInt;

  // Do not respect the alignment of bit-field types when laying out
  // structures. This corresponds to PCC_BITFIELD_TYPE_MATTERS in gcc.
  UseBitFieldTypeAlignment = false;

  /// gcc forces the alignment to 4 bytes, regardless of the type of the
  /// zero length bitfield.  This corresponds to EMPTY_FIELD_BOUNDARY in
  /// gcc.
  ZeroLengthBitfieldBoundary = 32;

  if (T.isOSBinFormatMachO() && IsAAPCS16) {
    assert(!BigEndian && "AAPCS16 does not support big-endian");
    resetDataLayout("e-m:o-p:32:32-Fi8-i64:64-a:0:32-n32-S128", "_");
  } else if (T.isOSBinFormatMachO())
    resetDataLayout(
        BigEndian
            ? "E-m:o-p:32:32-Fi8-f64:32:64-v64:32:64-v128:32:128-a:0:32-n32-S32"
            : "e-m:o-p:32:32-Fi8-f64:32:64-v64:32:64-v128:32:128-a:0:32-n32-S32",
        "_");
  else
    resetDataLayout(
        BigEndian
            ? "E-m:e-p:32:32-Fi8-f64:32:64-v64:32:64-v128:32:128-a:0:32-n32-S32"
            : "e-m:e-p:32:32-Fi8-f64:32:64-v64:32:64-v128:32:128-a:0:32-n32-S32");

  // FIXME: Override "preferred align" for double and long long.
}

void ARMTargetInfo::setArchInfo() {
  StringRef ArchName = getTriple().getArchName();

  ArchISA = llvm::ARM::parseArchISA(ArchName);
  CPU = std::string(llvm::ARM::getDefaultCPU(ArchName));
  llvm::ARM::ArchKind AK = llvm::ARM::parseArch(ArchName);
  if (AK != llvm::ARM::ArchKind::INVALID)
    ArchKind = AK;
  setArchInfo(ArchKind);
}

void ARMTargetInfo::setArchInfo(llvm::ARM::ArchKind Kind) {
  StringRef SubArch;

  // cache TargetParser info
  ArchKind = Kind;
  SubArch = llvm::ARM::getSubArch(ArchKind);
  ArchProfile = llvm::ARM::parseArchProfile(SubArch);
  ArchVersion = llvm::ARM::parseArchVersion(SubArch);

  // cache CPU related strings
  CPUAttr = getCPUAttr();
  CPUProfile = getCPUProfile();
}

void ARMTargetInfo::setAtomic() {
  // when triple does not specify a sub arch,
  // then we are not using inline atomics
  bool ShouldUseInlineAtomic =
      (ArchISA == llvm::ARM::ISAKind::ARM && ArchVersion >= 6) ||
      (ArchISA == llvm::ARM::ISAKind::THUMB && ArchVersion >= 7);
  // Cortex M does not support 8 byte atomics, while general Thumb2 does.
  if (ArchProfile == llvm::ARM::ProfileKind::M) {
    MaxAtomicPromoteWidth = 32;
    if (ShouldUseInlineAtomic)
      MaxAtomicInlineWidth = 32;
  } else {
    MaxAtomicPromoteWidth = 64;
    if (ShouldUseInlineAtomic)
      MaxAtomicInlineWidth = 64;
  }
}

bool ARMTargetInfo::hasMVE() const {
  return ArchKind == llvm::ARM::ArchKind::ARMV8_1MMainline && MVE != 0;
}

bool ARMTargetInfo::hasMVEFloat() const {
  return hasMVE() && (MVE & MVE_FP);
}

bool ARMTargetInfo::hasCDE() const { return getARMCDECoprocMask() != 0; }

bool ARMTargetInfo::isThumb() const {
  return ArchISA == llvm::ARM::ISAKind::THUMB;
}

bool ARMTargetInfo::supportsThumb() const {
  return CPUAttr.count('T') || ArchVersion >= 6;
}

bool ARMTargetInfo::supportsThumb2() const {
  return CPUAttr == "6T2" || (ArchVersion >= 7 && CPUAttr != "8M_BASE");
}

StringRef ARMTargetInfo::getCPUAttr() const {
  // For most sub-arches, the build attribute CPU name is enough.
  // For Cortex variants, it's slightly different.
  switch (ArchKind) {
  default:
    return llvm::ARM::getCPUAttr(ArchKind);
  case llvm::ARM::ArchKind::ARMV6M:
    return "6M";
  case llvm::ARM::ArchKind::ARMV7S:
    return "7S";
  case llvm::ARM::ArchKind::ARMV7A:
    return "7A";
  case llvm::ARM::ArchKind::ARMV7R:
    return "7R";
  case llvm::ARM::ArchKind::ARMV7M:
    return "7M";
  case llvm::ARM::ArchKind::ARMV7EM:
    return "7EM";
  case llvm::ARM::ArchKind::ARMV7VE:
    return "7VE";
  case llvm::ARM::ArchKind::ARMV8A:
    return "8A";
  case llvm::ARM::ArchKind::ARMV8_1A:
    return "8_1A";
  case llvm::ARM::ArchKind::ARMV8_2A:
    return "8_2A";
  case llvm::ARM::ArchKind::ARMV8_3A:
    return "8_3A";
  case llvm::ARM::ArchKind::ARMV8_4A:
    return "8_4A";
  case llvm::ARM::ArchKind::ARMV8_5A:
    return "8_5A";
  case llvm::ARM::ArchKind::ARMV8_6A:
    return "8_6A";
  case llvm::ARM::ArchKind::ARMV8_7A:
    return "8_7A";
  case llvm::ARM::ArchKind::ARMV8_8A:
    return "8_8A";
  case llvm::ARM::ArchKind::ARMV8_9A:
    return "8_9A";
  case llvm::ARM::ArchKind::ARMV9A:
    return "9A";
  case llvm::ARM::ArchKind::ARMV9_1A:
    return "9_1A";
  case llvm::ARM::ArchKind::ARMV9_2A:
    return "9_2A";
  case llvm::ARM::ArchKind::ARMV9_3A:
    return "9_3A";
  case llvm::ARM::ArchKind::ARMV9_4A:
    return "9_4A";
  case llvm::ARM::ArchKind::ARMV9_5A:
    return "9_5A";
  case llvm::ARM::ArchKind::ARMV8MBaseline:
    return "8M_BASE";
  case llvm::ARM::ArchKind::ARMV8MMainline:
    return "8M_MAIN";
  case llvm::ARM::ArchKind::ARMV8R:
    return "8R";
  case llvm::ARM::ArchKind::ARMV8_1MMainline:
    return "8_1M_MAIN";
  }
}

StringRef ARMTargetInfo::getCPUProfile() const {
  switch (ArchProfile) {
  case llvm::ARM::ProfileKind::A:
    return "A";
  case llvm::ARM::ProfileKind::R:
    return "R";
  case llvm::ARM::ProfileKind::M:
    return "M";
  default:
    return "";
  }
}

ARMTargetInfo::ARMTargetInfo(const llvm::Triple &Triple,
                             const TargetOptions &Opts)
    : TargetInfo(Triple), FPMath(FP_Default), IsAAPCS(true), LDREX(0),
      HW_FP(0) {
  bool IsFreeBSD = Triple.isOSFreeBSD();
  bool IsOpenBSD = Triple.isOSOpenBSD();
  bool IsNetBSD = Triple.isOSNetBSD();
  bool IsHaiku = Triple.isOSHaiku();
  bool IsOHOS = Triple.isOHOSFamily();

  // FIXME: the isOSBinFormatMachO is a workaround for identifying a Darwin-like
  // environment where size_t is `unsigned long` rather than `unsigned int`

  PtrDiffType = IntPtrType =
      (Triple.isOSDarwin() || Triple.isOSBinFormatMachO() || IsOpenBSD ||
       IsNetBSD)
          ? SignedLong
          : SignedInt;

  SizeType = (Triple.isOSDarwin() || Triple.isOSBinFormatMachO() || IsOpenBSD ||
              IsNetBSD)
                 ? UnsignedLong
                 : UnsignedInt;

  // ptrdiff_t is inconsistent on Darwin
  if ((Triple.isOSDarwin() || Triple.isOSBinFormatMachO()) &&
      !Triple.isWatchABI())
    PtrDiffType = SignedInt;

  // Cache arch related info.
  setArchInfo();

  // {} in inline assembly are neon specifiers, not assembly variant
  // specifiers.
  NoAsmVariants = true;

  // FIXME: This duplicates code from the driver that sets the -target-abi
  // option - this code is used if -target-abi isn't passed and should
  // be unified in some way.
  if (Triple.isOSBinFormatMachO()) {
    // The backend is hardwired to assume AAPCS for M-class processors, ensure
    // the frontend matches that.
    if (Triple.getEnvironment() == llvm::Triple::EABI ||
        Triple.getOS() == llvm::Triple::UnknownOS ||
        ArchProfile == llvm::ARM::ProfileKind::M) {
      setABI("aapcs");
    } else if (Triple.isWatchABI()) {
      setABI("aapcs16");
    } else {
      setABI("apcs-gnu");
    }
  } else if (Triple.isOSWindows()) {
    // FIXME: this is invalid for WindowsCE
    setABI("aapcs");
  } else {
    // Select the default based on the platform.
    switch (Triple.getEnvironment()) {
    case llvm::Triple::Android:
    case llvm::Triple::GNUEABI:
    case llvm::Triple::GNUEABIT64:
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::GNUEABIHFT64:
    case llvm::Triple::MuslEABI:
    case llvm::Triple::MuslEABIHF:
    case llvm::Triple::OpenHOS:
      setABI("aapcs-linux");
      break;
    case llvm::Triple::EABIHF:
    case llvm::Triple::EABI:
      setABI("aapcs");
      break;
    case llvm::Triple::GNU:
      setABI("apcs-gnu");
      break;
    default:
      if (IsNetBSD)
        setABI("apcs-gnu");
      else if (IsFreeBSD || IsOpenBSD || IsHaiku || IsOHOS)
        setABI("aapcs-linux");
      else
        setABI("aapcs");
      break;
    }
  }

  // ARM targets default to using the ARM C++ ABI.
  TheCXXABI.set(TargetCXXABI::GenericARM);

  // ARM has atomics up to 8 bytes
  setAtomic();

  // Maximum alignment for ARM NEON data types should be 64-bits (AAPCS)
  // as well the default alignment
  if (IsAAPCS && !Triple.isAndroid())
    DefaultAlignForAttributeAligned = MaxVectorAlign = 64;

  // Do force alignment of members that follow zero length bitfields.  If
  // the alignment of the zero-length bitfield is greater than the member
  // that follows it, `bar', `bar' will be aligned as the  type of the
  // zero length bitfield.
  UseZeroLengthBitfieldAlignment = true;

  if (Triple.getOS() == llvm::Triple::Linux ||
      Triple.getOS() == llvm::Triple::UnknownOS)
    this->MCountName = Opts.EABIVersion == llvm::EABI::GNU
                           ? "llvm.arm.gnu.eabi.mcount"
                           : "\01mcount";

  SoftFloatABI = llvm::is_contained(Opts.FeaturesAsWritten, "+soft-float-abi");
}

StringRef ARMTargetInfo::getABI() const { return ABI; }

bool ARMTargetInfo::setABI(const std::string &Name) {
  ABI = Name;

  // The defaults (above) are for AAPCS, check if we need to change them.
  //
  // FIXME: We need support for -meabi... we could just mangle it into the
  // name.
  if (Name == "apcs-gnu" || Name == "aapcs16") {
    setABIAPCS(Name == "aapcs16");
    return true;
  }
  if (Name == "aapcs" || Name == "aapcs-vfp" || Name == "aapcs-linux") {
    setABIAAPCS();
    return true;
  }
  return false;
}

bool ARMTargetInfo::isBranchProtectionSupportedArch(StringRef Arch) const {
  llvm::ARM::ArchKind CPUArch = llvm::ARM::parseCPUArch(Arch);
  if (CPUArch == llvm::ARM::ArchKind::INVALID)
    CPUArch = llvm::ARM::parseArch(getTriple().getArchName());

  if (CPUArch == llvm::ARM::ArchKind::INVALID)
    return false;

  StringRef ArchFeature = llvm::ARM::getArchName(CPUArch);
  auto a =
      llvm::Triple(ArchFeature, getTriple().getVendorName(),
                   getTriple().getOSName(), getTriple().getEnvironmentName());

  StringRef SubArch = llvm::ARM::getSubArch(CPUArch);
  llvm::ARM::ProfileKind Profile = llvm::ARM::parseArchProfile(SubArch);
  return a.isArmT32() && (Profile == llvm::ARM::ProfileKind::M);
}

bool ARMTargetInfo::validateBranchProtection(StringRef Spec, StringRef Arch,
                                             BranchProtectionInfo &BPI,
                                             StringRef &Err) const {
  llvm::ARM::ParsedBranchProtection PBP;
  if (!llvm::ARM::parseBranchProtection(Spec, PBP, Err))
    return false;

  if (!isBranchProtectionSupportedArch(Arch))
    return false;

  BPI.SignReturnAddr =
      llvm::StringSwitch<LangOptions::SignReturnAddressScopeKind>(PBP.Scope)
          .Case("non-leaf", LangOptions::SignReturnAddressScopeKind::NonLeaf)
          .Case("all", LangOptions::SignReturnAddressScopeKind::All)
          .Default(LangOptions::SignReturnAddressScopeKind::None);

  // Don't care for the sign key, beyond issuing a warning.
  if (PBP.Key == "b_key")
    Err = "b-key";
  BPI.SignKey = LangOptions::SignReturnAddressKeyKind::AKey;

  BPI.BranchTargetEnforcement = PBP.BranchTargetEnforcement;
  BPI.BranchProtectionPAuthLR = PBP.BranchProtectionPAuthLR;
  return true;
}

// FIXME: This should be based on Arch attributes, not CPU names.
bool ARMTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {

  std::string ArchFeature;
  std::vector<StringRef> TargetFeatures;
  llvm::ARM::ArchKind Arch = llvm::ARM::parseArch(getTriple().getArchName());

  // Map the base architecture to an appropriate target feature, so we don't
  // rely on the target triple.
  llvm::ARM::ArchKind CPUArch = llvm::ARM::parseCPUArch(CPU);
  if (CPUArch == llvm::ARM::ArchKind::INVALID)
    CPUArch = Arch;
  if (CPUArch != llvm::ARM::ArchKind::INVALID) {
    ArchFeature = ("+" + llvm::ARM::getArchName(CPUArch)).str();
    TargetFeatures.push_back(ArchFeature);

    // These features are added to allow arm_neon.h target(..) attributes to
    // match with both arm and aarch64. We need to add all previous architecture
    // versions, so that "8.6" also allows "8.1" functions. In case of v9.x the
    // v8.x counterparts are added too. We only need these for anything > 8.0-A.
    for (llvm::ARM::ArchKind I = llvm::ARM::convertV9toV8(CPUArch);
         I != llvm::ARM::ArchKind::INVALID; --I)
      Features[llvm::ARM::getSubArch(I)] = true;
    if (CPUArch > llvm::ARM::ArchKind::ARMV8A &&
        CPUArch <= llvm::ARM::ArchKind::ARMV9_3A)
      for (llvm::ARM::ArchKind I = CPUArch; I != llvm::ARM::ArchKind::INVALID;
           --I)
        Features[llvm::ARM::getSubArch(I)] = true;
  }

  // get default FPU features
  llvm::ARM::FPUKind FPUKind = llvm::ARM::getDefaultFPU(CPU, Arch);
  llvm::ARM::getFPUFeatures(FPUKind, TargetFeatures);

  // get default Extension features
  uint64_t Extensions = llvm::ARM::getDefaultExtensions(CPU, Arch);
  llvm::ARM::getExtensionFeatures(Extensions, TargetFeatures);

  for (auto Feature : TargetFeatures)
    if (Feature[0] == '+')
      Features[Feature.drop_front(1)] = true;

  // Enable or disable thumb-mode explicitly per function to enable mixed
  // ARM and Thumb code generation.
  if (isThumb())
    Features["thumb-mode"] = true;
  else
    Features["thumb-mode"] = false;

  // Convert user-provided arm and thumb GNU target attributes to
  // [-|+]thumb-mode target features respectively.
  std::vector<std::string> UpdatedFeaturesVec;
  for (const auto &Feature : FeaturesVec) {
    // Skip soft-float-abi; it's something we only use to initialize a bit of
    // class state, and is otherwise unrecognized.
    if (Feature == "+soft-float-abi")
      continue;

    StringRef FixedFeature;
    if (Feature == "+arm")
      FixedFeature = "-thumb-mode";
    else if (Feature == "+thumb")
      FixedFeature = "+thumb-mode";
    else
      FixedFeature = Feature;
    UpdatedFeaturesVec.push_back(FixedFeature.str());
  }

  return TargetInfo::initFeatureMap(Features, Diags, CPU, UpdatedFeaturesVec);
}


bool ARMTargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                         DiagnosticsEngine &Diags) {
  FPU = 0;
  MVE = 0;
  CRC = 0;
  Crypto = 0;
  SHA2 = 0;
  AES = 0;
  DSP = 0;
  HasUnalignedAccess = true;
  SoftFloat = false;
  // Note that SoftFloatABI is initialized in our constructor.
  HWDiv = 0;
  DotProd = 0;
  HasMatMul = 0;
  HasPAC = 0;
  HasBTI = 0;
  HasFloat16 = true;
  ARMCDECoprocMask = 0;
  HasBFloat16 = false;
  HasFullBFloat16 = false;
  FPRegsDisabled = false;

  // This does not diagnose illegal cases like having both
  // "+vfpv2" and "+vfpv3" or having "+neon" and "-fp64".
  for (const auto &Feature : Features) {
    if (Feature == "+soft-float") {
      SoftFloat = true;
    } else if (Feature == "+vfp2sp" || Feature == "+vfp2") {
      FPU |= VFP2FPU;
      HW_FP |= HW_FP_SP;
      if (Feature == "+vfp2")
          HW_FP |= HW_FP_DP;
    } else if (Feature == "+vfp3sp" || Feature == "+vfp3d16sp" ||
               Feature == "+vfp3" || Feature == "+vfp3d16") {
      FPU |= VFP3FPU;
      HW_FP |= HW_FP_SP;
      if (Feature == "+vfp3" || Feature == "+vfp3d16")
          HW_FP |= HW_FP_DP;
    } else if (Feature == "+vfp4sp" || Feature == "+vfp4d16sp" ||
               Feature == "+vfp4" || Feature == "+vfp4d16") {
      FPU |= VFP4FPU;
      HW_FP |= HW_FP_SP | HW_FP_HP;
      if (Feature == "+vfp4" || Feature == "+vfp4d16")
          HW_FP |= HW_FP_DP;
    } else if (Feature == "+fp-armv8sp" || Feature == "+fp-armv8d16sp" ||
               Feature == "+fp-armv8" || Feature == "+fp-armv8d16") {
      FPU |= FPARMV8;
      HW_FP |= HW_FP_SP | HW_FP_HP;
      if (Feature == "+fp-armv8" || Feature == "+fp-armv8d16")
          HW_FP |= HW_FP_DP;
    } else if (Feature == "+neon") {
      FPU |= NeonFPU;
      HW_FP |= HW_FP_SP;
    } else if (Feature == "+hwdiv") {
      HWDiv |= HWDivThumb;
    } else if (Feature == "+hwdiv-arm") {
      HWDiv |= HWDivARM;
    } else if (Feature == "+crc") {
      CRC = 1;
    } else if (Feature == "+crypto") {
      Crypto = 1;
    } else if (Feature == "+sha2") {
      SHA2 = 1;
    } else if (Feature == "+aes") {
      AES = 1;
    } else if (Feature == "+dsp") {
      DSP = 1;
    } else if (Feature == "+fp64") {
      HW_FP |= HW_FP_DP;
    } else if (Feature == "+8msecext") {
      if (CPUProfile != "M" || ArchVersion != 8) {
        Diags.Report(diag::err_target_unsupported_mcmse) << CPU;
        return false;
      }
    } else if (Feature == "+strict-align") {
      HasUnalignedAccess = false;
    } else if (Feature == "+fp16") {
      HW_FP |= HW_FP_HP;
    } else if (Feature == "+fullfp16") {
      HasLegalHalfType = true;
    } else if (Feature == "+dotprod") {
      DotProd = true;
    } else if (Feature == "+mve") {
      MVE |= MVE_INT;
    } else if (Feature == "+mve.fp") {
      HasLegalHalfType = true;
      FPU |= FPARMV8;
      MVE |= MVE_INT | MVE_FP;
      HW_FP |= HW_FP_SP | HW_FP_HP;
    } else if (Feature == "+i8mm") {
      HasMatMul = 1;
    } else if (Feature.size() == strlen("+cdecp0") && Feature >= "+cdecp0" &&
               Feature <= "+cdecp7") {
      unsigned Coproc = Feature.back() - '0';
      ARMCDECoprocMask |= (1U << Coproc);
    } else if (Feature == "+bf16") {
      HasBFloat16 = true;
    } else if (Feature == "-fpregs") {
      FPRegsDisabled = true;
    } else if (Feature == "+pacbti") {
      HasPAC = 1;
      HasBTI = 1;
    } else if (Feature == "+fullbf16") {
      HasFullBFloat16 = true;
    }
  }

  HalfArgsAndReturns = true;

  switch (ArchVersion) {
  case 6:
    if (ArchProfile == llvm::ARM::ProfileKind::M)
      LDREX = 0;
    else if (ArchKind == llvm::ARM::ArchKind::ARMV6K)
      LDREX = LDREX_D | LDREX_W | LDREX_H | LDREX_B;
    else
      LDREX = LDREX_W;
    break;
  case 7:
    if (ArchProfile == llvm::ARM::ProfileKind::M)
      LDREX = LDREX_W | LDREX_H | LDREX_B;
    else
      LDREX = LDREX_D | LDREX_W | LDREX_H | LDREX_B;
    break;
  case 8:
  case 9:
    LDREX = LDREX_D | LDREX_W | LDREX_H | LDREX_B;
  }

  if (!(FPU & NeonFPU) && FPMath == FP_Neon) {
    Diags.Report(diag::err_target_unsupported_fpmath) << "neon";
    return false;
  }

  if (FPMath == FP_Neon)
    Features.push_back("+neonfp");
  else if (FPMath == FP_VFP)
    Features.push_back("-neonfp");

  return true;
}

bool ARMTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("arm", true)
      .Case("aarch32", true)
      .Case("softfloat", SoftFloat)
      .Case("thumb", isThumb())
      .Case("neon", (FPU & NeonFPU) && !SoftFloat)
      .Case("vfp", FPU && !SoftFloat)
      .Case("hwdiv", HWDiv & HWDivThumb)
      .Case("hwdiv-arm", HWDiv & HWDivARM)
      .Case("mve", hasMVE())
      .Default(false);
}

bool ARMTargetInfo::hasBFloat16Type() const {
  // The __bf16 type is generally available so long as we have any fp registers.
  return HasBFloat16 || (FPU && !SoftFloat);
}

bool ARMTargetInfo::isValidCPUName(StringRef Name) const {
  return Name == "generic" ||
         llvm::ARM::parseCPUArch(Name) != llvm::ARM::ArchKind::INVALID;
}

void ARMTargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &Values) const {
  llvm::ARM::fillValidCPUArchList(Values);
}

bool ARMTargetInfo::setCPU(const std::string &Name) {
  if (Name != "generic")
    setArchInfo(llvm::ARM::parseCPUArch(Name));

  if (ArchKind == llvm::ARM::ArchKind::INVALID)
    return false;
  setAtomic();
  CPU = Name;
  return true;
}

bool ARMTargetInfo::setFPMath(StringRef Name) {
  if (Name == "neon") {
    FPMath = FP_Neon;
    return true;
  } else if (Name == "vfp" || Name == "vfp2" || Name == "vfp3" ||
             Name == "vfp4") {
    FPMath = FP_VFP;
    return true;
  }
  return false;
}

void ARMTargetInfo::getTargetDefinesARMV81A(const LangOptions &Opts,
                                            MacroBuilder &Builder) const {
  Builder.defineMacro("__ARM_FEATURE_QRDMX", "1");
}

void ARMTargetInfo::getTargetDefinesARMV82A(const LangOptions &Opts,
                                            MacroBuilder &Builder) const {
  // Also include the ARMv8.1-A defines
  getTargetDefinesARMV81A(Opts, Builder);
}

void ARMTargetInfo::getTargetDefinesARMV83A(const LangOptions &Opts,
                                            MacroBuilder &Builder) const {
  // Also include the ARMv8.2-A defines
  Builder.defineMacro("__ARM_FEATURE_COMPLEX", "1");
  getTargetDefinesARMV82A(Opts, Builder);
}

void ARMTargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  // Target identification.
  Builder.defineMacro("__arm");
  Builder.defineMacro("__arm__");
  // For bare-metal none-eabi.
  if (getTriple().getOS() == llvm::Triple::UnknownOS &&
      (getTriple().getEnvironment() == llvm::Triple::EABI ||
       getTriple().getEnvironment() == llvm::Triple::EABIHF) &&
      Opts.CPlusPlus) {
    Builder.defineMacro("_GNU_SOURCE");
  }

  // Target properties.
  Builder.defineMacro("__REGISTER_PREFIX__", "");

  // Unfortunately, __ARM_ARCH_7K__ is now more of an ABI descriptor. The CPU
  // happens to be Cortex-A7 though, so it should still get __ARM_ARCH_7A__.
  if (getTriple().isWatchABI())
    Builder.defineMacro("__ARM_ARCH_7K__", "2");

  if (!CPUAttr.empty())
    Builder.defineMacro("__ARM_ARCH_" + CPUAttr + "__");

  // ACLE 6.4.1 ARM/Thumb instruction set architecture
  // __ARM_ARCH is defined as an integer value indicating the current ARM ISA
  Builder.defineMacro("__ARM_ARCH", Twine(ArchVersion));

  if (ArchVersion >= 8) {
    // ACLE 6.5.7 Crypto Extension
    // The __ARM_FEATURE_CRYPTO is deprecated in favor of finer grained
    // feature macros for AES and SHA2
    if (SHA2 && AES)
      Builder.defineMacro("__ARM_FEATURE_CRYPTO", "1");
    if (SHA2)
      Builder.defineMacro("__ARM_FEATURE_SHA2", "1");
    if (AES)
      Builder.defineMacro("__ARM_FEATURE_AES", "1");
    // ACLE 6.5.8 CRC32 Extension
    if (CRC)
      Builder.defineMacro("__ARM_FEATURE_CRC32", "1");
    // ACLE 6.5.10 Numeric Maximum and Minimum
    Builder.defineMacro("__ARM_FEATURE_NUMERIC_MAXMIN", "1");
    // ACLE 6.5.9 Directed Rounding
    Builder.defineMacro("__ARM_FEATURE_DIRECTED_ROUNDING", "1");
  }

  // __ARM_ARCH_ISA_ARM is defined to 1 if the core supports the ARM ISA.  It
  // is not defined for the M-profile.
  // NOTE that the default profile is assumed to be 'A'
  if (CPUProfile.empty() || ArchProfile != llvm::ARM::ProfileKind::M)
    Builder.defineMacro("__ARM_ARCH_ISA_ARM", "1");

  // __ARM_ARCH_ISA_THUMB is defined to 1 if the core supports the original
  // Thumb ISA (including v6-M and v8-M Baseline).  It is set to 2 if the
  // core supports the Thumb-2 ISA as found in the v6T2 architecture and all
  // v7 and v8 architectures excluding v8-M Baseline.
  if (supportsThumb2())
    Builder.defineMacro("__ARM_ARCH_ISA_THUMB", "2");
  else if (supportsThumb())
    Builder.defineMacro("__ARM_ARCH_ISA_THUMB", "1");

  // __ARM_32BIT_STATE is defined to 1 if code is being generated for a 32-bit
  // instruction set such as ARM or Thumb.
  Builder.defineMacro("__ARM_32BIT_STATE", "1");

  // ACLE 6.4.2 Architectural Profile (A, R, M or pre-Cortex)

  // __ARM_ARCH_PROFILE is defined as 'A', 'R', 'M' or 'S', or unset.
  if (!CPUProfile.empty())
    Builder.defineMacro("__ARM_ARCH_PROFILE", "'" + CPUProfile + "'");

  // ACLE 6.4.3 Unaligned access supported in hardware
  if (HasUnalignedAccess)
    Builder.defineMacro("__ARM_FEATURE_UNALIGNED", "1");

  // ACLE 6.4.4 LDREX/STREX
  if (LDREX)
    Builder.defineMacro("__ARM_FEATURE_LDREX", "0x" + Twine::utohexstr(LDREX));

  // ACLE 6.4.5 CLZ
  if (ArchVersion == 5 || (ArchVersion == 6 && CPUProfile != "M") ||
      ArchVersion > 6)
    Builder.defineMacro("__ARM_FEATURE_CLZ", "1");

  // ACLE 6.5.1 Hardware Floating Point
  if (HW_FP)
    Builder.defineMacro("__ARM_FP", "0x" + Twine::utohexstr(HW_FP));

  // ACLE predefines.
  Builder.defineMacro("__ARM_ACLE", "200");

  // FP16 support (we currently only support IEEE format).
  Builder.defineMacro("__ARM_FP16_FORMAT_IEEE", "1");
  Builder.defineMacro("__ARM_FP16_ARGS", "1");

  // ACLE 6.5.3 Fused multiply-accumulate (FMA)
  if (ArchVersion >= 7 && (FPU & VFP4FPU))
    Builder.defineMacro("__ARM_FEATURE_FMA", "1");

  // Subtarget options.

  // FIXME: It's more complicated than this and we don't really support
  // interworking.
  // Windows on ARM does not "support" interworking
  if (5 <= ArchVersion && ArchVersion <= 8 && !getTriple().isOSWindows())
    Builder.defineMacro("__THUMB_INTERWORK__");

  if (ABI == "aapcs" || ABI == "aapcs-linux" || ABI == "aapcs-vfp") {
    // Embedded targets on Darwin follow AAPCS, but not EABI.
    // Windows on ARM follows AAPCS VFP, but does not conform to EABI.
    if (!getTriple().isOSBinFormatMachO() && !getTriple().isOSWindows())
      Builder.defineMacro("__ARM_EABI__");
    Builder.defineMacro("__ARM_PCS", "1");
  }

  if ((!SoftFloat && !SoftFloatABI) || ABI == "aapcs-vfp" || ABI == "aapcs16")
    Builder.defineMacro("__ARM_PCS_VFP", "1");

  if (SoftFloat || (SoftFloatABI && !FPU))
    Builder.defineMacro("__SOFTFP__");

  // ACLE position independent code macros.
  if (Opts.ROPI)
    Builder.defineMacro("__ARM_ROPI", "1");
  if (Opts.RWPI)
    Builder.defineMacro("__ARM_RWPI", "1");

  // Macros for enabling co-proc intrinsics
  uint64_t FeatureCoprocBF = 0;
  switch (ArchKind) {
  default:
    break;
  case llvm::ARM::ArchKind::ARMV4:
  case llvm::ARM::ArchKind::ARMV4T:
    // Filter __arm_ldcl and __arm_stcl in acle.h
    FeatureCoprocBF = isThumb() ? 0 : FEATURE_COPROC_B1;
    break;
  case llvm::ARM::ArchKind::ARMV5T:
    FeatureCoprocBF = isThumb() ? 0 : FEATURE_COPROC_B1 | FEATURE_COPROC_B2;
    break;
  case llvm::ARM::ArchKind::ARMV5TE:
  case llvm::ARM::ArchKind::ARMV5TEJ:
    if (!isThumb())
      FeatureCoprocBF =
          FEATURE_COPROC_B1 | FEATURE_COPROC_B2 | FEATURE_COPROC_B3;
    break;
  case llvm::ARM::ArchKind::ARMV6:
  case llvm::ARM::ArchKind::ARMV6K:
  case llvm::ARM::ArchKind::ARMV6KZ:
  case llvm::ARM::ArchKind::ARMV6T2:
    if (!isThumb() || ArchKind == llvm::ARM::ArchKind::ARMV6T2)
      FeatureCoprocBF = FEATURE_COPROC_B1 | FEATURE_COPROC_B2 |
                        FEATURE_COPROC_B3 | FEATURE_COPROC_B4;
    break;
  case llvm::ARM::ArchKind::ARMV7A:
  case llvm::ARM::ArchKind::ARMV7R:
  case llvm::ARM::ArchKind::ARMV7M:
  case llvm::ARM::ArchKind::ARMV7S:
  case llvm::ARM::ArchKind::ARMV7EM:
    FeatureCoprocBF = FEATURE_COPROC_B1 | FEATURE_COPROC_B2 |
                      FEATURE_COPROC_B3 | FEATURE_COPROC_B4;
    break;
  case llvm::ARM::ArchKind::ARMV8A:
  case llvm::ARM::ArchKind::ARMV8R:
  case llvm::ARM::ArchKind::ARMV8_1A:
  case llvm::ARM::ArchKind::ARMV8_2A:
  case llvm::ARM::ArchKind::ARMV8_3A:
  case llvm::ARM::ArchKind::ARMV8_4A:
  case llvm::ARM::ArchKind::ARMV8_5A:
  case llvm::ARM::ArchKind::ARMV8_6A:
  case llvm::ARM::ArchKind::ARMV8_7A:
  case llvm::ARM::ArchKind::ARMV8_8A:
  case llvm::ARM::ArchKind::ARMV8_9A:
  case llvm::ARM::ArchKind::ARMV9A:
  case llvm::ARM::ArchKind::ARMV9_1A:
  case llvm::ARM::ArchKind::ARMV9_2A:
  case llvm::ARM::ArchKind::ARMV9_3A:
  case llvm::ARM::ArchKind::ARMV9_4A:
  case llvm::ARM::ArchKind::ARMV9_5A:
    // Filter __arm_cdp, __arm_ldcl, __arm_stcl in arm_acle.h
    FeatureCoprocBF = FEATURE_COPROC_B1 | FEATURE_COPROC_B3;
    break;
  case llvm::ARM::ArchKind::ARMV8MMainline:
  case llvm::ARM::ArchKind::ARMV8_1MMainline:
    FeatureCoprocBF = FEATURE_COPROC_B1 | FEATURE_COPROC_B2 |
                      FEATURE_COPROC_B3 | FEATURE_COPROC_B4;
    break;
  }
  Builder.defineMacro("__ARM_FEATURE_COPROC",
                      "0x" + Twine::utohexstr(FeatureCoprocBF));

  if (ArchKind == llvm::ARM::ArchKind::XSCALE)
    Builder.defineMacro("__XSCALE__");

  if (isThumb()) {
    Builder.defineMacro("__THUMBEL__");
    Builder.defineMacro("__thumb__");
    if (supportsThumb2())
      Builder.defineMacro("__thumb2__");
  }

  // ACLE 6.4.9 32-bit SIMD instructions
  if ((CPUProfile != "M" && ArchVersion >= 6) || (CPUProfile == "M" && DSP))
    Builder.defineMacro("__ARM_FEATURE_SIMD32", "1");

  // ACLE 6.4.10 Hardware Integer Divide
  if (((HWDiv & HWDivThumb) && isThumb()) ||
      ((HWDiv & HWDivARM) && !isThumb())) {
    Builder.defineMacro("__ARM_FEATURE_IDIV", "1");
    Builder.defineMacro("__ARM_ARCH_EXT_IDIV__", "1");
  }

  // Note, this is always on in gcc, even though it doesn't make sense.
  Builder.defineMacro("__APCS_32__");

  // __VFP_FP__ means that the floating-point format is VFP, not that a hardware
  // FPU is present. Moreover, the VFP format is the only one supported by
  // clang. For these reasons, this macro is always defined.
  Builder.defineMacro("__VFP_FP__");

  if (FPUModeIsVFP((FPUMode)FPU)) {
    if (FPU & VFP2FPU)
      Builder.defineMacro("__ARM_VFPV2__");
    if (FPU & VFP3FPU)
      Builder.defineMacro("__ARM_VFPV3__");
    if (FPU & VFP4FPU)
      Builder.defineMacro("__ARM_VFPV4__");
    if (FPU & FPARMV8)
      Builder.defineMacro("__ARM_FPV5__");
  }

  // This only gets set when Neon instructions are actually available, unlike
  // the VFP define, hence the soft float and arch check. This is subtly
  // different from gcc, we follow the intent which was that it should be set
  // when Neon instructions are actually available.
  if ((FPU & NeonFPU) && !SoftFloat && ArchVersion >= 7) {
    Builder.defineMacro("__ARM_NEON", "1");
    Builder.defineMacro("__ARM_NEON__");
    // current AArch32 NEON implementations do not support double-precision
    // floating-point even when it is present in VFP.
    Builder.defineMacro("__ARM_NEON_FP",
                        "0x" + Twine::utohexstr(HW_FP & ~HW_FP_DP));
  }

  if (hasMVE()) {
    Builder.defineMacro("__ARM_FEATURE_MVE", hasMVEFloat() ? "3" : "1");
  }

  if (hasCDE()) {
    Builder.defineMacro("__ARM_FEATURE_CDE", "1");
    Builder.defineMacro("__ARM_FEATURE_CDE_COPROC",
                        "0x" + Twine::utohexstr(getARMCDECoprocMask()));
  }

  Builder.defineMacro("__ARM_SIZEOF_WCHAR_T",
                      Twine(Opts.WCharSize ? Opts.WCharSize : 4));

  Builder.defineMacro("__ARM_SIZEOF_MINIMAL_ENUM", Opts.ShortEnums ? "1" : "4");

  // CMSE
  if (ArchVersion == 8 && ArchProfile == llvm::ARM::ProfileKind::M)
    Builder.defineMacro("__ARM_FEATURE_CMSE", Opts.Cmse ? "3" : "1");

  if (ArchVersion >= 6 && CPUAttr != "6M" && CPUAttr != "8M_BASE") {
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
  }

  // ACLE 6.4.7 DSP instructions
  if (DSP) {
    Builder.defineMacro("__ARM_FEATURE_DSP", "1");
  }

  // ACLE 6.4.8 Saturation instructions
  bool SAT = false;
  if ((ArchVersion == 6 && CPUProfile != "M") || ArchVersion > 6) {
    Builder.defineMacro("__ARM_FEATURE_SAT", "1");
    SAT = true;
  }

  // ACLE 6.4.6 Q (saturation) flag
  if (DSP || SAT)
    Builder.defineMacro("__ARM_FEATURE_QBIT", "1");

  if (Opts.UnsafeFPMath)
    Builder.defineMacro("__ARM_FP_FAST", "1");

  // Armv8.2-A FP16 vector intrinsic
  if ((FPU & NeonFPU) && HasLegalHalfType)
    Builder.defineMacro("__ARM_FEATURE_FP16_VECTOR_ARITHMETIC", "1");

  // Armv8.2-A FP16 scalar intrinsics
  if (HasLegalHalfType)
    Builder.defineMacro("__ARM_FEATURE_FP16_SCALAR_ARITHMETIC", "1");

  // Armv8.2-A dot product intrinsics
  if (DotProd)
    Builder.defineMacro("__ARM_FEATURE_DOTPROD", "1");

  if (HasMatMul)
    Builder.defineMacro("__ARM_FEATURE_MATMUL_INT8", "1");

  if (HasPAC)
    Builder.defineMacro("__ARM_FEATURE_PAUTH", "1");

  if (HasBTI)
    Builder.defineMacro("__ARM_FEATURE_BTI", "1");

  if (HasBFloat16) {
    Builder.defineMacro("__ARM_FEATURE_BF16", "1");
    Builder.defineMacro("__ARM_FEATURE_BF16_VECTOR_ARITHMETIC", "1");
    Builder.defineMacro("__ARM_BF16_FORMAT_ALTERNATIVE", "1");
  }

  if (Opts.BranchTargetEnforcement)
    Builder.defineMacro("__ARM_FEATURE_BTI_DEFAULT", "1");

  if (Opts.hasSignReturnAddress()) {
    unsigned Value = 1;
    if (Opts.isSignReturnAddressScopeAll())
      Value |= 1 << 2;
    Builder.defineMacro("__ARM_FEATURE_PAC_DEFAULT", Twine(Value));
  }

  switch (ArchKind) {
  default:
    break;
  case llvm::ARM::ArchKind::ARMV8_1A:
    getTargetDefinesARMV81A(Opts, Builder);
    break;
  case llvm::ARM::ArchKind::ARMV8_2A:
    getTargetDefinesARMV82A(Opts, Builder);
    break;
  case llvm::ARM::ArchKind::ARMV8_3A:
  case llvm::ARM::ArchKind::ARMV8_4A:
  case llvm::ARM::ArchKind::ARMV8_5A:
  case llvm::ARM::ArchKind::ARMV8_6A:
  case llvm::ARM::ArchKind::ARMV8_7A:
  case llvm::ARM::ArchKind::ARMV8_8A:
  case llvm::ARM::ArchKind::ARMV8_9A:
  case llvm::ARM::ArchKind::ARMV9A:
  case llvm::ARM::ArchKind::ARMV9_1A:
  case llvm::ARM::ArchKind::ARMV9_2A:
  case llvm::ARM::ArchKind::ARMV9_3A:
  case llvm::ARM::ArchKind::ARMV9_4A:
  case llvm::ARM::ArchKind::ARMV9_5A:
    getTargetDefinesARMV83A(Opts, Builder);
    break;
  }
}

static constexpr Builtin::Info BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define LIBBUILTIN(ID, TYPE, ATTRS, HEADER)                                    \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#include "clang/Basic/BuiltinsNEON.def"

#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define LANGBUILTIN(ID, TYPE, ATTRS, LANG)                                     \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, LANG},
#define LIBBUILTIN(ID, TYPE, ATTRS, HEADER)                                    \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_HEADER_BUILTIN(ID, TYPE, ATTRS, HEADER, LANGS, FEATURE)         \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::HEADER, LANGS},
#include "clang/Basic/BuiltinsARM.def"
};

ArrayRef<Builtin::Info> ARMTargetInfo::getTargetBuiltins() const {
  return llvm::ArrayRef(BuiltinInfo,
                        clang::ARM::LastTSBuiltin - Builtin::FirstTSBuiltin);
}

bool ARMTargetInfo::isCLZForZeroUndef() const { return false; }
TargetInfo::BuiltinVaListKind ARMTargetInfo::getBuiltinVaListKind() const {
  return IsAAPCS
             ? AAPCSABIBuiltinVaList
             : (getTriple().isWatchABI() ? TargetInfo::CharPtrBuiltinVaList
                                         : TargetInfo::VoidPtrBuiltinVaList);
}

const char *const ARMTargetInfo::GCCRegNames[] = {
    // Integer registers
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11",
    "r12", "sp", "lr", "pc",

    // Float registers
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
    "s12", "s13", "s14", "s15", "s16", "s17", "s18", "s19", "s20", "s21", "s22",
    "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31",

    // Double registers
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11",
    "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22",
    "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31",

    // Quad registers
    "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11",
    "q12", "q13", "q14", "q15"};

ArrayRef<const char *> ARMTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

const TargetInfo::GCCRegAlias ARMTargetInfo::GCCRegAliases[] = {
    {{"a1"}, "r0"},  {{"a2"}, "r1"},        {{"a3"}, "r2"},  {{"a4"}, "r3"},
    {{"v1"}, "r4"},  {{"v2"}, "r5"},        {{"v3"}, "r6"},  {{"v4"}, "r7"},
    {{"v5"}, "r8"},  {{"v6", "rfp"}, "r9"}, {{"sl"}, "r10"}, {{"fp"}, "r11"},
    {{"ip"}, "r12"}, {{"r13"}, "sp"},       {{"r14"}, "lr"}, {{"r15"}, "pc"},
    // The S, D and Q registers overlap, but aren't really aliases; we
    // don't want to substitute one of these for a different-sized one.
};

ArrayRef<TargetInfo::GCCRegAlias> ARMTargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

bool ARMTargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    break;
  case 'l': // r0-r7 if thumb, r0-r15 if ARM
    Info.setAllowsRegister();
    return true;
  case 'h': // r8-r15, thumb only
    if (isThumb()) {
      Info.setAllowsRegister();
      return true;
    }
    break;
  case 's': // An integer constant, but allowing only relocatable values.
    return true;
  case 't': // s0-s31, d0-d31, or q0-q15
  case 'w': // s0-s15, d0-d7, or q0-q3
  case 'x': // s0-s31, d0-d15, or q0-q7
    if (FPRegsDisabled)
      return false;
    Info.setAllowsRegister();
    return true;
  case 'j': // An immediate integer between 0 and 65535 (valid for MOVW)
    // only available in ARMv6T2 and above
    if (CPUAttr == "6T2" || ArchVersion >= 7) {
      Info.setRequiresImmediate(0, 65535);
      return true;
    }
    break;
  case 'I':
    if (isThumb()) {
      if (!supportsThumb2())
        Info.setRequiresImmediate(0, 255);
      else
        // FIXME: should check if immediate value would be valid for a Thumb2
        // data-processing instruction
        Info.setRequiresImmediate();
    } else
      // FIXME: should check if immediate value would be valid for an ARM
      // data-processing instruction
      Info.setRequiresImmediate();
    return true;
  case 'J':
    if (isThumb() && !supportsThumb2())
      Info.setRequiresImmediate(-255, -1);
    else
      Info.setRequiresImmediate(-4095, 4095);
    return true;
  case 'K':
    if (isThumb()) {
      if (!supportsThumb2())
        // FIXME: should check if immediate value can be obtained from shifting
        // a value between 0 and 255 left by any amount
        Info.setRequiresImmediate();
      else
        // FIXME: should check if immediate value would be valid for a Thumb2
        // data-processing instruction when inverted
        Info.setRequiresImmediate();
    } else
      // FIXME: should check if immediate value would be valid for an ARM
      // data-processing instruction when inverted
      Info.setRequiresImmediate();
    return true;
  case 'L':
    if (isThumb()) {
      if (!supportsThumb2())
        Info.setRequiresImmediate(-7, 7);
      else
        // FIXME: should check if immediate value would be valid for a Thumb2
        // data-processing instruction when negated
        Info.setRequiresImmediate();
    } else
      // FIXME: should check if immediate value  would be valid for an ARM
      // data-processing instruction when negated
      Info.setRequiresImmediate();
    return true;
  case 'M':
    if (isThumb() && !supportsThumb2())
      // FIXME: should check if immediate value is a multiple of 4 between 0 and
      // 1020
      Info.setRequiresImmediate();
    else
      // FIXME: should check if immediate value is a power of two or a integer
      // between 0 and 32
      Info.setRequiresImmediate();
    return true;
  case 'N':
    // Thumb1 only
    if (isThumb() && !supportsThumb2()) {
      Info.setRequiresImmediate(0, 31);
      return true;
    }
    break;
  case 'O':
    // Thumb1 only
    if (isThumb() && !supportsThumb2()) {
      // FIXME: should check if immediate value is a multiple of 4 between -508
      // and 508
      Info.setRequiresImmediate();
      return true;
    }
    break;
  case 'Q': // A memory address that is a single base register.
    Info.setAllowsMemory();
    return true;
  case 'T':
    switch (Name[1]) {
    default:
      break;
    case 'e': // Even general-purpose register
    case 'o': // Odd general-purpose register
      Info.setAllowsRegister();
      Name++;
      return true;
    }
    break;
  case 'U': // a memory reference...
    switch (Name[1]) {
    case 'q': // ...ARMV4 ldrsb
    case 'v': // ...VFP load/store (reg+constant offset)
    case 'y': // ...iWMMXt load/store
    case 't': // address valid for load/store opaque types wider
              // than 128-bits
    case 'n': // valid address for Neon doubleword vector load/store
    case 'm': // valid address for Neon element and structure load/store
    case 's': // valid address for non-offset loads/stores of quad-word
              // values in four ARM registers
      Info.setAllowsMemory();
      Name++;
      return true;
    }
    break;
  }
  return false;
}

std::string ARMTargetInfo::convertConstraint(const char *&Constraint) const {
  std::string R;
  switch (*Constraint) {
  case 'U': // Two-character constraint; add "^" hint for later parsing.
  case 'T':
    R = std::string("^") + std::string(Constraint, 2);
    Constraint++;
    break;
  case 'p': // 'p' should be translated to 'r' by default.
    R = std::string("r");
    break;
  default:
    return std::string(1, *Constraint);
  }
  return R;
}

bool ARMTargetInfo::validateConstraintModifier(
    StringRef Constraint, char Modifier, unsigned Size,
    std::string &SuggestedModifier) const {
  bool isOutput = (Constraint[0] == '=');
  bool isInOut = (Constraint[0] == '+');

  // Strip off constraint modifiers.
  Constraint = Constraint.ltrim("=+&");

  switch (Constraint[0]) {
  default:
    break;
  case 'r': {
    switch (Modifier) {
    default:
      return (isInOut || isOutput || Size <= 64);
    case 'q':
      // A register of size 32 cannot fit a vector type.
      return false;
    }
  }
  }

  return true;
}
std::string_view ARMTargetInfo::getClobbers() const {
  // FIXME: Is this really right?
  return "";
}

TargetInfo::CallingConvCheckResult
ARMTargetInfo::checkCallingConvention(CallingConv CC) const {
  switch (CC) {
  case CC_AAPCS:
  case CC_AAPCS_VFP:
  case CC_Swift:
  case CC_SwiftAsync:
  case CC_OpenCLKernel:
    return CCCR_OK;
  default:
    return CCCR_Warning;
  }
}

int ARMTargetInfo::getEHDataRegisterNumber(unsigned RegNo) const {
  if (RegNo == 0)
    return 0;
  if (RegNo == 1)
    return 1;
  return -1;
}

bool ARMTargetInfo::hasSjLjLowering() const { return true; }

ARMleTargetInfo::ARMleTargetInfo(const llvm::Triple &Triple,
                                 const TargetOptions &Opts)
    : ARMTargetInfo(Triple, Opts) {}

void ARMleTargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  Builder.defineMacro("__ARMEL__");
  ARMTargetInfo::getTargetDefines(Opts, Builder);
}

ARMbeTargetInfo::ARMbeTargetInfo(const llvm::Triple &Triple,
                                 const TargetOptions &Opts)
    : ARMTargetInfo(Triple, Opts) {}

void ARMbeTargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  Builder.defineMacro("__ARMEB__");
  Builder.defineMacro("__ARM_BIG_ENDIAN");
  ARMTargetInfo::getTargetDefines(Opts, Builder);
}

WindowsARMTargetInfo::WindowsARMTargetInfo(const llvm::Triple &Triple,
                                           const TargetOptions &Opts)
    : WindowsTargetInfo<ARMleTargetInfo>(Triple, Opts), Triple(Triple) {
}

void WindowsARMTargetInfo::getVisualStudioDefines(const LangOptions &Opts,
                                                  MacroBuilder &Builder) const {
  // FIXME: this is invalid for WindowsCE
  Builder.defineMacro("_M_ARM_NT", "1");
  Builder.defineMacro("_M_ARMT", "_M_ARM");
  Builder.defineMacro("_M_THUMB", "_M_ARM");

  assert((Triple.getArch() == llvm::Triple::arm ||
          Triple.getArch() == llvm::Triple::thumb) &&
         "invalid architecture for Windows ARM target info");
  unsigned Offset = Triple.getArch() == llvm::Triple::arm ? 4 : 6;
  Builder.defineMacro("_M_ARM", Triple.getArchName().substr(Offset));

  // TODO map the complete set of values
  // 31: VFPv3 40: VFPv4
  Builder.defineMacro("_M_ARM_FP", "31");
}

TargetInfo::BuiltinVaListKind
WindowsARMTargetInfo::getBuiltinVaListKind() const {
  return TargetInfo::CharPtrBuiltinVaList;
}

TargetInfo::CallingConvCheckResult
WindowsARMTargetInfo::checkCallingConvention(CallingConv CC) const {
  switch (CC) {
  case CC_X86StdCall:
  case CC_X86ThisCall:
  case CC_X86FastCall:
  case CC_X86VectorCall:
    return CCCR_Ignore;
  case CC_C:
  case CC_OpenCLKernel:
  case CC_PreserveMost:
  case CC_PreserveAll:
  case CC_Swift:
  case CC_SwiftAsync:
    return CCCR_OK;
  default:
    return CCCR_Warning;
  }
}

// Windows ARM + Itanium C++ ABI Target
ItaniumWindowsARMleTargetInfo::ItaniumWindowsARMleTargetInfo(
    const llvm::Triple &Triple, const TargetOptions &Opts)
    : WindowsARMTargetInfo(Triple, Opts) {
  TheCXXABI.set(TargetCXXABI::GenericARM);
}

void ItaniumWindowsARMleTargetInfo::getTargetDefines(
    const LangOptions &Opts, MacroBuilder &Builder) const {
  WindowsARMTargetInfo::getTargetDefines(Opts, Builder);

  if (Opts.MSVCCompat)
    WindowsARMTargetInfo::getVisualStudioDefines(Opts, Builder);
}

// Windows ARM, MS (C++) ABI
MicrosoftARMleTargetInfo::MicrosoftARMleTargetInfo(const llvm::Triple &Triple,
                                                   const TargetOptions &Opts)
    : WindowsARMTargetInfo(Triple, Opts) {
  TheCXXABI.set(TargetCXXABI::Microsoft);
}

void MicrosoftARMleTargetInfo::getTargetDefines(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  WindowsARMTargetInfo::getTargetDefines(Opts, Builder);
  WindowsARMTargetInfo::getVisualStudioDefines(Opts, Builder);
}

MinGWARMTargetInfo::MinGWARMTargetInfo(const llvm::Triple &Triple,
                                       const TargetOptions &Opts)
    : WindowsARMTargetInfo(Triple, Opts) {
  TheCXXABI.set(TargetCXXABI::GenericARM);
}

void MinGWARMTargetInfo::getTargetDefines(const LangOptions &Opts,
                                          MacroBuilder &Builder) const {
  WindowsARMTargetInfo::getTargetDefines(Opts, Builder);
  Builder.defineMacro("_ARM_");
}

CygwinARMTargetInfo::CygwinARMTargetInfo(const llvm::Triple &Triple,
                                         const TargetOptions &Opts)
    : ARMleTargetInfo(Triple, Opts) {
  this->WCharType = TargetInfo::UnsignedShort;
  TLSSupported = false;
  DoubleAlign = LongLongAlign = 64;
  resetDataLayout("e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64");
}

void CygwinARMTargetInfo::getTargetDefines(const LangOptions &Opts,
                                           MacroBuilder &Builder) const {
  ARMleTargetInfo::getTargetDefines(Opts, Builder);
  Builder.defineMacro("_ARM_");
  Builder.defineMacro("__CYGWIN__");
  Builder.defineMacro("__CYGWIN32__");
  DefineStd(Builder, "unix", Opts);
  if (Opts.CPlusPlus)
    Builder.defineMacro("_GNU_SOURCE");
}

DarwinARMTargetInfo::DarwinARMTargetInfo(const llvm::Triple &Triple,
                                         const TargetOptions &Opts)
    : DarwinTargetInfo<ARMleTargetInfo>(Triple, Opts) {
  HasAlignMac68kSupport = true;
  if (Triple.isWatchABI()) {
    // Darwin on iOS uses a variant of the ARM C++ ABI.
    TheCXXABI.set(TargetCXXABI::WatchOS);

    // BOOL should be a real boolean on the new ABI
    UseSignedCharForObjCBool = false;
  } else
    TheCXXABI.set(TargetCXXABI::iOS);
}

void DarwinARMTargetInfo::getOSDefines(const LangOptions &Opts,
                                       const llvm::Triple &Triple,
                                       MacroBuilder &Builder) const {
  getDarwinDefines(Builder, Opts, Triple, PlatformName, PlatformMinVersion);
}

RenderScript32TargetInfo::RenderScript32TargetInfo(const llvm::Triple &Triple,
                                                   const TargetOptions &Opts)
    : ARMleTargetInfo(llvm::Triple("armv7", Triple.getVendorName(),
                                   Triple.getOSName(),
                                   Triple.getEnvironmentName()),
                      Opts) {
  IsRenderScriptTarget = true;
  LongWidth = LongAlign = 64;
}

void RenderScript32TargetInfo::getTargetDefines(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  Builder.defineMacro("__RENDERSCRIPT__");
  ARMleTargetInfo::getTargetDefines(Opts, Builder);
}
