//===- HexagonCommonGEP.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <utility>
#include <vector>

#define DEBUG_TYPE "commgep"

using namespace llvm;

static cl::opt<bool> OptSpeculate("commgep-speculate", cl::init(true),
                                  cl::Hidden);

static cl::opt<bool> OptEnableInv("commgep-inv", cl::init(true), cl::Hidden);

static cl::opt<bool> OptEnableConst("commgep-const", cl::init(true),
                                    cl::Hidden);

namespace llvm {

  void initializeHexagonCommonGEPPass(PassRegistry&);

} // end namespace llvm

namespace {

  struct GepNode;
  using NodeSet = std::set<GepNode *>;
  using NodeToValueMap = std::map<GepNode *, Value *>;
  using NodeVect = std::vector<GepNode *>;
  using NodeChildrenMap = std::map<GepNode *, NodeVect>;
  using UseSet = SetVector<Use *>;
  using NodeToUsesMap = std::map<GepNode *, UseSet>;

  // Numbering map for gep nodes. Used to keep track of ordering for
  // gep nodes.
  struct NodeOrdering {
    NodeOrdering() = default;

    void insert(const GepNode *N) { Map.insert(std::make_pair(N, ++LastNum)); }
    void clear() { Map.clear(); }

    bool operator()(const GepNode *N1, const GepNode *N2) const {
      auto F1 = Map.find(N1), F2 = Map.find(N2);
      assert(F1 != Map.end() && F2 != Map.end());
      return F1->second < F2->second;
    }

  private:
    std::map<const GepNode *, unsigned> Map;
    unsigned LastNum = 0;
  };

  class HexagonCommonGEP : public FunctionPass {
  public:
    static char ID;

    HexagonCommonGEP() : FunctionPass(ID) {
      initializeHexagonCommonGEPPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override;
    StringRef getPassName() const override { return "Hexagon Common GEP"; }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<DominatorTreeWrapperPass>();
      AU.addPreserved<DominatorTreeWrapperPass>();
      AU.addRequired<PostDominatorTreeWrapperPass>();
      AU.addPreserved<PostDominatorTreeWrapperPass>();
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addPreserved<LoopInfoWrapperPass>();
      FunctionPass::getAnalysisUsage(AU);
    }

  private:
    using ValueToNodeMap = std::map<Value *, GepNode *>;
    using ValueVect = std::vector<Value *>;
    using NodeToValuesMap = std::map<GepNode *, ValueVect>;

    void getBlockTraversalOrder(BasicBlock *Root, ValueVect &Order);
    bool isHandledGepForm(GetElementPtrInst *GepI);
    void processGepInst(GetElementPtrInst *GepI, ValueToNodeMap &NM);
    void collect();
    void common();

    BasicBlock *recalculatePlacement(GepNode *Node, NodeChildrenMap &NCM,
                                     NodeToValueMap &Loc);
    BasicBlock *recalculatePlacementRec(GepNode *Node, NodeChildrenMap &NCM,
                                        NodeToValueMap &Loc);
    bool isInvariantIn(Value *Val, Loop *L);
    bool isInvariantIn(GepNode *Node, Loop *L);
    bool isInMainPath(BasicBlock *B, Loop *L);
    BasicBlock *adjustForInvariance(GepNode *Node, NodeChildrenMap &NCM,
                                    NodeToValueMap &Loc);
    void separateChainForNode(GepNode *Node, Use *U, NodeToValueMap &Loc);
    void separateConstantChains(GepNode *Node, NodeChildrenMap &NCM,
                                NodeToValueMap &Loc);
    void computeNodePlacement(NodeToValueMap &Loc);

    Value *fabricateGEP(NodeVect &NA, BasicBlock::iterator At,
                        BasicBlock *LocB);
    void getAllUsersForNode(GepNode *Node, ValueVect &Values,
                            NodeChildrenMap &NCM);
    void materialize(NodeToValueMap &Loc);

    void removeDeadCode();

    NodeVect Nodes;
    NodeToUsesMap Uses;
    NodeOrdering NodeOrder;   // Node ordering, for deterministic behavior.
    SpecificBumpPtrAllocator<GepNode> *Mem;
    LLVMContext *Ctx;
    LoopInfo *LI;
    DominatorTree *DT;
    PostDominatorTree *PDT;
    Function *Fn;
  };

} // end anonymous namespace

char HexagonCommonGEP::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonCommonGEP, "hcommgep", "Hexagon Common GEP",
      false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(HexagonCommonGEP, "hcommgep", "Hexagon Common GEP",
      false, false)

namespace {

  struct GepNode {
    enum {
      None      = 0,
      Root      = 0x01,
      Internal  = 0x02,
      Used      = 0x04,
      InBounds  = 0x08,
      Pointer   = 0x10,   // See note below.
    };
    // Note: GEP indices generally traverse nested types, and so a GepNode
    // (representing a single index) can be associated with some composite
    // type. The exception is the GEP input, which is a pointer, and not
    // a composite type (at least not in the sense of having sub-types).
    // Also, the corresponding index plays a different role as well: it is
    // simply added to the input pointer. Since pointer types are becoming
    // opaque (i.e. are no longer going to include the pointee type), the
    // two pieces of information (1) the fact that it's a pointer, and
    // (2) the pointee type, need to be stored separately. The pointee type
    // will be stored in the PTy member, while the fact that the node
    // operates on a pointer will be reflected by the flag "Pointer".

    uint32_t Flags = 0;
    union {
      GepNode *Parent;
      Value *BaseVal;
    };
    Value *Idx = nullptr;
    Type *PTy = nullptr;    // Type indexed by this node. For pointer nodes
                            // this is the "pointee" type, and indexing a
                            // pointer does not change the type.

    GepNode() : Parent(nullptr) {}
    GepNode(const GepNode *N) : Flags(N->Flags), Idx(N->Idx), PTy(N->PTy) {
      if (Flags & Root)
        BaseVal = N->BaseVal;
      else
        Parent = N->Parent;
    }

    friend raw_ostream &operator<< (raw_ostream &OS, const GepNode &GN);
  };

  raw_ostream &operator<< (raw_ostream &OS, const GepNode &GN) {
    OS << "{ {";
    bool Comma = false;
    if (GN.Flags & GepNode::Root) {
      OS << "root";
      Comma = true;
    }
    if (GN.Flags & GepNode::Internal) {
      if (Comma)
        OS << ',';
      OS << "internal";
      Comma = true;
    }
    if (GN.Flags & GepNode::Used) {
      if (Comma)
        OS << ',';
      OS << "used";
    }
    if (GN.Flags & GepNode::InBounds) {
      if (Comma)
        OS << ',';
      OS << "inbounds";
    }
    if (GN.Flags & GepNode::Pointer) {
      if (Comma)
        OS << ',';
      OS << "pointer";
    }
    OS << "} ";
    if (GN.Flags & GepNode::Root)
      OS << "BaseVal:" << GN.BaseVal->getName() << '(' << GN.BaseVal << ')';
    else
      OS << "Parent:" << GN.Parent;

    OS << " Idx:";
    if (ConstantInt *CI = dyn_cast<ConstantInt>(GN.Idx))
      OS << CI->getValue().getSExtValue();
    else if (GN.Idx->hasName())
      OS << GN.Idx->getName();
    else
      OS << "<anon> =" << *GN.Idx;

    OS << " PTy:";
    if (GN.PTy->isStructTy()) {
      StructType *STy = cast<StructType>(GN.PTy);
      if (!STy->isLiteral())
        OS << GN.PTy->getStructName();
      else
        OS << "<anon-struct>:" << *STy;
    }
    else
      OS << *GN.PTy;
    OS << " }";
    return OS;
  }

  template <typename NodeContainer>
  void dump_node_container(raw_ostream &OS, const NodeContainer &S) {
    using const_iterator = typename NodeContainer::const_iterator;

    for (const_iterator I = S.begin(), E = S.end(); I != E; ++I)
      OS << *I << ' ' << **I << '\n';
  }

  raw_ostream &operator<< (raw_ostream &OS,
                           const NodeVect &S) LLVM_ATTRIBUTE_UNUSED;
  raw_ostream &operator<< (raw_ostream &OS, const NodeVect &S) {
    dump_node_container(OS, S);
    return OS;
  }

  raw_ostream &operator<< (raw_ostream &OS,
                           const NodeToUsesMap &M) LLVM_ATTRIBUTE_UNUSED;
  raw_ostream &operator<< (raw_ostream &OS, const NodeToUsesMap &M){
    for (const auto &I : M) {
      const UseSet &Us = I.second;
      OS << I.first << " -> #" << Us.size() << '{';
      for (const Use *U : Us) {
        User *R = U->getUser();
        if (R->hasName())
          OS << ' ' << R->getName();
        else
          OS << " <?>(" << *R << ')';
      }
      OS << " }\n";
    }
    return OS;
  }

  struct in_set {
    in_set(const NodeSet &S) : NS(S) {}

    bool operator() (GepNode *N) const {
      return NS.find(N) != NS.end();
    }

  private:
    const NodeSet &NS;
  };

} // end anonymous namespace

inline void *operator new(size_t, SpecificBumpPtrAllocator<GepNode> &A) {
  return A.Allocate();
}

void HexagonCommonGEP::getBlockTraversalOrder(BasicBlock *Root,
      ValueVect &Order) {
  // Compute block ordering for a typical DT-based traversal of the flow
  // graph: "before visiting a block, all of its dominators must have been
  // visited".

  Order.push_back(Root);
  for (auto *DTN : children<DomTreeNode*>(DT->getNode(Root)))
    getBlockTraversalOrder(DTN->getBlock(), Order);
}

bool HexagonCommonGEP::isHandledGepForm(GetElementPtrInst *GepI) {
  // No vector GEPs.
  if (!GepI->getType()->isPointerTy())
    return false;
  // No GEPs without any indices.  (Is this possible?)
  if (GepI->idx_begin() == GepI->idx_end())
    return false;
  return true;
}

void HexagonCommonGEP::processGepInst(GetElementPtrInst *GepI,
      ValueToNodeMap &NM) {
  LLVM_DEBUG(dbgs() << "Visiting GEP: " << *GepI << '\n');
  GepNode *N = new (*Mem) GepNode;
  Value *PtrOp = GepI->getPointerOperand();
  uint32_t InBounds = GepI->isInBounds() ? GepNode::InBounds : 0;
  ValueToNodeMap::iterator F = NM.find(PtrOp);
  if (F == NM.end()) {
    N->BaseVal = PtrOp;
    N->Flags |= GepNode::Root | InBounds;
  } else {
    // If PtrOp was a GEP instruction, it must have already been processed.
    // The ValueToNodeMap entry for it is the last gep node in the generated
    // chain. Link to it here.
    N->Parent = F->second;
  }
  N->PTy = GepI->getSourceElementType();
  N->Flags |= GepNode::Pointer;
  N->Idx = *GepI->idx_begin();

  // Collect the list of users of this GEP instruction. Will add it to the
  // last node created for it.
  UseSet Us;
  for (Value::user_iterator UI = GepI->user_begin(), UE = GepI->user_end();
       UI != UE; ++UI) {
    // Check if this gep is used by anything other than other geps that
    // we will process.
    if (isa<GetElementPtrInst>(*UI)) {
      GetElementPtrInst *UserG = cast<GetElementPtrInst>(*UI);
      if (isHandledGepForm(UserG))
        continue;
    }
    Us.insert(&UI.getUse());
  }
  Nodes.push_back(N);
  NodeOrder.insert(N);

  // Skip the first index operand, since it was already handled above. This
  // dereferences the pointer operand.
  GepNode *PN = N;
  Type *PtrTy = GepI->getSourceElementType();
  for (Use &U : llvm::drop_begin(GepI->indices())) {
    Value *Op = U;
    GepNode *Nx = new (*Mem) GepNode;
    Nx->Parent = PN;  // Link Nx to the previous node.
    Nx->Flags |= GepNode::Internal | InBounds;
    Nx->PTy = PtrTy;
    Nx->Idx = Op;
    Nodes.push_back(Nx);
    NodeOrder.insert(Nx);
    PN = Nx;

    PtrTy = GetElementPtrInst::getTypeAtIndex(PtrTy, Op);
  }

  // After last node has been created, update the use information.
  if (!Us.empty()) {
    PN->Flags |= GepNode::Used;
    Uses[PN].insert(Us.begin(), Us.end());
  }

  // Link the last node with the originating GEP instruction. This is to
  // help with linking chained GEP instructions.
  NM.insert(std::make_pair(GepI, PN));
}

void HexagonCommonGEP::collect() {
  // Establish depth-first traversal order of the dominator tree.
  ValueVect BO;
  getBlockTraversalOrder(&Fn->front(), BO);

  // The creation of gep nodes requires DT-traversal. When processing a GEP
  // instruction that uses another GEP instruction as the base pointer, the
  // gep node for the base pointer should already exist.
  ValueToNodeMap NM;
  for (Value *I : BO) {
    BasicBlock *B = cast<BasicBlock>(I);
    for (Instruction &J : *B)
      if (auto *GepI = dyn_cast<GetElementPtrInst>(&J))
        if (isHandledGepForm(GepI))
          processGepInst(GepI, NM);
  }

  LLVM_DEBUG(dbgs() << "Gep nodes after initial collection:\n" << Nodes);
}

static void invert_find_roots(const NodeVect &Nodes, NodeChildrenMap &NCM,
                              NodeVect &Roots) {
  for (GepNode *N : Nodes) {
    if (N->Flags & GepNode::Root) {
      Roots.push_back(N);
      continue;
    }
    GepNode *PN = N->Parent;
    NCM[PN].push_back(N);
  }
}

static void nodes_for_root(GepNode *Root, NodeChildrenMap &NCM,
                           NodeSet &Nodes) {
    NodeVect Work;
    Work.push_back(Root);
    Nodes.insert(Root);

    while (!Work.empty()) {
      NodeVect::iterator First = Work.begin();
      GepNode *N = *First;
      Work.erase(First);
      NodeChildrenMap::iterator CF = NCM.find(N);
      if (CF != NCM.end()) {
        llvm::append_range(Work, CF->second);
        Nodes.insert(CF->second.begin(), CF->second.end());
      }
    }
}

namespace {

  using NodeSymRel = std::set<NodeSet>;
  using NodePair = std::pair<GepNode *, GepNode *>;
  using NodePairSet = std::set<NodePair>;

} // end anonymous namespace

static const NodeSet *node_class(GepNode *N, NodeSymRel &Rel) {
  for (const NodeSet &S : Rel)
    if (S.count(N))
      return &S;
  return nullptr;
}

  // Create an ordered pair of GepNode pointers. The pair will be used in
  // determining equality. The only purpose of the ordering is to eliminate
  // duplication due to the commutativity of equality/non-equality.
static NodePair node_pair(GepNode *N1, GepNode *N2) {
  uintptr_t P1 = reinterpret_cast<uintptr_t>(N1);
  uintptr_t P2 = reinterpret_cast<uintptr_t>(N2);
  if (P1 <= P2)
    return std::make_pair(N1, N2);
  return std::make_pair(N2, N1);
}

static unsigned node_hash(GepNode *N) {
    // Include everything except flags and parent.
    FoldingSetNodeID ID;
    ID.AddPointer(N->Idx);
    ID.AddPointer(N->PTy);
    return ID.ComputeHash();
}

static bool node_eq(GepNode *N1, GepNode *N2, NodePairSet &Eq,
                    NodePairSet &Ne) {
    // Don't cache the result for nodes with different hashes. The hash
    // comparison is fast enough.
    if (node_hash(N1) != node_hash(N2))
      return false;

    NodePair NP = node_pair(N1, N2);
    NodePairSet::iterator FEq = Eq.find(NP);
    if (FEq != Eq.end())
      return true;
    NodePairSet::iterator FNe = Ne.find(NP);
    if (FNe != Ne.end())
      return false;
    // Not previously compared.
    bool Root1 = N1->Flags & GepNode::Root;
    uint32_t CmpFlags = GepNode::Root | GepNode::Pointer;
    bool Different = (N1->Flags & CmpFlags) != (N2->Flags & CmpFlags);
    NodePair P = node_pair(N1, N2);
    // If the root/pointer flags have different values, the nodes are
    // different.
    // If both nodes are root nodes, but their base pointers differ,
    // they are different.
    if (Different || (Root1 && N1->BaseVal != N2->BaseVal)) {
      Ne.insert(P);
      return false;
    }
    // Here the root/pointer flags are identical, and for root nodes the
    // base pointers are equal, so the root nodes are equal.
    // For non-root nodes, compare their parent nodes.
    if (Root1 || node_eq(N1->Parent, N2->Parent, Eq, Ne)) {
      Eq.insert(P);
      return true;
    }
    return false;
}

void HexagonCommonGEP::common() {
  // The essence of this commoning is finding gep nodes that are equal.
  // To do this we need to compare all pairs of nodes. To save time,
  // first, partition the set of all nodes into sets of potentially equal
  // nodes, and then compare pairs from within each partition.
  using NodeSetMap = std::map<unsigned, NodeSet>;
  NodeSetMap MaybeEq;

  for (GepNode *N : Nodes) {
    unsigned H = node_hash(N);
    MaybeEq[H].insert(N);
  }

  // Compute the equivalence relation for the gep nodes.  Use two caches,
  // one for equality and the other for non-equality.
  NodeSymRel EqRel;  // Equality relation (as set of equivalence classes).
  NodePairSet Eq, Ne;  // Caches.
  for (auto &I : MaybeEq) {
    NodeSet &S = I.second;
    for (NodeSet::iterator NI = S.begin(), NE = S.end(); NI != NE; ++NI) {
      GepNode *N = *NI;
      // If node already has a class, then the class must have been created
      // in a prior iteration of this loop. Since equality is transitive,
      // nothing more will be added to that class, so skip it.
      if (node_class(N, EqRel))
        continue;

      // Create a new class candidate now.
      NodeSet C;
      for (NodeSet::iterator NJ = std::next(NI); NJ != NE; ++NJ)
        if (node_eq(N, *NJ, Eq, Ne))
          C.insert(*NJ);
      // If Tmp is empty, N would be the only element in it. Don't bother
      // creating a class for it then.
      if (!C.empty()) {
        C.insert(N);  // Finalize the set before adding it to the relation.
        std::pair<NodeSymRel::iterator, bool> Ins = EqRel.insert(C);
        (void)Ins;
        assert(Ins.second && "Cannot add a class");
      }
    }
  }

  LLVM_DEBUG({
    dbgs() << "Gep node equality:\n";
    for (NodePairSet::iterator I = Eq.begin(), E = Eq.end(); I != E; ++I)
      dbgs() << "{ " << I->first << ", " << I->second << " }\n";

    dbgs() << "Gep equivalence classes:\n";
    for (const NodeSet &S : EqRel) {
      dbgs() << '{';
      for (NodeSet::const_iterator J = S.begin(), F = S.end(); J != F; ++J) {
        if (J != S.begin())
          dbgs() << ',';
        dbgs() << ' ' << *J;
      }
      dbgs() << " }\n";
    }
  });

  // Create a projection from a NodeSet to the minimal element in it.
  using ProjMap = std::map<const NodeSet *, GepNode *>;
  ProjMap PM;
  for (const NodeSet &S : EqRel) {
    GepNode *Min = *llvm::min_element(S, NodeOrder);
    std::pair<ProjMap::iterator,bool> Ins = PM.insert(std::make_pair(&S, Min));
    (void)Ins;
    assert(Ins.second && "Cannot add minimal element");

    // Update the min element's flags, and user list.
    uint32_t Flags = 0;
    UseSet &MinUs = Uses[Min];
    for (GepNode *N : S) {
      uint32_t NF = N->Flags;
      // If N is used, append all original values of N to the list of
      // original values of Min.
      if (NF & GepNode::Used)
        MinUs.insert(Uses[N].begin(), Uses[N].end());
      Flags |= NF;
    }
    if (MinUs.empty())
      Uses.erase(Min);

    // The collected flags should include all the flags from the min element.
    assert((Min->Flags & Flags) == Min->Flags);
    Min->Flags = Flags;
  }

  // Commoning: for each non-root gep node, replace "Parent" with the
  // selected (minimum) node from the corresponding equivalence class.
  // If a given parent does not have an equivalence class, leave it
  // unchanged (it means that it's the only element in its class).
  for (GepNode *N : Nodes) {
    if (N->Flags & GepNode::Root)
      continue;
    const NodeSet *PC = node_class(N->Parent, EqRel);
    if (!PC)
      continue;
    ProjMap::iterator F = PM.find(PC);
    if (F == PM.end())
      continue;
    // Found a replacement, use it.
    GepNode *Rep = F->second;
    N->Parent = Rep;
  }

  LLVM_DEBUG(dbgs() << "Gep nodes after commoning:\n" << Nodes);

  // Finally, erase the nodes that are no longer used.
  NodeSet Erase;
  for (GepNode *N : Nodes) {
    const NodeSet *PC = node_class(N, EqRel);
    if (!PC)
      continue;
    ProjMap::iterator F = PM.find(PC);
    if (F == PM.end())
      continue;
    if (N == F->second)
      continue;
    // Node for removal.
    Erase.insert(N);
  }
  erase_if(Nodes, in_set(Erase));

  LLVM_DEBUG(dbgs() << "Gep nodes after post-commoning cleanup:\n" << Nodes);
}

template <typename T>
static BasicBlock *nearest_common_dominator(DominatorTree *DT, T &Blocks) {
  LLVM_DEBUG({
    dbgs() << "NCD of {";
    for (typename T::iterator I = Blocks.begin(), E = Blocks.end(); I != E;
         ++I) {
      if (!*I)
        continue;
      BasicBlock *B = cast<BasicBlock>(*I);
      dbgs() << ' ' << B->getName();
    }
    dbgs() << " }\n";
  });

  // Allow null basic blocks in Blocks.  In such cases, return nullptr.
  typename T::iterator I = Blocks.begin(), E = Blocks.end();
  if (I == E || !*I)
    return nullptr;
  BasicBlock *Dom = cast<BasicBlock>(*I);
  while (++I != E) {
    BasicBlock *B = cast_or_null<BasicBlock>(*I);
    Dom = B ? DT->findNearestCommonDominator(Dom, B) : nullptr;
    if (!Dom)
      return nullptr;
    }
    LLVM_DEBUG(dbgs() << "computed:" << Dom->getName() << '\n');
    return Dom;
}

template <typename T>
static BasicBlock *nearest_common_dominatee(DominatorTree *DT, T &Blocks) {
    // If two blocks, A and B, dominate a block C, then A dominates B,
    // or B dominates A.
    typename T::iterator I = Blocks.begin(), E = Blocks.end();
    // Find the first non-null block.
    while (I != E && !*I)
      ++I;
    if (I == E)
      return DT->getRoot();
    BasicBlock *DomB = cast<BasicBlock>(*I);
    while (++I != E) {
      if (!*I)
        continue;
      BasicBlock *B = cast<BasicBlock>(*I);
      if (DT->dominates(B, DomB))
        continue;
      if (!DT->dominates(DomB, B))
        return nullptr;
      DomB = B;
    }
    return DomB;
}

// Find the first use in B of any value from Values. If no such use,
// return B->end().
template <typename T>
static BasicBlock::iterator first_use_of_in_block(T &Values, BasicBlock *B) {
    BasicBlock::iterator FirstUse = B->end(), BEnd = B->end();

    using iterator = typename T::iterator;

    for (iterator I = Values.begin(), E = Values.end(); I != E; ++I) {
      Value *V = *I;
      // If V is used in a PHI node, the use belongs to the incoming block,
      // not the block with the PHI node. In the incoming block, the use
      // would be considered as being at the end of it, so it cannot
      // influence the position of the first use (which is assumed to be
      // at the end to start with).
      if (isa<PHINode>(V))
        continue;
      if (!isa<Instruction>(V))
        continue;
      Instruction *In = cast<Instruction>(V);
      if (In->getParent() != B)
        continue;
      BasicBlock::iterator It = In->getIterator();
      if (std::distance(FirstUse, BEnd) < std::distance(It, BEnd))
        FirstUse = It;
    }
    return FirstUse;
}

static bool is_empty(const BasicBlock *B) {
    return B->empty() || (&*B->begin() == B->getTerminator());
}

BasicBlock *HexagonCommonGEP::recalculatePlacement(GepNode *Node,
      NodeChildrenMap &NCM, NodeToValueMap &Loc) {
  LLVM_DEBUG(dbgs() << "Loc for node:" << Node << '\n');
  // Recalculate the placement for Node, assuming that the locations of
  // its children in Loc are valid.
  // Return nullptr if there is no valid placement for Node (for example, it
  // uses an index value that is not available at the location required
  // to dominate all children, etc.).

  // Find the nearest common dominator for:
  // - all users, if the node is used, and
  // - all children.
  ValueVect Bs;
  if (Node->Flags & GepNode::Used) {
    // Append all blocks with uses of the original values to the
    // block vector Bs.
    NodeToUsesMap::iterator UF = Uses.find(Node);
    assert(UF != Uses.end() && "Used node with no use information");
    UseSet &Us = UF->second;
    for (Use *U : Us) {
      User *R = U->getUser();
      if (!isa<Instruction>(R))
        continue;
      BasicBlock *PB = isa<PHINode>(R)
          ? cast<PHINode>(R)->getIncomingBlock(*U)
          : cast<Instruction>(R)->getParent();
      Bs.push_back(PB);
    }
  }
  // Append the location of each child.
  NodeChildrenMap::iterator CF = NCM.find(Node);
  if (CF != NCM.end()) {
    NodeVect &Cs = CF->second;
    for (GepNode *CN : Cs) {
      NodeToValueMap::iterator LF = Loc.find(CN);
      // If the child is only used in GEP instructions (i.e. is not used in
      // non-GEP instructions), the nearest dominator computed for it may
      // have been null. In such case it won't have a location available.
      if (LF == Loc.end())
        continue;
      Bs.push_back(LF->second);
    }
  }

  BasicBlock *DomB = nearest_common_dominator(DT, Bs);
  if (!DomB)
    return nullptr;
  // Check if the index used by Node dominates the computed dominator.
  Instruction *IdxI = dyn_cast<Instruction>(Node->Idx);
  if (IdxI && !DT->dominates(IdxI->getParent(), DomB))
    return nullptr;

  // Avoid putting nodes into empty blocks.
  while (is_empty(DomB)) {
    DomTreeNode *N = (*DT)[DomB]->getIDom();
    if (!N)
      break;
    DomB = N->getBlock();
  }

  // Otherwise, DomB is fine. Update the location map.
  Loc[Node] = DomB;
  return DomB;
}

BasicBlock *HexagonCommonGEP::recalculatePlacementRec(GepNode *Node,
      NodeChildrenMap &NCM, NodeToValueMap &Loc) {
  LLVM_DEBUG(dbgs() << "LocRec begin for node:" << Node << '\n');
  // Recalculate the placement of Node, after recursively recalculating the
  // placements of all its children.
  NodeChildrenMap::iterator CF = NCM.find(Node);
  if (CF != NCM.end()) {
    NodeVect &Cs = CF->second;
    for (GepNode *C : Cs)
      recalculatePlacementRec(C, NCM, Loc);
  }
  BasicBlock *LB = recalculatePlacement(Node, NCM, Loc);
  LLVM_DEBUG(dbgs() << "LocRec end for node:" << Node << '\n');
  return LB;
}

bool HexagonCommonGEP::isInvariantIn(Value *Val, Loop *L) {
  if (isa<Constant>(Val) || isa<Argument>(Val))
    return true;
  Instruction *In = dyn_cast<Instruction>(Val);
  if (!In)
    return false;
  BasicBlock *HdrB = L->getHeader(), *DefB = In->getParent();
  return DT->properlyDominates(DefB, HdrB);
}

bool HexagonCommonGEP::isInvariantIn(GepNode *Node, Loop *L) {
  if (Node->Flags & GepNode::Root)
    if (!isInvariantIn(Node->BaseVal, L))
      return false;
  return isInvariantIn(Node->Idx, L);
}

bool HexagonCommonGEP::isInMainPath(BasicBlock *B, Loop *L) {
  BasicBlock *HB = L->getHeader();
  BasicBlock *LB = L->getLoopLatch();
  // B must post-dominate the loop header or dominate the loop latch.
  if (PDT->dominates(B, HB))
    return true;
  if (LB && DT->dominates(B, LB))
    return true;
  return false;
}

static BasicBlock *preheader(DominatorTree *DT, Loop *L) {
  if (BasicBlock *PH = L->getLoopPreheader())
    return PH;
  if (!OptSpeculate)
    return nullptr;
  DomTreeNode *DN = DT->getNode(L->getHeader());
  if (!DN)
    return nullptr;
  return DN->getIDom()->getBlock();
}

BasicBlock *HexagonCommonGEP::adjustForInvariance(GepNode *Node,
      NodeChildrenMap &NCM, NodeToValueMap &Loc) {
  // Find the "topmost" location for Node: it must be dominated by both,
  // its parent (or the BaseVal, if it's a root node), and by the index
  // value.
  ValueVect Bs;
  if (Node->Flags & GepNode::Root) {
    if (Instruction *PIn = dyn_cast<Instruction>(Node->BaseVal))
      Bs.push_back(PIn->getParent());
  } else {
    Bs.push_back(Loc[Node->Parent]);
  }
  if (Instruction *IIn = dyn_cast<Instruction>(Node->Idx))
    Bs.push_back(IIn->getParent());
  BasicBlock *TopB = nearest_common_dominatee(DT, Bs);

  // Traverse the loop nest upwards until we find a loop in which Node
  // is no longer invariant, or until we get to the upper limit of Node's
  // placement. The traversal will also stop when a suitable "preheader"
  // cannot be found for a given loop. The "preheader" may actually be
  // a regular block outside of the loop (i.e. not guarded), in which case
  // the Node will be speculated.
  // For nodes that are not in the main path of the containing loop (i.e.
  // are not executed in each iteration), do not move them out of the loop.
  BasicBlock *LocB = cast_or_null<BasicBlock>(Loc[Node]);
  if (LocB) {
    Loop *Lp = LI->getLoopFor(LocB);
    while (Lp) {
      if (!isInvariantIn(Node, Lp) || !isInMainPath(LocB, Lp))
        break;
      BasicBlock *NewLoc = preheader(DT, Lp);
      if (!NewLoc || !DT->dominates(TopB, NewLoc))
        break;
      Lp = Lp->getParentLoop();
      LocB = NewLoc;
    }
  }
  Loc[Node] = LocB;

  // Recursively compute the locations of all children nodes.
  NodeChildrenMap::iterator CF = NCM.find(Node);
  if (CF != NCM.end()) {
    NodeVect &Cs = CF->second;
    for (GepNode *C : Cs)
      adjustForInvariance(C, NCM, Loc);
  }
  return LocB;
}

namespace {

  struct LocationAsBlock {
    LocationAsBlock(const NodeToValueMap &L) : Map(L) {}

    const NodeToValueMap &Map;
  };

  raw_ostream &operator<< (raw_ostream &OS,
                           const LocationAsBlock &Loc) LLVM_ATTRIBUTE_UNUSED ;
  raw_ostream &operator<< (raw_ostream &OS, const LocationAsBlock &Loc) {
    for (const auto &I : Loc.Map) {
      OS << I.first << " -> ";
      if (BasicBlock *B = cast_or_null<BasicBlock>(I.second))
        OS << B->getName() << '(' << B << ')';
      else
        OS << "<null-block>";
      OS << '\n';
    }
    return OS;
  }

  inline bool is_constant(GepNode *N) {
    return isa<ConstantInt>(N->Idx);
  }

} // end anonymous namespace

void HexagonCommonGEP::separateChainForNode(GepNode *Node, Use *U,
      NodeToValueMap &Loc) {
  User *R = U->getUser();
  LLVM_DEBUG(dbgs() << "Separating chain for node (" << Node << ") user: " << *R
                    << '\n');
  BasicBlock *PB = cast<Instruction>(R)->getParent();

  GepNode *N = Node;
  GepNode *C = nullptr, *NewNode = nullptr;
  while (is_constant(N) && !(N->Flags & GepNode::Root)) {
    // XXX if (single-use) dont-replicate;
    GepNode *NewN = new (*Mem) GepNode(N);
    Nodes.push_back(NewN);
    Loc[NewN] = PB;

    if (N == Node)
      NewNode = NewN;
    NewN->Flags &= ~GepNode::Used;
    if (C)
      C->Parent = NewN;
    C = NewN;
    N = N->Parent;
  }
  if (!NewNode)
    return;

  // Move over all uses that share the same user as U from Node to NewNode.
  NodeToUsesMap::iterator UF = Uses.find(Node);
  assert(UF != Uses.end());
  UseSet &Us = UF->second;
  UseSet NewUs;
  for (Use *U : Us) {
    if (U->getUser() == R)
      NewUs.insert(U);
  }
  for (Use *U : NewUs)
    Us.remove(U); // erase takes an iterator.

  if (Us.empty()) {
    Node->Flags &= ~GepNode::Used;
    Uses.erase(UF);
  }

  // Should at least have U in NewUs.
  NewNode->Flags |= GepNode::Used;
  LLVM_DEBUG(dbgs() << "new node: " << NewNode << "  " << *NewNode << '\n');
  assert(!NewUs.empty());
  Uses[NewNode] = NewUs;
}

void HexagonCommonGEP::separateConstantChains(GepNode *Node,
      NodeChildrenMap &NCM, NodeToValueMap &Loc) {
  // First approximation: extract all chains.
  NodeSet Ns;
  nodes_for_root(Node, NCM, Ns);

  LLVM_DEBUG(dbgs() << "Separating constant chains for node: " << Node << '\n');
  // Collect all used nodes together with the uses from loads and stores,
  // where the GEP node could be folded into the load/store instruction.
  NodeToUsesMap FNs; // Foldable nodes.
  for (GepNode *N : Ns) {
    if (!(N->Flags & GepNode::Used))
      continue;
    NodeToUsesMap::iterator UF = Uses.find(N);
    assert(UF != Uses.end());
    UseSet &Us = UF->second;
    // Loads/stores that use the node N.
    UseSet LSs;
    for (Use *U : Us) {
      User *R = U->getUser();
      // We're interested in uses that provide the address. It can happen
      // that the value may also be provided via GEP, but we won't handle
      // those cases here for now.
      if (LoadInst *Ld = dyn_cast<LoadInst>(R)) {
        unsigned PtrX = LoadInst::getPointerOperandIndex();
        if (&Ld->getOperandUse(PtrX) == U)
          LSs.insert(U);
      } else if (StoreInst *St = dyn_cast<StoreInst>(R)) {
        unsigned PtrX = StoreInst::getPointerOperandIndex();
        if (&St->getOperandUse(PtrX) == U)
          LSs.insert(U);
      }
    }
    // Even if the total use count is 1, separating the chain may still be
    // beneficial, since the constant chain may be longer than the GEP alone
    // would be (e.g. if the parent node has a constant index and also has
    // other children).
    if (!LSs.empty())
      FNs.insert(std::make_pair(N, LSs));
  }

  LLVM_DEBUG(dbgs() << "Nodes with foldable users:\n" << FNs);

  for (auto &FN : FNs) {
    GepNode *N = FN.first;
    UseSet &Us = FN.second;
    for (Use *U : Us)
      separateChainForNode(N, U, Loc);
  }
}

void HexagonCommonGEP::computeNodePlacement(NodeToValueMap &Loc) {
  // Compute the inverse of the Node.Parent links. Also, collect the set
  // of root nodes.
  NodeChildrenMap NCM;
  NodeVect Roots;
  invert_find_roots(Nodes, NCM, Roots);

  // Compute the initial placement determined by the users' locations, and
  // the locations of the child nodes.
  for (GepNode *Root : Roots)
    recalculatePlacementRec(Root, NCM, Loc);

  LLVM_DEBUG(dbgs() << "Initial node placement:\n" << LocationAsBlock(Loc));

  if (OptEnableInv) {
    for (GepNode *Root : Roots)
      adjustForInvariance(Root, NCM, Loc);

    LLVM_DEBUG(dbgs() << "Node placement after adjustment for invariance:\n"
                      << LocationAsBlock(Loc));
  }
  if (OptEnableConst) {
    for (GepNode *Root : Roots)
      separateConstantChains(Root, NCM, Loc);
  }
  LLVM_DEBUG(dbgs() << "Node use information:\n" << Uses);

  // At the moment, there is no further refinement of the initial placement.
  // Such a refinement could include splitting the nodes if they are placed
  // too far from some of its users.

  LLVM_DEBUG(dbgs() << "Final node placement:\n" << LocationAsBlock(Loc));
}

Value *HexagonCommonGEP::fabricateGEP(NodeVect &NA, BasicBlock::iterator At,
      BasicBlock *LocB) {
  LLVM_DEBUG(dbgs() << "Fabricating GEP in " << LocB->getName()
                    << " for nodes:\n"
                    << NA);
  unsigned Num = NA.size();
  GepNode *RN = NA[0];
  assert((RN->Flags & GepNode::Root) && "Creating GEP for non-root");

  GetElementPtrInst *NewInst = nullptr;
  Value *Input = RN->BaseVal;
  Type *InpTy = RN->PTy;

  unsigned Idx = 0;
  do {
    SmallVector<Value*, 4> IdxList;
    // If the type of the input of the first node is not a pointer,
    // we need to add an artificial i32 0 to the indices (because the
    // actual input in the IR will be a pointer).
    if (!(NA[Idx]->Flags & GepNode::Pointer)) {
      Type *Int32Ty = Type::getInt32Ty(*Ctx);
      IdxList.push_back(ConstantInt::get(Int32Ty, 0));
    }

    // Keep adding indices from NA until we have to stop and generate
    // an "intermediate" GEP.
    while (++Idx <= Num) {
      GepNode *N = NA[Idx-1];
      IdxList.push_back(N->Idx);
      if (Idx < Num) {
        // We have to stop if we reach a pointer.
        if (NA[Idx]->Flags & GepNode::Pointer)
          break;
      }
    }
    NewInst = GetElementPtrInst::Create(InpTy, Input, IdxList, "cgep", At);
    NewInst->setIsInBounds(RN->Flags & GepNode::InBounds);
    LLVM_DEBUG(dbgs() << "new GEP: " << *NewInst << '\n');
    if (Idx < Num) {
      Input = NewInst;
      InpTy = NA[Idx]->PTy;
    }
  } while (Idx <= Num);

  return NewInst;
}

void HexagonCommonGEP::getAllUsersForNode(GepNode *Node, ValueVect &Values,
      NodeChildrenMap &NCM) {
  NodeVect Work;
  Work.push_back(Node);

  while (!Work.empty()) {
    NodeVect::iterator First = Work.begin();
    GepNode *N = *First;
    Work.erase(First);
    if (N->Flags & GepNode::Used) {
      NodeToUsesMap::iterator UF = Uses.find(N);
      assert(UF != Uses.end() && "No use information for used node");
      UseSet &Us = UF->second;
      for (const auto &U : Us)
        Values.push_back(U->getUser());
    }
    NodeChildrenMap::iterator CF = NCM.find(N);
    if (CF != NCM.end()) {
      NodeVect &Cs = CF->second;
      llvm::append_range(Work, Cs);
    }
  }
}

void HexagonCommonGEP::materialize(NodeToValueMap &Loc) {
  LLVM_DEBUG(dbgs() << "Nodes before materialization:\n" << Nodes << '\n');
  NodeChildrenMap NCM;
  NodeVect Roots;
  // Compute the inversion again, since computing placement could alter
  // "parent" relation between nodes.
  invert_find_roots(Nodes, NCM, Roots);

  while (!Roots.empty()) {
    NodeVect::iterator First = Roots.begin();
    GepNode *Root = *First, *Last = *First;
    Roots.erase(First);

    NodeVect NA;  // Nodes to assemble.
    // Append to NA all child nodes up to (and including) the first child
    // that:
    // (1) has more than 1 child, or
    // (2) is used, or
    // (3) has a child located in a different block.
    bool LastUsed = false;
    unsigned LastCN = 0;
    // The location may be null if the computation failed (it can legitimately
    // happen for nodes created from dead GEPs).
    Value *LocV = Loc[Last];
    if (!LocV)
      continue;
    BasicBlock *LastB = cast<BasicBlock>(LocV);
    do {
      NA.push_back(Last);
      LastUsed = (Last->Flags & GepNode::Used);
      if (LastUsed)
        break;
      NodeChildrenMap::iterator CF = NCM.find(Last);
      LastCN = (CF != NCM.end()) ? CF->second.size() : 0;
      if (LastCN != 1)
        break;
      GepNode *Child = CF->second.front();
      BasicBlock *ChildB = cast_or_null<BasicBlock>(Loc[Child]);
      if (ChildB != nullptr && LastB != ChildB)
        break;
      Last = Child;
    } while (true);

    BasicBlock::iterator InsertAt = LastB->getTerminator()->getIterator();
    if (LastUsed || LastCN > 0) {
      ValueVect Urs;
      getAllUsersForNode(Root, Urs, NCM);
      BasicBlock::iterator FirstUse = first_use_of_in_block(Urs, LastB);
      if (FirstUse != LastB->end())
        InsertAt = FirstUse;
    }

    // Generate a new instruction for NA.
    Value *NewInst = fabricateGEP(NA, InsertAt, LastB);

    // Convert all the children of Last node into roots, and append them
    // to the Roots list.
    if (LastCN > 0) {
      NodeVect &Cs = NCM[Last];
      for (GepNode *CN : Cs) {
        CN->Flags &= ~GepNode::Internal;
        CN->Flags |= GepNode::Root;
        CN->BaseVal = NewInst;
        Roots.push_back(CN);
      }
    }

    // Lastly, if the Last node was used, replace all uses with the new GEP.
    // The uses reference the original GEP values.
    if (LastUsed) {
      NodeToUsesMap::iterator UF = Uses.find(Last);
      assert(UF != Uses.end() && "No use information found");
      UseSet &Us = UF->second;
      for (Use *U : Us)
        U->set(NewInst);
    }
  }
}

void HexagonCommonGEP::removeDeadCode() {
  ValueVect BO;
  BO.push_back(&Fn->front());

  for (unsigned i = 0; i < BO.size(); ++i) {
    BasicBlock *B = cast<BasicBlock>(BO[i]);
    for (auto *DTN : children<DomTreeNode *>(DT->getNode(B)))
      BO.push_back(DTN->getBlock());
  }

  for (Value *V : llvm::reverse(BO)) {
    BasicBlock *B = cast<BasicBlock>(V);
    ValueVect Ins;
    for (Instruction &I : llvm::reverse(*B))
      Ins.push_back(&I);
    for (Value *I : Ins) {
      Instruction *In = cast<Instruction>(I);
      if (isInstructionTriviallyDead(In))
        In->eraseFromParent();
    }
  }
}

bool HexagonCommonGEP::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  // For now bail out on C++ exception handling.
  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB)
      if (isa<InvokeInst>(I) || isa<LandingPadInst>(I))
        return false;

  Fn = &F;
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  PDT = &getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  Ctx = &F.getContext();

  Nodes.clear();
  Uses.clear();
  NodeOrder.clear();

  SpecificBumpPtrAllocator<GepNode> Allocator;
  Mem = &Allocator;

  collect();
  common();

  NodeToValueMap Loc;
  computeNodePlacement(Loc);
  materialize(Loc);
  removeDeadCode();

#ifdef EXPENSIVE_CHECKS
  // Run this only when expensive checks are enabled.
  if (verifyFunction(F, &dbgs()))
    report_fatal_error("Broken function");
#endif
  return true;
}

namespace llvm {

  FunctionPass *createHexagonCommonGEP() {
    return new HexagonCommonGEP();
  }

} // end namespace llvm
