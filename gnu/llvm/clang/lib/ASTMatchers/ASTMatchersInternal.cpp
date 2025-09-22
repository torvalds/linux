//===- ASTMatchersInternal.cpp - Structural query framework ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Implements the base layer of the matcher framework.
//
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/LLVM.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clang {
namespace ast_matchers {

AST_MATCHER_P(ObjCMessageExpr, hasAnySelectorMatcher, std::vector<std::string>,
              Matches) {
  return llvm::is_contained(Matches, Node.getSelector().getAsString());
}

namespace internal {

static bool notUnaryOperator(const DynTypedNode &DynNode,
                             ASTMatchFinder *Finder,
                             BoundNodesTreeBuilder *Builder,
                             ArrayRef<DynTypedMatcher> InnerMatchers);

static bool allOfVariadicOperator(const DynTypedNode &DynNode,
                                  ASTMatchFinder *Finder,
                                  BoundNodesTreeBuilder *Builder,
                                  ArrayRef<DynTypedMatcher> InnerMatchers);

static bool eachOfVariadicOperator(const DynTypedNode &DynNode,
                                   ASTMatchFinder *Finder,
                                   BoundNodesTreeBuilder *Builder,
                                   ArrayRef<DynTypedMatcher> InnerMatchers);

static bool anyOfVariadicOperator(const DynTypedNode &DynNode,
                                  ASTMatchFinder *Finder,
                                  BoundNodesTreeBuilder *Builder,
                                  ArrayRef<DynTypedMatcher> InnerMatchers);

static bool optionallyVariadicOperator(const DynTypedNode &DynNode,
                                       ASTMatchFinder *Finder,
                                       BoundNodesTreeBuilder *Builder,
                                       ArrayRef<DynTypedMatcher> InnerMatchers);

bool matchesAnyBase(const CXXRecordDecl &Node,
                    const Matcher<CXXBaseSpecifier> &BaseSpecMatcher,
                    ASTMatchFinder *Finder, BoundNodesTreeBuilder *Builder) {
  if (!Node.hasDefinition())
    return false;

  CXXBasePaths Paths;
  Paths.setOrigin(&Node);

  const auto basePredicate =
      [Finder, Builder, &BaseSpecMatcher](const CXXBaseSpecifier *BaseSpec,
                                          CXXBasePath &IgnoredParam) {
        BoundNodesTreeBuilder Result(*Builder);
        if (BaseSpecMatcher.matches(*BaseSpec, Finder, &Result)) {
          *Builder = std::move(Result);
          return true;
        }
        return false;
      };

  return Node.lookupInBases(basePredicate, Paths,
                            /*LookupInDependent =*/true);
}

void BoundNodesTreeBuilder::visitMatches(Visitor *ResultVisitor) {
  if (Bindings.empty())
    Bindings.push_back(BoundNodesMap());
  for (BoundNodesMap &Binding : Bindings) {
    ResultVisitor->visitMatch(BoundNodes(Binding));
  }
}

namespace {

using VariadicOperatorFunction = bool (*)(
    const DynTypedNode &DynNode, ASTMatchFinder *Finder,
    BoundNodesTreeBuilder *Builder, ArrayRef<DynTypedMatcher> InnerMatchers);

template <VariadicOperatorFunction Func>
class VariadicMatcher : public DynMatcherInterface {
public:
  VariadicMatcher(std::vector<DynTypedMatcher> InnerMatchers)
      : InnerMatchers(std::move(InnerMatchers)) {}

  bool dynMatches(const DynTypedNode &DynNode, ASTMatchFinder *Finder,
                  BoundNodesTreeBuilder *Builder) const override {
    return Func(DynNode, Finder, Builder, InnerMatchers);
  }

private:
  std::vector<DynTypedMatcher> InnerMatchers;
};

class IdDynMatcher : public DynMatcherInterface {
public:
  IdDynMatcher(StringRef ID,
               IntrusiveRefCntPtr<DynMatcherInterface> InnerMatcher)
      : ID(ID), InnerMatcher(std::move(InnerMatcher)) {}

  bool dynMatches(const DynTypedNode &DynNode, ASTMatchFinder *Finder,
                  BoundNodesTreeBuilder *Builder) const override {
    bool Result = InnerMatcher->dynMatches(DynNode, Finder, Builder);
    if (Result) Builder->setBinding(ID, DynNode);
    return Result;
  }

  std::optional<clang::TraversalKind> TraversalKind() const override {
    return InnerMatcher->TraversalKind();
  }

private:
  const std::string ID;
  const IntrusiveRefCntPtr<DynMatcherInterface> InnerMatcher;
};

/// A matcher that always returns true.
class TrueMatcherImpl : public DynMatcherInterface {
public:
  TrueMatcherImpl() = default;

  bool dynMatches(const DynTypedNode &, ASTMatchFinder *,
                  BoundNodesTreeBuilder *) const override {
    return true;
  }
};

/// A matcher that specifies a particular \c TraversalKind.
///
/// The kind provided to the constructor overrides any kind that may be
/// specified by the `InnerMatcher`.
class DynTraversalMatcherImpl : public DynMatcherInterface {
public:
  explicit DynTraversalMatcherImpl(
      clang::TraversalKind TK,
      IntrusiveRefCntPtr<DynMatcherInterface> InnerMatcher)
      : TK(TK), InnerMatcher(std::move(InnerMatcher)) {}

  bool dynMatches(const DynTypedNode &DynNode, ASTMatchFinder *Finder,
                  BoundNodesTreeBuilder *Builder) const override {
    return this->InnerMatcher->dynMatches(DynNode, Finder, Builder);
  }

  std::optional<clang::TraversalKind> TraversalKind() const override {
    return TK;
  }

private:
  clang::TraversalKind TK;
  IntrusiveRefCntPtr<DynMatcherInterface> InnerMatcher;
};

} // namespace

bool ASTMatchFinder::isTraversalIgnoringImplicitNodes() const {
  return getASTContext().getParentMapContext().getTraversalKind() ==
         TK_IgnoreUnlessSpelledInSource;
}

DynTypedMatcher
DynTypedMatcher::constructVariadic(DynTypedMatcher::VariadicOperator Op,
                                   ASTNodeKind SupportedKind,
                                   std::vector<DynTypedMatcher> InnerMatchers) {
  assert(!InnerMatchers.empty() && "Array must not be empty.");
  assert(llvm::all_of(InnerMatchers,
                      [SupportedKind](const DynTypedMatcher &M) {
                        return M.canConvertTo(SupportedKind);
                      }) &&
         "InnerMatchers must be convertible to SupportedKind!");

  // We must relax the restrict kind here.
  // The different operators might deal differently with a mismatch.
  // Make it the same as SupportedKind, since that is the broadest type we are
  // allowed to accept.
  auto RestrictKind = SupportedKind;

  switch (Op) {
  case VO_AllOf:
    // In the case of allOf() we must pass all the checks, so making
    // RestrictKind the most restrictive can save us time. This way we reject
    // invalid types earlier and we can elide the kind checks inside the
    // matcher.
    for (auto &IM : InnerMatchers) {
      RestrictKind =
          ASTNodeKind::getMostDerivedType(RestrictKind, IM.RestrictKind);
    }
    return DynTypedMatcher(
        SupportedKind, RestrictKind,
        new VariadicMatcher<allOfVariadicOperator>(std::move(InnerMatchers)));

  case VO_AnyOf:
    return DynTypedMatcher(
        SupportedKind, RestrictKind,
        new VariadicMatcher<anyOfVariadicOperator>(std::move(InnerMatchers)));

  case VO_EachOf:
    return DynTypedMatcher(
        SupportedKind, RestrictKind,
        new VariadicMatcher<eachOfVariadicOperator>(std::move(InnerMatchers)));

  case VO_Optionally:
    return DynTypedMatcher(SupportedKind, RestrictKind,
                           new VariadicMatcher<optionallyVariadicOperator>(
                               std::move(InnerMatchers)));

  case VO_UnaryNot:
    // FIXME: Implement the Not operator to take a single matcher instead of a
    // vector.
    return DynTypedMatcher(
        SupportedKind, RestrictKind,
        new VariadicMatcher<notUnaryOperator>(std::move(InnerMatchers)));
  }
  llvm_unreachable("Invalid Op value.");
}

DynTypedMatcher
DynTypedMatcher::constructRestrictedWrapper(const DynTypedMatcher &InnerMatcher,
                                            ASTNodeKind RestrictKind) {
  DynTypedMatcher Copy = InnerMatcher;
  Copy.RestrictKind = RestrictKind;
  return Copy;
}

DynTypedMatcher DynTypedMatcher::withTraversalKind(TraversalKind TK) {
  auto Copy = *this;
  Copy.Implementation =
      new DynTraversalMatcherImpl(TK, std::move(Copy.Implementation));
  return Copy;
}

DynTypedMatcher DynTypedMatcher::trueMatcher(ASTNodeKind NodeKind) {
  // We only ever need one instance of TrueMatcherImpl, so we create a static
  // instance and reuse it to reduce the overhead of the matcher and increase
  // the chance of cache hits.
  static const llvm::IntrusiveRefCntPtr<TrueMatcherImpl> Instance =
      new TrueMatcherImpl();
  return DynTypedMatcher(NodeKind, NodeKind, Instance);
}

bool DynTypedMatcher::canMatchNodesOfKind(ASTNodeKind Kind) const {
  return RestrictKind.isBaseOf(Kind);
}

DynTypedMatcher DynTypedMatcher::dynCastTo(const ASTNodeKind Kind) const {
  auto Copy = *this;
  Copy.SupportedKind = Kind;
  Copy.RestrictKind = ASTNodeKind::getMostDerivedType(Kind, RestrictKind);
  return Copy;
}

bool DynTypedMatcher::matches(const DynTypedNode &DynNode,
                              ASTMatchFinder *Finder,
                              BoundNodesTreeBuilder *Builder) const {
  TraversalKindScope RAII(Finder->getASTContext(),
                          Implementation->TraversalKind());

  if (Finder->isTraversalIgnoringImplicitNodes() &&
      Finder->IsMatchingInASTNodeNotSpelledInSource())
    return false;

  if (!Finder->isTraversalIgnoringImplicitNodes() &&
      Finder->IsMatchingInASTNodeNotAsIs())
    return false;

  auto N =
      Finder->getASTContext().getParentMapContext().traverseIgnored(DynNode);

  if (RestrictKind.isBaseOf(N.getNodeKind()) &&
      Implementation->dynMatches(N, Finder, Builder)) {
    return true;
  }
  // Delete all bindings when a matcher does not match.
  // This prevents unexpected exposure of bound nodes in unmatches
  // branches of the match tree.
  Builder->removeBindings([](const BoundNodesMap &) { return true; });
  return false;
}

bool DynTypedMatcher::matchesNoKindCheck(const DynTypedNode &DynNode,
                                         ASTMatchFinder *Finder,
                                         BoundNodesTreeBuilder *Builder) const {
  TraversalKindScope raii(Finder->getASTContext(),
                          Implementation->TraversalKind());

  if (Finder->isTraversalIgnoringImplicitNodes() &&
      Finder->IsMatchingInASTNodeNotSpelledInSource())
    return false;

  if (!Finder->isTraversalIgnoringImplicitNodes() &&
      Finder->IsMatchingInASTNodeNotAsIs())
    return false;

  auto N =
      Finder->getASTContext().getParentMapContext().traverseIgnored(DynNode);

  assert(RestrictKind.isBaseOf(N.getNodeKind()));
  if (Implementation->dynMatches(N, Finder, Builder)) {
    return true;
  }
  // Delete all bindings when a matcher does not match.
  // This prevents unexpected exposure of bound nodes in unmatches
  // branches of the match tree.
  Builder->removeBindings([](const BoundNodesMap &) { return true; });
  return false;
}

std::optional<DynTypedMatcher> DynTypedMatcher::tryBind(StringRef ID) const {
  if (!AllowBind)
    return std::nullopt;
  auto Result = *this;
  Result.Implementation =
      new IdDynMatcher(ID, std::move(Result.Implementation));
  return std::move(Result);
}

bool DynTypedMatcher::canConvertTo(ASTNodeKind To) const {
  const auto From = getSupportedKind();
  auto QualKind = ASTNodeKind::getFromNodeKind<QualType>();
  auto TypeKind = ASTNodeKind::getFromNodeKind<Type>();
  /// Mimic the implicit conversions of Matcher<>.
  /// - From Matcher<Type> to Matcher<QualType>
  if (From.isSame(TypeKind) && To.isSame(QualKind)) return true;
  /// - From Matcher<Base> to Matcher<Derived>
  return From.isBaseOf(To);
}

void BoundNodesTreeBuilder::addMatch(const BoundNodesTreeBuilder &Other) {
  Bindings.append(Other.Bindings.begin(), Other.Bindings.end());
}

static bool notUnaryOperator(const DynTypedNode &DynNode,
                             ASTMatchFinder *Finder,
                             BoundNodesTreeBuilder *Builder,
                             ArrayRef<DynTypedMatcher> InnerMatchers) {
  if (InnerMatchers.size() != 1)
    return false;

  // The 'unless' matcher will always discard the result:
  // If the inner matcher doesn't match, unless returns true,
  // but the inner matcher cannot have bound anything.
  // If the inner matcher matches, the result is false, and
  // any possible binding will be discarded.
  // We still need to hand in all the bound nodes up to this
  // point so the inner matcher can depend on bound nodes,
  // and we need to actively discard the bound nodes, otherwise
  // the inner matcher will reset the bound nodes if it doesn't
  // match, but this would be inversed by 'unless'.
  BoundNodesTreeBuilder Discard(*Builder);
  return !InnerMatchers[0].matches(DynNode, Finder, &Discard);
}

static bool allOfVariadicOperator(const DynTypedNode &DynNode,
                                  ASTMatchFinder *Finder,
                                  BoundNodesTreeBuilder *Builder,
                                  ArrayRef<DynTypedMatcher> InnerMatchers) {
  // allOf leads to one matcher for each alternative in the first
  // matcher combined with each alternative in the second matcher.
  // Thus, we can reuse the same Builder.
  return llvm::all_of(InnerMatchers, [&](const DynTypedMatcher &InnerMatcher) {
    return InnerMatcher.matchesNoKindCheck(DynNode, Finder, Builder);
  });
}

static bool eachOfVariadicOperator(const DynTypedNode &DynNode,
                                   ASTMatchFinder *Finder,
                                   BoundNodesTreeBuilder *Builder,
                                   ArrayRef<DynTypedMatcher> InnerMatchers) {
  BoundNodesTreeBuilder Result;
  bool Matched = false;
  for (const DynTypedMatcher &InnerMatcher : InnerMatchers) {
    BoundNodesTreeBuilder BuilderInner(*Builder);
    if (InnerMatcher.matches(DynNode, Finder, &BuilderInner)) {
      Matched = true;
      Result.addMatch(BuilderInner);
    }
  }
  *Builder = std::move(Result);
  return Matched;
}

static bool anyOfVariadicOperator(const DynTypedNode &DynNode,
                                  ASTMatchFinder *Finder,
                                  BoundNodesTreeBuilder *Builder,
                                  ArrayRef<DynTypedMatcher> InnerMatchers) {
  for (const DynTypedMatcher &InnerMatcher : InnerMatchers) {
    BoundNodesTreeBuilder Result = *Builder;
    if (InnerMatcher.matches(DynNode, Finder, &Result)) {
      *Builder = std::move(Result);
      return true;
    }
  }
  return false;
}

static bool
optionallyVariadicOperator(const DynTypedNode &DynNode, ASTMatchFinder *Finder,
                           BoundNodesTreeBuilder *Builder,
                           ArrayRef<DynTypedMatcher> InnerMatchers) {
  if (InnerMatchers.size() != 1)
    return false;

  BoundNodesTreeBuilder Result(*Builder);
  if (InnerMatchers[0].matches(DynNode, Finder, &Result))
    *Builder = std::move(Result);
  return true;
}

inline static
std::vector<std::string> vectorFromRefs(ArrayRef<const StringRef *> NameRefs) {
  std::vector<std::string> Names;
  Names.reserve(NameRefs.size());
  for (auto *Name : NameRefs)
    Names.emplace_back(*Name);
  return Names;
}

Matcher<NamedDecl> hasAnyNameFunc(ArrayRef<const StringRef *> NameRefs) {
  return internal::Matcher<NamedDecl>(
      new internal::HasNameMatcher(vectorFromRefs(NameRefs)));
}

Matcher<ObjCMessageExpr> hasAnySelectorFunc(
    ArrayRef<const StringRef *> NameRefs) {
  return hasAnySelectorMatcher(vectorFromRefs(NameRefs));
}

HasOpNameMatcher hasAnyOperatorNameFunc(ArrayRef<const StringRef *> NameRefs) {
  return HasOpNameMatcher(vectorFromRefs(NameRefs));
}

HasOverloadOpNameMatcher
hasAnyOverloadedOperatorNameFunc(ArrayRef<const StringRef *> NameRefs) {
  return HasOverloadOpNameMatcher(vectorFromRefs(NameRefs));
}

HasNameMatcher::HasNameMatcher(std::vector<std::string> N)
    : UseUnqualifiedMatch(
          llvm::all_of(N, [](StringRef Name) { return !Name.contains("::"); })),
      Names(std::move(N)) {
#ifndef NDEBUG
  for (StringRef Name : Names)
    assert(!Name.empty());
#endif
}

static bool consumeNameSuffix(StringRef &FullName, StringRef Suffix) {
  StringRef Name = FullName;
  if (!Name.ends_with(Suffix))
    return false;
  Name = Name.drop_back(Suffix.size());
  if (!Name.empty()) {
    if (!Name.ends_with("::"))
      return false;
    Name = Name.drop_back(2);
  }
  FullName = Name;
  return true;
}

static StringRef getNodeName(const NamedDecl &Node,
                             llvm::SmallString<128> &Scratch) {
  // Simple name.
  if (Node.getIdentifier())
    return Node.getName();

  if (Node.getDeclName()) {
    // Name needs to be constructed.
    Scratch.clear();
    llvm::raw_svector_ostream OS(Scratch);
    Node.printName(OS);
    return OS.str();
  }

  return "(anonymous)";
}

static StringRef getNodeName(const RecordDecl &Node,
                             llvm::SmallString<128> &Scratch) {
  if (Node.getIdentifier()) {
    return Node.getName();
  }
  Scratch.clear();
  return ("(anonymous " + Node.getKindName() + ")").toStringRef(Scratch);
}

static StringRef getNodeName(const NamespaceDecl &Node,
                             llvm::SmallString<128> &Scratch) {
  return Node.isAnonymousNamespace() ? "(anonymous namespace)" : Node.getName();
}

namespace {

class PatternSet {
public:
  PatternSet(ArrayRef<std::string> Names) {
    Patterns.reserve(Names.size());
    for (StringRef Name : Names)
      Patterns.push_back({Name, Name.starts_with("::")});
  }

  /// Consumes the name suffix from each pattern in the set and removes the ones
  /// that didn't match.
  /// Return true if there are still any patterns left.
  bool consumeNameSuffix(StringRef NodeName, bool CanSkip) {
    for (size_t I = 0; I < Patterns.size();) {
      if (::clang::ast_matchers::internal::consumeNameSuffix(Patterns[I].P,
                                                             NodeName) ||
          CanSkip) {
        ++I;
      } else {
        Patterns.erase(Patterns.begin() + I);
      }
    }
    return !Patterns.empty();
  }

  /// Check if any of the patterns are a match.
  /// A match will be a pattern that was fully consumed, that also matches the
  /// 'fully qualified' requirement.
  bool foundMatch(bool AllowFullyQualified) const {
    return llvm::any_of(Patterns, [&](const Pattern &Pattern) {
      return Pattern.P.empty() &&
             (AllowFullyQualified || !Pattern.IsFullyQualified);
    });
  }

private:
  struct Pattern {
    StringRef P;
    bool IsFullyQualified;
  };

  llvm::SmallVector<Pattern, 8> Patterns;
};

} // namespace

bool HasNameMatcher::matchesNodeUnqualified(const NamedDecl &Node) const {
  assert(UseUnqualifiedMatch);
  llvm::SmallString<128> Scratch;
  StringRef NodeName = getNodeName(Node, Scratch);
  return llvm::any_of(Names, [&](StringRef Name) {
    return consumeNameSuffix(Name, NodeName) && Name.empty();
  });
}

bool HasNameMatcher::matchesNodeFullFast(const NamedDecl &Node) const {
  PatternSet Patterns(Names);
  llvm::SmallString<128> Scratch;

  // This function is copied and adapted from NamedDecl::printQualifiedName()
  // By matching each part individually we optimize in a couple of ways:
  //  - We can exit early on the first failure.
  //  - We can skip inline/anonymous namespaces without another pass.
  //  - We print one name at a time, reducing the chance of overflowing the
  //    inlined space of the SmallString.

  // First, match the name.
  if (!Patterns.consumeNameSuffix(getNodeName(Node, Scratch),
                                  /*CanSkip=*/false))
    return false;

  // Try to match each declaration context.
  // We are allowed to skip anonymous and inline namespaces if they don't match.
  const DeclContext *Ctx = Node.getDeclContext();

  if (Ctx->isFunctionOrMethod())
    return Patterns.foundMatch(/*AllowFullyQualified=*/false);

  for (; Ctx; Ctx = Ctx->getParent()) {
    // Linkage Spec can just be ignored
    // FIXME: Any other DeclContext kinds that can be safely disregarded
    if (isa<LinkageSpecDecl>(Ctx))
      continue;
    if (!isa<NamedDecl>(Ctx))
      break;
    if (Patterns.foundMatch(/*AllowFullyQualified=*/false))
      return true;

    if (const auto *ND = dyn_cast<NamespaceDecl>(Ctx)) {
      // If it matches (or we can skip it), continue.
      if (Patterns.consumeNameSuffix(getNodeName(*ND, Scratch),
                                     /*CanSkip=*/ND->isAnonymousNamespace() ||
                                         ND->isInline()))
        continue;
      return false;
    }
    if (const auto *RD = dyn_cast<RecordDecl>(Ctx)) {
      if (!isa<ClassTemplateSpecializationDecl>(Ctx)) {
        if (Patterns.consumeNameSuffix(getNodeName(*RD, Scratch),
                                       /*CanSkip=*/false))
          continue;

        return false;
      }
    }

    // We don't know how to deal with this DeclContext.
    // Fallback to the slow version of the code.
    return matchesNodeFullSlow(Node);
  }

  return Patterns.foundMatch(/*AllowFullyQualified=*/true);
}

bool HasNameMatcher::matchesNodeFullSlow(const NamedDecl &Node) const {
  const bool SkipUnwrittenCases[] = {false, true};
  for (bool SkipUnwritten : SkipUnwrittenCases) {
    llvm::SmallString<128> NodeName = StringRef("::");
    llvm::raw_svector_ostream OS(NodeName);

    PrintingPolicy Policy = Node.getASTContext().getPrintingPolicy();
    Policy.SuppressUnwrittenScope = SkipUnwritten;
    Policy.SuppressInlineNamespace = SkipUnwritten;
    Node.printQualifiedName(OS, Policy);

    const StringRef FullName = OS.str();

    for (const StringRef Pattern : Names) {
      if (Pattern.starts_with("::")) {
        if (FullName == Pattern)
          return true;
      } else if (FullName.ends_with(Pattern) &&
                 FullName.drop_back(Pattern.size()).ends_with("::")) {
        return true;
      }
    }
  }

  return false;
}

bool HasNameMatcher::matchesNode(const NamedDecl &Node) const {
  assert(matchesNodeFullFast(Node) == matchesNodeFullSlow(Node));
  if (UseUnqualifiedMatch) {
    assert(matchesNodeUnqualified(Node) == matchesNodeFullFast(Node));
    return matchesNodeUnqualified(Node);
  }
  return matchesNodeFullFast(Node);
}

// Checks whether \p Loc points to a token with source text of \p TokenText.
static bool isTokenAtLoc(const SourceManager &SM, const LangOptions &LangOpts,
                         StringRef Text, SourceLocation Loc) {
  llvm::SmallString<16> Buffer;
  bool Invalid = false;
  // Since `Loc` may point into an expansion buffer, which has no corresponding
  // source, we need to look at the spelling location to read the actual source.
  StringRef TokenText = Lexer::getSpelling(SM.getSpellingLoc(Loc), Buffer, SM,
                                           LangOpts, &Invalid);
  return !Invalid && Text == TokenText;
}

std::optional<SourceLocation>
getExpansionLocOfMacro(StringRef MacroName, SourceLocation Loc,
                       const ASTContext &Context) {
  auto &SM = Context.getSourceManager();
  const LangOptions &LangOpts = Context.getLangOpts();
  while (Loc.isMacroID()) {
    SrcMgr::ExpansionInfo Expansion =
        SM.getSLocEntry(SM.getFileID(Loc)).getExpansion();
    if (Expansion.isMacroArgExpansion())
      // Check macro argument for an expansion of the given macro. For example,
      // `F(G(3))`, where `MacroName` is `G`.
      if (std::optional<SourceLocation> ArgLoc = getExpansionLocOfMacro(
              MacroName, Expansion.getSpellingLoc(), Context))
        return ArgLoc;
    Loc = Expansion.getExpansionLocStart();
    if (isTokenAtLoc(SM, LangOpts, MacroName, Loc))
      return Loc;
  }
  return std::nullopt;
}

std::shared_ptr<llvm::Regex> createAndVerifyRegex(StringRef Regex,
                                                  llvm::Regex::RegexFlags Flags,
                                                  StringRef MatcherID) {
  assert(!Regex.empty() && "Empty regex string");
  auto SharedRegex = std::make_shared<llvm::Regex>(Regex, Flags);
  std::string Error;
  if (!SharedRegex->isValid(Error)) {
    llvm::WithColor::error()
        << "building matcher '" << MatcherID << "': " << Error << "\n";
    llvm::WithColor::note() << " input was '" << Regex << "'\n";
  }
  return SharedRegex;
}
} // end namespace internal

const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCAutoreleasePoolStmt>
    autoreleasePoolStmt;
const internal::VariadicDynCastAllOfMatcher<Decl, TranslationUnitDecl>
    translationUnitDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, TypedefDecl> typedefDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, TypedefNameDecl>
    typedefNameDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, TypeAliasDecl> typeAliasDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, TypeAliasTemplateDecl>
    typeAliasTemplateDecl;
const internal::VariadicAllOfMatcher<Decl> decl;
const internal::VariadicDynCastAllOfMatcher<Decl, DecompositionDecl> decompositionDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, BindingDecl> bindingDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, LinkageSpecDecl>
    linkageSpecDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, NamedDecl> namedDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, LabelDecl> labelDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, NamespaceDecl> namespaceDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, NamespaceAliasDecl>
    namespaceAliasDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, RecordDecl> recordDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, CXXRecordDecl> cxxRecordDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ClassTemplateDecl>
    classTemplateDecl;
const internal::VariadicDynCastAllOfMatcher<Decl,
                                            ClassTemplateSpecializationDecl>
    classTemplateSpecializationDecl;
const internal::VariadicDynCastAllOfMatcher<
    Decl, ClassTemplatePartialSpecializationDecl>
    classTemplatePartialSpecializationDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, DeclaratorDecl>
    declaratorDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ParmVarDecl> parmVarDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, AccessSpecDecl>
    accessSpecDecl;
const internal::VariadicAllOfMatcher<CXXBaseSpecifier> cxxBaseSpecifier;
const internal::VariadicAllOfMatcher<CXXCtorInitializer> cxxCtorInitializer;
const internal::VariadicAllOfMatcher<TemplateArgument> templateArgument;
const internal::VariadicAllOfMatcher<TemplateArgumentLoc> templateArgumentLoc;
const internal::VariadicAllOfMatcher<TemplateName> templateName;
const internal::VariadicDynCastAllOfMatcher<Decl, NonTypeTemplateParmDecl>
    nonTypeTemplateParmDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, TemplateTypeParmDecl>
    templateTypeParmDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, TemplateTemplateParmDecl>
    templateTemplateParmDecl;

const internal::VariadicAllOfMatcher<LambdaCapture> lambdaCapture;
const internal::VariadicAllOfMatcher<QualType> qualType;
const internal::VariadicAllOfMatcher<Type> type;
const internal::VariadicAllOfMatcher<TypeLoc> typeLoc;

const internal::VariadicDynCastAllOfMatcher<TypeLoc, QualifiedTypeLoc>
    qualifiedTypeLoc;
const internal::VariadicDynCastAllOfMatcher<TypeLoc, PointerTypeLoc>
    pointerTypeLoc;
const internal::VariadicDynCastAllOfMatcher<TypeLoc, ReferenceTypeLoc>
    referenceTypeLoc;
const internal::VariadicDynCastAllOfMatcher<TypeLoc,
                                            TemplateSpecializationTypeLoc>
    templateSpecializationTypeLoc;
const internal::VariadicDynCastAllOfMatcher<TypeLoc, ElaboratedTypeLoc>
    elaboratedTypeLoc;

const internal::VariadicDynCastAllOfMatcher<Stmt, UnaryExprOrTypeTraitExpr>
    unaryExprOrTypeTraitExpr;
const internal::VariadicDynCastAllOfMatcher<Decl, ValueDecl> valueDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, CXXConstructorDecl>
    cxxConstructorDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, CXXDestructorDecl>
    cxxDestructorDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, EnumDecl> enumDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, EnumConstantDecl>
    enumConstantDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, TagDecl> tagDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, CXXMethodDecl> cxxMethodDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, CXXConversionDecl>
    cxxConversionDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ConceptDecl> conceptDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, VarDecl> varDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, FieldDecl> fieldDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, IndirectFieldDecl>
    indirectFieldDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, FunctionDecl> functionDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, FunctionTemplateDecl>
    functionTemplateDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, FriendDecl> friendDecl;
const internal::VariadicAllOfMatcher<Stmt> stmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, DeclStmt> declStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, MemberExpr> memberExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, UnresolvedMemberExpr>
    unresolvedMemberExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXDependentScopeMemberExpr>
    cxxDependentScopeMemberExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CallExpr> callExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, LambdaExpr> lambdaExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXMemberCallExpr>
    cxxMemberCallExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCMessageExpr>
    objcMessageExpr;
const internal::VariadicDynCastAllOfMatcher<Decl, ObjCInterfaceDecl>
    objcInterfaceDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ObjCImplementationDecl>
    objcImplementationDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ObjCProtocolDecl>
    objcProtocolDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ObjCCategoryDecl>
    objcCategoryDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ObjCCategoryImplDecl>
    objcCategoryImplDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ObjCMethodDecl>
    objcMethodDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, BlockDecl>
    blockDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ObjCIvarDecl> objcIvarDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, ObjCPropertyDecl>
    objcPropertyDecl;
const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCAtThrowStmt>
    objcThrowStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCAtTryStmt> objcTryStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCAtCatchStmt>
    objcCatchStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCAtFinallyStmt>
    objcFinallyStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, ExprWithCleanups>
    exprWithCleanups;
const internal::VariadicDynCastAllOfMatcher<Stmt, InitListExpr> initListExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXStdInitializerListExpr>
    cxxStdInitializerListExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ImplicitValueInitExpr>
    implicitValueInitExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ParenListExpr> parenListExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, SubstNonTypeTemplateParmExpr>
    substNonTypeTemplateParmExpr;
const internal::VariadicDynCastAllOfMatcher<Decl, UsingDecl> usingDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, UsingEnumDecl> usingEnumDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, UsingDirectiveDecl>
    usingDirectiveDecl;
const internal::VariadicDynCastAllOfMatcher<Stmt, UnresolvedLookupExpr>
    unresolvedLookupExpr;
const internal::VariadicDynCastAllOfMatcher<Decl, UnresolvedUsingValueDecl>
    unresolvedUsingValueDecl;
const internal::VariadicDynCastAllOfMatcher<Decl, UnresolvedUsingTypenameDecl>
    unresolvedUsingTypenameDecl;
const internal::VariadicDynCastAllOfMatcher<Stmt, ConstantExpr> constantExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ParenExpr> parenExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXConstructExpr>
    cxxConstructExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXUnresolvedConstructExpr>
    cxxUnresolvedConstructExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXThisExpr> cxxThisExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXBindTemporaryExpr>
    cxxBindTemporaryExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, MaterializeTemporaryExpr>
    materializeTemporaryExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXNewExpr> cxxNewExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXDeleteExpr> cxxDeleteExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXNoexceptExpr>
    cxxNoexceptExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ArraySubscriptExpr>
    arraySubscriptExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ArrayInitIndexExpr>
    arrayInitIndexExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ArrayInitLoopExpr>
    arrayInitLoopExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXDefaultArgExpr>
    cxxDefaultArgExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXOperatorCallExpr>
    cxxOperatorCallExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXRewrittenBinaryOperator>
    cxxRewrittenBinaryOperator;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXFoldExpr> cxxFoldExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, Expr> expr;
const internal::VariadicDynCastAllOfMatcher<Stmt, DeclRefExpr> declRefExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCIvarRefExpr> objcIvarRefExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, BlockExpr> blockExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, IfStmt> ifStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, ForStmt> forStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXForRangeStmt>
    cxxForRangeStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, WhileStmt> whileStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, DoStmt> doStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, BreakStmt> breakStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, ContinueStmt> continueStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, CoreturnStmt> coreturnStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, ReturnStmt> returnStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, GotoStmt> gotoStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, LabelStmt> labelStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, AddrLabelExpr> addrLabelExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, SwitchStmt> switchStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, SwitchCase> switchCase;
const internal::VariadicDynCastAllOfMatcher<Stmt, CaseStmt> caseStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, DefaultStmt> defaultStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, CompoundStmt> compoundStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, CoroutineBodyStmt>
    coroutineBodyStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXCatchStmt> cxxCatchStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXTryStmt> cxxTryStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXThrowExpr> cxxThrowExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, NullStmt> nullStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, AsmStmt> asmStmt;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXBoolLiteralExpr>
    cxxBoolLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, StringLiteral> stringLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCStringLiteral> objcStringLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, CharacterLiteral>
    characterLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, IntegerLiteral>
    integerLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, FloatingLiteral> floatLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, ImaginaryLiteral> imaginaryLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, FixedPointLiteral>
    fixedPointLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, UserDefinedLiteral>
    userDefinedLiteral;
const internal::VariadicDynCastAllOfMatcher<Stmt, CompoundLiteralExpr>
    compoundLiteralExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXNullPtrLiteralExpr>
    cxxNullPtrLiteralExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ChooseExpr> chooseExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ConvertVectorExpr>
    convertVectorExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CoawaitExpr>
    coawaitExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, DependentCoawaitExpr>
    dependentCoawaitExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CoyieldExpr>
    coyieldExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, GNUNullExpr> gnuNullExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, GenericSelectionExpr>
    genericSelectionExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, AtomicExpr> atomicExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, StmtExpr> stmtExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, BinaryOperator>
    binaryOperator;
const internal::MapAnyOfMatcher<BinaryOperator, CXXOperatorCallExpr,
                                CXXRewrittenBinaryOperator>
    binaryOperation;
const internal::MapAnyOfMatcher<CallExpr, CXXConstructExpr> invocation;
const internal::VariadicDynCastAllOfMatcher<Stmt, UnaryOperator> unaryOperator;
const internal::VariadicDynCastAllOfMatcher<Stmt, ConditionalOperator>
    conditionalOperator;
const internal::VariadicDynCastAllOfMatcher<Stmt, BinaryConditionalOperator>
    binaryConditionalOperator;
const internal::VariadicDynCastAllOfMatcher<Stmt, OpaqueValueExpr>
    opaqueValueExpr;
const internal::VariadicDynCastAllOfMatcher<Decl, StaticAssertDecl>
    staticAssertDecl;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXReinterpretCastExpr>
    cxxReinterpretCastExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXStaticCastExpr>
    cxxStaticCastExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXDynamicCastExpr>
    cxxDynamicCastExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXConstCastExpr>
    cxxConstCastExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CStyleCastExpr>
    cStyleCastExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ExplicitCastExpr>
    explicitCastExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, ImplicitCastExpr>
    implicitCastExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CastExpr> castExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXFunctionalCastExpr>
    cxxFunctionalCastExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CXXTemporaryObjectExpr>
    cxxTemporaryObjectExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, PredefinedExpr>
    predefinedExpr;
const internal::VariadicDynCastAllOfMatcher<Stmt, DesignatedInitExpr>
    designatedInitExpr;
const internal::VariadicOperatorMatcherFunc<
    2, std::numeric_limits<unsigned>::max()>
    eachOf = {internal::DynTypedMatcher::VO_EachOf};
const internal::VariadicOperatorMatcherFunc<
    2, std::numeric_limits<unsigned>::max()>
    anyOf = {internal::DynTypedMatcher::VO_AnyOf};
const internal::VariadicOperatorMatcherFunc<
    2, std::numeric_limits<unsigned>::max()>
    allOf = {internal::DynTypedMatcher::VO_AllOf};
const internal::VariadicOperatorMatcherFunc<1, 1> optionally = {
    internal::DynTypedMatcher::VO_Optionally};
const internal::VariadicFunction<internal::Matcher<NamedDecl>, StringRef,
                                 internal::hasAnyNameFunc>
    hasAnyName = {};

const internal::VariadicFunction<internal::HasOpNameMatcher, StringRef,
                                 internal::hasAnyOperatorNameFunc>
    hasAnyOperatorName = {};
const internal::VariadicFunction<internal::HasOverloadOpNameMatcher, StringRef,
                                 internal::hasAnyOverloadedOperatorNameFunc>
    hasAnyOverloadedOperatorName = {};
const internal::VariadicFunction<internal::Matcher<ObjCMessageExpr>, StringRef,
                                 internal::hasAnySelectorFunc>
    hasAnySelector = {};
const internal::ArgumentAdaptingMatcherFunc<internal::HasMatcher> has = {};
const internal::ArgumentAdaptingMatcherFunc<internal::HasDescendantMatcher>
    hasDescendant = {};
const internal::ArgumentAdaptingMatcherFunc<internal::ForEachMatcher> forEach =
    {};
const internal::ArgumentAdaptingMatcherFunc<internal::ForEachDescendantMatcher>
    forEachDescendant = {};
const internal::ArgumentAdaptingMatcherFunc<
    internal::HasParentMatcher,
    internal::TypeList<Decl, NestedNameSpecifierLoc, Stmt, TypeLoc, Attr>,
    internal::TypeList<Decl, NestedNameSpecifierLoc, Stmt, TypeLoc, Attr>>
    hasParent = {};
const internal::ArgumentAdaptingMatcherFunc<
    internal::HasAncestorMatcher,
    internal::TypeList<Decl, NestedNameSpecifierLoc, Stmt, TypeLoc, Attr>,
    internal::TypeList<Decl, NestedNameSpecifierLoc, Stmt, TypeLoc, Attr>>
    hasAncestor = {};
const internal::VariadicOperatorMatcherFunc<1, 1> unless = {
    internal::DynTypedMatcher::VO_UnaryNot};
const internal::VariadicAllOfMatcher<NestedNameSpecifier> nestedNameSpecifier;
const internal::VariadicAllOfMatcher<NestedNameSpecifierLoc>
    nestedNameSpecifierLoc;
const internal::VariadicAllOfMatcher<Attr> attr;
const internal::VariadicDynCastAllOfMatcher<Stmt, CUDAKernelCallExpr>
    cudaKernelCallExpr;
const AstTypeMatcher<BuiltinType> builtinType;
const AstTypeMatcher<ArrayType> arrayType;
const AstTypeMatcher<ComplexType> complexType;
const AstTypeMatcher<ConstantArrayType> constantArrayType;
const AstTypeMatcher<DeducedTemplateSpecializationType>
    deducedTemplateSpecializationType;
const AstTypeMatcher<DependentSizedArrayType> dependentSizedArrayType;
const AstTypeMatcher<DependentSizedExtVectorType> dependentSizedExtVectorType;
const AstTypeMatcher<IncompleteArrayType> incompleteArrayType;
const AstTypeMatcher<VariableArrayType> variableArrayType;
const AstTypeMatcher<AtomicType> atomicType;
const AstTypeMatcher<AutoType> autoType;
const AstTypeMatcher<DecltypeType> decltypeType;
const AstTypeMatcher<FunctionType> functionType;
const AstTypeMatcher<FunctionProtoType> functionProtoType;
const AstTypeMatcher<ParenType> parenType;
const AstTypeMatcher<BlockPointerType> blockPointerType;
const AstTypeMatcher<MacroQualifiedType> macroQualifiedType;
const AstTypeMatcher<MemberPointerType> memberPointerType;
const AstTypeMatcher<PointerType> pointerType;
const AstTypeMatcher<ObjCObjectPointerType> objcObjectPointerType;
const AstTypeMatcher<ReferenceType> referenceType;
const AstTypeMatcher<LValueReferenceType> lValueReferenceType;
const AstTypeMatcher<RValueReferenceType> rValueReferenceType;
const AstTypeMatcher<TypedefType> typedefType;
const AstTypeMatcher<EnumType> enumType;
const AstTypeMatcher<TemplateSpecializationType> templateSpecializationType;
const AstTypeMatcher<UnaryTransformType> unaryTransformType;
const AstTypeMatcher<RecordType> recordType;
const AstTypeMatcher<TagType> tagType;
const AstTypeMatcher<ElaboratedType> elaboratedType;
const AstTypeMatcher<UsingType> usingType;
const AstTypeMatcher<SubstTemplateTypeParmType> substTemplateTypeParmType;
const AstTypeMatcher<TemplateTypeParmType> templateTypeParmType;
const AstTypeMatcher<InjectedClassNameType> injectedClassNameType;
const AstTypeMatcher<DecayedType> decayedType;
AST_TYPELOC_TRAVERSE_MATCHER_DEF(hasElementType,
                                 AST_POLYMORPHIC_SUPPORTED_TYPES(ArrayType,
                                                                 ComplexType));
AST_TYPELOC_TRAVERSE_MATCHER_DEF(hasValueType,
                                 AST_POLYMORPHIC_SUPPORTED_TYPES(AtomicType));
AST_TYPELOC_TRAVERSE_MATCHER_DEF(
    pointee,
    AST_POLYMORPHIC_SUPPORTED_TYPES(BlockPointerType, MemberPointerType,
                                    PointerType, ReferenceType));

const internal::VariadicDynCastAllOfMatcher<Stmt, OMPExecutableDirective>
    ompExecutableDirective;
const internal::VariadicDynCastAllOfMatcher<OMPClause, OMPDefaultClause>
    ompDefaultClause;
const internal::VariadicDynCastAllOfMatcher<Decl, CXXDeductionGuideDecl>
    cxxDeductionGuideDecl;

} // end namespace ast_matchers
} // end namespace clang
