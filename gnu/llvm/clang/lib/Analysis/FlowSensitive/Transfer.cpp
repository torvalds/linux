//===-- Transfer.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines transfer functions that evaluate program statements and
//  update an environment accordingly.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/Transfer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/FlowSensitive/ASTOps.h"
#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysisContext.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/NoopAnalysis.h"
#include "clang/Analysis/FlowSensitive/RecordOps.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include <assert.h>
#include <cassert>

#define DEBUG_TYPE "dataflow"

namespace clang {
namespace dataflow {

const Environment *StmtToEnvMap::getEnvironment(const Stmt &S) const {
  auto BlockIt = ACFG.getStmtToBlock().find(&ignoreCFGOmittedNodes(S));
  if (BlockIt == ACFG.getStmtToBlock().end()) {
    assert(false);
    // Return null to avoid dereferencing the end iterator in non-assert builds.
    return nullptr;
  }
  if (!ACFG.isBlockReachable(*BlockIt->getSecond()))
    return nullptr;
  if (BlockIt->getSecond()->getBlockID() == CurBlockID)
    return &CurState.Env;
  const auto &State = BlockToState[BlockIt->getSecond()->getBlockID()];
  if (!(State))
    return nullptr;
  return &State->Env;
}

static BoolValue &evaluateBooleanEquality(const Expr &LHS, const Expr &RHS,
                                          Environment &Env) {
  Value *LHSValue = Env.getValue(LHS);
  Value *RHSValue = Env.getValue(RHS);

  if (LHSValue == RHSValue)
    return Env.getBoolLiteralValue(true);

  if (auto *LHSBool = dyn_cast_or_null<BoolValue>(LHSValue))
    if (auto *RHSBool = dyn_cast_or_null<BoolValue>(RHSValue))
      return Env.makeIff(*LHSBool, *RHSBool);

  if (auto *LHSPtr = dyn_cast_or_null<PointerValue>(LHSValue))
    if (auto *RHSPtr = dyn_cast_or_null<PointerValue>(RHSValue))
      // If the storage locations are the same, the pointers definitely compare
      // the same. If the storage locations are different, they may still alias,
      // so we fall through to the case below that returns an atom.
      if (&LHSPtr->getPointeeLoc() == &RHSPtr->getPointeeLoc())
        return Env.getBoolLiteralValue(true);

  return Env.makeAtomicBoolValue();
}

static BoolValue &unpackValue(BoolValue &V, Environment &Env) {
  if (auto *Top = llvm::dyn_cast<TopBoolValue>(&V)) {
    auto &A = Env.getDataflowAnalysisContext().arena();
    return A.makeBoolValue(A.makeAtomRef(Top->getAtom()));
  }
  return V;
}

// Unpacks the value (if any) associated with `E` and updates `E` to the new
// value, if any unpacking occured. Also, does the lvalue-to-rvalue conversion,
// by skipping past the reference.
static Value *maybeUnpackLValueExpr(const Expr &E, Environment &Env) {
  auto *Loc = Env.getStorageLocation(E);
  if (Loc == nullptr)
    return nullptr;
  auto *Val = Env.getValue(*Loc);

  auto *B = dyn_cast_or_null<BoolValue>(Val);
  if (B == nullptr)
    return Val;

  auto &UnpackedVal = unpackValue(*B, Env);
  if (&UnpackedVal == Val)
    return Val;
  Env.setValue(*Loc, UnpackedVal);
  return &UnpackedVal;
}

static void propagateValue(const Expr &From, const Expr &To, Environment &Env) {
  if (From.getType()->isRecordType())
    return;
  if (auto *Val = Env.getValue(From))
    Env.setValue(To, *Val);
}

static void propagateStorageLocation(const Expr &From, const Expr &To,
                                     Environment &Env) {
  if (auto *Loc = Env.getStorageLocation(From))
    Env.setStorageLocation(To, *Loc);
}

// Propagates the value or storage location of `From` to `To` in cases where
// `From` may be either a glvalue or a prvalue. `To` must be a glvalue iff
// `From` is a glvalue.
static void propagateValueOrStorageLocation(const Expr &From, const Expr &To,
                                            Environment &Env) {
  assert(From.isGLValue() == To.isGLValue());
  if (From.isGLValue())
    propagateStorageLocation(From, To, Env);
  else
    propagateValue(From, To, Env);
}

namespace {

class TransferVisitor : public ConstStmtVisitor<TransferVisitor> {
public:
  TransferVisitor(const StmtToEnvMap &StmtToEnv, Environment &Env,
                  Environment::ValueModel &Model)
      : StmtToEnv(StmtToEnv), Env(Env), Model(Model) {}

  void VisitBinaryOperator(const BinaryOperator *S) {
    const Expr *LHS = S->getLHS();
    assert(LHS != nullptr);

    const Expr *RHS = S->getRHS();
    assert(RHS != nullptr);

    // Do compound assignments up-front, as there are so many of them and we
    // don't want to list all of them in the switch statement below.
    // To avoid generating unnecessary values, we don't create a new value but
    // instead leave it to the specific analysis to do this if desired.
    if (S->isCompoundAssignmentOp())
      propagateStorageLocation(*S->getLHS(), *S, Env);

    switch (S->getOpcode()) {
    case BO_Assign: {
      auto *LHSLoc = Env.getStorageLocation(*LHS);
      if (LHSLoc == nullptr)
        break;

      auto *RHSVal = Env.getValue(*RHS);
      if (RHSVal == nullptr)
        break;

      // Assign a value to the storage location of the left-hand side.
      Env.setValue(*LHSLoc, *RHSVal);

      // Assign a storage location for the whole expression.
      Env.setStorageLocation(*S, *LHSLoc);
      break;
    }
    case BO_LAnd:
    case BO_LOr: {
      BoolValue &LHSVal = getLogicOperatorSubExprValue(*LHS);
      BoolValue &RHSVal = getLogicOperatorSubExprValue(*RHS);

      if (S->getOpcode() == BO_LAnd)
        Env.setValue(*S, Env.makeAnd(LHSVal, RHSVal));
      else
        Env.setValue(*S, Env.makeOr(LHSVal, RHSVal));
      break;
    }
    case BO_NE:
    case BO_EQ: {
      auto &LHSEqRHSValue = evaluateBooleanEquality(*LHS, *RHS, Env);
      Env.setValue(*S, S->getOpcode() == BO_EQ ? LHSEqRHSValue
                                               : Env.makeNot(LHSEqRHSValue));
      break;
    }
    case BO_Comma: {
      propagateValueOrStorageLocation(*RHS, *S, Env);
      break;
    }
    default:
      break;
    }
  }

  void VisitDeclRefExpr(const DeclRefExpr *S) {
    const ValueDecl *VD = S->getDecl();
    assert(VD != nullptr);

    // Some `DeclRefExpr`s aren't glvalues, so we can't associate them with a
    // `StorageLocation`, and there's also no sensible `Value` that we can
    // assign to them. Examples:
    // - Non-static member variables
    // - Non static member functions
    //   Note: Member operators are an exception to this, but apparently only
    //   if the `DeclRefExpr` is used within the callee of a
    //   `CXXOperatorCallExpr`. In other cases, for example when applying the
    //   address-of operator, the `DeclRefExpr` is a prvalue.
    if (!S->isGLValue())
      return;

    auto *DeclLoc = Env.getStorageLocation(*VD);
    if (DeclLoc == nullptr)
      return;

    Env.setStorageLocation(*S, *DeclLoc);
  }

  void VisitDeclStmt(const DeclStmt *S) {
    // Group decls are converted into single decls in the CFG so the cast below
    // is safe.
    const auto &D = *cast<VarDecl>(S->getSingleDecl());

    ProcessVarDecl(D);
  }

  void ProcessVarDecl(const VarDecl &D) {
    // Static local vars are already initialized in `Environment`.
    if (D.hasGlobalStorage())
      return;

    // If this is the holding variable for a `BindingDecl`, we may already
    // have a storage location set up -- so check. (See also explanation below
    // where we process the `BindingDecl`.)
    if (D.getType()->isReferenceType() && Env.getStorageLocation(D) != nullptr)
      return;

    assert(Env.getStorageLocation(D) == nullptr);

    Env.setStorageLocation(D, Env.createObject(D));

    // `DecompositionDecl` must be handled after we've interpreted the loc
    // itself, because the binding expression refers back to the
    // `DecompositionDecl` (even though it has no written name).
    if (const auto *Decomp = dyn_cast<DecompositionDecl>(&D)) {
      // If VarDecl is a DecompositionDecl, evaluate each of its bindings. This
      // needs to be evaluated after initializing the values in the storage for
      // VarDecl, as the bindings refer to them.
      // FIXME: Add support for ArraySubscriptExpr.
      // FIXME: Consider adding AST nodes used in BindingDecls to the CFG.
      for (const auto *B : Decomp->bindings()) {
        if (auto *ME = dyn_cast_or_null<MemberExpr>(B->getBinding())) {
          auto *DE = dyn_cast_or_null<DeclRefExpr>(ME->getBase());
          if (DE == nullptr)
            continue;

          // ME and its base haven't been visited because they aren't included
          // in the statements of the CFG basic block.
          VisitDeclRefExpr(DE);
          VisitMemberExpr(ME);

          if (auto *Loc = Env.getStorageLocation(*ME))
            Env.setStorageLocation(*B, *Loc);
        } else if (auto *VD = B->getHoldingVar()) {
          // Holding vars are used to back the `BindingDecl`s of tuple-like
          // types. The holding var declarations appear after the
          // `DecompositionDecl`, so we have to explicitly process them here
          // to know their storage location. They will be processed a second
          // time when we visit their `VarDecl`s, so we have code that protects
          // against this above.
          ProcessVarDecl(*VD);
          auto *VDLoc = Env.getStorageLocation(*VD);
          assert(VDLoc != nullptr);
          Env.setStorageLocation(*B, *VDLoc);
        }
      }
    }
  }

  void VisitImplicitCastExpr(const ImplicitCastExpr *S) {
    const Expr *SubExpr = S->getSubExpr();
    assert(SubExpr != nullptr);

    switch (S->getCastKind()) {
    case CK_IntegralToBoolean: {
      // This cast creates a new, boolean value from the integral value. We
      // model that with a fresh value in the environment, unless it's already a
      // boolean.
      if (auto *SubExprVal =
              dyn_cast_or_null<BoolValue>(Env.getValue(*SubExpr)))
        Env.setValue(*S, *SubExprVal);
      else
        // FIXME: If integer modeling is added, then update this code to create
        // the boolean based on the integer model.
        Env.setValue(*S, Env.makeAtomicBoolValue());
      break;
    }

    case CK_LValueToRValue: {
      // When an L-value is used as an R-value, it may result in sharing, so we
      // need to unpack any nested `Top`s.
      auto *SubExprVal = maybeUnpackLValueExpr(*SubExpr, Env);
      if (SubExprVal == nullptr)
        break;

      Env.setValue(*S, *SubExprVal);
      break;
    }

    case CK_IntegralCast:
      // FIXME: This cast creates a new integral value from the
      // subexpression. But, because we don't model integers, we don't
      // distinguish between this new value and the underlying one. If integer
      // modeling is added, then update this code to create a fresh location and
      // value.
    case CK_UncheckedDerivedToBase:
    case CK_ConstructorConversion:
    case CK_UserDefinedConversion:
      // FIXME: Add tests that excercise CK_UncheckedDerivedToBase,
      // CK_ConstructorConversion, and CK_UserDefinedConversion.
    case CK_NoOp: {
      // FIXME: Consider making `Environment::getStorageLocation` skip noop
      // expressions (this and other similar expressions in the file) instead
      // of assigning them storage locations.
      propagateValueOrStorageLocation(*SubExpr, *S, Env);
      break;
    }
    case CK_NullToPointer: {
      auto &NullPointerVal =
          Env.getOrCreateNullPointerValue(S->getType()->getPointeeType());
      Env.setValue(*S, NullPointerVal);
      break;
    }
    case CK_NullToMemberPointer:
      // FIXME: Implement pointers to members. For now, don't associate a value
      // with this expression.
      break;
    case CK_FunctionToPointerDecay: {
      StorageLocation *PointeeLoc = Env.getStorageLocation(*SubExpr);
      if (PointeeLoc == nullptr)
        break;

      Env.setValue(*S, Env.create<PointerValue>(*PointeeLoc));
      break;
    }
    case CK_BuiltinFnToFnPtr:
      // Despite its name, the result type of `BuiltinFnToFnPtr` is a function,
      // not a function pointer. In addition, builtin functions can only be
      // called directly; it is not legal to take their address. We therefore
      // don't need to create a value or storage location for them.
      break;
    default:
      break;
    }
  }

  void VisitUnaryOperator(const UnaryOperator *S) {
    const Expr *SubExpr = S->getSubExpr();
    assert(SubExpr != nullptr);

    switch (S->getOpcode()) {
    case UO_Deref: {
      const auto *SubExprVal = Env.get<PointerValue>(*SubExpr);
      if (SubExprVal == nullptr)
        break;

      Env.setStorageLocation(*S, SubExprVal->getPointeeLoc());
      break;
    }
    case UO_AddrOf: {
      // FIXME: Model pointers to members.
      if (S->getType()->isMemberPointerType())
        break;

      if (StorageLocation *PointeeLoc = Env.getStorageLocation(*SubExpr))
        Env.setValue(*S, Env.create<PointerValue>(*PointeeLoc));
      break;
    }
    case UO_LNot: {
      auto *SubExprVal = dyn_cast_or_null<BoolValue>(Env.getValue(*SubExpr));
      if (SubExprVal == nullptr)
        break;

      Env.setValue(*S, Env.makeNot(*SubExprVal));
      break;
    }
    case UO_PreInc:
    case UO_PreDec:
      // Propagate the storage location and clear out any value associated with
      // it (to represent the fact that the value has definitely changed).
      // To avoid generating unnecessary values, we leave it to the specific
      // analysis to create a new value if desired.
      propagateStorageLocation(*S->getSubExpr(), *S, Env);
      if (StorageLocation *Loc = Env.getStorageLocation(*S->getSubExpr()))
        Env.clearValue(*Loc);
      break;
    case UO_PostInc:
    case UO_PostDec:
      // Propagate the old value, then clear out any value associated with the
      // storage location (to represent the fact that the value has definitely
      // changed). See above for rationale.
      propagateValue(*S->getSubExpr(), *S, Env);
      if (StorageLocation *Loc = Env.getStorageLocation(*S->getSubExpr()))
        Env.clearValue(*Loc);
      break;
    default:
      break;
    }
  }

  void VisitCXXThisExpr(const CXXThisExpr *S) {
    auto *ThisPointeeLoc = Env.getThisPointeeStorageLocation();
    if (ThisPointeeLoc == nullptr)
      // Unions are not supported yet, and will not have a location for the
      // `this` expression's pointee.
      return;

    Env.setValue(*S, Env.create<PointerValue>(*ThisPointeeLoc));
  }

  void VisitCXXNewExpr(const CXXNewExpr *S) {
    if (Value *Val = Env.createValue(S->getType()))
      Env.setValue(*S, *Val);
  }

  void VisitCXXDeleteExpr(const CXXDeleteExpr *S) {
    // Empty method.
    // We consciously don't do anything on deletes.  Diagnosing double deletes
    // (for example) should be done by a specific analysis, not by the
    // framework.
  }

  void VisitReturnStmt(const ReturnStmt *S) {
    if (!Env.getDataflowAnalysisContext().getOptions().ContextSensitiveOpts)
      return;

    auto *Ret = S->getRetValue();
    if (Ret == nullptr)
      return;

    if (Ret->isPRValue()) {
      if (Ret->getType()->isRecordType())
        return;

      auto *Val = Env.getValue(*Ret);
      if (Val == nullptr)
        return;

      // FIXME: Model NRVO.
      Env.setReturnValue(Val);
    } else {
      auto *Loc = Env.getStorageLocation(*Ret);
      if (Loc == nullptr)
        return;

      // FIXME: Model NRVO.
      Env.setReturnStorageLocation(Loc);
    }
  }

  void VisitMemberExpr(const MemberExpr *S) {
    ValueDecl *Member = S->getMemberDecl();
    assert(Member != nullptr);

    // FIXME: Consider assigning pointer values to function member expressions.
    if (Member->isFunctionOrFunctionTemplate())
      return;

    // FIXME: if/when we add support for modeling enums, use that support here.
    if (isa<EnumConstantDecl>(Member))
      return;

    if (auto *D = dyn_cast<VarDecl>(Member)) {
      if (D->hasGlobalStorage()) {
        auto *VarDeclLoc = Env.getStorageLocation(*D);
        if (VarDeclLoc == nullptr)
          return;

        Env.setStorageLocation(*S, *VarDeclLoc);
        return;
      }
    }

    RecordStorageLocation *BaseLoc = getBaseObjectLocation(*S, Env);
    if (BaseLoc == nullptr)
      return;

    auto *MemberLoc = BaseLoc->getChild(*Member);
    if (MemberLoc == nullptr)
      return;
    Env.setStorageLocation(*S, *MemberLoc);
  }

  void VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *S) {
    const Expr *ArgExpr = S->getExpr();
    assert(ArgExpr != nullptr);
    propagateValueOrStorageLocation(*ArgExpr, *S, Env);

    if (S->isPRValue() && S->getType()->isRecordType()) {
      auto &Loc = Env.getResultObjectLocation(*S);
      Env.initializeFieldsWithValues(Loc);
    }
  }

  void VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *S) {
    const Expr *InitExpr = S->getExpr();
    assert(InitExpr != nullptr);

    // If this is a prvalue of record type, the handler for `*InitExpr` (if one
    // exists) will initialize the result object; there is no value to propgate
    // here.
    if (S->getType()->isRecordType() && S->isPRValue())
      return;

    propagateValueOrStorageLocation(*InitExpr, *S, Env);
  }

  void VisitCXXConstructExpr(const CXXConstructExpr *S) {
    const CXXConstructorDecl *ConstructorDecl = S->getConstructor();
    assert(ConstructorDecl != nullptr);

    // `CXXConstructExpr` can have array type if default-initializing an array
    // of records. We don't handle this specifically beyond potentially inlining
    // the call.
    if (!S->getType()->isRecordType()) {
      transferInlineCall(S, ConstructorDecl);
      return;
    }

    RecordStorageLocation &Loc = Env.getResultObjectLocation(*S);

    if (ConstructorDecl->isCopyOrMoveConstructor()) {
      // It is permissible for a copy/move constructor to have additional
      // parameters as long as they have default arguments defined for them.
      assert(S->getNumArgs() != 0);

      const Expr *Arg = S->getArg(0);
      assert(Arg != nullptr);

      auto *ArgLoc = Env.get<RecordStorageLocation>(*Arg);
      if (ArgLoc == nullptr)
        return;

      // Even if the copy/move constructor call is elidable, we choose to copy
      // the record in all cases (which isn't wrong, just potentially not
      // optimal).
      copyRecord(*ArgLoc, Loc, Env);
      return;
    }

    Env.initializeFieldsWithValues(Loc, S->getType());

    transferInlineCall(S, ConstructorDecl);
  }

  void VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *S) {
    if (S->getOperator() == OO_Equal) {
      assert(S->getNumArgs() == 2);

      const Expr *Arg0 = S->getArg(0);
      assert(Arg0 != nullptr);

      const Expr *Arg1 = S->getArg(1);
      assert(Arg1 != nullptr);

      // Evaluate only copy and move assignment operators.
      const auto *Method =
          dyn_cast_or_null<CXXMethodDecl>(S->getDirectCallee());
      if (!Method)
        return;
      if (!Method->isCopyAssignmentOperator() &&
          !Method->isMoveAssignmentOperator())
        return;

      RecordStorageLocation *LocSrc = nullptr;
      if (Arg1->isPRValue()) {
        LocSrc = &Env.getResultObjectLocation(*Arg1);
      } else {
        LocSrc = Env.get<RecordStorageLocation>(*Arg1);
      }
      auto *LocDst = Env.get<RecordStorageLocation>(*Arg0);

      if (LocSrc == nullptr || LocDst == nullptr)
        return;

      copyRecord(*LocSrc, *LocDst, Env);

      // The assignment operator can have an arbitrary return type. We model the
      // return value only if the return type is the same as or a base class of
      // the destination type.
      if (S->getType().getCanonicalType().getUnqualifiedType() !=
          LocDst->getType().getCanonicalType().getUnqualifiedType()) {
        auto ReturnDecl = S->getType()->getAsCXXRecordDecl();
        auto DstDecl = LocDst->getType()->getAsCXXRecordDecl();
        if (ReturnDecl == nullptr || DstDecl == nullptr)
          return;
        if (!DstDecl->isDerivedFrom(ReturnDecl))
          return;
      }

      if (S->isGLValue())
        Env.setStorageLocation(*S, *LocDst);
      else
        copyRecord(*LocDst, Env.getResultObjectLocation(*S), Env);

      return;
    }

    // `CXXOperatorCallExpr` can be a prvalue. Call `VisitCallExpr`() to
    // initialize the prvalue's fields with values.
    VisitCallExpr(S);
  }

  void VisitCXXRewrittenBinaryOperator(const CXXRewrittenBinaryOperator *RBO) {
    propagateValue(*RBO->getSemanticForm(), *RBO, Env);
  }

  void VisitCallExpr(const CallExpr *S) {
    // Of clang's builtins, only `__builtin_expect` is handled explicitly, since
    // others (like trap, debugtrap, and unreachable) are handled by CFG
    // construction.
    if (S->isCallToStdMove()) {
      assert(S->getNumArgs() == 1);

      const Expr *Arg = S->getArg(0);
      assert(Arg != nullptr);

      auto *ArgLoc = Env.getStorageLocation(*Arg);
      if (ArgLoc == nullptr)
        return;

      Env.setStorageLocation(*S, *ArgLoc);
    } else if (S->getDirectCallee() != nullptr &&
               S->getDirectCallee()->getBuiltinID() ==
                   Builtin::BI__builtin_expect) {
      assert(S->getNumArgs() > 0);
      assert(S->getArg(0) != nullptr);
      auto *ArgVal = Env.getValue(*S->getArg(0));
      if (ArgVal == nullptr)
        return;
      Env.setValue(*S, *ArgVal);
    } else if (const FunctionDecl *F = S->getDirectCallee()) {
      transferInlineCall(S, F);

      // If this call produces a prvalue of record type, initialize its fields
      // with values.
      if (S->getType()->isRecordType() && S->isPRValue()) {
        RecordStorageLocation &Loc = Env.getResultObjectLocation(*S);
        Env.initializeFieldsWithValues(Loc);
      }
    }
  }

  void VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *S) {
    const Expr *SubExpr = S->getSubExpr();
    assert(SubExpr != nullptr);

    StorageLocation &Loc = Env.createStorageLocation(*S);
    Env.setStorageLocation(*S, Loc);

    if (SubExpr->getType()->isRecordType())
      // Nothing else left to do -- we initialized the record when transferring
      // `SubExpr`.
      return;

    if (Value *SubExprVal = Env.getValue(*SubExpr))
      Env.setValue(Loc, *SubExprVal);
  }

  void VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *S) {
    const Expr *SubExpr = S->getSubExpr();
    assert(SubExpr != nullptr);

    propagateValue(*SubExpr, *S, Env);
  }

  void VisitCXXStaticCastExpr(const CXXStaticCastExpr *S) {
    if (S->getCastKind() == CK_NoOp) {
      const Expr *SubExpr = S->getSubExpr();
      assert(SubExpr != nullptr);

      propagateValueOrStorageLocation(*SubExpr, *S, Env);
    }
  }

  void VisitConditionalOperator(const ConditionalOperator *S) {
    const Environment *TrueEnv = StmtToEnv.getEnvironment(*S->getTrueExpr());
    const Environment *FalseEnv = StmtToEnv.getEnvironment(*S->getFalseExpr());

    if (TrueEnv == nullptr || FalseEnv == nullptr) {
      // If the true or false branch is dead, we may not have an environment for
      // it. We could handle this specifically by forwarding the value or
      // location of the live branch, but this case is rare enough that this
      // probably isn't worth the additional complexity.
      return;
    }

    if (S->isGLValue()) {
      StorageLocation *TrueLoc = TrueEnv->getStorageLocation(*S->getTrueExpr());
      StorageLocation *FalseLoc =
          FalseEnv->getStorageLocation(*S->getFalseExpr());
      if (TrueLoc == FalseLoc && TrueLoc != nullptr)
        Env.setStorageLocation(*S, *TrueLoc);
    } else if (!S->getType()->isRecordType()) {
      // The conditional operator can evaluate to either of the values of the
      // two branches. To model this, join these two values together to yield
      // the result of the conditional operator.
      // Note: Most joins happen in `computeBlockInputState()`, but this case is
      // different:
      // - `computeBlockInputState()` (which in turn calls `Environment::join()`
      //   joins values associated with the _same_ expression or storage
      //   location, then associates the joined value with that expression or
      //   storage location. This join has nothing to do with transfer --
      //   instead, it joins together the results of performing transfer on two
      //   different blocks.
      // - Here, we join values associated with _different_ expressions (the
      //   true and false branch), then associate the joined value with a third
      //   expression (the conditional operator itself). This join is what it
      //   means to perform transfer on the conditional operator.
      if (Value *Val = Environment::joinValues(
              S->getType(), TrueEnv->getValue(*S->getTrueExpr()), *TrueEnv,
              FalseEnv->getValue(*S->getFalseExpr()), *FalseEnv, Env, Model))
        Env.setValue(*S, *Val);
    }
  }

  void VisitInitListExpr(const InitListExpr *S) {
    QualType Type = S->getType();

    if (!Type->isRecordType()) {
      // Until array initialization is implemented, we skip arrays and don't
      // need to care about cases where `getNumInits() > 1`.
      if (!Type->isArrayType() && S->getNumInits() == 1)
        propagateValueOrStorageLocation(*S->getInit(0), *S, Env);
      return;
    }

    // If the initializer list is transparent, there's nothing to do.
    if (S->isSemanticForm() && S->isTransparent())
      return;

    RecordStorageLocation &Loc = Env.getResultObjectLocation(*S);

    // Initialization of base classes and fields of record type happens when we
    // visit the nested `CXXConstructExpr` or `InitListExpr` for that base class
    // or field. We therefore only need to deal with fields of non-record type
    // here.

    RecordInitListHelper InitListHelper(S);

    for (auto [Field, Init] : InitListHelper.field_inits()) {
      if (Field->getType()->isRecordType())
        continue;
      if (Field->getType()->isReferenceType()) {
        assert(Field->getType().getCanonicalType()->getPointeeType() ==
               Init->getType().getCanonicalType());
        Loc.setChild(*Field, &Env.createObject(Field->getType(), Init));
        continue;
      }
      assert(Field->getType().getCanonicalType().getUnqualifiedType() ==
             Init->getType().getCanonicalType().getUnqualifiedType());
      StorageLocation *FieldLoc = Loc.getChild(*Field);
      // Locations for non-reference fields must always be non-null.
      assert(FieldLoc != nullptr);
      Value *Val = Env.getValue(*Init);
      if (Val == nullptr && isa<ImplicitValueInitExpr>(Init) &&
          Init->getType()->isPointerType())
        Val =
            &Env.getOrCreateNullPointerValue(Init->getType()->getPointeeType());
      if (Val == nullptr)
        Val = Env.createValue(Field->getType());
      if (Val != nullptr)
        Env.setValue(*FieldLoc, *Val);
    }

    for (const auto &[FieldName, FieldLoc] : Loc.synthetic_fields()) {
      QualType FieldType = FieldLoc->getType();
      if (FieldType->isRecordType()) {
        Env.initializeFieldsWithValues(*cast<RecordStorageLocation>(FieldLoc));
      } else {
        if (Value *Val = Env.createValue(FieldType))
          Env.setValue(*FieldLoc, *Val);
      }
    }

    // FIXME: Implement array initialization.
  }

  void VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *S) {
    Env.setValue(*S, Env.getBoolLiteralValue(S->getValue()));
  }

  void VisitIntegerLiteral(const IntegerLiteral *S) {
    Env.setValue(*S, Env.getIntLiteralValue(S->getValue()));
  }

  void VisitParenExpr(const ParenExpr *S) {
    // The CFG does not contain `ParenExpr` as top-level statements in basic
    // blocks, however manual traversal to sub-expressions may encounter them.
    // Redirect to the sub-expression.
    auto *SubExpr = S->getSubExpr();
    assert(SubExpr != nullptr);
    Visit(SubExpr);
  }

  void VisitExprWithCleanups(const ExprWithCleanups *S) {
    // The CFG does not contain `ExprWithCleanups` as top-level statements in
    // basic blocks, however manual traversal to sub-expressions may encounter
    // them. Redirect to the sub-expression.
    auto *SubExpr = S->getSubExpr();
    assert(SubExpr != nullptr);
    Visit(SubExpr);
  }

private:
  /// Returns the value for the sub-expression `SubExpr` of a logic operator.
  BoolValue &getLogicOperatorSubExprValue(const Expr &SubExpr) {
    // `SubExpr` and its parent logic operator might be part of different basic
    // blocks. We try to access the value that is assigned to `SubExpr` in the
    // corresponding environment.
    if (const Environment *SubExprEnv = StmtToEnv.getEnvironment(SubExpr))
      if (auto *Val =
              dyn_cast_or_null<BoolValue>(SubExprEnv->getValue(SubExpr)))
        return *Val;

    // The sub-expression may lie within a basic block that isn't reachable,
    // even if we need it to evaluate the current (reachable) expression
    // (see https://discourse.llvm.org/t/70775). In this case, visit `SubExpr`
    // within the current environment and then try to get the value that gets
    // assigned to it.
    if (Env.getValue(SubExpr) == nullptr)
      Visit(&SubExpr);
    if (auto *Val = dyn_cast_or_null<BoolValue>(Env.getValue(SubExpr)))
      return *Val;

    // If the value of `SubExpr` is still unknown, we create a fresh symbolic
    // boolean value for it.
    return Env.makeAtomicBoolValue();
  }

  // If context sensitivity is enabled, try to analyze the body of the callee
  // `F` of `S`. The type `E` must be either `CallExpr` or `CXXConstructExpr`.
  template <typename E>
  void transferInlineCall(const E *S, const FunctionDecl *F) {
    const auto &Options = Env.getDataflowAnalysisContext().getOptions();
    if (!(Options.ContextSensitiveOpts &&
          Env.canDescend(Options.ContextSensitiveOpts->Depth, F)))
      return;

    const AdornedCFG *ACFG = Env.getDataflowAnalysisContext().getAdornedCFG(F);
    if (!ACFG)
      return;

    // FIXME: We don't support context-sensitive analysis of recursion, so
    // we should return early here if `F` is the same as the `FunctionDecl`
    // holding `S` itself.

    auto ExitBlock = ACFG->getCFG().getExit().getBlockID();

    auto CalleeEnv = Env.pushCall(S);

    // FIXME: Use the same analysis as the caller for the callee. Note,
    // though, that doing so would require support for changing the analysis's
    // ASTContext.
    auto Analysis = NoopAnalysis(ACFG->getDecl().getASTContext(),
                                 DataflowAnalysisOptions{Options});

    auto BlockToOutputState =
        dataflow::runDataflowAnalysis(*ACFG, Analysis, CalleeEnv);
    assert(BlockToOutputState);
    assert(ExitBlock < BlockToOutputState->size());

    auto &ExitState = (*BlockToOutputState)[ExitBlock];
    assert(ExitState);

    Env.popCall(S, ExitState->Env);
  }

  const StmtToEnvMap &StmtToEnv;
  Environment &Env;
  Environment::ValueModel &Model;
};

} // namespace

void transfer(const StmtToEnvMap &StmtToEnv, const Stmt &S, Environment &Env,
              Environment::ValueModel &Model) {
  TransferVisitor(StmtToEnv, Env, Model).Visit(&S);
}

} // namespace dataflow
} // namespace clang
