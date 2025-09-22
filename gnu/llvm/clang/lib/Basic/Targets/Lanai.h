//===--- Lanai.h - Declare Lanai target feature support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares Lanai TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_LANAI_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_LANAI_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY LanaiTargetInfo : public TargetInfo {
  // Class for Lanai (32-bit).
  // The CPU profiles supported by the Lanai backend
  enum CPUKind {
    CK_NONE,
    CK_V11,
  } CPU;

  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  static const char *const GCCRegNames[];

public:
  LanaiTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // Description string has to be kept in sync with backend.
    resetDataLayout("E"        // Big endian
                    "-m:e"     // ELF name manging
                    "-p:32:32" // 32 bit pointers, 32 bit aligned
                    "-i64:64"  // 64 bit integers, 64 bit aligned
                    "-a:0:32"  // 32 bit alignment of objects of aggregate type
                    "-n32"     // 32 bit native integer width
                    "-S64"     // 64 bit natural stack alignment
    );

    // Setting RegParmMax equal to what mregparm was set to in the old
    // toolchain
    RegParmMax = 4;

    // Set the default CPU to V11
    CPU = CK_V11;

    // Temporary approach to make everything at least word-aligned and allow for
    // safely casting between pointers with different alignment requirements.
    // TODO: Remove this when there are no more cast align warnings on the
    // firmware.
    MinGlobalAlign = 32;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool isValidCPUName(StringRef Name) const override;

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override;

  bool hasFeature(StringRef Feature) const override;

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  ArrayRef<Builtin::Info> getTargetBuiltins() const override {
    return std::nullopt;
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override {
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  bool hasBitIntType() const override { return true; }
};
} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_LANAI_H
