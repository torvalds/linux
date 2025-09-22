//===--- PthreadLockChecker.cpp - Check for locking problems ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines:
//  * PthreadLockChecker, a simple lock -> unlock checker.
//    Which also checks for XNU locks, which behave similarly enough to share
//    code.
//  * FuchsiaLocksChecker, which is also rather similar.
//  * C11LockChecker which also closely follows Pthread semantics.
//
//  TODO: Path notes.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {

struct LockState {
  enum Kind {
    Destroyed,
    Locked,
    Unlocked,
    UntouchedAndPossiblyDestroyed,
    UnlockedAndPossiblyDestroyed
  } K;

private:
  LockState(Kind K) : K(K) {}

public:
  static LockState getLocked() { return LockState(Locked); }
  static LockState getUnlocked() { return LockState(Unlocked); }
  static LockState getDestroyed() { return LockState(Destroyed); }
  static LockState getUntouchedAndPossiblyDestroyed() {
    return LockState(UntouchedAndPossiblyDestroyed);
  }
  static LockState getUnlockedAndPossiblyDestroyed() {
    return LockState(UnlockedAndPossiblyDestroyed);
  }

  bool operator==(const LockState &X) const { return K == X.K; }

  bool isLocked() const { return K == Locked; }
  bool isUnlocked() const { return K == Unlocked; }
  bool isDestroyed() const { return K == Destroyed; }
  bool isUntouchedAndPossiblyDestroyed() const {
    return K == UntouchedAndPossiblyDestroyed;
  }
  bool isUnlockedAndPossiblyDestroyed() const {
    return K == UnlockedAndPossiblyDestroyed;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddInteger(K); }
};

class PthreadLockChecker : public Checker<check::PostCall, check::DeadSymbols,
                                          check::RegionChanges> {
public:
  enum LockingSemantics { NotApplicable = 0, PthreadSemantics, XNUSemantics };
  enum CheckerKind {
    CK_PthreadLockChecker,
    CK_FuchsiaLockChecker,
    CK_C11LockChecker,
    CK_NumCheckKinds
  };
  bool ChecksEnabled[CK_NumCheckKinds] = {false};
  CheckerNameRef CheckNames[CK_NumCheckKinds];

private:
  typedef void (PthreadLockChecker::*FnCheck)(const CallEvent &Call,
                                              CheckerContext &C,
                                              CheckerKind CheckKind) const;
  CallDescriptionMap<FnCheck> PThreadCallbacks = {
      // Init.
      {{CDM::CLibrary, {"pthread_mutex_init"}, 2},
       &PthreadLockChecker::InitAnyLock},
      // TODO: pthread_rwlock_init(2 arguments).
      // TODO: lck_mtx_init(3 arguments).
      // TODO: lck_mtx_alloc_init(2 arguments) => returns the mutex.
      // TODO: lck_rw_init(3 arguments).
      // TODO: lck_rw_alloc_init(2 arguments) => returns the mutex.

      // Acquire.
      {{CDM::CLibrary, {"pthread_mutex_lock"}, 1},
       &PthreadLockChecker::AcquirePthreadLock},
      {{CDM::CLibrary, {"pthread_rwlock_rdlock"}, 1},
       &PthreadLockChecker::AcquirePthreadLock},
      {{CDM::CLibrary, {"pthread_rwlock_wrlock"}, 1},
       &PthreadLockChecker::AcquirePthreadLock},
      {{CDM::CLibrary, {"lck_mtx_lock"}, 1},
       &PthreadLockChecker::AcquireXNULock},
      {{CDM::CLibrary, {"lck_rw_lock_exclusive"}, 1},
       &PthreadLockChecker::AcquireXNULock},
      {{CDM::CLibrary, {"lck_rw_lock_shared"}, 1},
       &PthreadLockChecker::AcquireXNULock},

      // Try.
      {{CDM::CLibrary, {"pthread_mutex_trylock"}, 1},
       &PthreadLockChecker::TryPthreadLock},
      {{CDM::CLibrary, {"pthread_rwlock_tryrdlock"}, 1},
       &PthreadLockChecker::TryPthreadLock},
      {{CDM::CLibrary, {"pthread_rwlock_trywrlock"}, 1},
       &PthreadLockChecker::TryPthreadLock},
      {{CDM::CLibrary, {"lck_mtx_try_lock"}, 1},
       &PthreadLockChecker::TryXNULock},
      {{CDM::CLibrary, {"lck_rw_try_lock_exclusive"}, 1},
       &PthreadLockChecker::TryXNULock},
      {{CDM::CLibrary, {"lck_rw_try_lock_shared"}, 1},
       &PthreadLockChecker::TryXNULock},

      // Release.
      {{CDM::CLibrary, {"pthread_mutex_unlock"}, 1},
       &PthreadLockChecker::ReleaseAnyLock},
      {{CDM::CLibrary, {"pthread_rwlock_unlock"}, 1},
       &PthreadLockChecker::ReleaseAnyLock},
      {{CDM::CLibrary, {"lck_mtx_unlock"}, 1},
       &PthreadLockChecker::ReleaseAnyLock},
      {{CDM::CLibrary, {"lck_rw_unlock_exclusive"}, 1},
       &PthreadLockChecker::ReleaseAnyLock},
      {{CDM::CLibrary, {"lck_rw_unlock_shared"}, 1},
       &PthreadLockChecker::ReleaseAnyLock},
      {{CDM::CLibrary, {"lck_rw_done"}, 1},
       &PthreadLockChecker::ReleaseAnyLock},

      // Destroy.
      {{CDM::CLibrary, {"pthread_mutex_destroy"}, 1},
       &PthreadLockChecker::DestroyPthreadLock},
      {{CDM::CLibrary, {"lck_mtx_destroy"}, 2},
       &PthreadLockChecker::DestroyXNULock},
      // TODO: pthread_rwlock_destroy(1 argument).
      // TODO: lck_rw_destroy(2 arguments).
  };

  CallDescriptionMap<FnCheck> FuchsiaCallbacks = {
      // Init.
      {{CDM::CLibrary, {"spin_lock_init"}, 1},
       &PthreadLockChecker::InitAnyLock},

      // Acquire.
      {{CDM::CLibrary, {"spin_lock"}, 1},
       &PthreadLockChecker::AcquirePthreadLock},
      {{CDM::CLibrary, {"spin_lock_save"}, 3},
       &PthreadLockChecker::AcquirePthreadLock},
      {{CDM::CLibrary, {"sync_mutex_lock"}, 1},
       &PthreadLockChecker::AcquirePthreadLock},
      {{CDM::CLibrary, {"sync_mutex_lock_with_waiter"}, 1},
       &PthreadLockChecker::AcquirePthreadLock},

      // Try.
      {{CDM::CLibrary, {"spin_trylock"}, 1},
       &PthreadLockChecker::TryFuchsiaLock},
      {{CDM::CLibrary, {"sync_mutex_trylock"}, 1},
       &PthreadLockChecker::TryFuchsiaLock},
      {{CDM::CLibrary, {"sync_mutex_timedlock"}, 2},
       &PthreadLockChecker::TryFuchsiaLock},

      // Release.
      {{CDM::CLibrary, {"spin_unlock"}, 1},
       &PthreadLockChecker::ReleaseAnyLock},
      {{CDM::CLibrary, {"spin_unlock_restore"}, 3},
       &PthreadLockChecker::ReleaseAnyLock},
      {{CDM::CLibrary, {"sync_mutex_unlock"}, 1},
       &PthreadLockChecker::ReleaseAnyLock},
  };

  CallDescriptionMap<FnCheck> C11Callbacks = {
      // Init.
      {{CDM::CLibrary, {"mtx_init"}, 2}, &PthreadLockChecker::InitAnyLock},

      // Acquire.
      {{CDM::CLibrary, {"mtx_lock"}, 1},
       &PthreadLockChecker::AcquirePthreadLock},

      // Try.
      {{CDM::CLibrary, {"mtx_trylock"}, 1}, &PthreadLockChecker::TryC11Lock},
      {{CDM::CLibrary, {"mtx_timedlock"}, 2}, &PthreadLockChecker::TryC11Lock},

      // Release.
      {{CDM::CLibrary, {"mtx_unlock"}, 1}, &PthreadLockChecker::ReleaseAnyLock},

      // Destroy
      {{CDM::CLibrary, {"mtx_destroy"}, 1},
       &PthreadLockChecker::DestroyPthreadLock},
  };

  ProgramStateRef resolvePossiblyDestroyedMutex(ProgramStateRef state,
                                                const MemRegion *lockR,
                                                const SymbolRef *sym) const;
  void reportBug(CheckerContext &C, std::unique_ptr<BugType> BT[],
                 const Expr *MtxExpr, CheckerKind CheckKind,
                 StringRef Desc) const;

  // Init.
  void InitAnyLock(const CallEvent &Call, CheckerContext &C,
                   CheckerKind CheckKind) const;
  void InitLockAux(const CallEvent &Call, CheckerContext &C,
                   const Expr *MtxExpr, SVal MtxVal,
                   CheckerKind CheckKind) const;

  // Lock, Try-lock.
  void AcquirePthreadLock(const CallEvent &Call, CheckerContext &C,
                          CheckerKind CheckKind) const;
  void AcquireXNULock(const CallEvent &Call, CheckerContext &C,
                      CheckerKind CheckKind) const;
  void TryPthreadLock(const CallEvent &Call, CheckerContext &C,
                      CheckerKind CheckKind) const;
  void TryXNULock(const CallEvent &Call, CheckerContext &C,
                  CheckerKind CheckKind) const;
  void TryFuchsiaLock(const CallEvent &Call, CheckerContext &C,
                      CheckerKind CheckKind) const;
  void TryC11Lock(const CallEvent &Call, CheckerContext &C,
                  CheckerKind CheckKind) const;
  void AcquireLockAux(const CallEvent &Call, CheckerContext &C,
                      const Expr *MtxExpr, SVal MtxVal, bool IsTryLock,
                      LockingSemantics Semantics, CheckerKind CheckKind) const;

  // Release.
  void ReleaseAnyLock(const CallEvent &Call, CheckerContext &C,
                      CheckerKind CheckKind) const;
  void ReleaseLockAux(const CallEvent &Call, CheckerContext &C,
                      const Expr *MtxExpr, SVal MtxVal,
                      CheckerKind CheckKind) const;

  // Destroy.
  void DestroyPthreadLock(const CallEvent &Call, CheckerContext &C,
                          CheckerKind CheckKind) const;
  void DestroyXNULock(const CallEvent &Call, CheckerContext &C,
                      CheckerKind CheckKind) const;
  void DestroyLockAux(const CallEvent &Call, CheckerContext &C,
                      const Expr *MtxExpr, SVal MtxVal,
                      LockingSemantics Semantics, CheckerKind CheckKind) const;

public:
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  ProgramStateRef
  checkRegionChanges(ProgramStateRef State, const InvalidatedSymbols *Symbols,
                     ArrayRef<const MemRegion *> ExplicitRegions,
                     ArrayRef<const MemRegion *> Regions,
                     const LocationContext *LCtx, const CallEvent *Call) const;
  void printState(raw_ostream &Out, ProgramStateRef State, const char *NL,
                  const char *Sep) const override;

private:
  mutable std::unique_ptr<BugType> BT_doublelock[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_doubleunlock[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_destroylock[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_initlock[CK_NumCheckKinds];
  mutable std::unique_ptr<BugType> BT_lor[CK_NumCheckKinds];

  void initBugType(CheckerKind CheckKind) const {
    if (BT_doublelock[CheckKind])
      return;
    BT_doublelock[CheckKind].reset(
        new BugType{CheckNames[CheckKind], "Double locking", "Lock checker"});
    BT_doubleunlock[CheckKind].reset(
        new BugType{CheckNames[CheckKind], "Double unlocking", "Lock checker"});
    BT_destroylock[CheckKind].reset(new BugType{
        CheckNames[CheckKind], "Use destroyed lock", "Lock checker"});
    BT_initlock[CheckKind].reset(new BugType{
        CheckNames[CheckKind], "Init invalid lock", "Lock checker"});
    BT_lor[CheckKind].reset(new BugType{CheckNames[CheckKind],
                                        "Lock order reversal", "Lock checker"});
  }
};
} // end anonymous namespace

// A stack of locks for tracking lock-unlock order.
REGISTER_LIST_WITH_PROGRAMSTATE(LockSet, const MemRegion *)

// An entry for tracking lock states.
REGISTER_MAP_WITH_PROGRAMSTATE(LockMap, const MemRegion *, LockState)

// Return values for unresolved calls to pthread_mutex_destroy().
REGISTER_MAP_WITH_PROGRAMSTATE(DestroyRetVal, const MemRegion *, SymbolRef)

void PthreadLockChecker::checkPostCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  // FIXME: Try to handle cases when the implementation was inlined rather
  // than just giving up.
  if (C.wasInlined)
    return;

  if (const FnCheck *Callback = PThreadCallbacks.lookup(Call))
    (this->**Callback)(Call, C, CK_PthreadLockChecker);
  else if (const FnCheck *Callback = FuchsiaCallbacks.lookup(Call))
    (this->**Callback)(Call, C, CK_FuchsiaLockChecker);
  else if (const FnCheck *Callback = C11Callbacks.lookup(Call))
    (this->**Callback)(Call, C, CK_C11LockChecker);
}

// When a lock is destroyed, in some semantics(like PthreadSemantics) we are not
// sure if the destroy call has succeeded or failed, and the lock enters one of
// the 'possibly destroyed' state. There is a short time frame for the
// programmer to check the return value to see if the lock was successfully
// destroyed. Before we model the next operation over that lock, we call this
// function to see if the return value was checked by now and set the lock state
// - either to destroyed state or back to its previous state.

// In PthreadSemantics, pthread_mutex_destroy() returns zero if the lock is
// successfully destroyed and it returns a non-zero value otherwise.
ProgramStateRef PthreadLockChecker::resolvePossiblyDestroyedMutex(
    ProgramStateRef state, const MemRegion *lockR, const SymbolRef *sym) const {
  const LockState *lstate = state->get<LockMap>(lockR);
  // Existence in DestroyRetVal ensures existence in LockMap.
  // Existence in Destroyed also ensures that the lock state for lockR is either
  // UntouchedAndPossiblyDestroyed or UnlockedAndPossiblyDestroyed.
  assert(lstate);
  assert(lstate->isUntouchedAndPossiblyDestroyed() ||
         lstate->isUnlockedAndPossiblyDestroyed());

  ConstraintManager &CMgr = state->getConstraintManager();
  ConditionTruthVal retZero = CMgr.isNull(state, *sym);
  if (retZero.isConstrainedFalse()) {
    if (lstate->isUntouchedAndPossiblyDestroyed())
      state = state->remove<LockMap>(lockR);
    else if (lstate->isUnlockedAndPossiblyDestroyed())
      state = state->set<LockMap>(lockR, LockState::getUnlocked());
  } else
    state = state->set<LockMap>(lockR, LockState::getDestroyed());

  // Removing the map entry (lockR, sym) from DestroyRetVal as the lock state is
  // now resolved.
  state = state->remove<DestroyRetVal>(lockR);
  return state;
}

void PthreadLockChecker::printState(raw_ostream &Out, ProgramStateRef State,
                                    const char *NL, const char *Sep) const {
  LockMapTy LM = State->get<LockMap>();
  if (!LM.isEmpty()) {
    Out << Sep << "Mutex states:" << NL;
    for (auto I : LM) {
      I.first->dumpToStream(Out);
      if (I.second.isLocked())
        Out << ": locked";
      else if (I.second.isUnlocked())
        Out << ": unlocked";
      else if (I.second.isDestroyed())
        Out << ": destroyed";
      else if (I.second.isUntouchedAndPossiblyDestroyed())
        Out << ": not tracked, possibly destroyed";
      else if (I.second.isUnlockedAndPossiblyDestroyed())
        Out << ": unlocked, possibly destroyed";
      Out << NL;
    }
  }

  LockSetTy LS = State->get<LockSet>();
  if (!LS.isEmpty()) {
    Out << Sep << "Mutex lock order:" << NL;
    for (auto I : LS) {
      I->dumpToStream(Out);
      Out << NL;
    }
  }

  DestroyRetValTy DRV = State->get<DestroyRetVal>();
  if (!DRV.isEmpty()) {
    Out << Sep << "Mutexes in unresolved possibly destroyed state:" << NL;
    for (auto I : DRV) {
      I.first->dumpToStream(Out);
      Out << ": ";
      I.second->dumpToStream(Out);
      Out << NL;
    }
  }
}

void PthreadLockChecker::AcquirePthreadLock(const CallEvent &Call,
                                            CheckerContext &C,
                                            CheckerKind CheckKind) const {
  AcquireLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), false,
                 PthreadSemantics, CheckKind);
}

void PthreadLockChecker::AcquireXNULock(const CallEvent &Call,
                                        CheckerContext &C,
                                        CheckerKind CheckKind) const {
  AcquireLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), false,
                 XNUSemantics, CheckKind);
}

void PthreadLockChecker::TryPthreadLock(const CallEvent &Call,
                                        CheckerContext &C,
                                        CheckerKind CheckKind) const {
  AcquireLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), true,
                 PthreadSemantics, CheckKind);
}

void PthreadLockChecker::TryXNULock(const CallEvent &Call, CheckerContext &C,
                                    CheckerKind CheckKind) const {
  AcquireLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), true,
                 PthreadSemantics, CheckKind);
}

void PthreadLockChecker::TryFuchsiaLock(const CallEvent &Call,
                                        CheckerContext &C,
                                        CheckerKind CheckKind) const {
  AcquireLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), true,
                 PthreadSemantics, CheckKind);
}

void PthreadLockChecker::TryC11Lock(const CallEvent &Call, CheckerContext &C,
                                    CheckerKind CheckKind) const {
  AcquireLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), true,
                 PthreadSemantics, CheckKind);
}

void PthreadLockChecker::AcquireLockAux(const CallEvent &Call,
                                        CheckerContext &C, const Expr *MtxExpr,
                                        SVal MtxVal, bool IsTryLock,
                                        enum LockingSemantics Semantics,
                                        CheckerKind CheckKind) const {
  if (!ChecksEnabled[CheckKind])
    return;

  const MemRegion *lockR = MtxVal.getAsRegion();
  if (!lockR)
    return;

  ProgramStateRef state = C.getState();
  const SymbolRef *sym = state->get<DestroyRetVal>(lockR);
  if (sym)
    state = resolvePossiblyDestroyedMutex(state, lockR, sym);

  if (const LockState *LState = state->get<LockMap>(lockR)) {
    if (LState->isLocked()) {
      reportBug(C, BT_doublelock, MtxExpr, CheckKind,
                "This lock has already been acquired");
      return;
    } else if (LState->isDestroyed()) {
      reportBug(C, BT_destroylock, MtxExpr, CheckKind,
                "This lock has already been destroyed");
      return;
    }
  }

  ProgramStateRef lockSucc = state;
  if (IsTryLock) {
    // Bifurcate the state, and allow a mode where the lock acquisition fails.
    SVal RetVal = Call.getReturnValue();
    if (auto DefinedRetVal = RetVal.getAs<DefinedSVal>()) {
      ProgramStateRef lockFail;
      switch (Semantics) {
      case PthreadSemantics:
        std::tie(lockFail, lockSucc) = state->assume(*DefinedRetVal);
        break;
      case XNUSemantics:
        std::tie(lockSucc, lockFail) = state->assume(*DefinedRetVal);
        break;
      default:
        llvm_unreachable("Unknown tryLock locking semantics");
      }
      assert(lockFail && lockSucc);
      C.addTransition(lockFail);
    }
    // We might want to handle the case when the mutex lock function was inlined
    // and returned an Unknown or Undefined value.
  } else if (Semantics == PthreadSemantics) {
    // Assume that the return value was 0.
    SVal RetVal = Call.getReturnValue();
    if (auto DefinedRetVal = RetVal.getAs<DefinedSVal>()) {
      // FIXME: If the lock function was inlined and returned true,
      // we need to behave sanely - at least generate sink.
      lockSucc = state->assume(*DefinedRetVal, false);
      assert(lockSucc);
    }
    // We might want to handle the case when the mutex lock function was inlined
    // and returned an Unknown or Undefined value.
  } else {
    // XNU locking semantics return void on non-try locks
    assert((Semantics == XNUSemantics) && "Unknown locking semantics");
    lockSucc = state;
  }

  // Record that the lock was acquired.
  lockSucc = lockSucc->add<LockSet>(lockR);
  lockSucc = lockSucc->set<LockMap>(lockR, LockState::getLocked());
  C.addTransition(lockSucc);
}

void PthreadLockChecker::ReleaseAnyLock(const CallEvent &Call,
                                        CheckerContext &C,
                                        CheckerKind CheckKind) const {
  ReleaseLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), CheckKind);
}

void PthreadLockChecker::ReleaseLockAux(const CallEvent &Call,
                                        CheckerContext &C, const Expr *MtxExpr,
                                        SVal MtxVal,
                                        CheckerKind CheckKind) const {
  if (!ChecksEnabled[CheckKind])
    return;

  const MemRegion *lockR = MtxVal.getAsRegion();
  if (!lockR)
    return;

  ProgramStateRef state = C.getState();
  const SymbolRef *sym = state->get<DestroyRetVal>(lockR);
  if (sym)
    state = resolvePossiblyDestroyedMutex(state, lockR, sym);

  if (const LockState *LState = state->get<LockMap>(lockR)) {
    if (LState->isUnlocked()) {
      reportBug(C, BT_doubleunlock, MtxExpr, CheckKind,
                "This lock has already been unlocked");
      return;
    } else if (LState->isDestroyed()) {
      reportBug(C, BT_destroylock, MtxExpr, CheckKind,
                "This lock has already been destroyed");
      return;
    }
  }

  LockSetTy LS = state->get<LockSet>();

  if (!LS.isEmpty()) {
    const MemRegion *firstLockR = LS.getHead();
    if (firstLockR != lockR) {
      reportBug(C, BT_lor, MtxExpr, CheckKind,
                "This was not the most recently acquired lock. Possible lock "
                "order reversal");
      return;
    }
    // Record that the lock was released.
    state = state->set<LockSet>(LS.getTail());
  }

  state = state->set<LockMap>(lockR, LockState::getUnlocked());
  C.addTransition(state);
}

void PthreadLockChecker::DestroyPthreadLock(const CallEvent &Call,
                                            CheckerContext &C,
                                            CheckerKind CheckKind) const {
  DestroyLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0),
                 PthreadSemantics, CheckKind);
}

void PthreadLockChecker::DestroyXNULock(const CallEvent &Call,
                                        CheckerContext &C,
                                        CheckerKind CheckKind) const {
  DestroyLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), XNUSemantics,
                 CheckKind);
}

void PthreadLockChecker::DestroyLockAux(const CallEvent &Call,
                                        CheckerContext &C, const Expr *MtxExpr,
                                        SVal MtxVal,
                                        enum LockingSemantics Semantics,
                                        CheckerKind CheckKind) const {
  if (!ChecksEnabled[CheckKind])
    return;

  const MemRegion *LockR = MtxVal.getAsRegion();
  if (!LockR)
    return;

  ProgramStateRef State = C.getState();

  const SymbolRef *sym = State->get<DestroyRetVal>(LockR);
  if (sym)
    State = resolvePossiblyDestroyedMutex(State, LockR, sym);

  const LockState *LState = State->get<LockMap>(LockR);
  // Checking the return value of the destroy method only in the case of
  // PthreadSemantics
  if (Semantics == PthreadSemantics) {
    if (!LState || LState->isUnlocked()) {
      SymbolRef sym = Call.getReturnValue().getAsSymbol();
      if (!sym) {
        State = State->remove<LockMap>(LockR);
        C.addTransition(State);
        return;
      }
      State = State->set<DestroyRetVal>(LockR, sym);
      if (LState && LState->isUnlocked())
        State = State->set<LockMap>(
            LockR, LockState::getUnlockedAndPossiblyDestroyed());
      else
        State = State->set<LockMap>(
            LockR, LockState::getUntouchedAndPossiblyDestroyed());
      C.addTransition(State);
      return;
    }
  } else {
    if (!LState || LState->isUnlocked()) {
      State = State->set<LockMap>(LockR, LockState::getDestroyed());
      C.addTransition(State);
      return;
    }
  }

  StringRef Message = LState->isLocked()
                          ? "This lock is still locked"
                          : "This lock has already been destroyed";

  reportBug(C, BT_destroylock, MtxExpr, CheckKind, Message);
}

void PthreadLockChecker::InitAnyLock(const CallEvent &Call, CheckerContext &C,
                                     CheckerKind CheckKind) const {
  InitLockAux(Call, C, Call.getArgExpr(0), Call.getArgSVal(0), CheckKind);
}

void PthreadLockChecker::InitLockAux(const CallEvent &Call, CheckerContext &C,
                                     const Expr *MtxExpr, SVal MtxVal,
                                     CheckerKind CheckKind) const {
  if (!ChecksEnabled[CheckKind])
    return;

  const MemRegion *LockR = MtxVal.getAsRegion();
  if (!LockR)
    return;

  ProgramStateRef State = C.getState();

  const SymbolRef *sym = State->get<DestroyRetVal>(LockR);
  if (sym)
    State = resolvePossiblyDestroyedMutex(State, LockR, sym);

  const struct LockState *LState = State->get<LockMap>(LockR);
  if (!LState || LState->isDestroyed()) {
    State = State->set<LockMap>(LockR, LockState::getUnlocked());
    C.addTransition(State);
    return;
  }

  StringRef Message = LState->isLocked()
                          ? "This lock is still being held"
                          : "This lock has already been initialized";

  reportBug(C, BT_initlock, MtxExpr, CheckKind, Message);
}

void PthreadLockChecker::reportBug(CheckerContext &C,
                                   std::unique_ptr<BugType> BT[],
                                   const Expr *MtxExpr, CheckerKind CheckKind,
                                   StringRef Desc) const {
  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;
  initBugType(CheckKind);
  auto Report =
      std::make_unique<PathSensitiveBugReport>(*BT[CheckKind], Desc, N);
  Report->addRange(MtxExpr->getSourceRange());
  C.emitReport(std::move(Report));
}

void PthreadLockChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                          CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  for (auto I : State->get<DestroyRetVal>()) {
    // Once the return value symbol dies, no more checks can be performed
    // against it. See if the return value was checked before this point.
    // This would remove the symbol from the map as well.
    if (SymReaper.isDead(I.second))
      State = resolvePossiblyDestroyedMutex(State, I.first, &I.second);
  }

  for (auto I : State->get<LockMap>()) {
    // Stop tracking dead mutex regions as well.
    if (!SymReaper.isLiveRegion(I.first)) {
      State = State->remove<LockMap>(I.first);
      State = State->remove<DestroyRetVal>(I.first);
    }
  }

  // TODO: We probably need to clean up the lock stack as well.
  // It is tricky though: even if the mutex cannot be unlocked anymore,
  // it can still participate in lock order reversal resolution.

  C.addTransition(State);
}

ProgramStateRef PthreadLockChecker::checkRegionChanges(
    ProgramStateRef State, const InvalidatedSymbols *Symbols,
    ArrayRef<const MemRegion *> ExplicitRegions,
    ArrayRef<const MemRegion *> Regions, const LocationContext *LCtx,
    const CallEvent *Call) const {

  bool IsLibraryFunction = false;
  if (Call && Call->isGlobalCFunction()) {
    // Avoid invalidating mutex state when a known supported function is called.
    if (PThreadCallbacks.lookup(*Call) || FuchsiaCallbacks.lookup(*Call) ||
        C11Callbacks.lookup(*Call))
      return State;

    if (Call->isInSystemHeader())
      IsLibraryFunction = true;
  }

  for (auto R : Regions) {
    // We assume that system library function wouldn't touch the mutex unless
    // it takes the mutex explicitly as an argument.
    // FIXME: This is a bit quadratic.
    if (IsLibraryFunction && !llvm::is_contained(ExplicitRegions, R))
      continue;

    State = State->remove<LockMap>(R);
    State = State->remove<DestroyRetVal>(R);

    // TODO: We need to invalidate the lock stack as well. This is tricky
    // to implement correctly and efficiently though, because the effects
    // of mutex escapes on lock order may be fairly varied.
  }

  return State;
}

void ento::registerPthreadLockBase(CheckerManager &mgr) {
  mgr.registerChecker<PthreadLockChecker>();
}

bool ento::shouldRegisterPthreadLockBase(const CheckerManager &mgr) { return true; }

#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name(CheckerManager &mgr) {                             \
    PthreadLockChecker *checker = mgr.getChecker<PthreadLockChecker>();        \
    checker->ChecksEnabled[PthreadLockChecker::CK_##name] = true;              \
    checker->CheckNames[PthreadLockChecker::CK_##name] =                       \
        mgr.getCurrentCheckerName();                                           \
  }                                                                            \
                                                                               \
  bool ento::shouldRegister##name(const CheckerManager &mgr) { return true; }

REGISTER_CHECKER(PthreadLockChecker)
REGISTER_CHECKER(FuchsiaLockChecker)
REGISTER_CHECKER(C11LockChecker)
