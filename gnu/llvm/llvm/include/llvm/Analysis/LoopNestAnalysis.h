//===- llvm/Analysis/LoopNestAnalysis.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interface for the loop nest analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPNESTANALYSIS_H
#define LLVM_ANALYSIS_LOOPNESTANALYSIS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"

namespace llvm {

using LoopVectorTy = SmallVector<Loop *, 8>;

class LPMUpdater;

/// This class represents a loop nest and can be used to query its properties.
class LLVM_EXTERNAL_VISIBILITY LoopNest {
public:
  using InstrVectorTy = SmallVector<const Instruction *>;

  /// Construct a loop nest rooted by loop \p Root.
  LoopNest(Loop &Root, ScalarEvolution &SE);

  LoopNest() = delete;

  /// Construct a LoopNest object.
  static std::unique_ptr<LoopNest> getLoopNest(Loop &Root, ScalarEvolution &SE);

  /// Return true if the given loops \p OuterLoop and \p InnerLoop are
  /// perfectly nested with respect to each other, and false otherwise.
  /// Example:
  /// \code
  ///   for(i)
  ///     for(j)
  ///       for(k)
  /// \endcode
  /// arePerfectlyNested(loop_i, loop_j, SE) would return true.
  /// arePerfectlyNested(loop_j, loop_k, SE) would return true.
  /// arePerfectlyNested(loop_i, loop_k, SE) would return false.
  static bool arePerfectlyNested(const Loop &OuterLoop, const Loop &InnerLoop,
                                 ScalarEvolution &SE);

  /// Return a vector of instructions that prevent the LoopNest given
  /// by loops \p OuterLoop and \p InnerLoop from being perfect.
  static InstrVectorTy getInterveningInstructions(const Loop &OuterLoop,
                                                  const Loop &InnerLoop,
                                                  ScalarEvolution &SE);

  /// Return the maximum nesting depth of the loop nest rooted by loop \p Root.
  /// For example given the loop nest:
  /// \code
  ///   for(i)     // loop at level 1 and Root of the nest
  ///     for(j)   // loop at level 2
  ///       <code>
  ///       for(k) // loop at level 3
  /// \endcode
  /// getMaxPerfectDepth(Loop_i) would return 2.
  static unsigned getMaxPerfectDepth(const Loop &Root, ScalarEvolution &SE);

  /// Recursivelly traverse all empty 'single successor' basic blocks of \p From
  /// (if there are any). When \p CheckUniquePred is set to true, check if
  /// each of the empty single successors has a unique predecessor. Return
  /// the last basic block found or \p End if it was reached during the search.
  static const BasicBlock &skipEmptyBlockUntil(const BasicBlock *From,
                                               const BasicBlock *End,
                                               bool CheckUniquePred = false);

  /// Return the outermost loop in the loop nest.
  Loop &getOutermostLoop() const { return *Loops.front(); }

  /// Return the innermost loop in the loop nest if the nest has only one
  /// innermost loop, and a nullptr otherwise.
  /// Note: the innermost loop returned is not necessarily perfectly nested.
  Loop *getInnermostLoop() const {
    if (Loops.size() == 1)
      return Loops.back();

    // The loops in the 'Loops' vector have been collected in breadth first
    // order, therefore if the last 2 loops in it have the same nesting depth
    // there isn't a unique innermost loop in the nest.
    Loop *LastLoop = Loops.back();
    auto SecondLastLoopIter = ++Loops.rbegin();
    return (LastLoop->getLoopDepth() == (*SecondLastLoopIter)->getLoopDepth())
               ? nullptr
               : LastLoop;
  }

  /// Return the loop at the given \p Index.
  Loop *getLoop(unsigned Index) const {
    assert(Index < Loops.size() && "Index is out of bounds");
    return Loops[Index];
  }

  /// Get the loop index of the given loop \p L.
  unsigned getLoopIndex(const Loop &L) const {
    for (unsigned I = 0; I < getNumLoops(); ++I)
      if (getLoop(I) == &L)
        return I;
    llvm_unreachable("Loop not in the loop nest");
  }

  /// Return the number of loops in the nest.
  size_t getNumLoops() const { return Loops.size(); }

  /// Get the loops in the nest.
  ArrayRef<Loop *> getLoops() const { return Loops; }

  /// Get the loops in the nest at the given \p Depth.
  LoopVectorTy getLoopsAtDepth(unsigned Depth) const {
    assert(Depth >= Loops.front()->getLoopDepth() &&
           Depth <= Loops.back()->getLoopDepth() && "Invalid depth");
    LoopVectorTy Result;
    for (unsigned I = 0; I < getNumLoops(); ++I) {
      Loop *L = getLoop(I);
      if (L->getLoopDepth() == Depth)
        Result.push_back(L);
      else if (L->getLoopDepth() > Depth)
        break;
    }
    return Result;
  }

  /// Retrieve a vector of perfect loop nests contained in the current loop
  /// nest. For example, given the following  nest containing 4 loops, this
  /// member function would return {{L1,L2},{L3,L4}}.
  /// \code
  ///   for(i) // L1
  ///     for(j) // L2
  ///       <code>
  ///       for(k) // L3
  ///         for(l) // L4
  /// \endcode
  SmallVector<LoopVectorTy, 4> getPerfectLoops(ScalarEvolution &SE) const;

  /// Return the loop nest depth (i.e. the loop depth of the 'deepest' loop)
  /// For example given the loop nest:
  /// \code
  ///   for(i)      // loop at level 1 and Root of the nest
  ///     for(j1)   // loop at level 2
  ///       for(k)  // loop at level 3
  ///     for(j2)   // loop at level 2
  /// \endcode
  /// getNestDepth() would return 3.
  unsigned getNestDepth() const {
    int NestDepth =
        Loops.back()->getLoopDepth() - Loops.front()->getLoopDepth() + 1;
    assert(NestDepth > 0 && "Expecting NestDepth to be at least 1");
    return NestDepth;
  }

  /// Return the maximum perfect nesting depth.
  unsigned getMaxPerfectDepth() const { return MaxPerfectDepth; }

  /// Return true if all loops in the loop nest are in simplify form.
  bool areAllLoopsSimplifyForm() const {
    return all_of(Loops, [](const Loop *L) { return L->isLoopSimplifyForm(); });
  }

  /// Return true if all loops in the loop nest are in rotated form.
  bool areAllLoopsRotatedForm() const {
    return all_of(Loops, [](const Loop *L) { return L->isRotatedForm(); });
  }

  /// Return the function to which the loop-nest belongs.
  Function *getParent() const {
    return Loops.front()->getHeader()->getParent();
  }

  StringRef getName() const { return Loops.front()->getName(); }

protected:
  const unsigned MaxPerfectDepth; // maximum perfect nesting depth level.
  LoopVectorTy Loops; // the loops in the nest (in breadth first order).

private:
  enum LoopNestEnum {
    PerfectLoopNest,
    ImperfectLoopNest,
    InvalidLoopStructure,
    OuterLoopLowerBoundUnknown
  };
  static LoopNestEnum analyzeLoopNestForPerfectNest(const Loop &OuterLoop,
                                                    const Loop &InnerLoop,
                                                    ScalarEvolution &SE);
};

raw_ostream &operator<<(raw_ostream &, const LoopNest &);

/// This analysis provides information for a loop nest. The analysis runs on
/// demand and can be initiated via AM.getResult<LoopNestAnalysis>.
class LoopNestAnalysis : public AnalysisInfoMixin<LoopNestAnalysis> {
  friend AnalysisInfoMixin<LoopNestAnalysis>;
  static AnalysisKey Key;

public:
  using Result = LoopNest;
  Result run(Loop &L, LoopAnalysisManager &AM, LoopStandardAnalysisResults &AR);
};

/// Printer pass for the \c LoopNest results.
class LoopNestPrinterPass : public PassInfoMixin<LoopNestPrinterPass> {
  raw_ostream &OS;

public:
  explicit LoopNestPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_ANALYSIS_LOOPNESTANALYSIS_H
