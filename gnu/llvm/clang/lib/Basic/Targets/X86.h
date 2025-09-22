//===--- X86.h - Declare X86 target feature support -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares X86 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_X86_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_X86_H

#include "OSTargets.h"
#include "clang/Basic/BitmaskEnum.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <optional>

namespace clang {
namespace targets {

static const unsigned X86AddrSpaceMap[] = {
    0,   // Default
    0,   // opencl_global
    0,   // opencl_local
    0,   // opencl_constant
    0,   // opencl_private
    0,   // opencl_generic
    0,   // opencl_global_device
    0,   // opencl_global_host
    0,   // cuda_device
    0,   // cuda_constant
    0,   // cuda_shared
    0,   // sycl_global
    0,   // sycl_global_device
    0,   // sycl_global_host
    0,   // sycl_local
    0,   // sycl_private
    270, // ptr32_sptr
    271, // ptr32_uptr
    272, // ptr64
    0,   // hlsl_groupshared
    // Wasm address space values for this target are dummy values,
    // as it is only enabled for Wasm targets.
    20, // wasm_funcref
};

// X86 target abstract base class; x86-32 and x86-64 are very close, so
// most of the implementation can be shared.
class LLVM_LIBRARY_VISIBILITY X86TargetInfo : public TargetInfo {

  enum X86SSEEnum {
    NoSSE,
    SSE1,
    SSE2,
    SSE3,
    SSSE3,
    SSE41,
    SSE42,
    AVX,
    AVX2,
    AVX512F
  } SSELevel = NoSSE;
  bool HasMMX = false;
  enum XOPEnum { NoXOP, SSE4A, FMA4, XOP } XOPLevel = NoXOP;
  enum AddrSpace { ptr32_sptr = 270, ptr32_uptr = 271, ptr64 = 272 };

  bool HasAES = false;
  bool HasVAES = false;
  bool HasPCLMUL = false;
  bool HasVPCLMULQDQ = false;
  bool HasGFNI = false;
  bool HasLZCNT = false;
  bool HasRDRND = false;
  bool HasFSGSBASE = false;
  bool HasBMI = false;
  bool HasBMI2 = false;
  bool HasPOPCNT = false;
  bool HasRTM = false;
  bool HasPRFCHW = false;
  bool HasRDSEED = false;
  bool HasADX = false;
  bool HasTBM = false;
  bool HasLWP = false;
  bool HasFMA = false;
  bool HasF16C = false;
  bool HasAVX10_1 = false;
  bool HasAVX10_1_512 = false;
  bool HasEVEX512 = false;
  bool HasAVX512CD = false;
  bool HasAVX512VPOPCNTDQ = false;
  bool HasAVX512VNNI = false;
  bool HasAVX512FP16 = false;
  bool HasAVX512BF16 = false;
  bool HasAVX512DQ = false;
  bool HasAVX512BITALG = false;
  bool HasAVX512BW = false;
  bool HasAVX512VL = false;
  bool HasAVX512VBMI = false;
  bool HasAVX512VBMI2 = false;
  bool HasAVXIFMA = false;
  bool HasAVX512IFMA = false;
  bool HasAVX512VP2INTERSECT = false;
  bool HasSHA = false;
  bool HasSHA512 = false;
  bool HasSHSTK = false;
  bool HasSM3 = false;
  bool HasSGX = false;
  bool HasSM4 = false;
  bool HasCX8 = false;
  bool HasCX16 = false;
  bool HasFXSR = false;
  bool HasXSAVE = false;
  bool HasXSAVEOPT = false;
  bool HasXSAVEC = false;
  bool HasXSAVES = false;
  bool HasMWAITX = false;
  bool HasCLZERO = false;
  bool HasCLDEMOTE = false;
  bool HasPCONFIG = false;
  bool HasPKU = false;
  bool HasCLFLUSHOPT = false;
  bool HasCLWB = false;
  bool HasMOVBE = false;
  bool HasPREFETCHI = false;
  bool HasRDPID = false;
  bool HasRDPRU = false;
  bool HasRetpolineExternalThunk = false;
  bool HasLAHFSAHF = false;
  bool HasWBNOINVD = false;
  bool HasWAITPKG = false;
  bool HasMOVDIRI = false;
  bool HasMOVDIR64B = false;
  bool HasPTWRITE = false;
  bool HasINVPCID = false;
  bool HasSaveArgs = false;
  bool HasENQCMD = false;
  bool HasAVXVNNIINT16 = false;
  bool HasAMXFP16 = false;
  bool HasCMPCCXADD = false;
  bool HasRAOINT = false;
  bool HasAVXVNNIINT8 = false;
  bool HasAVXNECONVERT = false;
  bool HasKL = false;      // For key locker
  bool HasWIDEKL = false; // For wide key locker
  bool HasHRESET = false;
  bool HasAVXVNNI = false;
  bool HasAMXTILE = false;
  bool HasAMXINT8 = false;
  bool HasAMXBF16 = false;
  bool HasAMXCOMPLEX = false;
  bool HasSERIALIZE = false;
  bool HasTSXLDTRK = false;
  bool HasUSERMSR = false;
  bool HasUINTR = false;
  bool HasCRC32 = false;
  bool HasX87 = false;
  bool HasEGPR = false;
  bool HasPush2Pop2 = false;
  bool HasPPX = false;
  bool HasNDD = false;
  bool HasCCMP = false;
  bool HasNF = false;
  bool HasCF = false;
  bool HasZU = false;
  bool HasInlineAsmUseGPR32 = false;
  bool HasBranchHint = false;

protected:
  llvm::X86::CPUKind CPU = llvm::X86::CK_None;

  enum FPMathKind { FP_Default, FP_SSE, FP_387 } FPMath = FP_Default;

public:
  X86TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    BFloat16Width = BFloat16Align = 16;
    BFloat16Format = &llvm::APFloat::BFloat();
    LongDoubleFormat = &llvm::APFloat::x87DoubleExtended();
    AddrSpaceMap = &X86AddrSpaceMap;
    HasStrictFP = true;
    HasUnalignedAccess = true;

    bool IsWinCOFF =
        getTriple().isOSWindows() && getTriple().isOSBinFormatCOFF();
    if (IsWinCOFF)
      MaxVectorAlign = MaxTLSAlign = 8192u * getCharWidth();
  }

  const char *getLongDoubleMangling() const override {
    return LongDoubleFormat == &llvm::APFloat::IEEEquad() ? "g" : "e";
  }

  LangOptions::FPEvalMethodKind getFPEvalMethod() const override {
    // X87 evaluates with 80 bits "long double" precision.
    return SSELevel == NoSSE ? LangOptions::FPEvalMethodKind::FEM_Extended
                             : LangOptions::FPEvalMethodKind::FEM_Source;
  }

  // EvalMethod `source` is not supported for targets with `NoSSE` feature.
  bool supportSourceEvalMethod() const override { return SSELevel > NoSSE; }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return std::nullopt;
  }

  ArrayRef<TargetInfo::AddlRegName> getGCCAddlRegNames() const override;

  bool isSPRegName(StringRef RegName) const override {
    return RegName == "esp" || RegName == "rsp";
  }

  bool supportsCpuSupports() const override { return true; }
  bool supportsCpuIs() const override { return true; }
  bool supportsCpuInit() const override { return true; }

  bool validateCpuSupports(StringRef FeatureStr) const override;

  bool validateCpuIs(StringRef FeatureStr) const override;

  bool validateCPUSpecificCPUDispatch(StringRef Name) const override;

  char CPUSpecificManglingCharacter(StringRef Name) const override;

  void getCPUSpecificCPUDispatchFeatures(
      StringRef Name,
      llvm::SmallVectorImpl<StringRef> &Features) const override;

  std::optional<unsigned> getCPUCacheLineSize() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override;

  bool validateGlobalRegisterVariable(StringRef RegName, unsigned RegSize,
                                      bool &HasSizeMismatch) const override {
    // esp and ebp are the only 32-bit registers the x86 backend can currently
    // handle.
    if (RegName == "esp" || RegName == "ebp") {
      // Check that the register size is 32-bit.
      HasSizeMismatch = RegSize != 32;
      return true;
    }

    return false;
  }

  bool validateOutputSize(const llvm::StringMap<bool> &FeatureMap,
                          StringRef Constraint, unsigned Size) const override;

  bool validateInputSize(const llvm::StringMap<bool> &FeatureMap,
                         StringRef Constraint, unsigned Size) const override;

  bool
  checkCFProtectionReturnSupported(DiagnosticsEngine &Diags) const override {
    if (CPU == llvm::X86::CK_None || CPU >= llvm::X86::CK_PentiumPro)
      return true;
    return TargetInfo::checkCFProtectionReturnSupported(Diags);
  };

  bool
  checkCFProtectionBranchSupported(DiagnosticsEngine &Diags) const override {
    if (CPU == llvm::X86::CK_None || CPU >= llvm::X86::CK_PentiumPro)
      return true;
    return TargetInfo::checkCFProtectionBranchSupported(Diags);
  };

  virtual bool validateOperandSize(const llvm::StringMap<bool> &FeatureMap,
                                   StringRef Constraint, unsigned Size) const;

  std::string convertConstraint(const char *&Constraint) const override;
  std::string_view getClobbers() const override {
    return "~{dirflag},~{fpsr},~{flags}";
  }

  StringRef getConstraintRegister(StringRef Constraint,
                                  StringRef Expression) const override {
    StringRef::iterator I, E;
    for (I = Constraint.begin(), E = Constraint.end(); I != E; ++I) {
      if (isalpha(*I) || *I == '@')
        break;
    }
    if (I == E)
      return "";
    switch (*I) {
    // For the register constraints, return the matching register name
    case 'a':
      return "ax";
    case 'b':
      return "bx";
    case 'c':
      return "cx";
    case 'd':
      return "dx";
    case 'S':
      return "si";
    case 'D':
      return "di";
    // In case the constraint is 'r' we need to return Expression
    case 'r':
      return Expression;
    // Double letters Y<x> constraints
    case 'Y':
      if ((++I != E) && ((*I == '0') || (*I == 'z')))
        return "xmm0";
      break;
    default:
      break;
    }
    return "";
  }

  bool useFP16ConversionIntrinsics() const override {
    return false;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  void setFeatureEnabled(llvm::StringMap<bool> &Features, StringRef Name,
                         bool Enabled) const final;

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;

  bool isValidFeatureName(StringRef Name) const override;

  bool hasFeature(StringRef Feature) const final;

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  StringRef getABI() const override {
    if (getTriple().getArch() == llvm::Triple::x86_64 && SSELevel >= AVX512F)
      return "avx512";
    if (getTriple().getArch() == llvm::Triple::x86_64 && SSELevel >= AVX)
      return "avx";
    if (getTriple().getArch() == llvm::Triple::x86 && !HasMMX)
      return "no-mmx";
    return "";
  }

  bool supportsTargetAttributeTune() const override {
    return true;
  }

  bool isValidCPUName(StringRef Name) const override {
    bool Only64Bit = getTriple().getArch() != llvm::Triple::x86;
    return llvm::X86::parseArchX86(Name, Only64Bit) != llvm::X86::CK_None;
  }

  bool isValidTuneCPUName(StringRef Name) const override {
    if (Name == "generic")
      return true;

    // Allow 32-bit only CPUs regardless of 64-bit mode unlike isValidCPUName.
    // NOTE: gcc rejects 32-bit mtune CPUs in 64-bit mode. But being lenient
    // since mtune was ignored by clang for so long.
    return llvm::X86::parseTuneCPU(Name) != llvm::X86::CK_None;
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;
  void fillValidTuneCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override {
    bool Only64Bit = getTriple().getArch() != llvm::Triple::x86;
    CPU = llvm::X86::parseArchX86(Name, Only64Bit);
    return CPU != llvm::X86::CK_None;
  }

  unsigned multiVersionSortPriority(StringRef Name) const override;

  bool setFPMath(StringRef Name) override;

  bool supportsExtendIntArgs() const override {
    return getTriple().getArch() != llvm::Triple::x86;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    // Most of the non-ARM calling conventions are i386 conventions.
    switch (CC) {
    case CC_X86ThisCall:
    case CC_X86FastCall:
    case CC_X86StdCall:
    case CC_X86VectorCall:
    case CC_X86RegCall:
    case CC_C:
    case CC_PreserveMost:
    case CC_Swift:
    case CC_X86Pascal:
    case CC_IntelOclBicc:
    case CC_OpenCLKernel:
      return CCCR_OK;
    case CC_SwiftAsync:
      return CCCR_Error;
    default:
      return CCCR_Warning;
    }
  }

  bool checkArithmeticFenceSupported() const override { return true; }

  CallingConv getDefaultCallingConv() const override {
    return CC_C;
  }

  bool hasSjLjLowering() const override { return true; }

  void setSupportedOpenCLOpts() override { supportAllOpenCLOpts(); }

  uint64_t getPointerWidthV(LangAS AS) const override {
    unsigned TargetAddrSpace = getTargetAddressSpace(AS);
    if (TargetAddrSpace == ptr32_sptr || TargetAddrSpace == ptr32_uptr)
      return 32;
    if (TargetAddrSpace == ptr64)
      return 64;
    return PointerWidth;
  }

  uint64_t getPointerAlignV(LangAS AddrSpace) const override {
    return getPointerWidthV(AddrSpace);
  }

};

// X86-32 generic target
class LLVM_LIBRARY_VISIBILITY X86_32TargetInfo : public X86TargetInfo {
public:
  X86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : X86TargetInfo(Triple, Opts) {
    DoubleAlign = LongLongAlign = 32;
    LongDoubleWidth = 96;
    LongDoubleAlign = 32;
    SuitableAlign = 128;
    resetDataLayout(Triple.isOSBinFormatMachO()
                        ? "e-m:o-p:32:32-p270:32:32-p271:32:32-p272:64:64-i128:"
                          "128-f64:32:64-f80:32-n8:16:32-S128"
                        : "e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i128:"
                          "128-f64:32:64-f80:32-n8:16:32-S128",
                    Triple.isOSBinFormatMachO() ? "_" : "");
    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;
    RegParmMax = 3;

    // Use fpret for all types.
    RealTypeUsesObjCFPRetMask =
        (unsigned)(FloatModeKind::Float | FloatModeKind::Double |
                   FloatModeKind::LongDouble);

    // x86-32 has atomics up to 8 bytes
    MaxAtomicPromoteWidth = 64;
    MaxAtomicInlineWidth = 32;
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    if (RegNo == 0)
      return 0;
    if (RegNo == 1)
      return 2;
    return -1;
  }

  bool validateOperandSize(const llvm::StringMap<bool> &FeatureMap,
                           StringRef Constraint, unsigned Size) const override {
    switch (Constraint[0]) {
    default:
      break;
    case 'R':
    case 'q':
    case 'Q':
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'S':
    case 'D':
      return Size <= 32;
    case 'A':
      return Size <= 64;
    }

    return X86TargetInfo::validateOperandSize(FeatureMap, Constraint, Size);
  }

  void setMaxAtomicWidth() override {
    if (hasFeature("cx8"))
      MaxAtomicInlineWidth = 64;
  }

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool hasBitIntType() const override { return true; }
  size_t getMaxBitIntWidth() const override {
    return llvm::IntegerType::MAX_INT_BITS;
  }
};

class LLVM_LIBRARY_VISIBILITY NetBSDI386TargetInfo
    : public NetBSDTargetInfo<X86_32TargetInfo> {
public:
  NetBSDI386TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : NetBSDTargetInfo<X86_32TargetInfo>(Triple, Opts) {}
};

class LLVM_LIBRARY_VISIBILITY OpenBSDI386TargetInfo
    : public OpenBSDTargetInfo<X86_32TargetInfo> {
public:
  OpenBSDI386TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OpenBSDTargetInfo<X86_32TargetInfo>(Triple, Opts) {
    SizeType = UnsignedLong;
    IntPtrType = SignedLong;
    PtrDiffType = SignedLong;
  }
};

class LLVM_LIBRARY_VISIBILITY DarwinI386TargetInfo
    : public DarwinTargetInfo<X86_32TargetInfo> {
public:
  DarwinI386TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : DarwinTargetInfo<X86_32TargetInfo>(Triple, Opts) {
    LongDoubleWidth = 128;
    LongDoubleAlign = 128;
    SuitableAlign = 128;
    MaxVectorAlign = 256;
    // The watchOS simulator uses the builtin bool type for Objective-C.
    llvm::Triple T = llvm::Triple(Triple);
    if (T.isWatchOS())
      UseSignedCharForObjCBool = false;
    SizeType = UnsignedLong;
    IntPtrType = SignedLong;
    resetDataLayout("e-m:o-p:32:32-p270:32:32-p271:32:32-p272:64:64-i128:128-"
                    "f64:32:64-f80:128-n8:16:32-S128",
                    "_");
    HasAlignMac68kSupport = true;
  }

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override {
    if (!DarwinTargetInfo<X86_32TargetInfo>::handleTargetFeatures(Features,
                                                                  Diags))
      return false;
    // We now know the features we have: we can decide how to align vectors.
    MaxVectorAlign =
        hasFeature("avx512f") ? 512 : hasFeature("avx") ? 256 : 128;
    return true;
  }
};

// x86-32 Windows target
class LLVM_LIBRARY_VISIBILITY WindowsX86_32TargetInfo
    : public WindowsTargetInfo<X86_32TargetInfo> {
public:
  WindowsX86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : WindowsTargetInfo<X86_32TargetInfo>(Triple, Opts) {
    DoubleAlign = LongLongAlign = 64;
    bool IsWinCOFF =
        getTriple().isOSWindows() && getTriple().isOSBinFormatCOFF();
    bool IsMSVC = getTriple().isWindowsMSVCEnvironment();
    std::string Layout = IsWinCOFF ? "e-m:x" : "e-m:e";
    Layout += "-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-";
    Layout += IsMSVC ? "f80:128" : "f80:32";
    Layout += "-n8:16:32-a:0:32-S32";
    resetDataLayout(Layout, IsWinCOFF ? "_" : "");
  }
};

// x86-32 Windows Visual Studio target
class LLVM_LIBRARY_VISIBILITY MicrosoftX86_32TargetInfo
    : public WindowsX86_32TargetInfo {
public:
  MicrosoftX86_32TargetInfo(const llvm::Triple &Triple,
                            const TargetOptions &Opts)
      : WindowsX86_32TargetInfo(Triple, Opts) {
    LongDoubleWidth = LongDoubleAlign = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    WindowsX86_32TargetInfo::getTargetDefines(Opts, Builder);
    // The value of the following reflects processor type.
    // 300=386, 400=486, 500=Pentium, 600=Blend (default)
    // We lost the original triple, so we use the default.
    Builder.defineMacro("_M_IX86", "600");
  }
};

// x86-32 MinGW target
class LLVM_LIBRARY_VISIBILITY MinGWX86_32TargetInfo
    : public WindowsX86_32TargetInfo {
public:
  MinGWX86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : WindowsX86_32TargetInfo(Triple, Opts) {
    HasFloat128 = true;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    WindowsX86_32TargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("_X86_");
  }
};

// x86-32 Cygwin target
class LLVM_LIBRARY_VISIBILITY CygwinX86_32TargetInfo : public X86_32TargetInfo {
public:
  CygwinX86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : X86_32TargetInfo(Triple, Opts) {
    this->WCharType = TargetInfo::UnsignedShort;
    DoubleAlign = LongLongAlign = 64;
    resetDataLayout("e-m:x-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-"
                    "i128:128-f80:32-n8:16:32-a:0:32-S32",
                    "_");
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    X86_32TargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("_X86_");
    Builder.defineMacro("__CYGWIN__");
    Builder.defineMacro("__CYGWIN32__");
    addCygMingDefines(Opts, Builder);
    DefineStd(Builder, "unix", Opts);
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");
  }
};

// x86-32 Haiku target
class LLVM_LIBRARY_VISIBILITY HaikuX86_32TargetInfo
    : public HaikuTargetInfo<X86_32TargetInfo> {
public:
  HaikuX86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : HaikuTargetInfo<X86_32TargetInfo>(Triple, Opts) {}

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    HaikuTargetInfo<X86_32TargetInfo>::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__INTEL__");
  }
};

// X86-32 MCU target
class LLVM_LIBRARY_VISIBILITY MCUX86_32TargetInfo : public X86_32TargetInfo {
public:
  MCUX86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : X86_32TargetInfo(Triple, Opts) {
    LongDoubleWidth = 64;
    DefaultAlignForAttributeAligned = 32;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    resetDataLayout("e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:32-"
                    "f64:32-f128:32-n8:16:32-a:0:32-S32");
    WIntType = UnsignedInt;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    // On MCU we support only C calling convention.
    return CC == CC_C ? CCCR_OK : CCCR_Warning;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    X86_32TargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__iamcu");
    Builder.defineMacro("__iamcu__");
  }

  bool allowsLargerPreferedTypeAlignment() const override { return false; }
};

// x86-32 RTEMS target
class LLVM_LIBRARY_VISIBILITY RTEMSX86_32TargetInfo : public X86_32TargetInfo {
public:
  RTEMSX86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : X86_32TargetInfo(Triple, Opts) {
    SizeType = UnsignedLong;
    IntPtrType = SignedLong;
    PtrDiffType = SignedLong;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    X86_32TargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__INTEL__");
    Builder.defineMacro("__rtems__");
  }
};

// x86-64 generic target
class LLVM_LIBRARY_VISIBILITY X86_64TargetInfo : public X86TargetInfo {
public:
  X86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : X86TargetInfo(Triple, Opts) {
    const bool IsX32 = getTriple().isX32();
    bool IsWinCOFF =
        getTriple().isOSWindows() && getTriple().isOSBinFormatCOFF();
    LongWidth = LongAlign = PointerWidth = PointerAlign = IsX32 ? 32 : 64;
    LongDoubleWidth = 128;
    LongDoubleAlign = 128;
    LargeArrayMinWidth = 128;
    LargeArrayAlign = 128;
    SuitableAlign = 128;
    SizeType = IsX32 ? UnsignedInt : UnsignedLong;
    PtrDiffType = IsX32 ? SignedInt : SignedLong;
    IntPtrType = IsX32 ? SignedInt : SignedLong;
    IntMaxType = IsX32 ? SignedLongLong : SignedLong;
    Int64Type = IsX32 ? SignedLongLong : SignedLong;
    RegParmMax = 6;

    // Pointers are 32-bit in x32.
    resetDataLayout(IsX32 ? "e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-"
                            "i64:64-i128:128-f80:128-n8:16:32:64-S128"
                    : IsWinCOFF ? "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:"
                                  "64-i128:128-f80:128-n8:16:32:64-S128"
                                : "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:"
                                  "64-i128:128-f80:128-n8:16:32:64-S128");

    // Use fpret only for long double.
    RealTypeUsesObjCFPRetMask = (unsigned)FloatModeKind::LongDouble;

    // Use fp2ret for _Complex long double.
    ComplexLongDoubleUsesFP2Ret = true;

    // Make __builtin_ms_va_list available.
    HasBuiltinMSVaList = true;

    // x86-64 has atomics up to 16 bytes.
    MaxAtomicPromoteWidth = 128;
    MaxAtomicInlineWidth = 64;
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::X86_64ABIBuiltinVaList;
  }

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    if (RegNo == 0)
      return 0;
    if (RegNo == 1)
      return 1;
    return -1;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    case CC_C:
    case CC_Swift:
    case CC_SwiftAsync:
    case CC_X86VectorCall:
    case CC_IntelOclBicc:
    case CC_Win64:
    case CC_PreserveMost:
    case CC_PreserveAll:
    case CC_PreserveNone:
    case CC_X86RegCall:
    case CC_OpenCLKernel:
      return CCCR_OK;
    default:
      return CCCR_Warning;
    }
  }

  CallingConv getDefaultCallingConv() const override {
    return CC_C;
  }

  // for x32 we need it here explicitly
  bool hasInt128Type() const override { return true; }

  unsigned getUnwindWordWidth() const override { return 64; }

  unsigned getRegisterWidth() const override { return 64; }

  bool validateGlobalRegisterVariable(StringRef RegName, unsigned RegSize,
                                      bool &HasSizeMismatch) const override {
    // rsp and rbp are the only 64-bit registers the x86 backend can currently
    // handle.
    if (RegName == "rsp" || RegName == "rbp") {
      // Check that the register size is 64-bit.
      HasSizeMismatch = RegSize != 64;
      return true;
    }

    // Check if the register is a 32-bit register the backend can handle.
    return X86TargetInfo::validateGlobalRegisterVariable(RegName, RegSize,
                                                         HasSizeMismatch);
  }

  void setMaxAtomicWidth() override {
    if (hasFeature("cx16"))
      MaxAtomicInlineWidth = 128;
  }

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool hasBitIntType() const override { return true; }
  size_t getMaxBitIntWidth() const override {
    return llvm::IntegerType::MAX_INT_BITS;
  }
};

// x86-64 Windows target
class LLVM_LIBRARY_VISIBILITY WindowsX86_64TargetInfo
    : public WindowsTargetInfo<X86_64TargetInfo> {
public:
  WindowsX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : WindowsTargetInfo<X86_64TargetInfo>(Triple, Opts) {
    LongWidth = LongAlign = 32;
    DoubleAlign = LongLongAlign = 64;
    IntMaxType = SignedLongLong;
    Int64Type = SignedLongLong;
    SizeType = UnsignedLongLong;
    PtrDiffType = SignedLongLong;
    IntPtrType = SignedLongLong;
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    case CC_X86StdCall:
    case CC_X86ThisCall:
    case CC_X86FastCall:
      return CCCR_Ignore;
    case CC_C:
    case CC_X86VectorCall:
    case CC_IntelOclBicc:
    case CC_PreserveMost:
    case CC_PreserveAll:
    case CC_PreserveNone:
    case CC_X86_64SysV:
    case CC_Swift:
    case CC_SwiftAsync:
    case CC_X86RegCall:
    case CC_OpenCLKernel:
      return CCCR_OK;
    default:
      return CCCR_Warning;
    }
  }
};

// x86-64 Windows Visual Studio target
class LLVM_LIBRARY_VISIBILITY MicrosoftX86_64TargetInfo
    : public WindowsX86_64TargetInfo {
public:
  MicrosoftX86_64TargetInfo(const llvm::Triple &Triple,
                            const TargetOptions &Opts)
      : WindowsX86_64TargetInfo(Triple, Opts) {
    LongDoubleWidth = LongDoubleAlign = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    WindowsX86_64TargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("_M_X64", "100");
    Builder.defineMacro("_M_AMD64", "100");
  }

  TargetInfo::CallingConvKind
  getCallingConvKind(bool ClangABICompat4) const override {
    return CCK_MicrosoftWin64;
  }
};

// x86-64 MinGW target
class LLVM_LIBRARY_VISIBILITY MinGWX86_64TargetInfo
    : public WindowsX86_64TargetInfo {
public:
  MinGWX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : WindowsX86_64TargetInfo(Triple, Opts) {
    // Mingw64 rounds long double size and alignment up to 16 bytes, but sticks
    // with x86 FP ops. Weird.
    LongDoubleWidth = LongDoubleAlign = 128;
    LongDoubleFormat = &llvm::APFloat::x87DoubleExtended();
    HasFloat128 = true;
  }
};

// x86-64 Cygwin target
class LLVM_LIBRARY_VISIBILITY CygwinX86_64TargetInfo : public X86_64TargetInfo {
public:
  CygwinX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : X86_64TargetInfo(Triple, Opts) {
    this->WCharType = TargetInfo::UnsignedShort;
    TLSSupported = false;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    X86_64TargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__x86_64__");
    Builder.defineMacro("__CYGWIN__");
    Builder.defineMacro("__CYGWIN64__");
    addCygMingDefines(Opts, Builder);
    DefineStd(Builder, "unix", Opts);
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");
  }
};

class LLVM_LIBRARY_VISIBILITY DarwinX86_64TargetInfo
    : public DarwinTargetInfo<X86_64TargetInfo> {
public:
  DarwinX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : DarwinTargetInfo<X86_64TargetInfo>(Triple, Opts) {
    Int64Type = SignedLongLong;
    // The 64-bit iOS simulator uses the builtin bool type for Objective-C.
    llvm::Triple T = llvm::Triple(Triple);
    if (T.isiOS())
      UseSignedCharForObjCBool = false;
    resetDataLayout("e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
                    "f80:128-n8:16:32:64-S128",
                    "_");
  }

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override {
    if (!DarwinTargetInfo<X86_64TargetInfo>::handleTargetFeatures(Features,
                                                                  Diags))
      return false;
    // We now know the features we have: we can decide how to align vectors.
    MaxVectorAlign =
        hasFeature("avx512f") ? 512 : hasFeature("avx") ? 256 : 128;
    return true;
  }
};

class LLVM_LIBRARY_VISIBILITY OpenBSDX86_64TargetInfo
    : public OpenBSDTargetInfo<X86_64TargetInfo> {
public:
  OpenBSDX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OpenBSDTargetInfo<X86_64TargetInfo>(Triple, Opts) {
    IntMaxType = SignedLongLong;
    Int64Type = SignedLongLong;
  }
};

// x86_32 Android target
class LLVM_LIBRARY_VISIBILITY AndroidX86_32TargetInfo
    : public LinuxTargetInfo<X86_32TargetInfo> {
public:
  AndroidX86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : LinuxTargetInfo<X86_32TargetInfo>(Triple, Opts) {
    SuitableAlign = 32;
    LongDoubleWidth = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }
};

// x86_64 Android target
class LLVM_LIBRARY_VISIBILITY AndroidX86_64TargetInfo
    : public LinuxTargetInfo<X86_64TargetInfo> {
public:
  AndroidX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : LinuxTargetInfo<X86_64TargetInfo>(Triple, Opts) {
    LongDoubleFormat = &llvm::APFloat::IEEEquad();
  }
};

// x86_32 OHOS target
class LLVM_LIBRARY_VISIBILITY OHOSX86_32TargetInfo
    : public OHOSTargetInfo<X86_32TargetInfo> {
public:
  OHOSX86_32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OHOSTargetInfo<X86_32TargetInfo>(Triple, Opts) {
    SuitableAlign = 32;
    LongDoubleWidth = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  }
};

// x86_64 OHOS target
class LLVM_LIBRARY_VISIBILITY OHOSX86_64TargetInfo
    : public OHOSTargetInfo<X86_64TargetInfo> {
public:
  OHOSX86_64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OHOSTargetInfo<X86_64TargetInfo>(Triple, Opts) {
    LongDoubleFormat = &llvm::APFloat::IEEEquad();
  }
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_X86_H
