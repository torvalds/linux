//===--- DirectX.cpp - Implement DirectX target feature support -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements DirectX TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "DirectX.h"
#include "Targets.h"

using namespace clang;
using namespace clang::targets;

void DirectXTargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  DefineStd(Builder, "DIRECTX", Opts);
}
