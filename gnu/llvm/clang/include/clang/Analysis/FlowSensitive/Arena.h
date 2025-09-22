//===-- Arena.h -------------------------------*- C++ -------------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE__ARENA_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE__ARENA_H

#include "clang/Analysis/FlowSensitive/Formula.h"
#include "clang/Analysis/FlowSensitive/StorageLocation.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "llvm/ADT/StringRef.h"
#include <vector>

namespace clang::dataflow {

/// The Arena owns the objects that model data within an analysis.
/// For example, `Value`, `StorageLocation`, `Atom`, and `Formula`.
class Arena {
public:
  Arena()
      : True(Formula::create(Alloc, Formula::Literal, {}, 1)),
        False(Formula::create(Alloc, Formula::Literal, {}, 0)) {}
  Arena(const Arena &) = delete;
  Arena &operator=(const Arena &) = delete;

  /// Creates a `T` (some subclass of `StorageLocation`), forwarding `args` to
  /// the constructor, and returns a reference to it.
  ///
  /// The `Arena` takes ownership of the created object. The object will be
  /// destroyed when the `Arena` is destroyed.
  template <typename T, typename... Args>
  std::enable_if_t<std::is_base_of<StorageLocation, T>::value, T &>
  create(Args &&...args) {
    // Note: If allocation of individual `StorageLocation`s turns out to be
    // costly, consider creating specializations of `create<T>` for commonly
    // used `StorageLocation` subclasses and make them use a `BumpPtrAllocator`.
    return *cast<T>(
        Locs.emplace_back(std::make_unique<T>(std::forward<Args>(args)...))
            .get());
  }

  /// Creates a `T` (some subclass of `Value`), forwarding `args` to the
  /// constructor, and returns a reference to it.
  ///
  /// The `Arena` takes ownership of the created object. The object will be
  /// destroyed when the `Arena` is destroyed.
  template <typename T, typename... Args>
  std::enable_if_t<std::is_base_of<Value, T>::value, T &>
  create(Args &&...args) {
    // Note: If allocation of individual `Value`s turns out to be costly,
    // consider creating specializations of `create<T>` for commonly used
    // `Value` subclasses and make them use a `BumpPtrAllocator`.
    return *cast<T>(
        Vals.emplace_back(std::make_unique<T>(std::forward<Args>(args)...))
            .get());
  }

  /// Creates a BoolValue wrapping a particular formula.
  ///
  /// Passing in the same formula will result in the same BoolValue.
  /// FIXME: Interning BoolValues but not other Values is inconsistent.
  ///        Decide whether we want Value interning or not.
  BoolValue &makeBoolValue(const Formula &);

  /// Creates a fresh atom and wraps in in an AtomicBoolValue.
  /// FIXME: For now, identical-address AtomicBoolValue <=> identical atom.
  ///        Stop relying on pointer identity and remove this guarantee.
  AtomicBoolValue &makeAtomValue() {
    return cast<AtomicBoolValue>(makeBoolValue(makeAtomRef(makeAtom())));
  }

  /// Creates a fresh Top boolean value.
  TopBoolValue &makeTopValue() {
    // No need for deduplicating: there's no way to create aliasing Tops.
    return create<TopBoolValue>(makeAtomRef(makeAtom()));
  }

  /// Returns a symbolic integer value that models an integer literal equal to
  /// `Value`. These literals are the same every time.
  /// Integer literals are not typed; the type is determined by the `Expr` that
  /// an integer literal is associated with.
  IntegerValue &makeIntLiteral(llvm::APInt Value);

  // Factories for boolean formulas.
  // Formulas are interned: passing the same arguments return the same result.
  // For commutative operations like And/Or, interning ignores order.
  // Simplifications are applied: makeOr(X, X) => X, etc.

  /// Returns a formula for the conjunction of `LHS` and `RHS`.
  const Formula &makeAnd(const Formula &LHS, const Formula &RHS);

  /// Returns a formula for the disjunction of `LHS` and `RHS`.
  const Formula &makeOr(const Formula &LHS, const Formula &RHS);

  /// Returns a formula for the negation of `Val`.
  const Formula &makeNot(const Formula &Val);

  /// Returns a formula for `LHS => RHS`.
  const Formula &makeImplies(const Formula &LHS, const Formula &RHS);

  /// Returns a formula for `LHS <=> RHS`.
  const Formula &makeEquals(const Formula &LHS, const Formula &RHS);

  /// Returns a formula for the variable A.
  const Formula &makeAtomRef(Atom A);

  /// Returns a formula for a literal true/false.
  const Formula &makeLiteral(bool Value) { return Value ? True : False; }

  // Parses a formula from its textual representation.
  // This may refer to atoms that were not produced by makeAtom() yet!
  llvm::Expected<const Formula &> parseFormula(llvm::StringRef);

  /// Returns a new atomic boolean variable, distinct from any other.
  Atom makeAtom() { return static_cast<Atom>(NextAtom++); };

  /// Creates a fresh flow condition and returns a token that identifies it. The
  /// token can be used to perform various operations on the flow condition such
  /// as adding constraints to it, forking it, joining it with another flow
  /// condition, or checking implications.
  Atom makeFlowConditionToken() { return makeAtom(); }

private:
  llvm::BumpPtrAllocator Alloc;

  // Storage for the state of a program.
  std::vector<std::unique_ptr<StorageLocation>> Locs;
  std::vector<std::unique_ptr<Value>> Vals;

  // Indices that are used to avoid recreating the same integer literals and
  // composite boolean values.
  llvm::DenseMap<llvm::APInt, IntegerValue *> IntegerLiterals;
  using FormulaPair = std::pair<const Formula *, const Formula *>;
  llvm::DenseMap<FormulaPair, const Formula *> Ands;
  llvm::DenseMap<FormulaPair, const Formula *> Ors;
  llvm::DenseMap<const Formula *, const Formula *> Nots;
  llvm::DenseMap<FormulaPair, const Formula *> Implies;
  llvm::DenseMap<FormulaPair, const Formula *> Equals;
  llvm::DenseMap<Atom, const Formula *> AtomRefs;

  llvm::DenseMap<const Formula *, BoolValue *> FormulaValues;
  unsigned NextAtom = 0;

  const Formula &True, &False;
};

} // namespace clang::dataflow

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE__ARENA_H
