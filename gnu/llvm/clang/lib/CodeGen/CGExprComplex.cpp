//===--- CGExprComplex.cpp - Emit LLVM Code for Complex Exprs -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Expr nodes with complex types as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CGOpenMPRuntime.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include <algorithm>
using namespace clang;
using namespace CodeGen;

//===----------------------------------------------------------------------===//
//                        Complex Expression Emitter
//===----------------------------------------------------------------------===//

namespace llvm {
extern cl::opt<bool> EnableSingleByteCoverage;
} // namespace llvm

typedef CodeGenFunction::ComplexPairTy ComplexPairTy;

/// Return the complex type that we are meant to emit.
static const ComplexType *getComplexType(QualType type) {
  type = type.getCanonicalType();
  if (const ComplexType *comp = dyn_cast<ComplexType>(type)) {
    return comp;
  } else {
    return cast<ComplexType>(cast<AtomicType>(type)->getValueType());
  }
}

namespace  {
class ComplexExprEmitter
  : public StmtVisitor<ComplexExprEmitter, ComplexPairTy> {
  CodeGenFunction &CGF;
  CGBuilderTy &Builder;
  bool IgnoreReal;
  bool IgnoreImag;
  bool FPHasBeenPromoted;

public:
  ComplexExprEmitter(CodeGenFunction &cgf, bool ir = false, bool ii = false)
      : CGF(cgf), Builder(CGF.Builder), IgnoreReal(ir), IgnoreImag(ii),
        FPHasBeenPromoted(false) {}

  //===--------------------------------------------------------------------===//
  //                               Utilities
  //===--------------------------------------------------------------------===//

  bool TestAndClearIgnoreReal() {
    bool I = IgnoreReal;
    IgnoreReal = false;
    return I;
  }
  bool TestAndClearIgnoreImag() {
    bool I = IgnoreImag;
    IgnoreImag = false;
    return I;
  }

  /// EmitLoadOfLValue - Given an expression with complex type that represents a
  /// value l-value, this method emits the address of the l-value, then loads
  /// and returns the result.
  ComplexPairTy EmitLoadOfLValue(const Expr *E) {
    return EmitLoadOfLValue(CGF.EmitLValue(E), E->getExprLoc());
  }

  ComplexPairTy EmitLoadOfLValue(LValue LV, SourceLocation Loc);

  /// EmitStoreOfComplex - Store the specified real/imag parts into the
  /// specified value pointer.
  void EmitStoreOfComplex(ComplexPairTy Val, LValue LV, bool isInit);

  /// Emit a cast from complex value Val to DestType.
  ComplexPairTy EmitComplexToComplexCast(ComplexPairTy Val, QualType SrcType,
                                         QualType DestType, SourceLocation Loc);
  /// Emit a cast from scalar value Val to DestType.
  ComplexPairTy EmitScalarToComplexCast(llvm::Value *Val, QualType SrcType,
                                        QualType DestType, SourceLocation Loc);

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  ComplexPairTy Visit(Expr *E) {
    ApplyDebugLocation DL(CGF, E);
    return StmtVisitor<ComplexExprEmitter, ComplexPairTy>::Visit(E);
  }

  ComplexPairTy VisitStmt(Stmt *S) {
    S->dump(llvm::errs(), CGF.getContext());
    llvm_unreachable("Stmt can't have complex result type!");
  }
  ComplexPairTy VisitExpr(Expr *S);
  ComplexPairTy VisitConstantExpr(ConstantExpr *E) {
    if (llvm::Constant *Result = ConstantEmitter(CGF).tryEmitConstantExpr(E))
      return ComplexPairTy(Result->getAggregateElement(0U),
                           Result->getAggregateElement(1U));
    return Visit(E->getSubExpr());
  }
  ComplexPairTy VisitParenExpr(ParenExpr *PE) { return Visit(PE->getSubExpr());}
  ComplexPairTy VisitGenericSelectionExpr(GenericSelectionExpr *GE) {
    return Visit(GE->getResultExpr());
  }
  ComplexPairTy VisitImaginaryLiteral(const ImaginaryLiteral *IL);
  ComplexPairTy
  VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *PE) {
    return Visit(PE->getReplacement());
  }
  ComplexPairTy VisitCoawaitExpr(CoawaitExpr *S) {
    return CGF.EmitCoawaitExpr(*S).getComplexVal();
  }
  ComplexPairTy VisitCoyieldExpr(CoyieldExpr *S) {
    return CGF.EmitCoyieldExpr(*S).getComplexVal();
  }
  ComplexPairTy VisitUnaryCoawait(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }

  ComplexPairTy emitConstant(const CodeGenFunction::ConstantEmission &Constant,
                             Expr *E) {
    assert(Constant && "not a constant");
    if (Constant.isReference())
      return EmitLoadOfLValue(Constant.getReferenceLValue(CGF, E),
                              E->getExprLoc());

    llvm::Constant *pair = Constant.getValue();
    return ComplexPairTy(pair->getAggregateElement(0U),
                         pair->getAggregateElement(1U));
  }

  // l-values.
  ComplexPairTy VisitDeclRefExpr(DeclRefExpr *E) {
    if (CodeGenFunction::ConstantEmission Constant = CGF.tryEmitAsConstant(E))
      return emitConstant(Constant, E);
    return EmitLoadOfLValue(E);
  }
  ComplexPairTy VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
    return EmitLoadOfLValue(E);
  }
  ComplexPairTy VisitObjCMessageExpr(ObjCMessageExpr *E) {
    return CGF.EmitObjCMessageExpr(E).getComplexVal();
  }
  ComplexPairTy VisitArraySubscriptExpr(Expr *E) { return EmitLoadOfLValue(E); }
  ComplexPairTy VisitMemberExpr(MemberExpr *ME) {
    if (CodeGenFunction::ConstantEmission Constant =
            CGF.tryEmitAsConstant(ME)) {
      CGF.EmitIgnoredExpr(ME->getBase());
      return emitConstant(Constant, ME);
    }
    return EmitLoadOfLValue(ME);
  }
  ComplexPairTy VisitOpaqueValueExpr(OpaqueValueExpr *E) {
    if (E->isGLValue())
      return EmitLoadOfLValue(CGF.getOrCreateOpaqueLValueMapping(E),
                              E->getExprLoc());
    return CGF.getOrCreateOpaqueRValueMapping(E).getComplexVal();
  }

  ComplexPairTy VisitPseudoObjectExpr(PseudoObjectExpr *E) {
    return CGF.EmitPseudoObjectRValue(E).getComplexVal();
  }

  // FIXME: CompoundLiteralExpr

  ComplexPairTy EmitCast(CastKind CK, Expr *Op, QualType DestTy);
  ComplexPairTy VisitImplicitCastExpr(ImplicitCastExpr *E) {
    // Unlike for scalars, we don't have to worry about function->ptr demotion
    // here.
    if (E->changesVolatileQualification())
      return EmitLoadOfLValue(E);
    return EmitCast(E->getCastKind(), E->getSubExpr(), E->getType());
  }
  ComplexPairTy VisitCastExpr(CastExpr *E) {
    if (const auto *ECE = dyn_cast<ExplicitCastExpr>(E))
      CGF.CGM.EmitExplicitCastExprType(ECE, &CGF);
    if (E->changesVolatileQualification())
       return EmitLoadOfLValue(E);
    return EmitCast(E->getCastKind(), E->getSubExpr(), E->getType());
  }
  ComplexPairTy VisitCallExpr(const CallExpr *E);
  ComplexPairTy VisitStmtExpr(const StmtExpr *E);

  // Operators.
  ComplexPairTy VisitPrePostIncDec(const UnaryOperator *E,
                                   bool isInc, bool isPre) {
    LValue LV = CGF.EmitLValue(E->getSubExpr());
    return CGF.EmitComplexPrePostIncDec(E, LV, isInc, isPre);
  }
  ComplexPairTy VisitUnaryPostDec(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, false, false);
  }
  ComplexPairTy VisitUnaryPostInc(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, true, false);
  }
  ComplexPairTy VisitUnaryPreDec(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, false, true);
  }
  ComplexPairTy VisitUnaryPreInc(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, true, true);
  }
  ComplexPairTy VisitUnaryDeref(const Expr *E) { return EmitLoadOfLValue(E); }

  ComplexPairTy VisitUnaryPlus(const UnaryOperator *E,
                               QualType PromotionType = QualType());
  ComplexPairTy VisitPlus(const UnaryOperator *E, QualType PromotionType);
  ComplexPairTy VisitUnaryMinus(const UnaryOperator *E,
                                QualType PromotionType = QualType());
  ComplexPairTy VisitMinus(const UnaryOperator *E, QualType PromotionType);
  ComplexPairTy VisitUnaryNot      (const UnaryOperator *E);
  // LNot,Real,Imag never return complex.
  ComplexPairTy VisitUnaryExtension(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }
  ComplexPairTy VisitCXXDefaultArgExpr(CXXDefaultArgExpr *DAE) {
    CodeGenFunction::CXXDefaultArgExprScope Scope(CGF, DAE);
    return Visit(DAE->getExpr());
  }
  ComplexPairTy VisitCXXDefaultInitExpr(CXXDefaultInitExpr *DIE) {
    CodeGenFunction::CXXDefaultInitExprScope Scope(CGF, DIE);
    return Visit(DIE->getExpr());
  }
  ComplexPairTy VisitExprWithCleanups(ExprWithCleanups *E) {
    CodeGenFunction::RunCleanupsScope Scope(CGF);
    ComplexPairTy Vals = Visit(E->getSubExpr());
    // Defend against dominance problems caused by jumps out of expression
    // evaluation through the shared cleanup block.
    Scope.ForceCleanup({&Vals.first, &Vals.second});
    return Vals;
  }
  ComplexPairTy VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E) {
    assert(E->getType()->isAnyComplexType() && "Expected complex type!");
    QualType Elem = E->getType()->castAs<ComplexType>()->getElementType();
    llvm::Constant *Null = llvm::Constant::getNullValue(CGF.ConvertType(Elem));
    return ComplexPairTy(Null, Null);
  }
  ComplexPairTy VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
    assert(E->getType()->isAnyComplexType() && "Expected complex type!");
    QualType Elem = E->getType()->castAs<ComplexType>()->getElementType();
    llvm::Constant *Null =
                       llvm::Constant::getNullValue(CGF.ConvertType(Elem));
    return ComplexPairTy(Null, Null);
  }

  struct BinOpInfo {
    ComplexPairTy LHS;
    ComplexPairTy RHS;
    QualType Ty;  // Computation Type.
    FPOptions FPFeatures;
  };

  BinOpInfo EmitBinOps(const BinaryOperator *E,
                       QualType PromotionTy = QualType());
  ComplexPairTy EmitPromoted(const Expr *E, QualType PromotionTy);
  ComplexPairTy EmitPromotedComplexOperand(const Expr *E, QualType PromotionTy);
  LValue EmitCompoundAssignLValue(const CompoundAssignOperator *E,
                                  ComplexPairTy (ComplexExprEmitter::*Func)
                                  (const BinOpInfo &),
                                  RValue &Val);
  ComplexPairTy EmitCompoundAssign(const CompoundAssignOperator *E,
                                   ComplexPairTy (ComplexExprEmitter::*Func)
                                   (const BinOpInfo &));

  ComplexPairTy EmitBinAdd(const BinOpInfo &Op);
  ComplexPairTy EmitBinSub(const BinOpInfo &Op);
  ComplexPairTy EmitBinMul(const BinOpInfo &Op);
  ComplexPairTy EmitBinDiv(const BinOpInfo &Op);
  ComplexPairTy EmitAlgebraicDiv(llvm::Value *A, llvm::Value *B, llvm::Value *C,
                                 llvm::Value *D);
  ComplexPairTy EmitRangeReductionDiv(llvm::Value *A, llvm::Value *B,
                                      llvm::Value *C, llvm::Value *D);

  ComplexPairTy EmitComplexBinOpLibCall(StringRef LibCallName,
                                        const BinOpInfo &Op);

  QualType GetHigherPrecisionFPType(QualType ElementType) {
    const auto *CurrentBT = cast<BuiltinType>(ElementType);
    switch (CurrentBT->getKind()) {
    case BuiltinType::Kind::Float16:
      return CGF.getContext().FloatTy;
    case BuiltinType::Kind::Float:
    case BuiltinType::Kind::BFloat16:
      return CGF.getContext().DoubleTy;
    case BuiltinType::Kind::Double:
      return CGF.getContext().LongDoubleTy;
    default:
      return ElementType;
    }
  }

  QualType HigherPrecisionTypeForComplexArithmetic(QualType ElementType,
                                                   bool IsDivOpCode) {
    QualType HigherElementType = GetHigherPrecisionFPType(ElementType);
    const llvm::fltSemantics &ElementTypeSemantics =
        CGF.getContext().getFloatTypeSemantics(ElementType);
    const llvm::fltSemantics &HigherElementTypeSemantics =
        CGF.getContext().getFloatTypeSemantics(HigherElementType);
    // Check that the promoted type can handle the intermediate values without
    // overflowing. This can be interpreted as:
    // (SmallerType.LargestFiniteVal * SmallerType.LargestFiniteVal) * 2 <=
    // LargerType.LargestFiniteVal.
    // In terms of exponent it gives this formula:
    // (SmallerType.LargestFiniteVal * SmallerType.LargestFiniteVal
    // doubles the exponent of SmallerType.LargestFiniteVal)
    if (llvm::APFloat::semanticsMaxExponent(ElementTypeSemantics) * 2 + 1 <=
        llvm::APFloat::semanticsMaxExponent(HigherElementTypeSemantics)) {
      FPHasBeenPromoted = true;
      return CGF.getContext().getComplexType(HigherElementType);
    } else {
      DiagnosticsEngine &Diags = CGF.CGM.getDiags();
      Diags.Report(diag::warn_next_larger_fp_type_same_size_than_fp);
      return QualType();
    }
  }

  QualType getPromotionType(FPOptionsOverride Features, QualType Ty,
                            bool IsDivOpCode = false) {
    if (auto *CT = Ty->getAs<ComplexType>()) {
      QualType ElementType = CT->getElementType();
      bool IsFloatingType = ElementType->isFloatingType();
      bool IsComplexRangePromoted = CGF.getLangOpts().getComplexRange() ==
                                    LangOptions::ComplexRangeKind::CX_Promoted;
      bool HasNoComplexRangeOverride = !Features.hasComplexRangeOverride();
      bool HasMatchingComplexRange = Features.hasComplexRangeOverride() &&
                                     Features.getComplexRangeOverride() ==
                                         CGF.getLangOpts().getComplexRange();

      if (IsDivOpCode && IsFloatingType && IsComplexRangePromoted &&
          (HasNoComplexRangeOverride || HasMatchingComplexRange))
        return HigherPrecisionTypeForComplexArithmetic(ElementType,
                                                       IsDivOpCode);
      if (ElementType.UseExcessPrecision(CGF.getContext()))
        return CGF.getContext().getComplexType(CGF.getContext().FloatTy);
    }
    if (Ty.UseExcessPrecision(CGF.getContext()))
      return CGF.getContext().FloatTy;
    return QualType();
  }

#define HANDLEBINOP(OP)                                                        \
  ComplexPairTy VisitBin##OP(const BinaryOperator *E) {                        \
    QualType promotionTy = getPromotionType(                                   \
        E->getStoredFPFeaturesOrDefault(), E->getType(),                       \
        (E->getOpcode() == BinaryOperatorKind::BO_Div) ? true : false);        \
    ComplexPairTy result = EmitBin##OP(EmitBinOps(E, promotionTy));            \
    if (!promotionTy.isNull())                                                 \
      result = CGF.EmitUnPromotedValue(result, E->getType());                  \
    return result;                                                             \
  }

  HANDLEBINOP(Mul)
  HANDLEBINOP(Div)
  HANDLEBINOP(Add)
  HANDLEBINOP(Sub)
#undef HANDLEBINOP

  ComplexPairTy VisitCXXRewrittenBinaryOperator(CXXRewrittenBinaryOperator *E) {
    return Visit(E->getSemanticForm());
  }

  // Compound assignments.
  ComplexPairTy VisitBinAddAssign(const CompoundAssignOperator *E) {
    return EmitCompoundAssign(E, &ComplexExprEmitter::EmitBinAdd);
  }
  ComplexPairTy VisitBinSubAssign(const CompoundAssignOperator *E) {
    return EmitCompoundAssign(E, &ComplexExprEmitter::EmitBinSub);
  }
  ComplexPairTy VisitBinMulAssign(const CompoundAssignOperator *E) {
    return EmitCompoundAssign(E, &ComplexExprEmitter::EmitBinMul);
  }
  ComplexPairTy VisitBinDivAssign(const CompoundAssignOperator *E) {
    return EmitCompoundAssign(E, &ComplexExprEmitter::EmitBinDiv);
  }

  // GCC rejects rem/and/or/xor for integer complex.
  // Logical and/or always return int, never complex.

  // No comparisons produce a complex result.

  LValue EmitBinAssignLValue(const BinaryOperator *E,
                             ComplexPairTy &Val);
  ComplexPairTy VisitBinAssign     (const BinaryOperator *E);
  ComplexPairTy VisitBinComma      (const BinaryOperator *E);


  ComplexPairTy
  VisitAbstractConditionalOperator(const AbstractConditionalOperator *CO);
  ComplexPairTy VisitChooseExpr(ChooseExpr *CE);

  ComplexPairTy VisitInitListExpr(InitListExpr *E);

  ComplexPairTy VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
    return EmitLoadOfLValue(E);
  }

  ComplexPairTy VisitVAArgExpr(VAArgExpr *E);

  ComplexPairTy VisitAtomicExpr(AtomicExpr *E) {
    return CGF.EmitAtomicExpr(E).getComplexVal();
  }

  ComplexPairTy VisitPackIndexingExpr(PackIndexingExpr *E) {
    return Visit(E->getSelectedExpr());
  }
};
}  // end anonymous namespace.

//===----------------------------------------------------------------------===//
//                                Utilities
//===----------------------------------------------------------------------===//

Address CodeGenFunction::emitAddrOfRealComponent(Address addr,
                                                 QualType complexType) {
  return Builder.CreateStructGEP(addr, 0, addr.getName() + ".realp");
}

Address CodeGenFunction::emitAddrOfImagComponent(Address addr,
                                                 QualType complexType) {
  return Builder.CreateStructGEP(addr, 1, addr.getName() + ".imagp");
}

/// EmitLoadOfLValue - Given an RValue reference for a complex, emit code to
/// load the real and imaginary pieces, returning them as Real/Imag.
ComplexPairTy ComplexExprEmitter::EmitLoadOfLValue(LValue lvalue,
                                                   SourceLocation loc) {
  assert(lvalue.isSimple() && "non-simple complex l-value?");
  if (lvalue.getType()->isAtomicType())
    return CGF.EmitAtomicLoad(lvalue, loc).getComplexVal();

  Address SrcPtr = lvalue.getAddress();
  bool isVolatile = lvalue.isVolatileQualified();

  llvm::Value *Real = nullptr, *Imag = nullptr;

  if (!IgnoreReal || isVolatile) {
    Address RealP = CGF.emitAddrOfRealComponent(SrcPtr, lvalue.getType());
    Real = Builder.CreateLoad(RealP, isVolatile, SrcPtr.getName() + ".real");
  }

  if (!IgnoreImag || isVolatile) {
    Address ImagP = CGF.emitAddrOfImagComponent(SrcPtr, lvalue.getType());
    Imag = Builder.CreateLoad(ImagP, isVolatile, SrcPtr.getName() + ".imag");
  }

  return ComplexPairTy(Real, Imag);
}

/// EmitStoreOfComplex - Store the specified real/imag parts into the
/// specified value pointer.
void ComplexExprEmitter::EmitStoreOfComplex(ComplexPairTy Val, LValue lvalue,
                                            bool isInit) {
  if (lvalue.getType()->isAtomicType() ||
      (!isInit && CGF.LValueIsSuitableForInlineAtomic(lvalue)))
    return CGF.EmitAtomicStore(RValue::getComplex(Val), lvalue, isInit);

  Address Ptr = lvalue.getAddress();
  Address RealPtr = CGF.emitAddrOfRealComponent(Ptr, lvalue.getType());
  Address ImagPtr = CGF.emitAddrOfImagComponent(Ptr, lvalue.getType());

  Builder.CreateStore(Val.first, RealPtr, lvalue.isVolatileQualified());
  Builder.CreateStore(Val.second, ImagPtr, lvalue.isVolatileQualified());
}



//===----------------------------------------------------------------------===//
//                            Visitor Methods
//===----------------------------------------------------------------------===//

ComplexPairTy ComplexExprEmitter::VisitExpr(Expr *E) {
  CGF.ErrorUnsupported(E, "complex expression");
  llvm::Type *EltTy =
    CGF.ConvertType(getComplexType(E->getType())->getElementType());
  llvm::Value *U = llvm::UndefValue::get(EltTy);
  return ComplexPairTy(U, U);
}

ComplexPairTy ComplexExprEmitter::
VisitImaginaryLiteral(const ImaginaryLiteral *IL) {
  llvm::Value *Imag = CGF.EmitScalarExpr(IL->getSubExpr());
  return ComplexPairTy(llvm::Constant::getNullValue(Imag->getType()), Imag);
}


ComplexPairTy ComplexExprEmitter::VisitCallExpr(const CallExpr *E) {
  if (E->getCallReturnType(CGF.getContext())->isReferenceType())
    return EmitLoadOfLValue(E);

  return CGF.EmitCallExpr(E).getComplexVal();
}

ComplexPairTy ComplexExprEmitter::VisitStmtExpr(const StmtExpr *E) {
  CodeGenFunction::StmtExprEvaluation eval(CGF);
  Address RetAlloca = CGF.EmitCompoundStmt(*E->getSubStmt(), true);
  assert(RetAlloca.isValid() && "Expected complex return value");
  return EmitLoadOfLValue(CGF.MakeAddrLValue(RetAlloca, E->getType()),
                          E->getExprLoc());
}

/// Emit a cast from complex value Val to DestType.
ComplexPairTy ComplexExprEmitter::EmitComplexToComplexCast(ComplexPairTy Val,
                                                           QualType SrcType,
                                                           QualType DestType,
                                                           SourceLocation Loc) {
  // Get the src/dest element type.
  SrcType = SrcType->castAs<ComplexType>()->getElementType();
  DestType = DestType->castAs<ComplexType>()->getElementType();

  // C99 6.3.1.6: When a value of complex type is converted to another
  // complex type, both the real and imaginary parts follow the conversion
  // rules for the corresponding real types.
  if (Val.first)
    Val.first = CGF.EmitScalarConversion(Val.first, SrcType, DestType, Loc);
  if (Val.second)
    Val.second = CGF.EmitScalarConversion(Val.second, SrcType, DestType, Loc);
  return Val;
}

ComplexPairTy ComplexExprEmitter::EmitScalarToComplexCast(llvm::Value *Val,
                                                          QualType SrcType,
                                                          QualType DestType,
                                                          SourceLocation Loc) {
  // Convert the input element to the element type of the complex.
  DestType = DestType->castAs<ComplexType>()->getElementType();
  Val = CGF.EmitScalarConversion(Val, SrcType, DestType, Loc);

  // Return (realval, 0).
  return ComplexPairTy(Val, llvm::Constant::getNullValue(Val->getType()));
}

ComplexPairTy ComplexExprEmitter::EmitCast(CastKind CK, Expr *Op,
                                           QualType DestTy) {
  switch (CK) {
  case CK_Dependent: llvm_unreachable("dependent cast kind in IR gen!");

  // Atomic to non-atomic casts may be more than a no-op for some platforms and
  // for some types.
  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
  case CK_NoOp:
  case CK_LValueToRValue:
  case CK_UserDefinedConversion:
    return Visit(Op);

  case CK_LValueBitCast: {
    LValue origLV = CGF.EmitLValue(Op);
    Address V = origLV.getAddress().withElementType(CGF.ConvertType(DestTy));
    return EmitLoadOfLValue(CGF.MakeAddrLValue(V, DestTy), Op->getExprLoc());
  }

  case CK_LValueToRValueBitCast: {
    LValue SourceLVal = CGF.EmitLValue(Op);
    Address Addr =
        SourceLVal.getAddress().withElementType(CGF.ConvertTypeForMem(DestTy));
    LValue DestLV = CGF.MakeAddrLValue(Addr, DestTy);
    DestLV.setTBAAInfo(TBAAAccessInfo::getMayAliasInfo());
    return EmitLoadOfLValue(DestLV, Op->getExprLoc());
  }

  case CK_BitCast:
  case CK_BaseToDerived:
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_Dynamic:
  case CK_ToUnion:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_NullToMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer:
  case CK_MemberPointerToBoolean:
  case CK_ReinterpretMemberPointer:
  case CK_ConstructorConversion:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ObjCObjectLValueCast:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_CopyAndAutoreleaseBlockObject:
  case CK_BuiltinFnToFnPtr:
  case CK_ZeroToOCLOpaqueType:
  case CK_AddressSpaceConversion:
  case CK_IntToOCLSampler:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
  case CK_HLSLVectorTruncation:
  case CK_HLSLArrayRValue:
    llvm_unreachable("invalid cast kind for complex value");

  case CK_FloatingRealToComplex:
  case CK_IntegralRealToComplex: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Op);
    return EmitScalarToComplexCast(CGF.EmitScalarExpr(Op), Op->getType(),
                                   DestTy, Op->getExprLoc());
  }

  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Op);
    return EmitComplexToComplexCast(Visit(Op), Op->getType(), DestTy,
                                    Op->getExprLoc());
  }
  }

  llvm_unreachable("unknown cast resulting in complex value");
}

ComplexPairTy ComplexExprEmitter::VisitUnaryPlus(const UnaryOperator *E,
                                                 QualType PromotionType) {
  E->hasStoredFPFeatures();
  QualType promotionTy =
      PromotionType.isNull()
          ? getPromotionType(E->getStoredFPFeaturesOrDefault(),
                             E->getSubExpr()->getType())
          : PromotionType;
  ComplexPairTy result = VisitPlus(E, promotionTy);
  if (!promotionTy.isNull())
    return CGF.EmitUnPromotedValue(result, E->getSubExpr()->getType());
  return result;
}

ComplexPairTy ComplexExprEmitter::VisitPlus(const UnaryOperator *E,
                                            QualType PromotionType) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  if (!PromotionType.isNull())
    return CGF.EmitPromotedComplexExpr(E->getSubExpr(), PromotionType);
  return Visit(E->getSubExpr());
}

ComplexPairTy ComplexExprEmitter::VisitUnaryMinus(const UnaryOperator *E,
                                                  QualType PromotionType) {
  QualType promotionTy =
      PromotionType.isNull()
          ? getPromotionType(E->getStoredFPFeaturesOrDefault(),
                             E->getSubExpr()->getType())
          : PromotionType;
  ComplexPairTy result = VisitMinus(E, promotionTy);
  if (!promotionTy.isNull())
    return CGF.EmitUnPromotedValue(result, E->getSubExpr()->getType());
  return result;
}
ComplexPairTy ComplexExprEmitter::VisitMinus(const UnaryOperator *E,
                                             QualType PromotionType) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  ComplexPairTy Op;
  if (!PromotionType.isNull())
    Op = CGF.EmitPromotedComplexExpr(E->getSubExpr(), PromotionType);
  else
    Op = Visit(E->getSubExpr());

  llvm::Value *ResR, *ResI;
  if (Op.first->getType()->isFloatingPointTy()) {
    ResR = Builder.CreateFNeg(Op.first,  "neg.r");
    ResI = Builder.CreateFNeg(Op.second, "neg.i");
  } else {
    ResR = Builder.CreateNeg(Op.first,  "neg.r");
    ResI = Builder.CreateNeg(Op.second, "neg.i");
  }
  return ComplexPairTy(ResR, ResI);
}

ComplexPairTy ComplexExprEmitter::VisitUnaryNot(const UnaryOperator *E) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  // ~(a+ib) = a + i*-b
  ComplexPairTy Op = Visit(E->getSubExpr());
  llvm::Value *ResI;
  if (Op.second->getType()->isFloatingPointTy())
    ResI = Builder.CreateFNeg(Op.second, "conj.i");
  else
    ResI = Builder.CreateNeg(Op.second, "conj.i");

  return ComplexPairTy(Op.first, ResI);
}

ComplexPairTy ComplexExprEmitter::EmitBinAdd(const BinOpInfo &Op) {
  llvm::Value *ResR, *ResI;

  if (Op.LHS.first->getType()->isFloatingPointTy()) {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Op.FPFeatures);
    ResR = Builder.CreateFAdd(Op.LHS.first,  Op.RHS.first,  "add.r");
    if (Op.LHS.second && Op.RHS.second)
      ResI = Builder.CreateFAdd(Op.LHS.second, Op.RHS.second, "add.i");
    else
      ResI = Op.LHS.second ? Op.LHS.second : Op.RHS.second;
    assert(ResI && "Only one operand may be real!");
  } else {
    ResR = Builder.CreateAdd(Op.LHS.first,  Op.RHS.first,  "add.r");
    assert(Op.LHS.second && Op.RHS.second &&
           "Both operands of integer complex operators must be complex!");
    ResI = Builder.CreateAdd(Op.LHS.second, Op.RHS.second, "add.i");
  }
  return ComplexPairTy(ResR, ResI);
}

ComplexPairTy ComplexExprEmitter::EmitBinSub(const BinOpInfo &Op) {
  llvm::Value *ResR, *ResI;
  if (Op.LHS.first->getType()->isFloatingPointTy()) {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Op.FPFeatures);
    ResR = Builder.CreateFSub(Op.LHS.first, Op.RHS.first, "sub.r");
    if (Op.LHS.second && Op.RHS.second)
      ResI = Builder.CreateFSub(Op.LHS.second, Op.RHS.second, "sub.i");
    else
      ResI = Op.LHS.second ? Op.LHS.second
                           : Builder.CreateFNeg(Op.RHS.second, "sub.i");
    assert(ResI && "Only one operand may be real!");
  } else {
    ResR = Builder.CreateSub(Op.LHS.first, Op.RHS.first, "sub.r");
    assert(Op.LHS.second && Op.RHS.second &&
           "Both operands of integer complex operators must be complex!");
    ResI = Builder.CreateSub(Op.LHS.second, Op.RHS.second, "sub.i");
  }
  return ComplexPairTy(ResR, ResI);
}

/// Emit a libcall for a binary operation on complex types.
ComplexPairTy ComplexExprEmitter::EmitComplexBinOpLibCall(StringRef LibCallName,
                                                          const BinOpInfo &Op) {
  CallArgList Args;
  Args.add(RValue::get(Op.LHS.first),
           Op.Ty->castAs<ComplexType>()->getElementType());
  Args.add(RValue::get(Op.LHS.second),
           Op.Ty->castAs<ComplexType>()->getElementType());
  Args.add(RValue::get(Op.RHS.first),
           Op.Ty->castAs<ComplexType>()->getElementType());
  Args.add(RValue::get(Op.RHS.second),
           Op.Ty->castAs<ComplexType>()->getElementType());

  // We *must* use the full CG function call building logic here because the
  // complex type has special ABI handling. We also should not forget about
  // special calling convention which may be used for compiler builtins.

  // We create a function qualified type to state that this call does not have
  // any exceptions.
  FunctionProtoType::ExtProtoInfo EPI;
  EPI = EPI.withExceptionSpec(
      FunctionProtoType::ExceptionSpecInfo(EST_BasicNoexcept));
  SmallVector<QualType, 4> ArgsQTys(
      4, Op.Ty->castAs<ComplexType>()->getElementType());
  QualType FQTy = CGF.getContext().getFunctionType(Op.Ty, ArgsQTys, EPI);
  const CGFunctionInfo &FuncInfo = CGF.CGM.getTypes().arrangeFreeFunctionCall(
      Args, cast<FunctionType>(FQTy.getTypePtr()), false);

  llvm::FunctionType *FTy = CGF.CGM.getTypes().GetFunctionType(FuncInfo);
  llvm::FunctionCallee Func = CGF.CGM.CreateRuntimeFunction(
      FTy, LibCallName, llvm::AttributeList(), true);
  CGCallee Callee = CGCallee::forDirect(Func, FQTy->getAs<FunctionProtoType>());

  llvm::CallBase *Call;
  RValue Res = CGF.EmitCall(FuncInfo, Callee, ReturnValueSlot(), Args, &Call);
  Call->setCallingConv(CGF.CGM.getRuntimeCC());
  return Res.getComplexVal();
}

/// Lookup the libcall name for a given floating point type complex
/// multiply.
static StringRef getComplexMultiplyLibCallName(llvm::Type *Ty) {
  switch (Ty->getTypeID()) {
  default:
    llvm_unreachable("Unsupported floating point type!");
  case llvm::Type::HalfTyID:
    return "__mulhc3";
  case llvm::Type::FloatTyID:
    return "__mulsc3";
  case llvm::Type::DoubleTyID:
    return "__muldc3";
  case llvm::Type::PPC_FP128TyID:
    return "__multc3";
  case llvm::Type::X86_FP80TyID:
    return "__mulxc3";
  case llvm::Type::FP128TyID:
    return "__multc3";
  }
}

// See C11 Annex G.5.1 for the semantics of multiplicative operators on complex
// typed values.
ComplexPairTy ComplexExprEmitter::EmitBinMul(const BinOpInfo &Op) {
  using llvm::Value;
  Value *ResR, *ResI;
  llvm::MDBuilder MDHelper(CGF.getLLVMContext());

  if (Op.LHS.first->getType()->isFloatingPointTy()) {
    // The general formulation is:
    // (a + ib) * (c + id) = (a * c - b * d) + i(a * d + b * c)
    //
    // But we can fold away components which would be zero due to a real
    // operand according to C11 Annex G.5.1p2.

    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Op.FPFeatures);
    if (Op.LHS.second && Op.RHS.second) {
      // If both operands are complex, emit the core math directly, and then
      // test for NaNs. If we find NaNs in the result, we delegate to a libcall
      // to carefully re-compute the correct infinity representation if
      // possible. The expectation is that the presence of NaNs here is
      // *extremely* rare, and so the cost of the libcall is almost irrelevant.
      // This is good, because the libcall re-computes the core multiplication
      // exactly the same as we do here and re-tests for NaNs in order to be
      // a generic complex*complex libcall.

      // First compute the four products.
      Value *AC = Builder.CreateFMul(Op.LHS.first, Op.RHS.first, "mul_ac");
      Value *BD = Builder.CreateFMul(Op.LHS.second, Op.RHS.second, "mul_bd");
      Value *AD = Builder.CreateFMul(Op.LHS.first, Op.RHS.second, "mul_ad");
      Value *BC = Builder.CreateFMul(Op.LHS.second, Op.RHS.first, "mul_bc");

      // The real part is the difference of the first two, the imaginary part is
      // the sum of the second.
      ResR = Builder.CreateFSub(AC, BD, "mul_r");
      ResI = Builder.CreateFAdd(AD, BC, "mul_i");

      if (Op.FPFeatures.getComplexRange() == LangOptions::CX_Basic ||
          Op.FPFeatures.getComplexRange() == LangOptions::CX_Improved ||
          Op.FPFeatures.getComplexRange() == LangOptions::CX_Promoted)
        return ComplexPairTy(ResR, ResI);

      // Emit the test for the real part becoming NaN and create a branch to
      // handle it. We test for NaN by comparing the number to itself.
      Value *IsRNaN = Builder.CreateFCmpUNO(ResR, ResR, "isnan_cmp");
      llvm::BasicBlock *ContBB = CGF.createBasicBlock("complex_mul_cont");
      llvm::BasicBlock *INaNBB = CGF.createBasicBlock("complex_mul_imag_nan");
      llvm::Instruction *Branch = Builder.CreateCondBr(IsRNaN, INaNBB, ContBB);
      llvm::BasicBlock *OrigBB = Branch->getParent();

      // Give hint that we very much don't expect to see NaNs.
      llvm::MDNode *BrWeight = MDHelper.createUnlikelyBranchWeights();
      Branch->setMetadata(llvm::LLVMContext::MD_prof, BrWeight);

      // Now test the imaginary part and create its branch.
      CGF.EmitBlock(INaNBB);
      Value *IsINaN = Builder.CreateFCmpUNO(ResI, ResI, "isnan_cmp");
      llvm::BasicBlock *LibCallBB = CGF.createBasicBlock("complex_mul_libcall");
      Branch = Builder.CreateCondBr(IsINaN, LibCallBB, ContBB);
      Branch->setMetadata(llvm::LLVMContext::MD_prof, BrWeight);

      // Now emit the libcall on this slowest of the slow paths.
      CGF.EmitBlock(LibCallBB);
      Value *LibCallR, *LibCallI;
      std::tie(LibCallR, LibCallI) = EmitComplexBinOpLibCall(
          getComplexMultiplyLibCallName(Op.LHS.first->getType()), Op);
      Builder.CreateBr(ContBB);

      // Finally continue execution by phi-ing together the different
      // computation paths.
      CGF.EmitBlock(ContBB);
      llvm::PHINode *RealPHI = Builder.CreatePHI(ResR->getType(), 3, "real_mul_phi");
      RealPHI->addIncoming(ResR, OrigBB);
      RealPHI->addIncoming(ResR, INaNBB);
      RealPHI->addIncoming(LibCallR, LibCallBB);
      llvm::PHINode *ImagPHI = Builder.CreatePHI(ResI->getType(), 3, "imag_mul_phi");
      ImagPHI->addIncoming(ResI, OrigBB);
      ImagPHI->addIncoming(ResI, INaNBB);
      ImagPHI->addIncoming(LibCallI, LibCallBB);
      return ComplexPairTy(RealPHI, ImagPHI);
    }
    assert((Op.LHS.second || Op.RHS.second) &&
           "At least one operand must be complex!");

    // If either of the operands is a real rather than a complex, the
    // imaginary component is ignored when computing the real component of the
    // result.
    ResR = Builder.CreateFMul(Op.LHS.first, Op.RHS.first, "mul.rl");

    ResI = Op.LHS.second
               ? Builder.CreateFMul(Op.LHS.second, Op.RHS.first, "mul.il")
               : Builder.CreateFMul(Op.LHS.first, Op.RHS.second, "mul.ir");
  } else {
    assert(Op.LHS.second && Op.RHS.second &&
           "Both operands of integer complex operators must be complex!");
    Value *ResRl = Builder.CreateMul(Op.LHS.first, Op.RHS.first, "mul.rl");
    Value *ResRr = Builder.CreateMul(Op.LHS.second, Op.RHS.second, "mul.rr");
    ResR = Builder.CreateSub(ResRl, ResRr, "mul.r");

    Value *ResIl = Builder.CreateMul(Op.LHS.second, Op.RHS.first, "mul.il");
    Value *ResIr = Builder.CreateMul(Op.LHS.first, Op.RHS.second, "mul.ir");
    ResI = Builder.CreateAdd(ResIl, ResIr, "mul.i");
  }
  return ComplexPairTy(ResR, ResI);
}

ComplexPairTy ComplexExprEmitter::EmitAlgebraicDiv(llvm::Value *LHSr,
                                                   llvm::Value *LHSi,
                                                   llvm::Value *RHSr,
                                                   llvm::Value *RHSi) {
  // (a+ib) / (c+id) = ((ac+bd)/(cc+dd)) + i((bc-ad)/(cc+dd))
  llvm::Value *DSTr, *DSTi;

  llvm::Value *AC = Builder.CreateFMul(LHSr, RHSr); // a*c
  llvm::Value *BD = Builder.CreateFMul(LHSi, RHSi); // b*d
  llvm::Value *ACpBD = Builder.CreateFAdd(AC, BD);  // ac+bd

  llvm::Value *CC = Builder.CreateFMul(RHSr, RHSr); // c*c
  llvm::Value *DD = Builder.CreateFMul(RHSi, RHSi); // d*d
  llvm::Value *CCpDD = Builder.CreateFAdd(CC, DD);  // cc+dd

  llvm::Value *BC = Builder.CreateFMul(LHSi, RHSr); // b*c
  llvm::Value *AD = Builder.CreateFMul(LHSr, RHSi); // a*d
  llvm::Value *BCmAD = Builder.CreateFSub(BC, AD);  // bc-ad

  DSTr = Builder.CreateFDiv(ACpBD, CCpDD);
  DSTi = Builder.CreateFDiv(BCmAD, CCpDD);
  return ComplexPairTy(DSTr, DSTi);
}

// EmitFAbs - Emit a call to @llvm.fabs.
static llvm::Value *EmitllvmFAbs(CodeGenFunction &CGF, llvm::Value *Value) {
  llvm::Function *Func =
      CGF.CGM.getIntrinsic(llvm::Intrinsic::fabs, Value->getType());
  llvm::Value *Call = CGF.Builder.CreateCall(Func, Value);
  return Call;
}

// EmitRangeReductionDiv - Implements Smith's algorithm for complex division.
// SMITH, R. L. Algorithm 116: Complex division. Commun. ACM 5, 8 (1962).
ComplexPairTy ComplexExprEmitter::EmitRangeReductionDiv(llvm::Value *LHSr,
                                                        llvm::Value *LHSi,
                                                        llvm::Value *RHSr,
                                                        llvm::Value *RHSi) {
  // FIXME: This could eventually be replaced by an LLVM intrinsic to
  // avoid this long IR sequence.

  // (a + ib) / (c + id) = (e + if)
  llvm::Value *FAbsRHSr = EmitllvmFAbs(CGF, RHSr); // |c|
  llvm::Value *FAbsRHSi = EmitllvmFAbs(CGF, RHSi); // |d|
  // |c| >= |d|
  llvm::Value *IsR = Builder.CreateFCmpUGT(FAbsRHSr, FAbsRHSi, "abs_cmp");

  llvm::BasicBlock *TrueBB =
      CGF.createBasicBlock("abs_rhsr_greater_or_equal_abs_rhsi");
  llvm::BasicBlock *FalseBB =
      CGF.createBasicBlock("abs_rhsr_less_than_abs_rhsi");
  llvm::BasicBlock *ContBB = CGF.createBasicBlock("complex_div");
  Builder.CreateCondBr(IsR, TrueBB, FalseBB);

  CGF.EmitBlock(TrueBB);
  // abs(c) >= abs(d)
  // r = d/c
  // tmp = c + rd
  // e = (a + br)/tmp
  // f = (b - ar)/tmp
  llvm::Value *DdC = Builder.CreateFDiv(RHSi, RHSr); // r=d/c

  llvm::Value *RD = Builder.CreateFMul(DdC, RHSi);  // rd
  llvm::Value *CpRD = Builder.CreateFAdd(RHSr, RD); // tmp=c+rd

  llvm::Value *T3 = Builder.CreateFMul(LHSi, DdC);   // br
  llvm::Value *T4 = Builder.CreateFAdd(LHSr, T3);    // a+br
  llvm::Value *DSTTr = Builder.CreateFDiv(T4, CpRD); // (a+br)/tmp

  llvm::Value *T5 = Builder.CreateFMul(LHSr, DdC);   // ar
  llvm::Value *T6 = Builder.CreateFSub(LHSi, T5);    // b-ar
  llvm::Value *DSTTi = Builder.CreateFDiv(T6, CpRD); // (b-ar)/tmp
  Builder.CreateBr(ContBB);

  CGF.EmitBlock(FalseBB);
  // abs(c) < abs(d)
  // r = c/d
  // tmp = d + rc
  // e = (ar + b)/tmp
  // f = (br - a)/tmp
  llvm::Value *CdD = Builder.CreateFDiv(RHSr, RHSi); // r=c/d

  llvm::Value *RC = Builder.CreateFMul(CdD, RHSr);  // rc
  llvm::Value *DpRC = Builder.CreateFAdd(RHSi, RC); // tmp=d+rc

  llvm::Value *T7 = Builder.CreateFMul(LHSr, CdD);   // ar
  llvm::Value *T8 = Builder.CreateFAdd(T7, LHSi);    // ar+b
  llvm::Value *DSTFr = Builder.CreateFDiv(T8, DpRC); // (ar+b)/tmp

  llvm::Value *T9 = Builder.CreateFMul(LHSi, CdD);    // br
  llvm::Value *T10 = Builder.CreateFSub(T9, LHSr);    // br-a
  llvm::Value *DSTFi = Builder.CreateFDiv(T10, DpRC); // (br-a)/tmp
  Builder.CreateBr(ContBB);

  // Phi together the computation paths.
  CGF.EmitBlock(ContBB);
  llvm::PHINode *VALr = Builder.CreatePHI(DSTTr->getType(), 2);
  VALr->addIncoming(DSTTr, TrueBB);
  VALr->addIncoming(DSTFr, FalseBB);
  llvm::PHINode *VALi = Builder.CreatePHI(DSTTi->getType(), 2);
  VALi->addIncoming(DSTTi, TrueBB);
  VALi->addIncoming(DSTFi, FalseBB);
  return ComplexPairTy(VALr, VALi);
}

// See C11 Annex G.5.1 for the semantics of multiplicative operators on complex
// typed values.
ComplexPairTy ComplexExprEmitter::EmitBinDiv(const BinOpInfo &Op) {
  llvm::Value *LHSr = Op.LHS.first, *LHSi = Op.LHS.second;
  llvm::Value *RHSr = Op.RHS.first, *RHSi = Op.RHS.second;
  llvm::Value *DSTr, *DSTi;
  if (LHSr->getType()->isFloatingPointTy()) {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Op.FPFeatures);
    if (!RHSi) {
      assert(LHSi && "Can have at most one non-complex operand!");

      DSTr = Builder.CreateFDiv(LHSr, RHSr);
      DSTi = Builder.CreateFDiv(LHSi, RHSr);
      return ComplexPairTy(DSTr, DSTi);
    }
    llvm::Value *OrigLHSi = LHSi;
    if (!LHSi)
      LHSi = llvm::Constant::getNullValue(RHSi->getType());
    if (Op.FPFeatures.getComplexRange() == LangOptions::CX_Improved ||
        (Op.FPFeatures.getComplexRange() == LangOptions::CX_Promoted &&
         !FPHasBeenPromoted))
      return EmitRangeReductionDiv(LHSr, LHSi, RHSr, RHSi);
    else if (Op.FPFeatures.getComplexRange() == LangOptions::CX_Basic ||
             Op.FPFeatures.getComplexRange() == LangOptions::CX_Promoted)
      return EmitAlgebraicDiv(LHSr, LHSi, RHSr, RHSi);
    // '-ffast-math' is used in the command line but followed by an
    // '-fno-cx-limited-range' or '-fcomplex-arithmetic=full'.
    else if (Op.FPFeatures.getComplexRange() == LangOptions::CX_Full) {
      LHSi = OrigLHSi;
      // If we have a complex operand on the RHS and FastMath is not allowed, we
      // delegate to a libcall to handle all of the complexities and minimize
      // underflow/overflow cases. When FastMath is allowed we construct the
      // divide inline using the same algorithm as for integer operands.
      BinOpInfo LibCallOp = Op;
      // If LHS was a real, supply a null imaginary part.
      if (!LHSi)
        LibCallOp.LHS.second = llvm::Constant::getNullValue(LHSr->getType());

      switch (LHSr->getType()->getTypeID()) {
      default:
        llvm_unreachable("Unsupported floating point type!");
      case llvm::Type::HalfTyID:
        return EmitComplexBinOpLibCall("__divhc3", LibCallOp);
      case llvm::Type::FloatTyID:
        return EmitComplexBinOpLibCall("__divsc3", LibCallOp);
      case llvm::Type::DoubleTyID:
        return EmitComplexBinOpLibCall("__divdc3", LibCallOp);
      case llvm::Type::PPC_FP128TyID:
        return EmitComplexBinOpLibCall("__divtc3", LibCallOp);
      case llvm::Type::X86_FP80TyID:
        return EmitComplexBinOpLibCall("__divxc3", LibCallOp);
      case llvm::Type::FP128TyID:
        return EmitComplexBinOpLibCall("__divtc3", LibCallOp);
      }
    } else {
      return EmitAlgebraicDiv(LHSr, LHSi, RHSr, RHSi);
    }
  } else {
    assert(Op.LHS.second && Op.RHS.second &&
           "Both operands of integer complex operators must be complex!");
    // (a+ib) / (c+id) = ((ac+bd)/(cc+dd)) + i((bc-ad)/(cc+dd))
    llvm::Value *Tmp1 = Builder.CreateMul(LHSr, RHSr); // a*c
    llvm::Value *Tmp2 = Builder.CreateMul(LHSi, RHSi); // b*d
    llvm::Value *Tmp3 = Builder.CreateAdd(Tmp1, Tmp2); // ac+bd

    llvm::Value *Tmp4 = Builder.CreateMul(RHSr, RHSr); // c*c
    llvm::Value *Tmp5 = Builder.CreateMul(RHSi, RHSi); // d*d
    llvm::Value *Tmp6 = Builder.CreateAdd(Tmp4, Tmp5); // cc+dd

    llvm::Value *Tmp7 = Builder.CreateMul(LHSi, RHSr); // b*c
    llvm::Value *Tmp8 = Builder.CreateMul(LHSr, RHSi); // a*d
    llvm::Value *Tmp9 = Builder.CreateSub(Tmp7, Tmp8); // bc-ad

    if (Op.Ty->castAs<ComplexType>()->getElementType()->isUnsignedIntegerType()) {
      DSTr = Builder.CreateUDiv(Tmp3, Tmp6);
      DSTi = Builder.CreateUDiv(Tmp9, Tmp6);
    } else {
      DSTr = Builder.CreateSDiv(Tmp3, Tmp6);
      DSTi = Builder.CreateSDiv(Tmp9, Tmp6);
    }
  }

  return ComplexPairTy(DSTr, DSTi);
}

ComplexPairTy CodeGenFunction::EmitUnPromotedValue(ComplexPairTy result,
                                                   QualType UnPromotionType) {
  llvm::Type *ComplexElementTy =
      ConvertType(UnPromotionType->castAs<ComplexType>()->getElementType());
  if (result.first)
    result.first =
        Builder.CreateFPTrunc(result.first, ComplexElementTy, "unpromotion");
  if (result.second)
    result.second =
        Builder.CreateFPTrunc(result.second, ComplexElementTy, "unpromotion");
  return result;
}

ComplexPairTy CodeGenFunction::EmitPromotedValue(ComplexPairTy result,
                                                 QualType PromotionType) {
  llvm::Type *ComplexElementTy =
      ConvertType(PromotionType->castAs<ComplexType>()->getElementType());
  if (result.first)
    result.first = Builder.CreateFPExt(result.first, ComplexElementTy, "ext");
  if (result.second)
    result.second = Builder.CreateFPExt(result.second, ComplexElementTy, "ext");

  return result;
}

ComplexPairTy ComplexExprEmitter::EmitPromoted(const Expr *E,
                                               QualType PromotionType) {
  E = E->IgnoreParens();
  if (auto BO = dyn_cast<BinaryOperator>(E)) {
    switch (BO->getOpcode()) {
#define HANDLE_BINOP(OP)                                                       \
  case BO_##OP:                                                                \
    return EmitBin##OP(EmitBinOps(BO, PromotionType));
      HANDLE_BINOP(Add)
      HANDLE_BINOP(Sub)
      HANDLE_BINOP(Mul)
      HANDLE_BINOP(Div)
#undef HANDLE_BINOP
    default:
      break;
    }
  } else if (auto UO = dyn_cast<UnaryOperator>(E)) {
    switch (UO->getOpcode()) {
    case UO_Minus:
      return VisitMinus(UO, PromotionType);
    case UO_Plus:
      return VisitPlus(UO, PromotionType);
    default:
      break;
    }
  }
  auto result = Visit(const_cast<Expr *>(E));
  if (!PromotionType.isNull())
    return CGF.EmitPromotedValue(result, PromotionType);
  else
    return result;
}

ComplexPairTy CodeGenFunction::EmitPromotedComplexExpr(const Expr *E,
                                                       QualType DstTy) {
  return ComplexExprEmitter(*this).EmitPromoted(E, DstTy);
}

ComplexPairTy
ComplexExprEmitter::EmitPromotedComplexOperand(const Expr *E,
                                               QualType OverallPromotionType) {
  if (E->getType()->isAnyComplexType()) {
    if (!OverallPromotionType.isNull())
      return CGF.EmitPromotedComplexExpr(E, OverallPromotionType);
    else
      return Visit(const_cast<Expr *>(E));
  } else {
    if (!OverallPromotionType.isNull()) {
      QualType ComplexElementTy =
          OverallPromotionType->castAs<ComplexType>()->getElementType();
      return ComplexPairTy(CGF.EmitPromotedScalarExpr(E, ComplexElementTy),
                           nullptr);
    } else {
      return ComplexPairTy(CGF.EmitScalarExpr(E), nullptr);
    }
  }
}

ComplexExprEmitter::BinOpInfo
ComplexExprEmitter::EmitBinOps(const BinaryOperator *E,
                               QualType PromotionType) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  BinOpInfo Ops;

  Ops.LHS = EmitPromotedComplexOperand(E->getLHS(), PromotionType);
  Ops.RHS = EmitPromotedComplexOperand(E->getRHS(), PromotionType);
  if (!PromotionType.isNull())
    Ops.Ty = PromotionType;
  else
    Ops.Ty = E->getType();
  Ops.FPFeatures = E->getFPFeaturesInEffect(CGF.getLangOpts());
  return Ops;
}


LValue ComplexExprEmitter::
EmitCompoundAssignLValue(const CompoundAssignOperator *E,
          ComplexPairTy (ComplexExprEmitter::*Func)(const BinOpInfo&),
                         RValue &Val) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  QualType LHSTy = E->getLHS()->getType();
  if (const AtomicType *AT = LHSTy->getAs<AtomicType>())
    LHSTy = AT->getValueType();

  BinOpInfo OpInfo;
  OpInfo.FPFeatures = E->getFPFeaturesInEffect(CGF.getLangOpts());
  CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, OpInfo.FPFeatures);

  // Load the RHS and LHS operands.
  // __block variables need to have the rhs evaluated first, plus this should
  // improve codegen a little.
  QualType PromotionTypeCR;
  PromotionTypeCR = getPromotionType(E->getStoredFPFeaturesOrDefault(),
                                     E->getComputationResultType());
  if (PromotionTypeCR.isNull())
    PromotionTypeCR = E->getComputationResultType();
  OpInfo.Ty = PromotionTypeCR;
  QualType ComplexElementTy =
      OpInfo.Ty->castAs<ComplexType>()->getElementType();
  QualType PromotionTypeRHS = getPromotionType(
      E->getStoredFPFeaturesOrDefault(), E->getRHS()->getType());

  // The RHS should have been converted to the computation type.
  if (E->getRHS()->getType()->isRealFloatingType()) {
    if (!PromotionTypeRHS.isNull())
      OpInfo.RHS = ComplexPairTy(
          CGF.EmitPromotedScalarExpr(E->getRHS(), PromotionTypeRHS), nullptr);
    else {
      assert(CGF.getContext().hasSameUnqualifiedType(ComplexElementTy,
                                                     E->getRHS()->getType()));

      OpInfo.RHS = ComplexPairTy(CGF.EmitScalarExpr(E->getRHS()), nullptr);
    }
  } else {
    if (!PromotionTypeRHS.isNull()) {
      OpInfo.RHS = ComplexPairTy(
          CGF.EmitPromotedComplexExpr(E->getRHS(), PromotionTypeRHS));
    } else {
      assert(CGF.getContext().hasSameUnqualifiedType(OpInfo.Ty,
                                                     E->getRHS()->getType()));
      OpInfo.RHS = Visit(E->getRHS());
    }
  }

  LValue LHS = CGF.EmitLValue(E->getLHS());

  // Load from the l-value and convert it.
  SourceLocation Loc = E->getExprLoc();
  QualType PromotionTypeLHS = getPromotionType(
      E->getStoredFPFeaturesOrDefault(), E->getComputationLHSType());
  if (LHSTy->isAnyComplexType()) {
    ComplexPairTy LHSVal = EmitLoadOfLValue(LHS, Loc);
    if (!PromotionTypeLHS.isNull())
      OpInfo.LHS =
          EmitComplexToComplexCast(LHSVal, LHSTy, PromotionTypeLHS, Loc);
    else
      OpInfo.LHS = EmitComplexToComplexCast(LHSVal, LHSTy, OpInfo.Ty, Loc);
  } else {
    llvm::Value *LHSVal = CGF.EmitLoadOfScalar(LHS, Loc);
    // For floating point real operands we can directly pass the scalar form
    // to the binary operator emission and potentially get more efficient code.
    if (LHSTy->isRealFloatingType()) {
      QualType PromotedComplexElementTy;
      if (!PromotionTypeLHS.isNull()) {
        PromotedComplexElementTy =
            cast<ComplexType>(PromotionTypeLHS)->getElementType();
        if (!CGF.getContext().hasSameUnqualifiedType(PromotedComplexElementTy,
                                                     PromotionTypeLHS))
          LHSVal = CGF.EmitScalarConversion(LHSVal, LHSTy,
                                            PromotedComplexElementTy, Loc);
      } else {
        if (!CGF.getContext().hasSameUnqualifiedType(ComplexElementTy, LHSTy))
          LHSVal =
              CGF.EmitScalarConversion(LHSVal, LHSTy, ComplexElementTy, Loc);
      }
      OpInfo.LHS = ComplexPairTy(LHSVal, nullptr);
    } else {
      OpInfo.LHS = EmitScalarToComplexCast(LHSVal, LHSTy, OpInfo.Ty, Loc);
    }
  }

  // Expand the binary operator.
  ComplexPairTy Result = (this->*Func)(OpInfo);

  // Truncate the result and store it into the LHS lvalue.
  if (LHSTy->isAnyComplexType()) {
    ComplexPairTy ResVal =
        EmitComplexToComplexCast(Result, OpInfo.Ty, LHSTy, Loc);
    EmitStoreOfComplex(ResVal, LHS, /*isInit*/ false);
    Val = RValue::getComplex(ResVal);
  } else {
    llvm::Value *ResVal =
        CGF.EmitComplexToScalarConversion(Result, OpInfo.Ty, LHSTy, Loc);
    CGF.EmitStoreOfScalar(ResVal, LHS, /*isInit*/ false);
    Val = RValue::get(ResVal);
  }

  return LHS;
}

// Compound assignments.
ComplexPairTy ComplexExprEmitter::
EmitCompoundAssign(const CompoundAssignOperator *E,
                   ComplexPairTy (ComplexExprEmitter::*Func)(const BinOpInfo&)){
  RValue Val;
  LValue LV = EmitCompoundAssignLValue(E, Func, Val);

  // The result of an assignment in C is the assigned r-value.
  if (!CGF.getLangOpts().CPlusPlus)
    return Val.getComplexVal();

  // If the lvalue is non-volatile, return the computed value of the assignment.
  if (!LV.isVolatileQualified())
    return Val.getComplexVal();

  return EmitLoadOfLValue(LV, E->getExprLoc());
}

LValue ComplexExprEmitter::EmitBinAssignLValue(const BinaryOperator *E,
                                               ComplexPairTy &Val) {
  assert(CGF.getContext().hasSameUnqualifiedType(E->getLHS()->getType(),
                                                 E->getRHS()->getType()) &&
         "Invalid assignment");
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();

  // Emit the RHS.  __block variables need the RHS evaluated first.
  Val = Visit(E->getRHS());

  // Compute the address to store into.
  LValue LHS = CGF.EmitLValue(E->getLHS());

  // Store the result value into the LHS lvalue.
  EmitStoreOfComplex(Val, LHS, /*isInit*/ false);

  return LHS;
}

ComplexPairTy ComplexExprEmitter::VisitBinAssign(const BinaryOperator *E) {
  ComplexPairTy Val;
  LValue LV = EmitBinAssignLValue(E, Val);

  // The result of an assignment in C is the assigned r-value.
  if (!CGF.getLangOpts().CPlusPlus)
    return Val;

  // If the lvalue is non-volatile, return the computed value of the assignment.
  if (!LV.isVolatileQualified())
    return Val;

  return EmitLoadOfLValue(LV, E->getExprLoc());
}

ComplexPairTy ComplexExprEmitter::VisitBinComma(const BinaryOperator *E) {
  CGF.EmitIgnoredExpr(E->getLHS());
  return Visit(E->getRHS());
}

ComplexPairTy ComplexExprEmitter::
VisitAbstractConditionalOperator(const AbstractConditionalOperator *E) {
  TestAndClearIgnoreReal();
  TestAndClearIgnoreImag();
  llvm::BasicBlock *LHSBlock = CGF.createBasicBlock("cond.true");
  llvm::BasicBlock *RHSBlock = CGF.createBasicBlock("cond.false");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("cond.end");

  // Bind the common expression if necessary.
  CodeGenFunction::OpaqueValueMapping binding(CGF, E);


  CodeGenFunction::ConditionalEvaluation eval(CGF);
  CGF.EmitBranchOnBoolExpr(E->getCond(), LHSBlock, RHSBlock,
                           CGF.getProfileCount(E));

  eval.begin(CGF);
  CGF.EmitBlock(LHSBlock);
  if (llvm::EnableSingleByteCoverage)
    CGF.incrementProfileCounter(E->getTrueExpr());
  else
    CGF.incrementProfileCounter(E);

  ComplexPairTy LHS = Visit(E->getTrueExpr());
  LHSBlock = Builder.GetInsertBlock();
  CGF.EmitBranch(ContBlock);
  eval.end(CGF);

  eval.begin(CGF);
  CGF.EmitBlock(RHSBlock);
  if (llvm::EnableSingleByteCoverage)
    CGF.incrementProfileCounter(E->getFalseExpr());
  ComplexPairTy RHS = Visit(E->getFalseExpr());
  RHSBlock = Builder.GetInsertBlock();
  CGF.EmitBlock(ContBlock);
  if (llvm::EnableSingleByteCoverage)
    CGF.incrementProfileCounter(E);
  eval.end(CGF);

  // Create a PHI node for the real part.
  llvm::PHINode *RealPN = Builder.CreatePHI(LHS.first->getType(), 2, "cond.r");
  RealPN->addIncoming(LHS.first, LHSBlock);
  RealPN->addIncoming(RHS.first, RHSBlock);

  // Create a PHI node for the imaginary part.
  llvm::PHINode *ImagPN = Builder.CreatePHI(LHS.first->getType(), 2, "cond.i");
  ImagPN->addIncoming(LHS.second, LHSBlock);
  ImagPN->addIncoming(RHS.second, RHSBlock);

  return ComplexPairTy(RealPN, ImagPN);
}

ComplexPairTy ComplexExprEmitter::VisitChooseExpr(ChooseExpr *E) {
  return Visit(E->getChosenSubExpr());
}

ComplexPairTy ComplexExprEmitter::VisitInitListExpr(InitListExpr *E) {
    bool Ignore = TestAndClearIgnoreReal();
    (void)Ignore;
    assert (Ignore == false && "init list ignored");
    Ignore = TestAndClearIgnoreImag();
    (void)Ignore;
    assert (Ignore == false && "init list ignored");

  if (E->getNumInits() == 2) {
    llvm::Value *Real = CGF.EmitScalarExpr(E->getInit(0));
    llvm::Value *Imag = CGF.EmitScalarExpr(E->getInit(1));
    return ComplexPairTy(Real, Imag);
  } else if (E->getNumInits() == 1) {
    return Visit(E->getInit(0));
  }

  // Empty init list initializes to null
  assert(E->getNumInits() == 0 && "Unexpected number of inits");
  QualType Ty = E->getType()->castAs<ComplexType>()->getElementType();
  llvm::Type* LTy = CGF.ConvertType(Ty);
  llvm::Value* zeroConstant = llvm::Constant::getNullValue(LTy);
  return ComplexPairTy(zeroConstant, zeroConstant);
}

ComplexPairTy ComplexExprEmitter::VisitVAArgExpr(VAArgExpr *E) {
  Address ArgValue = Address::invalid();
  RValue RV = CGF.EmitVAArg(E, ArgValue);

  if (!ArgValue.isValid()) {
    CGF.ErrorUnsupported(E, "complex va_arg expression");
    llvm::Type *EltTy =
      CGF.ConvertType(E->getType()->castAs<ComplexType>()->getElementType());
    llvm::Value *U = llvm::UndefValue::get(EltTy);
    return ComplexPairTy(U, U);
  }

  return RV.getComplexVal();
}

//===----------------------------------------------------------------------===//
//                         Entry Point into this File
//===----------------------------------------------------------------------===//

/// EmitComplexExpr - Emit the computation of the specified expression of
/// complex type, ignoring the result.
ComplexPairTy CodeGenFunction::EmitComplexExpr(const Expr *E, bool IgnoreReal,
                                               bool IgnoreImag) {
  assert(E && getComplexType(E->getType()) &&
         "Invalid complex expression to emit");

  return ComplexExprEmitter(*this, IgnoreReal, IgnoreImag)
      .Visit(const_cast<Expr *>(E));
}

void CodeGenFunction::EmitComplexExprIntoLValue(const Expr *E, LValue dest,
                                                bool isInit) {
  assert(E && getComplexType(E->getType()) &&
         "Invalid complex expression to emit");
  ComplexExprEmitter Emitter(*this);
  ComplexPairTy Val = Emitter.Visit(const_cast<Expr*>(E));
  Emitter.EmitStoreOfComplex(Val, dest, isInit);
}

/// EmitStoreOfComplex - Store a complex number into the specified l-value.
void CodeGenFunction::EmitStoreOfComplex(ComplexPairTy V, LValue dest,
                                         bool isInit) {
  ComplexExprEmitter(*this).EmitStoreOfComplex(V, dest, isInit);
}

/// EmitLoadOfComplex - Load a complex number from the specified address.
ComplexPairTy CodeGenFunction::EmitLoadOfComplex(LValue src,
                                                 SourceLocation loc) {
  return ComplexExprEmitter(*this).EmitLoadOfLValue(src, loc);
}

LValue CodeGenFunction::EmitComplexAssignmentLValue(const BinaryOperator *E) {
  assert(E->getOpcode() == BO_Assign);
  ComplexPairTy Val; // ignored
  LValue LVal = ComplexExprEmitter(*this).EmitBinAssignLValue(E, Val);
  if (getLangOpts().OpenMP)
    CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(*this,
                                                              E->getLHS());
  return LVal;
}

typedef ComplexPairTy (ComplexExprEmitter::*CompoundFunc)(
    const ComplexExprEmitter::BinOpInfo &);

static CompoundFunc getComplexOp(BinaryOperatorKind Op) {
  switch (Op) {
  case BO_MulAssign: return &ComplexExprEmitter::EmitBinMul;
  case BO_DivAssign: return &ComplexExprEmitter::EmitBinDiv;
  case BO_SubAssign: return &ComplexExprEmitter::EmitBinSub;
  case BO_AddAssign: return &ComplexExprEmitter::EmitBinAdd;
  default:
    llvm_unreachable("unexpected complex compound assignment");
  }
}

LValue CodeGenFunction::
EmitComplexCompoundAssignmentLValue(const CompoundAssignOperator *E) {
  CompoundFunc Op = getComplexOp(E->getOpcode());
  RValue Val;
  return ComplexExprEmitter(*this).EmitCompoundAssignLValue(E, Op, Val);
}

LValue CodeGenFunction::
EmitScalarCompoundAssignWithComplex(const CompoundAssignOperator *E,
                                    llvm::Value *&Result) {
  CompoundFunc Op = getComplexOp(E->getOpcode());
  RValue Val;
  LValue Ret = ComplexExprEmitter(*this).EmitCompoundAssignLValue(E, Op, Val);
  Result = Val.getScalarVal();
  return Ret;
}
