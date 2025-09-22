//===--- Hexagon.h - Declare Hexagon target feature support -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares Hexagon TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_HEXAGON_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_HEXAGON_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

namespace clang {
namespace targets {

// Hexagon abstract base class
class LLVM_LIBRARY_VISIBILITY HexagonTargetInfo : public TargetInfo {

  static const char *const GCCRegNames[];
  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  std::string CPU;
  std::string HVXVersion;
  bool HasHVX = false;
  bool HasHVX64B = false;
  bool HasHVX128B = false;
  bool HasAudio = false;
  bool UseLongCalls = false;

public:
  HexagonTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // Specify the vector alignment explicitly. For v512x1, the calculated
    // alignment would be 512*alignment(i1), which is 512 bytes, instead of
    // the required minimum of 64 bytes.
    resetDataLayout(
        "e-m:e-p:32:32:32-a:0-n16:32-"
        "i64:64:64-i32:32:32-i16:16:16-i1:8:8-f32:32:32-f64:64:64-"
        "v32:32:32-v64:64:64-v512:512:512-v1024:1024:1024-v2048:2048:2048");
    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;

    // {} in inline assembly are packet specifiers, not assembly variant
    // specifiers.
    NoAsmVariants = true;

    LargeArrayMinWidth = 64;
    LargeArrayAlign = 64;
    UseBitFieldTypeAlignment = true;
    ZeroLengthBitfieldBoundary = 32;
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;

    // These are the default values anyway, but explicitly make sure
    // that the size of the boolean type is 8 bits. Bool vectors are used
    // for modeling predicate registers in HVX, and the bool -> byte
    // correspondence matches the HVX architecture.
    BoolWidth = BoolAlign = 8;
  }

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    switch (*Name) {
    case 'v':
    case 'q':
      if (HasHVX) {
        Info.setAllowsRegister();
        return true;
      }
      break;
    case 'a': // Modifier register m0-m1.
      Info.setAllowsRegister();
      return true;
    case 's':
      // Relocatable constant.
      return true;
    }
    return false;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool isCLZForZeroUndef() const override { return false; }

  bool hasFeature(StringRef Feature) const override;

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    if (getTriple().isMusl())
      return TargetInfo::HexagonBuiltinVaList;
    return TargetInfo::CharPtrBuiltinVaList;
  }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  std::string_view getClobbers() const override { return ""; }

  static const char *getHexagonCPUSuffix(StringRef Name);
  static std::optional<unsigned> getHexagonCPURev(StringRef Name);

  bool isValidCPUName(StringRef Name) const override {
    return getHexagonCPUSuffix(Name);
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override {
    if (!isValidCPUName(Name))
      return false;
    CPU = Name;
    return true;
  }

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    return RegNo < 2 ? RegNo : -1;
  }

  bool isTinyCore() const {
    // We can write more stricter checks later.
    return CPU.find('t') != std::string::npos;
  }

  bool hasBitIntType() const override { return true; }

  std::pair<unsigned, unsigned> hardwareInterferenceSizes() const override {
    std::optional<unsigned> Rev = getHexagonCPURev(CPU);

    // V73 and later have 64-byte cache lines.
    unsigned CacheLineSizeBytes = Rev >= 73U ? 64 : 32;
    return std::make_pair(CacheLineSizeBytes, CacheLineSizeBytes);
  }
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_HEXAGON_H
