//===- FunctionSpecialization.h - Function Specialization -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Overview:
// ---------
// Function Specialization is a transformation which propagates the constant
// parameters of a function call from the caller to the callee. It is part of
// the Inter-Procedural Sparse Conditional Constant Propagation (IPSCCP) pass.
// The transformation runs iteratively a number of times which is controlled
// by the option `funcspec-max-iters`. Running it multiple times is needed
// for specializing recursive functions, but also exposes new opportunities
// arising from specializations which return constant values or contain calls
// which can be specialized.
//
// Function Specialization supports propagating constant parameters like
// function pointers, literal constants and addresses of global variables.
// By propagating function pointers, indirect calls become direct calls. This
// exposes inlining opportunities which we would have otherwise missed. That's
// why function specialization is run before the inliner in the optimization
// pipeline; that is by design.
//
// Cost Model:
// -----------
// The cost model facilitates a utility for estimating the specialization bonus
// from propagating a constant argument. This is the InstCostVisitor, a class
// that inherits from the InstVisitor. The bonus itself is expressed as codesize
// and latency savings. Codesize savings means the amount of code that becomes
// dead in the specialization from propagating the constant, whereas latency
// savings represents the cycles we are saving from replacing instructions with
// constant values. The InstCostVisitor overrides a set of `visit*` methods to
// be able to handle different types of instructions. These attempt to constant-
// fold the instruction in which case a constant is returned and propagated
// further.
//
// Function pointers are not handled by the InstCostVisitor. They are treated
// separately as they could expose inlining opportunities via indirect call
// promotion. The inlining bonus contributes to the total specialization score.
//
// For a specialization to be profitable its bonus needs to exceed a minimum
// threshold. There are three options for controlling the threshold which are
// expressed as percentages of the original function size:
//  * funcspec-min-codesize-savings
//  * funcspec-min-latency-savings
//  * funcspec-min-inlining-bonus
// There's also an option for controlling the codesize growth from recursive
// specializations. That is `funcspec-max-codesize-growth`.
//
// Once we have all the potential specializations with their score we need to
// choose the best ones, which fit in the module specialization budget. That
// is controlled by the option `funcspec-max-clones`. To find the best `NSpec`
// specializations we use a max-heap. For more details refer to D139346.
//
// Ideas:
// ------
// - With a function specialization attribute for arguments, we could have
//   a direct way to steer function specialization, avoiding the cost-model,
//   and thus control compile-times / code-size.
//
// - Perhaps a post-inlining function specialization pass could be more
//   aggressive on literal constants.
//
// References:
// -----------
// 2021 LLVM Dev Mtg “Introducing function specialisation, and can we enable
// it by default?”, https://www.youtube.com/watch?v=zJiCjeXgV5Q
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_FUNCTIONSPECIALIZATION_H
#define LLVM_TRANSFORMS_IPO_FUNCTIONSPECIALIZATION_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/SCCPSolver.h"
#include "llvm/Transforms/Utils/SizeOpts.h"

namespace llvm {
// Map of potential specializations for each function. The FunctionSpecializer
// keeps the discovered specialisation opportunities for the module in a single
// vector, where the specialisations of each function form a contiguous range.
// This map's value is the beginning and the end of that range.
using SpecMap = DenseMap<Function *, std::pair<unsigned, unsigned>>;

// Just a shorter abbreviation to improve indentation.
using Cost = InstructionCost;

// Map of known constants found during the specialization bonus estimation.
using ConstMap = DenseMap<Value *, Constant *>;

// Specialization signature, used to uniquely designate a specialization within
// a function.
struct SpecSig {
  // Hashing support, used to distinguish between ordinary, empty, or tombstone
  // keys.
  unsigned Key = 0;
  SmallVector<ArgInfo, 4> Args;

  bool operator==(const SpecSig &Other) const {
    if (Key != Other.Key)
      return false;
    return Args == Other.Args;
  }

  friend hash_code hash_value(const SpecSig &S) {
    return hash_combine(hash_value(S.Key),
                        hash_combine_range(S.Args.begin(), S.Args.end()));
  }
};

// Specialization instance.
struct Spec {
  // Original function.
  Function *F;

  // Cloned function, a specialized version of the original one.
  Function *Clone = nullptr;

  // Specialization signature.
  SpecSig Sig;

  // Profitability of the specialization.
  unsigned Score;

  // List of call sites, matching this specialization.
  SmallVector<CallBase *> CallSites;

  Spec(Function *F, const SpecSig &S, unsigned Score)
      : F(F), Sig(S), Score(Score) {}
  Spec(Function *F, const SpecSig &&S, unsigned Score)
      : F(F), Sig(S), Score(Score) {}
};

struct Bonus {
  unsigned CodeSize = 0;
  unsigned Latency = 0;

  Bonus() = default;

  Bonus(Cost CodeSize, Cost Latency) {
    int64_t Sz = *CodeSize.getValue();
    int64_t Ltc = *Latency.getValue();

    assert(Sz >= 0 && Ltc >= 0 && "CodeSize and Latency cannot be negative");
    // It is safe to down cast since we know the arguments
    // cannot be negative and Cost is of type int64_t.
    this->CodeSize = static_cast<unsigned>(Sz);
    this->Latency = static_cast<unsigned>(Ltc);
  }

  Bonus &operator+=(const Bonus RHS) {
    CodeSize += RHS.CodeSize;
    Latency += RHS.Latency;
    return *this;
  }

  Bonus operator+(const Bonus RHS) const {
    return Bonus(CodeSize + RHS.CodeSize, Latency + RHS.Latency);
  }

  bool operator==(const Bonus RHS) const {
    return CodeSize == RHS.CodeSize && Latency == RHS.Latency;
  }
};

class InstCostVisitor : public InstVisitor<InstCostVisitor, Constant *> {
  const DataLayout &DL;
  BlockFrequencyInfo &BFI;
  TargetTransformInfo &TTI;
  SCCPSolver &Solver;

  ConstMap KnownConstants;
  // Basic blocks known to be unreachable after constant propagation.
  DenseSet<BasicBlock *> DeadBlocks;
  // PHI nodes we have visited before.
  DenseSet<Instruction *> VisitedPHIs;
  // PHI nodes we have visited once without successfully constant folding them.
  // Once the InstCostVisitor has processed all the specialization arguments,
  // it should be possible to determine whether those PHIs can be folded
  // (some of their incoming values may have become constant or dead).
  SmallVector<Instruction *> PendingPHIs;

  ConstMap::iterator LastVisited;

public:
  InstCostVisitor(const DataLayout &DL, BlockFrequencyInfo &BFI,
                  TargetTransformInfo &TTI, SCCPSolver &Solver)
      : DL(DL), BFI(BFI), TTI(TTI), Solver(Solver) {}

  bool isBlockExecutable(BasicBlock *BB) {
    return Solver.isBlockExecutable(BB) && !DeadBlocks.contains(BB);
  }

  Bonus getSpecializationBonus(Argument *A, Constant *C);

  Bonus getBonusFromPendingPHIs();

private:
  friend class InstVisitor<InstCostVisitor, Constant *>;

  static bool canEliminateSuccessor(BasicBlock *BB, BasicBlock *Succ,
                                    DenseSet<BasicBlock *> &DeadBlocks);

  Bonus getUserBonus(Instruction *User, Value *Use = nullptr,
                     Constant *C = nullptr);

  Cost estimateBasicBlocks(SmallVectorImpl<BasicBlock *> &WorkList);
  Cost estimateSwitchInst(SwitchInst &I);
  Cost estimateBranchInst(BranchInst &I);

  // Transitively Incoming Values (TIV) is a set of Values that can "feed" a
  // value to the initial PHI-node. It is defined like this:
  //
  // * the initial PHI-node belongs to TIV.
  //
  // * for every PHI-node in TIV, its operands belong to TIV
  //
  // If TIV for the initial PHI-node (P) contains more than one constant or a
  // value that is not a PHI-node, then P cannot be folded to a constant.
  //
  // As soon as we detect these cases, we bail, without constructing the
  // full TIV.
  // Otherwise P can be folded to the one constant in TIV.
  bool discoverTransitivelyIncomingValues(Constant *Const, PHINode *Root,
                                          DenseSet<PHINode *> &TransitivePHIs);

  Constant *visitInstruction(Instruction &I) { return nullptr; }
  Constant *visitPHINode(PHINode &I);
  Constant *visitFreezeInst(FreezeInst &I);
  Constant *visitCallBase(CallBase &I);
  Constant *visitLoadInst(LoadInst &I);
  Constant *visitGetElementPtrInst(GetElementPtrInst &I);
  Constant *visitSelectInst(SelectInst &I);
  Constant *visitCastInst(CastInst &I);
  Constant *visitCmpInst(CmpInst &I);
  Constant *visitUnaryOperator(UnaryOperator &I);
  Constant *visitBinaryOperator(BinaryOperator &I);
};

class FunctionSpecializer {

  /// The IPSCCP Solver.
  SCCPSolver &Solver;

  Module &M;

  /// Analysis manager, needed to invalidate analyses.
  FunctionAnalysisManager *FAM;

  /// Analyses used to help determine if a function should be specialized.
  std::function<BlockFrequencyInfo &(Function &)> GetBFI;
  std::function<const TargetLibraryInfo &(Function &)> GetTLI;
  std::function<TargetTransformInfo &(Function &)> GetTTI;
  std::function<AssumptionCache &(Function &)> GetAC;

  SmallPtrSet<Function *, 32> Specializations;
  SmallPtrSet<Function *, 32> FullySpecialized;
  DenseMap<Function *, CodeMetrics> FunctionMetrics;
  DenseMap<Function *, unsigned> FunctionGrowth;
  unsigned NGlobals = 0;

public:
  FunctionSpecializer(
      SCCPSolver &Solver, Module &M, FunctionAnalysisManager *FAM,
      std::function<BlockFrequencyInfo &(Function &)> GetBFI,
      std::function<const TargetLibraryInfo &(Function &)> GetTLI,
      std::function<TargetTransformInfo &(Function &)> GetTTI,
      std::function<AssumptionCache &(Function &)> GetAC)
      : Solver(Solver), M(M), FAM(FAM), GetBFI(GetBFI), GetTLI(GetTLI),
        GetTTI(GetTTI), GetAC(GetAC) {}

  ~FunctionSpecializer();

  bool run();

  InstCostVisitor getInstCostVisitorFor(Function *F) {
    auto &BFI = GetBFI(*F);
    auto &TTI = GetTTI(*F);
    return InstCostVisitor(M.getDataLayout(), BFI, TTI, Solver);
  }

private:
  Constant *getPromotableAlloca(AllocaInst *Alloca, CallInst *Call);

  /// A constant stack value is an AllocaInst that has a single constant
  /// value stored to it. Return this constant if such an alloca stack value
  /// is a function argument.
  Constant *getConstantStackValue(CallInst *Call, Value *Val);

  /// See if there are any new constant values for the callers of \p F via
  /// stack variables and promote them to global variables.
  void promoteConstantStackValues(Function *F);

  /// Clean up fully specialized functions.
  void removeDeadFunctions();

  /// Remove any ssa_copy intrinsics that may have been introduced.
  void cleanUpSSA();

  /// @brief  Find potential specialization opportunities.
  /// @param F Function to specialize
  /// @param FuncSize Cost of specializing a function.
  /// @param AllSpecs A vector to add potential specializations to.
  /// @param SM  A map for a function's specialisation range
  /// @return True, if any potential specializations were found
  bool findSpecializations(Function *F, unsigned FuncSize,
                           SmallVectorImpl<Spec> &AllSpecs, SpecMap &SM);

  /// Compute the inlining bonus for replacing argument \p A with constant \p C.
  unsigned getInliningBonus(Argument *A, Constant *C);

  bool isCandidateFunction(Function *F);

  /// @brief Create a specialization of \p F and prime the SCCPSolver
  /// @param F Function to specialize
  /// @param S Which specialization to create
  /// @return The new, cloned function
  Function *createSpecialization(Function *F, const SpecSig &S);

  /// Determine if it is possible to specialise the function for constant values
  /// of the formal parameter \p A.
  bool isArgumentInteresting(Argument *A);

  /// Check if the value \p V  (an actual argument) is a constant or can only
  /// have a constant value. Return that constant.
  Constant *getCandidateConstant(Value *V);

  /// @brief Find and update calls to \p F, which match a specialization
  /// @param F Orginal function
  /// @param Begin Start of a range of possibly matching specialisations
  /// @param End End of a range (exclusive) of possibly matching specialisations
  void updateCallSites(Function *F, const Spec *Begin, const Spec *End);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_FUNCTIONSPECIALIZATION_H
