//= ProgramState.cpp - Path-Sensitive "State" for tracking values --*- C++ -*--=
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements ProgramState and ProgramStateManager.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/JsonSupport.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace clang { namespace  ento {
/// Increments the number of times this state is referenced.

void ProgramStateRetain(const ProgramState *state) {
  ++const_cast<ProgramState*>(state)->refCount;
}

/// Decrement the number of times this state is referenced.
void ProgramStateRelease(const ProgramState *state) {
  assert(state->refCount > 0);
  ProgramState *s = const_cast<ProgramState*>(state);
  if (--s->refCount == 0) {
    ProgramStateManager &Mgr = s->getStateManager();
    Mgr.StateSet.RemoveNode(s);
    s->~ProgramState();
    Mgr.freeStates.push_back(s);
  }
}
}}

ProgramState::ProgramState(ProgramStateManager *mgr, const Environment& env,
                 StoreRef st, GenericDataMap gdm)
  : stateMgr(mgr),
    Env(env),
    store(st.getStore()),
    GDM(gdm),
    refCount(0) {
  stateMgr->getStoreManager().incrementReferenceCount(store);
}

ProgramState::ProgramState(const ProgramState &RHS)
    : stateMgr(RHS.stateMgr), Env(RHS.Env), store(RHS.store), GDM(RHS.GDM),
      PosteriorlyOverconstrained(RHS.PosteriorlyOverconstrained), refCount(0) {
  stateMgr->getStoreManager().incrementReferenceCount(store);
}

ProgramState::~ProgramState() {
  if (store)
    stateMgr->getStoreManager().decrementReferenceCount(store);
}

int64_t ProgramState::getID() const {
  return getStateManager().Alloc.identifyKnownAlignedObject<ProgramState>(this);
}

ProgramStateManager::ProgramStateManager(ASTContext &Ctx,
                                         StoreManagerCreator CreateSMgr,
                                         ConstraintManagerCreator CreateCMgr,
                                         llvm::BumpPtrAllocator &alloc,
                                         ExprEngine *ExprEng)
  : Eng(ExprEng), EnvMgr(alloc), GDMFactory(alloc),
    svalBuilder(createSimpleSValBuilder(alloc, Ctx, *this)),
    CallEventMgr(new CallEventManager(alloc)), Alloc(alloc) {
  StoreMgr = (*CreateSMgr)(*this);
  ConstraintMgr = (*CreateCMgr)(*this, ExprEng);
}


ProgramStateManager::~ProgramStateManager() {
  for (GDMContextsTy::iterator I=GDMContexts.begin(), E=GDMContexts.end();
       I!=E; ++I)
    I->second.second(I->second.first);
}

ProgramStateRef ProgramStateManager::removeDeadBindingsFromEnvironmentAndStore(
    ProgramStateRef state, const StackFrameContext *LCtx,
    SymbolReaper &SymReaper) {

  // This code essentially performs a "mark-and-sweep" of the VariableBindings.
  // The roots are any Block-level exprs and Decls that our liveness algorithm
  // tells us are live.  We then see what Decls they may reference, and keep
  // those around.  This code more than likely can be made faster, and the
  // frequency of which this method is called should be experimented with
  // for optimum performance.
  ProgramState NewState = *state;

  NewState.Env = EnvMgr.removeDeadBindings(NewState.Env, SymReaper, state);

  // Clean up the store.
  StoreRef newStore = StoreMgr->removeDeadBindings(NewState.getStore(), LCtx,
                                                   SymReaper);
  NewState.setStore(newStore);
  SymReaper.setReapedStore(newStore);

  return getPersistentState(NewState);
}

ProgramStateRef ProgramState::bindLoc(Loc LV,
                                      SVal V,
                                      const LocationContext *LCtx,
                                      bool notifyChanges) const {
  ProgramStateManager &Mgr = getStateManager();
  ProgramStateRef newState = makeWithStore(Mgr.StoreMgr->Bind(getStore(),
                                                             LV, V));
  const MemRegion *MR = LV.getAsRegion();
  if (MR && notifyChanges)
    return Mgr.getOwningEngine().processRegionChange(newState, MR, LCtx);

  return newState;
}

ProgramStateRef
ProgramState::bindDefaultInitial(SVal loc, SVal V,
                                 const LocationContext *LCtx) const {
  ProgramStateManager &Mgr = getStateManager();
  const MemRegion *R = loc.castAs<loc::MemRegionVal>().getRegion();
  const StoreRef &newStore = Mgr.StoreMgr->BindDefaultInitial(getStore(), R, V);
  ProgramStateRef new_state = makeWithStore(newStore);
  return Mgr.getOwningEngine().processRegionChange(new_state, R, LCtx);
}

ProgramStateRef
ProgramState::bindDefaultZero(SVal loc, const LocationContext *LCtx) const {
  ProgramStateManager &Mgr = getStateManager();
  const MemRegion *R = loc.castAs<loc::MemRegionVal>().getRegion();
  const StoreRef &newStore = Mgr.StoreMgr->BindDefaultZero(getStore(), R);
  ProgramStateRef new_state = makeWithStore(newStore);
  return Mgr.getOwningEngine().processRegionChange(new_state, R, LCtx);
}

typedef ArrayRef<const MemRegion *> RegionList;
typedef ArrayRef<SVal> ValueList;

ProgramStateRef
ProgramState::invalidateRegions(RegionList Regions,
                             const Expr *E, unsigned Count,
                             const LocationContext *LCtx,
                             bool CausedByPointerEscape,
                             InvalidatedSymbols *IS,
                             const CallEvent *Call,
                             RegionAndSymbolInvalidationTraits *ITraits) const {
  SmallVector<SVal, 8> Values;
  for (const MemRegion *Reg : Regions)
    Values.push_back(loc::MemRegionVal(Reg));

  return invalidateRegionsImpl(Values, E, Count, LCtx, CausedByPointerEscape,
                               IS, ITraits, Call);
}

ProgramStateRef
ProgramState::invalidateRegions(ValueList Values,
                             const Expr *E, unsigned Count,
                             const LocationContext *LCtx,
                             bool CausedByPointerEscape,
                             InvalidatedSymbols *IS,
                             const CallEvent *Call,
                             RegionAndSymbolInvalidationTraits *ITraits) const {

  return invalidateRegionsImpl(Values, E, Count, LCtx, CausedByPointerEscape,
                               IS, ITraits, Call);
}

ProgramStateRef
ProgramState::invalidateRegionsImpl(ValueList Values,
                                    const Expr *E, unsigned Count,
                                    const LocationContext *LCtx,
                                    bool CausedByPointerEscape,
                                    InvalidatedSymbols *IS,
                                    RegionAndSymbolInvalidationTraits *ITraits,
                                    const CallEvent *Call) const {
  ProgramStateManager &Mgr = getStateManager();
  ExprEngine &Eng = Mgr.getOwningEngine();

  InvalidatedSymbols InvalidatedSyms;
  if (!IS)
    IS = &InvalidatedSyms;

  RegionAndSymbolInvalidationTraits ITraitsLocal;
  if (!ITraits)
    ITraits = &ITraitsLocal;

  StoreManager::InvalidatedRegions TopLevelInvalidated;
  StoreManager::InvalidatedRegions Invalidated;
  const StoreRef &newStore
  = Mgr.StoreMgr->invalidateRegions(getStore(), Values, E, Count, LCtx, Call,
                                    *IS, *ITraits, &TopLevelInvalidated,
                                    &Invalidated);

  ProgramStateRef newState = makeWithStore(newStore);

  if (CausedByPointerEscape) {
    newState = Eng.notifyCheckersOfPointerEscape(newState, IS,
                                                 TopLevelInvalidated,
                                                 Call,
                                                 *ITraits);
  }

  return Eng.processRegionChanges(newState, IS, TopLevelInvalidated,
                                  Invalidated, LCtx, Call);
}

ProgramStateRef ProgramState::killBinding(Loc LV) const {
  Store OldStore = getStore();
  const StoreRef &newStore =
    getStateManager().StoreMgr->killBinding(OldStore, LV);

  if (newStore.getStore() == OldStore)
    return this;

  return makeWithStore(newStore);
}

/// SymbolicRegions are expected to be wrapped by an ElementRegion as a
/// canonical representation. As a canonical representation, SymbolicRegions
/// should be wrapped by ElementRegions before getting a FieldRegion.
/// See f8643a9b31c4029942f67d4534c9139b45173504 why.
SVal ProgramState::wrapSymbolicRegion(SVal Val) const {
  const auto *BaseReg = dyn_cast_or_null<SymbolicRegion>(Val.getAsRegion());
  if (!BaseReg)
    return Val;

  StoreManager &SM = getStateManager().getStoreManager();
  QualType ElemTy = BaseReg->getPointeeStaticType();
  return loc::MemRegionVal{SM.GetElementZeroRegion(BaseReg, ElemTy)};
}

ProgramStateRef
ProgramState::enterStackFrame(const CallEvent &Call,
                              const StackFrameContext *CalleeCtx) const {
  const StoreRef &NewStore =
    getStateManager().StoreMgr->enterStackFrame(getStore(), Call, CalleeCtx);
  return makeWithStore(NewStore);
}

SVal ProgramState::getSelfSVal(const LocationContext *LCtx) const {
  const ImplicitParamDecl *SelfDecl = LCtx->getSelfDecl();
  if (!SelfDecl)
    return SVal();
  return getSVal(getRegion(SelfDecl, LCtx));
}

SVal ProgramState::getSValAsScalarOrLoc(const MemRegion *R) const {
  // We only want to do fetches from regions that we can actually bind
  // values.  For example, SymbolicRegions of type 'id<...>' cannot
  // have direct bindings (but their can be bindings on their subregions).
  if (!R->isBoundable())
    return UnknownVal();

  if (const TypedValueRegion *TR = dyn_cast<TypedValueRegion>(R)) {
    QualType T = TR->getValueType();
    if (Loc::isLocType(T) || T->isIntegralOrEnumerationType())
      return getSVal(R);
  }

  return UnknownVal();
}

SVal ProgramState::getSVal(Loc location, QualType T) const {
  SVal V = getRawSVal(location, T);

  // If 'V' is a symbolic value that is *perfectly* constrained to
  // be a constant value, use that value instead to lessen the burden
  // on later analysis stages (so we have less symbolic values to reason
  // about).
  // We only go into this branch if we can convert the APSInt value we have
  // to the type of T, which is not always the case (e.g. for void).
  if (!T.isNull() && (T->isIntegralOrEnumerationType() || Loc::isLocType(T))) {
    if (SymbolRef sym = V.getAsSymbol()) {
      if (const llvm::APSInt *Int = getStateManager()
                                    .getConstraintManager()
                                    .getSymVal(this, sym)) {
        // FIXME: Because we don't correctly model (yet) sign-extension
        // and truncation of symbolic values, we need to convert
        // the integer value to the correct signedness and bitwidth.
        //
        // This shows up in the following:
        //
        //   char foo();
        //   unsigned x = foo();
        //   if (x == 54)
        //     ...
        //
        //  The symbolic value stored to 'x' is actually the conjured
        //  symbol for the call to foo(); the type of that symbol is 'char',
        //  not unsigned.
        const llvm::APSInt &NewV = getBasicVals().Convert(T, *Int);

        if (V.getAs<Loc>())
          return loc::ConcreteInt(NewV);
        else
          return nonloc::ConcreteInt(NewV);
      }
    }
  }

  return V;
}

ProgramStateRef ProgramState::BindExpr(const Stmt *S,
                                           const LocationContext *LCtx,
                                           SVal V, bool Invalidate) const{
  Environment NewEnv =
    getStateManager().EnvMgr.bindExpr(Env, EnvironmentEntry(S, LCtx), V,
                                      Invalidate);
  if (NewEnv == Env)
    return this;

  ProgramState NewSt = *this;
  NewSt.Env = NewEnv;
  return getStateManager().getPersistentState(NewSt);
}

[[nodiscard]] std::pair<ProgramStateRef, ProgramStateRef>
ProgramState::assumeInBoundDual(DefinedOrUnknownSVal Idx,
                                DefinedOrUnknownSVal UpperBound,
                                QualType indexTy) const {
  if (Idx.isUnknown() || UpperBound.isUnknown())
    return {this, this};

  // Build an expression for 0 <= Idx < UpperBound.
  // This is the same as Idx + MIN < UpperBound + MIN, if overflow is allowed.
  // FIXME: This should probably be part of SValBuilder.
  ProgramStateManager &SM = getStateManager();
  SValBuilder &svalBuilder = SM.getSValBuilder();
  ASTContext &Ctx = svalBuilder.getContext();

  // Get the offset: the minimum value of the array index type.
  BasicValueFactory &BVF = svalBuilder.getBasicValueFactory();
  if (indexTy.isNull())
    indexTy = svalBuilder.getArrayIndexType();
  nonloc::ConcreteInt Min(BVF.getMinValue(indexTy));

  // Adjust the index.
  SVal newIdx = svalBuilder.evalBinOpNN(this, BO_Add,
                                        Idx.castAs<NonLoc>(), Min, indexTy);
  if (newIdx.isUnknownOrUndef())
    return {this, this};

  // Adjust the upper bound.
  SVal newBound =
    svalBuilder.evalBinOpNN(this, BO_Add, UpperBound.castAs<NonLoc>(),
                            Min, indexTy);

  if (newBound.isUnknownOrUndef())
    return {this, this};

  // Build the actual comparison.
  SVal inBound = svalBuilder.evalBinOpNN(this, BO_LT, newIdx.castAs<NonLoc>(),
                                         newBound.castAs<NonLoc>(), Ctx.IntTy);
  if (inBound.isUnknownOrUndef())
    return {this, this};

  // Finally, let the constraint manager take care of it.
  ConstraintManager &CM = SM.getConstraintManager();
  return CM.assumeDual(this, inBound.castAs<DefinedSVal>());
}

ProgramStateRef ProgramState::assumeInBound(DefinedOrUnknownSVal Idx,
                                            DefinedOrUnknownSVal UpperBound,
                                            bool Assumption,
                                            QualType indexTy) const {
  std::pair<ProgramStateRef, ProgramStateRef> R =
      assumeInBoundDual(Idx, UpperBound, indexTy);
  return Assumption ? R.first : R.second;
}

ConditionTruthVal ProgramState::isNonNull(SVal V) const {
  ConditionTruthVal IsNull = isNull(V);
  if (IsNull.isUnderconstrained())
    return IsNull;
  return ConditionTruthVal(!IsNull.getValue());
}

ConditionTruthVal ProgramState::areEqual(SVal Lhs, SVal Rhs) const {
  return stateMgr->getSValBuilder().areEqual(this, Lhs, Rhs);
}

ConditionTruthVal ProgramState::isNull(SVal V) const {
  if (V.isZeroConstant())
    return true;

  if (V.isConstant())
    return false;

  SymbolRef Sym = V.getAsSymbol(/* IncludeBaseRegion */ true);
  if (!Sym)
    return ConditionTruthVal();

  return getStateManager().ConstraintMgr->isNull(this, Sym);
}

ProgramStateRef ProgramStateManager::getInitialState(const LocationContext *InitLoc) {
  ProgramState State(this,
                EnvMgr.getInitialEnvironment(),
                StoreMgr->getInitialStore(InitLoc),
                GDMFactory.getEmptyMap());

  return getPersistentState(State);
}

ProgramStateRef ProgramStateManager::getPersistentStateWithGDM(
                                                     ProgramStateRef FromState,
                                                     ProgramStateRef GDMState) {
  ProgramState NewState(*FromState);
  NewState.GDM = GDMState->GDM;
  return getPersistentState(NewState);
}

ProgramStateRef ProgramStateManager::getPersistentState(ProgramState &State) {

  llvm::FoldingSetNodeID ID;
  State.Profile(ID);
  void *InsertPos;

  if (ProgramState *I = StateSet.FindNodeOrInsertPos(ID, InsertPos))
    return I;

  ProgramState *newState = nullptr;
  if (!freeStates.empty()) {
    newState = freeStates.back();
    freeStates.pop_back();
  }
  else {
    newState = Alloc.Allocate<ProgramState>();
  }
  new (newState) ProgramState(State);
  StateSet.InsertNode(newState, InsertPos);
  return newState;
}

ProgramStateRef ProgramState::makeWithStore(const StoreRef &store) const {
  ProgramState NewSt(*this);
  NewSt.setStore(store);
  return getStateManager().getPersistentState(NewSt);
}

ProgramStateRef ProgramState::cloneAsPosteriorlyOverconstrained() const {
  ProgramState NewSt(*this);
  NewSt.PosteriorlyOverconstrained = true;
  return getStateManager().getPersistentState(NewSt);
}

void ProgramState::setStore(const StoreRef &newStore) {
  Store newStoreStore = newStore.getStore();
  if (newStoreStore)
    stateMgr->getStoreManager().incrementReferenceCount(newStoreStore);
  if (store)
    stateMgr->getStoreManager().decrementReferenceCount(store);
  store = newStoreStore;
}

SVal ProgramState::getLValue(const FieldDecl *D, SVal Base) const {
  Base = wrapSymbolicRegion(Base);
  return getStateManager().StoreMgr->getLValueField(D, Base);
}

SVal ProgramState::getLValue(const IndirectFieldDecl *D, SVal Base) const {
  StoreManager &SM = *getStateManager().StoreMgr;
  Base = wrapSymbolicRegion(Base);

  // FIXME: This should work with `SM.getLValueField(D->getAnonField(), Base)`,
  // but that would break some tests. There is probably a bug somewhere that it
  // would expose.
  for (const auto *I : D->chain()) {
    Base = SM.getLValueField(cast<FieldDecl>(I), Base);
  }
  return Base;
}

//===----------------------------------------------------------------------===//
//  State pretty-printing.
//===----------------------------------------------------------------------===//

void ProgramState::printJson(raw_ostream &Out, const LocationContext *LCtx,
                             const char *NL, unsigned int Space,
                             bool IsDot) const {
  Indent(Out, Space, IsDot) << "\"program_state\": {" << NL;
  ++Space;

  ProgramStateManager &Mgr = getStateManager();

  // Print the store.
  Mgr.getStoreManager().printJson(Out, getStore(), NL, Space, IsDot);

  // Print out the environment.
  Env.printJson(Out, Mgr.getContext(), LCtx, NL, Space, IsDot);

  // Print out the constraints.
  Mgr.getConstraintManager().printJson(Out, this, NL, Space, IsDot);

  // Print out the tracked dynamic types.
  printDynamicTypeInfoJson(Out, this, NL, Space, IsDot);

  // Print checker-specific data.
  Mgr.getOwningEngine().printJson(Out, this, LCtx, NL, Space, IsDot);

  --Space;
  Indent(Out, Space, IsDot) << '}';
}

void ProgramState::printDOT(raw_ostream &Out, const LocationContext *LCtx,
                            unsigned int Space) const {
  printJson(Out, LCtx, /*NL=*/"\\l", Space, /*IsDot=*/true);
}

LLVM_DUMP_METHOD void ProgramState::dump() const {
  printJson(llvm::errs());
}

AnalysisManager& ProgramState::getAnalysisManager() const {
  return stateMgr->getOwningEngine().getAnalysisManager();
}

//===----------------------------------------------------------------------===//
// Generic Data Map.
//===----------------------------------------------------------------------===//

void *const* ProgramState::FindGDM(void *K) const {
  return GDM.lookup(K);
}

void*
ProgramStateManager::FindGDMContext(void *K,
                               void *(*CreateContext)(llvm::BumpPtrAllocator&),
                               void (*DeleteContext)(void*)) {

  std::pair<void*, void (*)(void*)>& p = GDMContexts[K];
  if (!p.first) {
    p.first = CreateContext(Alloc);
    p.second = DeleteContext;
  }

  return p.first;
}

ProgramStateRef ProgramStateManager::addGDM(ProgramStateRef St, void *Key, void *Data){
  ProgramState::GenericDataMap M1 = St->getGDM();
  ProgramState::GenericDataMap M2 = GDMFactory.add(M1, Key, Data);

  if (M1 == M2)
    return St;

  ProgramState NewSt = *St;
  NewSt.GDM = M2;
  return getPersistentState(NewSt);
}

ProgramStateRef ProgramStateManager::removeGDM(ProgramStateRef state, void *Key) {
  ProgramState::GenericDataMap OldM = state->getGDM();
  ProgramState::GenericDataMap NewM = GDMFactory.remove(OldM, Key);

  if (NewM == OldM)
    return state;

  ProgramState NewState = *state;
  NewState.GDM = NewM;
  return getPersistentState(NewState);
}

bool ScanReachableSymbols::scan(nonloc::LazyCompoundVal val) {
  bool wasVisited = !visited.insert(val.getCVData()).second;
  if (wasVisited)
    return true;

  StoreManager &StoreMgr = state->getStateManager().getStoreManager();
  // FIXME: We don't really want to use getBaseRegion() here because pointer
  // arithmetic doesn't apply, but scanReachableSymbols only accepts base
  // regions right now.
  const MemRegion *R = val.getRegion()->getBaseRegion();
  return StoreMgr.scanReachableSymbols(val.getStore(), R, *this);
}

bool ScanReachableSymbols::scan(nonloc::CompoundVal val) {
  for (SVal V : val)
    if (!scan(V))
      return false;

  return true;
}

bool ScanReachableSymbols::scan(const SymExpr *sym) {
  for (SymbolRef SubSym : sym->symbols()) {
    bool wasVisited = !visited.insert(SubSym).second;
    if (wasVisited)
      continue;

    if (!visitor.VisitSymbol(SubSym))
      return false;
  }

  return true;
}

bool ScanReachableSymbols::scan(SVal val) {
  if (std::optional<loc::MemRegionVal> X = val.getAs<loc::MemRegionVal>())
    return scan(X->getRegion());

  if (std::optional<nonloc::LazyCompoundVal> X =
          val.getAs<nonloc::LazyCompoundVal>())
    return scan(*X);

  if (std::optional<nonloc::LocAsInteger> X = val.getAs<nonloc::LocAsInteger>())
    return scan(X->getLoc());

  if (SymbolRef Sym = val.getAsSymbol())
    return scan(Sym);

  if (std::optional<nonloc::CompoundVal> X = val.getAs<nonloc::CompoundVal>())
    return scan(*X);

  return true;
}

bool ScanReachableSymbols::scan(const MemRegion *R) {
  if (isa<MemSpaceRegion>(R))
    return true;

  bool wasVisited = !visited.insert(R).second;
  if (wasVisited)
    return true;

  if (!visitor.VisitMemRegion(R))
    return false;

  // If this is a symbolic region, visit the symbol for the region.
  if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(R))
    if (!visitor.VisitSymbol(SR->getSymbol()))
      return false;

  // If this is a subregion, also visit the parent regions.
  if (const SubRegion *SR = dyn_cast<SubRegion>(R)) {
    const MemRegion *Super = SR->getSuperRegion();
    if (!scan(Super))
      return false;

    // When we reach the topmost region, scan all symbols in it.
    if (isa<MemSpaceRegion>(Super)) {
      StoreManager &StoreMgr = state->getStateManager().getStoreManager();
      if (!StoreMgr.scanReachableSymbols(state->getStore(), SR, *this))
        return false;
    }
  }

  // Regions captured by a block are also implicitly reachable.
  if (const BlockDataRegion *BDR = dyn_cast<BlockDataRegion>(R)) {
    for (auto Var : BDR->referenced_vars()) {
      if (!scan(Var.getCapturedRegion()))
        return false;
    }
  }

  return true;
}

bool ProgramState::scanReachableSymbols(SVal val, SymbolVisitor& visitor) const {
  ScanReachableSymbols S(this, visitor);
  return S.scan(val);
}

bool ProgramState::scanReachableSymbols(
    llvm::iterator_range<region_iterator> Reachable,
    SymbolVisitor &visitor) const {
  ScanReachableSymbols S(this, visitor);
  for (const MemRegion *R : Reachable) {
    if (!S.scan(R))
      return false;
  }
  return true;
}
