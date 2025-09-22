//===--- PNaCl.cpp - Implement PNaCl target feature support ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements PNaCl TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "PNaCl.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

ArrayRef<const char *> PNaClTargetInfo::getGCCRegNames() const {
  return std::nullopt;
}

ArrayRef<TargetInfo::GCCRegAlias> PNaClTargetInfo::getGCCRegAliases() const {
  return std::nullopt;
}

void PNaClTargetInfo::getArchDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Builder.defineMacro("__le32__");
  Builder.defineMacro("__pnacl__");
}
