//===--- RangeSelector.cpp - RangeSelector implementations ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Transformer/RangeSelector.h"
#include "clang/AST/Expr.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Transformer/SourceCode.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <string>
#include <utility>
#include <vector>

using namespace clang;
using namespace transformer;

using ast_matchers::MatchFinder;
using llvm::Error;
using llvm::StringError;

using MatchResult = MatchFinder::MatchResult;

static Error invalidArgumentError(Twine Message) {
  return llvm::make_error<StringError>(llvm::errc::invalid_argument, Message);
}

static Error typeError(StringRef ID, const ASTNodeKind &Kind) {
  return invalidArgumentError("mismatched type (node id=" + ID +
                              " kind=" + Kind.asStringRef() + ")");
}

static Error typeError(StringRef ID, const ASTNodeKind &Kind,
                       Twine ExpectedType) {
  return invalidArgumentError("mismatched type: expected one of " +
                              ExpectedType + " (node id=" + ID +
                              " kind=" + Kind.asStringRef() + ")");
}

static Error missingPropertyError(StringRef ID, Twine Description,
                                  StringRef Property) {
  return invalidArgumentError(Description + " requires property '" + Property +
                              "' (node id=" + ID + ")");
}

static Expected<DynTypedNode> getNode(const ast_matchers::BoundNodes &Nodes,
                                      StringRef ID) {
  auto &NodesMap = Nodes.getMap();
  auto It = NodesMap.find(ID);
  if (It == NodesMap.end())
    return invalidArgumentError("ID not bound: " + ID);
  return It->second;
}

// FIXME: handling of macros should be configurable.
static SourceLocation findPreviousTokenStart(SourceLocation Start,
                                             const SourceManager &SM,
                                             const LangOptions &LangOpts) {
  if (Start.isInvalid() || Start.isMacroID())
    return SourceLocation();

  SourceLocation BeforeStart = Start.getLocWithOffset(-1);
  if (BeforeStart.isInvalid() || BeforeStart.isMacroID())
    return SourceLocation();

  return Lexer::GetBeginningOfToken(BeforeStart, SM, LangOpts);
}

// Finds the start location of the previous token of kind \p TK.
// FIXME: handling of macros should be configurable.
static SourceLocation findPreviousTokenKind(SourceLocation Start,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts,
                                            tok::TokenKind TK) {
  while (true) {
    SourceLocation L = findPreviousTokenStart(Start, SM, LangOpts);
    if (L.isInvalid() || L.isMacroID())
      return SourceLocation();

    Token T;
    if (Lexer::getRawToken(L, T, SM, LangOpts, /*IgnoreWhiteSpace=*/true))
      return SourceLocation();

    if (T.is(TK))
      return T.getLocation();

    Start = L;
  }
}

RangeSelector transformer::before(RangeSelector Selector) {
  return [Selector](const MatchResult &Result) -> Expected<CharSourceRange> {
    Expected<CharSourceRange> SelectedRange = Selector(Result);
    if (!SelectedRange)
      return SelectedRange.takeError();
    return CharSourceRange::getCharRange(SelectedRange->getBegin());
  };
}

RangeSelector transformer::after(RangeSelector Selector) {
  return [Selector](const MatchResult &Result) -> Expected<CharSourceRange> {
    Expected<CharSourceRange> SelectedRange = Selector(Result);
    if (!SelectedRange)
      return SelectedRange.takeError();
    SourceLocation End = SelectedRange->getEnd();
    if (SelectedRange->isTokenRange()) {
      // We need to find the actual (exclusive) end location from which to
      // create a new source range. However, that's not guaranteed to be valid,
      // even if the token location itself is valid. So, we create a token range
      // consisting only of the last token, then map that range back to the
      // source file. If that succeeds, we have a valid location for the end of
      // the generated range.
      CharSourceRange Range = Lexer::makeFileCharRange(
          CharSourceRange::getTokenRange(SelectedRange->getEnd()),
          *Result.SourceManager, Result.Context->getLangOpts());
      if (Range.isInvalid())
        return invalidArgumentError(
            "after: can't resolve sub-range to valid source range");
      End = Range.getEnd();
    }

    return CharSourceRange::getCharRange(End);
  };
}

RangeSelector transformer::node(std::string ID) {
  return [ID](const MatchResult &Result) -> Expected<CharSourceRange> {
    Expected<DynTypedNode> Node = getNode(Result.Nodes, ID);
    if (!Node)
      return Node.takeError();
    return (Node->get<Decl>() != nullptr ||
            (Node->get<Stmt>() != nullptr && Node->get<Expr>() == nullptr))
               ? tooling::getExtendedRange(*Node, tok::TokenKind::semi,
                                           *Result.Context)
               : CharSourceRange::getTokenRange(Node->getSourceRange());
  };
}

RangeSelector transformer::statement(std::string ID) {
  return [ID](const MatchResult &Result) -> Expected<CharSourceRange> {
    Expected<DynTypedNode> Node = getNode(Result.Nodes, ID);
    if (!Node)
      return Node.takeError();
    return tooling::getExtendedRange(*Node, tok::TokenKind::semi,
                                     *Result.Context);
  };
}

RangeSelector transformer::enclose(RangeSelector Begin, RangeSelector End) {
  return [Begin, End](const MatchResult &Result) -> Expected<CharSourceRange> {
    Expected<CharSourceRange> BeginRange = Begin(Result);
    if (!BeginRange)
      return BeginRange.takeError();
    Expected<CharSourceRange> EndRange = End(Result);
    if (!EndRange)
      return EndRange.takeError();
    SourceLocation B = BeginRange->getBegin();
    SourceLocation E = EndRange->getEnd();
    // Note: we are precluding the possibility of sub-token ranges in the case
    // that EndRange is a token range.
    if (Result.SourceManager->isBeforeInTranslationUnit(E, B)) {
      return invalidArgumentError("Bad range: out of order");
    }
    return CharSourceRange(SourceRange(B, E), EndRange->isTokenRange());
  };
}

RangeSelector transformer::encloseNodes(std::string BeginID,
                                        std::string EndID) {
  return transformer::enclose(node(std::move(BeginID)), node(std::move(EndID)));
}

RangeSelector transformer::member(std::string ID) {
  return [ID](const MatchResult &Result) -> Expected<CharSourceRange> {
    Expected<DynTypedNode> Node = getNode(Result.Nodes, ID);
    if (!Node)
      return Node.takeError();
    if (auto *M = Node->get<clang::MemberExpr>())
      return CharSourceRange::getTokenRange(
          M->getMemberNameInfo().getSourceRange());
    return typeError(ID, Node->getNodeKind(), "MemberExpr");
  };
}

RangeSelector transformer::name(std::string ID) {
  return [ID](const MatchResult &Result) -> Expected<CharSourceRange> {
    Expected<DynTypedNode> N = getNode(Result.Nodes, ID);
    if (!N)
      return N.takeError();
    auto &Node = *N;
    if (const auto *D = Node.get<NamedDecl>()) {
      if (!D->getDeclName().isIdentifier())
        return missingPropertyError(ID, "name", "identifier");
      SourceLocation L = D->getLocation();
      auto R = CharSourceRange::getTokenRange(L, L);
      // Verify that the range covers exactly the name.
      // FIXME: extend this code to support cases like `operator +` or
      // `foo<int>` for which this range will be too short.  Doing so will
      // require subcasing `NamedDecl`, because it doesn't provide virtual
      // access to the \c DeclarationNameInfo.
      if (tooling::getText(R, *Result.Context) != D->getName())
        return CharSourceRange();
      return R;
    }
    if (const auto *E = Node.get<DeclRefExpr>()) {
      if (!E->getNameInfo().getName().isIdentifier())
        return missingPropertyError(ID, "name", "identifier");
      SourceLocation L = E->getLocation();
      return CharSourceRange::getTokenRange(L, L);
    }
    if (const auto *I = Node.get<CXXCtorInitializer>()) {
      if (!I->isMemberInitializer() && I->isWritten())
        return missingPropertyError(ID, "name", "explicit member initializer");
      SourceLocation L = I->getMemberLocation();
      return CharSourceRange::getTokenRange(L, L);
    }
    if (const auto *T = Node.get<TypeLoc>()) {
      TypeLoc Loc = *T;
      auto ET = Loc.getAs<ElaboratedTypeLoc>();
      if (!ET.isNull())
        Loc = ET.getNamedTypeLoc();
      if (auto SpecLoc = Loc.getAs<TemplateSpecializationTypeLoc>();
          !SpecLoc.isNull())
        return CharSourceRange::getTokenRange(SpecLoc.getTemplateNameLoc());
      return CharSourceRange::getTokenRange(Loc.getSourceRange());
    }
    return typeError(ID, Node.getNodeKind(),
                     "DeclRefExpr, NamedDecl, CXXCtorInitializer, TypeLoc");
  };
}

namespace {
// FIXME: make this available in the public API for users to easily create their
// own selectors.

// Creates a selector from a range-selection function \p Func, which selects a
// range that is relative to a bound node id.  \c T is the node type expected by
// \p Func.
template <typename T, CharSourceRange (*Func)(const MatchResult &, const T &)>
class RelativeSelector {
  std::string ID;

public:
  RelativeSelector(std::string ID) : ID(std::move(ID)) {}

  Expected<CharSourceRange> operator()(const MatchResult &Result) {
    Expected<DynTypedNode> N = getNode(Result.Nodes, ID);
    if (!N)
      return N.takeError();
    if (const auto *Arg = N->get<T>())
      return Func(Result, *Arg);
    return typeError(ID, N->getNodeKind());
  }
};
} // namespace

// FIXME: Change the following functions from being in an anonymous namespace
// to static functions, after the minimum Visual C++ has _MSC_VER >= 1915
// (equivalent to Visual Studio 2017 v15.8 or higher). Using the anonymous
// namespace works around a bug in earlier versions.
namespace {
// Returns the range of the statements (all source between the braces).
CharSourceRange getStatementsRange(const MatchResult &,
                                   const CompoundStmt &CS) {
  return CharSourceRange::getCharRange(CS.getLBracLoc().getLocWithOffset(1),
                                       CS.getRBracLoc());
}
} // namespace

RangeSelector transformer::statements(std::string ID) {
  return RelativeSelector<CompoundStmt, getStatementsRange>(std::move(ID));
}

namespace {

SourceLocation getRLoc(const CallExpr &E) { return E.getRParenLoc(); }

SourceLocation getRLoc(const CXXConstructExpr &E) {
  return E.getParenOrBraceRange().getEnd();
}

tok::TokenKind getStartToken(const CallExpr &E) {
  return tok::TokenKind::l_paren;
}

tok::TokenKind getStartToken(const CXXConstructExpr &E) {
  return isa<CXXTemporaryObjectExpr>(E) ? tok::TokenKind::l_paren
                                        : tok::TokenKind::l_brace;
}

template <typename ExprWithArgs>
SourceLocation findArgStartDelimiter(const ExprWithArgs &E, SourceLocation RLoc,
                                     const SourceManager &SM,
                                     const LangOptions &LangOpts) {
  SourceLocation Loc = E.getNumArgs() == 0 ? RLoc : E.getArg(0)->getBeginLoc();
  return findPreviousTokenKind(Loc, SM, LangOpts, getStartToken(E));
}
// Returns the range of the source between the call's or construct expr's
// parentheses/braces.
template <typename ExprWithArgs>
CharSourceRange getArgumentsRange(const MatchResult &Result,
                                  const ExprWithArgs &CE) {
  const SourceLocation RLoc = getRLoc(CE);
  return CharSourceRange::getCharRange(
      findArgStartDelimiter(CE, RLoc, *Result.SourceManager,
                            Result.Context->getLangOpts())
          .getLocWithOffset(1),
      RLoc);
}
} // namespace

RangeSelector transformer::callArgs(std::string ID) {
  return RelativeSelector<CallExpr, getArgumentsRange<CallExpr>>(std::move(ID));
}

RangeSelector transformer::constructExprArgs(std::string ID) {
  return RelativeSelector<CXXConstructExpr,
                          getArgumentsRange<CXXConstructExpr>>(std::move(ID));
}

namespace {
// Returns the range of the elements of the initializer list. Includes all
// source between the braces.
CharSourceRange getElementsRange(const MatchResult &,
                                 const InitListExpr &E) {
  return CharSourceRange::getCharRange(E.getLBraceLoc().getLocWithOffset(1),
                                       E.getRBraceLoc());
}
} // namespace

RangeSelector transformer::initListElements(std::string ID) {
  return RelativeSelector<InitListExpr, getElementsRange>(std::move(ID));
}

namespace {
// Returns the range of the else branch, including the `else` keyword.
CharSourceRange getElseRange(const MatchResult &Result, const IfStmt &S) {
  return tooling::maybeExtendRange(
      CharSourceRange::getTokenRange(S.getElseLoc(), S.getEndLoc()),
      tok::TokenKind::semi, *Result.Context);
}
} // namespace

RangeSelector transformer::elseBranch(std::string ID) {
  return RelativeSelector<IfStmt, getElseRange>(std::move(ID));
}

RangeSelector transformer::expansion(RangeSelector S) {
  return [S](const MatchResult &Result) -> Expected<CharSourceRange> {
    Expected<CharSourceRange> SRange = S(Result);
    if (!SRange)
      return SRange.takeError();
    return Result.SourceManager->getExpansionRange(*SRange);
  };
}
