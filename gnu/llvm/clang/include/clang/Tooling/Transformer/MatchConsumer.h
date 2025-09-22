//===--- MatchConsumer.h - MatchConsumer abstraction ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file defines the *MatchConsumer* abstraction: a computation over
/// match results, specifically the `ast_matchers::MatchFinder::MatchResult`
/// class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_TRANSFORMER_MATCHCONSUMER_H
#define LLVM_CLANG_TOOLING_TRANSFORMER_MATCHCONSUMER_H

#include "clang/AST/ASTTypeTraits.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace clang {
namespace transformer {
/// A failable computation over nodes bound by AST matchers.
///
/// The computation should report any errors though its return value (rather
/// than terminating the program) to enable usage in interactive scenarios like
/// clang-query.
///
/// This is a central abstraction of the Transformer framework.
template <typename T>
using MatchConsumer =
    std::function<Expected<T>(const ast_matchers::MatchFinder::MatchResult &)>;

/// Creates an error that signals that a `MatchConsumer` expected a certain node
/// to be bound by AST matchers, but it was not actually bound.
inline llvm::Error notBoundError(llvm::StringRef Id) {
  return llvm::make_error<llvm::StringError>(llvm::errc::invalid_argument,
                                             "Id not bound: " + Id);
}

/// Chooses between the two consumers, based on whether \p ID is bound in the
/// match.
template <typename T>
MatchConsumer<T> ifBound(std::string ID, MatchConsumer<T> TrueC,
                         MatchConsumer<T> FalseC) {
  return [=](const ast_matchers::MatchFinder::MatchResult &Result) {
    auto &Map = Result.Nodes.getMap();
    return (Map.find(ID) != Map.end() ? TrueC : FalseC)(Result);
  };
}

/// A failable computation over nodes bound by AST matchers, with (limited)
/// reflection via the `toString` method.
///
/// The computation should report any errors though its return value (rather
/// than terminating the program) to enable usage in interactive scenarios like
/// clang-query.
///
/// This is a central abstraction of the Transformer framework. It is a
/// generalization of `MatchConsumer` and intended to replace it.
template <typename T> class MatchComputation {
public:
  virtual ~MatchComputation() = default;

  /// Evaluates the computation and (potentially) updates the accumulator \c
  /// Result.  \c Result is undefined in the case of an error. `Result` is an
  /// out parameter to optimize case where the computation involves composing
  /// the result of sub-computation evaluations.
  virtual llvm::Error eval(const ast_matchers::MatchFinder::MatchResult &Match,
                           T *Result) const = 0;

  /// Convenience version of `eval`, for the case where the computation is being
  /// evaluated on its own.
  llvm::Expected<T> eval(const ast_matchers::MatchFinder::MatchResult &R) const;

  /// Constructs a string representation of the computation, for informational
  /// purposes. The representation must be deterministic, but is not required to
  /// be unique.
  virtual std::string toString() const = 0;

protected:
  MatchComputation() = default;

  // Since this is an abstract class, copying/assigning only make sense for
  // derived classes implementing `clone()`.
  MatchComputation(const MatchComputation &) = default;
  MatchComputation &operator=(const MatchComputation &) = default;
};

template <typename T>
llvm::Expected<T> MatchComputation<T>::eval(
    const ast_matchers::MatchFinder::MatchResult &R) const {
  T Output;
  if (auto Err = eval(R, &Output))
    return std::move(Err);
  return Output;
}
} // namespace transformer
} // namespace clang
#endif // LLVM_CLANG_TOOLING_TRANSFORMER_MATCHCONSUMER_H
