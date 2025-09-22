//===- OpenMP/OMPAssume.h --- OpenMP assumption helper functions  - C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides helper functions and classes to deal with OpenMP
/// assumptions, e.g., as used by `[begin/end] assumes` and `assume`.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMPASSUME_H
#define LLVM_FRONTEND_OPENMP_OMPASSUME_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

namespace omp {

/// Helper to describe assume clauses.
struct AssumptionClauseMappingInfo {
  /// The identifier describing the (beginning of the) clause.
  llvm::StringLiteral Identifier;
  /// Flag to determine if the identifier is a full name or the start of a name.
  bool StartsWith;
  /// Flag to determine if a directive lists follows.
  bool HasDirectiveList;
  /// Flag to determine if an expression follows.
  bool HasExpression;
};

/// All known assume clauses.
static constexpr AssumptionClauseMappingInfo AssumptionClauseMappings[] = {
#define OMP_ASSUME_CLAUSE(Identifier, StartsWith, HasDirectiveList,            \
                          HasExpression)                                       \
  {Identifier, StartsWith, HasDirectiveList, HasExpression},
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

inline std::string getAllAssumeClauseOptions() {
  std::string S;
  for (const AssumptionClauseMappingInfo &ACMI : AssumptionClauseMappings)
    S += (S.empty() ? "'" : "', '") + ACMI.Identifier.str();
  return S + "'";
}

} // namespace omp

} // namespace llvm

#endif // LLVM_FRONTEND_OPENMP_OMPASSUME_H
