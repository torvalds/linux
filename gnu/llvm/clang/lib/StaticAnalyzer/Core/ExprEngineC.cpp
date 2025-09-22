//=-- ExprEngineC.cpp - ExprEngine support for C expressions ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines ExprEngine's support for C expressions.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include <optional>

using namespace clang;
using namespace ento;
using llvm::APSInt;

/// Optionally conjure and return a symbol for offset when processing
/// an expression \p Expression.
/// If \p Other is a location, conjure a symbol for \p Symbol
/// (offset) if it is unknown so that memory arithmetic always
/// results in an ElementRegion.
/// \p Count The number of times the current basic block was visited.
static SVal conjureOffsetSymbolOnLocation(
    SVal Symbol, SVal Other, Expr* Expression, SValBuilder &svalBuilder,
    unsigned Count, const LocationContext *LCtx) {
  QualType Ty = Expression->getType();
  if (isa<Loc>(Other) && Ty->isIntegralOrEnumerationType() &&
      Symbol.isUnknown()) {
    return svalBuilder.conjureSymbolVal(Expression, LCtx, Ty, Count);
  }
  return Symbol;
}

void ExprEngine::VisitBinaryOperator(const BinaryOperator* B,
                                     ExplodedNode *Pred,
                                     ExplodedNodeSet &Dst) {

  Expr *LHS = B->getLHS()->IgnoreParens();
  Expr *RHS = B->getRHS()->IgnoreParens();

  // FIXME: Prechecks eventually go in ::Visit().
  ExplodedNodeSet CheckedSet;
  ExplodedNodeSet Tmp2;
  getCheckerManager().runCheckersForPreStmt(CheckedSet, Pred, B, *this);

  // With both the LHS and RHS evaluated, process the operation itself.
  for (ExplodedNodeSet::iterator it=CheckedSet.begin(), ei=CheckedSet.end();
         it != ei; ++it) {

    ProgramStateRef state = (*it)->getState();
    const LocationContext *LCtx = (*it)->getLocationContext();
    SVal LeftV = state->getSVal(LHS, LCtx);
    SVal RightV = state->getSVal(RHS, LCtx);

    BinaryOperator::Opcode Op = B->getOpcode();

    if (Op == BO_Assign) {
      // EXPERIMENTAL: "Conjured" symbols.
      // FIXME: Handle structs.
      if (RightV.isUnknown()) {
        unsigned Count = currBldrCtx->blockCount();
        RightV = svalBuilder.conjureSymbolVal(nullptr, B->getRHS(), LCtx,
                                              Count);
      }
      // Simulate the effects of a "store":  bind the value of the RHS
      // to the L-Value represented by the LHS.
      SVal ExprVal = B->isGLValue() ? LeftV : RightV;
      evalStore(Tmp2, B, LHS, *it, state->BindExpr(B, LCtx, ExprVal),
                LeftV, RightV);
      continue;
    }

    if (!B->isAssignmentOp()) {
      StmtNodeBuilder Bldr(*it, Tmp2, *currBldrCtx);

      if (B->isAdditiveOp()) {
        // TODO: This can be removed after we enable history tracking with
        // SymSymExpr.
        unsigned Count = currBldrCtx->blockCount();
        RightV = conjureOffsetSymbolOnLocation(
            RightV, LeftV, RHS, svalBuilder, Count, LCtx);
        LeftV = conjureOffsetSymbolOnLocation(
            LeftV, RightV, LHS, svalBuilder, Count, LCtx);
      }

      // Although we don't yet model pointers-to-members, we do need to make
      // sure that the members of temporaries have a valid 'this' pointer for
      // other checks.
      if (B->getOpcode() == BO_PtrMemD)
        state = createTemporaryRegionIfNeeded(state, LCtx, LHS);

      // Process non-assignments except commas or short-circuited
      // logical expressions (LAnd and LOr).
      SVal Result = evalBinOp(state, Op, LeftV, RightV, B->getType());
      if (!Result.isUnknown()) {
        state = state->BindExpr(B, LCtx, Result);
      } else {
        // If we cannot evaluate the operation escape the operands.
        state = escapeValues(state, LeftV, PSK_EscapeOther);
        state = escapeValues(state, RightV, PSK_EscapeOther);
      }

      Bldr.generateNode(B, *it, state);
      continue;
    }

    assert (B->isCompoundAssignmentOp());

    switch (Op) {
      default:
        llvm_unreachable("Invalid opcode for compound assignment.");
      case BO_MulAssign: Op = BO_Mul; break;
      case BO_DivAssign: Op = BO_Div; break;
      case BO_RemAssign: Op = BO_Rem; break;
      case BO_AddAssign: Op = BO_Add; break;
      case BO_SubAssign: Op = BO_Sub; break;
      case BO_ShlAssign: Op = BO_Shl; break;
      case BO_ShrAssign: Op = BO_Shr; break;
      case BO_AndAssign: Op = BO_And; break;
      case BO_XorAssign: Op = BO_Xor; break;
      case BO_OrAssign:  Op = BO_Or;  break;
    }

    // Perform a load (the LHS).  This performs the checks for
    // null dereferences, and so on.
    ExplodedNodeSet Tmp;
    SVal location = LeftV;
    evalLoad(Tmp, B, LHS, *it, state, location);

    for (ExplodedNode *N : Tmp) {
      state = N->getState();
      const LocationContext *LCtx = N->getLocationContext();
      SVal V = state->getSVal(LHS, LCtx);

      // Get the computation type.
      QualType CTy =
        cast<CompoundAssignOperator>(B)->getComputationResultType();
      CTy = getContext().getCanonicalType(CTy);

      QualType CLHSTy =
        cast<CompoundAssignOperator>(B)->getComputationLHSType();
      CLHSTy = getContext().getCanonicalType(CLHSTy);

      QualType LTy = getContext().getCanonicalType(LHS->getType());

      // Promote LHS.
      V = svalBuilder.evalCast(V, CLHSTy, LTy);

      // Compute the result of the operation.
      SVal Result = svalBuilder.evalCast(evalBinOp(state, Op, V, RightV, CTy),
                                         B->getType(), CTy);

      // EXPERIMENTAL: "Conjured" symbols.
      // FIXME: Handle structs.

      SVal LHSVal;

      if (Result.isUnknown()) {
        // The symbolic value is actually for the type of the left-hand side
        // expression, not the computation type, as this is the value the
        // LValue on the LHS will bind to.
        LHSVal = svalBuilder.conjureSymbolVal(nullptr, B->getRHS(), LCtx, LTy,
                                              currBldrCtx->blockCount());
        // However, we need to convert the symbol to the computation type.
        Result = svalBuilder.evalCast(LHSVal, CTy, LTy);
      } else {
        // The left-hand side may bind to a different value then the
        // computation type.
        LHSVal = svalBuilder.evalCast(Result, LTy, CTy);
      }

      // In C++, assignment and compound assignment operators return an
      // lvalue.
      if (B->isGLValue())
        state = state->BindExpr(B, LCtx, location);
      else
        state = state->BindExpr(B, LCtx, Result);

      evalStore(Tmp2, B, LHS, N, state, location, LHSVal);
    }
  }

  // FIXME: postvisits eventually go in ::Visit()
  getCheckerManager().runCheckersForPostStmt(Dst, Tmp2, B, *this);
}

void ExprEngine::VisitBlockExpr(const BlockExpr *BE, ExplodedNode *Pred,
                                ExplodedNodeSet &Dst) {

  CanQualType T = getContext().getCanonicalType(BE->getType());

  const BlockDecl *BD = BE->getBlockDecl();
  // Get the value of the block itself.
  SVal V = svalBuilder.getBlockPointer(BD, T,
                                       Pred->getLocationContext(),
                                       currBldrCtx->blockCount());

  ProgramStateRef State = Pred->getState();

  // If we created a new MemRegion for the block, we should explicitly bind
  // the captured variables.
  if (const BlockDataRegion *BDR =
      dyn_cast_or_null<BlockDataRegion>(V.getAsRegion())) {

    auto ReferencedVars = BDR->referenced_vars();
    auto CI = BD->capture_begin();
    auto CE = BD->capture_end();
    for (auto Var : ReferencedVars) {
      const VarRegion *capturedR = Var.getCapturedRegion();
      const TypedValueRegion *originalR = Var.getOriginalRegion();

      // If the capture had a copy expression, use the result of evaluating
      // that expression, otherwise use the original value.
      // We rely on the invariant that the block declaration's capture variables
      // are a prefix of the BlockDataRegion's referenced vars (which may include
      // referenced globals, etc.) to enable fast lookup of the capture for a
      // given referenced var.
      const Expr *copyExpr = nullptr;
      if (CI != CE) {
        assert(CI->getVariable() == capturedR->getDecl());
        copyExpr = CI->getCopyExpr();
        CI++;
      }

      if (capturedR != originalR) {
        SVal originalV;
        const LocationContext *LCtx = Pred->getLocationContext();
        if (copyExpr) {
          originalV = State->getSVal(copyExpr, LCtx);
        } else {
          originalV = State->getSVal(loc::MemRegionVal(originalR));
        }
        State = State->bindLoc(loc::MemRegionVal(capturedR), originalV, LCtx);
      }
    }
  }

  ExplodedNodeSet Tmp;
  StmtNodeBuilder Bldr(Pred, Tmp, *currBldrCtx);
  Bldr.generateNode(BE, Pred,
                    State->BindExpr(BE, Pred->getLocationContext(), V),
                    nullptr, ProgramPoint::PostLValueKind);

  // FIXME: Move all post/pre visits to ::Visit().
  getCheckerManager().runCheckersForPostStmt(Dst, Tmp, BE, *this);
}

ProgramStateRef ExprEngine::handleLValueBitCast(
    ProgramStateRef state, const Expr* Ex, const LocationContext* LCtx,
    QualType T, QualType ExTy, const CastExpr* CastE, StmtNodeBuilder& Bldr,
    ExplodedNode* Pred) {
  if (T->isLValueReferenceType()) {
    assert(!CastE->getType()->isLValueReferenceType());
    ExTy = getContext().getLValueReferenceType(ExTy);
  } else if (T->isRValueReferenceType()) {
    assert(!CastE->getType()->isRValueReferenceType());
    ExTy = getContext().getRValueReferenceType(ExTy);
  }
  // Delegate to SValBuilder to process.
  SVal OrigV = state->getSVal(Ex, LCtx);
  SVal SimplifiedOrigV = svalBuilder.simplifySVal(state, OrigV);
  SVal V = svalBuilder.evalCast(SimplifiedOrigV, T, ExTy);
  // Negate the result if we're treating the boolean as a signed i1
  if (CastE->getCastKind() == CK_BooleanToSignedIntegral && V.isValid())
    V = svalBuilder.evalMinus(V.castAs<NonLoc>());

  state = state->BindExpr(CastE, LCtx, V);
  if (V.isUnknown() && !OrigV.isUnknown()) {
    state = escapeValues(state, OrigV, PSK_EscapeOther);
  }
  Bldr.generateNode(CastE, Pred, state);

  return state;
}

void ExprEngine::VisitCast(const CastExpr *CastE, const Expr *Ex,
                           ExplodedNode *Pred, ExplodedNodeSet &Dst) {

  ExplodedNodeSet dstPreStmt;
  getCheckerManager().runCheckersForPreStmt(dstPreStmt, Pred, CastE, *this);

  if (CastE->getCastKind() == CK_LValueToRValue ||
      CastE->getCastKind() == CK_LValueToRValueBitCast) {
    for (ExplodedNode *subExprNode : dstPreStmt) {
      ProgramStateRef state = subExprNode->getState();
      const LocationContext *LCtx = subExprNode->getLocationContext();
      evalLoad(Dst, CastE, CastE, subExprNode, state, state->getSVal(Ex, LCtx));
    }
    return;
  }

  // All other casts.
  QualType T = CastE->getType();
  QualType ExTy = Ex->getType();

  if (const ExplicitCastExpr *ExCast=dyn_cast_or_null<ExplicitCastExpr>(CastE))
    T = ExCast->getTypeAsWritten();

  StmtNodeBuilder Bldr(dstPreStmt, Dst, *currBldrCtx);
  for (ExplodedNode *Pred : dstPreStmt) {
    ProgramStateRef state = Pred->getState();
    const LocationContext *LCtx = Pred->getLocationContext();

    switch (CastE->getCastKind()) {
      case CK_LValueToRValue:
      case CK_LValueToRValueBitCast:
        llvm_unreachable("LValueToRValue casts handled earlier.");
      case CK_ToVoid:
        continue;
        // The analyzer doesn't do anything special with these casts,
        // since it understands retain/release semantics already.
      case CK_ARCProduceObject:
      case CK_ARCConsumeObject:
      case CK_ARCReclaimReturnedObject:
      case CK_ARCExtendBlockObject: // Fall-through.
      case CK_CopyAndAutoreleaseBlockObject:
        // The analyser can ignore atomic casts for now, although some future
        // checkers may want to make certain that you're not modifying the same
        // value through atomic and nonatomic pointers.
      case CK_AtomicToNonAtomic:
      case CK_NonAtomicToAtomic:
        // True no-ops.
      case CK_NoOp:
      case CK_ConstructorConversion:
      case CK_UserDefinedConversion:
      case CK_FunctionToPointerDecay:
      case CK_BuiltinFnToFnPtr:
      case CK_HLSLArrayRValue: {
        // Copy the SVal of Ex to CastE.
        ProgramStateRef state = Pred->getState();
        const LocationContext *LCtx = Pred->getLocationContext();
        SVal V = state->getSVal(Ex, LCtx);
        state = state->BindExpr(CastE, LCtx, V);
        Bldr.generateNode(CastE, Pred, state);
        continue;
      }
      case CK_MemberPointerToBoolean:
      case CK_PointerToBoolean: {
        SVal V = state->getSVal(Ex, LCtx);
        auto PTMSV = V.getAs<nonloc::PointerToMember>();
        if (PTMSV)
          V = svalBuilder.makeTruthVal(!PTMSV->isNullMemberPointer(), ExTy);
        if (V.isUndef() || PTMSV) {
          state = state->BindExpr(CastE, LCtx, V);
          Bldr.generateNode(CastE, Pred, state);
          continue;
        }
        // Explicitly proceed with default handler for this case cascade.
        state =
            handleLValueBitCast(state, Ex, LCtx, T, ExTy, CastE, Bldr, Pred);
        continue;
      }
      case CK_Dependent:
      case CK_ArrayToPointerDecay:
      case CK_BitCast:
      case CK_AddressSpaceConversion:
      case CK_BooleanToSignedIntegral:
      case CK_IntegralToPointer:
      case CK_PointerToIntegral: {
        SVal V = state->getSVal(Ex, LCtx);
        if (isa<nonloc::PointerToMember>(V)) {
          state = state->BindExpr(CastE, LCtx, UnknownVal());
          Bldr.generateNode(CastE, Pred, state);
          continue;
        }
        // Explicitly proceed with default handler for this case cascade.
        state =
            handleLValueBitCast(state, Ex, LCtx, T, ExTy, CastE, Bldr, Pred);
        continue;
      }
      case CK_IntegralToBoolean:
      case CK_IntegralToFloating:
      case CK_FloatingToIntegral:
      case CK_FloatingToBoolean:
      case CK_FloatingCast:
      case CK_FloatingRealToComplex:
      case CK_FloatingComplexToReal:
      case CK_FloatingComplexToBoolean:
      case CK_FloatingComplexCast:
      case CK_FloatingComplexToIntegralComplex:
      case CK_IntegralRealToComplex:
      case CK_IntegralComplexToReal:
      case CK_IntegralComplexToBoolean:
      case CK_IntegralComplexCast:
      case CK_IntegralComplexToFloatingComplex:
      case CK_CPointerToObjCPointerCast:
      case CK_BlockPointerToObjCPointerCast:
      case CK_AnyPointerToBlockPointerCast:
      case CK_ObjCObjectLValueCast:
      case CK_ZeroToOCLOpaqueType:
      case CK_IntToOCLSampler:
      case CK_LValueBitCast:
      case CK_FloatingToFixedPoint:
      case CK_FixedPointToFloating:
      case CK_FixedPointCast:
      case CK_FixedPointToBoolean:
      case CK_FixedPointToIntegral:
      case CK_IntegralToFixedPoint: {
        state =
            handleLValueBitCast(state, Ex, LCtx, T, ExTy, CastE, Bldr, Pred);
        continue;
      }
      case CK_IntegralCast: {
        // Delegate to SValBuilder to process.
        SVal V = state->getSVal(Ex, LCtx);
        if (AMgr.options.ShouldSupportSymbolicIntegerCasts)
          V = svalBuilder.evalCast(V, T, ExTy);
        else
          V = svalBuilder.evalIntegralCast(state, V, T, ExTy);
        state = state->BindExpr(CastE, LCtx, V);
        Bldr.generateNode(CastE, Pred, state);
        continue;
      }
      case CK_DerivedToBase:
      case CK_UncheckedDerivedToBase: {
        // For DerivedToBase cast, delegate to the store manager.
        SVal val = state->getSVal(Ex, LCtx);
        val = getStoreManager().evalDerivedToBase(val, CastE);
        state = state->BindExpr(CastE, LCtx, val);
        Bldr.generateNode(CastE, Pred, state);
        continue;
      }
      // Handle C++ dyn_cast.
      case CK_Dynamic: {
        SVal val = state->getSVal(Ex, LCtx);

        // Compute the type of the result.
        QualType resultType = CastE->getType();
        if (CastE->isGLValue())
          resultType = getContext().getPointerType(resultType);

        bool Failed = true;

        // Check if the value being cast does not evaluates to 0.
        if (!val.isZeroConstant())
          if (std::optional<SVal> V =
                  StateMgr.getStoreManager().evalBaseToDerived(val, T)) {
          val = *V;
          Failed = false;
          }

        if (Failed) {
          if (T->isReferenceType()) {
            // A bad_cast exception is thrown if input value is a reference.
            // Currently, we model this, by generating a sink.
            Bldr.generateSink(CastE, Pred, state);
            continue;
          } else {
            // If the cast fails on a pointer, bind to 0.
            state = state->BindExpr(CastE, LCtx,
                                    svalBuilder.makeNullWithType(resultType));
          }
        } else {
          // If we don't know if the cast succeeded, conjure a new symbol.
          if (val.isUnknown()) {
            DefinedOrUnknownSVal NewSym =
              svalBuilder.conjureSymbolVal(nullptr, CastE, LCtx, resultType,
                                           currBldrCtx->blockCount());
            state = state->BindExpr(CastE, LCtx, NewSym);
          } else
            // Else, bind to the derived region value.
            state = state->BindExpr(CastE, LCtx, val);
        }
        Bldr.generateNode(CastE, Pred, state);
        continue;
      }
      case CK_BaseToDerived: {
        SVal val = state->getSVal(Ex, LCtx);
        QualType resultType = CastE->getType();
        if (CastE->isGLValue())
          resultType = getContext().getPointerType(resultType);

        if (!val.isConstant()) {
          std::optional<SVal> V = getStoreManager().evalBaseToDerived(val, T);
          val = V ? *V : UnknownVal();
        }

        // Failed to cast or the result is unknown, fall back to conservative.
        if (val.isUnknown()) {
          val =
            svalBuilder.conjureSymbolVal(nullptr, CastE, LCtx, resultType,
                                         currBldrCtx->blockCount());
        }
        state = state->BindExpr(CastE, LCtx, val);
        Bldr.generateNode(CastE, Pred, state);
        continue;
      }
      case CK_NullToPointer: {
        SVal V = svalBuilder.makeNullWithType(CastE->getType());
        state = state->BindExpr(CastE, LCtx, V);
        Bldr.generateNode(CastE, Pred, state);
        continue;
      }
      case CK_NullToMemberPointer: {
        SVal V = svalBuilder.getMemberPointer(nullptr);
        state = state->BindExpr(CastE, LCtx, V);
        Bldr.generateNode(CastE, Pred, state);
        continue;
      }
      case CK_DerivedToBaseMemberPointer:
      case CK_BaseToDerivedMemberPointer:
      case CK_ReinterpretMemberPointer: {
        SVal V = state->getSVal(Ex, LCtx);
        if (auto PTMSV = V.getAs<nonloc::PointerToMember>()) {
          SVal CastedPTMSV =
              svalBuilder.makePointerToMember(getBasicVals().accumCXXBase(
                  CastE->path(), *PTMSV, CastE->getCastKind()));
          state = state->BindExpr(CastE, LCtx, CastedPTMSV);
          Bldr.generateNode(CastE, Pred, state);
          continue;
        }
        // Explicitly proceed with default handler for this case cascade.
      }
        [[fallthrough]];
      // Various C++ casts that are not handled yet.
      case CK_ToUnion:
      case CK_MatrixCast:
      case CK_VectorSplat:
      case CK_HLSLVectorTruncation: {
        QualType resultType = CastE->getType();
        if (CastE->isGLValue())
          resultType = getContext().getPointerType(resultType);
        SVal result = svalBuilder.conjureSymbolVal(
            /*symbolTag=*/nullptr, CastE, LCtx, resultType,
            currBldrCtx->blockCount());
        state = state->BindExpr(CastE, LCtx, result);
        Bldr.generateNode(CastE, Pred, state);
        continue;
      }
    }
  }
}

void ExprEngine::VisitCompoundLiteralExpr(const CompoundLiteralExpr *CL,
                                          ExplodedNode *Pred,
                                          ExplodedNodeSet &Dst) {
  StmtNodeBuilder B(Pred, Dst, *currBldrCtx);

  ProgramStateRef State = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();

  const Expr *Init = CL->getInitializer();
  SVal V = State->getSVal(CL->getInitializer(), LCtx);

  if (isa<CXXConstructExpr, CXXStdInitializerListExpr>(Init)) {
    // No work needed. Just pass the value up to this expression.
  } else {
    assert(isa<InitListExpr>(Init));
    Loc CLLoc = State->getLValue(CL, LCtx);
    State = State->bindLoc(CLLoc, V, LCtx);

    if (CL->isGLValue())
      V = CLLoc;
  }

  B.generateNode(CL, Pred, State->BindExpr(CL, LCtx, V));
}

void ExprEngine::VisitDeclStmt(const DeclStmt *DS, ExplodedNode *Pred,
                               ExplodedNodeSet &Dst) {
  if (isa<TypedefNameDecl>(*DS->decl_begin())) {
    // C99 6.7.7 "Any array size expressions associated with variable length
    // array declarators are evaluated each time the declaration of the typedef
    // name is reached in the order of execution."
    // The checkers should know about typedef to be able to handle VLA size
    // expressions.
    ExplodedNodeSet DstPre;
    getCheckerManager().runCheckersForPreStmt(DstPre, Pred, DS, *this);
    getCheckerManager().runCheckersForPostStmt(Dst, DstPre, DS, *this);
    return;
  }

  // Assumption: The CFG has one DeclStmt per Decl.
  const VarDecl *VD = dyn_cast_or_null<VarDecl>(*DS->decl_begin());

  if (!VD) {
    //TODO:AZ: remove explicit insertion after refactoring is done.
    Dst.insert(Pred);
    return;
  }

  // FIXME: all pre/post visits should eventually be handled by ::Visit().
  ExplodedNodeSet dstPreVisit;
  getCheckerManager().runCheckersForPreStmt(dstPreVisit, Pred, DS, *this);

  ExplodedNodeSet dstEvaluated;
  StmtNodeBuilder B(dstPreVisit, dstEvaluated, *currBldrCtx);
  for (ExplodedNodeSet::iterator I = dstPreVisit.begin(), E = dstPreVisit.end();
       I!=E; ++I) {
    ExplodedNode *N = *I;
    ProgramStateRef state = N->getState();
    const LocationContext *LC = N->getLocationContext();

    // Decls without InitExpr are not initialized explicitly.
    if (const Expr *InitEx = VD->getInit()) {

      // Note in the state that the initialization has occurred.
      ExplodedNode *UpdatedN = N;
      SVal InitVal = state->getSVal(InitEx, LC);

      assert(DS->isSingleDecl());
      if (getObjectUnderConstruction(state, DS, LC)) {
        state = finishObjectConstruction(state, DS, LC);
        // We constructed the object directly in the variable.
        // No need to bind anything.
        B.generateNode(DS, UpdatedN, state);
      } else {
        // Recover some path-sensitivity if a scalar value evaluated to
        // UnknownVal.
        if (InitVal.isUnknown()) {
          QualType Ty = InitEx->getType();
          if (InitEx->isGLValue()) {
            Ty = getContext().getPointerType(Ty);
          }

          InitVal = svalBuilder.conjureSymbolVal(nullptr, InitEx, LC, Ty,
                                                 currBldrCtx->blockCount());
        }


        B.takeNodes(UpdatedN);
        ExplodedNodeSet Dst2;
        evalBind(Dst2, DS, UpdatedN, state->getLValue(VD, LC), InitVal, true);
        B.addNodes(Dst2);
      }
    }
    else {
      B.generateNode(DS, N, state);
    }
  }

  getCheckerManager().runCheckersForPostStmt(Dst, B.getResults(), DS, *this);
}

void ExprEngine::VisitLogicalExpr(const BinaryOperator* B, ExplodedNode *Pred,
                                  ExplodedNodeSet &Dst) {
  // This method acts upon CFG elements for logical operators && and ||
  // and attaches the value (true or false) to them as expressions.
  // It doesn't produce any state splits.
  // If we made it that far, we're past the point when we modeled the short
  // circuit. It means that we should have precise knowledge about whether
  // we've short-circuited. If we did, we already know the value we need to
  // bind. If we didn't, the value of the RHS (casted to the boolean type)
  // is the answer.
  // Currently this method tries to figure out whether we've short-circuited
  // by looking at the ExplodedGraph. This method is imperfect because there
  // could inevitably have been merges that would have resulted in multiple
  // potential path traversal histories. We bail out when we fail.
  // Due to this ambiguity, a more reliable solution would have been to
  // track the short circuit operation history path-sensitively until
  // we evaluate the respective logical operator.
  assert(B->getOpcode() == BO_LAnd ||
         B->getOpcode() == BO_LOr);

  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  ProgramStateRef state = Pred->getState();

  if (B->getType()->isVectorType()) {
    // FIXME: We do not model vector arithmetic yet. When adding support for
    // that, note that the CFG-based reasoning below does not apply, because
    // logical operators on vectors are not short-circuit. Currently they are
    // modeled as short-circuit in Clang CFG but this is incorrect.
    // Do not set the value for the expression. It'd be UnknownVal by default.
    Bldr.generateNode(B, Pred, state);
    return;
  }

  ExplodedNode *N = Pred;
  while (!N->getLocation().getAs<BlockEntrance>()) {
    ProgramPoint P = N->getLocation();
    assert(P.getAs<PreStmt>()|| P.getAs<PreStmtPurgeDeadSymbols>());
    (void) P;
    if (N->pred_size() != 1) {
      // We failed to track back where we came from.
      Bldr.generateNode(B, Pred, state);
      return;
    }
    N = *N->pred_begin();
  }

  if (N->pred_size() != 1) {
    // We failed to track back where we came from.
    Bldr.generateNode(B, Pred, state);
    return;
  }

  N = *N->pred_begin();
  BlockEdge BE = N->getLocation().castAs<BlockEdge>();
  SVal X;

  // Determine the value of the expression by introspecting how we
  // got this location in the CFG.  This requires looking at the previous
  // block we were in and what kind of control-flow transfer was involved.
  const CFGBlock *SrcBlock = BE.getSrc();
  // The only terminator (if there is one) that makes sense is a logical op.
  CFGTerminator T = SrcBlock->getTerminator();
  if (const BinaryOperator *Term = cast_or_null<BinaryOperator>(T.getStmt())) {
    (void) Term;
    assert(Term->isLogicalOp());
    assert(SrcBlock->succ_size() == 2);
    // Did we take the true or false branch?
    unsigned constant = (*SrcBlock->succ_begin() == BE.getDst()) ? 1 : 0;
    X = svalBuilder.makeIntVal(constant, B->getType());
  }
  else {
    // If there is no terminator, by construction the last statement
    // in SrcBlock is the value of the enclosing expression.
    // However, we still need to constrain that value to be 0 or 1.
    assert(!SrcBlock->empty());
    CFGStmt Elem = SrcBlock->rbegin()->castAs<CFGStmt>();
    const Expr *RHS = cast<Expr>(Elem.getStmt());
    SVal RHSVal = N->getState()->getSVal(RHS, Pred->getLocationContext());

    if (RHSVal.isUndef()) {
      X = RHSVal;
    } else {
      // We evaluate "RHSVal != 0" expression which result in 0 if the value is
      // known to be false, 1 if the value is known to be true and a new symbol
      // when the assumption is unknown.
      nonloc::ConcreteInt Zero(getBasicVals().getValue(0, B->getType()));
      X = evalBinOp(N->getState(), BO_NE,
                    svalBuilder.evalCast(RHSVal, B->getType(), RHS->getType()),
                    Zero, B->getType());
    }
  }
  Bldr.generateNode(B, Pred, state->BindExpr(B, Pred->getLocationContext(), X));
}

void ExprEngine::VisitInitListExpr(const InitListExpr *IE,
                                   ExplodedNode *Pred,
                                   ExplodedNodeSet &Dst) {
  StmtNodeBuilder B(Pred, Dst, *currBldrCtx);

  ProgramStateRef state = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();
  QualType T = getContext().getCanonicalType(IE->getType());
  unsigned NumInitElements = IE->getNumInits();

  if (!IE->isGLValue() && !IE->isTransparent() &&
      (T->isArrayType() || T->isRecordType() || T->isVectorType() ||
       T->isAnyComplexType())) {
    llvm::ImmutableList<SVal> vals = getBasicVals().getEmptySValList();

    // Handle base case where the initializer has no elements.
    // e.g: static int* myArray[] = {};
    if (NumInitElements == 0) {
      SVal V = svalBuilder.makeCompoundVal(T, vals);
      B.generateNode(IE, Pred, state->BindExpr(IE, LCtx, V));
      return;
    }

    for (const Stmt *S : llvm::reverse(*IE)) {
      SVal V = state->getSVal(cast<Expr>(S), LCtx);
      vals = getBasicVals().prependSVal(V, vals);
    }

    B.generateNode(IE, Pred,
                   state->BindExpr(IE, LCtx,
                                   svalBuilder.makeCompoundVal(T, vals)));
    return;
  }

  // Handle scalars: int{5} and int{} and GLvalues.
  // Note, if the InitListExpr is a GLvalue, it means that there is an address
  // representing it, so it must have a single init element.
  assert(NumInitElements <= 1);

  SVal V;
  if (NumInitElements == 0)
    V = getSValBuilder().makeZeroVal(T);
  else
    V = state->getSVal(IE->getInit(0), LCtx);

  B.generateNode(IE, Pred, state->BindExpr(IE, LCtx, V));
}

void ExprEngine::VisitGuardedExpr(const Expr *Ex,
                                  const Expr *L,
                                  const Expr *R,
                                  ExplodedNode *Pred,
                                  ExplodedNodeSet &Dst) {
  assert(L && R);

  StmtNodeBuilder B(Pred, Dst, *currBldrCtx);
  ProgramStateRef state = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();
  const CFGBlock *SrcBlock = nullptr;

  // Find the predecessor block.
  ProgramStateRef SrcState = state;
  for (const ExplodedNode *N = Pred ; N ; N = *N->pred_begin()) {
    ProgramPoint PP = N->getLocation();
    if (PP.getAs<PreStmtPurgeDeadSymbols>() || PP.getAs<BlockEntrance>()) {
      // If the state N has multiple predecessors P, it means that successors
      // of P are all equivalent.
      // In turn, that means that all nodes at P are equivalent in terms
      // of observable behavior at N, and we can follow any of them.
      // FIXME: a more robust solution which does not walk up the tree.
      continue;
    }
    SrcBlock = PP.castAs<BlockEdge>().getSrc();
    SrcState = N->getState();
    break;
  }

  assert(SrcBlock && "missing function entry");

  // Find the last expression in the predecessor block.  That is the
  // expression that is used for the value of the ternary expression.
  bool hasValue = false;
  SVal V;

  for (CFGElement CE : llvm::reverse(*SrcBlock)) {
    if (std::optional<CFGStmt> CS = CE.getAs<CFGStmt>()) {
      const Expr *ValEx = cast<Expr>(CS->getStmt());
      ValEx = ValEx->IgnoreParens();

      // For GNU extension '?:' operator, the left hand side will be an
      // OpaqueValueExpr, so get the underlying expression.
      if (const OpaqueValueExpr *OpaqueEx = dyn_cast<OpaqueValueExpr>(L))
        L = OpaqueEx->getSourceExpr();

      // If the last expression in the predecessor block matches true or false
      // subexpression, get its the value.
      if (ValEx == L->IgnoreParens() || ValEx == R->IgnoreParens()) {
        hasValue = true;
        V = SrcState->getSVal(ValEx, LCtx);
      }
      break;
    }
  }

  if (!hasValue)
    V = svalBuilder.conjureSymbolVal(nullptr, Ex, LCtx,
                                     currBldrCtx->blockCount());

  // Generate a new node with the binding from the appropriate path.
  B.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V, true));
}

void ExprEngine::
VisitOffsetOfExpr(const OffsetOfExpr *OOE,
                  ExplodedNode *Pred, ExplodedNodeSet &Dst) {
  StmtNodeBuilder B(Pred, Dst, *currBldrCtx);
  Expr::EvalResult Result;
  if (OOE->EvaluateAsInt(Result, getContext())) {
    APSInt IV = Result.Val.getInt();
    assert(IV.getBitWidth() == getContext().getTypeSize(OOE->getType()));
    assert(OOE->getType()->castAs<BuiltinType>()->isInteger());
    assert(IV.isSigned() == OOE->getType()->isSignedIntegerType());
    SVal X = svalBuilder.makeIntVal(IV);
    B.generateNode(OOE, Pred,
                   Pred->getState()->BindExpr(OOE, Pred->getLocationContext(),
                                              X));
  }
  // FIXME: Handle the case where __builtin_offsetof is not a constant.
}


void ExprEngine::
VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *Ex,
                              ExplodedNode *Pred,
                              ExplodedNodeSet &Dst) {
  // FIXME: Prechecks eventually go in ::Visit().
  ExplodedNodeSet CheckedSet;
  getCheckerManager().runCheckersForPreStmt(CheckedSet, Pred, Ex, *this);

  ExplodedNodeSet EvalSet;
  StmtNodeBuilder Bldr(CheckedSet, EvalSet, *currBldrCtx);

  QualType T = Ex->getTypeOfArgument();

  for (ExplodedNode *N : CheckedSet) {
    if (Ex->getKind() == UETT_SizeOf) {
      if (!T->isIncompleteType() && !T->isConstantSizeType()) {
        assert(T->isVariableArrayType() && "Unknown non-constant-sized type.");

        // FIXME: Add support for VLA type arguments and VLA expressions.
        // When that happens, we should probably refactor VLASizeChecker's code.
        continue;
      } else if (T->getAs<ObjCObjectType>()) {
        // Some code tries to take the sizeof an ObjCObjectType, relying that
        // the compiler has laid out its representation.  Just report Unknown
        // for these.
        continue;
      }
    }

    APSInt Value = Ex->EvaluateKnownConstInt(getContext());
    CharUnits amt = CharUnits::fromQuantity(Value.getZExtValue());

    ProgramStateRef state = N->getState();
    state = state->BindExpr(
        Ex, N->getLocationContext(),
        svalBuilder.makeIntVal(amt.getQuantity(), Ex->getType()));
    Bldr.generateNode(Ex, N, state);
  }

  getCheckerManager().runCheckersForPostStmt(Dst, EvalSet, Ex, *this);
}

void ExprEngine::handleUOExtension(ExplodedNode *N, const UnaryOperator *U,
                                   StmtNodeBuilder &Bldr) {
  // FIXME: We can probably just have some magic in Environment::getSVal()
  // that propagates values, instead of creating a new node here.
  //
  // Unary "+" is a no-op, similar to a parentheses.  We still have places
  // where it may be a block-level expression, so we need to
  // generate an extra node that just propagates the value of the
  // subexpression.
  const Expr *Ex = U->getSubExpr()->IgnoreParens();
  ProgramStateRef state = N->getState();
  const LocationContext *LCtx = N->getLocationContext();
  Bldr.generateNode(U, N, state->BindExpr(U, LCtx, state->getSVal(Ex, LCtx)));
}

void ExprEngine::VisitUnaryOperator(const UnaryOperator* U, ExplodedNode *Pred,
                                    ExplodedNodeSet &Dst) {
  // FIXME: Prechecks eventually go in ::Visit().
  ExplodedNodeSet CheckedSet;
  getCheckerManager().runCheckersForPreStmt(CheckedSet, Pred, U, *this);

  ExplodedNodeSet EvalSet;
  StmtNodeBuilder Bldr(CheckedSet, EvalSet, *currBldrCtx);

  for (ExplodedNode *N : CheckedSet) {
    switch (U->getOpcode()) {
    default: {
      Bldr.takeNodes(N);
      ExplodedNodeSet Tmp;
      VisitIncrementDecrementOperator(U, N, Tmp);
      Bldr.addNodes(Tmp);
      break;
    }
    case UO_Real: {
      const Expr *Ex = U->getSubExpr()->IgnoreParens();

      // FIXME: We don't have complex SValues yet.
      if (Ex->getType()->isAnyComplexType()) {
        // Just report "Unknown."
        break;
      }

      // For all other types, UO_Real is an identity operation.
      assert (U->getType() == Ex->getType());
      ProgramStateRef state = N->getState();
      const LocationContext *LCtx = N->getLocationContext();
      Bldr.generateNode(U, N,
                        state->BindExpr(U, LCtx, state->getSVal(Ex, LCtx)));
      break;
    }

    case UO_Imag: {
      const Expr *Ex = U->getSubExpr()->IgnoreParens();
      // FIXME: We don't have complex SValues yet.
      if (Ex->getType()->isAnyComplexType()) {
        // Just report "Unknown."
        break;
      }
      // For all other types, UO_Imag returns 0.
      ProgramStateRef state = N->getState();
      const LocationContext *LCtx = N->getLocationContext();
      SVal X = svalBuilder.makeZeroVal(Ex->getType());
      Bldr.generateNode(U, N, state->BindExpr(U, LCtx, X));
      break;
    }

    case UO_AddrOf: {
      // Process pointer-to-member address operation.
      const Expr *Ex = U->getSubExpr()->IgnoreParens();
      if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Ex)) {
        const ValueDecl *VD = DRE->getDecl();

        if (isa<CXXMethodDecl, FieldDecl, IndirectFieldDecl>(VD)) {
          ProgramStateRef State = N->getState();
          const LocationContext *LCtx = N->getLocationContext();
          SVal SV = svalBuilder.getMemberPointer(cast<NamedDecl>(VD));
          Bldr.generateNode(U, N, State->BindExpr(U, LCtx, SV));
          break;
        }
      }
      // Explicitly proceed with default handler for this case cascade.
      handleUOExtension(N, U, Bldr);
      break;
    }
    case UO_Plus:
      assert(!U->isGLValue());
      [[fallthrough]];
    case UO_Deref:
    case UO_Extension: {
      handleUOExtension(N, U, Bldr);
      break;
    }

    case UO_LNot:
    case UO_Minus:
    case UO_Not: {
      assert (!U->isGLValue());
      const Expr *Ex = U->getSubExpr()->IgnoreParens();
      ProgramStateRef state = N->getState();
      const LocationContext *LCtx = N->getLocationContext();

      // Get the value of the subexpression.
      SVal V = state->getSVal(Ex, LCtx);

      if (V.isUnknownOrUndef()) {
        Bldr.generateNode(U, N, state->BindExpr(U, LCtx, V));
        break;
      }

      switch (U->getOpcode()) {
        default:
          llvm_unreachable("Invalid Opcode.");
        case UO_Not:
          // FIXME: Do we need to handle promotions?
          state = state->BindExpr(
              U, LCtx, svalBuilder.evalComplement(V.castAs<NonLoc>()));
          break;
        case UO_Minus:
          // FIXME: Do we need to handle promotions?
          state = state->BindExpr(U, LCtx,
                                  svalBuilder.evalMinus(V.castAs<NonLoc>()));
          break;
        case UO_LNot:
          // C99 6.5.3.3: "The expression !E is equivalent to (0==E)."
          //
          //  Note: technically we do "E == 0", but this is the same in the
          //    transfer functions as "0 == E".
          SVal Result;
          if (std::optional<Loc> LV = V.getAs<Loc>()) {
          Loc X = svalBuilder.makeNullWithType(Ex->getType());
          Result = evalBinOp(state, BO_EQ, *LV, X, U->getType());
          } else if (Ex->getType()->isFloatingType()) {
          // FIXME: handle floating point types.
          Result = UnknownVal();
          } else {
          nonloc::ConcreteInt X(getBasicVals().getValue(0, Ex->getType()));
          Result = evalBinOp(state, BO_EQ, V.castAs<NonLoc>(), X, U->getType());
          }

          state = state->BindExpr(U, LCtx, Result);
          break;
      }
      Bldr.generateNode(U, N, state);
      break;
    }
    }
  }

  getCheckerManager().runCheckersForPostStmt(Dst, EvalSet, U, *this);
}

void ExprEngine::VisitIncrementDecrementOperator(const UnaryOperator* U,
                                                 ExplodedNode *Pred,
                                                 ExplodedNodeSet &Dst) {
  // Handle ++ and -- (both pre- and post-increment).
  assert (U->isIncrementDecrementOp());
  const Expr *Ex = U->getSubExpr()->IgnoreParens();

  const LocationContext *LCtx = Pred->getLocationContext();
  ProgramStateRef state = Pred->getState();
  SVal loc = state->getSVal(Ex, LCtx);

  // Perform a load.
  ExplodedNodeSet Tmp;
  evalLoad(Tmp, U, Ex, Pred, state, loc);

  ExplodedNodeSet Dst2;
  StmtNodeBuilder Bldr(Tmp, Dst2, *currBldrCtx);
  for (ExplodedNode *N : Tmp) {
    state = N->getState();
    assert(LCtx == N->getLocationContext());
    SVal V2_untested = state->getSVal(Ex, LCtx);

    // Propagate unknown and undefined values.
    if (V2_untested.isUnknownOrUndef()) {
      state = state->BindExpr(U, LCtx, V2_untested);

      // Perform the store, so that the uninitialized value detection happens.
      Bldr.takeNodes(N);
      ExplodedNodeSet Dst3;
      evalStore(Dst3, U, Ex, N, state, loc, V2_untested);
      Bldr.addNodes(Dst3);

      continue;
    }
    DefinedSVal V2 = V2_untested.castAs<DefinedSVal>();

    // Handle all other values.
    BinaryOperator::Opcode Op = U->isIncrementOp() ? BO_Add : BO_Sub;

    // If the UnaryOperator has non-location type, use its type to create the
    // constant value. If the UnaryOperator has location type, create the
    // constant with int type and pointer width.
    SVal RHS;
    SVal Result;

    if (U->getType()->isAnyPointerType())
      RHS = svalBuilder.makeArrayIndex(1);
    else if (U->getType()->isIntegralOrEnumerationType())
      RHS = svalBuilder.makeIntVal(1, U->getType());
    else
      RHS = UnknownVal();

    // The use of an operand of type bool with the ++ operators is deprecated
    // but valid until C++17. And if the operand of the ++ operator is of type
    // bool, it is set to true until C++17. Note that for '_Bool', it is also
    // set to true when it encounters ++ operator.
    if (U->getType()->isBooleanType() && U->isIncrementOp())
      Result = svalBuilder.makeTruthVal(true, U->getType());
    else
      Result = evalBinOp(state, Op, V2, RHS, U->getType());

    // Conjure a new symbol if necessary to recover precision.
    if (Result.isUnknown()){
      DefinedOrUnknownSVal SymVal =
        svalBuilder.conjureSymbolVal(nullptr, U, LCtx,
                                     currBldrCtx->blockCount());
      Result = SymVal;

      // If the value is a location, ++/-- should always preserve
      // non-nullness.  Check if the original value was non-null, and if so
      // propagate that constraint.
      if (Loc::isLocType(U->getType())) {
        DefinedOrUnknownSVal Constraint =
        svalBuilder.evalEQ(state, V2,svalBuilder.makeZeroVal(U->getType()));

        if (!state->assume(Constraint, true)) {
          // It isn't feasible for the original value to be null.
          // Propagate this constraint.
          Constraint = svalBuilder.evalEQ(state, SymVal,
                                       svalBuilder.makeZeroVal(U->getType()));

          state = state->assume(Constraint, false);
          assert(state);
        }
      }
    }

    // Since the lvalue-to-rvalue conversion is explicit in the AST,
    // we bind an l-value if the operator is prefix and an lvalue (in C++).
    if (U->isGLValue())
      state = state->BindExpr(U, LCtx, loc);
    else
      state = state->BindExpr(U, LCtx, U->isPostfix() ? V2 : Result);

    // Perform the store.
    Bldr.takeNodes(N);
    ExplodedNodeSet Dst3;
    evalStore(Dst3, U, Ex, N, state, loc, Result);
    Bldr.addNodes(Dst3);
  }
  Dst.insert(Dst2);
}
