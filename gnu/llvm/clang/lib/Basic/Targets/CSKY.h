//===--- CSKY.h - Declare CSKY target feature support -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares CSKY TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_CSKY_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_CSKY_H

#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/TargetParser/CSKYTargetParser.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY CSKYTargetInfo : public TargetInfo {
protected:
  std::string ABI;
  llvm::CSKY::ArchKind Arch = llvm::CSKY::ArchKind::INVALID;
  std::string CPU;

  bool HardFloat = false;
  bool HardFloatABI = false;
  bool FPUV2_SF = false;
  bool FPUV2_DF = false;
  bool FPUV3_SF = false;
  bool FPUV3_DF = false;
  bool VDSPV2 = false;
  bool VDSPV1 = false;
  bool DSPV2 = false;
  bool is3E3R1 = false;

public:
  CSKYTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : TargetInfo(Triple) {
    NoAsmVariants = true;
    LongLongAlign = 32;
    SuitableAlign = 32;
    DoubleAlign = LongDoubleAlign = 32;
    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;
    WCharType = SignedInt;
    WIntType = UnsignedInt;

    UseZeroLengthBitfieldAlignment = true;
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 32;
    resetDataLayout("e-m:e-S32-p:32:32-i32:32:32-i64:32:32-f32:32:32-f64:32:32-"
                    "v64:32:32-v128:32:32-a:0:32-Fi32-n32");

    setABI("abiv2");
  }

  StringRef getABI() const override { return ABI; }
  bool setABI(const std::string &Name) override {
    if (Name == "abiv2" || Name == "abiv1") {
      ABI = Name;
      return true;
    }
    return false;
  }

  bool setCPU(const std::string &Name) override;

  bool isValidCPUName(StringRef Name) const override;

  unsigned getMinGlobalAlign(uint64_t, bool HasNonWeakDef) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return VoidPtrBuiltinVaList;
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override;

  std::string_view getClobbers() const override { return ""; }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
  bool hasFeature(StringRef Feature) const override;
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  /// Whether target allows to overalign ABI-specified preferred alignment
  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  bool hasBitIntType() const override { return true; }

protected:
  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<GCCRegAlias> getGCCRegAliases() const override;
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_CSKY_H
