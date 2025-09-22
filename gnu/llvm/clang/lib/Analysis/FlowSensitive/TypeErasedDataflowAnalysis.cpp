//===- TypeErasedDataflowAnalysis.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines type-erased base types and functions for building dataflow
//  analyses that run over Control-Flow Graphs (CFGs).
//
//===----------------------------------------------------------------------===//

#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "clang/AST/ASTDumper.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/DataflowLattice.h"
#include "clang/Analysis/FlowSensitive/DataflowWorklist.h"
#include "clang/Analysis/FlowSensitive/RecordOps.h"
#include "clang/Analysis/FlowSensitive/Transfer.h"
#include "clang/Analysis/FlowSensitive/TypeErasedDataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"

#define DEBUG_TYPE "clang-dataflow"

namespace clang {
namespace dataflow {

/// Returns the index of `Block` in the successors of `Pred`.
static int blockIndexInPredecessor(const CFGBlock &Pred,
                                   const CFGBlock &Block) {
  auto BlockPos = llvm::find_if(
      Pred.succs(), [&Block](const CFGBlock::AdjacentBlock &Succ) {
        return Succ && Succ->getBlockID() == Block.getBlockID();
      });
  return BlockPos - Pred.succ_begin();
}

// A "backedge" node is a block introduced in the CFG exclusively to indicate a
// loop backedge. They are exactly identified by the presence of a non-null
// pointer to the entry block of the loop condition. Note that this is not
// necessarily the block with the loop statement as terminator, because
// short-circuit operators will result in multiple blocks encoding the loop
// condition, only one of which will contain the loop statement as terminator.
static bool isBackedgeNode(const CFGBlock &B) {
  return B.getLoopTarget() != nullptr;
}

namespace {

/// Extracts the terminator's condition expression.
class TerminatorVisitor
    : public ConstStmtVisitor<TerminatorVisitor, const Expr *> {
public:
  TerminatorVisitor() = default;
  const Expr *VisitIfStmt(const IfStmt *S) { return S->getCond(); }
  const Expr *VisitWhileStmt(const WhileStmt *S) { return S->getCond(); }
  const Expr *VisitDoStmt(const DoStmt *S) { return S->getCond(); }
  const Expr *VisitForStmt(const ForStmt *S) { return S->getCond(); }
  const Expr *VisitCXXForRangeStmt(const CXXForRangeStmt *) {
    // Don't do anything special for CXXForRangeStmt, because the condition
    // (being implicitly generated) isn't visible from the loop body.
    return nullptr;
  }
  const Expr *VisitBinaryOperator(const BinaryOperator *S) {
    assert(S->getOpcode() == BO_LAnd || S->getOpcode() == BO_LOr);
    return S->getLHS();
  }
  const Expr *VisitConditionalOperator(const ConditionalOperator *S) {
    return S->getCond();
  }
};

/// Holds data structures required for running dataflow analysis.
struct AnalysisContext {
  AnalysisContext(const AdornedCFG &ACFG, TypeErasedDataflowAnalysis &Analysis,
                  const Environment &InitEnv,
                  llvm::ArrayRef<std::optional<TypeErasedDataflowAnalysisState>>
                      BlockStates)
      : ACFG(ACFG), Analysis(Analysis), InitEnv(InitEnv),
        Log(*InitEnv.getDataflowAnalysisContext().getOptions().Log),
        BlockStates(BlockStates) {
    Log.beginAnalysis(ACFG, Analysis);
  }
  ~AnalysisContext() { Log.endAnalysis(); }

  /// Contains the CFG being analyzed.
  const AdornedCFG &ACFG;
  /// The analysis to be run.
  TypeErasedDataflowAnalysis &Analysis;
  /// Initial state to start the analysis.
  const Environment &InitEnv;
  Logger &Log;
  /// Stores the state of a CFG block if it has been evaluated by the analysis.
  /// The indices correspond to the block IDs.
  llvm::ArrayRef<std::optional<TypeErasedDataflowAnalysisState>> BlockStates;
};

class PrettyStackTraceAnalysis : public llvm::PrettyStackTraceEntry {
public:
  PrettyStackTraceAnalysis(const AdornedCFG &ACFG, const char *Message)
      : ACFG(ACFG), Message(Message) {}

  void print(raw_ostream &OS) const override {
    OS << Message << "\n";
    OS << "Decl:\n";
    ACFG.getDecl().dump(OS);
    OS << "CFG:\n";
    ACFG.getCFG().print(OS, LangOptions(), false);
  }

private:
  const AdornedCFG &ACFG;
  const char *Message;
};

class PrettyStackTraceCFGElement : public llvm::PrettyStackTraceEntry {
public:
  PrettyStackTraceCFGElement(const CFGElement &Element, int BlockIdx,
                             int ElementIdx, const char *Message)
      : Element(Element), BlockIdx(BlockIdx), ElementIdx(ElementIdx),
        Message(Message) {}

  void print(raw_ostream &OS) const override {
    OS << Message << ": Element [B" << BlockIdx << "." << ElementIdx << "]\n";
    if (auto Stmt = Element.getAs<CFGStmt>()) {
      OS << "Stmt:\n";
      ASTDumper Dumper(OS, false);
      Dumper.Visit(Stmt->getStmt());
    }
  }

private:
  const CFGElement &Element;
  int BlockIdx;
  int ElementIdx;
  const char *Message;
};

// Builds a joined TypeErasedDataflowAnalysisState from 0 or more sources,
// each of which may be owned (built as part of the join) or external (a
// reference to an Environment that will outlive the builder).
// Avoids unneccesary copies of the environment.
class JoinedStateBuilder {
  AnalysisContext &AC;
  Environment::ExprJoinBehavior JoinBehavior;
  std::vector<const TypeErasedDataflowAnalysisState *> All;
  std::deque<TypeErasedDataflowAnalysisState> Owned;

  TypeErasedDataflowAnalysisState
  join(const TypeErasedDataflowAnalysisState &L,
       const TypeErasedDataflowAnalysisState &R) {
    return {AC.Analysis.joinTypeErased(L.Lattice, R.Lattice),
            Environment::join(L.Env, R.Env, AC.Analysis, JoinBehavior)};
  }

public:
  JoinedStateBuilder(AnalysisContext &AC,
                     Environment::ExprJoinBehavior JoinBehavior)
      : AC(AC), JoinBehavior(JoinBehavior) {}

  void addOwned(TypeErasedDataflowAnalysisState State) {
    Owned.push_back(std::move(State));
    All.push_back(&Owned.back());
  }
  void addUnowned(const TypeErasedDataflowAnalysisState &State) {
    All.push_back(&State);
  }
  TypeErasedDataflowAnalysisState take() && {
    if (All.empty())
      // FIXME: Consider passing `Block` to Analysis.typeErasedInitialElement
      // to enable building analyses like computation of dominators that
      // initialize the state of each basic block differently.
      return {AC.Analysis.typeErasedInitialElement(), AC.InitEnv.fork()};
    if (All.size() == 1)
      // Join the environment with itself so that we discard expression state if
      // desired.
      // FIXME: We could consider writing special-case code for this that only
      // does the discarding, but it's not clear if this is worth it.
      return {All[0]->Lattice, Environment::join(All[0]->Env, All[0]->Env,
                                                 AC.Analysis, JoinBehavior)};

    auto Result = join(*All[0], *All[1]);
    for (unsigned I = 2; I < All.size(); ++I)
      Result = join(Result, *All[I]);
    return Result;
  }
};
} // namespace

static const Expr *getTerminatorCondition(const Stmt *TerminatorStmt) {
  return TerminatorStmt == nullptr ? nullptr
                                   : TerminatorVisitor().Visit(TerminatorStmt);
}

/// Computes the input state for a given basic block by joining the output
/// states of its predecessors.
///
/// Requirements:
///
///   All predecessors of `Block` except those with loop back edges must have
///   already been transferred. States in `AC.BlockStates` that are set to
///   `std::nullopt` represent basic blocks that are not evaluated yet.
static TypeErasedDataflowAnalysisState
computeBlockInputState(const CFGBlock &Block, AnalysisContext &AC) {
  std::vector<const CFGBlock *> Preds(Block.pred_begin(), Block.pred_end());
  if (Block.getTerminator().isTemporaryDtorsBranch()) {
    // This handles a special case where the code that produced the CFG includes
    // a conditional operator with a branch that constructs a temporary and
    // calls a destructor annotated as noreturn. The CFG models this as follows:
    //
    // B1 (contains the condition of the conditional operator) - succs: B2, B3
    // B2 (contains code that does not call a noreturn destructor) - succs: B4
    // B3 (contains code that calls a noreturn destructor) - succs: B4
    // B4 (has temporary destructor terminator) - succs: B5, B6
    // B5 (noreturn block that is associated with the noreturn destructor call)
    // B6 (contains code that follows the conditional operator statement)
    //
    // The first successor (B5 above) of a basic block with a temporary
    // destructor terminator (B4 above) is the block that evaluates the
    // destructor. If that block has a noreturn element then the predecessor
    // block that constructed the temporary object (B3 above) is effectively a
    // noreturn block and its state should not be used as input for the state
    // of the block that has a temporary destructor terminator (B4 above). This
    // holds regardless of which branch of the ternary operator calls the
    // noreturn destructor. However, it doesn't cases where a nested ternary
    // operator includes a branch that contains a noreturn destructor call.
    //
    // See `NoreturnDestructorTest` for concrete examples.
    if (Block.succ_begin()->getReachableBlock() != nullptr &&
        Block.succ_begin()->getReachableBlock()->hasNoReturnElement()) {
      auto &StmtToBlock = AC.ACFG.getStmtToBlock();
      auto StmtBlock = StmtToBlock.find(Block.getTerminatorStmt());
      assert(StmtBlock != StmtToBlock.end());
      llvm::erase(Preds, StmtBlock->getSecond());
    }
  }

  // If any of the predecessor blocks contains an expression consumed in a
  // different block, we need to keep expression state.
  // Note that in this case, we keep expression state for all predecessors,
  // rather than only those predecessors that actually contain an expression
  // consumed in a different block. While this is potentially suboptimal, it's
  // actually likely, if we have control flow within a full expression, that
  // all predecessors have expression state consumed in a different block.
  Environment::ExprJoinBehavior JoinBehavior = Environment::DiscardExprState;
  for (const CFGBlock *Pred : Preds) {
    if (Pred && AC.ACFG.containsExprConsumedInDifferentBlock(*Pred)) {
      JoinBehavior = Environment::KeepExprState;
      break;
    }
  }

  JoinedStateBuilder Builder(AC, JoinBehavior);
  for (const CFGBlock *Pred : Preds) {
    // Skip if the `Block` is unreachable or control flow cannot get past it.
    if (!Pred || Pred->hasNoReturnElement())
      continue;

    // Skip if `Pred` was not evaluated yet. This could happen if `Pred` has a
    // loop back edge to `Block`.
    const std::optional<TypeErasedDataflowAnalysisState> &MaybePredState =
        AC.BlockStates[Pred->getBlockID()];
    if (!MaybePredState)
      continue;

    const TypeErasedDataflowAnalysisState &PredState = *MaybePredState;
    const Expr *Cond = getTerminatorCondition(Pred->getTerminatorStmt());
    if (Cond == nullptr) {
      Builder.addUnowned(PredState);
      continue;
    }

    bool BranchVal = blockIndexInPredecessor(*Pred, Block) == 0;

    // `transferBranch` may need to mutate the environment to describe the
    // dynamic effect of the terminator for a given branch.  Copy now.
    TypeErasedDataflowAnalysisState Copy = MaybePredState->fork();
    if (AC.Analysis.builtinOptions()) {
      auto *CondVal = Copy.Env.get<BoolValue>(*Cond);
      // In transferCFGBlock(), we ensure that we always have a `Value`
      // for the terminator condition, so assert this. We consciously
      // assert ourselves instead of asserting via `cast()` so that we get
      // a more meaningful line number if the assertion fails.
      assert(CondVal != nullptr);
      BoolValue *AssertedVal =
          BranchVal ? CondVal : &Copy.Env.makeNot(*CondVal);
      Copy.Env.assume(AssertedVal->formula());
    }
    AC.Analysis.transferBranchTypeErased(BranchVal, Cond, Copy.Lattice,
                                         Copy.Env);
    Builder.addOwned(std::move(Copy));
  }
  return std::move(Builder).take();
}

/// Built-in transfer function for `CFGStmt`.
static void
builtinTransferStatement(unsigned CurBlockID, const CFGStmt &Elt,
                         TypeErasedDataflowAnalysisState &InputState,
                         AnalysisContext &AC) {
  const Stmt *S = Elt.getStmt();
  assert(S != nullptr);
  transfer(StmtToEnvMap(AC.ACFG, AC.BlockStates, CurBlockID, InputState), *S,
           InputState.Env, AC.Analysis);
}

/// Built-in transfer function for `CFGInitializer`.
static void
builtinTransferInitializer(const CFGInitializer &Elt,
                           TypeErasedDataflowAnalysisState &InputState) {
  const CXXCtorInitializer *Init = Elt.getInitializer();
  assert(Init != nullptr);

  auto &Env = InputState.Env;
  auto &ThisLoc = *Env.getThisPointeeStorageLocation();

  if (!Init->isAnyMemberInitializer())
    // FIXME: Handle base initialization
    return;

  auto *InitExpr = Init->getInit();
  assert(InitExpr != nullptr);

  const FieldDecl *Member = nullptr;
  RecordStorageLocation *ParentLoc = &ThisLoc;
  StorageLocation *MemberLoc = nullptr;
  if (Init->isMemberInitializer()) {
    Member = Init->getMember();
    MemberLoc = ThisLoc.getChild(*Member);
  } else {
    IndirectFieldDecl *IndirectField = Init->getIndirectMember();
    assert(IndirectField != nullptr);
    MemberLoc = &ThisLoc;
    for (const auto *I : IndirectField->chain()) {
      Member = cast<FieldDecl>(I);
      ParentLoc = cast<RecordStorageLocation>(MemberLoc);
      MemberLoc = ParentLoc->getChild(*Member);
    }
  }
  assert(Member != nullptr);

  // FIXME: Instead of these case distinctions, we would ideally want to be able
  // to simply use `Environment::createObject()` here, the same way that we do
  // this in `TransferVisitor::VisitInitListExpr()`. However, this would require
  // us to be able to build a list of fields that we then use to initialize an
  // `RecordStorageLocation` -- and the problem is that, when we get here,
  // the `RecordStorageLocation` already exists. We should explore if there's
  // anything that we can do to change this.
  if (Member->getType()->isReferenceType()) {
    auto *InitExprLoc = Env.getStorageLocation(*InitExpr);
    if (InitExprLoc == nullptr)
      return;

    ParentLoc->setChild(*Member, InitExprLoc);
    // Record-type initializers construct themselves directly into the result
    // object, so there is no need to handle them here.
  } else if (!Member->getType()->isRecordType()) {
    assert(MemberLoc != nullptr);
    if (auto *InitExprVal = Env.getValue(*InitExpr))
      Env.setValue(*MemberLoc, *InitExprVal);
  }
}

static void builtinTransfer(unsigned CurBlockID, const CFGElement &Elt,
                            TypeErasedDataflowAnalysisState &State,
                            AnalysisContext &AC) {
  switch (Elt.getKind()) {
  case CFGElement::Statement:
    builtinTransferStatement(CurBlockID, Elt.castAs<CFGStmt>(), State, AC);
    break;
  case CFGElement::Initializer:
    builtinTransferInitializer(Elt.castAs<CFGInitializer>(), State);
    break;
  case CFGElement::LifetimeEnds:
    // Removing declarations when their lifetime ends serves two purposes:
    // - Eliminate unnecessary clutter from `Environment::DeclToLoc`
    // - Allow us to assert that, when joining two `Environment`s, the two
    //   `DeclToLoc` maps never contain entries that map the same declaration to
    //   different storage locations.
    if (const ValueDecl *VD = Elt.castAs<CFGLifetimeEnds>().getVarDecl())
      State.Env.removeDecl(*VD);
    break;
  default:
    // FIXME: Evaluate other kinds of `CFGElement`
    break;
  }
}

/// Transfers `State` by evaluating each element in the `Block` based on the
/// `AC.Analysis` specified.
///
/// Built-in transfer functions (if the option for `ApplyBuiltinTransfer` is set
/// by the analysis) will be applied to the element before evaluation by the
/// user-specified analysis.
/// `PostVisitCFG` (if provided) will be applied to the element after evaluation
/// by the user-specified analysis.
static TypeErasedDataflowAnalysisState
transferCFGBlock(const CFGBlock &Block, AnalysisContext &AC,
                 const CFGEltCallbacksTypeErased &PostAnalysisCallbacks = {}) {
  AC.Log.enterBlock(Block, PostAnalysisCallbacks.Before != nullptr ||
                               PostAnalysisCallbacks.After != nullptr);
  auto State = computeBlockInputState(Block, AC);
  AC.Log.recordState(State);
  int ElementIdx = 1;
  for (const auto &Element : Block) {
    PrettyStackTraceCFGElement CrashInfo(Element, Block.getBlockID(),
                                         ElementIdx++, "transferCFGBlock");

    AC.Log.enterElement(Element);

    if (PostAnalysisCallbacks.Before) {
      PostAnalysisCallbacks.Before(Element, State);
    }

    // Built-in analysis
    if (AC.Analysis.builtinOptions()) {
      builtinTransfer(Block.getBlockID(), Element, State, AC);
    }

    // User-provided analysis
    AC.Analysis.transferTypeErased(Element, State.Lattice, State.Env);

    if (PostAnalysisCallbacks.After) {
      PostAnalysisCallbacks.After(Element, State);
    }

    AC.Log.recordState(State);
  }

  // If we have a terminator, evaluate its condition.
  // This `Expr` may not appear as a `CFGElement` anywhere else, and it's
  // important that we evaluate it here (rather than while processing the
  // terminator) so that we put the corresponding value in the right
  // environment.
  if (const Expr *TerminatorCond =
          dyn_cast_or_null<Expr>(Block.getTerminatorCondition())) {
    if (State.Env.getValue(*TerminatorCond) == nullptr)
      // FIXME: This only runs the builtin transfer, not the analysis-specific
      // transfer. Fixing this isn't trivial, as the analysis-specific transfer
      // takes a `CFGElement` as input, but some expressions only show up as a
      // terminator condition, but not as a `CFGElement`. The condition of an if
      // statement is one such example.
      transfer(StmtToEnvMap(AC.ACFG, AC.BlockStates, Block.getBlockID(), State),
               *TerminatorCond, State.Env, AC.Analysis);

    // If the transfer function didn't produce a value, create an atom so that
    // we have *some* value for the condition expression. This ensures that
    // when we extend the flow condition, it actually changes.
    if (State.Env.getValue(*TerminatorCond) == nullptr)
      State.Env.setValue(*TerminatorCond, State.Env.makeAtomicBoolValue());
    AC.Log.recordState(State);
  }

  return State;
}

llvm::Expected<std::vector<std::optional<TypeErasedDataflowAnalysisState>>>
runTypeErasedDataflowAnalysis(
    const AdornedCFG &ACFG, TypeErasedDataflowAnalysis &Analysis,
    const Environment &InitEnv,
    const CFGEltCallbacksTypeErased &PostAnalysisCallbacks,
    std::int32_t MaxBlockVisits) {
  PrettyStackTraceAnalysis CrashInfo(ACFG, "runTypeErasedDataflowAnalysis");

  std::optional<Environment> MaybeStartingEnv;
  if (InitEnv.callStackSize() == 0) {
    MaybeStartingEnv = InitEnv.fork();
    MaybeStartingEnv->initialize();
  }
  const Environment &StartingEnv =
      MaybeStartingEnv ? *MaybeStartingEnv : InitEnv;

  const clang::CFG &CFG = ACFG.getCFG();
  PostOrderCFGView POV(&CFG);
  ForwardDataflowWorklist Worklist(CFG, &POV);

  std::vector<std::optional<TypeErasedDataflowAnalysisState>> BlockStates(
      CFG.size());

  // The entry basic block doesn't contain statements so it can be skipped.
  const CFGBlock &Entry = CFG.getEntry();
  BlockStates[Entry.getBlockID()] = {Analysis.typeErasedInitialElement(),
                                     StartingEnv.fork()};
  Worklist.enqueueSuccessors(&Entry);

  AnalysisContext AC(ACFG, Analysis, StartingEnv, BlockStates);
  std::int32_t BlockVisits = 0;
  while (const CFGBlock *Block = Worklist.dequeue()) {
    LLVM_DEBUG(llvm::dbgs()
               << "Processing Block " << Block->getBlockID() << "\n");
    if (++BlockVisits > MaxBlockVisits) {
      return llvm::createStringError(std::errc::timed_out,
                                     "maximum number of blocks processed");
    }

    const std::optional<TypeErasedDataflowAnalysisState> &OldBlockState =
        BlockStates[Block->getBlockID()];
    TypeErasedDataflowAnalysisState NewBlockState =
        transferCFGBlock(*Block, AC);
    LLVM_DEBUG({
      llvm::errs() << "New Env:\n";
      NewBlockState.Env.dump();
    });

    if (OldBlockState) {
      LLVM_DEBUG({
        llvm::errs() << "Old Env:\n";
        OldBlockState->Env.dump();
      });
      if (isBackedgeNode(*Block)) {
        LatticeJoinEffect Effect1 = Analysis.widenTypeErased(
            NewBlockState.Lattice, OldBlockState->Lattice);
        LatticeJoinEffect Effect2 =
            NewBlockState.Env.widen(OldBlockState->Env, Analysis);
        if (Effect1 == LatticeJoinEffect::Unchanged &&
            Effect2 == LatticeJoinEffect::Unchanged) {
          // The state of `Block` didn't change from widening so there's no need
          // to revisit its successors.
          AC.Log.blockConverged();
          continue;
        }
      } else if (Analysis.isEqualTypeErased(OldBlockState->Lattice,
                                            NewBlockState.Lattice) &&
                 OldBlockState->Env.equivalentTo(NewBlockState.Env, Analysis)) {
        // The state of `Block` didn't change after transfer so there's no need
        // to revisit its successors.
        AC.Log.blockConverged();
        continue;
      }
    }

    BlockStates[Block->getBlockID()] = std::move(NewBlockState);

    // Do not add unreachable successor blocks to `Worklist`.
    if (Block->hasNoReturnElement())
      continue;

    Worklist.enqueueSuccessors(Block);
  }
  // FIXME: Consider evaluating unreachable basic blocks (those that have a
  // state set to `std::nullopt` at this point) to also analyze dead code.

  if (PostAnalysisCallbacks.Before || PostAnalysisCallbacks.After) {
    for (const CFGBlock *Block : ACFG.getCFG()) {
      // Skip blocks that were not evaluated.
      if (!BlockStates[Block->getBlockID()])
        continue;
      transferCFGBlock(*Block, AC, PostAnalysisCallbacks);
    }
  }

  return std::move(BlockStates);
}

} // namespace dataflow
} // namespace clang
