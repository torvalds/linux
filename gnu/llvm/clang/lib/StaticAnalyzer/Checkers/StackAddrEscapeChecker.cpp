//=== StackAddrEscapeChecker.cpp ----------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines stack address leak checker, which checks if an invalid
// stack address is stored into a global or heap location. See CERT DCL30-C.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ExprCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;
using namespace ento;

namespace {
class StackAddrEscapeChecker
    : public Checker<check::PreCall, check::PreStmt<ReturnStmt>,
                     check::EndFunction> {
  mutable IdentifierInfo *dispatch_semaphore_tII = nullptr;
  mutable std::unique_ptr<BugType> BT_stackleak;
  mutable std::unique_ptr<BugType> BT_returnstack;
  mutable std::unique_ptr<BugType> BT_capturedstackasync;
  mutable std::unique_ptr<BugType> BT_capturedstackret;

public:
  enum CheckKind {
    CK_StackAddrEscapeChecker,
    CK_StackAddrAsyncEscapeChecker,
    CK_NumCheckKinds
  };

  bool ChecksEnabled[CK_NumCheckKinds] = {false};
  CheckerNameRef CheckNames[CK_NumCheckKinds];

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &Ctx) const;

private:
  void checkReturnedBlockCaptures(const BlockDataRegion &B,
                                  CheckerContext &C) const;
  void checkAsyncExecutedBlockCaptures(const BlockDataRegion &B,
                                       CheckerContext &C) const;
  void EmitStackError(CheckerContext &C, const MemRegion *R,
                      const Expr *RetE) const;
  bool isSemaphoreCaptured(const BlockDecl &B) const;
  static SourceRange genName(raw_ostream &os, const MemRegion *R,
                             ASTContext &Ctx);
  static SmallVector<const MemRegion *, 4>
  getCapturedStackRegions(const BlockDataRegion &B, CheckerContext &C);
  static bool isNotInCurrentFrame(const MemRegion *R, CheckerContext &C);
};
} // namespace

SourceRange StackAddrEscapeChecker::genName(raw_ostream &os, const MemRegion *R,
                                            ASTContext &Ctx) {
  // Get the base region, stripping away fields and elements.
  R = R->getBaseRegion();
  SourceManager &SM = Ctx.getSourceManager();
  SourceRange range;
  os << "Address of ";

  // Check if the region is a compound literal.
  if (const auto *CR = dyn_cast<CompoundLiteralRegion>(R)) {
    const CompoundLiteralExpr *CL = CR->getLiteralExpr();
    os << "stack memory associated with a compound literal "
          "declared on line "
       << SM.getExpansionLineNumber(CL->getBeginLoc()) << " returned to caller";
    range = CL->getSourceRange();
  } else if (const auto *AR = dyn_cast<AllocaRegion>(R)) {
    const Expr *ARE = AR->getExpr();
    SourceLocation L = ARE->getBeginLoc();
    range = ARE->getSourceRange();
    os << "stack memory allocated by call to alloca() on line "
       << SM.getExpansionLineNumber(L);
  } else if (const auto *BR = dyn_cast<BlockDataRegion>(R)) {
    const BlockDecl *BD = BR->getCodeRegion()->getDecl();
    SourceLocation L = BD->getBeginLoc();
    range = BD->getSourceRange();
    os << "stack-allocated block declared on line "
       << SM.getExpansionLineNumber(L);
  } else if (const auto *VR = dyn_cast<VarRegion>(R)) {
    os << "stack memory associated with local variable '" << VR->getString()
       << '\'';
    range = VR->getDecl()->getSourceRange();
  } else if (const auto *LER = dyn_cast<CXXLifetimeExtendedObjectRegion>(R)) {
    QualType Ty = LER->getValueType().getLocalUnqualifiedType();
    os << "stack memory associated with temporary object of type '";
    Ty.print(os, Ctx.getPrintingPolicy());
    os << "' lifetime extended by local variable";
    if (const IdentifierInfo *ID = LER->getExtendingDecl()->getIdentifier())
      os << " '" << ID->getName() << '\'';
    range = LER->getExpr()->getSourceRange();
  } else if (const auto *TOR = dyn_cast<CXXTempObjectRegion>(R)) {
    QualType Ty = TOR->getValueType().getLocalUnqualifiedType();
    os << "stack memory associated with temporary object of type '";
    Ty.print(os, Ctx.getPrintingPolicy());
    os << "'";
    range = TOR->getExpr()->getSourceRange();
  } else {
    llvm_unreachable("Invalid region in ReturnStackAddressChecker.");
  }

  return range;
}

bool StackAddrEscapeChecker::isNotInCurrentFrame(const MemRegion *R,
                                                 CheckerContext &C) {
  const StackSpaceRegion *S = cast<StackSpaceRegion>(R->getMemorySpace());
  return S->getStackFrame() != C.getStackFrame();
}

bool StackAddrEscapeChecker::isSemaphoreCaptured(const BlockDecl &B) const {
  if (!dispatch_semaphore_tII)
    dispatch_semaphore_tII = &B.getASTContext().Idents.get("dispatch_semaphore_t");
  for (const auto &C : B.captures()) {
    const auto *T = C.getVariable()->getType()->getAs<TypedefType>();
    if (T && T->getDecl()->getIdentifier() == dispatch_semaphore_tII)
      return true;
  }
  return false;
}

SmallVector<const MemRegion *, 4>
StackAddrEscapeChecker::getCapturedStackRegions(const BlockDataRegion &B,
                                                CheckerContext &C) {
  SmallVector<const MemRegion *, 4> Regions;
  for (auto Var : B.referenced_vars()) {
    SVal Val = C.getState()->getSVal(Var.getCapturedRegion());
    const MemRegion *Region = Val.getAsRegion();
    if (Region && isa<StackSpaceRegion>(Region->getMemorySpace()))
      Regions.push_back(Region);
  }
  return Regions;
}

void StackAddrEscapeChecker::EmitStackError(CheckerContext &C,
                                            const MemRegion *R,
                                            const Expr *RetE) const {
  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N)
    return;
  if (!BT_returnstack)
    BT_returnstack = std::make_unique<BugType>(
        CheckNames[CK_StackAddrEscapeChecker],
        "Return of address to stack-allocated memory");
  // Generate a report for this bug.
  SmallString<128> buf;
  llvm::raw_svector_ostream os(buf);
  SourceRange range = genName(os, R, C.getASTContext());
  os << " returned to caller";
  auto report =
      std::make_unique<PathSensitiveBugReport>(*BT_returnstack, os.str(), N);
  report->addRange(RetE->getSourceRange());
  if (range.isValid())
    report->addRange(range);
  C.emitReport(std::move(report));
}

void StackAddrEscapeChecker::checkAsyncExecutedBlockCaptures(
    const BlockDataRegion &B, CheckerContext &C) const {
  // There is a not-too-uncommon idiom
  // where a block passed to dispatch_async captures a semaphore
  // and then the thread (which called dispatch_async) is blocked on waiting
  // for the completion of the execution of the block
  // via dispatch_semaphore_wait. To avoid false-positives (for now)
  // we ignore all the blocks which have captured
  // a variable of the type "dispatch_semaphore_t".
  if (isSemaphoreCaptured(*B.getDecl()))
    return;
  for (const MemRegion *Region : getCapturedStackRegions(B, C)) {
    // The block passed to dispatch_async may capture another block
    // created on the stack. However, there is no leak in this situaton,
    // no matter if ARC or no ARC is enabled:
    // dispatch_async copies the passed "outer" block (via Block_copy)
    // and if the block has captured another "inner" block,
    // the "inner" block will be copied as well.
    if (isa<BlockDataRegion>(Region))
      continue;
    ExplodedNode *N = C.generateNonFatalErrorNode();
    if (!N)
      continue;
    if (!BT_capturedstackasync)
      BT_capturedstackasync = std::make_unique<BugType>(
          CheckNames[CK_StackAddrAsyncEscapeChecker],
          "Address of stack-allocated memory is captured");
    SmallString<128> Buf;
    llvm::raw_svector_ostream Out(Buf);
    SourceRange Range = genName(Out, Region, C.getASTContext());
    Out << " is captured by an asynchronously-executed block";
    auto Report = std::make_unique<PathSensitiveBugReport>(
        *BT_capturedstackasync, Out.str(), N);
    if (Range.isValid())
      Report->addRange(Range);
    C.emitReport(std::move(Report));
  }
}

void StackAddrEscapeChecker::checkReturnedBlockCaptures(
    const BlockDataRegion &B, CheckerContext &C) const {
  for (const MemRegion *Region : getCapturedStackRegions(B, C)) {
    if (isNotInCurrentFrame(Region, C))
      continue;
    ExplodedNode *N = C.generateNonFatalErrorNode();
    if (!N)
      continue;
    if (!BT_capturedstackret)
      BT_capturedstackret = std::make_unique<BugType>(
          CheckNames[CK_StackAddrEscapeChecker],
          "Address of stack-allocated memory is captured");
    SmallString<128> Buf;
    llvm::raw_svector_ostream Out(Buf);
    SourceRange Range = genName(Out, Region, C.getASTContext());
    Out << " is captured by a returned block";
    auto Report = std::make_unique<PathSensitiveBugReport>(*BT_capturedstackret,
                                                           Out.str(), N);
    if (Range.isValid())
      Report->addRange(Range);
    C.emitReport(std::move(Report));
  }
}

void StackAddrEscapeChecker::checkPreCall(const CallEvent &Call,
                                          CheckerContext &C) const {
  if (!ChecksEnabled[CK_StackAddrAsyncEscapeChecker])
    return;
  if (!Call.isGlobalCFunction("dispatch_after") &&
      !Call.isGlobalCFunction("dispatch_async"))
    return;
  for (unsigned Idx = 0, NumArgs = Call.getNumArgs(); Idx < NumArgs; ++Idx) {
    if (const BlockDataRegion *B = dyn_cast_or_null<BlockDataRegion>(
            Call.getArgSVal(Idx).getAsRegion()))
      checkAsyncExecutedBlockCaptures(*B, C);
  }
}

void StackAddrEscapeChecker::checkPreStmt(const ReturnStmt *RS,
                                          CheckerContext &C) const {
  if (!ChecksEnabled[CK_StackAddrEscapeChecker])
    return;

  const Expr *RetE = RS->getRetValue();
  if (!RetE)
    return;
  RetE = RetE->IgnoreParens();

  SVal V = C.getSVal(RetE);
  const MemRegion *R = V.getAsRegion();
  if (!R)
    return;

  if (const BlockDataRegion *B = dyn_cast<BlockDataRegion>(R))
    checkReturnedBlockCaptures(*B, C);

  if (!isa<StackSpaceRegion>(R->getMemorySpace()) || isNotInCurrentFrame(R, C))
    return;

  // Returning a record by value is fine. (In this case, the returned
  // expression will be a copy-constructor, possibly wrapped in an
  // ExprWithCleanups node.)
  if (const ExprWithCleanups *Cleanup = dyn_cast<ExprWithCleanups>(RetE))
    RetE = Cleanup->getSubExpr();
  if (isa<CXXConstructExpr>(RetE) && RetE->getType()->isRecordType())
    return;

  // The CK_CopyAndAutoreleaseBlockObject cast causes the block to be copied
  // so the stack address is not escaping here.
  if (const auto *ICE = dyn_cast<ImplicitCastExpr>(RetE)) {
    if (isa<BlockDataRegion>(R) &&
        ICE->getCastKind() == CK_CopyAndAutoreleaseBlockObject) {
      return;
    }
  }

  EmitStackError(C, R, RetE);
}

void StackAddrEscapeChecker::checkEndFunction(const ReturnStmt *RS,
                                              CheckerContext &Ctx) const {
  if (!ChecksEnabled[CK_StackAddrEscapeChecker])
    return;

  ProgramStateRef State = Ctx.getState();

  // Iterate over all bindings to global variables and see if it contains
  // a memory region in the stack space.
  class CallBack : public StoreManager::BindingsHandler {
  private:
    CheckerContext &Ctx;
    const StackFrameContext *PoppedFrame;

    /// Look for stack variables referring to popped stack variables.
    /// Returns true only if it found some dangling stack variables
    /// referred by an other stack variable from different stack frame.
    bool checkForDanglingStackVariable(const MemRegion *Referrer,
                                       const MemRegion *Referred) {
      const auto *ReferrerMemSpace =
          Referrer->getMemorySpace()->getAs<StackSpaceRegion>();
      const auto *ReferredMemSpace =
          Referred->getMemorySpace()->getAs<StackSpaceRegion>();

      if (!ReferrerMemSpace || !ReferredMemSpace)
        return false;

      const auto *ReferrerFrame = ReferrerMemSpace->getStackFrame();
      const auto *ReferredFrame = ReferredMemSpace->getStackFrame();

      if (ReferrerMemSpace && ReferredMemSpace) {
        if (ReferredFrame == PoppedFrame &&
            ReferrerFrame->isParentOf(PoppedFrame)) {
          V.emplace_back(Referrer, Referred);
          return true;
        }
      }
      return false;
    }

  public:
    SmallVector<std::pair<const MemRegion *, const MemRegion *>, 10> V;

    CallBack(CheckerContext &CC) : Ctx(CC), PoppedFrame(CC.getStackFrame()) {}

    bool HandleBinding(StoreManager &SMgr, Store S, const MemRegion *Region,
                       SVal Val) override {
      const MemRegion *VR = Val.getAsRegion();
      if (!VR)
        return true;

      if (checkForDanglingStackVariable(Region, VR))
        return true;

      // Check the globals for the same.
      if (!isa<GlobalsSpaceRegion>(Region->getMemorySpace()))
        return true;
      if (VR && VR->hasStackStorage() && !isNotInCurrentFrame(VR, Ctx))
        V.emplace_back(Region, VR);
      return true;
    }
  };

  CallBack Cb(Ctx);
  State->getStateManager().getStoreManager().iterBindings(State->getStore(),
                                                          Cb);

  if (Cb.V.empty())
    return;

  // Generate an error node.
  ExplodedNode *N = Ctx.generateNonFatalErrorNode(State);
  if (!N)
    return;

  if (!BT_stackleak)
    BT_stackleak =
        std::make_unique<BugType>(CheckNames[CK_StackAddrEscapeChecker],
                                  "Stack address stored into global variable");

  for (const auto &P : Cb.V) {
    const MemRegion *Referrer = P.first->getBaseRegion();
    const MemRegion *Referred = P.second;

    // Generate a report for this bug.
    const StringRef CommonSuffix =
        "upon returning to the caller.  This will be a dangling reference";
    SmallString<128> Buf;
    llvm::raw_svector_ostream Out(Buf);
    const SourceRange Range = genName(Out, Referred, Ctx.getASTContext());

    if (isa<CXXTempObjectRegion, CXXLifetimeExtendedObjectRegion>(Referrer)) {
      Out << " is still referred to by a temporary object on the stack "
          << CommonSuffix;
      auto Report =
          std::make_unique<PathSensitiveBugReport>(*BT_stackleak, Out.str(), N);
      if (Range.isValid())
        Report->addRange(Range);
      Ctx.emitReport(std::move(Report));
      return;
    }

    const StringRef ReferrerMemorySpace = [](const MemSpaceRegion *Space) {
      if (isa<StaticGlobalSpaceRegion>(Space))
        return "static";
      if (isa<GlobalsSpaceRegion>(Space))
        return "global";
      assert(isa<StackSpaceRegion>(Space));
      return "stack";
    }(Referrer->getMemorySpace());

    // We should really only have VarRegions here.
    // Anything else is really surprising, and we should get notified if such
    // ever happens.
    const auto *ReferrerVar = dyn_cast<VarRegion>(Referrer);
    if (!ReferrerVar) {
      assert(false && "We should have a VarRegion here");
      continue; // Defensively skip this one.
    }
    const std::string ReferrerVarName =
        ReferrerVar->getDecl()->getDeclName().getAsString();

    Out << " is still referred to by the " << ReferrerMemorySpace
        << " variable '" << ReferrerVarName << "' " << CommonSuffix;
    auto Report =
        std::make_unique<PathSensitiveBugReport>(*BT_stackleak, Out.str(), N);
    if (Range.isValid())
      Report->addRange(Range);

    Ctx.emitReport(std::move(Report));
  }
}

void ento::registerStackAddrEscapeBase(CheckerManager &mgr) {
  mgr.registerChecker<StackAddrEscapeChecker>();
}

bool ento::shouldRegisterStackAddrEscapeBase(const CheckerManager &mgr) {
  return true;
}

#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name(CheckerManager &Mgr) {                             \
    StackAddrEscapeChecker *Chk = Mgr.getChecker<StackAddrEscapeChecker>();    \
    Chk->ChecksEnabled[StackAddrEscapeChecker::CK_##name] = true;              \
    Chk->CheckNames[StackAddrEscapeChecker::CK_##name] =                       \
        Mgr.getCurrentCheckerName();                                           \
  }                                                                            \
                                                                               \
  bool ento::shouldRegister##name(const CheckerManager &mgr) { return true; }

REGISTER_CHECKER(StackAddrEscapeChecker)
REGISTER_CHECKER(StackAddrAsyncEscapeChecker)
