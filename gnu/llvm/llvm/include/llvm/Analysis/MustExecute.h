//===- MustExecute.h - Is an instruction known to execute--------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Contains a collection of routines for determining if a given instruction is
/// guaranteed to execute if a given point in control flow is reached. The most
/// common example is an instruction within a loop being provably executed if we
/// branch to the header of it's containing loop.
///
/// There are two interfaces available to determine if an instruction is
/// executed once a given point in the control flow is reached:
/// 1) A loop-centric one derived from LoopSafetyInfo.
/// 2) A "must be executed context"-based one implemented in the
///    MustBeExecutedContextExplorer.
/// Please refer to the class comments for more information.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MUSTEXECUTE_H
#define LLVM_ANALYSIS_MUSTEXECUTE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/InstructionPrecedenceTracking.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

namespace {
template <typename T> using GetterTy = std::function<T *(const Function &F)>;
}

class BasicBlock;
class DominatorTree;
class Instruction;
class Loop;
class LoopInfo;
class PostDominatorTree;
class raw_ostream;

/// Captures loop safety information.
/// It keep information for loop blocks may throw exception or otherwise
/// exit abnormally on any iteration of the loop which might actually execute
/// at runtime.  The primary way to consume this information is via
/// isGuaranteedToExecute below, but some callers bailout or fallback to
/// alternate reasoning if a loop contains any implicit control flow.
/// NOTE: LoopSafetyInfo contains cached information regarding loops and their
/// particular blocks. This information is only dropped on invocation of
/// computeLoopSafetyInfo. If the loop or any of its block is deleted, or if
/// any thrower instructions have been added or removed from them, or if the
/// control flow has changed, or in case of other meaningful modifications, the
/// LoopSafetyInfo needs to be recomputed. If a meaningful modifications to the
/// loop were made and the info wasn't recomputed properly, the behavior of all
/// methods except for computeLoopSafetyInfo is undefined.
class LoopSafetyInfo {
  // Used to update funclet bundle operands.
  DenseMap<BasicBlock *, ColorVector> BlockColors;

protected:
  /// Computes block colors.
  void computeBlockColors(const Loop *CurLoop);

public:
  /// Returns block colors map that is used to update funclet operand bundles.
  const DenseMap<BasicBlock *, ColorVector> &getBlockColors() const;

  /// Copy colors of block \p Old into the block \p New.
  void copyColors(BasicBlock *New, BasicBlock *Old);

  /// Returns true iff the block \p BB potentially may throw exception. It can
  /// be false-positive in cases when we want to avoid complex analysis.
  virtual bool blockMayThrow(const BasicBlock *BB) const = 0;

  /// Returns true iff any block of the loop for which this info is contains an
  /// instruction that may throw or otherwise exit abnormally.
  virtual bool anyBlockMayThrow() const = 0;

  /// Return true if we must reach the block \p BB under assumption that the
  /// loop \p CurLoop is entered.
  bool allLoopPathsLeadToBlock(const Loop *CurLoop, const BasicBlock *BB,
                               const DominatorTree *DT) const;

  /// Computes safety information for a loop checks loop body & header for
  /// the possibility of may throw exception, it takes LoopSafetyInfo and loop
  /// as argument. Updates safety information in LoopSafetyInfo argument.
  /// Note: This is defined to clear and reinitialize an already initialized
  /// LoopSafetyInfo.  Some callers rely on this fact.
  virtual void computeLoopSafetyInfo(const Loop *CurLoop) = 0;

  /// Returns true if the instruction in a loop is guaranteed to execute at
  /// least once (under the assumption that the loop is entered).
  virtual bool isGuaranteedToExecute(const Instruction &Inst,
                                     const DominatorTree *DT,
                                     const Loop *CurLoop) const = 0;

  LoopSafetyInfo() = default;

  virtual ~LoopSafetyInfo() = default;
};


/// Simple and conservative implementation of LoopSafetyInfo that can give
/// false-positive answers to its queries in order to avoid complicated
/// analysis.
class SimpleLoopSafetyInfo: public LoopSafetyInfo {
  bool MayThrow = false;       // The current loop contains an instruction which
                               // may throw.
  bool HeaderMayThrow = false; // Same as previous, but specific to loop header

public:
  bool blockMayThrow(const BasicBlock *BB) const override;

  bool anyBlockMayThrow() const override;

  void computeLoopSafetyInfo(const Loop *CurLoop) override;

  bool isGuaranteedToExecute(const Instruction &Inst,
                             const DominatorTree *DT,
                             const Loop *CurLoop) const override;
};

/// This implementation of LoopSafetyInfo use ImplicitControlFlowTracking to
/// give precise answers on "may throw" queries. This implementation uses cache
/// that should be invalidated by calling the methods insertInstructionTo and
/// removeInstruction whenever we modify a basic block's contents by adding or
/// removing instructions.
class ICFLoopSafetyInfo: public LoopSafetyInfo {
  bool MayThrow = false;       // The current loop contains an instruction which
                               // may throw.
  // Contains information about implicit control flow in this loop's blocks.
  mutable ImplicitControlFlowTracking ICF;
  // Contains information about instruction that may possibly write memory.
  mutable MemoryWriteTracking MW;

public:
  bool blockMayThrow(const BasicBlock *BB) const override;

  bool anyBlockMayThrow() const override;

  void computeLoopSafetyInfo(const Loop *CurLoop) override;

  bool isGuaranteedToExecute(const Instruction &Inst,
                             const DominatorTree *DT,
                             const Loop *CurLoop) const override;

  /// Returns true if we could not execute a memory-modifying instruction before
  /// we enter \p BB under assumption that \p CurLoop is entered.
  bool doesNotWriteMemoryBefore(const BasicBlock *BB, const Loop *CurLoop)
      const;

  /// Returns true if we could not execute a memory-modifying instruction before
  /// we execute \p I under assumption that \p CurLoop is entered.
  bool doesNotWriteMemoryBefore(const Instruction &I, const Loop *CurLoop)
      const;

  /// Inform the safety info that we are planning to insert a new instruction
  /// \p Inst into the basic block \p BB. It will make all cache updates to keep
  /// it correct after this insertion.
  void insertInstructionTo(const Instruction *Inst, const BasicBlock *BB);

  /// Inform safety info that we are planning to remove the instruction \p Inst
  /// from its block. It will make all cache updates to keep it correct after
  /// this removal.
  void removeInstruction(const Instruction *Inst);
};

bool mayContainIrreducibleControl(const Function &F, const LoopInfo *LI);

struct MustBeExecutedContextExplorer;

/// Enum that allows us to spell out the direction.
enum class ExplorationDirection {
  BACKWARD = 0,
  FORWARD = 1,
};

/// Must be executed iterators visit stretches of instructions that are
/// guaranteed to be executed together, potentially with other instruction
/// executed in-between.
///
/// Given the following code, and assuming all statements are single
/// instructions which transfer execution to the successor (see
/// isGuaranteedToTransferExecutionToSuccessor), there are two possible
/// outcomes. If we start the iterator at A, B, or E, we will visit only A, B,
/// and E. If we start at C or D, we will visit all instructions A-E.
///
/// \code
///   A;
///   B;
///   if (...) {
///     C;
///     D;
///   }
///   E;
/// \endcode
///
///
/// Below is the example extneded with instructions F and G. Now we assume F
/// might not transfer execution to it's successor G. As a result we get the
/// following visit sets:
///
/// Start Instruction   | Visit Set
/// A                   | A, B,       E, F
///    B                | A, B,       E, F
///       C             | A, B, C, D, E, F
///          D          | A, B, C, D, E, F
///             E       | A, B,       E, F
///                F    | A, B,       E, F
///                   G | A, B,       E, F, G
///
///
/// \code
///   A;
///   B;
///   if (...) {
///     C;
///     D;
///   }
///   E;
///   F;  // Might not transfer execution to its successor G.
///   G;
/// \endcode
///
///
/// A more complex example involving conditionals, loops, break, and continue
/// is shown below. We again assume all instructions will transmit control to
/// the successor and we assume we can prove the inner loop to be finite. We
/// omit non-trivial branch conditions as the exploration is oblivious to them.
/// Constant branches are assumed to be unconditional in the CFG. The resulting
/// visist sets are shown in the table below.
///
/// \code
///   A;
///   while (true) {
///     B;
///     if (...)
///       C;
///     if (...)
///       continue;
///     D;
///     if (...)
///       break;
///     do {
///       if (...)
///         continue;
///       E;
///     } while (...);
///     F;
///   }
///   G;
/// \endcode
///
/// Start Instruction    | Visit Set
/// A                    | A, B
///    B                 | A, B
///       C              | A, B, C
///          D           | A, B,    D
///             E        | A, B,    D, E, F
///                F     | A, B,    D,    F
///                   G  | A, B,    D,       G
///
///
/// Note that the examples show optimal visist sets but not necessarily the ones
/// derived by the explorer depending on the available CFG analyses (see
/// MustBeExecutedContextExplorer). Also note that we, depending on the options,
/// the visit set can contain instructions from other functions.
struct MustBeExecutedIterator {
  /// Type declarations that make his class an input iterator.
  ///{
  typedef const Instruction *value_type;
  typedef std::ptrdiff_t difference_type;
  typedef const Instruction **pointer;
  typedef const Instruction *&reference;
  typedef std::input_iterator_tag iterator_category;
  ///}

  using ExplorerTy = MustBeExecutedContextExplorer;

  MustBeExecutedIterator(const MustBeExecutedIterator &Other) = default;

  MustBeExecutedIterator(MustBeExecutedIterator &&Other)
      : Visited(std::move(Other.Visited)), Explorer(Other.Explorer),
        CurInst(Other.CurInst), Head(Other.Head), Tail(Other.Tail) {}

  MustBeExecutedIterator &operator=(MustBeExecutedIterator &&Other) {
    if (this != &Other) {
      std::swap(Visited, Other.Visited);
      std::swap(CurInst, Other.CurInst);
      std::swap(Head, Other.Head);
      std::swap(Tail, Other.Tail);
    }
    return *this;
  }

  ~MustBeExecutedIterator() = default;

  /// Pre- and post-increment operators.
  ///{
  MustBeExecutedIterator &operator++() {
    CurInst = advance();
    return *this;
  }

  MustBeExecutedIterator operator++(int) {
    MustBeExecutedIterator tmp(*this);
    operator++();
    return tmp;
  }
  ///}

  /// Equality and inequality operators. Note that we ignore the history here.
  ///{
  bool operator==(const MustBeExecutedIterator &Other) const {
    return CurInst == Other.CurInst && Head == Other.Head && Tail == Other.Tail;
  }

  bool operator!=(const MustBeExecutedIterator &Other) const {
    return !(*this == Other);
  }
  ///}

  /// Return the underlying instruction.
  const Instruction *&operator*() { return CurInst; }
  const Instruction *getCurrentInst() const { return CurInst; }

  /// Return true if \p I was encountered by this iterator already.
  bool count(const Instruction *I) const {
    return Visited.count({I, ExplorationDirection::FORWARD}) ||
           Visited.count({I, ExplorationDirection::BACKWARD});
  }

private:
  using VisitedSetTy =
      DenseSet<PointerIntPair<const Instruction *, 1, ExplorationDirection>>;

  /// Private constructors.
  MustBeExecutedIterator(ExplorerTy &Explorer, const Instruction *I);

  /// Reset the iterator to its initial state pointing at \p I.
  void reset(const Instruction *I);

  /// Reset the iterator to point at \p I, keep cached state.
  void resetInstruction(const Instruction *I);

  /// Try to advance one of the underlying positions (Head or Tail).
  ///
  /// \return The next instruction in the must be executed context, or nullptr
  ///         if none was found.
  const Instruction *advance();

  /// A set to track the visited instructions in order to deal with endless
  /// loops and recursion.
  VisitedSetTy Visited;

  /// A reference to the explorer that created this iterator.
  ExplorerTy &Explorer;

  /// The instruction we are currently exposing to the user. There is always an
  /// instruction that we know is executed with the given program point,
  /// initially the program point itself.
  const Instruction *CurInst;

  /// Two positions that mark the program points where this iterator will look
  /// for the next instruction. Note that the current instruction is either the
  /// one pointed to by Head, Tail, or both.
  const Instruction *Head, *Tail;

  friend struct MustBeExecutedContextExplorer;
};

/// A "must be executed context" for a given program point PP is the set of
/// instructions, potentially before and after PP, that are executed always when
/// PP is reached. The MustBeExecutedContextExplorer an interface to explore
/// "must be executed contexts" in a module through the use of
/// MustBeExecutedIterator.
///
/// The explorer exposes "must be executed iterators" that traverse the must be
/// executed context. There is little information sharing between iterators as
/// the expected use case involves few iterators for "far apart" instructions.
/// If that changes, we should consider caching more intermediate results.
struct MustBeExecutedContextExplorer {

  /// In the description of the parameters we use PP to denote a program point
  /// for which the must be executed context is explored, or put differently,
  /// for which the MustBeExecutedIterator is created.
  ///
  /// \param ExploreInterBlock    Flag to indicate if instructions in blocks
  ///                             other than the parent of PP should be
  ///                             explored.
  /// \param ExploreCFGForward    Flag to indicate if instructions located after
  ///                             PP in the CFG, e.g., post-dominating PP,
  ///                             should be explored.
  /// \param ExploreCFGBackward   Flag to indicate if instructions located
  ///                             before PP in the CFG, e.g., dominating PP,
  ///                             should be explored.
  MustBeExecutedContextExplorer(
      bool ExploreInterBlock, bool ExploreCFGForward, bool ExploreCFGBackward,
      GetterTy<const LoopInfo> LIGetter =
          [](const Function &) { return nullptr; },
      GetterTy<const DominatorTree> DTGetter =
          [](const Function &) { return nullptr; },
      GetterTy<const PostDominatorTree> PDTGetter =
          [](const Function &) { return nullptr; })
      : ExploreInterBlock(ExploreInterBlock),
        ExploreCFGForward(ExploreCFGForward),
        ExploreCFGBackward(ExploreCFGBackward), LIGetter(LIGetter),
        DTGetter(DTGetter), PDTGetter(PDTGetter), EndIterator(*this, nullptr) {}

  /// Iterator-based interface. \see MustBeExecutedIterator.
  ///{
  using iterator = MustBeExecutedIterator;
  using const_iterator = const MustBeExecutedIterator;

  /// Return an iterator to explore the context around \p PP.
  iterator &begin(const Instruction *PP) {
    auto &It = InstructionIteratorMap[PP];
    if (!It)
      It.reset(new iterator(*this, PP));
    return *It;
  }

  /// Return an iterator to explore the cached context around \p PP.
  const_iterator &begin(const Instruction *PP) const {
    return *InstructionIteratorMap.find(PP)->second;
  }

  /// Return an universal end iterator.
  ///{
  iterator &end() { return EndIterator; }
  iterator &end(const Instruction *) { return EndIterator; }

  const_iterator &end() const { return EndIterator; }
  const_iterator &end(const Instruction *) const { return EndIterator; }
  ///}

  /// Return an iterator range to explore the context around \p PP.
  llvm::iterator_range<iterator> range(const Instruction *PP) {
    return llvm::make_range(begin(PP), end(PP));
  }

  /// Return an iterator range to explore the cached context around \p PP.
  llvm::iterator_range<const_iterator> range(const Instruction *PP) const {
    return llvm::make_range(begin(PP), end(PP));
  }
  ///}

  /// Check \p Pred on all instructions in the context.
  ///
  /// This method will evaluate \p Pred and return
  /// true if \p Pred holds in every instruction.
  bool checkForAllContext(const Instruction *PP,
                          function_ref<bool(const Instruction *)> Pred) {
    for (auto EIt = begin(PP), EEnd = end(PP); EIt != EEnd; ++EIt)
      if (!Pred(*EIt))
        return false;
    return true;
  }

  /// Helper to look for \p I in the context of \p PP.
  ///
  /// The context is expanded until \p I was found or no more expansion is
  /// possible.
  ///
  /// \returns True, iff \p I was found.
  bool findInContextOf(const Instruction *I, const Instruction *PP) {
    auto EIt = begin(PP), EEnd = end(PP);
    return findInContextOf(I, EIt, EEnd);
  }

  /// Helper to look for \p I in the context defined by \p EIt and \p EEnd.
  ///
  /// The context is expanded until \p I was found or no more expansion is
  /// possible.
  ///
  /// \returns True, iff \p I was found.
  bool findInContextOf(const Instruction *I, iterator &EIt, iterator &EEnd) {
    bool Found = EIt.count(I);
    while (!Found && EIt != EEnd)
      Found = (++EIt).getCurrentInst() == I;
    return Found;
  }

  /// Return the next instruction that is guaranteed to be executed after \p PP.
  ///
  /// \param It              The iterator that is used to traverse the must be
  ///                        executed context.
  /// \param PP              The program point for which the next instruction
  ///                        that is guaranteed to execute is determined.
  const Instruction *
  getMustBeExecutedNextInstruction(MustBeExecutedIterator &It,
                                   const Instruction *PP);
  /// Return the previous instr. that is guaranteed to be executed before \p PP.
  ///
  /// \param It              The iterator that is used to traverse the must be
  ///                        executed context.
  /// \param PP              The program point for which the previous instr.
  ///                        that is guaranteed to execute is determined.
  const Instruction *
  getMustBeExecutedPrevInstruction(MustBeExecutedIterator &It,
                                   const Instruction *PP);

  /// Find the next join point from \p InitBB in forward direction.
  const BasicBlock *findForwardJoinPoint(const BasicBlock *InitBB);

  /// Find the next join point from \p InitBB in backward direction.
  const BasicBlock *findBackwardJoinPoint(const BasicBlock *InitBB);

  /// Parameter that limit the performed exploration. See the constructor for
  /// their meaning.
  ///{
  const bool ExploreInterBlock;
  const bool ExploreCFGForward;
  const bool ExploreCFGBackward;
  ///}

private:
  /// Getters for common CFG analyses: LoopInfo, DominatorTree, and
  /// PostDominatorTree.
  ///{
  GetterTy<const LoopInfo> LIGetter;
  GetterTy<const DominatorTree> DTGetter;
  GetterTy<const PostDominatorTree> PDTGetter;
  ///}

  /// Map to cache isGuaranteedToTransferExecutionToSuccessor results.
  DenseMap<const BasicBlock *, std::optional<bool>> BlockTransferMap;

  /// Map to cache containsIrreducibleCFG results.
  DenseMap<const Function *, std::optional<bool>> IrreducibleControlMap;

  /// Map from instructions to associated must be executed iterators.
  DenseMap<const Instruction *, std::unique_ptr<MustBeExecutedIterator>>
      InstructionIteratorMap;

  /// A unique end iterator.
  MustBeExecutedIterator EndIterator;
};

class MustExecutePrinterPass : public PassInfoMixin<MustExecutePrinterPass> {
  raw_ostream &OS;

public:
  MustExecutePrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

class MustBeExecutedContextPrinterPass
    : public PassInfoMixin<MustBeExecutedContextPrinterPass> {
  raw_ostream &OS;

public:
  MustBeExecutedContextPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif
