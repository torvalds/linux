//===--- ASTSelection.cpp - Clang refactoring library ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/ASTSelection.h"
#include "clang/AST/LexicallyOrderedRecursiveASTVisitor.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/SaveAndRestore.h"
#include <optional>

using namespace clang;
using namespace tooling;

namespace {

CharSourceRange getLexicalDeclRange(Decl *D, const SourceManager &SM,
                                    const LangOptions &LangOpts) {
  if (!isa<ObjCImplDecl>(D))
    return CharSourceRange::getTokenRange(D->getSourceRange());
  // Objective-C implementation declarations end at the '@' instead of the 'end'
  // keyword. Use the lexer to find the location right after 'end'.
  SourceRange R = D->getSourceRange();
  SourceLocation LocAfterEnd = Lexer::findLocationAfterToken(
      R.getEnd(), tok::raw_identifier, SM, LangOpts,
      /*SkipTrailingWhitespaceAndNewLine=*/false);
  return LocAfterEnd.isValid()
             ? CharSourceRange::getCharRange(R.getBegin(), LocAfterEnd)
             : CharSourceRange::getTokenRange(R);
}

/// Constructs the tree of selected AST nodes that either contain the location
/// of the cursor or overlap with the selection range.
class ASTSelectionFinder
    : public LexicallyOrderedRecursiveASTVisitor<ASTSelectionFinder> {
public:
  ASTSelectionFinder(SourceRange Selection, FileID TargetFile,
                     const ASTContext &Context)
      : LexicallyOrderedRecursiveASTVisitor(Context.getSourceManager()),
        SelectionBegin(Selection.getBegin()),
        SelectionEnd(Selection.getBegin() == Selection.getEnd()
                         ? SourceLocation()
                         : Selection.getEnd()),
        TargetFile(TargetFile), Context(Context) {
    // The TU decl is the root of the selected node tree.
    SelectionStack.push_back(
        SelectedASTNode(DynTypedNode::create(*Context.getTranslationUnitDecl()),
                        SourceSelectionKind::None));
  }

  std::optional<SelectedASTNode> getSelectedASTNode() {
    assert(SelectionStack.size() == 1 && "stack was not popped");
    SelectedASTNode Result = std::move(SelectionStack.back());
    SelectionStack.pop_back();
    if (Result.Children.empty())
      return std::nullopt;
    return std::move(Result);
  }

  bool TraversePseudoObjectExpr(PseudoObjectExpr *E) {
    // Avoid traversing the semantic expressions. They should be handled by
    // looking through the appropriate opaque expressions in order to build
    // a meaningful selection tree.
    llvm::SaveAndRestore LookThrough(LookThroughOpaqueValueExprs, true);
    return TraverseStmt(E->getSyntacticForm());
  }

  bool TraverseOpaqueValueExpr(OpaqueValueExpr *E) {
    if (!LookThroughOpaqueValueExprs)
      return true;
    llvm::SaveAndRestore LookThrough(LookThroughOpaqueValueExprs, false);
    return TraverseStmt(E->getSourceExpr());
  }

  bool TraverseDecl(Decl *D) {
    if (isa<TranslationUnitDecl>(D))
      return LexicallyOrderedRecursiveASTVisitor::TraverseDecl(D);
    if (D->isImplicit())
      return true;

    // Check if this declaration is written in the file of interest.
    const SourceRange DeclRange = D->getSourceRange();
    const SourceManager &SM = Context.getSourceManager();
    SourceLocation FileLoc;
    if (DeclRange.getBegin().isMacroID() && !DeclRange.getEnd().isMacroID())
      FileLoc = DeclRange.getEnd();
    else
      FileLoc = SM.getSpellingLoc(DeclRange.getBegin());
    if (SM.getFileID(FileLoc) != TargetFile)
      return true;

    SourceSelectionKind SelectionKind =
        selectionKindFor(getLexicalDeclRange(D, SM, Context.getLangOpts()));
    SelectionStack.push_back(
        SelectedASTNode(DynTypedNode::create(*D), SelectionKind));
    LexicallyOrderedRecursiveASTVisitor::TraverseDecl(D);
    popAndAddToSelectionIfSelected(SelectionKind);

    if (DeclRange.getEnd().isValid() &&
        SM.isBeforeInTranslationUnit(SelectionEnd.isValid() ? SelectionEnd
                                                            : SelectionBegin,
                                     DeclRange.getEnd())) {
      // Stop early when we've reached a declaration after the selection.
      return false;
    }
    return true;
  }

  bool TraverseStmt(Stmt *S) {
    if (!S)
      return true;
    if (auto *Opaque = dyn_cast<OpaqueValueExpr>(S))
      return TraverseOpaqueValueExpr(Opaque);
    // Avoid selecting implicit 'this' expressions.
    if (auto *TE = dyn_cast<CXXThisExpr>(S)) {
      if (TE->isImplicit())
        return true;
    }
    // FIXME (Alex Lorenz): Improve handling for macro locations.
    SourceSelectionKind SelectionKind =
        selectionKindFor(CharSourceRange::getTokenRange(S->getSourceRange()));
    SelectionStack.push_back(
        SelectedASTNode(DynTypedNode::create(*S), SelectionKind));
    LexicallyOrderedRecursiveASTVisitor::TraverseStmt(S);
    popAndAddToSelectionIfSelected(SelectionKind);
    return true;
  }

private:
  void popAndAddToSelectionIfSelected(SourceSelectionKind SelectionKind) {
    SelectedASTNode Node = std::move(SelectionStack.back());
    SelectionStack.pop_back();
    if (SelectionKind != SourceSelectionKind::None || !Node.Children.empty())
      SelectionStack.back().Children.push_back(std::move(Node));
  }

  SourceSelectionKind selectionKindFor(CharSourceRange Range) {
    SourceLocation End = Range.getEnd();
    const SourceManager &SM = Context.getSourceManager();
    if (Range.isTokenRange())
      End = Lexer::getLocForEndOfToken(End, 0, SM, Context.getLangOpts());
    if (!SourceLocation::isPairOfFileLocations(Range.getBegin(), End))
      return SourceSelectionKind::None;
    if (!SelectionEnd.isValid()) {
      // Do a quick check when the selection is of length 0.
      if (SM.isPointWithin(SelectionBegin, Range.getBegin(), End))
        return SourceSelectionKind::ContainsSelection;
      return SourceSelectionKind::None;
    }
    bool HasStart = SM.isPointWithin(SelectionBegin, Range.getBegin(), End);
    bool HasEnd = SM.isPointWithin(SelectionEnd, Range.getBegin(), End);
    if (HasStart && HasEnd)
      return SourceSelectionKind::ContainsSelection;
    if (SM.isPointWithin(Range.getBegin(), SelectionBegin, SelectionEnd) &&
        SM.isPointWithin(End, SelectionBegin, SelectionEnd))
      return SourceSelectionKind::InsideSelection;
    // Ensure there's at least some overlap with the 'start'/'end' selection
    // types.
    if (HasStart && SelectionBegin != End)
      return SourceSelectionKind::ContainsSelectionStart;
    if (HasEnd && SelectionEnd != Range.getBegin())
      return SourceSelectionKind::ContainsSelectionEnd;

    return SourceSelectionKind::None;
  }

  const SourceLocation SelectionBegin, SelectionEnd;
  FileID TargetFile;
  const ASTContext &Context;
  std::vector<SelectedASTNode> SelectionStack;
  /// Controls whether we can traverse through the OpaqueValueExpr. This is
  /// typically enabled during the traversal of syntactic form for
  /// PseudoObjectExprs.
  bool LookThroughOpaqueValueExprs = false;
};

} // end anonymous namespace

std::optional<SelectedASTNode>
clang::tooling::findSelectedASTNodes(const ASTContext &Context,
                                     SourceRange SelectionRange) {
  assert(SelectionRange.isValid() &&
         SourceLocation::isPairOfFileLocations(SelectionRange.getBegin(),
                                               SelectionRange.getEnd()) &&
         "Expected a file range");
  FileID TargetFile =
      Context.getSourceManager().getFileID(SelectionRange.getBegin());
  assert(Context.getSourceManager().getFileID(SelectionRange.getEnd()) ==
             TargetFile &&
         "selection range must span one file");

  ASTSelectionFinder Visitor(SelectionRange, TargetFile, Context);
  Visitor.TraverseDecl(Context.getTranslationUnitDecl());
  return Visitor.getSelectedASTNode();
}

static const char *selectionKindToString(SourceSelectionKind Kind) {
  switch (Kind) {
  case SourceSelectionKind::None:
    return "none";
  case SourceSelectionKind::ContainsSelection:
    return "contains-selection";
  case SourceSelectionKind::ContainsSelectionStart:
    return "contains-selection-start";
  case SourceSelectionKind::ContainsSelectionEnd:
    return "contains-selection-end";
  case SourceSelectionKind::InsideSelection:
    return "inside";
  }
  llvm_unreachable("invalid selection kind");
}

static void dump(const SelectedASTNode &Node, llvm::raw_ostream &OS,
                 unsigned Indent = 0) {
  OS.indent(Indent * 2);
  if (const Decl *D = Node.Node.get<Decl>()) {
    OS << D->getDeclKindName() << "Decl";
    if (const auto *ND = dyn_cast<NamedDecl>(D))
      OS << " \"" << ND->getDeclName() << '"';
  } else if (const Stmt *S = Node.Node.get<Stmt>()) {
    OS << S->getStmtClassName();
  }
  OS << ' ' << selectionKindToString(Node.SelectionKind) << "\n";
  for (const auto &Child : Node.Children)
    dump(Child, OS, Indent + 1);
}

void SelectedASTNode::dump(llvm::raw_ostream &OS) const { ::dump(*this, OS); }

/// Returns true if the given node has any direct children with the following
/// selection kind.
///
/// Note: The direct children also include children of direct children with the
/// "None" selection kind.
static bool hasAnyDirectChildrenWithKind(const SelectedASTNode &Node,
                                         SourceSelectionKind Kind) {
  assert(Kind != SourceSelectionKind::None && "invalid predicate!");
  for (const auto &Child : Node.Children) {
    if (Child.SelectionKind == Kind)
      return true;
    if (Child.SelectionKind == SourceSelectionKind::None)
      return hasAnyDirectChildrenWithKind(Child, Kind);
  }
  return false;
}

namespace {
struct SelectedNodeWithParents {
  SelectedASTNode::ReferenceType Node;
  llvm::SmallVector<SelectedASTNode::ReferenceType, 8> Parents;

  /// Canonicalizes the given selection by selecting different related AST nodes
  /// when it makes sense to do so.
  void canonicalize();
};

enum SelectionCanonicalizationAction { KeepSelection, SelectParent };

/// Returns the canonicalization action which should be applied to the
/// selected statement.
SelectionCanonicalizationAction
getSelectionCanonizalizationAction(const Stmt *S, const Stmt *Parent) {
  // Select the parent expression when:
  // - The string literal in ObjC string literal is selected, e.g.:
  //     @"test"   becomes   @"test"
  //      ~~~~~~             ~~~~~~~
  if (isa<StringLiteral>(S) && isa<ObjCStringLiteral>(Parent))
    return SelectParent;
  // The entire call should be selected when just the member expression
  // that refers to the method or the decl ref that refers to the function
  // is selected.
  //    f.call(args)  becomes  f.call(args)
  //      ~~~~                 ~~~~~~~~~~~~
  //    func(args)  becomes  func(args)
  //    ~~~~                 ~~~~~~~~~~
  else if (const auto *CE = dyn_cast<CallExpr>(Parent)) {
    if ((isa<MemberExpr>(S) || isa<DeclRefExpr>(S)) &&
        CE->getCallee()->IgnoreImpCasts() == S)
      return SelectParent;
  }
  // FIXME: Syntactic form -> Entire pseudo-object expr.
  return KeepSelection;
}

} // end anonymous namespace

void SelectedNodeWithParents::canonicalize() {
  const Stmt *S = Node.get().Node.get<Stmt>();
  assert(S && "non statement selection!");
  const Stmt *Parent = Parents[Parents.size() - 1].get().Node.get<Stmt>();
  if (!Parent)
    return;

  // Look through the implicit casts in the parents.
  unsigned ParentIndex = 1;
  for (; (ParentIndex + 1) <= Parents.size() && isa<ImplicitCastExpr>(Parent);
       ++ParentIndex) {
    const Stmt *NewParent =
        Parents[Parents.size() - ParentIndex - 1].get().Node.get<Stmt>();
    if (!NewParent)
      break;
    Parent = NewParent;
  }

  switch (getSelectionCanonizalizationAction(S, Parent)) {
  case SelectParent:
    Node = Parents[Parents.size() - ParentIndex];
    for (; ParentIndex != 0; --ParentIndex)
      Parents.pop_back();
    break;
  case KeepSelection:
    break;
  }
}

/// Finds the set of bottom-most selected AST nodes that are in the selection
/// tree with the specified selection kind.
///
/// For example, given the following selection tree:
///
/// FunctionDecl "f" contains-selection
///   CompoundStmt contains-selection [#1]
///     CallExpr inside
///     ImplicitCastExpr inside
///       DeclRefExpr inside
///     IntegerLiteral inside
///     IntegerLiteral inside
/// FunctionDecl "f2" contains-selection
///   CompoundStmt contains-selection [#2]
///     CallExpr inside
///     ImplicitCastExpr inside
///       DeclRefExpr inside
///     IntegerLiteral inside
///     IntegerLiteral inside
///
/// This function will find references to nodes #1 and #2 when searching for the
/// \c ContainsSelection kind.
static void findDeepestWithKind(
    const SelectedASTNode &ASTSelection,
    llvm::SmallVectorImpl<SelectedNodeWithParents> &MatchingNodes,
    SourceSelectionKind Kind,
    llvm::SmallVectorImpl<SelectedASTNode::ReferenceType> &ParentStack) {
  if (ASTSelection.Node.get<DeclStmt>()) {
    // Select the entire decl stmt when any of its child declarations is the
    // bottom-most.
    for (const auto &Child : ASTSelection.Children) {
      if (!hasAnyDirectChildrenWithKind(Child, Kind)) {
        MatchingNodes.push_back(SelectedNodeWithParents{
            std::cref(ASTSelection), {ParentStack.begin(), ParentStack.end()}});
        return;
      }
    }
  } else {
    if (!hasAnyDirectChildrenWithKind(ASTSelection, Kind)) {
      // This node is the bottom-most.
      MatchingNodes.push_back(SelectedNodeWithParents{
          std::cref(ASTSelection), {ParentStack.begin(), ParentStack.end()}});
      return;
    }
  }
  // Search in the children.
  ParentStack.push_back(std::cref(ASTSelection));
  for (const auto &Child : ASTSelection.Children)
    findDeepestWithKind(Child, MatchingNodes, Kind, ParentStack);
  ParentStack.pop_back();
}

static void findDeepestWithKind(
    const SelectedASTNode &ASTSelection,
    llvm::SmallVectorImpl<SelectedNodeWithParents> &MatchingNodes,
    SourceSelectionKind Kind) {
  llvm::SmallVector<SelectedASTNode::ReferenceType, 16> ParentStack;
  findDeepestWithKind(ASTSelection, MatchingNodes, Kind, ParentStack);
}

std::optional<CodeRangeASTSelection>
CodeRangeASTSelection::create(SourceRange SelectionRange,
                              const SelectedASTNode &ASTSelection) {
  // Code range is selected when the selection range is not empty.
  if (SelectionRange.getBegin() == SelectionRange.getEnd())
    return std::nullopt;
  llvm::SmallVector<SelectedNodeWithParents, 4> ContainSelection;
  findDeepestWithKind(ASTSelection, ContainSelection,
                      SourceSelectionKind::ContainsSelection);
  // We are looking for a selection in one body of code, so let's focus on
  // one matching result.
  if (ContainSelection.size() != 1)
    return std::nullopt;
  SelectedNodeWithParents &Selected = ContainSelection[0];
  if (!Selected.Node.get().Node.get<Stmt>())
    return std::nullopt;
  const Stmt *CodeRangeStmt = Selected.Node.get().Node.get<Stmt>();
  if (!isa<CompoundStmt>(CodeRangeStmt)) {
    Selected.canonicalize();
    return CodeRangeASTSelection(Selected.Node, Selected.Parents,
                                 /*AreChildrenSelected=*/false);
  }
  // FIXME (Alex L): First selected SwitchCase means that first case statement.
  // is selected actually
  // (See https://github.com/apple/swift-clang & CompoundStmtRange).

  // FIXME (Alex L): Tweak selection rules for compound statements, see:
  // https://github.com/apple/swift-clang/blob/swift-4.1-branch/lib/Tooling/
  // Refactor/ASTSlice.cpp#L513
  // The user selected multiple statements in a compound statement.
  Selected.Parents.push_back(Selected.Node);
  return CodeRangeASTSelection(Selected.Node, Selected.Parents,
                               /*AreChildrenSelected=*/true);
}

static bool isFunctionLikeDeclaration(const Decl *D) {
  // FIXME (Alex L): Test for BlockDecl.
  return isa<FunctionDecl>(D) || isa<ObjCMethodDecl>(D);
}

bool CodeRangeASTSelection::isInFunctionLikeBodyOfCode() const {
  bool IsPrevCompound = false;
  // Scan through the parents (bottom-to-top) and check if the selection is
  // contained in a compound statement that's a body of a function/method
  // declaration.
  for (const auto &Parent : llvm::reverse(Parents)) {
    const DynTypedNode &Node = Parent.get().Node;
    if (const auto *D = Node.get<Decl>()) {
      if (isFunctionLikeDeclaration(D))
        return IsPrevCompound;
      // Stop the search at any type declaration to avoid returning true for
      // expressions in type declarations in functions, like:
      // function foo() { struct X {
      //   int m = /*selection:*/ 1 + 2 /*selection end*/; }; };
      if (isa<TypeDecl>(D))
        return false;
    }
    IsPrevCompound = Node.get<CompoundStmt>() != nullptr;
  }
  return false;
}

const Decl *CodeRangeASTSelection::getFunctionLikeNearestParent() const {
  for (const auto &Parent : llvm::reverse(Parents)) {
    const DynTypedNode &Node = Parent.get().Node;
    if (const auto *D = Node.get<Decl>()) {
      if (isFunctionLikeDeclaration(D))
        return D;
    }
  }
  return nullptr;
}
