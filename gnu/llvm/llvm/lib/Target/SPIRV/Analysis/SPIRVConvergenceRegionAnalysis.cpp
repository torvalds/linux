//===- ConvergenceRegionAnalysis.h -----------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The analysis determines the convergence region for each basic block of
// the module, and provides a tree-like structure describing the region
// hierarchy.
//
//===----------------------------------------------------------------------===//

#include "SPIRVConvergenceRegionAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/InitializePasses.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include <optional>
#include <queue>

#define DEBUG_TYPE "spirv-convergence-region-analysis"

using namespace llvm;

namespace llvm {
void initializeSPIRVConvergenceRegionAnalysisWrapperPassPass(PassRegistry &);
} // namespace llvm

INITIALIZE_PASS_BEGIN(SPIRVConvergenceRegionAnalysisWrapperPass,
                      "convergence-region",
                      "SPIRV convergence regions analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(SPIRVConvergenceRegionAnalysisWrapperPass,
                    "convergence-region", "SPIRV convergence regions analysis",
                    true, true)

namespace llvm {
namespace SPIRV {
namespace {

template <typename BasicBlockType, typename IntrinsicInstType>
std::optional<IntrinsicInstType *>
getConvergenceTokenInternal(BasicBlockType *BB) {
  static_assert(std::is_const_v<IntrinsicInstType> ==
                    std::is_const_v<BasicBlockType>,
                "Constness must match between input and output.");
  static_assert(std::is_same_v<BasicBlock, std::remove_const_t<BasicBlockType>>,
                "Input must be a basic block.");
  static_assert(
      std::is_same_v<IntrinsicInst, std::remove_const_t<IntrinsicInstType>>,
      "Output type must be an intrinsic instruction.");

  for (auto &I : *BB) {
    if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::experimental_convergence_entry:
      case Intrinsic::experimental_convergence_loop:
        return II;
      case Intrinsic::experimental_convergence_anchor: {
        auto Bundle = II->getOperandBundle(LLVMContext::OB_convergencectrl);
        assert(Bundle->Inputs.size() == 1 &&
               Bundle->Inputs[0]->getType()->isTokenTy());
        auto TII = dyn_cast<IntrinsicInst>(Bundle->Inputs[0].get());
        assert(TII != nullptr);
        return TII;
      }
      }
    }

    if (auto *CI = dyn_cast<CallInst>(&I)) {
      auto OB = CI->getOperandBundle(LLVMContext::OB_convergencectrl);
      if (!OB.has_value())
        continue;
      return dyn_cast<IntrinsicInst>(OB.value().Inputs[0]);
    }
  }

  return std::nullopt;
}

// Given a ConvergenceRegion tree with |Start| as its root, finds the smallest
// region |Entry| belongs to. If |Entry| does not belong to the region defined
// by |Start|, this function returns |nullptr|.
ConvergenceRegion *findParentRegion(ConvergenceRegion *Start,
                                    BasicBlock *Entry) {
  ConvergenceRegion *Candidate = nullptr;
  ConvergenceRegion *NextCandidate = Start;

  while (Candidate != NextCandidate && NextCandidate != nullptr) {
    Candidate = NextCandidate;
    NextCandidate = nullptr;

    // End of the search, we can return.
    if (Candidate->Children.size() == 0)
      return Candidate;

    for (auto *Child : Candidate->Children) {
      if (Child->Blocks.count(Entry) != 0) {
        NextCandidate = Child;
        break;
      }
    }
  }

  return Candidate;
}

} // anonymous namespace

std::optional<IntrinsicInst *> getConvergenceToken(BasicBlock *BB) {
  return getConvergenceTokenInternal<BasicBlock, IntrinsicInst>(BB);
}

std::optional<const IntrinsicInst *> getConvergenceToken(const BasicBlock *BB) {
  return getConvergenceTokenInternal<const BasicBlock, const IntrinsicInst>(BB);
}

ConvergenceRegion::ConvergenceRegion(DominatorTree &DT, LoopInfo &LI,
                                     Function &F)
    : DT(DT), LI(LI), Parent(nullptr) {
  Entry = &F.getEntryBlock();
  ConvergenceToken = getConvergenceToken(Entry);
  for (auto &B : F) {
    Blocks.insert(&B);
    if (isa<ReturnInst>(B.getTerminator()))
      Exits.insert(&B);
  }
}

ConvergenceRegion::ConvergenceRegion(
    DominatorTree &DT, LoopInfo &LI,
    std::optional<IntrinsicInst *> ConvergenceToken, BasicBlock *Entry,
    SmallPtrSet<BasicBlock *, 8> &&Blocks, SmallPtrSet<BasicBlock *, 2> &&Exits)
    : DT(DT), LI(LI), ConvergenceToken(ConvergenceToken), Entry(Entry),
      Exits(std::move(Exits)), Blocks(std::move(Blocks)) {
  for ([[maybe_unused]] auto *BB : this->Exits)
    assert(this->Blocks.count(BB) != 0);
  assert(this->Blocks.count(this->Entry) != 0);
}

void ConvergenceRegion::releaseMemory() {
  // Parent memory is owned by the parent.
  Parent = nullptr;
  for (auto *Child : Children) {
    Child->releaseMemory();
    delete Child;
  }
  Children.resize(0);
}

void ConvergenceRegion::dump(const unsigned IndentSize) const {
  const std::string Indent(IndentSize, '\t');
  dbgs() << Indent << this << ": {\n";
  dbgs() << Indent << "	Parent: " << Parent << "\n";

  if (ConvergenceToken.value_or(nullptr)) {
    dbgs() << Indent
           << "	ConvergenceToken: " << ConvergenceToken.value()->getName()
           << "\n";
  }

  if (Entry->getName() != "")
    dbgs() << Indent << "	Entry: " << Entry->getName() << "\n";
  else
    dbgs() << Indent << "	Entry: " << Entry << "\n";

  dbgs() << Indent << "	Exits: { ";
  for (const auto &Exit : Exits) {
    if (Exit->getName() != "")
      dbgs() << Exit->getName() << ", ";
    else
      dbgs() << Exit << ", ";
  }
  dbgs() << "	}\n";

  dbgs() << Indent << "	Blocks: { ";
  for (const auto &Block : Blocks) {
    if (Block->getName() != "")
      dbgs() << Block->getName() << ", ";
    else
      dbgs() << Block << ", ";
  }
  dbgs() << "	}\n";

  dbgs() << Indent << "	Children: {\n";
  for (const auto Child : Children)
    Child->dump(IndentSize + 2);
  dbgs() << Indent << "	}\n";

  dbgs() << Indent << "}\n";
}

class ConvergenceRegionAnalyzer {

public:
  ConvergenceRegionAnalyzer(Function &F, DominatorTree &DT, LoopInfo &LI)
      : DT(DT), LI(LI), F(F) {}

private:
  bool isBackEdge(const BasicBlock *From, const BasicBlock *To) const {
    assert(From != To && "From == To. This is awkward.");

    // We only handle loop in the simplified form. This means:
    // - a single back-edge, a single latch.
    // - meaning the back-edge target can only be the loop header.
    // - meaning the From can only be the loop latch.
    if (!LI.isLoopHeader(To))
      return false;

    auto *L = LI.getLoopFor(To);
    if (L->contains(From) && L->isLoopLatch(From))
      return true;

    return false;
  }

  std::unordered_set<BasicBlock *>
  findPathsToMatch(LoopInfo &LI, BasicBlock *From,
                   std::function<bool(const BasicBlock *)> isMatch) const {
    std::unordered_set<BasicBlock *> Output;

    if (isMatch(From))
      Output.insert(From);

    auto *Terminator = From->getTerminator();
    for (unsigned i = 0; i < Terminator->getNumSuccessors(); ++i) {
      auto *To = Terminator->getSuccessor(i);
      if (isBackEdge(From, To))
        continue;

      auto ChildSet = findPathsToMatch(LI, To, isMatch);
      if (ChildSet.size() == 0)
        continue;

      Output.insert(ChildSet.begin(), ChildSet.end());
      Output.insert(From);
      if (LI.isLoopHeader(From)) {
        auto *L = LI.getLoopFor(From);
        for (auto *BB : L->getBlocks()) {
          Output.insert(BB);
        }
      }
    }

    return Output;
  }

  SmallPtrSet<BasicBlock *, 2>
  findExitNodes(const SmallPtrSetImpl<BasicBlock *> &RegionBlocks) {
    SmallPtrSet<BasicBlock *, 2> Exits;

    for (auto *B : RegionBlocks) {
      auto *Terminator = B->getTerminator();
      for (unsigned i = 0; i < Terminator->getNumSuccessors(); ++i) {
        auto *Child = Terminator->getSuccessor(i);
        if (RegionBlocks.count(Child) == 0)
          Exits.insert(B);
      }
    }

    return Exits;
  }

public:
  ConvergenceRegionInfo analyze() {
    ConvergenceRegion *TopLevelRegion = new ConvergenceRegion(DT, LI, F);
    std::queue<Loop *> ToProcess;
    for (auto *L : LI.getLoopsInPreorder())
      ToProcess.push(L);

    while (ToProcess.size() != 0) {
      auto *L = ToProcess.front();
      ToProcess.pop();
      assert(L->isLoopSimplifyForm());

      auto CT = getConvergenceToken(L->getHeader());
      SmallPtrSet<BasicBlock *, 8> RegionBlocks(L->block_begin(),
                                                L->block_end());
      SmallVector<BasicBlock *> LoopExits;
      L->getExitingBlocks(LoopExits);
      if (CT.has_value()) {
        for (auto *Exit : LoopExits) {
          auto N = findPathsToMatch(LI, Exit, [&CT](const BasicBlock *block) {
            auto Token = getConvergenceToken(block);
            if (Token == std::nullopt)
              return false;
            return Token.value() == CT.value();
          });
          RegionBlocks.insert(N.begin(), N.end());
        }
      }

      auto RegionExits = findExitNodes(RegionBlocks);
      ConvergenceRegion *Region = new ConvergenceRegion(
          DT, LI, CT, L->getHeader(), std::move(RegionBlocks),
          std::move(RegionExits));
      Region->Parent = findParentRegion(TopLevelRegion, Region->Entry);
      assert(Region->Parent != nullptr && "This is impossible.");
      Region->Parent->Children.push_back(Region);
    }

    return ConvergenceRegionInfo(TopLevelRegion);
  }

private:
  DominatorTree &DT;
  LoopInfo &LI;
  Function &F;
};

ConvergenceRegionInfo getConvergenceRegions(Function &F, DominatorTree &DT,
                                            LoopInfo &LI) {
  ConvergenceRegionAnalyzer Analyzer(F, DT, LI);
  return Analyzer.analyze();
}

} // namespace SPIRV

char SPIRVConvergenceRegionAnalysisWrapperPass::ID = 0;

SPIRVConvergenceRegionAnalysisWrapperPass::
    SPIRVConvergenceRegionAnalysisWrapperPass()
    : FunctionPass(ID) {}

bool SPIRVConvergenceRegionAnalysisWrapperPass::runOnFunction(Function &F) {
  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

  CRI = SPIRV::getConvergenceRegions(F, DT, LI);
  // Nothing was modified.
  return false;
}

SPIRVConvergenceRegionAnalysis::Result
SPIRVConvergenceRegionAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
  Result CRI;
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  CRI = SPIRV::getConvergenceRegions(F, DT, LI);
  return CRI;
}

AnalysisKey SPIRVConvergenceRegionAnalysis::Key;

} // namespace llvm
