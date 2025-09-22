//===-- LoongArch.h - Declare LoongArch target feature support --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares LoongArch TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_LOONGARCH_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_LOONGARCH_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY LoongArchTargetInfo : public TargetInfo {
protected:
  std::string ABI;
  std::string CPU;
  bool HasFeatureD;
  bool HasFeatureF;
  bool HasFeatureLSX;
  bool HasFeatureLASX;
  bool HasFeatureFrecipe;

public:
  LoongArchTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    HasFeatureD = false;
    HasFeatureF = false;
    HasFeatureLSX = false;
    HasFeatureLASX = false;
    HasFeatureFrecipe = false;
    LongDoubleWidth = 128;
    LongDoubleAlign = 128;
    LongDoubleFormat = &llvm::APFloat::IEEEquad();
    MCountName = "_mcount";
    SuitableAlign = 128;
    WCharType = SignedInt;
    WIntType = UnsignedInt;
  }

  bool setCPU(const std::string &Name) override {
    if (!isValidCPUName(Name))
      return false;
    CPU = Name;
    return true;
  }

  StringRef getCPU() const { return CPU; }

  StringRef getABI() const override { return ABI; }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  std::string_view getClobbers() const override { return ""; }

  ArrayRef<const char *> getGCCRegNames() const override;

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    if (RegNo == 0)
      return 4;
    if (RegNo == 1)
      return 5;
    return -1;
  }

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;
  std::string convertConstraint(const char *&Constraint) const override;

  bool hasBitIntType() const override { return true; }

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;

  bool hasFeature(StringRef Feature) const override;

  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;
};

class LLVM_LIBRARY_VISIBILITY LoongArch32TargetInfo
    : public LoongArchTargetInfo {
public:
  LoongArch32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : LoongArchTargetInfo(Triple, Opts) {
    IntPtrType = SignedInt;
    PtrDiffType = SignedInt;
    SizeType = UnsignedInt;
    resetDataLayout("e-m:e-p:32:32-i64:64-n32-S128");
    // TODO: select appropriate ABI.
    setABI("ilp32d");
  }

  bool setABI(const std::string &Name) override {
    if (Name == "ilp32d" || Name == "ilp32f" || Name == "ilp32s") {
      ABI = Name;
      return true;
    }
    return false;
  }
  void setMaxAtomicWidth() override {
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 32;
  }
};

class LLVM_LIBRARY_VISIBILITY LoongArch64TargetInfo
    : public LoongArchTargetInfo {
public:
  LoongArch64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : LoongArchTargetInfo(Triple, Opts) {
    LongWidth = LongAlign = PointerWidth = PointerAlign = 64;
    IntMaxType = Int64Type = SignedLong;
    HasUnalignedAccess = true;
    resetDataLayout("e-m:e-p:64:64-i64:64-i128:128-n32:64-S128");
    // TODO: select appropriate ABI.
    setABI("lp64d");
  }

  bool setABI(const std::string &Name) override {
    if (Name == "lp64d" || Name == "lp64f" || Name == "lp64s") {
      ABI = Name;
      return true;
    }
    return false;
  }
  void setMaxAtomicWidth() override {
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;
  }
};
} // end namespace targets
} // end namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_LOONGARCH_H
