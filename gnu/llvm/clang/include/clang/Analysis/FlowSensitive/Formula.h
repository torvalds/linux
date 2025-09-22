//===- Formula.h - Boolean formulas -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_FORMULA_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_FORMULA_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>

namespace clang::dataflow {

/// Identifies an atomic boolean variable such as "V1".
///
/// This often represents an assertion that is interesting to the analysis but
/// cannot immediately be proven true or false. For example:
/// - V1 may mean "the program reaches this point",
/// - V2 may mean "the parameter was null"
///
/// We can use these variables in formulas to describe relationships we know
/// to be true: "if the parameter was null, the program reaches this point".
/// We also express hypotheses as formulas, and use a SAT solver to check
/// whether they are consistent with the known facts.
enum class Atom : unsigned {};

/// A boolean expression such as "true" or "V1 & !V2".
/// Expressions may refer to boolean atomic variables. These should take a
/// consistent true/false value across the set of formulas being considered.
///
/// (Formulas are always expressions in terms of boolean variables rather than
/// e.g. integers because our underlying model is SAT rather than e.g. SMT).
///
/// Simple formulas such as "true" and "V1" are self-contained.
/// Compound formulas connect other formulas, e.g. "(V1 & V2) || V3" is an 'or'
/// formula, with pointers to its operands "(V1 & V2)" and "V3" stored as
/// trailing objects.
/// For this reason, Formulas are Arena-allocated and over-aligned.
class Formula;
class alignas(const Formula *) Formula {
public:
  enum Kind : unsigned {
    /// A reference to an atomic boolean variable.
    /// We name these e.g. "V3", where 3 == atom identity == Value.
    AtomRef,
    /// Constant true or false.
    Literal,

    Not, /// True if its only operand is false

    // These kinds connect two operands LHS and RHS
    And,     /// True if LHS and RHS are both true
    Or,      /// True if either LHS or RHS is true
    Implies, /// True if LHS is false or RHS is true
    Equal,   /// True if LHS and RHS have the same truth value
  };
  Kind kind() const { return FormulaKind; }

  Atom getAtom() const {
    assert(kind() == AtomRef);
    return static_cast<Atom>(Value);
  }

  bool literal() const {
    assert(kind() == Literal);
    return static_cast<bool>(Value);
  }

  bool isLiteral(bool b) const {
    return kind() == Literal && static_cast<bool>(Value) == b;
  }

  ArrayRef<const Formula *> operands() const {
    return ArrayRef(reinterpret_cast<Formula *const *>(this + 1),
                    numOperands(kind()));
  }

  using AtomNames = llvm::DenseMap<Atom, std::string>;
  // Produce a stable human-readable representation of this formula.
  // For example: (V3 | !(V1 & V2))
  // If AtomNames is provided, these override the default V0, V1... names.
  void print(llvm::raw_ostream &OS, const AtomNames * = nullptr) const;

  // Allocate Formulas using Arena rather than calling this function directly.
  static const Formula &create(llvm::BumpPtrAllocator &Alloc, Kind K,
                               ArrayRef<const Formula *> Operands,
                               unsigned Value = 0);

private:
  Formula() = default;
  Formula(const Formula &) = delete;
  Formula &operator=(const Formula &) = delete;

  static unsigned numOperands(Kind K) {
    switch (K) {
    case AtomRef:
    case Literal:
      return 0;
    case Not:
      return 1;
    case And:
    case Or:
    case Implies:
    case Equal:
      return 2;
    }
    llvm_unreachable("Unhandled Formula::Kind enum");
  }

  Kind FormulaKind;
  // Some kinds of formula have scalar values, e.g. AtomRef's atom number.
  unsigned Value;
};

// The default names of atoms are V0, V1 etc in order of creation.
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, Atom A) {
  return OS << 'V' << static_cast<unsigned>(A);
}
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Formula &F) {
  F.print(OS);
  return OS;
}

} // namespace clang::dataflow
namespace llvm {
template <> struct DenseMapInfo<clang::dataflow::Atom> {
  using Atom = clang::dataflow::Atom;
  using Underlying = std::underlying_type_t<Atom>;

  static inline Atom getEmptyKey() { return Atom(Underlying(-1)); }
  static inline Atom getTombstoneKey() { return Atom(Underlying(-2)); }
  static unsigned getHashValue(const Atom &Val) {
    return DenseMapInfo<Underlying>::getHashValue(Underlying(Val));
  }
  static bool isEqual(const Atom &LHS, const Atom &RHS) { return LHS == RHS; }
};
} // namespace llvm
#endif
