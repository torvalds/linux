//===----- SemaMSP430.h --- MSP430 target-specific routines ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to MSP430.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAMSP430_H
#define LLVM_CLANG_SEMA_SEMAMSP430_H

#include "clang/Sema/SemaBase.h"

namespace clang {
class Decl;
class ParsedAttr;

class SemaMSP430 : public SemaBase {
public:
  SemaMSP430(Sema &S);

  void handleInterruptAttr(Decl *D, const ParsedAttr &AL);
};
} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAMSP430_H
