//===-- SimplifyConstraints.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_SIMPLIFYCONSTRAINTS_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_SIMPLIFYCONSTRAINTS_H

#include "clang/Analysis/FlowSensitive/Arena.h"
#include "clang/Analysis/FlowSensitive/Formula.h"
#include "llvm/ADT/SetVector.h"

namespace clang {
namespace dataflow {

/// Information on the way a set of constraints was simplified.
struct SimplifyConstraintsInfo {
  /// List of equivalence classes of atoms. For each equivalence class, the
  /// original constraints imply that all atoms in it must be equivalent.
  /// Simplification replaces all occurrences of atoms in an equivalence class
  /// with a single representative atom from the class.
  /// Does not contain equivalence classes with just one member or atoms
  /// contained in `TrueAtoms` or `FalseAtoms`.
  llvm::SmallVector<llvm::SmallVector<Atom>> EquivalentAtoms;
  /// Atoms that the original constraints imply must be true.
  /// Simplification replaces all occurrences of these atoms by a true literal
  /// (which may enable additional simplifications).
  llvm::SmallVector<Atom> TrueAtoms;
  /// Atoms that the original constraints imply must be false.
  /// Simplification replaces all occurrences of these atoms by a false literal
  /// (which may enable additional simplifications).
  llvm::SmallVector<Atom> FalseAtoms;
};

/// Simplifies a set of constraints (implicitly connected by "and") in a way
/// that does not change satisfiability of the constraints. This does _not_ mean
/// that the set of solutions is the same before and after simplification.
/// `Info`, if non-null, will be populated with information about the
/// simplifications that were made to the formula (e.g. to display to the user).
void simplifyConstraints(llvm::SetVector<const Formula *> &Constraints,
                         Arena &arena, SimplifyConstraintsInfo *Info = nullptr);

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_SIMPLIFYCONSTRAINTS_H
