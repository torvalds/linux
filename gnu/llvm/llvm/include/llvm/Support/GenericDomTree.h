//===- GenericDomTree.h - Generic dominator trees for graphs ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a set of templates that efficiently compute a dominator
/// tree over a generic graph. This is used typically in LLVM for fast
/// dominance queries on the CFG, but is fully generic w.r.t. the underlying
/// graph types.
///
/// Unlike ADT/* graph algorithms, generic dominator tree has more requirements
/// on the graph's NodeRef. The NodeRef should be a pointer and,
/// either NodeRef->getParent() must return the parent node that is also a
/// pointer or DomTreeNodeTraits needs to be specialized.
///
/// FIXME: Maybe GenericDomTree needs a TreeTraits, instead of GraphTraits.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GENERICDOMTREE_H
#define LLVM_SUPPORT_GENERICDOMTREE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CFGDiff.h"
#include "llvm/Support/CFGUpdate.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace llvm {

template <typename NodeT, bool IsPostDom>
class DominatorTreeBase;

namespace DomTreeBuilder {
template <typename DomTreeT>
struct SemiNCAInfo;
}  // namespace DomTreeBuilder

/// Base class for the actual dominator tree node.
template <class NodeT> class DomTreeNodeBase {
  friend class PostDominatorTree;
  friend class DominatorTreeBase<NodeT, false>;
  friend class DominatorTreeBase<NodeT, true>;
  friend struct DomTreeBuilder::SemiNCAInfo<DominatorTreeBase<NodeT, false>>;
  friend struct DomTreeBuilder::SemiNCAInfo<DominatorTreeBase<NodeT, true>>;

  NodeT *TheBB;
  DomTreeNodeBase *IDom;
  unsigned Level;
  SmallVector<DomTreeNodeBase *, 4> Children;
  mutable unsigned DFSNumIn = ~0;
  mutable unsigned DFSNumOut = ~0;

 public:
  DomTreeNodeBase(NodeT *BB, DomTreeNodeBase *iDom)
      : TheBB(BB), IDom(iDom), Level(IDom ? IDom->Level + 1 : 0) {}

  using iterator = typename SmallVector<DomTreeNodeBase *, 4>::iterator;
  using const_iterator =
      typename SmallVector<DomTreeNodeBase *, 4>::const_iterator;

  iterator begin() { return Children.begin(); }
  iterator end() { return Children.end(); }
  const_iterator begin() const { return Children.begin(); }
  const_iterator end() const { return Children.end(); }

  DomTreeNodeBase *const &back() const { return Children.back(); }
  DomTreeNodeBase *&back() { return Children.back(); }

  iterator_range<iterator> children() { return make_range(begin(), end()); }
  iterator_range<const_iterator> children() const {
    return make_range(begin(), end());
  }

  NodeT *getBlock() const { return TheBB; }
  DomTreeNodeBase *getIDom() const { return IDom; }
  unsigned getLevel() const { return Level; }

  std::unique_ptr<DomTreeNodeBase> addChild(
      std::unique_ptr<DomTreeNodeBase> C) {
    Children.push_back(C.get());
    return C;
  }

  bool isLeaf() const { return Children.empty(); }
  size_t getNumChildren() const { return Children.size(); }

  void clearAllChildren() { Children.clear(); }

  bool compare(const DomTreeNodeBase *Other) const {
    if (getNumChildren() != Other->getNumChildren())
      return true;

    if (Level != Other->Level) return true;

    SmallPtrSet<const NodeT *, 4> OtherChildren;
    for (const DomTreeNodeBase *I : *Other) {
      const NodeT *Nd = I->getBlock();
      OtherChildren.insert(Nd);
    }

    for (const DomTreeNodeBase *I : *this) {
      const NodeT *N = I->getBlock();
      if (OtherChildren.count(N) == 0)
        return true;
    }
    return false;
  }

  void setIDom(DomTreeNodeBase *NewIDom) {
    assert(IDom && "No immediate dominator?");
    if (IDom == NewIDom) return;

    auto I = find(IDom->Children, this);
    assert(I != IDom->Children.end() &&
           "Not in immediate dominator children set!");
    // I am no longer your child...
    IDom->Children.erase(I);

    // Switch to new dominator
    IDom = NewIDom;
    IDom->Children.push_back(this);

    UpdateLevel();
  }

  /// getDFSNumIn/getDFSNumOut - These return the DFS visitation order for nodes
  /// in the dominator tree. They are only guaranteed valid if
  /// updateDFSNumbers() has been called.
  unsigned getDFSNumIn() const { return DFSNumIn; }
  unsigned getDFSNumOut() const { return DFSNumOut; }

private:
  // Return true if this node is dominated by other. Use this only if DFS info
  // is valid.
  bool DominatedBy(const DomTreeNodeBase *other) const {
    return this->DFSNumIn >= other->DFSNumIn &&
           this->DFSNumOut <= other->DFSNumOut;
  }

  void UpdateLevel() {
    assert(IDom);
    if (Level == IDom->Level + 1) return;

    SmallVector<DomTreeNodeBase *, 64> WorkStack = {this};

    while (!WorkStack.empty()) {
      DomTreeNodeBase *Current = WorkStack.pop_back_val();
      Current->Level = Current->IDom->Level + 1;

      for (DomTreeNodeBase *C : *Current) {
        assert(C->IDom);
        if (C->Level != C->IDom->Level + 1) WorkStack.push_back(C);
      }
    }
  }
};

template <class NodeT>
raw_ostream &operator<<(raw_ostream &O, const DomTreeNodeBase<NodeT> *Node) {
  if (Node->getBlock())
    Node->getBlock()->printAsOperand(O, false);
  else
    O << " <<exit node>>";

  O << " {" << Node->getDFSNumIn() << "," << Node->getDFSNumOut() << "} ["
    << Node->getLevel() << "]\n";

  return O;
}

template <class NodeT>
void PrintDomTree(const DomTreeNodeBase<NodeT> *N, raw_ostream &O,
                  unsigned Lev) {
  O.indent(2 * Lev) << "[" << Lev << "] " << N;
  for (const auto &I : *N)
    PrintDomTree<NodeT>(I, O, Lev + 1);
}

namespace DomTreeBuilder {
// The routines below are provided in a separate header but referenced here.
template <typename DomTreeT>
void Calculate(DomTreeT &DT);

template <typename DomTreeT>
void CalculateWithUpdates(DomTreeT &DT,
                          ArrayRef<typename DomTreeT::UpdateType> Updates);

template <typename DomTreeT>
void InsertEdge(DomTreeT &DT, typename DomTreeT::NodePtr From,
                typename DomTreeT::NodePtr To);

template <typename DomTreeT>
void DeleteEdge(DomTreeT &DT, typename DomTreeT::NodePtr From,
                typename DomTreeT::NodePtr To);

template <typename DomTreeT>
void ApplyUpdates(DomTreeT &DT,
                  GraphDiff<typename DomTreeT::NodePtr,
                            DomTreeT::IsPostDominator> &PreViewCFG,
                  GraphDiff<typename DomTreeT::NodePtr,
                            DomTreeT::IsPostDominator> *PostViewCFG);

template <typename DomTreeT>
bool Verify(const DomTreeT &DT, typename DomTreeT::VerificationLevel VL);
}  // namespace DomTreeBuilder

/// Default DomTreeNode traits for NodeT. The default implementation assume a
/// Function-like NodeT. Can be specialized to support different node types.
template <typename NodeT> struct DomTreeNodeTraits {
  using NodeType = NodeT;
  using NodePtr = NodeT *;
  using ParentPtr = decltype(std::declval<NodePtr>()->getParent());
  static_assert(std::is_pointer_v<ParentPtr>,
                "Currently NodeT's parent must be a pointer type");
  using ParentType = std::remove_pointer_t<ParentPtr>;

  static NodeT *getEntryNode(ParentPtr Parent) { return &Parent->front(); }
  static ParentPtr getParent(NodePtr BB) { return BB->getParent(); }
};

/// Core dominator tree base class.
///
/// This class is a generic template over graph nodes. It is instantiated for
/// various graphs in the LLVM IR or in the code generator.
template <typename NodeT, bool IsPostDom>
class DominatorTreeBase {
 public:
  static_assert(std::is_pointer_v<typename GraphTraits<NodeT *>::NodeRef>,
                "Currently DominatorTreeBase supports only pointer nodes");
  using NodeTrait = DomTreeNodeTraits<NodeT>;
  using NodeType = typename NodeTrait::NodeType;
  using NodePtr = typename NodeTrait::NodePtr;
  using ParentPtr = typename NodeTrait::ParentPtr;
  static_assert(std::is_pointer_v<ParentPtr>,
                "Currently NodeT's parent must be a pointer type");
  using ParentType = std::remove_pointer_t<ParentPtr>;
  static constexpr bool IsPostDominator = IsPostDom;

  using UpdateType = cfg::Update<NodePtr>;
  using UpdateKind = cfg::UpdateKind;
  static constexpr UpdateKind Insert = UpdateKind::Insert;
  static constexpr UpdateKind Delete = UpdateKind::Delete;

  enum class VerificationLevel { Fast, Basic, Full };

protected:
  // Dominators always have a single root, postdominators can have more.
  SmallVector<NodeT *, IsPostDom ? 4 : 1> Roots;

  using DomTreeNodeMapType =
     DenseMap<NodeT *, std::unique_ptr<DomTreeNodeBase<NodeT>>>;
  DomTreeNodeMapType DomTreeNodes;
  DomTreeNodeBase<NodeT> *RootNode = nullptr;
  ParentPtr Parent = nullptr;

  mutable bool DFSInfoValid = false;
  mutable unsigned int SlowQueries = 0;

  friend struct DomTreeBuilder::SemiNCAInfo<DominatorTreeBase>;

 public:
  DominatorTreeBase() = default;

  DominatorTreeBase(DominatorTreeBase &&Arg)
      : Roots(std::move(Arg.Roots)),
        DomTreeNodes(std::move(Arg.DomTreeNodes)),
        RootNode(Arg.RootNode),
        Parent(Arg.Parent),
        DFSInfoValid(Arg.DFSInfoValid),
        SlowQueries(Arg.SlowQueries) {
    Arg.wipe();
  }

  DominatorTreeBase &operator=(DominatorTreeBase &&RHS) {
    Roots = std::move(RHS.Roots);
    DomTreeNodes = std::move(RHS.DomTreeNodes);
    RootNode = RHS.RootNode;
    Parent = RHS.Parent;
    DFSInfoValid = RHS.DFSInfoValid;
    SlowQueries = RHS.SlowQueries;
    RHS.wipe();
    return *this;
  }

  DominatorTreeBase(const DominatorTreeBase &) = delete;
  DominatorTreeBase &operator=(const DominatorTreeBase &) = delete;

  /// Iteration over roots.
  ///
  /// This may include multiple blocks if we are computing post dominators.
  /// For forward dominators, this will always be a single block (the entry
  /// block).
  using root_iterator = typename SmallVectorImpl<NodeT *>::iterator;
  using const_root_iterator = typename SmallVectorImpl<NodeT *>::const_iterator;

  root_iterator root_begin() { return Roots.begin(); }
  const_root_iterator root_begin() const { return Roots.begin(); }
  root_iterator root_end() { return Roots.end(); }
  const_root_iterator root_end() const { return Roots.end(); }

  size_t root_size() const { return Roots.size(); }

  iterator_range<root_iterator> roots() {
    return make_range(root_begin(), root_end());
  }
  iterator_range<const_root_iterator> roots() const {
    return make_range(root_begin(), root_end());
  }

  /// isPostDominator - Returns true if analysis based of postdoms
  ///
  bool isPostDominator() const { return IsPostDominator; }

  /// compare - Return false if the other dominator tree base matches this
  /// dominator tree base. Otherwise return true.
  bool compare(const DominatorTreeBase &Other) const {
    if (Parent != Other.Parent) return true;

    if (Roots.size() != Other.Roots.size())
      return true;

    if (!std::is_permutation(Roots.begin(), Roots.end(), Other.Roots.begin()))
      return true;

    const DomTreeNodeMapType &OtherDomTreeNodes = Other.DomTreeNodes;
    if (DomTreeNodes.size() != OtherDomTreeNodes.size())
      return true;

    for (const auto &DomTreeNode : DomTreeNodes) {
      NodeT *BB = DomTreeNode.first;
      typename DomTreeNodeMapType::const_iterator OI =
          OtherDomTreeNodes.find(BB);
      if (OI == OtherDomTreeNodes.end())
        return true;

      DomTreeNodeBase<NodeT> &MyNd = *DomTreeNode.second;
      DomTreeNodeBase<NodeT> &OtherNd = *OI->second;

      if (MyNd.compare(&OtherNd))
        return true;
    }

    return false;
  }

  /// getNode - return the (Post)DominatorTree node for the specified basic
  /// block.  This is the same as using operator[] on this class.  The result
  /// may (but is not required to) be null for a forward (backwards)
  /// statically unreachable block.
  DomTreeNodeBase<NodeT> *getNode(const NodeT *BB) const {
    auto I = DomTreeNodes.find(BB);
    if (I != DomTreeNodes.end())
      return I->second.get();
    return nullptr;
  }

  /// See getNode.
  DomTreeNodeBase<NodeT> *operator[](const NodeT *BB) const {
    return getNode(BB);
  }

  /// getRootNode - This returns the entry node for the CFG of the function.  If
  /// this tree represents the post-dominance relations for a function, however,
  /// this root may be a node with the block == NULL.  This is the case when
  /// there are multiple exit nodes from a particular function.  Consumers of
  /// post-dominance information must be capable of dealing with this
  /// possibility.
  ///
  DomTreeNodeBase<NodeT> *getRootNode() { return RootNode; }
  const DomTreeNodeBase<NodeT> *getRootNode() const { return RootNode; }

  /// Get all nodes dominated by R, including R itself.
  void getDescendants(NodeT *R, SmallVectorImpl<NodeT *> &Result) const {
    Result.clear();
    const DomTreeNodeBase<NodeT> *RN = getNode(R);
    if (!RN)
      return; // If R is unreachable, it will not be present in the DOM tree.
    SmallVector<const DomTreeNodeBase<NodeT> *, 8> WL;
    WL.push_back(RN);

    while (!WL.empty()) {
      const DomTreeNodeBase<NodeT> *N = WL.pop_back_val();
      Result.push_back(N->getBlock());
      WL.append(N->begin(), N->end());
    }
  }

  /// properlyDominates - Returns true iff A dominates B and A != B.
  /// Note that this is not a constant time operation!
  ///
  bool properlyDominates(const DomTreeNodeBase<NodeT> *A,
                         const DomTreeNodeBase<NodeT> *B) const {
    if (!A || !B)
      return false;
    if (A == B)
      return false;
    return dominates(A, B);
  }

  bool properlyDominates(const NodeT *A, const NodeT *B) const;

  /// isReachableFromEntry - Return true if A is dominated by the entry
  /// block of the function containing it.
  bool isReachableFromEntry(const NodeT *A) const {
    assert(!this->isPostDominator() &&
           "This is not implemented for post dominators");
    return isReachableFromEntry(getNode(const_cast<NodeT *>(A)));
  }

  bool isReachableFromEntry(const DomTreeNodeBase<NodeT> *A) const { return A; }

  /// dominates - Returns true iff A dominates B.  Note that this is not a
  /// constant time operation!
  ///
  bool dominates(const DomTreeNodeBase<NodeT> *A,
                 const DomTreeNodeBase<NodeT> *B) const {
    // A node trivially dominates itself.
    if (B == A)
      return true;

    // An unreachable node is dominated by anything.
    if (!isReachableFromEntry(B))
      return true;

    // And dominates nothing.
    if (!isReachableFromEntry(A))
      return false;

    if (B->getIDom() == A) return true;

    if (A->getIDom() == B) return false;

    // A can only dominate B if it is higher in the tree.
    if (A->getLevel() >= B->getLevel()) return false;

    // Compare the result of the tree walk and the dfs numbers, if expensive
    // checks are enabled.
#ifdef EXPENSIVE_CHECKS
    assert((!DFSInfoValid ||
            (dominatedBySlowTreeWalk(A, B) == B->DominatedBy(A))) &&
           "Tree walk disagrees with dfs numbers!");
#endif

    if (DFSInfoValid)
      return B->DominatedBy(A);

    // If we end up with too many slow queries, just update the
    // DFS numbers on the theory that we are going to keep querying.
    SlowQueries++;
    if (SlowQueries > 32) {
      updateDFSNumbers();
      return B->DominatedBy(A);
    }

    return dominatedBySlowTreeWalk(A, B);
  }

  bool dominates(const NodeT *A, const NodeT *B) const;

  NodeT *getRoot() const {
    assert(this->Roots.size() == 1 && "Should always have entry node!");
    return this->Roots[0];
  }

  /// Find nearest common dominator basic block for basic block A and B. A and B
  /// must have tree nodes.
  NodeT *findNearestCommonDominator(NodeT *A, NodeT *B) const {
    assert(A && B && "Pointers are not valid");
    assert(NodeTrait::getParent(A) == NodeTrait::getParent(B) &&
           "Two blocks are not in same function");

    // If either A or B is a entry block then it is nearest common dominator
    // (for forward-dominators).
    if (!isPostDominator()) {
      NodeT &Entry =
          *DomTreeNodeTraits<NodeT>::getEntryNode(NodeTrait::getParent(A));
      if (A == &Entry || B == &Entry)
        return &Entry;
    }

    DomTreeNodeBase<NodeT> *NodeA = getNode(A);
    DomTreeNodeBase<NodeT> *NodeB = getNode(B);
    assert(NodeA && "A must be in the tree");
    assert(NodeB && "B must be in the tree");

    // Use level information to go up the tree until the levels match. Then
    // continue going up til we arrive at the same node.
    while (NodeA != NodeB) {
      if (NodeA->getLevel() < NodeB->getLevel()) std::swap(NodeA, NodeB);

      NodeA = NodeA->IDom;
    }

    return NodeA->getBlock();
  }

  const NodeT *findNearestCommonDominator(const NodeT *A,
                                          const NodeT *B) const {
    // Cast away the const qualifiers here. This is ok since
    // const is re-introduced on the return type.
    return findNearestCommonDominator(const_cast<NodeT *>(A),
                                      const_cast<NodeT *>(B));
  }

  bool isVirtualRoot(const DomTreeNodeBase<NodeT> *A) const {
    return isPostDominator() && !A->getBlock();
  }

  //===--------------------------------------------------------------------===//
  // API to update (Post)DominatorTree information based on modifications to
  // the CFG...

  /// Inform the dominator tree about a sequence of CFG edge insertions and
  /// deletions and perform a batch update on the tree.
  ///
  /// This function should be used when there were multiple CFG updates after
  /// the last dominator tree update. It takes care of performing the updates
  /// in sync with the CFG and optimizes away the redundant operations that
  /// cancel each other.
  /// The functions expects the sequence of updates to be balanced. Eg.:
  ///  - {{Insert, A, B}, {Delete, A, B}, {Insert, A, B}} is fine, because
  ///    logically it results in a single insertions.
  ///  - {{Insert, A, B}, {Insert, A, B}} is invalid, because it doesn't make
  ///    sense to insert the same edge twice.
  ///
  /// What's more, the functions assumes that it's safe to ask every node in the
  /// CFG about its children and inverse children. This implies that deletions
  /// of CFG edges must not delete the CFG nodes before calling this function.
  ///
  /// The applyUpdates function can reorder the updates and remove redundant
  /// ones internally (as long as it is done in a deterministic fashion). The
  /// batch updater is also able to detect sequences of zero and exactly one
  /// update -- it's optimized to do less work in these cases.
  ///
  /// Note that for postdominators it automatically takes care of applying
  /// updates on reverse edges internally (so there's no need to swap the
  /// From and To pointers when constructing DominatorTree::UpdateType).
  /// The type of updates is the same for DomTreeBase<T> and PostDomTreeBase<T>
  /// with the same template parameter T.
  ///
  /// \param Updates An ordered sequence of updates to perform. The current CFG
  /// and the reverse of these updates provides the pre-view of the CFG.
  ///
  void applyUpdates(ArrayRef<UpdateType> Updates) {
    GraphDiff<NodePtr, IsPostDominator> PreViewCFG(
        Updates, /*ReverseApplyUpdates=*/true);
    DomTreeBuilder::ApplyUpdates(*this, PreViewCFG, nullptr);
  }

  /// \param Updates An ordered sequence of updates to perform. The current CFG
  /// and the reverse of these updates provides the pre-view of the CFG.
  /// \param PostViewUpdates An ordered sequence of update to perform in order
  /// to obtain a post-view of the CFG. The DT will be updated assuming the
  /// obtained PostViewCFG is the desired end state.
  void applyUpdates(ArrayRef<UpdateType> Updates,
                    ArrayRef<UpdateType> PostViewUpdates) {
    if (Updates.empty()) {
      GraphDiff<NodePtr, IsPostDom> PostViewCFG(PostViewUpdates);
      DomTreeBuilder::ApplyUpdates(*this, PostViewCFG, &PostViewCFG);
    } else {
      // PreViewCFG needs to merge Updates and PostViewCFG. The updates in
      // Updates need to be reversed, and match the direction in PostViewCFG.
      // The PostViewCFG is created with updates reversed (equivalent to changes
      // made to the CFG), so the PreViewCFG needs all the updates reverse
      // applied.
      SmallVector<UpdateType> AllUpdates(Updates.begin(), Updates.end());
      append_range(AllUpdates, PostViewUpdates);
      GraphDiff<NodePtr, IsPostDom> PreViewCFG(AllUpdates,
                                               /*ReverseApplyUpdates=*/true);
      GraphDiff<NodePtr, IsPostDom> PostViewCFG(PostViewUpdates);
      DomTreeBuilder::ApplyUpdates(*this, PreViewCFG, &PostViewCFG);
    }
  }

  /// Inform the dominator tree about a CFG edge insertion and update the tree.
  ///
  /// This function has to be called just before or just after making the update
  /// on the actual CFG. There cannot be any other updates that the dominator
  /// tree doesn't know about.
  ///
  /// Note that for postdominators it automatically takes care of inserting
  /// a reverse edge internally (so there's no need to swap the parameters).
  ///
  void insertEdge(NodeT *From, NodeT *To) {
    assert(From);
    assert(To);
    assert(NodeTrait::getParent(From) == Parent);
    assert(NodeTrait::getParent(To) == Parent);
    DomTreeBuilder::InsertEdge(*this, From, To);
  }

  /// Inform the dominator tree about a CFG edge deletion and update the tree.
  ///
  /// This function has to be called just after making the update on the actual
  /// CFG. An internal functions checks if the edge doesn't exist in the CFG in
  /// DEBUG mode. There cannot be any other updates that the
  /// dominator tree doesn't know about.
  ///
  /// Note that for postdominators it automatically takes care of deleting
  /// a reverse edge internally (so there's no need to swap the parameters).
  ///
  void deleteEdge(NodeT *From, NodeT *To) {
    assert(From);
    assert(To);
    assert(NodeTrait::getParent(From) == Parent);
    assert(NodeTrait::getParent(To) == Parent);
    DomTreeBuilder::DeleteEdge(*this, From, To);
  }

  /// Add a new node to the dominator tree information.
  ///
  /// This creates a new node as a child of DomBB dominator node, linking it
  /// into the children list of the immediate dominator.
  ///
  /// \param BB New node in CFG.
  /// \param DomBB CFG node that is dominator for BB.
  /// \returns New dominator tree node that represents new CFG node.
  ///
  DomTreeNodeBase<NodeT> *addNewBlock(NodeT *BB, NodeT *DomBB) {
    assert(getNode(BB) == nullptr && "Block already in dominator tree!");
    DomTreeNodeBase<NodeT> *IDomNode = getNode(DomBB);
    assert(IDomNode && "Not immediate dominator specified for block!");
    DFSInfoValid = false;
    return createChild(BB, IDomNode);
  }

  /// Add a new node to the forward dominator tree and make it a new root.
  ///
  /// \param BB New node in CFG.
  /// \returns New dominator tree node that represents new CFG node.
  ///
  DomTreeNodeBase<NodeT> *setNewRoot(NodeT *BB) {
    assert(getNode(BB) == nullptr && "Block already in dominator tree!");
    assert(!this->isPostDominator() &&
           "Cannot change root of post-dominator tree");
    DFSInfoValid = false;
    DomTreeNodeBase<NodeT> *NewNode = createNode(BB);
    if (Roots.empty()) {
      addRoot(BB);
    } else {
      assert(Roots.size() == 1);
      NodeT *OldRoot = Roots.front();
      auto &OldNode = DomTreeNodes[OldRoot];
      OldNode = NewNode->addChild(std::move(DomTreeNodes[OldRoot]));
      OldNode->IDom = NewNode;
      OldNode->UpdateLevel();
      Roots[0] = BB;
    }
    return RootNode = NewNode;
  }

  /// changeImmediateDominator - This method is used to update the dominator
  /// tree information when a node's immediate dominator changes.
  ///
  void changeImmediateDominator(DomTreeNodeBase<NodeT> *N,
                                DomTreeNodeBase<NodeT> *NewIDom) {
    assert(N && NewIDom && "Cannot change null node pointers!");
    DFSInfoValid = false;
    N->setIDom(NewIDom);
  }

  void changeImmediateDominator(NodeT *BB, NodeT *NewBB) {
    changeImmediateDominator(getNode(BB), getNode(NewBB));
  }

  /// eraseNode - Removes a node from the dominator tree. Block must not
  /// dominate any other blocks. Removes node from its immediate dominator's
  /// children list. Deletes dominator node associated with basic block BB.
  void eraseNode(NodeT *BB) {
    DomTreeNodeBase<NodeT> *Node = getNode(BB);
    assert(Node && "Removing node that isn't in dominator tree.");
    assert(Node->isLeaf() && "Node is not a leaf node.");

    DFSInfoValid = false;

    // Remove node from immediate dominator's children list.
    DomTreeNodeBase<NodeT> *IDom = Node->getIDom();
    if (IDom) {
      const auto I = find(IDom->Children, Node);
      assert(I != IDom->Children.end() &&
             "Not in immediate dominator children set!");
      // I am no longer your child...
      IDom->Children.erase(I);
    }

    DomTreeNodes.erase(BB);

    if (!IsPostDom) return;

    // Remember to update PostDominatorTree roots.
    auto RIt = llvm::find(Roots, BB);
    if (RIt != Roots.end()) {
      std::swap(*RIt, Roots.back());
      Roots.pop_back();
    }
  }

  /// splitBlock - BB is split and now it has one successor. Update dominator
  /// tree to reflect this change.
  void splitBlock(NodeT *NewBB) {
    if (IsPostDominator)
      Split<Inverse<NodeT *>>(NewBB);
    else
      Split<NodeT *>(NewBB);
  }

  /// print - Convert to human readable form
  ///
  void print(raw_ostream &O) const {
    O << "=============================--------------------------------\n";
    if (IsPostDominator)
      O << "Inorder PostDominator Tree: ";
    else
      O << "Inorder Dominator Tree: ";
    if (!DFSInfoValid)
      O << "DFSNumbers invalid: " << SlowQueries << " slow queries.";
    O << "\n";

    // The postdom tree can have a null root if there are no returns.
    if (getRootNode()) PrintDomTree<NodeT>(getRootNode(), O, 1);
    O << "Roots: ";
    for (const NodePtr Block : Roots) {
      Block->printAsOperand(O, false);
      O << " ";
    }
    O << "\n";
  }

public:
  /// updateDFSNumbers - Assign In and Out numbers to the nodes while walking
  /// dominator tree in dfs order.
  void updateDFSNumbers() const {
    if (DFSInfoValid) {
      SlowQueries = 0;
      return;
    }

    SmallVector<std::pair<const DomTreeNodeBase<NodeT> *,
                          typename DomTreeNodeBase<NodeT>::const_iterator>,
                32> WorkStack;

    const DomTreeNodeBase<NodeT> *ThisRoot = getRootNode();
    assert((!Parent || ThisRoot) && "Empty constructed DomTree");
    if (!ThisRoot)
      return;

    // Both dominators and postdominators have a single root node. In the case
    // case of PostDominatorTree, this node is a virtual root.
    WorkStack.push_back({ThisRoot, ThisRoot->begin()});

    unsigned DFSNum = 0;
    ThisRoot->DFSNumIn = DFSNum++;

    while (!WorkStack.empty()) {
      const DomTreeNodeBase<NodeT> *Node = WorkStack.back().first;
      const auto ChildIt = WorkStack.back().second;

      // If we visited all of the children of this node, "recurse" back up the
      // stack setting the DFOutNum.
      if (ChildIt == Node->end()) {
        Node->DFSNumOut = DFSNum++;
        WorkStack.pop_back();
      } else {
        // Otherwise, recursively visit this child.
        const DomTreeNodeBase<NodeT> *Child = *ChildIt;
        ++WorkStack.back().second;

        WorkStack.push_back({Child, Child->begin()});
        Child->DFSNumIn = DFSNum++;
      }
    }

    SlowQueries = 0;
    DFSInfoValid = true;
  }

  /// recalculate - compute a dominator tree for the given function
  void recalculate(ParentType &Func) {
    Parent = &Func;
    DomTreeBuilder::Calculate(*this);
  }

  void recalculate(ParentType &Func, ArrayRef<UpdateType> Updates) {
    Parent = &Func;
    DomTreeBuilder::CalculateWithUpdates(*this, Updates);
  }

  /// verify - checks if the tree is correct. There are 3 level of verification:
  ///  - Full --  verifies if the tree is correct by making sure all the
  ///             properties (including the parent and the sibling property)
  ///             hold.
  ///             Takes O(N^3) time.
  ///
  ///  - Basic -- checks if the tree is correct, but compares it to a freshly
  ///             constructed tree instead of checking the sibling property.
  ///             Takes O(N^2) time.
  ///
  ///  - Fast  -- checks basic tree structure and compares it with a freshly
  ///             constructed tree.
  ///             Takes O(N^2) time worst case, but is faster in practise (same
  ///             as tree construction).
  bool verify(VerificationLevel VL = VerificationLevel::Full) const {
    return DomTreeBuilder::Verify(*this, VL);
  }

  void reset() {
    DomTreeNodes.clear();
    Roots.clear();
    RootNode = nullptr;
    Parent = nullptr;
    DFSInfoValid = false;
    SlowQueries = 0;
  }

protected:
  void addRoot(NodeT *BB) { this->Roots.push_back(BB); }

  DomTreeNodeBase<NodeT> *createChild(NodeT *BB, DomTreeNodeBase<NodeT> *IDom) {
    return (DomTreeNodes[BB] = IDom->addChild(
                std::make_unique<DomTreeNodeBase<NodeT>>(BB, IDom)))
        .get();
  }

  DomTreeNodeBase<NodeT> *createNode(NodeT *BB) {
    return (DomTreeNodes[BB] =
                std::make_unique<DomTreeNodeBase<NodeT>>(BB, nullptr))
        .get();
  }

  // NewBB is split and now it has one successor. Update dominator tree to
  // reflect this change.
  template <class N>
  void Split(typename GraphTraits<N>::NodeRef NewBB) {
    using GraphT = GraphTraits<N>;
    using NodeRef = typename GraphT::NodeRef;
    assert(llvm::hasSingleElement(children<N>(NewBB)) &&
           "NewBB should have a single successor!");
    NodeRef NewBBSucc = *GraphT::child_begin(NewBB);

    SmallVector<NodeRef, 4> PredBlocks(inverse_children<N>(NewBB));

    assert(!PredBlocks.empty() && "No predblocks?");

    bool NewBBDominatesNewBBSucc = true;
    for (auto *Pred : inverse_children<N>(NewBBSucc)) {
      if (Pred != NewBB && !dominates(NewBBSucc, Pred) &&
          isReachableFromEntry(Pred)) {
        NewBBDominatesNewBBSucc = false;
        break;
      }
    }

    // Find NewBB's immediate dominator and create new dominator tree node for
    // NewBB.
    NodeT *NewBBIDom = nullptr;
    unsigned i = 0;
    for (i = 0; i < PredBlocks.size(); ++i)
      if (isReachableFromEntry(PredBlocks[i])) {
        NewBBIDom = PredBlocks[i];
        break;
      }

    // It's possible that none of the predecessors of NewBB are reachable;
    // in that case, NewBB itself is unreachable, so nothing needs to be
    // changed.
    if (!NewBBIDom) return;

    for (i = i + 1; i < PredBlocks.size(); ++i) {
      if (isReachableFromEntry(PredBlocks[i]))
        NewBBIDom = findNearestCommonDominator(NewBBIDom, PredBlocks[i]);
    }

    // Create the new dominator tree node... and set the idom of NewBB.
    DomTreeNodeBase<NodeT> *NewBBNode = addNewBlock(NewBB, NewBBIDom);

    // If NewBB strictly dominates other blocks, then it is now the immediate
    // dominator of NewBBSucc.  Update the dominator tree as appropriate.
    if (NewBBDominatesNewBBSucc) {
      DomTreeNodeBase<NodeT> *NewBBSuccNode = getNode(NewBBSucc);
      changeImmediateDominator(NewBBSuccNode, NewBBNode);
    }
  }

 private:
  bool dominatedBySlowTreeWalk(const DomTreeNodeBase<NodeT> *A,
                               const DomTreeNodeBase<NodeT> *B) const {
    assert(A != B);
    assert(isReachableFromEntry(B));
    assert(isReachableFromEntry(A));

    const unsigned ALevel = A->getLevel();
    const DomTreeNodeBase<NodeT> *IDom;

    // Don't walk nodes above A's subtree. When we reach A's level, we must
    // either find A or be in some other subtree not dominated by A.
    while ((IDom = B->getIDom()) != nullptr && IDom->getLevel() >= ALevel)
      B = IDom;  // Walk up the tree

    return B == A;
  }

  /// Wipe this tree's state without releasing any resources.
  ///
  /// This is essentially a post-move helper only. It leaves the object in an
  /// assignable and destroyable state, but otherwise invalid.
  void wipe() {
    DomTreeNodes.clear();
    RootNode = nullptr;
    Parent = nullptr;
  }
};

template <typename T>
using DomTreeBase = DominatorTreeBase<T, false>;

template <typename T>
using PostDomTreeBase = DominatorTreeBase<T, true>;

// These two functions are declared out of line as a workaround for building
// with old (< r147295) versions of clang because of pr11642.
template <typename NodeT, bool IsPostDom>
bool DominatorTreeBase<NodeT, IsPostDom>::dominates(const NodeT *A,
                                                    const NodeT *B) const {
  if (A == B)
    return true;

  // Cast away the const qualifiers here. This is ok since
  // this function doesn't actually return the values returned
  // from getNode.
  return dominates(getNode(const_cast<NodeT *>(A)),
                   getNode(const_cast<NodeT *>(B)));
}
template <typename NodeT, bool IsPostDom>
bool DominatorTreeBase<NodeT, IsPostDom>::properlyDominates(
    const NodeT *A, const NodeT *B) const {
  if (A == B)
    return false;

  // Cast away the const qualifiers here. This is ok since
  // this function doesn't actually return the values returned
  // from getNode.
  return dominates(getNode(const_cast<NodeT *>(A)),
                   getNode(const_cast<NodeT *>(B)));
}

} // end namespace llvm

#endif // LLVM_SUPPORT_GENERICDOMTREE_H
