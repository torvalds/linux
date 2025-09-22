//===--- PPDirectiveParameter.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the base class for preprocessor directive parameters, such
// as limit(1) or suffix(x) for #embed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PPDIRECTIVEPARAMETER_H
#define LLVM_CLANG_LEX_PPDIRECTIVEPARAMETER_H

#include "clang/Basic/SourceLocation.h"

namespace clang {

/// Captures basic information about a preprocessor directive parameter.
class PPDirectiveParameter {
  SourceRange R;

public:
  PPDirectiveParameter(SourceRange R) : R(R) {}

  SourceRange getParameterRange() const { return R; }
};

} // end namespace clang

#endif
