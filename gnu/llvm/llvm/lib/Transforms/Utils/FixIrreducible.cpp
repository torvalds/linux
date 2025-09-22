//===- FixIrreducible.cpp - Convert irreducible control-flow into loops ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An irreducible SCC is one which has multiple "header" blocks, i.e., blocks
// with control-flow edges incident from outside the SCC.  This pass converts a
// irreducible SCC into a natural loop by applying the following transformation:
//
// 1. Collect the set of headers H of the SCC.
// 2. Collect the set of predecessors P of these headers. These may be inside as
//    well as outside the SCC.
// 3. Create block N and redirect every edge from set P to set H through N.
//
// This converts the SCC into a natural loop with N as the header: N is the only
// block with edges incident from outside the SCC, and all backedges in the SCC
// are incident on N, i.e., for every backedge, the head now dominates the tail.
//
// INPUT CFG: The blocks A and B form an irreducible loop with two headers.
//
//                        Entry
//                       /     \
//                      v       v
//                      A ----> B
//                      ^      /|
//                       `----' |
//                              v
//                             Exit
//
// OUTPUT CFG: Edges incident on A and B are now redirected through a
// new block N, forming a natural loop consisting of N, A and B.
//
//                        Entry
//                          |
//                          v
//                    .---> N <---.
//                   /     / \     \
//                  |     /   \     |
//                  \    v     v    /
//                   `-- A     B --'
//                             |
//                             v
//                            Exit
//
// The transformation is applied to every maximal SCC that is not already
// recognized as a loop. The pass operates on all maximal SCCs found in the
// function body outside of any loop, as well as those found inside each loop,
// including inside any newly created loops. This ensures that any SCC hidden
// inside a maximal SCC is also transformed.
//
// The actual transformation is handled by function CreateControlFlowHub, which
// takes a set of incoming blocks (the predecessors) and outgoing blocks (the
// headers). The function also moves every PHINode in an outgoing block to the
// hub. Since the hub dominates all the outgoing blocks, each such PHINode
// continues to dominate its uses. Since every header in an SCC has at least two
// predecessors, every value used in the header (or later) but defined in a
// predecessor (or earlier) is represented by a PHINode in a header. Hence the
// above handling of PHINodes is sufficient and no further processing is
// required to restore SSA.
//
// Limitation: The pass cannot handle switch statements and indirect
//             branches. Both must be lowered to plain branches first.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/FixIrreducible.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#define DEBUG_TYPE "fix-irreducible"

using namespace llvm;

namespace {
struct FixIrreducible : public FunctionPass {
  static char ID;
  FixIrreducible() : FunctionPass(ID) {
    initializeFixIrreduciblePass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
  }

  bool runOnFunction(Function &F) override;
};
} // namespace

char FixIrreducible::ID = 0;

FunctionPass *llvm::createFixIrreduciblePass() { return new FixIrreducible(); }

INITIALIZE_PASS_BEGIN(FixIrreducible, "fix-irreducible",
                      "Convert irreducible control-flow into natural loops",
                      false /* Only looks at CFG */, false /* Analysis Pass */)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(FixIrreducible, "fix-irreducible",
                    "Convert irreducible control-flow into natural loops",
                    false /* Only looks at CFG */, false /* Analysis Pass */)

// When a new loop is created, existing children of the parent loop may now be
// fully inside the new loop. Reconnect these as children of the new loop.
static void reconnectChildLoops(LoopInfo &LI, Loop *ParentLoop, Loop *NewLoop,
                                SetVector<BasicBlock *> &Blocks,
                                SetVector<BasicBlock *> &Headers) {
  auto &CandidateLoops = ParentLoop ? ParentLoop->getSubLoopsVector()
                                    : LI.getTopLevelLoopsVector();
  // The new loop cannot be its own child, and any candidate is a
  // child iff its header is owned by the new loop. Move all the
  // children to a new vector.
  auto FirstChild = std::partition(
      CandidateLoops.begin(), CandidateLoops.end(), [&](Loop *L) {
        return L == NewLoop || !Blocks.contains(L->getHeader());
      });
  SmallVector<Loop *, 8> ChildLoops(FirstChild, CandidateLoops.end());
  CandidateLoops.erase(FirstChild, CandidateLoops.end());

  for (Loop *Child : ChildLoops) {
    LLVM_DEBUG(dbgs() << "child loop: " << Child->getHeader()->getName()
                      << "\n");
    // TODO: A child loop whose header is also a header in the current
    // SCC gets destroyed since its backedges are removed. That may
    // not be necessary if we can retain such backedges.
    if (Headers.count(Child->getHeader())) {
      for (auto *BB : Child->blocks()) {
        if (LI.getLoopFor(BB) != Child)
          continue;
        LI.changeLoopFor(BB, NewLoop);
        LLVM_DEBUG(dbgs() << "moved block from child: " << BB->getName()
                          << "\n");
      }
      std::vector<Loop *> GrandChildLoops;
      std::swap(GrandChildLoops, Child->getSubLoopsVector());
      for (auto *GrandChildLoop : GrandChildLoops) {
        GrandChildLoop->setParentLoop(nullptr);
        NewLoop->addChildLoop(GrandChildLoop);
      }
      LI.destroy(Child);
      LLVM_DEBUG(dbgs() << "subsumed child loop (common header)\n");
      continue;
    }

    Child->setParentLoop(nullptr);
    NewLoop->addChildLoop(Child);
    LLVM_DEBUG(dbgs() << "added child loop to new loop\n");
  }
}

// Given a set of blocks and headers in an irreducible SCC, convert it into a
// natural loop. Also insert this new loop at its appropriate place in the
// hierarchy of loops.
static void createNaturalLoopInternal(LoopInfo &LI, DominatorTree &DT,
                                      Loop *ParentLoop,
                                      SetVector<BasicBlock *> &Blocks,
                                      SetVector<BasicBlock *> &Headers) {
#ifndef NDEBUG
  // All headers are part of the SCC
  for (auto *H : Headers) {
    assert(Blocks.count(H));
  }
#endif

  SetVector<BasicBlock *> Predecessors;
  for (auto *H : Headers) {
    for (auto *P : predecessors(H)) {
      Predecessors.insert(P);
    }
  }

  LLVM_DEBUG(
      dbgs() << "Found predecessors:";
      for (auto P : Predecessors) {
        dbgs() << " " << P->getName();
      }
      dbgs() << "\n");

  // Redirect all the backedges through a "hub" consisting of a series
  // of guard blocks that manage the flow of control from the
  // predecessors to the headers.
  SmallVector<BasicBlock *, 8> GuardBlocks;
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  CreateControlFlowHub(&DTU, GuardBlocks, Predecessors, Headers, "irr");
#if defined(EXPENSIVE_CHECKS)
  assert(DT.verify(DominatorTree::VerificationLevel::Full));
#else
  assert(DT.verify(DominatorTree::VerificationLevel::Fast));
#endif

  // Create a new loop from the now-transformed cycle
  auto NewLoop = LI.AllocateLoop();
  if (ParentLoop) {
    ParentLoop->addChildLoop(NewLoop);
  } else {
    LI.addTopLevelLoop(NewLoop);
  }

  // Add the guard blocks to the new loop. The first guard block is
  // the head of all the backedges, and it is the first to be inserted
  // in the loop. This ensures that it is recognized as the
  // header. Since the new loop is already in LoopInfo, the new blocks
  // are also propagated up the chain of parent loops.
  for (auto *G : GuardBlocks) {
    LLVM_DEBUG(dbgs() << "added guard block: " << G->getName() << "\n");
    NewLoop->addBasicBlockToLoop(G, LI);
  }

  // Add the SCC blocks to the new loop.
  for (auto *BB : Blocks) {
    NewLoop->addBlockEntry(BB);
    if (LI.getLoopFor(BB) == ParentLoop) {
      LLVM_DEBUG(dbgs() << "moved block from parent: " << BB->getName()
                        << "\n");
      LI.changeLoopFor(BB, NewLoop);
    } else {
      LLVM_DEBUG(dbgs() << "added block from child: " << BB->getName() << "\n");
    }
  }
  LLVM_DEBUG(dbgs() << "header for new loop: "
                    << NewLoop->getHeader()->getName() << "\n");

  reconnectChildLoops(LI, ParentLoop, NewLoop, Blocks, Headers);

  NewLoop->verifyLoop();
  if (ParentLoop) {
    ParentLoop->verifyLoop();
  }
#if defined(EXPENSIVE_CHECKS)
  LI.verify(DT);
#endif // EXPENSIVE_CHECKS
}

namespace llvm {
// Enable the graph traits required for traversing a Loop body.
template <> struct GraphTraits<Loop> : LoopBodyTraits {};
} // namespace llvm

// Overloaded wrappers to go with the function template below.
static BasicBlock *unwrapBlock(BasicBlock *B) { return B; }
static BasicBlock *unwrapBlock(LoopBodyTraits::NodeRef &N) { return N.second; }

static void createNaturalLoop(LoopInfo &LI, DominatorTree &DT, Function *F,
                              SetVector<BasicBlock *> &Blocks,
                              SetVector<BasicBlock *> &Headers) {
  createNaturalLoopInternal(LI, DT, nullptr, Blocks, Headers);
}

static void createNaturalLoop(LoopInfo &LI, DominatorTree &DT, Loop &L,
                              SetVector<BasicBlock *> &Blocks,
                              SetVector<BasicBlock *> &Headers) {
  createNaturalLoopInternal(LI, DT, &L, Blocks, Headers);
}

// Convert irreducible SCCs; Graph G may be a Function* or a Loop&.
template <class Graph>
static bool makeReducible(LoopInfo &LI, DominatorTree &DT, Graph &&G) {
  bool Changed = false;
  for (auto Scc = scc_begin(G); !Scc.isAtEnd(); ++Scc) {
    if (Scc->size() < 2)
      continue;
    SetVector<BasicBlock *> Blocks;
    LLVM_DEBUG(dbgs() << "Found SCC:");
    for (auto N : *Scc) {
      auto BB = unwrapBlock(N);
      LLVM_DEBUG(dbgs() << " " << BB->getName());
      Blocks.insert(BB);
    }
    LLVM_DEBUG(dbgs() << "\n");

    // Minor optimization: The SCC blocks are usually discovered in an order
    // that is the opposite of the order in which these blocks appear as branch
    // targets. This results in a lot of condition inversions in the control
    // flow out of the new ControlFlowHub, which can be mitigated if the orders
    // match. So we discover the headers using the reverse of the block order.
    SetVector<BasicBlock *> Headers;
    LLVM_DEBUG(dbgs() << "Found headers:");
    for (auto *BB : reverse(Blocks)) {
      for (const auto P : predecessors(BB)) {
        // Skip unreachable predecessors.
        if (!DT.isReachableFromEntry(P))
          continue;
        if (!Blocks.count(P)) {
          LLVM_DEBUG(dbgs() << " " << BB->getName());
          Headers.insert(BB);
          break;
        }
      }
    }
    LLVM_DEBUG(dbgs() << "\n");

    if (Headers.size() == 1) {
      assert(LI.isLoopHeader(Headers.front()));
      LLVM_DEBUG(dbgs() << "Natural loop with a single header: skipped\n");
      continue;
    }
    createNaturalLoop(LI, DT, G, Blocks, Headers);
    Changed = true;
  }
  return Changed;
}

static bool FixIrreducibleImpl(Function &F, LoopInfo &LI, DominatorTree &DT) {
  LLVM_DEBUG(dbgs() << "===== Fix irreducible control-flow in function: "
                    << F.getName() << "\n");

  assert(hasOnlySimpleTerminator(F) && "Unsupported block terminator.");

  bool Changed = false;
  SmallVector<Loop *, 8> WorkList;

  LLVM_DEBUG(dbgs() << "visiting top-level\n");
  Changed |= makeReducible(LI, DT, &F);

  // Any SCCs reduced are now already in the list of top-level loops, so simply
  // add them all to the worklist.
  append_range(WorkList, LI);

  while (!WorkList.empty()) {
    auto L = WorkList.pop_back_val();
    LLVM_DEBUG(dbgs() << "visiting loop with header "
                      << L->getHeader()->getName() << "\n");
    Changed |= makeReducible(LI, DT, *L);
    // Any SCCs reduced are now already in the list of child loops, so simply
    // add them all to the worklist.
    WorkList.append(L->begin(), L->end());
  }

  return Changed;
}

bool FixIrreducible::runOnFunction(Function &F) {
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  return FixIrreducibleImpl(F, LI, DT);
}

PreservedAnalyses FixIrreduciblePass::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  if (!FixIrreducibleImpl(F, LI, DT))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<LoopAnalysis>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
