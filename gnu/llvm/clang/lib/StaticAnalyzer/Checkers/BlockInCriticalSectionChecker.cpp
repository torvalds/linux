//===-- BlockInCriticalSectionChecker.cpp -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a checker for blocks in critical sections. This checker should find
// the calls to blocking functions (for example: sleep, getc, fgets, read,
// recv etc.) inside a critical section. When sleep(x) is called while a mutex
// is held, other threades cannot lock the same mutex. This might take some
// time, leading to bad performance or even deadlock.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"

#include <iterator>
#include <utility>
#include <variant>

using namespace clang;
using namespace ento;

namespace {

struct CritSectionMarker {
  const Expr *LockExpr{};
  const MemRegion *LockReg{};

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.Add(LockExpr);
    ID.Add(LockReg);
  }

  [[nodiscard]] constexpr bool
  operator==(const CritSectionMarker &Other) const noexcept {
    return LockExpr == Other.LockExpr && LockReg == Other.LockReg;
  }
  [[nodiscard]] constexpr bool
  operator!=(const CritSectionMarker &Other) const noexcept {
    return !(*this == Other);
  }
};

class CallDescriptionBasedMatcher {
  CallDescription LockFn;
  CallDescription UnlockFn;

public:
  CallDescriptionBasedMatcher(CallDescription &&LockFn,
                              CallDescription &&UnlockFn)
      : LockFn(std::move(LockFn)), UnlockFn(std::move(UnlockFn)) {}
  [[nodiscard]] bool matches(const CallEvent &Call, bool IsLock) const {
    if (IsLock) {
      return LockFn.matches(Call);
    }
    return UnlockFn.matches(Call);
  }
};

class FirstArgMutexDescriptor : public CallDescriptionBasedMatcher {
public:
  FirstArgMutexDescriptor(CallDescription &&LockFn, CallDescription &&UnlockFn)
      : CallDescriptionBasedMatcher(std::move(LockFn), std::move(UnlockFn)) {}

  [[nodiscard]] const MemRegion *getRegion(const CallEvent &Call, bool) const {
    return Call.getArgSVal(0).getAsRegion();
  }
};

class MemberMutexDescriptor : public CallDescriptionBasedMatcher {
public:
  MemberMutexDescriptor(CallDescription &&LockFn, CallDescription &&UnlockFn)
      : CallDescriptionBasedMatcher(std::move(LockFn), std::move(UnlockFn)) {}

  [[nodiscard]] const MemRegion *getRegion(const CallEvent &Call, bool) const {
    return cast<CXXMemberCall>(Call).getCXXThisVal().getAsRegion();
  }
};

class RAIIMutexDescriptor {
  mutable const IdentifierInfo *Guard{};
  mutable bool IdentifierInfoInitialized{};
  mutable llvm::SmallString<32> GuardName{};

  void initIdentifierInfo(const CallEvent &Call) const {
    if (!IdentifierInfoInitialized) {
      // In case of checking C code, or when the corresponding headers are not
      // included, we might end up query the identifier table every time when
      // this function is called instead of early returning it. To avoid this, a
      // bool variable (IdentifierInfoInitialized) is used and the function will
      // be run only once.
      const auto &ASTCtx = Call.getState()->getStateManager().getContext();
      Guard = &ASTCtx.Idents.get(GuardName);
    }
  }

  template <typename T> bool matchesImpl(const CallEvent &Call) const {
    const T *C = dyn_cast<T>(&Call);
    if (!C)
      return false;
    const IdentifierInfo *II =
        cast<CXXRecordDecl>(C->getDecl()->getParent())->getIdentifier();
    return II == Guard;
  }

public:
  RAIIMutexDescriptor(StringRef GuardName) : GuardName(GuardName) {}
  [[nodiscard]] bool matches(const CallEvent &Call, bool IsLock) const {
    initIdentifierInfo(Call);
    if (IsLock) {
      return matchesImpl<CXXConstructorCall>(Call);
    }
    return matchesImpl<CXXDestructorCall>(Call);
  }
  [[nodiscard]] const MemRegion *getRegion(const CallEvent &Call,
                                           bool IsLock) const {
    const MemRegion *LockRegion = nullptr;
    if (IsLock) {
      if (std::optional<SVal> Object = Call.getReturnValueUnderConstruction()) {
        LockRegion = Object->getAsRegion();
      }
    } else {
      LockRegion = cast<CXXDestructorCall>(Call).getCXXThisVal().getAsRegion();
    }
    return LockRegion;
  }
};

using MutexDescriptor =
    std::variant<FirstArgMutexDescriptor, MemberMutexDescriptor,
                 RAIIMutexDescriptor>;

class BlockInCriticalSectionChecker : public Checker<check::PostCall> {
private:
  const std::array<MutexDescriptor, 8> MutexDescriptors{
      // NOTE: There are standard library implementations where some methods
      // of `std::mutex` are inherited from an implementation detail base
      // class, and those aren't matched by the name specification {"std",
      // "mutex", "lock"}.
      // As a workaround here we omit the class name and only require the
      // presence of the name parts "std" and "lock"/"unlock".
      // TODO: Ensure that CallDescription understands inherited methods.
      MemberMutexDescriptor(
          {/*MatchAs=*/CDM::CXXMethod,
           /*QualifiedName=*/{"std", /*"mutex",*/ "lock"},
           /*RequiredArgs=*/0},
          {CDM::CXXMethod, {"std", /*"mutex",*/ "unlock"}, 0}),
      FirstArgMutexDescriptor({CDM::CLibrary, {"pthread_mutex_lock"}, 1},
                              {CDM::CLibrary, {"pthread_mutex_unlock"}, 1}),
      FirstArgMutexDescriptor({CDM::CLibrary, {"mtx_lock"}, 1},
                              {CDM::CLibrary, {"mtx_unlock"}, 1}),
      FirstArgMutexDescriptor({CDM::CLibrary, {"pthread_mutex_trylock"}, 1},
                              {CDM::CLibrary, {"pthread_mutex_unlock"}, 1}),
      FirstArgMutexDescriptor({CDM::CLibrary, {"mtx_trylock"}, 1},
                              {CDM::CLibrary, {"mtx_unlock"}, 1}),
      FirstArgMutexDescriptor({CDM::CLibrary, {"mtx_timedlock"}, 1},
                              {CDM::CLibrary, {"mtx_unlock"}, 1}),
      RAIIMutexDescriptor("lock_guard"),
      RAIIMutexDescriptor("unique_lock")};

  const CallDescriptionSet BlockingFunctions{{CDM::CLibrary, {"sleep"}},
                                             {CDM::CLibrary, {"getc"}},
                                             {CDM::CLibrary, {"fgets"}},
                                             {CDM::CLibrary, {"read"}},
                                             {CDM::CLibrary, {"recv"}}};

  const BugType BlockInCritSectionBugType{
      this, "Call to blocking function in critical section", "Blocking Error"};

  void reportBlockInCritSection(const CallEvent &call, CheckerContext &C) const;

  [[nodiscard]] const NoteTag *createCritSectionNote(CritSectionMarker M,
                                                     CheckerContext &C) const;

  [[nodiscard]] std::optional<MutexDescriptor>
  checkDescriptorMatch(const CallEvent &Call, CheckerContext &C,
                       bool IsLock) const;

  void handleLock(const MutexDescriptor &Mutex, const CallEvent &Call,
                  CheckerContext &C) const;

  void handleUnlock(const MutexDescriptor &Mutex, const CallEvent &Call,
                    CheckerContext &C) const;

  [[nodiscard]] bool isBlockingInCritSection(const CallEvent &Call,
                                             CheckerContext &C) const;

public:
  /// Process unlock.
  /// Process lock.
  /// Process blocking functions (sleep, getc, fgets, read, recv)
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
};

} // end anonymous namespace

REGISTER_LIST_WITH_PROGRAMSTATE(ActiveCritSections, CritSectionMarker)

// Iterator traits for ImmutableList data structure
// that enable the use of STL algorithms.
// TODO: Move these to llvm::ImmutableList when overhauling immutable data
// structures for proper iterator concept support.
template <>
struct std::iterator_traits<
    typename llvm::ImmutableList<CritSectionMarker>::iterator> {
  using iterator_category = std::forward_iterator_tag;
  using value_type = CritSectionMarker;
  using difference_type = std::ptrdiff_t;
  using reference = CritSectionMarker &;
  using pointer = CritSectionMarker *;
};

std::optional<MutexDescriptor>
BlockInCriticalSectionChecker::checkDescriptorMatch(const CallEvent &Call,
                                                    CheckerContext &C,
                                                    bool IsLock) const {
  const auto Descriptor =
      llvm::find_if(MutexDescriptors, [&Call, IsLock](auto &&Descriptor) {
        return std::visit(
            [&Call, IsLock](auto &&DescriptorImpl) {
              return DescriptorImpl.matches(Call, IsLock);
            },
            Descriptor);
      });
  if (Descriptor != MutexDescriptors.end())
    return *Descriptor;
  return std::nullopt;
}

static const MemRegion *getRegion(const CallEvent &Call,
                                  const MutexDescriptor &Descriptor,
                                  bool IsLock) {
  return std::visit(
      [&Call, IsLock](auto &&Descriptor) {
        return Descriptor.getRegion(Call, IsLock);
      },
      Descriptor);
}

void BlockInCriticalSectionChecker::handleLock(
    const MutexDescriptor &LockDescriptor, const CallEvent &Call,
    CheckerContext &C) const {
  const MemRegion *MutexRegion =
      getRegion(Call, LockDescriptor, /*IsLock=*/true);
  if (!MutexRegion)
    return;

  const CritSectionMarker MarkToAdd{Call.getOriginExpr(), MutexRegion};
  ProgramStateRef StateWithLockEvent =
      C.getState()->add<ActiveCritSections>(MarkToAdd);
  C.addTransition(StateWithLockEvent, createCritSectionNote(MarkToAdd, C));
}

void BlockInCriticalSectionChecker::handleUnlock(
    const MutexDescriptor &UnlockDescriptor, const CallEvent &Call,
    CheckerContext &C) const {
  const MemRegion *MutexRegion =
      getRegion(Call, UnlockDescriptor, /*IsLock=*/false);
  if (!MutexRegion)
    return;

  ProgramStateRef State = C.getState();
  const auto ActiveSections = State->get<ActiveCritSections>();
  const auto MostRecentLock =
      llvm::find_if(ActiveSections, [MutexRegion](auto &&Marker) {
        return Marker.LockReg == MutexRegion;
      });
  if (MostRecentLock == ActiveSections.end())
    return;

  // Build a new ImmutableList without this element.
  auto &Factory = State->get_context<ActiveCritSections>();
  llvm::ImmutableList<CritSectionMarker> NewList = Factory.getEmptyList();
  for (auto It = ActiveSections.begin(), End = ActiveSections.end(); It != End;
       ++It) {
    if (It != MostRecentLock)
      NewList = Factory.add(*It, NewList);
  }

  State = State->set<ActiveCritSections>(NewList);
  C.addTransition(State);
}

bool BlockInCriticalSectionChecker::isBlockingInCritSection(
    const CallEvent &Call, CheckerContext &C) const {
  return BlockingFunctions.contains(Call) &&
         !C.getState()->get<ActiveCritSections>().isEmpty();
}

void BlockInCriticalSectionChecker::checkPostCall(const CallEvent &Call,
                                                  CheckerContext &C) const {
  if (isBlockingInCritSection(Call, C)) {
    reportBlockInCritSection(Call, C);
  } else if (std::optional<MutexDescriptor> LockDesc =
                 checkDescriptorMatch(Call, C, /*IsLock=*/true)) {
    handleLock(*LockDesc, Call, C);
  } else if (std::optional<MutexDescriptor> UnlockDesc =
                 checkDescriptorMatch(Call, C, /*IsLock=*/false)) {
    handleUnlock(*UnlockDesc, Call, C);
  }
}

void BlockInCriticalSectionChecker::reportBlockInCritSection(
    const CallEvent &Call, CheckerContext &C) const {
  ExplodedNode *ErrNode = C.generateNonFatalErrorNode(C.getState());
  if (!ErrNode)
    return;

  std::string msg;
  llvm::raw_string_ostream os(msg);
  os << "Call to blocking function '" << Call.getCalleeIdentifier()->getName()
     << "' inside of critical section";
  auto R = std::make_unique<PathSensitiveBugReport>(BlockInCritSectionBugType,
                                                    os.str(), ErrNode);
  R->addRange(Call.getSourceRange());
  R->markInteresting(Call.getReturnValue());
  C.emitReport(std::move(R));
}

const NoteTag *
BlockInCriticalSectionChecker::createCritSectionNote(CritSectionMarker M,
                                                     CheckerContext &C) const {
  const BugType *BT = &this->BlockInCritSectionBugType;
  return C.getNoteTag([M, BT](PathSensitiveBugReport &BR,
                              llvm::raw_ostream &OS) {
    if (&BR.getBugType() != BT)
      return;

    // Get the lock events for the mutex of the current line's lock event.
    const auto CritSectionBegins =
        BR.getErrorNode()->getState()->get<ActiveCritSections>();
    llvm::SmallVector<CritSectionMarker, 4> LocksForMutex;
    llvm::copy_if(
        CritSectionBegins, std::back_inserter(LocksForMutex),
        [M](const auto &Marker) { return Marker.LockReg == M.LockReg; });
    if (LocksForMutex.empty())
      return;

    // As the ImmutableList builds the locks by prepending them, we
    // reverse the list to get the correct order.
    std::reverse(LocksForMutex.begin(), LocksForMutex.end());

    // Find the index of the lock expression in the list of all locks for a
    // given mutex (in acquisition order).
    const auto Position =
        llvm::find_if(std::as_const(LocksForMutex), [M](const auto &Marker) {
          return Marker.LockExpr == M.LockExpr;
        });
    if (Position == LocksForMutex.end())
      return;

    // If there is only one lock event, we don't need to specify how many times
    // the critical section was entered.
    if (LocksForMutex.size() == 1) {
      OS << "Entering critical section here";
      return;
    }

    const auto IndexOfLock =
        std::distance(std::as_const(LocksForMutex).begin(), Position);

    const auto OrdinalOfLock = IndexOfLock + 1;
    OS << "Entering critical section for the " << OrdinalOfLock
       << llvm::getOrdinalSuffix(OrdinalOfLock) << " time here";
  });
}

void ento::registerBlockInCriticalSectionChecker(CheckerManager &mgr) {
  mgr.registerChecker<BlockInCriticalSectionChecker>();
}

bool ento::shouldRegisterBlockInCriticalSectionChecker(
    const CheckerManager &mgr) {
  return true;
}
