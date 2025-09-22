//===--- USRFinder.cpp - Clang refactoring library ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file Implements a recursive AST visitor that finds the USR of a symbol at a
/// point.
///
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/Rename/USRFinder.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Refactoring/RecursiveSymbolVisitor.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;

namespace clang {
namespace tooling {

namespace {

/// Recursively visits each AST node to find the symbol underneath the cursor.
class NamedDeclOccurrenceFindingVisitor
    : public RecursiveSymbolVisitor<NamedDeclOccurrenceFindingVisitor> {
public:
  // Finds the NamedDecl at a point in the source.
  // \param Point the location in the source to search for the NamedDecl.
  explicit NamedDeclOccurrenceFindingVisitor(const SourceLocation Point,
                                             const ASTContext &Context)
      : RecursiveSymbolVisitor(Context.getSourceManager(),
                               Context.getLangOpts()),
        Point(Point), Context(Context) {}

  bool visitSymbolOccurrence(const NamedDecl *ND,
                             ArrayRef<SourceRange> NameRanges) {
    if (!ND)
      return true;
    for (const auto &Range : NameRanges) {
      SourceLocation Start = Range.getBegin();
      SourceLocation End = Range.getEnd();
      if (!Start.isValid() || !Start.isFileID() || !End.isValid() ||
          !End.isFileID() || !isPointWithin(Start, End))
        return true;
    }
    Result = ND;
    return false;
  }

  const NamedDecl *getNamedDecl() const { return Result; }

private:
  // Determines if the Point is within Start and End.
  bool isPointWithin(const SourceLocation Start, const SourceLocation End) {
    // FIXME: Add tests for Point == End.
    return Point == Start || Point == End ||
           (Context.getSourceManager().isBeforeInTranslationUnit(Start,
                                                                 Point) &&
            Context.getSourceManager().isBeforeInTranslationUnit(Point, End));
  }

  const NamedDecl *Result = nullptr;
  const SourceLocation Point; // The location to find the NamedDecl.
  const ASTContext &Context;
};

} // end anonymous namespace

const NamedDecl *getNamedDeclAt(const ASTContext &Context,
                                const SourceLocation Point) {
  const SourceManager &SM = Context.getSourceManager();
  NamedDeclOccurrenceFindingVisitor Visitor(Point, Context);

  // Try to be clever about pruning down the number of top-level declarations we
  // see. If both start and end is either before or after the point we're
  // looking for the point cannot be inside of this decl. Don't even look at it.
  for (auto *CurrDecl : Context.getTranslationUnitDecl()->decls()) {
    SourceLocation StartLoc = CurrDecl->getBeginLoc();
    SourceLocation EndLoc = CurrDecl->getEndLoc();
    if (StartLoc.isValid() && EndLoc.isValid() &&
        SM.isBeforeInTranslationUnit(StartLoc, Point) !=
            SM.isBeforeInTranslationUnit(EndLoc, Point))
      Visitor.TraverseDecl(CurrDecl);
  }

  return Visitor.getNamedDecl();
}

namespace {

/// Recursively visits each NamedDecl node to find the declaration with a
/// specific name.
class NamedDeclFindingVisitor
    : public RecursiveASTVisitor<NamedDeclFindingVisitor> {
public:
  explicit NamedDeclFindingVisitor(StringRef Name) : Name(Name) {}

  // We don't have to traverse the uses to find some declaration with a
  // specific name, so just visit the named declarations.
  bool VisitNamedDecl(const NamedDecl *ND) {
    if (!ND)
      return true;
    // Fully qualified name is used to find the declaration.
    if (Name != ND->getQualifiedNameAsString() &&
        Name != "::" + ND->getQualifiedNameAsString())
      return true;
    Result = ND;
    return false;
  }

  const NamedDecl *getNamedDecl() const { return Result; }

private:
  const NamedDecl *Result = nullptr;
  StringRef Name;
};

} // end anonymous namespace

const NamedDecl *getNamedDeclFor(const ASTContext &Context,
                                 const std::string &Name) {
  NamedDeclFindingVisitor Visitor(Name);
  Visitor.TraverseDecl(Context.getTranslationUnitDecl());
  return Visitor.getNamedDecl();
}

std::string getUSRForDecl(const Decl *Decl) {
  llvm::SmallString<128> Buff;

  // FIXME: Add test for the nullptr case.
  if (Decl == nullptr || index::generateUSRForDecl(Decl, Buff))
    return "";

  return std::string(Buff);
}

} // end namespace tooling
} // end namespace clang
