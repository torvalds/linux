//===--- CallAndMessageChecker.cpp ------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines CallAndMessageChecker, a builtin checker that checks for various
// errors of call and objc message expressions.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMap.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {

class CallAndMessageChecker
    : public Checker<check::PreObjCMessage, check::ObjCMessageNil,
                     check::PreCall> {
  mutable std::unique_ptr<BugType> BT_call_null;
  mutable std::unique_ptr<BugType> BT_call_undef;
  mutable std::unique_ptr<BugType> BT_cxx_call_null;
  mutable std::unique_ptr<BugType> BT_cxx_call_undef;
  mutable std::unique_ptr<BugType> BT_call_arg;
  mutable std::unique_ptr<BugType> BT_cxx_delete_undef;
  mutable std::unique_ptr<BugType> BT_msg_undef;
  mutable std::unique_ptr<BugType> BT_objc_prop_undef;
  mutable std::unique_ptr<BugType> BT_objc_subscript_undef;
  mutable std::unique_ptr<BugType> BT_msg_arg;
  mutable std::unique_ptr<BugType> BT_msg_ret;
  mutable std::unique_ptr<BugType> BT_call_few_args;

public:
  // These correspond with the checker options. Looking at other checkers such
  // as MallocChecker and CStringChecker, this is similar as to how they pull
  // off having a modeling class, but emitting diagnostics under a smaller
  // checker's name that can be safely disabled without disturbing the
  // underlaying modeling engine.
  // The reason behind having *checker options* rather then actual *checkers*
  // here is that CallAndMessage is among the oldest checkers out there, and can
  // be responsible for the majority of the reports on any given project. This
  // is obviously not ideal, but changing checker name has the consequence of
  // changing the issue hashes associated with the reports, and databases
  // relying on this (CodeChecker, for instance) would suffer greatly.
  // If we ever end up making changes to the issue hash generation algorithm, or
  // the warning messages here, we should totally jump on the opportunity to
  // convert these to actual checkers.
  enum CheckKind {
    CK_FunctionPointer,
    CK_ParameterCount,
    CK_CXXThisMethodCall,
    CK_CXXDeallocationArg,
    CK_ArgInitializedness,
    CK_ArgPointeeInitializedness,
    CK_NilReceiver,
    CK_UndefReceiver,
    CK_NumCheckKinds
  };

  bool ChecksEnabled[CK_NumCheckKinds] = {false};
  // The original core.CallAndMessage checker name. This should rather be an
  // array, as seen in MallocChecker and CStringChecker.
  CheckerNameRef OriginalName;

  void checkPreObjCMessage(const ObjCMethodCall &msg, CheckerContext &C) const;

  /// Fill in the return value that results from messaging nil based on the
  /// return type and architecture and diagnose if the return value will be
  /// garbage.
  void checkObjCMessageNil(const ObjCMethodCall &msg, CheckerContext &C) const;

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;

  ProgramStateRef checkFunctionPointerCall(const CallExpr *CE,
                                           CheckerContext &C,
                                           ProgramStateRef State) const;

  ProgramStateRef checkCXXMethodCall(const CXXInstanceCall *CC,
                                     CheckerContext &C,
                                     ProgramStateRef State) const;

  ProgramStateRef checkParameterCount(const CallEvent &Call, CheckerContext &C,
                                      ProgramStateRef State) const;

  ProgramStateRef checkCXXDeallocation(const CXXDeallocatorCall *DC,
                                       CheckerContext &C,
                                       ProgramStateRef State) const;

  ProgramStateRef checkArgInitializedness(const CallEvent &Call,
                                          CheckerContext &C,
                                          ProgramStateRef State) const;

private:
  bool PreVisitProcessArg(CheckerContext &C, SVal V, SourceRange ArgRange,
                          const Expr *ArgEx, int ArgumentNumber,
                          bool CheckUninitFields, const CallEvent &Call,
                          std::unique_ptr<BugType> &BT,
                          const ParmVarDecl *ParamDecl) const;

  static void emitBadCall(BugType *BT, CheckerContext &C, const Expr *BadE);
  void emitNilReceiverBug(CheckerContext &C, const ObjCMethodCall &msg,
                          ExplodedNode *N) const;

  void HandleNilReceiver(CheckerContext &C,
                         ProgramStateRef state,
                         const ObjCMethodCall &msg) const;

  void LazyInit_BT(const char *desc, std::unique_ptr<BugType> &BT) const {
    if (!BT)
      BT.reset(new BugType(OriginalName, desc));
  }
  bool uninitRefOrPointer(CheckerContext &C, SVal V, SourceRange ArgRange,
                          const Expr *ArgEx, std::unique_ptr<BugType> &BT,
                          const ParmVarDecl *ParamDecl, const char *BD,
                          int ArgumentNumber) const;
};
} // end anonymous namespace

void CallAndMessageChecker::emitBadCall(BugType *BT, CheckerContext &C,
                                        const Expr *BadE) {
  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;

  auto R = std::make_unique<PathSensitiveBugReport>(*BT, BT->getDescription(), N);
  if (BadE) {
    R->addRange(BadE->getSourceRange());
    if (BadE->isGLValue())
      BadE = bugreporter::getDerefExpr(BadE);
    bugreporter::trackExpressionValue(N, BadE, *R);
  }
  C.emitReport(std::move(R));
}

static void describeUninitializedArgumentInCall(const CallEvent &Call,
                                                int ArgumentNumber,
                                                llvm::raw_svector_ostream &Os) {
  switch (Call.getKind()) {
  case CE_ObjCMessage: {
    const ObjCMethodCall &Msg = cast<ObjCMethodCall>(Call);
    switch (Msg.getMessageKind()) {
    case OCM_Message:
      Os << (ArgumentNumber + 1) << llvm::getOrdinalSuffix(ArgumentNumber + 1)
         << " argument in message expression is an uninitialized value";
      return;
    case OCM_PropertyAccess:
      assert(Msg.isSetter() && "Getters have no args");
      Os << "Argument for property setter is an uninitialized value";
      return;
    case OCM_Subscript:
      if (Msg.isSetter() && (ArgumentNumber == 0))
        Os << "Argument for subscript setter is an uninitialized value";
      else
        Os << "Subscript index is an uninitialized value";
      return;
    }
    llvm_unreachable("Unknown message kind.");
  }
  case CE_Block:
    Os << (ArgumentNumber + 1) << llvm::getOrdinalSuffix(ArgumentNumber + 1)
       << " block call argument is an uninitialized value";
    return;
  default:
    Os << (ArgumentNumber + 1) << llvm::getOrdinalSuffix(ArgumentNumber + 1)
       << " function call argument is an uninitialized value";
    return;
  }
}

bool CallAndMessageChecker::uninitRefOrPointer(
    CheckerContext &C, SVal V, SourceRange ArgRange, const Expr *ArgEx,
    std::unique_ptr<BugType> &BT, const ParmVarDecl *ParamDecl, const char *BD,
    int ArgumentNumber) const {

  // The pointee being uninitialized is a sign of code smell, not a bug, no need
  // to sink here.
  if (!ChecksEnabled[CK_ArgPointeeInitializedness])
    return false;

  // No parameter declaration available, i.e. variadic function argument.
  if(!ParamDecl)
    return false;

  // If parameter is declared as pointer to const in function declaration,
  // then check if corresponding argument in function call is
  // pointing to undefined symbol value (uninitialized memory).
  SmallString<200> Buf;
  llvm::raw_svector_ostream Os(Buf);

  if (ParamDecl->getType()->isPointerType()) {
    Os << (ArgumentNumber + 1) << llvm::getOrdinalSuffix(ArgumentNumber + 1)
       << " function call argument is a pointer to uninitialized value";
  } else if (ParamDecl->getType()->isReferenceType()) {
    Os << (ArgumentNumber + 1) << llvm::getOrdinalSuffix(ArgumentNumber + 1)
       << " function call argument is an uninitialized value";
  } else
    return false;

  if(!ParamDecl->getType()->getPointeeType().isConstQualified())
    return false;

  if (const MemRegion *SValMemRegion = V.getAsRegion()) {
    const ProgramStateRef State = C.getState();
    const SVal PSV = State->getSVal(SValMemRegion, C.getASTContext().CharTy);
    if (PSV.isUndef()) {
      if (ExplodedNode *N = C.generateErrorNode()) {
        LazyInit_BT(BD, BT);
        auto R = std::make_unique<PathSensitiveBugReport>(*BT, Os.str(), N);
        R->addRange(ArgRange);
        if (ArgEx)
          bugreporter::trackExpressionValue(N, ArgEx, *R);

        C.emitReport(std::move(R));
      }
      return true;
    }
  }
  return false;
}

namespace {
class FindUninitializedField {
public:
  SmallVector<const FieldDecl *, 10> FieldChain;

private:
  StoreManager &StoreMgr;
  MemRegionManager &MrMgr;
  Store store;

public:
  FindUninitializedField(StoreManager &storeMgr, MemRegionManager &mrMgr,
                         Store s)
      : StoreMgr(storeMgr), MrMgr(mrMgr), store(s) {}

  bool Find(const TypedValueRegion *R) {
    QualType T = R->getValueType();
    if (const RecordType *RT = T->getAsStructureType()) {
      const RecordDecl *RD = RT->getDecl()->getDefinition();
      assert(RD && "Referred record has no definition");
      for (const auto *I : RD->fields()) {
        const FieldRegion *FR = MrMgr.getFieldRegion(I, R);
        FieldChain.push_back(I);
        T = I->getType();
        if (T->getAsStructureType()) {
          if (Find(FR))
            return true;
        } else {
          SVal V = StoreMgr.getBinding(store, loc::MemRegionVal(FR));
          if (V.isUndef())
            return true;
        }
        FieldChain.pop_back();
      }
    }

    return false;
  }
};
} // namespace

bool CallAndMessageChecker::PreVisitProcessArg(CheckerContext &C,
                                               SVal V,
                                               SourceRange ArgRange,
                                               const Expr *ArgEx,
                                               int ArgumentNumber,
                                               bool CheckUninitFields,
                                               const CallEvent &Call,
                                               std::unique_ptr<BugType> &BT,
                                               const ParmVarDecl *ParamDecl
                                               ) const {
  const char *BD = "Uninitialized argument value";

  if (uninitRefOrPointer(C, V, ArgRange, ArgEx, BT, ParamDecl, BD,
                         ArgumentNumber))
    return true;

  if (V.isUndef()) {
    if (!ChecksEnabled[CK_ArgInitializedness]) {
      C.addSink();
      return true;
    }
    if (ExplodedNode *N = C.generateErrorNode()) {
      LazyInit_BT(BD, BT);
      // Generate a report for this bug.
      SmallString<200> Buf;
      llvm::raw_svector_ostream Os(Buf);
      describeUninitializedArgumentInCall(Call, ArgumentNumber, Os);
      auto R = std::make_unique<PathSensitiveBugReport>(*BT, Os.str(), N);

      R->addRange(ArgRange);
      if (ArgEx)
        bugreporter::trackExpressionValue(N, ArgEx, *R);
      C.emitReport(std::move(R));
    }
    return true;
  }

  if (!CheckUninitFields)
    return false;

  if (auto LV = V.getAs<nonloc::LazyCompoundVal>()) {
    const LazyCompoundValData *D = LV->getCVData();
    FindUninitializedField F(C.getState()->getStateManager().getStoreManager(),
                             C.getSValBuilder().getRegionManager(),
                             D->getStore());

    if (F.Find(D->getRegion())) {
      if (!ChecksEnabled[CK_ArgInitializedness]) {
        C.addSink();
        return true;
      }
      if (ExplodedNode *N = C.generateErrorNode()) {
        LazyInit_BT(BD, BT);
        SmallString<512> Str;
        llvm::raw_svector_ostream os(Str);
        os << "Passed-by-value struct argument contains uninitialized data";

        if (F.FieldChain.size() == 1)
          os << " (e.g., field: '" << *F.FieldChain[0] << "')";
        else {
          os << " (e.g., via the field chain: '";
          bool first = true;
          for (SmallVectorImpl<const FieldDecl *>::iterator
               DI = F.FieldChain.begin(), DE = F.FieldChain.end(); DI!=DE;++DI){
            if (first)
              first = false;
            else
              os << '.';
            os << **DI;
          }
          os << "')";
        }

        // Generate a report for this bug.
        auto R = std::make_unique<PathSensitiveBugReport>(*BT, os.str(), N);
        R->addRange(ArgRange);

        if (ArgEx)
          bugreporter::trackExpressionValue(N, ArgEx, *R);
        // FIXME: enhance track back for uninitialized value for arbitrary
        // memregions
        C.emitReport(std::move(R));
      }
      return true;
    }
  }

  return false;
}

ProgramStateRef CallAndMessageChecker::checkFunctionPointerCall(
    const CallExpr *CE, CheckerContext &C, ProgramStateRef State) const {

  const Expr *Callee = CE->getCallee()->IgnoreParens();
  const LocationContext *LCtx = C.getLocationContext();
  SVal L = State->getSVal(Callee, LCtx);

  if (L.isUndef()) {
    if (!ChecksEnabled[CK_FunctionPointer]) {
      C.addSink(State);
      return nullptr;
    }
    if (!BT_call_undef)
      BT_call_undef.reset(new BugType(
          OriginalName,
          "Called function pointer is an uninitialized pointer value"));
    emitBadCall(BT_call_undef.get(), C, Callee);
    return nullptr;
  }

  ProgramStateRef StNonNull, StNull;
  std::tie(StNonNull, StNull) = State->assume(L.castAs<DefinedOrUnknownSVal>());

  if (StNull && !StNonNull) {
    if (!ChecksEnabled[CK_FunctionPointer]) {
      C.addSink(StNull);
      return nullptr;
    }
    if (!BT_call_null)
      BT_call_null.reset(new BugType(
          OriginalName, "Called function pointer is null (null dereference)"));
    emitBadCall(BT_call_null.get(), C, Callee);
    return nullptr;
  }

  return StNonNull;
}

ProgramStateRef CallAndMessageChecker::checkParameterCount(
    const CallEvent &Call, CheckerContext &C, ProgramStateRef State) const {

  // If we have a function or block declaration, we can make sure we pass
  // enough parameters.
  unsigned Params = Call.parameters().size();
  if (Call.getNumArgs() >= Params)
    return State;

  if (!ChecksEnabled[CK_ParameterCount]) {
    C.addSink(State);
    return nullptr;
  }

  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return nullptr;

  LazyInit_BT("Function call with too few arguments", BT_call_few_args);

  SmallString<512> Str;
  llvm::raw_svector_ostream os(Str);
  if (isa<AnyFunctionCall>(Call)) {
    os << "Function ";
  } else {
    assert(isa<BlockCall>(Call));
    os << "Block ";
  }
  os << "taking " << Params << " argument" << (Params == 1 ? "" : "s")
     << " is called with fewer (" << Call.getNumArgs() << ")";

  C.emitReport(
      std::make_unique<PathSensitiveBugReport>(*BT_call_few_args, os.str(), N));
  return nullptr;
}

ProgramStateRef CallAndMessageChecker::checkCXXMethodCall(
    const CXXInstanceCall *CC, CheckerContext &C, ProgramStateRef State) const {

  SVal V = CC->getCXXThisVal();
  if (V.isUndef()) {
    if (!ChecksEnabled[CK_CXXThisMethodCall]) {
      C.addSink(State);
      return nullptr;
    }
    if (!BT_cxx_call_undef)
      BT_cxx_call_undef.reset(new BugType(
          OriginalName, "Called C++ object pointer is uninitialized"));
    emitBadCall(BT_cxx_call_undef.get(), C, CC->getCXXThisExpr());
    return nullptr;
  }

  ProgramStateRef StNonNull, StNull;
  std::tie(StNonNull, StNull) = State->assume(V.castAs<DefinedOrUnknownSVal>());

  if (StNull && !StNonNull) {
    if (!ChecksEnabled[CK_CXXThisMethodCall]) {
      C.addSink(StNull);
      return nullptr;
    }
    if (!BT_cxx_call_null)
      BT_cxx_call_null.reset(
          new BugType(OriginalName, "Called C++ object pointer is null"));
    emitBadCall(BT_cxx_call_null.get(), C, CC->getCXXThisExpr());
    return nullptr;
  }

  return StNonNull;
}

ProgramStateRef
CallAndMessageChecker::checkCXXDeallocation(const CXXDeallocatorCall *DC,
                                            CheckerContext &C,
                                            ProgramStateRef State) const {
  const CXXDeleteExpr *DE = DC->getOriginExpr();
  assert(DE);
  SVal Arg = C.getSVal(DE->getArgument());
  if (!Arg.isUndef())
    return State;

  if (!ChecksEnabled[CK_CXXDeallocationArg]) {
    C.addSink(State);
    return nullptr;
  }

  StringRef Desc;
  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return nullptr;
  if (!BT_cxx_delete_undef)
    BT_cxx_delete_undef.reset(
        new BugType(OriginalName, "Uninitialized argument value"));
  if (DE->isArrayFormAsWritten())
    Desc = "Argument to 'delete[]' is uninitialized";
  else
    Desc = "Argument to 'delete' is uninitialized";
  auto R =
      std::make_unique<PathSensitiveBugReport>(*BT_cxx_delete_undef, Desc, N);
  bugreporter::trackExpressionValue(N, DE, *R);
  C.emitReport(std::move(R));
  return nullptr;
}

ProgramStateRef CallAndMessageChecker::checkArgInitializedness(
    const CallEvent &Call, CheckerContext &C, ProgramStateRef State) const {

  const Decl *D = Call.getDecl();

  // Don't check for uninitialized field values in arguments if the
  // caller has a body that is available and we have the chance to inline it.
  // This is a hack, but is a reasonable compromise betweens sometimes warning
  // and sometimes not depending on if we decide to inline a function.
  const bool checkUninitFields =
      !(C.getAnalysisManager().shouldInlineCall() && (D && D->getBody()));

  std::unique_ptr<BugType> *BT;
  if (isa<ObjCMethodCall>(Call))
    BT = &BT_msg_arg;
  else
    BT = &BT_call_arg;

  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  for (unsigned i = 0, e = Call.getNumArgs(); i != e; ++i) {
    const ParmVarDecl *ParamDecl = nullptr;
    if (FD && i < FD->getNumParams())
      ParamDecl = FD->getParamDecl(i);
    if (PreVisitProcessArg(C, Call.getArgSVal(i), Call.getArgSourceRange(i),
                           Call.getArgExpr(i), i, checkUninitFields, Call, *BT,
                           ParamDecl))
      return nullptr;
  }
  return State;
}

void CallAndMessageChecker::checkPreCall(const CallEvent &Call,
                                         CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  if (const CallExpr *CE = dyn_cast_or_null<CallExpr>(Call.getOriginExpr()))
    State = checkFunctionPointerCall(CE, C, State);

  if (!State)
    return;

  if (Call.getDecl())
    State = checkParameterCount(Call, C, State);

  if (!State)
    return;

  if (const auto *CC = dyn_cast<CXXInstanceCall>(&Call))
    State = checkCXXMethodCall(CC, C, State);

  if (!State)
    return;

  if (const auto *DC = dyn_cast<CXXDeallocatorCall>(&Call))
    State = checkCXXDeallocation(DC, C, State);

  if (!State)
    return;

  State = checkArgInitializedness(Call, C, State);

  // If we make it here, record our assumptions about the callee.
  C.addTransition(State);
}

void CallAndMessageChecker::checkPreObjCMessage(const ObjCMethodCall &msg,
                                                CheckerContext &C) const {
  SVal recVal = msg.getReceiverSVal();
  if (recVal.isUndef()) {
    if (!ChecksEnabled[CK_UndefReceiver]) {
      C.addSink();
      return;
    }
    if (ExplodedNode *N = C.generateErrorNode()) {
      BugType *BT = nullptr;
      switch (msg.getMessageKind()) {
      case OCM_Message:
        if (!BT_msg_undef)
          BT_msg_undef.reset(new BugType(OriginalName,
                                         "Receiver in message expression "
                                         "is an uninitialized value"));
        BT = BT_msg_undef.get();
        break;
      case OCM_PropertyAccess:
        if (!BT_objc_prop_undef)
          BT_objc_prop_undef.reset(new BugType(
              OriginalName,
              "Property access on an uninitialized object pointer"));
        BT = BT_objc_prop_undef.get();
        break;
      case OCM_Subscript:
        if (!BT_objc_subscript_undef)
          BT_objc_subscript_undef.reset(new BugType(
              OriginalName,
              "Subscript access on an uninitialized object pointer"));
        BT = BT_objc_subscript_undef.get();
        break;
      }
      assert(BT && "Unknown message kind.");

      auto R = std::make_unique<PathSensitiveBugReport>(*BT, BT->getDescription(), N);
      const ObjCMessageExpr *ME = msg.getOriginExpr();
      R->addRange(ME->getReceiverRange());

      // FIXME: getTrackNullOrUndefValueVisitor can't handle "super" yet.
      if (const Expr *ReceiverE = ME->getInstanceReceiver())
        bugreporter::trackExpressionValue(N, ReceiverE, *R);
      C.emitReport(std::move(R));
    }
    return;
  }
}

void CallAndMessageChecker::checkObjCMessageNil(const ObjCMethodCall &msg,
                                                CheckerContext &C) const {
  HandleNilReceiver(C, C.getState(), msg);
}

void CallAndMessageChecker::emitNilReceiverBug(CheckerContext &C,
                                               const ObjCMethodCall &msg,
                                               ExplodedNode *N) const {
  if (!ChecksEnabled[CK_NilReceiver]) {
    C.addSink();
    return;
  }

  if (!BT_msg_ret)
    BT_msg_ret.reset(
        new BugType(OriginalName, "Receiver in message expression is 'nil'"));

  const ObjCMessageExpr *ME = msg.getOriginExpr();

  QualType ResTy = msg.getResultType();

  SmallString<200> buf;
  llvm::raw_svector_ostream os(buf);
  os << "The receiver of message '";
  ME->getSelector().print(os);
  os << "' is nil";
  if (ResTy->isReferenceType()) {
    os << ", which results in forming a null reference";
  } else {
    os << " and returns a value of type '";
    msg.getResultType().print(os, C.getLangOpts());
    os << "' that will be garbage";
  }

  auto report =
      std::make_unique<PathSensitiveBugReport>(*BT_msg_ret, os.str(), N);
  report->addRange(ME->getReceiverRange());
  // FIXME: This won't track "self" in messages to super.
  if (const Expr *receiver = ME->getInstanceReceiver()) {
    bugreporter::trackExpressionValue(N, receiver, *report);
  }
  C.emitReport(std::move(report));
}

static bool supportsNilWithFloatRet(const llvm::Triple &triple) {
  return (triple.getVendor() == llvm::Triple::Apple &&
          (triple.isiOS() || triple.isWatchOS() ||
           !triple.isMacOSXVersionLT(10,5)));
}

void CallAndMessageChecker::HandleNilReceiver(CheckerContext &C,
                                              ProgramStateRef state,
                                              const ObjCMethodCall &Msg) const {
  ASTContext &Ctx = C.getASTContext();
  static CheckerProgramPointTag Tag(this, "NilReceiver");

  // Check the return type of the message expression.  A message to nil will
  // return different values depending on the return type and the architecture.
  QualType RetTy = Msg.getResultType();
  CanQualType CanRetTy = Ctx.getCanonicalType(RetTy);
  const LocationContext *LCtx = C.getLocationContext();

  if (CanRetTy->isStructureOrClassType()) {
    // Structure returns are safe since the compiler zeroes them out.
    SVal V = C.getSValBuilder().makeZeroVal(RetTy);
    C.addTransition(state->BindExpr(Msg.getOriginExpr(), LCtx, V), &Tag);
    return;
  }

  // Other cases: check if sizeof(return type) > sizeof(void*)
  if (CanRetTy != Ctx.VoidTy && C.getLocationContext()->getParentMap()
                                  .isConsumedExpr(Msg.getOriginExpr())) {
    // Compute: sizeof(void *) and sizeof(return type)
    const uint64_t voidPtrSize = Ctx.getTypeSize(Ctx.VoidPtrTy);
    const uint64_t returnTypeSize = Ctx.getTypeSize(CanRetTy);

    if (CanRetTy.getTypePtr()->isReferenceType()||
        (voidPtrSize < returnTypeSize &&
         !(supportsNilWithFloatRet(Ctx.getTargetInfo().getTriple()) &&
           (Ctx.FloatTy == CanRetTy ||
            Ctx.DoubleTy == CanRetTy ||
            Ctx.LongDoubleTy == CanRetTy ||
            Ctx.LongLongTy == CanRetTy ||
            Ctx.UnsignedLongLongTy == CanRetTy)))) {
      if (ExplodedNode *N = C.generateErrorNode(state, &Tag))
        emitNilReceiverBug(C, Msg, N);
      return;
    }

    // Handle the safe cases where the return value is 0 if the
    // receiver is nil.
    //
    // FIXME: For now take the conservative approach that we only
    // return null values if we *know* that the receiver is nil.
    // This is because we can have surprises like:
    //
    //   ... = [[NSScreens screens] objectAtIndex:0];
    //
    // What can happen is that [... screens] could return nil, but
    // it most likely isn't nil.  We should assume the semantics
    // of this case unless we have *a lot* more knowledge.
    //
    SVal V = C.getSValBuilder().makeZeroVal(RetTy);
    C.addTransition(state->BindExpr(Msg.getOriginExpr(), LCtx, V), &Tag);
    return;
  }

  C.addTransition(state);
}

void ento::registerCallAndMessageModeling(CheckerManager &mgr) {
  mgr.registerChecker<CallAndMessageChecker>();
}

bool ento::shouldRegisterCallAndMessageModeling(const CheckerManager &mgr) {
  return true;
}

void ento::registerCallAndMessageChecker(CheckerManager &mgr) {
  CallAndMessageChecker *checker = mgr.getChecker<CallAndMessageChecker>();

  checker->OriginalName = mgr.getCurrentCheckerName();

#define QUERY_CHECKER_OPTION(OPTION)                                           \
  checker->ChecksEnabled[CallAndMessageChecker::CK_##OPTION] =                 \
      mgr.getAnalyzerOptions().getCheckerBooleanOption(                        \
          mgr.getCurrentCheckerName(), #OPTION);

  QUERY_CHECKER_OPTION(FunctionPointer)
  QUERY_CHECKER_OPTION(ParameterCount)
  QUERY_CHECKER_OPTION(CXXThisMethodCall)
  QUERY_CHECKER_OPTION(CXXDeallocationArg)
  QUERY_CHECKER_OPTION(ArgInitializedness)
  QUERY_CHECKER_OPTION(ArgPointeeInitializedness)
  QUERY_CHECKER_OPTION(NilReceiver)
  QUERY_CHECKER_OPTION(UndefReceiver)
}

bool ento::shouldRegisterCallAndMessageChecker(const CheckerManager &mgr) {
  return true;
}
