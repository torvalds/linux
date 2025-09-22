//===-- DataflowEnvironment.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines an Environment class that is used by dataflow analyses
//  that run over Control-Flow Graphs (CFGs) to keep track of the state of the
//  program at given program points.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/FlowSensitive/ASTOps.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysisContext.h"
#include "clang/Analysis/FlowSensitive/DataflowLattice.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>

#define DEBUG_TYPE "dataflow"

namespace clang {
namespace dataflow {

// FIXME: convert these to parameters of the analysis or environment. Current
// settings have been experimentaly validated, but only for a particular
// analysis.
static constexpr int MaxCompositeValueDepth = 3;
static constexpr int MaxCompositeValueSize = 1000;

/// Returns a map consisting of key-value entries that are present in both maps.
static llvm::DenseMap<const ValueDecl *, StorageLocation *> intersectDeclToLoc(
    const llvm::DenseMap<const ValueDecl *, StorageLocation *> &DeclToLoc1,
    const llvm::DenseMap<const ValueDecl *, StorageLocation *> &DeclToLoc2) {
  llvm::DenseMap<const ValueDecl *, StorageLocation *> Result;
  for (auto &Entry : DeclToLoc1) {
    auto It = DeclToLoc2.find(Entry.first);
    if (It != DeclToLoc2.end() && Entry.second == It->second)
      Result.insert({Entry.first, Entry.second});
  }
  return Result;
}

// Performs a join on either `ExprToLoc` or `ExprToVal`.
// The maps must be consistent in the sense that any entries for the same
// expression must map to the same location / value. This is the case if we are
// performing a join for control flow within a full-expression (which is the
// only case when this function should be used).
template <typename MapT> MapT joinExprMaps(const MapT &Map1, const MapT &Map2) {
  MapT Result = Map1;

  for (const auto &Entry : Map2) {
    [[maybe_unused]] auto [It, Inserted] = Result.insert(Entry);
    // If there was an existing entry, its value should be the same as for the
    // entry we were trying to insert.
    assert(It->second == Entry.second);
  }

  return Result;
}

// Whether to consider equivalent two values with an unknown relation.
//
// FIXME: this function is a hack enabling unsoundness to support
// convergence. Once we have widening support for the reference/pointer and
// struct built-in models, this should be unconditionally `false` (and inlined
// as such at its call sites).
static bool equateUnknownValues(Value::Kind K) {
  switch (K) {
  case Value::Kind::Integer:
  case Value::Kind::Pointer:
    return true;
  default:
    return false;
  }
}

static bool compareDistinctValues(QualType Type, Value &Val1,
                                  const Environment &Env1, Value &Val2,
                                  const Environment &Env2,
                                  Environment::ValueModel &Model) {
  // Note: Potentially costly, but, for booleans, we could check whether both
  // can be proven equivalent in their respective environments.

  // FIXME: move the reference/pointers logic from `areEquivalentValues` to here
  // and implement separate, join/widen specific handling for
  // reference/pointers.
  switch (Model.compare(Type, Val1, Env1, Val2, Env2)) {
  case ComparisonResult::Same:
    return true;
  case ComparisonResult::Different:
    return false;
  case ComparisonResult::Unknown:
    return equateUnknownValues(Val1.getKind());
  }
  llvm_unreachable("All cases covered in switch");
}

/// Attempts to join distinct values `Val1` and `Val2` in `Env1` and `Env2`,
/// respectively, of the same type `Type`. Joining generally produces a single
/// value that (soundly) approximates the two inputs, although the actual
/// meaning depends on `Model`.
static Value *joinDistinctValues(QualType Type, Value &Val1,
                                 const Environment &Env1, Value &Val2,
                                 const Environment &Env2,
                                 Environment &JoinedEnv,
                                 Environment::ValueModel &Model) {
  // Join distinct boolean values preserving information about the constraints
  // in the respective path conditions.
  if (isa<BoolValue>(&Val1) && isa<BoolValue>(&Val2)) {
    // FIXME: Checking both values should be unnecessary, since they should have
    // a consistent shape.  However, right now we can end up with BoolValue's in
    // integer-typed variables due to our incorrect handling of
    // boolean-to-integer casts (we just propagate the BoolValue to the result
    // of the cast). So, a join can encounter an integer in one branch but a
    // bool in the other.
    // For example:
    // ```
    // std::optional<bool> o;
    // int x;
    // if (o.has_value())
    //   x = o.value();
    // ```
    auto &Expr1 = cast<BoolValue>(Val1).formula();
    auto &Expr2 = cast<BoolValue>(Val2).formula();
    auto &A = JoinedEnv.arena();
    auto &JoinedVal = A.makeAtomRef(A.makeAtom());
    JoinedEnv.assume(
        A.makeOr(A.makeAnd(A.makeAtomRef(Env1.getFlowConditionToken()),
                           A.makeEquals(JoinedVal, Expr1)),
                 A.makeAnd(A.makeAtomRef(Env2.getFlowConditionToken()),
                           A.makeEquals(JoinedVal, Expr2))));
    return &A.makeBoolValue(JoinedVal);
  }

  Value *JoinedVal = JoinedEnv.createValue(Type);
  if (JoinedVal)
    Model.join(Type, Val1, Env1, Val2, Env2, *JoinedVal, JoinedEnv);

  return JoinedVal;
}

static WidenResult widenDistinctValues(QualType Type, Value &Prev,
                                       const Environment &PrevEnv,
                                       Value &Current, Environment &CurrentEnv,
                                       Environment::ValueModel &Model) {
  // Boolean-model widening.
  if (isa<BoolValue>(Prev) && isa<BoolValue>(Current)) {
    // FIXME: Checking both values should be unnecessary, but we can currently
    // end up with `BoolValue`s in integer-typed variables. See comment in
    // `joinDistinctValues()` for details.
    auto &PrevBool = cast<BoolValue>(Prev);
    auto &CurBool = cast<BoolValue>(Current);

    if (isa<TopBoolValue>(Prev))
      // Safe to return `Prev` here, because Top is never dependent on the
      // environment.
      return {&Prev, LatticeEffect::Unchanged};

    // We may need to widen to Top, but before we do so, check whether both
    // values are implied to be either true or false in the current environment.
    // In that case, we can simply return a literal instead.
    bool TruePrev = PrevEnv.proves(PrevBool.formula());
    bool TrueCur = CurrentEnv.proves(CurBool.formula());
    if (TruePrev && TrueCur)
      return {&CurrentEnv.getBoolLiteralValue(true), LatticeEffect::Unchanged};
    if (!TruePrev && !TrueCur &&
        PrevEnv.proves(PrevEnv.arena().makeNot(PrevBool.formula())) &&
        CurrentEnv.proves(CurrentEnv.arena().makeNot(CurBool.formula())))
      return {&CurrentEnv.getBoolLiteralValue(false), LatticeEffect::Unchanged};

    return {&CurrentEnv.makeTopBoolValue(), LatticeEffect::Changed};
  }

  // FIXME: Add other built-in model widening.

  // Custom-model widening.
  if (auto Result = Model.widen(Type, Prev, PrevEnv, Current, CurrentEnv))
    return *Result;

  return {&Current, equateUnknownValues(Prev.getKind())
                        ? LatticeEffect::Unchanged
                        : LatticeEffect::Changed};
}

// Returns whether the values in `Map1` and `Map2` compare equal for those
// keys that `Map1` and `Map2` have in common.
template <typename Key>
bool compareKeyToValueMaps(const llvm::MapVector<Key, Value *> &Map1,
                           const llvm::MapVector<Key, Value *> &Map2,
                           const Environment &Env1, const Environment &Env2,
                           Environment::ValueModel &Model) {
  for (auto &Entry : Map1) {
    Key K = Entry.first;
    assert(K != nullptr);

    Value *Val = Entry.second;
    assert(Val != nullptr);

    auto It = Map2.find(K);
    if (It == Map2.end())
      continue;
    assert(It->second != nullptr);

    if (!areEquivalentValues(*Val, *It->second) &&
        !compareDistinctValues(K->getType(), *Val, Env1, *It->second, Env2,
                               Model))
      return false;
  }

  return true;
}

// Perform a join on two `LocToVal` maps.
static llvm::MapVector<const StorageLocation *, Value *>
joinLocToVal(const llvm::MapVector<const StorageLocation *, Value *> &LocToVal,
             const llvm::MapVector<const StorageLocation *, Value *> &LocToVal2,
             const Environment &Env1, const Environment &Env2,
             Environment &JoinedEnv, Environment::ValueModel &Model) {
  llvm::MapVector<const StorageLocation *, Value *> Result;
  for (auto &Entry : LocToVal) {
    const StorageLocation *Loc = Entry.first;
    assert(Loc != nullptr);

    Value *Val = Entry.second;
    assert(Val != nullptr);

    auto It = LocToVal2.find(Loc);
    if (It == LocToVal2.end())
      continue;
    assert(It->second != nullptr);

    if (Value *JoinedVal = Environment::joinValues(
            Loc->getType(), Val, Env1, It->second, Env2, JoinedEnv, Model)) {
      Result.insert({Loc, JoinedVal});
    }
  }

  return Result;
}

// Perform widening on either `LocToVal` or `ExprToVal`. `Key` must be either
// `const StorageLocation *` or `const Expr *`.
template <typename Key>
llvm::MapVector<Key, Value *>
widenKeyToValueMap(const llvm::MapVector<Key, Value *> &CurMap,
                   const llvm::MapVector<Key, Value *> &PrevMap,
                   Environment &CurEnv, const Environment &PrevEnv,
                   Environment::ValueModel &Model, LatticeEffect &Effect) {
  llvm::MapVector<Key, Value *> WidenedMap;
  for (auto &Entry : CurMap) {
    Key K = Entry.first;
    assert(K != nullptr);

    Value *Val = Entry.second;
    assert(Val != nullptr);

    auto PrevIt = PrevMap.find(K);
    if (PrevIt == PrevMap.end())
      continue;
    assert(PrevIt->second != nullptr);

    if (areEquivalentValues(*Val, *PrevIt->second)) {
      WidenedMap.insert({K, Val});
      continue;
    }

    auto [WidenedVal, ValEffect] = widenDistinctValues(
        K->getType(), *PrevIt->second, PrevEnv, *Val, CurEnv, Model);
    WidenedMap.insert({K, WidenedVal});
    if (ValEffect == LatticeEffect::Changed)
      Effect = LatticeEffect::Changed;
  }

  return WidenedMap;
}

namespace {

// Visitor that builds a map from record prvalues to result objects.
// For each result object that it encounters, it propagates the storage location
// of the result object to all record prvalues that can initialize it.
class ResultObjectVisitor : public AnalysisASTVisitor<ResultObjectVisitor> {
public:
  // `ResultObjectMap` will be filled with a map from record prvalues to result
  // object. If this visitor will traverse a function that returns a record by
  // value, `LocForRecordReturnVal` is the location to which this record should
  // be written; otherwise, it is null.
  explicit ResultObjectVisitor(
      llvm::DenseMap<const Expr *, RecordStorageLocation *> &ResultObjectMap,
      RecordStorageLocation *LocForRecordReturnVal,
      DataflowAnalysisContext &DACtx)
      : ResultObjectMap(ResultObjectMap),
        LocForRecordReturnVal(LocForRecordReturnVal), DACtx(DACtx) {}

  // Traverse all member and base initializers of `Ctor`. This function is not
  // called by `RecursiveASTVisitor`; it should be called manually if we are
  // analyzing a constructor. `ThisPointeeLoc` is the storage location that
  // `this` points to.
  void TraverseConstructorInits(const CXXConstructorDecl *Ctor,
                                RecordStorageLocation *ThisPointeeLoc) {
    assert(ThisPointeeLoc != nullptr);
    for (const CXXCtorInitializer *Init : Ctor->inits()) {
      Expr *InitExpr = Init->getInit();
      if (FieldDecl *Field = Init->getMember();
          Field != nullptr && Field->getType()->isRecordType()) {
        PropagateResultObject(InitExpr, cast<RecordStorageLocation>(
                                            ThisPointeeLoc->getChild(*Field)));
      } else if (Init->getBaseClass()) {
        PropagateResultObject(InitExpr, ThisPointeeLoc);
      }

      // Ensure that any result objects within `InitExpr` (e.g. temporaries)
      // are also propagated to the prvalues that initialize them.
      TraverseStmt(InitExpr);

      // If this is a `CXXDefaultInitExpr`, also propagate any result objects
      // within the default expression.
      if (auto *DefaultInit = dyn_cast<CXXDefaultInitExpr>(InitExpr))
        TraverseStmt(DefaultInit->getExpr());
    }
  }

  bool VisitVarDecl(VarDecl *VD) {
    if (VD->getType()->isRecordType() && VD->hasInit())
      PropagateResultObject(
          VD->getInit(),
          &cast<RecordStorageLocation>(DACtx.getStableStorageLocation(*VD)));
    return true;
  }

  bool VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *MTE) {
    if (MTE->getType()->isRecordType())
      PropagateResultObject(
          MTE->getSubExpr(),
          &cast<RecordStorageLocation>(DACtx.getStableStorageLocation(*MTE)));
    return true;
  }

  bool VisitReturnStmt(ReturnStmt *Return) {
    Expr *RetValue = Return->getRetValue();
    if (RetValue != nullptr && RetValue->getType()->isRecordType() &&
        RetValue->isPRValue())
      PropagateResultObject(RetValue, LocForRecordReturnVal);
    return true;
  }

  bool VisitExpr(Expr *E) {
    // Clang's AST can have record-type prvalues without a result object -- for
    // example as full-expressions contained in a compound statement or as
    // arguments of call expressions. We notice this if we get here and a
    // storage location has not yet been associated with `E`. In this case,
    // treat this as if it was a `MaterializeTemporaryExpr`.
    if (E->isPRValue() && E->getType()->isRecordType() &&
        !ResultObjectMap.contains(E))
      PropagateResultObject(
          E, &cast<RecordStorageLocation>(DACtx.getStableStorageLocation(*E)));
    return true;
  }

  void
  PropagateResultObjectToRecordInitList(const RecordInitListHelper &InitList,
                                        RecordStorageLocation *Loc) {
    for (auto [Base, Init] : InitList.base_inits()) {
      assert(Base->getType().getCanonicalType() ==
             Init->getType().getCanonicalType());

      // Storage location for the base class is the same as that of the
      // derived class because we "flatten" the object hierarchy and put all
      // fields in `RecordStorageLocation` of the derived class.
      PropagateResultObject(Init, Loc);
    }

    for (auto [Field, Init] : InitList.field_inits()) {
      // Fields of non-record type are handled in
      // `TransferVisitor::VisitInitListExpr()`.
      if (Field->getType()->isRecordType())
        PropagateResultObject(
            Init, cast<RecordStorageLocation>(Loc->getChild(*Field)));
    }
  }

  // Assigns `Loc` as the result object location of `E`, then propagates the
  // location to all lower-level prvalues that initialize the same object as
  // `E` (or one of its base classes or member variables).
  void PropagateResultObject(Expr *E, RecordStorageLocation *Loc) {
    if (!E->isPRValue() || !E->getType()->isRecordType()) {
      assert(false);
      // Ensure we don't propagate the result object if we hit this in a
      // release build.
      return;
    }

    ResultObjectMap[E] = Loc;

    // The following AST node kinds are "original initializers": They are the
    // lowest-level AST node that initializes a given object, and nothing
    // below them can initialize the same object (or part of it).
    if (isa<CXXConstructExpr>(E) || isa<CallExpr>(E) || isa<LambdaExpr>(E) ||
        isa<CXXDefaultArgExpr>(E) || isa<CXXStdInitializerListExpr>(E) ||
        isa<AtomicExpr>(E) ||
        // We treat `BuiltinBitCastExpr` as an "original initializer" too as
        // it may not even be casting from a record type -- and even if it is,
        // the two objects are in general of unrelated type.
        isa<BuiltinBitCastExpr>(E)) {
      return;
    }
    if (auto *Op = dyn_cast<BinaryOperator>(E);
        Op && Op->getOpcode() == BO_Cmp) {
      // Builtin `<=>` returns a `std::strong_ordering` object.
      return;
    }

    if (auto *InitList = dyn_cast<InitListExpr>(E)) {
      if (!InitList->isSemanticForm())
        return;
      if (InitList->isTransparent()) {
        PropagateResultObject(InitList->getInit(0), Loc);
        return;
      }

      PropagateResultObjectToRecordInitList(RecordInitListHelper(InitList),
                                            Loc);
      return;
    }

    if (auto *ParenInitList = dyn_cast<CXXParenListInitExpr>(E)) {
      PropagateResultObjectToRecordInitList(RecordInitListHelper(ParenInitList),
                                            Loc);
      return;
    }

    if (auto *Op = dyn_cast<BinaryOperator>(E); Op && Op->isCommaOp()) {
      PropagateResultObject(Op->getRHS(), Loc);
      return;
    }

    if (auto *Cond = dyn_cast<AbstractConditionalOperator>(E)) {
      PropagateResultObject(Cond->getTrueExpr(), Loc);
      PropagateResultObject(Cond->getFalseExpr(), Loc);
      return;
    }

    if (auto *SE = dyn_cast<StmtExpr>(E)) {
      PropagateResultObject(cast<Expr>(SE->getSubStmt()->body_back()), Loc);
      return;
    }

    if (auto *DIE = dyn_cast<CXXDefaultInitExpr>(E)) {
      PropagateResultObject(DIE->getExpr(), Loc);
      return;
    }

    // All other expression nodes that propagate a record prvalue should have
    // exactly one child.
    SmallVector<Stmt *, 1> Children(E->child_begin(), E->child_end());
    LLVM_DEBUG({
      if (Children.size() != 1)
        E->dump();
    });
    assert(Children.size() == 1);
    for (Stmt *S : Children)
      PropagateResultObject(cast<Expr>(S), Loc);
  }

private:
  llvm::DenseMap<const Expr *, RecordStorageLocation *> &ResultObjectMap;
  RecordStorageLocation *LocForRecordReturnVal;
  DataflowAnalysisContext &DACtx;
};

} // namespace

void Environment::initialize() {
  if (InitialTargetStmt == nullptr)
    return;

  if (InitialTargetFunc == nullptr) {
    initFieldsGlobalsAndFuncs(getReferencedDecls(*InitialTargetStmt));
    ResultObjectMap =
        std::make_shared<PrValueToResultObject>(buildResultObjectMap(
            DACtx, InitialTargetStmt, getThisPointeeStorageLocation(),
            /*LocForRecordReturnValue=*/nullptr));
    return;
  }

  initFieldsGlobalsAndFuncs(getReferencedDecls(*InitialTargetFunc));

  for (const auto *ParamDecl : InitialTargetFunc->parameters()) {
    assert(ParamDecl != nullptr);
    setStorageLocation(*ParamDecl, createObject(*ParamDecl, nullptr));
  }

  if (InitialTargetFunc->getReturnType()->isRecordType())
    LocForRecordReturnVal = &cast<RecordStorageLocation>(
        createStorageLocation(InitialTargetFunc->getReturnType()));

  if (const auto *MethodDecl = dyn_cast<CXXMethodDecl>(InitialTargetFunc)) {
    auto *Parent = MethodDecl->getParent();
    assert(Parent != nullptr);

    if (Parent->isLambda()) {
      for (const auto &Capture : Parent->captures()) {
        if (Capture.capturesVariable()) {
          const auto *VarDecl = Capture.getCapturedVar();
          assert(VarDecl != nullptr);
          setStorageLocation(*VarDecl, createObject(*VarDecl, nullptr));
        } else if (Capture.capturesThis()) {
          if (auto *Ancestor = InitialTargetFunc->getNonClosureAncestor()) {
            const auto *SurroundingMethodDecl = cast<CXXMethodDecl>(Ancestor);
            QualType ThisPointeeType =
                SurroundingMethodDecl->getFunctionObjectParameterType();
            setThisPointeeStorageLocation(
                cast<RecordStorageLocation>(createObject(ThisPointeeType)));
          } else if (auto *FieldBeingInitialized =
                         dyn_cast<FieldDecl>(Parent->getLambdaContextDecl())) {
            // This is in a field initializer, rather than a method.
            setThisPointeeStorageLocation(
                cast<RecordStorageLocation>(createObject(QualType(
                    FieldBeingInitialized->getParent()->getTypeForDecl(), 0))));
          } else {
            assert(false && "Unexpected this-capturing lambda context.");
          }
        }
      }
    } else if (MethodDecl->isImplicitObjectMemberFunction()) {
      QualType ThisPointeeType = MethodDecl->getFunctionObjectParameterType();
      auto &ThisLoc =
          cast<RecordStorageLocation>(createStorageLocation(ThisPointeeType));
      setThisPointeeStorageLocation(ThisLoc);
      // Initialize fields of `*this` with values, but only if we're not
      // analyzing a constructor; after all, it's the constructor's job to do
      // this (and we want to be able to test that).
      if (!isa<CXXConstructorDecl>(MethodDecl))
        initializeFieldsWithValues(ThisLoc);
    }
  }

  // We do this below the handling of `CXXMethodDecl` above so that we can
  // be sure that the storage location for `this` has been set.
  ResultObjectMap =
      std::make_shared<PrValueToResultObject>(buildResultObjectMap(
          DACtx, InitialTargetFunc, getThisPointeeStorageLocation(),
          LocForRecordReturnVal));
}

// FIXME: Add support for resetting globals after function calls to enable the
// implementation of sound analyses.

void Environment::initFieldsGlobalsAndFuncs(const ReferencedDecls &Referenced) {
  // These have to be added before the lines that follow to ensure that
  // `create*` work correctly for structs.
  DACtx->addModeledFields(Referenced.Fields);

  for (const VarDecl *D : Referenced.Globals) {
    if (getStorageLocation(*D) != nullptr)
      continue;

    // We don't run transfer functions on the initializers of global variables,
    // so they won't be associated with a value or storage location. We
    // therefore intentionally don't pass an initializer to `createObject()`; in
    // particular, this ensures that `createObject()` will initialize the fields
    // of record-type variables with values.
    setStorageLocation(*D, createObject(*D, nullptr));
  }

  for (const FunctionDecl *FD : Referenced.Functions) {
    if (getStorageLocation(*FD) != nullptr)
      continue;
    auto &Loc = createStorageLocation(*FD);
    setStorageLocation(*FD, Loc);
  }
}

Environment Environment::fork() const {
  Environment Copy(*this);
  Copy.FlowConditionToken = DACtx->forkFlowCondition(FlowConditionToken);
  return Copy;
}

bool Environment::canDescend(unsigned MaxDepth,
                             const FunctionDecl *Callee) const {
  return CallStack.size() < MaxDepth && !llvm::is_contained(CallStack, Callee);
}

Environment Environment::pushCall(const CallExpr *Call) const {
  Environment Env(*this);

  if (const auto *MethodCall = dyn_cast<CXXMemberCallExpr>(Call)) {
    if (const Expr *Arg = MethodCall->getImplicitObjectArgument()) {
      if (!isa<CXXThisExpr>(Arg))
        Env.ThisPointeeLoc =
            cast<RecordStorageLocation>(getStorageLocation(*Arg));
      // Otherwise (when the argument is `this`), retain the current
      // environment's `ThisPointeeLoc`.
    }
  }

  if (Call->getType()->isRecordType() && Call->isPRValue())
    Env.LocForRecordReturnVal = &Env.getResultObjectLocation(*Call);

  Env.pushCallInternal(Call->getDirectCallee(),
                       llvm::ArrayRef(Call->getArgs(), Call->getNumArgs()));

  return Env;
}

Environment Environment::pushCall(const CXXConstructExpr *Call) const {
  Environment Env(*this);

  Env.ThisPointeeLoc = &Env.getResultObjectLocation(*Call);
  Env.LocForRecordReturnVal = &Env.getResultObjectLocation(*Call);

  Env.pushCallInternal(Call->getConstructor(),
                       llvm::ArrayRef(Call->getArgs(), Call->getNumArgs()));

  return Env;
}

void Environment::pushCallInternal(const FunctionDecl *FuncDecl,
                                   ArrayRef<const Expr *> Args) {
  // Canonicalize to the definition of the function. This ensures that we're
  // putting arguments into the same `ParamVarDecl`s` that the callee will later
  // be retrieving them from.
  assert(FuncDecl->getDefinition() != nullptr);
  FuncDecl = FuncDecl->getDefinition();

  CallStack.push_back(FuncDecl);

  initFieldsGlobalsAndFuncs(getReferencedDecls(*FuncDecl));

  const auto *ParamIt = FuncDecl->param_begin();

  // FIXME: Parameters don't always map to arguments 1:1; examples include
  // overloaded operators implemented as member functions, and parameter packs.
  for (unsigned ArgIndex = 0; ArgIndex < Args.size(); ++ParamIt, ++ArgIndex) {
    assert(ParamIt != FuncDecl->param_end());
    const VarDecl *Param = *ParamIt;
    setStorageLocation(*Param, createObject(*Param, Args[ArgIndex]));
  }

  ResultObjectMap = std::make_shared<PrValueToResultObject>(
      buildResultObjectMap(DACtx, FuncDecl, getThisPointeeStorageLocation(),
                           LocForRecordReturnVal));
}

void Environment::popCall(const CallExpr *Call, const Environment &CalleeEnv) {
  // We ignore some entries of `CalleeEnv`:
  // - `DACtx` because is already the same in both
  // - We don't want the callee's `DeclCtx`, `ReturnVal`, `ReturnLoc` or
  //   `ThisPointeeLoc` because they don't apply to us.
  // - `DeclToLoc`, `ExprToLoc`, and `ExprToVal` capture information from the
  //   callee's local scope, so when popping that scope, we do not propagate
  //   the maps.
  this->LocToVal = std::move(CalleeEnv.LocToVal);
  this->FlowConditionToken = std::move(CalleeEnv.FlowConditionToken);

  if (Call->isGLValue()) {
    if (CalleeEnv.ReturnLoc != nullptr)
      setStorageLocation(*Call, *CalleeEnv.ReturnLoc);
  } else if (!Call->getType()->isVoidType()) {
    if (CalleeEnv.ReturnVal != nullptr)
      setValue(*Call, *CalleeEnv.ReturnVal);
  }
}

void Environment::popCall(const CXXConstructExpr *Call,
                          const Environment &CalleeEnv) {
  // See also comment in `popCall(const CallExpr *, const Environment &)` above.
  this->LocToVal = std::move(CalleeEnv.LocToVal);
  this->FlowConditionToken = std::move(CalleeEnv.FlowConditionToken);
}

bool Environment::equivalentTo(const Environment &Other,
                               Environment::ValueModel &Model) const {
  assert(DACtx == Other.DACtx);

  if (ReturnVal != Other.ReturnVal)
    return false;

  if (ReturnLoc != Other.ReturnLoc)
    return false;

  if (LocForRecordReturnVal != Other.LocForRecordReturnVal)
    return false;

  if (ThisPointeeLoc != Other.ThisPointeeLoc)
    return false;

  if (DeclToLoc != Other.DeclToLoc)
    return false;

  if (ExprToLoc != Other.ExprToLoc)
    return false;

  if (!compareKeyToValueMaps(ExprToVal, Other.ExprToVal, *this, Other, Model))
    return false;

  if (!compareKeyToValueMaps(LocToVal, Other.LocToVal, *this, Other, Model))
    return false;

  return true;
}

LatticeEffect Environment::widen(const Environment &PrevEnv,
                                 Environment::ValueModel &Model) {
  assert(DACtx == PrevEnv.DACtx);
  assert(ReturnVal == PrevEnv.ReturnVal);
  assert(ReturnLoc == PrevEnv.ReturnLoc);
  assert(LocForRecordReturnVal == PrevEnv.LocForRecordReturnVal);
  assert(ThisPointeeLoc == PrevEnv.ThisPointeeLoc);
  assert(CallStack == PrevEnv.CallStack);
  assert(ResultObjectMap == PrevEnv.ResultObjectMap);
  assert(InitialTargetFunc == PrevEnv.InitialTargetFunc);
  assert(InitialTargetStmt == PrevEnv.InitialTargetStmt);

  auto Effect = LatticeEffect::Unchanged;

  // By the API, `PrevEnv` is a previous version of the environment for the same
  // block, so we have some guarantees about its shape. In particular, it will
  // be the result of a join or widen operation on previous values for this
  // block. For `DeclToLoc`, `ExprToVal`, and `ExprToLoc`, join guarantees that
  // these maps are subsets of the maps in `PrevEnv`. So, as long as we maintain
  // this property here, we don't need change their current values to widen.
  assert(DeclToLoc.size() <= PrevEnv.DeclToLoc.size());
  assert(ExprToVal.size() <= PrevEnv.ExprToVal.size());
  assert(ExprToLoc.size() <= PrevEnv.ExprToLoc.size());

  ExprToVal = widenKeyToValueMap(ExprToVal, PrevEnv.ExprToVal, *this, PrevEnv,
                                 Model, Effect);

  LocToVal = widenKeyToValueMap(LocToVal, PrevEnv.LocToVal, *this, PrevEnv,
                                Model, Effect);
  if (DeclToLoc.size() != PrevEnv.DeclToLoc.size() ||
      ExprToLoc.size() != PrevEnv.ExprToLoc.size() ||
      ExprToVal.size() != PrevEnv.ExprToVal.size() ||
      LocToVal.size() != PrevEnv.LocToVal.size())
    Effect = LatticeEffect::Changed;

  return Effect;
}

Environment Environment::join(const Environment &EnvA, const Environment &EnvB,
                              Environment::ValueModel &Model,
                              ExprJoinBehavior ExprBehavior) {
  assert(EnvA.DACtx == EnvB.DACtx);
  assert(EnvA.LocForRecordReturnVal == EnvB.LocForRecordReturnVal);
  assert(EnvA.ThisPointeeLoc == EnvB.ThisPointeeLoc);
  assert(EnvA.CallStack == EnvB.CallStack);
  assert(EnvA.ResultObjectMap == EnvB.ResultObjectMap);
  assert(EnvA.InitialTargetFunc == EnvB.InitialTargetFunc);
  assert(EnvA.InitialTargetStmt == EnvB.InitialTargetStmt);

  Environment JoinedEnv(*EnvA.DACtx);

  JoinedEnv.CallStack = EnvA.CallStack;
  JoinedEnv.ResultObjectMap = EnvA.ResultObjectMap;
  JoinedEnv.LocForRecordReturnVal = EnvA.LocForRecordReturnVal;
  JoinedEnv.ThisPointeeLoc = EnvA.ThisPointeeLoc;
  JoinedEnv.InitialTargetFunc = EnvA.InitialTargetFunc;
  JoinedEnv.InitialTargetStmt = EnvA.InitialTargetStmt;

  const FunctionDecl *Func = EnvA.getCurrentFunc();
  if (!Func) {
    JoinedEnv.ReturnVal = nullptr;
  } else {
    JoinedEnv.ReturnVal =
        joinValues(Func->getReturnType(), EnvA.ReturnVal, EnvA, EnvB.ReturnVal,
                   EnvB, JoinedEnv, Model);
  }

  if (EnvA.ReturnLoc == EnvB.ReturnLoc)
    JoinedEnv.ReturnLoc = EnvA.ReturnLoc;
  else
    JoinedEnv.ReturnLoc = nullptr;

  JoinedEnv.DeclToLoc = intersectDeclToLoc(EnvA.DeclToLoc, EnvB.DeclToLoc);

  // FIXME: update join to detect backedges and simplify the flow condition
  // accordingly.
  JoinedEnv.FlowConditionToken = EnvA.DACtx->joinFlowConditions(
      EnvA.FlowConditionToken, EnvB.FlowConditionToken);

  JoinedEnv.LocToVal =
      joinLocToVal(EnvA.LocToVal, EnvB.LocToVal, EnvA, EnvB, JoinedEnv, Model);

  if (ExprBehavior == KeepExprState) {
    JoinedEnv.ExprToVal = joinExprMaps(EnvA.ExprToVal, EnvB.ExprToVal);
    JoinedEnv.ExprToLoc = joinExprMaps(EnvA.ExprToLoc, EnvB.ExprToLoc);
  }

  return JoinedEnv;
}

Value *Environment::joinValues(QualType Ty, Value *Val1,
                               const Environment &Env1, Value *Val2,
                               const Environment &Env2, Environment &JoinedEnv,
                               Environment::ValueModel &Model) {
  if (Val1 == nullptr || Val2 == nullptr)
    // We can't say anything about the joined value -- even if one of the values
    // is non-null, we don't want to simply propagate it, because it would be
    // too specific: Because the other value is null, that means we have no
    // information at all about the value (i.e. the value is unconstrained).
    return nullptr;

  if (areEquivalentValues(*Val1, *Val2))
    // Arbitrarily return one of the two values.
    return Val1;

  return joinDistinctValues(Ty, *Val1, Env1, *Val2, Env2, JoinedEnv, Model);
}

StorageLocation &Environment::createStorageLocation(QualType Type) {
  return DACtx->createStorageLocation(Type);
}

StorageLocation &Environment::createStorageLocation(const ValueDecl &D) {
  // Evaluated declarations are always assigned the same storage locations to
  // ensure that the environment stabilizes across loop iterations. Storage
  // locations for evaluated declarations are stored in the analysis context.
  return DACtx->getStableStorageLocation(D);
}

StorageLocation &Environment::createStorageLocation(const Expr &E) {
  // Evaluated expressions are always assigned the same storage locations to
  // ensure that the environment stabilizes across loop iterations. Storage
  // locations for evaluated expressions are stored in the analysis context.
  return DACtx->getStableStorageLocation(E);
}

void Environment::setStorageLocation(const ValueDecl &D, StorageLocation &Loc) {
  assert(!DeclToLoc.contains(&D));
  // The only kinds of declarations that may have a "variable" storage location
  // are declarations of reference type and `BindingDecl`. For all other
  // declaration, the storage location should be the stable storage location
  // returned by `createStorageLocation()`.
  assert(D.getType()->isReferenceType() || isa<BindingDecl>(D) ||
         &Loc == &createStorageLocation(D));
  DeclToLoc[&D] = &Loc;
}

StorageLocation *Environment::getStorageLocation(const ValueDecl &D) const {
  auto It = DeclToLoc.find(&D);
  if (It == DeclToLoc.end())
    return nullptr;

  StorageLocation *Loc = It->second;

  return Loc;
}

void Environment::removeDecl(const ValueDecl &D) { DeclToLoc.erase(&D); }

void Environment::setStorageLocation(const Expr &E, StorageLocation &Loc) {
  // `DeclRefExpr`s to builtin function types aren't glvalues, for some reason,
  // but we still want to be able to associate a `StorageLocation` with them,
  // so allow these as an exception.
  assert(E.isGLValue() ||
         E.getType()->isSpecificBuiltinType(BuiltinType::BuiltinFn));
  const Expr &CanonE = ignoreCFGOmittedNodes(E);
  assert(!ExprToLoc.contains(&CanonE));
  ExprToLoc[&CanonE] = &Loc;
}

StorageLocation *Environment::getStorageLocation(const Expr &E) const {
  // See comment in `setStorageLocation()`.
  assert(E.isGLValue() ||
         E.getType()->isSpecificBuiltinType(BuiltinType::BuiltinFn));
  auto It = ExprToLoc.find(&ignoreCFGOmittedNodes(E));
  return It == ExprToLoc.end() ? nullptr : &*It->second;
}

RecordStorageLocation &
Environment::getResultObjectLocation(const Expr &RecordPRValue) const {
  assert(RecordPRValue.getType()->isRecordType());
  assert(RecordPRValue.isPRValue());

  assert(ResultObjectMap != nullptr);
  RecordStorageLocation *Loc = ResultObjectMap->lookup(&RecordPRValue);
  assert(Loc != nullptr);
  // In release builds, use the "stable" storage location if the map lookup
  // failed.
  if (Loc == nullptr)
    return cast<RecordStorageLocation>(
        DACtx->getStableStorageLocation(RecordPRValue));
  return *Loc;
}

PointerValue &Environment::getOrCreateNullPointerValue(QualType PointeeType) {
  return DACtx->getOrCreateNullPointerValue(PointeeType);
}

void Environment::initializeFieldsWithValues(RecordStorageLocation &Loc,
                                             QualType Type) {
  llvm::DenseSet<QualType> Visited;
  int CreatedValuesCount = 0;
  initializeFieldsWithValues(Loc, Type, Visited, 0, CreatedValuesCount);
  if (CreatedValuesCount > MaxCompositeValueSize) {
    llvm::errs() << "Attempting to initialize a huge value of type: " << Type
                 << '\n';
  }
}

void Environment::setValue(const StorageLocation &Loc, Value &Val) {
  // Records should not be associated with values.
  assert(!isa<RecordStorageLocation>(Loc));
  LocToVal[&Loc] = &Val;
}

void Environment::setValue(const Expr &E, Value &Val) {
  const Expr &CanonE = ignoreCFGOmittedNodes(E);

  assert(CanonE.isPRValue());
  // Records should not be associated with values.
  assert(!CanonE.getType()->isRecordType());
  ExprToVal[&CanonE] = &Val;
}

Value *Environment::getValue(const StorageLocation &Loc) const {
  // Records should not be associated with values.
  assert(!isa<RecordStorageLocation>(Loc));
  return LocToVal.lookup(&Loc);
}

Value *Environment::getValue(const ValueDecl &D) const {
  auto *Loc = getStorageLocation(D);
  if (Loc == nullptr)
    return nullptr;
  return getValue(*Loc);
}

Value *Environment::getValue(const Expr &E) const {
  // Records should not be associated with values.
  assert(!E.getType()->isRecordType());

  if (E.isPRValue()) {
    auto It = ExprToVal.find(&ignoreCFGOmittedNodes(E));
    return It == ExprToVal.end() ? nullptr : It->second;
  }

  auto It = ExprToLoc.find(&ignoreCFGOmittedNodes(E));
  if (It == ExprToLoc.end())
    return nullptr;
  return getValue(*It->second);
}

Value *Environment::createValue(QualType Type) {
  llvm::DenseSet<QualType> Visited;
  int CreatedValuesCount = 0;
  Value *Val = createValueUnlessSelfReferential(Type, Visited, /*Depth=*/0,
                                                CreatedValuesCount);
  if (CreatedValuesCount > MaxCompositeValueSize) {
    llvm::errs() << "Attempting to initialize a huge value of type: " << Type
                 << '\n';
  }
  return Val;
}

Value *Environment::createValueUnlessSelfReferential(
    QualType Type, llvm::DenseSet<QualType> &Visited, int Depth,
    int &CreatedValuesCount) {
  assert(!Type.isNull());
  assert(!Type->isReferenceType());
  assert(!Type->isRecordType());

  // Allow unlimited fields at depth 1; only cap at deeper nesting levels.
  if ((Depth > 1 && CreatedValuesCount > MaxCompositeValueSize) ||
      Depth > MaxCompositeValueDepth)
    return nullptr;

  if (Type->isBooleanType()) {
    CreatedValuesCount++;
    return &makeAtomicBoolValue();
  }

  if (Type->isIntegerType()) {
    // FIXME: consider instead `return nullptr`, given that we do nothing useful
    // with integers, and so distinguishing them serves no purpose, but could
    // prevent convergence.
    CreatedValuesCount++;
    return &arena().create<IntegerValue>();
  }

  if (Type->isPointerType()) {
    CreatedValuesCount++;
    QualType PointeeType = Type->getPointeeType();
    StorageLocation &PointeeLoc =
        createLocAndMaybeValue(PointeeType, Visited, Depth, CreatedValuesCount);

    return &arena().create<PointerValue>(PointeeLoc);
  }

  return nullptr;
}

StorageLocation &
Environment::createLocAndMaybeValue(QualType Ty,
                                    llvm::DenseSet<QualType> &Visited,
                                    int Depth, int &CreatedValuesCount) {
  if (!Visited.insert(Ty.getCanonicalType()).second)
    return createStorageLocation(Ty.getNonReferenceType());
  auto EraseVisited = llvm::make_scope_exit(
      [&Visited, Ty] { Visited.erase(Ty.getCanonicalType()); });

  Ty = Ty.getNonReferenceType();

  if (Ty->isRecordType()) {
    auto &Loc = cast<RecordStorageLocation>(createStorageLocation(Ty));
    initializeFieldsWithValues(Loc, Ty, Visited, Depth, CreatedValuesCount);
    return Loc;
  }

  StorageLocation &Loc = createStorageLocation(Ty);

  if (Value *Val = createValueUnlessSelfReferential(Ty, Visited, Depth,
                                                    CreatedValuesCount))
    setValue(Loc, *Val);

  return Loc;
}

void Environment::initializeFieldsWithValues(RecordStorageLocation &Loc,
                                             QualType Type,
                                             llvm::DenseSet<QualType> &Visited,
                                             int Depth,
                                             int &CreatedValuesCount) {
  auto initField = [&](QualType FieldType, StorageLocation &FieldLoc) {
    if (FieldType->isRecordType()) {
      auto &FieldRecordLoc = cast<RecordStorageLocation>(FieldLoc);
      initializeFieldsWithValues(FieldRecordLoc, FieldRecordLoc.getType(),
                                 Visited, Depth + 1, CreatedValuesCount);
    } else {
      if (getValue(FieldLoc) != nullptr)
        return;
      if (!Visited.insert(FieldType.getCanonicalType()).second)
        return;
      if (Value *Val = createValueUnlessSelfReferential(
              FieldType, Visited, Depth + 1, CreatedValuesCount))
        setValue(FieldLoc, *Val);
      Visited.erase(FieldType.getCanonicalType());
    }
  };

  for (const FieldDecl *Field : DACtx->getModeledFields(Type)) {
    assert(Field != nullptr);
    QualType FieldType = Field->getType();

    if (FieldType->isReferenceType()) {
      Loc.setChild(*Field,
                   &createLocAndMaybeValue(FieldType, Visited, Depth + 1,
                                           CreatedValuesCount));
    } else {
      StorageLocation *FieldLoc = Loc.getChild(*Field);
      assert(FieldLoc != nullptr);
      initField(FieldType, *FieldLoc);
    }
  }
  for (const auto &[FieldName, FieldType] : DACtx->getSyntheticFields(Type)) {
    // Synthetic fields cannot have reference type, so we don't need to deal
    // with this case.
    assert(!FieldType->isReferenceType());
    initField(FieldType, Loc.getSyntheticField(FieldName));
  }
}

StorageLocation &Environment::createObjectInternal(const ValueDecl *D,
                                                   QualType Ty,
                                                   const Expr *InitExpr) {
  if (Ty->isReferenceType()) {
    // Although variables of reference type always need to be initialized, it
    // can happen that we can't see the initializer, so `InitExpr` may still
    // be null.
    if (InitExpr) {
      if (auto *InitExprLoc = getStorageLocation(*InitExpr))
        return *InitExprLoc;
    }

    // Even though we have an initializer, we might not get an
    // InitExprLoc, for example if the InitExpr is a CallExpr for which we
    // don't have a function body. In this case, we just invent a storage
    // location and value -- it's the best we can do.
    return createObjectInternal(D, Ty.getNonReferenceType(), nullptr);
  }

  StorageLocation &Loc =
      D ? createStorageLocation(*D) : createStorageLocation(Ty);

  if (Ty->isRecordType()) {
    auto &RecordLoc = cast<RecordStorageLocation>(Loc);
    if (!InitExpr)
      initializeFieldsWithValues(RecordLoc);
  } else {
    Value *Val = nullptr;
    if (InitExpr)
      // In the (few) cases where an expression is intentionally
      // "uninterpreted", `InitExpr` is not associated with a value.  There are
      // two ways to handle this situation: propagate the status, so that
      // uninterpreted initializers result in uninterpreted variables, or
      // provide a default value. We choose the latter so that later refinements
      // of the variable can be used for reasoning about the surrounding code.
      // For this reason, we let this case be handled by the `createValue()`
      // call below.
      //
      // FIXME. If and when we interpret all language cases, change this to
      // assert that `InitExpr` is interpreted, rather than supplying a
      // default value (assuming we don't update the environment API to return
      // references).
      Val = getValue(*InitExpr);
    if (!Val)
      Val = createValue(Ty);
    if (Val)
      setValue(Loc, *Val);
  }

  return Loc;
}

void Environment::assume(const Formula &F) {
  DACtx->addFlowConditionConstraint(FlowConditionToken, F);
}

bool Environment::proves(const Formula &F) const {
  return DACtx->flowConditionImplies(FlowConditionToken, F);
}

bool Environment::allows(const Formula &F) const {
  return DACtx->flowConditionAllows(FlowConditionToken, F);
}

void Environment::dump(raw_ostream &OS) const {
  llvm::DenseMap<const StorageLocation *, std::string> LocToName;
  if (LocForRecordReturnVal != nullptr)
    LocToName[LocForRecordReturnVal] = "(returned record)";
  if (ThisPointeeLoc != nullptr)
    LocToName[ThisPointeeLoc] = "this";

  OS << "DeclToLoc:\n";
  for (auto [D, L] : DeclToLoc) {
    auto Iter = LocToName.insert({L, D->getNameAsString()}).first;
    OS << "  [" << Iter->second << ", " << L << "]\n";
  }
  OS << "ExprToLoc:\n";
  for (auto [E, L] : ExprToLoc)
    OS << "  [" << E << ", " << L << "]\n";

  OS << "ExprToVal:\n";
  for (auto [E, V] : ExprToVal)
    OS << "  [" << E << ", " << V << ": " << *V << "]\n";

  OS << "LocToVal:\n";
  for (auto [L, V] : LocToVal) {
    OS << "  [" << L;
    if (auto Iter = LocToName.find(L); Iter != LocToName.end())
      OS << " (" << Iter->second << ")";
    OS << ", " << V << ": " << *V << "]\n";
  }

  if (const FunctionDecl *Func = getCurrentFunc()) {
    if (Func->getReturnType()->isReferenceType()) {
      OS << "ReturnLoc: " << ReturnLoc;
      if (auto Iter = LocToName.find(ReturnLoc); Iter != LocToName.end())
        OS << " (" << Iter->second << ")";
      OS << "\n";
    } else if (Func->getReturnType()->isRecordType() ||
               isa<CXXConstructorDecl>(Func)) {
      OS << "LocForRecordReturnVal: " << LocForRecordReturnVal << "\n";
    } else if (!Func->getReturnType()->isVoidType()) {
      if (ReturnVal == nullptr)
        OS << "ReturnVal: nullptr\n";
      else
        OS << "ReturnVal: " << *ReturnVal << "\n";
    }

    if (isa<CXXMethodDecl>(Func)) {
      OS << "ThisPointeeLoc: " << ThisPointeeLoc << "\n";
    }
  }

  OS << "\n";
  DACtx->dumpFlowCondition(FlowConditionToken, OS);
}

void Environment::dump() const { dump(llvm::dbgs()); }

Environment::PrValueToResultObject Environment::buildResultObjectMap(
    DataflowAnalysisContext *DACtx, const FunctionDecl *FuncDecl,
    RecordStorageLocation *ThisPointeeLoc,
    RecordStorageLocation *LocForRecordReturnVal) {
  assert(FuncDecl->doesThisDeclarationHaveABody());

  PrValueToResultObject Map = buildResultObjectMap(
      DACtx, FuncDecl->getBody(), ThisPointeeLoc, LocForRecordReturnVal);

  ResultObjectVisitor Visitor(Map, LocForRecordReturnVal, *DACtx);
  if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(FuncDecl))
    Visitor.TraverseConstructorInits(Ctor, ThisPointeeLoc);

  return Map;
}

Environment::PrValueToResultObject Environment::buildResultObjectMap(
    DataflowAnalysisContext *DACtx, Stmt *S,
    RecordStorageLocation *ThisPointeeLoc,
    RecordStorageLocation *LocForRecordReturnVal) {
  PrValueToResultObject Map;
  ResultObjectVisitor Visitor(Map, LocForRecordReturnVal, *DACtx);
  Visitor.TraverseStmt(S);
  return Map;
}

RecordStorageLocation *getImplicitObjectLocation(const CXXMemberCallExpr &MCE,
                                                 const Environment &Env) {
  Expr *ImplicitObject = MCE.getImplicitObjectArgument();
  if (ImplicitObject == nullptr)
    return nullptr;
  if (ImplicitObject->getType()->isPointerType()) {
    if (auto *Val = Env.get<PointerValue>(*ImplicitObject))
      return &cast<RecordStorageLocation>(Val->getPointeeLoc());
    return nullptr;
  }
  return cast_or_null<RecordStorageLocation>(
      Env.getStorageLocation(*ImplicitObject));
}

RecordStorageLocation *getBaseObjectLocation(const MemberExpr &ME,
                                             const Environment &Env) {
  Expr *Base = ME.getBase();
  if (Base == nullptr)
    return nullptr;
  if (ME.isArrow()) {
    if (auto *Val = Env.get<PointerValue>(*Base))
      return &cast<RecordStorageLocation>(Val->getPointeeLoc());
    return nullptr;
  }
  return Env.get<RecordStorageLocation>(*Base);
}

} // namespace dataflow
} // namespace clang
