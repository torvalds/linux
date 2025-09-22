//===--- SystemZ.h - Declare SystemZ target feature support -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares SystemZ TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_SYSTEMZ_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_SYSTEMZ_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY SystemZTargetInfo : public TargetInfo {

  static const char *const GCCRegNames[];
  std::string CPU;
  int ISARevision;
  bool HasTransactionalExecution;
  bool HasVector;
  bool SoftFloat;
  bool UnalignedSymbols;

public:
  SystemZTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple), CPU("z10"), ISARevision(8),
        HasTransactionalExecution(false), HasVector(false), SoftFloat(false),
        UnalignedSymbols(false) {
    IntMaxType = SignedLong;
    Int64Type = SignedLong;
    IntWidth = IntAlign = 32;
    LongWidth = LongLongWidth = LongAlign = LongLongAlign = 64;
    Int128Align = 64;
    PointerWidth = PointerAlign = 64;
    LongDoubleWidth = 128;
    LongDoubleAlign = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEquad();
    DefaultAlignForAttributeAligned = 64;
    MinGlobalAlign = 16;
    HasUnalignedAccess = true;
    if (Triple.isOSzOS()) {
      TLSSupported = false;
      // All vector types are default aligned on an 8-byte boundary, even if the
      // vector facility is not available. That is different from Linux.
      MaxVectorAlign = 64;
      // Compared to Linux/ELF, the data layout differs only in some details:
      // - name mangling is GOFF.
      // - 32 bit pointers, either as default or special address space
      resetDataLayout("E-m:l-i1:8:16-i8:8:16-i64:64-f128:64-v128:64-"
                      "a:8:16-n32:64");
    } else {
      TLSSupported = true;
      resetDataLayout("E-m:e-i1:8:16-i8:8:16-i64:64-f128:64"
                      "-v128:64-a:8:16-n32:64");
    }
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 128;
    HasStrictFP = true;
  }

  unsigned getMinGlobalAlign(uint64_t Size, bool HasNonWeakDef) const override;

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    // No aliases.
    return std::nullopt;
  }

  ArrayRef<TargetInfo::AddlRegName> getGCCAddlRegNames() const override;

  bool isSPRegName(StringRef RegName) const override {
    return RegName == "r15";
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override;

  std::string convertConstraint(const char *&Constraint) const override {
    switch (Constraint[0]) {
    case 'p': // Keep 'p' constraint.
      return std::string("p");
    case 'Z':
      switch (Constraint[1]) {
      case 'Q': // Address with base and unsigned 12-bit displacement
      case 'R': // Likewise, plus an index
      case 'S': // Address with base and signed 20-bit displacement
      case 'T': // Likewise, plus an index
        // "^" hints llvm that this is a 2 letter constraint.
        // "Constraint++" is used to promote the string iterator
        // to the next constraint.
        return std::string("^") + std::string(Constraint++, 2);
      default:
        break;
      }
      break;
    default:
      break;
    }
    return TargetInfo::convertConstraint(Constraint);
  }

  std::string_view getClobbers() const override {
    // FIXME: Is this really right?
    return "";
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::SystemZBuiltinVaList;
  }

  int getISARevision(StringRef Name) const;

  bool isValidCPUName(StringRef Name) const override {
    return getISARevision(Name) != -1;
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool isValidTuneCPUName(StringRef Name) const override {
    return isValidCPUName(Name);
  }

  void fillValidTuneCPUList(SmallVectorImpl<StringRef> &Values) const override {
    fillValidCPUList(Values);
  }

  bool setCPU(const std::string &Name) override {
    CPU = Name;
    ISARevision = getISARevision(CPU);
    return ISARevision != -1;
  }

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override {
    int ISARevision = getISARevision(CPU);
    if (ISARevision >= 10)
      Features["transactional-execution"] = true;
    if (ISARevision >= 11)
      Features["vector"] = true;
    if (ISARevision >= 12)
      Features["vector-enhancements-1"] = true;
    if (ISARevision >= 13)
      Features["vector-enhancements-2"] = true;
    if (ISARevision >= 14)
      Features["nnp-assist"] = true;
    return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
  }

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override {
    HasTransactionalExecution = false;
    HasVector = false;
    SoftFloat = false;
    UnalignedSymbols = false;
    for (const auto &Feature : Features) {
      if (Feature == "+transactional-execution")
        HasTransactionalExecution = true;
      else if (Feature == "+vector")
        HasVector = true;
      else if (Feature == "+soft-float")
        SoftFloat = true;
      else if (Feature == "+unaligned-symbols")
        UnalignedSymbols = true;
    }
    HasVector &= !SoftFloat;

    // If we use the vector ABI, vector types are 64-bit aligned. The
    // DataLayout string is always set to this alignment as it is not a
    // requirement that it follows the alignment emitted by the front end. It
    // is assumed generally that the Datalayout should reflect only the
    // target triple and not any specific feature.
    if (HasVector && !getTriple().isOSzOS())
      MaxVectorAlign = 64;

    return true;
  }

  bool hasFeature(StringRef Feature) const override;

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    case CC_C:
    case CC_Swift:
    case CC_OpenCLKernel:
      return CCCR_OK;
    case CC_SwiftAsync:
      return CCCR_Error;
    default:
      return CCCR_Warning;
    }
  }

  StringRef getABI() const override {
    if (HasVector)
      return "vector";
    return "";
  }

  const char *getLongDoubleMangling() const override { return "g"; }

  bool hasBitIntType() const override { return true; }

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    return RegNo < 4 ? 6 + RegNo : -1;
  }

  std::pair<unsigned, unsigned> hardwareInterferenceSizes() const override {
    return std::make_pair(256, 256);
  }
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_SYSTEMZ_H
