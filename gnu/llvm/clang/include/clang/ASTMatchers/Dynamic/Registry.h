//===- Registry.h - Matcher registry ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Registry of all known matchers.
///
/// The registry provides a generic interface to construct any matcher by name.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ASTMATCHERS_DYNAMIC_REGISTRY_H
#define LLVM_CLANG_ASTMATCHERS_DYNAMIC_REGISTRY_H

#include "clang/ASTMatchers/Dynamic/Diagnostics.h"
#include "clang/ASTMatchers/Dynamic/VariantValue.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clang {
namespace ast_matchers {
namespace dynamic {

namespace internal {

class MatcherDescriptor;

/// A smart (owning) pointer for MatcherDescriptor. We can't use unique_ptr
/// because MatcherDescriptor is forward declared
class MatcherDescriptorPtr {
public:
  explicit MatcherDescriptorPtr(MatcherDescriptor *);
  ~MatcherDescriptorPtr();
  MatcherDescriptorPtr(MatcherDescriptorPtr &&) = default;
  MatcherDescriptorPtr &operator=(MatcherDescriptorPtr &&) = default;
  MatcherDescriptorPtr(const MatcherDescriptorPtr &) = delete;
  MatcherDescriptorPtr &operator=(const MatcherDescriptorPtr &) = delete;

  MatcherDescriptor *get() { return Ptr; }

private:
  MatcherDescriptor *Ptr;
};

} // namespace internal

using MatcherCtor = const internal::MatcherDescriptor *;

struct MatcherCompletion {
  MatcherCompletion() = default;
  MatcherCompletion(StringRef TypedText, StringRef MatcherDecl,
                    unsigned Specificity)
      : TypedText(TypedText), MatcherDecl(MatcherDecl),
        Specificity(Specificity) {}

  bool operator==(const MatcherCompletion &Other) const {
    return TypedText == Other.TypedText && MatcherDecl == Other.MatcherDecl;
  }

  /// The text to type to select this matcher.
  std::string TypedText;

  /// The "declaration" of the matcher, with type information.
  std::string MatcherDecl;

  /// Value corresponding to the "specificity" of the converted matcher.
  ///
  /// Zero specificity indicates that this conversion would produce a trivial
  /// matcher that will either always or never match.
  /// Such matchers are excluded from code completion results.
  unsigned Specificity;
};

class Registry {
public:
  Registry() = delete;

  static ASTNodeKind nodeMatcherType(MatcherCtor);

  static bool isBuilderMatcher(MatcherCtor Ctor);

  static internal::MatcherDescriptorPtr
  buildMatcherCtor(MatcherCtor, SourceRange NameRange,
                   ArrayRef<ParserValue> Args, Diagnostics *Error);

  /// Look up a matcher in the registry by name,
  ///
  /// \return An opaque value which may be used to refer to the matcher
  /// constructor, or std::optional<MatcherCtor>() if not found.
  static std::optional<MatcherCtor> lookupMatcherCtor(StringRef MatcherName);

  /// Compute the list of completion types for \p Context.
  ///
  /// Each element of \p Context represents a matcher invocation, going from
  /// outermost to innermost. Elements are pairs consisting of a reference to
  /// the matcher constructor and the index of the next element in the
  /// argument list of that matcher (or for the last element, the index of
  /// the completion point in the argument list). An empty list requests
  /// completion for the root matcher.
  static std::vector<ArgKind> getAcceptedCompletionTypes(
      llvm::ArrayRef<std::pair<MatcherCtor, unsigned>> Context);

  /// Compute the list of completions that match any of
  /// \p AcceptedTypes.
  ///
  /// \param AcceptedTypes All types accepted for this completion.
  ///
  /// \return All completions for the specified types.
  /// Completions should be valid when used in \c lookupMatcherCtor().
  /// The matcher constructed from the return of \c lookupMatcherCtor()
  /// should be convertible to some type in \p AcceptedTypes.
  static std::vector<MatcherCompletion>
  getMatcherCompletions(ArrayRef<ArgKind> AcceptedTypes);

  /// Construct a matcher from the registry.
  ///
  /// \param Ctor The matcher constructor to instantiate.
  ///
  /// \param NameRange The location of the name in the matcher source.
  ///   Useful for error reporting.
  ///
  /// \param Args The argument list for the matcher. The number and types of the
  ///   values must be valid for the matcher requested. Otherwise, the function
  ///   will return an error.
  ///
  /// \return The matcher object constructed if no error was found.
  ///   A null matcher if the number of arguments or argument types do not match
  ///   the signature.  In that case \c Error will contain the description of
  ///   the error.
  static VariantMatcher constructMatcher(MatcherCtor Ctor,
                                         SourceRange NameRange,
                                         ArrayRef<ParserValue> Args,
                                         Diagnostics *Error);

  /// Construct a matcher from the registry and bind it.
  ///
  /// Similar the \c constructMatcher() above, but it then tries to bind the
  /// matcher to the specified \c BindID.
  /// If the matcher is not bindable, it sets an error in \c Error and returns
  /// a null matcher.
  static VariantMatcher constructBoundMatcher(MatcherCtor Ctor,
                                              SourceRange NameRange,
                                              StringRef BindID,
                                              ArrayRef<ParserValue> Args,
                                              Diagnostics *Error);
};

} // namespace dynamic
} // namespace ast_matchers
} // namespace clang

#endif // LLVM_CLANG_ASTMATCHERS_DYNAMIC_REGISTRY_H
