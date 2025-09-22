//== BodyFarm.cpp  - Factory for conjuring up fake bodies ----------*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BodyFarm is a factory for creating faux implementations for functions/methods
// for analysis purposes.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/BodyFarm.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/Analysis/CodeInjector.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Debug.h"
#include <optional>

#define DEBUG_TYPE "body-farm"

using namespace clang;

//===----------------------------------------------------------------------===//
// Helper creation functions for constructing faux ASTs.
//===----------------------------------------------------------------------===//

static bool isDispatchBlock(QualType Ty) {
  // Is it a block pointer?
  const BlockPointerType *BPT = Ty->getAs<BlockPointerType>();
  if (!BPT)
    return false;

  // Check if the block pointer type takes no arguments and
  // returns void.
  const FunctionProtoType *FT =
  BPT->getPointeeType()->getAs<FunctionProtoType>();
  return FT && FT->getReturnType()->isVoidType() && FT->getNumParams() == 0;
}

namespace {
class ASTMaker {
public:
  ASTMaker(ASTContext &C) : C(C) {}

  /// Create a new BinaryOperator representing a simple assignment.
  BinaryOperator *makeAssignment(const Expr *LHS, const Expr *RHS, QualType Ty);

  /// Create a new BinaryOperator representing a comparison.
  BinaryOperator *makeComparison(const Expr *LHS, const Expr *RHS,
                                 BinaryOperator::Opcode Op);

  /// Create a new compound stmt using the provided statements.
  CompoundStmt *makeCompound(ArrayRef<Stmt*>);

  /// Create a new DeclRefExpr for the referenced variable.
  DeclRefExpr *makeDeclRefExpr(const VarDecl *D,
                               bool RefersToEnclosingVariableOrCapture = false);

  /// Create a new UnaryOperator representing a dereference.
  UnaryOperator *makeDereference(const Expr *Arg, QualType Ty);

  /// Create an implicit cast for an integer conversion.
  Expr *makeIntegralCast(const Expr *Arg, QualType Ty);

  /// Create an implicit cast to a builtin boolean type.
  ImplicitCastExpr *makeIntegralCastToBoolean(const Expr *Arg);

  /// Create an implicit cast for lvalue-to-rvaluate conversions.
  ImplicitCastExpr *makeLvalueToRvalue(const Expr *Arg, QualType Ty);

  /// Make RValue out of variable declaration, creating a temporary
  /// DeclRefExpr in the process.
  ImplicitCastExpr *
  makeLvalueToRvalue(const VarDecl *Decl,
                     bool RefersToEnclosingVariableOrCapture = false);

  /// Create an implicit cast of the given type.
  ImplicitCastExpr *makeImplicitCast(const Expr *Arg, QualType Ty,
                                     CastKind CK = CK_LValueToRValue);

  /// Create a cast to reference type.
  CastExpr *makeReferenceCast(const Expr *Arg, QualType Ty);

  /// Create an Objective-C bool literal.
  ObjCBoolLiteralExpr *makeObjCBool(bool Val);

  /// Create an Objective-C ivar reference.
  ObjCIvarRefExpr *makeObjCIvarRef(const Expr *Base, const ObjCIvarDecl *IVar);

  /// Create a Return statement.
  ReturnStmt *makeReturn(const Expr *RetVal);

  /// Create an integer literal expression of the given type.
  IntegerLiteral *makeIntegerLiteral(uint64_t Value, QualType Ty);

  /// Create a member expression.
  MemberExpr *makeMemberExpression(Expr *base, ValueDecl *MemberDecl,
                                   bool IsArrow = false,
                                   ExprValueKind ValueKind = VK_LValue);

  /// Returns a *first* member field of a record declaration with a given name.
  /// \return an nullptr if no member with such a name exists.
  ValueDecl *findMemberField(const RecordDecl *RD, StringRef Name);

private:
  ASTContext &C;
};
}

BinaryOperator *ASTMaker::makeAssignment(const Expr *LHS, const Expr *RHS,
                                         QualType Ty) {
  return BinaryOperator::Create(
      C, const_cast<Expr *>(LHS), const_cast<Expr *>(RHS), BO_Assign, Ty,
      VK_PRValue, OK_Ordinary, SourceLocation(), FPOptionsOverride());
}

BinaryOperator *ASTMaker::makeComparison(const Expr *LHS, const Expr *RHS,
                                         BinaryOperator::Opcode Op) {
  assert(BinaryOperator::isLogicalOp(Op) ||
         BinaryOperator::isComparisonOp(Op));
  return BinaryOperator::Create(
      C, const_cast<Expr *>(LHS), const_cast<Expr *>(RHS), Op,
      C.getLogicalOperationType(), VK_PRValue, OK_Ordinary, SourceLocation(),
      FPOptionsOverride());
}

CompoundStmt *ASTMaker::makeCompound(ArrayRef<Stmt *> Stmts) {
  return CompoundStmt::Create(C, Stmts, FPOptionsOverride(), SourceLocation(),
                              SourceLocation());
}

DeclRefExpr *ASTMaker::makeDeclRefExpr(
    const VarDecl *D,
    bool RefersToEnclosingVariableOrCapture) {
  QualType Type = D->getType().getNonReferenceType();

  DeclRefExpr *DR = DeclRefExpr::Create(
      C, NestedNameSpecifierLoc(), SourceLocation(), const_cast<VarDecl *>(D),
      RefersToEnclosingVariableOrCapture, SourceLocation(), Type, VK_LValue);
  return DR;
}

UnaryOperator *ASTMaker::makeDereference(const Expr *Arg, QualType Ty) {
  return UnaryOperator::Create(C, const_cast<Expr *>(Arg), UO_Deref, Ty,
                               VK_LValue, OK_Ordinary, SourceLocation(),
                               /*CanOverflow*/ false, FPOptionsOverride());
}

ImplicitCastExpr *ASTMaker::makeLvalueToRvalue(const Expr *Arg, QualType Ty) {
  return makeImplicitCast(Arg, Ty, CK_LValueToRValue);
}

ImplicitCastExpr *
ASTMaker::makeLvalueToRvalue(const VarDecl *Arg,
                             bool RefersToEnclosingVariableOrCapture) {
  QualType Type = Arg->getType().getNonReferenceType();
  return makeLvalueToRvalue(makeDeclRefExpr(Arg,
                                            RefersToEnclosingVariableOrCapture),
                            Type);
}

ImplicitCastExpr *ASTMaker::makeImplicitCast(const Expr *Arg, QualType Ty,
                                             CastKind CK) {
  return ImplicitCastExpr::Create(C, Ty,
                                  /* CastKind=*/CK,
                                  /* Expr=*/const_cast<Expr *>(Arg),
                                  /* CXXCastPath=*/nullptr,
                                  /* ExprValueKind=*/VK_PRValue,
                                  /* FPFeatures */ FPOptionsOverride());
}

CastExpr *ASTMaker::makeReferenceCast(const Expr *Arg, QualType Ty) {
  assert(Ty->isReferenceType());
  return CXXStaticCastExpr::Create(
      C, Ty.getNonReferenceType(),
      Ty->isLValueReferenceType() ? VK_LValue : VK_XValue, CK_NoOp,
      const_cast<Expr *>(Arg), /*CXXCastPath=*/nullptr,
      /*Written=*/C.getTrivialTypeSourceInfo(Ty), FPOptionsOverride(),
      SourceLocation(), SourceLocation(), SourceRange());
}

Expr *ASTMaker::makeIntegralCast(const Expr *Arg, QualType Ty) {
  if (Arg->getType() == Ty)
    return const_cast<Expr*>(Arg);
  return makeImplicitCast(Arg, Ty, CK_IntegralCast);
}

ImplicitCastExpr *ASTMaker::makeIntegralCastToBoolean(const Expr *Arg) {
  return makeImplicitCast(Arg, C.BoolTy, CK_IntegralToBoolean);
}

ObjCBoolLiteralExpr *ASTMaker::makeObjCBool(bool Val) {
  QualType Ty = C.getBOOLDecl() ? C.getBOOLType() : C.ObjCBuiltinBoolTy;
  return new (C) ObjCBoolLiteralExpr(Val, Ty, SourceLocation());
}

ObjCIvarRefExpr *ASTMaker::makeObjCIvarRef(const Expr *Base,
                                           const ObjCIvarDecl *IVar) {
  return new (C) ObjCIvarRefExpr(const_cast<ObjCIvarDecl*>(IVar),
                                 IVar->getType(), SourceLocation(),
                                 SourceLocation(), const_cast<Expr*>(Base),
                                 /*arrow=*/true, /*free=*/false);
}

ReturnStmt *ASTMaker::makeReturn(const Expr *RetVal) {
  return ReturnStmt::Create(C, SourceLocation(), const_cast<Expr *>(RetVal),
                            /* NRVOCandidate=*/nullptr);
}

IntegerLiteral *ASTMaker::makeIntegerLiteral(uint64_t Value, QualType Ty) {
  llvm::APInt APValue = llvm::APInt(C.getTypeSize(Ty), Value);
  return IntegerLiteral::Create(C, APValue, Ty, SourceLocation());
}

MemberExpr *ASTMaker::makeMemberExpression(Expr *base, ValueDecl *MemberDecl,
                                           bool IsArrow,
                                           ExprValueKind ValueKind) {

  DeclAccessPair FoundDecl = DeclAccessPair::make(MemberDecl, AS_public);
  return MemberExpr::Create(
      C, base, IsArrow, SourceLocation(), NestedNameSpecifierLoc(),
      SourceLocation(), MemberDecl, FoundDecl,
      DeclarationNameInfo(MemberDecl->getDeclName(), SourceLocation()),
      /* TemplateArgumentListInfo=*/ nullptr, MemberDecl->getType(), ValueKind,
      OK_Ordinary, NOUR_None);
}

ValueDecl *ASTMaker::findMemberField(const RecordDecl *RD, StringRef Name) {

  CXXBasePaths Paths(
      /* FindAmbiguities=*/false,
      /* RecordPaths=*/false,
      /* DetectVirtual=*/ false);
  const IdentifierInfo &II = C.Idents.get(Name);
  DeclarationName DeclName = C.DeclarationNames.getIdentifier(&II);

  DeclContextLookupResult Decls = RD->lookup(DeclName);
  for (NamedDecl *FoundDecl : Decls)
    if (!FoundDecl->getDeclContext()->isFunctionOrMethod())
      return cast<ValueDecl>(FoundDecl);

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Creation functions for faux ASTs.
//===----------------------------------------------------------------------===//

typedef Stmt *(*FunctionFarmer)(ASTContext &C, const FunctionDecl *D);

static CallExpr *create_call_once_funcptr_call(ASTContext &C, ASTMaker M,
                                               const ParmVarDecl *Callback,
                                               ArrayRef<Expr *> CallArgs) {

  QualType Ty = Callback->getType();
  DeclRefExpr *Call = M.makeDeclRefExpr(Callback);
  Expr *SubExpr;
  if (Ty->isRValueReferenceType()) {
    SubExpr = M.makeImplicitCast(
        Call, Ty.getNonReferenceType(), CK_LValueToRValue);
  } else if (Ty->isLValueReferenceType() &&
             Call->getType()->isFunctionType()) {
    Ty = C.getPointerType(Ty.getNonReferenceType());
    SubExpr = M.makeImplicitCast(Call, Ty, CK_FunctionToPointerDecay);
  } else if (Ty->isLValueReferenceType()
             && Call->getType()->isPointerType()
             && Call->getType()->getPointeeType()->isFunctionType()){
    SubExpr = Call;
  } else {
    llvm_unreachable("Unexpected state");
  }

  return CallExpr::Create(C, SubExpr, CallArgs, C.VoidTy, VK_PRValue,
                          SourceLocation(), FPOptionsOverride());
}

static CallExpr *create_call_once_lambda_call(ASTContext &C, ASTMaker M,
                                              const ParmVarDecl *Callback,
                                              CXXRecordDecl *CallbackDecl,
                                              ArrayRef<Expr *> CallArgs) {
  assert(CallbackDecl != nullptr);
  assert(CallbackDecl->isLambda());
  FunctionDecl *callOperatorDecl = CallbackDecl->getLambdaCallOperator();
  assert(callOperatorDecl != nullptr);

  DeclRefExpr *callOperatorDeclRef =
      DeclRefExpr::Create(/* Ctx =*/ C,
                          /* QualifierLoc =*/ NestedNameSpecifierLoc(),
                          /* TemplateKWLoc =*/ SourceLocation(),
                          const_cast<FunctionDecl *>(callOperatorDecl),
                          /* RefersToEnclosingVariableOrCapture=*/ false,
                          /* NameLoc =*/ SourceLocation(),
                          /* T =*/ callOperatorDecl->getType(),
                          /* VK =*/ VK_LValue);

  return CXXOperatorCallExpr::Create(
      /*AstContext=*/C, OO_Call, callOperatorDeclRef,
      /*Args=*/CallArgs,
      /*QualType=*/C.VoidTy,
      /*ExprValueType=*/VK_PRValue,
      /*SourceLocation=*/SourceLocation(),
      /*FPFeatures=*/FPOptionsOverride());
}

/// Create a fake body for 'std::move' or 'std::forward'. This is just:
///
/// \code
/// return static_cast<return_type>(param);
/// \endcode
static Stmt *create_std_move_forward(ASTContext &C, const FunctionDecl *D) {
  LLVM_DEBUG(llvm::dbgs() << "Generating body for std::move / std::forward\n");

  ASTMaker M(C);

  QualType ReturnType = D->getType()->castAs<FunctionType>()->getReturnType();
  Expr *Param = M.makeDeclRefExpr(D->getParamDecl(0));
  Expr *Cast = M.makeReferenceCast(Param, ReturnType);
  return M.makeReturn(Cast);
}

/// Create a fake body for std::call_once.
/// Emulates the following function body:
///
/// \code
/// typedef struct once_flag_s {
///   unsigned long __state = 0;
/// } once_flag;
/// template<class Callable>
/// void call_once(once_flag& o, Callable func) {
///   if (!o.__state) {
///     func();
///   }
///   o.__state = 1;
/// }
/// \endcode
static Stmt *create_call_once(ASTContext &C, const FunctionDecl *D) {
  LLVM_DEBUG(llvm::dbgs() << "Generating body for call_once\n");

  // We need at least two parameters.
  if (D->param_size() < 2)
    return nullptr;

  ASTMaker M(C);

  const ParmVarDecl *Flag = D->getParamDecl(0);
  const ParmVarDecl *Callback = D->getParamDecl(1);

  if (!Callback->getType()->isReferenceType()) {
    llvm::dbgs() << "libcxx03 std::call_once implementation, skipping.\n";
    return nullptr;
  }
  if (!Flag->getType()->isReferenceType()) {
    llvm::dbgs() << "unknown std::call_once implementation, skipping.\n";
    return nullptr;
  }

  QualType CallbackType = Callback->getType().getNonReferenceType();

  // Nullable pointer, non-null iff function is a CXXRecordDecl.
  CXXRecordDecl *CallbackRecordDecl = CallbackType->getAsCXXRecordDecl();
  QualType FlagType = Flag->getType().getNonReferenceType();
  auto *FlagRecordDecl = FlagType->getAsRecordDecl();

  if (!FlagRecordDecl) {
    LLVM_DEBUG(llvm::dbgs() << "Flag field is not a record: "
                            << "unknown std::call_once implementation, "
                            << "ignoring the call.\n");
    return nullptr;
  }

  // We initially assume libc++ implementation of call_once,
  // where the once_flag struct has a field `__state_`.
  ValueDecl *FlagFieldDecl = M.findMemberField(FlagRecordDecl, "__state_");

  // Otherwise, try libstdc++ implementation, with a field
  // `_M_once`
  if (!FlagFieldDecl) {
    FlagFieldDecl = M.findMemberField(FlagRecordDecl, "_M_once");
  }

  if (!FlagFieldDecl) {
    LLVM_DEBUG(llvm::dbgs() << "No field _M_once or __state_ found on "
                            << "std::once_flag struct: unknown std::call_once "
                            << "implementation, ignoring the call.");
    return nullptr;
  }

  bool isLambdaCall = CallbackRecordDecl && CallbackRecordDecl->isLambda();
  if (CallbackRecordDecl && !isLambdaCall) {
    LLVM_DEBUG(llvm::dbgs()
               << "Not supported: synthesizing body for functors when "
               << "body farming std::call_once, ignoring the call.");
    return nullptr;
  }

  SmallVector<Expr *, 5> CallArgs;
  const FunctionProtoType *CallbackFunctionType;
  if (isLambdaCall) {

    // Lambda requires callback itself inserted as a first parameter.
    CallArgs.push_back(
        M.makeDeclRefExpr(Callback,
                          /* RefersToEnclosingVariableOrCapture=*/ true));
    CallbackFunctionType = CallbackRecordDecl->getLambdaCallOperator()
                               ->getType()
                               ->getAs<FunctionProtoType>();
  } else if (!CallbackType->getPointeeType().isNull()) {
    CallbackFunctionType =
        CallbackType->getPointeeType()->getAs<FunctionProtoType>();
  } else {
    CallbackFunctionType = CallbackType->getAs<FunctionProtoType>();
  }

  if (!CallbackFunctionType)
    return nullptr;

  // First two arguments are used for the flag and for the callback.
  if (D->getNumParams() != CallbackFunctionType->getNumParams() + 2) {
    LLVM_DEBUG(llvm::dbgs() << "Types of params of the callback do not match "
                            << "params passed to std::call_once, "
                            << "ignoring the call\n");
    return nullptr;
  }

  // All arguments past first two ones are passed to the callback,
  // and we turn lvalues into rvalues if the argument is not passed by
  // reference.
  for (unsigned int ParamIdx = 2; ParamIdx < D->getNumParams(); ParamIdx++) {
    const ParmVarDecl *PDecl = D->getParamDecl(ParamIdx);
    assert(PDecl);
    if (CallbackFunctionType->getParamType(ParamIdx - 2)
                .getNonReferenceType()
                .getCanonicalType() !=
            PDecl->getType().getNonReferenceType().getCanonicalType()) {
      LLVM_DEBUG(llvm::dbgs() << "Types of params of the callback do not match "
                              << "params passed to std::call_once, "
                              << "ignoring the call\n");
      return nullptr;
    }
    Expr *ParamExpr = M.makeDeclRefExpr(PDecl);
    if (!CallbackFunctionType->getParamType(ParamIdx - 2)->isReferenceType()) {
      QualType PTy = PDecl->getType().getNonReferenceType();
      ParamExpr = M.makeLvalueToRvalue(ParamExpr, PTy);
    }
    CallArgs.push_back(ParamExpr);
  }

  CallExpr *CallbackCall;
  if (isLambdaCall) {

    CallbackCall = create_call_once_lambda_call(C, M, Callback,
                                                CallbackRecordDecl, CallArgs);
  } else {

    // Function pointer case.
    CallbackCall = create_call_once_funcptr_call(C, M, Callback, CallArgs);
  }

  DeclRefExpr *FlagDecl =
      M.makeDeclRefExpr(Flag,
                        /* RefersToEnclosingVariableOrCapture=*/true);


  MemberExpr *Deref = M.makeMemberExpression(FlagDecl, FlagFieldDecl);
  assert(Deref->isLValue());
  QualType DerefType = Deref->getType();

  // Negation predicate.
  UnaryOperator *FlagCheck = UnaryOperator::Create(
      C,
      /* input=*/
      M.makeImplicitCast(M.makeLvalueToRvalue(Deref, DerefType), DerefType,
                         CK_IntegralToBoolean),
      /* opc=*/UO_LNot,
      /* QualType=*/C.IntTy,
      /* ExprValueKind=*/VK_PRValue,
      /* ExprObjectKind=*/OK_Ordinary, SourceLocation(),
      /* CanOverflow*/ false, FPOptionsOverride());

  // Create assignment.
  BinaryOperator *FlagAssignment = M.makeAssignment(
      Deref, M.makeIntegralCast(M.makeIntegerLiteral(1, C.IntTy), DerefType),
      DerefType);

  auto *Out =
      IfStmt::Create(C, SourceLocation(), IfStatementKind::Ordinary,
                     /* Init=*/nullptr,
                     /* Var=*/nullptr,
                     /* Cond=*/FlagCheck,
                     /* LPL=*/SourceLocation(),
                     /* RPL=*/SourceLocation(),
                     /* Then=*/M.makeCompound({CallbackCall, FlagAssignment}));

  return Out;
}

/// Create a fake body for dispatch_once.
static Stmt *create_dispatch_once(ASTContext &C, const FunctionDecl *D) {
  // Check if we have at least two parameters.
  if (D->param_size() != 2)
    return nullptr;

  // Check if the first parameter is a pointer to integer type.
  const ParmVarDecl *Predicate = D->getParamDecl(0);
  QualType PredicateQPtrTy = Predicate->getType();
  const PointerType *PredicatePtrTy = PredicateQPtrTy->getAs<PointerType>();
  if (!PredicatePtrTy)
    return nullptr;
  QualType PredicateTy = PredicatePtrTy->getPointeeType();
  if (!PredicateTy->isIntegerType())
    return nullptr;

  // Check if the second parameter is the proper block type.
  const ParmVarDecl *Block = D->getParamDecl(1);
  QualType Ty = Block->getType();
  if (!isDispatchBlock(Ty))
    return nullptr;

  // Everything checks out.  Create a fakse body that checks the predicate,
  // sets it, and calls the block.  Basically, an AST dump of:
  //
  // void dispatch_once(dispatch_once_t *predicate, dispatch_block_t block) {
  //  if (*predicate != ~0l) {
  //    *predicate = ~0l;
  //    block();
  //  }
  // }

  ASTMaker M(C);

  // (1) Create the call.
  CallExpr *CE = CallExpr::Create(
      /*ASTContext=*/C,
      /*StmtClass=*/M.makeLvalueToRvalue(/*Expr=*/Block),
      /*Args=*/std::nullopt,
      /*QualType=*/C.VoidTy,
      /*ExprValueType=*/VK_PRValue,
      /*SourceLocation=*/SourceLocation(), FPOptionsOverride());

  // (2) Create the assignment to the predicate.
  Expr *DoneValue =
      UnaryOperator::Create(C, M.makeIntegerLiteral(0, C.LongTy), UO_Not,
                            C.LongTy, VK_PRValue, OK_Ordinary, SourceLocation(),
                            /*CanOverflow*/ false, FPOptionsOverride());

  BinaryOperator *B =
    M.makeAssignment(
       M.makeDereference(
          M.makeLvalueToRvalue(
            M.makeDeclRefExpr(Predicate), PredicateQPtrTy),
            PredicateTy),
       M.makeIntegralCast(DoneValue, PredicateTy),
       PredicateTy);

  // (3) Create the compound statement.
  Stmt *Stmts[] = { B, CE };
  CompoundStmt *CS = M.makeCompound(Stmts);

  // (4) Create the 'if' condition.
  ImplicitCastExpr *LValToRval =
    M.makeLvalueToRvalue(
      M.makeDereference(
        M.makeLvalueToRvalue(
          M.makeDeclRefExpr(Predicate),
          PredicateQPtrTy),
        PredicateTy),
    PredicateTy);

  Expr *GuardCondition = M.makeComparison(LValToRval, DoneValue, BO_NE);
  // (5) Create the 'if' statement.
  auto *If = IfStmt::Create(C, SourceLocation(), IfStatementKind::Ordinary,
                            /* Init=*/nullptr,
                            /* Var=*/nullptr,
                            /* Cond=*/GuardCondition,
                            /* LPL=*/SourceLocation(),
                            /* RPL=*/SourceLocation(),
                            /* Then=*/CS);
  return If;
}

/// Create a fake body for dispatch_sync.
static Stmt *create_dispatch_sync(ASTContext &C, const FunctionDecl *D) {
  // Check if we have at least two parameters.
  if (D->param_size() != 2)
    return nullptr;

  // Check if the second parameter is a block.
  const ParmVarDecl *PV = D->getParamDecl(1);
  QualType Ty = PV->getType();
  if (!isDispatchBlock(Ty))
    return nullptr;

  // Everything checks out.  Create a fake body that just calls the block.
  // This is basically just an AST dump of:
  //
  // void dispatch_sync(dispatch_queue_t queue, void (^block)(void)) {
  //   block();
  // }
  //
  ASTMaker M(C);
  DeclRefExpr *DR = M.makeDeclRefExpr(PV);
  ImplicitCastExpr *ICE = M.makeLvalueToRvalue(DR, Ty);
  CallExpr *CE = CallExpr::Create(C, ICE, std::nullopt, C.VoidTy, VK_PRValue,
                                  SourceLocation(), FPOptionsOverride());
  return CE;
}

static Stmt *create_OSAtomicCompareAndSwap(ASTContext &C, const FunctionDecl *D)
{
  // There are exactly 3 arguments.
  if (D->param_size() != 3)
    return nullptr;

  // Signature:
  // _Bool OSAtomicCompareAndSwapPtr(void *__oldValue,
  //                                 void *__newValue,
  //                                 void * volatile *__theValue)
  // Generate body:
  //   if (oldValue == *theValue) {
  //    *theValue = newValue;
  //    return YES;
  //   }
  //   else return NO;

  QualType ResultTy = D->getReturnType();
  bool isBoolean = ResultTy->isBooleanType();
  if (!isBoolean && !ResultTy->isIntegralType(C))
    return nullptr;

  const ParmVarDecl *OldValue = D->getParamDecl(0);
  QualType OldValueTy = OldValue->getType();

  const ParmVarDecl *NewValue = D->getParamDecl(1);
  QualType NewValueTy = NewValue->getType();

  assert(OldValueTy == NewValueTy);

  const ParmVarDecl *TheValue = D->getParamDecl(2);
  QualType TheValueTy = TheValue->getType();
  const PointerType *PT = TheValueTy->getAs<PointerType>();
  if (!PT)
    return nullptr;
  QualType PointeeTy = PT->getPointeeType();

  ASTMaker M(C);
  // Construct the comparison.
  Expr *Comparison =
    M.makeComparison(
      M.makeLvalueToRvalue(M.makeDeclRefExpr(OldValue), OldValueTy),
      M.makeLvalueToRvalue(
        M.makeDereference(
          M.makeLvalueToRvalue(M.makeDeclRefExpr(TheValue), TheValueTy),
          PointeeTy),
        PointeeTy),
      BO_EQ);

  // Construct the body of the IfStmt.
  Stmt *Stmts[2];
  Stmts[0] =
    M.makeAssignment(
      M.makeDereference(
        M.makeLvalueToRvalue(M.makeDeclRefExpr(TheValue), TheValueTy),
        PointeeTy),
      M.makeLvalueToRvalue(M.makeDeclRefExpr(NewValue), NewValueTy),
      NewValueTy);

  Expr *BoolVal = M.makeObjCBool(true);
  Expr *RetVal = isBoolean ? M.makeIntegralCastToBoolean(BoolVal)
                           : M.makeIntegralCast(BoolVal, ResultTy);
  Stmts[1] = M.makeReturn(RetVal);
  CompoundStmt *Body = M.makeCompound(Stmts);

  // Construct the else clause.
  BoolVal = M.makeObjCBool(false);
  RetVal = isBoolean ? M.makeIntegralCastToBoolean(BoolVal)
                     : M.makeIntegralCast(BoolVal, ResultTy);
  Stmt *Else = M.makeReturn(RetVal);

  /// Construct the If.
  auto *If =
      IfStmt::Create(C, SourceLocation(), IfStatementKind::Ordinary,
                     /* Init=*/nullptr,
                     /* Var=*/nullptr, Comparison,
                     /* LPL=*/SourceLocation(),
                     /* RPL=*/SourceLocation(), Body, SourceLocation(), Else);

  return If;
}

Stmt *BodyFarm::getBody(const FunctionDecl *D) {
  std::optional<Stmt *> &Val = Bodies[D];
  if (Val)
    return *Val;

  Val = nullptr;

  if (D->getIdentifier() == nullptr)
    return nullptr;

  StringRef Name = D->getName();
  if (Name.empty())
    return nullptr;

  FunctionFarmer FF;

  if (unsigned BuiltinID = D->getBuiltinID()) {
    switch (BuiltinID) {
    case Builtin::BIas_const:
    case Builtin::BIforward:
    case Builtin::BIforward_like:
    case Builtin::BImove:
    case Builtin::BImove_if_noexcept:
      FF = create_std_move_forward;
      break;
    default:
      FF = nullptr;
      break;
    }
  } else if (Name.starts_with("OSAtomicCompareAndSwap") ||
             Name.starts_with("objc_atomicCompareAndSwap")) {
    FF = create_OSAtomicCompareAndSwap;
  } else if (Name == "call_once" && D->getDeclContext()->isStdNamespace()) {
    FF = create_call_once;
  } else {
    FF = llvm::StringSwitch<FunctionFarmer>(Name)
          .Case("dispatch_sync", create_dispatch_sync)
          .Case("dispatch_once", create_dispatch_once)
          .Default(nullptr);
  }

  if (FF) { Val = FF(C, D); }
  else if (Injector) { Val = Injector->getBody(D); }
  return *Val;
}

static const ObjCIvarDecl *findBackingIvar(const ObjCPropertyDecl *Prop) {
  const ObjCIvarDecl *IVar = Prop->getPropertyIvarDecl();

  if (IVar)
    return IVar;

  // When a readonly property is shadowed in a class extensions with a
  // a readwrite property, the instance variable belongs to the shadowing
  // property rather than the shadowed property. If there is no instance
  // variable on a readonly property, check to see whether the property is
  // shadowed and if so try to get the instance variable from shadowing
  // property.
  if (!Prop->isReadOnly())
    return nullptr;

  auto *Container = cast<ObjCContainerDecl>(Prop->getDeclContext());
  const ObjCInterfaceDecl *PrimaryInterface = nullptr;
  if (auto *InterfaceDecl = dyn_cast<ObjCInterfaceDecl>(Container)) {
    PrimaryInterface = InterfaceDecl;
  } else if (auto *CategoryDecl = dyn_cast<ObjCCategoryDecl>(Container)) {
    PrimaryInterface = CategoryDecl->getClassInterface();
  } else if (auto *ImplDecl = dyn_cast<ObjCImplDecl>(Container)) {
    PrimaryInterface = ImplDecl->getClassInterface();
  } else {
    return nullptr;
  }

  // FindPropertyVisibleInPrimaryClass() looks first in class extensions, so it
  // is guaranteed to find the shadowing property, if it exists, rather than
  // the shadowed property.
  auto *ShadowingProp = PrimaryInterface->FindPropertyVisibleInPrimaryClass(
      Prop->getIdentifier(), Prop->getQueryKind());
  if (ShadowingProp && ShadowingProp != Prop) {
    IVar = ShadowingProp->getPropertyIvarDecl();
  }

  return IVar;
}

static Stmt *createObjCPropertyGetter(ASTContext &Ctx,
                                      const ObjCMethodDecl *MD) {
  // First, find the backing ivar.
  const ObjCIvarDecl *IVar = nullptr;
  const ObjCPropertyDecl *Prop = nullptr;

  // Property accessor stubs sometimes do not correspond to any property decl
  // in the current interface (but in a superclass). They still have a
  // corresponding property impl decl in this case.
  if (MD->isSynthesizedAccessorStub()) {
    const ObjCInterfaceDecl *IntD = MD->getClassInterface();
    const ObjCImplementationDecl *ImpD = IntD->getImplementation();
    for (const auto *PI : ImpD->property_impls()) {
      if (const ObjCPropertyDecl *Candidate = PI->getPropertyDecl()) {
        if (Candidate->getGetterName() == MD->getSelector()) {
          Prop = Candidate;
          IVar = Prop->getPropertyIvarDecl();
        }
      }
    }
  }

  if (!IVar) {
    Prop = MD->findPropertyDecl();
    IVar = Prop ? findBackingIvar(Prop) : nullptr;
  }

  if (!IVar || !Prop)
    return nullptr;

  // Ignore weak variables, which have special behavior.
  if (Prop->getPropertyAttributes() & ObjCPropertyAttribute::kind_weak)
    return nullptr;

  // Look to see if Sema has synthesized a body for us. This happens in
  // Objective-C++ because the return value may be a C++ class type with a
  // non-trivial copy constructor. We can only do this if we can find the
  // @synthesize for this property, though (or if we know it's been auto-
  // synthesized).
  const ObjCImplementationDecl *ImplDecl =
      IVar->getContainingInterface()->getImplementation();
  if (ImplDecl) {
    for (const auto *I : ImplDecl->property_impls()) {
      if (I->getPropertyDecl() != Prop)
        continue;

      if (I->getGetterCXXConstructor()) {
        ASTMaker M(Ctx);
        return M.makeReturn(I->getGetterCXXConstructor());
      }
    }
  }

  // We expect that the property is the same type as the ivar, or a reference to
  // it, and that it is either an object pointer or trivially copyable.
  if (!Ctx.hasSameUnqualifiedType(IVar->getType(),
                                  Prop->getType().getNonReferenceType()))
    return nullptr;
  if (!IVar->getType()->isObjCLifetimeType() &&
      !IVar->getType().isTriviallyCopyableType(Ctx))
    return nullptr;

  // Generate our body:
  //   return self->_ivar;
  ASTMaker M(Ctx);

  const VarDecl *selfVar = MD->getSelfDecl();
  if (!selfVar)
    return nullptr;

  Expr *loadedIVar = M.makeObjCIvarRef(
      M.makeLvalueToRvalue(M.makeDeclRefExpr(selfVar), selfVar->getType()),
      IVar);

  if (!MD->getReturnType()->isReferenceType())
    loadedIVar = M.makeLvalueToRvalue(loadedIVar, IVar->getType());

  return M.makeReturn(loadedIVar);
}

Stmt *BodyFarm::getBody(const ObjCMethodDecl *D) {
  // We currently only know how to synthesize property accessors.
  if (!D->isPropertyAccessor())
    return nullptr;

  D = D->getCanonicalDecl();

  // We should not try to synthesize explicitly redefined accessors.
  // We do not know for sure how they behave.
  if (!D->isImplicit())
    return nullptr;

  std::optional<Stmt *> &Val = Bodies[D];
  if (Val)
    return *Val;
  Val = nullptr;

  // For now, we only synthesize getters.
  // Synthesizing setters would cause false negatives in the
  // RetainCountChecker because the method body would bind the parameter
  // to an instance variable, causing it to escape. This would prevent
  // warning in the following common scenario:
  //
  //  id foo = [[NSObject alloc] init];
  //  self.foo = foo; // We should warn that foo leaks here.
  //
  if (D->param_size() != 0)
    return nullptr;

  // If the property was defined in an extension, search the extensions for
  // overrides.
  const ObjCInterfaceDecl *OID = D->getClassInterface();
  if (dyn_cast<ObjCInterfaceDecl>(D->getParent()) != OID)
    for (auto *Ext : OID->known_extensions()) {
      auto *OMD = Ext->getInstanceMethod(D->getSelector());
      if (OMD && !OMD->isImplicit())
        return nullptr;
    }

  Val = createObjCPropertyGetter(C, D);

  return *Val;
}
