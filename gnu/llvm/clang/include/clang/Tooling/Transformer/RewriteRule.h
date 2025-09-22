//===--- RewriteRule.h - RewriteRule class ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
///  \file
///  Defines the RewriteRule class and related functions for creating,
///  modifying and interpreting RewriteRules.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_TRANSFORMER_REWRITERULE_H
#define LLVM_CLANG_TOOLING_TRANSFORMER_REWRITERULE_H

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "clang/Tooling/Refactoring/AtomicChange.h"
#include "clang/Tooling/Transformer/MatchConsumer.h"
#include "clang/Tooling/Transformer/RangeSelector.h"
#include "llvm/ADT/Any.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <string>
#include <utility>

namespace clang {
namespace transformer {
// Specifies how to interpret an edit.
enum class EditKind {
  // Edits a source range in the file.
  Range,
  // Inserts an include in the file. The `Replacement` field is the name of the
  // newly included file.
  AddInclude,
};

/// A concrete description of a source edit, represented by a character range in
/// the source to be replaced and a corresponding replacement string.
struct Edit {
  EditKind Kind = EditKind::Range;
  CharSourceRange Range;
  std::string Replacement;
  std::string Note;
  llvm::Any Metadata;
};

/// Format of the path in an include directive -- angle brackets or quotes.
enum class IncludeFormat {
  Quoted,
  Angled,
};

/// Maps a match result to a list of concrete edits (with possible
/// failure). This type is a building block of rewrite rules, but users will
/// generally work in terms of `ASTEdit`s (below) rather than directly in terms
/// of `EditGenerator`.
using EditGenerator = MatchConsumer<llvm::SmallVector<Edit, 1>>;

template <typename T> using Generator = std::shared_ptr<MatchComputation<T>>;

using TextGenerator = Generator<std::string>;

using AnyGenerator = MatchConsumer<llvm::Any>;

// Description of a source-code edit, expressed in terms of an AST node.
// Includes: an ID for the (bound) node, a selector for source related to the
// node, a replacement and, optionally, an explanation for the edit.
//
// * Target: the source code impacted by the rule. This identifies an AST node,
//   or part thereof (\c Part), whose source range indicates the extent of the
//   replacement applied by the replacement term.  By default, the extent is the
//   node matched by the pattern term (\c NodePart::Node). Target's are typed
//   (\c Kind), which guides the determination of the node extent.
//
// * Replacement: a function that produces a replacement string for the target,
//   based on the match result.
//
// * Note: (optional) a note specifically for this edit, potentially referencing
//   elements of the match.  This will be displayed to the user, where possible;
//   for example, in clang-tidy diagnostics.  Use of notes should be rare --
//   explanations of the entire rewrite should be set in the rule
//   (`RewriteRule::Explanation`) instead.  Notes serve the rare cases wherein
//   edit-specific diagnostics are required.
//
// `ASTEdit` should be built using the `change` convenience functions. For
// example,
// \code
//   changeTo(name(fun), cat("Frodo"))
// \endcode
// Or, if we use Stencil for the TextGenerator:
// \code
//   using stencil::cat;
//   changeTo(statement(thenNode), cat("{", thenNode, "}"))
//   changeTo(callArgs(call), cat(x, ",", y))
// \endcode
// Or, if you are changing the node corresponding to the rule's matcher, you can
// use the single-argument override of \c change:
// \code
//   changeTo(cat("different_expr"))
// \endcode
struct ASTEdit {
  EditKind Kind = EditKind::Range;
  RangeSelector TargetRange;
  TextGenerator Replacement;
  TextGenerator Note;
  // Not all transformations will want or need to attach metadata and therefore
  // should not be required to do so.
  AnyGenerator Metadata = [](const ast_matchers::MatchFinder::MatchResult &)
      -> llvm::Expected<llvm::Any> {
    return llvm::Expected<llvm::Any>(llvm::Any());
  };
};

/// Generates a single (specified) edit.
EditGenerator edit(ASTEdit E);

/// Lifts a list of `ASTEdit`s into an `EditGenerator`.
///
/// The `EditGenerator` will return an empty vector if any of the edits apply to
/// portions of the source that are ineligible for rewriting (certain
/// interactions with macros, for example) and it will fail if any invariants
/// are violated relating to bound nodes in the match.  However, it does not
/// fail in the case of conflicting edits -- conflict handling is left to
/// clients.  We recommend use of the \c AtomicChange or \c Replacements classes
/// for assistance in detecting such conflicts.
EditGenerator editList(llvm::SmallVector<ASTEdit, 1> Edits);

/// Generates no edits.
inline EditGenerator noEdits() { return editList({}); }

/// Generates a single, no-op edit anchored at the start location of the
/// specified range. A `noopEdit` may be preferred over `noEdits` to associate a
/// diagnostic `Explanation` with the rule.
EditGenerator noopEdit(RangeSelector Anchor);

/// Generates a single, no-op edit with the associated note anchored at the
/// start location of the specified range.
ASTEdit note(RangeSelector Anchor, TextGenerator Note);

/// Version of `ifBound` specialized to `ASTEdit`.
inline EditGenerator ifBound(std::string ID, ASTEdit TrueEdit,
                             ASTEdit FalseEdit) {
  return ifBound(std::move(ID), edit(std::move(TrueEdit)),
                 edit(std::move(FalseEdit)));
}

/// Version of `ifBound` that has no "False" branch. If the node is not bound,
/// then no edits are produced.
inline EditGenerator ifBound(std::string ID, ASTEdit TrueEdit) {
  return ifBound(std::move(ID), edit(std::move(TrueEdit)), noEdits());
}

/// Flattens a list of generators into a single generator whose elements are the
/// concatenation of the results of the argument generators.
EditGenerator flattenVector(SmallVector<EditGenerator, 2> Generators);

namespace detail {
/// Helper function to construct an \c EditGenerator. Overloaded for common
/// cases so that user doesn't need to specify which factory function to
/// use. This pattern gives benefits similar to implicit constructors, while
/// maintaing a higher degree of explicitness.
inline EditGenerator injectEdits(ASTEdit E) { return edit(std::move(E)); }
inline EditGenerator injectEdits(EditGenerator G) { return G; }
} // namespace detail

template <typename... Ts> EditGenerator flatten(Ts &&...Edits) {
  return flattenVector({detail::injectEdits(std::forward<Ts>(Edits))...});
}

// Every rewrite rule is triggered by a match against some AST node.
// Transformer guarantees that this ID is bound to the triggering node whenever
// a rewrite rule is applied.
extern const char RootID[];

/// Replaces a portion of the source text with \p Replacement.
ASTEdit changeTo(RangeSelector Target, TextGenerator Replacement);
/// DEPRECATED: use \c changeTo.
inline ASTEdit change(RangeSelector Target, TextGenerator Replacement) {
  return changeTo(std::move(Target), std::move(Replacement));
}

/// Replaces the entirety of a RewriteRule's match with \p Replacement.  For
/// example, to replace a function call, one could write:
/// \code
///   makeRule(callExpr(callee(functionDecl(hasName("foo")))),
///            changeTo(cat("bar()")))
/// \endcode
inline ASTEdit changeTo(TextGenerator Replacement) {
  return changeTo(node(RootID), std::move(Replacement));
}
/// DEPRECATED: use \c changeTo.
inline ASTEdit change(TextGenerator Replacement) {
  return changeTo(std::move(Replacement));
}

/// Inserts \p Replacement before \p S, leaving the source selected by \S
/// unchanged.
inline ASTEdit insertBefore(RangeSelector S, TextGenerator Replacement) {
  return changeTo(before(std::move(S)), std::move(Replacement));
}

/// Inserts \p Replacement after \p S, leaving the source selected by \S
/// unchanged.
inline ASTEdit insertAfter(RangeSelector S, TextGenerator Replacement) {
  return changeTo(after(std::move(S)), std::move(Replacement));
}

/// Removes the source selected by \p S.
ASTEdit remove(RangeSelector S);

/// Adds an include directive for the given header to the file of `Target`. The
/// particular location specified by `Target` is ignored.
ASTEdit addInclude(RangeSelector Target, StringRef Header,
                   IncludeFormat Format = IncludeFormat::Quoted);

/// Adds an include directive for the given header to the file associated with
/// `RootID`. If `RootID` matches inside a macro expansion, will add the
/// directive to the file in which the macro was expanded (as opposed to the
/// file in which the macro is defined).
inline ASTEdit addInclude(StringRef Header,
                          IncludeFormat Format = IncludeFormat::Quoted) {
  return addInclude(expansion(node(RootID)), Header, Format);
}

// FIXME: If `Metadata` returns an `llvm::Expected<T>` the `AnyGenerator` will
// construct an `llvm::Expected<llvm::Any>` where no error is present but the
// `llvm::Any` holds the error. This is unlikely but potentially surprising.
// Perhaps the `llvm::Expected` should be unwrapped, or perhaps this should be a
// compile-time error. No solution here is perfect.
//
// Note: This function template accepts any type callable with a MatchResult
// rather than a `std::function` because the return-type needs to be deduced. If
// it accepted a `std::function<R(MatchResult)>`, lambdas or other callable
// types would not be able to deduce `R`, and users would be forced to specify
// explicitly the type they intended to return by wrapping the lambda at the
// call-site.
template <typename Callable>
inline ASTEdit withMetadata(ASTEdit Edit, Callable Metadata) {
  Edit.Metadata =
      [Gen = std::move(Metadata)](
          const ast_matchers::MatchFinder::MatchResult &R) -> llvm::Any {
    return Gen(R);
  };

  return Edit;
}

/// Assuming that the inner range is enclosed by the outer range, creates
/// precision edits to remove the parts of the outer range that are not included
/// in the inner range.
inline EditGenerator shrinkTo(RangeSelector outer, RangeSelector inner) {
  return editList({remove(enclose(before(outer), before(inner))),
                   remove(enclose(after(inner), after(outer)))});
}

/// Description of a source-code transformation.
//
// A *rewrite rule* describes a transformation of source code. A simple rule
// contains each of the following components:
//
// * Matcher: the pattern term, expressed as clang matchers (with Transformer
//   extensions).
//
// * Edits: a set of Edits to the source code, described with ASTEdits.
//
// However, rules can also consist of (sub)rules, where the first that matches
// is applied and the rest are ignored.  So, the above components together form
// a logical "case" and a rule is a sequence of cases.
//
// Rule cases have an additional, implicit, component: the parameters. These are
// portions of the pattern which are left unspecified, yet bound in the pattern
// so that we can reference them in the edits.
//
// The \c Transformer class can be used to apply the rewrite rule and obtain the
// corresponding replacements.
struct RewriteRuleBase {
  struct Case {
    ast_matchers::internal::DynTypedMatcher Matcher;
    EditGenerator Edits;
  };
  // We expect RewriteRules will most commonly include only one case.
  SmallVector<Case, 1> Cases;
};

/// A source-code transformation with accompanying metadata.
///
/// When a case of the rule matches, the \c Transformer invokes the
/// corresponding metadata generator and provides it alongside the edits.
template <typename MetadataT> struct RewriteRuleWith : RewriteRuleBase {
  SmallVector<Generator<MetadataT>, 1> Metadata;
};

template <> struct RewriteRuleWith<void> : RewriteRuleBase {};

using RewriteRule = RewriteRuleWith<void>;

namespace detail {

RewriteRule makeRule(ast_matchers::internal::DynTypedMatcher M,
                     EditGenerator Edits);

template <typename MetadataT>
RewriteRuleWith<MetadataT> makeRule(ast_matchers::internal::DynTypedMatcher M,
                                    EditGenerator Edits,
                                    Generator<MetadataT> Metadata) {
  RewriteRuleWith<MetadataT> R;
  R.Cases = {{std::move(M), std::move(Edits)}};
  R.Metadata = {std::move(Metadata)};
  return R;
}

inline EditGenerator makeEditGenerator(EditGenerator Edits) { return Edits; }
EditGenerator makeEditGenerator(llvm::SmallVector<ASTEdit, 1> Edits);
EditGenerator makeEditGenerator(ASTEdit Edit);

} // namespace detail

/// Constructs a simple \c RewriteRule. \c Edits can be an \c EditGenerator,
/// multiple \c ASTEdits, or a single \c ASTEdit.
/// @{
template <int &..., typename EditsT>
RewriteRule makeRule(ast_matchers::internal::DynTypedMatcher M,
                     EditsT &&Edits) {
  return detail::makeRule(
      std::move(M), detail::makeEditGenerator(std::forward<EditsT>(Edits)));
}

RewriteRule makeRule(ast_matchers::internal::DynTypedMatcher M,
                     std::initializer_list<ASTEdit> Edits);
/// @}

/// Overloads of \c makeRule that also generate metadata when matching.
/// @{
template <typename MetadataT, int &..., typename EditsT>
RewriteRuleWith<MetadataT> makeRule(ast_matchers::internal::DynTypedMatcher M,
                                    EditsT &&Edits,
                                    Generator<MetadataT> Metadata) {
  return detail::makeRule(
      std::move(M), detail::makeEditGenerator(std::forward<EditsT>(Edits)),
      std::move(Metadata));
}

template <typename MetadataT>
RewriteRuleWith<MetadataT> makeRule(ast_matchers::internal::DynTypedMatcher M,
                                    std::initializer_list<ASTEdit> Edits,
                                    Generator<MetadataT> Metadata) {
  return detail::makeRule(std::move(M),
                          detail::makeEditGenerator(std::move(Edits)),
                          std::move(Metadata));
}
/// @}

/// For every case in Rule, adds an include directive for the given header. The
/// common use is assumed to be a rule with only one case. For example, to
/// replace a function call and add headers corresponding to the new code, one
/// could write:
/// \code
///   auto R = makeRule(callExpr(callee(functionDecl(hasName("foo")))),
///            changeTo(cat("bar()")));
///   addInclude(R, "path/to/bar_header.h");
///   addInclude(R, "vector", IncludeFormat::Angled);
/// \endcode
void addInclude(RewriteRuleBase &Rule, llvm::StringRef Header,
                IncludeFormat Format = IncludeFormat::Quoted);

/// Applies the first rule whose pattern matches; other rules are ignored.  If
/// the matchers are independent then order doesn't matter. In that case,
/// `applyFirst` is simply joining the set of rules into one.
//
// `applyFirst` is like an `anyOf` matcher with an edit action attached to each
// of its cases. Anywhere you'd use `anyOf(m1.bind("id1"), m2.bind("id2"))` and
// then dispatch on those ids in your code for control flow, `applyFirst` lifts
// that behavior to the rule level.  So, you can write `applyFirst({makeRule(m1,
// action1), makeRule(m2, action2), ...});`
//
// For example, consider a type `T` with a deterministic serialization function,
// `serialize()`.  For performance reasons, we would like to make it
// non-deterministic.  Therefore, we want to drop the expectation that
// `a.serialize() = b.serialize() iff a = b` (although we'll maintain
// `deserialize(a.serialize()) = a`).
//
// We have three cases to consider (for some equality function, `eq`):
// ```
// eq(a.serialize(), b.serialize()) --> eq(a,b)
// eq(a, b.serialize())             --> eq(deserialize(a), b)
// eq(a.serialize(), b)             --> eq(a, deserialize(b))
// ```
//
// `applyFirst` allows us to specify each independently:
// ```
// auto eq_fun = functionDecl(...);
// auto method_call = cxxMemberCallExpr(...);
//
// auto two_calls = callExpr(callee(eq_fun), hasArgument(0, method_call),
//                           hasArgument(1, method_call));
// auto left_call =
//     callExpr(callee(eq_fun), callExpr(hasArgument(0, method_call)));
// auto right_call =
//     callExpr(callee(eq_fun), callExpr(hasArgument(1, method_call)));
//
// RewriteRule R = applyFirst({makeRule(two_calls, two_calls_action),
//                             makeRule(left_call, left_call_action),
//                             makeRule(right_call, right_call_action)});
// ```
/// @{
template <typename MetadataT>
RewriteRuleWith<MetadataT>
applyFirst(ArrayRef<RewriteRuleWith<MetadataT>> Rules) {
  RewriteRuleWith<MetadataT> R;
  for (auto &Rule : Rules) {
    assert(Rule.Cases.size() == Rule.Metadata.size() &&
           "mis-match in case and metadata array size");
    R.Cases.append(Rule.Cases.begin(), Rule.Cases.end());
    R.Metadata.append(Rule.Metadata.begin(), Rule.Metadata.end());
  }
  return R;
}

template <>
RewriteRuleWith<void> applyFirst(ArrayRef<RewriteRuleWith<void>> Rules);

template <typename MetadataT>
RewriteRuleWith<MetadataT>
applyFirst(const std::vector<RewriteRuleWith<MetadataT>> &Rules) {
  return applyFirst(llvm::ArrayRef(Rules));
}

template <typename MetadataT>
RewriteRuleWith<MetadataT>
applyFirst(std::initializer_list<RewriteRuleWith<MetadataT>> Rules) {
  return applyFirst(llvm::ArrayRef(Rules.begin(), Rules.end()));
}
/// @}

/// Converts a \c RewriteRuleWith<T> to a \c RewriteRule by stripping off the
/// metadata generators.
template <int &..., typename MetadataT>
std::enable_if_t<!std::is_same<MetadataT, void>::value, RewriteRule>
stripMetadata(RewriteRuleWith<MetadataT> Rule) {
  RewriteRule R;
  R.Cases = std::move(Rule.Cases);
  return R;
}

/// Applies `Rule` to all descendants of the node bound to `NodeId`. `Rule` can
/// refer to nodes bound by the calling rule. `Rule` is not applied to the node
/// itself.
///
/// For example,
/// ```
/// auto InlineX =
///     makeRule(declRefExpr(to(varDecl(hasName("x")))), changeTo(cat("3")));
/// makeRule(functionDecl(hasName("f"), hasBody(stmt().bind("body"))).bind("f"),
///          flatten(
///            changeTo(name("f"), cat("newName")),
///            rewriteDescendants("body", InlineX)));
/// ```
/// Here, we find the function `f`, change its name to `newName` and change all
/// appearances of `x` in its body to `3`.
EditGenerator rewriteDescendants(std::string NodeId, RewriteRule Rule);

/// The following three functions are a low-level part of the RewriteRule
/// API. We expose them for use in implementing the fixtures that interpret
/// RewriteRule, like Transformer and TransfomerTidy, or for more advanced
/// users.
//
// FIXME: These functions are really public, if advanced, elements of the
// RewriteRule API.  Recast them as such.  Or, just declare these functions
// public and well-supported and move them out of `detail`.
namespace detail {
/// The following overload set is a version of `rewriteDescendants` that
/// operates directly on the AST, rather than generating a Transformer
/// combinator. It applies `Rule` to all descendants of `Node`, although not
/// `Node` itself. `Rule` can refer to nodes bound in `Result`.
///
/// For example, assuming that "body" is bound to a function body in MatchResult
/// `Results`, this will produce edits to change all appearances of `x` in that
/// body to `3`.
/// ```
/// auto InlineX =
///     makeRule(declRefExpr(to(varDecl(hasName("x")))), changeTo(cat("3")));
/// const auto *Node = Results.Nodes.getNodeAs<Stmt>("body");
/// auto Edits = rewriteDescendants(*Node, InlineX, Results);
/// ```
/// @{
llvm::Expected<SmallVector<Edit, 1>>
rewriteDescendants(const Decl &Node, RewriteRule Rule,
                   const ast_matchers::MatchFinder::MatchResult &Result);

llvm::Expected<SmallVector<Edit, 1>>
rewriteDescendants(const Stmt &Node, RewriteRule Rule,
                   const ast_matchers::MatchFinder::MatchResult &Result);

llvm::Expected<SmallVector<Edit, 1>>
rewriteDescendants(const TypeLoc &Node, RewriteRule Rule,
                   const ast_matchers::MatchFinder::MatchResult &Result);

llvm::Expected<SmallVector<Edit, 1>>
rewriteDescendants(const DynTypedNode &Node, RewriteRule Rule,
                   const ast_matchers::MatchFinder::MatchResult &Result);
/// @}

/// Builds a single matcher for the rule, covering all of the rule's cases.
/// Only supports Rules whose cases' matchers share the same base "kind"
/// (`Stmt`, `Decl`, etc.)  Deprecated: use `buildMatchers` instead, which
/// supports mixing matchers of different kinds.
ast_matchers::internal::DynTypedMatcher
buildMatcher(const RewriteRuleBase &Rule);

/// Builds a set of matchers that cover the rule.
///
/// One matcher is built for each distinct node matcher base kind: Stmt, Decl,
/// etc. Node-matchers for `QualType` and `Type` are not permitted, since such
/// nodes carry no source location information and are therefore not relevant
/// for rewriting. If any such matchers are included, will return an empty
/// vector.
std::vector<ast_matchers::internal::DynTypedMatcher>
buildMatchers(const RewriteRuleBase &Rule);

/// Gets the beginning location of the source matched by a rewrite rule. If the
/// match occurs within a macro expansion, returns the beginning of the
/// expansion point. `Result` must come from the matching of a rewrite rule.
SourceLocation
getRuleMatchLoc(const ast_matchers::MatchFinder::MatchResult &Result);

/// Returns the index of the \c Case of \c Rule that was selected in the match
/// result. Assumes a matcher built with \c buildMatcher.
size_t findSelectedCase(const ast_matchers::MatchFinder::MatchResult &Result,
                        const RewriteRuleBase &Rule);
} // namespace detail
} // namespace transformer
} // namespace clang

#endif // LLVM_CLANG_TOOLING_TRANSFORMER_REWRITERULE_H
