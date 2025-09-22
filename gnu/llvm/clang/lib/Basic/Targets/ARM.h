//===--- ARM.h - Declare ARM target feature support -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares ARM TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_ARM_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_ARM_H

#include "OSTargets.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/ARMTargetParser.h"
#include "llvm/TargetParser/ARMTargetParserCommon.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY ARMTargetInfo : public TargetInfo {
  // Possible FPU choices.
  enum FPUMode {
    VFP2FPU = (1 << 0),
    VFP3FPU = (1 << 1),
    VFP4FPU = (1 << 2),
    NeonFPU = (1 << 3),
    FPARMV8 = (1 << 4)
  };

  enum MVEMode {
      MVE_INT = (1 << 0),
      MVE_FP  = (1 << 1)
  };

  // Possible HWDiv features.
  enum HWDivMode { HWDivThumb = (1 << 0), HWDivARM = (1 << 1) };

  static bool FPUModeIsVFP(FPUMode Mode) {
    return Mode & (VFP2FPU | VFP3FPU | VFP4FPU | NeonFPU | FPARMV8);
  }

  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  static const char *const GCCRegNames[];

  std::string ABI, CPU;

  StringRef CPUProfile;
  StringRef CPUAttr;

  enum { FP_Default, FP_VFP, FP_Neon } FPMath;

  llvm::ARM::ISAKind ArchISA;
  llvm::ARM::ArchKind ArchKind = llvm::ARM::ArchKind::ARMV4T;
  llvm::ARM::ProfileKind ArchProfile;
  unsigned ArchVersion;

  LLVM_PREFERRED_TYPE(FPUMode)
  unsigned FPU : 5;
  LLVM_PREFERRED_TYPE(MVEMode)
  unsigned MVE : 2;

  LLVM_PREFERRED_TYPE(bool)
  unsigned IsAAPCS : 1;
  LLVM_PREFERRED_TYPE(HWDivMode)
  unsigned HWDiv : 2;

  // Initialized via features.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SoftFloat : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SoftFloatABI : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned CRC : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned Crypto : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SHA2 : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned AES : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned DSP : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned DotProd : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasMatMul : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned FPRegsDisabled : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasPAC : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasBTI : 1;

  enum {
    LDREX_B = (1 << 0), /// byte (8-bit)
    LDREX_H = (1 << 1), /// half (16-bit)
    LDREX_W = (1 << 2), /// word (32-bit)
    LDREX_D = (1 << 3), /// double (64-bit)
  };

  uint32_t LDREX;

  // ACLE 6.5.1 Hardware floating point
  enum {
    HW_FP_HP = (1 << 1), /// half (16-bit)
    HW_FP_SP = (1 << 2), /// single (32-bit)
    HW_FP_DP = (1 << 3), /// double (64-bit)
  };
  uint32_t HW_FP;

  enum {
    /// __arm_cdp __arm_ldc, __arm_ldcl, __arm_stc,
    /// __arm_stcl, __arm_mcr and __arm_mrc
    FEATURE_COPROC_B1 = (1 << 0),
    /// __arm_cdp2, __arm_ldc2, __arm_stc2, __arm_ldc2l,
    /// __arm_stc2l, __arm_mcr2 and __arm_mrc2
    FEATURE_COPROC_B2 = (1 << 1),
    /// __arm_mcrr, __arm_mrrc
    FEATURE_COPROC_B3 = (1 << 2),
    /// __arm_mcrr2,  __arm_mrrc2
    FEATURE_COPROC_B4 = (1 << 3),
  };

  void setABIAAPCS();
  void setABIAPCS(bool IsAAPCS16);

  void setArchInfo();
  void setArchInfo(llvm::ARM::ArchKind Kind);

  void setAtomic();

  bool isThumb() const;
  bool supportsThumb() const;
  bool supportsThumb2() const;
  bool hasMVE() const;
  bool hasMVEFloat() const;
  bool hasCDE() const;

  StringRef getCPUAttr() const;
  StringRef getCPUProfile() const;

public:
  ARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  StringRef getABI() const override;
  bool setABI(const std::string &Name) override;

  bool isBranchProtectionSupportedArch(StringRef Arch) const override;
  bool validateBranchProtection(StringRef Spec, StringRef Arch,
                                BranchProtectionInfo &BPI,
                                StringRef &Err) const override;

  // FIXME: This should be based on Arch attributes, not CPU names.
  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;

  bool isValidFeatureName(StringRef Feature) const override {
    // We pass soft-float-abi in as a -target-feature, but the backend figures
    // this out through other means.
    return Feature != "soft-float-abi";
  }

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  bool hasFeature(StringRef Feature) const override;

  bool hasBFloat16Type() const override;

  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override;

  bool setFPMath(StringRef Name) override;

  bool useFP16ConversionIntrinsics() const override {
    return false;
  }

  void getTargetDefinesARMV81A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV82A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefinesARMV83A(const LangOptions &Opts,
                                 MacroBuilder &Builder) const;
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool isCLZForZeroUndef() const override;
  BuiltinVaListKind getBuiltinVaListKind() const override;

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;
  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;
  std::string convertConstraint(const char *&Constraint) const override;
  bool
  validateConstraintModifier(StringRef Constraint, char Modifier, unsigned Size,
                             std::string &SuggestedModifier) const override;
  std::string_view getClobbers() const override;

  StringRef getConstraintRegister(StringRef Constraint,
                                  StringRef Expression) const override {
    return Expression;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override;

  int getEHDataRegisterNumber(unsigned RegNo) const override;

  bool hasSjLjLowering() const override;

  bool hasBitIntType() const override { return true; }

  const char *getBFloat16Mangling() const override { return "u6__bf16"; };

  std::pair<unsigned, unsigned> hardwareInterferenceSizes() const override {
    return std::make_pair(getTriple().isArch64Bit() ? 256 : 64, 64);
  }
};

class LLVM_LIBRARY_VISIBILITY ARMleTargetInfo : public ARMTargetInfo {
public:
  ARMleTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY ARMbeTargetInfo : public ARMTargetInfo {
public:
  ARMbeTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY WindowsARMTargetInfo
    : public WindowsTargetInfo<ARMleTargetInfo> {
  const llvm::Triple Triple;

public:
  WindowsARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getVisualStudioDefines(const LangOptions &Opts,
                              MacroBuilder &Builder) const;

  BuiltinVaListKind getBuiltinVaListKind() const override;

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override;
};

// Windows ARM + Itanium C++ ABI Target
class LLVM_LIBRARY_VISIBILITY ItaniumWindowsARMleTargetInfo
    : public WindowsARMTargetInfo {
public:
  ItaniumWindowsARMleTargetInfo(const llvm::Triple &Triple,
                                const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

// Windows ARM, MS (C++) ABI
class LLVM_LIBRARY_VISIBILITY MicrosoftARMleTargetInfo
    : public WindowsARMTargetInfo {
public:
  MicrosoftARMleTargetInfo(const llvm::Triple &Triple,
                           const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

// ARM MinGW target
class LLVM_LIBRARY_VISIBILITY MinGWARMTargetInfo : public WindowsARMTargetInfo {
public:
  MinGWARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

// ARM Cygwin target
class LLVM_LIBRARY_VISIBILITY CygwinARMTargetInfo : public ARMleTargetInfo {
public:
  CygwinARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY DarwinARMTargetInfo
    : public DarwinTargetInfo<ARMleTargetInfo> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override;

public:
  DarwinARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);
};

// 32-bit RenderScript is armv7 with width and align of 'long' set to 8-bytes
class LLVM_LIBRARY_VISIBILITY RenderScript32TargetInfo
    : public ARMleTargetInfo {
public:
  RenderScript32TargetInfo(const llvm::Triple &Triple,
                           const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_ARM_H
