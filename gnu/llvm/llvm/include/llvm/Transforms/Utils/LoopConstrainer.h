//===- LoopConstrainer.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOOP_CONSTRAINER_H
#define LLVM_TRANSFORMS_UTILS_LOOP_CONSTRAINER_H

#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <optional>

namespace llvm {

class BasicBlock;
class BranchInst;
class DominatorTree;
class IntegerType;
class Loop;
class LoopInfo;
class PHINode;
class ScalarEvolution;
class SCEV;
class Value;

// Keeps track of the structure of a loop.  This is similar to llvm::Loop,
// except that it is more lightweight and can track the state of a loop through
// changing and potentially invalid IR.  This structure also formalizes the
// kinds of loops we can deal with -- ones that have a single latch that is also
// an exiting block *and* have a canonical induction variable.
struct LoopStructure {
  const char *Tag = "";

  BasicBlock *Header = nullptr;
  BasicBlock *Latch = nullptr;

  // `Latch's terminator instruction is `LatchBr', and it's `LatchBrExitIdx'th
  // successor is `LatchExit', the exit block of the loop.
  BranchInst *LatchBr = nullptr;
  BasicBlock *LatchExit = nullptr;
  unsigned LatchBrExitIdx = std::numeric_limits<unsigned>::max();

  // The loop represented by this instance of LoopStructure is semantically
  // equivalent to:
  //
  // intN_ty inc = IndVarIncreasing ? 1 : -1;
  // pred_ty predicate = IndVarIncreasing ? ICMP_SLT : ICMP_SGT;
  //
  // for (intN_ty iv = IndVarStart; predicate(iv, LoopExitAt); iv = IndVarBase)
  //   ... body ...

  Value *IndVarBase = nullptr;
  Value *IndVarStart = nullptr;
  Value *IndVarStep = nullptr;
  Value *LoopExitAt = nullptr;
  bool IndVarIncreasing = false;
  bool IsSignedPredicate = true;
  IntegerType *ExitCountTy = nullptr;

  LoopStructure() = default;

  template <typename M> LoopStructure map(M Map) const {
    LoopStructure Result;
    Result.Tag = Tag;
    Result.Header = cast<BasicBlock>(Map(Header));
    Result.Latch = cast<BasicBlock>(Map(Latch));
    Result.LatchBr = cast<BranchInst>(Map(LatchBr));
    Result.LatchExit = cast<BasicBlock>(Map(LatchExit));
    Result.LatchBrExitIdx = LatchBrExitIdx;
    Result.IndVarBase = Map(IndVarBase);
    Result.IndVarStart = Map(IndVarStart);
    Result.IndVarStep = Map(IndVarStep);
    Result.LoopExitAt = Map(LoopExitAt);
    Result.IndVarIncreasing = IndVarIncreasing;
    Result.IsSignedPredicate = IsSignedPredicate;
    Result.ExitCountTy = ExitCountTy;
    return Result;
  }

  static std::optional<LoopStructure>
  parseLoopStructure(ScalarEvolution &, Loop &, bool, const char *&);
};

/// This class is used to constrain loops to run within a given iteration space.
/// The algorithm this class implements is given a Loop and a range [Begin,
/// End).  The algorithm then tries to break out a "main loop" out of the loop
/// it is given in a way that the "main loop" runs with the induction variable
/// in a subset of [Begin, End).  The algorithm emits appropriate pre and post
/// loops to run any remaining iterations.  The pre loop runs any iterations in
/// which the induction variable is < Begin, and the post loop runs any
/// iterations in which the induction variable is >= End.
class LoopConstrainer {
public:
  // Calculated subranges we restrict the iteration space of the main loop to.
  // See the implementation of `calculateSubRanges' for more details on how
  // these fields are computed.  `LowLimit` is std::nullopt if there is no
  // restriction on low end of the restricted iteration space of the main loop.
  // `HighLimit` is std::nullopt if there is no restriction on high end of the
  // restricted iteration space of the main loop.

  struct SubRanges {
    std::optional<const SCEV *> LowLimit;
    std::optional<const SCEV *> HighLimit;
  };

private:
  // The representation of a clone of the original loop we started out with.
  struct ClonedLoop {
    // The cloned blocks
    std::vector<BasicBlock *> Blocks;

    // `Map` maps values in the clonee into values in the cloned version
    ValueToValueMapTy Map;

    // An instance of `LoopStructure` for the cloned loop
    LoopStructure Structure;
  };

  // Result of rewriting the range of a loop.  See changeIterationSpaceEnd for
  // more details on what these fields mean.
  struct RewrittenRangeInfo {
    BasicBlock *PseudoExit = nullptr;
    BasicBlock *ExitSelector = nullptr;
    std::vector<PHINode *> PHIValuesAtPseudoExit;
    PHINode *IndVarEnd = nullptr;

    RewrittenRangeInfo() = default;
  };

  // Clone `OriginalLoop' and return the result in CLResult.  The IR after
  // running `cloneLoop' is well formed except for the PHI nodes in CLResult --
  // the PHI nodes say that there is an incoming edge from `OriginalPreheader`
  // but there is no such edge.
  void cloneLoop(ClonedLoop &CLResult, const char *Tag) const;

  // Create the appropriate loop structure needed to describe a cloned copy of
  // `Original`.  The clone is described by `VM`.
  Loop *createClonedLoopStructure(Loop *Original, Loop *Parent,
                                  ValueToValueMapTy &VM, bool IsSubloop);

  // Rewrite the iteration space of the loop denoted by (LS, Preheader). The
  // iteration space of the rewritten loop ends at ExitLoopAt.  The start of the
  // iteration space is not changed.  `ExitLoopAt' is assumed to be slt
  // `OriginalHeaderCount'.
  //
  // If there are iterations left to execute, control is made to jump to
  // `ContinuationBlock', otherwise they take the normal loop exit.  The
  // returned `RewrittenRangeInfo' object is populated as follows:
  //
  //  .PseudoExit is a basic block that unconditionally branches to
  //      `ContinuationBlock'.
  //
  //  .ExitSelector is a basic block that decides, on exit from the loop,
  //      whether to branch to the "true" exit or to `PseudoExit'.
  //
  //  .PHIValuesAtPseudoExit are PHINodes in `PseudoExit' that compute the value
  //      for each PHINode in the loop header on taking the pseudo exit.
  //
  // After changeIterationSpaceEnd, `Preheader' is no longer a legitimate
  // preheader because it is made to branch to the loop header only
  // conditionally.
  RewrittenRangeInfo
  changeIterationSpaceEnd(const LoopStructure &LS, BasicBlock *Preheader,
                          Value *ExitLoopAt,
                          BasicBlock *ContinuationBlock) const;

  // The loop denoted by `LS' has `OldPreheader' as its preheader.  This
  // function creates a new preheader for `LS' and returns it.
  BasicBlock *createPreheader(const LoopStructure &LS, BasicBlock *OldPreheader,
                              const char *Tag) const;

  // `ContinuationBlockAndPreheader' was the continuation block for some call to
  // `changeIterationSpaceEnd' and is the preheader to the loop denoted by `LS'.
  // This function rewrites the PHI nodes in `LS.Header' to start with the
  // correct value.
  void rewriteIncomingValuesForPHIs(
      LoopStructure &LS, BasicBlock *ContinuationBlockAndPreheader,
      const LoopConstrainer::RewrittenRangeInfo &RRI) const;

  // Even though we do not preserve any passes at this time, we at least need to
  // keep the parent loop structure consistent.  The `LPPassManager' seems to
  // verify this after running a loop pass.  This function adds the list of
  // blocks denoted by BBs to this loops parent loop if required.
  void addToParentLoopIfNeeded(ArrayRef<BasicBlock *> BBs);

  // Some global state.
  Function &F;
  LLVMContext &Ctx;
  ScalarEvolution &SE;
  DominatorTree &DT;
  LoopInfo &LI;
  function_ref<void(Loop *, bool)> LPMAddNewLoop;

  // Information about the original loop we started out with.
  Loop &OriginalLoop;

  BasicBlock *OriginalPreheader = nullptr;

  // The preheader of the main loop.  This may or may not be different from
  // `OriginalPreheader'.
  BasicBlock *MainLoopPreheader = nullptr;

  // Type of the range we need to run the main loop in.
  Type *RangeTy;

  // The structure of the main loop (see comment at the beginning of this class
  // for a definition)
  LoopStructure MainLoopStructure;

  SubRanges SR;

public:
  LoopConstrainer(Loop &L, LoopInfo &LI,
                  function_ref<void(Loop *, bool)> LPMAddNewLoop,
                  const LoopStructure &LS, ScalarEvolution &SE,
                  DominatorTree &DT, Type *T, SubRanges SR);

  // Entry point for the algorithm.  Returns true on success.
  bool run();
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOOP_CONSTRAINER_H
