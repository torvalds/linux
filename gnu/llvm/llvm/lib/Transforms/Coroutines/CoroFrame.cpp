//===- CoroFrame.cpp - Builds and manipulates coroutine frame -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file contains classes used to discover if for a particular value
// there from sue to definition that crosses a suspend block.
//
// Using the information discovered we form a Coroutine Frame structure to
// contain those values. All uses of those values are replaced with appropriate
// GEP + load from the coroutine frame. At the point of the definition we spill
// the value into the coroutine frame.
//===----------------------------------------------------------------------===//

#include "CoroInternal.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/PtrUseVisitor.h"
#include "llvm/Analysis/StackLifetime.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/OptimizedStructLayout.h"
#include "llvm/Support/circular_raw_ostream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <algorithm>
#include <deque>
#include <optional>

using namespace llvm;

extern cl::opt<bool> UseNewDbgInfoFormat;

// The "coro-suspend-crossing" flag is very noisy. There is another debug type,
// "coro-frame", which results in leaner debug spew.
#define DEBUG_TYPE "coro-suspend-crossing"

enum { SmallVectorThreshold = 32 };

// Provides two way mapping between the blocks and numbers.
namespace {
class BlockToIndexMapping {
  SmallVector<BasicBlock *, SmallVectorThreshold> V;

public:
  size_t size() const { return V.size(); }

  BlockToIndexMapping(Function &F) {
    for (BasicBlock &BB : F)
      V.push_back(&BB);
    llvm::sort(V);
  }

  size_t blockToIndex(BasicBlock const *BB) const {
    auto *I = llvm::lower_bound(V, BB);
    assert(I != V.end() && *I == BB && "BasicBlockNumberng: Unknown block");
    return I - V.begin();
  }

  BasicBlock *indexToBlock(unsigned Index) const { return V[Index]; }
};
} // end anonymous namespace

// The SuspendCrossingInfo maintains data that allows to answer a question
// whether given two BasicBlocks A and B there is a path from A to B that
// passes through a suspend point.
//
// For every basic block 'i' it maintains a BlockData that consists of:
//   Consumes:  a bit vector which contains a set of indices of blocks that can
//              reach block 'i'. A block can trivially reach itself.
//   Kills: a bit vector which contains a set of indices of blocks that can
//          reach block 'i' but there is a path crossing a suspend point
//          not repeating 'i' (path to 'i' without cycles containing 'i').
//   Suspend: a boolean indicating whether block 'i' contains a suspend point.
//   End: a boolean indicating whether block 'i' contains a coro.end intrinsic.
//   KillLoop: There is a path from 'i' to 'i' not otherwise repeating 'i' that
//             crosses a suspend point.
//
namespace {
class SuspendCrossingInfo {
  BlockToIndexMapping Mapping;

  struct BlockData {
    BitVector Consumes;
    BitVector Kills;
    bool Suspend = false;
    bool End = false;
    bool KillLoop = false;
    bool Changed = false;
  };
  SmallVector<BlockData, SmallVectorThreshold> Block;

  iterator_range<pred_iterator> predecessors(BlockData const &BD) const {
    BasicBlock *BB = Mapping.indexToBlock(&BD - &Block[0]);
    return llvm::predecessors(BB);
  }

  BlockData &getBlockData(BasicBlock *BB) {
    return Block[Mapping.blockToIndex(BB)];
  }

  /// Compute the BlockData for the current function in one iteration.
  /// Initialize - Whether this is the first iteration, we can optimize
  /// the initial case a little bit by manual loop switch.
  /// Returns whether the BlockData changes in this iteration.
  template <bool Initialize = false>
  bool computeBlockData(const ReversePostOrderTraversal<Function *> &RPOT);

public:
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const;
  void dump(StringRef Label, BitVector const &BV) const;
#endif

  SuspendCrossingInfo(Function &F, coro::Shape &Shape);

  /// Returns true if there is a path from \p From to \p To crossing a suspend
  /// point without crossing \p From a 2nd time.
  bool hasPathCrossingSuspendPoint(BasicBlock *From, BasicBlock *To) const {
    size_t const FromIndex = Mapping.blockToIndex(From);
    size_t const ToIndex = Mapping.blockToIndex(To);
    bool const Result = Block[ToIndex].Kills[FromIndex];
    LLVM_DEBUG(dbgs() << From->getName() << " => " << To->getName()
                      << " answer is " << Result << "\n");
    return Result;
  }

  /// Returns true if there is a path from \p From to \p To crossing a suspend
  /// point without crossing \p From a 2nd time. If \p From is the same as \p To
  /// this will also check if there is a looping path crossing a suspend point.
  bool hasPathOrLoopCrossingSuspendPoint(BasicBlock *From,
                                         BasicBlock *To) const {
    size_t const FromIndex = Mapping.blockToIndex(From);
    size_t const ToIndex = Mapping.blockToIndex(To);
    bool Result = Block[ToIndex].Kills[FromIndex] ||
                  (From == To && Block[ToIndex].KillLoop);
    LLVM_DEBUG(dbgs() << From->getName() << " => " << To->getName()
                      << " answer is " << Result << " (path or loop)\n");
    return Result;
  }

  bool isDefinitionAcrossSuspend(BasicBlock *DefBB, User *U) const {
    auto *I = cast<Instruction>(U);

    // We rewrote PHINodes, so that only the ones with exactly one incoming
    // value need to be analyzed.
    if (auto *PN = dyn_cast<PHINode>(I))
      if (PN->getNumIncomingValues() > 1)
        return false;

    BasicBlock *UseBB = I->getParent();

    // As a special case, treat uses by an llvm.coro.suspend.retcon or an
    // llvm.coro.suspend.async as if they were uses in the suspend's single
    // predecessor: the uses conceptually occur before the suspend.
    if (isa<CoroSuspendRetconInst>(I) || isa<CoroSuspendAsyncInst>(I)) {
      UseBB = UseBB->getSinglePredecessor();
      assert(UseBB && "should have split coro.suspend into its own block");
    }

    return hasPathCrossingSuspendPoint(DefBB, UseBB);
  }

  bool isDefinitionAcrossSuspend(Argument &A, User *U) const {
    return isDefinitionAcrossSuspend(&A.getParent()->getEntryBlock(), U);
  }

  bool isDefinitionAcrossSuspend(Instruction &I, User *U) const {
    auto *DefBB = I.getParent();

    // As a special case, treat values produced by an llvm.coro.suspend.*
    // as if they were defined in the single successor: the uses
    // conceptually occur after the suspend.
    if (isa<AnyCoroSuspendInst>(I)) {
      DefBB = DefBB->getSingleSuccessor();
      assert(DefBB && "should have split coro.suspend into its own block");
    }

    return isDefinitionAcrossSuspend(DefBB, U);
  }

  bool isDefinitionAcrossSuspend(Value &V, User *U) const {
    if (auto *Arg = dyn_cast<Argument>(&V))
      return isDefinitionAcrossSuspend(*Arg, U);
    if (auto *Inst = dyn_cast<Instruction>(&V))
      return isDefinitionAcrossSuspend(*Inst, U);

    llvm_unreachable(
        "Coroutine could only collect Argument and Instruction now.");
  }
};
} // end anonymous namespace

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SuspendCrossingInfo::dump(StringRef Label,
                                                BitVector const &BV) const {
  dbgs() << Label << ":";
  for (size_t I = 0, N = BV.size(); I < N; ++I)
    if (BV[I])
      dbgs() << " " << Mapping.indexToBlock(I)->getName();
  dbgs() << "\n";
}

LLVM_DUMP_METHOD void SuspendCrossingInfo::dump() const {
  for (size_t I = 0, N = Block.size(); I < N; ++I) {
    BasicBlock *const B = Mapping.indexToBlock(I);
    dbgs() << B->getName() << ":\n";
    dump("   Consumes", Block[I].Consumes);
    dump("      Kills", Block[I].Kills);
  }
  dbgs() << "\n";
}
#endif

template <bool Initialize>
bool SuspendCrossingInfo::computeBlockData(
    const ReversePostOrderTraversal<Function *> &RPOT) {
  bool Changed = false;

  for (const BasicBlock *BB : RPOT) {
    auto BBNo = Mapping.blockToIndex(BB);
    auto &B = Block[BBNo];

    // We don't need to count the predecessors when initialization.
    if constexpr (!Initialize)
      // If all the predecessors of the current Block don't change,
      // the BlockData for the current block must not change too.
      if (all_of(predecessors(B), [this](BasicBlock *BB) {
            return !Block[Mapping.blockToIndex(BB)].Changed;
          })) {
        B.Changed = false;
        continue;
      }

    // Saved Consumes and Kills bitsets so that it is easy to see
    // if anything changed after propagation.
    auto SavedConsumes = B.Consumes;
    auto SavedKills = B.Kills;

    for (BasicBlock *PI : predecessors(B)) {
      auto PrevNo = Mapping.blockToIndex(PI);
      auto &P = Block[PrevNo];

      // Propagate Kills and Consumes from predecessors into B.
      B.Consumes |= P.Consumes;
      B.Kills |= P.Kills;

      // If block P is a suspend block, it should propagate kills into block
      // B for every block P consumes.
      if (P.Suspend)
        B.Kills |= P.Consumes;
    }

    if (B.Suspend) {
      // If block B is a suspend block, it should kill all of the blocks it
      // consumes.
      B.Kills |= B.Consumes;
    } else if (B.End) {
      // If block B is an end block, it should not propagate kills as the
      // blocks following coro.end() are reached during initial invocation
      // of the coroutine while all the data are still available on the
      // stack or in the registers.
      B.Kills.reset();
    } else {
      // This is reached when B block it not Suspend nor coro.end and it
      // need to make sure that it is not in the kill set.
      B.KillLoop |= B.Kills[BBNo];
      B.Kills.reset(BBNo);
    }

    if constexpr (!Initialize) {
      B.Changed = (B.Kills != SavedKills) || (B.Consumes != SavedConsumes);
      Changed |= B.Changed;
    }
  }

  return Changed;
}

SuspendCrossingInfo::SuspendCrossingInfo(Function &F, coro::Shape &Shape)
    : Mapping(F) {
  const size_t N = Mapping.size();
  Block.resize(N);

  // Initialize every block so that it consumes itself
  for (size_t I = 0; I < N; ++I) {
    auto &B = Block[I];
    B.Consumes.resize(N);
    B.Kills.resize(N);
    B.Consumes.set(I);
    B.Changed = true;
  }

  // Mark all CoroEnd Blocks. We do not propagate Kills beyond coro.ends as
  // the code beyond coro.end is reachable during initial invocation of the
  // coroutine.
  for (auto *CE : Shape.CoroEnds)
    getBlockData(CE->getParent()).End = true;

  // Mark all suspend blocks and indicate that they kill everything they
  // consume. Note, that crossing coro.save also requires a spill, as any code
  // between coro.save and coro.suspend may resume the coroutine and all of the
  // state needs to be saved by that time.
  auto markSuspendBlock = [&](IntrinsicInst *BarrierInst) {
    BasicBlock *SuspendBlock = BarrierInst->getParent();
    auto &B = getBlockData(SuspendBlock);
    B.Suspend = true;
    B.Kills |= B.Consumes;
  };
  for (auto *CSI : Shape.CoroSuspends) {
    markSuspendBlock(CSI);
    if (auto *Save = CSI->getCoroSave())
      markSuspendBlock(Save);
  }

  // It is considered to be faster to use RPO traversal for forward-edges
  // dataflow analysis.
  ReversePostOrderTraversal<Function *> RPOT(&F);
  computeBlockData</*Initialize=*/true>(RPOT);
  while (computeBlockData</*Initialize*/ false>(RPOT))
    ;

  LLVM_DEBUG(dump());
}

namespace {

// RematGraph is used to construct a DAG for rematerializable instructions
// When the constructor is invoked with a candidate instruction (which is
// materializable) it builds a DAG of materializable instructions from that
// point.
// Typically, for each instruction identified as re-materializable across a
// suspend point, a RematGraph will be created.
struct RematGraph {
  // Each RematNode in the graph contains the edges to instructions providing
  // operands in the current node.
  struct RematNode {
    Instruction *Node;
    SmallVector<RematNode *> Operands;
    RematNode() = default;
    RematNode(Instruction *V) : Node(V) {}
  };

  RematNode *EntryNode;
  using RematNodeMap =
      SmallMapVector<Instruction *, std::unique_ptr<RematNode>, 8>;
  RematNodeMap Remats;
  const std::function<bool(Instruction &)> &MaterializableCallback;
  SuspendCrossingInfo &Checker;

  RematGraph(const std::function<bool(Instruction &)> &MaterializableCallback,
             Instruction *I, SuspendCrossingInfo &Checker)
      : MaterializableCallback(MaterializableCallback), Checker(Checker) {
    std::unique_ptr<RematNode> FirstNode = std::make_unique<RematNode>(I);
    EntryNode = FirstNode.get();
    std::deque<std::unique_ptr<RematNode>> WorkList;
    addNode(std::move(FirstNode), WorkList, cast<User>(I));
    while (WorkList.size()) {
      std::unique_ptr<RematNode> N = std::move(WorkList.front());
      WorkList.pop_front();
      addNode(std::move(N), WorkList, cast<User>(I));
    }
  }

  void addNode(std::unique_ptr<RematNode> NUPtr,
               std::deque<std::unique_ptr<RematNode>> &WorkList,
               User *FirstUse) {
    RematNode *N = NUPtr.get();
    if (Remats.count(N->Node))
      return;

    // We haven't see this node yet - add to the list
    Remats[N->Node] = std::move(NUPtr);
    for (auto &Def : N->Node->operands()) {
      Instruction *D = dyn_cast<Instruction>(Def.get());
      if (!D || !MaterializableCallback(*D) ||
          !Checker.isDefinitionAcrossSuspend(*D, FirstUse))
        continue;

      if (Remats.count(D)) {
        // Already have this in the graph
        N->Operands.push_back(Remats[D].get());
        continue;
      }

      bool NoMatch = true;
      for (auto &I : WorkList) {
        if (I->Node == D) {
          NoMatch = false;
          N->Operands.push_back(I.get());
          break;
        }
      }
      if (NoMatch) {
        // Create a new node
        std::unique_ptr<RematNode> ChildNode = std::make_unique<RematNode>(D);
        N->Operands.push_back(ChildNode.get());
        WorkList.push_back(std::move(ChildNode));
      }
    }
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const {
    dbgs() << "Entry (";
    if (EntryNode->Node->getParent()->hasName())
      dbgs() << EntryNode->Node->getParent()->getName();
    else
      EntryNode->Node->getParent()->printAsOperand(dbgs(), false);
    dbgs() << ") : " << *EntryNode->Node << "\n";
    for (auto &E : Remats) {
      dbgs() << *(E.first) << "\n";
      for (RematNode *U : E.second->Operands)
        dbgs() << "  " << *U->Node << "\n";
    }
  }
#endif
};
} // end anonymous namespace

namespace llvm {

template <> struct GraphTraits<RematGraph *> {
  using NodeRef = RematGraph::RematNode *;
  using ChildIteratorType = RematGraph::RematNode **;

  static NodeRef getEntryNode(RematGraph *G) { return G->EntryNode; }
  static ChildIteratorType child_begin(NodeRef N) {
    return N->Operands.begin();
  }
  static ChildIteratorType child_end(NodeRef N) { return N->Operands.end(); }
};

} // end namespace llvm

#undef DEBUG_TYPE // "coro-suspend-crossing"
#define DEBUG_TYPE "coro-frame"

namespace {
class FrameTypeBuilder;
// Mapping from the to-be-spilled value to all the users that need reload.
using SpillInfo = SmallMapVector<Value *, SmallVector<Instruction *, 2>, 8>;
struct AllocaInfo {
  AllocaInst *Alloca;
  DenseMap<Instruction *, std::optional<APInt>> Aliases;
  bool MayWriteBeforeCoroBegin;
  AllocaInfo(AllocaInst *Alloca,
             DenseMap<Instruction *, std::optional<APInt>> Aliases,
             bool MayWriteBeforeCoroBegin)
      : Alloca(Alloca), Aliases(std::move(Aliases)),
        MayWriteBeforeCoroBegin(MayWriteBeforeCoroBegin) {}
};
struct FrameDataInfo {
  // All the values (that are not allocas) that needs to be spilled to the
  // frame.
  SpillInfo Spills;
  // Allocas contains all values defined as allocas that need to live in the
  // frame.
  SmallVector<AllocaInfo, 8> Allocas;

  SmallVector<Value *, 8> getAllDefs() const {
    SmallVector<Value *, 8> Defs;
    for (const auto &P : Spills)
      Defs.push_back(P.first);
    for (const auto &A : Allocas)
      Defs.push_back(A.Alloca);
    return Defs;
  }

  uint32_t getFieldIndex(Value *V) const {
    auto Itr = FieldIndexMap.find(V);
    assert(Itr != FieldIndexMap.end() &&
           "Value does not have a frame field index");
    return Itr->second;
  }

  void setFieldIndex(Value *V, uint32_t Index) {
    assert((LayoutIndexUpdateStarted || FieldIndexMap.count(V) == 0) &&
           "Cannot set the index for the same field twice.");
    FieldIndexMap[V] = Index;
  }

  Align getAlign(Value *V) const {
    auto Iter = FieldAlignMap.find(V);
    assert(Iter != FieldAlignMap.end());
    return Iter->second;
  }

  void setAlign(Value *V, Align AL) {
    assert(FieldAlignMap.count(V) == 0);
    FieldAlignMap.insert({V, AL});
  }

  uint64_t getDynamicAlign(Value *V) const {
    auto Iter = FieldDynamicAlignMap.find(V);
    assert(Iter != FieldDynamicAlignMap.end());
    return Iter->second;
  }

  void setDynamicAlign(Value *V, uint64_t Align) {
    assert(FieldDynamicAlignMap.count(V) == 0);
    FieldDynamicAlignMap.insert({V, Align});
  }

  uint64_t getOffset(Value *V) const {
    auto Iter = FieldOffsetMap.find(V);
    assert(Iter != FieldOffsetMap.end());
    return Iter->second;
  }

  void setOffset(Value *V, uint64_t Offset) {
    assert(FieldOffsetMap.count(V) == 0);
    FieldOffsetMap.insert({V, Offset});
  }

  // Remap the index of every field in the frame, using the final layout index.
  void updateLayoutIndex(FrameTypeBuilder &B);

private:
  // LayoutIndexUpdateStarted is used to avoid updating the index of any field
  // twice by mistake.
  bool LayoutIndexUpdateStarted = false;
  // Map from values to their slot indexes on the frame. They will be first set
  // with their original insertion field index. After the frame is built, their
  // indexes will be updated into the final layout index.
  DenseMap<Value *, uint32_t> FieldIndexMap;
  // Map from values to their alignment on the frame. They would be set after
  // the frame is built.
  DenseMap<Value *, Align> FieldAlignMap;
  DenseMap<Value *, uint64_t> FieldDynamicAlignMap;
  // Map from values to their offset on the frame. They would be set after
  // the frame is built.
  DenseMap<Value *, uint64_t> FieldOffsetMap;
};
} // namespace

#ifndef NDEBUG
static void dumpSpills(StringRef Title, const SpillInfo &Spills) {
  dbgs() << "------------- " << Title << "--------------\n";
  for (const auto &E : Spills) {
    E.first->dump();
    dbgs() << "   user: ";
    for (auto *I : E.second)
      I->dump();
  }
}
static void dumpRemats(
    StringRef Title,
    const SmallMapVector<Instruction *, std::unique_ptr<RematGraph>, 8> &RM) {
  dbgs() << "------------- " << Title << "--------------\n";
  for (const auto &E : RM) {
    E.second->dump();
    dbgs() << "--\n";
  }
}

static void dumpAllocas(const SmallVectorImpl<AllocaInfo> &Allocas) {
  dbgs() << "------------- Allocas --------------\n";
  for (const auto &A : Allocas) {
    A.Alloca->dump();
  }
}
#endif

namespace {
using FieldIDType = size_t;
// We cannot rely solely on natural alignment of a type when building a
// coroutine frame and if the alignment specified on the Alloca instruction
// differs from the natural alignment of the alloca type we will need to insert
// padding.
class FrameTypeBuilder {
private:
  struct Field {
    uint64_t Size;
    uint64_t Offset;
    Type *Ty;
    FieldIDType LayoutFieldIndex;
    Align Alignment;
    Align TyAlignment;
    uint64_t DynamicAlignBuffer;
  };

  const DataLayout &DL;
  LLVMContext &Context;
  uint64_t StructSize = 0;
  Align StructAlign;
  bool IsFinished = false;

  std::optional<Align> MaxFrameAlignment;

  SmallVector<Field, 8> Fields;
  DenseMap<Value*, unsigned> FieldIndexByKey;

public:
  FrameTypeBuilder(LLVMContext &Context, const DataLayout &DL,
                   std::optional<Align> MaxFrameAlignment)
      : DL(DL), Context(Context), MaxFrameAlignment(MaxFrameAlignment) {}

  /// Add a field to this structure for the storage of an `alloca`
  /// instruction.
  [[nodiscard]] FieldIDType addFieldForAlloca(AllocaInst *AI,
                                              bool IsHeader = false) {
    Type *Ty = AI->getAllocatedType();

    // Make an array type if this is a static array allocation.
    if (AI->isArrayAllocation()) {
      if (auto *CI = dyn_cast<ConstantInt>(AI->getArraySize()))
        Ty = ArrayType::get(Ty, CI->getValue().getZExtValue());
      else
        report_fatal_error("Coroutines cannot handle non static allocas yet");
    }

    return addField(Ty, AI->getAlign(), IsHeader);
  }

  /// We want to put the allocas whose lifetime-ranges are not overlapped
  /// into one slot of coroutine frame.
  /// Consider the example at:https://bugs.llvm.org/show_bug.cgi?id=45566
  ///
  ///     cppcoro::task<void> alternative_paths(bool cond) {
  ///         if (cond) {
  ///             big_structure a;
  ///             process(a);
  ///             co_await something();
  ///         } else {
  ///             big_structure b;
  ///             process2(b);
  ///             co_await something();
  ///         }
  ///     }
  ///
  /// We want to put variable a and variable b in the same slot to
  /// reduce the size of coroutine frame.
  ///
  /// This function use StackLifetime algorithm to partition the AllocaInsts in
  /// Spills to non-overlapped sets in order to put Alloca in the same
  /// non-overlapped set into the same slot in the Coroutine Frame. Then add
  /// field for the allocas in the same non-overlapped set by using the largest
  /// type as the field type.
  ///
  /// Side Effects: Because We sort the allocas, the order of allocas in the
  /// frame may be different with the order in the source code.
  void addFieldForAllocas(const Function &F, FrameDataInfo &FrameData,
                          coro::Shape &Shape);

  /// Add a field to this structure.
  [[nodiscard]] FieldIDType addField(Type *Ty, MaybeAlign MaybeFieldAlignment,
                                     bool IsHeader = false,
                                     bool IsSpillOfValue = false) {
    assert(!IsFinished && "adding fields to a finished builder");
    assert(Ty && "must provide a type for a field");

    // The field size is always the alloc size of the type.
    uint64_t FieldSize = DL.getTypeAllocSize(Ty);

    // For an alloca with size=0, we don't need to add a field and they
    // can just point to any index in the frame. Use index 0.
    if (FieldSize == 0) {
      return 0;
    }

    // The field alignment might not be the type alignment, but we need
    // to remember the type alignment anyway to build the type.
    // If we are spilling values we don't need to worry about ABI alignment
    // concerns.
    Align ABIAlign = DL.getABITypeAlign(Ty);
    Align TyAlignment = ABIAlign;
    if (IsSpillOfValue && MaxFrameAlignment && *MaxFrameAlignment < ABIAlign)
      TyAlignment = *MaxFrameAlignment;
    Align FieldAlignment = MaybeFieldAlignment.value_or(TyAlignment);

    // The field alignment could be bigger than the max frame case, in that case
    // we request additional storage to be able to dynamically align the
    // pointer.
    uint64_t DynamicAlignBuffer = 0;
    if (MaxFrameAlignment && (FieldAlignment > *MaxFrameAlignment)) {
      DynamicAlignBuffer =
          offsetToAlignment(MaxFrameAlignment->value(), FieldAlignment);
      FieldAlignment = *MaxFrameAlignment;
      FieldSize = FieldSize + DynamicAlignBuffer;
    }

    // Lay out header fields immediately.
    uint64_t Offset;
    if (IsHeader) {
      Offset = alignTo(StructSize, FieldAlignment);
      StructSize = Offset + FieldSize;

      // Everything else has a flexible offset.
    } else {
      Offset = OptimizedStructLayoutField::FlexibleOffset;
    }

    Fields.push_back({FieldSize, Offset, Ty, 0, FieldAlignment, TyAlignment,
                      DynamicAlignBuffer});
    return Fields.size() - 1;
  }

  /// Finish the layout and set the body on the given type.
  void finish(StructType *Ty);

  uint64_t getStructSize() const {
    assert(IsFinished && "not yet finished!");
    return StructSize;
  }

  Align getStructAlign() const {
    assert(IsFinished && "not yet finished!");
    return StructAlign;
  }

  FieldIDType getLayoutFieldIndex(FieldIDType Id) const {
    assert(IsFinished && "not yet finished!");
    return Fields[Id].LayoutFieldIndex;
  }

  Field getLayoutField(FieldIDType Id) const {
    assert(IsFinished && "not yet finished!");
    return Fields[Id];
  }
};
} // namespace

void FrameDataInfo::updateLayoutIndex(FrameTypeBuilder &B) {
  auto Updater = [&](Value *I) {
    auto Field = B.getLayoutField(getFieldIndex(I));
    setFieldIndex(I, Field.LayoutFieldIndex);
    setAlign(I, Field.Alignment);
    uint64_t dynamicAlign =
        Field.DynamicAlignBuffer
            ? Field.DynamicAlignBuffer + Field.Alignment.value()
            : 0;
    setDynamicAlign(I, dynamicAlign);
    setOffset(I, Field.Offset);
  };
  LayoutIndexUpdateStarted = true;
  for (auto &S : Spills)
    Updater(S.first);
  for (const auto &A : Allocas)
    Updater(A.Alloca);
  LayoutIndexUpdateStarted = false;
}

void FrameTypeBuilder::addFieldForAllocas(const Function &F,
                                          FrameDataInfo &FrameData,
                                          coro::Shape &Shape) {
  using AllocaSetType = SmallVector<AllocaInst *, 4>;
  SmallVector<AllocaSetType, 4> NonOverlapedAllocas;

  // We need to add field for allocas at the end of this function.
  auto AddFieldForAllocasAtExit = make_scope_exit([&]() {
    for (auto AllocaList : NonOverlapedAllocas) {
      auto *LargestAI = *AllocaList.begin();
      FieldIDType Id = addFieldForAlloca(LargestAI);
      for (auto *Alloca : AllocaList)
        FrameData.setFieldIndex(Alloca, Id);
    }
  });

  if (!Shape.OptimizeFrame) {
    for (const auto &A : FrameData.Allocas) {
      AllocaInst *Alloca = A.Alloca;
      NonOverlapedAllocas.emplace_back(AllocaSetType(1, Alloca));
    }
    return;
  }

  // Because there are paths from the lifetime.start to coro.end
  // for each alloca, the liferanges for every alloca is overlaped
  // in the blocks who contain coro.end and the successor blocks.
  // So we choose to skip there blocks when we calculate the liferange
  // for each alloca. It should be reasonable since there shouldn't be uses
  // in these blocks and the coroutine frame shouldn't be used outside the
  // coroutine body.
  //
  // Note that the user of coro.suspend may not be SwitchInst. However, this
  // case seems too complex to handle. And it is harmless to skip these
  // patterns since it just prevend putting the allocas to live in the same
  // slot.
  DenseMap<SwitchInst *, BasicBlock *> DefaultSuspendDest;
  for (auto *CoroSuspendInst : Shape.CoroSuspends) {
    for (auto *U : CoroSuspendInst->users()) {
      if (auto *ConstSWI = dyn_cast<SwitchInst>(U)) {
        auto *SWI = const_cast<SwitchInst *>(ConstSWI);
        DefaultSuspendDest[SWI] = SWI->getDefaultDest();
        SWI->setDefaultDest(SWI->getSuccessor(1));
      }
    }
  }

  auto ExtractAllocas = [&]() {
    AllocaSetType Allocas;
    Allocas.reserve(FrameData.Allocas.size());
    for (const auto &A : FrameData.Allocas)
      Allocas.push_back(A.Alloca);
    return Allocas;
  };
  StackLifetime StackLifetimeAnalyzer(F, ExtractAllocas(),
                                      StackLifetime::LivenessType::May);
  StackLifetimeAnalyzer.run();
  auto IsAllocaInferenre = [&](const AllocaInst *AI1, const AllocaInst *AI2) {
    return StackLifetimeAnalyzer.getLiveRange(AI1).overlaps(
        StackLifetimeAnalyzer.getLiveRange(AI2));
  };
  auto GetAllocaSize = [&](const AllocaInfo &A) {
    std::optional<TypeSize> RetSize = A.Alloca->getAllocationSize(DL);
    assert(RetSize && "Variable Length Arrays (VLA) are not supported.\n");
    assert(!RetSize->isScalable() && "Scalable vectors are not yet supported");
    return RetSize->getFixedValue();
  };
  // Put larger allocas in the front. So the larger allocas have higher
  // priority to merge, which can save more space potentially. Also each
  // AllocaSet would be ordered. So we can get the largest Alloca in one
  // AllocaSet easily.
  sort(FrameData.Allocas, [&](const auto &Iter1, const auto &Iter2) {
    return GetAllocaSize(Iter1) > GetAllocaSize(Iter2);
  });
  for (const auto &A : FrameData.Allocas) {
    AllocaInst *Alloca = A.Alloca;
    bool Merged = false;
    // Try to find if the Alloca is not inferenced with any existing
    // NonOverlappedAllocaSet. If it is true, insert the alloca to that
    // NonOverlappedAllocaSet.
    for (auto &AllocaSet : NonOverlapedAllocas) {
      assert(!AllocaSet.empty() && "Processing Alloca Set is not empty.\n");
      bool NoInference = none_of(AllocaSet, [&](auto Iter) {
        return IsAllocaInferenre(Alloca, Iter);
      });
      // If the alignment of A is multiple of the alignment of B, the address
      // of A should satisfy the requirement for aligning for B.
      //
      // There may be other more fine-grained strategies to handle the alignment
      // infomation during the merging process. But it seems hard to handle
      // these strategies and benefit little.
      bool Alignable = [&]() -> bool {
        auto *LargestAlloca = *AllocaSet.begin();
        return LargestAlloca->getAlign().value() % Alloca->getAlign().value() ==
               0;
      }();
      bool CouldMerge = NoInference && Alignable;
      if (!CouldMerge)
        continue;
      AllocaSet.push_back(Alloca);
      Merged = true;
      break;
    }
    if (!Merged) {
      NonOverlapedAllocas.emplace_back(AllocaSetType(1, Alloca));
    }
  }
  // Recover the default target destination for each Switch statement
  // reserved.
  for (auto SwitchAndDefaultDest : DefaultSuspendDest) {
    SwitchInst *SWI = SwitchAndDefaultDest.first;
    BasicBlock *DestBB = SwitchAndDefaultDest.second;
    SWI->setDefaultDest(DestBB);
  }
  // This Debug Info could tell us which allocas are merged into one slot.
  LLVM_DEBUG(for (auto &AllocaSet
                  : NonOverlapedAllocas) {
    if (AllocaSet.size() > 1) {
      dbgs() << "In Function:" << F.getName() << "\n";
      dbgs() << "Find Union Set "
             << "\n";
      dbgs() << "\tAllocas are \n";
      for (auto Alloca : AllocaSet)
        dbgs() << "\t\t" << *Alloca << "\n";
    }
  });
}

void FrameTypeBuilder::finish(StructType *Ty) {
  assert(!IsFinished && "already finished!");

  // Prepare the optimal-layout field array.
  // The Id in the layout field is a pointer to our Field for it.
  SmallVector<OptimizedStructLayoutField, 8> LayoutFields;
  LayoutFields.reserve(Fields.size());
  for (auto &Field : Fields) {
    LayoutFields.emplace_back(&Field, Field.Size, Field.Alignment,
                              Field.Offset);
  }

  // Perform layout.
  auto SizeAndAlign = performOptimizedStructLayout(LayoutFields);
  StructSize = SizeAndAlign.first;
  StructAlign = SizeAndAlign.second;

  auto getField = [](const OptimizedStructLayoutField &LayoutField) -> Field & {
    return *static_cast<Field *>(const_cast<void*>(LayoutField.Id));
  };

  // We need to produce a packed struct type if there's a field whose
  // assigned offset isn't a multiple of its natural type alignment.
  bool Packed = [&] {
    for (auto &LayoutField : LayoutFields) {
      auto &F = getField(LayoutField);
      if (!isAligned(F.TyAlignment, LayoutField.Offset))
        return true;
    }
    return false;
  }();

  // Build the struct body.
  SmallVector<Type*, 16> FieldTypes;
  FieldTypes.reserve(LayoutFields.size() * 3 / 2);
  uint64_t LastOffset = 0;
  for (auto &LayoutField : LayoutFields) {
    auto &F = getField(LayoutField);

    auto Offset = LayoutField.Offset;

    // Add a padding field if there's a padding gap and we're either
    // building a packed struct or the padding gap is more than we'd
    // get from aligning to the field type's natural alignment.
    assert(Offset >= LastOffset);
    if (Offset != LastOffset) {
      if (Packed || alignTo(LastOffset, F.TyAlignment) != Offset)
        FieldTypes.push_back(ArrayType::get(Type::getInt8Ty(Context),
                                            Offset - LastOffset));
    }

    F.Offset = Offset;
    F.LayoutFieldIndex = FieldTypes.size();

    FieldTypes.push_back(F.Ty);
    if (F.DynamicAlignBuffer) {
      FieldTypes.push_back(
          ArrayType::get(Type::getInt8Ty(Context), F.DynamicAlignBuffer));
    }
    LastOffset = Offset + F.Size;
  }

  Ty->setBody(FieldTypes, Packed);

#ifndef NDEBUG
  // Check that the IR layout matches the offsets we expect.
  auto Layout = DL.getStructLayout(Ty);
  for (auto &F : Fields) {
    assert(Ty->getElementType(F.LayoutFieldIndex) == F.Ty);
    assert(Layout->getElementOffset(F.LayoutFieldIndex) == F.Offset);
  }
#endif

  IsFinished = true;
}

static void cacheDIVar(FrameDataInfo &FrameData,
                       DenseMap<Value *, DILocalVariable *> &DIVarCache) {
  for (auto *V : FrameData.getAllDefs()) {
    if (DIVarCache.contains(V))
      continue;

    auto CacheIt = [&DIVarCache, V](const auto &Container) {
      auto *I = llvm::find_if(Container, [](auto *DDI) {
        return DDI->getExpression()->getNumElements() == 0;
      });
      if (I != Container.end())
        DIVarCache.insert({V, (*I)->getVariable()});
    };
    CacheIt(findDbgDeclares(V));
    CacheIt(findDVRDeclares(V));
  }
}

/// Create name for Type. It uses MDString to store new created string to
/// avoid memory leak.
static StringRef solveTypeName(Type *Ty) {
  if (Ty->isIntegerTy()) {
    // The longest name in common may be '__int_128', which has 9 bits.
    SmallString<16> Buffer;
    raw_svector_ostream OS(Buffer);
    OS << "__int_" << cast<IntegerType>(Ty)->getBitWidth();
    auto *MDName = MDString::get(Ty->getContext(), OS.str());
    return MDName->getString();
  }

  if (Ty->isFloatingPointTy()) {
    if (Ty->isFloatTy())
      return "__float_";
    if (Ty->isDoubleTy())
      return "__double_";
    return "__floating_type_";
  }

  if (Ty->isPointerTy())
    return "PointerType";

  if (Ty->isStructTy()) {
    if (!cast<StructType>(Ty)->hasName())
      return "__LiteralStructType_";

    auto Name = Ty->getStructName();

    SmallString<16> Buffer(Name);
    for (auto &Iter : Buffer)
      if (Iter == '.' || Iter == ':')
        Iter = '_';
    auto *MDName = MDString::get(Ty->getContext(), Buffer.str());
    return MDName->getString();
  }

  return "UnknownType";
}

static DIType *solveDIType(DIBuilder &Builder, Type *Ty,
                           const DataLayout &Layout, DIScope *Scope,
                           unsigned LineNum,
                           DenseMap<Type *, DIType *> &DITypeCache) {
  if (DIType *DT = DITypeCache.lookup(Ty))
    return DT;

  StringRef Name = solveTypeName(Ty);

  DIType *RetType = nullptr;

  if (Ty->isIntegerTy()) {
    auto BitWidth = cast<IntegerType>(Ty)->getBitWidth();
    RetType = Builder.createBasicType(Name, BitWidth, dwarf::DW_ATE_signed,
                                      llvm::DINode::FlagArtificial);
  } else if (Ty->isFloatingPointTy()) {
    RetType = Builder.createBasicType(Name, Layout.getTypeSizeInBits(Ty),
                                      dwarf::DW_ATE_float,
                                      llvm::DINode::FlagArtificial);
  } else if (Ty->isPointerTy()) {
    // Construct PointerType points to null (aka void *) instead of exploring
    // pointee type to avoid infinite search problem. For example, we would be
    // in trouble if we traverse recursively:
    //
    //  struct Node {
    //      Node* ptr;
    //  };
    RetType =
        Builder.createPointerType(nullptr, Layout.getTypeSizeInBits(Ty),
                                  Layout.getABITypeAlign(Ty).value() * CHAR_BIT,
                                  /*DWARFAddressSpace=*/std::nullopt, Name);
  } else if (Ty->isStructTy()) {
    auto *DIStruct = Builder.createStructType(
        Scope, Name, Scope->getFile(), LineNum, Layout.getTypeSizeInBits(Ty),
        Layout.getPrefTypeAlign(Ty).value() * CHAR_BIT,
        llvm::DINode::FlagArtificial, nullptr, llvm::DINodeArray());

    auto *StructTy = cast<StructType>(Ty);
    SmallVector<Metadata *, 16> Elements;
    for (unsigned I = 0; I < StructTy->getNumElements(); I++) {
      DIType *DITy = solveDIType(Builder, StructTy->getElementType(I), Layout,
                                 Scope, LineNum, DITypeCache);
      assert(DITy);
      Elements.push_back(Builder.createMemberType(
          Scope, DITy->getName(), Scope->getFile(), LineNum,
          DITy->getSizeInBits(), DITy->getAlignInBits(),
          Layout.getStructLayout(StructTy)->getElementOffsetInBits(I),
          llvm::DINode::FlagArtificial, DITy));
    }

    Builder.replaceArrays(DIStruct, Builder.getOrCreateArray(Elements));

    RetType = DIStruct;
  } else {
    LLVM_DEBUG(dbgs() << "Unresolved Type: " << *Ty << "\n");
    TypeSize Size = Layout.getTypeSizeInBits(Ty);
    auto *CharSizeType = Builder.createBasicType(
        Name, 8, dwarf::DW_ATE_unsigned_char, llvm::DINode::FlagArtificial);

    if (Size <= 8)
      RetType = CharSizeType;
    else {
      if (Size % 8 != 0)
        Size = TypeSize::getFixed(Size + 8 - (Size % 8));

      RetType = Builder.createArrayType(
          Size, Layout.getPrefTypeAlign(Ty).value(), CharSizeType,
          Builder.getOrCreateArray(Builder.getOrCreateSubrange(0, Size / 8)));
    }
  }

  DITypeCache.insert({Ty, RetType});
  return RetType;
}

/// Build artificial debug info for C++ coroutine frames to allow users to
/// inspect the contents of the frame directly
///
/// Create Debug information for coroutine frame with debug name "__coro_frame".
/// The debug information for the fields of coroutine frame is constructed from
/// the following way:
/// 1. For all the value in the Frame, we search the use of dbg.declare to find
///    the corresponding debug variables for the value. If we can find the
///    debug variable, we can get full and accurate debug information.
/// 2. If we can't get debug information in step 1 and 2, we could only try to
///    build the DIType by Type. We did this in solveDIType. We only handle
///    integer, float, double, integer type and struct type for now.
static void buildFrameDebugInfo(Function &F, coro::Shape &Shape,
                                FrameDataInfo &FrameData) {
  DISubprogram *DIS = F.getSubprogram();
  // If there is no DISubprogram for F, it implies the Function are not compiled
  // with debug info. So we also don't need to generate debug info for the frame
  // neither.
  if (!DIS || !DIS->getUnit() ||
      !dwarf::isCPlusPlus(
          (dwarf::SourceLanguage)DIS->getUnit()->getSourceLanguage()))
    return;

  assert(Shape.ABI == coro::ABI::Switch &&
         "We could only build debug infomation for C++ coroutine now.\n");

  DIBuilder DBuilder(*F.getParent(), /*AllowUnresolved*/ false);

  AllocaInst *PromiseAlloca = Shape.getPromiseAlloca();
  assert(PromiseAlloca &&
         "Coroutine with switch ABI should own Promise alloca");

  TinyPtrVector<DbgDeclareInst *> DIs = findDbgDeclares(PromiseAlloca);
  TinyPtrVector<DbgVariableRecord *> DVRs = findDVRDeclares(PromiseAlloca);

  DILocalVariable *PromiseDIVariable = nullptr;
  DILocation *DILoc = nullptr;
  if (!DIs.empty()) {
    DbgDeclareInst *PromiseDDI = DIs.front();
    PromiseDIVariable = PromiseDDI->getVariable();
    DILoc = PromiseDDI->getDebugLoc().get();
  } else if (!DVRs.empty()) {
    DbgVariableRecord *PromiseDVR = DVRs.front();
    PromiseDIVariable = PromiseDVR->getVariable();
    DILoc = PromiseDVR->getDebugLoc().get();
  } else {
    return;
  }

  DILocalScope *PromiseDIScope = PromiseDIVariable->getScope();
  DIFile *DFile = PromiseDIScope->getFile();
  unsigned LineNum = PromiseDIVariable->getLine();

  DICompositeType *FrameDITy = DBuilder.createStructType(
      DIS->getUnit(), Twine(F.getName() + ".coro_frame_ty").str(),
      DFile, LineNum, Shape.FrameSize * 8,
      Shape.FrameAlign.value() * 8, llvm::DINode::FlagArtificial, nullptr,
      llvm::DINodeArray());
  StructType *FrameTy = Shape.FrameTy;
  SmallVector<Metadata *, 16> Elements;
  DataLayout Layout = F.getDataLayout();

  DenseMap<Value *, DILocalVariable *> DIVarCache;
  cacheDIVar(FrameData, DIVarCache);

  unsigned ResumeIndex = coro::Shape::SwitchFieldIndex::Resume;
  unsigned DestroyIndex = coro::Shape::SwitchFieldIndex::Destroy;
  unsigned IndexIndex = Shape.SwitchLowering.IndexField;

  DenseMap<unsigned, StringRef> NameCache;
  NameCache.insert({ResumeIndex, "__resume_fn"});
  NameCache.insert({DestroyIndex, "__destroy_fn"});
  NameCache.insert({IndexIndex, "__coro_index"});

  Type *ResumeFnTy = FrameTy->getElementType(ResumeIndex),
       *DestroyFnTy = FrameTy->getElementType(DestroyIndex),
       *IndexTy = FrameTy->getElementType(IndexIndex);

  DenseMap<unsigned, DIType *> TyCache;
  TyCache.insert(
      {ResumeIndex, DBuilder.createPointerType(
                        nullptr, Layout.getTypeSizeInBits(ResumeFnTy))});
  TyCache.insert(
      {DestroyIndex, DBuilder.createPointerType(
                         nullptr, Layout.getTypeSizeInBits(DestroyFnTy))});

  /// FIXME: If we fill the field `SizeInBits` with the actual size of
  /// __coro_index in bits, then __coro_index wouldn't show in the debugger.
  TyCache.insert({IndexIndex, DBuilder.createBasicType(
                                  "__coro_index",
                                  (Layout.getTypeSizeInBits(IndexTy) < 8)
                                      ? 8
                                      : Layout.getTypeSizeInBits(IndexTy),
                                  dwarf::DW_ATE_unsigned_char)});

  for (auto *V : FrameData.getAllDefs()) {
    if (!DIVarCache.contains(V))
      continue;

    auto Index = FrameData.getFieldIndex(V);

    NameCache.insert({Index, DIVarCache[V]->getName()});
    TyCache.insert({Index, DIVarCache[V]->getType()});
  }

  // Cache from index to (Align, Offset Pair)
  DenseMap<unsigned, std::pair<unsigned, unsigned>> OffsetCache;
  // The Align and Offset of Resume function and Destroy function are fixed.
  OffsetCache.insert({ResumeIndex, {8, 0}});
  OffsetCache.insert({DestroyIndex, {8, 8}});
  OffsetCache.insert(
      {IndexIndex,
       {Shape.SwitchLowering.IndexAlign, Shape.SwitchLowering.IndexOffset}});

  for (auto *V : FrameData.getAllDefs()) {
    auto Index = FrameData.getFieldIndex(V);

    OffsetCache.insert(
        {Index, {FrameData.getAlign(V).value(), FrameData.getOffset(V)}});
  }

  DenseMap<Type *, DIType *> DITypeCache;
  // This counter is used to avoid same type names. e.g., there would be
  // many i32 and i64 types in one coroutine. And we would use i32_0 and
  // i32_1 to avoid the same type. Since it makes no sense the name of the
  // fields confilicts with each other.
  unsigned UnknownTypeNum = 0;
  for (unsigned Index = 0; Index < FrameTy->getNumElements(); Index++) {
    if (!OffsetCache.contains(Index))
      continue;

    std::string Name;
    uint64_t SizeInBits;
    uint32_t AlignInBits;
    uint64_t OffsetInBits;
    DIType *DITy = nullptr;

    Type *Ty = FrameTy->getElementType(Index);
    assert(Ty->isSized() && "We can't handle type which is not sized.\n");
    SizeInBits = Layout.getTypeSizeInBits(Ty).getFixedValue();
    AlignInBits = OffsetCache[Index].first * 8;
    OffsetInBits = OffsetCache[Index].second * 8;

    if (NameCache.contains(Index)) {
      Name = NameCache[Index].str();
      DITy = TyCache[Index];
    } else {
      DITy = solveDIType(DBuilder, Ty, Layout, FrameDITy, LineNum, DITypeCache);
      assert(DITy && "SolveDIType shouldn't return nullptr.\n");
      Name = DITy->getName().str();
      Name += "_" + std::to_string(UnknownTypeNum);
      UnknownTypeNum++;
    }

    Elements.push_back(DBuilder.createMemberType(
        FrameDITy, Name, DFile, LineNum, SizeInBits, AlignInBits, OffsetInBits,
        llvm::DINode::FlagArtificial, DITy));
  }

  DBuilder.replaceArrays(FrameDITy, DBuilder.getOrCreateArray(Elements));

  auto *FrameDIVar = DBuilder.createAutoVariable(PromiseDIScope, "__coro_frame",
                                                 DFile, LineNum, FrameDITy,
                                                 true, DINode::FlagArtificial);
  assert(FrameDIVar->isValidLocationForIntrinsic(DILoc));

  // Subprogram would have ContainedNodes field which records the debug
  // variables it contained. So we need to add __coro_frame to the
  // ContainedNodes of it.
  //
  // If we don't add __coro_frame to the RetainedNodes, user may get
  // `no symbol __coro_frame in context` rather than `__coro_frame`
  // is optimized out, which is more precise.
  if (auto *SubProgram = dyn_cast<DISubprogram>(PromiseDIScope)) {
    auto RetainedNodes = SubProgram->getRetainedNodes();
    SmallVector<Metadata *, 32> RetainedNodesVec(RetainedNodes.begin(),
                                                 RetainedNodes.end());
    RetainedNodesVec.push_back(FrameDIVar);
    SubProgram->replaceOperandWith(
        7, (MDTuple::get(F.getContext(), RetainedNodesVec)));
  }

  if (UseNewDbgInfoFormat) {
    DbgVariableRecord *NewDVR =
        new DbgVariableRecord(ValueAsMetadata::get(Shape.FramePtr), FrameDIVar,
                              DBuilder.createExpression(), DILoc,
                              DbgVariableRecord::LocationType::Declare);
    BasicBlock::iterator It = Shape.getInsertPtAfterFramePtr();
    It->getParent()->insertDbgRecordBefore(NewDVR, It);
  } else {
    DBuilder.insertDeclare(Shape.FramePtr, FrameDIVar,
                           DBuilder.createExpression(), DILoc,
                           &*Shape.getInsertPtAfterFramePtr());
  }
}

// Build a struct that will keep state for an active coroutine.
//   struct f.frame {
//     ResumeFnTy ResumeFnAddr;
//     ResumeFnTy DestroyFnAddr;
//     ... promise (if present) ...
//     int ResumeIndex;
//     ... spills ...
//   };
static StructType *buildFrameType(Function &F, coro::Shape &Shape,
                                  FrameDataInfo &FrameData) {
  LLVMContext &C = F.getContext();
  const DataLayout &DL = F.getDataLayout();
  StructType *FrameTy = [&] {
    SmallString<32> Name(F.getName());
    Name.append(".Frame");
    return StructType::create(C, Name);
  }();

  // We will use this value to cap the alignment of spilled values.
  std::optional<Align> MaxFrameAlignment;
  if (Shape.ABI == coro::ABI::Async)
    MaxFrameAlignment = Shape.AsyncLowering.getContextAlignment();
  FrameTypeBuilder B(C, DL, MaxFrameAlignment);

  AllocaInst *PromiseAlloca = Shape.getPromiseAlloca();
  std::optional<FieldIDType> SwitchIndexFieldId;

  if (Shape.ABI == coro::ABI::Switch) {
    auto *FnPtrTy = PointerType::getUnqual(C);

    // Add header fields for the resume and destroy functions.
    // We can rely on these being perfectly packed.
    (void)B.addField(FnPtrTy, std::nullopt, /*header*/ true);
    (void)B.addField(FnPtrTy, std::nullopt, /*header*/ true);

    // PromiseAlloca field needs to be explicitly added here because it's
    // a header field with a fixed offset based on its alignment. Hence it
    // needs special handling and cannot be added to FrameData.Allocas.
    if (PromiseAlloca)
      FrameData.setFieldIndex(
          PromiseAlloca, B.addFieldForAlloca(PromiseAlloca, /*header*/ true));

    // Add a field to store the suspend index.  This doesn't need to
    // be in the header.
    unsigned IndexBits = std::max(1U, Log2_64_Ceil(Shape.CoroSuspends.size()));
    Type *IndexType = Type::getIntNTy(C, IndexBits);

    SwitchIndexFieldId = B.addField(IndexType, std::nullopt);
  } else {
    assert(PromiseAlloca == nullptr && "lowering doesn't support promises");
  }

  // Because multiple allocas may own the same field slot,
  // we add allocas to field here.
  B.addFieldForAllocas(F, FrameData, Shape);
  // Add PromiseAlloca to Allocas list so that
  // 1. updateLayoutIndex could update its index after
  // `performOptimizedStructLayout`
  // 2. it is processed in insertSpills.
  if (Shape.ABI == coro::ABI::Switch && PromiseAlloca)
    // We assume that the promise alloca won't be modified before
    // CoroBegin and no alias will be create before CoroBegin.
    FrameData.Allocas.emplace_back(
        PromiseAlloca, DenseMap<Instruction *, std::optional<APInt>>{}, false);
  // Create an entry for every spilled value.
  for (auto &S : FrameData.Spills) {
    Type *FieldType = S.first->getType();
    // For byval arguments, we need to store the pointed value in the frame,
    // instead of the pointer itself.
    if (const Argument *A = dyn_cast<Argument>(S.first))
      if (A->hasByValAttr())
        FieldType = A->getParamByValType();
    FieldIDType Id = B.addField(FieldType, std::nullopt, false /*header*/,
                                true /*IsSpillOfValue*/);
    FrameData.setFieldIndex(S.first, Id);
  }

  B.finish(FrameTy);
  FrameData.updateLayoutIndex(B);
  Shape.FrameAlign = B.getStructAlign();
  Shape.FrameSize = B.getStructSize();

  switch (Shape.ABI) {
  case coro::ABI::Switch: {
    // In the switch ABI, remember the switch-index field.
    auto IndexField = B.getLayoutField(*SwitchIndexFieldId);
    Shape.SwitchLowering.IndexField = IndexField.LayoutFieldIndex;
    Shape.SwitchLowering.IndexAlign = IndexField.Alignment.value();
    Shape.SwitchLowering.IndexOffset = IndexField.Offset;

    // Also round the frame size up to a multiple of its alignment, as is
    // generally expected in C/C++.
    Shape.FrameSize = alignTo(Shape.FrameSize, Shape.FrameAlign);
    break;
  }

  // In the retcon ABI, remember whether the frame is inline in the storage.
  case coro::ABI::Retcon:
  case coro::ABI::RetconOnce: {
    auto Id = Shape.getRetconCoroId();
    Shape.RetconLowering.IsFrameInlineInStorage
      = (B.getStructSize() <= Id->getStorageSize() &&
         B.getStructAlign() <= Id->getStorageAlignment());
    break;
  }
  case coro::ABI::Async: {
    Shape.AsyncLowering.FrameOffset =
        alignTo(Shape.AsyncLowering.ContextHeaderSize, Shape.FrameAlign);
    // Also make the final context size a multiple of the context alignment to
    // make allocation easier for allocators.
    Shape.AsyncLowering.ContextSize =
        alignTo(Shape.AsyncLowering.FrameOffset + Shape.FrameSize,
                Shape.AsyncLowering.getContextAlignment());
    if (Shape.AsyncLowering.getContextAlignment() < Shape.FrameAlign) {
      report_fatal_error(
          "The alignment requirment of frame variables cannot be higher than "
          "the alignment of the async function context");
    }
    break;
  }
  }

  return FrameTy;
}

// We use a pointer use visitor to track how an alloca is being used.
// The goal is to be able to answer the following three questions:
// 1. Should this alloca be allocated on the frame instead.
// 2. Could the content of the alloca be modified prior to CoroBegn, which would
// require copying the data from alloca to the frame after CoroBegin.
// 3. Is there any alias created for this alloca prior to CoroBegin, but used
// after CoroBegin. In that case, we will need to recreate the alias after
// CoroBegin based off the frame. To answer question 1, we track two things:
//   a. List of all BasicBlocks that use this alloca or any of the aliases of
//   the alloca. In the end, we check if there exists any two basic blocks that
//   cross suspension points. If so, this alloca must be put on the frame. b.
//   Whether the alloca or any alias of the alloca is escaped at some point,
//   either by storing the address somewhere, or the address is used in a
//   function call that might capture. If it's ever escaped, this alloca must be
//   put on the frame conservatively.
// To answer quetion 2, we track through the variable MayWriteBeforeCoroBegin.
// Whenever a potential write happens, either through a store instruction, a
// function call or any of the memory intrinsics, we check whether this
// instruction is prior to CoroBegin. To answer question 3, we track the offsets
// of all aliases created for the alloca prior to CoroBegin but used after
// CoroBegin. std::optional is used to be able to represent the case when the
// offset is unknown (e.g. when you have a PHINode that takes in different
// offset values). We cannot handle unknown offsets and will assert. This is the
// potential issue left out. An ideal solution would likely require a
// significant redesign.
namespace {
struct AllocaUseVisitor : PtrUseVisitor<AllocaUseVisitor> {
  using Base = PtrUseVisitor<AllocaUseVisitor>;
  AllocaUseVisitor(const DataLayout &DL, const DominatorTree &DT,
                   const coro::Shape &CoroShape,
                   const SuspendCrossingInfo &Checker,
                   bool ShouldUseLifetimeStartInfo)
      : PtrUseVisitor(DL), DT(DT), CoroShape(CoroShape), Checker(Checker),
        ShouldUseLifetimeStartInfo(ShouldUseLifetimeStartInfo) {
    for (AnyCoroSuspendInst *SuspendInst : CoroShape.CoroSuspends)
      CoroSuspendBBs.insert(SuspendInst->getParent());
  }

  void visit(Instruction &I) {
    Users.insert(&I);
    Base::visit(I);
    // If the pointer is escaped prior to CoroBegin, we have to assume it would
    // be written into before CoroBegin as well.
    if (PI.isEscaped() &&
        !DT.dominates(CoroShape.CoroBegin, PI.getEscapingInst())) {
      MayWriteBeforeCoroBegin = true;
    }
  }
  // We need to provide this overload as PtrUseVisitor uses a pointer based
  // visiting function.
  void visit(Instruction *I) { return visit(*I); }

  void visitPHINode(PHINode &I) {
    enqueueUsers(I);
    handleAlias(I);
  }

  void visitSelectInst(SelectInst &I) {
    enqueueUsers(I);
    handleAlias(I);
  }

  void visitStoreInst(StoreInst &SI) {
    // Regardless whether the alias of the alloca is the value operand or the
    // pointer operand, we need to assume the alloca is been written.
    handleMayWrite(SI);

    if (SI.getValueOperand() != U->get())
      return;

    // We are storing the pointer into a memory location, potentially escaping.
    // As an optimization, we try to detect simple cases where it doesn't
    // actually escape, for example:
    //   %ptr = alloca ..
    //   %addr = alloca ..
    //   store %ptr, %addr
    //   %x = load %addr
    //   ..
    // If %addr is only used by loading from it, we could simply treat %x as
    // another alias of %ptr, and not considering %ptr being escaped.
    auto IsSimpleStoreThenLoad = [&]() {
      auto *AI = dyn_cast<AllocaInst>(SI.getPointerOperand());
      // If the memory location we are storing to is not an alloca, it
      // could be an alias of some other memory locations, which is difficult
      // to analyze.
      if (!AI)
        return false;
      // StoreAliases contains aliases of the memory location stored into.
      SmallVector<Instruction *, 4> StoreAliases = {AI};
      while (!StoreAliases.empty()) {
        Instruction *I = StoreAliases.pop_back_val();
        for (User *U : I->users()) {
          // If we are loading from the memory location, we are creating an
          // alias of the original pointer.
          if (auto *LI = dyn_cast<LoadInst>(U)) {
            enqueueUsers(*LI);
            handleAlias(*LI);
            continue;
          }
          // If we are overriding the memory location, the pointer certainly
          // won't escape.
          if (auto *S = dyn_cast<StoreInst>(U))
            if (S->getPointerOperand() == I)
              continue;
          if (auto *II = dyn_cast<IntrinsicInst>(U))
            if (II->isLifetimeStartOrEnd())
              continue;
          // BitCastInst creats aliases of the memory location being stored
          // into.
          if (auto *BI = dyn_cast<BitCastInst>(U)) {
            StoreAliases.push_back(BI);
            continue;
          }
          return false;
        }
      }

      return true;
    };

    if (!IsSimpleStoreThenLoad())
      PI.setEscaped(&SI);
  }

  // All mem intrinsics modify the data.
  void visitMemIntrinsic(MemIntrinsic &MI) { handleMayWrite(MI); }

  void visitBitCastInst(BitCastInst &BC) {
    Base::visitBitCastInst(BC);
    handleAlias(BC);
  }

  void visitAddrSpaceCastInst(AddrSpaceCastInst &ASC) {
    Base::visitAddrSpaceCastInst(ASC);
    handleAlias(ASC);
  }

  void visitGetElementPtrInst(GetElementPtrInst &GEPI) {
    // The base visitor will adjust Offset accordingly.
    Base::visitGetElementPtrInst(GEPI);
    handleAlias(GEPI);
  }

  void visitIntrinsicInst(IntrinsicInst &II) {
    // When we found the lifetime markers refers to a
    // subrange of the original alloca, ignore the lifetime
    // markers to avoid misleading the analysis.
    if (!IsOffsetKnown || !Offset.isZero())
      return Base::visitIntrinsicInst(II);
    switch (II.getIntrinsicID()) {
    default:
      return Base::visitIntrinsicInst(II);
    case Intrinsic::lifetime_start:
      LifetimeStarts.insert(&II);
      LifetimeStartBBs.push_back(II.getParent());
      break;
    case Intrinsic::lifetime_end:
      LifetimeEndBBs.insert(II.getParent());
      break;
    }
  }

  void visitCallBase(CallBase &CB) {
    for (unsigned Op = 0, OpCount = CB.arg_size(); Op < OpCount; ++Op)
      if (U->get() == CB.getArgOperand(Op) && !CB.doesNotCapture(Op))
        PI.setEscaped(&CB);
    handleMayWrite(CB);
  }

  bool getShouldLiveOnFrame() const {
    if (!ShouldLiveOnFrame)
      ShouldLiveOnFrame = computeShouldLiveOnFrame();
    return *ShouldLiveOnFrame;
  }

  bool getMayWriteBeforeCoroBegin() const { return MayWriteBeforeCoroBegin; }

  DenseMap<Instruction *, std::optional<APInt>> getAliasesCopy() const {
    assert(getShouldLiveOnFrame() && "This method should only be called if the "
                                     "alloca needs to live on the frame.");
    for (const auto &P : AliasOffetMap)
      if (!P.second)
        report_fatal_error("Unable to handle an alias with unknown offset "
                           "created before CoroBegin.");
    return AliasOffetMap;
  }

private:
  const DominatorTree &DT;
  const coro::Shape &CoroShape;
  const SuspendCrossingInfo &Checker;
  // All alias to the original AllocaInst, created before CoroBegin and used
  // after CoroBegin. Each entry contains the instruction and the offset in the
  // original Alloca. They need to be recreated after CoroBegin off the frame.
  DenseMap<Instruction *, std::optional<APInt>> AliasOffetMap{};
  SmallPtrSet<Instruction *, 4> Users{};
  SmallPtrSet<IntrinsicInst *, 2> LifetimeStarts{};
  SmallVector<BasicBlock *> LifetimeStartBBs{};
  SmallPtrSet<BasicBlock *, 2> LifetimeEndBBs{};
  SmallPtrSet<const BasicBlock *, 2> CoroSuspendBBs{};
  bool MayWriteBeforeCoroBegin{false};
  bool ShouldUseLifetimeStartInfo{true};

  mutable std::optional<bool> ShouldLiveOnFrame{};

  bool computeShouldLiveOnFrame() const {
    // If lifetime information is available, we check it first since it's
    // more precise. We look at every pair of lifetime.start intrinsic and
    // every basic block that uses the pointer to see if they cross suspension
    // points. The uses cover both direct uses as well as indirect uses.
    if (ShouldUseLifetimeStartInfo && !LifetimeStarts.empty()) {
      // If there is no explicit lifetime.end, then assume the address can
      // cross suspension points.
      if (LifetimeEndBBs.empty())
        return true;

      // If there is a path from a lifetime.start to a suspend without a
      // corresponding lifetime.end, then the alloca's lifetime persists
      // beyond that suspension point and the alloca must go on the frame.
      llvm::SmallVector<BasicBlock *> Worklist(LifetimeStartBBs);
      if (isManyPotentiallyReachableFromMany(Worklist, CoroSuspendBBs,
                                             &LifetimeEndBBs, &DT))
        return true;

      // Addresses are guaranteed to be identical after every lifetime.start so
      // we cannot use the local stack if the address escaped and there is a
      // suspend point between lifetime markers. This should also cover the
      // case of a single lifetime.start intrinsic in a loop with suspend point.
      if (PI.isEscaped()) {
        for (auto *A : LifetimeStarts) {
          for (auto *B : LifetimeStarts) {
            if (Checker.hasPathOrLoopCrossingSuspendPoint(A->getParent(),
                                                          B->getParent()))
              return true;
          }
        }
      }
      return false;
    }
    // FIXME: Ideally the isEscaped check should come at the beginning.
    // However there are a few loose ends that need to be fixed first before
    // we can do that. We need to make sure we are not over-conservative, so
    // that the data accessed in-between await_suspend and symmetric transfer
    // is always put on the stack, and also data accessed after coro.end is
    // always put on the stack (esp the return object). To fix that, we need
    // to:
    //  1) Potentially treat sret as nocapture in calls
    //  2) Special handle the return object and put it on the stack
    //  3) Utilize lifetime.end intrinsic
    if (PI.isEscaped())
      return true;

    for (auto *U1 : Users)
      for (auto *U2 : Users)
        if (Checker.isDefinitionAcrossSuspend(*U1, U2))
          return true;

    return false;
  }

  void handleMayWrite(const Instruction &I) {
    if (!DT.dominates(CoroShape.CoroBegin, &I))
      MayWriteBeforeCoroBegin = true;
  }

  bool usedAfterCoroBegin(Instruction &I) {
    for (auto &U : I.uses())
      if (DT.dominates(CoroShape.CoroBegin, U))
        return true;
    return false;
  }

  void handleAlias(Instruction &I) {
    // We track all aliases created prior to CoroBegin but used after.
    // These aliases may need to be recreated after CoroBegin if the alloca
    // need to live on the frame.
    if (DT.dominates(CoroShape.CoroBegin, &I) || !usedAfterCoroBegin(I))
      return;

    if (!IsOffsetKnown) {
      AliasOffetMap[&I].reset();
    } else {
      auto Itr = AliasOffetMap.find(&I);
      if (Itr == AliasOffetMap.end()) {
        AliasOffetMap[&I] = Offset;
      } else if (Itr->second && *Itr->second != Offset) {
        // If we have seen two different possible values for this alias, we set
        // it to empty.
        AliasOffetMap[&I].reset();
      }
    }
  }
};
} // namespace

// We need to make room to insert a spill after initial PHIs, but before
// catchswitch instruction. Placing it before violates the requirement that
// catchswitch, like all other EHPads must be the first nonPHI in a block.
//
// Split away catchswitch into a separate block and insert in its place:
//
//   cleanuppad <InsertPt> cleanupret.
//
// cleanupret instruction will act as an insert point for the spill.
static Instruction *splitBeforeCatchSwitch(CatchSwitchInst *CatchSwitch) {
  BasicBlock *CurrentBlock = CatchSwitch->getParent();
  BasicBlock *NewBlock = CurrentBlock->splitBasicBlock(CatchSwitch);
  CurrentBlock->getTerminator()->eraseFromParent();

  auto *CleanupPad =
      CleanupPadInst::Create(CatchSwitch->getParentPad(), {}, "", CurrentBlock);
  auto *CleanupRet =
      CleanupReturnInst::Create(CleanupPad, NewBlock, CurrentBlock);
  return CleanupRet;
}

// Replace all alloca and SSA values that are accessed across suspend points
// with GetElementPointer from coroutine frame + loads and stores. Create an
// AllocaSpillBB that will become the new entry block for the resume parts of
// the coroutine:
//
//    %hdl = coro.begin(...)
//    whatever
//
// becomes:
//
//    %hdl = coro.begin(...)
//    br label %AllocaSpillBB
//
//  AllocaSpillBB:
//    ; geps corresponding to allocas that were moved to coroutine frame
//    br label PostSpill
//
//  PostSpill:
//    whatever
//
//
static void insertSpills(const FrameDataInfo &FrameData, coro::Shape &Shape) {
  auto *CB = Shape.CoroBegin;
  LLVMContext &C = CB->getContext();
  Function *F = CB->getFunction();
  IRBuilder<> Builder(C);
  StructType *FrameTy = Shape.FrameTy;
  Value *FramePtr = Shape.FramePtr;
  DominatorTree DT(*F);
  SmallDenseMap<Argument *, AllocaInst *, 4> ArgToAllocaMap;

  // Create a GEP with the given index into the coroutine frame for the original
  // value Orig. Appends an extra 0 index for array-allocas, preserving the
  // original type.
  auto GetFramePointer = [&](Value *Orig) -> Value * {
    FieldIDType Index = FrameData.getFieldIndex(Orig);
    SmallVector<Value *, 3> Indices = {
        ConstantInt::get(Type::getInt32Ty(C), 0),
        ConstantInt::get(Type::getInt32Ty(C), Index),
    };

    if (auto *AI = dyn_cast<AllocaInst>(Orig)) {
      if (auto *CI = dyn_cast<ConstantInt>(AI->getArraySize())) {
        auto Count = CI->getValue().getZExtValue();
        if (Count > 1) {
          Indices.push_back(ConstantInt::get(Type::getInt32Ty(C), 0));
        }
      } else {
        report_fatal_error("Coroutines cannot handle non static allocas yet");
      }
    }

    auto GEP = cast<GetElementPtrInst>(
        Builder.CreateInBoundsGEP(FrameTy, FramePtr, Indices));
    if (auto *AI = dyn_cast<AllocaInst>(Orig)) {
      if (FrameData.getDynamicAlign(Orig) != 0) {
        assert(FrameData.getDynamicAlign(Orig) == AI->getAlign().value());
        auto *M = AI->getModule();
        auto *IntPtrTy = M->getDataLayout().getIntPtrType(AI->getType());
        auto *PtrValue = Builder.CreatePtrToInt(GEP, IntPtrTy);
        auto *AlignMask =
            ConstantInt::get(IntPtrTy, AI->getAlign().value() - 1);
        PtrValue = Builder.CreateAdd(PtrValue, AlignMask);
        PtrValue = Builder.CreateAnd(PtrValue, Builder.CreateNot(AlignMask));
        return Builder.CreateIntToPtr(PtrValue, AI->getType());
      }
      // If the type of GEP is not equal to the type of AllocaInst, it implies
      // that the AllocaInst may be reused in the Frame slot of other
      // AllocaInst. So We cast GEP to the AllocaInst here to re-use
      // the Frame storage.
      //
      // Note: If we change the strategy dealing with alignment, we need to refine
      // this casting.
      if (GEP->getType() != Orig->getType())
        return Builder.CreateAddrSpaceCast(GEP, Orig->getType(),
                                           Orig->getName() + Twine(".cast"));
    }
    return GEP;
  };

  for (auto const &E : FrameData.Spills) {
    Value *Def = E.first;
    auto SpillAlignment = Align(FrameData.getAlign(Def));
    // Create a store instruction storing the value into the
    // coroutine frame.
    BasicBlock::iterator InsertPt;
    Type *ByValTy = nullptr;
    if (auto *Arg = dyn_cast<Argument>(Def)) {
      // For arguments, we will place the store instruction right after
      // the coroutine frame pointer instruction, i.e. coro.begin.
      InsertPt = Shape.getInsertPtAfterFramePtr();

      // If we're spilling an Argument, make sure we clear 'nocapture'
      // from the coroutine function.
      Arg->getParent()->removeParamAttr(Arg->getArgNo(), Attribute::NoCapture);

      if (Arg->hasByValAttr())
        ByValTy = Arg->getParamByValType();
    } else if (auto *CSI = dyn_cast<AnyCoroSuspendInst>(Def)) {
      // Don't spill immediately after a suspend; splitting assumes
      // that the suspend will be followed by a branch.
      InsertPt = CSI->getParent()->getSingleSuccessor()->getFirstNonPHIIt();
    } else {
      auto *I = cast<Instruction>(Def);
      if (!DT.dominates(CB, I)) {
        // If it is not dominated by CoroBegin, then spill should be
        // inserted immediately after CoroFrame is computed.
        InsertPt = Shape.getInsertPtAfterFramePtr();
      } else if (auto *II = dyn_cast<InvokeInst>(I)) {
        // If we are spilling the result of the invoke instruction, split
        // the normal edge and insert the spill in the new block.
        auto *NewBB = SplitEdge(II->getParent(), II->getNormalDest());
        InsertPt = NewBB->getTerminator()->getIterator();
      } else if (isa<PHINode>(I)) {
        // Skip the PHINodes and EH pads instructions.
        BasicBlock *DefBlock = I->getParent();
        if (auto *CSI = dyn_cast<CatchSwitchInst>(DefBlock->getTerminator()))
          InsertPt = splitBeforeCatchSwitch(CSI)->getIterator();
        else
          InsertPt = DefBlock->getFirstInsertionPt();
      } else {
        assert(!I->isTerminator() && "unexpected terminator");
        // For all other values, the spill is placed immediately after
        // the definition.
        InsertPt = I->getNextNode()->getIterator();
      }
    }

    auto Index = FrameData.getFieldIndex(Def);
    Builder.SetInsertPoint(InsertPt->getParent(), InsertPt);
    auto *G = Builder.CreateConstInBoundsGEP2_32(
        FrameTy, FramePtr, 0, Index, Def->getName() + Twine(".spill.addr"));
    if (ByValTy) {
      // For byval arguments, we need to store the pointed value in the frame,
      // instead of the pointer itself.
      auto *Value = Builder.CreateLoad(ByValTy, Def);
      Builder.CreateAlignedStore(Value, G, SpillAlignment);
    } else {
      Builder.CreateAlignedStore(Def, G, SpillAlignment);
    }

    BasicBlock *CurrentBlock = nullptr;
    Value *CurrentReload = nullptr;
    for (auto *U : E.second) {
      // If we have not seen the use block, create a load instruction to reload
      // the spilled value from the coroutine frame. Populates the Value pointer
      // reference provided with the frame GEP.
      if (CurrentBlock != U->getParent()) {
        CurrentBlock = U->getParent();
        Builder.SetInsertPoint(CurrentBlock,
                               CurrentBlock->getFirstInsertionPt());

        auto *GEP = GetFramePointer(E.first);
        GEP->setName(E.first->getName() + Twine(".reload.addr"));
        if (ByValTy)
          CurrentReload = GEP;
        else
          CurrentReload = Builder.CreateAlignedLoad(
              FrameTy->getElementType(FrameData.getFieldIndex(E.first)), GEP,
              SpillAlignment, E.first->getName() + Twine(".reload"));

        TinyPtrVector<DbgDeclareInst *> DIs = findDbgDeclares(Def);
        TinyPtrVector<DbgVariableRecord *> DVRs = findDVRDeclares(Def);
        // Try best to find dbg.declare. If the spill is a temp, there may not
        // be a direct dbg.declare. Walk up the load chain to find one from an
        // alias.
        if (F->getSubprogram()) {
          auto *CurDef = Def;
          while (DIs.empty() && DVRs.empty() && isa<LoadInst>(CurDef)) {
            auto *LdInst = cast<LoadInst>(CurDef);
            // Only consider ptr to ptr same type load.
            if (LdInst->getPointerOperandType() != LdInst->getType())
              break;
            CurDef = LdInst->getPointerOperand();
            if (!isa<AllocaInst, LoadInst>(CurDef))
              break;
            DIs = findDbgDeclares(CurDef);
            DVRs = findDVRDeclares(CurDef);
          }
        }

        auto SalvageOne = [&](auto *DDI) {
          bool AllowUnresolved = false;
          // This dbg.declare is preserved for all coro-split function
          // fragments. It will be unreachable in the main function, and
          // processed by coro::salvageDebugInfo() by CoroCloner.
          if (UseNewDbgInfoFormat) {
            DbgVariableRecord *NewDVR = new DbgVariableRecord(
                ValueAsMetadata::get(CurrentReload), DDI->getVariable(),
                DDI->getExpression(), DDI->getDebugLoc(),
                DbgVariableRecord::LocationType::Declare);
            Builder.GetInsertPoint()->getParent()->insertDbgRecordBefore(
                NewDVR, Builder.GetInsertPoint());
          } else {
            DIBuilder(*CurrentBlock->getParent()->getParent(), AllowUnresolved)
                .insertDeclare(CurrentReload, DDI->getVariable(),
                               DDI->getExpression(), DDI->getDebugLoc(),
                               &*Builder.GetInsertPoint());
          }
          // This dbg.declare is for the main function entry point.  It
          // will be deleted in all coro-split functions.
          coro::salvageDebugInfo(ArgToAllocaMap, *DDI, Shape.OptimizeFrame,
                                 false /*UseEntryValue*/);
        };
        for_each(DIs, SalvageOne);
        for_each(DVRs, SalvageOne);
      }

      // If we have a single edge PHINode, remove it and replace it with a
      // reload from the coroutine frame. (We already took care of multi edge
      // PHINodes by rewriting them in the rewritePHIs function).
      if (auto *PN = dyn_cast<PHINode>(U)) {
        assert(PN->getNumIncomingValues() == 1 &&
               "unexpected number of incoming "
               "values in the PHINode");
        PN->replaceAllUsesWith(CurrentReload);
        PN->eraseFromParent();
        continue;
      }

      // Replace all uses of CurrentValue in the current instruction with
      // reload.
      U->replaceUsesOfWith(Def, CurrentReload);
      // Instructions are added to Def's user list if the attached
      // debug records use Def. Update those now.
      for (DbgVariableRecord &DVR : filterDbgVars(U->getDbgRecordRange()))
        DVR.replaceVariableLocationOp(Def, CurrentReload, true);
    }
  }

  BasicBlock *FramePtrBB = Shape.getInsertPtAfterFramePtr()->getParent();

  auto SpillBlock = FramePtrBB->splitBasicBlock(
      Shape.getInsertPtAfterFramePtr(), "AllocaSpillBB");
  SpillBlock->splitBasicBlock(&SpillBlock->front(), "PostSpill");
  Shape.AllocaSpillBlock = SpillBlock;

  // retcon and retcon.once lowering assumes all uses have been sunk.
  if (Shape.ABI == coro::ABI::Retcon || Shape.ABI == coro::ABI::RetconOnce ||
      Shape.ABI == coro::ABI::Async) {
    // If we found any allocas, replace all of their remaining uses with Geps.
    Builder.SetInsertPoint(SpillBlock, SpillBlock->begin());
    for (const auto &P : FrameData.Allocas) {
      AllocaInst *Alloca = P.Alloca;
      auto *G = GetFramePointer(Alloca);

      // We are not using ReplaceInstWithInst(P.first, cast<Instruction>(G))
      // here, as we are changing location of the instruction.
      G->takeName(Alloca);
      Alloca->replaceAllUsesWith(G);
      Alloca->eraseFromParent();
    }
    return;
  }

  // If we found any alloca, replace all of their remaining uses with GEP
  // instructions. To remain debugbility, we replace the uses of allocas for
  // dbg.declares and dbg.values with the reload from the frame.
  // Note: We cannot replace the alloca with GEP instructions indiscriminately,
  // as some of the uses may not be dominated by CoroBegin.
  Builder.SetInsertPoint(Shape.AllocaSpillBlock,
                         Shape.AllocaSpillBlock->begin());
  SmallVector<Instruction *, 4> UsersToUpdate;
  for (const auto &A : FrameData.Allocas) {
    AllocaInst *Alloca = A.Alloca;
    UsersToUpdate.clear();
    for (User *U : Alloca->users()) {
      auto *I = cast<Instruction>(U);
      if (DT.dominates(CB, I))
        UsersToUpdate.push_back(I);
    }
    if (UsersToUpdate.empty())
      continue;
    auto *G = GetFramePointer(Alloca);
    G->setName(Alloca->getName() + Twine(".reload.addr"));

    SmallVector<DbgVariableIntrinsic *, 4> DIs;
    SmallVector<DbgVariableRecord *> DbgVariableRecords;
    findDbgUsers(DIs, Alloca, &DbgVariableRecords);
    for (auto *DVI : DIs)
      DVI->replaceUsesOfWith(Alloca, G);
    for (auto *DVR : DbgVariableRecords)
      DVR->replaceVariableLocationOp(Alloca, G);

    for (Instruction *I : UsersToUpdate) {
      // It is meaningless to retain the lifetime intrinsics refer for the
      // member of coroutine frames and the meaningless lifetime intrinsics
      // are possible to block further optimizations.
      if (I->isLifetimeStartOrEnd()) {
        I->eraseFromParent();
        continue;
      }

      I->replaceUsesOfWith(Alloca, G);
    }
  }
  Builder.SetInsertPoint(&*Shape.getInsertPtAfterFramePtr());
  for (const auto &A : FrameData.Allocas) {
    AllocaInst *Alloca = A.Alloca;
    if (A.MayWriteBeforeCoroBegin) {
      // isEscaped really means potentially modified before CoroBegin.
      if (Alloca->isArrayAllocation())
        report_fatal_error(
            "Coroutines cannot handle copying of array allocas yet");

      auto *G = GetFramePointer(Alloca);
      auto *Value = Builder.CreateLoad(Alloca->getAllocatedType(), Alloca);
      Builder.CreateStore(Value, G);
    }
    // For each alias to Alloca created before CoroBegin but used after
    // CoroBegin, we recreate them after CoroBegin by appplying the offset
    // to the pointer in the frame.
    for (const auto &Alias : A.Aliases) {
      auto *FramePtr = GetFramePointer(Alloca);
      auto &Value = *Alias.second;
      auto ITy = IntegerType::get(C, Value.getBitWidth());
      auto *AliasPtr =
          Builder.CreatePtrAdd(FramePtr, ConstantInt::get(ITy, Value));
      Alias.first->replaceUsesWithIf(
          AliasPtr, [&](Use &U) { return DT.dominates(CB, U); });
    }
  }

  // PromiseAlloca is not collected in FrameData.Allocas. So we don't handle
  // the case that the PromiseAlloca may have writes before CoroBegin in the
  // above codes. And it may be problematic in edge cases. See
  // https://github.com/llvm/llvm-project/issues/57861 for an example.
  if (Shape.ABI == coro::ABI::Switch && Shape.SwitchLowering.PromiseAlloca) {
    AllocaInst *PA = Shape.SwitchLowering.PromiseAlloca;
    // If there is memory accessing to promise alloca before CoroBegin;
    bool HasAccessingPromiseBeforeCB = llvm::any_of(PA->uses(), [&](Use &U) {
      auto *Inst = dyn_cast<Instruction>(U.getUser());
      if (!Inst || DT.dominates(CB, Inst))
        return false;

      if (auto *CI = dyn_cast<CallInst>(Inst)) {
        // It is fine if the call wouldn't write to the Promise.
        // This is possible for @llvm.coro.id intrinsics, which
        // would take the promise as the second argument as a
        // marker.
        if (CI->onlyReadsMemory() ||
            CI->onlyReadsMemory(CI->getArgOperandNo(&U)))
          return false;
        return true;
      }

      return isa<StoreInst>(Inst) ||
             // It may take too much time to track the uses.
             // Be conservative about the case the use may escape.
             isa<GetElementPtrInst>(Inst) ||
             // There would always be a bitcast for the promise alloca
             // before we enabled Opaque pointers. And now given
             // opaque pointers are enabled by default. This should be
             // fine.
             isa<BitCastInst>(Inst);
    });
    if (HasAccessingPromiseBeforeCB) {
      Builder.SetInsertPoint(&*Shape.getInsertPtAfterFramePtr());
      auto *G = GetFramePointer(PA);
      auto *Value = Builder.CreateLoad(PA->getAllocatedType(), PA);
      Builder.CreateStore(Value, G);
    }
  }
}

// Moves the values in the PHIs in SuccBB that correspong to PredBB into a new
// PHI in InsertedBB.
static void movePHIValuesToInsertedBlock(BasicBlock *SuccBB,
                                         BasicBlock *InsertedBB,
                                         BasicBlock *PredBB,
                                         PHINode *UntilPHI = nullptr) {
  auto *PN = cast<PHINode>(&SuccBB->front());
  do {
    int Index = PN->getBasicBlockIndex(InsertedBB);
    Value *V = PN->getIncomingValue(Index);
    PHINode *InputV = PHINode::Create(
        V->getType(), 1, V->getName() + Twine(".") + SuccBB->getName());
    InputV->insertBefore(InsertedBB->begin());
    InputV->addIncoming(V, PredBB);
    PN->setIncomingValue(Index, InputV);
    PN = dyn_cast<PHINode>(PN->getNextNode());
  } while (PN != UntilPHI);
}

// Rewrites the PHI Nodes in a cleanuppad.
static void rewritePHIsForCleanupPad(BasicBlock *CleanupPadBB,
                                     CleanupPadInst *CleanupPad) {
  // For every incoming edge to a CleanupPad we will create a new block holding
  // all incoming values in single-value PHI nodes. We will then create another
  // block to act as a dispather (as all unwind edges for related EH blocks
  // must be the same).
  //
  // cleanuppad:
  //    %2 = phi i32[%0, %catchswitch], [%1, %catch.1]
  //    %3 = cleanuppad within none []
  //
  // It will create:
  //
  // cleanuppad.corodispatch
  //    %2 = phi i8[0, %catchswitch], [1, %catch.1]
  //    %3 = cleanuppad within none []
  //    switch i8 % 2, label %unreachable
  //            [i8 0, label %cleanuppad.from.catchswitch
  //             i8 1, label %cleanuppad.from.catch.1]
  // cleanuppad.from.catchswitch:
  //    %4 = phi i32 [%0, %catchswitch]
  //    br %label cleanuppad
  // cleanuppad.from.catch.1:
  //    %6 = phi i32 [%1, %catch.1]
  //    br %label cleanuppad
  // cleanuppad:
  //    %8 = phi i32 [%4, %cleanuppad.from.catchswitch],
  //                 [%6, %cleanuppad.from.catch.1]

  // Unreachable BB, in case switching on an invalid value in the dispatcher.
  auto *UnreachBB = BasicBlock::Create(
      CleanupPadBB->getContext(), "unreachable", CleanupPadBB->getParent());
  IRBuilder<> Builder(UnreachBB);
  Builder.CreateUnreachable();

  // Create a new cleanuppad which will be the dispatcher.
  auto *NewCleanupPadBB =
      BasicBlock::Create(CleanupPadBB->getContext(),
                         CleanupPadBB->getName() + Twine(".corodispatch"),
                         CleanupPadBB->getParent(), CleanupPadBB);
  Builder.SetInsertPoint(NewCleanupPadBB);
  auto *SwitchType = Builder.getInt8Ty();
  auto *SetDispatchValuePN =
      Builder.CreatePHI(SwitchType, pred_size(CleanupPadBB));
  CleanupPad->removeFromParent();
  CleanupPad->insertAfter(SetDispatchValuePN);
  auto *SwitchOnDispatch = Builder.CreateSwitch(SetDispatchValuePN, UnreachBB,
                                                pred_size(CleanupPadBB));

  int SwitchIndex = 0;
  SmallVector<BasicBlock *, 8> Preds(predecessors(CleanupPadBB));
  for (BasicBlock *Pred : Preds) {
    // Create a new cleanuppad and move the PHI values to there.
    auto *CaseBB = BasicBlock::Create(CleanupPadBB->getContext(),
                                      CleanupPadBB->getName() +
                                          Twine(".from.") + Pred->getName(),
                                      CleanupPadBB->getParent(), CleanupPadBB);
    updatePhiNodes(CleanupPadBB, Pred, CaseBB);
    CaseBB->setName(CleanupPadBB->getName() + Twine(".from.") +
                    Pred->getName());
    Builder.SetInsertPoint(CaseBB);
    Builder.CreateBr(CleanupPadBB);
    movePHIValuesToInsertedBlock(CleanupPadBB, CaseBB, NewCleanupPadBB);

    // Update this Pred to the new unwind point.
    setUnwindEdgeTo(Pred->getTerminator(), NewCleanupPadBB);

    // Setup the switch in the dispatcher.
    auto *SwitchConstant = ConstantInt::get(SwitchType, SwitchIndex);
    SetDispatchValuePN->addIncoming(SwitchConstant, Pred);
    SwitchOnDispatch->addCase(SwitchConstant, CaseBB);
    SwitchIndex++;
  }
}

static void cleanupSinglePredPHIs(Function &F) {
  SmallVector<PHINode *, 32> Worklist;
  for (auto &BB : F) {
    for (auto &Phi : BB.phis()) {
      if (Phi.getNumIncomingValues() == 1) {
        Worklist.push_back(&Phi);
      } else
        break;
    }
  }
  while (!Worklist.empty()) {
    auto *Phi = Worklist.pop_back_val();
    auto *OriginalValue = Phi->getIncomingValue(0);
    Phi->replaceAllUsesWith(OriginalValue);
  }
}

static void rewritePHIs(BasicBlock &BB) {
  // For every incoming edge we will create a block holding all
  // incoming values in a single PHI nodes.
  //
  // loop:
  //    %n.val = phi i32[%n, %entry], [%inc, %loop]
  //
  // It will create:
  //
  // loop.from.entry:
  //    %n.loop.pre = phi i32 [%n, %entry]
  //    br %label loop
  // loop.from.loop:
  //    %inc.loop.pre = phi i32 [%inc, %loop]
  //    br %label loop
  //
  // After this rewrite, further analysis will ignore any phi nodes with more
  // than one incoming edge.

  // TODO: Simplify PHINodes in the basic block to remove duplicate
  // predecessors.

  // Special case for CleanupPad: all EH blocks must have the same unwind edge
  // so we need to create an additional "dispatcher" block.
  if (auto *CleanupPad =
          dyn_cast_or_null<CleanupPadInst>(BB.getFirstNonPHI())) {
    SmallVector<BasicBlock *, 8> Preds(predecessors(&BB));
    for (BasicBlock *Pred : Preds) {
      if (CatchSwitchInst *CS =
              dyn_cast<CatchSwitchInst>(Pred->getTerminator())) {
        // CleanupPad with a CatchSwitch predecessor: therefore this is an
        // unwind destination that needs to be handle specially.
        assert(CS->getUnwindDest() == &BB);
        (void)CS;
        rewritePHIsForCleanupPad(&BB, CleanupPad);
        return;
      }
    }
  }

  LandingPadInst *LandingPad = nullptr;
  PHINode *ReplPHI = nullptr;
  if ((LandingPad = dyn_cast_or_null<LandingPadInst>(BB.getFirstNonPHI()))) {
    // ehAwareSplitEdge will clone the LandingPad in all the edge blocks.
    // We replace the original landing pad with a PHINode that will collect the
    // results from all of them.
    ReplPHI = PHINode::Create(LandingPad->getType(), 1, "");
    ReplPHI->insertBefore(LandingPad->getIterator());
    ReplPHI->takeName(LandingPad);
    LandingPad->replaceAllUsesWith(ReplPHI);
    // We will erase the original landing pad at the end of this function after
    // ehAwareSplitEdge cloned it in the transition blocks.
  }

  SmallVector<BasicBlock *, 8> Preds(predecessors(&BB));
  for (BasicBlock *Pred : Preds) {
    auto *IncomingBB = ehAwareSplitEdge(Pred, &BB, LandingPad, ReplPHI);
    IncomingBB->setName(BB.getName() + Twine(".from.") + Pred->getName());

    // Stop the moving of values at ReplPHI, as this is either null or the PHI
    // that replaced the landing pad.
    movePHIValuesToInsertedBlock(&BB, IncomingBB, Pred, ReplPHI);
  }

  if (LandingPad) {
    // Calls to ehAwareSplitEdge function cloned the original lading pad.
    // No longer need it.
    LandingPad->eraseFromParent();
  }
}

static void rewritePHIs(Function &F) {
  SmallVector<BasicBlock *, 8> WorkList;

  for (BasicBlock &BB : F)
    if (auto *PN = dyn_cast<PHINode>(&BB.front()))
      if (PN->getNumIncomingValues() > 1)
        WorkList.push_back(&BB);

  for (BasicBlock *BB : WorkList)
    rewritePHIs(*BB);
}

/// Default materializable callback
// Check for instructions that we can recreate on resume as opposed to spill
// the result into a coroutine frame.
bool coro::defaultMaterializable(Instruction &V) {
  return (isa<CastInst>(&V) || isa<GetElementPtrInst>(&V) ||
          isa<BinaryOperator>(&V) || isa<CmpInst>(&V) || isa<SelectInst>(&V));
}

// Check for structural coroutine intrinsics that should not be spilled into
// the coroutine frame.
static bool isCoroutineStructureIntrinsic(Instruction &I) {
  return isa<CoroIdInst>(&I) || isa<CoroSaveInst>(&I) ||
         isa<CoroSuspendInst>(&I);
}

// For each instruction identified as materializable across the suspend point,
// and its associated DAG of other rematerializable instructions,
// recreate the DAG of instructions after the suspend point.
static void rewriteMaterializableInstructions(
    const SmallMapVector<Instruction *, std::unique_ptr<RematGraph>, 8>
        &AllRemats) {
  // This has to be done in 2 phases
  // Do the remats and record the required defs to be replaced in the
  // original use instructions
  // Once all the remats are complete, replace the uses in the final
  // instructions with the new defs
  typedef struct {
    Instruction *Use;
    Instruction *Def;
    Instruction *Remat;
  } ProcessNode;

  SmallVector<ProcessNode> FinalInstructionsToProcess;

  for (const auto &E : AllRemats) {
    Instruction *Use = E.first;
    Instruction *CurrentMaterialization = nullptr;
    RematGraph *RG = E.second.get();
    ReversePostOrderTraversal<RematGraph *> RPOT(RG);
    SmallVector<Instruction *> InstructionsToProcess;

    // If the target use is actually a suspend instruction then we have to
    // insert the remats into the end of the predecessor (there should only be
    // one). This is so that suspend blocks always have the suspend instruction
    // as the first instruction.
    auto InsertPoint = &*Use->getParent()->getFirstInsertionPt();
    if (isa<AnyCoroSuspendInst>(Use)) {
      BasicBlock *SuspendPredecessorBlock =
          Use->getParent()->getSinglePredecessor();
      assert(SuspendPredecessorBlock && "malformed coro suspend instruction");
      InsertPoint = SuspendPredecessorBlock->getTerminator();
    }

    // Note: skip the first instruction as this is the actual use that we're
    // rematerializing everything for.
    auto I = RPOT.begin();
    ++I;
    for (; I != RPOT.end(); ++I) {
      Instruction *D = (*I)->Node;
      CurrentMaterialization = D->clone();
      CurrentMaterialization->setName(D->getName());
      CurrentMaterialization->insertBefore(InsertPoint);
      InsertPoint = CurrentMaterialization;

      // Replace all uses of Def in the instructions being added as part of this
      // rematerialization group
      for (auto &I : InstructionsToProcess)
        I->replaceUsesOfWith(D, CurrentMaterialization);

      // Don't replace the final use at this point as this can cause problems
      // for other materializations. Instead, for any final use that uses a
      // define that's being rematerialized, record the replace values
      for (unsigned i = 0, E = Use->getNumOperands(); i != E; ++i)
        if (Use->getOperand(i) == D) // Is this operand pointing to oldval?
          FinalInstructionsToProcess.push_back(
              {Use, D, CurrentMaterialization});

      InstructionsToProcess.push_back(CurrentMaterialization);
    }
  }

  // Finally, replace the uses with the defines that we've just rematerialized
  for (auto &R : FinalInstructionsToProcess) {
    if (auto *PN = dyn_cast<PHINode>(R.Use)) {
      assert(PN->getNumIncomingValues() == 1 && "unexpected number of incoming "
                                                "values in the PHINode");
      PN->replaceAllUsesWith(R.Remat);
      PN->eraseFromParent();
      continue;
    }
    R.Use->replaceUsesOfWith(R.Def, R.Remat);
  }
}

// Splits the block at a particular instruction unless it is the first
// instruction in the block with a single predecessor.
static BasicBlock *splitBlockIfNotFirst(Instruction *I, const Twine &Name) {
  auto *BB = I->getParent();
  if (&BB->front() == I) {
    if (BB->getSinglePredecessor()) {
      BB->setName(Name);
      return BB;
    }
  }
  return BB->splitBasicBlock(I, Name);
}

// Split above and below a particular instruction so that it
// will be all alone by itself in a block.
static void splitAround(Instruction *I, const Twine &Name) {
  splitBlockIfNotFirst(I, Name);
  splitBlockIfNotFirst(I->getNextNode(), "After" + Name);
}

static bool isSuspendBlock(BasicBlock *BB) {
  return isa<AnyCoroSuspendInst>(BB->front());
}

typedef SmallPtrSet<BasicBlock*, 8> VisitedBlocksSet;

/// Does control flow starting at the given block ever reach a suspend
/// instruction before reaching a block in VisitedOrFreeBBs?
static bool isSuspendReachableFrom(BasicBlock *From,
                                   VisitedBlocksSet &VisitedOrFreeBBs) {
  // Eagerly try to add this block to the visited set.  If it's already
  // there, stop recursing; this path doesn't reach a suspend before
  // either looping or reaching a freeing block.
  if (!VisitedOrFreeBBs.insert(From).second)
    return false;

  // We assume that we'll already have split suspends into their own blocks.
  if (isSuspendBlock(From))
    return true;

  // Recurse on the successors.
  for (auto *Succ : successors(From)) {
    if (isSuspendReachableFrom(Succ, VisitedOrFreeBBs))
      return true;
  }

  return false;
}

/// Is the given alloca "local", i.e. bounded in lifetime to not cross a
/// suspend point?
static bool isLocalAlloca(CoroAllocaAllocInst *AI) {
  // Seed the visited set with all the basic blocks containing a free
  // so that we won't pass them up.
  VisitedBlocksSet VisitedOrFreeBBs;
  for (auto *User : AI->users()) {
    if (auto FI = dyn_cast<CoroAllocaFreeInst>(User))
      VisitedOrFreeBBs.insert(FI->getParent());
  }

  return !isSuspendReachableFrom(AI->getParent(), VisitedOrFreeBBs);
}

/// After we split the coroutine, will the given basic block be along
/// an obvious exit path for the resumption function?
static bool willLeaveFunctionImmediatelyAfter(BasicBlock *BB,
                                              unsigned depth = 3) {
  // If we've bottomed out our depth count, stop searching and assume
  // that the path might loop back.
  if (depth == 0) return false;

  // If this is a suspend block, we're about to exit the resumption function.
  if (isSuspendBlock(BB)) return true;

  // Recurse into the successors.
  for (auto *Succ : successors(BB)) {
    if (!willLeaveFunctionImmediatelyAfter(Succ, depth - 1))
      return false;
  }

  // If none of the successors leads back in a loop, we're on an exit/abort.
  return true;
}

static bool localAllocaNeedsStackSave(CoroAllocaAllocInst *AI) {
  // Look for a free that isn't sufficiently obviously followed by
  // either a suspend or a termination, i.e. something that will leave
  // the coro resumption frame.
  for (auto *U : AI->users()) {
    auto FI = dyn_cast<CoroAllocaFreeInst>(U);
    if (!FI) continue;

    if (!willLeaveFunctionImmediatelyAfter(FI->getParent()))
      return true;
  }

  // If we never found one, we don't need a stack save.
  return false;
}

/// Turn each of the given local allocas into a normal (dynamic) alloca
/// instruction.
static void lowerLocalAllocas(ArrayRef<CoroAllocaAllocInst*> LocalAllocas,
                              SmallVectorImpl<Instruction*> &DeadInsts) {
  for (auto *AI : LocalAllocas) {
    IRBuilder<> Builder(AI);

    // Save the stack depth.  Try to avoid doing this if the stackrestore
    // is going to immediately precede a return or something.
    Value *StackSave = nullptr;
    if (localAllocaNeedsStackSave(AI))
      StackSave = Builder.CreateStackSave();

    // Allocate memory.
    auto Alloca = Builder.CreateAlloca(Builder.getInt8Ty(), AI->getSize());
    Alloca->setAlignment(AI->getAlignment());

    for (auto *U : AI->users()) {
      // Replace gets with the allocation.
      if (isa<CoroAllocaGetInst>(U)) {
        U->replaceAllUsesWith(Alloca);

      // Replace frees with stackrestores.  This is safe because
      // alloca.alloc is required to obey a stack discipline, although we
      // don't enforce that structurally.
      } else {
        auto FI = cast<CoroAllocaFreeInst>(U);
        if (StackSave) {
          Builder.SetInsertPoint(FI);
          Builder.CreateStackRestore(StackSave);
        }
      }
      DeadInsts.push_back(cast<Instruction>(U));
    }

    DeadInsts.push_back(AI);
  }
}

/// Turn the given coro.alloca.alloc call into a dynamic allocation.
/// This happens during the all-instructions iteration, so it must not
/// delete the call.
static Instruction *lowerNonLocalAlloca(CoroAllocaAllocInst *AI,
                                        coro::Shape &Shape,
                                   SmallVectorImpl<Instruction*> &DeadInsts) {
  IRBuilder<> Builder(AI);
  auto Alloc = Shape.emitAlloc(Builder, AI->getSize(), nullptr);

  for (User *U : AI->users()) {
    if (isa<CoroAllocaGetInst>(U)) {
      U->replaceAllUsesWith(Alloc);
    } else {
      auto FI = cast<CoroAllocaFreeInst>(U);
      Builder.SetInsertPoint(FI);
      Shape.emitDealloc(Builder, Alloc, nullptr);
    }
    DeadInsts.push_back(cast<Instruction>(U));
  }

  // Push this on last so that it gets deleted after all the others.
  DeadInsts.push_back(AI);

  // Return the new allocation value so that we can check for needed spills.
  return cast<Instruction>(Alloc);
}

/// Get the current swifterror value.
static Value *emitGetSwiftErrorValue(IRBuilder<> &Builder, Type *ValueTy,
                                     coro::Shape &Shape) {
  // Make a fake function pointer as a sort of intrinsic.
  auto FnTy = FunctionType::get(ValueTy, {}, false);
  auto Fn = ConstantPointerNull::get(Builder.getPtrTy());

  auto Call = Builder.CreateCall(FnTy, Fn, {});
  Shape.SwiftErrorOps.push_back(Call);

  return Call;
}

/// Set the given value as the current swifterror value.
///
/// Returns a slot that can be used as a swifterror slot.
static Value *emitSetSwiftErrorValue(IRBuilder<> &Builder, Value *V,
                                     coro::Shape &Shape) {
  // Make a fake function pointer as a sort of intrinsic.
  auto FnTy = FunctionType::get(Builder.getPtrTy(),
                                {V->getType()}, false);
  auto Fn = ConstantPointerNull::get(Builder.getPtrTy());

  auto Call = Builder.CreateCall(FnTy, Fn, { V });
  Shape.SwiftErrorOps.push_back(Call);

  return Call;
}

/// Set the swifterror value from the given alloca before a call,
/// then put in back in the alloca afterwards.
///
/// Returns an address that will stand in for the swifterror slot
/// until splitting.
static Value *emitSetAndGetSwiftErrorValueAround(Instruction *Call,
                                                 AllocaInst *Alloca,
                                                 coro::Shape &Shape) {
  auto ValueTy = Alloca->getAllocatedType();
  IRBuilder<> Builder(Call);

  // Load the current value from the alloca and set it as the
  // swifterror value.
  auto ValueBeforeCall = Builder.CreateLoad(ValueTy, Alloca);
  auto Addr = emitSetSwiftErrorValue(Builder, ValueBeforeCall, Shape);

  // Move to after the call.  Since swifterror only has a guaranteed
  // value on normal exits, we can ignore implicit and explicit unwind
  // edges.
  if (isa<CallInst>(Call)) {
    Builder.SetInsertPoint(Call->getNextNode());
  } else {
    auto Invoke = cast<InvokeInst>(Call);
    Builder.SetInsertPoint(Invoke->getNormalDest()->getFirstNonPHIOrDbg());
  }

  // Get the current swifterror value and store it to the alloca.
  auto ValueAfterCall = emitGetSwiftErrorValue(Builder, ValueTy, Shape);
  Builder.CreateStore(ValueAfterCall, Alloca);

  return Addr;
}

/// Eliminate a formerly-swifterror alloca by inserting the get/set
/// intrinsics and attempting to MemToReg the alloca away.
static void eliminateSwiftErrorAlloca(Function &F, AllocaInst *Alloca,
                                      coro::Shape &Shape) {
  for (Use &Use : llvm::make_early_inc_range(Alloca->uses())) {
    // swifterror values can only be used in very specific ways.
    // We take advantage of that here.
    auto User = Use.getUser();
    if (isa<LoadInst>(User) || isa<StoreInst>(User))
      continue;

    assert(isa<CallInst>(User) || isa<InvokeInst>(User));
    auto Call = cast<Instruction>(User);

    auto Addr = emitSetAndGetSwiftErrorValueAround(Call, Alloca, Shape);

    // Use the returned slot address as the call argument.
    Use.set(Addr);
  }

  // All the uses should be loads and stores now.
  assert(isAllocaPromotable(Alloca));
}

/// "Eliminate" a swifterror argument by reducing it to the alloca case
/// and then loading and storing in the prologue and epilog.
///
/// The argument keeps the swifterror flag.
static void eliminateSwiftErrorArgument(Function &F, Argument &Arg,
                                        coro::Shape &Shape,
                             SmallVectorImpl<AllocaInst*> &AllocasToPromote) {
  IRBuilder<> Builder(F.getEntryBlock().getFirstNonPHIOrDbg());

  auto ArgTy = cast<PointerType>(Arg.getType());
  auto ValueTy = PointerType::getUnqual(F.getContext());

  // Reduce to the alloca case:

  // Create an alloca and replace all uses of the arg with it.
  auto Alloca = Builder.CreateAlloca(ValueTy, ArgTy->getAddressSpace());
  Arg.replaceAllUsesWith(Alloca);

  // Set an initial value in the alloca.  swifterror is always null on entry.
  auto InitialValue = Constant::getNullValue(ValueTy);
  Builder.CreateStore(InitialValue, Alloca);

  // Find all the suspends in the function and save and restore around them.
  for (auto *Suspend : Shape.CoroSuspends) {
    (void) emitSetAndGetSwiftErrorValueAround(Suspend, Alloca, Shape);
  }

  // Find all the coro.ends in the function and restore the error value.
  for (auto *End : Shape.CoroEnds) {
    Builder.SetInsertPoint(End);
    auto FinalValue = Builder.CreateLoad(ValueTy, Alloca);
    (void) emitSetSwiftErrorValue(Builder, FinalValue, Shape);
  }

  // Now we can use the alloca logic.
  AllocasToPromote.push_back(Alloca);
  eliminateSwiftErrorAlloca(F, Alloca, Shape);
}

/// Eliminate all problematic uses of swifterror arguments and allocas
/// from the function.  We'll fix them up later when splitting the function.
static void eliminateSwiftError(Function &F, coro::Shape &Shape) {
  SmallVector<AllocaInst*, 4> AllocasToPromote;

  // Look for a swifterror argument.
  for (auto &Arg : F.args()) {
    if (!Arg.hasSwiftErrorAttr()) continue;

    eliminateSwiftErrorArgument(F, Arg, Shape, AllocasToPromote);
    break;
  }

  // Look for swifterror allocas.
  for (auto &Inst : F.getEntryBlock()) {
    auto Alloca = dyn_cast<AllocaInst>(&Inst);
    if (!Alloca || !Alloca->isSwiftError()) continue;

    // Clear the swifterror flag.
    Alloca->setSwiftError(false);

    AllocasToPromote.push_back(Alloca);
    eliminateSwiftErrorAlloca(F, Alloca, Shape);
  }

  // If we have any allocas to promote, compute a dominator tree and
  // promote them en masse.
  if (!AllocasToPromote.empty()) {
    DominatorTree DT(F);
    PromoteMemToReg(AllocasToPromote, DT);
  }
}

/// retcon and retcon.once conventions assume that all spill uses can be sunk
/// after the coro.begin intrinsic.
static void sinkSpillUsesAfterCoroBegin(Function &F,
                                        const FrameDataInfo &FrameData,
                                        CoroBeginInst *CoroBegin) {
  DominatorTree Dom(F);

  SmallSetVector<Instruction *, 32> ToMove;
  SmallVector<Instruction *, 32> Worklist;

  // Collect all users that precede coro.begin.
  for (auto *Def : FrameData.getAllDefs()) {
    for (User *U : Def->users()) {
      auto Inst = cast<Instruction>(U);
      if (Inst->getParent() != CoroBegin->getParent() ||
          Dom.dominates(CoroBegin, Inst))
        continue;
      if (ToMove.insert(Inst))
        Worklist.push_back(Inst);
    }
  }
  // Recursively collect users before coro.begin.
  while (!Worklist.empty()) {
    auto *Def = Worklist.pop_back_val();
    for (User *U : Def->users()) {
      auto Inst = cast<Instruction>(U);
      if (Dom.dominates(CoroBegin, Inst))
        continue;
      if (ToMove.insert(Inst))
        Worklist.push_back(Inst);
    }
  }

  // Sort by dominance.
  SmallVector<Instruction *, 64> InsertionList(ToMove.begin(), ToMove.end());
  llvm::sort(InsertionList, [&Dom](Instruction *A, Instruction *B) -> bool {
    // If a dominates b it should preceed (<) b.
    return Dom.dominates(A, B);
  });

  Instruction *InsertPt = CoroBegin->getNextNode();
  for (Instruction *Inst : InsertionList)
    Inst->moveBefore(InsertPt);
}

/// For each local variable that all of its user are only used inside one of
/// suspended region, we sink their lifetime.start markers to the place where
/// after the suspend block. Doing so minimizes the lifetime of each variable,
/// hence minimizing the amount of data we end up putting on the frame.
static void sinkLifetimeStartMarkers(Function &F, coro::Shape &Shape,
                                     SuspendCrossingInfo &Checker,
                                     const DominatorTree &DT) {
  if (F.hasOptNone())
    return;

  // Collect all possible basic blocks which may dominate all uses of allocas.
  SmallPtrSet<BasicBlock *, 4> DomSet;
  DomSet.insert(&F.getEntryBlock());
  for (auto *CSI : Shape.CoroSuspends) {
    BasicBlock *SuspendBlock = CSI->getParent();
    assert(isSuspendBlock(SuspendBlock) && SuspendBlock->getSingleSuccessor() &&
           "should have split coro.suspend into its own block");
    DomSet.insert(SuspendBlock->getSingleSuccessor());
  }

  for (Instruction &I : instructions(F)) {
    AllocaInst* AI = dyn_cast<AllocaInst>(&I);
    if (!AI)
      continue;

    for (BasicBlock *DomBB : DomSet) {
      bool Valid = true;
      SmallVector<Instruction *, 1> Lifetimes;

      auto isLifetimeStart = [](Instruction* I) {
        if (auto* II = dyn_cast<IntrinsicInst>(I))
          return II->getIntrinsicID() == Intrinsic::lifetime_start;
        return false;
      };

      auto collectLifetimeStart = [&](Instruction *U, AllocaInst *AI) {
        if (isLifetimeStart(U)) {
          Lifetimes.push_back(U);
          return true;
        }
        if (!U->hasOneUse() || U->stripPointerCasts() != AI)
          return false;
        if (isLifetimeStart(U->user_back())) {
          Lifetimes.push_back(U->user_back());
          return true;
        }
        return false;
      };

      for (User *U : AI->users()) {
        Instruction *UI = cast<Instruction>(U);
        // For all users except lifetime.start markers, if they are all
        // dominated by one of the basic blocks and do not cross
        // suspend points as well, then there is no need to spill the
        // instruction.
        if (!DT.dominates(DomBB, UI->getParent()) ||
            Checker.isDefinitionAcrossSuspend(DomBB, UI)) {
          // Skip lifetime.start, GEP and bitcast used by lifetime.start
          // markers.
          if (collectLifetimeStart(UI, AI))
            continue;
          Valid = false;
          break;
        }
      }
      // Sink lifetime.start markers to dominate block when they are
      // only used outside the region.
      if (Valid && Lifetimes.size() != 0) {
        auto *NewLifetime = Lifetimes[0]->clone();
        NewLifetime->replaceUsesOfWith(NewLifetime->getOperand(1), AI);
        NewLifetime->insertBefore(DomBB->getTerminator());

        // All the outsided lifetime.start markers are no longer necessary.
        for (Instruction *S : Lifetimes)
          S->eraseFromParent();

        break;
      }
    }
  }
}

static void collectFrameAlloca(AllocaInst *AI, coro::Shape &Shape,
                               const SuspendCrossingInfo &Checker,
                               SmallVectorImpl<AllocaInfo> &Allocas,
                               const DominatorTree &DT) {
  if (Shape.CoroSuspends.empty())
    return;

  // The PromiseAlloca will be specially handled since it needs to be in a
  // fixed position in the frame.
  if (AI == Shape.SwitchLowering.PromiseAlloca)
    return;

  // The __coro_gro alloca should outlive the promise, make sure we
  // keep it outside the frame.
  if (AI->hasMetadata(LLVMContext::MD_coro_outside_frame))
    return;

  // The code that uses lifetime.start intrinsic does not work for functions
  // with loops without exit. Disable it on ABIs we know to generate such
  // code.
  bool ShouldUseLifetimeStartInfo =
      (Shape.ABI != coro::ABI::Async && Shape.ABI != coro::ABI::Retcon &&
       Shape.ABI != coro::ABI::RetconOnce);
  AllocaUseVisitor Visitor{AI->getDataLayout(), DT, Shape, Checker,
                           ShouldUseLifetimeStartInfo};
  Visitor.visitPtr(*AI);
  if (!Visitor.getShouldLiveOnFrame())
    return;
  Allocas.emplace_back(AI, Visitor.getAliasesCopy(),
                       Visitor.getMayWriteBeforeCoroBegin());
}

static std::optional<std::pair<Value &, DIExpression &>>
salvageDebugInfoImpl(SmallDenseMap<Argument *, AllocaInst *, 4> &ArgToAllocaMap,
                     bool OptimizeFrame, bool UseEntryValue, Function *F,
                     Value *Storage, DIExpression *Expr,
                     bool SkipOutermostLoad) {
  IRBuilder<> Builder(F->getContext());
  auto InsertPt = F->getEntryBlock().getFirstInsertionPt();
  while (isa<IntrinsicInst>(InsertPt))
    ++InsertPt;
  Builder.SetInsertPoint(&F->getEntryBlock(), InsertPt);

  while (auto *Inst = dyn_cast_or_null<Instruction>(Storage)) {
    if (auto *LdInst = dyn_cast<LoadInst>(Inst)) {
      Storage = LdInst->getPointerOperand();
      // FIXME: This is a heuristic that works around the fact that
      // LLVM IR debug intrinsics cannot yet distinguish between
      // memory and value locations: Because a dbg.declare(alloca) is
      // implicitly a memory location no DW_OP_deref operation for the
      // last direct load from an alloca is necessary.  This condition
      // effectively drops the *last* DW_OP_deref in the expression.
      if (!SkipOutermostLoad)
        Expr = DIExpression::prepend(Expr, DIExpression::DerefBefore);
    } else if (auto *StInst = dyn_cast<StoreInst>(Inst)) {
      Storage = StInst->getValueOperand();
    } else {
      SmallVector<uint64_t, 16> Ops;
      SmallVector<Value *, 0> AdditionalValues;
      Value *Op = llvm::salvageDebugInfoImpl(
          *Inst, Expr ? Expr->getNumLocationOperands() : 0, Ops,
          AdditionalValues);
      if (!Op || !AdditionalValues.empty()) {
        // If salvaging failed or salvaging produced more than one location
        // operand, give up.
        break;
      }
      Storage = Op;
      Expr = DIExpression::appendOpsToArg(Expr, Ops, 0, /*StackValue*/ false);
    }
    SkipOutermostLoad = false;
  }
  if (!Storage)
    return std::nullopt;

  auto *StorageAsArg = dyn_cast<Argument>(Storage);
  const bool IsSwiftAsyncArg =
      StorageAsArg && StorageAsArg->hasAttribute(Attribute::SwiftAsync);

  // Swift async arguments are described by an entry value of the ABI-defined
  // register containing the coroutine context.
  // Entry values in variadic expressions are not supported.
  if (IsSwiftAsyncArg && UseEntryValue && !Expr->isEntryValue() &&
      Expr->isSingleLocationExpression())
    Expr = DIExpression::prepend(Expr, DIExpression::EntryValue);

  // If the coroutine frame is an Argument, store it in an alloca to improve
  // its availability (e.g. registers may be clobbered).
  // Avoid this if optimizations are enabled (they would remove the alloca) or
  // if the value is guaranteed to be available through other means (e.g. swift
  // ABI guarantees).
  if (StorageAsArg && !OptimizeFrame && !IsSwiftAsyncArg) {
    auto &Cached = ArgToAllocaMap[StorageAsArg];
    if (!Cached) {
      Cached = Builder.CreateAlloca(Storage->getType(), 0, nullptr,
                                    Storage->getName() + ".debug");
      Builder.CreateStore(Storage, Cached);
    }
    Storage = Cached;
    // FIXME: LLVM lacks nuanced semantics to differentiate between
    // memory and direct locations at the IR level. The backend will
    // turn a dbg.declare(alloca, ..., DIExpression()) into a memory
    // location. Thus, if there are deref and offset operations in the
    // expression, we need to add a DW_OP_deref at the *start* of the
    // expression to first load the contents of the alloca before
    // adjusting it with the expression.
    Expr = DIExpression::prepend(Expr, DIExpression::DerefBefore);
  }

  return {{*Storage, *Expr}};
}

void coro::salvageDebugInfo(
    SmallDenseMap<Argument *, AllocaInst *, 4> &ArgToAllocaMap,
    DbgVariableIntrinsic &DVI, bool OptimizeFrame, bool UseEntryValue) {

  Function *F = DVI.getFunction();
  // Follow the pointer arithmetic all the way to the incoming
  // function argument and convert into a DIExpression.
  bool SkipOutermostLoad = !isa<DbgValueInst>(DVI);
  Value *OriginalStorage = DVI.getVariableLocationOp(0);

  auto SalvagedInfo = ::salvageDebugInfoImpl(
      ArgToAllocaMap, OptimizeFrame, UseEntryValue, F, OriginalStorage,
      DVI.getExpression(), SkipOutermostLoad);
  if (!SalvagedInfo)
    return;

  Value *Storage = &SalvagedInfo->first;
  DIExpression *Expr = &SalvagedInfo->second;

  DVI.replaceVariableLocationOp(OriginalStorage, Storage);
  DVI.setExpression(Expr);
  // We only hoist dbg.declare today since it doesn't make sense to hoist
  // dbg.value since it does not have the same function wide guarantees that
  // dbg.declare does.
  if (isa<DbgDeclareInst>(DVI)) {
    std::optional<BasicBlock::iterator> InsertPt;
    if (auto *I = dyn_cast<Instruction>(Storage)) {
      InsertPt = I->getInsertionPointAfterDef();
      // Update DILocation only if variable was not inlined.
      DebugLoc ILoc = I->getDebugLoc();
      DebugLoc DVILoc = DVI.getDebugLoc();
      if (ILoc && DVILoc &&
          DVILoc->getScope()->getSubprogram() ==
              ILoc->getScope()->getSubprogram())
        DVI.setDebugLoc(I->getDebugLoc());
    } else if (isa<Argument>(Storage))
      InsertPt = F->getEntryBlock().begin();
    if (InsertPt)
      DVI.moveBefore(*(*InsertPt)->getParent(), *InsertPt);
  }
}

void coro::salvageDebugInfo(
    SmallDenseMap<Argument *, AllocaInst *, 4> &ArgToAllocaMap,
    DbgVariableRecord &DVR, bool OptimizeFrame, bool UseEntryValue) {

  Function *F = DVR.getFunction();
  // Follow the pointer arithmetic all the way to the incoming
  // function argument and convert into a DIExpression.
  bool SkipOutermostLoad = DVR.isDbgDeclare();
  Value *OriginalStorage = DVR.getVariableLocationOp(0);

  auto SalvagedInfo = ::salvageDebugInfoImpl(
      ArgToAllocaMap, OptimizeFrame, UseEntryValue, F, OriginalStorage,
      DVR.getExpression(), SkipOutermostLoad);
  if (!SalvagedInfo)
    return;

  Value *Storage = &SalvagedInfo->first;
  DIExpression *Expr = &SalvagedInfo->second;

  DVR.replaceVariableLocationOp(OriginalStorage, Storage);
  DVR.setExpression(Expr);
  // We only hoist dbg.declare today since it doesn't make sense to hoist
  // dbg.value since it does not have the same function wide guarantees that
  // dbg.declare does.
  if (DVR.getType() == DbgVariableRecord::LocationType::Declare) {
    std::optional<BasicBlock::iterator> InsertPt;
    if (auto *I = dyn_cast<Instruction>(Storage)) {
      InsertPt = I->getInsertionPointAfterDef();
      // Update DILocation only if variable was not inlined.
      DebugLoc ILoc = I->getDebugLoc();
      DebugLoc DVRLoc = DVR.getDebugLoc();
      if (ILoc && DVRLoc &&
          DVRLoc->getScope()->getSubprogram() ==
              ILoc->getScope()->getSubprogram())
        DVR.setDebugLoc(ILoc);
    } else if (isa<Argument>(Storage))
      InsertPt = F->getEntryBlock().begin();
    if (InsertPt) {
      DVR.removeFromParent();
      (*InsertPt)->getParent()->insertDbgRecordBefore(&DVR, *InsertPt);
    }
  }
}

static void doRematerializations(
    Function &F, SuspendCrossingInfo &Checker,
    const std::function<bool(Instruction &)> &MaterializableCallback) {
  if (F.hasOptNone())
    return;

  SpillInfo Spills;

  // See if there are materializable instructions across suspend points
  // We record these as the starting point to also identify materializable
  // defs of uses in these operations
  for (Instruction &I : instructions(F)) {
    if (!MaterializableCallback(I))
      continue;
    for (User *U : I.users())
      if (Checker.isDefinitionAcrossSuspend(I, U))
        Spills[&I].push_back(cast<Instruction>(U));
  }

  // Process each of the identified rematerializable instructions
  // and add predecessor instructions that can also be rematerialized.
  // This is actually a graph of instructions since we could potentially
  // have multiple uses of a def in the set of predecessor instructions.
  // The approach here is to maintain a graph of instructions for each bottom
  // level instruction - where we have a unique set of instructions (nodes)
  // and edges between them. We then walk the graph in reverse post-dominator
  // order to insert them past the suspend point, but ensure that ordering is
  // correct. We also rely on CSE removing duplicate defs for remats of
  // different instructions with a def in common (rather than maintaining more
  // complex graphs for each suspend point)

  // We can do this by adding new nodes to the list for each suspend
  // point. Then using standard GraphTraits to give a reverse post-order
  // traversal when we insert the nodes after the suspend
  SmallMapVector<Instruction *, std::unique_ptr<RematGraph>, 8> AllRemats;
  for (auto &E : Spills) {
    for (Instruction *U : E.second) {
      // Don't process a user twice (this can happen if the instruction uses
      // more than one rematerializable def)
      if (AllRemats.count(U))
        continue;

      // Constructor creates the whole RematGraph for the given Use
      auto RematUPtr =
          std::make_unique<RematGraph>(MaterializableCallback, U, Checker);

      LLVM_DEBUG(dbgs() << "***** Next remat group *****\n";
                 ReversePostOrderTraversal<RematGraph *> RPOT(RematUPtr.get());
                 for (auto I = RPOT.begin(); I != RPOT.end();
                      ++I) { (*I)->Node->dump(); } dbgs()
                 << "\n";);

      AllRemats[U] = std::move(RematUPtr);
    }
  }

  // Rewrite materializable instructions to be materialized at the use
  // point.
  LLVM_DEBUG(dumpRemats("Materializations", AllRemats));
  rewriteMaterializableInstructions(AllRemats);
}

void coro::buildCoroutineFrame(
    Function &F, Shape &Shape, TargetTransformInfo &TTI,
    const std::function<bool(Instruction &)> &MaterializableCallback) {
  // Don't eliminate swifterror in async functions that won't be split.
  if (Shape.ABI != coro::ABI::Async || !Shape.CoroSuspends.empty())
    eliminateSwiftError(F, Shape);

  if (Shape.ABI == coro::ABI::Switch &&
      Shape.SwitchLowering.PromiseAlloca) {
    Shape.getSwitchCoroId()->clearPromise();
  }

  // Make sure that all coro.save, coro.suspend and the fallthrough coro.end
  // intrinsics are in their own blocks to simplify the logic of building up
  // SuspendCrossing data.
  for (auto *CSI : Shape.CoroSuspends) {
    if (auto *Save = CSI->getCoroSave())
      splitAround(Save, "CoroSave");
    splitAround(CSI, "CoroSuspend");
  }

  // Put CoroEnds into their own blocks.
  for (AnyCoroEndInst *CE : Shape.CoroEnds) {
    splitAround(CE, "CoroEnd");

    // Emit the musttail call function in a new block before the CoroEnd.
    // We do this here so that the right suspend crossing info is computed for
    // the uses of the musttail call function call. (Arguments to the coro.end
    // instructions would be ignored)
    if (auto *AsyncEnd = dyn_cast<CoroAsyncEndInst>(CE)) {
      auto *MustTailCallFn = AsyncEnd->getMustTailCallFunction();
      if (!MustTailCallFn)
        continue;
      IRBuilder<> Builder(AsyncEnd);
      SmallVector<Value *, 8> Args(AsyncEnd->args());
      auto Arguments = ArrayRef<Value *>(Args).drop_front(3);
      auto *Call = createMustTailCall(AsyncEnd->getDebugLoc(), MustTailCallFn,
                                      TTI, Arguments, Builder);
      splitAround(Call, "MustTailCall.Before.CoroEnd");
    }
  }

  // Later code makes structural assumptions about single predecessors phis e.g
  // that they are not live across a suspend point.
  cleanupSinglePredPHIs(F);

  // Transforms multi-edge PHI Nodes, so that any value feeding into a PHI will
  // never has its definition separated from the PHI by the suspend point.
  rewritePHIs(F);

  // Build suspend crossing info.
  SuspendCrossingInfo Checker(F, Shape);

  doRematerializations(F, Checker, MaterializableCallback);

  const DominatorTree DT(F);
  FrameDataInfo FrameData;
  SmallVector<CoroAllocaAllocInst*, 4> LocalAllocas;
  SmallVector<Instruction*, 4> DeadInstructions;
  if (Shape.ABI != coro::ABI::Async && Shape.ABI != coro::ABI::Retcon &&
      Shape.ABI != coro::ABI::RetconOnce)
    sinkLifetimeStartMarkers(F, Shape, Checker, DT);

  // Collect the spills for arguments and other not-materializable values.
  for (Argument &A : F.args())
    for (User *U : A.users())
      if (Checker.isDefinitionAcrossSuspend(A, U))
        FrameData.Spills[&A].push_back(cast<Instruction>(U));

  for (Instruction &I : instructions(F)) {
    // Values returned from coroutine structure intrinsics should not be part
    // of the Coroutine Frame.
    if (isCoroutineStructureIntrinsic(I) || &I == Shape.CoroBegin)
      continue;

    // Handle alloca.alloc specially here.
    if (auto AI = dyn_cast<CoroAllocaAllocInst>(&I)) {
      // Check whether the alloca's lifetime is bounded by suspend points.
      if (isLocalAlloca(AI)) {
        LocalAllocas.push_back(AI);
        continue;
      }

      // If not, do a quick rewrite of the alloca and then add spills of
      // the rewritten value.  The rewrite doesn't invalidate anything in
      // Spills because the other alloca intrinsics have no other operands
      // besides AI, and it doesn't invalidate the iteration because we delay
      // erasing AI.
      auto Alloc = lowerNonLocalAlloca(AI, Shape, DeadInstructions);

      for (User *U : Alloc->users()) {
        if (Checker.isDefinitionAcrossSuspend(*Alloc, U))
          FrameData.Spills[Alloc].push_back(cast<Instruction>(U));
      }
      continue;
    }

    // Ignore alloca.get; we process this as part of coro.alloca.alloc.
    if (isa<CoroAllocaGetInst>(I))
      continue;

    if (auto *AI = dyn_cast<AllocaInst>(&I)) {
      collectFrameAlloca(AI, Shape, Checker, FrameData.Allocas, DT);
      continue;
    }

    for (User *U : I.users())
      if (Checker.isDefinitionAcrossSuspend(I, U)) {
        // We cannot spill a token.
        if (I.getType()->isTokenTy())
          report_fatal_error(
              "token definition is separated from the use by a suspend point");
        FrameData.Spills[&I].push_back(cast<Instruction>(U));
      }
  }

  LLVM_DEBUG(dumpAllocas(FrameData.Allocas));

  // We don't want the layout of coroutine frame to be affected
  // by debug information. So we only choose to salvage DbgValueInst for
  // whose value is already in the frame.
  // We would handle the dbg.values for allocas specially
  for (auto &Iter : FrameData.Spills) {
    auto *V = Iter.first;
    SmallVector<DbgValueInst *, 16> DVIs;
    SmallVector<DbgVariableRecord *, 16> DVRs;
    findDbgValues(DVIs, V, &DVRs);
    for (DbgValueInst *DVI : DVIs)
      if (Checker.isDefinitionAcrossSuspend(*V, DVI))
        FrameData.Spills[V].push_back(DVI);
    // Add the instructions which carry debug info that is in the frame.
    for (DbgVariableRecord *DVR : DVRs)
      if (Checker.isDefinitionAcrossSuspend(*V, DVR->Marker->MarkedInstr))
        FrameData.Spills[V].push_back(DVR->Marker->MarkedInstr);
  }

  LLVM_DEBUG(dumpSpills("Spills", FrameData.Spills));
  if (Shape.ABI == coro::ABI::Retcon || Shape.ABI == coro::ABI::RetconOnce ||
      Shape.ABI == coro::ABI::Async)
    sinkSpillUsesAfterCoroBegin(F, FrameData, Shape.CoroBegin);
  Shape.FrameTy = buildFrameType(F, Shape, FrameData);
  Shape.FramePtr = Shape.CoroBegin;
  // For now, this works for C++ programs only.
  buildFrameDebugInfo(F, Shape, FrameData);
  insertSpills(FrameData, Shape);
  lowerLocalAllocas(LocalAllocas, DeadInstructions);

  for (auto *I : DeadInstructions)
    I->eraseFromParent();
}
