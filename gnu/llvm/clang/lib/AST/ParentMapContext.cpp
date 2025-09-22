//===- ParentMapContext.cpp - Map of parents using DynTypedNode -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Similar to ParentMap.cpp, but generalizes to non-Stmt nodes, which can have
// multiple parents.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/TemplateBase.h"

using namespace clang;

ParentMapContext::ParentMapContext(ASTContext &Ctx) : ASTCtx(Ctx) {}

ParentMapContext::~ParentMapContext() = default;

void ParentMapContext::clear() { Parents.reset(); }

const Expr *ParentMapContext::traverseIgnored(const Expr *E) const {
  return traverseIgnored(const_cast<Expr *>(E));
}

Expr *ParentMapContext::traverseIgnored(Expr *E) const {
  if (!E)
    return nullptr;

  switch (Traversal) {
  case TK_AsIs:
    return E;
  case TK_IgnoreUnlessSpelledInSource:
    return E->IgnoreUnlessSpelledInSource();
  }
  llvm_unreachable("Invalid Traversal type!");
}

DynTypedNode ParentMapContext::traverseIgnored(const DynTypedNode &N) const {
  if (const auto *E = N.get<Expr>()) {
    return DynTypedNode::create(*traverseIgnored(E));
  }
  return N;
}

template <typename T, typename... U>
std::tuple<bool, DynTypedNodeList, const T *, const U *...>
matchParents(const DynTypedNodeList &NodeList,
             ParentMapContext::ParentMap *ParentMap);

template <typename, typename...> struct MatchParents;

class ParentMapContext::ParentMap {

  template <typename, typename...> friend struct ::MatchParents;

  /// Contains parents of a node.
  class ParentVector {
  public:
    ParentVector() = default;
    explicit ParentVector(size_t N, const DynTypedNode &Value) {
      Items.reserve(N);
      for (; N > 0; --N)
        push_back(Value);
    }
    bool contains(const DynTypedNode &Value) {
      return Seen.contains(Value);
    }
    void push_back(const DynTypedNode &Value) {
      if (!Value.getMemoizationData() || Seen.insert(Value).second)
        Items.push_back(Value);
    }
    llvm::ArrayRef<DynTypedNode> view() const { return Items; }
  private:
    llvm::SmallVector<DynTypedNode, 2> Items;
    llvm::SmallDenseSet<DynTypedNode, 2> Seen;
  };

  /// Maps from a node to its parents. This is used for nodes that have
  /// pointer identity only, which are more common and we can save space by
  /// only storing a unique pointer to them.
  using ParentMapPointers =
      llvm::DenseMap<const void *,
                     llvm::PointerUnion<const Decl *, const Stmt *,
                                        DynTypedNode *, ParentVector *>>;

  /// Parent map for nodes without pointer identity. We store a full
  /// DynTypedNode for all keys.
  using ParentMapOtherNodes =
      llvm::DenseMap<DynTypedNode,
                     llvm::PointerUnion<const Decl *, const Stmt *,
                                        DynTypedNode *, ParentVector *>>;

  ParentMapPointers PointerParents;
  ParentMapOtherNodes OtherParents;
  class ASTVisitor;

  static DynTypedNode
  getSingleDynTypedNodeFromParentMap(ParentMapPointers::mapped_type U) {
    if (const auto *D = U.dyn_cast<const Decl *>())
      return DynTypedNode::create(*D);
    if (const auto *S = U.dyn_cast<const Stmt *>())
      return DynTypedNode::create(*S);
    return *U.get<DynTypedNode *>();
  }

  template <typename NodeTy, typename MapTy>
  static DynTypedNodeList getDynNodeFromMap(const NodeTy &Node,
                                                        const MapTy &Map) {
    auto I = Map.find(Node);
    if (I == Map.end()) {
      return llvm::ArrayRef<DynTypedNode>();
    }
    if (const auto *V = I->second.template dyn_cast<ParentVector *>()) {
      return V->view();
    }
    return getSingleDynTypedNodeFromParentMap(I->second);
  }

public:
  ParentMap(ASTContext &Ctx);
  ~ParentMap() {
    for (const auto &Entry : PointerParents) {
      if (Entry.second.is<DynTypedNode *>()) {
        delete Entry.second.get<DynTypedNode *>();
      } else if (Entry.second.is<ParentVector *>()) {
        delete Entry.second.get<ParentVector *>();
      }
    }
    for (const auto &Entry : OtherParents) {
      if (Entry.second.is<DynTypedNode *>()) {
        delete Entry.second.get<DynTypedNode *>();
      } else if (Entry.second.is<ParentVector *>()) {
        delete Entry.second.get<ParentVector *>();
      }
    }
  }

  DynTypedNodeList getParents(TraversalKind TK, const DynTypedNode &Node) {
    if (Node.getNodeKind().hasPointerIdentity()) {
      auto ParentList =
          getDynNodeFromMap(Node.getMemoizationData(), PointerParents);
      if (ParentList.size() > 0 && TK == TK_IgnoreUnlessSpelledInSource) {

        const auto *ChildExpr = Node.get<Expr>();

        {
          // Don't match explicit node types because different stdlib
          // implementations implement this in different ways and have
          // different intermediate nodes.
          // Look up 4 levels for a cxxRewrittenBinaryOperator as that is
          // enough for the major stdlib implementations.
          auto RewrittenBinOpParentsList = ParentList;
          int I = 0;
          while (ChildExpr && RewrittenBinOpParentsList.size() == 1 &&
                 I++ < 4) {
            const auto *S = RewrittenBinOpParentsList[0].get<Stmt>();
            if (!S)
              break;

            const auto *RWBO = dyn_cast<CXXRewrittenBinaryOperator>(S);
            if (!RWBO) {
              RewrittenBinOpParentsList = getDynNodeFromMap(S, PointerParents);
              continue;
            }
            if (RWBO->getLHS()->IgnoreUnlessSpelledInSource() != ChildExpr &&
                RWBO->getRHS()->IgnoreUnlessSpelledInSource() != ChildExpr)
              break;
            return DynTypedNode::create(*RWBO);
          }
        }

        const auto *ParentExpr = ParentList[0].get<Expr>();
        if (ParentExpr && ChildExpr)
          return AscendIgnoreUnlessSpelledInSource(ParentExpr, ChildExpr);

        {
          auto AncestorNodes =
              matchParents<DeclStmt, CXXForRangeStmt>(ParentList, this);
          if (std::get<bool>(AncestorNodes) &&
              std::get<const CXXForRangeStmt *>(AncestorNodes)
                      ->getLoopVarStmt() ==
                  std::get<const DeclStmt *>(AncestorNodes))
            return std::get<DynTypedNodeList>(AncestorNodes);
        }
        {
          auto AncestorNodes = matchParents<VarDecl, DeclStmt, CXXForRangeStmt>(
              ParentList, this);
          if (std::get<bool>(AncestorNodes) &&
              std::get<const CXXForRangeStmt *>(AncestorNodes)
                      ->getRangeStmt() ==
                  std::get<const DeclStmt *>(AncestorNodes))
            return std::get<DynTypedNodeList>(AncestorNodes);
        }
        {
          auto AncestorNodes =
              matchParents<CXXMethodDecl, CXXRecordDecl, LambdaExpr>(ParentList,
                                                                     this);
          if (std::get<bool>(AncestorNodes))
            return std::get<DynTypedNodeList>(AncestorNodes);
        }
        {
          auto AncestorNodes =
              matchParents<FunctionTemplateDecl, CXXRecordDecl, LambdaExpr>(
                  ParentList, this);
          if (std::get<bool>(AncestorNodes))
            return std::get<DynTypedNodeList>(AncestorNodes);
        }
      }
      return ParentList;
    }
    return getDynNodeFromMap(Node, OtherParents);
  }

  DynTypedNodeList AscendIgnoreUnlessSpelledInSource(const Expr *E,
                                                     const Expr *Child) {

    auto ShouldSkip = [](const Expr *E, const Expr *Child) {
      if (isa<ImplicitCastExpr>(E))
        return true;

      if (isa<FullExpr>(E))
        return true;

      if (isa<MaterializeTemporaryExpr>(E))
        return true;

      if (isa<CXXBindTemporaryExpr>(E))
        return true;

      if (isa<ParenExpr>(E))
        return true;

      if (isa<ExprWithCleanups>(E))
        return true;

      auto SR = Child->getSourceRange();

      if (const auto *C = dyn_cast<CXXFunctionalCastExpr>(E)) {
        if (C->getSourceRange() == SR)
          return true;
      }

      if (const auto *C = dyn_cast<CXXConstructExpr>(E)) {
        if (C->getSourceRange() == SR || C->isElidable())
          return true;
      }

      if (const auto *C = dyn_cast<CXXMemberCallExpr>(E)) {
        if (C->getSourceRange() == SR)
          return true;
      }

      if (const auto *C = dyn_cast<MemberExpr>(E)) {
        if (C->getSourceRange() == SR)
          return true;
      }
      return false;
    };

    while (ShouldSkip(E, Child)) {
      auto It = PointerParents.find(E);
      if (It == PointerParents.end())
        break;
      const auto *S = It->second.dyn_cast<const Stmt *>();
      if (!S) {
        if (auto *Vec = It->second.dyn_cast<ParentVector *>())
          return Vec->view();
        return getSingleDynTypedNodeFromParentMap(It->second);
      }
      const auto *P = dyn_cast<Expr>(S);
      if (!P)
        return DynTypedNode::create(*S);
      Child = E;
      E = P;
    }
    return DynTypedNode::create(*E);
  }
};

template <typename T, typename... U> struct MatchParents {
  static std::tuple<bool, DynTypedNodeList, const T *, const U *...>
  match(const DynTypedNodeList &NodeList,
        ParentMapContext::ParentMap *ParentMap) {
    if (const auto *TypedNode = NodeList[0].get<T>()) {
      auto NextParentList =
          ParentMap->getDynNodeFromMap(TypedNode, ParentMap->PointerParents);
      if (NextParentList.size() == 1) {
        auto TailTuple = MatchParents<U...>::match(NextParentList, ParentMap);
        if (std::get<bool>(TailTuple)) {
          return std::apply(
              [TypedNode](bool, DynTypedNodeList NodeList, auto... TupleTail) {
                return std::make_tuple(true, NodeList, TypedNode, TupleTail...);
              },
              TailTuple);
        }
      }
    }
    return std::tuple_cat(std::make_tuple(false, NodeList),
                          std::tuple<const T *, const U *...>());
  }
};

template <typename T> struct MatchParents<T> {
  static std::tuple<bool, DynTypedNodeList, const T *>
  match(const DynTypedNodeList &NodeList,
        ParentMapContext::ParentMap *ParentMap) {
    if (const auto *TypedNode = NodeList[0].get<T>()) {
      auto NextParentList =
          ParentMap->getDynNodeFromMap(TypedNode, ParentMap->PointerParents);
      if (NextParentList.size() == 1)
        return std::make_tuple(true, NodeList, TypedNode);
    }
    return std::make_tuple(false, NodeList, nullptr);
  }
};

template <typename T, typename... U>
std::tuple<bool, DynTypedNodeList, const T *, const U *...>
matchParents(const DynTypedNodeList &NodeList,
             ParentMapContext::ParentMap *ParentMap) {
  return MatchParents<T, U...>::match(NodeList, ParentMap);
}

/// Template specializations to abstract away from pointers and TypeLocs.
/// @{
template <typename T> static DynTypedNode createDynTypedNode(const T &Node) {
  return DynTypedNode::create(*Node);
}
template <> DynTypedNode createDynTypedNode(const TypeLoc &Node) {
  return DynTypedNode::create(Node);
}
template <>
DynTypedNode createDynTypedNode(const NestedNameSpecifierLoc &Node) {
  return DynTypedNode::create(Node);
}
template <> DynTypedNode createDynTypedNode(const ObjCProtocolLoc &Node) {
  return DynTypedNode::create(Node);
}
/// @}

/// A \c RecursiveASTVisitor that builds a map from nodes to their
/// parents as defined by the \c RecursiveASTVisitor.
///
/// Note that the relationship described here is purely in terms of AST
/// traversal - there are other relationships (for example declaration context)
/// in the AST that are better modeled by special matchers.
class ParentMapContext::ParentMap::ASTVisitor
    : public RecursiveASTVisitor<ASTVisitor> {
public:
  ASTVisitor(ParentMap &Map) : Map(Map) {}

private:
  friend class RecursiveASTVisitor<ASTVisitor>;

  using VisitorBase = RecursiveASTVisitor<ASTVisitor>;

  bool shouldVisitTemplateInstantiations() const { return true; }

  bool shouldVisitImplicitCode() const { return true; }

  /// Record the parent of the node we're visiting.
  /// MapNode is the child, the parent is on top of ParentStack.
  /// Parents is the parent storage (either PointerParents or OtherParents).
  template <typename MapNodeTy, typename MapTy>
  void addParent(MapNodeTy MapNode, MapTy *Parents) {
    if (ParentStack.empty())
      return;

    // FIXME: Currently we add the same parent multiple times, but only
    // when no memoization data is available for the type.
    // For example when we visit all subexpressions of template
    // instantiations; this is suboptimal, but benign: the only way to
    // visit those is with hasAncestor / hasParent, and those do not create
    // new matches.
    // The plan is to enable DynTypedNode to be storable in a map or hash
    // map. The main problem there is to implement hash functions /
    // comparison operators for all types that DynTypedNode supports that
    // do not have pointer identity.
    auto &NodeOrVector = (*Parents)[MapNode];
    if (NodeOrVector.isNull()) {
      if (const auto *D = ParentStack.back().get<Decl>())
        NodeOrVector = D;
      else if (const auto *S = ParentStack.back().get<Stmt>())
        NodeOrVector = S;
      else
        NodeOrVector = new DynTypedNode(ParentStack.back());
    } else {
      if (!NodeOrVector.template is<ParentVector *>()) {
        auto *Vector = new ParentVector(
            1, getSingleDynTypedNodeFromParentMap(NodeOrVector));
        delete NodeOrVector.template dyn_cast<DynTypedNode *>();
        NodeOrVector = Vector;
      }

      auto *Vector = NodeOrVector.template get<ParentVector *>();
      // Skip duplicates for types that have memoization data.
      // We must check that the type has memoization data before calling
      // llvm::is_contained() because DynTypedNode::operator== can't compare all
      // types.
      bool Found = ParentStack.back().getMemoizationData() &&
                   llvm::is_contained(*Vector, ParentStack.back());
      if (!Found)
        Vector->push_back(ParentStack.back());
    }
  }

  template <typename T> static bool isNull(T Node) { return !Node; }
  static bool isNull(ObjCProtocolLoc Node) { return false; }

  template <typename T, typename MapNodeTy, typename BaseTraverseFn,
            typename MapTy>
  bool TraverseNode(T Node, MapNodeTy MapNode, BaseTraverseFn BaseTraverse,
                    MapTy *Parents) {
    if (isNull(Node))
      return true;
    addParent(MapNode, Parents);
    ParentStack.push_back(createDynTypedNode(Node));
    bool Result = BaseTraverse();
    ParentStack.pop_back();
    return Result;
  }

  bool TraverseDecl(Decl *DeclNode) {
    return TraverseNode(
        DeclNode, DeclNode, [&] { return VisitorBase::TraverseDecl(DeclNode); },
        &Map.PointerParents);
  }
  bool TraverseTypeLoc(TypeLoc TypeLocNode) {
    return TraverseNode(
        TypeLocNode, DynTypedNode::create(TypeLocNode),
        [&] { return VisitorBase::TraverseTypeLoc(TypeLocNode); },
        &Map.OtherParents);
  }
  bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc NNSLocNode) {
    return TraverseNode(
        NNSLocNode, DynTypedNode::create(NNSLocNode),
        [&] { return VisitorBase::TraverseNestedNameSpecifierLoc(NNSLocNode); },
        &Map.OtherParents);
  }
  bool TraverseAttr(Attr *AttrNode) {
    return TraverseNode(
        AttrNode, AttrNode, [&] { return VisitorBase::TraverseAttr(AttrNode); },
        &Map.PointerParents);
  }
  bool TraverseObjCProtocolLoc(ObjCProtocolLoc ProtocolLocNode) {
    return TraverseNode(
        ProtocolLocNode, DynTypedNode::create(ProtocolLocNode),
        [&] { return VisitorBase::TraverseObjCProtocolLoc(ProtocolLocNode); },
        &Map.OtherParents);
  }

  // Using generic TraverseNode for Stmt would prevent data-recursion.
  bool dataTraverseStmtPre(Stmt *StmtNode) {
    addParent(StmtNode, &Map.PointerParents);
    ParentStack.push_back(DynTypedNode::create(*StmtNode));
    return true;
  }
  bool dataTraverseStmtPost(Stmt *StmtNode) {
    ParentStack.pop_back();
    return true;
  }

  ParentMap &Map;
  llvm::SmallVector<DynTypedNode, 16> ParentStack;
};

ParentMapContext::ParentMap::ParentMap(ASTContext &Ctx) {
  ASTVisitor(*this).TraverseAST(Ctx);
}

DynTypedNodeList ParentMapContext::getParents(const DynTypedNode &Node) {
  if (!Parents)
    // We build the parent map for the traversal scope (usually whole TU), as
    // hasAncestor can escape any subtree.
    Parents = std::make_unique<ParentMap>(ASTCtx);
  return Parents->getParents(getTraversalKind(), Node);
}
