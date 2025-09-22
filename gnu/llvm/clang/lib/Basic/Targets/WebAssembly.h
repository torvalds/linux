//=== WebAssembly.h - Declare WebAssembly target feature support *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares WebAssembly TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_WEBASSEMBLY_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_WEBASSEMBLY_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

static const unsigned WebAssemblyAddrSpaceMap[] = {
    0, // Default
    0, // opencl_global
    0, // opencl_local
    0, // opencl_constant
    0, // opencl_private
    0, // opencl_generic
    0, // opencl_global_device
    0, // opencl_global_host
    0, // cuda_device
    0, // cuda_constant
    0, // cuda_shared
    0, // sycl_global
    0, // sycl_global_device
    0, // sycl_global_host
    0, // sycl_local
    0, // sycl_private
    0, // ptr32_sptr
    0, // ptr32_uptr
    0, // ptr64
    0, // hlsl_groupshared
    20, // wasm_funcref
};

class LLVM_LIBRARY_VISIBILITY WebAssemblyTargetInfo : public TargetInfo {

  enum SIMDEnum {
    NoSIMD,
    SIMD128,
    RelaxedSIMD,
  } SIMDLevel = NoSIMD;

  bool HasAtomics = false;
  bool HasBulkMemory = false;
  bool HasExceptionHandling = false;
  bool HasExtendedConst = false;
  bool HasHalfPrecision = false;
  bool HasMultiMemory = false;
  bool HasMultivalue = false;
  bool HasMutableGlobals = false;
  bool HasNontrappingFPToInt = false;
  bool HasReferenceTypes = false;
  bool HasSignExt = false;
  bool HasTailCall = false;

  std::string ABI;

public:
  explicit WebAssemblyTargetInfo(const llvm::Triple &T, const TargetOptions &)
      : TargetInfo(T) {
    AddrSpaceMap = &WebAssemblyAddrSpaceMap;
    NoAsmVariants = true;
    SuitableAlign = 128;
    LargeArrayMinWidth = 128;
    LargeArrayAlign = 128;
    SigAtomicType = SignedLong;
    LongDoubleWidth = LongDoubleAlign = 128;
    LongDoubleFormat = &llvm::APFloat::IEEEquad();
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;
    // size_t being unsigned long for both wasm32 and wasm64 makes mangled names
    // more consistent between the two.
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    HasUnalignedAccess = true;
  }

  StringRef getABI() const override;
  bool setABI(const std::string &Name) override;
  bool useFP16ConversionIntrinsics() const override {
    return !HasHalfPrecision;
  }

protected:
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

private:
  static void setSIMDLevel(llvm::StringMap<bool> &Features, SIMDEnum Level,
                           bool Enabled);

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;
  bool hasFeature(StringRef Feature) const final;

  void setFeatureEnabled(llvm::StringMap<bool> &Features, StringRef Name,
                         bool Enabled) const final;

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) final;

  bool isValidCPUName(StringRef Name) const final;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const final;

  bool setCPU(const std::string &Name) final { return isValidCPUName(Name); }

  ArrayRef<Builtin::Info> getTargetBuiltins() const final;

  BuiltinVaListKind getBuiltinVaListKind() const final {
    return VoidPtrBuiltinVaList;
  }

  ArrayRef<const char *> getGCCRegNames() const final { return std::nullopt; }

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const final {
    return std::nullopt;
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const final {
    return false;
  }

  std::string_view getClobbers() const final { return ""; }

  bool isCLZForZeroUndef() const final { return false; }

  bool hasInt128Type() const final { return true; }

  IntType getIntTypeByWidth(unsigned BitWidth, bool IsSigned) const final {
    // WebAssembly prefers long long for explicitly 64-bit integers.
    return BitWidth == 64 ? (IsSigned ? SignedLongLong : UnsignedLongLong)
                          : TargetInfo::getIntTypeByWidth(BitWidth, IsSigned);
  }

  IntType getLeastIntTypeByWidth(unsigned BitWidth, bool IsSigned) const final {
    // WebAssembly uses long long for int_least64_t and int_fast64_t.
    return BitWidth == 64
               ? (IsSigned ? SignedLongLong : UnsignedLongLong)
               : TargetInfo::getLeastIntTypeByWidth(BitWidth, IsSigned);
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    case CC_C:
    case CC_Swift:
      return CCCR_OK;
    case CC_SwiftAsync:
      return CCCR_Error;
    default:
      return CCCR_Warning;
    }
  }

  bool hasBitIntType() const override { return true; }

  bool hasProtectedVisibility() const override { return false; }

  void adjust(DiagnosticsEngine &Diags, LangOptions &Opts) override;
};

class LLVM_LIBRARY_VISIBILITY WebAssembly32TargetInfo
    : public WebAssemblyTargetInfo {
public:
  explicit WebAssembly32TargetInfo(const llvm::Triple &T,
                                   const TargetOptions &Opts)
      : WebAssemblyTargetInfo(T, Opts) {
    if (T.isOSEmscripten())
      resetDataLayout("e-m:e-p:32:32-p10:8:8-p20:8:8-i64:64-f128:64-n32:64-"
                      "S128-ni:1:10:20");
    else
      resetDataLayout(
          "e-m:e-p:32:32-p10:8:8-p20:8:8-i64:64-n32:64-S128-ni:1:10:20");
  }

protected:
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY WebAssembly64TargetInfo
    : public WebAssemblyTargetInfo {
public:
  explicit WebAssembly64TargetInfo(const llvm::Triple &T,
                                   const TargetOptions &Opts)
      : WebAssemblyTargetInfo(T, Opts) {
    LongAlign = LongWidth = 64;
    PointerAlign = PointerWidth = 64;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    if (T.isOSEmscripten())
      resetDataLayout("e-m:e-p:64:64-p10:8:8-p20:8:8-i64:64-f128:64-n32:64-"
                      "S128-ni:1:10:20");
    else
      resetDataLayout(
          "e-m:e-p:64:64-p10:8:8-p20:8:8-i64:64-n32:64-S128-ni:1:10:20");
  }

protected:
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_WEBASSEMBLY_H
