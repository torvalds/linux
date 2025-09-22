//===--- ASTMatchFinder.cpp - Structural query framework ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Implements an algorithm to efficiently search for matches on AST nodes.
//  Uses memoization to support recursive matches like HasDescendant.
//
//  The general idea is to visit all AST nodes with a RecursiveASTVisitor,
//  calling the Matches(...) method of each matcher we are running on each
//  AST node. The matcher can recurse via the ASTMatchFinder interface.
//
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Timer.h"
#include <deque>
#include <memory>
#include <set>

namespace clang {
namespace ast_matchers {
namespace internal {
namespace {

typedef MatchFinder::MatchCallback MatchCallback;

// The maximum number of memoization entries to store.
// 10k has been experimentally found to give a good trade-off
// of performance vs. memory consumption by running matcher
// that match on every statement over a very large codebase.
//
// FIXME: Do some performance optimization in general and
// revisit this number; also, put up micro-benchmarks that we can
// optimize this on.
static const unsigned MaxMemoizationEntries = 10000;

enum class MatchType {
  Ancestors,

  Descendants,
  Child,
};

// We use memoization to avoid running the same matcher on the same
// AST node twice.  This struct is the key for looking up match
// result.  It consists of an ID of the MatcherInterface (for
// identifying the matcher), a pointer to the AST node and the
// bound nodes before the matcher was executed.
//
// We currently only memoize on nodes whose pointers identify the
// nodes (\c Stmt and \c Decl, but not \c QualType or \c TypeLoc).
// For \c QualType and \c TypeLoc it is possible to implement
// generation of keys for each type.
// FIXME: Benchmark whether memoization of non-pointer typed nodes
// provides enough benefit for the additional amount of code.
struct MatchKey {
  DynTypedMatcher::MatcherIDType MatcherID;
  DynTypedNode Node;
  BoundNodesTreeBuilder BoundNodes;
  TraversalKind Traversal = TK_AsIs;
  MatchType Type;

  bool operator<(const MatchKey &Other) const {
    return std::tie(Traversal, Type, MatcherID, Node, BoundNodes) <
           std::tie(Other.Traversal, Other.Type, Other.MatcherID, Other.Node,
                    Other.BoundNodes);
  }
};

// Used to store the result of a match and possibly bound nodes.
struct MemoizedMatchResult {
  bool ResultOfMatch;
  BoundNodesTreeBuilder Nodes;
};

// A RecursiveASTVisitor that traverses all children or all descendants of
// a node.
class MatchChildASTVisitor
    : public RecursiveASTVisitor<MatchChildASTVisitor> {
public:
  typedef RecursiveASTVisitor<MatchChildASTVisitor> VisitorBase;

  // Creates an AST visitor that matches 'matcher' on all children or
  // descendants of a traversed node. max_depth is the maximum depth
  // to traverse: use 1 for matching the children and INT_MAX for
  // matching the descendants.
  MatchChildASTVisitor(const DynTypedMatcher *Matcher, ASTMatchFinder *Finder,
                       BoundNodesTreeBuilder *Builder, int MaxDepth,
                       bool IgnoreImplicitChildren,
                       ASTMatchFinder::BindKind Bind)
      : Matcher(Matcher), Finder(Finder), Builder(Builder), CurrentDepth(0),
        MaxDepth(MaxDepth), IgnoreImplicitChildren(IgnoreImplicitChildren),
        Bind(Bind), Matches(false) {}

  // Returns true if a match is found in the subtree rooted at the
  // given AST node. This is done via a set of mutually recursive
  // functions. Here's how the recursion is done (the  *wildcard can
  // actually be Decl, Stmt, or Type):
  //
  //   - Traverse(node) calls BaseTraverse(node) when it needs
  //     to visit the descendants of node.
  //   - BaseTraverse(node) then calls (via VisitorBase::Traverse*(node))
  //     Traverse*(c) for each child c of 'node'.
  //   - Traverse*(c) in turn calls Traverse(c), completing the
  //     recursion.
  bool findMatch(const DynTypedNode &DynNode) {
    reset();
    if (const Decl *D = DynNode.get<Decl>())
      traverse(*D);
    else if (const Stmt *S = DynNode.get<Stmt>())
      traverse(*S);
    else if (const NestedNameSpecifier *NNS =
             DynNode.get<NestedNameSpecifier>())
      traverse(*NNS);
    else if (const NestedNameSpecifierLoc *NNSLoc =
             DynNode.get<NestedNameSpecifierLoc>())
      traverse(*NNSLoc);
    else if (const QualType *Q = DynNode.get<QualType>())
      traverse(*Q);
    else if (const TypeLoc *T = DynNode.get<TypeLoc>())
      traverse(*T);
    else if (const auto *C = DynNode.get<CXXCtorInitializer>())
      traverse(*C);
    else if (const TemplateArgumentLoc *TALoc =
                 DynNode.get<TemplateArgumentLoc>())
      traverse(*TALoc);
    else if (const Attr *A = DynNode.get<Attr>())
      traverse(*A);
    // FIXME: Add other base types after adding tests.

    // It's OK to always overwrite the bound nodes, as if there was
    // no match in this recursive branch, the result set is empty
    // anyway.
    *Builder = ResultBindings;

    return Matches;
  }

  // The following are overriding methods from the base visitor class.
  // They are public only to allow CRTP to work. They are *not *part
  // of the public API of this class.
  bool TraverseDecl(Decl *DeclNode) {

    if (DeclNode && DeclNode->isImplicit() &&
        Finder->isTraversalIgnoringImplicitNodes())
      return baseTraverse(*DeclNode);

    ScopedIncrement ScopedDepth(&CurrentDepth);
    return (DeclNode == nullptr) || traverse(*DeclNode);
  }

  Stmt *getStmtToTraverse(Stmt *StmtNode) {
    Stmt *StmtToTraverse = StmtNode;
    if (auto *ExprNode = dyn_cast_or_null<Expr>(StmtNode)) {
      auto *LambdaNode = dyn_cast_or_null<LambdaExpr>(StmtNode);
      if (LambdaNode && Finder->isTraversalIgnoringImplicitNodes())
        StmtToTraverse = LambdaNode;
      else
        StmtToTraverse =
            Finder->getASTContext().getParentMapContext().traverseIgnored(
                ExprNode);
    }
    return StmtToTraverse;
  }

  bool TraverseStmt(Stmt *StmtNode, DataRecursionQueue *Queue = nullptr) {
    // If we need to keep track of the depth, we can't perform data recursion.
    if (CurrentDepth == 0 || (CurrentDepth <= MaxDepth && MaxDepth < INT_MAX))
      Queue = nullptr;

    ScopedIncrement ScopedDepth(&CurrentDepth);
    Stmt *StmtToTraverse = getStmtToTraverse(StmtNode);
    if (!StmtToTraverse)
      return true;

    if (IgnoreImplicitChildren && isa<CXXDefaultArgExpr>(StmtNode))
      return true;

    if (!match(*StmtToTraverse))
      return false;
    return VisitorBase::TraverseStmt(StmtToTraverse, Queue);
  }
  // We assume that the QualType and the contained type are on the same
  // hierarchy level. Thus, we try to match either of them.
  bool TraverseType(QualType TypeNode) {
    if (TypeNode.isNull())
      return true;
    ScopedIncrement ScopedDepth(&CurrentDepth);
    // Match the Type.
    if (!match(*TypeNode))
      return false;
    // The QualType is matched inside traverse.
    return traverse(TypeNode);
  }
  // We assume that the TypeLoc, contained QualType and contained Type all are
  // on the same hierarchy level. Thus, we try to match all of them.
  bool TraverseTypeLoc(TypeLoc TypeLocNode) {
    if (TypeLocNode.isNull())
      return true;
    ScopedIncrement ScopedDepth(&CurrentDepth);
    // Match the Type.
    if (!match(*TypeLocNode.getType()))
      return false;
    // Match the QualType.
    if (!match(TypeLocNode.getType()))
      return false;
    // The TypeLoc is matched inside traverse.
    return traverse(TypeLocNode);
  }
  bool TraverseNestedNameSpecifier(NestedNameSpecifier *NNS) {
    ScopedIncrement ScopedDepth(&CurrentDepth);
    return (NNS == nullptr) || traverse(*NNS);
  }
  bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS) {
    if (!NNS)
      return true;
    ScopedIncrement ScopedDepth(&CurrentDepth);
    if (!match(*NNS.getNestedNameSpecifier()))
      return false;
    return traverse(NNS);
  }
  bool TraverseConstructorInitializer(CXXCtorInitializer *CtorInit) {
    if (!CtorInit)
      return true;
    ScopedIncrement ScopedDepth(&CurrentDepth);
    return traverse(*CtorInit);
  }
  bool TraverseTemplateArgumentLoc(TemplateArgumentLoc TAL) {
    ScopedIncrement ScopedDepth(&CurrentDepth);
    return traverse(TAL);
  }
  bool TraverseCXXForRangeStmt(CXXForRangeStmt *Node) {
    if (!Finder->isTraversalIgnoringImplicitNodes())
      return VisitorBase::TraverseCXXForRangeStmt(Node);
    if (!Node)
      return true;
    ScopedIncrement ScopedDepth(&CurrentDepth);
    if (auto *Init = Node->getInit())
      if (!traverse(*Init))
        return false;
    if (!match(*Node->getLoopVariable()))
      return false;
    if (match(*Node->getRangeInit()))
      if (!VisitorBase::TraverseStmt(Node->getRangeInit()))
        return false;
    if (!match(*Node->getBody()))
      return false;
    return VisitorBase::TraverseStmt(Node->getBody());
  }
  bool TraverseCXXRewrittenBinaryOperator(CXXRewrittenBinaryOperator *Node) {
    if (!Finder->isTraversalIgnoringImplicitNodes())
      return VisitorBase::TraverseCXXRewrittenBinaryOperator(Node);
    if (!Node)
      return true;
    ScopedIncrement ScopedDepth(&CurrentDepth);

    return match(*Node->getLHS()) && match(*Node->getRHS());
  }
  bool TraverseAttr(Attr *A) {
    if (A == nullptr ||
        (A->isImplicit() &&
         Finder->getASTContext().getParentMapContext().getTraversalKind() ==
             TK_IgnoreUnlessSpelledInSource))
      return true;
    ScopedIncrement ScopedDepth(&CurrentDepth);
    return traverse(*A);
  }
  bool TraverseLambdaExpr(LambdaExpr *Node) {
    if (!Finder->isTraversalIgnoringImplicitNodes())
      return VisitorBase::TraverseLambdaExpr(Node);
    if (!Node)
      return true;
    ScopedIncrement ScopedDepth(&CurrentDepth);

    for (unsigned I = 0, N = Node->capture_size(); I != N; ++I) {
      const auto *C = Node->capture_begin() + I;
      if (!C->isExplicit())
        continue;
      if (Node->isInitCapture(C) && !match(*C->getCapturedVar()))
        return false;
      if (!match(*Node->capture_init_begin()[I]))
        return false;
    }

    if (const auto *TPL = Node->getTemplateParameterList()) {
      for (const auto *TP : *TPL) {
        if (!match(*TP))
          return false;
      }
    }

    for (const auto *P : Node->getCallOperator()->parameters()) {
      if (!match(*P))
        return false;
    }

    if (!match(*Node->getBody()))
      return false;

    return VisitorBase::TraverseStmt(Node->getBody());
  }

  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return !IgnoreImplicitChildren; }

private:
  // Used for updating the depth during traversal.
  struct ScopedIncrement {
    explicit ScopedIncrement(int *Depth) : Depth(Depth) { ++(*Depth); }
    ~ScopedIncrement() { --(*Depth); }

   private:
    int *Depth;
  };

  // Resets the state of this object.
  void reset() {
    Matches = false;
    CurrentDepth = 0;
  }

  // Forwards the call to the corresponding Traverse*() method in the
  // base visitor class.
  bool baseTraverse(const Decl &DeclNode) {
    return VisitorBase::TraverseDecl(const_cast<Decl*>(&DeclNode));
  }
  bool baseTraverse(const Stmt &StmtNode) {
    return VisitorBase::TraverseStmt(const_cast<Stmt*>(&StmtNode));
  }
  bool baseTraverse(QualType TypeNode) {
    return VisitorBase::TraverseType(TypeNode);
  }
  bool baseTraverse(TypeLoc TypeLocNode) {
    return VisitorBase::TraverseTypeLoc(TypeLocNode);
  }
  bool baseTraverse(const NestedNameSpecifier &NNS) {
    return VisitorBase::TraverseNestedNameSpecifier(
        const_cast<NestedNameSpecifier*>(&NNS));
  }
  bool baseTraverse(NestedNameSpecifierLoc NNS) {
    return VisitorBase::TraverseNestedNameSpecifierLoc(NNS);
  }
  bool baseTraverse(const CXXCtorInitializer &CtorInit) {
    return VisitorBase::TraverseConstructorInitializer(
        const_cast<CXXCtorInitializer *>(&CtorInit));
  }
  bool baseTraverse(TemplateArgumentLoc TAL) {
    return VisitorBase::TraverseTemplateArgumentLoc(TAL);
  }
  bool baseTraverse(const Attr &AttrNode) {
    return VisitorBase::TraverseAttr(const_cast<Attr *>(&AttrNode));
  }

  // Sets 'Matched' to true if 'Matcher' matches 'Node' and:
  //   0 < CurrentDepth <= MaxDepth.
  //
  // Returns 'true' if traversal should continue after this function
  // returns, i.e. if no match is found or 'Bind' is 'BK_All'.
  template <typename T>
  bool match(const T &Node) {
    if (CurrentDepth == 0 || CurrentDepth > MaxDepth) {
      return true;
    }
    if (Bind != ASTMatchFinder::BK_All) {
      BoundNodesTreeBuilder RecursiveBuilder(*Builder);
      if (Matcher->matches(DynTypedNode::create(Node), Finder,
                           &RecursiveBuilder)) {
        Matches = true;
        ResultBindings.addMatch(RecursiveBuilder);
        return false; // Abort as soon as a match is found.
      }
    } else {
      BoundNodesTreeBuilder RecursiveBuilder(*Builder);
      if (Matcher->matches(DynTypedNode::create(Node), Finder,
                           &RecursiveBuilder)) {
        // After the first match the matcher succeeds.
        Matches = true;
        ResultBindings.addMatch(RecursiveBuilder);
      }
    }
    return true;
  }

  // Traverses the subtree rooted at 'Node'; returns true if the
  // traversal should continue after this function returns.
  template <typename T>
  bool traverse(const T &Node) {
    static_assert(IsBaseType<T>::value,
                  "traverse can only be instantiated with base type");
    if (!match(Node))
      return false;
    return baseTraverse(Node);
  }

  const DynTypedMatcher *const Matcher;
  ASTMatchFinder *const Finder;
  BoundNodesTreeBuilder *const Builder;
  BoundNodesTreeBuilder ResultBindings;
  int CurrentDepth;
  const int MaxDepth;
  const bool IgnoreImplicitChildren;
  const ASTMatchFinder::BindKind Bind;
  bool Matches;
};

// Controls the outermost traversal of the AST and allows to match multiple
// matchers.
class MatchASTVisitor : public RecursiveASTVisitor<MatchASTVisitor>,
                        public ASTMatchFinder {
public:
  MatchASTVisitor(const MatchFinder::MatchersByType *Matchers,
                  const MatchFinder::MatchFinderOptions &Options)
      : Matchers(Matchers), Options(Options), ActiveASTContext(nullptr) {}

  ~MatchASTVisitor() override {
    if (Options.CheckProfiling) {
      Options.CheckProfiling->Records = std::move(TimeByBucket);
    }
  }

  void onStartOfTranslationUnit() {
    const bool EnableCheckProfiling = Options.CheckProfiling.has_value();
    TimeBucketRegion Timer;
    for (MatchCallback *MC : Matchers->AllCallbacks) {
      if (EnableCheckProfiling)
        Timer.setBucket(&TimeByBucket[MC->getID()]);
      MC->onStartOfTranslationUnit();
    }
  }

  void onEndOfTranslationUnit() {
    const bool EnableCheckProfiling = Options.CheckProfiling.has_value();
    TimeBucketRegion Timer;
    for (MatchCallback *MC : Matchers->AllCallbacks) {
      if (EnableCheckProfiling)
        Timer.setBucket(&TimeByBucket[MC->getID()]);
      MC->onEndOfTranslationUnit();
    }
  }

  void set_active_ast_context(ASTContext *NewActiveASTContext) {
    ActiveASTContext = NewActiveASTContext;
  }

  // The following Visit*() and Traverse*() functions "override"
  // methods in RecursiveASTVisitor.

  bool VisitTypedefNameDecl(TypedefNameDecl *DeclNode) {
    // When we see 'typedef A B', we add name 'B' to the set of names
    // A's canonical type maps to.  This is necessary for implementing
    // isDerivedFrom(x) properly, where x can be the name of the base
    // class or any of its aliases.
    //
    // In general, the is-alias-of (as defined by typedefs) relation
    // is tree-shaped, as you can typedef a type more than once.  For
    // example,
    //
    //   typedef A B;
    //   typedef A C;
    //   typedef C D;
    //   typedef C E;
    //
    // gives you
    //
    //   A
    //   |- B
    //   `- C
    //      |- D
    //      `- E
    //
    // It is wrong to assume that the relation is a chain.  A correct
    // implementation of isDerivedFrom() needs to recognize that B and
    // E are aliases, even though neither is a typedef of the other.
    // Therefore, we cannot simply walk through one typedef chain to
    // find out whether the type name matches.
    const Type *TypeNode = DeclNode->getUnderlyingType().getTypePtr();
    const Type *CanonicalType =  // root of the typedef tree
        ActiveASTContext->getCanonicalType(TypeNode);
    TypeAliases[CanonicalType].insert(DeclNode);
    return true;
  }

  bool VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *CAD) {
    const ObjCInterfaceDecl *InterfaceDecl = CAD->getClassInterface();
    CompatibleAliases[InterfaceDecl].insert(CAD);
    return true;
  }

  bool TraverseDecl(Decl *DeclNode);
  bool TraverseStmt(Stmt *StmtNode, DataRecursionQueue *Queue = nullptr);
  bool TraverseType(QualType TypeNode);
  bool TraverseTypeLoc(TypeLoc TypeNode);
  bool TraverseNestedNameSpecifier(NestedNameSpecifier *NNS);
  bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS);
  bool TraverseConstructorInitializer(CXXCtorInitializer *CtorInit);
  bool TraverseTemplateArgumentLoc(TemplateArgumentLoc TAL);
  bool TraverseAttr(Attr *AttrNode);

  bool dataTraverseNode(Stmt *S, DataRecursionQueue *Queue) {
    if (auto *RF = dyn_cast<CXXForRangeStmt>(S)) {
      {
        ASTNodeNotAsIsSourceScope RAII(this, true);
        TraverseStmt(RF->getInit());
        // Don't traverse under the loop variable
        match(*RF->getLoopVariable());
        TraverseStmt(RF->getRangeInit());
      }
      {
        ASTNodeNotSpelledInSourceScope RAII(this, true);
        for (auto *SubStmt : RF->children()) {
          if (SubStmt != RF->getBody())
            TraverseStmt(SubStmt);
        }
      }
      TraverseStmt(RF->getBody());
      return true;
    } else if (auto *RBO = dyn_cast<CXXRewrittenBinaryOperator>(S)) {
      {
        ASTNodeNotAsIsSourceScope RAII(this, true);
        TraverseStmt(const_cast<Expr *>(RBO->getLHS()));
        TraverseStmt(const_cast<Expr *>(RBO->getRHS()));
      }
      {
        ASTNodeNotSpelledInSourceScope RAII(this, true);
        for (auto *SubStmt : RBO->children()) {
          TraverseStmt(SubStmt);
        }
      }
      return true;
    } else if (auto *LE = dyn_cast<LambdaExpr>(S)) {
      for (auto I : llvm::zip(LE->captures(), LE->capture_inits())) {
        auto C = std::get<0>(I);
        ASTNodeNotSpelledInSourceScope RAII(
            this, TraversingASTNodeNotSpelledInSource || !C.isExplicit());
        TraverseLambdaCapture(LE, &C, std::get<1>(I));
      }

      {
        ASTNodeNotSpelledInSourceScope RAII(this, true);
        TraverseDecl(LE->getLambdaClass());
      }
      {
        ASTNodeNotAsIsSourceScope RAII(this, true);

        // We need to poke around to find the bits that might be explicitly
        // written.
        TypeLoc TL = LE->getCallOperator()->getTypeSourceInfo()->getTypeLoc();
        FunctionProtoTypeLoc Proto = TL.getAsAdjusted<FunctionProtoTypeLoc>();

        if (auto *TPL = LE->getTemplateParameterList()) {
          for (NamedDecl *D : *TPL) {
            TraverseDecl(D);
          }
          if (Expr *RequiresClause = TPL->getRequiresClause()) {
            TraverseStmt(RequiresClause);
          }
        }

        if (LE->hasExplicitParameters()) {
          // Visit parameters.
          for (ParmVarDecl *Param : Proto.getParams())
            TraverseDecl(Param);
        }

        const auto *T = Proto.getTypePtr();
        for (const auto &E : T->exceptions())
          TraverseType(E);

        if (Expr *NE = T->getNoexceptExpr())
          TraverseStmt(NE, Queue);

        if (LE->hasExplicitResultType())
          TraverseTypeLoc(Proto.getReturnLoc());
        TraverseStmt(LE->getTrailingRequiresClause());
      }

      TraverseStmt(LE->getBody());
      return true;
    }
    return RecursiveASTVisitor<MatchASTVisitor>::dataTraverseNode(S, Queue);
  }

  // Matches children or descendants of 'Node' with 'BaseMatcher'.
  bool memoizedMatchesRecursively(const DynTypedNode &Node, ASTContext &Ctx,
                                  const DynTypedMatcher &Matcher,
                                  BoundNodesTreeBuilder *Builder, int MaxDepth,
                                  BindKind Bind) {
    // For AST-nodes that don't have an identity, we can't memoize.
    if (!Node.getMemoizationData() || !Builder->isComparable())
      return matchesRecursively(Node, Matcher, Builder, MaxDepth, Bind);

    MatchKey Key;
    Key.MatcherID = Matcher.getID();
    Key.Node = Node;
    // Note that we key on the bindings *before* the match.
    Key.BoundNodes = *Builder;
    Key.Traversal = Ctx.getParentMapContext().getTraversalKind();
    // Memoize result even doing a single-level match, it might be expensive.
    Key.Type = MaxDepth == 1 ? MatchType::Child : MatchType::Descendants;
    MemoizationMap::iterator I = ResultCache.find(Key);
    if (I != ResultCache.end()) {
      *Builder = I->second.Nodes;
      return I->second.ResultOfMatch;
    }

    MemoizedMatchResult Result;
    Result.Nodes = *Builder;
    Result.ResultOfMatch =
        matchesRecursively(Node, Matcher, &Result.Nodes, MaxDepth, Bind);

    MemoizedMatchResult &CachedResult = ResultCache[Key];
    CachedResult = std::move(Result);

    *Builder = CachedResult.Nodes;
    return CachedResult.ResultOfMatch;
  }

  // Matches children or descendants of 'Node' with 'BaseMatcher'.
  bool matchesRecursively(const DynTypedNode &Node,
                          const DynTypedMatcher &Matcher,
                          BoundNodesTreeBuilder *Builder, int MaxDepth,
                          BindKind Bind) {
    bool ScopedTraversal = TraversingASTNodeNotSpelledInSource ||
                           TraversingASTChildrenNotSpelledInSource;

    bool IgnoreImplicitChildren = false;

    if (isTraversalIgnoringImplicitNodes()) {
      IgnoreImplicitChildren = true;
    }

    ASTNodeNotSpelledInSourceScope RAII(this, ScopedTraversal);

    MatchChildASTVisitor Visitor(&Matcher, this, Builder, MaxDepth,
                                 IgnoreImplicitChildren, Bind);
    return Visitor.findMatch(Node);
  }

  bool classIsDerivedFrom(const CXXRecordDecl *Declaration,
                          const Matcher<NamedDecl> &Base,
                          BoundNodesTreeBuilder *Builder,
                          bool Directly) override;

private:
  bool
  classIsDerivedFromImpl(const CXXRecordDecl *Declaration,
                         const Matcher<NamedDecl> &Base,
                         BoundNodesTreeBuilder *Builder, bool Directly,
                         llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Visited);

public:
  bool objcClassIsDerivedFrom(const ObjCInterfaceDecl *Declaration,
                              const Matcher<NamedDecl> &Base,
                              BoundNodesTreeBuilder *Builder,
                              bool Directly) override;

public:
  // Implements ASTMatchFinder::matchesChildOf.
  bool matchesChildOf(const DynTypedNode &Node, ASTContext &Ctx,
                      const DynTypedMatcher &Matcher,
                      BoundNodesTreeBuilder *Builder, BindKind Bind) override {
    if (ResultCache.size() > MaxMemoizationEntries)
      ResultCache.clear();
    return memoizedMatchesRecursively(Node, Ctx, Matcher, Builder, 1, Bind);
  }
  // Implements ASTMatchFinder::matchesDescendantOf.
  bool matchesDescendantOf(const DynTypedNode &Node, ASTContext &Ctx,
                           const DynTypedMatcher &Matcher,
                           BoundNodesTreeBuilder *Builder,
                           BindKind Bind) override {
    if (ResultCache.size() > MaxMemoizationEntries)
      ResultCache.clear();
    return memoizedMatchesRecursively(Node, Ctx, Matcher, Builder, INT_MAX,
                                      Bind);
  }
  // Implements ASTMatchFinder::matchesAncestorOf.
  bool matchesAncestorOf(const DynTypedNode &Node, ASTContext &Ctx,
                         const DynTypedMatcher &Matcher,
                         BoundNodesTreeBuilder *Builder,
                         AncestorMatchMode MatchMode) override {
    // Reset the cache outside of the recursive call to make sure we
    // don't invalidate any iterators.
    if (ResultCache.size() > MaxMemoizationEntries)
      ResultCache.clear();
    if (MatchMode == AncestorMatchMode::AMM_ParentOnly)
      return matchesParentOf(Node, Matcher, Builder);
    return matchesAnyAncestorOf(Node, Ctx, Matcher, Builder);
  }

  // Matches all registered matchers on the given node and calls the
  // result callback for every node that matches.
  void match(const DynTypedNode &Node) {
    // FIXME: Improve this with a switch or a visitor pattern.
    if (auto *N = Node.get<Decl>()) {
      match(*N);
    } else if (auto *N = Node.get<Stmt>()) {
      match(*N);
    } else if (auto *N = Node.get<Type>()) {
      match(*N);
    } else if (auto *N = Node.get<QualType>()) {
      match(*N);
    } else if (auto *N = Node.get<NestedNameSpecifier>()) {
      match(*N);
    } else if (auto *N = Node.get<NestedNameSpecifierLoc>()) {
      match(*N);
    } else if (auto *N = Node.get<TypeLoc>()) {
      match(*N);
    } else if (auto *N = Node.get<CXXCtorInitializer>()) {
      match(*N);
    } else if (auto *N = Node.get<TemplateArgumentLoc>()) {
      match(*N);
    } else if (auto *N = Node.get<Attr>()) {
      match(*N);
    }
  }

  template <typename T> void match(const T &Node) {
    matchDispatch(&Node);
  }

  // Implements ASTMatchFinder::getASTContext.
  ASTContext &getASTContext() const override { return *ActiveASTContext; }

  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }

  // We visit the lambda body explicitly, so instruct the RAV
  // to not visit it on our behalf too.
  bool shouldVisitLambdaBody() const { return false; }

  bool IsMatchingInASTNodeNotSpelledInSource() const override {
    return TraversingASTNodeNotSpelledInSource;
  }
  bool isMatchingChildrenNotSpelledInSource() const override {
    return TraversingASTChildrenNotSpelledInSource;
  }
  void setMatchingChildrenNotSpelledInSource(bool Set) override {
    TraversingASTChildrenNotSpelledInSource = Set;
  }

  bool IsMatchingInASTNodeNotAsIs() const override {
    return TraversingASTNodeNotAsIs;
  }

  bool TraverseTemplateInstantiations(ClassTemplateDecl *D) {
    ASTNodeNotSpelledInSourceScope RAII(this, true);
    return RecursiveASTVisitor<MatchASTVisitor>::TraverseTemplateInstantiations(
        D);
  }

  bool TraverseTemplateInstantiations(VarTemplateDecl *D) {
    ASTNodeNotSpelledInSourceScope RAII(this, true);
    return RecursiveASTVisitor<MatchASTVisitor>::TraverseTemplateInstantiations(
        D);
  }

  bool TraverseTemplateInstantiations(FunctionTemplateDecl *D) {
    ASTNodeNotSpelledInSourceScope RAII(this, true);
    return RecursiveASTVisitor<MatchASTVisitor>::TraverseTemplateInstantiations(
        D);
  }

private:
  bool TraversingASTNodeNotSpelledInSource = false;
  bool TraversingASTNodeNotAsIs = false;
  bool TraversingASTChildrenNotSpelledInSource = false;

  class CurMatchData {
// We don't have enough free low bits in 32bit builds to discriminate 8 pointer
// types in PointerUnion. so split the union in 2 using a free bit from the
// callback pointer.
#define CMD_TYPES_0                                                            \
  const QualType *, const TypeLoc *, const NestedNameSpecifier *,              \
      const NestedNameSpecifierLoc *
#define CMD_TYPES_1                                                            \
  const CXXCtorInitializer *, const TemplateArgumentLoc *, const Attr *,       \
      const DynTypedNode *

#define IMPL(Index)                                                            \
  template <typename NodeType>                                                 \
  std::enable_if_t<                                                            \
      llvm::is_one_of<const NodeType *, CMD_TYPES_##Index>::value>             \
  SetCallbackAndRawNode(const MatchCallback *CB, const NodeType &N) {          \
    assertEmpty();                                                             \
    Callback.setPointerAndInt(CB, Index);                                      \
    Node##Index = &N;                                                          \
  }                                                                            \
                                                                               \
  template <typename T>                                                        \
  std::enable_if_t<llvm::is_one_of<const T *, CMD_TYPES_##Index>::value,       \
                   const T *>                                                  \
  getNode() const {                                                            \
    assertHoldsState();                                                        \
    return Callback.getInt() == (Index) ? Node##Index.dyn_cast<const T *>()    \
                                        : nullptr;                             \
  }

  public:
    CurMatchData() : Node0(nullptr) {}

    IMPL(0)
    IMPL(1)

    const MatchCallback *getCallback() const { return Callback.getPointer(); }

    void SetBoundNodes(const BoundNodes &BN) {
      assertHoldsState();
      BNodes = &BN;
    }

    void clearBoundNodes() {
      assertHoldsState();
      BNodes = nullptr;
    }

    const BoundNodes *getBoundNodes() const {
      assertHoldsState();
      return BNodes;
    }

    void reset() {
      assertHoldsState();
      Callback.setPointerAndInt(nullptr, 0);
      Node0 = nullptr;
    }

  private:
    void assertHoldsState() const {
      assert(Callback.getPointer() != nullptr && !Node0.isNull());
    }

    void assertEmpty() const {
      assert(Callback.getPointer() == nullptr && Node0.isNull() &&
             BNodes == nullptr);
    }

    llvm::PointerIntPair<const MatchCallback *, 1> Callback;
    union {
      llvm::PointerUnion<CMD_TYPES_0> Node0;
      llvm::PointerUnion<CMD_TYPES_1> Node1;
    };
    const BoundNodes *BNodes = nullptr;

#undef CMD_TYPES_0
#undef CMD_TYPES_1
#undef IMPL
  } CurMatchState;

  struct CurMatchRAII {
    template <typename NodeType>
    CurMatchRAII(MatchASTVisitor &MV, const MatchCallback *CB,
                 const NodeType &NT)
        : MV(MV) {
      MV.CurMatchState.SetCallbackAndRawNode(CB, NT);
    }

    ~CurMatchRAII() { MV.CurMatchState.reset(); }

  private:
    MatchASTVisitor &MV;
  };

public:
  class TraceReporter : llvm::PrettyStackTraceEntry {
    static void dumpNode(const ASTContext &Ctx, const DynTypedNode &Node,
                         raw_ostream &OS) {
      if (const auto *D = Node.get<Decl>()) {
        OS << D->getDeclKindName() << "Decl ";
        if (const auto *ND = dyn_cast<NamedDecl>(D)) {
          ND->printQualifiedName(OS);
          OS << " : ";
        } else
          OS << ": ";
        D->getSourceRange().print(OS, Ctx.getSourceManager());
      } else if (const auto *S = Node.get<Stmt>()) {
        OS << S->getStmtClassName() << " : ";
        S->getSourceRange().print(OS, Ctx.getSourceManager());
      } else if (const auto *T = Node.get<Type>()) {
        OS << T->getTypeClassName() << "Type : ";
        QualType(T, 0).print(OS, Ctx.getPrintingPolicy());
      } else if (const auto *QT = Node.get<QualType>()) {
        OS << "QualType : ";
        QT->print(OS, Ctx.getPrintingPolicy());
      } else {
        OS << Node.getNodeKind().asStringRef() << " : ";
        Node.getSourceRange().print(OS, Ctx.getSourceManager());
      }
    }

    static void dumpNodeFromState(const ASTContext &Ctx,
                                  const CurMatchData &State, raw_ostream &OS) {
      if (const DynTypedNode *MatchNode = State.getNode<DynTypedNode>()) {
        dumpNode(Ctx, *MatchNode, OS);
      } else if (const auto *QT = State.getNode<QualType>()) {
        dumpNode(Ctx, DynTypedNode::create(*QT), OS);
      } else if (const auto *TL = State.getNode<TypeLoc>()) {
        dumpNode(Ctx, DynTypedNode::create(*TL), OS);
      } else if (const auto *NNS = State.getNode<NestedNameSpecifier>()) {
        dumpNode(Ctx, DynTypedNode::create(*NNS), OS);
      } else if (const auto *NNSL = State.getNode<NestedNameSpecifierLoc>()) {
        dumpNode(Ctx, DynTypedNode::create(*NNSL), OS);
      } else if (const auto *CtorInit = State.getNode<CXXCtorInitializer>()) {
        dumpNode(Ctx, DynTypedNode::create(*CtorInit), OS);
      } else if (const auto *TAL = State.getNode<TemplateArgumentLoc>()) {
        dumpNode(Ctx, DynTypedNode::create(*TAL), OS);
      } else if (const auto *At = State.getNode<Attr>()) {
        dumpNode(Ctx, DynTypedNode::create(*At), OS);
      }
    }

  public:
    TraceReporter(const MatchASTVisitor &MV) : MV(MV) {}
    void print(raw_ostream &OS) const override {
      const CurMatchData &State = MV.CurMatchState;
      const MatchCallback *CB = State.getCallback();
      if (!CB) {
        OS << "ASTMatcher: Not currently matching\n";
        return;
      }

      assert(MV.ActiveASTContext &&
             "ActiveASTContext should be set if there is a matched callback");

      ASTContext &Ctx = MV.getASTContext();

      if (const BoundNodes *Nodes = State.getBoundNodes()) {
        OS << "ASTMatcher: Processing '" << CB->getID() << "' against:\n\t";
        dumpNodeFromState(Ctx, State, OS);
        const BoundNodes::IDToNodeMap &Map = Nodes->getMap();
        if (Map.empty()) {
          OS << "\nNo bound nodes\n";
          return;
        }
        OS << "\n--- Bound Nodes Begin ---\n";
        for (const auto &Item : Map) {
          OS << "    " << Item.first << " - { ";
          dumpNode(Ctx, Item.second, OS);
          OS << " }\n";
        }
        OS << "--- Bound Nodes End ---\n";
      } else {
        OS << "ASTMatcher: Matching '" << CB->getID() << "' against:\n\t";
        dumpNodeFromState(Ctx, State, OS);
        OS << '\n';
      }
    }

  private:
    const MatchASTVisitor &MV;
  };

private:
  struct ASTNodeNotSpelledInSourceScope {
    ASTNodeNotSpelledInSourceScope(MatchASTVisitor *V, bool B)
        : MV(V), MB(V->TraversingASTNodeNotSpelledInSource) {
      V->TraversingASTNodeNotSpelledInSource = B;
    }
    ~ASTNodeNotSpelledInSourceScope() {
      MV->TraversingASTNodeNotSpelledInSource = MB;
    }

  private:
    MatchASTVisitor *MV;
    bool MB;
  };

  struct ASTNodeNotAsIsSourceScope {
    ASTNodeNotAsIsSourceScope(MatchASTVisitor *V, bool B)
        : MV(V), MB(V->TraversingASTNodeNotAsIs) {
      V->TraversingASTNodeNotAsIs = B;
    }
    ~ASTNodeNotAsIsSourceScope() { MV->TraversingASTNodeNotAsIs = MB; }

  private:
    MatchASTVisitor *MV;
    bool MB;
  };

  class TimeBucketRegion {
  public:
    TimeBucketRegion() = default;
    ~TimeBucketRegion() { setBucket(nullptr); }

    /// Start timing for \p NewBucket.
    ///
    /// If there was a bucket already set, it will finish the timing for that
    /// other bucket.
    /// \p NewBucket will be timed until the next call to \c setBucket() or
    /// until the \c TimeBucketRegion is destroyed.
    /// If \p NewBucket is the same as the currently timed bucket, this call
    /// does nothing.
    void setBucket(llvm::TimeRecord *NewBucket) {
      if (Bucket != NewBucket) {
        auto Now = llvm::TimeRecord::getCurrentTime(true);
        if (Bucket)
          *Bucket += Now;
        if (NewBucket)
          *NewBucket -= Now;
        Bucket = NewBucket;
      }
    }

  private:
    llvm::TimeRecord *Bucket = nullptr;
  };

  /// Runs all the \p Matchers on \p Node.
  ///
  /// Used by \c matchDispatch() below.
  template <typename T, typename MC>
  void matchWithoutFilter(const T &Node, const MC &Matchers) {
    const bool EnableCheckProfiling = Options.CheckProfiling.has_value();
    TimeBucketRegion Timer;
    for (const auto &MP : Matchers) {
      if (EnableCheckProfiling)
        Timer.setBucket(&TimeByBucket[MP.second->getID()]);
      BoundNodesTreeBuilder Builder;
      CurMatchRAII RAII(*this, MP.second, Node);
      if (MP.first.matches(Node, this, &Builder)) {
        MatchVisitor Visitor(*this, ActiveASTContext, MP.second);
        Builder.visitMatches(&Visitor);
      }
    }
  }

  void matchWithFilter(const DynTypedNode &DynNode) {
    auto Kind = DynNode.getNodeKind();
    auto it = MatcherFiltersMap.find(Kind);
    const auto &Filter =
        it != MatcherFiltersMap.end() ? it->second : getFilterForKind(Kind);

    if (Filter.empty())
      return;

    const bool EnableCheckProfiling = Options.CheckProfiling.has_value();
    TimeBucketRegion Timer;
    auto &Matchers = this->Matchers->DeclOrStmt;
    for (unsigned short I : Filter) {
      auto &MP = Matchers[I];
      if (EnableCheckProfiling)
        Timer.setBucket(&TimeByBucket[MP.second->getID()]);
      BoundNodesTreeBuilder Builder;

      {
        TraversalKindScope RAII(getASTContext(), MP.first.getTraversalKind());
        if (getASTContext().getParentMapContext().traverseIgnored(DynNode) !=
            DynNode)
          continue;
      }

      CurMatchRAII RAII(*this, MP.second, DynNode);
      if (MP.first.matches(DynNode, this, &Builder)) {
        MatchVisitor Visitor(*this, ActiveASTContext, MP.second);
        Builder.visitMatches(&Visitor);
      }
    }
  }

  const std::vector<unsigned short> &getFilterForKind(ASTNodeKind Kind) {
    auto &Filter = MatcherFiltersMap[Kind];
    auto &Matchers = this->Matchers->DeclOrStmt;
    assert((Matchers.size() < USHRT_MAX) && "Too many matchers.");
    for (unsigned I = 0, E = Matchers.size(); I != E; ++I) {
      if (Matchers[I].first.canMatchNodesOfKind(Kind)) {
        Filter.push_back(I);
      }
    }
    return Filter;
  }

  /// @{
  /// Overloads to pair the different node types to their matchers.
  void matchDispatch(const Decl *Node) {
    return matchWithFilter(DynTypedNode::create(*Node));
  }
  void matchDispatch(const Stmt *Node) {
    return matchWithFilter(DynTypedNode::create(*Node));
  }

  void matchDispatch(const Type *Node) {
    matchWithoutFilter(QualType(Node, 0), Matchers->Type);
  }
  void matchDispatch(const TypeLoc *Node) {
    matchWithoutFilter(*Node, Matchers->TypeLoc);
  }
  void matchDispatch(const QualType *Node) {
    matchWithoutFilter(*Node, Matchers->Type);
  }
  void matchDispatch(const NestedNameSpecifier *Node) {
    matchWithoutFilter(*Node, Matchers->NestedNameSpecifier);
  }
  void matchDispatch(const NestedNameSpecifierLoc *Node) {
    matchWithoutFilter(*Node, Matchers->NestedNameSpecifierLoc);
  }
  void matchDispatch(const CXXCtorInitializer *Node) {
    matchWithoutFilter(*Node, Matchers->CtorInit);
  }
  void matchDispatch(const TemplateArgumentLoc *Node) {
    matchWithoutFilter(*Node, Matchers->TemplateArgumentLoc);
  }
  void matchDispatch(const Attr *Node) {
    matchWithoutFilter(*Node, Matchers->Attr);
  }
  void matchDispatch(const void *) { /* Do nothing. */ }
  /// @}

  // Returns whether a direct parent of \p Node matches \p Matcher.
  // Unlike matchesAnyAncestorOf there's no memoization: it doesn't save much.
  bool matchesParentOf(const DynTypedNode &Node, const DynTypedMatcher &Matcher,
                       BoundNodesTreeBuilder *Builder) {
    for (const auto &Parent : ActiveASTContext->getParents(Node)) {
      BoundNodesTreeBuilder BuilderCopy = *Builder;
      if (Matcher.matches(Parent, this, &BuilderCopy)) {
        *Builder = std::move(BuilderCopy);
        return true;
      }
    }
    return false;
  }

  // Returns whether an ancestor of \p Node matches \p Matcher.
  //
  // The order of matching (which can lead to different nodes being bound in
  // case there are multiple matches) is breadth first search.
  //
  // To allow memoization in the very common case of having deeply nested
  // expressions inside a template function, we first walk up the AST, memoizing
  // the result of the match along the way, as long as there is only a single
  // parent.
  //
  // Once there are multiple parents, the breadth first search order does not
  // allow simple memoization on the ancestors. Thus, we only memoize as long
  // as there is a single parent.
  //
  // We avoid a recursive implementation to prevent excessive stack use on
  // very deep ASTs (similarly to RecursiveASTVisitor's data recursion).
  bool matchesAnyAncestorOf(DynTypedNode Node, ASTContext &Ctx,
                            const DynTypedMatcher &Matcher,
                            BoundNodesTreeBuilder *Builder) {

    // Memoization keys that can be updated with the result.
    // These are the memoizable nodes in the chain of unique parents, which
    // terminates when a node has multiple parents, or matches, or is the root.
    std::vector<MatchKey> Keys;
    // When returning, update the memoization cache.
    auto Finish = [&](bool Matched) {
      for (const auto &Key : Keys) {
        MemoizedMatchResult &CachedResult = ResultCache[Key];
        CachedResult.ResultOfMatch = Matched;
        CachedResult.Nodes = *Builder;
      }
      return Matched;
    };

    // Loop while there's a single parent and we want to attempt memoization.
    DynTypedNodeList Parents{ArrayRef<DynTypedNode>()}; // after loop: size != 1
    for (;;) {
      // A cache key only makes sense if memoization is possible.
      if (Builder->isComparable()) {
        Keys.emplace_back();
        Keys.back().MatcherID = Matcher.getID();
        Keys.back().Node = Node;
        Keys.back().BoundNodes = *Builder;
        Keys.back().Traversal = Ctx.getParentMapContext().getTraversalKind();
        Keys.back().Type = MatchType::Ancestors;

        // Check the cache.
        MemoizationMap::iterator I = ResultCache.find(Keys.back());
        if (I != ResultCache.end()) {
          Keys.pop_back(); // Don't populate the cache for the matching node!
          *Builder = I->second.Nodes;
          return Finish(I->second.ResultOfMatch);
        }
      }

      Parents = ActiveASTContext->getParents(Node);
      // Either no parents or multiple parents: leave chain+memoize mode and
      // enter bfs+forgetful mode.
      if (Parents.size() != 1)
        break;

      // Check the next parent.
      Node = *Parents.begin();
      BoundNodesTreeBuilder BuilderCopy = *Builder;
      if (Matcher.matches(Node, this, &BuilderCopy)) {
        *Builder = std::move(BuilderCopy);
        return Finish(true);
      }
    }
    // We reached the end of the chain.

    if (Parents.empty()) {
      // Nodes may have no parents if:
      //  a) the node is the TranslationUnitDecl
      //  b) we have a limited traversal scope that excludes the parent edges
      //  c) there is a bug in the AST, and the node is not reachable
      // Usually the traversal scope is the whole AST, which precludes b.
      // Bugs are common enough that it's worthwhile asserting when we can.
#ifndef NDEBUG
      if (!Node.get<TranslationUnitDecl>() &&
          /* Traversal scope is full AST if any of the bounds are the TU */
          llvm::any_of(ActiveASTContext->getTraversalScope(), [](Decl *D) {
            return D->getKind() == Decl::TranslationUnit;
          })) {
        llvm::errs() << "Tried to match orphan node:\n";
        Node.dump(llvm::errs(), *ActiveASTContext);
        llvm_unreachable("Parent map should be complete!");
      }
#endif
    } else {
      assert(Parents.size() > 1);
      // BFS starting from the parents not yet considered.
      // Memoization of newly visited nodes is not possible (but we still update
      // results for the elements in the chain we found above).
      std::deque<DynTypedNode> Queue(Parents.begin(), Parents.end());
      llvm::DenseSet<const void *> Visited;
      while (!Queue.empty()) {
        BoundNodesTreeBuilder BuilderCopy = *Builder;
        if (Matcher.matches(Queue.front(), this, &BuilderCopy)) {
          *Builder = std::move(BuilderCopy);
          return Finish(true);
        }
        for (const auto &Parent : ActiveASTContext->getParents(Queue.front())) {
          // Make sure we do not visit the same node twice.
          // Otherwise, we'll visit the common ancestors as often as there
          // are splits on the way down.
          if (Visited.insert(Parent.getMemoizationData()).second)
            Queue.push_back(Parent);
        }
        Queue.pop_front();
      }
    }
    return Finish(false);
  }

  // Implements a BoundNodesTree::Visitor that calls a MatchCallback with
  // the aggregated bound nodes for each match.
  class MatchVisitor : public BoundNodesTreeBuilder::Visitor {
    struct CurBoundScope {
      CurBoundScope(MatchASTVisitor::CurMatchData &State, const BoundNodes &BN)
          : State(State) {
        State.SetBoundNodes(BN);
      }

      ~CurBoundScope() { State.clearBoundNodes(); }

    private:
      MatchASTVisitor::CurMatchData &State;
    };

  public:
    MatchVisitor(MatchASTVisitor &MV, ASTContext *Context,
                 MatchFinder::MatchCallback *Callback)
        : State(MV.CurMatchState), Context(Context), Callback(Callback) {}

    void visitMatch(const BoundNodes& BoundNodesView) override {
      TraversalKindScope RAII(*Context, Callback->getCheckTraversalKind());
      CurBoundScope RAII2(State, BoundNodesView);
      Callback->run(MatchFinder::MatchResult(BoundNodesView, Context));
    }

  private:
    MatchASTVisitor::CurMatchData &State;
    ASTContext* Context;
    MatchFinder::MatchCallback* Callback;
  };

  // Returns true if 'TypeNode' has an alias that matches the given matcher.
  bool typeHasMatchingAlias(const Type *TypeNode,
                            const Matcher<NamedDecl> &Matcher,
                            BoundNodesTreeBuilder *Builder) {
    const Type *const CanonicalType =
      ActiveASTContext->getCanonicalType(TypeNode);
    auto Aliases = TypeAliases.find(CanonicalType);
    if (Aliases == TypeAliases.end())
      return false;
    for (const TypedefNameDecl *Alias : Aliases->second) {
      BoundNodesTreeBuilder Result(*Builder);
      if (Matcher.matches(*Alias, this, &Result)) {
        *Builder = std::move(Result);
        return true;
      }
    }
    return false;
  }

  bool
  objcClassHasMatchingCompatibilityAlias(const ObjCInterfaceDecl *InterfaceDecl,
                                         const Matcher<NamedDecl> &Matcher,
                                         BoundNodesTreeBuilder *Builder) {
    auto Aliases = CompatibleAliases.find(InterfaceDecl);
    if (Aliases == CompatibleAliases.end())
      return false;
    for (const ObjCCompatibleAliasDecl *Alias : Aliases->second) {
      BoundNodesTreeBuilder Result(*Builder);
      if (Matcher.matches(*Alias, this, &Result)) {
        *Builder = std::move(Result);
        return true;
      }
    }
    return false;
  }

  /// Bucket to record map.
  ///
  /// Used to get the appropriate bucket for each matcher.
  llvm::StringMap<llvm::TimeRecord> TimeByBucket;

  const MatchFinder::MatchersByType *Matchers;

  /// Filtered list of matcher indices for each matcher kind.
  ///
  /// \c Decl and \c Stmt toplevel matchers usually apply to a specific node
  /// kind (and derived kinds) so it is a waste to try every matcher on every
  /// node.
  /// We precalculate a list of matchers that pass the toplevel restrict check.
  llvm::DenseMap<ASTNodeKind, std::vector<unsigned short>> MatcherFiltersMap;

  const MatchFinder::MatchFinderOptions &Options;
  ASTContext *ActiveASTContext;

  // Maps a canonical type to its TypedefDecls.
  llvm::DenseMap<const Type*, std::set<const TypedefNameDecl*> > TypeAliases;

  // Maps an Objective-C interface to its ObjCCompatibleAliasDecls.
  llvm::DenseMap<const ObjCInterfaceDecl *,
                 llvm::SmallPtrSet<const ObjCCompatibleAliasDecl *, 2>>
      CompatibleAliases;

  // Maps (matcher, node) -> the match result for memoization.
  typedef std::map<MatchKey, MemoizedMatchResult> MemoizationMap;
  MemoizationMap ResultCache;
};

static CXXRecordDecl *
getAsCXXRecordDeclOrPrimaryTemplate(const Type *TypeNode) {
  if (auto *RD = TypeNode->getAsCXXRecordDecl())
    return RD;

  // Find the innermost TemplateSpecializationType that isn't an alias template.
  auto *TemplateType = TypeNode->getAs<TemplateSpecializationType>();
  while (TemplateType && TemplateType->isTypeAlias())
    TemplateType =
        TemplateType->getAliasedType()->getAs<TemplateSpecializationType>();

  // If this is the name of a (dependent) template specialization, use the
  // definition of the template, even though it might be specialized later.
  if (TemplateType)
    if (auto *ClassTemplate = dyn_cast_or_null<ClassTemplateDecl>(
          TemplateType->getTemplateName().getAsTemplateDecl()))
      return ClassTemplate->getTemplatedDecl();

  return nullptr;
}

// Returns true if the given C++ class is directly or indirectly derived
// from a base type with the given name.  A class is not considered to be
// derived from itself.
bool MatchASTVisitor::classIsDerivedFrom(const CXXRecordDecl *Declaration,
                                         const Matcher<NamedDecl> &Base,
                                         BoundNodesTreeBuilder *Builder,
                                         bool Directly) {
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> Visited;
  return classIsDerivedFromImpl(Declaration, Base, Builder, Directly, Visited);
}

bool MatchASTVisitor::classIsDerivedFromImpl(
    const CXXRecordDecl *Declaration, const Matcher<NamedDecl> &Base,
    BoundNodesTreeBuilder *Builder, bool Directly,
    llvm::SmallPtrSetImpl<const CXXRecordDecl *> &Visited) {
  if (!Declaration->hasDefinition())
    return false;
  if (!Visited.insert(Declaration).second)
    return false;
  for (const auto &It : Declaration->bases()) {
    const Type *TypeNode = It.getType().getTypePtr();

    if (typeHasMatchingAlias(TypeNode, Base, Builder))
      return true;

    // FIXME: Going to the primary template here isn't really correct, but
    // unfortunately we accept a Decl matcher for the base class not a Type
    // matcher, so it's the best thing we can do with our current interface.
    CXXRecordDecl *ClassDecl = getAsCXXRecordDeclOrPrimaryTemplate(TypeNode);
    if (!ClassDecl)
      continue;
    if (ClassDecl == Declaration) {
      // This can happen for recursive template definitions.
      continue;
    }
    BoundNodesTreeBuilder Result(*Builder);
    if (Base.matches(*ClassDecl, this, &Result)) {
      *Builder = std::move(Result);
      return true;
    }
    if (!Directly &&
        classIsDerivedFromImpl(ClassDecl, Base, Builder, Directly, Visited))
      return true;
  }
  return false;
}

// Returns true if the given Objective-C class is directly or indirectly
// derived from a matching base class. A class is not considered to be derived
// from itself.
bool MatchASTVisitor::objcClassIsDerivedFrom(
    const ObjCInterfaceDecl *Declaration, const Matcher<NamedDecl> &Base,
    BoundNodesTreeBuilder *Builder, bool Directly) {
  // Check if any of the superclasses of the class match.
  for (const ObjCInterfaceDecl *ClassDecl = Declaration->getSuperClass();
       ClassDecl != nullptr; ClassDecl = ClassDecl->getSuperClass()) {
    // Check if there are any matching compatibility aliases.
    if (objcClassHasMatchingCompatibilityAlias(ClassDecl, Base, Builder))
      return true;

    // Check if there are any matching type aliases.
    const Type *TypeNode = ClassDecl->getTypeForDecl();
    if (typeHasMatchingAlias(TypeNode, Base, Builder))
      return true;

    if (Base.matches(*ClassDecl, this, Builder))
      return true;

    // Not `return false` as a temporary workaround for PR43879.
    if (Directly)
      break;
  }

  return false;
}

bool MatchASTVisitor::TraverseDecl(Decl *DeclNode) {
  if (!DeclNode) {
    return true;
  }

  bool ScopedTraversal =
      TraversingASTNodeNotSpelledInSource || DeclNode->isImplicit();
  bool ScopedChildren = TraversingASTChildrenNotSpelledInSource;

  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(DeclNode)) {
    auto SK = CTSD->getSpecializationKind();
    if (SK == TSK_ExplicitInstantiationDeclaration ||
        SK == TSK_ExplicitInstantiationDefinition)
      ScopedChildren = true;
  } else if (const auto *FD = dyn_cast<FunctionDecl>(DeclNode)) {
    if (FD->isDefaulted())
      ScopedChildren = true;
    if (FD->isTemplateInstantiation())
      ScopedTraversal = true;
  } else if (isa<BindingDecl>(DeclNode)) {
    ScopedChildren = true;
  }

  ASTNodeNotSpelledInSourceScope RAII1(this, ScopedTraversal);
  ASTChildrenNotSpelledInSourceScope RAII2(this, ScopedChildren);

  match(*DeclNode);
  return RecursiveASTVisitor<MatchASTVisitor>::TraverseDecl(DeclNode);
}

bool MatchASTVisitor::TraverseStmt(Stmt *StmtNode, DataRecursionQueue *Queue) {
  if (!StmtNode) {
    return true;
  }
  bool ScopedTraversal = TraversingASTNodeNotSpelledInSource ||
                         TraversingASTChildrenNotSpelledInSource;

  ASTNodeNotSpelledInSourceScope RAII(this, ScopedTraversal);
  match(*StmtNode);
  return RecursiveASTVisitor<MatchASTVisitor>::TraverseStmt(StmtNode, Queue);
}

bool MatchASTVisitor::TraverseType(QualType TypeNode) {
  match(TypeNode);
  return RecursiveASTVisitor<MatchASTVisitor>::TraverseType(TypeNode);
}

bool MatchASTVisitor::TraverseTypeLoc(TypeLoc TypeLocNode) {
  // The RecursiveASTVisitor only visits types if they're not within TypeLocs.
  // We still want to find those types via matchers, so we match them here. Note
  // that the TypeLocs are structurally a shadow-hierarchy to the expressed
  // type, so we visit all involved parts of a compound type when matching on
  // each TypeLoc.
  match(TypeLocNode);
  match(TypeLocNode.getType());
  return RecursiveASTVisitor<MatchASTVisitor>::TraverseTypeLoc(TypeLocNode);
}

bool MatchASTVisitor::TraverseNestedNameSpecifier(NestedNameSpecifier *NNS) {
  match(*NNS);
  return RecursiveASTVisitor<MatchASTVisitor>::TraverseNestedNameSpecifier(NNS);
}

bool MatchASTVisitor::TraverseNestedNameSpecifierLoc(
    NestedNameSpecifierLoc NNS) {
  if (!NNS)
    return true;

  match(NNS);

  // We only match the nested name specifier here (as opposed to traversing it)
  // because the traversal is already done in the parallel "Loc"-hierarchy.
  if (NNS.hasQualifier())
    match(*NNS.getNestedNameSpecifier());
  return
      RecursiveASTVisitor<MatchASTVisitor>::TraverseNestedNameSpecifierLoc(NNS);
}

bool MatchASTVisitor::TraverseConstructorInitializer(
    CXXCtorInitializer *CtorInit) {
  if (!CtorInit)
    return true;

  bool ScopedTraversal = TraversingASTNodeNotSpelledInSource ||
                         TraversingASTChildrenNotSpelledInSource;

  if (!CtorInit->isWritten())
    ScopedTraversal = true;

  ASTNodeNotSpelledInSourceScope RAII1(this, ScopedTraversal);

  match(*CtorInit);

  return RecursiveASTVisitor<MatchASTVisitor>::TraverseConstructorInitializer(
      CtorInit);
}

bool MatchASTVisitor::TraverseTemplateArgumentLoc(TemplateArgumentLoc Loc) {
  match(Loc);
  return RecursiveASTVisitor<MatchASTVisitor>::TraverseTemplateArgumentLoc(Loc);
}

bool MatchASTVisitor::TraverseAttr(Attr *AttrNode) {
  match(*AttrNode);
  return RecursiveASTVisitor<MatchASTVisitor>::TraverseAttr(AttrNode);
}

class MatchASTConsumer : public ASTConsumer {
public:
  MatchASTConsumer(MatchFinder *Finder,
                   MatchFinder::ParsingDoneTestCallback *ParsingDone)
      : Finder(Finder), ParsingDone(ParsingDone) {}

private:
  void HandleTranslationUnit(ASTContext &Context) override {
    if (ParsingDone != nullptr) {
      ParsingDone->run();
    }
    Finder->matchAST(Context);
  }

  MatchFinder *Finder;
  MatchFinder::ParsingDoneTestCallback *ParsingDone;
};

} // end namespace
} // end namespace internal

MatchFinder::MatchResult::MatchResult(const BoundNodes &Nodes,
                                      ASTContext *Context)
  : Nodes(Nodes), Context(Context),
    SourceManager(&Context->getSourceManager()) {}

MatchFinder::MatchCallback::~MatchCallback() {}
MatchFinder::ParsingDoneTestCallback::~ParsingDoneTestCallback() {}

MatchFinder::MatchFinder(MatchFinderOptions Options)
    : Options(std::move(Options)), ParsingDone(nullptr) {}

MatchFinder::~MatchFinder() {}

void MatchFinder::addMatcher(const DeclarationMatcher &NodeMatch,
                             MatchCallback *Action) {
  std::optional<TraversalKind> TK;
  if (Action)
    TK = Action->getCheckTraversalKind();
  if (TK)
    Matchers.DeclOrStmt.emplace_back(traverse(*TK, NodeMatch), Action);
  else
    Matchers.DeclOrStmt.emplace_back(NodeMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

void MatchFinder::addMatcher(const TypeMatcher &NodeMatch,
                             MatchCallback *Action) {
  Matchers.Type.emplace_back(NodeMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

void MatchFinder::addMatcher(const StatementMatcher &NodeMatch,
                             MatchCallback *Action) {
  std::optional<TraversalKind> TK;
  if (Action)
    TK = Action->getCheckTraversalKind();
  if (TK)
    Matchers.DeclOrStmt.emplace_back(traverse(*TK, NodeMatch), Action);
  else
    Matchers.DeclOrStmt.emplace_back(NodeMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

void MatchFinder::addMatcher(const NestedNameSpecifierMatcher &NodeMatch,
                             MatchCallback *Action) {
  Matchers.NestedNameSpecifier.emplace_back(NodeMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

void MatchFinder::addMatcher(const NestedNameSpecifierLocMatcher &NodeMatch,
                             MatchCallback *Action) {
  Matchers.NestedNameSpecifierLoc.emplace_back(NodeMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

void MatchFinder::addMatcher(const TypeLocMatcher &NodeMatch,
                             MatchCallback *Action) {
  Matchers.TypeLoc.emplace_back(NodeMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

void MatchFinder::addMatcher(const CXXCtorInitializerMatcher &NodeMatch,
                             MatchCallback *Action) {
  Matchers.CtorInit.emplace_back(NodeMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

void MatchFinder::addMatcher(const TemplateArgumentLocMatcher &NodeMatch,
                             MatchCallback *Action) {
  Matchers.TemplateArgumentLoc.emplace_back(NodeMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

void MatchFinder::addMatcher(const AttrMatcher &AttrMatch,
                             MatchCallback *Action) {
  Matchers.Attr.emplace_back(AttrMatch, Action);
  Matchers.AllCallbacks.insert(Action);
}

bool MatchFinder::addDynamicMatcher(const internal::DynTypedMatcher &NodeMatch,
                                    MatchCallback *Action) {
  if (NodeMatch.canConvertTo<Decl>()) {
    addMatcher(NodeMatch.convertTo<Decl>(), Action);
    return true;
  } else if (NodeMatch.canConvertTo<QualType>()) {
    addMatcher(NodeMatch.convertTo<QualType>(), Action);
    return true;
  } else if (NodeMatch.canConvertTo<Stmt>()) {
    addMatcher(NodeMatch.convertTo<Stmt>(), Action);
    return true;
  } else if (NodeMatch.canConvertTo<NestedNameSpecifier>()) {
    addMatcher(NodeMatch.convertTo<NestedNameSpecifier>(), Action);
    return true;
  } else if (NodeMatch.canConvertTo<NestedNameSpecifierLoc>()) {
    addMatcher(NodeMatch.convertTo<NestedNameSpecifierLoc>(), Action);
    return true;
  } else if (NodeMatch.canConvertTo<TypeLoc>()) {
    addMatcher(NodeMatch.convertTo<TypeLoc>(), Action);
    return true;
  } else if (NodeMatch.canConvertTo<CXXCtorInitializer>()) {
    addMatcher(NodeMatch.convertTo<CXXCtorInitializer>(), Action);
    return true;
  } else if (NodeMatch.canConvertTo<TemplateArgumentLoc>()) {
    addMatcher(NodeMatch.convertTo<TemplateArgumentLoc>(), Action);
    return true;
  } else if (NodeMatch.canConvertTo<Attr>()) {
    addMatcher(NodeMatch.convertTo<Attr>(), Action);
    return true;
  }
  return false;
}

std::unique_ptr<ASTConsumer> MatchFinder::newASTConsumer() {
  return std::make_unique<internal::MatchASTConsumer>(this, ParsingDone);
}

void MatchFinder::match(const clang::DynTypedNode &Node, ASTContext &Context) {
  internal::MatchASTVisitor Visitor(&Matchers, Options);
  Visitor.set_active_ast_context(&Context);
  Visitor.match(Node);
}

void MatchFinder::matchAST(ASTContext &Context) {
  internal::MatchASTVisitor Visitor(&Matchers, Options);
  internal::MatchASTVisitor::TraceReporter StackTrace(Visitor);
  Visitor.set_active_ast_context(&Context);
  Visitor.onStartOfTranslationUnit();
  Visitor.TraverseAST(Context);
  Visitor.onEndOfTranslationUnit();
}

void MatchFinder::registerTestCallbackAfterParsing(
    MatchFinder::ParsingDoneTestCallback *NewParsingDone) {
  ParsingDone = NewParsingDone;
}

StringRef MatchFinder::MatchCallback::getID() const { return "<unknown>"; }

std::optional<TraversalKind>
MatchFinder::MatchCallback::getCheckTraversalKind() const {
  return std::nullopt;
}

} // end namespace ast_matchers
} // end namespace clang
