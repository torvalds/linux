//===- DynamicCastInfo.h - Runtime cast information -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICCASTINFO_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICCASTINFO_H

#include "clang/AST/Type.h"

namespace clang {
namespace ento {

class DynamicCastInfo {
public:
  enum CastResult { Success, Failure };

  DynamicCastInfo(QualType from, QualType to, CastResult resultKind)
      : From(from), To(to), ResultKind(resultKind) {}

  QualType from() const { return From; }
  QualType to() const { return To; }

  bool equals(QualType from, QualType to) const {
    return From == from && To == to;
  }

  bool succeeds() const { return ResultKind == CastResult::Success; }
  bool fails() const { return ResultKind == CastResult::Failure; }

  bool operator==(const DynamicCastInfo &RHS) const {
    return From == RHS.From && To == RHS.To;
  }
  bool operator<(const DynamicCastInfo &RHS) const {
    return From < RHS.From && To < RHS.To;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.Add(From);
    ID.Add(To);
    ID.AddInteger(ResultKind);
  }

private:
  QualType From, To;
  CastResult ResultKind;
};

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICCASTINFO_H
