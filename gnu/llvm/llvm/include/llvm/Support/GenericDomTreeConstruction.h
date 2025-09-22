//===- GenericDomTreeConstruction.h - Dominator Calculation ------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Generic dominator tree construction - this file provides routines to
/// construct immediate dominator information for a flow-graph based on the
/// Semi-NCA algorithm described in this dissertation:
///
///   [1] Linear-Time Algorithms for Dominators and Related Problems
///   Loukas Georgiadis, Princeton University, November 2005, pp. 21-23:
///   ftp://ftp.cs.princeton.edu/reports/2005/737.pdf
///
/// Semi-NCA algorithm runs in O(n^2) worst-case time but usually slightly
/// faster than Simple Lengauer-Tarjan in practice.
///
/// O(n^2) worst cases happen when the computation of nearest common ancestors
/// requires O(n) average time, which is very unlikely in real world. If this
/// ever turns out to be an issue, consider implementing a hybrid algorithm
/// that uses SLT to perform full constructions and SemiNCA for incremental
/// updates.
///
/// The file uses the Depth Based Search algorithm to perform incremental
/// updates (insertion and deletions). The implemented algorithm is based on
/// this publication:
///
///   [2] An Experimental Study of Dynamic Dominators
///   Loukas Georgiadis, et al., April 12 2016, pp. 5-7, 9-10:
///   https://arxiv.org/pdf/1604.02711.pdf
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GENERICDOMTREECONSTRUCTION_H
#define LLVM_SUPPORT_GENERICDOMTREECONSTRUCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GenericDomTree.h"
#include <optional>
#include <queue>

#define DEBUG_TYPE "dom-tree-builder"

namespace llvm {
namespace DomTreeBuilder {

template <typename DomTreeT>
struct SemiNCAInfo {
  using NodePtr = typename DomTreeT::NodePtr;
  using NodeT = typename DomTreeT::NodeType;
  using TreeNodePtr = DomTreeNodeBase<NodeT> *;
  using RootsT = decltype(DomTreeT::Roots);
  static constexpr bool IsPostDom = DomTreeT::IsPostDominator;
  using GraphDiffT = GraphDiff<NodePtr, IsPostDom>;

  // Information record used by Semi-NCA during tree construction.
  struct InfoRec {
    unsigned DFSNum = 0;
    unsigned Parent = 0;
    unsigned Semi = 0;
    unsigned Label = 0;
    NodePtr IDom = nullptr;
    SmallVector<unsigned, 4> ReverseChildren;
  };

  // Number to node mapping is 1-based. Initialize the mapping to start with
  // a dummy element.
  SmallVector<NodePtr, 64> NumToNode = {nullptr};
  DenseMap<NodePtr, InfoRec> NodeToInfo;

  using UpdateT = typename DomTreeT::UpdateType;
  using UpdateKind = typename DomTreeT::UpdateKind;
  struct BatchUpdateInfo {
    // Note: Updates inside PreViewCFG are already legalized.
    BatchUpdateInfo(GraphDiffT &PreViewCFG, GraphDiffT *PostViewCFG = nullptr)
        : PreViewCFG(PreViewCFG), PostViewCFG(PostViewCFG),
          NumLegalized(PreViewCFG.getNumLegalizedUpdates()) {}

    // Remembers if the whole tree was recalculated at some point during the
    // current batch update.
    bool IsRecalculated = false;
    GraphDiffT &PreViewCFG;
    GraphDiffT *PostViewCFG;
    const size_t NumLegalized;
  };

  BatchUpdateInfo *BatchUpdates;
  using BatchUpdatePtr = BatchUpdateInfo *;

  // If BUI is a nullptr, then there's no batch update in progress.
  SemiNCAInfo(BatchUpdatePtr BUI) : BatchUpdates(BUI) {}

  void clear() {
    NumToNode = {nullptr}; // Restore to initial state with a dummy start node.
    NodeToInfo.clear();
    // Don't reset the pointer to BatchUpdateInfo here -- if there's an update
    // in progress, we need this information to continue it.
  }

  template <bool Inversed>
  static SmallVector<NodePtr, 8> getChildren(NodePtr N, BatchUpdatePtr BUI) {
    if (BUI)
      return BUI->PreViewCFG.template getChildren<Inversed>(N);
    return getChildren<Inversed>(N);
  }

  template <bool Inversed>
  static SmallVector<NodePtr, 8> getChildren(NodePtr N) {
    using DirectedNodeT =
        std::conditional_t<Inversed, Inverse<NodePtr>, NodePtr>;
    auto R = children<DirectedNodeT>(N);
    SmallVector<NodePtr, 8> Res(detail::reverse_if<!Inversed>(R));

    // Remove nullptr children for clang.
    llvm::erase(Res, nullptr);
    return Res;
  }

  NodePtr getIDom(NodePtr BB) const {
    auto InfoIt = NodeToInfo.find(BB);
    if (InfoIt == NodeToInfo.end()) return nullptr;

    return InfoIt->second.IDom;
  }

  TreeNodePtr getNodeForBlock(NodePtr BB, DomTreeT &DT) {
    if (TreeNodePtr Node = DT.getNode(BB)) return Node;

    // Haven't calculated this node yet?  Get or calculate the node for the
    // immediate dominator.
    NodePtr IDom = getIDom(BB);

    assert(IDom || DT.DomTreeNodes[nullptr]);
    TreeNodePtr IDomNode = getNodeForBlock(IDom, DT);

    // Add a new tree node for this NodeT, and link it as a child of
    // IDomNode
    return DT.createChild(BB, IDomNode);
  }

  static bool AlwaysDescend(NodePtr, NodePtr) { return true; }

  struct BlockNamePrinter {
    NodePtr N;

    BlockNamePrinter(NodePtr Block) : N(Block) {}
    BlockNamePrinter(TreeNodePtr TN) : N(TN ? TN->getBlock() : nullptr) {}

    friend raw_ostream &operator<<(raw_ostream &O, const BlockNamePrinter &BP) {
      if (!BP.N)
        O << "nullptr";
      else
        BP.N->printAsOperand(O, false);

      return O;
    }
  };

  using NodeOrderMap = DenseMap<NodePtr, unsigned>;

  // Custom DFS implementation which can skip nodes based on a provided
  // predicate. It also collects ReverseChildren so that we don't have to spend
  // time getting predecessors in SemiNCA.
  //
  // If IsReverse is set to true, the DFS walk will be performed backwards
  // relative to IsPostDom -- using reverse edges for dominators and forward
  // edges for postdominators.
  //
  // If SuccOrder is specified then in this order the DFS traverses the children
  // otherwise the order is implied by the results of getChildren().
  template <bool IsReverse = false, typename DescendCondition>
  unsigned runDFS(NodePtr V, unsigned LastNum, DescendCondition Condition,
                  unsigned AttachToNum,
                  const NodeOrderMap *SuccOrder = nullptr) {
    assert(V);
    SmallVector<std::pair<NodePtr, unsigned>, 64> WorkList = {{V, AttachToNum}};
    NodeToInfo[V].Parent = AttachToNum;

    while (!WorkList.empty()) {
      const auto [BB, ParentNum] = WorkList.pop_back_val();
      auto &BBInfo = NodeToInfo[BB];
      BBInfo.ReverseChildren.push_back(ParentNum);

      // Visited nodes always have positive DFS numbers.
      if (BBInfo.DFSNum != 0) continue;
      BBInfo.Parent = ParentNum;
      BBInfo.DFSNum = BBInfo.Semi = BBInfo.Label = ++LastNum;
      NumToNode.push_back(BB);

      constexpr bool Direction = IsReverse != IsPostDom;  // XOR.
      auto Successors = getChildren<Direction>(BB, BatchUpdates);
      if (SuccOrder && Successors.size() > 1)
        llvm::sort(
            Successors.begin(), Successors.end(), [=](NodePtr A, NodePtr B) {
              return SuccOrder->find(A)->second < SuccOrder->find(B)->second;
            });

      for (const NodePtr Succ : Successors) {
        if (!Condition(BB, Succ)) continue;

        WorkList.push_back({Succ, LastNum});
      }
    }

    return LastNum;
  }

  // V is a predecessor of W. eval() returns V if V < W, otherwise the minimum
  // of sdom(U), where U > W and there is a virtual forest path from U to V. The
  // virtual forest consists of linked edges of processed vertices.
  //
  // We can follow Parent pointers (virtual forest edges) to determine the
  // ancestor U with minimum sdom(U). But it is slow and thus we employ the path
  // compression technique to speed up to O(m*log(n)). Theoretically the virtual
  // forest can be organized as balanced trees to achieve almost linear
  // O(m*alpha(m,n)) running time. But it requires two auxiliary arrays (Size
  // and Child) and is unlikely to be faster than the simple implementation.
  //
  // For each vertex V, its Label points to the vertex with the minimal sdom(U)
  // (Semi) in its path from V (included) to NodeToInfo[V].Parent (excluded).
  unsigned eval(unsigned V, unsigned LastLinked,
                SmallVectorImpl<InfoRec *> &Stack,
                ArrayRef<InfoRec *> NumToInfo) {
    InfoRec *VInfo = NumToInfo[V];
    if (VInfo->Parent < LastLinked)
      return VInfo->Label;

    // Store ancestors except the last (root of a virtual tree) into a stack.
    assert(Stack.empty());
    do {
      Stack.push_back(VInfo);
      VInfo = NumToInfo[VInfo->Parent];
    } while (VInfo->Parent >= LastLinked);

    // Path compression. Point each vertex's Parent to the root and update its
    // Label if any of its ancestors (PInfo->Label) has a smaller Semi.
    const InfoRec *PInfo = VInfo;
    const InfoRec *PLabelInfo = NumToInfo[PInfo->Label];
    do {
      VInfo = Stack.pop_back_val();
      VInfo->Parent = PInfo->Parent;
      const InfoRec *VLabelInfo = NumToInfo[VInfo->Label];
      if (PLabelInfo->Semi < VLabelInfo->Semi)
        VInfo->Label = PInfo->Label;
      else
        PLabelInfo = VLabelInfo;
      PInfo = VInfo;
    } while (!Stack.empty());
    return VInfo->Label;
  }

  // This function requires DFS to be run before calling it.
  void runSemiNCA() {
    const unsigned NextDFSNum(NumToNode.size());
    SmallVector<InfoRec *, 8> NumToInfo = {nullptr};
    NumToInfo.reserve(NextDFSNum);
    // Initialize IDoms to spanning tree parents.
    for (unsigned i = 1; i < NextDFSNum; ++i) {
      const NodePtr V = NumToNode[i];
      auto &VInfo = NodeToInfo[V];
      VInfo.IDom = NumToNode[VInfo.Parent];
      NumToInfo.push_back(&VInfo);
    }

    // Step #1: Calculate the semidominators of all vertices.
    SmallVector<InfoRec *, 32> EvalStack;
    for (unsigned i = NextDFSNum - 1; i >= 2; --i) {
      auto &WInfo = *NumToInfo[i];

      // Initialize the semi dominator to point to the parent node.
      WInfo.Semi = WInfo.Parent;
      for (unsigned N : WInfo.ReverseChildren) {
        unsigned SemiU = NumToInfo[eval(N, i + 1, EvalStack, NumToInfo)]->Semi;
        if (SemiU < WInfo.Semi) WInfo.Semi = SemiU;
      }
    }

    // Step #2: Explicitly define the immediate dominator of each vertex.
    //          IDom[i] = NCA(SDom[i], SpanningTreeParent(i)).
    // Note that the parents were stored in IDoms and later got invalidated
    // during path compression in Eval.
    for (unsigned i = 2; i < NextDFSNum; ++i) {
      auto &WInfo = *NumToInfo[i];
      assert(WInfo.Semi != 0);
      const unsigned SDomNum = NumToInfo[WInfo.Semi]->DFSNum;
      NodePtr WIDomCandidate = WInfo.IDom;
      while (true) {
        auto &WIDomCandidateInfo = NodeToInfo.find(WIDomCandidate)->second;
        if (WIDomCandidateInfo.DFSNum <= SDomNum)
          break;
        WIDomCandidate = WIDomCandidateInfo.IDom;
      }

      WInfo.IDom = WIDomCandidate;
    }
  }

  // PostDominatorTree always has a virtual root that represents a virtual CFG
  // node that serves as a single exit from the function. All the other exits
  // (CFG nodes with terminators and nodes in infinite loops are logically
  // connected to this virtual CFG exit node).
  // This functions maps a nullptr CFG node to the virtual root tree node.
  void addVirtualRoot() {
    assert(IsPostDom && "Only postdominators have a virtual root");
    assert(NumToNode.size() == 1 && "SNCAInfo must be freshly constructed");

    auto &BBInfo = NodeToInfo[nullptr];
    BBInfo.DFSNum = BBInfo.Semi = BBInfo.Label = 1;

    NumToNode.push_back(nullptr);  // NumToNode[1] = nullptr;
  }

  // For postdominators, nodes with no forward successors are trivial roots that
  // are always selected as tree roots. Roots with forward successors correspond
  // to CFG nodes within infinite loops.
  static bool HasForwardSuccessors(const NodePtr N, BatchUpdatePtr BUI) {
    assert(N && "N must be a valid node");
    return !getChildren<false>(N, BUI).empty();
  }

  static NodePtr GetEntryNode(const DomTreeT &DT) {
    assert(DT.Parent && "Parent not set");
    return GraphTraits<typename DomTreeT::ParentPtr>::getEntryNode(DT.Parent);
  }

  // Finds all roots without relaying on the set of roots already stored in the
  // tree.
  // We define roots to be some non-redundant set of the CFG nodes
  static RootsT FindRoots(const DomTreeT &DT, BatchUpdatePtr BUI) {
    assert(DT.Parent && "Parent pointer is not set");
    RootsT Roots;

    // For dominators, function entry CFG node is always a tree root node.
    if (!IsPostDom) {
      Roots.push_back(GetEntryNode(DT));
      return Roots;
    }

    SemiNCAInfo SNCA(BUI);

    // PostDominatorTree always has a virtual root.
    SNCA.addVirtualRoot();
    unsigned Num = 1;

    LLVM_DEBUG(dbgs() << "\t\tLooking for trivial roots\n");

    // Step #1: Find all the trivial roots that are going to will definitely
    // remain tree roots.
    unsigned Total = 0;
    // It may happen that there are some new nodes in the CFG that are result of
    // the ongoing batch update, but we cannot really pretend that they don't
    // exist -- we won't see any outgoing or incoming edges to them, so it's
    // fine to discover them here, as they would end up appearing in the CFG at
    // some point anyway.
    for (const NodePtr N : nodes(DT.Parent)) {
      ++Total;
      // If it has no *successors*, it is definitely a root.
      if (!HasForwardSuccessors(N, BUI)) {
        Roots.push_back(N);
        // Run DFS not to walk this part of CFG later.
        Num = SNCA.runDFS(N, Num, AlwaysDescend, 1);
        LLVM_DEBUG(dbgs() << "Found a new trivial root: " << BlockNamePrinter(N)
                          << "\n");
        LLVM_DEBUG(dbgs() << "Last visited node: "
                          << BlockNamePrinter(SNCA.NumToNode[Num]) << "\n");
      }
    }

    LLVM_DEBUG(dbgs() << "\t\tLooking for non-trivial roots\n");

    // Step #2: Find all non-trivial root candidates. Those are CFG nodes that
    // are reverse-unreachable were not visited by previous DFS walks (i.e. CFG
    // nodes in infinite loops).
    bool HasNonTrivialRoots = false;
    // Accounting for the virtual exit, see if we had any reverse-unreachable
    // nodes.
    if (Total + 1 != Num) {
      HasNonTrivialRoots = true;

      // SuccOrder is the order of blocks in the function. It is needed to make
      // the calculation of the FurthestAway node and the whole PostDomTree
      // immune to swap successors transformation (e.g. canonicalizing branch
      // predicates). SuccOrder is initialized lazily only for successors of
      // reverse unreachable nodes.
      std::optional<NodeOrderMap> SuccOrder;
      auto InitSuccOrderOnce = [&]() {
        SuccOrder = NodeOrderMap();
        for (const auto Node : nodes(DT.Parent))
          if (SNCA.NodeToInfo.count(Node) == 0)
            for (const auto Succ : getChildren<false>(Node, SNCA.BatchUpdates))
              SuccOrder->try_emplace(Succ, 0);

        // Add mapping for all entries of SuccOrder.
        unsigned NodeNum = 0;
        for (const auto Node : nodes(DT.Parent)) {
          ++NodeNum;
          auto Order = SuccOrder->find(Node);
          if (Order != SuccOrder->end()) {
            assert(Order->second == 0);
            Order->second = NodeNum;
          }
        }
      };

      // Make another DFS pass over all other nodes to find the
      // reverse-unreachable blocks, and find the furthest paths we'll be able
      // to make.
      // Note that this looks N^2, but it's really 2N worst case, if every node
      // is unreachable. This is because we are still going to only visit each
      // unreachable node once, we may just visit it in two directions,
      // depending on how lucky we get.
      for (const NodePtr I : nodes(DT.Parent)) {
        if (SNCA.NodeToInfo.count(I) == 0) {
          LLVM_DEBUG(dbgs()
                     << "\t\t\tVisiting node " << BlockNamePrinter(I) << "\n");
          // Find the furthest away we can get by following successors, then
          // follow them in reverse.  This gives us some reasonable answer about
          // the post-dom tree inside any infinite loop. In particular, it
          // guarantees we get to the farthest away point along *some*
          // path. This also matches the GCC's behavior.
          // If we really wanted a totally complete picture of dominance inside
          // this infinite loop, we could do it with SCC-like algorithms to find
          // the lowest and highest points in the infinite loop.  In theory, it
          // would be nice to give the canonical backedge for the loop, but it's
          // expensive and does not always lead to a minimal set of roots.
          LLVM_DEBUG(dbgs() << "\t\t\tRunning forward DFS\n");

          if (!SuccOrder)
            InitSuccOrderOnce();
          assert(SuccOrder);

          const unsigned NewNum =
              SNCA.runDFS<true>(I, Num, AlwaysDescend, Num, &*SuccOrder);
          const NodePtr FurthestAway = SNCA.NumToNode[NewNum];
          LLVM_DEBUG(dbgs() << "\t\t\tFound a new furthest away node "
                            << "(non-trivial root): "
                            << BlockNamePrinter(FurthestAway) << "\n");
          Roots.push_back(FurthestAway);
          LLVM_DEBUG(dbgs() << "\t\t\tPrev DFSNum: " << Num << ", new DFSNum: "
                            << NewNum << "\n\t\t\tRemoving DFS info\n");
          for (unsigned i = NewNum; i > Num; --i) {
            const NodePtr N = SNCA.NumToNode[i];
            LLVM_DEBUG(dbgs() << "\t\t\t\tRemoving DFS info for "
                              << BlockNamePrinter(N) << "\n");
            SNCA.NodeToInfo.erase(N);
            SNCA.NumToNode.pop_back();
          }
          const unsigned PrevNum = Num;
          LLVM_DEBUG(dbgs() << "\t\t\tRunning reverse DFS\n");
          Num = SNCA.runDFS(FurthestAway, Num, AlwaysDescend, 1);
          for (unsigned i = PrevNum + 1; i <= Num; ++i)
            LLVM_DEBUG(dbgs() << "\t\t\t\tfound node "
                              << BlockNamePrinter(SNCA.NumToNode[i]) << "\n");
        }
      }
    }

    LLVM_DEBUG(dbgs() << "Total: " << Total << ", Num: " << Num << "\n");
    LLVM_DEBUG(dbgs() << "Discovered CFG nodes:\n");
    LLVM_DEBUG(for (size_t i = 0; i <= Num; ++i) dbgs()
               << i << ": " << BlockNamePrinter(SNCA.NumToNode[i]) << "\n");

    assert((Total + 1 == Num) && "Everything should have been visited");

    // Step #3: If we found some non-trivial roots, make them non-redundant.
    if (HasNonTrivialRoots) RemoveRedundantRoots(DT, BUI, Roots);

    LLVM_DEBUG(dbgs() << "Found roots: ");
    LLVM_DEBUG(for (auto *Root
                    : Roots) dbgs()
               << BlockNamePrinter(Root) << " ");
    LLVM_DEBUG(dbgs() << "\n");

    return Roots;
  }

  // This function only makes sense for postdominators.
  // We define roots to be some set of CFG nodes where (reverse) DFS walks have
  // to start in order to visit all the CFG nodes (including the
  // reverse-unreachable ones).
  // When the search for non-trivial roots is done it may happen that some of
  // the non-trivial roots are reverse-reachable from other non-trivial roots,
  // which makes them redundant. This function removes them from the set of
  // input roots.
  static void RemoveRedundantRoots(const DomTreeT &DT, BatchUpdatePtr BUI,
                                   RootsT &Roots) {
    assert(IsPostDom && "This function is for postdominators only");
    LLVM_DEBUG(dbgs() << "Removing redundant roots\n");

    SemiNCAInfo SNCA(BUI);

    for (unsigned i = 0; i < Roots.size(); ++i) {
      auto &Root = Roots[i];
      // Trivial roots are always non-redundant.
      if (!HasForwardSuccessors(Root, BUI)) continue;
      LLVM_DEBUG(dbgs() << "\tChecking if " << BlockNamePrinter(Root)
                        << " remains a root\n");
      SNCA.clear();
      // Do a forward walk looking for the other roots.
      const unsigned Num = SNCA.runDFS<true>(Root, 0, AlwaysDescend, 0);
      // Skip the start node and begin from the second one (note that DFS uses
      // 1-based indexing).
      for (unsigned x = 2; x <= Num; ++x) {
        const NodePtr N = SNCA.NumToNode[x];
        // If we wound another root in a (forward) DFS walk, remove the current
        // root from the set of roots, as it is reverse-reachable from the other
        // one.
        if (llvm::is_contained(Roots, N)) {
          LLVM_DEBUG(dbgs() << "\tForward DFS walk found another root "
                            << BlockNamePrinter(N) << "\n\tRemoving root "
                            << BlockNamePrinter(Root) << "\n");
          std::swap(Root, Roots.back());
          Roots.pop_back();

          // Root at the back takes the current root's place.
          // Start the next loop iteration with the same index.
          --i;
          break;
        }
      }
    }
  }

  template <typename DescendCondition>
  void doFullDFSWalk(const DomTreeT &DT, DescendCondition DC) {
    if (!IsPostDom) {
      assert(DT.Roots.size() == 1 && "Dominators should have a singe root");
      runDFS(DT.Roots[0], 0, DC, 0);
      return;
    }

    addVirtualRoot();
    unsigned Num = 1;
    for (const NodePtr Root : DT.Roots) Num = runDFS(Root, Num, DC, 1);
  }

  static void CalculateFromScratch(DomTreeT &DT, BatchUpdatePtr BUI) {
    auto *Parent = DT.Parent;
    DT.reset();
    DT.Parent = Parent;
    // If the update is using the actual CFG, BUI is null. If it's using a view,
    // BUI is non-null and the PreCFGView is used. When calculating from
    // scratch, make the PreViewCFG equal to the PostCFGView, so Post is used.
    BatchUpdatePtr PostViewBUI = nullptr;
    if (BUI && BUI->PostViewCFG) {
      BUI->PreViewCFG = *BUI->PostViewCFG;
      PostViewBUI = BUI;
    }
    // This is rebuilding the whole tree, not incrementally, but PostViewBUI is
    // used in case the caller needs a DT update with a CFGView.
    SemiNCAInfo SNCA(PostViewBUI);

    // Step #0: Number blocks in depth-first order and initialize variables used
    // in later stages of the algorithm.
    DT.Roots = FindRoots(DT, PostViewBUI);
    SNCA.doFullDFSWalk(DT, AlwaysDescend);

    SNCA.runSemiNCA();
    if (BUI) {
      BUI->IsRecalculated = true;
      LLVM_DEBUG(
          dbgs() << "DomTree recalculated, skipping future batch updates\n");
    }

    if (DT.Roots.empty()) return;

    // Add a node for the root. If the tree is a PostDominatorTree it will be
    // the virtual exit (denoted by (BasicBlock *) nullptr) which postdominates
    // all real exits (including multiple exit blocks, infinite loops).
    NodePtr Root = IsPostDom ? nullptr : DT.Roots[0];

    DT.RootNode = DT.createNode(Root);
    SNCA.attachNewSubtree(DT, DT.RootNode);
  }

  void attachNewSubtree(DomTreeT& DT, const TreeNodePtr AttachTo) {
    // Attach the first unreachable block to AttachTo.
    NodeToInfo[NumToNode[1]].IDom = AttachTo->getBlock();
    // Loop over all of the discovered blocks in the function...
    for (NodePtr W : llvm::drop_begin(NumToNode)) {
      // Don't replace this with 'count', the insertion side effect is important
      if (DT.DomTreeNodes[W]) continue;  // Haven't calculated this node yet?

      NodePtr ImmDom = getIDom(W);

      // Get or calculate the node for the immediate dominator.
      TreeNodePtr IDomNode = getNodeForBlock(ImmDom, DT);

      // Add a new tree node for this BasicBlock, and link it as a child of
      // IDomNode.
      DT.createChild(W, IDomNode);
    }
  }

  void reattachExistingSubtree(DomTreeT &DT, const TreeNodePtr AttachTo) {
    NodeToInfo[NumToNode[1]].IDom = AttachTo->getBlock();
    for (const NodePtr N : llvm::drop_begin(NumToNode)) {
      const TreeNodePtr TN = DT.getNode(N);
      assert(TN);
      const TreeNodePtr NewIDom = DT.getNode(NodeToInfo[N].IDom);
      TN->setIDom(NewIDom);
    }
  }

  // Helper struct used during edge insertions.
  struct InsertionInfo {
    struct Compare {
      bool operator()(TreeNodePtr LHS, TreeNodePtr RHS) const {
        return LHS->getLevel() < RHS->getLevel();
      }
    };

    // Bucket queue of tree nodes ordered by descending level. For simplicity,
    // we use a priority_queue here.
    std::priority_queue<TreeNodePtr, SmallVector<TreeNodePtr, 8>,
                        Compare>
        Bucket;
    SmallDenseSet<TreeNodePtr, 8> Visited;
    SmallVector<TreeNodePtr, 8> Affected;
#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
    SmallVector<TreeNodePtr, 8> VisitedUnaffected;
#endif
  };

  static void InsertEdge(DomTreeT &DT, const BatchUpdatePtr BUI,
                         const NodePtr From, const NodePtr To) {
    assert((From || IsPostDom) &&
           "From has to be a valid CFG node or a virtual root");
    assert(To && "Cannot be a nullptr");
    LLVM_DEBUG(dbgs() << "Inserting edge " << BlockNamePrinter(From) << " -> "
                      << BlockNamePrinter(To) << "\n");
    TreeNodePtr FromTN = DT.getNode(From);

    if (!FromTN) {
      // Ignore edges from unreachable nodes for (forward) dominators.
      if (!IsPostDom) return;

      // The unreachable node becomes a new root -- a tree node for it.
      TreeNodePtr VirtualRoot = DT.getNode(nullptr);
      FromTN = DT.createChild(From, VirtualRoot);
      DT.Roots.push_back(From);
    }

    DT.DFSInfoValid = false;

    const TreeNodePtr ToTN = DT.getNode(To);
    if (!ToTN)
      InsertUnreachable(DT, BUI, FromTN, To);
    else
      InsertReachable(DT, BUI, FromTN, ToTN);
  }

  // Determines if some existing root becomes reverse-reachable after the
  // insertion. Rebuilds the whole tree if that situation happens.
  static bool UpdateRootsBeforeInsertion(DomTreeT &DT, const BatchUpdatePtr BUI,
                                         const TreeNodePtr From,
                                         const TreeNodePtr To) {
    assert(IsPostDom && "This function is only for postdominators");
    // Destination node is not attached to the virtual root, so it cannot be a
    // root.
    if (!DT.isVirtualRoot(To->getIDom())) return false;

    if (!llvm::is_contained(DT.Roots, To->getBlock()))
      return false;  // To is not a root, nothing to update.

    LLVM_DEBUG(dbgs() << "\t\tAfter the insertion, " << BlockNamePrinter(To)
                      << " is no longer a root\n\t\tRebuilding the tree!!!\n");

    CalculateFromScratch(DT, BUI);
    return true;
  }

  static bool isPermutation(const SmallVectorImpl<NodePtr> &A,
                            const SmallVectorImpl<NodePtr> &B) {
    if (A.size() != B.size())
      return false;
    SmallPtrSet<NodePtr, 4> Set(A.begin(), A.end());
    for (NodePtr N : B)
      if (Set.count(N) == 0)
        return false;
    return true;
  }

  // Updates the set of roots after insertion or deletion. This ensures that
  // roots are the same when after a series of updates and when the tree would
  // be built from scratch.
  static void UpdateRootsAfterUpdate(DomTreeT &DT, const BatchUpdatePtr BUI) {
    assert(IsPostDom && "This function is only for postdominators");

    // The tree has only trivial roots -- nothing to update.
    if (llvm::none_of(DT.Roots, [BUI](const NodePtr N) {
          return HasForwardSuccessors(N, BUI);
        }))
      return;

    // Recalculate the set of roots.
    RootsT Roots = FindRoots(DT, BUI);
    if (!isPermutation(DT.Roots, Roots)) {
      // The roots chosen in the CFG have changed. This is because the
      // incremental algorithm does not really know or use the set of roots and
      // can make a different (implicit) decision about which node within an
      // infinite loop becomes a root.

      LLVM_DEBUG(dbgs() << "Roots are different in updated trees\n"
                        << "The entire tree needs to be rebuilt\n");
      // It may be possible to update the tree without recalculating it, but
      // we do not know yet how to do it, and it happens rarely in practice.
      CalculateFromScratch(DT, BUI);
    }
  }

  // Handles insertion to a node already in the dominator tree.
  static void InsertReachable(DomTreeT &DT, const BatchUpdatePtr BUI,
                              const TreeNodePtr From, const TreeNodePtr To) {
    LLVM_DEBUG(dbgs() << "\tReachable " << BlockNamePrinter(From->getBlock())
                      << " -> " << BlockNamePrinter(To->getBlock()) << "\n");
    if (IsPostDom && UpdateRootsBeforeInsertion(DT, BUI, From, To)) return;
    // DT.findNCD expects both pointers to be valid. When From is a virtual
    // root, then its CFG block pointer is a nullptr, so we have to 'compute'
    // the NCD manually.
    const NodePtr NCDBlock =
        (From->getBlock() && To->getBlock())
            ? DT.findNearestCommonDominator(From->getBlock(), To->getBlock())
            : nullptr;
    assert(NCDBlock || DT.isPostDominator());
    const TreeNodePtr NCD = DT.getNode(NCDBlock);
    assert(NCD);

    LLVM_DEBUG(dbgs() << "\t\tNCA == " << BlockNamePrinter(NCD) << "\n");
    const unsigned NCDLevel = NCD->getLevel();

    // Based on Lemma 2.5 from [2], after insertion of (From,To), v is affected
    // iff depth(NCD)+1 < depth(v) && a path P from To to v exists where every
    // w on P s.t. depth(v) <= depth(w)
    //
    // This reduces to a widest path problem (maximizing the depth of the
    // minimum vertex in the path) which can be solved by a modified version of
    // Dijkstra with a bucket queue (named depth-based search in [2]).

    // To is in the path, so depth(NCD)+1 < depth(v) <= depth(To). Nothing
    // affected if this does not hold.
    if (NCDLevel + 1 >= To->getLevel())
      return;

    InsertionInfo II;
    SmallVector<TreeNodePtr, 8> UnaffectedOnCurrentLevel;
    II.Bucket.push(To);
    II.Visited.insert(To);

    while (!II.Bucket.empty()) {
      TreeNodePtr TN = II.Bucket.top();
      II.Bucket.pop();
      II.Affected.push_back(TN);

      const unsigned CurrentLevel = TN->getLevel();
      LLVM_DEBUG(dbgs() << "Mark " << BlockNamePrinter(TN) <<
                 "as affected, CurrentLevel " << CurrentLevel << "\n");

      assert(TN->getBlock() && II.Visited.count(TN) && "Preconditions!");

      while (true) {
        // Unlike regular Dijkstra, we have an inner loop to expand more
        // vertices. The first iteration is for the (affected) vertex popped
        // from II.Bucket and the rest are for vertices in
        // UnaffectedOnCurrentLevel, which may eventually expand to affected
        // vertices.
        //
        // Invariant: there is an optimal path from `To` to TN with the minimum
        // depth being CurrentLevel.
        for (const NodePtr Succ : getChildren<IsPostDom>(TN->getBlock(), BUI)) {
          const TreeNodePtr SuccTN = DT.getNode(Succ);
          assert(SuccTN &&
                 "Unreachable successor found at reachable insertion");
          const unsigned SuccLevel = SuccTN->getLevel();

          LLVM_DEBUG(dbgs() << "\tSuccessor " << BlockNamePrinter(Succ)
                            << ", level = " << SuccLevel << "\n");

          // There is an optimal path from `To` to Succ with the minimum depth
          // being min(CurrentLevel, SuccLevel).
          //
          // If depth(NCD)+1 < depth(Succ) is not satisfied, Succ is unaffected
          // and no affected vertex may be reached by a path passing through it.
          // Stop here. Also, Succ may be visited by other predecessors but the
          // first visit has the optimal path. Stop if Succ has been visited.
          if (SuccLevel <= NCDLevel + 1 || !II.Visited.insert(SuccTN).second)
            continue;

          if (SuccLevel > CurrentLevel) {
            // Succ is unaffected but it may (transitively) expand to affected
            // vertices. Store it in UnaffectedOnCurrentLevel.
            LLVM_DEBUG(dbgs() << "\t\tMarking visited not affected "
                              << BlockNamePrinter(Succ) << "\n");
            UnaffectedOnCurrentLevel.push_back(SuccTN);
#ifndef NDEBUG
            II.VisitedUnaffected.push_back(SuccTN);
#endif
          } else {
            // The condition is satisfied (Succ is affected). Add Succ to the
            // bucket queue.
            LLVM_DEBUG(dbgs() << "\t\tAdd " << BlockNamePrinter(Succ)
                              << " to a Bucket\n");
            II.Bucket.push(SuccTN);
          }
        }

        if (UnaffectedOnCurrentLevel.empty())
          break;
        TN = UnaffectedOnCurrentLevel.pop_back_val();
        LLVM_DEBUG(dbgs() << " Next: " << BlockNamePrinter(TN) << "\n");
      }
    }

    // Finish by updating immediate dominators and levels.
    UpdateInsertion(DT, BUI, NCD, II);
  }

  // Updates immediate dominators and levels after insertion.
  static void UpdateInsertion(DomTreeT &DT, const BatchUpdatePtr BUI,
                              const TreeNodePtr NCD, InsertionInfo &II) {
    LLVM_DEBUG(dbgs() << "Updating NCD = " << BlockNamePrinter(NCD) << "\n");

    for (const TreeNodePtr TN : II.Affected) {
      LLVM_DEBUG(dbgs() << "\tIDom(" << BlockNamePrinter(TN)
                        << ") = " << BlockNamePrinter(NCD) << "\n");
      TN->setIDom(NCD);
    }

#if defined(LLVM_ENABLE_ABI_BREAKING_CHECKS) && !defined(NDEBUG)
    for (const TreeNodePtr TN : II.VisitedUnaffected)
      assert(TN->getLevel() == TN->getIDom()->getLevel() + 1 &&
             "TN should have been updated by an affected ancestor");
#endif

    if (IsPostDom) UpdateRootsAfterUpdate(DT, BUI);
  }

  // Handles insertion to previously unreachable nodes.
  static void InsertUnreachable(DomTreeT &DT, const BatchUpdatePtr BUI,
                                const TreeNodePtr From, const NodePtr To) {
    LLVM_DEBUG(dbgs() << "Inserting " << BlockNamePrinter(From)
                      << " -> (unreachable) " << BlockNamePrinter(To) << "\n");

    // Collect discovered edges to already reachable nodes.
    SmallVector<std::pair<NodePtr, TreeNodePtr>, 8> DiscoveredEdgesToReachable;
    // Discover and connect nodes that became reachable with the insertion.
    ComputeUnreachableDominators(DT, BUI, To, From, DiscoveredEdgesToReachable);

    LLVM_DEBUG(dbgs() << "Inserted " << BlockNamePrinter(From)
                      << " -> (prev unreachable) " << BlockNamePrinter(To)
                      << "\n");

    // Used the discovered edges and inset discovered connecting (incoming)
    // edges.
    for (const auto &Edge : DiscoveredEdgesToReachable) {
      LLVM_DEBUG(dbgs() << "\tInserting discovered connecting edge "
                        << BlockNamePrinter(Edge.first) << " -> "
                        << BlockNamePrinter(Edge.second) << "\n");
      InsertReachable(DT, BUI, DT.getNode(Edge.first), Edge.second);
    }
  }

  // Connects nodes that become reachable with an insertion.
  static void ComputeUnreachableDominators(
      DomTreeT &DT, const BatchUpdatePtr BUI, const NodePtr Root,
      const TreeNodePtr Incoming,
      SmallVectorImpl<std::pair<NodePtr, TreeNodePtr>>
          &DiscoveredConnectingEdges) {
    assert(!DT.getNode(Root) && "Root must not be reachable");

    // Visit only previously unreachable nodes.
    auto UnreachableDescender = [&DT, &DiscoveredConnectingEdges](NodePtr From,
                                                                  NodePtr To) {
      const TreeNodePtr ToTN = DT.getNode(To);
      if (!ToTN) return true;

      DiscoveredConnectingEdges.push_back({From, ToTN});
      return false;
    };

    SemiNCAInfo SNCA(BUI);
    SNCA.runDFS(Root, 0, UnreachableDescender, 0);
    SNCA.runSemiNCA();
    SNCA.attachNewSubtree(DT, Incoming);

    LLVM_DEBUG(dbgs() << "After adding unreachable nodes\n");
  }

  static void DeleteEdge(DomTreeT &DT, const BatchUpdatePtr BUI,
                         const NodePtr From, const NodePtr To) {
    assert(From && To && "Cannot disconnect nullptrs");
    LLVM_DEBUG(dbgs() << "Deleting edge " << BlockNamePrinter(From) << " -> "
                      << BlockNamePrinter(To) << "\n");

#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
    // Ensure that the edge was in fact deleted from the CFG before informing
    // the DomTree about it.
    // The check is O(N), so run it only in debug configuration.
    auto IsSuccessor = [BUI](const NodePtr SuccCandidate, const NodePtr Of) {
      auto Successors = getChildren<IsPostDom>(Of, BUI);
      return llvm::is_contained(Successors, SuccCandidate);
    };
    (void)IsSuccessor;
    assert(!IsSuccessor(To, From) && "Deleted edge still exists in the CFG!");
#endif

    const TreeNodePtr FromTN = DT.getNode(From);
    // Deletion in an unreachable subtree -- nothing to do.
    if (!FromTN) return;

    const TreeNodePtr ToTN = DT.getNode(To);
    if (!ToTN) {
      LLVM_DEBUG(
          dbgs() << "\tTo (" << BlockNamePrinter(To)
                 << ") already unreachable -- there is no edge to delete\n");
      return;
    }

    const NodePtr NCDBlock = DT.findNearestCommonDominator(From, To);
    const TreeNodePtr NCD = DT.getNode(NCDBlock);

    // If To dominates From -- nothing to do.
    if (ToTN != NCD) {
      DT.DFSInfoValid = false;

      const TreeNodePtr ToIDom = ToTN->getIDom();
      LLVM_DEBUG(dbgs() << "\tNCD " << BlockNamePrinter(NCD) << ", ToIDom "
                        << BlockNamePrinter(ToIDom) << "\n");

      // To remains reachable after deletion.
      // (Based on the caption under Figure 4. from [2].)
      if (FromTN != ToIDom || HasProperSupport(DT, BUI, ToTN))
        DeleteReachable(DT, BUI, FromTN, ToTN);
      else
        DeleteUnreachable(DT, BUI, ToTN);
    }

    if (IsPostDom) UpdateRootsAfterUpdate(DT, BUI);
  }

  // Handles deletions that leave destination nodes reachable.
  static void DeleteReachable(DomTreeT &DT, const BatchUpdatePtr BUI,
                              const TreeNodePtr FromTN,
                              const TreeNodePtr ToTN) {
    LLVM_DEBUG(dbgs() << "Deleting reachable " << BlockNamePrinter(FromTN)
                      << " -> " << BlockNamePrinter(ToTN) << "\n");
    LLVM_DEBUG(dbgs() << "\tRebuilding subtree\n");

    // Find the top of the subtree that needs to be rebuilt.
    // (Based on the lemma 2.6 from [2].)
    const NodePtr ToIDom =
        DT.findNearestCommonDominator(FromTN->getBlock(), ToTN->getBlock());
    assert(ToIDom || DT.isPostDominator());
    const TreeNodePtr ToIDomTN = DT.getNode(ToIDom);
    assert(ToIDomTN);
    const TreeNodePtr PrevIDomSubTree = ToIDomTN->getIDom();
    // Top of the subtree to rebuild is the root node. Rebuild the tree from
    // scratch.
    if (!PrevIDomSubTree) {
      LLVM_DEBUG(dbgs() << "The entire tree needs to be rebuilt\n");
      CalculateFromScratch(DT, BUI);
      return;
    }

    // Only visit nodes in the subtree starting at To.
    const unsigned Level = ToIDomTN->getLevel();
    auto DescendBelow = [Level, &DT](NodePtr, NodePtr To) {
      return DT.getNode(To)->getLevel() > Level;
    };

    LLVM_DEBUG(dbgs() << "\tTop of subtree: " << BlockNamePrinter(ToIDomTN)
                      << "\n");

    SemiNCAInfo SNCA(BUI);
    SNCA.runDFS(ToIDom, 0, DescendBelow, 0);
    LLVM_DEBUG(dbgs() << "\tRunning Semi-NCA\n");
    SNCA.runSemiNCA();
    SNCA.reattachExistingSubtree(DT, PrevIDomSubTree);
  }

  // Checks if a node has proper support, as defined on the page 3 and later
  // explained on the page 7 of [2].
  static bool HasProperSupport(DomTreeT &DT, const BatchUpdatePtr BUI,
                               const TreeNodePtr TN) {
    LLVM_DEBUG(dbgs() << "IsReachableFromIDom " << BlockNamePrinter(TN)
                      << "\n");
    auto TNB = TN->getBlock();
    for (const NodePtr Pred : getChildren<!IsPostDom>(TNB, BUI)) {
      LLVM_DEBUG(dbgs() << "\tPred " << BlockNamePrinter(Pred) << "\n");
      if (!DT.getNode(Pred)) continue;

      const NodePtr Support = DT.findNearestCommonDominator(TNB, Pred);
      LLVM_DEBUG(dbgs() << "\tSupport " << BlockNamePrinter(Support) << "\n");
      if (Support != TNB) {
        LLVM_DEBUG(dbgs() << "\t" << BlockNamePrinter(TN)
                          << " is reachable from support "
                          << BlockNamePrinter(Support) << "\n");
        return true;
      }
    }

    return false;
  }

  // Handle deletions that make destination node unreachable.
  // (Based on the lemma 2.7 from the [2].)
  static void DeleteUnreachable(DomTreeT &DT, const BatchUpdatePtr BUI,
                                const TreeNodePtr ToTN) {
    LLVM_DEBUG(dbgs() << "Deleting unreachable subtree "
                      << BlockNamePrinter(ToTN) << "\n");
    assert(ToTN);
    assert(ToTN->getBlock());

    if (IsPostDom) {
      // Deletion makes a region reverse-unreachable and creates a new root.
      // Simulate that by inserting an edge from the virtual root to ToTN and
      // adding it as a new root.
      LLVM_DEBUG(dbgs() << "\tDeletion made a region reverse-unreachable\n");
      LLVM_DEBUG(dbgs() << "\tAdding new root " << BlockNamePrinter(ToTN)
                        << "\n");
      DT.Roots.push_back(ToTN->getBlock());
      InsertReachable(DT, BUI, DT.getNode(nullptr), ToTN);
      return;
    }

    SmallVector<NodePtr, 16> AffectedQueue;
    const unsigned Level = ToTN->getLevel();

    // Traverse destination node's descendants with greater level in the tree
    // and collect visited nodes.
    auto DescendAndCollect = [Level, &AffectedQueue, &DT](NodePtr, NodePtr To) {
      const TreeNodePtr TN = DT.getNode(To);
      assert(TN);
      if (TN->getLevel() > Level) return true;
      if (!llvm::is_contained(AffectedQueue, To))
        AffectedQueue.push_back(To);

      return false;
    };

    SemiNCAInfo SNCA(BUI);
    unsigned LastDFSNum =
        SNCA.runDFS(ToTN->getBlock(), 0, DescendAndCollect, 0);

    TreeNodePtr MinNode = ToTN;

    // Identify the top of the subtree to rebuild by finding the NCD of all
    // the affected nodes.
    for (const NodePtr N : AffectedQueue) {
      const TreeNodePtr TN = DT.getNode(N);
      const NodePtr NCDBlock =
          DT.findNearestCommonDominator(TN->getBlock(), ToTN->getBlock());
      assert(NCDBlock || DT.isPostDominator());
      const TreeNodePtr NCD = DT.getNode(NCDBlock);
      assert(NCD);

      LLVM_DEBUG(dbgs() << "Processing affected node " << BlockNamePrinter(TN)
                        << " with NCD = " << BlockNamePrinter(NCD)
                        << ", MinNode =" << BlockNamePrinter(MinNode) << "\n");
      if (NCD != TN && NCD->getLevel() < MinNode->getLevel()) MinNode = NCD;
    }

    // Root reached, rebuild the whole tree from scratch.
    if (!MinNode->getIDom()) {
      LLVM_DEBUG(dbgs() << "The entire tree needs to be rebuilt\n");
      CalculateFromScratch(DT, BUI);
      return;
    }

    // Erase the unreachable subtree in reverse preorder to process all children
    // before deleting their parent.
    for (unsigned i = LastDFSNum; i > 0; --i) {
      const NodePtr N = SNCA.NumToNode[i];
      const TreeNodePtr TN = DT.getNode(N);
      LLVM_DEBUG(dbgs() << "Erasing node " << BlockNamePrinter(TN) << "\n");

      EraseNode(DT, TN);
    }

    // The affected subtree start at the To node -- there's no extra work to do.
    if (MinNode == ToTN) return;

    LLVM_DEBUG(dbgs() << "DeleteUnreachable: running DFS with MinNode = "
                      << BlockNamePrinter(MinNode) << "\n");
    const unsigned MinLevel = MinNode->getLevel();
    const TreeNodePtr PrevIDom = MinNode->getIDom();
    assert(PrevIDom);
    SNCA.clear();

    // Identify nodes that remain in the affected subtree.
    auto DescendBelow = [MinLevel, &DT](NodePtr, NodePtr To) {
      const TreeNodePtr ToTN = DT.getNode(To);
      return ToTN && ToTN->getLevel() > MinLevel;
    };
    SNCA.runDFS(MinNode->getBlock(), 0, DescendBelow, 0);

    LLVM_DEBUG(dbgs() << "Previous IDom(MinNode) = "
                      << BlockNamePrinter(PrevIDom) << "\nRunning Semi-NCA\n");

    // Rebuild the remaining part of affected subtree.
    SNCA.runSemiNCA();
    SNCA.reattachExistingSubtree(DT, PrevIDom);
  }

  // Removes leaf tree nodes from the dominator tree.
  static void EraseNode(DomTreeT &DT, const TreeNodePtr TN) {
    assert(TN);
    assert(TN->getNumChildren() == 0 && "Not a tree leaf");

    const TreeNodePtr IDom = TN->getIDom();
    assert(IDom);

    auto ChIt = llvm::find(IDom->Children, TN);
    assert(ChIt != IDom->Children.end());
    std::swap(*ChIt, IDom->Children.back());
    IDom->Children.pop_back();

    DT.DomTreeNodes.erase(TN->getBlock());
  }

  //~~
  //===--------------------- DomTree Batch Updater --------------------------===
  //~~

  static void ApplyUpdates(DomTreeT &DT, GraphDiffT &PreViewCFG,
                           GraphDiffT *PostViewCFG) {
    // Note: the PostViewCFG is only used when computing from scratch. It's data
    // should already included in the PreViewCFG for incremental updates.
    const size_t NumUpdates = PreViewCFG.getNumLegalizedUpdates();
    if (NumUpdates == 0)
      return;

    // Take the fast path for a single update and avoid running the batch update
    // machinery.
    if (NumUpdates == 1) {
      UpdateT Update = PreViewCFG.popUpdateForIncrementalUpdates();
      if (!PostViewCFG) {
        if (Update.getKind() == UpdateKind::Insert)
          InsertEdge(DT, /*BUI=*/nullptr, Update.getFrom(), Update.getTo());
        else
          DeleteEdge(DT, /*BUI=*/nullptr, Update.getFrom(), Update.getTo());
      } else {
        BatchUpdateInfo BUI(*PostViewCFG, PostViewCFG);
        if (Update.getKind() == UpdateKind::Insert)
          InsertEdge(DT, &BUI, Update.getFrom(), Update.getTo());
        else
          DeleteEdge(DT, &BUI, Update.getFrom(), Update.getTo());
      }
      return;
    }

    BatchUpdateInfo BUI(PreViewCFG, PostViewCFG);
    // Recalculate the DominatorTree when the number of updates
    // exceeds a threshold, which usually makes direct updating slower than
    // recalculation. We select this threshold proportional to the
    // size of the DominatorTree. The constant is selected
    // by choosing the one with an acceptable performance on some real-world
    // inputs.

    // Make unittests of the incremental algorithm work
    if (DT.DomTreeNodes.size() <= 100) {
      if (BUI.NumLegalized > DT.DomTreeNodes.size())
        CalculateFromScratch(DT, &BUI);
    } else if (BUI.NumLegalized > DT.DomTreeNodes.size() / 40)
      CalculateFromScratch(DT, &BUI);

    // If the DominatorTree was recalculated at some point, stop the batch
    // updates. Full recalculations ignore batch updates and look at the actual
    // CFG.
    for (size_t i = 0; i < BUI.NumLegalized && !BUI.IsRecalculated; ++i)
      ApplyNextUpdate(DT, BUI);
  }

  static void ApplyNextUpdate(DomTreeT &DT, BatchUpdateInfo &BUI) {
    // Popping the next update, will move the PreViewCFG to the next snapshot.
    UpdateT CurrentUpdate = BUI.PreViewCFG.popUpdateForIncrementalUpdates();
#if 0
    // FIXME: The LLVM_DEBUG macro only plays well with a modular
    // build of LLVM when the header is marked as textual, but doing
    // so causes redefinition errors.
    LLVM_DEBUG(dbgs() << "Applying update: ");
    LLVM_DEBUG(CurrentUpdate.dump(); dbgs() << "\n");
#endif

    if (CurrentUpdate.getKind() == UpdateKind::Insert)
      InsertEdge(DT, &BUI, CurrentUpdate.getFrom(), CurrentUpdate.getTo());
    else
      DeleteEdge(DT, &BUI, CurrentUpdate.getFrom(), CurrentUpdate.getTo());
  }

  //~~
  //===--------------- DomTree correctness verification ---------------------===
  //~~

  // Check if the tree has correct roots. A DominatorTree always has a single
  // root which is the function's entry node. A PostDominatorTree can have
  // multiple roots - one for each node with no successors and for infinite
  // loops.
  // Running time: O(N).
  bool verifyRoots(const DomTreeT &DT) {
    if (!DT.Parent && !DT.Roots.empty()) {
      errs() << "Tree has no parent but has roots!\n";
      errs().flush();
      return false;
    }

    if (!IsPostDom) {
      if (DT.Roots.empty()) {
        errs() << "Tree doesn't have a root!\n";
        errs().flush();
        return false;
      }

      if (DT.getRoot() != GetEntryNode(DT)) {
        errs() << "Tree's root is not its parent's entry node!\n";
        errs().flush();
        return false;
      }
    }

    RootsT ComputedRoots = FindRoots(DT, nullptr);
    if (!isPermutation(DT.Roots, ComputedRoots)) {
      errs() << "Tree has different roots than freshly computed ones!\n";
      errs() << "\tPDT roots: ";
      for (const NodePtr N : DT.Roots) errs() << BlockNamePrinter(N) << ", ";
      errs() << "\n\tComputed roots: ";
      for (const NodePtr N : ComputedRoots)
        errs() << BlockNamePrinter(N) << ", ";
      errs() << "\n";
      errs().flush();
      return false;
    }

    return true;
  }

  // Checks if the tree contains all reachable nodes in the input graph.
  // Running time: O(N).
  bool verifyReachability(const DomTreeT &DT) {
    clear();
    doFullDFSWalk(DT, AlwaysDescend);

    for (auto &NodeToTN : DT.DomTreeNodes) {
      const TreeNodePtr TN = NodeToTN.second.get();
      const NodePtr BB = TN->getBlock();

      // Virtual root has a corresponding virtual CFG node.
      if (DT.isVirtualRoot(TN)) continue;

      if (NodeToInfo.count(BB) == 0) {
        errs() << "DomTree node " << BlockNamePrinter(BB)
               << " not found by DFS walk!\n";
        errs().flush();

        return false;
      }
    }

    for (const NodePtr N : NumToNode) {
      if (N && !DT.getNode(N)) {
        errs() << "CFG node " << BlockNamePrinter(N)
               << " not found in the DomTree!\n";
        errs().flush();

        return false;
      }
    }

    return true;
  }

  // Check if for every parent with a level L in the tree all of its children
  // have level L + 1.
  // Running time: O(N).
  static bool VerifyLevels(const DomTreeT &DT) {
    for (auto &NodeToTN : DT.DomTreeNodes) {
      const TreeNodePtr TN = NodeToTN.second.get();
      const NodePtr BB = TN->getBlock();
      if (!BB) continue;

      const TreeNodePtr IDom = TN->getIDom();
      if (!IDom && TN->getLevel() != 0) {
        errs() << "Node without an IDom " << BlockNamePrinter(BB)
               << " has a nonzero level " << TN->getLevel() << "!\n";
        errs().flush();

        return false;
      }

      if (IDom && TN->getLevel() != IDom->getLevel() + 1) {
        errs() << "Node " << BlockNamePrinter(BB) << " has level "
               << TN->getLevel() << " while its IDom "
               << BlockNamePrinter(IDom->getBlock()) << " has level "
               << IDom->getLevel() << "!\n";
        errs().flush();

        return false;
      }
    }

    return true;
  }

  // Check if the computed DFS numbers are correct. Note that DFS info may not
  // be valid, and when that is the case, we don't verify the numbers.
  // Running time: O(N log(N)).
  static bool VerifyDFSNumbers(const DomTreeT &DT) {
    if (!DT.DFSInfoValid || !DT.Parent)
      return true;

    const NodePtr RootBB = IsPostDom ? nullptr : *DT.root_begin();
    const TreeNodePtr Root = DT.getNode(RootBB);

    auto PrintNodeAndDFSNums = [](const TreeNodePtr TN) {
      errs() << BlockNamePrinter(TN) << " {" << TN->getDFSNumIn() << ", "
             << TN->getDFSNumOut() << '}';
    };

    // Verify the root's DFS In number. Although DFS numbering would also work
    // if we started from some other value, we assume 0-based numbering.
    if (Root->getDFSNumIn() != 0) {
      errs() << "DFSIn number for the tree root is not:\n\t";
      PrintNodeAndDFSNums(Root);
      errs() << '\n';
      errs().flush();
      return false;
    }

    // For each tree node verify if children's DFS numbers cover their parent's
    // DFS numbers with no gaps.
    for (const auto &NodeToTN : DT.DomTreeNodes) {
      const TreeNodePtr Node = NodeToTN.second.get();

      // Handle tree leaves.
      if (Node->isLeaf()) {
        if (Node->getDFSNumIn() + 1 != Node->getDFSNumOut()) {
          errs() << "Tree leaf should have DFSOut = DFSIn + 1:\n\t";
          PrintNodeAndDFSNums(Node);
          errs() << '\n';
          errs().flush();
          return false;
        }

        continue;
      }

      // Make a copy and sort it such that it is possible to check if there are
      // no gaps between DFS numbers of adjacent children.
      SmallVector<TreeNodePtr, 8> Children(Node->begin(), Node->end());
      llvm::sort(Children, [](const TreeNodePtr Ch1, const TreeNodePtr Ch2) {
        return Ch1->getDFSNumIn() < Ch2->getDFSNumIn();
      });

      auto PrintChildrenError = [Node, &Children, PrintNodeAndDFSNums](
          const TreeNodePtr FirstCh, const TreeNodePtr SecondCh) {
        assert(FirstCh);

        errs() << "Incorrect DFS numbers for:\n\tParent ";
        PrintNodeAndDFSNums(Node);

        errs() << "\n\tChild ";
        PrintNodeAndDFSNums(FirstCh);

        if (SecondCh) {
          errs() << "\n\tSecond child ";
          PrintNodeAndDFSNums(SecondCh);
        }

        errs() << "\nAll children: ";
        for (const TreeNodePtr Ch : Children) {
          PrintNodeAndDFSNums(Ch);
          errs() << ", ";
        }

        errs() << '\n';
        errs().flush();
      };

      if (Children.front()->getDFSNumIn() != Node->getDFSNumIn() + 1) {
        PrintChildrenError(Children.front(), nullptr);
        return false;
      }

      if (Children.back()->getDFSNumOut() + 1 != Node->getDFSNumOut()) {
        PrintChildrenError(Children.back(), nullptr);
        return false;
      }

      for (size_t i = 0, e = Children.size() - 1; i != e; ++i) {
        if (Children[i]->getDFSNumOut() + 1 != Children[i + 1]->getDFSNumIn()) {
          PrintChildrenError(Children[i], Children[i + 1]);
          return false;
        }
      }
    }

    return true;
  }

  // The below routines verify the correctness of the dominator tree relative to
  // the CFG it's coming from.  A tree is a dominator tree iff it has two
  // properties, called the parent property and the sibling property.  Tarjan
  // and Lengauer prove (but don't explicitly name) the properties as part of
  // the proofs in their 1972 paper, but the proofs are mostly part of proving
  // things about semidominators and idoms, and some of them are simply asserted
  // based on even earlier papers (see, e.g., lemma 2).  Some papers refer to
  // these properties as "valid" and "co-valid".  See, e.g., "Dominators,
  // directed bipolar orders, and independent spanning trees" by Loukas
  // Georgiadis and Robert E. Tarjan, as well as "Dominator Tree Verification
  // and Vertex-Disjoint Paths " by the same authors.

  // A very simple and direct explanation of these properties can be found in
  // "An Experimental Study of Dynamic Dominators", found at
  // https://arxiv.org/abs/1604.02711

  // The easiest way to think of the parent property is that it's a requirement
  // of being a dominator.  Let's just take immediate dominators.  For PARENT to
  // be an immediate dominator of CHILD, all paths in the CFG must go through
  // PARENT before they hit CHILD.  This implies that if you were to cut PARENT
  // out of the CFG, there should be no paths to CHILD that are reachable.  If
  // there are, then you now have a path from PARENT to CHILD that goes around
  // PARENT and still reaches CHILD, which by definition, means PARENT can't be
  // a dominator of CHILD (let alone an immediate one).

  // The sibling property is similar.  It says that for each pair of sibling
  // nodes in the dominator tree (LEFT and RIGHT) , they must not dominate each
  // other.  If sibling LEFT dominated sibling RIGHT, it means there are no
  // paths in the CFG from sibling LEFT to sibling RIGHT that do not go through
  // LEFT, and thus, LEFT is really an ancestor (in the dominator tree) of
  // RIGHT, not a sibling.

  // It is possible to verify the parent and sibling properties in linear time,
  // but the algorithms are complex. Instead, we do it in a straightforward
  // N^2 and N^3 way below, using direct path reachability.

  // Checks if the tree has the parent property: if for all edges from V to W in
  // the input graph, such that V is reachable, the parent of W in the tree is
  // an ancestor of V in the tree.
  // Running time: O(N^2).
  //
  // This means that if a node gets disconnected from the graph, then all of
  // the nodes it dominated previously will now become unreachable.
  bool verifyParentProperty(const DomTreeT &DT) {
    for (auto &NodeToTN : DT.DomTreeNodes) {
      const TreeNodePtr TN = NodeToTN.second.get();
      const NodePtr BB = TN->getBlock();
      if (!BB || TN->isLeaf())
        continue;

      LLVM_DEBUG(dbgs() << "Verifying parent property of node "
                        << BlockNamePrinter(TN) << "\n");
      clear();
      doFullDFSWalk(DT, [BB](NodePtr From, NodePtr To) {
        return From != BB && To != BB;
      });

      for (TreeNodePtr Child : TN->children())
        if (NodeToInfo.count(Child->getBlock()) != 0) {
          errs() << "Child " << BlockNamePrinter(Child)
                 << " reachable after its parent " << BlockNamePrinter(BB)
                 << " is removed!\n";
          errs().flush();

          return false;
        }
    }

    return true;
  }

  // Check if the tree has sibling property: if a node V does not dominate a
  // node W for all siblings V and W in the tree.
  // Running time: O(N^3).
  //
  // This means that if a node gets disconnected from the graph, then all of its
  // siblings will now still be reachable.
  bool verifySiblingProperty(const DomTreeT &DT) {
    for (auto &NodeToTN : DT.DomTreeNodes) {
      const TreeNodePtr TN = NodeToTN.second.get();
      const NodePtr BB = TN->getBlock();
      if (!BB || TN->isLeaf())
        continue;

      for (const TreeNodePtr N : TN->children()) {
        clear();
        NodePtr BBN = N->getBlock();
        doFullDFSWalk(DT, [BBN](NodePtr From, NodePtr To) {
          return From != BBN && To != BBN;
        });

        for (const TreeNodePtr S : TN->children()) {
          if (S == N) continue;

          if (NodeToInfo.count(S->getBlock()) == 0) {
            errs() << "Node " << BlockNamePrinter(S)
                   << " not reachable when its sibling " << BlockNamePrinter(N)
                   << " is removed!\n";
            errs().flush();

            return false;
          }
        }
      }
    }

    return true;
  }

  // Check if the given tree is the same as a freshly computed one for the same
  // Parent.
  // Running time: O(N^2), but faster in practice (same as tree construction).
  //
  // Note that this does not check if that the tree construction algorithm is
  // correct and should be only used for fast (but possibly unsound)
  // verification.
  static bool IsSameAsFreshTree(const DomTreeT &DT) {
    DomTreeT FreshTree;
    FreshTree.recalculate(*DT.Parent);
    const bool Different = DT.compare(FreshTree);

    if (Different) {
      errs() << (DT.isPostDominator() ? "Post" : "")
             << "DominatorTree is different than a freshly computed one!\n"
             << "\tCurrent:\n";
      DT.print(errs());
      errs() << "\n\tFreshly computed tree:\n";
      FreshTree.print(errs());
      errs().flush();
    }

    return !Different;
  }
};

template <class DomTreeT>
void Calculate(DomTreeT &DT) {
  SemiNCAInfo<DomTreeT>::CalculateFromScratch(DT, nullptr);
}

template <typename DomTreeT>
void CalculateWithUpdates(DomTreeT &DT,
                          ArrayRef<typename DomTreeT::UpdateType> Updates) {
  // FIXME: Updated to use the PreViewCFG and behave the same as until now.
  // This behavior is however incorrect; this actually needs the PostViewCFG.
  GraphDiff<typename DomTreeT::NodePtr, DomTreeT::IsPostDominator> PreViewCFG(
      Updates, /*ReverseApplyUpdates=*/true);
  typename SemiNCAInfo<DomTreeT>::BatchUpdateInfo BUI(PreViewCFG);
  SemiNCAInfo<DomTreeT>::CalculateFromScratch(DT, &BUI);
}

template <class DomTreeT>
void InsertEdge(DomTreeT &DT, typename DomTreeT::NodePtr From,
                typename DomTreeT::NodePtr To) {
  if (DT.isPostDominator()) std::swap(From, To);
  SemiNCAInfo<DomTreeT>::InsertEdge(DT, nullptr, From, To);
}

template <class DomTreeT>
void DeleteEdge(DomTreeT &DT, typename DomTreeT::NodePtr From,
                typename DomTreeT::NodePtr To) {
  if (DT.isPostDominator()) std::swap(From, To);
  SemiNCAInfo<DomTreeT>::DeleteEdge(DT, nullptr, From, To);
}

template <class DomTreeT>
void ApplyUpdates(DomTreeT &DT,
                  GraphDiff<typename DomTreeT::NodePtr,
                            DomTreeT::IsPostDominator> &PreViewCFG,
                  GraphDiff<typename DomTreeT::NodePtr,
                            DomTreeT::IsPostDominator> *PostViewCFG) {
  SemiNCAInfo<DomTreeT>::ApplyUpdates(DT, PreViewCFG, PostViewCFG);
}

template <class DomTreeT>
bool Verify(const DomTreeT &DT, typename DomTreeT::VerificationLevel VL) {
  SemiNCAInfo<DomTreeT> SNCA(nullptr);

  // Simplist check is to compare against a new tree. This will also
  // usefully print the old and new trees, if they are different.
  if (!SNCA.IsSameAsFreshTree(DT))
    return false;

  // Common checks to verify the properties of the tree. O(N log N) at worst.
  if (!SNCA.verifyRoots(DT) || !SNCA.verifyReachability(DT) ||
      !SNCA.VerifyLevels(DT) || !SNCA.VerifyDFSNumbers(DT))
    return false;

  // Extra checks depending on VerificationLevel. Up to O(N^3).
  if (VL == DomTreeT::VerificationLevel::Basic ||
      VL == DomTreeT::VerificationLevel::Full)
    if (!SNCA.verifyParentProperty(DT))
      return false;
  if (VL == DomTreeT::VerificationLevel::Full)
    if (!SNCA.verifySiblingProperty(DT))
      return false;

  return true;
}

}  // namespace DomTreeBuilder
}  // namespace llvm

#undef DEBUG_TYPE

#endif
