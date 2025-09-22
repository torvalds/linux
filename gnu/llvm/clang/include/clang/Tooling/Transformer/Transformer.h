//===--- Transformer.h - Transformer class ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_TRANSFORMER_TRANSFORMER_H_
#define LLVM_CLANG_TOOLING_TRANSFORMER_TRANSFORMER_H_

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Refactoring/AtomicChange.h"
#include "clang/Tooling/Transformer/RewriteRule.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <utility>

namespace clang {
namespace tooling {

namespace detail {
/// Implementation details of \c Transformer with type erasure around
/// \c RewriteRule<T> as well as the corresponding consumers.
class TransformerImpl {
public:
  virtual ~TransformerImpl() = default;

  void onMatch(const ast_matchers::MatchFinder::MatchResult &Result);

  virtual std::vector<ast_matchers::internal::DynTypedMatcher>
  buildMatchers() const = 0;

protected:
  /// Converts a set of \c Edit into a \c AtomicChange per file modified.
  /// Returns an error if the edits fail to compose, e.g. overlapping edits.
  static llvm::Expected<llvm::SmallVector<AtomicChange, 1>>
  convertToAtomicChanges(const llvm::SmallVectorImpl<transformer::Edit> &Edits,
                         const ast_matchers::MatchFinder::MatchResult &Result);

private:
  virtual void
  onMatchImpl(const ast_matchers::MatchFinder::MatchResult &Result) = 0;
};

// FIXME: Use std::type_identity or backport when available.
template <class T> struct type_identity {
  using type = T;
};
} // namespace detail

template <typename T> struct TransformerResult {
  llvm::MutableArrayRef<AtomicChange> Changes;
  T Metadata;
};

template <> struct TransformerResult<void> {
  llvm::MutableArrayRef<AtomicChange> Changes;
};

/// Handles the matcher and callback registration for a single `RewriteRule`, as
/// defined by the arguments of the constructor.
class Transformer : public ast_matchers::MatchFinder::MatchCallback {
public:
  /// Provides the set of changes to the consumer.  The callback is free to move
  /// or destructively consume the changes as needed.
  ///
  /// We use \c MutableArrayRef as an abstraction to provide decoupling, and we
  /// expect the majority of consumers to copy or move the individual values
  /// into a separate data structure.
  using ChangeSetConsumer = std::function<void(
      Expected<llvm::MutableArrayRef<AtomicChange>> Changes)>;

  /// \param Consumer receives all rewrites for a single match, or an error.
  /// Will not necessarily be called for each match; for example, if the rule
  /// generates no edits but does not fail.  Note that clients are responsible
  /// for handling the case that independent \c AtomicChanges conflict with each
  /// other.
  explicit Transformer(transformer::RewriteRuleWith<void> Rule,
                       ChangeSetConsumer Consumer)
      : Transformer(std::move(Rule),
                    [Consumer = std::move(Consumer)](
                        llvm::Expected<TransformerResult<void>> Result) {
                      if (Result)
                        Consumer(Result->Changes);
                      else
                        Consumer(Result.takeError());
                    }) {}

  /// \param Consumer receives all rewrites and the associated metadata for a
  /// single match, or an error. Will always be called for each match, even if
  /// the rule generates no edits.  Note that clients are responsible for
  /// handling the case that independent \c AtomicChanges conflict with each
  /// other.
  template <typename MetadataT>
  explicit Transformer(
      transformer::RewriteRuleWith<MetadataT> Rule,
      std::function<void(llvm::Expected<TransformerResult<
                             typename detail::type_identity<MetadataT>::type>>)>
          Consumer);

  /// N.B. Passes `this` pointer to `MatchFinder`.  So, this object should not
  /// be moved after this call.
  void registerMatchers(ast_matchers::MatchFinder *MatchFinder);

  /// Not called directly by users -- called by the framework, via base class
  /// pointer.
  void run(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  std::unique_ptr<detail::TransformerImpl> Impl;
};

namespace detail {
/// Runs the metadata generator on \c Rule and stuffs it into \c Result.
/// @{
template <typename T>
llvm::Error
populateMetadata(const transformer::RewriteRuleWith<T> &Rule,
                 size_t SelectedCase,
                 const ast_matchers::MatchFinder::MatchResult &Match,
                 TransformerResult<T> &Result) {
  // Silence a false positive GCC -Wunused-but-set-parameter warning in constexpr
  // cases, by marking SelectedCase as used. See
  // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85827 for details. The issue is
  // fixed in GCC 10.
  (void)SelectedCase;
  if constexpr (!std::is_void_v<T>) {
    auto Metadata = Rule.Metadata[SelectedCase]->eval(Match);
    if (!Metadata)
      return Metadata.takeError();
    Result.Metadata = std::move(*Metadata);
  }
  return llvm::Error::success();
}
/// @}

/// Implementation when metadata is generated as a part of the rewrite. This
/// happens when we have a \c RewriteRuleWith<T>.
template <typename T> class WithMetadataImpl final : public TransformerImpl {
  transformer::RewriteRuleWith<T> Rule;
  std::function<void(llvm::Expected<TransformerResult<T>>)> Consumer;

public:
  explicit WithMetadataImpl(
      transformer::RewriteRuleWith<T> R,
      std::function<void(llvm::Expected<TransformerResult<T>>)> Consumer)
      : Rule(std::move(R)), Consumer(std::move(Consumer)) {
    assert(llvm::all_of(Rule.Cases,
                        [](const transformer::RewriteRuleBase::Case &Case)
                            -> bool { return !!Case.Edits; }) &&
           "edit generator must be provided for each rule");
    if constexpr (!std::is_void_v<T>)
      assert(llvm::all_of(Rule.Metadata,
                          [](const typename transformer::Generator<T> &Metadata)
                              -> bool { return !!Metadata; }) &&
             "metadata generator must be provided for each rule");
  }

private:
  void onMatchImpl(const ast_matchers::MatchFinder::MatchResult &Result) final {
    size_t I = transformer::detail::findSelectedCase(Result, Rule);
    auto Transformations = Rule.Cases[I].Edits(Result);
    if (!Transformations) {
      Consumer(Transformations.takeError());
      return;
    }

    llvm::SmallVector<AtomicChange, 1> Changes;
    if (!Transformations->empty()) {
      auto C = convertToAtomicChanges(*Transformations, Result);
      if (C) {
        Changes = std::move(*C);
      } else {
        Consumer(C.takeError());
        return;
      }
    } else if (std::is_void<T>::value) {
      // If we don't have metadata and we don't have any edits, skip.
      return;
    }

    TransformerResult<T> RewriteResult;
    if (auto E = populateMetadata(Rule, I, Result, RewriteResult)) {
      Consumer(std::move(E));
      return;
    }

    RewriteResult.Changes = llvm::MutableArrayRef<AtomicChange>(Changes);
    Consumer(std::move(RewriteResult));
  }

  std::vector<ast_matchers::internal::DynTypedMatcher>
  buildMatchers() const final {
    return transformer::detail::buildMatchers(Rule);
  }
};
} // namespace detail

template <typename MetadataT>
Transformer::Transformer(
    transformer::RewriteRuleWith<MetadataT> Rule,
    std::function<void(llvm::Expected<TransformerResult<
                           typename detail::type_identity<MetadataT>::type>>)>
        Consumer)
    : Impl(std::make_unique<detail::WithMetadataImpl<MetadataT>>(
          std::move(Rule), std::move(Consumer))) {}

} // namespace tooling
} // namespace clang

#endif // LLVM_CLANG_TOOLING_TRANSFORMER_TRANSFORMER_H_
