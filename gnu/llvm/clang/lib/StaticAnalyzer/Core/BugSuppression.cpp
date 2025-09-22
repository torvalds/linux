//===- BugSuppression.cpp - Suppression interface -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/BugReporter/BugSuppression.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"

using namespace clang;
using namespace ento;

namespace {

using Ranges = llvm::SmallVectorImpl<SourceRange>;

inline bool hasSuppression(const Decl *D) {
  // FIXME: Implement diagnostic identifier arguments
  // (checker names, "hashtags").
  if (const auto *Suppression = D->getAttr<SuppressAttr>())
    return !Suppression->isGSL() &&
           (Suppression->diagnosticIdentifiers().empty());
  return false;
}
inline bool hasSuppression(const AttributedStmt *S) {
  // FIXME: Implement diagnostic identifier arguments
  // (checker names, "hashtags").
  return llvm::any_of(S->getAttrs(), [](const Attr *A) {
    const auto *Suppression = dyn_cast<SuppressAttr>(A);
    return Suppression && !Suppression->isGSL() &&
           (Suppression->diagnosticIdentifiers().empty());
  });
}

template <class NodeType> inline SourceRange getRange(const NodeType *Node) {
  return Node->getSourceRange();
}
template <> inline SourceRange getRange(const AttributedStmt *S) {
  // Begin location for attributed statement node seems to be ALWAYS invalid.
  //
  // It is unlikely that we ever report any warnings on suppression
  // attribute itself, but even if we do, we wouldn't want that warning
  // to be suppressed by that same attribute.
  //
  // Long story short, we can use inner statement and it's not going to break
  // anything.
  return getRange(S->getSubStmt());
}

inline bool isLessOrEqual(SourceLocation LHS, SourceLocation RHS,
                          const SourceManager &SM) {
  // SourceManager::isBeforeInTranslationUnit tests for strict
  // inequality, when we need a non-strict comparison (bug
  // can be reported directly on the annotated note).
  // For this reason, we use the following equivalence:
  //
  //   A <= B <==> !(B < A)
  //
  return !SM.isBeforeInTranslationUnit(RHS, LHS);
}

inline bool fullyContains(SourceRange Larger, SourceRange Smaller,
                          const SourceManager &SM) {
  // Essentially this means:
  //
  //   Larger.fullyContains(Smaller)
  //
  // However, that method has a very trivial implementation and couldn't
  // compare regular locations and locations from macro expansions.
  // We could've converted everything into regular locations as a solution,
  // but the following solution seems to be the most bulletproof.
  return isLessOrEqual(Larger.getBegin(), Smaller.getBegin(), SM) &&
         isLessOrEqual(Smaller.getEnd(), Larger.getEnd(), SM);
}

class CacheInitializer : public RecursiveASTVisitor<CacheInitializer> {
public:
  static void initialize(const Decl *D, Ranges &ToInit) {
    CacheInitializer(ToInit).TraverseDecl(const_cast<Decl *>(D));
  }

  bool VisitDecl(Decl *D) {
    // Bug location could be somewhere in the init value of
    // a freshly declared variable.  Even though it looks like the
    // user applied attribute to a statement, it will apply to a
    // variable declaration, and this is where we check for it.
    return VisitAttributedNode(D);
  }

  bool VisitAttributedStmt(AttributedStmt *AS) {
    // When we apply attributes to statements, it actually creates
    // a wrapper statement that only contains attributes and the wrapped
    // statement.
    return VisitAttributedNode(AS);
  }

private:
  template <class NodeType> bool VisitAttributedNode(NodeType *Node) {
    if (hasSuppression(Node)) {
      // TODO: In the future, when we come up with good stable IDs for checkers
      //       we can return a list of kinds to ignore, or all if no arguments
      //       were provided.
      addRange(getRange(Node));
    }
    // We should keep traversing AST.
    return true;
  }

  void addRange(SourceRange R) {
    if (R.isValid()) {
      Result.push_back(R);
    }
  }

  CacheInitializer(Ranges &R) : Result(R) {}
  Ranges &Result;
};

} // end anonymous namespace

// TODO: Introduce stable IDs for checkers and check for those here
//       to be more specific.  Attribute without arguments should still
//       be considered as "suppress all".
//       It is already much finer granularity than what we have now
//       (i.e. removing the whole function from the analysis).
bool BugSuppression::isSuppressed(const BugReport &R) {
  PathDiagnosticLocation Location = R.getLocation();
  PathDiagnosticLocation UniqueingLocation = R.getUniqueingLocation();
  const Decl *DeclWithIssue = R.getDeclWithIssue();

  return isSuppressed(Location, DeclWithIssue, {}) ||
         isSuppressed(UniqueingLocation, DeclWithIssue, {});
}

bool BugSuppression::isSuppressed(const PathDiagnosticLocation &Location,
                                  const Decl *DeclWithIssue,
                                  DiagnosticIdentifierList Hashtags) {
  if (!Location.isValid())
    return false;

  if (!DeclWithIssue) {
    // FIXME: This defeats the purpose of passing DeclWithIssue to begin with.
    // If this branch is ever hit, we're re-doing all the work we've already
    // done as well as perform a lot of work we'll never need.
    // Gladly, none of our on-by-default checkers currently need it.
    DeclWithIssue = ACtx.getTranslationUnitDecl();
  } else {
    // This is the fast path. However, we should still consider the topmost
    // declaration that isn't TranslationUnitDecl, because we should respect
    // attributes on the entire declaration chain.
    while (true) {
      // Use the "lexical" parent. Eg., if the attribute is on a class, suppress
      // warnings in inline methods but not in out-of-line methods.
      const Decl *Parent =
          dyn_cast_or_null<Decl>(DeclWithIssue->getLexicalDeclContext());
      if (Parent == nullptr || isa<TranslationUnitDecl>(Parent))
        break;

      DeclWithIssue = Parent;
    }
  }

  // While some warnings are attached to AST nodes (mostly path-sensitive
  // checks), others are simply associated with a plain source location
  // or range.  Figuring out the node based on locations can be tricky,
  // so instead, we traverse the whole body of the declaration and gather
  // information on ALL suppressions.  After that we can simply check if
  // any of those suppressions affect the warning in question.
  //
  // Traversing AST of a function is not a heavy operation, but for
  // large functions with a lot of bugs it can make a dent in performance.
  // In order to avoid this scenario, we cache traversal results.
  auto InsertionResult = CachedSuppressionLocations.insert(
      std::make_pair(DeclWithIssue, CachedRanges{}));
  Ranges &SuppressionRanges = InsertionResult.first->second;
  if (InsertionResult.second) {
    // We haven't checked this declaration for suppressions yet!
    CacheInitializer::initialize(DeclWithIssue, SuppressionRanges);
  }

  SourceRange BugRange = Location.asRange();
  const SourceManager &SM = Location.getManager();

  return llvm::any_of(SuppressionRanges,
                      [BugRange, &SM](SourceRange Suppression) {
                        return fullyContains(Suppression, BugRange, SM);
                      });
}
