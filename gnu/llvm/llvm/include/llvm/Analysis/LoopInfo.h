//===- llvm/Analysis/LoopInfo.h - Natural Loop Calculator -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a GenericLoopInfo instantiation for LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPINFO_H
#define LLVM_ANALYSIS_LOOPINFO_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/GenericLoopInfo.h"
#include <algorithm>
#include <optional>
#include <utility>

namespace llvm {

class DominatorTree;
class InductionDescriptor;
class Instruction;
class LoopInfo;
class Loop;
class MDNode;
class MemorySSAUpdater;
class ScalarEvolution;
class raw_ostream;

// Implementation in Support/GenericLoopInfoImpl.h
extern template class LoopBase<BasicBlock, Loop>;

/// Represents a single loop in the control flow graph.  Note that not all SCCs
/// in the CFG are necessarily loops.
class LLVM_EXTERNAL_VISIBILITY Loop : public LoopBase<BasicBlock, Loop> {
public:
  /// A range representing the start and end location of a loop.
  class LocRange {
    DebugLoc Start;
    DebugLoc End;

  public:
    LocRange() = default;
    LocRange(DebugLoc Start) : Start(Start), End(Start) {}
    LocRange(DebugLoc Start, DebugLoc End)
        : Start(std::move(Start)), End(std::move(End)) {}

    const DebugLoc &getStart() const { return Start; }
    const DebugLoc &getEnd() const { return End; }

    /// Check for null.
    ///
    explicit operator bool() const { return Start && End; }
  };

  /// Return true if the specified value is loop invariant.
  bool isLoopInvariant(const Value *V) const;

  /// Return true if all the operands of the specified instruction are loop
  /// invariant.
  bool hasLoopInvariantOperands(const Instruction *I) const;

  /// If the given value is an instruction inside of the loop and it can be
  /// hoisted, do so to make it trivially loop-invariant.
  /// Return true if \c V is already loop-invariant, and false if \c V can't
  /// be made loop-invariant. If \c V is made loop-invariant, \c Changed is
  /// set to true. This function can be used as a slightly more aggressive
  /// replacement for isLoopInvariant.
  ///
  /// If InsertPt is specified, it is the point to hoist instructions to.
  /// If null, the terminator of the loop preheader is used.
  ///
  bool makeLoopInvariant(Value *V, bool &Changed,
                         Instruction *InsertPt = nullptr,
                         MemorySSAUpdater *MSSAU = nullptr,
                         ScalarEvolution *SE = nullptr) const;

  /// If the given instruction is inside of the loop and it can be hoisted, do
  /// so to make it trivially loop-invariant.
  /// Return true if \c I is already loop-invariant, and false if \c I can't
  /// be made loop-invariant. If \c I is made loop-invariant, \c Changed is
  /// set to true. This function can be used as a slightly more aggressive
  /// replacement for isLoopInvariant.
  ///
  /// If InsertPt is specified, it is the point to hoist instructions to.
  /// If null, the terminator of the loop preheader is used.
  ///
  bool makeLoopInvariant(Instruction *I, bool &Changed,
                         Instruction *InsertPt = nullptr,
                         MemorySSAUpdater *MSSAU = nullptr,
                         ScalarEvolution *SE = nullptr) const;

  /// Check to see if the loop has a canonical induction variable: an integer
  /// recurrence that starts at 0 and increments by one each time through the
  /// loop. If so, return the phi node that corresponds to it.
  ///
  /// The IndVarSimplify pass transforms loops to have a canonical induction
  /// variable.
  ///
  PHINode *getCanonicalInductionVariable() const;

  /// Get the latch condition instruction.
  ICmpInst *getLatchCmpInst() const;

  /// Obtain the unique incoming and back edge. Return false if they are
  /// non-unique or the loop is dead; otherwise, return true.
  bool getIncomingAndBackEdge(BasicBlock *&Incoming,
                              BasicBlock *&Backedge) const;

  /// Below are some utilities to get the loop guard, loop bounds and induction
  /// variable, and to check if a given phinode is an auxiliary induction
  /// variable, if the loop is guarded, and if the loop is canonical.
  ///
  /// Here is an example:
  /// \code
  /// for (int i = lb; i < ub; i+=step)
  ///   <loop body>
  /// --- pseudo LLVMIR ---
  /// beforeloop:
  ///   guardcmp = (lb < ub)
  ///   if (guardcmp) goto preheader; else goto afterloop
  /// preheader:
  /// loop:
  ///   i_1 = phi[{lb, preheader}, {i_2, latch}]
  ///   <loop body>
  ///   i_2 = i_1 + step
  /// latch:
  ///   cmp = (i_2 < ub)
  ///   if (cmp) goto loop
  /// exit:
  /// afterloop:
  /// \endcode
  ///
  /// - getBounds
  ///   - getInitialIVValue      --> lb
  ///   - getStepInst            --> i_2 = i_1 + step
  ///   - getStepValue           --> step
  ///   - getFinalIVValue        --> ub
  ///   - getCanonicalPredicate  --> '<'
  ///   - getDirection           --> Increasing
  ///
  /// - getInductionVariable            --> i_1
  /// - isAuxiliaryInductionVariable(x) --> true if x == i_1
  /// - getLoopGuardBranch()
  ///                 --> `if (guardcmp) goto preheader; else goto afterloop`
  /// - isGuarded()                     --> true
  /// - isCanonical                     --> false
  struct LoopBounds {
    /// Return the LoopBounds object if
    /// - the given \p IndVar is an induction variable
    /// - the initial value of the induction variable can be found
    /// - the step instruction of the induction variable can be found
    /// - the final value of the induction variable can be found
    ///
    /// Else std::nullopt.
    static std::optional<Loop::LoopBounds>
    getBounds(const Loop &L, PHINode &IndVar, ScalarEvolution &SE);

    /// Get the initial value of the loop induction variable.
    Value &getInitialIVValue() const { return InitialIVValue; }

    /// Get the instruction that updates the loop induction variable.
    Instruction &getStepInst() const { return StepInst; }

    /// Get the step that the loop induction variable gets updated by in each
    /// loop iteration. Return nullptr if not found.
    Value *getStepValue() const { return StepValue; }

    /// Get the final value of the loop induction variable.
    Value &getFinalIVValue() const { return FinalIVValue; }

    /// Return the canonical predicate for the latch compare instruction, if
    /// able to be calcuated. Else BAD_ICMP_PREDICATE.
    ///
    /// A predicate is considered as canonical if requirements below are all
    /// satisfied:
    /// 1. The first successor of the latch branch is the loop header
    ///    If not, inverse the predicate.
    /// 2. One of the operands of the latch comparison is StepInst
    ///    If not, and
    ///    - if the current calcuated predicate is not ne or eq, flip the
    ///      predicate.
    ///    - else if the loop is increasing, return slt
    ///      (notice that it is safe to change from ne or eq to sign compare)
    ///    - else if the loop is decreasing, return sgt
    ///      (notice that it is safe to change from ne or eq to sign compare)
    ///
    /// Here is an example when both (1) and (2) are not satisfied:
    /// \code
    /// loop.header:
    ///  %iv = phi [%initialiv, %loop.preheader], [%inc, %loop.header]
    ///  %inc = add %iv, %step
    ///  %cmp = slt %iv, %finaliv
    ///  br %cmp, %loop.exit, %loop.header
    /// loop.exit:
    /// \endcode
    /// - The second successor of the latch branch is the loop header instead
    ///   of the first successor (slt -> sge)
    /// - The first operand of the latch comparison (%cmp) is the IndVar (%iv)
    ///   instead of the StepInst (%inc) (sge -> sgt)
    ///
    /// The predicate would be sgt if both (1) and (2) are satisfied.
    /// getCanonicalPredicate() returns sgt for this example.
    /// Note: The IR is not changed.
    ICmpInst::Predicate getCanonicalPredicate() const;

    /// An enum for the direction of the loop
    /// - for (int i = 0; i < ub; ++i)  --> Increasing
    /// - for (int i = ub; i > 0; --i)  --> Descresing
    /// - for (int i = x; i != y; i+=z) --> Unknown
    enum class Direction { Increasing, Decreasing, Unknown };

    /// Get the direction of the loop.
    Direction getDirection() const;

  private:
    LoopBounds(const Loop &Loop, Value &I, Instruction &SI, Value *SV, Value &F,
               ScalarEvolution &SE)
        : L(Loop), InitialIVValue(I), StepInst(SI), StepValue(SV),
          FinalIVValue(F), SE(SE) {}

    const Loop &L;

    // The initial value of the loop induction variable
    Value &InitialIVValue;

    // The instruction that updates the loop induction variable
    Instruction &StepInst;

    // The value that the loop induction variable gets updated by in each loop
    // iteration
    Value *StepValue;

    // The final value of the loop induction variable
    Value &FinalIVValue;

    ScalarEvolution &SE;
  };

  /// Return the struct LoopBounds collected if all struct members are found,
  /// else std::nullopt.
  std::optional<LoopBounds> getBounds(ScalarEvolution &SE) const;

  /// Return the loop induction variable if found, else return nullptr.
  /// An instruction is considered as the loop induction variable if
  /// - it is an induction variable of the loop; and
  /// - it is used to determine the condition of the branch in the loop latch
  ///
  /// Note: the induction variable doesn't need to be canonical, i.e. starts at
  /// zero and increments by one each time through the loop (but it can be).
  PHINode *getInductionVariable(ScalarEvolution &SE) const;

  /// Get the loop induction descriptor for the loop induction variable. Return
  /// true if the loop induction variable is found.
  bool getInductionDescriptor(ScalarEvolution &SE,
                              InductionDescriptor &IndDesc) const;

  /// Return true if the given PHINode \p AuxIndVar is
  /// - in the loop header
  /// - not used outside of the loop
  /// - incremented by a loop invariant step for each loop iteration
  /// - step instruction opcode should be add or sub
  /// Note: auxiliary induction variable is not required to be used in the
  ///       conditional branch in the loop latch. (but it can be)
  bool isAuxiliaryInductionVariable(PHINode &AuxIndVar,
                                    ScalarEvolution &SE) const;

  /// Return the loop guard branch, if it exists.
  ///
  /// This currently only works on simplified loop, as it requires a preheader
  /// and a latch to identify the guard. It will work on loops of the form:
  /// \code
  /// GuardBB:
  ///   br cond1, Preheader, ExitSucc <== GuardBranch
  /// Preheader:
  ///   br Header
  /// Header:
  ///  ...
  ///   br Latch
  /// Latch:
  ///   br cond2, Header, ExitBlock
  /// ExitBlock:
  ///   br ExitSucc
  /// ExitSucc:
  /// \endcode
  BranchInst *getLoopGuardBranch() const;

  /// Return true iff the loop is
  /// - in simplify rotated form, and
  /// - guarded by a loop guard branch.
  bool isGuarded() const { return (getLoopGuardBranch() != nullptr); }

  /// Return true if the loop is in rotated form.
  ///
  /// This does not check if the loop was rotated by loop rotation, instead it
  /// only checks if the loop is in rotated form (has a valid latch that exists
  /// the loop).
  bool isRotatedForm() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    BasicBlock *Latch = getLoopLatch();
    return Latch && isLoopExiting(Latch);
  }

  /// Return true if the loop induction variable starts at zero and increments
  /// by one each time through the loop.
  bool isCanonical(ScalarEvolution &SE) const;

  /// Return true if the Loop is in LCSSA form. If \p IgnoreTokens is set to
  /// true, token values defined inside loop are allowed to violate LCSSA form.
  bool isLCSSAForm(const DominatorTree &DT, bool IgnoreTokens = true) const;

  /// Return true if this Loop and all inner subloops are in LCSSA form. If \p
  /// IgnoreTokens is set to true, token values defined inside loop are allowed
  /// to violate LCSSA form.
  bool isRecursivelyLCSSAForm(const DominatorTree &DT, const LoopInfo &LI,
                              bool IgnoreTokens = true) const;

  /// Return true if the Loop is in the form that the LoopSimplify form
  /// transforms loops to, which is sometimes called normal form.
  bool isLoopSimplifyForm() const;

  /// Return true if the loop body is safe to clone in practice.
  bool isSafeToClone() const;

  /// Returns true if the loop is annotated parallel.
  ///
  /// A parallel loop can be assumed to not contain any dependencies between
  /// iterations by the compiler. That is, any loop-carried dependency checking
  /// can be skipped completely when parallelizing the loop on the target
  /// machine. Thus, if the parallel loop information originates from the
  /// programmer, e.g. via the OpenMP parallel for pragma, it is the
  /// programmer's responsibility to ensure there are no loop-carried
  /// dependencies. The final execution order of the instructions across
  /// iterations is not guaranteed, thus, the end result might or might not
  /// implement actual concurrent execution of instructions across multiple
  /// iterations.
  bool isAnnotatedParallel() const;

  /// Return the llvm.loop loop id metadata node for this loop if it is present.
  ///
  /// If this loop contains the same llvm.loop metadata on each branch to the
  /// header then the node is returned. If any latch instruction does not
  /// contain llvm.loop or if multiple latches contain different nodes then
  /// 0 is returned.
  MDNode *getLoopID() const;
  /// Set the llvm.loop loop id metadata for this loop.
  ///
  /// The LoopID metadata node will be added to each terminator instruction in
  /// the loop that branches to the loop header.
  ///
  /// The LoopID metadata node should have one or more operands and the first
  /// operand should be the node itself.
  void setLoopID(MDNode *LoopID) const;

  /// Add llvm.loop.unroll.disable to this loop's loop id metadata.
  ///
  /// Remove existing unroll metadata and add unroll disable metadata to
  /// indicate the loop has already been unrolled.  This prevents a loop
  /// from being unrolled more than is directed by a pragma if the loop
  /// unrolling pass is run more than once (which it generally is).
  void setLoopAlreadyUnrolled();

  /// Add llvm.loop.mustprogress to this loop's loop id metadata.
  void setLoopMustProgress();

  void dump() const;
  void dumpVerbose() const;

  /// Return the debug location of the start of this loop.
  /// This looks for a BB terminating instruction with a known debug
  /// location by looking at the preheader and header blocks. If it
  /// cannot find a terminating instruction with location information,
  /// it returns an unknown location.
  DebugLoc getStartLoc() const;

  /// Return the source code span of the loop.
  LocRange getLocRange() const;

  /// Return a string containing the debug location of the loop (file name +
  /// line number if present, otherwise module name). Meant to be used for debug
  /// printing within LLVM_DEBUG.
  std::string getLocStr() const;

  StringRef getName() const {
    if (BasicBlock *Header = getHeader())
      if (Header->hasName())
        return Header->getName();
    return "<unnamed loop>";
  }

private:
  Loop() = default;

  friend class LoopInfoBase<BasicBlock, Loop>;
  friend class LoopBase<BasicBlock, Loop>;
  explicit Loop(BasicBlock *BB) : LoopBase<BasicBlock, Loop>(BB) {}
  ~Loop() = default;
};

// Implementation in Support/GenericLoopInfoImpl.h
extern template class LoopInfoBase<BasicBlock, Loop>;

class LoopInfo : public LoopInfoBase<BasicBlock, Loop> {
  typedef LoopInfoBase<BasicBlock, Loop> BaseT;

  friend class LoopBase<BasicBlock, Loop>;

  void operator=(const LoopInfo &) = delete;
  LoopInfo(const LoopInfo &) = delete;

public:
  LoopInfo() = default;
  explicit LoopInfo(const DominatorTreeBase<BasicBlock, false> &DomTree);

  LoopInfo(LoopInfo &&Arg) : BaseT(std::move(static_cast<BaseT &>(Arg))) {}
  LoopInfo &operator=(LoopInfo &&RHS) {
    BaseT::operator=(std::move(static_cast<BaseT &>(RHS)));
    return *this;
  }

  /// Handle invalidation explicitly.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);

  // Most of the public interface is provided via LoopInfoBase.

  /// Update LoopInfo after removing the last backedge from a loop. This updates
  /// the loop forest and parent loops for each block so that \c L is no longer
  /// referenced, but does not actually delete \c L immediately. The pointer
  /// will remain valid until this LoopInfo's memory is released.
  void erase(Loop *L);

  /// Returns true if replacing From with To everywhere is guaranteed to
  /// preserve LCSSA form.
  bool replacementPreservesLCSSAForm(Instruction *From, Value *To) {
    // Preserving LCSSA form is only problematic if the replacing value is an
    // instruction.
    Instruction *I = dyn_cast<Instruction>(To);
    if (!I)
      return true;
    // If both instructions are defined in the same basic block then replacement
    // cannot break LCSSA form.
    if (I->getParent() == From->getParent())
      return true;
    // If the instruction is not defined in a loop then it can safely replace
    // anything.
    Loop *ToLoop = getLoopFor(I->getParent());
    if (!ToLoop)
      return true;
    // If the replacing instruction is defined in the same loop as the original
    // instruction, or in a loop that contains it as an inner loop, then using
    // it as a replacement will not break LCSSA form.
    return ToLoop->contains(getLoopFor(From->getParent()));
  }

  /// Checks if moving a specific instruction can break LCSSA in any loop.
  ///
  /// Return true if moving \p Inst to before \p NewLoc will break LCSSA,
  /// assuming that the function containing \p Inst and \p NewLoc is currently
  /// in LCSSA form.
  bool movementPreservesLCSSAForm(Instruction *Inst, Instruction *NewLoc) {
    assert(Inst->getFunction() == NewLoc->getFunction() &&
           "Can't reason about IPO!");

    auto *OldBB = Inst->getParent();
    auto *NewBB = NewLoc->getParent();

    // Movement within the same loop does not break LCSSA (the equality check is
    // to avoid doing a hashtable lookup in case of intra-block movement).
    if (OldBB == NewBB)
      return true;

    auto *OldLoop = getLoopFor(OldBB);
    auto *NewLoop = getLoopFor(NewBB);

    if (OldLoop == NewLoop)
      return true;

    // Check if Outer contains Inner; with the null loop counting as the
    // "outermost" loop.
    auto Contains = [](const Loop *Outer, const Loop *Inner) {
      return !Outer || Outer->contains(Inner);
    };

    // To check that the movement of Inst to before NewLoc does not break LCSSA,
    // we need to check two sets of uses for possible LCSSA violations at
    // NewLoc: the users of NewInst, and the operands of NewInst.

    // If we know we're hoisting Inst out of an inner loop to an outer loop,
    // then the uses *of* Inst don't need to be checked.

    if (!Contains(NewLoop, OldLoop)) {
      for (Use &U : Inst->uses()) {
        auto *UI = cast<Instruction>(U.getUser());
        auto *UBB = isa<PHINode>(UI) ? cast<PHINode>(UI)->getIncomingBlock(U)
                                     : UI->getParent();
        if (UBB != NewBB && getLoopFor(UBB) != NewLoop)
          return false;
      }
    }

    // If we know we're sinking Inst from an outer loop into an inner loop, then
    // the *operands* of Inst don't need to be checked.

    if (!Contains(OldLoop, NewLoop)) {
      // See below on why we can't handle phi nodes here.
      if (isa<PHINode>(Inst))
        return false;

      for (Use &U : Inst->operands()) {
        auto *DefI = dyn_cast<Instruction>(U.get());
        if (!DefI)
          return false;

        // This would need adjustment if we allow Inst to be a phi node -- the
        // new use block won't simply be NewBB.

        auto *DefBlock = DefI->getParent();
        if (DefBlock != NewBB && getLoopFor(DefBlock) != NewLoop)
          return false;
      }
    }

    return true;
  }

  // Return true if a new use of V added in ExitBB would require an LCSSA PHI
  // to be inserted at the begining of the block.  Note that V is assumed to
  // dominate ExitBB, and ExitBB must be the exit block of some loop.  The
  // IR is assumed to be in LCSSA form before the planned insertion.
  bool wouldBeOutOfLoopUseRequiringLCSSA(const Value *V,
                                         const BasicBlock *ExitBB) const;
};

/// Enable verification of loop info.
///
/// The flag enables checks which are expensive and are disabled by default
/// unless the `EXPENSIVE_CHECKS` macro is defined.  The `-verify-loop-info`
/// flag allows the checks to be enabled selectively without re-compilation.
extern bool VerifyLoopInfo;

// Allow clients to walk the list of nested loops...
template <> struct GraphTraits<const Loop *> {
  typedef const Loop *NodeRef;
  typedef LoopInfo::iterator ChildIteratorType;

  static NodeRef getEntryNode(const Loop *L) { return L; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

template <> struct GraphTraits<Loop *> {
  typedef Loop *NodeRef;
  typedef LoopInfo::iterator ChildIteratorType;

  static NodeRef getEntryNode(Loop *L) { return L; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

/// Analysis pass that exposes the \c LoopInfo for a function.
class LoopAnalysis : public AnalysisInfoMixin<LoopAnalysis> {
  friend AnalysisInfoMixin<LoopAnalysis>;
  static AnalysisKey Key;

public:
  typedef LoopInfo Result;

  LoopInfo run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c LoopAnalysis results.
class LoopPrinterPass : public PassInfoMixin<LoopPrinterPass> {
  raw_ostream &OS;

public:
  explicit LoopPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

/// Verifier pass for the \c LoopAnalysis results.
struct LoopVerifierPass : public PassInfoMixin<LoopVerifierPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

/// The legacy pass manager's analysis pass to compute loop information.
class LoopInfoWrapperPass : public FunctionPass {
  LoopInfo LI;

public:
  static char ID; // Pass identification, replacement for typeid

  LoopInfoWrapperPass();

  LoopInfo &getLoopInfo() { return LI; }
  const LoopInfo &getLoopInfo() const { return LI; }

  /// Calculate the natural loop information for a given function.
  bool runOnFunction(Function &F) override;

  void verifyAnalysis() const override;

  void releaseMemory() override { LI.releaseMemory(); }

  void print(raw_ostream &O, const Module *M = nullptr) const override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Function to print a loop's contents as LLVM's text IR assembly.
void printLoop(Loop &L, raw_ostream &OS, const std::string &Banner = "");

/// Find and return the loop attribute node for the attribute @p Name in
/// @p LoopID. Return nullptr if there is no such attribute.
MDNode *findOptionMDForLoopID(MDNode *LoopID, StringRef Name);

/// Find string metadata for a loop.
///
/// Returns the MDNode where the first operand is the metadata's name. The
/// following operands are the metadata's values. If no metadata with @p Name is
/// found, return nullptr.
MDNode *findOptionMDForLoop(const Loop *TheLoop, StringRef Name);

std::optional<bool> getOptionalBoolLoopAttribute(const Loop *TheLoop,
                                                 StringRef Name);

/// Returns true if Name is applied to TheLoop and enabled.
bool getBooleanLoopAttribute(const Loop *TheLoop, StringRef Name);

/// Find named metadata for a loop with an integer value.
std::optional<int> getOptionalIntLoopAttribute(const Loop *TheLoop,
                                               StringRef Name);

/// Find named metadata for a loop with an integer value. Return \p Default if
/// not set.
int getIntLoopAttribute(const Loop *TheLoop, StringRef Name, int Default = 0);

/// Find string metadata for loop
///
/// If it has a value (e.g. {"llvm.distribute", 1} return the value as an
/// operand or null otherwise.  If the string metadata is not found return
/// Optional's not-a-value.
std::optional<const MDOperand *> findStringMetadataForLoop(const Loop *TheLoop,
                                                           StringRef Name);

/// Find the convergence heart of the loop.
CallBase *getLoopConvergenceHeart(const Loop *TheLoop);

/// Look for the loop attribute that requires progress within the loop.
/// Note: Most consumers probably want "isMustProgress" which checks
/// the containing function attribute too.
bool hasMustProgress(const Loop *L);

/// Return true if this loop can be assumed to make progress.  (i.e. can't
/// be infinite without side effects without also being undefined)
bool isMustProgress(const Loop *L);

/// Return true if this loop can be assumed to run for a finite number of
/// iterations.
bool isFinite(const Loop *L);

/// Return whether an MDNode might represent an access group.
///
/// Access group metadata nodes have to be distinct and empty. Being
/// always-empty ensures that it never needs to be changed (which -- because
/// MDNodes are designed immutable -- would require creating a new MDNode). Note
/// that this is not a sufficient condition: not every distinct and empty NDNode
/// is representing an access group.
bool isValidAsAccessGroup(MDNode *AccGroup);

/// Create a new LoopID after the loop has been transformed.
///
/// This can be used when no follow-up loop attributes are defined
/// (llvm::makeFollowupLoopID returning None) to stop transformations to be
/// applied again.
///
/// @param Context        The LLVMContext in which to create the new LoopID.
/// @param OrigLoopID     The original LoopID; can be nullptr if the original
///                       loop has no LoopID.
/// @param RemovePrefixes Remove all loop attributes that have these prefixes.
///                       Use to remove metadata of the transformation that has
///                       been applied.
/// @param AddAttrs       Add these loop attributes to the new LoopID.
///
/// @return A new LoopID that can be applied using Loop::setLoopID().
llvm::MDNode *
makePostTransformationMetadata(llvm::LLVMContext &Context, MDNode *OrigLoopID,
                               llvm::ArrayRef<llvm::StringRef> RemovePrefixes,
                               llvm::ArrayRef<llvm::MDNode *> AddAttrs);
} // namespace llvm

#endif
