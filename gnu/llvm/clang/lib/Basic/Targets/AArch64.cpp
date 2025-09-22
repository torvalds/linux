//===--- AArch64.cpp - Implement AArch64 target feature support -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements AArch64 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/ARMTargetParserCommon.h"
#include <optional>

using namespace clang;
using namespace clang::targets;

static constexpr Builtin::Info BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#include "clang/Basic/BuiltinsNEON.def"

#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#include "clang/Basic/BuiltinsSVE.def"

#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#include "clang/Basic/BuiltinsSME.def"

#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define LANGBUILTIN(ID, TYPE, ATTRS, LANG)                                     \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, LANG},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_HEADER_BUILTIN(ID, TYPE, ATTRS, HEADER, LANGS, FEATURE)         \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::HEADER, LANGS},
#include "clang/Basic/BuiltinsAArch64.def"
};

void AArch64TargetInfo::setArchFeatures() {
  if (*ArchInfo == llvm::AArch64::ARMV8R) {
    HasDotProd = true;
    HasDIT = true;
    HasFlagM = true;
    HasRCPC = true;
    FPU |= NeonMode;
    HasCCPP = true;
    HasCRC = true;
    HasLSE = true;
    HasRDM = true;
  } else if (ArchInfo->Version.getMajor() == 8) {
    if (ArchInfo->Version.getMinor() >= 7u) {
      HasWFxT = true;
    }
    if (ArchInfo->Version.getMinor() >= 6u) {
      HasBFloat16 = true;
      HasMatMul = true;
    }
    if (ArchInfo->Version.getMinor() >= 5u) {
      HasAlternativeNZCV = true;
      HasFRInt3264 = true;
      HasSSBS = true;
      HasSB = true;
      HasPredRes = true;
      HasBTI = true;
    }
    if (ArchInfo->Version.getMinor() >= 4u) {
      HasDotProd = true;
      HasDIT = true;
      HasFlagM = true;
    }
    if (ArchInfo->Version.getMinor() >= 3u) {
      HasRCPC = true;
      FPU |= NeonMode;
    }
    if (ArchInfo->Version.getMinor() >= 2u) {
      HasCCPP = true;
    }
    if (ArchInfo->Version.getMinor() >= 1u) {
      HasCRC = true;
      HasLSE = true;
      HasRDM = true;
    }
  } else if (ArchInfo->Version.getMajor() == 9) {
    if (ArchInfo->Version.getMinor() >= 2u) {
      HasWFxT = true;
    }
    if (ArchInfo->Version.getMinor() >= 1u) {
      HasBFloat16 = true;
      HasMatMul = true;
    }
    FPU |= SveMode;
    HasSVE2 = true;
    HasFullFP16 = true;
    HasAlternativeNZCV = true;
    HasFRInt3264 = true;
    HasSSBS = true;
    HasSB = true;
    HasPredRes = true;
    HasBTI = true;
    HasDotProd = true;
    HasDIT = true;
    HasFlagM = true;
    HasRCPC = true;
    FPU |= NeonMode;
    HasCCPP = true;
    HasCRC = true;
    HasLSE = true;
    HasRDM = true;
  }
}

AArch64TargetInfo::AArch64TargetInfo(const llvm::Triple &Triple,
                                     const TargetOptions &Opts)
    : TargetInfo(Triple), ABI("aapcs") {
  if (getTriple().isOSOpenBSD()) {
    Int64Type = SignedLongLong;
    IntMaxType = SignedLongLong;
  } else {
    if (!getTriple().isOSDarwin() && !getTriple().isOSNetBSD())
      WCharType = UnsignedInt;

    Int64Type = SignedLong;
    IntMaxType = SignedLong;
  }

  // All AArch64 implementations support ARMv8 FP, which makes half a legal type.
  HasLegalHalfType = true;
  HalfArgsAndReturns = true;
  HasFloat16 = true;
  HasStrictFP = true;

  if (Triple.isArch64Bit())
    LongWidth = LongAlign = PointerWidth = PointerAlign = 64;
  else
    LongWidth = LongAlign = PointerWidth = PointerAlign = 32;

  BitIntMaxAlign = 128;
  MaxVectorAlign = 128;
  MaxAtomicInlineWidth = 128;
  MaxAtomicPromoteWidth = 128;

  LongDoubleWidth = LongDoubleAlign = SuitableAlign = 128;
  LongDoubleFormat = &llvm::APFloat::IEEEquad();

  BFloat16Width = BFloat16Align = 16;
  BFloat16Format = &llvm::APFloat::BFloat();

  // Make __builtin_ms_va_list available.
  HasBuiltinMSVaList = true;

  // Make the SVE types available.  Note that this deliberately doesn't
  // depend on SveMode, since in principle it should be possible to turn
  // SVE on and off within a translation unit.  It should also be possible
  // to compile the global declaration:
  //
  // __SVInt8_t *ptr;
  //
  // even without SVE.
  HasAArch64SVETypes = true;

  // {} in inline assembly are neon specifiers, not assembly variant
  // specifiers.
  NoAsmVariants = true;

  // AAPCS gives rules for bitfields. 7.1.7 says: "The container type
  // contributes to the alignment of the containing aggregate in the same way
  // a plain (non bit-field) member of that type would, without exception for
  // zero-sized or anonymous bit-fields."
  assert(UseBitFieldTypeAlignment && "bitfields affect type alignment");
  UseZeroLengthBitfieldAlignment = true;

  HasUnalignedAccess = true;

  // AArch64 targets default to using the ARM C++ ABI.
  TheCXXABI.set(TargetCXXABI::GenericAArch64);

  if (Triple.getOS() == llvm::Triple::Linux)
    this->MCountName = "\01_mcount";
  else if (Triple.getOS() == llvm::Triple::UnknownOS)
    this->MCountName =
        Opts.EABIVersion == llvm::EABI::GNU ? "\01_mcount" : "mcount";
}

StringRef AArch64TargetInfo::getABI() const { return ABI; }

bool AArch64TargetInfo::setABI(const std::string &Name) {
  if (Name != "aapcs" && Name != "aapcs-soft" && Name != "darwinpcs" &&
      Name != "pauthtest")
    return false;

  ABI = Name;
  return true;
}

bool AArch64TargetInfo::validateTarget(DiagnosticsEngine &Diags) const {
  if (hasFeature("fp") && ABI == "aapcs-soft") {
    // aapcs-soft is not allowed for targets with an FPU, to avoid there being
    // two incomatible ABIs.
    Diags.Report(diag::err_target_unsupported_abi_with_fpu) << ABI;
    return false;
  }
  if (getTriple().getEnvironment() == llvm::Triple::PAuthTest &&
      getTriple().getOS() != llvm::Triple::Linux) {
    Diags.Report(diag::err_target_unsupported_abi_for_triple)
        << getTriple().getEnvironmentName() << getTriple().getTriple();
    return false;
  }
  return true;
}

bool AArch64TargetInfo::validateGlobalRegisterVariable(
    StringRef RegName, unsigned RegSize, bool &HasSizeMismatch) const {
  if ((RegName == "sp") || RegName.starts_with("x")) {
    HasSizeMismatch = RegSize != 64;
    return true;
  } else if (RegName.starts_with("w")) {
    HasSizeMismatch = RegSize != 32;
    return true;
  }
  return false;
}

bool AArch64TargetInfo::validateBranchProtection(StringRef Spec, StringRef,
                                                 BranchProtectionInfo &BPI,
                                                 StringRef &Err) const {
  llvm::ARM::ParsedBranchProtection PBP;
  if (!llvm::ARM::parseBranchProtection(Spec, PBP, Err, HasPAuthLR))
    return false;

  BPI.SignReturnAddr =
      llvm::StringSwitch<LangOptions::SignReturnAddressScopeKind>(PBP.Scope)
          .Case("non-leaf", LangOptions::SignReturnAddressScopeKind::NonLeaf)
          .Case("all", LangOptions::SignReturnAddressScopeKind::All)
          .Default(LangOptions::SignReturnAddressScopeKind::None);

  if (PBP.Key == "a_key")
    BPI.SignKey = LangOptions::SignReturnAddressKeyKind::AKey;
  else
    BPI.SignKey = LangOptions::SignReturnAddressKeyKind::BKey;

  BPI.BranchTargetEnforcement = PBP.BranchTargetEnforcement;
  BPI.BranchProtectionPAuthLR = PBP.BranchProtectionPAuthLR;
  BPI.GuardedControlStack = PBP.GuardedControlStack;
  return true;
}

bool AArch64TargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::AArch64::parseCpu(Name).has_value();
}

bool AArch64TargetInfo::setCPU(const std::string &Name) {
  return isValidCPUName(Name);
}

void AArch64TargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  llvm::AArch64::fillValidCPUArchList(Values);
}

void AArch64TargetInfo::getTargetDefinesARMV81A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  Builder.defineMacro("__ARM_FEATURE_QRDMX", "1");
}

void AArch64TargetInfo::getTargetDefinesARMV82A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Also include the ARMv8.1 defines
  getTargetDefinesARMV81A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV83A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  Builder.defineMacro("__ARM_FEATURE_COMPLEX", "1");
  Builder.defineMacro("__ARM_FEATURE_JCVT", "1");
  // Also include the Armv8.2 defines
  getTargetDefinesARMV82A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV84A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Also include the Armv8.3 defines
  getTargetDefinesARMV83A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV85A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  Builder.defineMacro("__ARM_FEATURE_FRINT", "1");
  // Also include the Armv8.4 defines
  getTargetDefinesARMV84A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV86A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Also include the Armv8.5 defines
  // FIXME: Armv8.6 makes the following extensions mandatory:
  // - __ARM_FEATURE_BF16
  // - __ARM_FEATURE_MATMUL_INT8
  // Handle them here.
  getTargetDefinesARMV85A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV87A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Also include the Armv8.6 defines
  getTargetDefinesARMV86A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV88A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Also include the Armv8.7 defines
  getTargetDefinesARMV87A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV89A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Also include the Armv8.8 defines
  getTargetDefinesARMV88A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV9A(const LangOptions &Opts,
                                               MacroBuilder &Builder) const {
  // Armv9-A maps to Armv8.5-A
  getTargetDefinesARMV85A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV91A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Armv9.1-A maps to Armv8.6-A
  getTargetDefinesARMV86A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV92A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Armv9.2-A maps to Armv8.7-A
  getTargetDefinesARMV87A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV93A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Armv9.3-A maps to Armv8.8-A
  getTargetDefinesARMV88A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV94A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Armv9.4-A maps to Armv8.9-A
  getTargetDefinesARMV89A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefinesARMV95A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Armv9.5-A does not have a v8.* equivalent, but is a superset of v9.4-A.
  getTargetDefinesARMV94A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  // Target identification.
  if (getTriple().isWindowsArm64EC()) {
    // Define the same set of macros as would be defined on x86_64 to ensure that
    // ARM64EC datatype layouts match those of x86_64 compiled code
    Builder.defineMacro("__amd64__");
    Builder.defineMacro("__amd64");
    Builder.defineMacro("__x86_64");
    Builder.defineMacro("__x86_64__");
    Builder.defineMacro("__arm64ec__");
  } else {
    Builder.defineMacro("__aarch64__");
  }

  // Inline assembly supports AArch64 flag outputs.
  Builder.defineMacro("__GCC_ASM_FLAG_OUTPUTS__");

  std::string CodeModel = getTargetOpts().CodeModel;
  if (CodeModel == "default")
    CodeModel = "small";
  for (char &c : CodeModel)
    c = toupper(c);
  Builder.defineMacro("__AARCH64_CMODEL_" + CodeModel + "__");

  // ACLE predefines. Many can only have one possible value on v8 AArch64.
  Builder.defineMacro("__ARM_ACLE", "200");
  Builder.defineMacro("__ARM_ARCH",
                      std::to_string(ArchInfo->Version.getMajor()));
  Builder.defineMacro("__ARM_ARCH_PROFILE",
                      std::string("'") + (char)ArchInfo->Profile + "'");

  Builder.defineMacro("__ARM_64BIT_STATE", "1");
  Builder.defineMacro("__ARM_PCS_AAPCS64", "1");
  Builder.defineMacro("__ARM_ARCH_ISA_A64", "1");

  Builder.defineMacro("__ARM_FEATURE_CLZ", "1");
  Builder.defineMacro("__ARM_FEATURE_FMA", "1");
  Builder.defineMacro("__ARM_FEATURE_LDREX", "0xF");
  Builder.defineMacro("__ARM_FEATURE_IDIV", "1"); // As specified in ACLE
  Builder.defineMacro("__ARM_FEATURE_DIV");       // For backwards compatibility
  Builder.defineMacro("__ARM_FEATURE_NUMERIC_MAXMIN", "1");
  Builder.defineMacro("__ARM_FEATURE_DIRECTED_ROUNDING", "1");

  Builder.defineMacro("__ARM_ALIGN_MAX_STACK_PWR", "4");

  // These macros are set when Clang can parse declarations with these
  // attributes.
  Builder.defineMacro("__ARM_STATE_ZA", "1");
  Builder.defineMacro("__ARM_STATE_ZT0", "1");

  // 0xe implies support for half, single and double precision operations.
  if (FPU & FPUMode)
    Builder.defineMacro("__ARM_FP", "0xE");

  // PCS specifies this for SysV variants, which is all we support. Other ABIs
  // may choose __ARM_FP16_FORMAT_ALTERNATIVE.
  Builder.defineMacro("__ARM_FP16_FORMAT_IEEE", "1");
  Builder.defineMacro("__ARM_FP16_ARGS", "1");

  if (Opts.UnsafeFPMath)
    Builder.defineMacro("__ARM_FP_FAST", "1");

  Builder.defineMacro("__ARM_SIZEOF_WCHAR_T",
                      Twine(Opts.WCharSize ? Opts.WCharSize : 4));

  Builder.defineMacro("__ARM_SIZEOF_MINIMAL_ENUM", Opts.ShortEnums ? "1" : "4");

  if (FPU & NeonMode) {
    Builder.defineMacro("__ARM_NEON", "1");
    // 64-bit NEON supports half, single and double precision operations.
    Builder.defineMacro("__ARM_NEON_FP", "0xE");
  }

  if (FPU & SveMode)
    Builder.defineMacro("__ARM_FEATURE_SVE", "1");

  if ((FPU & NeonMode) && (FPU & SveMode))
    Builder.defineMacro("__ARM_NEON_SVE_BRIDGE", "1");

  if (HasSVE2)
    Builder.defineMacro("__ARM_FEATURE_SVE2", "1");

  if (HasSVE2p1)
    Builder.defineMacro("__ARM_FEATURE_SVE2p1", "1");

  if (HasSVE2 && HasSVE2AES)
    Builder.defineMacro("__ARM_FEATURE_SVE2_AES", "1");

  if (HasSVE2 && HasSVE2BitPerm)
    Builder.defineMacro("__ARM_FEATURE_SVE2_BITPERM", "1");

  if (HasSVE2 && HasSVE2SHA3)
    Builder.defineMacro("__ARM_FEATURE_SVE2_SHA3", "1");

  if (HasSVE2 && HasSVE2SM4)
    Builder.defineMacro("__ARM_FEATURE_SVE2_SM4", "1");

  if (HasSVEB16B16)
    Builder.defineMacro("__ARM_FEATURE_SVE_B16B16", "1");

  if (HasSME) {
    Builder.defineMacro("__ARM_FEATURE_SME");
    Builder.defineMacro("__ARM_FEATURE_LOCALLY_STREAMING", "1");
  }

  if (HasSME2)
    Builder.defineMacro("__ARM_FEATURE_SME2", "1");

  if (HasSME2p1)
    Builder.defineMacro("__ARM_FEATURE_SME2p1", "1");

  if (HasSMEF16F16)
    Builder.defineMacro("__ARM_FEATURE_SME_F16F16", "1");

  if (HasSMEB16B16)
    Builder.defineMacro("__ARM_FEATURE_SME_B16B16", "1");

  if (HasCRC)
    Builder.defineMacro("__ARM_FEATURE_CRC32", "1");

  if (HasRCPC3)
    Builder.defineMacro("__ARM_FEATURE_RCPC", "3");
  else if (HasRCPC)
    Builder.defineMacro("__ARM_FEATURE_RCPC", "1");

  if (HasFMV)
    Builder.defineMacro("__HAVE_FUNCTION_MULTI_VERSIONING", "1");

  // The __ARM_FEATURE_CRYPTO is deprecated in favor of finer grained feature
  // macros for AES, SHA2, SHA3 and SM4
  if (HasAES && HasSHA2)
    Builder.defineMacro("__ARM_FEATURE_CRYPTO", "1");

  if (HasAES)
    Builder.defineMacro("__ARM_FEATURE_AES", "1");

  if (HasSHA2)
    Builder.defineMacro("__ARM_FEATURE_SHA2", "1");

  if (HasSHA3) {
    Builder.defineMacro("__ARM_FEATURE_SHA3", "1");
    Builder.defineMacro("__ARM_FEATURE_SHA512", "1");
  }

  if (HasSM4) {
    Builder.defineMacro("__ARM_FEATURE_SM3", "1");
    Builder.defineMacro("__ARM_FEATURE_SM4", "1");
  }

  if (HasPAuth)
    Builder.defineMacro("__ARM_FEATURE_PAUTH", "1");

  if (HasPAuthLR)
    Builder.defineMacro("__ARM_FEATURE_PAUTH_LR", "1");

  if (HasBTI)
    Builder.defineMacro("__ARM_FEATURE_BTI", "1");

  if (HasUnalignedAccess)
    Builder.defineMacro("__ARM_FEATURE_UNALIGNED", "1");

  if ((FPU & NeonMode) && HasFullFP16)
    Builder.defineMacro("__ARM_FEATURE_FP16_VECTOR_ARITHMETIC", "1");
  if (HasFullFP16)
   Builder.defineMacro("__ARM_FEATURE_FP16_SCALAR_ARITHMETIC", "1");

  if (HasDotProd)
    Builder.defineMacro("__ARM_FEATURE_DOTPROD", "1");

  if (HasMTE)
    Builder.defineMacro("__ARM_FEATURE_MEMORY_TAGGING", "1");

  if (HasTME)
    Builder.defineMacro("__ARM_FEATURE_TME", "1");

  if (HasMatMul)
    Builder.defineMacro("__ARM_FEATURE_MATMUL_INT8", "1");

  if (HasLSE)
    Builder.defineMacro("__ARM_FEATURE_ATOMICS", "1");

  if (HasBFloat16) {
    Builder.defineMacro("__ARM_FEATURE_BF16", "1");
    Builder.defineMacro("__ARM_FEATURE_BF16_VECTOR_ARITHMETIC", "1");
    Builder.defineMacro("__ARM_BF16_FORMAT_ALTERNATIVE", "1");
    Builder.defineMacro("__ARM_FEATURE_BF16_SCALAR_ARITHMETIC", "1");
  }

  if ((FPU & SveMode) && HasBFloat16) {
    Builder.defineMacro("__ARM_FEATURE_SVE_BF16", "1");
  }

  if ((FPU & SveMode) && HasMatmulFP64)
    Builder.defineMacro("__ARM_FEATURE_SVE_MATMUL_FP64", "1");

  if ((FPU & SveMode) && HasMatmulFP32)
    Builder.defineMacro("__ARM_FEATURE_SVE_MATMUL_FP32", "1");

  if ((FPU & SveMode) && HasMatMul)
    Builder.defineMacro("__ARM_FEATURE_SVE_MATMUL_INT8", "1");

  if ((FPU & NeonMode) && HasFP16FML)
    Builder.defineMacro("__ARM_FEATURE_FP16_FML", "1");

  if (Opts.hasSignReturnAddress()) {
    // Bitmask:
    // 0: Protection using the A key
    // 1: Protection using the B key
    // 2: Protection including leaf functions
    // 3: Protection using PC as a diversifier
    unsigned Value = 0;

    if (Opts.isSignReturnAddressWithAKey())
      Value |= (1 << 0);
    else
      Value |= (1 << 1);

    if (Opts.isSignReturnAddressScopeAll())
      Value |= (1 << 2);

    if (Opts.BranchProtectionPAuthLR)
      Value |= (1 << 3);

    Builder.defineMacro("__ARM_FEATURE_PAC_DEFAULT", std::to_string(Value));
  }

  if (Opts.BranchTargetEnforcement)
    Builder.defineMacro("__ARM_FEATURE_BTI_DEFAULT", "1");

  if (Opts.GuardedControlStack)
    Builder.defineMacro("__ARM_FEATURE_GCS_DEFAULT", "1");

  if (HasLS64)
    Builder.defineMacro("__ARM_FEATURE_LS64", "1");

  if (HasRandGen)
    Builder.defineMacro("__ARM_FEATURE_RNG", "1");

  if (HasMOPS)
    Builder.defineMacro("__ARM_FEATURE_MOPS", "1");

  if (HasD128)
    Builder.defineMacro("__ARM_FEATURE_SYSREG128", "1");

  if (HasGCS)
    Builder.defineMacro("__ARM_FEATURE_GCS", "1");

  if (*ArchInfo == llvm::AArch64::ARMV8_1A)
    getTargetDefinesARMV81A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV8_2A)
    getTargetDefinesARMV82A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV8_3A)
    getTargetDefinesARMV83A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV8_4A)
    getTargetDefinesARMV84A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV8_5A)
    getTargetDefinesARMV85A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV8_6A)
    getTargetDefinesARMV86A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV8_7A)
    getTargetDefinesARMV87A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV8_8A)
    getTargetDefinesARMV88A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV8_9A)
    getTargetDefinesARMV89A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV9A)
    getTargetDefinesARMV9A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV9_1A)
    getTargetDefinesARMV91A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV9_2A)
    getTargetDefinesARMV92A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV9_3A)
    getTargetDefinesARMV93A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV9_4A)
    getTargetDefinesARMV94A(Opts, Builder);
  else if (*ArchInfo == llvm::AArch64::ARMV9_5A)
    getTargetDefinesARMV95A(Opts, Builder);

  // All of the __sync_(bool|val)_compare_and_swap_(1|2|4|8|16) builtins work.
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16");

  // Allow detection of fast FMA support.
  Builder.defineMacro("__FP_FAST_FMA", "1");
  Builder.defineMacro("__FP_FAST_FMAF", "1");

  // C/C++ operators work on both VLS and VLA SVE types
  if (FPU & SveMode)
    Builder.defineMacro("__ARM_FEATURE_SVE_VECTOR_OPERATORS", "2");

  if (Opts.VScaleMin && Opts.VScaleMin == Opts.VScaleMax) {
    Builder.defineMacro("__ARM_FEATURE_SVE_BITS", Twine(Opts.VScaleMin * 128));
  }
}

ArrayRef<Builtin::Info> AArch64TargetInfo::getTargetBuiltins() const {
  return llvm::ArrayRef(BuiltinInfo, clang::AArch64::LastTSBuiltin -
                                         Builtin::FirstTSBuiltin);
}

std::optional<std::pair<unsigned, unsigned>>
AArch64TargetInfo::getVScaleRange(const LangOptions &LangOpts) const {
  if (LangOpts.VScaleMin || LangOpts.VScaleMax)
    return std::pair<unsigned, unsigned>(
        LangOpts.VScaleMin ? LangOpts.VScaleMin : 1, LangOpts.VScaleMax);

  if (hasFeature("sve"))
    return std::pair<unsigned, unsigned>(1, 16);

  return std::nullopt;
}

unsigned AArch64TargetInfo::multiVersionSortPriority(StringRef Name) const {
  if (Name == "default")
    return 0;
  if (auto Ext = llvm::AArch64::parseFMVExtension(Name))
    return Ext->Priority;
  return 0;
}

unsigned AArch64TargetInfo::multiVersionFeatureCost() const {
  // Take the maximum priority as per feature cost, so more features win.
  constexpr unsigned MaxFMVPriority = 1000;
  return MaxFMVPriority;
}

bool AArch64TargetInfo::doesFeatureAffectCodeGen(StringRef Name) const {
  // FMV extensions which imply no backend features do not affect codegen.
  if (auto Ext = llvm::AArch64::parseFMVExtension(Name))
    return !Ext->Features.empty();
  return false;
}

bool AArch64TargetInfo::validateCpuSupports(StringRef FeatureStr) const {
  // CPU features might be separated by '+', extract them and check
  llvm::SmallVector<StringRef, 8> Features;
  FeatureStr.split(Features, "+");
  for (auto &Feature : Features)
    if (!llvm::AArch64::parseFMVExtension(Feature.trim()).has_value())
      return false;
  return true;
}

bool AArch64TargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Cases("aarch64", "arm64", "arm", true)
      .Case("fmv", HasFMV)
      .Case("fp", FPU & FPUMode)
      .Cases("neon", "simd", FPU & NeonMode)
      .Case("jscvt", HasJSCVT)
      .Case("fcma", HasFCMA)
      .Case("rng", HasRandGen)
      .Case("flagm", HasFlagM)
      .Case("flagm2", HasAlternativeNZCV)
      .Case("fp16fml", HasFP16FML)
      .Case("dotprod", HasDotProd)
      .Case("sm4", HasSM4)
      .Case("rdm", HasRDM)
      .Case("lse", HasLSE)
      .Case("crc", HasCRC)
      .Case("sha2", HasSHA2)
      .Case("sha3", HasSHA3)
      .Cases("aes", "pmull", HasAES)
      .Cases("fp16", "fullfp16", HasFullFP16)
      .Case("dit", HasDIT)
      .Case("dpb", HasCCPP)
      .Case("dpb2", HasCCDP)
      .Case("rcpc", HasRCPC)
      .Case("frintts", HasFRInt3264)
      .Case("i8mm", HasMatMul)
      .Case("bf16", HasBFloat16)
      .Case("sve", FPU & SveMode)
      .Case("sve-bf16", FPU & SveMode && HasBFloat16)
      .Case("sve-i8mm", FPU & SveMode && HasMatMul)
      .Case("sve-b16b16", HasSVEB16B16)
      .Case("f32mm", FPU & SveMode && HasMatmulFP32)
      .Case("f64mm", FPU & SveMode && HasMatmulFP64)
      .Case("sve2", FPU & SveMode && HasSVE2)
      .Case("sve2-pmull128", FPU & SveMode && HasSVE2AES)
      .Case("sve2-bitperm", FPU & SveMode && HasSVE2BitPerm)
      .Case("sve2-sha3", FPU & SveMode && HasSVE2SHA3)
      .Case("sve2-sm4", FPU & SveMode && HasSVE2SM4)
      .Case("sve2p1", FPU & SveMode && HasSVE2p1)
      .Case("sme", HasSME)
      .Case("sme2", HasSME2)
      .Case("sme2p1", HasSME2p1)
      .Case("sme-f64f64", HasSMEF64F64)
      .Case("sme-i16i64", HasSMEI16I64)
      .Case("sme-fa64", HasSMEFA64)
      .Case("sme-f16f16", HasSMEF16F16)
      .Case("sme-b16b16", HasSMEB16B16)
      .Cases("memtag", "memtag2", HasMTE)
      .Case("sb", HasSB)
      .Case("predres", HasPredRes)
      .Cases("ssbs", "ssbs2", HasSSBS)
      .Case("bti", HasBTI)
      .Cases("ls64", "ls64_v", "ls64_accdata", HasLS64)
      .Case("wfxt", HasWFxT)
      .Case("rcpc3", HasRCPC3)
      .Default(false);
}

void AArch64TargetInfo::setFeatureEnabled(llvm::StringMap<bool> &Features,
                                          StringRef Name, bool Enabled) const {
  Features[Name] = Enabled;
  // If the feature is an architecture feature (like v8.2a), add all previous
  // architecture versions and any dependant target features.
  const std::optional<llvm::AArch64::ArchInfo> ArchInfo =
      llvm::AArch64::ArchInfo::findBySubArch(Name);

  if (!ArchInfo)
    return; // Not an architecture, nothing more to do.

  // Disabling an architecture feature does not affect dependent features
  if (!Enabled)
    return;

  for (const auto *OtherArch : llvm::AArch64::ArchInfos)
    if (ArchInfo->implies(*OtherArch))
      Features[OtherArch->getSubArch()] = true;

  // Set any features implied by the architecture
  std::vector<StringRef> CPUFeats;
  if (llvm::AArch64::getExtensionFeatures(ArchInfo->DefaultExts, CPUFeats)) {
    for (auto F : CPUFeats) {
      assert(F[0] == '+' && "Expected + in target feature!");
      Features[F.drop_front(1)] = true;
    }
  }
}

bool AArch64TargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                             DiagnosticsEngine &Diags) {
  for (const auto &Feature : Features) {
    if (Feature == "-fp-armv8")
      HasNoFP = true;
    if (Feature == "-neon")
      HasNoNeon = true;
    if (Feature == "-sve")
      HasNoSVE = true;

    if (Feature == "+neon" || Feature == "+fp-armv8")
      FPU |= NeonMode;
    if (Feature == "+jscvt") {
      HasJSCVT = true;
      FPU |= NeonMode;
    }
    if (Feature == "+fcma") {
      HasFCMA = true;
      FPU |= NeonMode;
    }

    if (Feature == "+sve") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
    }
    if (Feature == "+sve2") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
      HasSVE2 = true;
    }
    if (Feature == "+sve2p1") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
      HasSVE2 = true;
      HasSVE2p1 = true;
    }
    if (Feature == "+sve2-aes") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
      HasSVE2 = true;
      HasSVE2AES = true;
    }
    if (Feature == "+sve2-sha3") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
      HasSVE2 = true;
      HasSVE2SHA3 = true;
    }
    if (Feature == "+sve2-sm4") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
      HasSVE2 = true;
      HasSVE2SM4 = true;
    }
    if (Feature == "+sve-b16b16")
      HasSVEB16B16 = true;
    if (Feature == "+sve2-bitperm") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
      HasSVE2 = true;
      HasSVE2BitPerm = true;
    }
    if (Feature == "+f32mm") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
      HasMatmulFP32 = true;
    }
    if (Feature == "+f64mm") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasFullFP16 = true;
      HasMatmulFP64 = true;
    }
    if (Feature == "+sme") {
      HasSME = true;
      HasBFloat16 = true;
      HasFullFP16 = true;
    }
    if (Feature == "+sme2") {
      HasSME = true;
      HasSME2 = true;
      HasBFloat16 = true;
      HasFullFP16 = true;
    }
    if (Feature == "+sme2p1") {
      HasSME = true;
      HasSME2 = true;
      HasSME2p1 = true;
      HasBFloat16 = true;
      HasFullFP16 = true;
    }
    if (Feature == "+sme-f64f64") {
      HasSME = true;
      HasSMEF64F64 = true;
      HasBFloat16 = true;
      HasFullFP16 = true;
    }
    if (Feature == "+sme-i16i64") {
      HasSME = true;
      HasSMEI16I64 = true;
      HasBFloat16 = true;
      HasFullFP16 = true;
    }
    if (Feature == "+sme-fa64") {
      FPU |= NeonMode;
      FPU |= SveMode;
      HasSME = true;
      HasSVE2 = true;
      HasSMEFA64 = true;
    }
    if (Feature == "+sme-f16f16") {
      HasSME = true;
      HasSME2 = true;
      HasBFloat16 = true;
      HasFullFP16 = true;
      HasSMEF16F16 = true;
    }
    if (Feature == "+sme-b16b16") {
      HasSME = true;
      HasSME2 = true;
      HasBFloat16 = true;
      HasFullFP16 = true;
      HasSVEB16B16 = true;
      HasSMEB16B16 = true;
    }
    if (Feature == "+sb")
      HasSB = true;
    if (Feature == "+predres")
      HasPredRes = true;
    if (Feature == "+ssbs")
      HasSSBS = true;
    if (Feature == "+bti")
      HasBTI = true;
    if (Feature == "+wfxt")
      HasWFxT = true;
    if (Feature == "-fmv")
      HasFMV = false;
    if (Feature == "+crc")
      HasCRC = true;
    if (Feature == "+rcpc")
      HasRCPC = true;
    if (Feature == "+aes") {
      FPU |= NeonMode;
      HasAES = true;
    }
    if (Feature == "+sha2") {
      FPU |= NeonMode;
      HasSHA2 = true;
    }
    if (Feature == "+sha3") {
      FPU |= NeonMode;
      HasSHA2 = true;
      HasSHA3 = true;
    }
    if (Feature == "+rdm") {
      FPU |= NeonMode;
      HasRDM = true;
    }
    if (Feature == "+dit")
      HasDIT = true;
    if (Feature == "+cccp")
      HasCCPP = true;
    if (Feature == "+ccdp") {
      HasCCPP = true;
      HasCCDP = true;
    }
    if (Feature == "+fptoint")
      HasFRInt3264 = true;
    if (Feature == "+sm4") {
      FPU |= NeonMode;
      HasSM4 = true;
    }
    if (Feature == "+strict-align")
      HasUnalignedAccess = false;

    // All predecessor archs are added but select the latest one for ArchKind.
    if (Feature == "+v8a" && ArchInfo->Version < llvm::AArch64::ARMV8A.Version)
      ArchInfo = &llvm::AArch64::ARMV8A;
    if (Feature == "+v8.1a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_1A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_1A;
    if (Feature == "+v8.2a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_2A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_2A;
    if (Feature == "+v8.3a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_3A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_3A;
    if (Feature == "+v8.4a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_4A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_4A;
    if (Feature == "+v8.5a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_5A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_5A;
    if (Feature == "+v8.6a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_6A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_6A;
    if (Feature == "+v8.7a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_7A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_7A;
    if (Feature == "+v8.8a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_8A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_8A;
    if (Feature == "+v8.9a" &&
        ArchInfo->Version < llvm::AArch64::ARMV8_9A.Version)
      ArchInfo = &llvm::AArch64::ARMV8_9A;
    if (Feature == "+v9a" && ArchInfo->Version < llvm::AArch64::ARMV9A.Version)
      ArchInfo = &llvm::AArch64::ARMV9A;
    if (Feature == "+v9.1a" &&
        ArchInfo->Version < llvm::AArch64::ARMV9_1A.Version)
      ArchInfo = &llvm::AArch64::ARMV9_1A;
    if (Feature == "+v9.2a" &&
        ArchInfo->Version < llvm::AArch64::ARMV9_2A.Version)
      ArchInfo = &llvm::AArch64::ARMV9_2A;
    if (Feature == "+v9.3a" &&
        ArchInfo->Version < llvm::AArch64::ARMV9_3A.Version)
      ArchInfo = &llvm::AArch64::ARMV9_3A;
    if (Feature == "+v9.4a" &&
        ArchInfo->Version < llvm::AArch64::ARMV9_4A.Version)
      ArchInfo = &llvm::AArch64::ARMV9_4A;
    if (Feature == "+v9.5a" &&
        ArchInfo->Version < llvm::AArch64::ARMV9_5A.Version)
      ArchInfo = &llvm::AArch64::ARMV9_5A;
    if (Feature == "+v8r")
      ArchInfo = &llvm::AArch64::ARMV8R;
    if (Feature == "+fullfp16") {
      FPU |= NeonMode;
      HasFullFP16 = true;
    }
    if (Feature == "+dotprod") {
      FPU |= NeonMode;
      HasDotProd = true;
    }
    if (Feature == "+fp16fml") {
      FPU |= NeonMode;
      HasFullFP16 = true;
      HasFP16FML = true;
    }
    if (Feature == "+mte")
      HasMTE = true;
    if (Feature == "+tme")
      HasTME = true;
    if (Feature == "+pauth")
      HasPAuth = true;
    if (Feature == "+i8mm")
      HasMatMul = true;
    if (Feature == "+bf16")
      HasBFloat16 = true;
    if (Feature == "+lse")
      HasLSE = true;
    if (Feature == "+ls64")
      HasLS64 = true;
    if (Feature == "+rand")
      HasRandGen = true;
    if (Feature == "+flagm")
      HasFlagM = true;
    if (Feature == "+altnzcv") {
      HasFlagM = true;
      HasAlternativeNZCV = true;
    }
    if (Feature == "+mops")
      HasMOPS = true;
    if (Feature == "+d128")
      HasD128 = true;
    if (Feature == "+gcs")
      HasGCS = true;
    if (Feature == "+rcpc3")
      HasRCPC3 = true;
    if (Feature == "+pauth-lr") {
      HasPAuthLR = true;
      HasPAuth = true;
    }
  }

  // Check features that are manually disabled by command line options.
  // This needs to be checked after architecture-related features are handled,
  // making sure they are properly disabled when required.
  for (const auto &Feature : Features) {
    if (Feature == "-d128")
      HasD128 = false;
  }

  setDataLayout();
  setArchFeatures();

  if (HasNoFP) {
    FPU &= ~FPUMode;
    FPU &= ~NeonMode;
    FPU &= ~SveMode;
  }
  if (HasNoNeon) {
    FPU &= ~NeonMode;
    FPU &= ~SveMode;
  }
  if (HasNoSVE)
    FPU &= ~SveMode;

  return true;
}

// Parse AArch64 Target attributes, which are a comma separated list of:
//  "arch=<arch>" - parsed to features as per -march=..
//  "cpu=<cpu>" - parsed to features as per -mcpu=.., with CPU set to <cpu>
//  "tune=<cpu>" - TuneCPU set to <cpu>
//  "feature", "no-feature" - Add (or remove) feature.
//  "+feature", "+nofeature" - Add (or remove) feature.
//
// A feature may correspond to an Extension (anything with a corresponding
// AEK_), in which case an ExtensionSet is used to parse it and expand its
// dependencies. If the feature does not yield a successful parse then it
// is passed through.
ParsedTargetAttr AArch64TargetInfo::parseTargetAttr(StringRef Features) const {
  ParsedTargetAttr Ret;
  if (Features == "default")
    return Ret;
  SmallVector<StringRef, 1> AttrFeatures;
  Features.split(AttrFeatures, ",");
  bool FoundArch = false;

  auto SplitAndAddFeatures = [](StringRef FeatString,
                                std::vector<std::string> &Features,
                                llvm::AArch64::ExtensionSet &FeatureBits) {
    SmallVector<StringRef, 8> SplitFeatures;
    FeatString.split(SplitFeatures, StringRef("+"), -1, false);
    for (StringRef Feature : SplitFeatures) {
      if (FeatureBits.parseModifier(Feature))
        continue;
      // Pass through anything that failed to parse so that we can emit
      // diagnostics, as well as valid internal feature names.
      //
      // FIXME: We should consider rejecting internal feature names like
      //        neon, v8a, etc.
      // FIXME: We should consider emitting diagnostics here.
      if (Feature.starts_with("no"))
        Features.push_back("-" + Feature.drop_front(2).str());
      else
        Features.push_back("+" + Feature.str());
    }
  };

  llvm::AArch64::ExtensionSet FeatureBits;
  // Reconstruct the bitset from the command line option features.
  FeatureBits.reconstructFromParsedFeatures(getTargetOpts().FeaturesAsWritten,
                                            Ret.Features);

  for (auto &Feature : AttrFeatures) {
    Feature = Feature.trim();
    if (Feature.starts_with("fpmath="))
      continue;

    if (Feature.starts_with("branch-protection=")) {
      Ret.BranchProtection = Feature.split('=').second.trim();
      continue;
    }

    if (Feature.starts_with("arch=")) {
      if (FoundArch)
        Ret.Duplicate = "arch=";
      FoundArch = true;
      std::pair<StringRef, StringRef> Split =
          Feature.split("=").second.trim().split("+");
      const llvm::AArch64::ArchInfo *AI = llvm::AArch64::parseArch(Split.first);

      // Parse the architecture version, adding the required features to
      // Ret.Features.
      if (!AI)
        continue;
      FeatureBits.addArchDefaults(*AI);
      // Add any extra features, after the +
      SplitAndAddFeatures(Split.second, Ret.Features, FeatureBits);
    } else if (Feature.starts_with("cpu=")) {
      if (!Ret.CPU.empty())
        Ret.Duplicate = "cpu=";
      else {
        // Split the cpu string into "cpu=", "cortex-a710" and any remaining
        // "+feat" features.
        std::pair<StringRef, StringRef> Split =
            Feature.split("=").second.trim().split("+");
        Ret.CPU = Split.first;
        if (auto CpuInfo = llvm::AArch64::parseCpu(Ret.CPU)) {
          FeatureBits.addCPUDefaults(*CpuInfo);
          SplitAndAddFeatures(Split.second, Ret.Features, FeatureBits);
        }
      }
    } else if (Feature.starts_with("tune=")) {
      if (!Ret.Tune.empty())
        Ret.Duplicate = "tune=";
      else
        Ret.Tune = Feature.split("=").second.trim();
    } else if (Feature.starts_with("+")) {
      SplitAndAddFeatures(Feature, Ret.Features, FeatureBits);
    } else {
      if (FeatureBits.parseModifier(Feature, /* AllowNoDashForm = */ true))
        continue;
      // Pass through anything that failed to parse so that we can emit
      // diagnostics, as well as valid internal feature names.
      //
      // FIXME: We should consider rejecting internal feature names like
      //        neon, v8a, etc.
      // FIXME: We should consider emitting diagnostics here.
      if (Feature.starts_with("no-"))
        Ret.Features.push_back("-" + Feature.drop_front(3).str());
      else
        Ret.Features.push_back("+" + Feature.str());
    }
  }
  FeatureBits.toLLVMFeatureList(Ret.Features);
  return Ret;
}

bool AArch64TargetInfo::hasBFloat16Type() const {
  return true;
}

TargetInfo::CallingConvCheckResult
AArch64TargetInfo::checkCallingConvention(CallingConv CC) const {
  switch (CC) {
  case CC_C:
  case CC_Swift:
  case CC_SwiftAsync:
  case CC_PreserveMost:
  case CC_PreserveAll:
  case CC_PreserveNone:
  case CC_OpenCLKernel:
  case CC_AArch64VectorCall:
  case CC_AArch64SVEPCS:
  case CC_Win64:
    return CCCR_OK;
  default:
    return CCCR_Warning;
  }
}

bool AArch64TargetInfo::isCLZForZeroUndef() const { return false; }

TargetInfo::BuiltinVaListKind AArch64TargetInfo::getBuiltinVaListKind() const {
  return TargetInfo::AArch64ABIBuiltinVaList;
}

const char *const AArch64TargetInfo::GCCRegNames[] = {
    // clang-format off

    // 32-bit Integer registers
    "w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8", "w9", "w10", "w11",
    "w12", "w13", "w14", "w15", "w16", "w17", "w18", "w19", "w20", "w21", "w22",
    "w23", "w24", "w25", "w26", "w27", "w28", "w29", "w30", "wsp",

    // 64-bit Integer registers
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
    "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21", "x22",
    "x23", "x24", "x25", "x26", "x27", "x28", "fp", "lr", "sp",

    // 32-bit floating point regsisters
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
    "s12", "s13", "s14", "s15", "s16", "s17", "s18", "s19", "s20", "s21", "s22",
    "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31",

    // 64-bit floating point regsisters
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11",
    "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22",
    "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31",

    // Neon vector registers
    "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",
    "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22",
    "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",

    // SVE vector registers
    "z0",  "z1",  "z2",  "z3",  "z4",  "z5",  "z6",  "z7",  "z8",  "z9",  "z10",
    "z11", "z12", "z13", "z14", "z15", "z16", "z17", "z18", "z19", "z20", "z21",
    "z22", "z23", "z24", "z25", "z26", "z27", "z28", "z29", "z30", "z31",

    // SVE predicate registers
    "p0",  "p1",  "p2",  "p3",  "p4",  "p5",  "p6",  "p7",  "p8",  "p9",  "p10",
    "p11", "p12", "p13", "p14", "p15",

    // SVE predicate-as-counter registers
    "pn0",  "pn1",  "pn2",  "pn3",  "pn4",  "pn5",  "pn6",  "pn7",  "pn8",
    "pn9",  "pn10", "pn11", "pn12", "pn13", "pn14", "pn15",

    // SME registers
    "za", "zt0",

    // clang-format on
};

ArrayRef<const char *> AArch64TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

const TargetInfo::GCCRegAlias AArch64TargetInfo::GCCRegAliases[] = {
    {{"w31"}, "wsp"},
    {{"x31"}, "sp"},
    // GCC rN registers are aliases of xN registers.
    {{"r0"}, "x0"},
    {{"r1"}, "x1"},
    {{"r2"}, "x2"},
    {{"r3"}, "x3"},
    {{"r4"}, "x4"},
    {{"r5"}, "x5"},
    {{"r6"}, "x6"},
    {{"r7"}, "x7"},
    {{"r8"}, "x8"},
    {{"r9"}, "x9"},
    {{"r10"}, "x10"},
    {{"r11"}, "x11"},
    {{"r12"}, "x12"},
    {{"r13"}, "x13"},
    {{"r14"}, "x14"},
    {{"r15"}, "x15"},
    {{"r16"}, "x16"},
    {{"r17"}, "x17"},
    {{"r18"}, "x18"},
    {{"r19"}, "x19"},
    {{"r20"}, "x20"},
    {{"r21"}, "x21"},
    {{"r22"}, "x22"},
    {{"r23"}, "x23"},
    {{"r24"}, "x24"},
    {{"r25"}, "x25"},
    {{"r26"}, "x26"},
    {{"r27"}, "x27"},
    {{"r28"}, "x28"},
    {{"r29", "x29"}, "fp"},
    {{"r30", "x30"}, "lr"},
    // The S/D/Q and W/X registers overlap, but aren't really aliases; we
    // don't want to substitute one of these for a different-sized one.
};

ArrayRef<TargetInfo::GCCRegAlias> AArch64TargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

// Returns the length of cc constraint.
static unsigned matchAsmCCConstraint(const char *Name) {
  constexpr unsigned len = 5;
  auto RV = llvm::StringSwitch<unsigned>(Name)
                .Case("@cceq", len)
                .Case("@ccne", len)
                .Case("@cchs", len)
                .Case("@cccs", len)
                .Case("@cccc", len)
                .Case("@cclo", len)
                .Case("@ccmi", len)
                .Case("@ccpl", len)
                .Case("@ccvs", len)
                .Case("@ccvc", len)
                .Case("@cchi", len)
                .Case("@ccls", len)
                .Case("@ccge", len)
                .Case("@cclt", len)
                .Case("@ccgt", len)
                .Case("@ccle", len)
                .Default(0);
  return RV;
}

std::string
AArch64TargetInfo::convertConstraint(const char *&Constraint) const {
  std::string R;
  switch (*Constraint) {
  case 'U': // Three-character constraint; add "@3" hint for later parsing.
    R = std::string("@3") + std::string(Constraint, 3);
    Constraint += 2;
    break;
  case '@':
    if (const unsigned Len = matchAsmCCConstraint(Constraint)) {
      std::string Converted = "{" + std::string(Constraint, Len) + "}";
      Constraint += Len - 1;
      return Converted;
    }
    return std::string(1, *Constraint);
  default:
    R = TargetInfo::convertConstraint(Constraint);
    break;
  }
  return R;
}

bool AArch64TargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;
  case 'w': // Floating point and SIMD registers (V0-V31)
    Info.setAllowsRegister();
    return true;
  case 'I': // Constant that can be used with an ADD instruction
  case 'J': // Constant that can be used with a SUB instruction
  case 'K': // Constant that can be used with a 32-bit logical instruction
  case 'L': // Constant that can be used with a 64-bit logical instruction
  case 'M': // Constant that can be used as a 32-bit MOV immediate
  case 'N': // Constant that can be used as a 64-bit MOV immediate
  case 'Y': // Floating point constant zero
  case 'Z': // Integer constant zero
    return true;
  case 'Q': // A memory reference with base register and no offset
    Info.setAllowsMemory();
    return true;
  case 'S': // A symbolic address
    Info.setAllowsRegister();
    return true;
  case 'U':
    if (Name[1] == 'p' &&
        (Name[2] == 'l' || Name[2] == 'a' || Name[2] == 'h')) {
      // SVE predicate registers ("Upa"=P0-15, "Upl"=P0-P7, "Uph"=P8-P15)
      Info.setAllowsRegister();
      Name += 2;
      return true;
    }
    if (Name[1] == 'c' && (Name[2] == 'i' || Name[2] == 'j')) {
      // Gpr registers ("Uci"=w8-11, "Ucj"=w12-15)
      Info.setAllowsRegister();
      Name += 2;
      return true;
    }
    // Ump: A memory address suitable for ldp/stp in SI, DI, SF and DF modes.
    // Utf: A memory address suitable for ldp/stp in TF mode.
    // Usa: An absolute symbolic address.
    // Ush: The high part (bits 32:12) of a pc-relative symbolic address.

    // Better to return an error saying that it's an unrecognised constraint
    // even if this is a valid constraint in gcc.
    return false;
  case 'z': // Zero register, wzr or xzr
    Info.setAllowsRegister();
    return true;
  case 'x': // Floating point and SIMD registers (V0-V15)
    Info.setAllowsRegister();
    return true;
  case 'y': // SVE registers (V0-V7)
    Info.setAllowsRegister();
    return true;
  case '@':
    // CC condition
    if (const unsigned Len = matchAsmCCConstraint(Name)) {
      Name += Len - 1;
      Info.setAllowsRegister();
      return true;
    }
  }
  return false;
}

bool AArch64TargetInfo::validateConstraintModifier(
    StringRef Constraint, char Modifier, unsigned Size,
    std::string &SuggestedModifier) const {
  // Strip off constraint modifiers.
  Constraint = Constraint.ltrim("=+&");

  switch (Constraint[0]) {
  default:
    return true;
  case 'z':
  case 'r': {
    switch (Modifier) {
    case 'x':
    case 'w':
      // For now assume that the person knows what they're
      // doing with the modifier.
      return true;
    default:
      // By default an 'r' constraint will be in the 'x'
      // registers.
      if (Size == 64)
        return true;

      if (Size == 512)
        return HasLS64;

      SuggestedModifier = "w";
      return false;
    }
  }
  }
}

std::string_view AArch64TargetInfo::getClobbers() const { return ""; }

int AArch64TargetInfo::getEHDataRegisterNumber(unsigned RegNo) const {
  if (RegNo == 0)
    return 0;
  if (RegNo == 1)
    return 1;
  return -1;
}

bool AArch64TargetInfo::validatePointerAuthKey(
    const llvm::APSInt &value) const {
  return 0 <= value && value <= 3;
}

bool AArch64TargetInfo::hasInt128Type() const { return true; }

AArch64leTargetInfo::AArch64leTargetInfo(const llvm::Triple &Triple,
                                         const TargetOptions &Opts)
    : AArch64TargetInfo(Triple, Opts) {}

void AArch64leTargetInfo::setDataLayout() {
  if (getTriple().isOSBinFormatMachO()) {
    if(getTriple().isArch32Bit())
      resetDataLayout("e-m:o-p:32:32-i64:64-i128:128-n32:64-S128-Fn32", "_");
    else
      resetDataLayout("e-m:o-i64:64-i128:128-n32:64-S128-Fn32", "_");
  } else
    resetDataLayout("e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32");
}

void AArch64leTargetInfo::getTargetDefines(const LangOptions &Opts,
                                           MacroBuilder &Builder) const {
  Builder.defineMacro("__AARCH64EL__");
  AArch64TargetInfo::getTargetDefines(Opts, Builder);
}

AArch64beTargetInfo::AArch64beTargetInfo(const llvm::Triple &Triple,
                                         const TargetOptions &Opts)
    : AArch64TargetInfo(Triple, Opts) {}

void AArch64beTargetInfo::getTargetDefines(const LangOptions &Opts,
                                           MacroBuilder &Builder) const {
  Builder.defineMacro("__AARCH64EB__");
  Builder.defineMacro("__AARCH_BIG_ENDIAN");
  Builder.defineMacro("__ARM_BIG_ENDIAN");
  AArch64TargetInfo::getTargetDefines(Opts, Builder);
}

void AArch64beTargetInfo::setDataLayout() {
  assert(!getTriple().isOSBinFormatMachO());
  resetDataLayout("E-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32");
}

WindowsARM64TargetInfo::WindowsARM64TargetInfo(const llvm::Triple &Triple,
                                               const TargetOptions &Opts)
    : WindowsTargetInfo<AArch64leTargetInfo>(Triple, Opts), Triple(Triple) {

  // This is an LLP64 platform.
  // int:4, long:4, long long:8, long double:8.
  IntWidth = IntAlign = 32;
  LongWidth = LongAlign = 32;
  DoubleAlign = LongLongAlign = 64;
  LongDoubleWidth = LongDoubleAlign = 64;
  LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  IntMaxType = SignedLongLong;
  Int64Type = SignedLongLong;
  SizeType = UnsignedLongLong;
  PtrDiffType = SignedLongLong;
  IntPtrType = SignedLongLong;
}

void WindowsARM64TargetInfo::setDataLayout() {
  resetDataLayout(Triple.isOSBinFormatMachO()
                      ? "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
                      : "e-m:w-p:64:64-i32:32-i64:64-i128:128-n32:64-S128-Fn32",
                  Triple.isOSBinFormatMachO() ? "_" : "");
}

TargetInfo::BuiltinVaListKind
WindowsARM64TargetInfo::getBuiltinVaListKind() const {
  return TargetInfo::CharPtrBuiltinVaList;
}

TargetInfo::CallingConvCheckResult
WindowsARM64TargetInfo::checkCallingConvention(CallingConv CC) const {
  switch (CC) {
  case CC_X86VectorCall:
    if (getTriple().isWindowsArm64EC())
      return CCCR_OK;
    return CCCR_Ignore;
  case CC_X86StdCall:
  case CC_X86ThisCall:
  case CC_X86FastCall:
    return CCCR_Ignore;
  case CC_C:
  case CC_OpenCLKernel:
  case CC_PreserveMost:
  case CC_PreserveAll:
  case CC_PreserveNone:
  case CC_Swift:
  case CC_SwiftAsync:
  case CC_Win64:
    return CCCR_OK;
  default:
    return CCCR_Warning;
  }
}

MicrosoftARM64TargetInfo::MicrosoftARM64TargetInfo(const llvm::Triple &Triple,
                                                   const TargetOptions &Opts)
    : WindowsARM64TargetInfo(Triple, Opts) {
  TheCXXABI.set(TargetCXXABI::Microsoft);
}

void MicrosoftARM64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  WindowsARM64TargetInfo::getTargetDefines(Opts, Builder);
  if (getTriple().isWindowsArm64EC()) {
    Builder.defineMacro("_M_X64", "100");
    Builder.defineMacro("_M_AMD64", "100");
    Builder.defineMacro("_M_ARM64EC", "1");
  } else {
    Builder.defineMacro("_M_ARM64", "1");
  }
}

TargetInfo::CallingConvKind
MicrosoftARM64TargetInfo::getCallingConvKind(bool ClangABICompat4) const {
  return CCK_MicrosoftWin64;
}

unsigned MicrosoftARM64TargetInfo::getMinGlobalAlign(uint64_t TypeSize,
                                                     bool HasNonWeakDef) const {
  unsigned Align =
      WindowsARM64TargetInfo::getMinGlobalAlign(TypeSize, HasNonWeakDef);

  // MSVC does size based alignment for arm64 based on alignment section in
  // below document, replicate that to keep alignment consistent with object
  // files compiled by MSVC.
  // https://docs.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions
  if (TypeSize >= 512) {              // TypeSize >= 64 bytes
    Align = std::max(Align, 128u);    // align type at least 16 bytes
  } else if (TypeSize >= 64) {        // TypeSize >= 8 bytes
    Align = std::max(Align, 64u);     // align type at least 8 butes
  } else if (TypeSize >= 16) {        // TypeSize >= 2 bytes
    Align = std::max(Align, 32u);     // align type at least 4 bytes
  }
  return Align;
}

MinGWARM64TargetInfo::MinGWARM64TargetInfo(const llvm::Triple &Triple,
                                           const TargetOptions &Opts)
    : WindowsARM64TargetInfo(Triple, Opts) {
  TheCXXABI.set(TargetCXXABI::GenericAArch64);
}

DarwinAArch64TargetInfo::DarwinAArch64TargetInfo(const llvm::Triple &Triple,
                                                 const TargetOptions &Opts)
    : DarwinTargetInfo<AArch64leTargetInfo>(Triple, Opts) {
  Int64Type = SignedLongLong;
  if (getTriple().isArch32Bit())
    IntMaxType = SignedLongLong;

  WCharType = SignedInt;
  UseSignedCharForObjCBool = false;

  LongDoubleWidth = LongDoubleAlign = SuitableAlign = 64;
  LongDoubleFormat = &llvm::APFloat::IEEEdouble();

  UseZeroLengthBitfieldAlignment = false;

  if (getTriple().isArch32Bit()) {
    UseBitFieldTypeAlignment = false;
    ZeroLengthBitfieldBoundary = 32;
    UseZeroLengthBitfieldAlignment = true;
    TheCXXABI.set(TargetCXXABI::WatchOS);
  } else
    TheCXXABI.set(TargetCXXABI::AppleARM64);
}

void DarwinAArch64TargetInfo::getOSDefines(const LangOptions &Opts,
                                           const llvm::Triple &Triple,
                                           MacroBuilder &Builder) const {
  Builder.defineMacro("__AARCH64_SIMD__");
  if (Triple.isArch32Bit())
    Builder.defineMacro("__ARM64_ARCH_8_32__");
  else
    Builder.defineMacro("__ARM64_ARCH_8__");
  Builder.defineMacro("__ARM_NEON__");
  Builder.defineMacro("__REGISTER_PREFIX__", "");
  Builder.defineMacro("__arm64", "1");
  Builder.defineMacro("__arm64__", "1");

  if (Triple.isArm64e())
    Builder.defineMacro("__arm64e__", "1");

  getDarwinDefines(Builder, Opts, Triple, PlatformName, PlatformMinVersion);
}

TargetInfo::BuiltinVaListKind
DarwinAArch64TargetInfo::getBuiltinVaListKind() const {
  return TargetInfo::CharPtrBuiltinVaList;
}

// 64-bit RenderScript is aarch64
RenderScript64TargetInfo::RenderScript64TargetInfo(const llvm::Triple &Triple,
                                                   const TargetOptions &Opts)
    : AArch64leTargetInfo(llvm::Triple("aarch64", Triple.getVendorName(),
                                       Triple.getOSName(),
                                       Triple.getEnvironmentName()),
                          Opts) {
  IsRenderScriptTarget = true;
}

void RenderScript64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  Builder.defineMacro("__RENDERSCRIPT__");
  AArch64leTargetInfo::getTargetDefines(Opts, Builder);
}
