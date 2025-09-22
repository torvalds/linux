//===- OptionalDiagnostic.h - An optional diagnostic ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implements a partial diagnostic which may not be emitted.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_OPTIONALDIAGNOSTIC_H
#define LLVM_CLANG_AST_OPTIONALDIAGNOSTIC_H

#include "clang/AST/APValue.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace clang {

/// A partial diagnostic which we might know in advance that we are not going
/// to emit.
class OptionalDiagnostic {
  PartialDiagnostic *Diag;

public:
  explicit OptionalDiagnostic(PartialDiagnostic *Diag = nullptr) : Diag(Diag) {}

  template <typename T> OptionalDiagnostic &operator<<(const T &v) {
    if (Diag)
      *Diag << v;
    return *this;
  }

  OptionalDiagnostic &operator<<(const llvm::APSInt &I) {
    if (Diag) {
      SmallVector<char, 32> Buffer;
      I.toString(Buffer);
      *Diag << StringRef(Buffer.data(), Buffer.size());
    }
    return *this;
  }

  OptionalDiagnostic &operator<<(const llvm::APFloat &F) {
    if (Diag) {
      // FIXME: Force the precision of the source value down so we don't
      // print digits which are usually useless (we don't really care here if
      // we truncate a digit by accident in edge cases).  Ideally,
      // APFloat::toString would automatically print the shortest
      // representation which rounds to the correct value, but it's a bit
      // tricky to implement. Could use std::to_chars.
      unsigned precision = llvm::APFloat::semanticsPrecision(F.getSemantics());
      precision = (precision * 59 + 195) / 196;
      SmallVector<char, 32> Buffer;
      F.toString(Buffer, precision);
      *Diag << StringRef(Buffer.data(), Buffer.size());
    }
    return *this;
  }

  OptionalDiagnostic &operator<<(const llvm::APFixedPoint &FX) {
    if (Diag) {
      SmallVector<char, 32> Buffer;
      FX.toString(Buffer);
      *Diag << StringRef(Buffer.data(), Buffer.size());
    }
    return *this;
  }
};

} // namespace clang

#endif
