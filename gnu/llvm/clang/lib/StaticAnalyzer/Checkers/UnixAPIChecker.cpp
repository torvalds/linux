//= UnixAPIChecker.h - Checks preconditions for various Unix APIs --*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines UnixAPIChecker, which is an assortment of checks on calls
// to various, widely used UNIX/Posix functions.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace clang;
using namespace ento;

enum class OpenVariant {
  /// The standard open() call:
  ///    int open(const char *path, int oflag, ...);
  Open,

  /// The variant taking a directory file descriptor and a relative path:
  ///    int openat(int fd, const char *path, int oflag, ...);
  OpenAt
};

namespace {

class UnixAPIMisuseChecker
    : public Checker<check::PreCall, check::ASTDecl<TranslationUnitDecl>> {
  const BugType BT_open{this, "Improper use of 'open'", categories::UnixAPI};
  const BugType BT_getline{this, "Improper use of getdelim",
                           categories::UnixAPI};
  const BugType BT_pthreadOnce{this, "Improper use of 'pthread_once'",
                               categories::UnixAPI};
  const BugType BT_ArgumentNull{this, "NULL pointer", categories::UnixAPI};
  mutable std::optional<uint64_t> Val_O_CREAT;

  ProgramStateRef
  EnsurePtrNotNull(SVal PtrVal, const Expr *PtrExpr, CheckerContext &C,
                   ProgramStateRef State, const StringRef PtrDescr,
                   std::optional<std::reference_wrapper<const BugType>> BT =
                       std::nullopt) const;

  ProgramStateRef EnsureGetdelimBufferAndSizeCorrect(
      SVal LinePtrPtrSVal, SVal SizePtrSVal, const Expr *LinePtrPtrExpr,
      const Expr *SizePtrExpr, CheckerContext &C, ProgramStateRef State) const;

public:
  void checkASTDecl(const TranslationUnitDecl *TU, AnalysisManager &Mgr,
                    BugReporter &BR) const;

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;

  void CheckOpen(CheckerContext &C, const CallEvent &Call) const;
  void CheckOpenAt(CheckerContext &C, const CallEvent &Call) const;
  void CheckGetDelim(CheckerContext &C, const CallEvent &Call) const;
  void CheckPthreadOnce(CheckerContext &C, const CallEvent &Call) const;

  void CheckOpenVariant(CheckerContext &C, const CallEvent &Call,
                        OpenVariant Variant) const;

  void ReportOpenBug(CheckerContext &C, ProgramStateRef State, const char *Msg,
                     SourceRange SR) const;
};

class UnixAPIPortabilityChecker : public Checker< check::PreStmt<CallExpr> > {
public:
  void checkPreStmt(const CallExpr *CE, CheckerContext &C) const;

private:
  const BugType BT_mallocZero{
      this, "Undefined allocation of 0 bytes (CERT MEM04-C; CWE-131)",
      categories::UnixAPI};

  void CheckCallocZero(CheckerContext &C, const CallExpr *CE) const;
  void CheckMallocZero(CheckerContext &C, const CallExpr *CE) const;
  void CheckReallocZero(CheckerContext &C, const CallExpr *CE) const;
  void CheckReallocfZero(CheckerContext &C, const CallExpr *CE) const;
  void CheckAllocaZero(CheckerContext &C, const CallExpr *CE) const;
  void CheckAllocaWithAlignZero(CheckerContext &C, const CallExpr *CE) const;
  void CheckVallocZero(CheckerContext &C, const CallExpr *CE) const;

  bool ReportZeroByteAllocation(CheckerContext &C,
                                ProgramStateRef falseState,
                                const Expr *arg,
                                const char *fn_name) const;
  void BasicAllocationCheck(CheckerContext &C,
                            const CallExpr *CE,
                            const unsigned numArgs,
                            const unsigned sizeArg,
                            const char *fn) const;
};

} // end anonymous namespace

ProgramStateRef UnixAPIMisuseChecker::EnsurePtrNotNull(
    SVal PtrVal, const Expr *PtrExpr, CheckerContext &C, ProgramStateRef State,
    const StringRef PtrDescr,
    std::optional<std::reference_wrapper<const BugType>> BT) const {
  const auto Ptr = PtrVal.getAs<DefinedSVal>();
  if (!Ptr)
    return State;

  const auto [PtrNotNull, PtrNull] = State->assume(*Ptr);
  if (!PtrNotNull && PtrNull) {
    if (ExplodedNode *N = C.generateErrorNode(PtrNull)) {
      auto R = std::make_unique<PathSensitiveBugReport>(
          BT.value_or(std::cref(BT_ArgumentNull)),
          (PtrDescr + " pointer might be NULL.").str(), N);
      if (PtrExpr)
        bugreporter::trackExpressionValue(N, PtrExpr, *R);
      C.emitReport(std::move(R));
    }
    return nullptr;
  }

  return PtrNotNull;
}

void UnixAPIMisuseChecker::checkASTDecl(const TranslationUnitDecl *TU,
                                        AnalysisManager &Mgr,
                                        BugReporter &) const {
  // The definition of O_CREAT is platform specific.
  // Try to get the macro value from the preprocessor.
  Val_O_CREAT = tryExpandAsInteger("O_CREAT", Mgr.getPreprocessor());
  // If we failed, fall-back to known values.
  if (!Val_O_CREAT) {
    if (TU->getASTContext().getTargetInfo().getTriple().getVendor() ==
        llvm::Triple::Apple)
      Val_O_CREAT = 0x0200;
  }
}

//===----------------------------------------------------------------------===//
// "open" (man 2 open)
//===----------------------------------------------------------------------===/

void UnixAPIMisuseChecker::checkPreCall(const CallEvent &Call,
                                        CheckerContext &C) const {
  const FunctionDecl *FD = dyn_cast_if_present<FunctionDecl>(Call.getDecl());
  if (!FD || FD->getKind() != Decl::Function)
    return;

  // Don't treat functions in namespaces with the same name a Unix function
  // as a call to the Unix function.
  const DeclContext *NamespaceCtx = FD->getEnclosingNamespaceContext();
  if (isa_and_nonnull<NamespaceDecl>(NamespaceCtx))
    return;

  StringRef FName = C.getCalleeName(FD);
  if (FName.empty())
    return;

  if (FName == "open")
    CheckOpen(C, Call);

  else if (FName == "openat")
    CheckOpenAt(C, Call);

  else if (FName == "pthread_once")
    CheckPthreadOnce(C, Call);

  else if (is_contained({"getdelim", "getline"}, FName))
    CheckGetDelim(C, Call);
}
void UnixAPIMisuseChecker::ReportOpenBug(CheckerContext &C,
                                         ProgramStateRef State,
                                         const char *Msg,
                                         SourceRange SR) const {
  ExplodedNode *N = C.generateErrorNode(State);
  if (!N)
    return;

  auto Report = std::make_unique<PathSensitiveBugReport>(BT_open, Msg, N);
  Report->addRange(SR);
  C.emitReport(std::move(Report));
}

void UnixAPIMisuseChecker::CheckOpen(CheckerContext &C,
                                     const CallEvent &Call) const {
  CheckOpenVariant(C, Call, OpenVariant::Open);
}

void UnixAPIMisuseChecker::CheckOpenAt(CheckerContext &C,
                                       const CallEvent &Call) const {
  CheckOpenVariant(C, Call, OpenVariant::OpenAt);
}

void UnixAPIMisuseChecker::CheckOpenVariant(CheckerContext &C,
                                            const CallEvent &Call,
                                            OpenVariant Variant) const {
  // The index of the argument taking the flags open flags (O_RDONLY,
  // O_WRONLY, O_CREAT, etc.),
  unsigned int FlagsArgIndex;
  const char *VariantName;
  switch (Variant) {
  case OpenVariant::Open:
    FlagsArgIndex = 1;
    VariantName = "open";
    break;
  case OpenVariant::OpenAt:
    FlagsArgIndex = 2;
    VariantName = "openat";
    break;
  };

  // All calls should at least provide arguments up to the 'flags' parameter.
  unsigned int MinArgCount = FlagsArgIndex + 1;

  // If the flags has O_CREAT set then open/openat() require an additional
  // argument specifying the file mode (permission bits) for the created file.
  unsigned int CreateModeArgIndex = FlagsArgIndex + 1;

  // The create mode argument should be the last argument.
  unsigned int MaxArgCount = CreateModeArgIndex + 1;

  ProgramStateRef state = C.getState();

  if (Call.getNumArgs() < MinArgCount) {
    // The frontend should issue a warning for this case. Just return.
    return;
  } else if (Call.getNumArgs() == MaxArgCount) {
    const Expr *Arg = Call.getArgExpr(CreateModeArgIndex);
    QualType QT = Arg->getType();
    if (!QT->isIntegerType()) {
      SmallString<256> SBuf;
      llvm::raw_svector_ostream OS(SBuf);
      OS << "The " << CreateModeArgIndex + 1
         << llvm::getOrdinalSuffix(CreateModeArgIndex + 1)
         << " argument to '" << VariantName << "' is not an integer";

      ReportOpenBug(C, state,
                    SBuf.c_str(),
                    Arg->getSourceRange());
      return;
    }
  } else if (Call.getNumArgs() > MaxArgCount) {
    SmallString<256> SBuf;
    llvm::raw_svector_ostream OS(SBuf);
    OS << "Call to '" << VariantName << "' with more than " << MaxArgCount
       << " arguments";

    ReportOpenBug(C, state, SBuf.c_str(),
                  Call.getArgExpr(MaxArgCount)->getSourceRange());
    return;
  }

  if (!Val_O_CREAT) {
    return;
  }

  // Now check if oflags has O_CREAT set.
  const Expr *oflagsEx = Call.getArgExpr(FlagsArgIndex);
  const SVal V = Call.getArgSVal(FlagsArgIndex);
  if (!isa<NonLoc>(V)) {
    // The case where 'V' can be a location can only be due to a bad header,
    // so in this case bail out.
    return;
  }
  NonLoc oflags = V.castAs<NonLoc>();
  NonLoc ocreateFlag = C.getSValBuilder()
                           .makeIntVal(*Val_O_CREAT, oflagsEx->getType())
                           .castAs<NonLoc>();
  SVal maskedFlagsUC = C.getSValBuilder().evalBinOpNN(state, BO_And,
                                                      oflags, ocreateFlag,
                                                      oflagsEx->getType());
  if (maskedFlagsUC.isUnknownOrUndef())
    return;
  DefinedSVal maskedFlags = maskedFlagsUC.castAs<DefinedSVal>();

  // Check if maskedFlags is non-zero.
  ProgramStateRef trueState, falseState;
  std::tie(trueState, falseState) = state->assume(maskedFlags);

  // Only emit an error if the value of 'maskedFlags' is properly
  // constrained;
  if (!(trueState && !falseState))
    return;

  if (Call.getNumArgs() < MaxArgCount) {
    SmallString<256> SBuf;
    llvm::raw_svector_ostream OS(SBuf);
    OS << "Call to '" << VariantName << "' requires a "
       << CreateModeArgIndex + 1
       << llvm::getOrdinalSuffix(CreateModeArgIndex + 1)
       << " argument when the 'O_CREAT' flag is set";
    ReportOpenBug(C, trueState,
                  SBuf.c_str(),
                  oflagsEx->getSourceRange());
  }
}

//===----------------------------------------------------------------------===//
// getdelim and getline
//===----------------------------------------------------------------------===//

ProgramStateRef UnixAPIMisuseChecker::EnsureGetdelimBufferAndSizeCorrect(
    SVal LinePtrPtrSVal, SVal SizePtrSVal, const Expr *LinePtrPtrExpr,
    const Expr *SizePtrExpr, CheckerContext &C, ProgramStateRef State) const {
  static constexpr llvm::StringLiteral SizeGreaterThanBufferSize =
      "The buffer from the first argument is smaller than the size "
      "specified by the second parameter";
  static constexpr llvm::StringLiteral SizeUndef =
      "The buffer from the first argument is not NULL, but the size specified "
      "by the second parameter is undefined.";

  auto EmitBugReport = [this, &C, SizePtrExpr, LinePtrPtrExpr](
                           ProgramStateRef BugState, StringRef ErrMsg) {
    if (ExplodedNode *N = C.generateErrorNode(BugState)) {
      auto R = std::make_unique<PathSensitiveBugReport>(BT_getline, ErrMsg, N);
      bugreporter::trackExpressionValue(N, SizePtrExpr, *R);
      bugreporter::trackExpressionValue(N, LinePtrPtrExpr, *R);
      C.emitReport(std::move(R));
    }
  };

  // We have a pointer to a pointer to the buffer, and a pointer to the size.
  // We want what they point at.
  auto LinePtrSVal = getPointeeVal(LinePtrPtrSVal, State)->getAs<DefinedSVal>();
  auto NSVal = getPointeeVal(SizePtrSVal, State);
  if (!LinePtrSVal || !NSVal || NSVal->isUnknown())
    return nullptr;

  assert(LinePtrPtrExpr && SizePtrExpr);

  const auto [LinePtrNotNull, LinePtrNull] = State->assume(*LinePtrSVal);
  if (LinePtrNotNull && !LinePtrNull) {
    // If `*lineptr` is not null, but `*n` is undefined, there is UB.
    if (NSVal->isUndef()) {
      EmitBugReport(LinePtrNotNull, SizeUndef);
      return nullptr;
    }

    // If it is defined, and known, its size must be less than or equal to
    // the buffer size.
    auto NDefSVal = NSVal->getAs<DefinedSVal>();
    auto &SVB = C.getSValBuilder();
    auto LineBufSize =
        getDynamicExtent(LinePtrNotNull, LinePtrSVal->getAsRegion(), SVB);
    auto LineBufSizeGtN = SVB.evalBinOp(LinePtrNotNull, BO_GE, LineBufSize,
                                        *NDefSVal, SVB.getConditionType())
                              .getAs<DefinedOrUnknownSVal>();
    if (!LineBufSizeGtN)
      return LinePtrNotNull;
    if (auto LineBufSizeOk = LinePtrNotNull->assume(*LineBufSizeGtN, true))
      return LineBufSizeOk;

    EmitBugReport(LinePtrNotNull, SizeGreaterThanBufferSize);
    return nullptr;
  }
  return State;
}

void UnixAPIMisuseChecker::CheckGetDelim(CheckerContext &C,
                                         const CallEvent &Call) const {
  ProgramStateRef State = C.getState();

  // The parameter `n` must not be NULL.
  SVal SizePtrSval = Call.getArgSVal(1);
  State = EnsurePtrNotNull(SizePtrSval, Call.getArgExpr(1), C, State, "Size");
  if (!State)
    return;

  // The parameter `lineptr` must not be NULL.
  SVal LinePtrPtrSVal = Call.getArgSVal(0);
  State =
      EnsurePtrNotNull(LinePtrPtrSVal, Call.getArgExpr(0), C, State, "Line");
  if (!State)
    return;

  State = EnsureGetdelimBufferAndSizeCorrect(LinePtrPtrSVal, SizePtrSval,
                                             Call.getArgExpr(0),
                                             Call.getArgExpr(1), C, State);
  if (!State)
    return;

  C.addTransition(State);
}

//===----------------------------------------------------------------------===//
// pthread_once
//===----------------------------------------------------------------------===//

void UnixAPIMisuseChecker::CheckPthreadOnce(CheckerContext &C,
                                            const CallEvent &Call) const {

  // This is similar to 'CheckDispatchOnce' in the MacOSXAPIChecker.
  // They can possibly be refactored.

  if (Call.getNumArgs() < 1)
    return;

  // Check if the first argument is stack allocated.  If so, issue a warning
  // because that's likely to be bad news.
  ProgramStateRef state = C.getState();
  const MemRegion *R = Call.getArgSVal(0).getAsRegion();
  if (!R || !isa<StackSpaceRegion>(R->getMemorySpace()))
    return;

  ExplodedNode *N = C.generateErrorNode(state);
  if (!N)
    return;

  SmallString<256> S;
  llvm::raw_svector_ostream os(S);
  os << "Call to 'pthread_once' uses";
  if (const VarRegion *VR = dyn_cast<VarRegion>(R))
    os << " the local variable '" << VR->getDecl()->getName() << '\'';
  else
    os << " stack allocated memory";
  os << " for the \"control\" value.  Using such transient memory for "
  "the control value is potentially dangerous.";
  if (isa<VarRegion>(R) && isa<StackLocalsSpaceRegion>(R->getMemorySpace()))
    os << "  Perhaps you intended to declare the variable as 'static'?";

  auto report =
      std::make_unique<PathSensitiveBugReport>(BT_pthreadOnce, os.str(), N);
  report->addRange(Call.getArgExpr(0)->getSourceRange());
  C.emitReport(std::move(report));
}

//===----------------------------------------------------------------------===//
// "calloc", "malloc", "realloc", "reallocf", "alloca" and "valloc"
// with allocation size 0
//===----------------------------------------------------------------------===//

// FIXME: Eventually these should be rolled into the MallocChecker, but right now
// they're more basic and valuable for widespread use.

// Returns true if we try to do a zero byte allocation, false otherwise.
// Fills in trueState and falseState.
static bool IsZeroByteAllocation(ProgramStateRef state,
                                 const SVal argVal,
                                 ProgramStateRef *trueState,
                                 ProgramStateRef *falseState) {
  std::tie(*trueState, *falseState) =
    state->assume(argVal.castAs<DefinedSVal>());

  return (*falseState && !*trueState);
}

// Generates an error report, indicating that the function whose name is given
// will perform a zero byte allocation.
// Returns false if an error occurred, true otherwise.
bool UnixAPIPortabilityChecker::ReportZeroByteAllocation(
                                                    CheckerContext &C,
                                                    ProgramStateRef falseState,
                                                    const Expr *arg,
                                                    const char *fn_name) const {
  ExplodedNode *N = C.generateErrorNode(falseState);
  if (!N)
    return false;

  SmallString<256> S;
  llvm::raw_svector_ostream os(S);
  os << "Call to '" << fn_name << "' has an allocation size of 0 bytes";
  auto report =
      std::make_unique<PathSensitiveBugReport>(BT_mallocZero, os.str(), N);

  report->addRange(arg->getSourceRange());
  bugreporter::trackExpressionValue(N, arg, *report);
  C.emitReport(std::move(report));

  return true;
}

// Does a basic check for 0-sized allocations suitable for most of the below
// functions (modulo "calloc")
void UnixAPIPortabilityChecker::BasicAllocationCheck(CheckerContext &C,
                                                     const CallExpr *CE,
                                                     const unsigned numArgs,
                                                     const unsigned sizeArg,
                                                     const char *fn) const {
  // Check for the correct number of arguments.
  if (CE->getNumArgs() != numArgs)
    return;

  // Check if the allocation size is 0.
  ProgramStateRef state = C.getState();
  ProgramStateRef trueState = nullptr, falseState = nullptr;
  const Expr *arg = CE->getArg(sizeArg);
  SVal argVal = C.getSVal(arg);

  if (argVal.isUnknownOrUndef())
    return;

  // Is the value perfectly constrained to zero?
  if (IsZeroByteAllocation(state, argVal, &trueState, &falseState)) {
    (void) ReportZeroByteAllocation(C, falseState, arg, fn);
    return;
  }
  // Assume the value is non-zero going forward.
  assert(trueState);
  if (trueState != state)
    C.addTransition(trueState);
}

void UnixAPIPortabilityChecker::CheckCallocZero(CheckerContext &C,
                                                const CallExpr *CE) const {
  unsigned int nArgs = CE->getNumArgs();
  if (nArgs != 2)
    return;

  ProgramStateRef state = C.getState();
  ProgramStateRef trueState = nullptr, falseState = nullptr;

  unsigned int i;
  for (i = 0; i < nArgs; i++) {
    const Expr *arg = CE->getArg(i);
    SVal argVal = C.getSVal(arg);
    if (argVal.isUnknownOrUndef()) {
      if (i == 0)
        continue;
      else
        return;
    }

    if (IsZeroByteAllocation(state, argVal, &trueState, &falseState)) {
      if (ReportZeroByteAllocation(C, falseState, arg, "calloc"))
        return;
      else if (i == 0)
        continue;
      else
        return;
    }
  }

  // Assume the value is non-zero going forward.
  assert(trueState);
  if (trueState != state)
    C.addTransition(trueState);
}

void UnixAPIPortabilityChecker::CheckMallocZero(CheckerContext &C,
                                                const CallExpr *CE) const {
  BasicAllocationCheck(C, CE, 1, 0, "malloc");
}

void UnixAPIPortabilityChecker::CheckReallocZero(CheckerContext &C,
                                                 const CallExpr *CE) const {
  BasicAllocationCheck(C, CE, 2, 1, "realloc");
}

void UnixAPIPortabilityChecker::CheckReallocfZero(CheckerContext &C,
                                                  const CallExpr *CE) const {
  BasicAllocationCheck(C, CE, 2, 1, "reallocf");
}

void UnixAPIPortabilityChecker::CheckAllocaZero(CheckerContext &C,
                                                const CallExpr *CE) const {
  BasicAllocationCheck(C, CE, 1, 0, "alloca");
}

void UnixAPIPortabilityChecker::CheckAllocaWithAlignZero(
                                                     CheckerContext &C,
                                                     const CallExpr *CE) const {
  BasicAllocationCheck(C, CE, 2, 0, "__builtin_alloca_with_align");
}

void UnixAPIPortabilityChecker::CheckVallocZero(CheckerContext &C,
                                                const CallExpr *CE) const {
  BasicAllocationCheck(C, CE, 1, 0, "valloc");
}

void UnixAPIPortabilityChecker::checkPreStmt(const CallExpr *CE,
                                             CheckerContext &C) const {
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD || FD->getKind() != Decl::Function)
    return;

  // Don't treat functions in namespaces with the same name a Unix function
  // as a call to the Unix function.
  const DeclContext *NamespaceCtx = FD->getEnclosingNamespaceContext();
  if (isa_and_nonnull<NamespaceDecl>(NamespaceCtx))
    return;

  StringRef FName = C.getCalleeName(FD);
  if (FName.empty())
    return;

  if (FName == "calloc")
    CheckCallocZero(C, CE);

  else if (FName == "malloc")
    CheckMallocZero(C, CE);

  else if (FName == "realloc")
    CheckReallocZero(C, CE);

  else if (FName == "reallocf")
    CheckReallocfZero(C, CE);

  else if (FName == "alloca" || FName ==  "__builtin_alloca")
    CheckAllocaZero(C, CE);

  else if (FName == "__builtin_alloca_with_align")
    CheckAllocaWithAlignZero(C, CE);

  else if (FName == "valloc")
    CheckVallocZero(C, CE);
}

//===----------------------------------------------------------------------===//
// Registration.
//===----------------------------------------------------------------------===//

#define REGISTER_CHECKER(CHECKERNAME)                                          \
  void ento::register##CHECKERNAME(CheckerManager &mgr) {                      \
    mgr.registerChecker<CHECKERNAME>();                                        \
  }                                                                            \
                                                                               \
  bool ento::shouldRegister##CHECKERNAME(const CheckerManager &mgr) {          \
    return true;                                                               \
  }

REGISTER_CHECKER(UnixAPIMisuseChecker)
REGISTER_CHECKER(UnixAPIPortabilityChecker)
