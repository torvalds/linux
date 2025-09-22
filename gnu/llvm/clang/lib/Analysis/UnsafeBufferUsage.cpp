//===- UnsafeBufferUsage.cpp - Replace pointers with modern C++ -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/UnsafeBufferUsage.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <memory>
#include <optional>
#include <queue>
#include <sstream>

using namespace llvm;
using namespace clang;
using namespace ast_matchers;

#ifndef NDEBUG
namespace {
class StmtDebugPrinter
    : public ConstStmtVisitor<StmtDebugPrinter, std::string> {
public:
  std::string VisitStmt(const Stmt *S) { return S->getStmtClassName(); }

  std::string VisitBinaryOperator(const BinaryOperator *BO) {
    return "BinaryOperator(" + BO->getOpcodeStr().str() + ")";
  }

  std::string VisitUnaryOperator(const UnaryOperator *UO) {
    return "UnaryOperator(" + UO->getOpcodeStr(UO->getOpcode()).str() + ")";
  }

  std::string VisitImplicitCastExpr(const ImplicitCastExpr *ICE) {
    return "ImplicitCastExpr(" + std::string(ICE->getCastKindName()) + ")";
  }
};

// Returns a string of ancestor `Stmt`s of the given `DRE` in such a form:
// "DRE ==> parent-of-DRE ==> grandparent-of-DRE ==> ...".
static std::string getDREAncestorString(const DeclRefExpr *DRE,
                                        ASTContext &Ctx) {
  std::stringstream SS;
  const Stmt *St = DRE;
  StmtDebugPrinter StmtPriner;

  do {
    SS << StmtPriner.Visit(St);

    DynTypedNodeList StParents = Ctx.getParents(*St);

    if (StParents.size() > 1)
      return "unavailable due to multiple parents";
    if (StParents.size() == 0)
      break;
    St = StParents.begin()->get<Stmt>();
    if (St)
      SS << " ==> ";
  } while (St);
  return SS.str();
}
} // namespace
#endif /* NDEBUG */

namespace clang::ast_matchers {
// A `RecursiveASTVisitor` that traverses all descendants of a given node "n"
// except for those belonging to a different callable of "n".
class MatchDescendantVisitor
    : public RecursiveASTVisitor<MatchDescendantVisitor> {
public:
  typedef RecursiveASTVisitor<MatchDescendantVisitor> VisitorBase;

  // Creates an AST visitor that matches `Matcher` on all
  // descendants of a given node "n" except for the ones
  // belonging to a different callable of "n".
  MatchDescendantVisitor(const internal::DynTypedMatcher *Matcher,
                         internal::ASTMatchFinder *Finder,
                         internal::BoundNodesTreeBuilder *Builder,
                         internal::ASTMatchFinder::BindKind Bind,
                         const bool ignoreUnevaluatedContext)
      : Matcher(Matcher), Finder(Finder), Builder(Builder), Bind(Bind),
        Matches(false), ignoreUnevaluatedContext(ignoreUnevaluatedContext) {}

  // Returns true if a match is found in a subtree of `DynNode`, which belongs
  // to the same callable of `DynNode`.
  bool findMatch(const DynTypedNode &DynNode) {
    Matches = false;
    if (const Stmt *StmtNode = DynNode.get<Stmt>()) {
      TraverseStmt(const_cast<Stmt *>(StmtNode));
      *Builder = ResultBindings;
      return Matches;
    }
    return false;
  }

  // The following are overriding methods from the base visitor class.
  // They are public only to allow CRTP to work. They are *not *part
  // of the public API of this class.

  // For the matchers so far used in safe buffers, we only need to match
  // `Stmt`s.  To override more as needed.

  bool TraverseDecl(Decl *Node) {
    if (!Node)
      return true;
    if (!match(*Node))
      return false;
    // To skip callables:
    if (isa<FunctionDecl, BlockDecl, ObjCMethodDecl>(Node))
      return true;
    // Traverse descendants
    return VisitorBase::TraverseDecl(Node);
  }

  bool TraverseGenericSelectionExpr(GenericSelectionExpr *Node) {
    // These are unevaluated, except the result expression.
    if (ignoreUnevaluatedContext)
      return TraverseStmt(Node->getResultExpr());
    return VisitorBase::TraverseGenericSelectionExpr(Node);
  }

  bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *Node) {
    // Unevaluated context.
    if (ignoreUnevaluatedContext)
      return true;
    return VisitorBase::TraverseUnaryExprOrTypeTraitExpr(Node);
  }

  bool TraverseTypeOfExprTypeLoc(TypeOfExprTypeLoc Node) {
    // Unevaluated context.
    if (ignoreUnevaluatedContext)
      return true;
    return VisitorBase::TraverseTypeOfExprTypeLoc(Node);
  }

  bool TraverseDecltypeTypeLoc(DecltypeTypeLoc Node) {
    // Unevaluated context.
    if (ignoreUnevaluatedContext)
      return true;
    return VisitorBase::TraverseDecltypeTypeLoc(Node);
  }

  bool TraverseCXXNoexceptExpr(CXXNoexceptExpr *Node) {
    // Unevaluated context.
    if (ignoreUnevaluatedContext)
      return true;
    return VisitorBase::TraverseCXXNoexceptExpr(Node);
  }

  bool TraverseCXXTypeidExpr(CXXTypeidExpr *Node) {
    // Unevaluated context.
    if (ignoreUnevaluatedContext)
      return true;
    return VisitorBase::TraverseCXXTypeidExpr(Node);
  }

  bool TraverseStmt(Stmt *Node, DataRecursionQueue *Queue = nullptr) {
    if (!Node)
      return true;
    if (!match(*Node))
      return false;
    return VisitorBase::TraverseStmt(Node);
  }

  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const {
    // TODO: let's ignore implicit code for now
    return false;
  }

private:
  // Sets 'Matched' to true if 'Matcher' matches 'Node'
  //
  // Returns 'true' if traversal should continue after this function
  // returns, i.e. if no match is found or 'Bind' is 'BK_All'.
  template <typename T> bool match(const T &Node) {
    internal::BoundNodesTreeBuilder RecursiveBuilder(*Builder);

    if (Matcher->matches(DynTypedNode::create(Node), Finder,
                         &RecursiveBuilder)) {
      ResultBindings.addMatch(RecursiveBuilder);
      Matches = true;
      if (Bind != internal::ASTMatchFinder::BK_All)
        return false; // Abort as soon as a match is found.
    }
    return true;
  }

  const internal::DynTypedMatcher *const Matcher;
  internal::ASTMatchFinder *const Finder;
  internal::BoundNodesTreeBuilder *const Builder;
  internal::BoundNodesTreeBuilder ResultBindings;
  const internal::ASTMatchFinder::BindKind Bind;
  bool Matches;
  bool ignoreUnevaluatedContext;
};

// Because we're dealing with raw pointers, let's define what we mean by that.
static auto hasPointerType() {
  return hasType(hasCanonicalType(pointerType()));
}

static auto hasArrayType() { return hasType(hasCanonicalType(arrayType())); }

AST_MATCHER_P(Stmt, forEachDescendantEvaluatedStmt, internal::Matcher<Stmt>,
              innerMatcher) {
  const DynTypedMatcher &DTM = static_cast<DynTypedMatcher>(innerMatcher);

  MatchDescendantVisitor Visitor(&DTM, Finder, Builder, ASTMatchFinder::BK_All,
                                 true);
  return Visitor.findMatch(DynTypedNode::create(Node));
}

AST_MATCHER_P(Stmt, forEachDescendantStmt, internal::Matcher<Stmt>,
              innerMatcher) {
  const DynTypedMatcher &DTM = static_cast<DynTypedMatcher>(innerMatcher);

  MatchDescendantVisitor Visitor(&DTM, Finder, Builder, ASTMatchFinder::BK_All,
                                 false);
  return Visitor.findMatch(DynTypedNode::create(Node));
}

// Matches a `Stmt` node iff the node is in a safe-buffer opt-out region
AST_MATCHER_P(Stmt, notInSafeBufferOptOut, const UnsafeBufferUsageHandler *,
              Handler) {
  return !Handler->isSafeBufferOptOut(Node.getBeginLoc());
}

AST_MATCHER_P(Stmt, ignoreUnsafeBufferInContainer,
              const UnsafeBufferUsageHandler *, Handler) {
  return Handler->ignoreUnsafeBufferInContainer(Node.getBeginLoc());
}

AST_MATCHER_P(CastExpr, castSubExpr, internal::Matcher<Expr>, innerMatcher) {
  return innerMatcher.matches(*Node.getSubExpr(), Finder, Builder);
}

// Matches a `UnaryOperator` whose operator is pre-increment:
AST_MATCHER(UnaryOperator, isPreInc) {
  return Node.getOpcode() == UnaryOperator::Opcode::UO_PreInc;
}

// Returns a matcher that matches any expression 'e' such that `innerMatcher`
// matches 'e' and 'e' is in an Unspecified Lvalue Context.
static auto isInUnspecifiedLvalueContext(internal::Matcher<Expr> innerMatcher) {
  // clang-format off
  return
    expr(anyOf(
      implicitCastExpr(
        hasCastKind(CastKind::CK_LValueToRValue),
        castSubExpr(innerMatcher)),
      binaryOperator(
        hasAnyOperatorName("="),
        hasLHS(innerMatcher)
      )
    ));
  // clang-format on
}

// Returns a matcher that matches any expression `e` such that `InnerMatcher`
// matches `e` and `e` is in an Unspecified Pointer Context (UPC).
static internal::Matcher<Stmt>
isInUnspecifiedPointerContext(internal::Matcher<Stmt> InnerMatcher) {
  // A UPC can be
  // 1. an argument of a function call (except the callee has [[unsafe_...]]
  //    attribute), or
  // 2. the operand of a pointer-to-(integer or bool) cast operation; or
  // 3. the operand of a comparator operation; or
  // 4. the operand of a pointer subtraction operation
  //    (i.e., computing the distance between two pointers); or ...

  // clang-format off
  auto CallArgMatcher = callExpr(
    forEachArgumentWithParamType(
      InnerMatcher,
      isAnyPointer() /* array also decays to pointer type*/),
    unless(callee(
      functionDecl(hasAttr(attr::UnsafeBufferUsage)))));

  auto CastOperandMatcher =
      castExpr(anyOf(hasCastKind(CastKind::CK_PointerToIntegral),
		     hasCastKind(CastKind::CK_PointerToBoolean)),
	       castSubExpr(allOf(hasPointerType(), InnerMatcher)));

  auto CompOperandMatcher =
      binaryOperator(hasAnyOperatorName("!=", "==", "<", "<=", ">", ">="),
                     eachOf(hasLHS(allOf(hasPointerType(), InnerMatcher)),
                            hasRHS(allOf(hasPointerType(), InnerMatcher))));

  // A matcher that matches pointer subtractions:
  auto PtrSubtractionMatcher =
      binaryOperator(hasOperatorName("-"),
		     // Note that here we need both LHS and RHS to be
		     // pointer. Then the inner matcher can match any of
		     // them:
		     allOf(hasLHS(hasPointerType()),
			   hasRHS(hasPointerType())),
		     eachOf(hasLHS(InnerMatcher),
			    hasRHS(InnerMatcher)));
  // clang-format on

  return stmt(anyOf(CallArgMatcher, CastOperandMatcher, CompOperandMatcher,
                    PtrSubtractionMatcher));
  // FIXME: any more cases? (UPC excludes the RHS of an assignment.  For now we
  // don't have to check that.)
}

// Returns a matcher that matches any expression 'e' such that `innerMatcher`
// matches 'e' and 'e' is in an unspecified untyped context (i.e the expression
// 'e' isn't evaluated to an RValue). For example, consider the following code:
//    int *p = new int[4];
//    int *q = new int[4];
//    if ((p = q)) {}
//    p = q;
// The expression `p = q` in the conditional of the `if` statement
// `if ((p = q))` is evaluated as an RValue, whereas the expression `p = q;`
// in the assignment statement is in an untyped context.
static internal::Matcher<Stmt>
isInUnspecifiedUntypedContext(internal::Matcher<Stmt> InnerMatcher) {
  // An unspecified context can be
  // 1. A compound statement,
  // 2. The body of an if statement
  // 3. Body of a loop
  auto CompStmt = compoundStmt(forEach(InnerMatcher));
  auto IfStmtThen = ifStmt(hasThen(InnerMatcher));
  auto IfStmtElse = ifStmt(hasElse(InnerMatcher));
  // FIXME: Handle loop bodies.
  return stmt(anyOf(CompStmt, IfStmtThen, IfStmtElse));
}

// Given a two-param std::span construct call, matches iff the call has the
// following forms:
//   1. `std::span<T>{new T[n], n}`, where `n` is a literal or a DRE
//   2. `std::span<T>{new T, 1}`
//   3. `std::span<T>{&var, 1}`
//   4. `std::span<T>{a, n}`, where `a` is of an array-of-T with constant size
//   `n`
//   5. `std::span<T>{any, 0}`
AST_MATCHER(CXXConstructExpr, isSafeSpanTwoParamConstruct) {
  assert(Node.getNumArgs() == 2 &&
         "expecting a two-parameter std::span constructor");
  const Expr *Arg0 = Node.getArg(0)->IgnoreImplicit();
  const Expr *Arg1 = Node.getArg(1)->IgnoreImplicit();
  auto HaveEqualConstantValues = [&Finder](const Expr *E0, const Expr *E1) {
    if (auto E0CV = E0->getIntegerConstantExpr(Finder->getASTContext()))
      if (auto E1CV = E1->getIntegerConstantExpr(Finder->getASTContext())) {
        return APSInt::compareValues(*E0CV, *E1CV) == 0;
      }
    return false;
  };
  auto AreSameDRE = [](const Expr *E0, const Expr *E1) {
    if (auto *DRE0 = dyn_cast<DeclRefExpr>(E0))
      if (auto *DRE1 = dyn_cast<DeclRefExpr>(E1)) {
        return DRE0->getDecl() == DRE1->getDecl();
      }
    return false;
  };
  std::optional<APSInt> Arg1CV =
      Arg1->getIntegerConstantExpr(Finder->getASTContext());

  if (Arg1CV && Arg1CV->isZero())
    // Check form 5:
    return true;
  switch (Arg0->IgnoreImplicit()->getStmtClass()) {
  case Stmt::CXXNewExprClass:
    if (auto Size = cast<CXXNewExpr>(Arg0)->getArraySize()) {
      // Check form 1:
      return AreSameDRE((*Size)->IgnoreImplicit(), Arg1) ||
             HaveEqualConstantValues(*Size, Arg1);
    }
    // TODO: what's placeholder type? avoid it for now.
    if (!cast<CXXNewExpr>(Arg0)->hasPlaceholderType()) {
      // Check form 2:
      return Arg1CV && Arg1CV->isOne();
    }
    break;
  case Stmt::UnaryOperatorClass:
    if (cast<UnaryOperator>(Arg0)->getOpcode() ==
        UnaryOperator::Opcode::UO_AddrOf)
      // Check form 3:
      return Arg1CV && Arg1CV->isOne();
    break;
  default:
    break;
  }

  QualType Arg0Ty = Arg0->IgnoreImplicit()->getType();

  if (Arg0Ty->isConstantArrayType()) {
    const APSInt ConstArrSize =
        APSInt(cast<ConstantArrayType>(Arg0Ty)->getSize());

    // Check form 4:
    return Arg1CV && APSInt::compareValues(ConstArrSize, *Arg1CV) == 0;
  }
  return false;
}

AST_MATCHER(ArraySubscriptExpr, isSafeArraySubscript) {
  // FIXME: Proper solution:
  //  - refactor Sema::CheckArrayAccess
  //    - split safe/OOB/unknown decision logic from diagnostics emitting code
  //    -  e. g. "Try harder to find a NamedDecl to point at in the note."
  //    already duplicated
  //  - call both from Sema and from here

  const auto *BaseDRE =
      dyn_cast<DeclRefExpr>(Node.getBase()->IgnoreParenImpCasts());
  if (!BaseDRE)
    return false;
  if (!BaseDRE->getDecl())
    return false;
  const auto *CATy = Finder->getASTContext().getAsConstantArrayType(
      BaseDRE->getDecl()->getType());
  if (!CATy)
    return false;

  if (const auto *IdxLit = dyn_cast<IntegerLiteral>(Node.getIdx())) {
    const APInt ArrIdx = IdxLit->getValue();
    // FIXME: ArrIdx.isNegative() we could immediately emit an error as that's a
    // bug
    if (ArrIdx.isNonNegative() &&
        ArrIdx.getLimitedValue() < CATy->getLimitedSize())
      return true;
  }

  return false;
}

} // namespace clang::ast_matchers

namespace {
// Because the analysis revolves around variables and their types, we'll need to
// track uses of variables (aka DeclRefExprs).
using DeclUseList = SmallVector<const DeclRefExpr *, 1>;

// Convenience typedef.
using FixItList = SmallVector<FixItHint, 4>;
} // namespace

namespace {
/// Gadget is an individual operation in the code that may be of interest to
/// this analysis. Each (non-abstract) subclass corresponds to a specific
/// rigid AST structure that constitutes an operation on a pointer-type object.
/// Discovery of a gadget in the code corresponds to claiming that we understand
/// what this part of code is doing well enough to potentially improve it.
/// Gadgets can be warning (immediately deserving a warning) or fixable (not
/// always deserving a warning per se, but requires our attention to identify
/// it warrants a fixit).
class Gadget {
public:
  enum class Kind {
#define GADGET(x) x,
#include "clang/Analysis/Analyses/UnsafeBufferUsageGadgets.def"
  };

  /// Common type of ASTMatchers used for discovering gadgets.
  /// Useful for implementing the static matcher() methods
  /// that are expected from all non-abstract subclasses.
  using Matcher = decltype(stmt());

  Gadget(Kind K) : K(K) {}

  Kind getKind() const { return K; }

#ifndef NDEBUG
  StringRef getDebugName() const {
    switch (K) {
#define GADGET(x)                                                              \
  case Kind::x:                                                                \
    return #x;
#include "clang/Analysis/Analyses/UnsafeBufferUsageGadgets.def"
    }
    llvm_unreachable("Unhandled Gadget::Kind enum");
  }
#endif

  virtual bool isWarningGadget() const = 0;
  // TODO remove this method from WarningGadget interface. It's only used for
  // debug prints in FixableGadget.
  virtual SourceLocation getSourceLoc() const = 0;

  /// Returns the list of pointer-type variables on which this gadget performs
  /// its operation. Typically, there's only one variable. This isn't a list
  /// of all DeclRefExprs in the gadget's AST!
  virtual DeclUseList getClaimedVarUseSites() const = 0;

  virtual ~Gadget() = default;

private:
  Kind K;
};

/// Warning gadgets correspond to unsafe code patterns that warrants
/// an immediate warning.
class WarningGadget : public Gadget {
public:
  WarningGadget(Kind K) : Gadget(K) {}

  static bool classof(const Gadget *G) { return G->isWarningGadget(); }
  bool isWarningGadget() const final { return true; }

  virtual void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                                     bool IsRelatedToDecl,
                                     ASTContext &Ctx) const = 0;
};

/// Fixable gadgets correspond to code patterns that aren't always unsafe but
/// need to be properly recognized in order to emit fixes. For example, if a raw
/// pointer-type variable is replaced by a safe C++ container, every use of such
/// variable must be carefully considered and possibly updated.
class FixableGadget : public Gadget {
public:
  FixableGadget(Kind K) : Gadget(K) {}

  static bool classof(const Gadget *G) { return !G->isWarningGadget(); }
  bool isWarningGadget() const final { return false; }

  /// Returns a fixit that would fix the current gadget according to
  /// the current strategy. Returns std::nullopt if the fix cannot be produced;
  /// returns an empty list if no fixes are necessary.
  virtual std::optional<FixItList> getFixits(const FixitStrategy &) const {
    return std::nullopt;
  }

  /// Returns a list of two elements where the first element is the LHS of a
  /// pointer assignment statement and the second element is the RHS. This
  /// two-element list represents the fact that the LHS buffer gets its bounds
  /// information from the RHS buffer. This information will be used later to
  /// group all those variables whose types must be modified together to prevent
  /// type mismatches.
  virtual std::optional<std::pair<const VarDecl *, const VarDecl *>>
  getStrategyImplications() const {
    return std::nullopt;
  }
};

static auto toSupportedVariable() { return to(varDecl()); }

using FixableGadgetList = std::vector<std::unique_ptr<FixableGadget>>;
using WarningGadgetList = std::vector<std::unique_ptr<WarningGadget>>;

/// An increment of a pointer-type value is unsafe as it may run the pointer
/// out of bounds.
class IncrementGadget : public WarningGadget {
  static constexpr const char *const OpTag = "op";
  const UnaryOperator *Op;

public:
  IncrementGadget(const MatchFinder::MatchResult &Result)
      : WarningGadget(Kind::Increment),
        Op(Result.Nodes.getNodeAs<UnaryOperator>(OpTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::Increment;
  }

  static Matcher matcher() {
    return stmt(
        unaryOperator(hasOperatorName("++"),
                      hasUnaryOperand(ignoringParenImpCasts(hasPointerType())))
            .bind(OpTag));
  }

  void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                             bool IsRelatedToDecl,
                             ASTContext &Ctx) const override {
    Handler.handleUnsafeOperation(Op, IsRelatedToDecl, Ctx);
  }
  SourceLocation getSourceLoc() const override { return Op->getBeginLoc(); }

  DeclUseList getClaimedVarUseSites() const override {
    SmallVector<const DeclRefExpr *, 2> Uses;
    if (const auto *DRE =
            dyn_cast<DeclRefExpr>(Op->getSubExpr()->IgnoreParenImpCasts())) {
      Uses.push_back(DRE);
    }

    return std::move(Uses);
  }
};

/// A decrement of a pointer-type value is unsafe as it may run the pointer
/// out of bounds.
class DecrementGadget : public WarningGadget {
  static constexpr const char *const OpTag = "op";
  const UnaryOperator *Op;

public:
  DecrementGadget(const MatchFinder::MatchResult &Result)
      : WarningGadget(Kind::Decrement),
        Op(Result.Nodes.getNodeAs<UnaryOperator>(OpTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::Decrement;
  }

  static Matcher matcher() {
    return stmt(
        unaryOperator(hasOperatorName("--"),
                      hasUnaryOperand(ignoringParenImpCasts(hasPointerType())))
            .bind(OpTag));
  }

  void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                             bool IsRelatedToDecl,
                             ASTContext &Ctx) const override {
    Handler.handleUnsafeOperation(Op, IsRelatedToDecl, Ctx);
  }
  SourceLocation getSourceLoc() const override { return Op->getBeginLoc(); }

  DeclUseList getClaimedVarUseSites() const override {
    if (const auto *DRE =
            dyn_cast<DeclRefExpr>(Op->getSubExpr()->IgnoreParenImpCasts())) {
      return {DRE};
    }

    return {};
  }
};

/// Array subscript expressions on raw pointers as if they're arrays. Unsafe as
/// it doesn't have any bounds checks for the array.
class ArraySubscriptGadget : public WarningGadget {
  static constexpr const char *const ArraySubscrTag = "ArraySubscript";
  const ArraySubscriptExpr *ASE;

public:
  ArraySubscriptGadget(const MatchFinder::MatchResult &Result)
      : WarningGadget(Kind::ArraySubscript),
        ASE(Result.Nodes.getNodeAs<ArraySubscriptExpr>(ArraySubscrTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::ArraySubscript;
  }

  static Matcher matcher() {
    // clang-format off
      return stmt(arraySubscriptExpr(
            hasBase(ignoringParenImpCasts(
              anyOf(hasPointerType(), hasArrayType()))),
            unless(anyOf(
              isSafeArraySubscript(),
              hasIndex(
                  anyOf(integerLiteral(equals(0)), arrayInitIndexExpr())
              )
            ))).bind(ArraySubscrTag));
    // clang-format on
  }

  void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                             bool IsRelatedToDecl,
                             ASTContext &Ctx) const override {
    Handler.handleUnsafeOperation(ASE, IsRelatedToDecl, Ctx);
  }
  SourceLocation getSourceLoc() const override { return ASE->getBeginLoc(); }

  DeclUseList getClaimedVarUseSites() const override {
    if (const auto *DRE =
            dyn_cast<DeclRefExpr>(ASE->getBase()->IgnoreParenImpCasts())) {
      return {DRE};
    }

    return {};
  }
};

/// A pointer arithmetic expression of one of the forms:
///  \code
///  ptr + n | n + ptr | ptr - n | ptr += n | ptr -= n
///  \endcode
class PointerArithmeticGadget : public WarningGadget {
  static constexpr const char *const PointerArithmeticTag = "ptrAdd";
  static constexpr const char *const PointerArithmeticPointerTag = "ptrAddPtr";
  const BinaryOperator *PA; // pointer arithmetic expression
  const Expr *Ptr;          // the pointer expression in `PA`

public:
  PointerArithmeticGadget(const MatchFinder::MatchResult &Result)
      : WarningGadget(Kind::PointerArithmetic),
        PA(Result.Nodes.getNodeAs<BinaryOperator>(PointerArithmeticTag)),
        Ptr(Result.Nodes.getNodeAs<Expr>(PointerArithmeticPointerTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::PointerArithmetic;
  }

  static Matcher matcher() {
    auto HasIntegerType = anyOf(hasType(isInteger()), hasType(enumType()));
    auto PtrAtRight =
        allOf(hasOperatorName("+"),
              hasRHS(expr(hasPointerType()).bind(PointerArithmeticPointerTag)),
              hasLHS(HasIntegerType));
    auto PtrAtLeft =
        allOf(anyOf(hasOperatorName("+"), hasOperatorName("-"),
                    hasOperatorName("+="), hasOperatorName("-=")),
              hasLHS(expr(hasPointerType()).bind(PointerArithmeticPointerTag)),
              hasRHS(HasIntegerType));

    return stmt(binaryOperator(anyOf(PtrAtLeft, PtrAtRight))
                    .bind(PointerArithmeticTag));
  }

  void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                             bool IsRelatedToDecl,
                             ASTContext &Ctx) const override {
    Handler.handleUnsafeOperation(PA, IsRelatedToDecl, Ctx);
  }
  SourceLocation getSourceLoc() const override { return PA->getBeginLoc(); }

  DeclUseList getClaimedVarUseSites() const override {
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Ptr->IgnoreParenImpCasts())) {
      return {DRE};
    }

    return {};
  }
  // FIXME: pointer adding zero should be fine
  // FIXME: this gadge will need a fix-it
};

class SpanTwoParamConstructorGadget : public WarningGadget {
  static constexpr const char *const SpanTwoParamConstructorTag =
      "spanTwoParamConstructor";
  const CXXConstructExpr *Ctor; // the span constructor expression

public:
  SpanTwoParamConstructorGadget(const MatchFinder::MatchResult &Result)
      : WarningGadget(Kind::SpanTwoParamConstructor),
        Ctor(Result.Nodes.getNodeAs<CXXConstructExpr>(
            SpanTwoParamConstructorTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::SpanTwoParamConstructor;
  }

  static Matcher matcher() {
    auto HasTwoParamSpanCtorDecl = hasDeclaration(
        cxxConstructorDecl(hasDeclContext(isInStdNamespace()), hasName("span"),
                           parameterCountIs(2)));

    return stmt(cxxConstructExpr(HasTwoParamSpanCtorDecl,
                                 unless(isSafeSpanTwoParamConstruct()))
                    .bind(SpanTwoParamConstructorTag));
  }

  void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                             bool IsRelatedToDecl,
                             ASTContext &Ctx) const override {
    Handler.handleUnsafeOperationInContainer(Ctor, IsRelatedToDecl, Ctx);
  }
  SourceLocation getSourceLoc() const override { return Ctor->getBeginLoc(); }

  DeclUseList getClaimedVarUseSites() const override {
    // If the constructor call is of the form `std::span{var, n}`, `var` is
    // considered an unsafe variable.
    if (auto *DRE = dyn_cast<DeclRefExpr>(Ctor->getArg(0))) {
      if (isa<VarDecl>(DRE->getDecl()))
        return {DRE};
    }
    return {};
  }
};

/// A pointer initialization expression of the form:
///  \code
///  int *p = q;
///  \endcode
class PointerInitGadget : public FixableGadget {
private:
  static constexpr const char *const PointerInitLHSTag = "ptrInitLHS";
  static constexpr const char *const PointerInitRHSTag = "ptrInitRHS";
  const VarDecl *PtrInitLHS;     // the LHS pointer expression in `PI`
  const DeclRefExpr *PtrInitRHS; // the RHS pointer expression in `PI`

public:
  PointerInitGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::PointerInit),
        PtrInitLHS(Result.Nodes.getNodeAs<VarDecl>(PointerInitLHSTag)),
        PtrInitRHS(Result.Nodes.getNodeAs<DeclRefExpr>(PointerInitRHSTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::PointerInit;
  }

  static Matcher matcher() {
    auto PtrInitStmt = declStmt(hasSingleDecl(
        varDecl(hasInitializer(ignoringImpCasts(
                    declRefExpr(hasPointerType(), toSupportedVariable())
                        .bind(PointerInitRHSTag))))
            .bind(PointerInitLHSTag)));

    return stmt(PtrInitStmt);
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &S) const override;
  SourceLocation getSourceLoc() const override {
    return PtrInitRHS->getBeginLoc();
  }

  virtual DeclUseList getClaimedVarUseSites() const override {
    return DeclUseList{PtrInitRHS};
  }

  virtual std::optional<std::pair<const VarDecl *, const VarDecl *>>
  getStrategyImplications() const override {
    return std::make_pair(PtrInitLHS, cast<VarDecl>(PtrInitRHS->getDecl()));
  }
};

/// A pointer assignment expression of the form:
///  \code
///  p = q;
///  \endcode
/// where both `p` and `q` are pointers.
class PtrToPtrAssignmentGadget : public FixableGadget {
private:
  static constexpr const char *const PointerAssignLHSTag = "ptrLHS";
  static constexpr const char *const PointerAssignRHSTag = "ptrRHS";
  const DeclRefExpr *PtrLHS; // the LHS pointer expression in `PA`
  const DeclRefExpr *PtrRHS; // the RHS pointer expression in `PA`

public:
  PtrToPtrAssignmentGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::PtrToPtrAssignment),
        PtrLHS(Result.Nodes.getNodeAs<DeclRefExpr>(PointerAssignLHSTag)),
        PtrRHS(Result.Nodes.getNodeAs<DeclRefExpr>(PointerAssignRHSTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::PtrToPtrAssignment;
  }

  static Matcher matcher() {
    auto PtrAssignExpr = binaryOperator(
        allOf(hasOperatorName("="),
              hasRHS(ignoringParenImpCasts(
                  declRefExpr(hasPointerType(), toSupportedVariable())
                      .bind(PointerAssignRHSTag))),
              hasLHS(declRefExpr(hasPointerType(), toSupportedVariable())
                         .bind(PointerAssignLHSTag))));

    return stmt(isInUnspecifiedUntypedContext(PtrAssignExpr));
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &S) const override;
  SourceLocation getSourceLoc() const override { return PtrLHS->getBeginLoc(); }

  virtual DeclUseList getClaimedVarUseSites() const override {
    return DeclUseList{PtrLHS, PtrRHS};
  }

  virtual std::optional<std::pair<const VarDecl *, const VarDecl *>>
  getStrategyImplications() const override {
    return std::make_pair(cast<VarDecl>(PtrLHS->getDecl()),
                          cast<VarDecl>(PtrRHS->getDecl()));
  }
};

/// An assignment expression of the form:
///  \code
///  ptr = array;
///  \endcode
/// where `p` is a pointer and `array` is a constant size array.
class CArrayToPtrAssignmentGadget : public FixableGadget {
private:
  static constexpr const char *const PointerAssignLHSTag = "ptrLHS";
  static constexpr const char *const PointerAssignRHSTag = "ptrRHS";
  const DeclRefExpr *PtrLHS; // the LHS pointer expression in `PA`
  const DeclRefExpr *PtrRHS; // the RHS pointer expression in `PA`

public:
  CArrayToPtrAssignmentGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::CArrayToPtrAssignment),
        PtrLHS(Result.Nodes.getNodeAs<DeclRefExpr>(PointerAssignLHSTag)),
        PtrRHS(Result.Nodes.getNodeAs<DeclRefExpr>(PointerAssignRHSTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::CArrayToPtrAssignment;
  }

  static Matcher matcher() {
    auto PtrAssignExpr = binaryOperator(
        allOf(hasOperatorName("="),
              hasRHS(ignoringParenImpCasts(
                  declRefExpr(hasType(hasCanonicalType(constantArrayType())),
                              toSupportedVariable())
                      .bind(PointerAssignRHSTag))),
              hasLHS(declRefExpr(hasPointerType(), toSupportedVariable())
                         .bind(PointerAssignLHSTag))));

    return stmt(isInUnspecifiedUntypedContext(PtrAssignExpr));
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &S) const override;
  SourceLocation getSourceLoc() const override { return PtrLHS->getBeginLoc(); }

  virtual DeclUseList getClaimedVarUseSites() const override {
    return DeclUseList{PtrLHS, PtrRHS};
  }

  virtual std::optional<std::pair<const VarDecl *, const VarDecl *>>
  getStrategyImplications() const override {
    return {};
  }
};

/// A call of a function or method that performs unchecked buffer operations
/// over one of its pointer parameters.
class UnsafeBufferUsageAttrGadget : public WarningGadget {
  constexpr static const char *const OpTag = "call_expr";
  const CallExpr *Op;

public:
  UnsafeBufferUsageAttrGadget(const MatchFinder::MatchResult &Result)
      : WarningGadget(Kind::UnsafeBufferUsageAttr),
        Op(Result.Nodes.getNodeAs<CallExpr>(OpTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::UnsafeBufferUsageAttr;
  }

  static Matcher matcher() {
    auto HasUnsafeFnDecl =
        callee(functionDecl(hasAttr(attr::UnsafeBufferUsage)));
    return stmt(callExpr(HasUnsafeFnDecl).bind(OpTag));
  }

  void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                             bool IsRelatedToDecl,
                             ASTContext &Ctx) const override {
    Handler.handleUnsafeOperation(Op, IsRelatedToDecl, Ctx);
  }
  SourceLocation getSourceLoc() const override { return Op->getBeginLoc(); }

  DeclUseList getClaimedVarUseSites() const override { return {}; }
};

/// A call of a constructor that performs unchecked buffer operations
/// over one of its pointer parameters, or constructs a class object that will
/// perform buffer operations that depend on the correctness of the parameters.
class UnsafeBufferUsageCtorAttrGadget : public WarningGadget {
  constexpr static const char *const OpTag = "cxx_construct_expr";
  const CXXConstructExpr *Op;

public:
  UnsafeBufferUsageCtorAttrGadget(const MatchFinder::MatchResult &Result)
      : WarningGadget(Kind::UnsafeBufferUsageCtorAttr),
        Op(Result.Nodes.getNodeAs<CXXConstructExpr>(OpTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::UnsafeBufferUsageCtorAttr;
  }

  static Matcher matcher() {
    auto HasUnsafeCtorDecl =
        hasDeclaration(cxxConstructorDecl(hasAttr(attr::UnsafeBufferUsage)));
    // std::span(ptr, size) ctor is handled by SpanTwoParamConstructorGadget.
    auto HasTwoParamSpanCtorDecl = SpanTwoParamConstructorGadget::matcher();
    return stmt(
        cxxConstructExpr(HasUnsafeCtorDecl, unless(HasTwoParamSpanCtorDecl))
            .bind(OpTag));
  }

  void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                             bool IsRelatedToDecl,
                             ASTContext &Ctx) const override {
    Handler.handleUnsafeOperation(Op, IsRelatedToDecl, Ctx);
  }
  SourceLocation getSourceLoc() const override { return Op->getBeginLoc(); }

  DeclUseList getClaimedVarUseSites() const override { return {}; }
};

// Warning gadget for unsafe invocation of span::data method.
// Triggers when the pointer returned by the invocation is immediately
// cast to a larger type.

class DataInvocationGadget : public WarningGadget {
  constexpr static const char *const OpTag = "data_invocation_expr";
  const ExplicitCastExpr *Op;

public:
  DataInvocationGadget(const MatchFinder::MatchResult &Result)
      : WarningGadget(Kind::DataInvocation),
        Op(Result.Nodes.getNodeAs<ExplicitCastExpr>(OpTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::DataInvocation;
  }

  static Matcher matcher() {
    Matcher callExpr = cxxMemberCallExpr(
        callee(cxxMethodDecl(hasName("data"), ofClass(hasName("std::span")))));
    return stmt(
        explicitCastExpr(anyOf(has(callExpr), has(parenExpr(has(callExpr)))))
            .bind(OpTag));
  }

  void handleUnsafeOperation(UnsafeBufferUsageHandler &Handler,
                             bool IsRelatedToDecl,
                             ASTContext &Ctx) const override {
    Handler.handleUnsafeOperation(Op, IsRelatedToDecl, Ctx);
  }
  SourceLocation getSourceLoc() const override { return Op->getBeginLoc(); }

  DeclUseList getClaimedVarUseSites() const override { return {}; }
};

// Represents expressions of the form `DRE[*]` in the Unspecified Lvalue
// Context (see `isInUnspecifiedLvalueContext`).
// Note here `[]` is the built-in subscript operator.
class ULCArraySubscriptGadget : public FixableGadget {
private:
  static constexpr const char *const ULCArraySubscriptTag =
      "ArraySubscriptUnderULC";
  const ArraySubscriptExpr *Node;

public:
  ULCArraySubscriptGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::ULCArraySubscript),
        Node(Result.Nodes.getNodeAs<ArraySubscriptExpr>(ULCArraySubscriptTag)) {
    assert(Node != nullptr && "Expecting a non-null matching result");
  }

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::ULCArraySubscript;
  }

  static Matcher matcher() {
    auto ArrayOrPtr = anyOf(hasPointerType(), hasArrayType());
    auto BaseIsArrayOrPtrDRE = hasBase(
        ignoringParenImpCasts(declRefExpr(ArrayOrPtr, toSupportedVariable())));
    auto Target =
        arraySubscriptExpr(BaseIsArrayOrPtrDRE).bind(ULCArraySubscriptTag);

    return expr(isInUnspecifiedLvalueContext(Target));
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &S) const override;
  SourceLocation getSourceLoc() const override { return Node->getBeginLoc(); }

  virtual DeclUseList getClaimedVarUseSites() const override {
    if (const auto *DRE =
            dyn_cast<DeclRefExpr>(Node->getBase()->IgnoreImpCasts())) {
      return {DRE};
    }
    return {};
  }
};

// Fixable gadget to handle stand alone pointers of the form `UPC(DRE)` in the
// unspecified pointer context (isInUnspecifiedPointerContext). The gadget emits
// fixit of the form `UPC(DRE.data())`.
class UPCStandalonePointerGadget : public FixableGadget {
private:
  static constexpr const char *const DeclRefExprTag = "StandalonePointer";
  const DeclRefExpr *Node;

public:
  UPCStandalonePointerGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::UPCStandalonePointer),
        Node(Result.Nodes.getNodeAs<DeclRefExpr>(DeclRefExprTag)) {
    assert(Node != nullptr && "Expecting a non-null matching result");
  }

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::UPCStandalonePointer;
  }

  static Matcher matcher() {
    auto ArrayOrPtr = anyOf(hasPointerType(), hasArrayType());
    auto target = expr(ignoringParenImpCasts(
        declRefExpr(allOf(ArrayOrPtr, toSupportedVariable()))
            .bind(DeclRefExprTag)));
    return stmt(isInUnspecifiedPointerContext(target));
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &S) const override;
  SourceLocation getSourceLoc() const override { return Node->getBeginLoc(); }

  virtual DeclUseList getClaimedVarUseSites() const override { return {Node}; }
};

class PointerDereferenceGadget : public FixableGadget {
  static constexpr const char *const BaseDeclRefExprTag = "BaseDRE";
  static constexpr const char *const OperatorTag = "op";

  const DeclRefExpr *BaseDeclRefExpr = nullptr;
  const UnaryOperator *Op = nullptr;

public:
  PointerDereferenceGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::PointerDereference),
        BaseDeclRefExpr(
            Result.Nodes.getNodeAs<DeclRefExpr>(BaseDeclRefExprTag)),
        Op(Result.Nodes.getNodeAs<UnaryOperator>(OperatorTag)) {}

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::PointerDereference;
  }

  static Matcher matcher() {
    auto Target =
        unaryOperator(
            hasOperatorName("*"),
            has(expr(ignoringParenImpCasts(
                declRefExpr(toSupportedVariable()).bind(BaseDeclRefExprTag)))))
            .bind(OperatorTag);

    return expr(isInUnspecifiedLvalueContext(Target));
  }

  DeclUseList getClaimedVarUseSites() const override {
    return {BaseDeclRefExpr};
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &S) const override;
  SourceLocation getSourceLoc() const override { return Op->getBeginLoc(); }
};

// Represents expressions of the form `&DRE[any]` in the Unspecified Pointer
// Context (see `isInUnspecifiedPointerContext`).
// Note here `[]` is the built-in subscript operator.
class UPCAddressofArraySubscriptGadget : public FixableGadget {
private:
  static constexpr const char *const UPCAddressofArraySubscriptTag =
      "AddressofArraySubscriptUnderUPC";
  const UnaryOperator *Node; // the `&DRE[any]` node

public:
  UPCAddressofArraySubscriptGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::ULCArraySubscript),
        Node(Result.Nodes.getNodeAs<UnaryOperator>(
            UPCAddressofArraySubscriptTag)) {
    assert(Node != nullptr && "Expecting a non-null matching result");
  }

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::UPCAddressofArraySubscript;
  }

  static Matcher matcher() {
    return expr(isInUnspecifiedPointerContext(expr(ignoringImpCasts(
        unaryOperator(
            hasOperatorName("&"),
            hasUnaryOperand(arraySubscriptExpr(hasBase(
                ignoringParenImpCasts(declRefExpr(toSupportedVariable()))))))
            .bind(UPCAddressofArraySubscriptTag)))));
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &) const override;
  SourceLocation getSourceLoc() const override { return Node->getBeginLoc(); }

  virtual DeclUseList getClaimedVarUseSites() const override {
    const auto *ArraySubst = cast<ArraySubscriptExpr>(Node->getSubExpr());
    const auto *DRE =
        cast<DeclRefExpr>(ArraySubst->getBase()->IgnoreParenImpCasts());
    return {DRE};
  }
};
} // namespace

namespace {
// An auxiliary tracking facility for the fixit analysis. It helps connect
// declarations to its uses and make sure we've covered all uses with our
// analysis before we try to fix the declaration.
class DeclUseTracker {
  using UseSetTy = SmallSet<const DeclRefExpr *, 16>;
  using DefMapTy = DenseMap<const VarDecl *, const DeclStmt *>;

  // Allocate on the heap for easier move.
  std::unique_ptr<UseSetTy> Uses{std::make_unique<UseSetTy>()};
  DefMapTy Defs{};

public:
  DeclUseTracker() = default;
  DeclUseTracker(const DeclUseTracker &) = delete; // Let's avoid copies.
  DeclUseTracker &operator=(const DeclUseTracker &) = delete;
  DeclUseTracker(DeclUseTracker &&) = default;
  DeclUseTracker &operator=(DeclUseTracker &&) = default;

  // Start tracking a freshly discovered DRE.
  void discoverUse(const DeclRefExpr *DRE) { Uses->insert(DRE); }

  // Stop tracking the DRE as it's been fully figured out.
  void claimUse(const DeclRefExpr *DRE) {
    assert(Uses->count(DRE) &&
           "DRE not found or claimed by multiple matchers!");
    Uses->erase(DRE);
  }

  // A variable is unclaimed if at least one use is unclaimed.
  bool hasUnclaimedUses(const VarDecl *VD) const {
    // FIXME: Can this be less linear? Maybe maintain a map from VDs to DREs?
    return any_of(*Uses, [VD](const DeclRefExpr *DRE) {
      return DRE->getDecl()->getCanonicalDecl() == VD->getCanonicalDecl();
    });
  }

  UseSetTy getUnclaimedUses(const VarDecl *VD) const {
    UseSetTy ReturnSet;
    for (auto use : *Uses) {
      if (use->getDecl()->getCanonicalDecl() == VD->getCanonicalDecl()) {
        ReturnSet.insert(use);
      }
    }
    return ReturnSet;
  }

  void discoverDecl(const DeclStmt *DS) {
    for (const Decl *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        // FIXME: Assertion temporarily disabled due to a bug in
        // ASTMatcher internal behavior in presence of GNU
        // statement-expressions. We need to properly investigate this
        // because it can screw up our algorithm in other ways.
        // assert(Defs.count(VD) == 0 && "Definition already discovered!");
        Defs[VD] = DS;
      }
    }
  }

  const DeclStmt *lookupDecl(const VarDecl *VD) const {
    return Defs.lookup(VD);
  }
};
} // namespace

// Representing a pointer type expression of the form `++Ptr` in an Unspecified
// Pointer Context (UPC):
class UPCPreIncrementGadget : public FixableGadget {
private:
  static constexpr const char *const UPCPreIncrementTag =
      "PointerPreIncrementUnderUPC";
  const UnaryOperator *Node; // the `++Ptr` node

public:
  UPCPreIncrementGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::UPCPreIncrement),
        Node(Result.Nodes.getNodeAs<UnaryOperator>(UPCPreIncrementTag)) {
    assert(Node != nullptr && "Expecting a non-null matching result");
  }

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::UPCPreIncrement;
  }

  static Matcher matcher() {
    // Note here we match `++Ptr` for any expression `Ptr` of pointer type.
    // Although currently we can only provide fix-its when `Ptr` is a DRE, we
    // can have the matcher be general, so long as `getClaimedVarUseSites` does
    // things right.
    return stmt(isInUnspecifiedPointerContext(expr(ignoringImpCasts(
        unaryOperator(isPreInc(),
                      hasUnaryOperand(declRefExpr(toSupportedVariable())))
            .bind(UPCPreIncrementTag)))));
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &S) const override;
  SourceLocation getSourceLoc() const override { return Node->getBeginLoc(); }

  virtual DeclUseList getClaimedVarUseSites() const override {
    return {dyn_cast<DeclRefExpr>(Node->getSubExpr())};
  }
};

// Representing a pointer type expression of the form `Ptr += n` in an
// Unspecified Untyped Context (UUC):
class UUCAddAssignGadget : public FixableGadget {
private:
  static constexpr const char *const UUCAddAssignTag =
      "PointerAddAssignUnderUUC";
  static constexpr const char *const OffsetTag = "Offset";

  const BinaryOperator *Node; // the `Ptr += n` node
  const Expr *Offset = nullptr;

public:
  UUCAddAssignGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::UUCAddAssign),
        Node(Result.Nodes.getNodeAs<BinaryOperator>(UUCAddAssignTag)),
        Offset(Result.Nodes.getNodeAs<Expr>(OffsetTag)) {
    assert(Node != nullptr && "Expecting a non-null matching result");
  }

  static bool classof(const Gadget *G) {
    return G->getKind() == Kind::UUCAddAssign;
  }

  static Matcher matcher() {
    // clang-format off
    return stmt(isInUnspecifiedUntypedContext(expr(ignoringImpCasts(
        binaryOperator(hasOperatorName("+="),
                       hasLHS(
                        declRefExpr(
                          hasPointerType(),
                          toSupportedVariable())),
                       hasRHS(expr().bind(OffsetTag)))
            .bind(UUCAddAssignTag)))));
    // clang-format on
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &S) const override;
  SourceLocation getSourceLoc() const override { return Node->getBeginLoc(); }

  virtual DeclUseList getClaimedVarUseSites() const override {
    return {dyn_cast<DeclRefExpr>(Node->getLHS())};
  }
};

// Representing a fixable expression of the form `*(ptr + 123)` or `*(123 +
// ptr)`:
class DerefSimplePtrArithFixableGadget : public FixableGadget {
  static constexpr const char *const BaseDeclRefExprTag = "BaseDRE";
  static constexpr const char *const DerefOpTag = "DerefOp";
  static constexpr const char *const AddOpTag = "AddOp";
  static constexpr const char *const OffsetTag = "Offset";

  const DeclRefExpr *BaseDeclRefExpr = nullptr;
  const UnaryOperator *DerefOp = nullptr;
  const BinaryOperator *AddOp = nullptr;
  const IntegerLiteral *Offset = nullptr;

public:
  DerefSimplePtrArithFixableGadget(const MatchFinder::MatchResult &Result)
      : FixableGadget(Kind::DerefSimplePtrArithFixable),
        BaseDeclRefExpr(
            Result.Nodes.getNodeAs<DeclRefExpr>(BaseDeclRefExprTag)),
        DerefOp(Result.Nodes.getNodeAs<UnaryOperator>(DerefOpTag)),
        AddOp(Result.Nodes.getNodeAs<BinaryOperator>(AddOpTag)),
        Offset(Result.Nodes.getNodeAs<IntegerLiteral>(OffsetTag)) {}

  static Matcher matcher() {
    // clang-format off
    auto ThePtr = expr(hasPointerType(),
                       ignoringImpCasts(declRefExpr(toSupportedVariable()).
                                        bind(BaseDeclRefExprTag)));
    auto PlusOverPtrAndInteger = expr(anyOf(
          binaryOperator(hasOperatorName("+"), hasLHS(ThePtr),
                         hasRHS(integerLiteral().bind(OffsetTag)))
                         .bind(AddOpTag),
          binaryOperator(hasOperatorName("+"), hasRHS(ThePtr),
                         hasLHS(integerLiteral().bind(OffsetTag)))
                         .bind(AddOpTag)));
    return isInUnspecifiedLvalueContext(unaryOperator(
        hasOperatorName("*"),
        hasUnaryOperand(ignoringParens(PlusOverPtrAndInteger)))
        .bind(DerefOpTag));
    // clang-format on
  }

  virtual std::optional<FixItList>
  getFixits(const FixitStrategy &s) const final;
  SourceLocation getSourceLoc() const override {
    return DerefOp->getBeginLoc();
  }

  virtual DeclUseList getClaimedVarUseSites() const final {
    return {BaseDeclRefExpr};
  }
};

/// Scan the function and return a list of gadgets found with provided kits.
static std::tuple<FixableGadgetList, WarningGadgetList, DeclUseTracker>
findGadgets(const Decl *D, const UnsafeBufferUsageHandler &Handler,
            bool EmitSuggestions) {

  struct GadgetFinderCallback : MatchFinder::MatchCallback {
    FixableGadgetList FixableGadgets;
    WarningGadgetList WarningGadgets;
    DeclUseTracker Tracker;

    void run(const MatchFinder::MatchResult &Result) override {
      // In debug mode, assert that we've found exactly one gadget.
      // This helps us avoid conflicts in .bind() tags.
#if NDEBUG
#define NEXT return
#else
      [[maybe_unused]] int numFound = 0;
#define NEXT ++numFound
#endif

      if (const auto *DRE = Result.Nodes.getNodeAs<DeclRefExpr>("any_dre")) {
        Tracker.discoverUse(DRE);
        NEXT;
      }

      if (const auto *DS = Result.Nodes.getNodeAs<DeclStmt>("any_ds")) {
        Tracker.discoverDecl(DS);
        NEXT;
      }

      // Figure out which matcher we've found, and call the appropriate
      // subclass constructor.
      // FIXME: Can we do this more logarithmically?
#define FIXABLE_GADGET(name)                                                   \
  if (Result.Nodes.getNodeAs<Stmt>(#name)) {                                   \
    FixableGadgets.push_back(std::make_unique<name##Gadget>(Result));          \
    NEXT;                                                                      \
  }
#include "clang/Analysis/Analyses/UnsafeBufferUsageGadgets.def"
#define WARNING_GADGET(name)                                                   \
  if (Result.Nodes.getNodeAs<Stmt>(#name)) {                                   \
    WarningGadgets.push_back(std::make_unique<name##Gadget>(Result));          \
    NEXT;                                                                      \
  }
#include "clang/Analysis/Analyses/UnsafeBufferUsageGadgets.def"

      assert(numFound >= 1 && "Gadgets not found in match result!");
      assert(numFound <= 1 && "Conflicting bind tags in gadgets!");
    }
  };

  MatchFinder M;
  GadgetFinderCallback CB;

  // clang-format off
  M.addMatcher(
      stmt(
        forEachDescendantEvaluatedStmt(stmt(anyOf(
          // Add Gadget::matcher() for every gadget in the registry.
#define WARNING_GADGET(x)                                                      \
          allOf(x ## Gadget::matcher().bind(#x),                               \
                notInSafeBufferOptOut(&Handler)),
#define WARNING_CONTAINER_GADGET(x)                                            \
          allOf(x ## Gadget::matcher().bind(#x),                               \
                notInSafeBufferOptOut(&Handler),                               \
                unless(ignoreUnsafeBufferInContainer(&Handler))),
#include "clang/Analysis/Analyses/UnsafeBufferUsageGadgets.def"
            // Avoid a hanging comma.
            unless(stmt())
        )))
    ),
    &CB
  );
  // clang-format on

  if (EmitSuggestions) {
    // clang-format off
    M.addMatcher(
        stmt(
          forEachDescendantStmt(stmt(eachOf(
#define FIXABLE_GADGET(x)                                                      \
            x ## Gadget::matcher().bind(#x),
#include "clang/Analysis/Analyses/UnsafeBufferUsageGadgets.def"
            // In parallel, match all DeclRefExprs so that to find out
            // whether there are any uncovered by gadgets.
            declRefExpr(anyOf(hasPointerType(), hasArrayType()),
                        to(anyOf(varDecl(), bindingDecl()))).bind("any_dre"),
            // Also match DeclStmts because we'll need them when fixing
            // their underlying VarDecls that otherwise don't have
            // any backreferences to DeclStmts.
            declStmt().bind("any_ds")
          )))
      ),
      &CB
    );
    // clang-format on
  }

  M.match(*D->getBody(), D->getASTContext());
  return {std::move(CB.FixableGadgets), std::move(CB.WarningGadgets),
          std::move(CB.Tracker)};
}

// Compares AST nodes by source locations.
template <typename NodeTy> struct CompareNode {
  bool operator()(const NodeTy *N1, const NodeTy *N2) const {
    return N1->getBeginLoc().getRawEncoding() <
           N2->getBeginLoc().getRawEncoding();
  }
};

struct WarningGadgetSets {
  std::map<const VarDecl *, std::set<const WarningGadget *>,
           // To keep keys sorted by their locations in the map so that the
           // order is deterministic:
           CompareNode<VarDecl>>
      byVar;
  // These Gadgets are not related to pointer variables (e. g. temporaries).
  llvm::SmallVector<const WarningGadget *, 16> noVar;
};

static WarningGadgetSets
groupWarningGadgetsByVar(const WarningGadgetList &AllUnsafeOperations) {
  WarningGadgetSets result;
  // If some gadgets cover more than one
  // variable, they'll appear more than once in the map.
  for (auto &G : AllUnsafeOperations) {
    DeclUseList ClaimedVarUseSites = G->getClaimedVarUseSites();

    bool AssociatedWithVarDecl = false;
    for (const DeclRefExpr *DRE : ClaimedVarUseSites) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        result.byVar[VD].insert(G.get());
        AssociatedWithVarDecl = true;
      }
    }

    if (!AssociatedWithVarDecl) {
      result.noVar.push_back(G.get());
      continue;
    }
  }
  return result;
}

struct FixableGadgetSets {
  std::map<const VarDecl *, std::set<const FixableGadget *>,
           // To keep keys sorted by their locations in the map so that the
           // order is deterministic:
           CompareNode<VarDecl>>
      byVar;
};

static FixableGadgetSets
groupFixablesByVar(FixableGadgetList &&AllFixableOperations) {
  FixableGadgetSets FixablesForUnsafeVars;
  for (auto &F : AllFixableOperations) {
    DeclUseList DREs = F->getClaimedVarUseSites();

    for (const DeclRefExpr *DRE : DREs) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        FixablesForUnsafeVars.byVar[VD].insert(F.get());
      }
    }
  }
  return FixablesForUnsafeVars;
}

bool clang::internal::anyConflict(const SmallVectorImpl<FixItHint> &FixIts,
                                  const SourceManager &SM) {
  // A simple interval overlap detection algorithm.  Sorts all ranges by their
  // begin location then finds the first overlap in one pass.
  std::vector<const FixItHint *> All; // a copy of `FixIts`

  for (const FixItHint &H : FixIts)
    All.push_back(&H);
  std::sort(All.begin(), All.end(),
            [&SM](const FixItHint *H1, const FixItHint *H2) {
              return SM.isBeforeInTranslationUnit(H1->RemoveRange.getBegin(),
                                                  H2->RemoveRange.getBegin());
            });

  const FixItHint *CurrHint = nullptr;

  for (const FixItHint *Hint : All) {
    if (!CurrHint ||
        SM.isBeforeInTranslationUnit(CurrHint->RemoveRange.getEnd(),
                                     Hint->RemoveRange.getBegin())) {
      // Either to initialize `CurrHint` or `CurrHint` does not
      // overlap with `Hint`:
      CurrHint = Hint;
    } else
      // In case `Hint` overlaps the `CurrHint`, we found at least one
      // conflict:
      return true;
  }
  return false;
}

std::optional<FixItList>
PtrToPtrAssignmentGadget::getFixits(const FixitStrategy &S) const {
  const auto *LeftVD = cast<VarDecl>(PtrLHS->getDecl());
  const auto *RightVD = cast<VarDecl>(PtrRHS->getDecl());
  switch (S.lookup(LeftVD)) {
  case FixitStrategy::Kind::Span:
    if (S.lookup(RightVD) == FixitStrategy::Kind::Span)
      return FixItList{};
    return std::nullopt;
  case FixitStrategy::Kind::Wontfix:
    return std::nullopt;
  case FixitStrategy::Kind::Iterator:
  case FixitStrategy::Kind::Array:
    return std::nullopt;
  case FixitStrategy::Kind::Vector:
    llvm_unreachable("unsupported strategies for FixableGadgets");
  }
  return std::nullopt;
}

/// \returns fixit that adds .data() call after \DRE.
static inline std::optional<FixItList> createDataFixit(const ASTContext &Ctx,
                                                       const DeclRefExpr *DRE);

std::optional<FixItList>
CArrayToPtrAssignmentGadget::getFixits(const FixitStrategy &S) const {
  const auto *LeftVD = cast<VarDecl>(PtrLHS->getDecl());
  const auto *RightVD = cast<VarDecl>(PtrRHS->getDecl());
  // TLDR: Implementing fixits for non-Wontfix strategy on both LHS and RHS is
  // non-trivial.
  //
  // CArrayToPtrAssignmentGadget doesn't have strategy implications because
  // constant size array propagates its bounds. Because of that LHS and RHS are
  // addressed by two different fixits.
  //
  // At the same time FixitStrategy S doesn't reflect what group a fixit belongs
  // to and can't be generally relied on in multi-variable Fixables!
  //
  // E. g. If an instance of this gadget is fixing variable on LHS then the
  // variable on RHS is fixed by a different fixit and its strategy for LHS
  // fixit is as if Wontfix.
  //
  // The only exception is Wontfix strategy for a given variable as that is
  // valid for any fixit produced for the given input source code.
  if (S.lookup(LeftVD) == FixitStrategy::Kind::Span) {
    if (S.lookup(RightVD) == FixitStrategy::Kind::Wontfix) {
      return FixItList{};
    }
  } else if (S.lookup(LeftVD) == FixitStrategy::Kind::Wontfix) {
    if (S.lookup(RightVD) == FixitStrategy::Kind::Array) {
      return createDataFixit(RightVD->getASTContext(), PtrRHS);
    }
  }
  return std::nullopt;
}

std::optional<FixItList>
PointerInitGadget::getFixits(const FixitStrategy &S) const {
  const auto *LeftVD = PtrInitLHS;
  const auto *RightVD = cast<VarDecl>(PtrInitRHS->getDecl());
  switch (S.lookup(LeftVD)) {
  case FixitStrategy::Kind::Span:
    if (S.lookup(RightVD) == FixitStrategy::Kind::Span)
      return FixItList{};
    return std::nullopt;
  case FixitStrategy::Kind::Wontfix:
    return std::nullopt;
  case FixitStrategy::Kind::Iterator:
  case FixitStrategy::Kind::Array:
    return std::nullopt;
  case FixitStrategy::Kind::Vector:
    llvm_unreachable("unsupported strategies for FixableGadgets");
  }
  return std::nullopt;
}

static bool isNonNegativeIntegerExpr(const Expr *Expr, const VarDecl *VD,
                                     const ASTContext &Ctx) {
  if (auto ConstVal = Expr->getIntegerConstantExpr(Ctx)) {
    if (ConstVal->isNegative())
      return false;
  } else if (!Expr->getType()->isUnsignedIntegerType())
    return false;
  return true;
}

std::optional<FixItList>
ULCArraySubscriptGadget::getFixits(const FixitStrategy &S) const {
  if (const auto *DRE =
          dyn_cast<DeclRefExpr>(Node->getBase()->IgnoreImpCasts()))
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      switch (S.lookup(VD)) {
      case FixitStrategy::Kind::Span: {

        // If the index has a negative constant value, we give up as no valid
        // fix-it can be generated:
        const ASTContext &Ctx = // FIXME: we need ASTContext to be passed in!
            VD->getASTContext();
        if (!isNonNegativeIntegerExpr(Node->getIdx(), VD, Ctx))
          return std::nullopt;
        // no-op is a good fix-it, otherwise
        return FixItList{};
      }
      case FixitStrategy::Kind::Array:
        return FixItList{};
      case FixitStrategy::Kind::Wontfix:
      case FixitStrategy::Kind::Iterator:
      case FixitStrategy::Kind::Vector:
        llvm_unreachable("unsupported strategies for FixableGadgets");
      }
    }
  return std::nullopt;
}

static std::optional<FixItList> // forward declaration
fixUPCAddressofArraySubscriptWithSpan(const UnaryOperator *Node);

std::optional<FixItList>
UPCAddressofArraySubscriptGadget::getFixits(const FixitStrategy &S) const {
  auto DREs = getClaimedVarUseSites();
  const auto *VD = cast<VarDecl>(DREs.front()->getDecl());

  switch (S.lookup(VD)) {
  case FixitStrategy::Kind::Span:
    return fixUPCAddressofArraySubscriptWithSpan(Node);
  case FixitStrategy::Kind::Wontfix:
  case FixitStrategy::Kind::Iterator:
  case FixitStrategy::Kind::Array:
    return std::nullopt;
  case FixitStrategy::Kind::Vector:
    llvm_unreachable("unsupported strategies for FixableGadgets");
  }
  return std::nullopt; // something went wrong, no fix-it
}

// FIXME: this function should be customizable through format
static StringRef getEndOfLine() {
  static const char *const EOL = "\n";
  return EOL;
}

// Returns the text indicating that the user needs to provide input there:
std::string getUserFillPlaceHolder(StringRef HintTextToUser = "placeholder") {
  std::string s = std::string("<# ");
  s += HintTextToUser;
  s += " #>";
  return s;
}

// Return the source location of the last character of the AST `Node`.
template <typename NodeTy>
static std::optional<SourceLocation>
getEndCharLoc(const NodeTy *Node, const SourceManager &SM,
              const LangOptions &LangOpts) {
  unsigned TkLen = Lexer::MeasureTokenLength(Node->getEndLoc(), SM, LangOpts);
  SourceLocation Loc = Node->getEndLoc().getLocWithOffset(TkLen - 1);

  if (Loc.isValid())
    return Loc;

  return std::nullopt;
}

// Return the source location just past the last character of the AST `Node`.
template <typename NodeTy>
static std::optional<SourceLocation> getPastLoc(const NodeTy *Node,
                                                const SourceManager &SM,
                                                const LangOptions &LangOpts) {
  SourceLocation Loc =
      Lexer::getLocForEndOfToken(Node->getEndLoc(), 0, SM, LangOpts);
  if (Loc.isValid())
    return Loc;
  return std::nullopt;
}

// Return text representation of an `Expr`.
static std::optional<StringRef> getExprText(const Expr *E,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts) {
  std::optional<SourceLocation> LastCharLoc = getPastLoc(E, SM, LangOpts);

  if (LastCharLoc)
    return Lexer::getSourceText(
        CharSourceRange::getCharRange(E->getBeginLoc(), *LastCharLoc), SM,
        LangOpts);

  return std::nullopt;
}

// Returns the literal text in `SourceRange SR`, if `SR` is a valid range.
static std::optional<StringRef> getRangeText(SourceRange SR,
                                             const SourceManager &SM,
                                             const LangOptions &LangOpts) {
  bool Invalid = false;
  CharSourceRange CSR = CharSourceRange::getCharRange(SR);
  StringRef Text = Lexer::getSourceText(CSR, SM, LangOpts, &Invalid);

  if (!Invalid)
    return Text;
  return std::nullopt;
}

// Returns the begin location of the identifier of the given variable
// declaration.
static SourceLocation getVarDeclIdentifierLoc(const VarDecl *VD) {
  // According to the implementation of `VarDecl`, `VD->getLocation()` actually
  // returns the begin location of the identifier of the declaration:
  return VD->getLocation();
}

// Returns the literal text of the identifier of the given variable declaration.
static std::optional<StringRef>
getVarDeclIdentifierText(const VarDecl *VD, const SourceManager &SM,
                         const LangOptions &LangOpts) {
  SourceLocation ParmIdentBeginLoc = getVarDeclIdentifierLoc(VD);
  SourceLocation ParmIdentEndLoc =
      Lexer::getLocForEndOfToken(ParmIdentBeginLoc, 0, SM, LangOpts);

  if (ParmIdentEndLoc.isMacroID() &&
      !Lexer::isAtEndOfMacroExpansion(ParmIdentEndLoc, SM, LangOpts))
    return std::nullopt;
  return getRangeText({ParmIdentBeginLoc, ParmIdentEndLoc}, SM, LangOpts);
}

// We cannot fix a variable declaration if it has some other specifiers than the
// type specifier.  Because the source ranges of those specifiers could overlap
// with the source range that is being replaced using fix-its.  Especially when
// we often cannot obtain accurate source ranges of cv-qualified type
// specifiers.
// FIXME: also deal with type attributes
static bool hasUnsupportedSpecifiers(const VarDecl *VD,
                                     const SourceManager &SM) {
  // AttrRangeOverlapping: true if at least one attribute of `VD` overlaps the
  // source range of `VD`:
  bool AttrRangeOverlapping = llvm::any_of(VD->attrs(), [&](Attr *At) -> bool {
    return !(SM.isBeforeInTranslationUnit(At->getRange().getEnd(),
                                          VD->getBeginLoc())) &&
           !(SM.isBeforeInTranslationUnit(VD->getEndLoc(),
                                          At->getRange().getBegin()));
  });
  return VD->isInlineSpecified() || VD->isConstexpr() ||
         VD->hasConstantInitialization() || !VD->hasLocalStorage() ||
         AttrRangeOverlapping;
}

// Returns the `SourceRange` of `D`.  The reason why this function exists is
// that `D->getSourceRange()` may return a range where the end location is the
// starting location of the last token.  The end location of the source range
// returned by this function is the last location of the last token.
static SourceRange getSourceRangeToTokenEnd(const Decl *D,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts) {
  SourceLocation Begin = D->getBeginLoc();
  SourceLocation
      End = // `D->getEndLoc` should always return the starting location of the
      // last token, so we should get the end of the token
      Lexer::getLocForEndOfToken(D->getEndLoc(), 0, SM, LangOpts);

  return SourceRange(Begin, End);
}

// Returns the text of the pointee type of `T` from a `VarDecl` of a pointer
// type. The text is obtained through from `TypeLoc`s.  Since `TypeLoc` does not
// have source ranges of qualifiers ( The `QualifiedTypeLoc` looks hacky too me
// :( ), `Qualifiers` of the pointee type is returned separately through the
// output parameter `QualifiersToAppend`.
static std::optional<std::string>
getPointeeTypeText(const VarDecl *VD, const SourceManager &SM,
                   const LangOptions &LangOpts,
                   std::optional<Qualifiers> *QualifiersToAppend) {
  QualType Ty = VD->getType();
  QualType PteTy;

  assert(Ty->isPointerType() && !Ty->isFunctionPointerType() &&
         "Expecting a VarDecl of type of pointer to object type");
  PteTy = Ty->getPointeeType();

  TypeLoc TyLoc = VD->getTypeSourceInfo()->getTypeLoc().getUnqualifiedLoc();
  TypeLoc PteTyLoc;

  // We only deal with the cases that we know `TypeLoc::getNextTypeLoc` returns
  // the `TypeLoc` of the pointee type:
  switch (TyLoc.getTypeLocClass()) {
  case TypeLoc::ConstantArray:
  case TypeLoc::IncompleteArray:
  case TypeLoc::VariableArray:
  case TypeLoc::DependentSizedArray:
  case TypeLoc::Decayed:
    assert(isa<ParmVarDecl>(VD) && "An array type shall not be treated as a "
                                   "pointer type unless it decays.");
    PteTyLoc = TyLoc.getNextTypeLoc();
    break;
  case TypeLoc::Pointer:
    PteTyLoc = TyLoc.castAs<PointerTypeLoc>().getPointeeLoc();
    break;
  default:
    return std::nullopt;
  }
  if (PteTyLoc.isNull())
    // Sometimes we cannot get a useful `TypeLoc` for the pointee type, e.g.,
    // when the pointer type is `auto`.
    return std::nullopt;

  SourceLocation IdentLoc = getVarDeclIdentifierLoc(VD);

  if (!(IdentLoc.isValid() && PteTyLoc.getSourceRange().isValid())) {
    // We are expecting these locations to be valid. But in some cases, they are
    // not all valid. It is a Clang bug to me and we are not responsible for
    // fixing it.  So we will just give up for now when it happens.
    return std::nullopt;
  }

  // Note that TypeLoc.getEndLoc() returns the begin location of the last token:
  SourceLocation PteEndOfTokenLoc =
      Lexer::getLocForEndOfToken(PteTyLoc.getEndLoc(), 0, SM, LangOpts);

  if (!PteEndOfTokenLoc.isValid())
    // Sometimes we cannot get the end location of the pointee type, e.g., when
    // there are macros involved.
    return std::nullopt;
  if (!SM.isBeforeInTranslationUnit(PteEndOfTokenLoc, IdentLoc)) {
    // We only deal with the cases where the source text of the pointee type
    // appears on the left-hand side of the variable identifier completely,
    // including the following forms:
    // `T ident`,
    // `T ident[]`, where `T` is any type.
    // Examples of excluded cases are `T (*ident)[]` or `T ident[][n]`.
    return std::nullopt;
  }
  if (PteTy.hasQualifiers()) {
    // TypeLoc does not provide source ranges for qualifiers (it says it's
    // intentional but seems fishy to me), so we cannot get the full text
    // `PteTy` via source ranges.
    *QualifiersToAppend = PteTy.getQualifiers();
  }
  return getRangeText({PteTyLoc.getBeginLoc(), PteEndOfTokenLoc}, SM, LangOpts)
      ->str();
}

// Returns the text of the name (with qualifiers) of a `FunctionDecl`.
static std::optional<StringRef> getFunNameText(const FunctionDecl *FD,
                                               const SourceManager &SM,
                                               const LangOptions &LangOpts) {
  SourceLocation BeginLoc = FD->getQualifier()
                                ? FD->getQualifierLoc().getBeginLoc()
                                : FD->getNameInfo().getBeginLoc();
  // Note that `FD->getNameInfo().getEndLoc()` returns the begin location of the
  // last token:
  SourceLocation EndLoc = Lexer::getLocForEndOfToken(
      FD->getNameInfo().getEndLoc(), 0, SM, LangOpts);
  SourceRange NameRange{BeginLoc, EndLoc};

  return getRangeText(NameRange, SM, LangOpts);
}

// Returns the text representing a `std::span` type where the element type is
// represented by `EltTyText`.
//
// Note the optional parameter `Qualifiers`: one needs to pass qualifiers
// explicitly if the element type needs to be qualified.
static std::string
getSpanTypeText(StringRef EltTyText,
                std::optional<Qualifiers> Quals = std::nullopt) {
  const char *const SpanOpen = "std::span<";

  if (Quals)
    return SpanOpen + EltTyText.str() + ' ' + Quals->getAsString() + '>';
  return SpanOpen + EltTyText.str() + '>';
}

std::optional<FixItList>
DerefSimplePtrArithFixableGadget::getFixits(const FixitStrategy &s) const {
  const VarDecl *VD = dyn_cast<VarDecl>(BaseDeclRefExpr->getDecl());

  if (VD && s.lookup(VD) == FixitStrategy::Kind::Span) {
    ASTContext &Ctx = VD->getASTContext();
    // std::span can't represent elements before its begin()
    if (auto ConstVal = Offset->getIntegerConstantExpr(Ctx))
      if (ConstVal->isNegative())
        return std::nullopt;

    // note that the expr may (oddly) has multiple layers of parens
    // example:
    //   *((..(pointer + 123)..))
    // goal:
    //   pointer[123]
    // Fix-It:
    //   remove '*('
    //   replace ' + ' with '['
    //   replace ')' with ']'

    // example:
    //   *((..(123 + pointer)..))
    // goal:
    //   123[pointer]
    // Fix-It:
    //   remove '*('
    //   replace ' + ' with '['
    //   replace ')' with ']'

    const Expr *LHS = AddOp->getLHS(), *RHS = AddOp->getRHS();
    const SourceManager &SM = Ctx.getSourceManager();
    const LangOptions &LangOpts = Ctx.getLangOpts();
    CharSourceRange StarWithTrailWhitespace =
        clang::CharSourceRange::getCharRange(DerefOp->getOperatorLoc(),
                                             LHS->getBeginLoc());

    std::optional<SourceLocation> LHSLocation = getPastLoc(LHS, SM, LangOpts);
    if (!LHSLocation)
      return std::nullopt;

    CharSourceRange PlusWithSurroundingWhitespace =
        clang::CharSourceRange::getCharRange(*LHSLocation, RHS->getBeginLoc());

    std::optional<SourceLocation> AddOpLocation =
        getPastLoc(AddOp, SM, LangOpts);
    std::optional<SourceLocation> DerefOpLocation =
        getPastLoc(DerefOp, SM, LangOpts);

    if (!AddOpLocation || !DerefOpLocation)
      return std::nullopt;

    CharSourceRange ClosingParenWithPrecWhitespace =
        clang::CharSourceRange::getCharRange(*AddOpLocation, *DerefOpLocation);

    return FixItList{
        {FixItHint::CreateRemoval(StarWithTrailWhitespace),
         FixItHint::CreateReplacement(PlusWithSurroundingWhitespace, "["),
         FixItHint::CreateReplacement(ClosingParenWithPrecWhitespace, "]")}};
  }
  return std::nullopt; // something wrong or unsupported, give up
}

std::optional<FixItList>
PointerDereferenceGadget::getFixits(const FixitStrategy &S) const {
  const VarDecl *VD = cast<VarDecl>(BaseDeclRefExpr->getDecl());
  switch (S.lookup(VD)) {
  case FixitStrategy::Kind::Span: {
    ASTContext &Ctx = VD->getASTContext();
    SourceManager &SM = Ctx.getSourceManager();
    // Required changes: *(ptr); => (ptr[0]); and *ptr; => ptr[0]
    // Deletes the *operand
    CharSourceRange derefRange = clang::CharSourceRange::getCharRange(
        Op->getBeginLoc(), Op->getBeginLoc().getLocWithOffset(1));
    // Inserts the [0]
    if (auto LocPastOperand =
            getPastLoc(BaseDeclRefExpr, SM, Ctx.getLangOpts())) {
      return FixItList{{FixItHint::CreateRemoval(derefRange),
                        FixItHint::CreateInsertion(*LocPastOperand, "[0]")}};
    }
    break;
  }
  case FixitStrategy::Kind::Iterator:
  case FixitStrategy::Kind::Array:
    return std::nullopt;
  case FixitStrategy::Kind::Vector:
    llvm_unreachable("FixitStrategy not implemented yet!");
  case FixitStrategy::Kind::Wontfix:
    llvm_unreachable("Invalid strategy!");
  }

  return std::nullopt;
}

static inline std::optional<FixItList> createDataFixit(const ASTContext &Ctx,
                                                       const DeclRefExpr *DRE) {
  const SourceManager &SM = Ctx.getSourceManager();
  // Inserts the .data() after the DRE
  std::optional<SourceLocation> EndOfOperand =
      getPastLoc(DRE, SM, Ctx.getLangOpts());

  if (EndOfOperand)
    return FixItList{{FixItHint::CreateInsertion(*EndOfOperand, ".data()")}};

  return std::nullopt;
}

// Generates fix-its replacing an expression of the form UPC(DRE) with
// `DRE.data()`
std::optional<FixItList>
UPCStandalonePointerGadget::getFixits(const FixitStrategy &S) const {
  const auto VD = cast<VarDecl>(Node->getDecl());
  switch (S.lookup(VD)) {
  case FixitStrategy::Kind::Array:
  case FixitStrategy::Kind::Span: {
    return createDataFixit(VD->getASTContext(), Node);
    // FIXME: Points inside a macro expansion.
    break;
  }
  case FixitStrategy::Kind::Wontfix:
  case FixitStrategy::Kind::Iterator:
    return std::nullopt;
  case FixitStrategy::Kind::Vector:
    llvm_unreachable("unsupported strategies for FixableGadgets");
  }

  return std::nullopt;
}

// Generates fix-its replacing an expression of the form `&DRE[e]` with
// `&DRE.data()[e]`:
static std::optional<FixItList>
fixUPCAddressofArraySubscriptWithSpan(const UnaryOperator *Node) {
  const auto *ArraySub = cast<ArraySubscriptExpr>(Node->getSubExpr());
  const auto *DRE = cast<DeclRefExpr>(ArraySub->getBase()->IgnoreImpCasts());
  // FIXME: this `getASTContext` call is costly, we should pass the
  // ASTContext in:
  const ASTContext &Ctx = DRE->getDecl()->getASTContext();
  const Expr *Idx = ArraySub->getIdx();
  const SourceManager &SM = Ctx.getSourceManager();
  const LangOptions &LangOpts = Ctx.getLangOpts();
  std::stringstream SS;
  bool IdxIsLitZero = false;

  if (auto ICE = Idx->getIntegerConstantExpr(Ctx))
    if ((*ICE).isZero())
      IdxIsLitZero = true;
  std::optional<StringRef> DreString = getExprText(DRE, SM, LangOpts);
  if (!DreString)
    return std::nullopt;

  if (IdxIsLitZero) {
    // If the index is literal zero, we produce the most concise fix-it:
    SS << (*DreString).str() << ".data()";
  } else {
    std::optional<StringRef> IndexString = getExprText(Idx, SM, LangOpts);
    if (!IndexString)
      return std::nullopt;

    SS << "&" << (*DreString).str() << ".data()"
       << "[" << (*IndexString).str() << "]";
  }
  return FixItList{
      FixItHint::CreateReplacement(Node->getSourceRange(), SS.str())};
}

std::optional<FixItList>
UUCAddAssignGadget::getFixits(const FixitStrategy &S) const {
  DeclUseList DREs = getClaimedVarUseSites();

  if (DREs.size() != 1)
    return std::nullopt; // In cases of `Ptr += n` where `Ptr` is not a DRE, we
                         // give up
  if (const VarDecl *VD = dyn_cast<VarDecl>(DREs.front()->getDecl())) {
    if (S.lookup(VD) == FixitStrategy::Kind::Span) {
      FixItList Fixes;

      const Stmt *AddAssignNode = Node;
      StringRef varName = VD->getName();
      const ASTContext &Ctx = VD->getASTContext();

      if (!isNonNegativeIntegerExpr(Offset, VD, Ctx))
        return std::nullopt;

      // To transform UUC(p += n) to UUC(p = p.subspan(..)):
      bool NotParenExpr =
          (Offset->IgnoreParens()->getBeginLoc() == Offset->getBeginLoc());
      std::string SS = varName.str() + " = " + varName.str() + ".subspan";
      if (NotParenExpr)
        SS += "(";

      std::optional<SourceLocation> AddAssignLocation = getEndCharLoc(
          AddAssignNode, Ctx.getSourceManager(), Ctx.getLangOpts());
      if (!AddAssignLocation)
        return std::nullopt;

      Fixes.push_back(FixItHint::CreateReplacement(
          SourceRange(AddAssignNode->getBeginLoc(), Node->getOperatorLoc()),
          SS));
      if (NotParenExpr)
        Fixes.push_back(FixItHint::CreateInsertion(
            Offset->getEndLoc().getLocWithOffset(1), ")"));
      return Fixes;
    }
  }
  return std::nullopt; // Not in the cases that we can handle for now, give up.
}

std::optional<FixItList>
UPCPreIncrementGadget::getFixits(const FixitStrategy &S) const {
  DeclUseList DREs = getClaimedVarUseSites();

  if (DREs.size() != 1)
    return std::nullopt; // In cases of `++Ptr` where `Ptr` is not a DRE, we
                         // give up
  if (const VarDecl *VD = dyn_cast<VarDecl>(DREs.front()->getDecl())) {
    if (S.lookup(VD) == FixitStrategy::Kind::Span) {
      FixItList Fixes;
      std::stringstream SS;
      StringRef varName = VD->getName();
      const ASTContext &Ctx = VD->getASTContext();

      // To transform UPC(++p) to UPC((p = p.subspan(1)).data()):
      SS << "(" << varName.data() << " = " << varName.data()
         << ".subspan(1)).data()";
      std::optional<SourceLocation> PreIncLocation =
          getEndCharLoc(Node, Ctx.getSourceManager(), Ctx.getLangOpts());
      if (!PreIncLocation)
        return std::nullopt;

      Fixes.push_back(FixItHint::CreateReplacement(
          SourceRange(Node->getBeginLoc(), *PreIncLocation), SS.str()));
      return Fixes;
    }
  }
  return std::nullopt; // Not in the cases that we can handle for now, give up.
}

// For a non-null initializer `Init` of `T *` type, this function returns
// `FixItHint`s producing a list initializer `{Init,  S}` as a part of a fix-it
// to output stream.
// In many cases, this function cannot figure out the actual extent `S`.  It
// then will use a place holder to replace `S` to ask users to fill `S` in.  The
// initializer shall be used to initialize a variable of type `std::span<T>`.
// In some cases (e. g. constant size array) the initializer should remain
// unchanged and the function returns empty list. In case the function can't
// provide the right fixit it will return nullopt.
//
// FIXME: Support multi-level pointers
//
// Parameters:
//   `Init` a pointer to the initializer expression
//   `Ctx` a reference to the ASTContext
static std::optional<FixItList>
FixVarInitializerWithSpan(const Expr *Init, ASTContext &Ctx,
                          const StringRef UserFillPlaceHolder) {
  const SourceManager &SM = Ctx.getSourceManager();
  const LangOptions &LangOpts = Ctx.getLangOpts();

  // If `Init` has a constant value that is (or equivalent to) a
  // NULL pointer, we use the default constructor to initialize the span
  // object, i.e., a `std:span` variable declaration with no initializer.
  // So the fix-it is just to remove the initializer.
  if (Init->isNullPointerConstant(
          Ctx,
          // FIXME: Why does this function not ask for `const ASTContext
          // &`? It should. Maybe worth an NFC patch later.
          Expr::NullPointerConstantValueDependence::
              NPC_ValueDependentIsNotNull)) {
    std::optional<SourceLocation> InitLocation =
        getEndCharLoc(Init, SM, LangOpts);
    if (!InitLocation)
      return std::nullopt;

    SourceRange SR(Init->getBeginLoc(), *InitLocation);

    return FixItList{FixItHint::CreateRemoval(SR)};
  }

  FixItList FixIts{};
  std::string ExtentText = UserFillPlaceHolder.data();
  StringRef One = "1";

  // Insert `{` before `Init`:
  FixIts.push_back(FixItHint::CreateInsertion(Init->getBeginLoc(), "{"));
  // Try to get the data extent. Break into different cases:
  if (auto CxxNew = dyn_cast<CXXNewExpr>(Init->IgnoreImpCasts())) {
    // In cases `Init` is `new T[n]` and there is no explicit cast over
    // `Init`, we know that `Init` must evaluates to a pointer to `n` objects
    // of `T`. So the extent is `n` unless `n` has side effects.  Similar but
    // simpler for the case where `Init` is `new T`.
    if (const Expr *Ext = CxxNew->getArraySize().value_or(nullptr)) {
      if (!Ext->HasSideEffects(Ctx)) {
        std::optional<StringRef> ExtentString = getExprText(Ext, SM, LangOpts);
        if (!ExtentString)
          return std::nullopt;
        ExtentText = *ExtentString;
      }
    } else if (!CxxNew->isArray())
      // Although the initializer is not allocating a buffer, the pointer
      // variable could still be used in buffer access operations.
      ExtentText = One;
  } else if (Ctx.getAsConstantArrayType(Init->IgnoreImpCasts()->getType())) {
    // std::span has a single parameter constructor for initialization with
    // constant size array. The size is auto-deduced as the constructor is a
    // function template. The correct fixit is empty - no changes should happen.
    return FixItList{};
  } else {
    // In cases `Init` is of the form `&Var` after stripping of implicit
    // casts, where `&` is the built-in operator, the extent is 1.
    if (auto AddrOfExpr = dyn_cast<UnaryOperator>(Init->IgnoreImpCasts()))
      if (AddrOfExpr->getOpcode() == UnaryOperatorKind::UO_AddrOf &&
          isa_and_present<DeclRefExpr>(AddrOfExpr->getSubExpr()))
        ExtentText = One;
    // TODO: we can handle more cases, e.g., `&a[0]`, `&a`, `std::addressof`,
    // and explicit casting, etc. etc.
  }

  SmallString<32> StrBuffer{};
  std::optional<SourceLocation> LocPassInit = getPastLoc(Init, SM, LangOpts);

  if (!LocPassInit)
    return std::nullopt;

  StrBuffer.append(", ");
  StrBuffer.append(ExtentText);
  StrBuffer.append("}");
  FixIts.push_back(FixItHint::CreateInsertion(*LocPassInit, StrBuffer.str()));
  return FixIts;
}

#ifndef NDEBUG
#define DEBUG_NOTE_DECL_FAIL(D, Msg)                                           \
  Handler.addDebugNoteForVar((D), (D)->getBeginLoc(),                          \
                             "failed to produce fixit for declaration '" +     \
                                 (D)->getNameAsString() + "'" + (Msg))
#else
#define DEBUG_NOTE_DECL_FAIL(D, Msg)
#endif

// For the given variable declaration with a pointer-to-T type, returns the text
// `std::span<T>`.  If it is unable to generate the text, returns
// `std::nullopt`.
static std::optional<std::string>
createSpanTypeForVarDecl(const VarDecl *VD, const ASTContext &Ctx) {
  assert(VD->getType()->isPointerType());

  std::optional<Qualifiers> PteTyQualifiers = std::nullopt;
  std::optional<std::string> PteTyText = getPointeeTypeText(
      VD, Ctx.getSourceManager(), Ctx.getLangOpts(), &PteTyQualifiers);

  if (!PteTyText)
    return std::nullopt;

  std::string SpanTyText = "std::span<";

  SpanTyText.append(*PteTyText);
  // Append qualifiers to span element type if any:
  if (PteTyQualifiers) {
    SpanTyText.append(" ");
    SpanTyText.append(PteTyQualifiers->getAsString());
  }
  SpanTyText.append(">");
  return SpanTyText;
}

// For a `VarDecl` of the form `T  * var (= Init)?`, this
// function generates fix-its that
//  1) replace `T * var` with `std::span<T> var`; and
//  2) change `Init` accordingly to a span constructor, if it exists.
//
// FIXME: support Multi-level pointers
//
// Parameters:
//   `D` a pointer the variable declaration node
//   `Ctx` a reference to the ASTContext
//   `UserFillPlaceHolder` the user-input placeholder text
// Returns:
//    the non-empty fix-it list, if fix-its are successfuly generated; empty
//    list otherwise.
static FixItList fixLocalVarDeclWithSpan(const VarDecl *D, ASTContext &Ctx,
                                         const StringRef UserFillPlaceHolder,
                                         UnsafeBufferUsageHandler &Handler) {
  if (hasUnsupportedSpecifiers(D, Ctx.getSourceManager()))
    return {};

  FixItList FixIts{};
  std::optional<std::string> SpanTyText = createSpanTypeForVarDecl(D, Ctx);

  if (!SpanTyText) {
    DEBUG_NOTE_DECL_FAIL(D, " : failed to generate 'std::span' type");
    return {};
  }

  // Will hold the text for `std::span<T> Ident`:
  std::stringstream SS;

  SS << *SpanTyText;
  // Fix the initializer if it exists:
  if (const Expr *Init = D->getInit()) {
    std::optional<FixItList> InitFixIts =
        FixVarInitializerWithSpan(Init, Ctx, UserFillPlaceHolder);
    if (!InitFixIts)
      return {};
    FixIts.insert(FixIts.end(), std::make_move_iterator(InitFixIts->begin()),
                  std::make_move_iterator(InitFixIts->end()));
  }
  // For declaration of the form `T * ident = init;`, we want to replace
  // `T * ` with `std::span<T>`.
  // We ignore CV-qualifiers so for `T * const ident;` we also want to replace
  // just `T *` with `std::span<T>`.
  const SourceLocation EndLocForReplacement = D->getTypeSpecEndLoc();
  if (!EndLocForReplacement.isValid()) {
    DEBUG_NOTE_DECL_FAIL(D, " : failed to locate the end of the declaration");
    return {};
  }
  // The only exception is that for `T *ident` we'll add a single space between
  // "std::span<T>" and "ident".
  // FIXME: The condition is false for identifiers expended from macros.
  if (EndLocForReplacement.getLocWithOffset(1) == getVarDeclIdentifierLoc(D))
    SS << " ";

  FixIts.push_back(FixItHint::CreateReplacement(
      SourceRange(D->getBeginLoc(), EndLocForReplacement), SS.str()));
  return FixIts;
}

static bool hasConflictingOverload(const FunctionDecl *FD) {
  return !FD->getDeclContext()->lookup(FD->getDeclName()).isSingleResult();
}

// For a `FunctionDecl`, whose `ParmVarDecl`s are being changed to have new
// types, this function produces fix-its to make the change self-contained.  Let
// 'F' be the entity defined by the original `FunctionDecl` and "NewF" be the
// entity defined by the `FunctionDecl` after the change to the parameters.
// Fix-its produced by this function are
//   1. Add the `[[clang::unsafe_buffer_usage]]` attribute to each declaration
//   of 'F';
//   2. Create a declaration of "NewF" next to each declaration of `F`;
//   3. Create a definition of "F" (as its' original definition is now belongs
//      to "NewF") next to its original definition.  The body of the creating
//      definition calls to "NewF".
//
// Example:
//
// void f(int *p);  // original declaration
// void f(int *p) { // original definition
//    p[5];
// }
//
// To change the parameter `p` to be of `std::span<int>` type, we
// also add overloads:
//
// [[clang::unsafe_buffer_usage]] void f(int *p); // original decl
// void f(std::span<int> p);                      // added overload decl
// void f(std::span<int> p) {     // original def where param is changed
//    p[5];
// }
// [[clang::unsafe_buffer_usage]] void f(int *p) {  // added def
//   return f(std::span(p, <# size #>));
// }
//
static std::optional<FixItList>
createOverloadsForFixedParams(const FixitStrategy &S, const FunctionDecl *FD,
                              const ASTContext &Ctx,
                              UnsafeBufferUsageHandler &Handler) {
  // FIXME: need to make this conflict checking better:
  if (hasConflictingOverload(FD))
    return std::nullopt;

  const SourceManager &SM = Ctx.getSourceManager();
  const LangOptions &LangOpts = Ctx.getLangOpts();
  const unsigned NumParms = FD->getNumParams();
  std::vector<std::string> NewTysTexts(NumParms);
  std::vector<bool> ParmsMask(NumParms, false);
  bool AtLeastOneParmToFix = false;

  for (unsigned i = 0; i < NumParms; i++) {
    const ParmVarDecl *PVD = FD->getParamDecl(i);

    if (S.lookup(PVD) == FixitStrategy::Kind::Wontfix)
      continue;
    if (S.lookup(PVD) != FixitStrategy::Kind::Span)
      // Not supported, not suppose to happen:
      return std::nullopt;

    std::optional<Qualifiers> PteTyQuals = std::nullopt;
    std::optional<std::string> PteTyText =
        getPointeeTypeText(PVD, SM, LangOpts, &PteTyQuals);

    if (!PteTyText)
      // something wrong in obtaining the text of the pointee type, give up
      return std::nullopt;
    // FIXME: whether we should create std::span type depends on the
    // FixitStrategy.
    NewTysTexts[i] = getSpanTypeText(*PteTyText, PteTyQuals);
    ParmsMask[i] = true;
    AtLeastOneParmToFix = true;
  }
  if (!AtLeastOneParmToFix)
    // No need to create function overloads:
    return {};
  // FIXME Respect indentation of the original code.

  // A lambda that creates the text representation of a function declaration
  // with the new type signatures:
  const auto NewOverloadSignatureCreator =
      [&SM, &LangOpts, &NewTysTexts,
       &ParmsMask](const FunctionDecl *FD) -> std::optional<std::string> {
    std::stringstream SS;

    SS << ";";
    SS << getEndOfLine().str();
    // Append: ret-type func-name "("
    if (auto Prefix = getRangeText(
            SourceRange(FD->getBeginLoc(), (*FD->param_begin())->getBeginLoc()),
            SM, LangOpts))
      SS << Prefix->str();
    else
      return std::nullopt; // give up
    // Append: parameter-type-list
    const unsigned NumParms = FD->getNumParams();

    for (unsigned i = 0; i < NumParms; i++) {
      const ParmVarDecl *Parm = FD->getParamDecl(i);

      if (Parm->isImplicit())
        continue;
      if (ParmsMask[i]) {
        // This `i`-th parameter will be fixed with `NewTysTexts[i]` being its
        // new type:
        SS << NewTysTexts[i];
        // print parameter name if provided:
        if (IdentifierInfo *II = Parm->getIdentifier())
          SS << ' ' << II->getName().str();
      } else if (auto ParmTypeText =
                     getRangeText(getSourceRangeToTokenEnd(Parm, SM, LangOpts),
                                  SM, LangOpts)) {
        // print the whole `Parm` without modification:
        SS << ParmTypeText->str();
      } else
        return std::nullopt; // something wrong, give up
      if (i != NumParms - 1)
        SS << ", ";
    }
    SS << ")";
    return SS.str();
  };

  // A lambda that creates the text representation of a function definition with
  // the original signature:
  const auto OldOverloadDefCreator =
      [&Handler, &SM, &LangOpts, &NewTysTexts,
       &ParmsMask](const FunctionDecl *FD) -> std::optional<std::string> {
    std::stringstream SS;

    SS << getEndOfLine().str();
    // Append: attr-name ret-type func-name "(" param-list ")" "{"
    if (auto FDPrefix = getRangeText(
            SourceRange(FD->getBeginLoc(), FD->getBody()->getBeginLoc()), SM,
            LangOpts))
      SS << Handler.getUnsafeBufferUsageAttributeTextAt(FD->getBeginLoc(), " ")
         << FDPrefix->str() << "{";
    else
      return std::nullopt;
    // Append: "return" func-name "("
    if (auto FunQualName = getFunNameText(FD, SM, LangOpts))
      SS << "return " << FunQualName->str() << "(";
    else
      return std::nullopt;

    // Append: arg-list
    const unsigned NumParms = FD->getNumParams();
    for (unsigned i = 0; i < NumParms; i++) {
      const ParmVarDecl *Parm = FD->getParamDecl(i);

      if (Parm->isImplicit())
        continue;
      // FIXME: If a parameter has no name, it is unused in the
      // definition. So we could just leave it as it is.
      if (!Parm->getIdentifier())
        // If a parameter of a function definition has no name:
        return std::nullopt;
      if (ParmsMask[i])
        // This is our spanified paramter!
        SS << NewTysTexts[i] << "(" << Parm->getIdentifier()->getName().str()
           << ", " << getUserFillPlaceHolder("size") << ")";
      else
        SS << Parm->getIdentifier()->getName().str();
      if (i != NumParms - 1)
        SS << ", ";
    }
    // finish call and the body
    SS << ");}" << getEndOfLine().str();
    // FIXME: 80-char line formatting?
    return SS.str();
  };

  FixItList FixIts{};
  for (FunctionDecl *FReDecl : FD->redecls()) {
    std::optional<SourceLocation> Loc = getPastLoc(FReDecl, SM, LangOpts);

    if (!Loc)
      return {};
    if (FReDecl->isThisDeclarationADefinition()) {
      assert(FReDecl == FD && "inconsistent function definition");
      // Inserts a definition with the old signature to the end of
      // `FReDecl`:
      if (auto OldOverloadDef = OldOverloadDefCreator(FReDecl))
        FixIts.emplace_back(FixItHint::CreateInsertion(*Loc, *OldOverloadDef));
      else
        return {}; // give up
    } else {
      // Adds the unsafe-buffer attribute (if not already there) to `FReDecl`:
      if (!FReDecl->hasAttr<UnsafeBufferUsageAttr>()) {
        FixIts.emplace_back(FixItHint::CreateInsertion(
            FReDecl->getBeginLoc(), Handler.getUnsafeBufferUsageAttributeTextAt(
                                        FReDecl->getBeginLoc(), " ")));
      }
      // Inserts a declaration with the new signature to the end of `FReDecl`:
      if (auto NewOverloadDecl = NewOverloadSignatureCreator(FReDecl))
        FixIts.emplace_back(FixItHint::CreateInsertion(*Loc, *NewOverloadDecl));
      else
        return {};
    }
  }
  return FixIts;
}

// To fix a `ParmVarDecl` to be of `std::span` type.
static FixItList fixParamWithSpan(const ParmVarDecl *PVD, const ASTContext &Ctx,
                                  UnsafeBufferUsageHandler &Handler) {
  if (hasUnsupportedSpecifiers(PVD, Ctx.getSourceManager())) {
    DEBUG_NOTE_DECL_FAIL(PVD, " : has unsupport specifier(s)");
    return {};
  }
  if (PVD->hasDefaultArg()) {
    // FIXME: generate fix-its for default values:
    DEBUG_NOTE_DECL_FAIL(PVD, " : has default arg");
    return {};
  }

  std::optional<Qualifiers> PteTyQualifiers = std::nullopt;
  std::optional<std::string> PteTyText = getPointeeTypeText(
      PVD, Ctx.getSourceManager(), Ctx.getLangOpts(), &PteTyQualifiers);

  if (!PteTyText) {
    DEBUG_NOTE_DECL_FAIL(PVD, " : invalid pointee type");
    return {};
  }

  std::optional<StringRef> PVDNameText = PVD->getIdentifier()->getName();

  if (!PVDNameText) {
    DEBUG_NOTE_DECL_FAIL(PVD, " : invalid identifier name");
    return {};
  }

  std::stringstream SS;
  std::optional<std::string> SpanTyText = createSpanTypeForVarDecl(PVD, Ctx);

  if (PteTyQualifiers)
    // Append qualifiers if they exist:
    SS << getSpanTypeText(*PteTyText, PteTyQualifiers);
  else
    SS << getSpanTypeText(*PteTyText);
  // Append qualifiers to the type of the parameter:
  if (PVD->getType().hasQualifiers())
    SS << ' ' << PVD->getType().getQualifiers().getAsString();
  // Append parameter's name:
  SS << ' ' << PVDNameText->str();
  // Add replacement fix-it:
  return {FixItHint::CreateReplacement(PVD->getSourceRange(), SS.str())};
}

static FixItList fixVariableWithSpan(const VarDecl *VD,
                                     const DeclUseTracker &Tracker,
                                     ASTContext &Ctx,
                                     UnsafeBufferUsageHandler &Handler) {
  const DeclStmt *DS = Tracker.lookupDecl(VD);
  if (!DS) {
    DEBUG_NOTE_DECL_FAIL(VD,
                         " : variables declared this way not implemented yet");
    return {};
  }
  if (!DS->isSingleDecl()) {
    // FIXME: to support handling multiple `VarDecl`s in a single `DeclStmt`
    DEBUG_NOTE_DECL_FAIL(VD, " : multiple VarDecls");
    return {};
  }
  // Currently DS is an unused variable but we'll need it when
  // non-single decls are implemented, where the pointee type name
  // and the '*' are spread around the place.
  (void)DS;

  // FIXME: handle cases where DS has multiple declarations
  return fixLocalVarDeclWithSpan(VD, Ctx, getUserFillPlaceHolder(), Handler);
}

static FixItList fixVarDeclWithArray(const VarDecl *D, const ASTContext &Ctx,
                                     UnsafeBufferUsageHandler &Handler) {
  FixItList FixIts{};

  // Note: the code below expects the declaration to not use any type sugar like
  // typedef.
  if (auto CAT = dyn_cast<clang::ConstantArrayType>(D->getType())) {
    const QualType &ArrayEltT = CAT->getElementType();
    assert(!ArrayEltT.isNull() && "Trying to fix a non-array type variable!");
    // FIXME: support multi-dimensional arrays
    if (isa<clang::ArrayType>(ArrayEltT.getCanonicalType()))
      return {};

    const SourceLocation IdentifierLoc = getVarDeclIdentifierLoc(D);

    // Get the spelling of the element type as written in the source file
    // (including macros, etc.).
    auto MaybeElemTypeTxt =
        getRangeText({D->getBeginLoc(), IdentifierLoc}, Ctx.getSourceManager(),
                     Ctx.getLangOpts());
    if (!MaybeElemTypeTxt)
      return {};
    const llvm::StringRef ElemTypeTxt = MaybeElemTypeTxt->trim();

    // Find the '[' token.
    std::optional<Token> NextTok = Lexer::findNextToken(
        IdentifierLoc, Ctx.getSourceManager(), Ctx.getLangOpts());
    while (NextTok && !NextTok->is(tok::l_square) &&
           NextTok->getLocation() <= D->getSourceRange().getEnd())
      NextTok = Lexer::findNextToken(NextTok->getLocation(),
                                     Ctx.getSourceManager(), Ctx.getLangOpts());
    if (!NextTok)
      return {};
    const SourceLocation LSqBracketLoc = NextTok->getLocation();

    // Get the spelling of the array size as written in the source file
    // (including macros, etc.).
    auto MaybeArraySizeTxt = getRangeText(
        {LSqBracketLoc.getLocWithOffset(1), D->getTypeSpecEndLoc()},
        Ctx.getSourceManager(), Ctx.getLangOpts());
    if (!MaybeArraySizeTxt)
      return {};
    const llvm::StringRef ArraySizeTxt = MaybeArraySizeTxt->trim();
    if (ArraySizeTxt.empty()) {
      // FIXME: Support array size getting determined from the initializer.
      // Examples:
      //    int arr1[] = {0, 1, 2};
      //    int arr2{3, 4, 5};
      // We might be able to preserve the non-specified size with `auto` and
      // `std::to_array`:
      //    auto arr1 = std::to_array<int>({0, 1, 2});
      return {};
    }

    std::optional<StringRef> IdentText =
        getVarDeclIdentifierText(D, Ctx.getSourceManager(), Ctx.getLangOpts());

    if (!IdentText) {
      DEBUG_NOTE_DECL_FAIL(D, " : failed to locate the identifier");
      return {};
    }

    SmallString<32> Replacement;
    raw_svector_ostream OS(Replacement);
    OS << "std::array<" << ElemTypeTxt << ", " << ArraySizeTxt << "> "
       << IdentText->str();

    FixIts.push_back(FixItHint::CreateReplacement(
        SourceRange{D->getBeginLoc(), D->getTypeSpecEndLoc()}, OS.str()));
  }

  return FixIts;
}

static FixItList fixVariableWithArray(const VarDecl *VD,
                                      const DeclUseTracker &Tracker,
                                      const ASTContext &Ctx,
                                      UnsafeBufferUsageHandler &Handler) {
  const DeclStmt *DS = Tracker.lookupDecl(VD);
  assert(DS && "Fixing non-local variables not implemented yet!");
  if (!DS->isSingleDecl()) {
    // FIXME: to support handling multiple `VarDecl`s in a single `DeclStmt`
    return {};
  }
  // Currently DS is an unused variable but we'll need it when
  // non-single decls are implemented, where the pointee type name
  // and the '*' are spread around the place.
  (void)DS;

  // FIXME: handle cases where DS has multiple declarations
  return fixVarDeclWithArray(VD, Ctx, Handler);
}

// TODO: we should be consistent to use `std::nullopt` to represent no-fix due
// to any unexpected problem.
static FixItList
fixVariable(const VarDecl *VD, FixitStrategy::Kind K,
            /* The function decl under analysis */ const Decl *D,
            const DeclUseTracker &Tracker, ASTContext &Ctx,
            UnsafeBufferUsageHandler &Handler) {
  if (const auto *PVD = dyn_cast<ParmVarDecl>(VD)) {
    auto *FD = dyn_cast<clang::FunctionDecl>(PVD->getDeclContext());
    if (!FD || FD != D) {
      // `FD != D` means that `PVD` belongs to a function that is not being
      // analyzed currently.  Thus `FD` may not be complete.
      DEBUG_NOTE_DECL_FAIL(VD, " : function not currently analyzed");
      return {};
    }

    // TODO If function has a try block we can't change params unless we check
    // also its catch block for their use.
    // FIXME We might support static class methods, some select methods,
    // operators and possibly lamdas.
    if (FD->isMain() || FD->isConstexpr() ||
        FD->getTemplatedKind() != FunctionDecl::TemplatedKind::TK_NonTemplate ||
        FD->isVariadic() ||
        // also covers call-operator of lamdas
        isa<CXXMethodDecl>(FD) ||
        // skip when the function body is a try-block
        (FD->hasBody() && isa<CXXTryStmt>(FD->getBody())) ||
        FD->isOverloadedOperator()) {
      DEBUG_NOTE_DECL_FAIL(VD, " : unsupported function decl");
      return {}; // TODO test all these cases
    }
  }

  switch (K) {
  case FixitStrategy::Kind::Span: {
    if (VD->getType()->isPointerType()) {
      if (const auto *PVD = dyn_cast<ParmVarDecl>(VD))
        return fixParamWithSpan(PVD, Ctx, Handler);

      if (VD->isLocalVarDecl())
        return fixVariableWithSpan(VD, Tracker, Ctx, Handler);
    }
    DEBUG_NOTE_DECL_FAIL(VD, " : not a pointer");
    return {};
  }
  case FixitStrategy::Kind::Array: {
    if (VD->isLocalVarDecl() &&
        isa<clang::ConstantArrayType>(VD->getType().getCanonicalType()))
      return fixVariableWithArray(VD, Tracker, Ctx, Handler);

    DEBUG_NOTE_DECL_FAIL(VD, " : not a local const-size array");
    return {};
  }
  case FixitStrategy::Kind::Iterator:
  case FixitStrategy::Kind::Vector:
    llvm_unreachable("FixitStrategy not implemented yet!");
  case FixitStrategy::Kind::Wontfix:
    llvm_unreachable("Invalid strategy!");
  }
  llvm_unreachable("Unknown strategy!");
}

// Returns true iff there exists a `FixItHint` 'h' in `FixIts` such that the
// `RemoveRange` of 'h' overlaps with a macro use.
static bool overlapWithMacro(const FixItList &FixIts) {
  // FIXME: For now we only check if the range (or the first token) is (part of)
  // a macro expansion.  Ideally, we want to check for all tokens in the range.
  return llvm::any_of(FixIts, [](const FixItHint &Hint) {
    auto Range = Hint.RemoveRange;
    if (Range.getBegin().isMacroID() || Range.getEnd().isMacroID())
      // If the range (or the first token) is (part of) a macro expansion:
      return true;
    return false;
  });
}

// Returns true iff `VD` is a parameter of the declaration `D`:
static bool isParameterOf(const VarDecl *VD, const Decl *D) {
  return isa<ParmVarDecl>(VD) &&
         VD->getDeclContext() == dyn_cast<DeclContext>(D);
}

// Erases variables in `FixItsForVariable`, if such a variable has an unfixable
// group mate.  A variable `v` is unfixable iff `FixItsForVariable` does not
// contain `v`.
static void eraseVarsForUnfixableGroupMates(
    std::map<const VarDecl *, FixItList> &FixItsForVariable,
    const VariableGroupsManager &VarGrpMgr) {
  // Variables will be removed from `FixItsForVariable`:
  SmallVector<const VarDecl *, 8> ToErase;

  for (const auto &[VD, Ignore] : FixItsForVariable) {
    VarGrpRef Grp = VarGrpMgr.getGroupOfVar(VD);
    if (llvm::any_of(Grp,
                     [&FixItsForVariable](const VarDecl *GrpMember) -> bool {
                       return !FixItsForVariable.count(GrpMember);
                     })) {
      // At least one group member cannot be fixed, so we have to erase the
      // whole group:
      for (const VarDecl *Member : Grp)
        ToErase.push_back(Member);
    }
  }
  for (auto *VarToErase : ToErase)
    FixItsForVariable.erase(VarToErase);
}

// Returns the fix-its that create bounds-safe function overloads for the
// function `D`, if `D`'s parameters will be changed to safe-types through
// fix-its in `FixItsForVariable`.
//
// NOTE: In case `D`'s parameters will be changed but bounds-safe function
// overloads cannot created, the whole group that contains the parameters will
// be erased from `FixItsForVariable`.
static FixItList createFunctionOverloadsForParms(
    std::map<const VarDecl *, FixItList> &FixItsForVariable /* mutable */,
    const VariableGroupsManager &VarGrpMgr, const FunctionDecl *FD,
    const FixitStrategy &S, ASTContext &Ctx,
    UnsafeBufferUsageHandler &Handler) {
  FixItList FixItsSharedByParms{};

  std::optional<FixItList> OverloadFixes =
      createOverloadsForFixedParams(S, FD, Ctx, Handler);

  if (OverloadFixes) {
    FixItsSharedByParms.append(*OverloadFixes);
  } else {
    // Something wrong in generating `OverloadFixes`, need to remove the
    // whole group, where parameters are in, from `FixItsForVariable` (Note
    // that all parameters should be in the same group):
    for (auto *Member : VarGrpMgr.getGroupOfParms())
      FixItsForVariable.erase(Member);
  }
  return FixItsSharedByParms;
}

// Constructs self-contained fix-its for each variable in `FixablesForAllVars`.
static std::map<const VarDecl *, FixItList>
getFixIts(FixableGadgetSets &FixablesForAllVars, const FixitStrategy &S,
          ASTContext &Ctx,
          /* The function decl under analysis */ const Decl *D,
          const DeclUseTracker &Tracker, UnsafeBufferUsageHandler &Handler,
          const VariableGroupsManager &VarGrpMgr) {
  // `FixItsForVariable` will map each variable to a set of fix-its directly
  // associated to the variable itself.  Fix-its of distinct variables in
  // `FixItsForVariable` are disjoint.
  std::map<const VarDecl *, FixItList> FixItsForVariable;

  // Populate `FixItsForVariable` with fix-its directly associated with each
  // variable.  Fix-its directly associated to a variable 'v' are the ones
  // produced by the `FixableGadget`s whose claimed variable is 'v'.
  for (const auto &[VD, Fixables] : FixablesForAllVars.byVar) {
    FixItsForVariable[VD] =
        fixVariable(VD, S.lookup(VD), D, Tracker, Ctx, Handler);
    // If we fail to produce Fix-It for the declaration we have to skip the
    // variable entirely.
    if (FixItsForVariable[VD].empty()) {
      FixItsForVariable.erase(VD);
      continue;
    }
    for (const auto &F : Fixables) {
      std::optional<FixItList> Fixits = F->getFixits(S);

      if (Fixits) {
        FixItsForVariable[VD].insert(FixItsForVariable[VD].end(),
                                     Fixits->begin(), Fixits->end());
        continue;
      }
#ifndef NDEBUG
      Handler.addDebugNoteForVar(
          VD, F->getSourceLoc(),
          ("gadget '" + F->getDebugName() + "' refused to produce a fix")
              .str());
#endif
      FixItsForVariable.erase(VD);
      break;
    }
  }

  // `FixItsForVariable` now contains only variables that can be
  // fixed. A variable can be fixed if its' declaration and all Fixables
  // associated to it can all be fixed.

  // To further remove from `FixItsForVariable` variables whose group mates
  // cannot be fixed...
  eraseVarsForUnfixableGroupMates(FixItsForVariable, VarGrpMgr);
  // Now `FixItsForVariable` gets further reduced: a variable is in
  // `FixItsForVariable` iff it can be fixed and all its group mates can be
  // fixed.

  // Fix-its of bounds-safe overloads of `D` are shared by parameters of `D`.
  // That is,  when fixing multiple parameters in one step,  these fix-its will
  // be applied only once (instead of being applied per parameter).
  FixItList FixItsSharedByParms{};

  if (auto *FD = dyn_cast<FunctionDecl>(D))
    FixItsSharedByParms = createFunctionOverloadsForParms(
        FixItsForVariable, VarGrpMgr, FD, S, Ctx, Handler);

  // The map that maps each variable `v` to fix-its for the whole group where
  // `v` is in:
  std::map<const VarDecl *, FixItList> FinalFixItsForVariable{
      FixItsForVariable};

  for (auto &[Var, Ignore] : FixItsForVariable) {
    bool AnyParm = false;
    const auto VarGroupForVD = VarGrpMgr.getGroupOfVar(Var, &AnyParm);

    for (const VarDecl *GrpMate : VarGroupForVD) {
      if (Var == GrpMate)
        continue;
      if (FixItsForVariable.count(GrpMate))
        FinalFixItsForVariable[Var].append(FixItsForVariable[GrpMate]);
    }
    if (AnyParm) {
      // This assertion should never fail.  Otherwise we have a bug.
      assert(!FixItsSharedByParms.empty() &&
             "Should not try to fix a parameter that does not belong to a "
             "FunctionDecl");
      FinalFixItsForVariable[Var].append(FixItsSharedByParms);
    }
  }
  // Fix-its that will be applied in one step shall NOT:
  // 1. overlap with macros or/and templates; or
  // 2. conflict with each other.
  // Otherwise, the fix-its will be dropped.
  for (auto Iter = FinalFixItsForVariable.begin();
       Iter != FinalFixItsForVariable.end();)
    if (overlapWithMacro(Iter->second) ||
        clang::internal::anyConflict(Iter->second, Ctx.getSourceManager())) {
      Iter = FinalFixItsForVariable.erase(Iter);
    } else
      Iter++;
  return FinalFixItsForVariable;
}

template <typename VarDeclIterTy>
static FixitStrategy
getNaiveStrategy(llvm::iterator_range<VarDeclIterTy> UnsafeVars) {
  FixitStrategy S;
  for (const VarDecl *VD : UnsafeVars) {
    if (isa<ConstantArrayType>(VD->getType().getCanonicalType()))
      S.set(VD, FixitStrategy::Kind::Array);
    else
      S.set(VD, FixitStrategy::Kind::Span);
  }
  return S;
}

//  Manages variable groups:
class VariableGroupsManagerImpl : public VariableGroupsManager {
  const std::vector<VarGrpTy> Groups;
  const std::map<const VarDecl *, unsigned> &VarGrpMap;
  const llvm::SetVector<const VarDecl *> &GrpsUnionForParms;

public:
  VariableGroupsManagerImpl(
      const std::vector<VarGrpTy> &Groups,
      const std::map<const VarDecl *, unsigned> &VarGrpMap,
      const llvm::SetVector<const VarDecl *> &GrpsUnionForParms)
      : Groups(Groups), VarGrpMap(VarGrpMap),
        GrpsUnionForParms(GrpsUnionForParms) {}

  VarGrpRef getGroupOfVar(const VarDecl *Var, bool *HasParm) const override {
    if (GrpsUnionForParms.contains(Var)) {
      if (HasParm)
        *HasParm = true;
      return GrpsUnionForParms.getArrayRef();
    }
    if (HasParm)
      *HasParm = false;

    auto It = VarGrpMap.find(Var);

    if (It == VarGrpMap.end())
      return std::nullopt;
    return Groups[It->second];
  }

  VarGrpRef getGroupOfParms() const override {
    return GrpsUnionForParms.getArrayRef();
  }
};

void clang::checkUnsafeBufferUsage(const Decl *D,
                                   UnsafeBufferUsageHandler &Handler,
                                   bool EmitSuggestions) {
#ifndef NDEBUG
  Handler.clearDebugNotes();
#endif

  assert(D && D->getBody());
  // We do not want to visit a Lambda expression defined inside a method
  // independently. Instead, it should be visited along with the outer method.
  // FIXME: do we want to do the same thing for `BlockDecl`s?
  if (const auto *fd = dyn_cast<CXXMethodDecl>(D)) {
    if (fd->getParent()->isLambda() && fd->getParent()->isLocalClass())
      return;
  }

  // Do not emit fixit suggestions for functions declared in an
  // extern "C" block.
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    for (FunctionDecl *FReDecl : FD->redecls()) {
      if (FReDecl->isExternC()) {
        EmitSuggestions = false;
        break;
      }
    }
  }

  WarningGadgetSets UnsafeOps;
  FixableGadgetSets FixablesForAllVars;

  auto [FixableGadgets, WarningGadgets, Tracker] =
      findGadgets(D, Handler, EmitSuggestions);

  if (!EmitSuggestions) {
    // Our job is very easy without suggestions. Just warn about
    // every problematic operation and consider it done. No need to deal
    // with fixable gadgets, no need to group operations by variable.
    for (const auto &G : WarningGadgets) {
      G->handleUnsafeOperation(Handler, /*IsRelatedToDecl=*/false,
                               D->getASTContext());
    }

    // This return guarantees that most of the machine doesn't run when
    // suggestions aren't requested.
    assert(FixableGadgets.size() == 0 &&
           "Fixable gadgets found but suggestions not requested!");
    return;
  }

  // If no `WarningGadget`s ever matched, there is no unsafe operations in the
  //  function under the analysis. No need to fix any Fixables.
  if (!WarningGadgets.empty()) {
    // Gadgets "claim" variables they're responsible for. Once this loop
    // finishes, the tracker will only track DREs that weren't claimed by any
    // gadgets, i.e. not understood by the analysis.
    for (const auto &G : FixableGadgets) {
      for (const auto *DRE : G->getClaimedVarUseSites()) {
        Tracker.claimUse(DRE);
      }
    }
  }

  // If no `WarningGadget`s ever matched, there is no unsafe operations in the
  // function under the analysis.  Thus, it early returns here as there is
  // nothing needs to be fixed.
  //
  // Note this claim is based on the assumption that there is no unsafe
  // variable whose declaration is invisible from the analyzing function.
  // Otherwise, we need to consider if the uses of those unsafe varuables needs
  // fix.
  // So far, we are not fixing any global variables or class members. And,
  // lambdas will be analyzed along with the enclosing function. So this early
  // return is correct for now.
  if (WarningGadgets.empty())
    return;

  UnsafeOps = groupWarningGadgetsByVar(std::move(WarningGadgets));
  FixablesForAllVars = groupFixablesByVar(std::move(FixableGadgets));

  std::map<const VarDecl *, FixItList> FixItsForVariableGroup;

  // Filter out non-local vars and vars with unclaimed DeclRefExpr-s.
  for (auto it = FixablesForAllVars.byVar.cbegin();
       it != FixablesForAllVars.byVar.cend();) {
    // FIXME: need to deal with global variables later
    if ((!it->first->isLocalVarDecl() && !isa<ParmVarDecl>(it->first))) {
#ifndef NDEBUG
      Handler.addDebugNoteForVar(it->first, it->first->getBeginLoc(),
                                 ("failed to produce fixit for '" +
                                  it->first->getNameAsString() +
                                  "' : neither local nor a parameter"));
#endif
      it = FixablesForAllVars.byVar.erase(it);
    } else if (it->first->getType().getCanonicalType()->isReferenceType()) {
#ifndef NDEBUG
      Handler.addDebugNoteForVar(it->first, it->first->getBeginLoc(),
                                 ("failed to produce fixit for '" +
                                  it->first->getNameAsString() +
                                  "' : has a reference type"));
#endif
      it = FixablesForAllVars.byVar.erase(it);
    } else if (Tracker.hasUnclaimedUses(it->first)) {
      it = FixablesForAllVars.byVar.erase(it);
    } else if (it->first->isInitCapture()) {
#ifndef NDEBUG
      Handler.addDebugNoteForVar(it->first, it->first->getBeginLoc(),
                                 ("failed to produce fixit for '" +
                                  it->first->getNameAsString() +
                                  "' : init capture"));
#endif
      it = FixablesForAllVars.byVar.erase(it);
    } else {
      ++it;
    }
  }

#ifndef NDEBUG
  for (const auto &it : UnsafeOps.byVar) {
    const VarDecl *const UnsafeVD = it.first;
    auto UnclaimedDREs = Tracker.getUnclaimedUses(UnsafeVD);
    if (UnclaimedDREs.empty())
      continue;
    const auto UnfixedVDName = UnsafeVD->getNameAsString();
    for (const clang::DeclRefExpr *UnclaimedDRE : UnclaimedDREs) {
      std::string UnclaimedUseTrace =
          getDREAncestorString(UnclaimedDRE, D->getASTContext());

      Handler.addDebugNoteForVar(
          UnsafeVD, UnclaimedDRE->getBeginLoc(),
          ("failed to produce fixit for '" + UnfixedVDName +
           "' : has an unclaimed use\nThe unclaimed DRE trace: " +
           UnclaimedUseTrace));
    }
  }
#endif

  // Fixpoint iteration for pointer assignments
  using DepMapTy = DenseMap<const VarDecl *, llvm::SetVector<const VarDecl *>>;
  DepMapTy DependenciesMap{};
  DepMapTy PtrAssignmentGraph{};

  for (auto it : FixablesForAllVars.byVar) {
    for (const FixableGadget *fixable : it.second) {
      std::optional<std::pair<const VarDecl *, const VarDecl *>> ImplPair =
          fixable->getStrategyImplications();
      if (ImplPair) {
        std::pair<const VarDecl *, const VarDecl *> Impl = std::move(*ImplPair);
        PtrAssignmentGraph[Impl.first].insert(Impl.second);
      }
    }
  }

  /*
   The following code does a BFS traversal of the `PtrAssignmentGraph`
   considering all unsafe vars as starting nodes and constructs an undirected
   graph `DependenciesMap`. Constructing the `DependenciesMap` in this manner
   elimiates all variables that are unreachable from any unsafe var. In other
   words, this removes all dependencies that don't include any unsafe variable
   and consequently don't need any fixit generation.
   Note: A careful reader would observe that the code traverses
   `PtrAssignmentGraph` using `CurrentVar` but adds edges between `Var` and
   `Adj` and not between `CurrentVar` and `Adj`. Both approaches would
   achieve the same result but the one used here dramatically cuts the
   amount of hoops the second part of the algorithm needs to jump, given that
   a lot of these connections become "direct". The reader is advised not to
   imagine how the graph is transformed because of using `Var` instead of
   `CurrentVar`. The reader can continue reading as if `CurrentVar` was used,
   and think about why it's equivalent later.
   */
  std::set<const VarDecl *> VisitedVarsDirected{};
  for (const auto &[Var, ignore] : UnsafeOps.byVar) {
    if (VisitedVarsDirected.find(Var) == VisitedVarsDirected.end()) {

      std::queue<const VarDecl *> QueueDirected{};
      QueueDirected.push(Var);
      while (!QueueDirected.empty()) {
        const VarDecl *CurrentVar = QueueDirected.front();
        QueueDirected.pop();
        VisitedVarsDirected.insert(CurrentVar);
        auto AdjacentNodes = PtrAssignmentGraph[CurrentVar];
        for (const VarDecl *Adj : AdjacentNodes) {
          if (VisitedVarsDirected.find(Adj) == VisitedVarsDirected.end()) {
            QueueDirected.push(Adj);
          }
          DependenciesMap[Var].insert(Adj);
          DependenciesMap[Adj].insert(Var);
        }
      }
    }
  }

  // `Groups` stores the set of Connected Components in the graph.
  std::vector<VarGrpTy> Groups;
  // `VarGrpMap` maps variables that need fix to the groups (indexes) that the
  // variables belong to.  Group indexes refer to the elements in `Groups`.
  // `VarGrpMap` is complete in that every variable that needs fix is in it.
  std::map<const VarDecl *, unsigned> VarGrpMap;
  // The union group over the ones in "Groups" that contain parameters of `D`:
  llvm::SetVector<const VarDecl *>
      GrpsUnionForParms; // these variables need to be fixed in one step

  // Group Connected Components for Unsafe Vars
  // (Dependencies based on pointer assignments)
  std::set<const VarDecl *> VisitedVars{};
  for (const auto &[Var, ignore] : UnsafeOps.byVar) {
    if (VisitedVars.find(Var) == VisitedVars.end()) {
      VarGrpTy &VarGroup = Groups.emplace_back();
      std::queue<const VarDecl *> Queue{};

      Queue.push(Var);
      while (!Queue.empty()) {
        const VarDecl *CurrentVar = Queue.front();
        Queue.pop();
        VisitedVars.insert(CurrentVar);
        VarGroup.push_back(CurrentVar);
        auto AdjacentNodes = DependenciesMap[CurrentVar];
        for (const VarDecl *Adj : AdjacentNodes) {
          if (VisitedVars.find(Adj) == VisitedVars.end()) {
            Queue.push(Adj);
          }
        }
      }

      bool HasParm = false;
      unsigned GrpIdx = Groups.size() - 1;

      for (const VarDecl *V : VarGroup) {
        VarGrpMap[V] = GrpIdx;
        if (!HasParm && isParameterOf(V, D))
          HasParm = true;
      }
      if (HasParm)
        GrpsUnionForParms.insert(VarGroup.begin(), VarGroup.end());
    }
  }

  // Remove a `FixableGadget` if the associated variable is not in the graph
  // computed above.  We do not want to generate fix-its for such variables,
  // since they are neither warned nor reachable from a warned one.
  //
  // Note a variable is not warned if it is not directly used in any unsafe
  // operation. A variable `v` is NOT reachable from an unsafe variable, if it
  // does not exist another variable `u` such that `u` is warned and fixing `u`
  // (transitively) implicates fixing `v`.
  //
  // For example,
  // ```
  // void f(int * p) {
  //   int * a = p; *p = 0;
  // }
  // ```
  // `*p = 0` is a fixable gadget associated with a variable `p` that is neither
  // warned nor reachable from a warned one.  If we add `a[5] = 0` to the end of
  // the function above, `p` becomes reachable from a warned variable.
  for (auto I = FixablesForAllVars.byVar.begin();
       I != FixablesForAllVars.byVar.end();) {
    // Note `VisitedVars` contain all the variables in the graph:
    if (!VisitedVars.count((*I).first)) {
      // no such var in graph:
      I = FixablesForAllVars.byVar.erase(I);
    } else
      ++I;
  }

  // We assign strategies to variables that are 1) in the graph and 2) can be
  // fixed. Other variables have the default "Won't fix" strategy.
  FixitStrategy NaiveStrategy = getNaiveStrategy(llvm::make_filter_range(
      VisitedVars, [&FixablesForAllVars](const VarDecl *V) {
        // If a warned variable has no "Fixable", it is considered unfixable:
        return FixablesForAllVars.byVar.count(V);
      }));
  VariableGroupsManagerImpl VarGrpMgr(Groups, VarGrpMap, GrpsUnionForParms);

  if (isa<NamedDecl>(D))
    // The only case where `D` is not a `NamedDecl` is when `D` is a
    // `BlockDecl`. Let's not fix variables in blocks for now
    FixItsForVariableGroup =
        getFixIts(FixablesForAllVars, NaiveStrategy, D->getASTContext(), D,
                  Tracker, Handler, VarGrpMgr);

  for (const auto &G : UnsafeOps.noVar) {
    G->handleUnsafeOperation(Handler, /*IsRelatedToDecl=*/false,
                             D->getASTContext());
  }

  for (const auto &[VD, WarningGadgets] : UnsafeOps.byVar) {
    auto FixItsIt = FixItsForVariableGroup.find(VD);
    Handler.handleUnsafeVariableGroup(VD, VarGrpMgr,
                                      FixItsIt != FixItsForVariableGroup.end()
                                          ? std::move(FixItsIt->second)
                                          : FixItList{},
                                      D, NaiveStrategy);
    for (const auto &G : WarningGadgets) {
      G->handleUnsafeOperation(Handler, /*IsRelatedToDecl=*/true,
                               D->getASTContext());
    }
  }
}
