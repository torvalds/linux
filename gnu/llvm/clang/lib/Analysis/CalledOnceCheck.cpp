//===- CalledOnceCheck.cpp - Check 'called once' parameters ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/CalledOnceCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/DataflowWorklist.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <memory>
#include <optional>

using namespace clang;

namespace {
static constexpr unsigned EXPECTED_MAX_NUMBER_OF_PARAMS = 2;
template <class T>
using ParamSizedVector = llvm::SmallVector<T, EXPECTED_MAX_NUMBER_OF_PARAMS>;
static constexpr unsigned EXPECTED_NUMBER_OF_BASIC_BLOCKS = 8;
template <class T>
using CFGSizedVector = llvm::SmallVector<T, EXPECTED_NUMBER_OF_BASIC_BLOCKS>;
constexpr llvm::StringLiteral CONVENTIONAL_NAMES[] = {
    "completionHandler", "completion",      "withCompletionHandler",
    "withCompletion",    "completionBlock", "withCompletionBlock",
    "replyTo",           "reply",           "withReplyTo"};
constexpr llvm::StringLiteral CONVENTIONAL_SUFFIXES[] = {
    "WithCompletionHandler", "WithCompletion", "WithCompletionBlock",
    "WithReplyTo", "WithReply"};
constexpr llvm::StringLiteral CONVENTIONAL_CONDITIONS[] = {
    "error", "cancel", "shouldCall", "done", "OK", "success"};

struct KnownCalledOnceParameter {
  llvm::StringLiteral FunctionName;
  unsigned ParamIndex;
};
constexpr KnownCalledOnceParameter KNOWN_CALLED_ONCE_PARAMETERS[] = {
    {llvm::StringLiteral{"dispatch_async"}, 1},
    {llvm::StringLiteral{"dispatch_async_and_wait"}, 1},
    {llvm::StringLiteral{"dispatch_after"}, 2},
    {llvm::StringLiteral{"dispatch_sync"}, 1},
    {llvm::StringLiteral{"dispatch_once"}, 1},
    {llvm::StringLiteral{"dispatch_barrier_async"}, 1},
    {llvm::StringLiteral{"dispatch_barrier_async_and_wait"}, 1},
    {llvm::StringLiteral{"dispatch_barrier_sync"}, 1}};

class ParameterStatus {
public:
  // Status kind is basically the main part of parameter's status.
  // The kind represents our knowledge (so far) about a tracked parameter
  // in the context of this analysis.
  //
  // Since we want to report on missing and extraneous calls, we need to
  // track the fact whether paramater was called or not.  This automatically
  // decides two kinds: `NotCalled` and `Called`.
  //
  // One of the erroneous situations is the case when parameter is called only
  // on some of the paths.  We could've considered it `NotCalled`, but we want
  // to report double call warnings even if these two calls are not guaranteed
  // to happen in every execution.  We also don't want to have it as `Called`
  // because not calling tracked parameter on all of the paths is an error
  // on its own.  For these reasons, we need to have a separate kind,
  // `MaybeCalled`, and change `Called` to `DefinitelyCalled` to avoid
  // confusion.
  //
  // Two violations of calling parameter more than once and not calling it on
  // every path are not, however, mutually exclusive.  In situations where both
  // violations take place, we prefer to report ONLY double call.  It's always
  // harder to pinpoint a bug that has arisen when a user neglects to take the
  // right action (and therefore, no action is taken), than when a user takes
  // the wrong action.  And, in order to remember that we already reported
  // a double call, we need another kind: `Reported`.
  //
  // Our analysis is intra-procedural and, while in the perfect world,
  // developers only use tracked parameters to call them, in the real world,
  // the picture might be different.  Parameters can be stored in global
  // variables or leaked into other functions that we know nothing about.
  // We try to be lenient and trust users.  Another kind `Escaped` reflects
  // such situations.  We don't know if it gets called there or not, but we
  // should always think of `Escaped` as the best possible option.
  //
  // Some of the paths in the analyzed functions might end with a call
  // to noreturn functions.  Such paths are not required to have parameter
  // calls and we want to track that.  For the purposes of better diagnostics,
  // we don't want to reuse `Escaped` and, thus, have another kind `NoReturn`.
  //
  // Additionally, we have `NotVisited` kind that tells us nothing about
  // a tracked parameter, but is used for tracking analyzed (aka visited)
  // basic blocks.
  //
  // If we consider `|` to be a JOIN operation of two kinds coming from
  // two different paths, the following properties must hold:
  //
  //   1. for any Kind K: K | K == K
  //      Joining two identical kinds should result in the same kind.
  //
  //   2. for any Kind K: Reported | K == Reported
  //      Doesn't matter on which path it was reported, it still is.
  //
  //   3. for any Kind K: NoReturn | K == K
  //      We can totally ignore noreturn paths during merges.
  //
  //   4. DefinitelyCalled | NotCalled == MaybeCalled
  //      Called on one path, not called on another - that's simply
  //      a definition for MaybeCalled.
  //
  //   5. for any Kind K in [DefinitelyCalled, NotCalled, MaybeCalled]:
  //      Escaped | K == K
  //      Escaped mirrors other statuses after joins.
  //      Every situation, when we join any of the listed kinds K,
  //      is a violation.  For this reason, in order to assume the
  //      best outcome for this escape, we consider it to be the
  //      same as the other path.
  //
  //   6. for any Kind K in [DefinitelyCalled, NotCalled]:
  //      MaybeCalled | K == MaybeCalled
  //      MaybeCalled should basically stay after almost every join.
  enum Kind {
    // No-return paths should be absolutely transparent for the analysis.
    // 0x0 is the identity element for selected join operation (binary or).
    NoReturn = 0x0, /* 0000 */
    // Escaped marks situations when marked parameter escaped into
    // another function (so we can assume that it was possibly called there).
    Escaped = 0x1, /* 0001 */
    // Parameter was definitely called once at this point.
    DefinitelyCalled = 0x3, /* 0011 */
    // Kinds less or equal to NON_ERROR_STATUS are not considered errors.
    NON_ERROR_STATUS = DefinitelyCalled,
    // Parameter was not yet called.
    NotCalled = 0x5, /* 0101 */
    // Parameter was not called at least on one path leading to this point,
    // while there is also at least one path that it gets called.
    MaybeCalled = 0x7, /* 0111 */
    // Parameter was not yet analyzed.
    NotVisited = 0x8, /* 1000 */
    // We already reported a violation and stopped tracking calls for this
    // parameter.
    Reported = 0xF, /* 1111 */
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ Reported)
  };

  constexpr ParameterStatus() = default;
  /* implicit */ ParameterStatus(Kind K) : StatusKind(K) {
    assert(!seenAnyCalls(K) && "Can't initialize status without a call");
  }
  ParameterStatus(Kind K, const Expr *Call) : StatusKind(K), Call(Call) {
    assert(seenAnyCalls(K) && "This kind is not supposed to have a call");
  }

  const Expr &getCall() const {
    assert(seenAnyCalls(getKind()) && "ParameterStatus doesn't have a call");
    return *Call;
  }
  static bool seenAnyCalls(Kind K) {
    return (K & DefinitelyCalled) == DefinitelyCalled && K != Reported;
  }
  bool seenAnyCalls() const { return seenAnyCalls(getKind()); }

  static bool isErrorStatus(Kind K) { return K > NON_ERROR_STATUS; }
  bool isErrorStatus() const { return isErrorStatus(getKind()); }

  Kind getKind() const { return StatusKind; }

  void join(const ParameterStatus &Other) {
    // If we have a pointer already, let's keep it.
    // For the purposes of the analysis, it doesn't really matter
    // which call we report.
    //
    // If we don't have a pointer, let's take whatever gets joined.
    if (!Call) {
      Call = Other.Call;
    }
    // Join kinds.
    StatusKind |= Other.getKind();
  }

  bool operator==(const ParameterStatus &Other) const {
    // We compare only kinds, pointers on their own is only additional
    // information.
    return getKind() == Other.getKind();
  }

private:
  // It would've been a perfect place to use llvm::PointerIntPair, but
  // unfortunately NumLowBitsAvailable for clang::Expr had been reduced to 2.
  Kind StatusKind = NotVisited;
  const Expr *Call = nullptr;
};

/// State aggregates statuses of all tracked parameters.
class State {
public:
  State(unsigned Size, ParameterStatus::Kind K = ParameterStatus::NotVisited)
      : ParamData(Size, K) {}

  /// Return status of a parameter with the given index.
  /// \{
  ParameterStatus &getStatusFor(unsigned Index) { return ParamData[Index]; }
  const ParameterStatus &getStatusFor(unsigned Index) const {
    return ParamData[Index];
  }
  /// \}

  /// Return true if parameter with the given index can be called.
  bool seenAnyCalls(unsigned Index) const {
    return getStatusFor(Index).seenAnyCalls();
  }
  /// Return a reference that we consider a call.
  ///
  /// Should only be used for parameters that can be called.
  const Expr &getCallFor(unsigned Index) const {
    return getStatusFor(Index).getCall();
  }
  /// Return status kind of parameter with the given index.
  ParameterStatus::Kind getKindFor(unsigned Index) const {
    return getStatusFor(Index).getKind();
  }

  bool isVisited() const {
    return llvm::all_of(ParamData, [](const ParameterStatus &S) {
      return S.getKind() != ParameterStatus::NotVisited;
    });
  }

  // Join other state into the current state.
  void join(const State &Other) {
    assert(ParamData.size() == Other.ParamData.size() &&
           "Couldn't join statuses with different sizes");
    for (auto Pair : llvm::zip(ParamData, Other.ParamData)) {
      std::get<0>(Pair).join(std::get<1>(Pair));
    }
  }

  using iterator = ParamSizedVector<ParameterStatus>::iterator;
  using const_iterator = ParamSizedVector<ParameterStatus>::const_iterator;

  iterator begin() { return ParamData.begin(); }
  iterator end() { return ParamData.end(); }

  const_iterator begin() const { return ParamData.begin(); }
  const_iterator end() const { return ParamData.end(); }

  bool operator==(const State &Other) const {
    return ParamData == Other.ParamData;
  }

private:
  ParamSizedVector<ParameterStatus> ParamData;
};

/// A simple class that finds DeclRefExpr in the given expression.
///
/// However, we don't want to find ANY nested DeclRefExpr skipping whatever
/// expressions on our way.  Only certain expressions considered "no-op"
/// for our task are indeed skipped.
class DeclRefFinder
    : public ConstStmtVisitor<DeclRefFinder, const DeclRefExpr *> {
public:
  /// Find a DeclRefExpr in the given expression.
  ///
  /// In its most basic form (ShouldRetrieveFromComparisons == false),
  /// this function can be simply reduced to the following question:
  ///
  ///   - If expression E is used as a function argument, could we say
  ///     that DeclRefExpr nested in E is used as an argument?
  ///
  /// According to this rule, we can say that parens, casts and dereferencing
  /// (dereferencing only applied to function pointers, but this is our case)
  /// can be skipped.
  ///
  /// When we should look into comparisons the question changes to:
  ///
  ///   - If expression E is used as a condition, could we say that
  ///     DeclRefExpr is being checked?
  ///
  /// And even though, these are two different questions, they have quite a lot
  /// in common.  Actually, we can say that whatever expression answers
  /// positively the first question also fits the second question as well.
  ///
  /// In addition, we skip binary operators == and !=, and unary opeartor !.
  static const DeclRefExpr *find(const Expr *E,
                                 bool ShouldRetrieveFromComparisons = false) {
    return DeclRefFinder(ShouldRetrieveFromComparisons).Visit(E);
  }

  const DeclRefExpr *VisitDeclRefExpr(const DeclRefExpr *DR) { return DR; }

  const DeclRefExpr *VisitUnaryOperator(const UnaryOperator *UO) {
    switch (UO->getOpcode()) {
    case UO_LNot:
      // We care about logical not only if we care about comparisons.
      if (!ShouldRetrieveFromComparisons)
        return nullptr;
      [[fallthrough]];
    // Function pointer/references can be dereferenced before a call.
    // That doesn't make it, however, any different from a regular call.
    // For this reason, dereference operation is a "no-op".
    case UO_Deref:
      return Visit(UO->getSubExpr());
    default:
      return nullptr;
    }
  }

  const DeclRefExpr *VisitBinaryOperator(const BinaryOperator *BO) {
    if (!ShouldRetrieveFromComparisons)
      return nullptr;

    switch (BO->getOpcode()) {
    case BO_EQ:
    case BO_NE: {
      const DeclRefExpr *LHS = Visit(BO->getLHS());
      return LHS ? LHS : Visit(BO->getRHS());
    }
    default:
      return nullptr;
    }
  }

  const DeclRefExpr *VisitOpaqueValueExpr(const OpaqueValueExpr *OVE) {
    return Visit(OVE->getSourceExpr());
  }

  const DeclRefExpr *VisitCallExpr(const CallExpr *CE) {
    if (!ShouldRetrieveFromComparisons)
      return nullptr;

    // We want to see through some of the boolean builtin functions
    // that we are likely to see in conditions.
    switch (CE->getBuiltinCallee()) {
    case Builtin::BI__builtin_expect:
    case Builtin::BI__builtin_expect_with_probability: {
      assert(CE->getNumArgs() >= 2);

      const DeclRefExpr *Candidate = Visit(CE->getArg(0));
      return Candidate != nullptr ? Candidate : Visit(CE->getArg(1));
    }

    case Builtin::BI__builtin_unpredictable:
      return Visit(CE->getArg(0));

    default:
      return nullptr;
    }
  }

  const DeclRefExpr *VisitExpr(const Expr *E) {
    // It is a fallback method that gets called whenever the actual type
    // of the given expression is not covered.
    //
    // We first check if we have anything to skip.  And then repeat the whole
    // procedure for a nested expression instead.
    const Expr *DeclutteredExpr = E->IgnoreParenCasts();
    return E != DeclutteredExpr ? Visit(DeclutteredExpr) : nullptr;
  }

private:
  DeclRefFinder(bool ShouldRetrieveFromComparisons)
      : ShouldRetrieveFromComparisons(ShouldRetrieveFromComparisons) {}

  bool ShouldRetrieveFromComparisons;
};

const DeclRefExpr *findDeclRefExpr(const Expr *In,
                                   bool ShouldRetrieveFromComparisons = false) {
  return DeclRefFinder::find(In, ShouldRetrieveFromComparisons);
}

const ParmVarDecl *
findReferencedParmVarDecl(const Expr *In,
                          bool ShouldRetrieveFromComparisons = false) {
  if (const DeclRefExpr *DR =
          findDeclRefExpr(In, ShouldRetrieveFromComparisons)) {
    return dyn_cast<ParmVarDecl>(DR->getDecl());
  }

  return nullptr;
}

/// Return conditions expression of a statement if it has one.
const Expr *getCondition(const Stmt *S) {
  if (!S) {
    return nullptr;
  }

  if (const auto *If = dyn_cast<IfStmt>(S)) {
    return If->getCond();
  }
  if (const auto *Ternary = dyn_cast<AbstractConditionalOperator>(S)) {
    return Ternary->getCond();
  }

  return nullptr;
}

/// A small helper class that collects all named identifiers in the given
/// expression.  It traverses it recursively, so names from deeper levels
/// of the AST will end up in the results.
/// Results might have duplicate names, if this is a problem, convert to
/// string sets afterwards.
class NamesCollector : public RecursiveASTVisitor<NamesCollector> {
public:
  static constexpr unsigned EXPECTED_NUMBER_OF_NAMES = 5;
  using NameCollection =
      llvm::SmallVector<llvm::StringRef, EXPECTED_NUMBER_OF_NAMES>;

  static NameCollection collect(const Expr *From) {
    NamesCollector Impl;
    Impl.TraverseStmt(const_cast<Expr *>(From));
    return Impl.Result;
  }

  bool VisitDeclRefExpr(const DeclRefExpr *E) {
    Result.push_back(E->getDecl()->getName());
    return true;
  }

  bool VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *E) {
    llvm::StringRef Name;

    if (E->isImplicitProperty()) {
      ObjCMethodDecl *PropertyMethodDecl = nullptr;
      if (E->isMessagingGetter()) {
        PropertyMethodDecl = E->getImplicitPropertyGetter();
      } else {
        PropertyMethodDecl = E->getImplicitPropertySetter();
      }
      assert(PropertyMethodDecl &&
             "Implicit property must have associated declaration");
      Name = PropertyMethodDecl->getSelector().getNameForSlot(0);
    } else {
      assert(E->isExplicitProperty());
      Name = E->getExplicitProperty()->getName();
    }

    Result.push_back(Name);
    return true;
  }

private:
  NamesCollector() = default;
  NameCollection Result;
};

/// Check whether the given expression mentions any of conventional names.
bool mentionsAnyOfConventionalNames(const Expr *E) {
  NamesCollector::NameCollection MentionedNames = NamesCollector::collect(E);

  return llvm::any_of(MentionedNames, [](llvm::StringRef ConditionName) {
    return llvm::any_of(
        CONVENTIONAL_CONDITIONS,
        [ConditionName](const llvm::StringLiteral &Conventional) {
          return ConditionName.contains_insensitive(Conventional);
        });
  });
}

/// Clarification is a simple pair of a reason why parameter is not called
/// on every path and a statement to blame.
struct Clarification {
  NeverCalledReason Reason;
  const Stmt *Location;
};

/// A helper class that can produce a clarification based on the given pair
/// of basic blocks.
class NotCalledClarifier
    : public ConstStmtVisitor<NotCalledClarifier,
                              std::optional<Clarification>> {
public:
  /// The main entrypoint for the class, the function that tries to find the
  /// clarification of how to explain which sub-path starts with a CFG edge
  /// from Conditional to SuccWithoutCall.
  ///
  /// This means that this function has one precondition:
  ///   SuccWithoutCall should be a successor block for Conditional.
  ///
  /// Because clarification is not needed for non-trivial pairs of blocks
  /// (i.e. SuccWithoutCall is not the only successor), it returns meaningful
  /// results only for such cases.  For this very reason, the parent basic
  /// block, Conditional, is named that way, so it is clear what kind of
  /// block is expected.
  static std::optional<Clarification> clarify(const CFGBlock *Conditional,
                                              const CFGBlock *SuccWithoutCall) {
    if (const Stmt *Terminator = Conditional->getTerminatorStmt()) {
      return NotCalledClarifier{Conditional, SuccWithoutCall}.Visit(Terminator);
    }
    return std::nullopt;
  }

  std::optional<Clarification> VisitIfStmt(const IfStmt *If) {
    return VisitBranchingBlock(If, NeverCalledReason::IfThen);
  }

  std::optional<Clarification>
  VisitAbstractConditionalOperator(const AbstractConditionalOperator *Ternary) {
    return VisitBranchingBlock(Ternary, NeverCalledReason::IfThen);
  }

  std::optional<Clarification> VisitSwitchStmt(const SwitchStmt *Switch) {
    const Stmt *CaseToBlame = SuccInQuestion->getLabel();
    if (!CaseToBlame) {
      // If interesting basic block is not labeled, it means that this
      // basic block does not represent any of the cases.
      return Clarification{NeverCalledReason::SwitchSkipped, Switch};
    }

    for (const SwitchCase *Case = Switch->getSwitchCaseList(); Case;
         Case = Case->getNextSwitchCase()) {
      if (Case == CaseToBlame) {
        return Clarification{NeverCalledReason::Switch, Case};
      }
    }

    llvm_unreachable("Found unexpected switch structure");
  }

  std::optional<Clarification> VisitForStmt(const ForStmt *For) {
    return VisitBranchingBlock(For, NeverCalledReason::LoopEntered);
  }

  std::optional<Clarification> VisitWhileStmt(const WhileStmt *While) {
    return VisitBranchingBlock(While, NeverCalledReason::LoopEntered);
  }

  std::optional<Clarification>
  VisitBranchingBlock(const Stmt *Terminator, NeverCalledReason DefaultReason) {
    assert(Parent->succ_size() == 2 &&
           "Branching block should have exactly two successors");
    unsigned SuccessorIndex = getSuccessorIndex(Parent, SuccInQuestion);
    NeverCalledReason ActualReason =
        updateForSuccessor(DefaultReason, SuccessorIndex);
    return Clarification{ActualReason, Terminator};
  }

  std::optional<Clarification> VisitBinaryOperator(const BinaryOperator *) {
    // We don't want to report on short-curcuit logical operations.
    return std::nullopt;
  }

  std::optional<Clarification> VisitStmt(const Stmt *Terminator) {
    // If we got here, we didn't have a visit function for more derived
    // classes of statement that this terminator actually belongs to.
    //
    // This is not a good scenario and should not happen in practice, but
    // at least we'll warn the user.
    return Clarification{NeverCalledReason::FallbackReason, Terminator};
  }

  static unsigned getSuccessorIndex(const CFGBlock *Parent,
                                    const CFGBlock *Child) {
    CFGBlock::const_succ_iterator It = llvm::find(Parent->succs(), Child);
    assert(It != Parent->succ_end() &&
           "Given blocks should be in parent-child relationship");
    return It - Parent->succ_begin();
  }

  static NeverCalledReason
  updateForSuccessor(NeverCalledReason ReasonForTrueBranch,
                     unsigned SuccessorIndex) {
    assert(SuccessorIndex <= 1);
    unsigned RawReason =
        static_cast<unsigned>(ReasonForTrueBranch) + SuccessorIndex;
    assert(RawReason <=
           static_cast<unsigned>(NeverCalledReason::LARGEST_VALUE));
    return static_cast<NeverCalledReason>(RawReason);
  }

private:
  NotCalledClarifier(const CFGBlock *Parent, const CFGBlock *SuccInQuestion)
      : Parent(Parent), SuccInQuestion(SuccInQuestion) {}

  const CFGBlock *Parent, *SuccInQuestion;
};

class CalledOnceChecker : public ConstStmtVisitor<CalledOnceChecker> {
public:
  static void check(AnalysisDeclContext &AC, CalledOnceCheckHandler &Handler,
                    bool CheckConventionalParameters) {
    CalledOnceChecker(AC, Handler, CheckConventionalParameters).check();
  }

private:
  CalledOnceChecker(AnalysisDeclContext &AC, CalledOnceCheckHandler &Handler,
                    bool CheckConventionalParameters)
      : FunctionCFG(*AC.getCFG()), AC(AC), Handler(Handler),
        CheckConventionalParameters(CheckConventionalParameters),
        CurrentState(0) {
    initDataStructures();
    assert((size() == 0 || !States.empty()) &&
           "Data structures are inconsistent");
  }

  //===----------------------------------------------------------------------===//
  //                            Initializing functions
  //===----------------------------------------------------------------------===//

  void initDataStructures() {
    const Decl *AnalyzedDecl = AC.getDecl();

    if (const auto *Function = dyn_cast<FunctionDecl>(AnalyzedDecl)) {
      findParamsToTrack(Function);
    } else if (const auto *Method = dyn_cast<ObjCMethodDecl>(AnalyzedDecl)) {
      findParamsToTrack(Method);
    } else if (const auto *Block = dyn_cast<BlockDecl>(AnalyzedDecl)) {
      findCapturesToTrack(Block);
      findParamsToTrack(Block);
    }

    // Have something to track, let's init states for every block from the CFG.
    if (size() != 0) {
      States =
          CFGSizedVector<State>(FunctionCFG.getNumBlockIDs(), State(size()));
    }
  }

  void findCapturesToTrack(const BlockDecl *Block) {
    for (const auto &Capture : Block->captures()) {
      if (const auto *P = dyn_cast<ParmVarDecl>(Capture.getVariable())) {
        // Parameter DeclContext is its owning function or method.
        const DeclContext *ParamContext = P->getDeclContext();
        if (shouldBeCalledOnce(ParamContext, P)) {
          TrackedParams.push_back(P);
        }
      }
    }
  }

  template <class FunctionLikeDecl>
  void findParamsToTrack(const FunctionLikeDecl *Function) {
    for (unsigned Index : llvm::seq<unsigned>(0u, Function->param_size())) {
      if (shouldBeCalledOnce(Function, Index)) {
        TrackedParams.push_back(Function->getParamDecl(Index));
      }
    }
  }

  //===----------------------------------------------------------------------===//
  //                         Main logic 'check' functions
  //===----------------------------------------------------------------------===//

  void check() {
    // Nothing to check here: we don't have marked parameters.
    if (size() == 0 || isPossiblyEmptyImpl())
      return;

    assert(
        llvm::none_of(States, [](const State &S) { return S.isVisited(); }) &&
        "None of the blocks should be 'visited' before the analysis");

    // For our task, both backward and forward approaches suite well.
    // However, in order to report better diagnostics, we decided to go with
    // backward analysis.
    //
    // Let's consider the following CFG and how forward and backward analyses
    // will work for it.
    //
    //                  FORWARD:           |                 BACKWARD:
    //                    #1               |                     #1
    //                +---------+          |               +-----------+
    //                |   if    |          |               |MaybeCalled|
    //                +---------+          |               +-----------+
    //                |NotCalled|          |               |    if     |
    //                +---------+          |               +-----------+
    //                 /       \           |                 /       \
    //         #2     /         \  #3      |         #2     /         \  #3
    // +----------------+      +---------+ | +----------------+      +---------+
    // |     foo()      |      |   ...   | | |DefinitelyCalled|      |NotCalled|
    // +----------------+      +---------+ | +----------------+      +---------+
    // |DefinitelyCalled|      |NotCalled| | |     foo()      |      |   ...   |
    // +----------------+      +---------+ | +----------------+      +---------+
    //                \         /          |                \         /
    //                 \  #4   /           |                 \  #4   /
    //               +-----------+         |                +---------+
    //               |    ...    |         |                |NotCalled|
    //               +-----------+         |                +---------+
    //               |MaybeCalled|         |                |   ...   |
    //               +-----------+         |                +---------+
    //
    // The most natural way to report lacking call in the block #3 would be to
    // message that the false branch of the if statement in the block #1 doesn't
    // have a call.  And while with the forward approach we'll need to find a
    // least common ancestor or something like that to find the 'if' to blame,
    // backward analysis gives it to us out of the box.
    BackwardDataflowWorklist Worklist(FunctionCFG, AC);

    // Let's visit EXIT.
    const CFGBlock *Exit = &FunctionCFG.getExit();
    assignState(Exit, State(size(), ParameterStatus::NotCalled));
    Worklist.enqueuePredecessors(Exit);

    while (const CFGBlock *BB = Worklist.dequeue()) {
      assert(BB && "Worklist should filter out null blocks");
      check(BB);
      assert(CurrentState.isVisited() &&
             "After the check, basic block should be visited");

      // Traverse successor basic blocks if the status of this block
      // has changed.
      if (assignState(BB, CurrentState)) {
        Worklist.enqueuePredecessors(BB);
      }
    }

    // Check that we have all tracked parameters at the last block.
    // As we are performing a backward version of the analysis,
    // it should be the ENTRY block.
    checkEntry(&FunctionCFG.getEntry());
  }

  void check(const CFGBlock *BB) {
    // We start with a state 'inherited' from all the successors.
    CurrentState = joinSuccessors(BB);
    assert(CurrentState.isVisited() &&
           "Shouldn't start with a 'not visited' state");

    // This is the 'exit' situation, broken promises are probably OK
    // in such scenarios.
    if (BB->hasNoReturnElement()) {
      markNoReturn();
      // This block still can have calls (even multiple calls) and
      // for this reason there is no early return here.
    }

    // We use a backward dataflow propagation and for this reason we
    // should traverse basic blocks bottom-up.
    for (const CFGElement &Element : llvm::reverse(*BB)) {
      if (std::optional<CFGStmt> S = Element.getAs<CFGStmt>()) {
        check(S->getStmt());
      }
    }
  }
  void check(const Stmt *S) { Visit(S); }

  void checkEntry(const CFGBlock *Entry) {
    // We finalize this algorithm with the ENTRY block because
    // we use a backward version of the analysis.  This is where
    // we can judge that some of the tracked parameters are not called on
    // every path from ENTRY to EXIT.

    const State &EntryStatus = getState(Entry);
    llvm::BitVector NotCalledOnEveryPath(size(), false);
    llvm::BitVector NotUsedOnEveryPath(size(), false);

    // Check if there are no calls of the marked parameter at all
    for (const auto &IndexedStatus : llvm::enumerate(EntryStatus)) {
      const ParmVarDecl *Parameter = getParameter(IndexedStatus.index());

      switch (IndexedStatus.value().getKind()) {
      case ParameterStatus::NotCalled:
        // If there were places where this parameter escapes (aka being used),
        // we can provide a more useful diagnostic by pointing at the exact
        // branches where it is not even mentioned.
        if (!hasEverEscaped(IndexedStatus.index())) {
          // This parameter is was not used at all, so we should report the
          // most generic version of the warning.
          if (isCaptured(Parameter)) {
            // We want to specify that it was captured by the block.
            Handler.handleCapturedNeverCalled(Parameter, AC.getDecl(),
                                              !isExplicitlyMarked(Parameter));
          } else {
            Handler.handleNeverCalled(Parameter,
                                      !isExplicitlyMarked(Parameter));
          }
        } else {
          // Mark it as 'interesting' to figure out which paths don't even
          // have escapes.
          NotUsedOnEveryPath[IndexedStatus.index()] = true;
        }

        break;
      case ParameterStatus::MaybeCalled:
        // If we have 'maybe called' at this point, we have an error
        // that there is at least one path where this parameter
        // is not called.
        //
        // However, reporting the warning with only that information can be
        // too vague for the users.  For this reason, we mark such parameters
        // as "interesting" for further analysis.
        NotCalledOnEveryPath[IndexedStatus.index()] = true;
        break;
      default:
        break;
      }
    }

    // Early exit if we don't have parameters for extra analysis...
    if (NotCalledOnEveryPath.none() && NotUsedOnEveryPath.none() &&
        // ... or if we've seen variables with cleanup functions.
        // We can't reason that we've seen every path in this case,
        // and thus abandon reporting any warnings that imply that.
        !FunctionHasCleanupVars)
      return;

    // We are looking for a pair of blocks A, B so that the following is true:
    //   * A is a predecessor of B
    //   * B is marked as NotCalled
    //   * A has at least one successor marked as either
    //     Escaped or DefinitelyCalled
    //
    // In that situation, it is guaranteed that B is the first block of the path
    // where the user doesn't call or use parameter in question.
    //
    // For this reason, branch A -> B can be used for reporting.
    //
    // This part of the algorithm is guarded by a condition that the function
    // does indeed have a violation of contract.  For this reason, we can
    // spend more time to find a good spot to place the warning.
    //
    // The following algorithm has the worst case complexity of O(V + E),
    // where V is the number of basic blocks in FunctionCFG,
    //       E is the number of edges between blocks in FunctionCFG.
    for (const CFGBlock *BB : FunctionCFG) {
      if (!BB)
        continue;

      const State &BlockState = getState(BB);

      for (unsigned Index : llvm::seq(0u, size())) {
        // We don't want to use 'isLosingCall' here because we want to report
        // the following situation as well:
        //
        //           MaybeCalled
        //            |  ...  |
        //    MaybeCalled   NotCalled
        //
        // Even though successor is not 'DefinitelyCalled', it is still useful
        // to report it, it is still a path without a call.
        if (NotCalledOnEveryPath[Index] &&
            BlockState.getKindFor(Index) == ParameterStatus::MaybeCalled) {

          findAndReportNotCalledBranches(BB, Index);
        } else if (NotUsedOnEveryPath[Index] &&
                   isLosingEscape(BlockState, BB, Index)) {

          findAndReportNotCalledBranches(BB, Index, /* IsEscape = */ true);
        }
      }
    }
  }

  /// Check potential call of a tracked parameter.
  void checkDirectCall(const CallExpr *Call) {
    if (auto Index = getIndexOfCallee(Call)) {
      processCallFor(*Index, Call);
    }
  }

  /// Check the call expression for being an indirect call of one of the tracked
  /// parameters.  It is indirect in the sense that this particular call is not
  /// calling the parameter itself, but rather uses it as the argument.
  template <class CallLikeExpr>
  void checkIndirectCall(const CallLikeExpr *CallOrMessage) {
    // CallExpr::arguments does not interact nicely with llvm::enumerate.
    llvm::ArrayRef<const Expr *> Arguments =
        llvm::ArrayRef(CallOrMessage->getArgs(), CallOrMessage->getNumArgs());

    // Let's check if any of the call arguments is a point of interest.
    for (const auto &Argument : llvm::enumerate(Arguments)) {
      if (auto Index = getIndexOfExpression(Argument.value())) {
        if (shouldBeCalledOnce(CallOrMessage, Argument.index())) {
          // If the corresponding parameter is marked as 'called_once' we should
          // consider it as a call.
          processCallFor(*Index, CallOrMessage);
        } else {
          // Otherwise, we mark this parameter as escaped, which can be
          // interpreted both as called or not called depending on the context.
          processEscapeFor(*Index);
        }
        // Otherwise, let's keep the state as it is.
      }
    }
  }

  /// Process call of the parameter with the given index
  void processCallFor(unsigned Index, const Expr *Call) {
    ParameterStatus &CurrentParamStatus = CurrentState.getStatusFor(Index);

    if (CurrentParamStatus.seenAnyCalls()) {

      // At this point, this parameter was called, so this is a second call.
      const ParmVarDecl *Parameter = getParameter(Index);
      Handler.handleDoubleCall(
          Parameter, &CurrentState.getCallFor(Index), Call,
          !isExplicitlyMarked(Parameter),
          // We are sure that the second call is definitely
          // going to happen if the status is 'DefinitelyCalled'.
          CurrentParamStatus.getKind() == ParameterStatus::DefinitelyCalled);

      // Mark this parameter as already reported on, so we don't repeat
      // warnings.
      CurrentParamStatus = ParameterStatus::Reported;

    } else if (CurrentParamStatus.getKind() != ParameterStatus::Reported) {
      // If we didn't report anything yet, let's mark this parameter
      // as called.
      ParameterStatus Called(ParameterStatus::DefinitelyCalled, Call);
      CurrentParamStatus = Called;
    }
  }

  /// Process escape of the parameter with the given index
  void processEscapeFor(unsigned Index) {
    ParameterStatus &CurrentParamStatus = CurrentState.getStatusFor(Index);

    // Escape overrides whatever error we think happened.
    if (CurrentParamStatus.isErrorStatus() &&
        CurrentParamStatus.getKind() != ParameterStatus::Kind::Reported) {
      CurrentParamStatus = ParameterStatus::Escaped;
    }
  }

  void findAndReportNotCalledBranches(const CFGBlock *Parent, unsigned Index,
                                      bool IsEscape = false) {
    for (const CFGBlock *Succ : Parent->succs()) {
      if (!Succ)
        continue;

      if (getState(Succ).getKindFor(Index) == ParameterStatus::NotCalled) {
        assert(Parent->succ_size() >= 2 &&
               "Block should have at least two successors at this point");
        if (auto Clarification = NotCalledClarifier::clarify(Parent, Succ)) {
          const ParmVarDecl *Parameter = getParameter(Index);
          Handler.handleNeverCalled(
              Parameter, AC.getDecl(), Clarification->Location,
              Clarification->Reason, !IsEscape, !isExplicitlyMarked(Parameter));
        }
      }
    }
  }

  //===----------------------------------------------------------------------===//
  //                   Predicate functions to check parameters
  //===----------------------------------------------------------------------===//

  /// Return true if parameter is explicitly marked as 'called_once'.
  static bool isExplicitlyMarked(const ParmVarDecl *Parameter) {
    return Parameter->hasAttr<CalledOnceAttr>();
  }

  /// Return true if the given name matches conventional pattens.
  static bool isConventional(llvm::StringRef Name) {
    return llvm::count(CONVENTIONAL_NAMES, Name) != 0;
  }

  /// Return true if the given name has conventional suffixes.
  static bool hasConventionalSuffix(llvm::StringRef Name) {
    return llvm::any_of(CONVENTIONAL_SUFFIXES, [Name](llvm::StringRef Suffix) {
      return Name.ends_with(Suffix);
    });
  }

  /// Return true if the given type can be used for conventional parameters.
  static bool isConventional(QualType Ty) {
    if (!Ty->isBlockPointerType()) {
      return false;
    }

    QualType BlockType = Ty->castAs<BlockPointerType>()->getPointeeType();
    // Completion handlers should have a block type with void return type.
    return BlockType->castAs<FunctionType>()->getReturnType()->isVoidType();
  }

  /// Return true if the only parameter of the function is conventional.
  static bool isOnlyParameterConventional(const FunctionDecl *Function) {
    IdentifierInfo *II = Function->getIdentifier();
    return Function->getNumParams() == 1 && II &&
           hasConventionalSuffix(II->getName());
  }

  /// Return true/false if 'swift_async' attribute states that the given
  /// parameter is conventionally called once.
  /// Return std::nullopt if the given declaration doesn't have 'swift_async'
  /// attribute.
  static std::optional<bool> isConventionalSwiftAsync(const Decl *D,
                                                      unsigned ParamIndex) {
    if (const SwiftAsyncAttr *A = D->getAttr<SwiftAsyncAttr>()) {
      if (A->getKind() == SwiftAsyncAttr::None) {
        return false;
      }

      return A->getCompletionHandlerIndex().getASTIndex() == ParamIndex;
    }
    return std::nullopt;
  }

  /// Return true if the specified selector represents init method.
  static bool isInitMethod(Selector MethodSelector) {
    return MethodSelector.getMethodFamily() == OMF_init;
  }

  /// Return true if the specified selector piece matches conventions.
  static bool isConventionalSelectorPiece(Selector MethodSelector,
                                          unsigned PieceIndex,
                                          QualType PieceType) {
    if (!isConventional(PieceType) || isInitMethod(MethodSelector)) {
      return false;
    }

    if (MethodSelector.getNumArgs() == 1) {
      assert(PieceIndex == 0);
      return hasConventionalSuffix(MethodSelector.getNameForSlot(0));
    }

    llvm::StringRef PieceName = MethodSelector.getNameForSlot(PieceIndex);
    return isConventional(PieceName) || hasConventionalSuffix(PieceName);
  }

  bool shouldBeCalledOnce(const ParmVarDecl *Parameter) const {
    return isExplicitlyMarked(Parameter) ||
           (CheckConventionalParameters &&
            (isConventional(Parameter->getName()) ||
             hasConventionalSuffix(Parameter->getName())) &&
            isConventional(Parameter->getType()));
  }

  bool shouldBeCalledOnce(const DeclContext *ParamContext,
                          const ParmVarDecl *Param) {
    unsigned ParamIndex = Param->getFunctionScopeIndex();
    if (const auto *Function = dyn_cast<FunctionDecl>(ParamContext)) {
      return shouldBeCalledOnce(Function, ParamIndex);
    }
    if (const auto *Method = dyn_cast<ObjCMethodDecl>(ParamContext)) {
      return shouldBeCalledOnce(Method, ParamIndex);
    }
    return shouldBeCalledOnce(Param);
  }

  bool shouldBeCalledOnce(const BlockDecl *Block, unsigned ParamIndex) const {
    return shouldBeCalledOnce(Block->getParamDecl(ParamIndex));
  }

  bool shouldBeCalledOnce(const FunctionDecl *Function,
                          unsigned ParamIndex) const {
    if (ParamIndex >= Function->getNumParams()) {
      return false;
    }
    // 'swift_async' goes first and overrides anything else.
    if (auto ConventionalAsync =
            isConventionalSwiftAsync(Function, ParamIndex)) {
      return *ConventionalAsync;
    }

    return shouldBeCalledOnce(Function->getParamDecl(ParamIndex)) ||
           (CheckConventionalParameters &&
            isOnlyParameterConventional(Function));
  }

  bool shouldBeCalledOnce(const ObjCMethodDecl *Method,
                          unsigned ParamIndex) const {
    Selector MethodSelector = Method->getSelector();
    if (ParamIndex >= MethodSelector.getNumArgs()) {
      return false;
    }

    // 'swift_async' goes first and overrides anything else.
    if (auto ConventionalAsync = isConventionalSwiftAsync(Method, ParamIndex)) {
      return *ConventionalAsync;
    }

    const ParmVarDecl *Parameter = Method->getParamDecl(ParamIndex);
    return shouldBeCalledOnce(Parameter) ||
           (CheckConventionalParameters &&
            isConventionalSelectorPiece(MethodSelector, ParamIndex,
                                        Parameter->getType()));
  }

  bool shouldBeCalledOnce(const CallExpr *Call, unsigned ParamIndex) const {
    const FunctionDecl *Function = Call->getDirectCallee();
    return Function && shouldBeCalledOnce(Function, ParamIndex);
  }

  bool shouldBeCalledOnce(const ObjCMessageExpr *Message,
                          unsigned ParamIndex) const {
    const ObjCMethodDecl *Method = Message->getMethodDecl();
    return Method && ParamIndex < Method->param_size() &&
           shouldBeCalledOnce(Method, ParamIndex);
  }

  //===----------------------------------------------------------------------===//
  //                               Utility methods
  //===----------------------------------------------------------------------===//

  bool isCaptured(const ParmVarDecl *Parameter) const {
    if (const BlockDecl *Block = dyn_cast<BlockDecl>(AC.getDecl())) {
      return Block->capturesVariable(Parameter);
    }
    return false;
  }

  // Return a call site where the block is called exactly once or null otherwise
  const Expr *getBlockGuaraneedCallSite(const BlockExpr *Block) const {
    ParentMap &PM = AC.getParentMap();

    // We don't want to track the block through assignments and so on, instead
    // we simply see how the block used and if it's used directly in a call,
    // we decide based on call to what it is.
    //
    // In order to do this, we go up the parents of the block looking for
    // a call or a message expressions.  These might not be immediate parents
    // of the actual block expression due to casts and parens, so we skip them.
    for (const Stmt *Prev = Block, *Current = PM.getParent(Block);
         Current != nullptr; Prev = Current, Current = PM.getParent(Current)) {
      // Skip no-op (for our case) operations.
      if (isa<CastExpr>(Current) || isa<ParenExpr>(Current))
        continue;

      // At this point, Prev represents our block as an immediate child of the
      // call.
      if (const auto *Call = dyn_cast<CallExpr>(Current)) {
        // It might be the call of the Block itself...
        if (Call->getCallee() == Prev)
          return Call;

        // ...or it can be an indirect call of the block.
        return shouldBlockArgumentBeCalledOnce(Call, Prev) ? Call : nullptr;
      }
      if (const auto *Message = dyn_cast<ObjCMessageExpr>(Current)) {
        return shouldBlockArgumentBeCalledOnce(Message, Prev) ? Message
                                                              : nullptr;
      }

      break;
    }

    return nullptr;
  }

  template <class CallLikeExpr>
  bool shouldBlockArgumentBeCalledOnce(const CallLikeExpr *CallOrMessage,
                                       const Stmt *BlockArgument) const {
    // CallExpr::arguments does not interact nicely with llvm::enumerate.
    llvm::ArrayRef<const Expr *> Arguments =
        llvm::ArrayRef(CallOrMessage->getArgs(), CallOrMessage->getNumArgs());

    for (const auto &Argument : llvm::enumerate(Arguments)) {
      if (Argument.value() == BlockArgument) {
        return shouldBlockArgumentBeCalledOnce(CallOrMessage, Argument.index());
      }
    }

    return false;
  }

  bool shouldBlockArgumentBeCalledOnce(const CallExpr *Call,
                                       unsigned ParamIndex) const {
    const FunctionDecl *Function = Call->getDirectCallee();
    return shouldBlockArgumentBeCalledOnce(Function, ParamIndex) ||
           shouldBeCalledOnce(Call, ParamIndex);
  }

  bool shouldBlockArgumentBeCalledOnce(const ObjCMessageExpr *Message,
                                       unsigned ParamIndex) const {
    // At the moment, we don't have any Obj-C methods we want to specifically
    // check in here.
    return shouldBeCalledOnce(Message, ParamIndex);
  }

  static bool shouldBlockArgumentBeCalledOnce(const FunctionDecl *Function,
                                              unsigned ParamIndex) {
    // There is a list of important API functions that while not following
    // conventions nor being directly annotated, still guarantee that the
    // callback parameter will be called exactly once.
    //
    // Here we check if this is the case.
    return Function &&
           llvm::any_of(KNOWN_CALLED_ONCE_PARAMETERS,
                        [Function, ParamIndex](
                            const KnownCalledOnceParameter &Reference) {
                          return Reference.FunctionName ==
                                     Function->getName() &&
                                 Reference.ParamIndex == ParamIndex;
                        });
  }

  /// Return true if the analyzed function is actually a default implementation
  /// of the method that has to be overriden.
  ///
  /// These functions can have tracked parameters, but wouldn't call them
  /// because they are not designed to perform any meaningful actions.
  ///
  /// There are a couple of flavors of such default implementations:
  ///   1. Empty methods or methods with a single return statement
  ///   2. Methods that have one block with a call to no return function
  ///   3. Methods with only assertion-like operations
  bool isPossiblyEmptyImpl() const {
    if (!isa<ObjCMethodDecl>(AC.getDecl())) {
      // We care only about functions that are not supposed to be called.
      // Only methods can be overriden.
      return false;
    }

    // Case #1 (without return statements)
    if (FunctionCFG.size() == 2) {
      // Method has only two blocks: ENTRY and EXIT.
      // This is equivalent to empty function.
      return true;
    }

    // Case #2
    if (FunctionCFG.size() == 3) {
      const CFGBlock &Entry = FunctionCFG.getEntry();
      if (Entry.succ_empty()) {
        return false;
      }

      const CFGBlock *OnlyBlock = *Entry.succ_begin();
      // Method has only one block, let's see if it has a no-return
      // element.
      if (OnlyBlock && OnlyBlock->hasNoReturnElement()) {
        return true;
      }
      // Fallthrough, CFGs with only one block can fall into #1 and #3 as well.
    }

    // Cases #1 (return statements) and #3.
    //
    // It is hard to detect that something is an assertion or came
    // from assertion.  Here we use a simple heuristic:
    //
    //   - If it came from a macro, it can be an assertion.
    //
    // Additionally, we can't assume a number of basic blocks or the CFG's
    // structure because assertions might include loops and conditions.
    return llvm::all_of(FunctionCFG, [](const CFGBlock *BB) {
      if (!BB) {
        // Unreachable blocks are totally fine.
        return true;
      }

      // Return statements can have sub-expressions that are represented as
      // separate statements of a basic block.  We should allow this.
      // This parent map will be initialized with a parent tree for all
      // subexpressions of the block's return statement (if it has one).
      std::unique_ptr<ParentMap> ReturnChildren;

      return llvm::all_of(
          llvm::reverse(*BB), // we should start with return statements, if we
                              // have any, i.e. from the bottom of the block
          [&ReturnChildren](const CFGElement &Element) {
            if (std::optional<CFGStmt> S = Element.getAs<CFGStmt>()) {
              const Stmt *SuspiciousStmt = S->getStmt();

              if (isa<ReturnStmt>(SuspiciousStmt)) {
                // Let's initialize this structure to test whether
                // some further statement is a part of this return.
                ReturnChildren = std::make_unique<ParentMap>(
                    const_cast<Stmt *>(SuspiciousStmt));
                // Return statements are allowed as part of #1.
                return true;
              }

              return SuspiciousStmt->getBeginLoc().isMacroID() ||
                     (ReturnChildren &&
                      ReturnChildren->hasParent(SuspiciousStmt));
            }
            return true;
          });
    });
  }

  /// Check if parameter with the given index has ever escaped.
  bool hasEverEscaped(unsigned Index) const {
    return llvm::any_of(States, [Index](const State &StateForOneBB) {
      return StateForOneBB.getKindFor(Index) == ParameterStatus::Escaped;
    });
  }

  /// Return status stored for the given basic block.
  /// \{
  State &getState(const CFGBlock *BB) {
    assert(BB);
    return States[BB->getBlockID()];
  }
  const State &getState(const CFGBlock *BB) const {
    assert(BB);
    return States[BB->getBlockID()];
  }
  /// \}

  /// Assign status to the given basic block.
  ///
  /// Returns true when the stored status changed.
  bool assignState(const CFGBlock *BB, const State &ToAssign) {
    State &Current = getState(BB);
    if (Current == ToAssign) {
      return false;
    }

    Current = ToAssign;
    return true;
  }

  /// Join all incoming statuses for the given basic block.
  State joinSuccessors(const CFGBlock *BB) const {
    auto Succs =
        llvm::make_filter_range(BB->succs(), [this](const CFGBlock *Succ) {
          return Succ && this->getState(Succ).isVisited();
        });
    // We came to this block from somewhere after all.
    assert(!Succs.empty() &&
           "Basic block should have at least one visited successor");

    State Result = getState(*Succs.begin());

    for (const CFGBlock *Succ : llvm::drop_begin(Succs, 1)) {
      Result.join(getState(Succ));
    }

    if (const Expr *Condition = getCondition(BB->getTerminatorStmt())) {
      handleConditional(BB, Condition, Result);
    }

    return Result;
  }

  void handleConditional(const CFGBlock *BB, const Expr *Condition,
                         State &ToAlter) const {
    handleParameterCheck(BB, Condition, ToAlter);
    if (SuppressOnConventionalErrorPaths) {
      handleConventionalCheck(BB, Condition, ToAlter);
    }
  }

  void handleParameterCheck(const CFGBlock *BB, const Expr *Condition,
                            State &ToAlter) const {
    // In this function, we try to deal with the following pattern:
    //
    //   if (parameter)
    //     parameter(...);
    //
    // It's not good to show a warning here because clearly 'parameter'
    // couldn't and shouldn't be called on the 'else' path.
    //
    // Let's check if this if statement has a check involving one of
    // the tracked parameters.
    if (const ParmVarDecl *Parameter = findReferencedParmVarDecl(
            Condition,
            /* ShouldRetrieveFromComparisons = */ true)) {
      if (const auto Index = getIndex(*Parameter)) {
        ParameterStatus &CurrentStatus = ToAlter.getStatusFor(*Index);

        // We don't want to deep dive into semantics of the check and
        // figure out if that check was for null or something else.
        // We simply trust the user that they know what they are doing.
        //
        // For this reason, in the following loop we look for the
        // best-looking option.
        for (const CFGBlock *Succ : BB->succs()) {
          if (!Succ)
            continue;

          const ParameterStatus &StatusInSucc =
              getState(Succ).getStatusFor(*Index);

          if (StatusInSucc.isErrorStatus()) {
            continue;
          }

          // Let's use this status instead.
          CurrentStatus = StatusInSucc;

          if (StatusInSucc.getKind() == ParameterStatus::DefinitelyCalled) {
            // This is the best option to have and we already found it.
            break;
          }

          // If we found 'Escaped' first, we still might find 'DefinitelyCalled'
          // on the other branch.  And we prefer the latter.
        }
      }
    }
  }

  void handleConventionalCheck(const CFGBlock *BB, const Expr *Condition,
                               State &ToAlter) const {
    // Even when the analysis is technically correct, it is a widespread pattern
    // not to call completion handlers in some scenarios.  These usually have
    // typical conditional names, such as 'error' or 'cancel'.
    if (!mentionsAnyOfConventionalNames(Condition)) {
      return;
    }

    for (const auto &IndexedStatus : llvm::enumerate(ToAlter)) {
      const ParmVarDecl *Parameter = getParameter(IndexedStatus.index());
      // Conventions do not apply to explicitly marked parameters.
      if (isExplicitlyMarked(Parameter)) {
        continue;
      }

      ParameterStatus &CurrentStatus = IndexedStatus.value();
      // If we did find that on one of the branches the user uses the callback
      // and doesn't on the other path, we believe that they know what they are
      // doing and trust them.
      //
      // There are two possible scenarios for that:
      //   1. Current status is 'MaybeCalled' and one of the branches is
      //      'DefinitelyCalled'
      //   2. Current status is 'NotCalled' and one of the branches is 'Escaped'
      if (isLosingCall(ToAlter, BB, IndexedStatus.index()) ||
          isLosingEscape(ToAlter, BB, IndexedStatus.index())) {
        CurrentStatus = ParameterStatus::Escaped;
      }
    }
  }

  bool isLosingCall(const State &StateAfterJoin, const CFGBlock *JoinBlock,
                    unsigned ParameterIndex) const {
    // Let's check if the block represents DefinitelyCalled -> MaybeCalled
    // transition.
    return isLosingJoin(StateAfterJoin, JoinBlock, ParameterIndex,
                        ParameterStatus::MaybeCalled,
                        ParameterStatus::DefinitelyCalled);
  }

  bool isLosingEscape(const State &StateAfterJoin, const CFGBlock *JoinBlock,
                      unsigned ParameterIndex) const {
    // Let's check if the block represents Escaped -> NotCalled transition.
    return isLosingJoin(StateAfterJoin, JoinBlock, ParameterIndex,
                        ParameterStatus::NotCalled, ParameterStatus::Escaped);
  }

  bool isLosingJoin(const State &StateAfterJoin, const CFGBlock *JoinBlock,
                    unsigned ParameterIndex, ParameterStatus::Kind AfterJoin,
                    ParameterStatus::Kind BeforeJoin) const {
    assert(!ParameterStatus::isErrorStatus(BeforeJoin) &&
           ParameterStatus::isErrorStatus(AfterJoin) &&
           "It's not a losing join if statuses do not represent "
           "correct-to-error transition");

    const ParameterStatus &CurrentStatus =
        StateAfterJoin.getStatusFor(ParameterIndex);

    return CurrentStatus.getKind() == AfterJoin &&
           anySuccessorHasStatus(JoinBlock, ParameterIndex, BeforeJoin);
  }

  /// Return true if any of the successors of the given basic block has
  /// a specified status for the given parameter.
  bool anySuccessorHasStatus(const CFGBlock *Parent, unsigned ParameterIndex,
                             ParameterStatus::Kind ToFind) const {
    return llvm::any_of(
        Parent->succs(), [this, ParameterIndex, ToFind](const CFGBlock *Succ) {
          return Succ && getState(Succ).getKindFor(ParameterIndex) == ToFind;
        });
  }

  /// Check given expression that was discovered to escape.
  void checkEscapee(const Expr *E) {
    if (const ParmVarDecl *Parameter = findReferencedParmVarDecl(E)) {
      checkEscapee(*Parameter);
    }
  }

  /// Check given parameter that was discovered to escape.
  void checkEscapee(const ParmVarDecl &Parameter) {
    if (auto Index = getIndex(Parameter)) {
      processEscapeFor(*Index);
    }
  }

  /// Mark all parameters in the current state as 'no-return'.
  void markNoReturn() {
    for (ParameterStatus &PS : CurrentState) {
      PS = ParameterStatus::NoReturn;
    }
  }

  /// Check if the given assignment represents suppression and act on it.
  void checkSuppression(const BinaryOperator *Assignment) {
    // Suppression has the following form:
    //    parameter = 0;
    // 0 can be of any form (NULL, nil, etc.)
    if (auto Index = getIndexOfExpression(Assignment->getLHS())) {

      // We don't care what is written in the RHS, it could be whatever
      // we can interpret as 0.
      if (auto Constant =
              Assignment->getRHS()->IgnoreParenCasts()->getIntegerConstantExpr(
                  AC.getASTContext())) {

        ParameterStatus &CurrentParamStatus = CurrentState.getStatusFor(*Index);

        if (0 == *Constant && CurrentParamStatus.seenAnyCalls()) {
          // Even though this suppression mechanism is introduced to tackle
          // false positives for multiple calls, the fact that the user has
          // to use suppression can also tell us that we couldn't figure out
          // how different paths cancel each other out.  And if that is true,
          // we will most certainly have false positives about parameters not
          // being called on certain paths.
          //
          // For this reason, we abandon tracking this parameter altogether.
          CurrentParamStatus = ParameterStatus::Reported;
        }
      }
    }
  }

public:
  //===----------------------------------------------------------------------===//
  //                            Tree traversal methods
  //===----------------------------------------------------------------------===//

  void VisitCallExpr(const CallExpr *Call) {
    // This call might be a direct call, i.e. a parameter call...
    checkDirectCall(Call);
    // ... or an indirect call, i.e. when parameter is an argument.
    checkIndirectCall(Call);
  }

  void VisitObjCMessageExpr(const ObjCMessageExpr *Message) {
    // The most common situation that we are defending against here is
    // copying a tracked parameter.
    if (const Expr *Receiver = Message->getInstanceReceiver()) {
      checkEscapee(Receiver);
    }
    // Message expressions unlike calls, could not be direct.
    checkIndirectCall(Message);
  }

  void VisitBlockExpr(const BlockExpr *Block) {
    // Block expressions are tricky.  It is a very common practice to capture
    // completion handlers by blocks and use them there.
    // For this reason, it is important to analyze blocks and report warnings
    // for completion handler misuse in blocks.
    //
    // However, it can be quite difficult to track how the block itself is being
    // used.  The full precise anlysis of that will be similar to alias analysis
    // for completion handlers and can be too heavyweight for a compile-time
    // diagnostic.  Instead, we judge about the immediate use of the block.
    //
    // Here, we try to find a call expression where we know due to conventions,
    // annotations, or other reasons that the block is called once and only
    // once.
    const Expr *CalledOnceCallSite = getBlockGuaraneedCallSite(Block);

    // We need to report this information to the handler because in the
    // situation when we know that the block is called exactly once, we can be
    // stricter in terms of reported diagnostics.
    if (CalledOnceCallSite) {
      Handler.handleBlockThatIsGuaranteedToBeCalledOnce(Block->getBlockDecl());
    } else {
      Handler.handleBlockWithNoGuarantees(Block->getBlockDecl());
    }

    for (const auto &Capture : Block->getBlockDecl()->captures()) {
      if (const auto *Param = dyn_cast<ParmVarDecl>(Capture.getVariable())) {
        if (auto Index = getIndex(*Param)) {
          if (CalledOnceCallSite) {
            // The call site of a block can be considered a call site of the
            // captured parameter we track.
            processCallFor(*Index, CalledOnceCallSite);
          } else {
            // We still should consider this block as an escape for parameter,
            // if we don't know about its call site or the number of time it
            // can be invoked.
            processEscapeFor(*Index);
          }
        }
      }
    }
  }

  void VisitBinaryOperator(const BinaryOperator *Op) {
    if (Op->getOpcode() == clang::BO_Assign) {
      // Let's check if one of the tracked parameters is assigned into
      // something, and if it is we don't want to track extra variables, so we
      // consider it as an escapee.
      checkEscapee(Op->getRHS());

      // Let's check whether this assignment is a suppression.
      checkSuppression(Op);
    }
  }

  void VisitDeclStmt(const DeclStmt *DS) {
    // Variable initialization is not assignment and should be handled
    // separately.
    //
    // Multiple declarations can be a part of declaration statement.
    for (const auto *Declaration : DS->getDeclGroup()) {
      if (const auto *Var = dyn_cast<VarDecl>(Declaration)) {
        if (Var->getInit()) {
          checkEscapee(Var->getInit());
        }

        if (Var->hasAttr<CleanupAttr>()) {
          FunctionHasCleanupVars = true;
        }
      }
    }
  }

  void VisitCStyleCastExpr(const CStyleCastExpr *Cast) {
    // We consider '(void)parameter' as a manual no-op escape.
    // It should be used to explicitly tell the analysis that this parameter
    // is intentionally not called on this path.
    if (Cast->getType().getCanonicalType()->isVoidType()) {
      checkEscapee(Cast->getSubExpr());
    }
  }

  void VisitObjCAtThrowStmt(const ObjCAtThrowStmt *) {
    // It is OK not to call marked parameters on exceptional paths.
    markNoReturn();
  }

private:
  unsigned size() const { return TrackedParams.size(); }

  std::optional<unsigned> getIndexOfCallee(const CallExpr *Call) const {
    return getIndexOfExpression(Call->getCallee());
  }

  std::optional<unsigned> getIndexOfExpression(const Expr *E) const {
    if (const ParmVarDecl *Parameter = findReferencedParmVarDecl(E)) {
      return getIndex(*Parameter);
    }

    return std::nullopt;
  }

  std::optional<unsigned> getIndex(const ParmVarDecl &Parameter) const {
    // Expected number of parameters that we actually track is 1.
    //
    // Also, the maximum number of declared parameters could not be on a scale
    // of hundreds of thousands.
    //
    // In this setting, linear search seems reasonable and even performs better
    // than bisection.
    ParamSizedVector<const ParmVarDecl *>::const_iterator It =
        llvm::find(TrackedParams, &Parameter);

    if (It != TrackedParams.end()) {
      return It - TrackedParams.begin();
    }

    return std::nullopt;
  }

  const ParmVarDecl *getParameter(unsigned Index) const {
    assert(Index < TrackedParams.size());
    return TrackedParams[Index];
  }

  const CFG &FunctionCFG;
  AnalysisDeclContext &AC;
  CalledOnceCheckHandler &Handler;
  bool CheckConventionalParameters;
  // As of now, we turn this behavior off.  So, we still are going to report
  // missing calls on paths that look like it was intentional.
  // Technically such reports are true positives, but they can make some users
  // grumpy because of the sheer number of warnings.
  // It can be turned back on if we decide that we want to have the other way
  // around.
  bool SuppressOnConventionalErrorPaths = false;

  // The user can annotate variable declarations with cleanup functions, which
  // essentially imposes a custom destructor logic on that variable.
  // It is possible to use it, however, to call tracked parameters on all exits
  // from the function.  For this reason, we track the fact that the function
  // actually has these.
  bool FunctionHasCleanupVars = false;

  State CurrentState;
  ParamSizedVector<const ParmVarDecl *> TrackedParams;
  CFGSizedVector<State> States;
};

} // end anonymous namespace

namespace clang {
void checkCalledOnceParameters(AnalysisDeclContext &AC,
                               CalledOnceCheckHandler &Handler,
                               bool CheckConventionalParameters) {
  CalledOnceChecker::check(AC, Handler, CheckConventionalParameters);
}
} // end namespace clang
