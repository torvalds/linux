//===- ASTDiff.cpp - AST differencing implementation-----------*- C++ -*- -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitons for the AST differencing interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/ASTDiff/ASTDiff.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/PriorityQueue.h"

#include <limits>
#include <memory>
#include <optional>
#include <unordered_set>

using namespace llvm;
using namespace clang;

namespace clang {
namespace diff {

namespace {
/// Maps nodes of the left tree to ones on the right, and vice versa.
class Mapping {
public:
  Mapping() = default;
  Mapping(Mapping &&Other) = default;
  Mapping &operator=(Mapping &&Other) = default;

  Mapping(size_t Size) {
    SrcToDst = std::make_unique<NodeId[]>(Size);
    DstToSrc = std::make_unique<NodeId[]>(Size);
  }

  void link(NodeId Src, NodeId Dst) {
    SrcToDst[Src] = Dst, DstToSrc[Dst] = Src;
  }

  NodeId getDst(NodeId Src) const { return SrcToDst[Src]; }
  NodeId getSrc(NodeId Dst) const { return DstToSrc[Dst]; }
  bool hasSrc(NodeId Src) const { return getDst(Src).isValid(); }
  bool hasDst(NodeId Dst) const { return getSrc(Dst).isValid(); }

private:
  std::unique_ptr<NodeId[]> SrcToDst, DstToSrc;
};
} // end anonymous namespace

class ASTDiff::Impl {
public:
  SyntaxTree::Impl &T1, &T2;
  Mapping TheMapping;

  Impl(SyntaxTree::Impl &T1, SyntaxTree::Impl &T2,
       const ComparisonOptions &Options);

  /// Matches nodes one-by-one based on their similarity.
  void computeMapping();

  // Compute Change for each node based on similarity.
  void computeChangeKinds(Mapping &M);

  NodeId getMapped(const std::unique_ptr<SyntaxTree::Impl> &Tree,
                   NodeId Id) const {
    if (&*Tree == &T1)
      return TheMapping.getDst(Id);
    assert(&*Tree == &T2 && "Invalid tree.");
    return TheMapping.getSrc(Id);
  }

private:
  // Returns true if the two subtrees are identical.
  bool identical(NodeId Id1, NodeId Id2) const;

  // Returns false if the nodes must not be mached.
  bool isMatchingPossible(NodeId Id1, NodeId Id2) const;

  // Returns true if the nodes' parents are matched.
  bool haveSameParents(const Mapping &M, NodeId Id1, NodeId Id2) const;

  // Uses an optimal albeit slow algorithm to compute a mapping between two
  // subtrees, but only if both have fewer nodes than MaxSize.
  void addOptimalMapping(Mapping &M, NodeId Id1, NodeId Id2) const;

  // Computes the ratio of common descendants between the two nodes.
  // Descendants are only considered to be equal when they are mapped in M.
  double getJaccardSimilarity(const Mapping &M, NodeId Id1, NodeId Id2) const;

  // Returns the node that has the highest degree of similarity.
  NodeId findCandidate(const Mapping &M, NodeId Id1) const;

  // Returns a mapping of identical subtrees.
  Mapping matchTopDown() const;

  // Tries to match any yet unmapped nodes, in a bottom-up fashion.
  void matchBottomUp(Mapping &M) const;

  const ComparisonOptions &Options;

  friend class ZhangShashaMatcher;
};

/// Represents the AST of a TranslationUnit.
class SyntaxTree::Impl {
public:
  Impl(SyntaxTree *Parent, ASTContext &AST);
  /// Constructs a tree from an AST node.
  Impl(SyntaxTree *Parent, Decl *N, ASTContext &AST);
  Impl(SyntaxTree *Parent, Stmt *N, ASTContext &AST);
  template <class T>
  Impl(SyntaxTree *Parent,
       std::enable_if_t<std::is_base_of_v<Stmt, T>, T> *Node, ASTContext &AST)
      : Impl(Parent, dyn_cast<Stmt>(Node), AST) {}
  template <class T>
  Impl(SyntaxTree *Parent,
       std::enable_if_t<std::is_base_of_v<Decl, T>, T> *Node, ASTContext &AST)
      : Impl(Parent, dyn_cast<Decl>(Node), AST) {}

  SyntaxTree *Parent;
  ASTContext &AST;
  PrintingPolicy TypePP;
  /// Nodes in preorder.
  std::vector<Node> Nodes;
  std::vector<NodeId> Leaves;
  // Maps preorder indices to postorder ones.
  std::vector<int> PostorderIds;
  std::vector<NodeId> NodesBfs;

  int getSize() const { return Nodes.size(); }
  NodeId getRootId() const { return 0; }
  PreorderIterator begin() const { return getRootId(); }
  PreorderIterator end() const { return getSize(); }

  const Node &getNode(NodeId Id) const { return Nodes[Id]; }
  Node &getMutableNode(NodeId Id) { return Nodes[Id]; }
  bool isValidNodeId(NodeId Id) const { return Id >= 0 && Id < getSize(); }
  void addNode(Node &N) { Nodes.push_back(N); }
  int getNumberOfDescendants(NodeId Id) const;
  bool isInSubtree(NodeId Id, NodeId SubtreeRoot) const;
  int findPositionInParent(NodeId Id, bool Shifted = false) const;

  std::string getRelativeName(const NamedDecl *ND,
                              const DeclContext *Context) const;
  std::string getRelativeName(const NamedDecl *ND) const;

  std::string getNodeValue(NodeId Id) const;
  std::string getNodeValue(const Node &Node) const;
  std::string getDeclValue(const Decl *D) const;
  std::string getStmtValue(const Stmt *S) const;

private:
  void initTree();
  void setLeftMostDescendants();
};

static bool isSpecializedNodeExcluded(const Decl *D) { return D->isImplicit(); }
static bool isSpecializedNodeExcluded(const Stmt *S) { return false; }
static bool isSpecializedNodeExcluded(CXXCtorInitializer *I) {
  return !I->isWritten();
}

template <class T>
static bool isNodeExcluded(const SourceManager &SrcMgr, T *N) {
  if (!N)
    return true;
  SourceLocation SLoc = N->getSourceRange().getBegin();
  if (SLoc.isValid()) {
    // Ignore everything from other files.
    if (!SrcMgr.isInMainFile(SLoc))
      return true;
    // Ignore macros.
    if (SLoc != SrcMgr.getSpellingLoc(SLoc))
      return true;
  }
  return isSpecializedNodeExcluded(N);
}

namespace {
// Sets Height, Parent and Children for each node.
struct PreorderVisitor : public RecursiveASTVisitor<PreorderVisitor> {
  int Id = 0, Depth = 0;
  NodeId Parent;
  SyntaxTree::Impl &Tree;

  PreorderVisitor(SyntaxTree::Impl &Tree) : Tree(Tree) {}

  template <class T> std::tuple<NodeId, NodeId> PreTraverse(T *ASTNode) {
    NodeId MyId = Id;
    Tree.Nodes.emplace_back();
    Node &N = Tree.getMutableNode(MyId);
    N.Parent = Parent;
    N.Depth = Depth;
    N.ASTNode = DynTypedNode::create(*ASTNode);
    assert(!N.ASTNode.getNodeKind().isNone() &&
           "Expected nodes to have a valid kind.");
    if (Parent.isValid()) {
      Node &P = Tree.getMutableNode(Parent);
      P.Children.push_back(MyId);
    }
    Parent = MyId;
    ++Id;
    ++Depth;
    return std::make_tuple(MyId, Tree.getNode(MyId).Parent);
  }
  void PostTraverse(std::tuple<NodeId, NodeId> State) {
    NodeId MyId, PreviousParent;
    std::tie(MyId, PreviousParent) = State;
    assert(MyId.isValid() && "Expecting to only traverse valid nodes.");
    Parent = PreviousParent;
    --Depth;
    Node &N = Tree.getMutableNode(MyId);
    N.RightMostDescendant = Id - 1;
    assert(N.RightMostDescendant >= 0 &&
           N.RightMostDescendant < Tree.getSize() &&
           "Rightmost descendant must be a valid tree node.");
    if (N.isLeaf())
      Tree.Leaves.push_back(MyId);
    N.Height = 1;
    for (NodeId Child : N.Children)
      N.Height = std::max(N.Height, 1 + Tree.getNode(Child).Height);
  }
  bool TraverseDecl(Decl *D) {
    if (isNodeExcluded(Tree.AST.getSourceManager(), D))
      return true;
    auto SavedState = PreTraverse(D);
    RecursiveASTVisitor<PreorderVisitor>::TraverseDecl(D);
    PostTraverse(SavedState);
    return true;
  }
  bool TraverseStmt(Stmt *S) {
    if (auto *E = dyn_cast_or_null<Expr>(S))
      S = E->IgnoreImplicit();
    if (isNodeExcluded(Tree.AST.getSourceManager(), S))
      return true;
    auto SavedState = PreTraverse(S);
    RecursiveASTVisitor<PreorderVisitor>::TraverseStmt(S);
    PostTraverse(SavedState);
    return true;
  }
  bool TraverseType(QualType T) { return true; }
  bool TraverseConstructorInitializer(CXXCtorInitializer *Init) {
    if (isNodeExcluded(Tree.AST.getSourceManager(), Init))
      return true;
    auto SavedState = PreTraverse(Init);
    RecursiveASTVisitor<PreorderVisitor>::TraverseConstructorInitializer(Init);
    PostTraverse(SavedState);
    return true;
  }
};
} // end anonymous namespace

SyntaxTree::Impl::Impl(SyntaxTree *Parent, ASTContext &AST)
    : Parent(Parent), AST(AST), TypePP(AST.getLangOpts()) {
  TypePP.AnonymousTagLocations = false;
}

SyntaxTree::Impl::Impl(SyntaxTree *Parent, Decl *N, ASTContext &AST)
    : Impl(Parent, AST) {
  PreorderVisitor PreorderWalker(*this);
  PreorderWalker.TraverseDecl(N);
  initTree();
}

SyntaxTree::Impl::Impl(SyntaxTree *Parent, Stmt *N, ASTContext &AST)
    : Impl(Parent, AST) {
  PreorderVisitor PreorderWalker(*this);
  PreorderWalker.TraverseStmt(N);
  initTree();
}

static std::vector<NodeId> getSubtreePostorder(const SyntaxTree::Impl &Tree,
                                               NodeId Root) {
  std::vector<NodeId> Postorder;
  std::function<void(NodeId)> Traverse = [&](NodeId Id) {
    const Node &N = Tree.getNode(Id);
    for (NodeId Child : N.Children)
      Traverse(Child);
    Postorder.push_back(Id);
  };
  Traverse(Root);
  return Postorder;
}

static std::vector<NodeId> getSubtreeBfs(const SyntaxTree::Impl &Tree,
                                         NodeId Root) {
  std::vector<NodeId> Ids;
  size_t Expanded = 0;
  Ids.push_back(Root);
  while (Expanded < Ids.size())
    for (NodeId Child : Tree.getNode(Ids[Expanded++]).Children)
      Ids.push_back(Child);
  return Ids;
}

void SyntaxTree::Impl::initTree() {
  setLeftMostDescendants();
  int PostorderId = 0;
  PostorderIds.resize(getSize());
  std::function<void(NodeId)> PostorderTraverse = [&](NodeId Id) {
    for (NodeId Child : getNode(Id).Children)
      PostorderTraverse(Child);
    PostorderIds[Id] = PostorderId;
    ++PostorderId;
  };
  PostorderTraverse(getRootId());
  NodesBfs = getSubtreeBfs(*this, getRootId());
}

void SyntaxTree::Impl::setLeftMostDescendants() {
  for (NodeId Leaf : Leaves) {
    getMutableNode(Leaf).LeftMostDescendant = Leaf;
    NodeId Parent, Cur = Leaf;
    while ((Parent = getNode(Cur).Parent).isValid() &&
           getNode(Parent).Children[0] == Cur) {
      Cur = Parent;
      getMutableNode(Cur).LeftMostDescendant = Leaf;
    }
  }
}

int SyntaxTree::Impl::getNumberOfDescendants(NodeId Id) const {
  return getNode(Id).RightMostDescendant - Id + 1;
}

bool SyntaxTree::Impl::isInSubtree(NodeId Id, NodeId SubtreeRoot) const {
  return Id >= SubtreeRoot && Id <= getNode(SubtreeRoot).RightMostDescendant;
}

int SyntaxTree::Impl::findPositionInParent(NodeId Id, bool Shifted) const {
  NodeId Parent = getNode(Id).Parent;
  if (Parent.isInvalid())
    return 0;
  const auto &Siblings = getNode(Parent).Children;
  int Position = 0;
  for (size_t I = 0, E = Siblings.size(); I < E; ++I) {
    if (Shifted)
      Position += getNode(Siblings[I]).Shift;
    if (Siblings[I] == Id) {
      Position += I;
      return Position;
    }
  }
  llvm_unreachable("Node not found in parent's children.");
}

// Returns the qualified name of ND. If it is subordinate to Context,
// then the prefix of the latter is removed from the returned value.
std::string
SyntaxTree::Impl::getRelativeName(const NamedDecl *ND,
                                  const DeclContext *Context) const {
  std::string Val = ND->getQualifiedNameAsString();
  std::string ContextPrefix;
  if (!Context)
    return Val;
  if (auto *Namespace = dyn_cast<NamespaceDecl>(Context))
    ContextPrefix = Namespace->getQualifiedNameAsString();
  else if (auto *Record = dyn_cast<RecordDecl>(Context))
    ContextPrefix = Record->getQualifiedNameAsString();
  else if (AST.getLangOpts().CPlusPlus11)
    if (auto *Tag = dyn_cast<TagDecl>(Context))
      ContextPrefix = Tag->getQualifiedNameAsString();
  // Strip the qualifier, if Val refers to something in the current scope.
  // But leave one leading ':' in place, so that we know that this is a
  // relative path.
  if (!ContextPrefix.empty() && StringRef(Val).starts_with(ContextPrefix))
    Val = Val.substr(ContextPrefix.size() + 1);
  return Val;
}

std::string SyntaxTree::Impl::getRelativeName(const NamedDecl *ND) const {
  return getRelativeName(ND, ND->getDeclContext());
}

static const DeclContext *getEnclosingDeclContext(ASTContext &AST,
                                                  const Stmt *S) {
  while (S) {
    const auto &Parents = AST.getParents(*S);
    if (Parents.empty())
      return nullptr;
    const auto &P = Parents[0];
    if (const auto *D = P.get<Decl>())
      return D->getDeclContext();
    S = P.get<Stmt>();
  }
  return nullptr;
}

static std::string getInitializerValue(const CXXCtorInitializer *Init,
                                       const PrintingPolicy &TypePP) {
  if (Init->isAnyMemberInitializer())
    return std::string(Init->getAnyMember()->getName());
  if (Init->isBaseInitializer())
    return QualType(Init->getBaseClass(), 0).getAsString(TypePP);
  if (Init->isDelegatingInitializer())
    return Init->getTypeSourceInfo()->getType().getAsString(TypePP);
  llvm_unreachable("Unknown initializer type");
}

std::string SyntaxTree::Impl::getNodeValue(NodeId Id) const {
  return getNodeValue(getNode(Id));
}

std::string SyntaxTree::Impl::getNodeValue(const Node &N) const {
  const DynTypedNode &DTN = N.ASTNode;
  if (auto *S = DTN.get<Stmt>())
    return getStmtValue(S);
  if (auto *D = DTN.get<Decl>())
    return getDeclValue(D);
  if (auto *Init = DTN.get<CXXCtorInitializer>())
    return getInitializerValue(Init, TypePP);
  llvm_unreachable("Fatal: unhandled AST node.\n");
}

std::string SyntaxTree::Impl::getDeclValue(const Decl *D) const {
  std::string Value;
  if (auto *V = dyn_cast<ValueDecl>(D))
    return getRelativeName(V) + "(" + V->getType().getAsString(TypePP) + ")";
  if (auto *N = dyn_cast<NamedDecl>(D))
    Value += getRelativeName(N) + ";";
  if (auto *T = dyn_cast<TypedefNameDecl>(D))
    return Value + T->getUnderlyingType().getAsString(TypePP) + ";";
  if (auto *T = dyn_cast<TypeDecl>(D))
    if (T->getTypeForDecl())
      Value +=
          T->getTypeForDecl()->getCanonicalTypeInternal().getAsString(TypePP) +
          ";";
  if (auto *U = dyn_cast<UsingDirectiveDecl>(D))
    return std::string(U->getNominatedNamespace()->getName());
  if (auto *A = dyn_cast<AccessSpecDecl>(D)) {
    CharSourceRange Range(A->getSourceRange(), false);
    return std::string(
        Lexer::getSourceText(Range, AST.getSourceManager(), AST.getLangOpts()));
  }
  return Value;
}

std::string SyntaxTree::Impl::getStmtValue(const Stmt *S) const {
  if (auto *U = dyn_cast<UnaryOperator>(S))
    return std::string(UnaryOperator::getOpcodeStr(U->getOpcode()));
  if (auto *B = dyn_cast<BinaryOperator>(S))
    return std::string(B->getOpcodeStr());
  if (auto *M = dyn_cast<MemberExpr>(S))
    return getRelativeName(M->getMemberDecl());
  if (auto *I = dyn_cast<IntegerLiteral>(S)) {
    SmallString<256> Str;
    I->getValue().toString(Str, /*Radix=*/10, /*Signed=*/false);
    return std::string(Str);
  }
  if (auto *F = dyn_cast<FloatingLiteral>(S)) {
    SmallString<256> Str;
    F->getValue().toString(Str);
    return std::string(Str);
  }
  if (auto *D = dyn_cast<DeclRefExpr>(S))
    return getRelativeName(D->getDecl(), getEnclosingDeclContext(AST, S));
  if (auto *String = dyn_cast<StringLiteral>(S))
    return std::string(String->getString());
  if (auto *B = dyn_cast<CXXBoolLiteralExpr>(S))
    return B->getValue() ? "true" : "false";
  return "";
}

/// Identifies a node in a subtree by its postorder offset, starting at 1.
struct SNodeId {
  int Id = 0;

  explicit SNodeId(int Id) : Id(Id) {}
  explicit SNodeId() = default;

  operator int() const { return Id; }
  SNodeId &operator++() { return ++Id, *this; }
  SNodeId &operator--() { return --Id, *this; }
  SNodeId operator+(int Other) const { return SNodeId(Id + Other); }
};

class Subtree {
private:
  /// The parent tree.
  const SyntaxTree::Impl &Tree;
  /// Maps SNodeIds to original ids.
  std::vector<NodeId> RootIds;
  /// Maps subtree nodes to their leftmost descendants wtihin the subtree.
  std::vector<SNodeId> LeftMostDescendants;

public:
  std::vector<SNodeId> KeyRoots;

  Subtree(const SyntaxTree::Impl &Tree, NodeId SubtreeRoot) : Tree(Tree) {
    RootIds = getSubtreePostorder(Tree, SubtreeRoot);
    int NumLeaves = setLeftMostDescendants();
    computeKeyRoots(NumLeaves);
  }
  int getSize() const { return RootIds.size(); }
  NodeId getIdInRoot(SNodeId Id) const {
    assert(Id > 0 && Id <= getSize() && "Invalid subtree node index.");
    return RootIds[Id - 1];
  }
  const Node &getNode(SNodeId Id) const {
    return Tree.getNode(getIdInRoot(Id));
  }
  SNodeId getLeftMostDescendant(SNodeId Id) const {
    assert(Id > 0 && Id <= getSize() && "Invalid subtree node index.");
    return LeftMostDescendants[Id - 1];
  }
  /// Returns the postorder index of the leftmost descendant in the subtree.
  NodeId getPostorderOffset() const {
    return Tree.PostorderIds[getIdInRoot(SNodeId(1))];
  }
  std::string getNodeValue(SNodeId Id) const {
    return Tree.getNodeValue(getIdInRoot(Id));
  }

private:
  /// Returns the number of leafs in the subtree.
  int setLeftMostDescendants() {
    int NumLeaves = 0;
    LeftMostDescendants.resize(getSize());
    for (int I = 0; I < getSize(); ++I) {
      SNodeId SI(I + 1);
      const Node &N = getNode(SI);
      NumLeaves += N.isLeaf();
      assert(I == Tree.PostorderIds[getIdInRoot(SI)] - getPostorderOffset() &&
             "Postorder traversal in subtree should correspond to traversal in "
             "the root tree by a constant offset.");
      LeftMostDescendants[I] = SNodeId(Tree.PostorderIds[N.LeftMostDescendant] -
                                       getPostorderOffset());
    }
    return NumLeaves;
  }
  void computeKeyRoots(int Leaves) {
    KeyRoots.resize(Leaves);
    std::unordered_set<int> Visited;
    int K = Leaves - 1;
    for (SNodeId I(getSize()); I > 0; --I) {
      SNodeId LeftDesc = getLeftMostDescendant(I);
      if (Visited.count(LeftDesc))
        continue;
      assert(K >= 0 && "K should be non-negative");
      KeyRoots[K] = I;
      Visited.insert(LeftDesc);
      --K;
    }
  }
};

/// Implementation of Zhang and Shasha's Algorithm for tree edit distance.
/// Computes an optimal mapping between two trees using only insertion,
/// deletion and update as edit actions (similar to the Levenshtein distance).
class ZhangShashaMatcher {
  const ASTDiff::Impl &DiffImpl;
  Subtree S1;
  Subtree S2;
  std::unique_ptr<std::unique_ptr<double[]>[]> TreeDist, ForestDist;

public:
  ZhangShashaMatcher(const ASTDiff::Impl &DiffImpl, const SyntaxTree::Impl &T1,
                     const SyntaxTree::Impl &T2, NodeId Id1, NodeId Id2)
      : DiffImpl(DiffImpl), S1(T1, Id1), S2(T2, Id2) {
    TreeDist = std::make_unique<std::unique_ptr<double[]>[]>(
        size_t(S1.getSize()) + 1);
    ForestDist = std::make_unique<std::unique_ptr<double[]>[]>(
        size_t(S1.getSize()) + 1);
    for (int I = 0, E = S1.getSize() + 1; I < E; ++I) {
      TreeDist[I] = std::make_unique<double[]>(size_t(S2.getSize()) + 1);
      ForestDist[I] = std::make_unique<double[]>(size_t(S2.getSize()) + 1);
    }
  }

  std::vector<std::pair<NodeId, NodeId>> getMatchingNodes() {
    std::vector<std::pair<NodeId, NodeId>> Matches;
    std::vector<std::pair<SNodeId, SNodeId>> TreePairs;

    computeTreeDist();

    bool RootNodePair = true;

    TreePairs.emplace_back(SNodeId(S1.getSize()), SNodeId(S2.getSize()));

    while (!TreePairs.empty()) {
      SNodeId LastRow, LastCol, FirstRow, FirstCol, Row, Col;
      std::tie(LastRow, LastCol) = TreePairs.back();
      TreePairs.pop_back();

      if (!RootNodePair) {
        computeForestDist(LastRow, LastCol);
      }

      RootNodePair = false;

      FirstRow = S1.getLeftMostDescendant(LastRow);
      FirstCol = S2.getLeftMostDescendant(LastCol);

      Row = LastRow;
      Col = LastCol;

      while (Row > FirstRow || Col > FirstCol) {
        if (Row > FirstRow &&
            ForestDist[Row - 1][Col] + 1 == ForestDist[Row][Col]) {
          --Row;
        } else if (Col > FirstCol &&
                   ForestDist[Row][Col - 1] + 1 == ForestDist[Row][Col]) {
          --Col;
        } else {
          SNodeId LMD1 = S1.getLeftMostDescendant(Row);
          SNodeId LMD2 = S2.getLeftMostDescendant(Col);
          if (LMD1 == S1.getLeftMostDescendant(LastRow) &&
              LMD2 == S2.getLeftMostDescendant(LastCol)) {
            NodeId Id1 = S1.getIdInRoot(Row);
            NodeId Id2 = S2.getIdInRoot(Col);
            assert(DiffImpl.isMatchingPossible(Id1, Id2) &&
                   "These nodes must not be matched.");
            Matches.emplace_back(Id1, Id2);
            --Row;
            --Col;
          } else {
            TreePairs.emplace_back(Row, Col);
            Row = LMD1;
            Col = LMD2;
          }
        }
      }
    }
    return Matches;
  }

private:
  /// We use a simple cost model for edit actions, which seems good enough.
  /// Simple cost model for edit actions. This seems to make the matching
  /// algorithm perform reasonably well.
  /// The values range between 0 and 1, or infinity if this edit action should
  /// always be avoided.
  static constexpr double DeletionCost = 1;
  static constexpr double InsertionCost = 1;

  double getUpdateCost(SNodeId Id1, SNodeId Id2) {
    if (!DiffImpl.isMatchingPossible(S1.getIdInRoot(Id1), S2.getIdInRoot(Id2)))
      return std::numeric_limits<double>::max();
    return S1.getNodeValue(Id1) != S2.getNodeValue(Id2);
  }

  void computeTreeDist() {
    for (SNodeId Id1 : S1.KeyRoots)
      for (SNodeId Id2 : S2.KeyRoots)
        computeForestDist(Id1, Id2);
  }

  void computeForestDist(SNodeId Id1, SNodeId Id2) {
    assert(Id1 > 0 && Id2 > 0 && "Expecting offsets greater than 0.");
    SNodeId LMD1 = S1.getLeftMostDescendant(Id1);
    SNodeId LMD2 = S2.getLeftMostDescendant(Id2);

    ForestDist[LMD1][LMD2] = 0;
    for (SNodeId D1 = LMD1 + 1; D1 <= Id1; ++D1) {
      ForestDist[D1][LMD2] = ForestDist[D1 - 1][LMD2] + DeletionCost;
      for (SNodeId D2 = LMD2 + 1; D2 <= Id2; ++D2) {
        ForestDist[LMD1][D2] = ForestDist[LMD1][D2 - 1] + InsertionCost;
        SNodeId DLMD1 = S1.getLeftMostDescendant(D1);
        SNodeId DLMD2 = S2.getLeftMostDescendant(D2);
        if (DLMD1 == LMD1 && DLMD2 == LMD2) {
          double UpdateCost = getUpdateCost(D1, D2);
          ForestDist[D1][D2] =
              std::min({ForestDist[D1 - 1][D2] + DeletionCost,
                        ForestDist[D1][D2 - 1] + InsertionCost,
                        ForestDist[D1 - 1][D2 - 1] + UpdateCost});
          TreeDist[D1][D2] = ForestDist[D1][D2];
        } else {
          ForestDist[D1][D2] =
              std::min({ForestDist[D1 - 1][D2] + DeletionCost,
                        ForestDist[D1][D2 - 1] + InsertionCost,
                        ForestDist[DLMD1][DLMD2] + TreeDist[D1][D2]});
        }
      }
    }
  }
};

ASTNodeKind Node::getType() const { return ASTNode.getNodeKind(); }

StringRef Node::getTypeLabel() const { return getType().asStringRef(); }

std::optional<std::string> Node::getQualifiedIdentifier() const {
  if (auto *ND = ASTNode.get<NamedDecl>()) {
    if (ND->getDeclName().isIdentifier())
      return ND->getQualifiedNameAsString();
  }
  return std::nullopt;
}

std::optional<StringRef> Node::getIdentifier() const {
  if (auto *ND = ASTNode.get<NamedDecl>()) {
    if (ND->getDeclName().isIdentifier())
      return ND->getName();
  }
  return std::nullopt;
}

namespace {
// Compares nodes by their depth.
struct HeightLess {
  const SyntaxTree::Impl &Tree;
  HeightLess(const SyntaxTree::Impl &Tree) : Tree(Tree) {}
  bool operator()(NodeId Id1, NodeId Id2) const {
    return Tree.getNode(Id1).Height < Tree.getNode(Id2).Height;
  }
};
} // end anonymous namespace

namespace {
// Priority queue for nodes, sorted descendingly by their height.
class PriorityList {
  const SyntaxTree::Impl &Tree;
  HeightLess Cmp;
  std::vector<NodeId> Container;
  PriorityQueue<NodeId, std::vector<NodeId>, HeightLess> List;

public:
  PriorityList(const SyntaxTree::Impl &Tree)
      : Tree(Tree), Cmp(Tree), List(Cmp, Container) {}

  void push(NodeId id) { List.push(id); }

  std::vector<NodeId> pop() {
    int Max = peekMax();
    std::vector<NodeId> Result;
    if (Max == 0)
      return Result;
    while (peekMax() == Max) {
      Result.push_back(List.top());
      List.pop();
    }
    // TODO this is here to get a stable output, not a good heuristic
    llvm::sort(Result);
    return Result;
  }
  int peekMax() const {
    if (List.empty())
      return 0;
    return Tree.getNode(List.top()).Height;
  }
  void open(NodeId Id) {
    for (NodeId Child : Tree.getNode(Id).Children)
      push(Child);
  }
};
} // end anonymous namespace

bool ASTDiff::Impl::identical(NodeId Id1, NodeId Id2) const {
  const Node &N1 = T1.getNode(Id1);
  const Node &N2 = T2.getNode(Id2);
  if (N1.Children.size() != N2.Children.size() ||
      !isMatchingPossible(Id1, Id2) ||
      T1.getNodeValue(Id1) != T2.getNodeValue(Id2))
    return false;
  for (size_t Id = 0, E = N1.Children.size(); Id < E; ++Id)
    if (!identical(N1.Children[Id], N2.Children[Id]))
      return false;
  return true;
}

bool ASTDiff::Impl::isMatchingPossible(NodeId Id1, NodeId Id2) const {
  return Options.isMatchingAllowed(T1.getNode(Id1), T2.getNode(Id2));
}

bool ASTDiff::Impl::haveSameParents(const Mapping &M, NodeId Id1,
                                    NodeId Id2) const {
  NodeId P1 = T1.getNode(Id1).Parent;
  NodeId P2 = T2.getNode(Id2).Parent;
  return (P1.isInvalid() && P2.isInvalid()) ||
         (P1.isValid() && P2.isValid() && M.getDst(P1) == P2);
}

void ASTDiff::Impl::addOptimalMapping(Mapping &M, NodeId Id1,
                                      NodeId Id2) const {
  if (std::max(T1.getNumberOfDescendants(Id1), T2.getNumberOfDescendants(Id2)) >
      Options.MaxSize)
    return;
  ZhangShashaMatcher Matcher(*this, T1, T2, Id1, Id2);
  std::vector<std::pair<NodeId, NodeId>> R = Matcher.getMatchingNodes();
  for (const auto &Tuple : R) {
    NodeId Src = Tuple.first;
    NodeId Dst = Tuple.second;
    if (!M.hasSrc(Src) && !M.hasDst(Dst))
      M.link(Src, Dst);
  }
}

double ASTDiff::Impl::getJaccardSimilarity(const Mapping &M, NodeId Id1,
                                           NodeId Id2) const {
  int CommonDescendants = 0;
  const Node &N1 = T1.getNode(Id1);
  // Count the common descendants, excluding the subtree root.
  for (NodeId Src = Id1 + 1; Src <= N1.RightMostDescendant; ++Src) {
    NodeId Dst = M.getDst(Src);
    CommonDescendants += int(Dst.isValid() && T2.isInSubtree(Dst, Id2));
  }
  // We need to subtract 1 to get the number of descendants excluding the root.
  double Denominator = T1.getNumberOfDescendants(Id1) - 1 +
                       T2.getNumberOfDescendants(Id2) - 1 - CommonDescendants;
  // CommonDescendants is less than the size of one subtree.
  assert(Denominator >= 0 && "Expected non-negative denominator.");
  if (Denominator == 0)
    return 0;
  return CommonDescendants / Denominator;
}

NodeId ASTDiff::Impl::findCandidate(const Mapping &M, NodeId Id1) const {
  NodeId Candidate;
  double HighestSimilarity = 0.0;
  for (NodeId Id2 : T2) {
    if (!isMatchingPossible(Id1, Id2))
      continue;
    if (M.hasDst(Id2))
      continue;
    double Similarity = getJaccardSimilarity(M, Id1, Id2);
    if (Similarity >= Options.MinSimilarity && Similarity > HighestSimilarity) {
      HighestSimilarity = Similarity;
      Candidate = Id2;
    }
  }
  return Candidate;
}

void ASTDiff::Impl::matchBottomUp(Mapping &M) const {
  std::vector<NodeId> Postorder = getSubtreePostorder(T1, T1.getRootId());
  for (NodeId Id1 : Postorder) {
    if (Id1 == T1.getRootId() && !M.hasSrc(T1.getRootId()) &&
        !M.hasDst(T2.getRootId())) {
      if (isMatchingPossible(T1.getRootId(), T2.getRootId())) {
        M.link(T1.getRootId(), T2.getRootId());
        addOptimalMapping(M, T1.getRootId(), T2.getRootId());
      }
      break;
    }
    bool Matched = M.hasSrc(Id1);
    const Node &N1 = T1.getNode(Id1);
    bool MatchedChildren = llvm::any_of(
        N1.Children, [&](NodeId Child) { return M.hasSrc(Child); });
    if (Matched || !MatchedChildren)
      continue;
    NodeId Id2 = findCandidate(M, Id1);
    if (Id2.isValid()) {
      M.link(Id1, Id2);
      addOptimalMapping(M, Id1, Id2);
    }
  }
}

Mapping ASTDiff::Impl::matchTopDown() const {
  PriorityList L1(T1);
  PriorityList L2(T2);

  Mapping M(T1.getSize() + T2.getSize());

  L1.push(T1.getRootId());
  L2.push(T2.getRootId());

  int Max1, Max2;
  while (std::min(Max1 = L1.peekMax(), Max2 = L2.peekMax()) >
         Options.MinHeight) {
    if (Max1 > Max2) {
      for (NodeId Id : L1.pop())
        L1.open(Id);
      continue;
    }
    if (Max2 > Max1) {
      for (NodeId Id : L2.pop())
        L2.open(Id);
      continue;
    }
    std::vector<NodeId> H1, H2;
    H1 = L1.pop();
    H2 = L2.pop();
    for (NodeId Id1 : H1) {
      for (NodeId Id2 : H2) {
        if (identical(Id1, Id2) && !M.hasSrc(Id1) && !M.hasDst(Id2)) {
          for (int I = 0, E = T1.getNumberOfDescendants(Id1); I < E; ++I)
            M.link(Id1 + I, Id2 + I);
        }
      }
    }
    for (NodeId Id1 : H1) {
      if (!M.hasSrc(Id1))
        L1.open(Id1);
    }
    for (NodeId Id2 : H2) {
      if (!M.hasDst(Id2))
        L2.open(Id2);
    }
  }
  return M;
}

ASTDiff::Impl::Impl(SyntaxTree::Impl &T1, SyntaxTree::Impl &T2,
                    const ComparisonOptions &Options)
    : T1(T1), T2(T2), Options(Options) {
  computeMapping();
  computeChangeKinds(TheMapping);
}

void ASTDiff::Impl::computeMapping() {
  TheMapping = matchTopDown();
  if (Options.StopAfterTopDown)
    return;
  matchBottomUp(TheMapping);
}

void ASTDiff::Impl::computeChangeKinds(Mapping &M) {
  for (NodeId Id1 : T1) {
    if (!M.hasSrc(Id1)) {
      T1.getMutableNode(Id1).Change = Delete;
      T1.getMutableNode(Id1).Shift -= 1;
    }
  }
  for (NodeId Id2 : T2) {
    if (!M.hasDst(Id2)) {
      T2.getMutableNode(Id2).Change = Insert;
      T2.getMutableNode(Id2).Shift -= 1;
    }
  }
  for (NodeId Id1 : T1.NodesBfs) {
    NodeId Id2 = M.getDst(Id1);
    if (Id2.isInvalid())
      continue;
    if (!haveSameParents(M, Id1, Id2) ||
        T1.findPositionInParent(Id1, true) !=
            T2.findPositionInParent(Id2, true)) {
      T1.getMutableNode(Id1).Shift -= 1;
      T2.getMutableNode(Id2).Shift -= 1;
    }
  }
  for (NodeId Id2 : T2.NodesBfs) {
    NodeId Id1 = M.getSrc(Id2);
    if (Id1.isInvalid())
      continue;
    Node &N1 = T1.getMutableNode(Id1);
    Node &N2 = T2.getMutableNode(Id2);
    if (Id1.isInvalid())
      continue;
    if (!haveSameParents(M, Id1, Id2) ||
        T1.findPositionInParent(Id1, true) !=
            T2.findPositionInParent(Id2, true)) {
      N1.Change = N2.Change = Move;
    }
    if (T1.getNodeValue(Id1) != T2.getNodeValue(Id2)) {
      N1.Change = N2.Change = (N1.Change == Move ? UpdateMove : Update);
    }
  }
}

ASTDiff::ASTDiff(SyntaxTree &T1, SyntaxTree &T2,
                 const ComparisonOptions &Options)
    : DiffImpl(std::make_unique<Impl>(*T1.TreeImpl, *T2.TreeImpl, Options)) {}

ASTDiff::~ASTDiff() = default;

NodeId ASTDiff::getMapped(const SyntaxTree &SourceTree, NodeId Id) const {
  return DiffImpl->getMapped(SourceTree.TreeImpl, Id);
}

SyntaxTree::SyntaxTree(ASTContext &AST)
    : TreeImpl(std::make_unique<SyntaxTree::Impl>(
          this, AST.getTranslationUnitDecl(), AST)) {}

SyntaxTree::~SyntaxTree() = default;

const ASTContext &SyntaxTree::getASTContext() const { return TreeImpl->AST; }

const Node &SyntaxTree::getNode(NodeId Id) const {
  return TreeImpl->getNode(Id);
}

int SyntaxTree::getSize() const { return TreeImpl->getSize(); }
NodeId SyntaxTree::getRootId() const { return TreeImpl->getRootId(); }
SyntaxTree::PreorderIterator SyntaxTree::begin() const {
  return TreeImpl->begin();
}
SyntaxTree::PreorderIterator SyntaxTree::end() const { return TreeImpl->end(); }

int SyntaxTree::findPositionInParent(NodeId Id) const {
  return TreeImpl->findPositionInParent(Id);
}

std::pair<unsigned, unsigned>
SyntaxTree::getSourceRangeOffsets(const Node &N) const {
  const SourceManager &SrcMgr = TreeImpl->AST.getSourceManager();
  SourceRange Range = N.ASTNode.getSourceRange();
  SourceLocation BeginLoc = Range.getBegin();
  SourceLocation EndLoc = Lexer::getLocForEndOfToken(
      Range.getEnd(), /*Offset=*/0, SrcMgr, TreeImpl->AST.getLangOpts());
  if (auto *ThisExpr = N.ASTNode.get<CXXThisExpr>()) {
    if (ThisExpr->isImplicit())
      EndLoc = BeginLoc;
  }
  unsigned Begin = SrcMgr.getFileOffset(SrcMgr.getExpansionLoc(BeginLoc));
  unsigned End = SrcMgr.getFileOffset(SrcMgr.getExpansionLoc(EndLoc));
  return {Begin, End};
}

std::string SyntaxTree::getNodeValue(NodeId Id) const {
  return TreeImpl->getNodeValue(Id);
}

std::string SyntaxTree::getNodeValue(const Node &N) const {
  return TreeImpl->getNodeValue(N);
}

} // end namespace diff
} // end namespace clang
