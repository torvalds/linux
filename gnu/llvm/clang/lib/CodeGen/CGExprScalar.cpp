//===--- CGExprScalar.cpp - Emit LLVM Code for Scalar Exprs ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Expr nodes with scalar LLVM types as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGCleanup.h"
#include "CGDebugInfo.h"
#include "CGObjCRuntime.h"
#include "CGOpenMPRuntime.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/APFixedPoint.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FixedPointBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsPowerPC.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TypeSize.h"
#include <cstdarg>
#include <optional>

using namespace clang;
using namespace CodeGen;
using llvm::Value;

//===----------------------------------------------------------------------===//
//                         Scalar Expression Emitter
//===----------------------------------------------------------------------===//

namespace llvm {
extern cl::opt<bool> EnableSingleByteCoverage;
} // namespace llvm

namespace {

/// Determine whether the given binary operation may overflow.
/// Sets \p Result to the value of the operation for BO_Add, BO_Sub, BO_Mul,
/// and signed BO_{Div,Rem}. For these opcodes, and for unsigned BO_{Div,Rem},
/// the returned overflow check is precise. The returned value is 'true' for
/// all other opcodes, to be conservative.
bool mayHaveIntegerOverflow(llvm::ConstantInt *LHS, llvm::ConstantInt *RHS,
                             BinaryOperator::Opcode Opcode, bool Signed,
                             llvm::APInt &Result) {
  // Assume overflow is possible, unless we can prove otherwise.
  bool Overflow = true;
  const auto &LHSAP = LHS->getValue();
  const auto &RHSAP = RHS->getValue();
  if (Opcode == BO_Add) {
    Result = Signed ? LHSAP.sadd_ov(RHSAP, Overflow)
                    : LHSAP.uadd_ov(RHSAP, Overflow);
  } else if (Opcode == BO_Sub) {
    Result = Signed ? LHSAP.ssub_ov(RHSAP, Overflow)
                    : LHSAP.usub_ov(RHSAP, Overflow);
  } else if (Opcode == BO_Mul) {
    Result = Signed ? LHSAP.smul_ov(RHSAP, Overflow)
                    : LHSAP.umul_ov(RHSAP, Overflow);
  } else if (Opcode == BO_Div || Opcode == BO_Rem) {
    if (Signed && !RHS->isZero())
      Result = LHSAP.sdiv_ov(RHSAP, Overflow);
    else
      return false;
  }
  return Overflow;
}

struct BinOpInfo {
  Value *LHS;
  Value *RHS;
  QualType Ty;  // Computation Type.
  BinaryOperator::Opcode Opcode; // Opcode of BinOp to perform
  FPOptions FPFeatures;
  const Expr *E;      // Entire expr, for error unsupported.  May not be binop.

  /// Check if the binop can result in integer overflow.
  bool mayHaveIntegerOverflow() const {
    // Without constant input, we can't rule out overflow.
    auto *LHSCI = dyn_cast<llvm::ConstantInt>(LHS);
    auto *RHSCI = dyn_cast<llvm::ConstantInt>(RHS);
    if (!LHSCI || !RHSCI)
      return true;

    llvm::APInt Result;
    return ::mayHaveIntegerOverflow(
        LHSCI, RHSCI, Opcode, Ty->hasSignedIntegerRepresentation(), Result);
  }

  /// Check if the binop computes a division or a remainder.
  bool isDivremOp() const {
    return Opcode == BO_Div || Opcode == BO_Rem || Opcode == BO_DivAssign ||
           Opcode == BO_RemAssign;
  }

  /// Check if the binop can result in an integer division by zero.
  bool mayHaveIntegerDivisionByZero() const {
    if (isDivremOp())
      if (auto *CI = dyn_cast<llvm::ConstantInt>(RHS))
        return CI->isZero();
    return true;
  }

  /// Check if the binop can result in a float division by zero.
  bool mayHaveFloatDivisionByZero() const {
    if (isDivremOp())
      if (auto *CFP = dyn_cast<llvm::ConstantFP>(RHS))
        return CFP->isZero();
    return true;
  }

  /// Check if at least one operand is a fixed point type. In such cases, this
  /// operation did not follow usual arithmetic conversion and both operands
  /// might not be of the same type.
  bool isFixedPointOp() const {
    // We cannot simply check the result type since comparison operations return
    // an int.
    if (const auto *BinOp = dyn_cast<BinaryOperator>(E)) {
      QualType LHSType = BinOp->getLHS()->getType();
      QualType RHSType = BinOp->getRHS()->getType();
      return LHSType->isFixedPointType() || RHSType->isFixedPointType();
    }
    if (const auto *UnOp = dyn_cast<UnaryOperator>(E))
      return UnOp->getSubExpr()->getType()->isFixedPointType();
    return false;
  }

  /// Check if the RHS has a signed integer representation.
  bool rhsHasSignedIntegerRepresentation() const {
    if (const auto *BinOp = dyn_cast<BinaryOperator>(E)) {
      QualType RHSType = BinOp->getRHS()->getType();
      return RHSType->hasSignedIntegerRepresentation();
    }
    return false;
  }
};

static bool MustVisitNullValue(const Expr *E) {
  // If a null pointer expression's type is the C++0x nullptr_t, then
  // it's not necessarily a simple constant and it must be evaluated
  // for its potential side effects.
  return E->getType()->isNullPtrType();
}

/// If \p E is a widened promoted integer, get its base (unpromoted) type.
static std::optional<QualType> getUnwidenedIntegerType(const ASTContext &Ctx,
                                                       const Expr *E) {
  const Expr *Base = E->IgnoreImpCasts();
  if (E == Base)
    return std::nullopt;

  QualType BaseTy = Base->getType();
  if (!Ctx.isPromotableIntegerType(BaseTy) ||
      Ctx.getTypeSize(BaseTy) >= Ctx.getTypeSize(E->getType()))
    return std::nullopt;

  return BaseTy;
}

/// Check if \p E is a widened promoted integer.
static bool IsWidenedIntegerOp(const ASTContext &Ctx, const Expr *E) {
  return getUnwidenedIntegerType(Ctx, E).has_value();
}

/// Check if we can skip the overflow check for \p Op.
static bool CanElideOverflowCheck(const ASTContext &Ctx, const BinOpInfo &Op) {
  assert((isa<UnaryOperator>(Op.E) || isa<BinaryOperator>(Op.E)) &&
         "Expected a unary or binary operator");

  // If the binop has constant inputs and we can prove there is no overflow,
  // we can elide the overflow check.
  if (!Op.mayHaveIntegerOverflow())
    return true;

  // If a unary op has a widened operand, the op cannot overflow.
  if (const auto *UO = dyn_cast<UnaryOperator>(Op.E))
    return !UO->canOverflow();

  // We usually don't need overflow checks for binops with widened operands.
  // Multiplication with promoted unsigned operands is a special case.
  const auto *BO = cast<BinaryOperator>(Op.E);
  auto OptionalLHSTy = getUnwidenedIntegerType(Ctx, BO->getLHS());
  if (!OptionalLHSTy)
    return false;

  auto OptionalRHSTy = getUnwidenedIntegerType(Ctx, BO->getRHS());
  if (!OptionalRHSTy)
    return false;

  QualType LHSTy = *OptionalLHSTy;
  QualType RHSTy = *OptionalRHSTy;

  // This is the simple case: binops without unsigned multiplication, and with
  // widened operands. No overflow check is needed here.
  if ((Op.Opcode != BO_Mul && Op.Opcode != BO_MulAssign) ||
      !LHSTy->isUnsignedIntegerType() || !RHSTy->isUnsignedIntegerType())
    return true;

  // For unsigned multiplication the overflow check can be elided if either one
  // of the unpromoted types are less than half the size of the promoted type.
  unsigned PromotedSize = Ctx.getTypeSize(Op.E->getType());
  return (2 * Ctx.getTypeSize(LHSTy)) < PromotedSize ||
         (2 * Ctx.getTypeSize(RHSTy)) < PromotedSize;
}

class ScalarExprEmitter
  : public StmtVisitor<ScalarExprEmitter, Value*> {
  CodeGenFunction &CGF;
  CGBuilderTy &Builder;
  bool IgnoreResultAssign;
  llvm::LLVMContext &VMContext;
public:

  ScalarExprEmitter(CodeGenFunction &cgf, bool ira=false)
    : CGF(cgf), Builder(CGF.Builder), IgnoreResultAssign(ira),
      VMContext(cgf.getLLVMContext()) {
  }

  //===--------------------------------------------------------------------===//
  //                               Utilities
  //===--------------------------------------------------------------------===//

  bool TestAndClearIgnoreResultAssign() {
    bool I = IgnoreResultAssign;
    IgnoreResultAssign = false;
    return I;
  }

  llvm::Type *ConvertType(QualType T) { return CGF.ConvertType(T); }
  LValue EmitLValue(const Expr *E) { return CGF.EmitLValue(E); }
  LValue EmitCheckedLValue(const Expr *E, CodeGenFunction::TypeCheckKind TCK) {
    return CGF.EmitCheckedLValue(E, TCK);
  }

  void EmitBinOpCheck(ArrayRef<std::pair<Value *, SanitizerMask>> Checks,
                      const BinOpInfo &Info);

  Value *EmitLoadOfLValue(LValue LV, SourceLocation Loc) {
    return CGF.EmitLoadOfLValue(LV, Loc).getScalarVal();
  }

  void EmitLValueAlignmentAssumption(const Expr *E, Value *V) {
    const AlignValueAttr *AVAttr = nullptr;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      const ValueDecl *VD = DRE->getDecl();

      if (VD->getType()->isReferenceType()) {
        if (const auto *TTy =
                VD->getType().getNonReferenceType()->getAs<TypedefType>())
          AVAttr = TTy->getDecl()->getAttr<AlignValueAttr>();
      } else {
        // Assumptions for function parameters are emitted at the start of the
        // function, so there is no need to repeat that here,
        // unless the alignment-assumption sanitizer is enabled,
        // then we prefer the assumption over alignment attribute
        // on IR function param.
        if (isa<ParmVarDecl>(VD) && !CGF.SanOpts.has(SanitizerKind::Alignment))
          return;

        AVAttr = VD->getAttr<AlignValueAttr>();
      }
    }

    if (!AVAttr)
      if (const auto *TTy = E->getType()->getAs<TypedefType>())
        AVAttr = TTy->getDecl()->getAttr<AlignValueAttr>();

    if (!AVAttr)
      return;

    Value *AlignmentValue = CGF.EmitScalarExpr(AVAttr->getAlignment());
    llvm::ConstantInt *AlignmentCI = cast<llvm::ConstantInt>(AlignmentValue);
    CGF.emitAlignmentAssumption(V, E, AVAttr->getLocation(), AlignmentCI);
  }

  /// EmitLoadOfLValue - Given an expression with complex type that represents a
  /// value l-value, this method emits the address of the l-value, then loads
  /// and returns the result.
  Value *EmitLoadOfLValue(const Expr *E) {
    Value *V = EmitLoadOfLValue(EmitCheckedLValue(E, CodeGenFunction::TCK_Load),
                                E->getExprLoc());

    EmitLValueAlignmentAssumption(E, V);
    return V;
  }

  /// EmitConversionToBool - Convert the specified expression value to a
  /// boolean (i1) truth value.  This is equivalent to "Val != 0".
  Value *EmitConversionToBool(Value *Src, QualType DstTy);

  /// Emit a check that a conversion from a floating-point type does not
  /// overflow.
  void EmitFloatConversionCheck(Value *OrigSrc, QualType OrigSrcType,
                                Value *Src, QualType SrcType, QualType DstType,
                                llvm::Type *DstTy, SourceLocation Loc);

  /// Known implicit conversion check kinds.
  /// This is used for bitfield conversion checks as well.
  /// Keep in sync with the enum of the same name in ubsan_handlers.h
  enum ImplicitConversionCheckKind : unsigned char {
    ICCK_IntegerTruncation = 0, // Legacy, was only used by clang 7.
    ICCK_UnsignedIntegerTruncation = 1,
    ICCK_SignedIntegerTruncation = 2,
    ICCK_IntegerSignChange = 3,
    ICCK_SignedIntegerTruncationOrSignChange = 4,
  };

  /// Emit a check that an [implicit] truncation of an integer  does not
  /// discard any bits. It is not UB, so we use the value after truncation.
  void EmitIntegerTruncationCheck(Value *Src, QualType SrcType, Value *Dst,
                                  QualType DstType, SourceLocation Loc);

  /// Emit a check that an [implicit] conversion of an integer does not change
  /// the sign of the value. It is not UB, so we use the value after conversion.
  /// NOTE: Src and Dst may be the exact same value! (point to the same thing)
  void EmitIntegerSignChangeCheck(Value *Src, QualType SrcType, Value *Dst,
                                  QualType DstType, SourceLocation Loc);

  /// Emit a conversion from the specified type to the specified destination
  /// type, both of which are LLVM scalar types.
  struct ScalarConversionOpts {
    bool TreatBooleanAsSigned;
    bool EmitImplicitIntegerTruncationChecks;
    bool EmitImplicitIntegerSignChangeChecks;

    ScalarConversionOpts()
        : TreatBooleanAsSigned(false),
          EmitImplicitIntegerTruncationChecks(false),
          EmitImplicitIntegerSignChangeChecks(false) {}

    ScalarConversionOpts(clang::SanitizerSet SanOpts)
        : TreatBooleanAsSigned(false),
          EmitImplicitIntegerTruncationChecks(
              SanOpts.hasOneOf(SanitizerKind::ImplicitIntegerTruncation)),
          EmitImplicitIntegerSignChangeChecks(
              SanOpts.has(SanitizerKind::ImplicitIntegerSignChange)) {}
  };
  Value *EmitScalarCast(Value *Src, QualType SrcType, QualType DstType,
                        llvm::Type *SrcTy, llvm::Type *DstTy,
                        ScalarConversionOpts Opts);
  Value *
  EmitScalarConversion(Value *Src, QualType SrcTy, QualType DstTy,
                       SourceLocation Loc,
                       ScalarConversionOpts Opts = ScalarConversionOpts());

  /// Convert between either a fixed point and other fixed point or fixed point
  /// and an integer.
  Value *EmitFixedPointConversion(Value *Src, QualType SrcTy, QualType DstTy,
                                  SourceLocation Loc);

  /// Emit a conversion from the specified complex type to the specified
  /// destination type, where the destination type is an LLVM scalar type.
  Value *EmitComplexToScalarConversion(CodeGenFunction::ComplexPairTy Src,
                                       QualType SrcTy, QualType DstTy,
                                       SourceLocation Loc);

  /// EmitNullValue - Emit a value that corresponds to null for the given type.
  Value *EmitNullValue(QualType Ty);

  /// EmitFloatToBoolConversion - Perform an FP to boolean conversion.
  Value *EmitFloatToBoolConversion(Value *V) {
    // Compare against 0.0 for fp scalars.
    llvm::Value *Zero = llvm::Constant::getNullValue(V->getType());
    return Builder.CreateFCmpUNE(V, Zero, "tobool");
  }

  /// EmitPointerToBoolConversion - Perform a pointer to boolean conversion.
  Value *EmitPointerToBoolConversion(Value *V, QualType QT) {
    Value *Zero = CGF.CGM.getNullPointer(cast<llvm::PointerType>(V->getType()), QT);

    return Builder.CreateICmpNE(V, Zero, "tobool");
  }

  Value *EmitIntToBoolConversion(Value *V) {
    // Because of the type rules of C, we often end up computing a
    // logical value, then zero extending it to int, then wanting it
    // as a logical value again.  Optimize this common case.
    if (llvm::ZExtInst *ZI = dyn_cast<llvm::ZExtInst>(V)) {
      if (ZI->getOperand(0)->getType() == Builder.getInt1Ty()) {
        Value *Result = ZI->getOperand(0);
        // If there aren't any more uses, zap the instruction to save space.
        // Note that there can be more uses, for example if this
        // is the result of an assignment.
        if (ZI->use_empty())
          ZI->eraseFromParent();
        return Result;
      }
    }

    return Builder.CreateIsNotNull(V, "tobool");
  }

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  Value *Visit(Expr *E) {
    ApplyDebugLocation DL(CGF, E);
    return StmtVisitor<ScalarExprEmitter, Value*>::Visit(E);
  }

  Value *VisitStmt(Stmt *S) {
    S->dump(llvm::errs(), CGF.getContext());
    llvm_unreachable("Stmt can't have complex result type!");
  }
  Value *VisitExpr(Expr *S);

  Value *VisitConstantExpr(ConstantExpr *E) {
    // A constant expression of type 'void' generates no code and produces no
    // value.
    if (E->getType()->isVoidType())
      return nullptr;

    if (Value *Result = ConstantEmitter(CGF).tryEmitConstantExpr(E)) {
      if (E->isGLValue())
        return CGF.EmitLoadOfScalar(
            Address(Result, CGF.convertTypeForLoadStore(E->getType()),
                    CGF.getContext().getTypeAlignInChars(E->getType())),
            /*Volatile*/ false, E->getType(), E->getExprLoc());
      return Result;
    }
    return Visit(E->getSubExpr());
  }
  Value *VisitParenExpr(ParenExpr *PE) {
    return Visit(PE->getSubExpr());
  }
  Value *VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *E) {
    return Visit(E->getReplacement());
  }
  Value *VisitGenericSelectionExpr(GenericSelectionExpr *GE) {
    return Visit(GE->getResultExpr());
  }
  Value *VisitCoawaitExpr(CoawaitExpr *S) {
    return CGF.EmitCoawaitExpr(*S).getScalarVal();
  }
  Value *VisitCoyieldExpr(CoyieldExpr *S) {
    return CGF.EmitCoyieldExpr(*S).getScalarVal();
  }
  Value *VisitUnaryCoawait(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }

  // Leaves.
  Value *VisitIntegerLiteral(const IntegerLiteral *E) {
    return Builder.getInt(E->getValue());
  }
  Value *VisitFixedPointLiteral(const FixedPointLiteral *E) {
    return Builder.getInt(E->getValue());
  }
  Value *VisitFloatingLiteral(const FloatingLiteral *E) {
    return llvm::ConstantFP::get(VMContext, E->getValue());
  }
  Value *VisitCharacterLiteral(const CharacterLiteral *E) {
    return llvm::ConstantInt::get(ConvertType(E->getType()), E->getValue());
  }
  Value *VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *E) {
    return llvm::ConstantInt::get(ConvertType(E->getType()), E->getValue());
  }
  Value *VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *E) {
    return llvm::ConstantInt::get(ConvertType(E->getType()), E->getValue());
  }
  Value *VisitCXXScalarValueInitExpr(const CXXScalarValueInitExpr *E) {
    if (E->getType()->isVoidType())
      return nullptr;

    return EmitNullValue(E->getType());
  }
  Value *VisitGNUNullExpr(const GNUNullExpr *E) {
    return EmitNullValue(E->getType());
  }
  Value *VisitOffsetOfExpr(OffsetOfExpr *E);
  Value *VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *E);
  Value *VisitAddrLabelExpr(const AddrLabelExpr *E) {
    llvm::Value *V = CGF.GetAddrOfLabel(E->getLabel());
    return Builder.CreateBitCast(V, ConvertType(E->getType()));
  }

  Value *VisitSizeOfPackExpr(SizeOfPackExpr *E) {
    return llvm::ConstantInt::get(ConvertType(E->getType()),E->getPackLength());
  }

  Value *VisitPseudoObjectExpr(PseudoObjectExpr *E) {
    return CGF.EmitPseudoObjectRValue(E).getScalarVal();
  }

  Value *VisitSYCLUniqueStableNameExpr(SYCLUniqueStableNameExpr *E);
  Value *VisitEmbedExpr(EmbedExpr *E);

  Value *VisitOpaqueValueExpr(OpaqueValueExpr *E) {
    if (E->isGLValue())
      return EmitLoadOfLValue(CGF.getOrCreateOpaqueLValueMapping(E),
                              E->getExprLoc());

    // Otherwise, assume the mapping is the scalar directly.
    return CGF.getOrCreateOpaqueRValueMapping(E).getScalarVal();
  }

  // l-values.
  Value *VisitDeclRefExpr(DeclRefExpr *E) {
    if (CodeGenFunction::ConstantEmission Constant = CGF.tryEmitAsConstant(E))
      return CGF.emitScalarConstant(Constant, E);
    return EmitLoadOfLValue(E);
  }

  Value *VisitObjCSelectorExpr(ObjCSelectorExpr *E) {
    return CGF.EmitObjCSelectorExpr(E);
  }
  Value *VisitObjCProtocolExpr(ObjCProtocolExpr *E) {
    return CGF.EmitObjCProtocolExpr(E);
  }
  Value *VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
    return EmitLoadOfLValue(E);
  }
  Value *VisitObjCMessageExpr(ObjCMessageExpr *E) {
    if (E->getMethodDecl() &&
        E->getMethodDecl()->getReturnType()->isReferenceType())
      return EmitLoadOfLValue(E);
    return CGF.EmitObjCMessageExpr(E).getScalarVal();
  }

  Value *VisitObjCIsaExpr(ObjCIsaExpr *E) {
    LValue LV = CGF.EmitObjCIsaExpr(E);
    Value *V = CGF.EmitLoadOfLValue(LV, E->getExprLoc()).getScalarVal();
    return V;
  }

  Value *VisitObjCAvailabilityCheckExpr(ObjCAvailabilityCheckExpr *E) {
    VersionTuple Version = E->getVersion();

    // If we're checking for a platform older than our minimum deployment
    // target, we can fold the check away.
    if (Version <= CGF.CGM.getTarget().getPlatformMinVersion())
      return llvm::ConstantInt::get(Builder.getInt1Ty(), 1);

    return CGF.EmitBuiltinAvailable(Version);
  }

  Value *VisitArraySubscriptExpr(ArraySubscriptExpr *E);
  Value *VisitMatrixSubscriptExpr(MatrixSubscriptExpr *E);
  Value *VisitShuffleVectorExpr(ShuffleVectorExpr *E);
  Value *VisitConvertVectorExpr(ConvertVectorExpr *E);
  Value *VisitMemberExpr(MemberExpr *E);
  Value *VisitExtVectorElementExpr(Expr *E) { return EmitLoadOfLValue(E); }
  Value *VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
    // Strictly speaking, we shouldn't be calling EmitLoadOfLValue, which
    // transitively calls EmitCompoundLiteralLValue, here in C++ since compound
    // literals aren't l-values in C++. We do so simply because that's the
    // cleanest way to handle compound literals in C++.
    // See the discussion here: https://reviews.llvm.org/D64464
    return EmitLoadOfLValue(E);
  }

  Value *VisitInitListExpr(InitListExpr *E);

  Value *VisitArrayInitIndexExpr(ArrayInitIndexExpr *E) {
    assert(CGF.getArrayInitIndex() &&
           "ArrayInitIndexExpr not inside an ArrayInitLoopExpr?");
    return CGF.getArrayInitIndex();
  }

  Value *VisitImplicitValueInitExpr(const ImplicitValueInitExpr *E) {
    return EmitNullValue(E->getType());
  }
  Value *VisitExplicitCastExpr(ExplicitCastExpr *E) {
    CGF.CGM.EmitExplicitCastExprType(E, &CGF);
    return VisitCastExpr(E);
  }
  Value *VisitCastExpr(CastExpr *E);

  Value *VisitCallExpr(const CallExpr *E) {
    if (E->getCallReturnType(CGF.getContext())->isReferenceType())
      return EmitLoadOfLValue(E);

    Value *V = CGF.EmitCallExpr(E).getScalarVal();

    EmitLValueAlignmentAssumption(E, V);
    return V;
  }

  Value *VisitStmtExpr(const StmtExpr *E);

  // Unary Operators.
  Value *VisitUnaryPostDec(const UnaryOperator *E) {
    LValue LV = EmitLValue(E->getSubExpr());
    return EmitScalarPrePostIncDec(E, LV, false, false);
  }
  Value *VisitUnaryPostInc(const UnaryOperator *E) {
    LValue LV = EmitLValue(E->getSubExpr());
    return EmitScalarPrePostIncDec(E, LV, true, false);
  }
  Value *VisitUnaryPreDec(const UnaryOperator *E) {
    LValue LV = EmitLValue(E->getSubExpr());
    return EmitScalarPrePostIncDec(E, LV, false, true);
  }
  Value *VisitUnaryPreInc(const UnaryOperator *E) {
    LValue LV = EmitLValue(E->getSubExpr());
    return EmitScalarPrePostIncDec(E, LV, true, true);
  }

  llvm::Value *EmitIncDecConsiderOverflowBehavior(const UnaryOperator *E,
                                                  llvm::Value *InVal,
                                                  bool IsInc);

  llvm::Value *EmitScalarPrePostIncDec(const UnaryOperator *E, LValue LV,
                                       bool isInc, bool isPre);


  Value *VisitUnaryAddrOf(const UnaryOperator *E) {
    if (isa<MemberPointerType>(E->getType())) // never sugared
      return CGF.CGM.getMemberPointerConstant(E);

    return EmitLValue(E->getSubExpr()).getPointer(CGF);
  }
  Value *VisitUnaryDeref(const UnaryOperator *E) {
    if (E->getType()->isVoidType())
      return Visit(E->getSubExpr()); // the actual value should be unused
    return EmitLoadOfLValue(E);
  }

  Value *VisitUnaryPlus(const UnaryOperator *E,
                        QualType PromotionType = QualType());
  Value *VisitPlus(const UnaryOperator *E, QualType PromotionType);
  Value *VisitUnaryMinus(const UnaryOperator *E,
                         QualType PromotionType = QualType());
  Value *VisitMinus(const UnaryOperator *E, QualType PromotionType);

  Value *VisitUnaryNot      (const UnaryOperator *E);
  Value *VisitUnaryLNot     (const UnaryOperator *E);
  Value *VisitUnaryReal(const UnaryOperator *E,
                        QualType PromotionType = QualType());
  Value *VisitReal(const UnaryOperator *E, QualType PromotionType);
  Value *VisitUnaryImag(const UnaryOperator *E,
                        QualType PromotionType = QualType());
  Value *VisitImag(const UnaryOperator *E, QualType PromotionType);
  Value *VisitUnaryExtension(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }

  // C++
  Value *VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *E) {
    return EmitLoadOfLValue(E);
  }
  Value *VisitSourceLocExpr(SourceLocExpr *SLE) {
    auto &Ctx = CGF.getContext();
    APValue Evaluated =
        SLE->EvaluateInContext(Ctx, CGF.CurSourceLocExprScope.getDefaultExpr());
    return ConstantEmitter(CGF).emitAbstract(SLE->getLocation(), Evaluated,
                                             SLE->getType());
  }

  Value *VisitCXXDefaultArgExpr(CXXDefaultArgExpr *DAE) {
    CodeGenFunction::CXXDefaultArgExprScope Scope(CGF, DAE);
    return Visit(DAE->getExpr());
  }
  Value *VisitCXXDefaultInitExpr(CXXDefaultInitExpr *DIE) {
    CodeGenFunction::CXXDefaultInitExprScope Scope(CGF, DIE);
    return Visit(DIE->getExpr());
  }
  Value *VisitCXXThisExpr(CXXThisExpr *TE) {
    return CGF.LoadCXXThis();
  }

  Value *VisitExprWithCleanups(ExprWithCleanups *E);
  Value *VisitCXXNewExpr(const CXXNewExpr *E) {
    return CGF.EmitCXXNewExpr(E);
  }
  Value *VisitCXXDeleteExpr(const CXXDeleteExpr *E) {
    CGF.EmitCXXDeleteExpr(E);
    return nullptr;
  }

  Value *VisitTypeTraitExpr(const TypeTraitExpr *E) {
    return llvm::ConstantInt::get(ConvertType(E->getType()), E->getValue());
  }

  Value *VisitConceptSpecializationExpr(const ConceptSpecializationExpr *E) {
    return Builder.getInt1(E->isSatisfied());
  }

  Value *VisitRequiresExpr(const RequiresExpr *E) {
    return Builder.getInt1(E->isSatisfied());
  }

  Value *VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *E) {
    return llvm::ConstantInt::get(Builder.getInt32Ty(), E->getValue());
  }

  Value *VisitExpressionTraitExpr(const ExpressionTraitExpr *E) {
    return llvm::ConstantInt::get(Builder.getInt1Ty(), E->getValue());
  }

  Value *VisitCXXPseudoDestructorExpr(const CXXPseudoDestructorExpr *E) {
    // C++ [expr.pseudo]p1:
    //   The result shall only be used as the operand for the function call
    //   operator (), and the result of such a call has type void. The only
    //   effect is the evaluation of the postfix-expression before the dot or
    //   arrow.
    CGF.EmitScalarExpr(E->getBase());
    return nullptr;
  }

  Value *VisitCXXNullPtrLiteralExpr(const CXXNullPtrLiteralExpr *E) {
    return EmitNullValue(E->getType());
  }

  Value *VisitCXXThrowExpr(const CXXThrowExpr *E) {
    CGF.EmitCXXThrowExpr(E);
    return nullptr;
  }

  Value *VisitCXXNoexceptExpr(const CXXNoexceptExpr *E) {
    return Builder.getInt1(E->getValue());
  }

  // Binary Operators.
  Value *EmitMul(const BinOpInfo &Ops) {
    if (Ops.Ty->isSignedIntegerOrEnumerationType()) {
      switch (CGF.getLangOpts().getSignedOverflowBehavior()) {
      case LangOptions::SOB_Defined:
        if (!CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow))
          return Builder.CreateMul(Ops.LHS, Ops.RHS, "mul");
        [[fallthrough]];
      case LangOptions::SOB_Undefined:
        if (!CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow))
          return Builder.CreateNSWMul(Ops.LHS, Ops.RHS, "mul");
        [[fallthrough]];
      case LangOptions::SOB_Trapping:
        if (CanElideOverflowCheck(CGF.getContext(), Ops))
          return Builder.CreateNSWMul(Ops.LHS, Ops.RHS, "mul");
        return EmitOverflowCheckedBinOp(Ops);
      }
    }

    if (Ops.Ty->isConstantMatrixType()) {
      llvm::MatrixBuilder MB(Builder);
      // We need to check the types of the operands of the operator to get the
      // correct matrix dimensions.
      auto *BO = cast<BinaryOperator>(Ops.E);
      auto *LHSMatTy = dyn_cast<ConstantMatrixType>(
          BO->getLHS()->getType().getCanonicalType());
      auto *RHSMatTy = dyn_cast<ConstantMatrixType>(
          BO->getRHS()->getType().getCanonicalType());
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Ops.FPFeatures);
      if (LHSMatTy && RHSMatTy)
        return MB.CreateMatrixMultiply(Ops.LHS, Ops.RHS, LHSMatTy->getNumRows(),
                                       LHSMatTy->getNumColumns(),
                                       RHSMatTy->getNumColumns());
      return MB.CreateScalarMultiply(Ops.LHS, Ops.RHS);
    }

    if (Ops.Ty->isUnsignedIntegerType() &&
        CGF.SanOpts.has(SanitizerKind::UnsignedIntegerOverflow) &&
        !CanElideOverflowCheck(CGF.getContext(), Ops))
      return EmitOverflowCheckedBinOp(Ops);

    if (Ops.LHS->getType()->isFPOrFPVectorTy()) {
      //  Preserve the old values
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Ops.FPFeatures);
      return Builder.CreateFMul(Ops.LHS, Ops.RHS, "mul");
    }
    if (Ops.isFixedPointOp())
      return EmitFixedPointBinOp(Ops);
    return Builder.CreateMul(Ops.LHS, Ops.RHS, "mul");
  }
  /// Create a binary op that checks for overflow.
  /// Currently only supports +, - and *.
  Value *EmitOverflowCheckedBinOp(const BinOpInfo &Ops);

  // Check for undefined division and modulus behaviors.
  void EmitUndefinedBehaviorIntegerDivAndRemCheck(const BinOpInfo &Ops,
                                                  llvm::Value *Zero,bool isDiv);
  // Common helper for getting how wide LHS of shift is.
  static Value *GetMaximumShiftAmount(Value *LHS, Value *RHS, bool RHSIsSigned);

  // Used for shifting constraints for OpenCL, do mask for powers of 2, URem for
  // non powers of two.
  Value *ConstrainShiftValue(Value *LHS, Value *RHS, const Twine &Name);

  Value *EmitDiv(const BinOpInfo &Ops);
  Value *EmitRem(const BinOpInfo &Ops);
  Value *EmitAdd(const BinOpInfo &Ops);
  Value *EmitSub(const BinOpInfo &Ops);
  Value *EmitShl(const BinOpInfo &Ops);
  Value *EmitShr(const BinOpInfo &Ops);
  Value *EmitAnd(const BinOpInfo &Ops) {
    return Builder.CreateAnd(Ops.LHS, Ops.RHS, "and");
  }
  Value *EmitXor(const BinOpInfo &Ops) {
    return Builder.CreateXor(Ops.LHS, Ops.RHS, "xor");
  }
  Value *EmitOr (const BinOpInfo &Ops) {
    return Builder.CreateOr(Ops.LHS, Ops.RHS, "or");
  }

  // Helper functions for fixed point binary operations.
  Value *EmitFixedPointBinOp(const BinOpInfo &Ops);

  BinOpInfo EmitBinOps(const BinaryOperator *E,
                       QualType PromotionTy = QualType());

  Value *EmitPromotedValue(Value *result, QualType PromotionType);
  Value *EmitUnPromotedValue(Value *result, QualType ExprType);
  Value *EmitPromoted(const Expr *E, QualType PromotionType);

  LValue EmitCompoundAssignLValue(const CompoundAssignOperator *E,
                            Value *(ScalarExprEmitter::*F)(const BinOpInfo &),
                                  Value *&Result);

  Value *EmitCompoundAssign(const CompoundAssignOperator *E,
                            Value *(ScalarExprEmitter::*F)(const BinOpInfo &));

  QualType getPromotionType(QualType Ty) {
    const auto &Ctx = CGF.getContext();
    if (auto *CT = Ty->getAs<ComplexType>()) {
      QualType ElementType = CT->getElementType();
      if (ElementType.UseExcessPrecision(Ctx))
        return Ctx.getComplexType(Ctx.FloatTy);
    }

    if (Ty.UseExcessPrecision(Ctx)) {
      if (auto *VT = Ty->getAs<VectorType>()) {
        unsigned NumElements = VT->getNumElements();
        return Ctx.getVectorType(Ctx.FloatTy, NumElements, VT->getVectorKind());
      }
      return Ctx.FloatTy;
    }

    return QualType();
  }

  // Binary operators and binary compound assignment operators.
#define HANDLEBINOP(OP)                                                        \
  Value *VisitBin##OP(const BinaryOperator *E) {                               \
    QualType promotionTy = getPromotionType(E->getType());                     \
    auto result = Emit##OP(EmitBinOps(E, promotionTy));                        \
    if (result && !promotionTy.isNull())                                       \
      result = EmitUnPromotedValue(result, E->getType());                      \
    return result;                                                             \
  }                                                                            \
  Value *VisitBin##OP##Assign(const CompoundAssignOperator *E) {               \
    return EmitCompoundAssign(E, &ScalarExprEmitter::Emit##OP);                \
  }
  HANDLEBINOP(Mul)
  HANDLEBINOP(Div)
  HANDLEBINOP(Rem)
  HANDLEBINOP(Add)
  HANDLEBINOP(Sub)
  HANDLEBINOP(Shl)
  HANDLEBINOP(Shr)
  HANDLEBINOP(And)
  HANDLEBINOP(Xor)
  HANDLEBINOP(Or)
#undef HANDLEBINOP

  // Comparisons.
  Value *EmitCompare(const BinaryOperator *E, llvm::CmpInst::Predicate UICmpOpc,
                     llvm::CmpInst::Predicate SICmpOpc,
                     llvm::CmpInst::Predicate FCmpOpc, bool IsSignaling);
#define VISITCOMP(CODE, UI, SI, FP, SIG) \
    Value *VisitBin##CODE(const BinaryOperator *E) { \
      return EmitCompare(E, llvm::ICmpInst::UI, llvm::ICmpInst::SI, \
                         llvm::FCmpInst::FP, SIG); }
  VISITCOMP(LT, ICMP_ULT, ICMP_SLT, FCMP_OLT, true)
  VISITCOMP(GT, ICMP_UGT, ICMP_SGT, FCMP_OGT, true)
  VISITCOMP(LE, ICMP_ULE, ICMP_SLE, FCMP_OLE, true)
  VISITCOMP(GE, ICMP_UGE, ICMP_SGE, FCMP_OGE, true)
  VISITCOMP(EQ, ICMP_EQ , ICMP_EQ , FCMP_OEQ, false)
  VISITCOMP(NE, ICMP_NE , ICMP_NE , FCMP_UNE, false)
#undef VISITCOMP

  Value *VisitBinAssign     (const BinaryOperator *E);

  Value *VisitBinLAnd       (const BinaryOperator *E);
  Value *VisitBinLOr        (const BinaryOperator *E);
  Value *VisitBinComma      (const BinaryOperator *E);

  Value *VisitBinPtrMemD(const Expr *E) { return EmitLoadOfLValue(E); }
  Value *VisitBinPtrMemI(const Expr *E) { return EmitLoadOfLValue(E); }

  Value *VisitCXXRewrittenBinaryOperator(CXXRewrittenBinaryOperator *E) {
    return Visit(E->getSemanticForm());
  }

  // Other Operators.
  Value *VisitBlockExpr(const BlockExpr *BE);
  Value *VisitAbstractConditionalOperator(const AbstractConditionalOperator *);
  Value *VisitChooseExpr(ChooseExpr *CE);
  Value *VisitVAArgExpr(VAArgExpr *VE);
  Value *VisitObjCStringLiteral(const ObjCStringLiteral *E) {
    return CGF.EmitObjCStringLiteral(E);
  }
  Value *VisitObjCBoxedExpr(ObjCBoxedExpr *E) {
    return CGF.EmitObjCBoxedExpr(E);
  }
  Value *VisitObjCArrayLiteral(ObjCArrayLiteral *E) {
    return CGF.EmitObjCArrayLiteral(E);
  }
  Value *VisitObjCDictionaryLiteral(ObjCDictionaryLiteral *E) {
    return CGF.EmitObjCDictionaryLiteral(E);
  }
  Value *VisitAsTypeExpr(AsTypeExpr *CE);
  Value *VisitAtomicExpr(AtomicExpr *AE);
  Value *VisitPackIndexingExpr(PackIndexingExpr *E) {
    return Visit(E->getSelectedExpr());
  }
};
}  // end anonymous namespace.

//===----------------------------------------------------------------------===//
//                                Utilities
//===----------------------------------------------------------------------===//

/// EmitConversionToBool - Convert the specified expression value to a
/// boolean (i1) truth value.  This is equivalent to "Val != 0".
Value *ScalarExprEmitter::EmitConversionToBool(Value *Src, QualType SrcType) {
  assert(SrcType.isCanonical() && "EmitScalarConversion strips typedefs");

  if (SrcType->isRealFloatingType())
    return EmitFloatToBoolConversion(Src);

  if (const MemberPointerType *MPT = dyn_cast<MemberPointerType>(SrcType))
    return CGF.CGM.getCXXABI().EmitMemberPointerIsNotNull(CGF, Src, MPT);

  assert((SrcType->isIntegerType() || isa<llvm::PointerType>(Src->getType())) &&
         "Unknown scalar type to convert");

  if (isa<llvm::IntegerType>(Src->getType()))
    return EmitIntToBoolConversion(Src);

  assert(isa<llvm::PointerType>(Src->getType()));
  return EmitPointerToBoolConversion(Src, SrcType);
}

void ScalarExprEmitter::EmitFloatConversionCheck(
    Value *OrigSrc, QualType OrigSrcType, Value *Src, QualType SrcType,
    QualType DstType, llvm::Type *DstTy, SourceLocation Loc) {
  assert(SrcType->isFloatingType() && "not a conversion from floating point");
  if (!isa<llvm::IntegerType>(DstTy))
    return;

  CodeGenFunction::SanitizerScope SanScope(&CGF);
  using llvm::APFloat;
  using llvm::APSInt;

  llvm::Value *Check = nullptr;
  const llvm::fltSemantics &SrcSema =
    CGF.getContext().getFloatTypeSemantics(OrigSrcType);

  // Floating-point to integer. This has undefined behavior if the source is
  // +-Inf, NaN, or doesn't fit into the destination type (after truncation
  // to an integer).
  unsigned Width = CGF.getContext().getIntWidth(DstType);
  bool Unsigned = DstType->isUnsignedIntegerOrEnumerationType();

  APSInt Min = APSInt::getMinValue(Width, Unsigned);
  APFloat MinSrc(SrcSema, APFloat::uninitialized);
  if (MinSrc.convertFromAPInt(Min, !Unsigned, APFloat::rmTowardZero) &
      APFloat::opOverflow)
    // Don't need an overflow check for lower bound. Just check for
    // -Inf/NaN.
    MinSrc = APFloat::getInf(SrcSema, true);
  else
    // Find the largest value which is too small to represent (before
    // truncation toward zero).
    MinSrc.subtract(APFloat(SrcSema, 1), APFloat::rmTowardNegative);

  APSInt Max = APSInt::getMaxValue(Width, Unsigned);
  APFloat MaxSrc(SrcSema, APFloat::uninitialized);
  if (MaxSrc.convertFromAPInt(Max, !Unsigned, APFloat::rmTowardZero) &
      APFloat::opOverflow)
    // Don't need an overflow check for upper bound. Just check for
    // +Inf/NaN.
    MaxSrc = APFloat::getInf(SrcSema, false);
  else
    // Find the smallest value which is too large to represent (before
    // truncation toward zero).
    MaxSrc.add(APFloat(SrcSema, 1), APFloat::rmTowardPositive);

  // If we're converting from __half, convert the range to float to match
  // the type of src.
  if (OrigSrcType->isHalfType()) {
    const llvm::fltSemantics &Sema =
      CGF.getContext().getFloatTypeSemantics(SrcType);
    bool IsInexact;
    MinSrc.convert(Sema, APFloat::rmTowardZero, &IsInexact);
    MaxSrc.convert(Sema, APFloat::rmTowardZero, &IsInexact);
  }

  llvm::Value *GE =
    Builder.CreateFCmpOGT(Src, llvm::ConstantFP::get(VMContext, MinSrc));
  llvm::Value *LE =
    Builder.CreateFCmpOLT(Src, llvm::ConstantFP::get(VMContext, MaxSrc));
  Check = Builder.CreateAnd(GE, LE);

  llvm::Constant *StaticArgs[] = {CGF.EmitCheckSourceLocation(Loc),
                                  CGF.EmitCheckTypeDescriptor(OrigSrcType),
                                  CGF.EmitCheckTypeDescriptor(DstType)};
  CGF.EmitCheck(std::make_pair(Check, SanitizerKind::FloatCastOverflow),
                SanitizerHandler::FloatCastOverflow, StaticArgs, OrigSrc);
}

// Should be called within CodeGenFunction::SanitizerScope RAII scope.
// Returns 'i1 false' when the truncation Src -> Dst was lossy.
static std::pair<ScalarExprEmitter::ImplicitConversionCheckKind,
                 std::pair<llvm::Value *, SanitizerMask>>
EmitIntegerTruncationCheckHelper(Value *Src, QualType SrcType, Value *Dst,
                                 QualType DstType, CGBuilderTy &Builder) {
  llvm::Type *SrcTy = Src->getType();
  llvm::Type *DstTy = Dst->getType();
  (void)DstTy; // Only used in assert()

  // This should be truncation of integral types.
  assert(Src != Dst);
  assert(SrcTy->getScalarSizeInBits() > Dst->getType()->getScalarSizeInBits());
  assert(isa<llvm::IntegerType>(SrcTy) && isa<llvm::IntegerType>(DstTy) &&
         "non-integer llvm type");

  bool SrcSigned = SrcType->isSignedIntegerOrEnumerationType();
  bool DstSigned = DstType->isSignedIntegerOrEnumerationType();

  // If both (src and dst) types are unsigned, then it's an unsigned truncation.
  // Else, it is a signed truncation.
  ScalarExprEmitter::ImplicitConversionCheckKind Kind;
  SanitizerMask Mask;
  if (!SrcSigned && !DstSigned) {
    Kind = ScalarExprEmitter::ICCK_UnsignedIntegerTruncation;
    Mask = SanitizerKind::ImplicitUnsignedIntegerTruncation;
  } else {
    Kind = ScalarExprEmitter::ICCK_SignedIntegerTruncation;
    Mask = SanitizerKind::ImplicitSignedIntegerTruncation;
  }

  llvm::Value *Check = nullptr;
  // 1. Extend the truncated value back to the same width as the Src.
  Check = Builder.CreateIntCast(Dst, SrcTy, DstSigned, "anyext");
  // 2. Equality-compare with the original source value
  Check = Builder.CreateICmpEQ(Check, Src, "truncheck");
  // If the comparison result is 'i1 false', then the truncation was lossy.
  return std::make_pair(Kind, std::make_pair(Check, Mask));
}

static bool PromotionIsPotentiallyEligibleForImplicitIntegerConversionCheck(
    QualType SrcType, QualType DstType) {
  return SrcType->isIntegerType() && DstType->isIntegerType();
}

void ScalarExprEmitter::EmitIntegerTruncationCheck(Value *Src, QualType SrcType,
                                                   Value *Dst, QualType DstType,
                                                   SourceLocation Loc) {
  if (!CGF.SanOpts.hasOneOf(SanitizerKind::ImplicitIntegerTruncation))
    return;

  // We only care about int->int conversions here.
  // We ignore conversions to/from pointer and/or bool.
  if (!PromotionIsPotentiallyEligibleForImplicitIntegerConversionCheck(SrcType,
                                                                       DstType))
    return;

  unsigned SrcBits = Src->getType()->getScalarSizeInBits();
  unsigned DstBits = Dst->getType()->getScalarSizeInBits();
  // This must be truncation. Else we do not care.
  if (SrcBits <= DstBits)
    return;

  assert(!DstType->isBooleanType() && "we should not get here with booleans.");

  // If the integer sign change sanitizer is enabled,
  // and we are truncating from larger unsigned type to smaller signed type,
  // let that next sanitizer deal with it.
  bool SrcSigned = SrcType->isSignedIntegerOrEnumerationType();
  bool DstSigned = DstType->isSignedIntegerOrEnumerationType();
  if (CGF.SanOpts.has(SanitizerKind::ImplicitIntegerSignChange) &&
      (!SrcSigned && DstSigned))
    return;

  CodeGenFunction::SanitizerScope SanScope(&CGF);

  std::pair<ScalarExprEmitter::ImplicitConversionCheckKind,
            std::pair<llvm::Value *, SanitizerMask>>
      Check =
          EmitIntegerTruncationCheckHelper(Src, SrcType, Dst, DstType, Builder);
  // If the comparison result is 'i1 false', then the truncation was lossy.

  // Do we care about this type of truncation?
  if (!CGF.SanOpts.has(Check.second.second))
    return;

  llvm::Constant *StaticArgs[] = {
      CGF.EmitCheckSourceLocation(Loc), CGF.EmitCheckTypeDescriptor(SrcType),
      CGF.EmitCheckTypeDescriptor(DstType),
      llvm::ConstantInt::get(Builder.getInt8Ty(), Check.first),
      llvm::ConstantInt::get(Builder.getInt32Ty(), 0)};

  CGF.EmitCheck(Check.second, SanitizerHandler::ImplicitConversion, StaticArgs,
                {Src, Dst});
}

static llvm::Value *EmitIsNegativeTestHelper(Value *V, QualType VType,
                                             const char *Name,
                                             CGBuilderTy &Builder) {
  bool VSigned = VType->isSignedIntegerOrEnumerationType();
  llvm::Type *VTy = V->getType();
  if (!VSigned) {
    // If the value is unsigned, then it is never negative.
    return llvm::ConstantInt::getFalse(VTy->getContext());
  }
  llvm::Constant *Zero = llvm::ConstantInt::get(VTy, 0);
  return Builder.CreateICmp(llvm::ICmpInst::ICMP_SLT, V, Zero,
                            llvm::Twine(Name) + "." + V->getName() +
                                ".negativitycheck");
}

// Should be called within CodeGenFunction::SanitizerScope RAII scope.
// Returns 'i1 false' when the conversion Src -> Dst changed the sign.
static std::pair<ScalarExprEmitter::ImplicitConversionCheckKind,
                 std::pair<llvm::Value *, SanitizerMask>>
EmitIntegerSignChangeCheckHelper(Value *Src, QualType SrcType, Value *Dst,
                                 QualType DstType, CGBuilderTy &Builder) {
  llvm::Type *SrcTy = Src->getType();
  llvm::Type *DstTy = Dst->getType();

  assert(isa<llvm::IntegerType>(SrcTy) && isa<llvm::IntegerType>(DstTy) &&
         "non-integer llvm type");

  bool SrcSigned = SrcType->isSignedIntegerOrEnumerationType();
  bool DstSigned = DstType->isSignedIntegerOrEnumerationType();
  (void)SrcSigned; // Only used in assert()
  (void)DstSigned; // Only used in assert()
  unsigned SrcBits = SrcTy->getScalarSizeInBits();
  unsigned DstBits = DstTy->getScalarSizeInBits();
  (void)SrcBits; // Only used in assert()
  (void)DstBits; // Only used in assert()

  assert(((SrcBits != DstBits) || (SrcSigned != DstSigned)) &&
         "either the widths should be different, or the signednesses.");

  // 1. Was the old Value negative?
  llvm::Value *SrcIsNegative =
      EmitIsNegativeTestHelper(Src, SrcType, "src", Builder);
  // 2. Is the new Value negative?
  llvm::Value *DstIsNegative =
      EmitIsNegativeTestHelper(Dst, DstType, "dst", Builder);
  // 3. Now, was the 'negativity status' preserved during the conversion?
  //    NOTE: conversion from negative to zero is considered to change the sign.
  //    (We want to get 'false' when the conversion changed the sign)
  //    So we should just equality-compare the negativity statuses.
  llvm::Value *Check = nullptr;
  Check = Builder.CreateICmpEQ(SrcIsNegative, DstIsNegative, "signchangecheck");
  // If the comparison result is 'false', then the conversion changed the sign.
  return std::make_pair(
      ScalarExprEmitter::ICCK_IntegerSignChange,
      std::make_pair(Check, SanitizerKind::ImplicitIntegerSignChange));
}

void ScalarExprEmitter::EmitIntegerSignChangeCheck(Value *Src, QualType SrcType,
                                                   Value *Dst, QualType DstType,
                                                   SourceLocation Loc) {
  if (!CGF.SanOpts.has(SanitizerKind::ImplicitIntegerSignChange))
    return;

  llvm::Type *SrcTy = Src->getType();
  llvm::Type *DstTy = Dst->getType();

  // We only care about int->int conversions here.
  // We ignore conversions to/from pointer and/or bool.
  if (!PromotionIsPotentiallyEligibleForImplicitIntegerConversionCheck(SrcType,
                                                                       DstType))
    return;

  bool SrcSigned = SrcType->isSignedIntegerOrEnumerationType();
  bool DstSigned = DstType->isSignedIntegerOrEnumerationType();
  unsigned SrcBits = SrcTy->getScalarSizeInBits();
  unsigned DstBits = DstTy->getScalarSizeInBits();

  // Now, we do not need to emit the check in *all* of the cases.
  // We can avoid emitting it in some obvious cases where it would have been
  // dropped by the opt passes (instcombine) always anyways.
  // If it's a cast between effectively the same type, no check.
  // NOTE: this is *not* equivalent to checking the canonical types.
  if (SrcSigned == DstSigned && SrcBits == DstBits)
    return;
  // At least one of the values needs to have signed type.
  // If both are unsigned, then obviously, neither of them can be negative.
  if (!SrcSigned && !DstSigned)
    return;
  // If the conversion is to *larger* *signed* type, then no check is needed.
  // Because either sign-extension happens (so the sign will remain),
  // or zero-extension will happen (the sign bit will be zero.)
  if ((DstBits > SrcBits) && DstSigned)
    return;
  if (CGF.SanOpts.has(SanitizerKind::ImplicitSignedIntegerTruncation) &&
      (SrcBits > DstBits) && SrcSigned) {
    // If the signed integer truncation sanitizer is enabled,
    // and this is a truncation from signed type, then no check is needed.
    // Because here sign change check is interchangeable with truncation check.
    return;
  }
  // That's it. We can't rule out any more cases with the data we have.

  CodeGenFunction::SanitizerScope SanScope(&CGF);

  std::pair<ScalarExprEmitter::ImplicitConversionCheckKind,
            std::pair<llvm::Value *, SanitizerMask>>
      Check;

  // Each of these checks needs to return 'false' when an issue was detected.
  ImplicitConversionCheckKind CheckKind;
  llvm::SmallVector<std::pair<llvm::Value *, SanitizerMask>, 2> Checks;
  // So we can 'and' all the checks together, and still get 'false',
  // if at least one of the checks detected an issue.

  Check = EmitIntegerSignChangeCheckHelper(Src, SrcType, Dst, DstType, Builder);
  CheckKind = Check.first;
  Checks.emplace_back(Check.second);

  if (CGF.SanOpts.has(SanitizerKind::ImplicitSignedIntegerTruncation) &&
      (SrcBits > DstBits) && !SrcSigned && DstSigned) {
    // If the signed integer truncation sanitizer was enabled,
    // and we are truncating from larger unsigned type to smaller signed type,
    // let's handle the case we skipped in that check.
    Check =
        EmitIntegerTruncationCheckHelper(Src, SrcType, Dst, DstType, Builder);
    CheckKind = ICCK_SignedIntegerTruncationOrSignChange;
    Checks.emplace_back(Check.second);
    // If the comparison result is 'i1 false', then the truncation was lossy.
  }

  llvm::Constant *StaticArgs[] = {
      CGF.EmitCheckSourceLocation(Loc), CGF.EmitCheckTypeDescriptor(SrcType),
      CGF.EmitCheckTypeDescriptor(DstType),
      llvm::ConstantInt::get(Builder.getInt8Ty(), CheckKind),
      llvm::ConstantInt::get(Builder.getInt32Ty(), 0)};
  // EmitCheck() will 'and' all the checks together.
  CGF.EmitCheck(Checks, SanitizerHandler::ImplicitConversion, StaticArgs,
                {Src, Dst});
}

// Should be called within CodeGenFunction::SanitizerScope RAII scope.
// Returns 'i1 false' when the truncation Src -> Dst was lossy.
static std::pair<ScalarExprEmitter::ImplicitConversionCheckKind,
                 std::pair<llvm::Value *, SanitizerMask>>
EmitBitfieldTruncationCheckHelper(Value *Src, QualType SrcType, Value *Dst,
                                  QualType DstType, CGBuilderTy &Builder) {
  bool SrcSigned = SrcType->isSignedIntegerOrEnumerationType();
  bool DstSigned = DstType->isSignedIntegerOrEnumerationType();

  ScalarExprEmitter::ImplicitConversionCheckKind Kind;
  if (!SrcSigned && !DstSigned)
    Kind = ScalarExprEmitter::ICCK_UnsignedIntegerTruncation;
  else
    Kind = ScalarExprEmitter::ICCK_SignedIntegerTruncation;

  llvm::Value *Check = nullptr;
  // 1. Extend the truncated value back to the same width as the Src.
  Check = Builder.CreateIntCast(Dst, Src->getType(), DstSigned, "bf.anyext");
  // 2. Equality-compare with the original source value
  Check = Builder.CreateICmpEQ(Check, Src, "bf.truncheck");
  // If the comparison result is 'i1 false', then the truncation was lossy.

  return std::make_pair(
      Kind, std::make_pair(Check, SanitizerKind::ImplicitBitfieldConversion));
}

// Should be called within CodeGenFunction::SanitizerScope RAII scope.
// Returns 'i1 false' when the conversion Src -> Dst changed the sign.
static std::pair<ScalarExprEmitter::ImplicitConversionCheckKind,
                 std::pair<llvm::Value *, SanitizerMask>>
EmitBitfieldSignChangeCheckHelper(Value *Src, QualType SrcType, Value *Dst,
                                  QualType DstType, CGBuilderTy &Builder) {
  // 1. Was the old Value negative?
  llvm::Value *SrcIsNegative =
      EmitIsNegativeTestHelper(Src, SrcType, "bf.src", Builder);
  // 2. Is the new Value negative?
  llvm::Value *DstIsNegative =
      EmitIsNegativeTestHelper(Dst, DstType, "bf.dst", Builder);
  // 3. Now, was the 'negativity status' preserved during the conversion?
  //    NOTE: conversion from negative to zero is considered to change the sign.
  //    (We want to get 'false' when the conversion changed the sign)
  //    So we should just equality-compare the negativity statuses.
  llvm::Value *Check = nullptr;
  Check =
      Builder.CreateICmpEQ(SrcIsNegative, DstIsNegative, "bf.signchangecheck");
  // If the comparison result is 'false', then the conversion changed the sign.
  return std::make_pair(
      ScalarExprEmitter::ICCK_IntegerSignChange,
      std::make_pair(Check, SanitizerKind::ImplicitBitfieldConversion));
}

void CodeGenFunction::EmitBitfieldConversionCheck(Value *Src, QualType SrcType,
                                                  Value *Dst, QualType DstType,
                                                  const CGBitFieldInfo &Info,
                                                  SourceLocation Loc) {

  if (!SanOpts.has(SanitizerKind::ImplicitBitfieldConversion))
    return;

  // We only care about int->int conversions here.
  // We ignore conversions to/from pointer and/or bool.
  if (!PromotionIsPotentiallyEligibleForImplicitIntegerConversionCheck(SrcType,
                                                                       DstType))
    return;

  if (DstType->isBooleanType() || SrcType->isBooleanType())
    return;

  // This should be truncation of integral types.
  assert(isa<llvm::IntegerType>(Src->getType()) &&
         isa<llvm::IntegerType>(Dst->getType()) && "non-integer llvm type");

  // TODO: Calculate src width to avoid emitting code
  // for unecessary cases.
  unsigned SrcBits = ConvertType(SrcType)->getScalarSizeInBits();
  unsigned DstBits = Info.Size;

  bool SrcSigned = SrcType->isSignedIntegerOrEnumerationType();
  bool DstSigned = DstType->isSignedIntegerOrEnumerationType();

  CodeGenFunction::SanitizerScope SanScope(this);

  std::pair<ScalarExprEmitter::ImplicitConversionCheckKind,
            std::pair<llvm::Value *, SanitizerMask>>
      Check;

  // Truncation
  bool EmitTruncation = DstBits < SrcBits;
  // If Dst is signed and Src unsigned, we want to be more specific
  // about the CheckKind we emit, in this case we want to emit
  // ICCK_SignedIntegerTruncationOrSignChange.
  bool EmitTruncationFromUnsignedToSigned =
      EmitTruncation && DstSigned && !SrcSigned;
  // Sign change
  bool SameTypeSameSize = SrcSigned == DstSigned && SrcBits == DstBits;
  bool BothUnsigned = !SrcSigned && !DstSigned;
  bool LargerSigned = (DstBits > SrcBits) && DstSigned;
  // We can avoid emitting sign change checks in some obvious cases
  //   1. If Src and Dst have the same signedness and size
  //   2. If both are unsigned sign check is unecessary!
  //   3. If Dst is signed and bigger than Src, either
  //      sign-extension or zero-extension will make sure
  //      the sign remains.
  bool EmitSignChange = !SameTypeSameSize && !BothUnsigned && !LargerSigned;

  if (EmitTruncation)
    Check =
        EmitBitfieldTruncationCheckHelper(Src, SrcType, Dst, DstType, Builder);
  else if (EmitSignChange) {
    assert(((SrcBits != DstBits) || (SrcSigned != DstSigned)) &&
           "either the widths should be different, or the signednesses.");
    Check =
        EmitBitfieldSignChangeCheckHelper(Src, SrcType, Dst, DstType, Builder);
  } else
    return;

  ScalarExprEmitter::ImplicitConversionCheckKind CheckKind = Check.first;
  if (EmitTruncationFromUnsignedToSigned)
    CheckKind = ScalarExprEmitter::ICCK_SignedIntegerTruncationOrSignChange;

  llvm::Constant *StaticArgs[] = {
      EmitCheckSourceLocation(Loc), EmitCheckTypeDescriptor(SrcType),
      EmitCheckTypeDescriptor(DstType),
      llvm::ConstantInt::get(Builder.getInt8Ty(), CheckKind),
      llvm::ConstantInt::get(Builder.getInt32Ty(), Info.Size)};

  EmitCheck(Check.second, SanitizerHandler::ImplicitConversion, StaticArgs,
            {Src, Dst});
}

Value *ScalarExprEmitter::EmitScalarCast(Value *Src, QualType SrcType,
                                         QualType DstType, llvm::Type *SrcTy,
                                         llvm::Type *DstTy,
                                         ScalarConversionOpts Opts) {
  // The Element types determine the type of cast to perform.
  llvm::Type *SrcElementTy;
  llvm::Type *DstElementTy;
  QualType SrcElementType;
  QualType DstElementType;
  if (SrcType->isMatrixType() && DstType->isMatrixType()) {
    SrcElementTy = cast<llvm::VectorType>(SrcTy)->getElementType();
    DstElementTy = cast<llvm::VectorType>(DstTy)->getElementType();
    SrcElementType = SrcType->castAs<MatrixType>()->getElementType();
    DstElementType = DstType->castAs<MatrixType>()->getElementType();
  } else {
    assert(!SrcType->isMatrixType() && !DstType->isMatrixType() &&
           "cannot cast between matrix and non-matrix types");
    SrcElementTy = SrcTy;
    DstElementTy = DstTy;
    SrcElementType = SrcType;
    DstElementType = DstType;
  }

  if (isa<llvm::IntegerType>(SrcElementTy)) {
    bool InputSigned = SrcElementType->isSignedIntegerOrEnumerationType();
    if (SrcElementType->isBooleanType() && Opts.TreatBooleanAsSigned) {
      InputSigned = true;
    }

    if (isa<llvm::IntegerType>(DstElementTy))
      return Builder.CreateIntCast(Src, DstTy, InputSigned, "conv");
    if (InputSigned)
      return Builder.CreateSIToFP(Src, DstTy, "conv");
    return Builder.CreateUIToFP(Src, DstTy, "conv");
  }

  if (isa<llvm::IntegerType>(DstElementTy)) {
    assert(SrcElementTy->isFloatingPointTy() && "Unknown real conversion");
    bool IsSigned = DstElementType->isSignedIntegerOrEnumerationType();

    // If we can't recognize overflow as undefined behavior, assume that
    // overflow saturates. This protects against normal optimizations if we are
    // compiling with non-standard FP semantics.
    if (!CGF.CGM.getCodeGenOpts().StrictFloatCastOverflow) {
      llvm::Intrinsic::ID IID =
          IsSigned ? llvm::Intrinsic::fptosi_sat : llvm::Intrinsic::fptoui_sat;
      return Builder.CreateCall(CGF.CGM.getIntrinsic(IID, {DstTy, SrcTy}), Src);
    }

    if (IsSigned)
      return Builder.CreateFPToSI(Src, DstTy, "conv");
    return Builder.CreateFPToUI(Src, DstTy, "conv");
  }

  if (DstElementTy->getTypeID() < SrcElementTy->getTypeID())
    return Builder.CreateFPTrunc(Src, DstTy, "conv");
  return Builder.CreateFPExt(Src, DstTy, "conv");
}

/// Emit a conversion from the specified type to the specified destination type,
/// both of which are LLVM scalar types.
Value *ScalarExprEmitter::EmitScalarConversion(Value *Src, QualType SrcType,
                                               QualType DstType,
                                               SourceLocation Loc,
                                               ScalarConversionOpts Opts) {
  // All conversions involving fixed point types should be handled by the
  // EmitFixedPoint family functions. This is done to prevent bloating up this
  // function more, and although fixed point numbers are represented by
  // integers, we do not want to follow any logic that assumes they should be
  // treated as integers.
  // TODO(leonardchan): When necessary, add another if statement checking for
  // conversions to fixed point types from other types.
  if (SrcType->isFixedPointType()) {
    if (DstType->isBooleanType())
      // It is important that we check this before checking if the dest type is
      // an integer because booleans are technically integer types.
      // We do not need to check the padding bit on unsigned types if unsigned
      // padding is enabled because overflow into this bit is undefined
      // behavior.
      return Builder.CreateIsNotNull(Src, "tobool");
    if (DstType->isFixedPointType() || DstType->isIntegerType() ||
        DstType->isRealFloatingType())
      return EmitFixedPointConversion(Src, SrcType, DstType, Loc);

    llvm_unreachable(
        "Unhandled scalar conversion from a fixed point type to another type.");
  } else if (DstType->isFixedPointType()) {
    if (SrcType->isIntegerType() || SrcType->isRealFloatingType())
      // This also includes converting booleans and enums to fixed point types.
      return EmitFixedPointConversion(Src, SrcType, DstType, Loc);

    llvm_unreachable(
        "Unhandled scalar conversion to a fixed point type from another type.");
  }

  QualType NoncanonicalSrcType = SrcType;
  QualType NoncanonicalDstType = DstType;

  SrcType = CGF.getContext().getCanonicalType(SrcType);
  DstType = CGF.getContext().getCanonicalType(DstType);
  if (SrcType == DstType) return Src;

  if (DstType->isVoidType()) return nullptr;

  llvm::Value *OrigSrc = Src;
  QualType OrigSrcType = SrcType;
  llvm::Type *SrcTy = Src->getType();

  // Handle conversions to bool first, they are special: comparisons against 0.
  if (DstType->isBooleanType())
    return EmitConversionToBool(Src, SrcType);

  llvm::Type *DstTy = ConvertType(DstType);

  // Cast from half through float if half isn't a native type.
  if (SrcType->isHalfType() && !CGF.getContext().getLangOpts().NativeHalfType) {
    // Cast to FP using the intrinsic if the half type itself isn't supported.
    if (DstTy->isFloatingPointTy()) {
      if (CGF.getContext().getTargetInfo().useFP16ConversionIntrinsics())
        return Builder.CreateCall(
            CGF.CGM.getIntrinsic(llvm::Intrinsic::convert_from_fp16, DstTy),
            Src);
    } else {
      // Cast to other types through float, using either the intrinsic or FPExt,
      // depending on whether the half type itself is supported
      // (as opposed to operations on half, available with NativeHalfType).
      if (CGF.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
        Src = Builder.CreateCall(
            CGF.CGM.getIntrinsic(llvm::Intrinsic::convert_from_fp16,
                                 CGF.CGM.FloatTy),
            Src);
      } else {
        Src = Builder.CreateFPExt(Src, CGF.CGM.FloatTy, "conv");
      }
      SrcType = CGF.getContext().FloatTy;
      SrcTy = CGF.FloatTy;
    }
  }

  // Ignore conversions like int -> uint.
  if (SrcTy == DstTy) {
    if (Opts.EmitImplicitIntegerSignChangeChecks)
      EmitIntegerSignChangeCheck(Src, NoncanonicalSrcType, Src,
                                 NoncanonicalDstType, Loc);

    return Src;
  }

  // Handle pointer conversions next: pointers can only be converted to/from
  // other pointers and integers. Check for pointer types in terms of LLVM, as
  // some native types (like Obj-C id) may map to a pointer type.
  if (auto DstPT = dyn_cast<llvm::PointerType>(DstTy)) {
    // The source value may be an integer, or a pointer.
    if (isa<llvm::PointerType>(SrcTy))
      return Src;

    assert(SrcType->isIntegerType() && "Not ptr->ptr or int->ptr conversion?");
    // First, convert to the correct width so that we control the kind of
    // extension.
    llvm::Type *MiddleTy = CGF.CGM.getDataLayout().getIntPtrType(DstPT);
    bool InputSigned = SrcType->isSignedIntegerOrEnumerationType();
    llvm::Value* IntResult =
        Builder.CreateIntCast(Src, MiddleTy, InputSigned, "conv");
    // Then, cast to pointer.
    return Builder.CreateIntToPtr(IntResult, DstTy, "conv");
  }

  if (isa<llvm::PointerType>(SrcTy)) {
    // Must be an ptr to int cast.
    assert(isa<llvm::IntegerType>(DstTy) && "not ptr->int?");
    return Builder.CreatePtrToInt(Src, DstTy, "conv");
  }

  // A scalar can be splatted to an extended vector of the same element type
  if (DstType->isExtVectorType() && !SrcType->isVectorType()) {
    // Sema should add casts to make sure that the source expression's type is
    // the same as the vector's element type (sans qualifiers)
    assert(DstType->castAs<ExtVectorType>()->getElementType().getTypePtr() ==
               SrcType.getTypePtr() &&
           "Splatted expr doesn't match with vector element type?");

    // Splat the element across to all elements
    unsigned NumElements = cast<llvm::FixedVectorType>(DstTy)->getNumElements();
    return Builder.CreateVectorSplat(NumElements, Src, "splat");
  }

  if (SrcType->isMatrixType() && DstType->isMatrixType())
    return EmitScalarCast(Src, SrcType, DstType, SrcTy, DstTy, Opts);

  if (isa<llvm::VectorType>(SrcTy) || isa<llvm::VectorType>(DstTy)) {
    // Allow bitcast from vector to integer/fp of the same size.
    llvm::TypeSize SrcSize = SrcTy->getPrimitiveSizeInBits();
    llvm::TypeSize DstSize = DstTy->getPrimitiveSizeInBits();
    if (SrcSize == DstSize)
      return Builder.CreateBitCast(Src, DstTy, "conv");

    // Conversions between vectors of different sizes are not allowed except
    // when vectors of half are involved. Operations on storage-only half
    // vectors require promoting half vector operands to float vectors and
    // truncating the result, which is either an int or float vector, to a
    // short or half vector.

    // Source and destination are both expected to be vectors.
    llvm::Type *SrcElementTy = cast<llvm::VectorType>(SrcTy)->getElementType();
    llvm::Type *DstElementTy = cast<llvm::VectorType>(DstTy)->getElementType();
    (void)DstElementTy;

    assert(((SrcElementTy->isIntegerTy() &&
             DstElementTy->isIntegerTy()) ||
            (SrcElementTy->isFloatingPointTy() &&
             DstElementTy->isFloatingPointTy())) &&
           "unexpected conversion between a floating-point vector and an "
           "integer vector");

    // Truncate an i32 vector to an i16 vector.
    if (SrcElementTy->isIntegerTy())
      return Builder.CreateIntCast(Src, DstTy, false, "conv");

    // Truncate a float vector to a half vector.
    if (SrcSize > DstSize)
      return Builder.CreateFPTrunc(Src, DstTy, "conv");

    // Promote a half vector to a float vector.
    return Builder.CreateFPExt(Src, DstTy, "conv");
  }

  // Finally, we have the arithmetic types: real int/float.
  Value *Res = nullptr;
  llvm::Type *ResTy = DstTy;

  // An overflowing conversion has undefined behavior if either the source type
  // or the destination type is a floating-point type. However, we consider the
  // range of representable values for all floating-point types to be
  // [-inf,+inf], so no overflow can ever happen when the destination type is a
  // floating-point type.
  if (CGF.SanOpts.has(SanitizerKind::FloatCastOverflow) &&
      OrigSrcType->isFloatingType())
    EmitFloatConversionCheck(OrigSrc, OrigSrcType, Src, SrcType, DstType, DstTy,
                             Loc);

  // Cast to half through float if half isn't a native type.
  if (DstType->isHalfType() && !CGF.getContext().getLangOpts().NativeHalfType) {
    // Make sure we cast in a single step if from another FP type.
    if (SrcTy->isFloatingPointTy()) {
      // Use the intrinsic if the half type itself isn't supported
      // (as opposed to operations on half, available with NativeHalfType).
      if (CGF.getContext().getTargetInfo().useFP16ConversionIntrinsics())
        return Builder.CreateCall(
            CGF.CGM.getIntrinsic(llvm::Intrinsic::convert_to_fp16, SrcTy), Src);
      // If the half type is supported, just use an fptrunc.
      return Builder.CreateFPTrunc(Src, DstTy);
    }
    DstTy = CGF.FloatTy;
  }

  Res = EmitScalarCast(Src, SrcType, DstType, SrcTy, DstTy, Opts);

  if (DstTy != ResTy) {
    if (CGF.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
      assert(ResTy->isIntegerTy(16) && "Only half FP requires extra conversion");
      Res = Builder.CreateCall(
        CGF.CGM.getIntrinsic(llvm::Intrinsic::convert_to_fp16, CGF.CGM.FloatTy),
        Res);
    } else {
      Res = Builder.CreateFPTrunc(Res, ResTy, "conv");
    }
  }

  if (Opts.EmitImplicitIntegerTruncationChecks)
    EmitIntegerTruncationCheck(Src, NoncanonicalSrcType, Res,
                               NoncanonicalDstType, Loc);

  if (Opts.EmitImplicitIntegerSignChangeChecks)
    EmitIntegerSignChangeCheck(Src, NoncanonicalSrcType, Res,
                               NoncanonicalDstType, Loc);

  return Res;
}

Value *ScalarExprEmitter::EmitFixedPointConversion(Value *Src, QualType SrcTy,
                                                   QualType DstTy,
                                                   SourceLocation Loc) {
  llvm::FixedPointBuilder<CGBuilderTy> FPBuilder(Builder);
  llvm::Value *Result;
  if (SrcTy->isRealFloatingType())
    Result = FPBuilder.CreateFloatingToFixed(Src,
        CGF.getContext().getFixedPointSemantics(DstTy));
  else if (DstTy->isRealFloatingType())
    Result = FPBuilder.CreateFixedToFloating(Src,
        CGF.getContext().getFixedPointSemantics(SrcTy),
        ConvertType(DstTy));
  else {
    auto SrcFPSema = CGF.getContext().getFixedPointSemantics(SrcTy);
    auto DstFPSema = CGF.getContext().getFixedPointSemantics(DstTy);

    if (DstTy->isIntegerType())
      Result = FPBuilder.CreateFixedToInteger(Src, SrcFPSema,
                                              DstFPSema.getWidth(),
                                              DstFPSema.isSigned());
    else if (SrcTy->isIntegerType())
      Result =  FPBuilder.CreateIntegerToFixed(Src, SrcFPSema.isSigned(),
                                               DstFPSema);
    else
      Result = FPBuilder.CreateFixedToFixed(Src, SrcFPSema, DstFPSema);
  }
  return Result;
}

/// Emit a conversion from the specified complex type to the specified
/// destination type, where the destination type is an LLVM scalar type.
Value *ScalarExprEmitter::EmitComplexToScalarConversion(
    CodeGenFunction::ComplexPairTy Src, QualType SrcTy, QualType DstTy,
    SourceLocation Loc) {
  // Get the source element type.
  SrcTy = SrcTy->castAs<ComplexType>()->getElementType();

  // Handle conversions to bool first, they are special: comparisons against 0.
  if (DstTy->isBooleanType()) {
    //  Complex != 0  -> (Real != 0) | (Imag != 0)
    Src.first = EmitScalarConversion(Src.first, SrcTy, DstTy, Loc);
    Src.second = EmitScalarConversion(Src.second, SrcTy, DstTy, Loc);
    return Builder.CreateOr(Src.first, Src.second, "tobool");
  }

  // C99 6.3.1.7p2: "When a value of complex type is converted to a real type,
  // the imaginary part of the complex value is discarded and the value of the
  // real part is converted according to the conversion rules for the
  // corresponding real type.
  return EmitScalarConversion(Src.first, SrcTy, DstTy, Loc);
}

Value *ScalarExprEmitter::EmitNullValue(QualType Ty) {
  return CGF.EmitFromMemory(CGF.CGM.EmitNullConstant(Ty), Ty);
}

/// Emit a sanitization check for the given "binary" operation (which
/// might actually be a unary increment which has been lowered to a binary
/// operation). The check passes if all values in \p Checks (which are \c i1),
/// are \c true.
void ScalarExprEmitter::EmitBinOpCheck(
    ArrayRef<std::pair<Value *, SanitizerMask>> Checks, const BinOpInfo &Info) {
  assert(CGF.IsSanitizerScope);
  SanitizerHandler Check;
  SmallVector<llvm::Constant *, 4> StaticData;
  SmallVector<llvm::Value *, 2> DynamicData;

  BinaryOperatorKind Opcode = Info.Opcode;
  if (BinaryOperator::isCompoundAssignmentOp(Opcode))
    Opcode = BinaryOperator::getOpForCompoundAssignment(Opcode);

  StaticData.push_back(CGF.EmitCheckSourceLocation(Info.E->getExprLoc()));
  const UnaryOperator *UO = dyn_cast<UnaryOperator>(Info.E);
  if (UO && UO->getOpcode() == UO_Minus) {
    Check = SanitizerHandler::NegateOverflow;
    StaticData.push_back(CGF.EmitCheckTypeDescriptor(UO->getType()));
    DynamicData.push_back(Info.RHS);
  } else {
    if (BinaryOperator::isShiftOp(Opcode)) {
      // Shift LHS negative or too large, or RHS out of bounds.
      Check = SanitizerHandler::ShiftOutOfBounds;
      const BinaryOperator *BO = cast<BinaryOperator>(Info.E);
      StaticData.push_back(
        CGF.EmitCheckTypeDescriptor(BO->getLHS()->getType()));
      StaticData.push_back(
        CGF.EmitCheckTypeDescriptor(BO->getRHS()->getType()));
    } else if (Opcode == BO_Div || Opcode == BO_Rem) {
      // Divide or modulo by zero, or signed overflow (eg INT_MAX / -1).
      Check = SanitizerHandler::DivremOverflow;
      StaticData.push_back(CGF.EmitCheckTypeDescriptor(Info.Ty));
    } else {
      // Arithmetic overflow (+, -, *).
      switch (Opcode) {
      case BO_Add: Check = SanitizerHandler::AddOverflow; break;
      case BO_Sub: Check = SanitizerHandler::SubOverflow; break;
      case BO_Mul: Check = SanitizerHandler::MulOverflow; break;
      default: llvm_unreachable("unexpected opcode for bin op check");
      }
      StaticData.push_back(CGF.EmitCheckTypeDescriptor(Info.Ty));
    }
    DynamicData.push_back(Info.LHS);
    DynamicData.push_back(Info.RHS);
  }

  CGF.EmitCheck(Checks, Check, StaticData, DynamicData);
}

//===----------------------------------------------------------------------===//
//                            Visitor Methods
//===----------------------------------------------------------------------===//

Value *ScalarExprEmitter::VisitExpr(Expr *E) {
  CGF.ErrorUnsupported(E, "scalar expression");
  if (E->getType()->isVoidType())
    return nullptr;
  return llvm::UndefValue::get(CGF.ConvertType(E->getType()));
}

Value *
ScalarExprEmitter::VisitSYCLUniqueStableNameExpr(SYCLUniqueStableNameExpr *E) {
  ASTContext &Context = CGF.getContext();
  unsigned AddrSpace =
      Context.getTargetAddressSpace(CGF.CGM.GetGlobalConstantAddressSpace());
  llvm::Constant *GlobalConstStr = Builder.CreateGlobalStringPtr(
      E->ComputeName(Context), "__usn_str", AddrSpace);

  llvm::Type *ExprTy = ConvertType(E->getType());
  return Builder.CreatePointerBitCastOrAddrSpaceCast(GlobalConstStr, ExprTy,
                                                     "usn_addr_cast");
}

Value *ScalarExprEmitter::VisitEmbedExpr(EmbedExpr *E) {
  assert(E->getDataElementCount() == 1);
  auto It = E->begin();
  return Builder.getInt((*It)->getValue());
}

Value *ScalarExprEmitter::VisitShuffleVectorExpr(ShuffleVectorExpr *E) {
  // Vector Mask Case
  if (E->getNumSubExprs() == 2) {
    Value *LHS = CGF.EmitScalarExpr(E->getExpr(0));
    Value *RHS = CGF.EmitScalarExpr(E->getExpr(1));
    Value *Mask;

    auto *LTy = cast<llvm::FixedVectorType>(LHS->getType());
    unsigned LHSElts = LTy->getNumElements();

    Mask = RHS;

    auto *MTy = cast<llvm::FixedVectorType>(Mask->getType());

    // Mask off the high bits of each shuffle index.
    Value *MaskBits =
        llvm::ConstantInt::get(MTy, llvm::NextPowerOf2(LHSElts - 1) - 1);
    Mask = Builder.CreateAnd(Mask, MaskBits, "mask");

    // newv = undef
    // mask = mask & maskbits
    // for each elt
    //   n = extract mask i
    //   x = extract val n
    //   newv = insert newv, x, i
    auto *RTy = llvm::FixedVectorType::get(LTy->getElementType(),
                                           MTy->getNumElements());
    Value* NewV = llvm::PoisonValue::get(RTy);
    for (unsigned i = 0, e = MTy->getNumElements(); i != e; ++i) {
      Value *IIndx = llvm::ConstantInt::get(CGF.SizeTy, i);
      Value *Indx = Builder.CreateExtractElement(Mask, IIndx, "shuf_idx");

      Value *VExt = Builder.CreateExtractElement(LHS, Indx, "shuf_elt");
      NewV = Builder.CreateInsertElement(NewV, VExt, IIndx, "shuf_ins");
    }
    return NewV;
  }

  Value* V1 = CGF.EmitScalarExpr(E->getExpr(0));
  Value* V2 = CGF.EmitScalarExpr(E->getExpr(1));

  SmallVector<int, 32> Indices;
  for (unsigned i = 2; i < E->getNumSubExprs(); ++i) {
    llvm::APSInt Idx = E->getShuffleMaskIdx(CGF.getContext(), i-2);
    // Check for -1 and output it as undef in the IR.
    if (Idx.isSigned() && Idx.isAllOnes())
      Indices.push_back(-1);
    else
      Indices.push_back(Idx.getZExtValue());
  }

  return Builder.CreateShuffleVector(V1, V2, Indices, "shuffle");
}

Value *ScalarExprEmitter::VisitConvertVectorExpr(ConvertVectorExpr *E) {
  QualType SrcType = E->getSrcExpr()->getType(),
           DstType = E->getType();

  Value *Src  = CGF.EmitScalarExpr(E->getSrcExpr());

  SrcType = CGF.getContext().getCanonicalType(SrcType);
  DstType = CGF.getContext().getCanonicalType(DstType);
  if (SrcType == DstType) return Src;

  assert(SrcType->isVectorType() &&
         "ConvertVector source type must be a vector");
  assert(DstType->isVectorType() &&
         "ConvertVector destination type must be a vector");

  llvm::Type *SrcTy = Src->getType();
  llvm::Type *DstTy = ConvertType(DstType);

  // Ignore conversions like int -> uint.
  if (SrcTy == DstTy)
    return Src;

  QualType SrcEltType = SrcType->castAs<VectorType>()->getElementType(),
           DstEltType = DstType->castAs<VectorType>()->getElementType();

  assert(SrcTy->isVectorTy() &&
         "ConvertVector source IR type must be a vector");
  assert(DstTy->isVectorTy() &&
         "ConvertVector destination IR type must be a vector");

  llvm::Type *SrcEltTy = cast<llvm::VectorType>(SrcTy)->getElementType(),
             *DstEltTy = cast<llvm::VectorType>(DstTy)->getElementType();

  if (DstEltType->isBooleanType()) {
    assert((SrcEltTy->isFloatingPointTy() ||
            isa<llvm::IntegerType>(SrcEltTy)) && "Unknown boolean conversion");

    llvm::Value *Zero = llvm::Constant::getNullValue(SrcTy);
    if (SrcEltTy->isFloatingPointTy()) {
      return Builder.CreateFCmpUNE(Src, Zero, "tobool");
    } else {
      return Builder.CreateICmpNE(Src, Zero, "tobool");
    }
  }

  // We have the arithmetic types: real int/float.
  Value *Res = nullptr;

  if (isa<llvm::IntegerType>(SrcEltTy)) {
    bool InputSigned = SrcEltType->isSignedIntegerOrEnumerationType();
    if (isa<llvm::IntegerType>(DstEltTy))
      Res = Builder.CreateIntCast(Src, DstTy, InputSigned, "conv");
    else if (InputSigned)
      Res = Builder.CreateSIToFP(Src, DstTy, "conv");
    else
      Res = Builder.CreateUIToFP(Src, DstTy, "conv");
  } else if (isa<llvm::IntegerType>(DstEltTy)) {
    assert(SrcEltTy->isFloatingPointTy() && "Unknown real conversion");
    if (DstEltType->isSignedIntegerOrEnumerationType())
      Res = Builder.CreateFPToSI(Src, DstTy, "conv");
    else
      Res = Builder.CreateFPToUI(Src, DstTy, "conv");
  } else {
    assert(SrcEltTy->isFloatingPointTy() && DstEltTy->isFloatingPointTy() &&
           "Unknown real conversion");
    if (DstEltTy->getTypeID() < SrcEltTy->getTypeID())
      Res = Builder.CreateFPTrunc(Src, DstTy, "conv");
    else
      Res = Builder.CreateFPExt(Src, DstTy, "conv");
  }

  return Res;
}

Value *ScalarExprEmitter::VisitMemberExpr(MemberExpr *E) {
  if (CodeGenFunction::ConstantEmission Constant = CGF.tryEmitAsConstant(E)) {
    CGF.EmitIgnoredExpr(E->getBase());
    return CGF.emitScalarConstant(Constant, E);
  } else {
    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, CGF.getContext(), Expr::SE_AllowSideEffects)) {
      llvm::APSInt Value = Result.Val.getInt();
      CGF.EmitIgnoredExpr(E->getBase());
      return Builder.getInt(Value);
    }
  }

  llvm::Value *Result = EmitLoadOfLValue(E);

  // If -fdebug-info-for-profiling is specified, emit a pseudo variable and its
  // debug info for the pointer, even if there is no variable associated with
  // the pointer's expression.
  if (CGF.CGM.getCodeGenOpts().DebugInfoForProfiling && CGF.getDebugInfo()) {
    if (llvm::LoadInst *Load = dyn_cast<llvm::LoadInst>(Result)) {
      if (llvm::GetElementPtrInst *GEP =
              dyn_cast<llvm::GetElementPtrInst>(Load->getPointerOperand())) {
        if (llvm::Instruction *Pointer =
                dyn_cast<llvm::Instruction>(GEP->getPointerOperand())) {
          QualType Ty = E->getBase()->getType();
          if (!E->isArrow())
            Ty = CGF.getContext().getPointerType(Ty);
          CGF.getDebugInfo()->EmitPseudoVariable(Builder, Pointer, Ty);
        }
      }
    }
  }
  return Result;
}

Value *ScalarExprEmitter::VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
  TestAndClearIgnoreResultAssign();

  // Emit subscript expressions in rvalue context's.  For most cases, this just
  // loads the lvalue formed by the subscript expr.  However, we have to be
  // careful, because the base of a vector subscript is occasionally an rvalue,
  // so we can't get it as an lvalue.
  if (!E->getBase()->getType()->isVectorType() &&
      !E->getBase()->getType()->isSveVLSBuiltinType())
    return EmitLoadOfLValue(E);

  // Handle the vector case.  The base must be a vector, the index must be an
  // integer value.
  Value *Base = Visit(E->getBase());
  Value *Idx  = Visit(E->getIdx());
  QualType IdxTy = E->getIdx()->getType();

  if (CGF.SanOpts.has(SanitizerKind::ArrayBounds))
    CGF.EmitBoundsCheck(E, E->getBase(), Idx, IdxTy, /*Accessed*/true);

  return Builder.CreateExtractElement(Base, Idx, "vecext");
}

Value *ScalarExprEmitter::VisitMatrixSubscriptExpr(MatrixSubscriptExpr *E) {
  TestAndClearIgnoreResultAssign();

  // Handle the vector case.  The base must be a vector, the index must be an
  // integer value.
  Value *RowIdx = Visit(E->getRowIdx());
  Value *ColumnIdx = Visit(E->getColumnIdx());

  const auto *MatrixTy = E->getBase()->getType()->castAs<ConstantMatrixType>();
  unsigned NumRows = MatrixTy->getNumRows();
  llvm::MatrixBuilder MB(Builder);
  Value *Idx = MB.CreateIndex(RowIdx, ColumnIdx, NumRows);
  if (CGF.CGM.getCodeGenOpts().OptimizationLevel > 0)
    MB.CreateIndexAssumption(Idx, MatrixTy->getNumElementsFlattened());

  Value *Matrix = Visit(E->getBase());

  // TODO: Should we emit bounds checks with SanitizerKind::ArrayBounds?
  return Builder.CreateExtractElement(Matrix, Idx, "matrixext");
}

static int getMaskElt(llvm::ShuffleVectorInst *SVI, unsigned Idx,
                      unsigned Off) {
  int MV = SVI->getMaskValue(Idx);
  if (MV == -1)
    return -1;
  return Off + MV;
}

static int getAsInt32(llvm::ConstantInt *C, llvm::Type *I32Ty) {
  assert(llvm::ConstantInt::isValueValidForType(I32Ty, C->getZExtValue()) &&
         "Index operand too large for shufflevector mask!");
  return C->getZExtValue();
}

Value *ScalarExprEmitter::VisitInitListExpr(InitListExpr *E) {
  bool Ignore = TestAndClearIgnoreResultAssign();
  (void)Ignore;
  assert (Ignore == false && "init list ignored");
  unsigned NumInitElements = E->getNumInits();

  if (E->hadArrayRangeDesignator())
    CGF.ErrorUnsupported(E, "GNU array range designator extension");

  llvm::VectorType *VType =
    dyn_cast<llvm::VectorType>(ConvertType(E->getType()));

  if (!VType) {
    if (NumInitElements == 0) {
      // C++11 value-initialization for the scalar.
      return EmitNullValue(E->getType());
    }
    // We have a scalar in braces. Just use the first element.
    return Visit(E->getInit(0));
  }

  if (isa<llvm::ScalableVectorType>(VType)) {
    if (NumInitElements == 0) {
      // C++11 value-initialization for the vector.
      return EmitNullValue(E->getType());
    }

    if (NumInitElements == 1) {
      Expr *InitVector = E->getInit(0);

      // Initialize from another scalable vector of the same type.
      if (InitVector->getType() == E->getType())
        return Visit(InitVector);
    }

    llvm_unreachable("Unexpected initialization of a scalable vector!");
  }

  unsigned ResElts = cast<llvm::FixedVectorType>(VType)->getNumElements();

  // Loop over initializers collecting the Value for each, and remembering
  // whether the source was swizzle (ExtVectorElementExpr).  This will allow
  // us to fold the shuffle for the swizzle into the shuffle for the vector
  // initializer, since LLVM optimizers generally do not want to touch
  // shuffles.
  unsigned CurIdx = 0;
  bool VIsPoisonShuffle = false;
  llvm::Value *V = llvm::PoisonValue::get(VType);
  for (unsigned i = 0; i != NumInitElements; ++i) {
    Expr *IE = E->getInit(i);
    Value *Init = Visit(IE);
    SmallVector<int, 16> Args;

    llvm::VectorType *VVT = dyn_cast<llvm::VectorType>(Init->getType());

    // Handle scalar elements.  If the scalar initializer is actually one
    // element of a different vector of the same width, use shuffle instead of
    // extract+insert.
    if (!VVT) {
      if (isa<ExtVectorElementExpr>(IE)) {
        llvm::ExtractElementInst *EI = cast<llvm::ExtractElementInst>(Init);

        if (cast<llvm::FixedVectorType>(EI->getVectorOperandType())
                ->getNumElements() == ResElts) {
          llvm::ConstantInt *C = cast<llvm::ConstantInt>(EI->getIndexOperand());
          Value *LHS = nullptr, *RHS = nullptr;
          if (CurIdx == 0) {
            // insert into poison -> shuffle (src, poison)
            // shufflemask must use an i32
            Args.push_back(getAsInt32(C, CGF.Int32Ty));
            Args.resize(ResElts, -1);

            LHS = EI->getVectorOperand();
            RHS = V;
            VIsPoisonShuffle = true;
          } else if (VIsPoisonShuffle) {
            // insert into poison shuffle && size match -> shuffle (v, src)
            llvm::ShuffleVectorInst *SVV = cast<llvm::ShuffleVectorInst>(V);
            for (unsigned j = 0; j != CurIdx; ++j)
              Args.push_back(getMaskElt(SVV, j, 0));
            Args.push_back(ResElts + C->getZExtValue());
            Args.resize(ResElts, -1);

            LHS = cast<llvm::ShuffleVectorInst>(V)->getOperand(0);
            RHS = EI->getVectorOperand();
            VIsPoisonShuffle = false;
          }
          if (!Args.empty()) {
            V = Builder.CreateShuffleVector(LHS, RHS, Args);
            ++CurIdx;
            continue;
          }
        }
      }
      V = Builder.CreateInsertElement(V, Init, Builder.getInt32(CurIdx),
                                      "vecinit");
      VIsPoisonShuffle = false;
      ++CurIdx;
      continue;
    }

    unsigned InitElts = cast<llvm::FixedVectorType>(VVT)->getNumElements();

    // If the initializer is an ExtVecEltExpr (a swizzle), and the swizzle's
    // input is the same width as the vector being constructed, generate an
    // optimized shuffle of the swizzle input into the result.
    unsigned Offset = (CurIdx == 0) ? 0 : ResElts;
    if (isa<ExtVectorElementExpr>(IE)) {
      llvm::ShuffleVectorInst *SVI = cast<llvm::ShuffleVectorInst>(Init);
      Value *SVOp = SVI->getOperand(0);
      auto *OpTy = cast<llvm::FixedVectorType>(SVOp->getType());

      if (OpTy->getNumElements() == ResElts) {
        for (unsigned j = 0; j != CurIdx; ++j) {
          // If the current vector initializer is a shuffle with poison, merge
          // this shuffle directly into it.
          if (VIsPoisonShuffle) {
            Args.push_back(getMaskElt(cast<llvm::ShuffleVectorInst>(V), j, 0));
          } else {
            Args.push_back(j);
          }
        }
        for (unsigned j = 0, je = InitElts; j != je; ++j)
          Args.push_back(getMaskElt(SVI, j, Offset));
        Args.resize(ResElts, -1);

        if (VIsPoisonShuffle)
          V = cast<llvm::ShuffleVectorInst>(V)->getOperand(0);

        Init = SVOp;
      }
    }

    // Extend init to result vector length, and then shuffle its contribution
    // to the vector initializer into V.
    if (Args.empty()) {
      for (unsigned j = 0; j != InitElts; ++j)
        Args.push_back(j);
      Args.resize(ResElts, -1);
      Init = Builder.CreateShuffleVector(Init, Args, "vext");

      Args.clear();
      for (unsigned j = 0; j != CurIdx; ++j)
        Args.push_back(j);
      for (unsigned j = 0; j != InitElts; ++j)
        Args.push_back(j + Offset);
      Args.resize(ResElts, -1);
    }

    // If V is poison, make sure it ends up on the RHS of the shuffle to aid
    // merging subsequent shuffles into this one.
    if (CurIdx == 0)
      std::swap(V, Init);
    V = Builder.CreateShuffleVector(V, Init, Args, "vecinit");
    VIsPoisonShuffle = isa<llvm::PoisonValue>(Init);
    CurIdx += InitElts;
  }

  // FIXME: evaluate codegen vs. shuffling against constant null vector.
  // Emit remaining default initializers.
  llvm::Type *EltTy = VType->getElementType();

  // Emit remaining default initializers
  for (/* Do not initialize i*/; CurIdx < ResElts; ++CurIdx) {
    Value *Idx = Builder.getInt32(CurIdx);
    llvm::Value *Init = llvm::Constant::getNullValue(EltTy);
    V = Builder.CreateInsertElement(V, Init, Idx, "vecinit");
  }
  return V;
}

bool CodeGenFunction::ShouldNullCheckClassCastValue(const CastExpr *CE) {
  const Expr *E = CE->getSubExpr();

  if (CE->getCastKind() == CK_UncheckedDerivedToBase)
    return false;

  if (isa<CXXThisExpr>(E->IgnoreParens())) {
    // We always assume that 'this' is never null.
    return false;
  }

  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(CE)) {
    // And that glvalue casts are never null.
    if (ICE->isGLValue())
      return false;
  }

  return true;
}

// VisitCastExpr - Emit code for an explicit or implicit cast.  Implicit casts
// have to handle a more broad range of conversions than explicit casts, as they
// handle things like function to ptr-to-function decay etc.
Value *ScalarExprEmitter::VisitCastExpr(CastExpr *CE) {
  Expr *E = CE->getSubExpr();
  QualType DestTy = CE->getType();
  CastKind Kind = CE->getCastKind();
  CodeGenFunction::CGFPOptionsRAII FPOptions(CGF, CE);

  // These cases are generally not written to ignore the result of
  // evaluating their sub-expressions, so we clear this now.
  bool Ignored = TestAndClearIgnoreResultAssign();

  // Since almost all cast kinds apply to scalars, this switch doesn't have
  // a default case, so the compiler will warn on a missing case.  The cases
  // are in the same order as in the CastKind enum.
  switch (Kind) {
  case CK_Dependent: llvm_unreachable("dependent cast kind in IR gen!");
  case CK_BuiltinFnToFnPtr:
    llvm_unreachable("builtin functions are handled elsewhere");

  case CK_LValueBitCast:
  case CK_ObjCObjectLValueCast: {
    Address Addr = EmitLValue(E).getAddress();
    Addr = Addr.withElementType(CGF.ConvertTypeForMem(DestTy));
    LValue LV = CGF.MakeAddrLValue(Addr, DestTy);
    return EmitLoadOfLValue(LV, CE->getExprLoc());
  }

  case CK_LValueToRValueBitCast: {
    LValue SourceLVal = CGF.EmitLValue(E);
    Address Addr =
        SourceLVal.getAddress().withElementType(CGF.ConvertTypeForMem(DestTy));
    LValue DestLV = CGF.MakeAddrLValue(Addr, DestTy);
    DestLV.setTBAAInfo(TBAAAccessInfo::getMayAliasInfo());
    return EmitLoadOfLValue(DestLV, CE->getExprLoc());
  }

  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_BitCast: {
    Value *Src = Visit(const_cast<Expr*>(E));
    llvm::Type *SrcTy = Src->getType();
    llvm::Type *DstTy = ConvertType(DestTy);
    assert(
        (!SrcTy->isPtrOrPtrVectorTy() || !DstTy->isPtrOrPtrVectorTy() ||
         SrcTy->getPointerAddressSpace() == DstTy->getPointerAddressSpace()) &&
        "Address-space cast must be used to convert address spaces");

    if (CGF.SanOpts.has(SanitizerKind::CFIUnrelatedCast)) {
      if (auto *PT = DestTy->getAs<PointerType>()) {
        CGF.EmitVTablePtrCheckForCast(
            PT->getPointeeType(),
            Address(Src,
                    CGF.ConvertTypeForMem(
                        E->getType()->castAs<PointerType>()->getPointeeType()),
                    CGF.getPointerAlign()),
            /*MayBeNull=*/true, CodeGenFunction::CFITCK_UnrelatedCast,
            CE->getBeginLoc());
      }
    }

    if (CGF.CGM.getCodeGenOpts().StrictVTablePointers) {
      const QualType SrcType = E->getType();

      if (SrcType.mayBeNotDynamicClass() && DestTy.mayBeDynamicClass()) {
        // Casting to pointer that could carry dynamic information (provided by
        // invariant.group) requires launder.
        Src = Builder.CreateLaunderInvariantGroup(Src);
      } else if (SrcType.mayBeDynamicClass() && DestTy.mayBeNotDynamicClass()) {
        // Casting to pointer that does not carry dynamic information (provided
        // by invariant.group) requires stripping it.  Note that we don't do it
        // if the source could not be dynamic type and destination could be
        // dynamic because dynamic information is already laundered.  It is
        // because launder(strip(src)) == launder(src), so there is no need to
        // add extra strip before launder.
        Src = Builder.CreateStripInvariantGroup(Src);
      }
    }

    // Update heapallocsite metadata when there is an explicit pointer cast.
    if (auto *CI = dyn_cast<llvm::CallBase>(Src)) {
      if (CI->getMetadata("heapallocsite") && isa<ExplicitCastExpr>(CE) &&
          !isa<CastExpr>(E)) {
        QualType PointeeType = DestTy->getPointeeType();
        if (!PointeeType.isNull())
          CGF.getDebugInfo()->addHeapAllocSiteMetadata(CI, PointeeType,
                                                       CE->getExprLoc());
      }
    }

    // If Src is a fixed vector and Dst is a scalable vector, and both have the
    // same element type, use the llvm.vector.insert intrinsic to perform the
    // bitcast.
    if (auto *FixedSrcTy = dyn_cast<llvm::FixedVectorType>(SrcTy)) {
      if (auto *ScalableDstTy = dyn_cast<llvm::ScalableVectorType>(DstTy)) {
        // If we are casting a fixed i8 vector to a scalable i1 predicate
        // vector, use a vector insert and bitcast the result.
        if (ScalableDstTy->getElementType()->isIntegerTy(1) &&
            ScalableDstTy->getElementCount().isKnownMultipleOf(8) &&
            FixedSrcTy->getElementType()->isIntegerTy(8)) {
          ScalableDstTy = llvm::ScalableVectorType::get(
              FixedSrcTy->getElementType(),
              ScalableDstTy->getElementCount().getKnownMinValue() / 8);
        }
        if (FixedSrcTy->getElementType() == ScalableDstTy->getElementType()) {
          llvm::Value *UndefVec = llvm::UndefValue::get(ScalableDstTy);
          llvm::Value *Zero = llvm::Constant::getNullValue(CGF.CGM.Int64Ty);
          llvm::Value *Result = Builder.CreateInsertVector(
              ScalableDstTy, UndefVec, Src, Zero, "cast.scalable");
          if (Result->getType() != DstTy)
            Result = Builder.CreateBitCast(Result, DstTy);
          return Result;
        }
      }
    }

    // If Src is a scalable vector and Dst is a fixed vector, and both have the
    // same element type, use the llvm.vector.extract intrinsic to perform the
    // bitcast.
    if (auto *ScalableSrcTy = dyn_cast<llvm::ScalableVectorType>(SrcTy)) {
      if (auto *FixedDstTy = dyn_cast<llvm::FixedVectorType>(DstTy)) {
        // If we are casting a scalable i1 predicate vector to a fixed i8
        // vector, bitcast the source and use a vector extract.
        if (ScalableSrcTy->getElementType()->isIntegerTy(1) &&
            ScalableSrcTy->getElementCount().isKnownMultipleOf(8) &&
            FixedDstTy->getElementType()->isIntegerTy(8)) {
          ScalableSrcTy = llvm::ScalableVectorType::get(
              FixedDstTy->getElementType(),
              ScalableSrcTy->getElementCount().getKnownMinValue() / 8);
          Src = Builder.CreateBitCast(Src, ScalableSrcTy);
        }
        if (ScalableSrcTy->getElementType() == FixedDstTy->getElementType()) {
          llvm::Value *Zero = llvm::Constant::getNullValue(CGF.CGM.Int64Ty);
          return Builder.CreateExtractVector(DstTy, Src, Zero, "cast.fixed");
        }
      }
    }

    // Perform VLAT <-> VLST bitcast through memory.
    // TODO: since the llvm.vector.{insert,extract} intrinsics
    //       require the element types of the vectors to be the same, we
    //       need to keep this around for bitcasts between VLAT <-> VLST where
    //       the element types of the vectors are not the same, until we figure
    //       out a better way of doing these casts.
    if ((isa<llvm::FixedVectorType>(SrcTy) &&
         isa<llvm::ScalableVectorType>(DstTy)) ||
        (isa<llvm::ScalableVectorType>(SrcTy) &&
         isa<llvm::FixedVectorType>(DstTy))) {
      Address Addr = CGF.CreateDefaultAlignTempAlloca(SrcTy, "saved-value");
      LValue LV = CGF.MakeAddrLValue(Addr, E->getType());
      CGF.EmitStoreOfScalar(Src, LV);
      Addr = Addr.withElementType(CGF.ConvertTypeForMem(DestTy));
      LValue DestLV = CGF.MakeAddrLValue(Addr, DestTy);
      DestLV.setTBAAInfo(TBAAAccessInfo::getMayAliasInfo());
      return EmitLoadOfLValue(DestLV, CE->getExprLoc());
    }

    llvm::Value *Result = Builder.CreateBitCast(Src, DstTy);
    return CGF.authPointerToPointerCast(Result, E->getType(), DestTy);
  }
  case CK_AddressSpaceConversion: {
    Expr::EvalResult Result;
    if (E->EvaluateAsRValue(Result, CGF.getContext()) &&
        Result.Val.isNullPointer()) {
      // If E has side effect, it is emitted even if its final result is a
      // null pointer. In that case, a DCE pass should be able to
      // eliminate the useless instructions emitted during translating E.
      if (Result.HasSideEffects)
        Visit(E);
      return CGF.CGM.getNullPointer(cast<llvm::PointerType>(
          ConvertType(DestTy)), DestTy);
    }
    // Since target may map different address spaces in AST to the same address
    // space, an address space conversion may end up as a bitcast.
    return CGF.CGM.getTargetCodeGenInfo().performAddrSpaceCast(
        CGF, Visit(E), E->getType()->getPointeeType().getAddressSpace(),
        DestTy->getPointeeType().getAddressSpace(), ConvertType(DestTy));
  }
  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
  case CK_UserDefinedConversion:
    return Visit(const_cast<Expr*>(E));

  case CK_NoOp: {
    return CE->changesVolatileQualification() ? EmitLoadOfLValue(CE)
                                              : Visit(const_cast<Expr *>(E));
  }

  case CK_BaseToDerived: {
    const CXXRecordDecl *DerivedClassDecl = DestTy->getPointeeCXXRecordDecl();
    assert(DerivedClassDecl && "BaseToDerived arg isn't a C++ object pointer!");

    Address Base = CGF.EmitPointerWithAlignment(E);
    Address Derived =
      CGF.GetAddressOfDerivedClass(Base, DerivedClassDecl,
                                   CE->path_begin(), CE->path_end(),
                                   CGF.ShouldNullCheckClassCastValue(CE));

    // C++11 [expr.static.cast]p11: Behavior is undefined if a downcast is
    // performed and the object is not of the derived type.
    if (CGF.sanitizePerformTypeCheck())
      CGF.EmitTypeCheck(CodeGenFunction::TCK_DowncastPointer, CE->getExprLoc(),
                        Derived, DestTy->getPointeeType());

    if (CGF.SanOpts.has(SanitizerKind::CFIDerivedCast))
      CGF.EmitVTablePtrCheckForCast(DestTy->getPointeeType(), Derived,
                                    /*MayBeNull=*/true,
                                    CodeGenFunction::CFITCK_DerivedCast,
                                    CE->getBeginLoc());

    return CGF.getAsNaturalPointerTo(Derived, CE->getType()->getPointeeType());
  }
  case CK_UncheckedDerivedToBase:
  case CK_DerivedToBase: {
    // The EmitPointerWithAlignment path does this fine; just discard
    // the alignment.
    return CGF.getAsNaturalPointerTo(CGF.EmitPointerWithAlignment(CE),
                                     CE->getType()->getPointeeType());
  }

  case CK_Dynamic: {
    Address V = CGF.EmitPointerWithAlignment(E);
    const CXXDynamicCastExpr *DCE = cast<CXXDynamicCastExpr>(CE);
    return CGF.EmitDynamicCast(V, DCE);
  }

  case CK_ArrayToPointerDecay:
    return CGF.getAsNaturalPointerTo(CGF.EmitArrayToPointerDecay(E),
                                     CE->getType()->getPointeeType());
  case CK_FunctionToPointerDecay:
    return EmitLValue(E).getPointer(CGF);

  case CK_NullToPointer:
    if (MustVisitNullValue(E))
      CGF.EmitIgnoredExpr(E);

    return CGF.CGM.getNullPointer(cast<llvm::PointerType>(ConvertType(DestTy)),
                              DestTy);

  case CK_NullToMemberPointer: {
    if (MustVisitNullValue(E))
      CGF.EmitIgnoredExpr(E);

    const MemberPointerType *MPT = CE->getType()->getAs<MemberPointerType>();
    return CGF.CGM.getCXXABI().EmitNullMemberPointer(MPT);
  }

  case CK_ReinterpretMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer: {
    Value *Src = Visit(E);

    // Note that the AST doesn't distinguish between checked and
    // unchecked member pointer conversions, so we always have to
    // implement checked conversions here.  This is inefficient when
    // actual control flow may be required in order to perform the
    // check, which it is for data member pointers (but not member
    // function pointers on Itanium and ARM).
    return CGF.CGM.getCXXABI().EmitMemberPointerConversion(CGF, CE, Src);
  }

  case CK_ARCProduceObject:
    return CGF.EmitARCRetainScalarExpr(E);
  case CK_ARCConsumeObject:
    return CGF.EmitObjCConsumeObject(E->getType(), Visit(E));
  case CK_ARCReclaimReturnedObject:
    return CGF.EmitARCReclaimReturnedObject(E, /*allowUnsafe*/ Ignored);
  case CK_ARCExtendBlockObject:
    return CGF.EmitARCExtendBlockObject(E);

  case CK_CopyAndAutoreleaseBlockObject:
    return CGF.EmitBlockCopyAndAutorelease(Visit(E), E->getType());

  case CK_FloatingRealToComplex:
  case CK_FloatingComplexCast:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_FloatingComplexToIntegralComplex:
  case CK_ConstructorConversion:
  case CK_ToUnion:
  case CK_HLSLArrayRValue:
    llvm_unreachable("scalar cast to non-scalar value");

  case CK_LValueToRValue:
    assert(CGF.getContext().hasSameUnqualifiedType(E->getType(), DestTy));
    assert(E->isGLValue() && "lvalue-to-rvalue applied to r-value!");
    return Visit(const_cast<Expr*>(E));

  case CK_IntegralToPointer: {
    Value *Src = Visit(const_cast<Expr*>(E));

    // First, convert to the correct width so that we control the kind of
    // extension.
    auto DestLLVMTy = ConvertType(DestTy);
    llvm::Type *MiddleTy = CGF.CGM.getDataLayout().getIntPtrType(DestLLVMTy);
    bool InputSigned = E->getType()->isSignedIntegerOrEnumerationType();
    llvm::Value* IntResult =
      Builder.CreateIntCast(Src, MiddleTy, InputSigned, "conv");

    auto *IntToPtr = Builder.CreateIntToPtr(IntResult, DestLLVMTy);

    if (CGF.CGM.getCodeGenOpts().StrictVTablePointers) {
      // Going from integer to pointer that could be dynamic requires reloading
      // dynamic information from invariant.group.
      if (DestTy.mayBeDynamicClass())
        IntToPtr = Builder.CreateLaunderInvariantGroup(IntToPtr);
    }

    IntToPtr = CGF.authPointerToPointerCast(IntToPtr, E->getType(), DestTy);
    return IntToPtr;
  }
  case CK_PointerToIntegral: {
    assert(!DestTy->isBooleanType() && "bool should use PointerToBool");
    auto *PtrExpr = Visit(E);

    if (CGF.CGM.getCodeGenOpts().StrictVTablePointers) {
      const QualType SrcType = E->getType();

      // Casting to integer requires stripping dynamic information as it does
      // not carries it.
      if (SrcType.mayBeDynamicClass())
        PtrExpr = Builder.CreateStripInvariantGroup(PtrExpr);
    }

    PtrExpr = CGF.authPointerToPointerCast(PtrExpr, E->getType(), DestTy);
    return Builder.CreatePtrToInt(PtrExpr, ConvertType(DestTy));
  }
  case CK_ToVoid: {
    CGF.EmitIgnoredExpr(E);
    return nullptr;
  }
  case CK_MatrixCast: {
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());
  }
  case CK_VectorSplat: {
    llvm::Type *DstTy = ConvertType(DestTy);
    Value *Elt = Visit(const_cast<Expr *>(E));
    // Splat the element across to all elements
    llvm::ElementCount NumElements =
        cast<llvm::VectorType>(DstTy)->getElementCount();
    return Builder.CreateVectorSplat(NumElements, Elt, "splat");
  }

  case CK_FixedPointCast:
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());

  case CK_FixedPointToBoolean:
    assert(E->getType()->isFixedPointType() &&
           "Expected src type to be fixed point type");
    assert(DestTy->isBooleanType() && "Expected dest type to be boolean type");
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());

  case CK_FixedPointToIntegral:
    assert(E->getType()->isFixedPointType() &&
           "Expected src type to be fixed point type");
    assert(DestTy->isIntegerType() && "Expected dest type to be an integer");
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());

  case CK_IntegralToFixedPoint:
    assert(E->getType()->isIntegerType() &&
           "Expected src type to be an integer");
    assert(DestTy->isFixedPointType() &&
           "Expected dest type to be fixed point type");
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());

  case CK_IntegralCast: {
    if (E->getType()->isExtVectorType() && DestTy->isExtVectorType()) {
      QualType SrcElTy = E->getType()->castAs<VectorType>()->getElementType();
      return Builder.CreateIntCast(Visit(E), ConvertType(DestTy),
                                   SrcElTy->isSignedIntegerOrEnumerationType(),
                                   "conv");
    }
    ScalarConversionOpts Opts;
    if (auto *ICE = dyn_cast<ImplicitCastExpr>(CE)) {
      if (!ICE->isPartOfExplicitCast())
        Opts = ScalarConversionOpts(CGF.SanOpts);
    }
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc(), Opts);
  }
  case CK_IntegralToFloating: {
    if (E->getType()->isVectorType() && DestTy->isVectorType()) {
      // TODO: Support constrained FP intrinsics.
      QualType SrcElTy = E->getType()->castAs<VectorType>()->getElementType();
      if (SrcElTy->isSignedIntegerOrEnumerationType())
        return Builder.CreateSIToFP(Visit(E), ConvertType(DestTy), "conv");
      return Builder.CreateUIToFP(Visit(E), ConvertType(DestTy), "conv");
    }
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, CE);
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());
  }
  case CK_FloatingToIntegral: {
    if (E->getType()->isVectorType() && DestTy->isVectorType()) {
      // TODO: Support constrained FP intrinsics.
      QualType DstElTy = DestTy->castAs<VectorType>()->getElementType();
      if (DstElTy->isSignedIntegerOrEnumerationType())
        return Builder.CreateFPToSI(Visit(E), ConvertType(DestTy), "conv");
      return Builder.CreateFPToUI(Visit(E), ConvertType(DestTy), "conv");
    }
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, CE);
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());
  }
  case CK_FloatingCast: {
    if (E->getType()->isVectorType() && DestTy->isVectorType()) {
      // TODO: Support constrained FP intrinsics.
      QualType SrcElTy = E->getType()->castAs<VectorType>()->getElementType();
      QualType DstElTy = DestTy->castAs<VectorType>()->getElementType();
      if (DstElTy->castAs<BuiltinType>()->getKind() <
          SrcElTy->castAs<BuiltinType>()->getKind())
        return Builder.CreateFPTrunc(Visit(E), ConvertType(DestTy), "conv");
      return Builder.CreateFPExt(Visit(E), ConvertType(DestTy), "conv");
    }
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, CE);
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());
  }
  case CK_FixedPointToFloating:
  case CK_FloatingToFixedPoint: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, CE);
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc());
  }
  case CK_BooleanToSignedIntegral: {
    ScalarConversionOpts Opts;
    Opts.TreatBooleanAsSigned = true;
    return EmitScalarConversion(Visit(E), E->getType(), DestTy,
                                CE->getExprLoc(), Opts);
  }
  case CK_IntegralToBoolean:
    return EmitIntToBoolConversion(Visit(E));
  case CK_PointerToBoolean:
    return EmitPointerToBoolConversion(Visit(E), E->getType());
  case CK_FloatingToBoolean: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, CE);
    return EmitFloatToBoolConversion(Visit(E));
  }
  case CK_MemberPointerToBoolean: {
    llvm::Value *MemPtr = Visit(E);
    const MemberPointerType *MPT = E->getType()->getAs<MemberPointerType>();
    return CGF.CGM.getCXXABI().EmitMemberPointerIsNotNull(CGF, MemPtr, MPT);
  }

  case CK_FloatingComplexToReal:
  case CK_IntegralComplexToReal:
    return CGF.EmitComplexExpr(E, false, true).first;

  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToBoolean: {
    CodeGenFunction::ComplexPairTy V = CGF.EmitComplexExpr(E);

    // TODO: kill this function off, inline appropriate case here
    return EmitComplexToScalarConversion(V, E->getType(), DestTy,
                                         CE->getExprLoc());
  }

  case CK_ZeroToOCLOpaqueType: {
    assert((DestTy->isEventT() || DestTy->isQueueT() ||
            DestTy->isOCLIntelSubgroupAVCType()) &&
           "CK_ZeroToOCLEvent cast on non-event type");
    return llvm::Constant::getNullValue(ConvertType(DestTy));
  }

  case CK_IntToOCLSampler:
    return CGF.CGM.createOpenCLIntToSamplerConversion(E, CGF);

  case CK_HLSLVectorTruncation: {
    assert(DestTy->isVectorType() && "Expected dest type to be vector type");
    Value *Vec = Visit(const_cast<Expr *>(E));
    SmallVector<int, 16> Mask;
    unsigned NumElts = DestTy->castAs<VectorType>()->getNumElements();
    for (unsigned I = 0; I != NumElts; ++I)
      Mask.push_back(I);

    return Builder.CreateShuffleVector(Vec, Mask, "trunc");
  }

  } // end of switch

  llvm_unreachable("unknown scalar cast");
}

Value *ScalarExprEmitter::VisitStmtExpr(const StmtExpr *E) {
  CodeGenFunction::StmtExprEvaluation eval(CGF);
  Address RetAlloca = CGF.EmitCompoundStmt(*E->getSubStmt(),
                                           !E->getType()->isVoidType());
  if (!RetAlloca.isValid())
    return nullptr;
  return CGF.EmitLoadOfScalar(CGF.MakeAddrLValue(RetAlloca, E->getType()),
                              E->getExprLoc());
}

Value *ScalarExprEmitter::VisitExprWithCleanups(ExprWithCleanups *E) {
  CodeGenFunction::RunCleanupsScope Scope(CGF);
  Value *V = Visit(E->getSubExpr());
  // Defend against dominance problems caused by jumps out of expression
  // evaluation through the shared cleanup block.
  Scope.ForceCleanup({&V});
  return V;
}

//===----------------------------------------------------------------------===//
//                             Unary Operators
//===----------------------------------------------------------------------===//

static BinOpInfo createBinOpInfoFromIncDec(const UnaryOperator *E,
                                           llvm::Value *InVal, bool IsInc,
                                           FPOptions FPFeatures) {
  BinOpInfo BinOp;
  BinOp.LHS = InVal;
  BinOp.RHS = llvm::ConstantInt::get(InVal->getType(), 1, false);
  BinOp.Ty = E->getType();
  BinOp.Opcode = IsInc ? BO_Add : BO_Sub;
  BinOp.FPFeatures = FPFeatures;
  BinOp.E = E;
  return BinOp;
}

llvm::Value *ScalarExprEmitter::EmitIncDecConsiderOverflowBehavior(
    const UnaryOperator *E, llvm::Value *InVal, bool IsInc) {
  llvm::Value *Amount =
      llvm::ConstantInt::get(InVal->getType(), IsInc ? 1 : -1, true);
  StringRef Name = IsInc ? "inc" : "dec";
  switch (CGF.getLangOpts().getSignedOverflowBehavior()) {
  case LangOptions::SOB_Defined:
    if (!CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow))
      return Builder.CreateAdd(InVal, Amount, Name);
    [[fallthrough]];
  case LangOptions::SOB_Undefined:
    if (!CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow))
      return Builder.CreateNSWAdd(InVal, Amount, Name);
    [[fallthrough]];
  case LangOptions::SOB_Trapping:
    if (!E->canOverflow())
      return Builder.CreateNSWAdd(InVal, Amount, Name);
    return EmitOverflowCheckedBinOp(createBinOpInfoFromIncDec(
        E, InVal, IsInc, E->getFPFeaturesInEffect(CGF.getLangOpts())));
  }
  llvm_unreachable("Unknown SignedOverflowBehaviorTy");
}

namespace {
/// Handles check and update for lastprivate conditional variables.
class OMPLastprivateConditionalUpdateRAII {
private:
  CodeGenFunction &CGF;
  const UnaryOperator *E;

public:
  OMPLastprivateConditionalUpdateRAII(CodeGenFunction &CGF,
                                      const UnaryOperator *E)
      : CGF(CGF), E(E) {}
  ~OMPLastprivateConditionalUpdateRAII() {
    if (CGF.getLangOpts().OpenMP)
      CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(
          CGF, E->getSubExpr());
  }
};
} // namespace

llvm::Value *
ScalarExprEmitter::EmitScalarPrePostIncDec(const UnaryOperator *E, LValue LV,
                                           bool isInc, bool isPre) {
  OMPLastprivateConditionalUpdateRAII OMPRegion(CGF, E);
  QualType type = E->getSubExpr()->getType();
  llvm::PHINode *atomicPHI = nullptr;
  llvm::Value *value;
  llvm::Value *input;
  llvm::Value *Previous = nullptr;
  QualType SrcType = E->getType();

  int amount = (isInc ? 1 : -1);
  bool isSubtraction = !isInc;

  if (const AtomicType *atomicTy = type->getAs<AtomicType>()) {
    type = atomicTy->getValueType();
    if (isInc && type->isBooleanType()) {
      llvm::Value *True = CGF.EmitToMemory(Builder.getTrue(), type);
      if (isPre) {
        Builder.CreateStore(True, LV.getAddress(), LV.isVolatileQualified())
            ->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        return Builder.getTrue();
      }
      // For atomic bool increment, we just store true and return it for
      // preincrement, do an atomic swap with true for postincrement
      return Builder.CreateAtomicRMW(
          llvm::AtomicRMWInst::Xchg, LV.getAddress(), True,
          llvm::AtomicOrdering::SequentiallyConsistent);
    }
    // Special case for atomic increment / decrement on integers, emit
    // atomicrmw instructions.  We skip this if we want to be doing overflow
    // checking, and fall into the slow path with the atomic cmpxchg loop.
    if (!type->isBooleanType() && type->isIntegerType() &&
        !(type->isUnsignedIntegerType() &&
          CGF.SanOpts.has(SanitizerKind::UnsignedIntegerOverflow)) &&
        CGF.getLangOpts().getSignedOverflowBehavior() !=
            LangOptions::SOB_Trapping) {
      llvm::AtomicRMWInst::BinOp aop = isInc ? llvm::AtomicRMWInst::Add :
        llvm::AtomicRMWInst::Sub;
      llvm::Instruction::BinaryOps op = isInc ? llvm::Instruction::Add :
        llvm::Instruction::Sub;
      llvm::Value *amt = CGF.EmitToMemory(
          llvm::ConstantInt::get(ConvertType(type), 1, true), type);
      llvm::Value *old =
          Builder.CreateAtomicRMW(aop, LV.getAddress(), amt,
                                  llvm::AtomicOrdering::SequentiallyConsistent);
      return isPre ? Builder.CreateBinOp(op, old, amt) : old;
    }
    // Special case for atomic increment/decrement on floats.
    // Bail out non-power-of-2-sized floating point types (e.g., x86_fp80).
    if (type->isFloatingType()) {
      llvm::Type *Ty = ConvertType(type);
      if (llvm::has_single_bit(Ty->getScalarSizeInBits())) {
        llvm::AtomicRMWInst::BinOp aop =
            isInc ? llvm::AtomicRMWInst::FAdd : llvm::AtomicRMWInst::FSub;
        llvm::Instruction::BinaryOps op =
            isInc ? llvm::Instruction::FAdd : llvm::Instruction::FSub;
        llvm::Value *amt = llvm::ConstantFP::get(Ty, 1.0);
        llvm::AtomicRMWInst *old = Builder.CreateAtomicRMW(
            aop, LV.getAddress(), amt,
            llvm::AtomicOrdering::SequentiallyConsistent);

        return isPre ? Builder.CreateBinOp(op, old, amt) : old;
      }
    }
    value = EmitLoadOfLValue(LV, E->getExprLoc());
    input = value;
    // For every other atomic operation, we need to emit a load-op-cmpxchg loop
    llvm::BasicBlock *startBB = Builder.GetInsertBlock();
    llvm::BasicBlock *opBB = CGF.createBasicBlock("atomic_op", CGF.CurFn);
    value = CGF.EmitToMemory(value, type);
    Builder.CreateBr(opBB);
    Builder.SetInsertPoint(opBB);
    atomicPHI = Builder.CreatePHI(value->getType(), 2);
    atomicPHI->addIncoming(value, startBB);
    value = atomicPHI;
  } else {
    value = EmitLoadOfLValue(LV, E->getExprLoc());
    input = value;
  }

  // Special case of integer increment that we have to check first: bool++.
  // Due to promotion rules, we get:
  //   bool++ -> bool = bool + 1
  //          -> bool = (int)bool + 1
  //          -> bool = ((int)bool + 1 != 0)
  // An interesting aspect of this is that increment is always true.
  // Decrement does not have this property.
  if (isInc && type->isBooleanType()) {
    value = Builder.getTrue();

  // Most common case by far: integer increment.
  } else if (type->isIntegerType()) {
    QualType promotedType;
    bool canPerformLossyDemotionCheck = false;
    if (CGF.getContext().isPromotableIntegerType(type)) {
      promotedType = CGF.getContext().getPromotedIntegerType(type);
      assert(promotedType != type && "Shouldn't promote to the same type.");
      canPerformLossyDemotionCheck = true;
      canPerformLossyDemotionCheck &=
          CGF.getContext().getCanonicalType(type) !=
          CGF.getContext().getCanonicalType(promotedType);
      canPerformLossyDemotionCheck &=
          PromotionIsPotentiallyEligibleForImplicitIntegerConversionCheck(
              type, promotedType);
      assert((!canPerformLossyDemotionCheck ||
              type->isSignedIntegerOrEnumerationType() ||
              promotedType->isSignedIntegerOrEnumerationType() ||
              ConvertType(type)->getScalarSizeInBits() ==
                  ConvertType(promotedType)->getScalarSizeInBits()) &&
             "The following check expects that if we do promotion to different "
             "underlying canonical type, at least one of the types (either "
             "base or promoted) will be signed, or the bitwidths will match.");
    }
    if (CGF.SanOpts.hasOneOf(
            SanitizerKind::ImplicitIntegerArithmeticValueChange |
            SanitizerKind::ImplicitBitfieldConversion) &&
        canPerformLossyDemotionCheck) {
      // While `x += 1` (for `x` with width less than int) is modeled as
      // promotion+arithmetics+demotion, and we can catch lossy demotion with
      // ease; inc/dec with width less than int can't overflow because of
      // promotion rules, so we omit promotion+demotion, which means that we can
      // not catch lossy "demotion". Because we still want to catch these cases
      // when the sanitizer is enabled, we perform the promotion, then perform
      // the increment/decrement in the wider type, and finally
      // perform the demotion. This will catch lossy demotions.

      // We have a special case for bitfields defined using all the bits of the
      // type. In this case we need to do the same trick as for the integer
      // sanitizer checks, i.e., promotion -> increment/decrement -> demotion.

      value = EmitScalarConversion(value, type, promotedType, E->getExprLoc());
      Value *amt = llvm::ConstantInt::get(value->getType(), amount, true);
      value = Builder.CreateAdd(value, amt, isInc ? "inc" : "dec");
      // Do pass non-default ScalarConversionOpts so that sanitizer check is
      // emitted if LV is not a bitfield, otherwise the bitfield sanitizer
      // checks will take care of the conversion.
      ScalarConversionOpts Opts;
      if (!LV.isBitField())
        Opts = ScalarConversionOpts(CGF.SanOpts);
      else if (CGF.SanOpts.has(SanitizerKind::ImplicitBitfieldConversion)) {
        Previous = value;
        SrcType = promotedType;
      }

      value = EmitScalarConversion(value, promotedType, type, E->getExprLoc(),
                                   Opts);

      // Note that signed integer inc/dec with width less than int can't
      // overflow because of promotion rules; we're just eliding a few steps
      // here.
    } else if (E->canOverflow() && type->isSignedIntegerOrEnumerationType()) {
      value = EmitIncDecConsiderOverflowBehavior(E, value, isInc);
    } else if (E->canOverflow() && type->isUnsignedIntegerType() &&
               CGF.SanOpts.has(SanitizerKind::UnsignedIntegerOverflow)) {
      value = EmitOverflowCheckedBinOp(createBinOpInfoFromIncDec(
          E, value, isInc, E->getFPFeaturesInEffect(CGF.getLangOpts())));
    } else {
      llvm::Value *amt = llvm::ConstantInt::get(value->getType(), amount, true);
      value = Builder.CreateAdd(value, amt, isInc ? "inc" : "dec");
    }

  // Next most common: pointer increment.
  } else if (const PointerType *ptr = type->getAs<PointerType>()) {
    QualType type = ptr->getPointeeType();

    // VLA types don't have constant size.
    if (const VariableArrayType *vla
          = CGF.getContext().getAsVariableArrayType(type)) {
      llvm::Value *numElts = CGF.getVLASize(vla).NumElts;
      if (!isInc) numElts = Builder.CreateNSWNeg(numElts, "vla.negsize");
      llvm::Type *elemTy = CGF.ConvertTypeForMem(vla->getElementType());
      if (CGF.getLangOpts().isSignedOverflowDefined())
        value = Builder.CreateGEP(elemTy, value, numElts, "vla.inc");
      else
        value = CGF.EmitCheckedInBoundsGEP(
            elemTy, value, numElts, /*SignedIndices=*/false, isSubtraction,
            E->getExprLoc(), "vla.inc");

    // Arithmetic on function pointers (!) is just +-1.
    } else if (type->isFunctionType()) {
      llvm::Value *amt = Builder.getInt32(amount);

      if (CGF.getLangOpts().isSignedOverflowDefined())
        value = Builder.CreateGEP(CGF.Int8Ty, value, amt, "incdec.funcptr");
      else
        value =
            CGF.EmitCheckedInBoundsGEP(CGF.Int8Ty, value, amt,
                                       /*SignedIndices=*/false, isSubtraction,
                                       E->getExprLoc(), "incdec.funcptr");

    // For everything else, we can just do a simple increment.
    } else {
      llvm::Value *amt = Builder.getInt32(amount);
      llvm::Type *elemTy = CGF.ConvertTypeForMem(type);
      if (CGF.getLangOpts().isSignedOverflowDefined())
        value = Builder.CreateGEP(elemTy, value, amt, "incdec.ptr");
      else
        value = CGF.EmitCheckedInBoundsGEP(
            elemTy, value, amt, /*SignedIndices=*/false, isSubtraction,
            E->getExprLoc(), "incdec.ptr");
    }

  // Vector increment/decrement.
  } else if (type->isVectorType()) {
    if (type->hasIntegerRepresentation()) {
      llvm::Value *amt = llvm::ConstantInt::get(value->getType(), amount);

      value = Builder.CreateAdd(value, amt, isInc ? "inc" : "dec");
    } else {
      value = Builder.CreateFAdd(
                  value,
                  llvm::ConstantFP::get(value->getType(), amount),
                  isInc ? "inc" : "dec");
    }

  // Floating point.
  } else if (type->isRealFloatingType()) {
    // Add the inc/dec to the real part.
    llvm::Value *amt;
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);

    if (type->isHalfType() && !CGF.getContext().getLangOpts().NativeHalfType) {
      // Another special case: half FP increment should be done via float
      if (CGF.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
        value = Builder.CreateCall(
            CGF.CGM.getIntrinsic(llvm::Intrinsic::convert_from_fp16,
                                 CGF.CGM.FloatTy),
            input, "incdec.conv");
      } else {
        value = Builder.CreateFPExt(input, CGF.CGM.FloatTy, "incdec.conv");
      }
    }

    if (value->getType()->isFloatTy())
      amt = llvm::ConstantFP::get(VMContext,
                                  llvm::APFloat(static_cast<float>(amount)));
    else if (value->getType()->isDoubleTy())
      amt = llvm::ConstantFP::get(VMContext,
                                  llvm::APFloat(static_cast<double>(amount)));
    else {
      // Remaining types are Half, Bfloat16, LongDouble, __ibm128 or __float128.
      // Convert from float.
      llvm::APFloat F(static_cast<float>(amount));
      bool ignored;
      const llvm::fltSemantics *FS;
      // Don't use getFloatTypeSemantics because Half isn't
      // necessarily represented using the "half" LLVM type.
      if (value->getType()->isFP128Ty())
        FS = &CGF.getTarget().getFloat128Format();
      else if (value->getType()->isHalfTy())
        FS = &CGF.getTarget().getHalfFormat();
      else if (value->getType()->isBFloatTy())
        FS = &CGF.getTarget().getBFloat16Format();
      else if (value->getType()->isPPC_FP128Ty())
        FS = &CGF.getTarget().getIbm128Format();
      else
        FS = &CGF.getTarget().getLongDoubleFormat();
      F.convert(*FS, llvm::APFloat::rmTowardZero, &ignored);
      amt = llvm::ConstantFP::get(VMContext, F);
    }
    value = Builder.CreateFAdd(value, amt, isInc ? "inc" : "dec");

    if (type->isHalfType() && !CGF.getContext().getLangOpts().NativeHalfType) {
      if (CGF.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
        value = Builder.CreateCall(
            CGF.CGM.getIntrinsic(llvm::Intrinsic::convert_to_fp16,
                                 CGF.CGM.FloatTy),
            value, "incdec.conv");
      } else {
        value = Builder.CreateFPTrunc(value, input->getType(), "incdec.conv");
      }
    }

  // Fixed-point types.
  } else if (type->isFixedPointType()) {
    // Fixed-point types are tricky. In some cases, it isn't possible to
    // represent a 1 or a -1 in the type at all. Piggyback off of
    // EmitFixedPointBinOp to avoid having to reimplement saturation.
    BinOpInfo Info;
    Info.E = E;
    Info.Ty = E->getType();
    Info.Opcode = isInc ? BO_Add : BO_Sub;
    Info.LHS = value;
    Info.RHS = llvm::ConstantInt::get(value->getType(), 1, false);
    // If the type is signed, it's better to represent this as +(-1) or -(-1),
    // since -1 is guaranteed to be representable.
    if (type->isSignedFixedPointType()) {
      Info.Opcode = isInc ? BO_Sub : BO_Add;
      Info.RHS = Builder.CreateNeg(Info.RHS);
    }
    // Now, convert from our invented integer literal to the type of the unary
    // op. This will upscale and saturate if necessary. This value can become
    // undef in some cases.
    llvm::FixedPointBuilder<CGBuilderTy> FPBuilder(Builder);
    auto DstSema = CGF.getContext().getFixedPointSemantics(Info.Ty);
    Info.RHS = FPBuilder.CreateIntegerToFixed(Info.RHS, true, DstSema);
    value = EmitFixedPointBinOp(Info);

  // Objective-C pointer types.
  } else {
    const ObjCObjectPointerType *OPT = type->castAs<ObjCObjectPointerType>();

    CharUnits size = CGF.getContext().getTypeSizeInChars(OPT->getObjectType());
    if (!isInc) size = -size;
    llvm::Value *sizeValue =
      llvm::ConstantInt::get(CGF.SizeTy, size.getQuantity());

    if (CGF.getLangOpts().isSignedOverflowDefined())
      value = Builder.CreateGEP(CGF.Int8Ty, value, sizeValue, "incdec.objptr");
    else
      value = CGF.EmitCheckedInBoundsGEP(
          CGF.Int8Ty, value, sizeValue, /*SignedIndices=*/false, isSubtraction,
          E->getExprLoc(), "incdec.objptr");
    value = Builder.CreateBitCast(value, input->getType());
  }

  if (atomicPHI) {
    llvm::BasicBlock *curBlock = Builder.GetInsertBlock();
    llvm::BasicBlock *contBB = CGF.createBasicBlock("atomic_cont", CGF.CurFn);
    auto Pair = CGF.EmitAtomicCompareExchange(
        LV, RValue::get(atomicPHI), RValue::get(value), E->getExprLoc());
    llvm::Value *old = CGF.EmitToMemory(Pair.first.getScalarVal(), type);
    llvm::Value *success = Pair.second;
    atomicPHI->addIncoming(old, curBlock);
    Builder.CreateCondBr(success, contBB, atomicPHI->getParent());
    Builder.SetInsertPoint(contBB);
    return isPre ? value : input;
  }

  // Store the updated result through the lvalue.
  if (LV.isBitField()) {
    Value *Src = Previous ? Previous : value;
    CGF.EmitStoreThroughBitfieldLValue(RValue::get(value), LV, &value);
    CGF.EmitBitfieldConversionCheck(Src, SrcType, value, E->getType(),
                                    LV.getBitFieldInfo(), E->getExprLoc());
  } else
    CGF.EmitStoreThroughLValue(RValue::get(value), LV);

  // If this is a postinc, return the value read from memory, otherwise use the
  // updated value.
  return isPre ? value : input;
}


Value *ScalarExprEmitter::VisitUnaryPlus(const UnaryOperator *E,
                                         QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  Value *result = VisitPlus(E, promotionTy);
  if (result && !promotionTy.isNull())
    result = EmitUnPromotedValue(result, E->getType());
  return result;
}

Value *ScalarExprEmitter::VisitPlus(const UnaryOperator *E,
                                    QualType PromotionType) {
  // This differs from gcc, though, most likely due to a bug in gcc.
  TestAndClearIgnoreResultAssign();
  if (!PromotionType.isNull())
    return CGF.EmitPromotedScalarExpr(E->getSubExpr(), PromotionType);
  return Visit(E->getSubExpr());
}

Value *ScalarExprEmitter::VisitUnaryMinus(const UnaryOperator *E,
                                          QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  Value *result = VisitMinus(E, promotionTy);
  if (result && !promotionTy.isNull())
    result = EmitUnPromotedValue(result, E->getType());
  return result;
}

Value *ScalarExprEmitter::VisitMinus(const UnaryOperator *E,
                                     QualType PromotionType) {
  TestAndClearIgnoreResultAssign();
  Value *Op;
  if (!PromotionType.isNull())
    Op = CGF.EmitPromotedScalarExpr(E->getSubExpr(), PromotionType);
  else
    Op = Visit(E->getSubExpr());

  // Generate a unary FNeg for FP ops.
  if (Op->getType()->isFPOrFPVectorTy())
    return Builder.CreateFNeg(Op, "fneg");

  // Emit unary minus with EmitSub so we handle overflow cases etc.
  BinOpInfo BinOp;
  BinOp.RHS = Op;
  BinOp.LHS = llvm::Constant::getNullValue(BinOp.RHS->getType());
  BinOp.Ty = E->getType();
  BinOp.Opcode = BO_Sub;
  BinOp.FPFeatures = E->getFPFeaturesInEffect(CGF.getLangOpts());
  BinOp.E = E;
  return EmitSub(BinOp);
}

Value *ScalarExprEmitter::VisitUnaryNot(const UnaryOperator *E) {
  TestAndClearIgnoreResultAssign();
  Value *Op = Visit(E->getSubExpr());
  return Builder.CreateNot(Op, "not");
}

Value *ScalarExprEmitter::VisitUnaryLNot(const UnaryOperator *E) {
  // Perform vector logical not on comparison with zero vector.
  if (E->getType()->isVectorType() &&
      E->getType()->castAs<VectorType>()->getVectorKind() ==
          VectorKind::Generic) {
    Value *Oper = Visit(E->getSubExpr());
    Value *Zero = llvm::Constant::getNullValue(Oper->getType());
    Value *Result;
    if (Oper->getType()->isFPOrFPVectorTy()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(
          CGF, E->getFPFeaturesInEffect(CGF.getLangOpts()));
      Result = Builder.CreateFCmp(llvm::CmpInst::FCMP_OEQ, Oper, Zero, "cmp");
    } else
      Result = Builder.CreateICmp(llvm::CmpInst::ICMP_EQ, Oper, Zero, "cmp");
    return Builder.CreateSExt(Result, ConvertType(E->getType()), "sext");
  }

  // Compare operand to zero.
  Value *BoolVal = CGF.EvaluateExprAsBool(E->getSubExpr());

  // Invert value.
  // TODO: Could dynamically modify easy computations here.  For example, if
  // the operand is an icmp ne, turn into icmp eq.
  BoolVal = Builder.CreateNot(BoolVal, "lnot");

  // ZExt result to the expr type.
  return Builder.CreateZExt(BoolVal, ConvertType(E->getType()), "lnot.ext");
}

Value *ScalarExprEmitter::VisitOffsetOfExpr(OffsetOfExpr *E) {
  // Try folding the offsetof to a constant.
  Expr::EvalResult EVResult;
  if (E->EvaluateAsInt(EVResult, CGF.getContext())) {
    llvm::APSInt Value = EVResult.Val.getInt();
    return Builder.getInt(Value);
  }

  // Loop over the components of the offsetof to compute the value.
  unsigned n = E->getNumComponents();
  llvm::Type* ResultType = ConvertType(E->getType());
  llvm::Value* Result = llvm::Constant::getNullValue(ResultType);
  QualType CurrentType = E->getTypeSourceInfo()->getType();
  for (unsigned i = 0; i != n; ++i) {
    OffsetOfNode ON = E->getComponent(i);
    llvm::Value *Offset = nullptr;
    switch (ON.getKind()) {
    case OffsetOfNode::Array: {
      // Compute the index
      Expr *IdxExpr = E->getIndexExpr(ON.getArrayExprIndex());
      llvm::Value* Idx = CGF.EmitScalarExpr(IdxExpr);
      bool IdxSigned = IdxExpr->getType()->isSignedIntegerOrEnumerationType();
      Idx = Builder.CreateIntCast(Idx, ResultType, IdxSigned, "conv");

      // Save the element type
      CurrentType =
          CGF.getContext().getAsArrayType(CurrentType)->getElementType();

      // Compute the element size
      llvm::Value* ElemSize = llvm::ConstantInt::get(ResultType,
          CGF.getContext().getTypeSizeInChars(CurrentType).getQuantity());

      // Multiply out to compute the result
      Offset = Builder.CreateMul(Idx, ElemSize);
      break;
    }

    case OffsetOfNode::Field: {
      FieldDecl *MemberDecl = ON.getField();
      RecordDecl *RD = CurrentType->castAs<RecordType>()->getDecl();
      const ASTRecordLayout &RL = CGF.getContext().getASTRecordLayout(RD);

      // Compute the index of the field in its parent.
      unsigned i = 0;
      // FIXME: It would be nice if we didn't have to loop here!
      for (RecordDecl::field_iterator Field = RD->field_begin(),
                                      FieldEnd = RD->field_end();
           Field != FieldEnd; ++Field, ++i) {
        if (*Field == MemberDecl)
          break;
      }
      assert(i < RL.getFieldCount() && "offsetof field in wrong type");

      // Compute the offset to the field
      int64_t OffsetInt = RL.getFieldOffset(i) /
                          CGF.getContext().getCharWidth();
      Offset = llvm::ConstantInt::get(ResultType, OffsetInt);

      // Save the element type.
      CurrentType = MemberDecl->getType();
      break;
    }

    case OffsetOfNode::Identifier:
      llvm_unreachable("dependent __builtin_offsetof");

    case OffsetOfNode::Base: {
      if (ON.getBase()->isVirtual()) {
        CGF.ErrorUnsupported(E, "virtual base in offsetof");
        continue;
      }

      RecordDecl *RD = CurrentType->castAs<RecordType>()->getDecl();
      const ASTRecordLayout &RL = CGF.getContext().getASTRecordLayout(RD);

      // Save the element type.
      CurrentType = ON.getBase()->getType();

      // Compute the offset to the base.
      auto *BaseRT = CurrentType->castAs<RecordType>();
      auto *BaseRD = cast<CXXRecordDecl>(BaseRT->getDecl());
      CharUnits OffsetInt = RL.getBaseClassOffset(BaseRD);
      Offset = llvm::ConstantInt::get(ResultType, OffsetInt.getQuantity());
      break;
    }
    }
    Result = Builder.CreateAdd(Result, Offset);
  }
  return Result;
}

/// VisitUnaryExprOrTypeTraitExpr - Return the size or alignment of the type of
/// argument of the sizeof expression as an integer.
Value *
ScalarExprEmitter::VisitUnaryExprOrTypeTraitExpr(
                              const UnaryExprOrTypeTraitExpr *E) {
  QualType TypeToSize = E->getTypeOfArgument();
  if (auto Kind = E->getKind();
      Kind == UETT_SizeOf || Kind == UETT_DataSizeOf) {
    if (const VariableArrayType *VAT =
            CGF.getContext().getAsVariableArrayType(TypeToSize)) {
      if (E->isArgumentType()) {
        // sizeof(type) - make sure to emit the VLA size.
        CGF.EmitVariablyModifiedType(TypeToSize);
      } else {
        // C99 6.5.3.4p2: If the argument is an expression of type
        // VLA, it is evaluated.
        CGF.EmitIgnoredExpr(E->getArgumentExpr());
      }

      auto VlaSize = CGF.getVLASize(VAT);
      llvm::Value *size = VlaSize.NumElts;

      // Scale the number of non-VLA elements by the non-VLA element size.
      CharUnits eltSize = CGF.getContext().getTypeSizeInChars(VlaSize.Type);
      if (!eltSize.isOne())
        size = CGF.Builder.CreateNUWMul(CGF.CGM.getSize(eltSize), size);

      return size;
    }
  } else if (E->getKind() == UETT_OpenMPRequiredSimdAlign) {
    auto Alignment =
        CGF.getContext()
            .toCharUnitsFromBits(CGF.getContext().getOpenMPDefaultSimdAlign(
                E->getTypeOfArgument()->getPointeeType()))
            .getQuantity();
    return llvm::ConstantInt::get(CGF.SizeTy, Alignment);
  } else if (E->getKind() == UETT_VectorElements) {
    auto *VecTy = cast<llvm::VectorType>(ConvertType(E->getTypeOfArgument()));
    return Builder.CreateElementCount(CGF.SizeTy, VecTy->getElementCount());
  }

  // If this isn't sizeof(vla), the result must be constant; use the constant
  // folding logic so we don't have to duplicate it here.
  return Builder.getInt(E->EvaluateKnownConstInt(CGF.getContext()));
}

Value *ScalarExprEmitter::VisitUnaryReal(const UnaryOperator *E,
                                         QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  Value *result = VisitReal(E, promotionTy);
  if (result && !promotionTy.isNull())
    result = EmitUnPromotedValue(result, E->getType());
  return result;
}

Value *ScalarExprEmitter::VisitReal(const UnaryOperator *E,
                                    QualType PromotionType) {
  Expr *Op = E->getSubExpr();
  if (Op->getType()->isAnyComplexType()) {
    // If it's an l-value, load through the appropriate subobject l-value.
    // Note that we have to ask E because Op might be an l-value that
    // this won't work for, e.g. an Obj-C property.
    if (E->isGLValue())  {
      if (!PromotionType.isNull()) {
        CodeGenFunction::ComplexPairTy result = CGF.EmitComplexExpr(
            Op, /*IgnoreReal*/ IgnoreResultAssign, /*IgnoreImag*/ true);
        if (result.first)
          result.first = CGF.EmitPromotedValue(result, PromotionType).first;
        return result.first;
      } else {
        return CGF.EmitLoadOfLValue(CGF.EmitLValue(E), E->getExprLoc())
            .getScalarVal();
      }
    }
    // Otherwise, calculate and project.
    return CGF.EmitComplexExpr(Op, false, true).first;
  }

  if (!PromotionType.isNull())
    return CGF.EmitPromotedScalarExpr(Op, PromotionType);
  return Visit(Op);
}

Value *ScalarExprEmitter::VisitUnaryImag(const UnaryOperator *E,
                                         QualType PromotionType) {
  QualType promotionTy = PromotionType.isNull()
                             ? getPromotionType(E->getSubExpr()->getType())
                             : PromotionType;
  Value *result = VisitImag(E, promotionTy);
  if (result && !promotionTy.isNull())
    result = EmitUnPromotedValue(result, E->getType());
  return result;
}

Value *ScalarExprEmitter::VisitImag(const UnaryOperator *E,
                                    QualType PromotionType) {
  Expr *Op = E->getSubExpr();
  if (Op->getType()->isAnyComplexType()) {
    // If it's an l-value, load through the appropriate subobject l-value.
    // Note that we have to ask E because Op might be an l-value that
    // this won't work for, e.g. an Obj-C property.
    if (Op->isGLValue()) {
      if (!PromotionType.isNull()) {
        CodeGenFunction::ComplexPairTy result = CGF.EmitComplexExpr(
            Op, /*IgnoreReal*/ true, /*IgnoreImag*/ IgnoreResultAssign);
        if (result.second)
          result.second = CGF.EmitPromotedValue(result, PromotionType).second;
        return result.second;
      } else {
        return CGF.EmitLoadOfLValue(CGF.EmitLValue(E), E->getExprLoc())
            .getScalarVal();
      }
    }
    // Otherwise, calculate and project.
    return CGF.EmitComplexExpr(Op, true, false).second;
  }

  // __imag on a scalar returns zero.  Emit the subexpr to ensure side
  // effects are evaluated, but not the actual value.
  if (Op->isGLValue())
    CGF.EmitLValue(Op);
  else if (!PromotionType.isNull())
    CGF.EmitPromotedScalarExpr(Op, PromotionType);
  else
    CGF.EmitScalarExpr(Op, true);
  if (!PromotionType.isNull())
    return llvm::Constant::getNullValue(ConvertType(PromotionType));
  return llvm::Constant::getNullValue(ConvertType(E->getType()));
}

//===----------------------------------------------------------------------===//
//                           Binary Operators
//===----------------------------------------------------------------------===//

Value *ScalarExprEmitter::EmitPromotedValue(Value *result,
                                            QualType PromotionType) {
  return CGF.Builder.CreateFPExt(result, ConvertType(PromotionType), "ext");
}

Value *ScalarExprEmitter::EmitUnPromotedValue(Value *result,
                                              QualType ExprType) {
  return CGF.Builder.CreateFPTrunc(result, ConvertType(ExprType), "unpromotion");
}

Value *ScalarExprEmitter::EmitPromoted(const Expr *E, QualType PromotionType) {
  E = E->IgnoreParens();
  if (auto BO = dyn_cast<BinaryOperator>(E)) {
    switch (BO->getOpcode()) {
#define HANDLE_BINOP(OP)                                                       \
  case BO_##OP:                                                                \
    return Emit##OP(EmitBinOps(BO, PromotionType));
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
    case UO_Imag:
      return VisitImag(UO, PromotionType);
    case UO_Real:
      return VisitReal(UO, PromotionType);
    case UO_Minus:
      return VisitMinus(UO, PromotionType);
    case UO_Plus:
      return VisitPlus(UO, PromotionType);
    default:
      break;
    }
  }
  auto result = Visit(const_cast<Expr *>(E));
  if (result) {
    if (!PromotionType.isNull())
      return EmitPromotedValue(result, PromotionType);
    else
      return EmitUnPromotedValue(result, E->getType());
  }
  return result;
}

BinOpInfo ScalarExprEmitter::EmitBinOps(const BinaryOperator *E,
                                        QualType PromotionType) {
  TestAndClearIgnoreResultAssign();
  BinOpInfo Result;
  Result.LHS = CGF.EmitPromotedScalarExpr(E->getLHS(), PromotionType);
  Result.RHS = CGF.EmitPromotedScalarExpr(E->getRHS(), PromotionType);
  if (!PromotionType.isNull())
    Result.Ty = PromotionType;
  else
    Result.Ty  = E->getType();
  Result.Opcode = E->getOpcode();
  Result.FPFeatures = E->getFPFeaturesInEffect(CGF.getLangOpts());
  Result.E = E;
  return Result;
}

LValue ScalarExprEmitter::EmitCompoundAssignLValue(
                                              const CompoundAssignOperator *E,
                        Value *(ScalarExprEmitter::*Func)(const BinOpInfo &),
                                                   Value *&Result) {
  QualType LHSTy = E->getLHS()->getType();
  BinOpInfo OpInfo;

  if (E->getComputationResultType()->isAnyComplexType())
    return CGF.EmitScalarCompoundAssignWithComplex(E, Result);

  // Emit the RHS first.  __block variables need to have the rhs evaluated
  // first, plus this should improve codegen a little.

  QualType PromotionTypeCR;
  PromotionTypeCR = getPromotionType(E->getComputationResultType());
  if (PromotionTypeCR.isNull())
      PromotionTypeCR = E->getComputationResultType();
  QualType PromotionTypeLHS = getPromotionType(E->getComputationLHSType());
  QualType PromotionTypeRHS = getPromotionType(E->getRHS()->getType());
  if (!PromotionTypeRHS.isNull())
    OpInfo.RHS = CGF.EmitPromotedScalarExpr(E->getRHS(), PromotionTypeRHS);
  else
    OpInfo.RHS = Visit(E->getRHS());
  OpInfo.Ty = PromotionTypeCR;
  OpInfo.Opcode = E->getOpcode();
  OpInfo.FPFeatures = E->getFPFeaturesInEffect(CGF.getLangOpts());
  OpInfo.E = E;
  // Load/convert the LHS.
  LValue LHSLV = EmitCheckedLValue(E->getLHS(), CodeGenFunction::TCK_Store);

  llvm::PHINode *atomicPHI = nullptr;
  if (const AtomicType *atomicTy = LHSTy->getAs<AtomicType>()) {
    QualType type = atomicTy->getValueType();
    if (!type->isBooleanType() && type->isIntegerType() &&
        !(type->isUnsignedIntegerType() &&
          CGF.SanOpts.has(SanitizerKind::UnsignedIntegerOverflow)) &&
        CGF.getLangOpts().getSignedOverflowBehavior() !=
            LangOptions::SOB_Trapping) {
      llvm::AtomicRMWInst::BinOp AtomicOp = llvm::AtomicRMWInst::BAD_BINOP;
      llvm::Instruction::BinaryOps Op;
      switch (OpInfo.Opcode) {
        // We don't have atomicrmw operands for *, %, /, <<, >>
        case BO_MulAssign: case BO_DivAssign:
        case BO_RemAssign:
        case BO_ShlAssign:
        case BO_ShrAssign:
          break;
        case BO_AddAssign:
          AtomicOp = llvm::AtomicRMWInst::Add;
          Op = llvm::Instruction::Add;
          break;
        case BO_SubAssign:
          AtomicOp = llvm::AtomicRMWInst::Sub;
          Op = llvm::Instruction::Sub;
          break;
        case BO_AndAssign:
          AtomicOp = llvm::AtomicRMWInst::And;
          Op = llvm::Instruction::And;
          break;
        case BO_XorAssign:
          AtomicOp = llvm::AtomicRMWInst::Xor;
          Op = llvm::Instruction::Xor;
          break;
        case BO_OrAssign:
          AtomicOp = llvm::AtomicRMWInst::Or;
          Op = llvm::Instruction::Or;
          break;
        default:
          llvm_unreachable("Invalid compound assignment type");
      }
      if (AtomicOp != llvm::AtomicRMWInst::BAD_BINOP) {
        llvm::Value *Amt = CGF.EmitToMemory(
            EmitScalarConversion(OpInfo.RHS, E->getRHS()->getType(), LHSTy,
                                 E->getExprLoc()),
            LHSTy);
        Value *OldVal = Builder.CreateAtomicRMW(
            AtomicOp, LHSLV.getAddress(), Amt,
            llvm::AtomicOrdering::SequentiallyConsistent);

        // Since operation is atomic, the result type is guaranteed to be the
        // same as the input in LLVM terms.
        Result = Builder.CreateBinOp(Op, OldVal, Amt);
        return LHSLV;
      }
    }
    // FIXME: For floating point types, we should be saving and restoring the
    // floating point environment in the loop.
    llvm::BasicBlock *startBB = Builder.GetInsertBlock();
    llvm::BasicBlock *opBB = CGF.createBasicBlock("atomic_op", CGF.CurFn);
    OpInfo.LHS = EmitLoadOfLValue(LHSLV, E->getExprLoc());
    OpInfo.LHS = CGF.EmitToMemory(OpInfo.LHS, type);
    Builder.CreateBr(opBB);
    Builder.SetInsertPoint(opBB);
    atomicPHI = Builder.CreatePHI(OpInfo.LHS->getType(), 2);
    atomicPHI->addIncoming(OpInfo.LHS, startBB);
    OpInfo.LHS = atomicPHI;
  }
  else
    OpInfo.LHS = EmitLoadOfLValue(LHSLV, E->getExprLoc());

  CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, OpInfo.FPFeatures);
  SourceLocation Loc = E->getExprLoc();
  if (!PromotionTypeLHS.isNull())
    OpInfo.LHS = EmitScalarConversion(OpInfo.LHS, LHSTy, PromotionTypeLHS,
                                      E->getExprLoc());
  else
    OpInfo.LHS = EmitScalarConversion(OpInfo.LHS, LHSTy,
                                      E->getComputationLHSType(), Loc);

  // Expand the binary operator.
  Result = (this->*Func)(OpInfo);

  // Convert the result back to the LHS type,
  // potentially with Implicit Conversion sanitizer check.
  // If LHSLV is a bitfield, use default ScalarConversionOpts
  // to avoid emit any implicit integer checks.
  Value *Previous = nullptr;
  if (LHSLV.isBitField()) {
    Previous = Result;
    Result = EmitScalarConversion(Result, PromotionTypeCR, LHSTy, Loc);
  } else
    Result = EmitScalarConversion(Result, PromotionTypeCR, LHSTy, Loc,
                                  ScalarConversionOpts(CGF.SanOpts));

  if (atomicPHI) {
    llvm::BasicBlock *curBlock = Builder.GetInsertBlock();
    llvm::BasicBlock *contBB = CGF.createBasicBlock("atomic_cont", CGF.CurFn);
    auto Pair = CGF.EmitAtomicCompareExchange(
        LHSLV, RValue::get(atomicPHI), RValue::get(Result), E->getExprLoc());
    llvm::Value *old = CGF.EmitToMemory(Pair.first.getScalarVal(), LHSTy);
    llvm::Value *success = Pair.second;
    atomicPHI->addIncoming(old, curBlock);
    Builder.CreateCondBr(success, contBB, atomicPHI->getParent());
    Builder.SetInsertPoint(contBB);
    return LHSLV;
  }

  // Store the result value into the LHS lvalue. Bit-fields are handled
  // specially because the result is altered by the store, i.e., [C99 6.5.16p1]
  // 'An assignment expression has the value of the left operand after the
  // assignment...'.
  if (LHSLV.isBitField()) {
    Value *Src = Previous ? Previous : Result;
    QualType SrcType = E->getRHS()->getType();
    QualType DstType = E->getLHS()->getType();
    CGF.EmitStoreThroughBitfieldLValue(RValue::get(Result), LHSLV, &Result);
    CGF.EmitBitfieldConversionCheck(Src, SrcType, Result, DstType,
                                    LHSLV.getBitFieldInfo(), E->getExprLoc());
  } else
    CGF.EmitStoreThroughLValue(RValue::get(Result), LHSLV);

  if (CGF.getLangOpts().OpenMP)
    CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF,
                                                                  E->getLHS());
  return LHSLV;
}

Value *ScalarExprEmitter::EmitCompoundAssign(const CompoundAssignOperator *E,
                      Value *(ScalarExprEmitter::*Func)(const BinOpInfo &)) {
  bool Ignore = TestAndClearIgnoreResultAssign();
  Value *RHS = nullptr;
  LValue LHS = EmitCompoundAssignLValue(E, Func, RHS);

  // If the result is clearly ignored, return now.
  if (Ignore)
    return nullptr;

  // The result of an assignment in C is the assigned r-value.
  if (!CGF.getLangOpts().CPlusPlus)
    return RHS;

  // If the lvalue is non-volatile, return the computed value of the assignment.
  if (!LHS.isVolatileQualified())
    return RHS;

  // Otherwise, reload the value.
  return EmitLoadOfLValue(LHS, E->getExprLoc());
}

void ScalarExprEmitter::EmitUndefinedBehaviorIntegerDivAndRemCheck(
    const BinOpInfo &Ops, llvm::Value *Zero, bool isDiv) {
  SmallVector<std::pair<llvm::Value *, SanitizerMask>, 2> Checks;

  if (CGF.SanOpts.has(SanitizerKind::IntegerDivideByZero)) {
    Checks.push_back(std::make_pair(Builder.CreateICmpNE(Ops.RHS, Zero),
                                    SanitizerKind::IntegerDivideByZero));
  }

  const auto *BO = cast<BinaryOperator>(Ops.E);
  if (CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow) &&
      Ops.Ty->hasSignedIntegerRepresentation() &&
      !IsWidenedIntegerOp(CGF.getContext(), BO->getLHS()) &&
      Ops.mayHaveIntegerOverflow()) {
    llvm::IntegerType *Ty = cast<llvm::IntegerType>(Zero->getType());

    llvm::Value *IntMin =
      Builder.getInt(llvm::APInt::getSignedMinValue(Ty->getBitWidth()));
    llvm::Value *NegOne = llvm::Constant::getAllOnesValue(Ty);

    llvm::Value *LHSCmp = Builder.CreateICmpNE(Ops.LHS, IntMin);
    llvm::Value *RHSCmp = Builder.CreateICmpNE(Ops.RHS, NegOne);
    llvm::Value *NotOverflow = Builder.CreateOr(LHSCmp, RHSCmp, "or");
    Checks.push_back(
        std::make_pair(NotOverflow, SanitizerKind::SignedIntegerOverflow));
  }

  if (Checks.size() > 0)
    EmitBinOpCheck(Checks, Ops);
}

Value *ScalarExprEmitter::EmitDiv(const BinOpInfo &Ops) {
  {
    CodeGenFunction::SanitizerScope SanScope(&CGF);
    if ((CGF.SanOpts.has(SanitizerKind::IntegerDivideByZero) ||
         CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow)) &&
        Ops.Ty->isIntegerType() &&
        (Ops.mayHaveIntegerDivisionByZero() || Ops.mayHaveIntegerOverflow())) {
      llvm::Value *Zero = llvm::Constant::getNullValue(ConvertType(Ops.Ty));
      EmitUndefinedBehaviorIntegerDivAndRemCheck(Ops, Zero, true);
    } else if (CGF.SanOpts.has(SanitizerKind::FloatDivideByZero) &&
               Ops.Ty->isRealFloatingType() &&
               Ops.mayHaveFloatDivisionByZero()) {
      llvm::Value *Zero = llvm::Constant::getNullValue(ConvertType(Ops.Ty));
      llvm::Value *NonZero = Builder.CreateFCmpUNE(Ops.RHS, Zero);
      EmitBinOpCheck(std::make_pair(NonZero, SanitizerKind::FloatDivideByZero),
                     Ops);
    }
  }

  if (Ops.Ty->isConstantMatrixType()) {
    llvm::MatrixBuilder MB(Builder);
    // We need to check the types of the operands of the operator to get the
    // correct matrix dimensions.
    auto *BO = cast<BinaryOperator>(Ops.E);
    (void)BO;
    assert(
        isa<ConstantMatrixType>(BO->getLHS()->getType().getCanonicalType()) &&
        "first operand must be a matrix");
    assert(BO->getRHS()->getType().getCanonicalType()->isArithmeticType() &&
           "second operand must be an arithmetic type");
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Ops.FPFeatures);
    return MB.CreateScalarDiv(Ops.LHS, Ops.RHS,
                              Ops.Ty->hasUnsignedIntegerRepresentation());
  }

  if (Ops.LHS->getType()->isFPOrFPVectorTy()) {
    llvm::Value *Val;
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, Ops.FPFeatures);
    Val = Builder.CreateFDiv(Ops.LHS, Ops.RHS, "div");
    CGF.SetDivFPAccuracy(Val);
    return Val;
  }
  else if (Ops.isFixedPointOp())
    return EmitFixedPointBinOp(Ops);
  else if (Ops.Ty->hasUnsignedIntegerRepresentation())
    return Builder.CreateUDiv(Ops.LHS, Ops.RHS, "div");
  else
    return Builder.CreateSDiv(Ops.LHS, Ops.RHS, "div");
}

Value *ScalarExprEmitter::EmitRem(const BinOpInfo &Ops) {
  // Rem in C can't be a floating point type: C99 6.5.5p2.
  if ((CGF.SanOpts.has(SanitizerKind::IntegerDivideByZero) ||
       CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow)) &&
      Ops.Ty->isIntegerType() &&
      (Ops.mayHaveIntegerDivisionByZero() || Ops.mayHaveIntegerOverflow())) {
    CodeGenFunction::SanitizerScope SanScope(&CGF);
    llvm::Value *Zero = llvm::Constant::getNullValue(ConvertType(Ops.Ty));
    EmitUndefinedBehaviorIntegerDivAndRemCheck(Ops, Zero, false);
  }

  if (Ops.Ty->hasUnsignedIntegerRepresentation())
    return Builder.CreateURem(Ops.LHS, Ops.RHS, "rem");
  else
    return Builder.CreateSRem(Ops.LHS, Ops.RHS, "rem");
}

Value *ScalarExprEmitter::EmitOverflowCheckedBinOp(const BinOpInfo &Ops) {
  unsigned IID;
  unsigned OpID = 0;
  SanitizerHandler OverflowKind;

  bool isSigned = Ops.Ty->isSignedIntegerOrEnumerationType();
  switch (Ops.Opcode) {
  case BO_Add:
  case BO_AddAssign:
    OpID = 1;
    IID = isSigned ? llvm::Intrinsic::sadd_with_overflow :
                     llvm::Intrinsic::uadd_with_overflow;
    OverflowKind = SanitizerHandler::AddOverflow;
    break;
  case BO_Sub:
  case BO_SubAssign:
    OpID = 2;
    IID = isSigned ? llvm::Intrinsic::ssub_with_overflow :
                     llvm::Intrinsic::usub_with_overflow;
    OverflowKind = SanitizerHandler::SubOverflow;
    break;
  case BO_Mul:
  case BO_MulAssign:
    OpID = 3;
    IID = isSigned ? llvm::Intrinsic::smul_with_overflow :
                     llvm::Intrinsic::umul_with_overflow;
    OverflowKind = SanitizerHandler::MulOverflow;
    break;
  default:
    llvm_unreachable("Unsupported operation for overflow detection");
  }
  OpID <<= 1;
  if (isSigned)
    OpID |= 1;

  CodeGenFunction::SanitizerScope SanScope(&CGF);
  llvm::Type *opTy = CGF.CGM.getTypes().ConvertType(Ops.Ty);

  llvm::Function *intrinsic = CGF.CGM.getIntrinsic(IID, opTy);

  Value *resultAndOverflow = Builder.CreateCall(intrinsic, {Ops.LHS, Ops.RHS});
  Value *result = Builder.CreateExtractValue(resultAndOverflow, 0);
  Value *overflow = Builder.CreateExtractValue(resultAndOverflow, 1);

  // Handle overflow with llvm.trap if no custom handler has been specified.
  const std::string *handlerName =
    &CGF.getLangOpts().OverflowHandler;
  if (handlerName->empty()) {
    // If the signed-integer-overflow sanitizer is enabled, emit a call to its
    // runtime. Otherwise, this is a -ftrapv check, so just emit a trap.
    if (!isSigned || CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow)) {
      llvm::Value *NotOverflow = Builder.CreateNot(overflow);
      SanitizerMask Kind = isSigned ? SanitizerKind::SignedIntegerOverflow
                              : SanitizerKind::UnsignedIntegerOverflow;
      EmitBinOpCheck(std::make_pair(NotOverflow, Kind), Ops);
    } else
      CGF.EmitTrapCheck(Builder.CreateNot(overflow), OverflowKind);
    return result;
  }

  // Branch in case of overflow.
  llvm::BasicBlock *initialBB = Builder.GetInsertBlock();
  llvm::BasicBlock *continueBB =
      CGF.createBasicBlock("nooverflow", CGF.CurFn, initialBB->getNextNode());
  llvm::BasicBlock *overflowBB = CGF.createBasicBlock("overflow", CGF.CurFn);

  Builder.CreateCondBr(overflow, overflowBB, continueBB);

  // If an overflow handler is set, then we want to call it and then use its
  // result, if it returns.
  Builder.SetInsertPoint(overflowBB);

  // Get the overflow handler.
  llvm::Type *Int8Ty = CGF.Int8Ty;
  llvm::Type *argTypes[] = { CGF.Int64Ty, CGF.Int64Ty, Int8Ty, Int8Ty };
  llvm::FunctionType *handlerTy =
      llvm::FunctionType::get(CGF.Int64Ty, argTypes, true);
  llvm::FunctionCallee handler =
      CGF.CGM.CreateRuntimeFunction(handlerTy, *handlerName);

  // Sign extend the args to 64-bit, so that we can use the same handler for
  // all types of overflow.
  llvm::Value *lhs = Builder.CreateSExt(Ops.LHS, CGF.Int64Ty);
  llvm::Value *rhs = Builder.CreateSExt(Ops.RHS, CGF.Int64Ty);

  // Call the handler with the two arguments, the operation, and the size of
  // the result.
  llvm::Value *handlerArgs[] = {
    lhs,
    rhs,
    Builder.getInt8(OpID),
    Builder.getInt8(cast<llvm::IntegerType>(opTy)->getBitWidth())
  };
  llvm::Value *handlerResult =
    CGF.EmitNounwindRuntimeCall(handler, handlerArgs);

  // Truncate the result back to the desired size.
  handlerResult = Builder.CreateTrunc(handlerResult, opTy);
  Builder.CreateBr(continueBB);

  Builder.SetInsertPoint(continueBB);
  llvm::PHINode *phi = Builder.CreatePHI(opTy, 2);
  phi->addIncoming(result, initialBB);
  phi->addIncoming(handlerResult, overflowBB);

  return phi;
}

/// Emit pointer + index arithmetic.
static Value *emitPointerArithmetic(CodeGenFunction &CGF,
                                    const BinOpInfo &op,
                                    bool isSubtraction) {
  // Must have binary (not unary) expr here.  Unary pointer
  // increment/decrement doesn't use this path.
  const BinaryOperator *expr = cast<BinaryOperator>(op.E);

  Value *pointer = op.LHS;
  Expr *pointerOperand = expr->getLHS();
  Value *index = op.RHS;
  Expr *indexOperand = expr->getRHS();

  // In a subtraction, the LHS is always the pointer.
  if (!isSubtraction && !pointer->getType()->isPointerTy()) {
    std::swap(pointer, index);
    std::swap(pointerOperand, indexOperand);
  }

  bool isSigned = indexOperand->getType()->isSignedIntegerOrEnumerationType();

  unsigned width = cast<llvm::IntegerType>(index->getType())->getBitWidth();
  auto &DL = CGF.CGM.getDataLayout();
  auto PtrTy = cast<llvm::PointerType>(pointer->getType());

  // Some versions of glibc and gcc use idioms (particularly in their malloc
  // routines) that add a pointer-sized integer (known to be a pointer value)
  // to a null pointer in order to cast the value back to an integer or as
  // part of a pointer alignment algorithm.  This is undefined behavior, but
  // we'd like to be able to compile programs that use it.
  //
  // Normally, we'd generate a GEP with a null-pointer base here in response
  // to that code, but it's also UB to dereference a pointer created that
  // way.  Instead (as an acknowledged hack to tolerate the idiom) we will
  // generate a direct cast of the integer value to a pointer.
  //
  // The idiom (p = nullptr + N) is not met if any of the following are true:
  //
  //   The operation is subtraction.
  //   The index is not pointer-sized.
  //   The pointer type is not byte-sized.
  //
  if (BinaryOperator::isNullPointerArithmeticExtension(CGF.getContext(),
                                                       op.Opcode,
                                                       expr->getLHS(),
                                                       expr->getRHS()))
    return CGF.Builder.CreateIntToPtr(index, pointer->getType());

  if (width != DL.getIndexTypeSizeInBits(PtrTy)) {
    // Zero-extend or sign-extend the pointer value according to
    // whether the index is signed or not.
    index = CGF.Builder.CreateIntCast(index, DL.getIndexType(PtrTy), isSigned,
                                      "idx.ext");
  }

  // If this is subtraction, negate the index.
  if (isSubtraction)
    index = CGF.Builder.CreateNeg(index, "idx.neg");

  if (CGF.SanOpts.has(SanitizerKind::ArrayBounds))
    CGF.EmitBoundsCheck(op.E, pointerOperand, index, indexOperand->getType(),
                        /*Accessed*/ false);

  const PointerType *pointerType
    = pointerOperand->getType()->getAs<PointerType>();
  if (!pointerType) {
    QualType objectType = pointerOperand->getType()
                                        ->castAs<ObjCObjectPointerType>()
                                        ->getPointeeType();
    llvm::Value *objectSize
      = CGF.CGM.getSize(CGF.getContext().getTypeSizeInChars(objectType));

    index = CGF.Builder.CreateMul(index, objectSize);

    Value *result =
        CGF.Builder.CreateGEP(CGF.Int8Ty, pointer, index, "add.ptr");
    return CGF.Builder.CreateBitCast(result, pointer->getType());
  }

  QualType elementType = pointerType->getPointeeType();
  if (const VariableArrayType *vla
        = CGF.getContext().getAsVariableArrayType(elementType)) {
    // The element count here is the total number of non-VLA elements.
    llvm::Value *numElements = CGF.getVLASize(vla).NumElts;

    // Effectively, the multiply by the VLA size is part of the GEP.
    // GEP indexes are signed, and scaling an index isn't permitted to
    // signed-overflow, so we use the same semantics for our explicit
    // multiply.  We suppress this if overflow is not undefined behavior.
    llvm::Type *elemTy = CGF.ConvertTypeForMem(vla->getElementType());
    if (CGF.getLangOpts().isSignedOverflowDefined()) {
      index = CGF.Builder.CreateMul(index, numElements, "vla.index");
      pointer = CGF.Builder.CreateGEP(elemTy, pointer, index, "add.ptr");
    } else {
      index = CGF.Builder.CreateNSWMul(index, numElements, "vla.index");
      pointer = CGF.EmitCheckedInBoundsGEP(
          elemTy, pointer, index, isSigned, isSubtraction, op.E->getExprLoc(),
          "add.ptr");
    }
    return pointer;
  }

  // Explicitly handle GNU void* and function pointer arithmetic extensions. The
  // GNU void* casts amount to no-ops since our void* type is i8*, but this is
  // future proof.
  llvm::Type *elemTy;
  if (elementType->isVoidType() || elementType->isFunctionType())
    elemTy = CGF.Int8Ty;
  else
    elemTy = CGF.ConvertTypeForMem(elementType);

  if (CGF.getLangOpts().isSignedOverflowDefined())
    return CGF.Builder.CreateGEP(elemTy, pointer, index, "add.ptr");

  return CGF.EmitCheckedInBoundsGEP(
      elemTy, pointer, index, isSigned, isSubtraction, op.E->getExprLoc(),
      "add.ptr");
}

// Construct an fmuladd intrinsic to represent a fused mul-add of MulOp and
// Addend. Use negMul and negAdd to negate the first operand of the Mul or
// the add operand respectively. This allows fmuladd to represent a*b-c, or
// c-a*b. Patterns in LLVM should catch the negated forms and translate them to
// efficient operations.
static Value* buildFMulAdd(llvm::Instruction *MulOp, Value *Addend,
                           const CodeGenFunction &CGF, CGBuilderTy &Builder,
                           bool negMul, bool negAdd) {
  Value *MulOp0 = MulOp->getOperand(0);
  Value *MulOp1 = MulOp->getOperand(1);
  if (negMul)
    MulOp0 = Builder.CreateFNeg(MulOp0, "neg");
  if (negAdd)
    Addend = Builder.CreateFNeg(Addend, "neg");

  Value *FMulAdd = nullptr;
  if (Builder.getIsFPConstrained()) {
    assert(isa<llvm::ConstrainedFPIntrinsic>(MulOp) &&
           "Only constrained operation should be created when Builder is in FP "
           "constrained mode");
    FMulAdd = Builder.CreateConstrainedFPCall(
        CGF.CGM.getIntrinsic(llvm::Intrinsic::experimental_constrained_fmuladd,
                             Addend->getType()),
        {MulOp0, MulOp1, Addend});
  } else {
    FMulAdd = Builder.CreateCall(
        CGF.CGM.getIntrinsic(llvm::Intrinsic::fmuladd, Addend->getType()),
        {MulOp0, MulOp1, Addend});
  }
  MulOp->eraseFromParent();

  return FMulAdd;
}

// Check whether it would be legal to emit an fmuladd intrinsic call to
// represent op and if so, build the fmuladd.
//
// Checks that (a) the operation is fusable, and (b) -ffp-contract=on.
// Does NOT check the type of the operation - it's assumed that this function
// will be called from contexts where it's known that the type is contractable.
static Value* tryEmitFMulAdd(const BinOpInfo &op,
                         const CodeGenFunction &CGF, CGBuilderTy &Builder,
                         bool isSub=false) {

  assert((op.Opcode == BO_Add || op.Opcode == BO_AddAssign ||
          op.Opcode == BO_Sub || op.Opcode == BO_SubAssign) &&
         "Only fadd/fsub can be the root of an fmuladd.");

  // Check whether this op is marked as fusable.
  if (!op.FPFeatures.allowFPContractWithinStatement())
    return nullptr;

  Value *LHS = op.LHS;
  Value *RHS = op.RHS;

  // Peek through fneg to look for fmul. Make sure fneg has no users, and that
  // it is the only use of its operand.
  bool NegLHS = false;
  if (auto *LHSUnOp = dyn_cast<llvm::UnaryOperator>(LHS)) {
    if (LHSUnOp->getOpcode() == llvm::Instruction::FNeg &&
        LHSUnOp->use_empty() && LHSUnOp->getOperand(0)->hasOneUse()) {
      LHS = LHSUnOp->getOperand(0);
      NegLHS = true;
    }
  }

  bool NegRHS = false;
  if (auto *RHSUnOp = dyn_cast<llvm::UnaryOperator>(RHS)) {
    if (RHSUnOp->getOpcode() == llvm::Instruction::FNeg &&
        RHSUnOp->use_empty() && RHSUnOp->getOperand(0)->hasOneUse()) {
      RHS = RHSUnOp->getOperand(0);
      NegRHS = true;
    }
  }

  // We have a potentially fusable op. Look for a mul on one of the operands.
  // Also, make sure that the mul result isn't used directly. In that case,
  // there's no point creating a muladd operation.
  if (auto *LHSBinOp = dyn_cast<llvm::BinaryOperator>(LHS)) {
    if (LHSBinOp->getOpcode() == llvm::Instruction::FMul &&
        (LHSBinOp->use_empty() || NegLHS)) {
      // If we looked through fneg, erase it.
      if (NegLHS)
        cast<llvm::Instruction>(op.LHS)->eraseFromParent();
      return buildFMulAdd(LHSBinOp, op.RHS, CGF, Builder, NegLHS, isSub);
    }
  }
  if (auto *RHSBinOp = dyn_cast<llvm::BinaryOperator>(RHS)) {
    if (RHSBinOp->getOpcode() == llvm::Instruction::FMul &&
        (RHSBinOp->use_empty() || NegRHS)) {
      // If we looked through fneg, erase it.
      if (NegRHS)
        cast<llvm::Instruction>(op.RHS)->eraseFromParent();
      return buildFMulAdd(RHSBinOp, op.LHS, CGF, Builder, isSub ^ NegRHS, false);
    }
  }

  if (auto *LHSBinOp = dyn_cast<llvm::CallBase>(LHS)) {
    if (LHSBinOp->getIntrinsicID() ==
            llvm::Intrinsic::experimental_constrained_fmul &&
        (LHSBinOp->use_empty() || NegLHS)) {
      // If we looked through fneg, erase it.
      if (NegLHS)
        cast<llvm::Instruction>(op.LHS)->eraseFromParent();
      return buildFMulAdd(LHSBinOp, op.RHS, CGF, Builder, NegLHS, isSub);
    }
  }
  if (auto *RHSBinOp = dyn_cast<llvm::CallBase>(RHS)) {
    if (RHSBinOp->getIntrinsicID() ==
            llvm::Intrinsic::experimental_constrained_fmul &&
        (RHSBinOp->use_empty() || NegRHS)) {
      // If we looked through fneg, erase it.
      if (NegRHS)
        cast<llvm::Instruction>(op.RHS)->eraseFromParent();
      return buildFMulAdd(RHSBinOp, op.LHS, CGF, Builder, isSub ^ NegRHS, false);
    }
  }

  return nullptr;
}

Value *ScalarExprEmitter::EmitAdd(const BinOpInfo &op) {
  if (op.LHS->getType()->isPointerTy() ||
      op.RHS->getType()->isPointerTy())
    return emitPointerArithmetic(CGF, op, CodeGenFunction::NotSubtraction);

  if (op.Ty->isSignedIntegerOrEnumerationType()) {
    switch (CGF.getLangOpts().getSignedOverflowBehavior()) {
    case LangOptions::SOB_Defined:
      if (!CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow))
        return Builder.CreateAdd(op.LHS, op.RHS, "add");
      [[fallthrough]];
    case LangOptions::SOB_Undefined:
      if (!CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow))
        return Builder.CreateNSWAdd(op.LHS, op.RHS, "add");
      [[fallthrough]];
    case LangOptions::SOB_Trapping:
      if (CanElideOverflowCheck(CGF.getContext(), op))
        return Builder.CreateNSWAdd(op.LHS, op.RHS, "add");
      return EmitOverflowCheckedBinOp(op);
    }
  }

  // For vector and matrix adds, try to fold into a fmuladd.
  if (op.LHS->getType()->isFPOrFPVectorTy()) {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, op.FPFeatures);
    // Try to form an fmuladd.
    if (Value *FMulAdd = tryEmitFMulAdd(op, CGF, Builder))
      return FMulAdd;
  }

  if (op.Ty->isConstantMatrixType()) {
    llvm::MatrixBuilder MB(Builder);
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, op.FPFeatures);
    return MB.CreateAdd(op.LHS, op.RHS);
  }

  if (op.Ty->isUnsignedIntegerType() &&
      CGF.SanOpts.has(SanitizerKind::UnsignedIntegerOverflow) &&
      !CanElideOverflowCheck(CGF.getContext(), op))
    return EmitOverflowCheckedBinOp(op);

  if (op.LHS->getType()->isFPOrFPVectorTy()) {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, op.FPFeatures);
    return Builder.CreateFAdd(op.LHS, op.RHS, "add");
  }

  if (op.isFixedPointOp())
    return EmitFixedPointBinOp(op);

  return Builder.CreateAdd(op.LHS, op.RHS, "add");
}

/// The resulting value must be calculated with exact precision, so the operands
/// may not be the same type.
Value *ScalarExprEmitter::EmitFixedPointBinOp(const BinOpInfo &op) {
  using llvm::APSInt;
  using llvm::ConstantInt;

  // This is either a binary operation where at least one of the operands is
  // a fixed-point type, or a unary operation where the operand is a fixed-point
  // type. The result type of a binary operation is determined by
  // Sema::handleFixedPointConversions().
  QualType ResultTy = op.Ty;
  QualType LHSTy, RHSTy;
  if (const auto *BinOp = dyn_cast<BinaryOperator>(op.E)) {
    RHSTy = BinOp->getRHS()->getType();
    if (const auto *CAO = dyn_cast<CompoundAssignOperator>(BinOp)) {
      // For compound assignment, the effective type of the LHS at this point
      // is the computation LHS type, not the actual LHS type, and the final
      // result type is not the type of the expression but rather the
      // computation result type.
      LHSTy = CAO->getComputationLHSType();
      ResultTy = CAO->getComputationResultType();
    } else
      LHSTy = BinOp->getLHS()->getType();
  } else if (const auto *UnOp = dyn_cast<UnaryOperator>(op.E)) {
    LHSTy = UnOp->getSubExpr()->getType();
    RHSTy = UnOp->getSubExpr()->getType();
  }
  ASTContext &Ctx = CGF.getContext();
  Value *LHS = op.LHS;
  Value *RHS = op.RHS;

  auto LHSFixedSema = Ctx.getFixedPointSemantics(LHSTy);
  auto RHSFixedSema = Ctx.getFixedPointSemantics(RHSTy);
  auto ResultFixedSema = Ctx.getFixedPointSemantics(ResultTy);
  auto CommonFixedSema = LHSFixedSema.getCommonSemantics(RHSFixedSema);

  // Perform the actual operation.
  Value *Result;
  llvm::FixedPointBuilder<CGBuilderTy> FPBuilder(Builder);
  switch (op.Opcode) {
  case BO_AddAssign:
  case BO_Add:
    Result = FPBuilder.CreateAdd(LHS, LHSFixedSema, RHS, RHSFixedSema);
    break;
  case BO_SubAssign:
  case BO_Sub:
    Result = FPBuilder.CreateSub(LHS, LHSFixedSema, RHS, RHSFixedSema);
    break;
  case BO_MulAssign:
  case BO_Mul:
    Result = FPBuilder.CreateMul(LHS, LHSFixedSema, RHS, RHSFixedSema);
    break;
  case BO_DivAssign:
  case BO_Div:
    Result = FPBuilder.CreateDiv(LHS, LHSFixedSema, RHS, RHSFixedSema);
    break;
  case BO_ShlAssign:
  case BO_Shl:
    Result = FPBuilder.CreateShl(LHS, LHSFixedSema, RHS);
    break;
  case BO_ShrAssign:
  case BO_Shr:
    Result = FPBuilder.CreateShr(LHS, LHSFixedSema, RHS);
    break;
  case BO_LT:
    return FPBuilder.CreateLT(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_GT:
    return FPBuilder.CreateGT(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_LE:
    return FPBuilder.CreateLE(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_GE:
    return FPBuilder.CreateGE(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_EQ:
    // For equality operations, we assume any padding bits on unsigned types are
    // zero'd out. They could be overwritten through non-saturating operations
    // that cause overflow, but this leads to undefined behavior.
    return FPBuilder.CreateEQ(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_NE:
    return FPBuilder.CreateNE(LHS, LHSFixedSema, RHS, RHSFixedSema);
  case BO_Cmp:
  case BO_LAnd:
  case BO_LOr:
    llvm_unreachable("Found unimplemented fixed point binary operation");
  case BO_PtrMemD:
  case BO_PtrMemI:
  case BO_Rem:
  case BO_Xor:
  case BO_And:
  case BO_Or:
  case BO_Assign:
  case BO_RemAssign:
  case BO_AndAssign:
  case BO_XorAssign:
  case BO_OrAssign:
  case BO_Comma:
    llvm_unreachable("Found unsupported binary operation for fixed point types.");
  }

  bool IsShift = BinaryOperator::isShiftOp(op.Opcode) ||
                 BinaryOperator::isShiftAssignOp(op.Opcode);
  // Convert to the result type.
  return FPBuilder.CreateFixedToFixed(Result, IsShift ? LHSFixedSema
                                                      : CommonFixedSema,
                                      ResultFixedSema);
}

Value *ScalarExprEmitter::EmitSub(const BinOpInfo &op) {
  // The LHS is always a pointer if either side is.
  if (!op.LHS->getType()->isPointerTy()) {
    if (op.Ty->isSignedIntegerOrEnumerationType()) {
      switch (CGF.getLangOpts().getSignedOverflowBehavior()) {
      case LangOptions::SOB_Defined:
        if (!CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow))
          return Builder.CreateSub(op.LHS, op.RHS, "sub");
        [[fallthrough]];
      case LangOptions::SOB_Undefined:
        if (!CGF.SanOpts.has(SanitizerKind::SignedIntegerOverflow))
          return Builder.CreateNSWSub(op.LHS, op.RHS, "sub");
        [[fallthrough]];
      case LangOptions::SOB_Trapping:
        if (CanElideOverflowCheck(CGF.getContext(), op))
          return Builder.CreateNSWSub(op.LHS, op.RHS, "sub");
        return EmitOverflowCheckedBinOp(op);
      }
    }

    // For vector and matrix subs, try to fold into a fmuladd.
    if (op.LHS->getType()->isFPOrFPVectorTy()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, op.FPFeatures);
      // Try to form an fmuladd.
      if (Value *FMulAdd = tryEmitFMulAdd(op, CGF, Builder, true))
        return FMulAdd;
    }

    if (op.Ty->isConstantMatrixType()) {
      llvm::MatrixBuilder MB(Builder);
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, op.FPFeatures);
      return MB.CreateSub(op.LHS, op.RHS);
    }

    if (op.Ty->isUnsignedIntegerType() &&
        CGF.SanOpts.has(SanitizerKind::UnsignedIntegerOverflow) &&
        !CanElideOverflowCheck(CGF.getContext(), op))
      return EmitOverflowCheckedBinOp(op);

    if (op.LHS->getType()->isFPOrFPVectorTy()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, op.FPFeatures);
      return Builder.CreateFSub(op.LHS, op.RHS, "sub");
    }

    if (op.isFixedPointOp())
      return EmitFixedPointBinOp(op);

    return Builder.CreateSub(op.LHS, op.RHS, "sub");
  }

  // If the RHS is not a pointer, then we have normal pointer
  // arithmetic.
  if (!op.RHS->getType()->isPointerTy())
    return emitPointerArithmetic(CGF, op, CodeGenFunction::IsSubtraction);

  // Otherwise, this is a pointer subtraction.

  // Do the raw subtraction part.
  llvm::Value *LHS
    = Builder.CreatePtrToInt(op.LHS, CGF.PtrDiffTy, "sub.ptr.lhs.cast");
  llvm::Value *RHS
    = Builder.CreatePtrToInt(op.RHS, CGF.PtrDiffTy, "sub.ptr.rhs.cast");
  Value *diffInChars = Builder.CreateSub(LHS, RHS, "sub.ptr.sub");

  // Okay, figure out the element size.
  const BinaryOperator *expr = cast<BinaryOperator>(op.E);
  QualType elementType = expr->getLHS()->getType()->getPointeeType();

  llvm::Value *divisor = nullptr;

  // For a variable-length array, this is going to be non-constant.
  if (const VariableArrayType *vla
        = CGF.getContext().getAsVariableArrayType(elementType)) {
    auto VlaSize = CGF.getVLASize(vla);
    elementType = VlaSize.Type;
    divisor = VlaSize.NumElts;

    // Scale the number of non-VLA elements by the non-VLA element size.
    CharUnits eltSize = CGF.getContext().getTypeSizeInChars(elementType);
    if (!eltSize.isOne())
      divisor = CGF.Builder.CreateNUWMul(CGF.CGM.getSize(eltSize), divisor);

  // For everything elese, we can just compute it, safe in the
  // assumption that Sema won't let anything through that we can't
  // safely compute the size of.
  } else {
    CharUnits elementSize;
    // Handle GCC extension for pointer arithmetic on void* and
    // function pointer types.
    if (elementType->isVoidType() || elementType->isFunctionType())
      elementSize = CharUnits::One();
    else
      elementSize = CGF.getContext().getTypeSizeInChars(elementType);

    // Don't even emit the divide for element size of 1.
    if (elementSize.isOne())
      return diffInChars;

    divisor = CGF.CGM.getSize(elementSize);
  }

  // Otherwise, do a full sdiv. This uses the "exact" form of sdiv, since
  // pointer difference in C is only defined in the case where both operands
  // are pointing to elements of an array.
  return Builder.CreateExactSDiv(diffInChars, divisor, "sub.ptr.div");
}

Value *ScalarExprEmitter::GetMaximumShiftAmount(Value *LHS, Value *RHS,
                                                bool RHSIsSigned) {
  llvm::IntegerType *Ty;
  if (llvm::VectorType *VT = dyn_cast<llvm::VectorType>(LHS->getType()))
    Ty = cast<llvm::IntegerType>(VT->getElementType());
  else
    Ty = cast<llvm::IntegerType>(LHS->getType());
  // For a given type of LHS the maximum shift amount is width(LHS)-1, however
  // it can occur that width(LHS)-1 > range(RHS). Since there is no check for
  // this in ConstantInt::get, this results in the value getting truncated.
  // Constrain the return value to be max(RHS) in this case.
  llvm::Type *RHSTy = RHS->getType();
  llvm::APInt RHSMax =
      RHSIsSigned ? llvm::APInt::getSignedMaxValue(RHSTy->getScalarSizeInBits())
                  : llvm::APInt::getMaxValue(RHSTy->getScalarSizeInBits());
  if (RHSMax.ult(Ty->getBitWidth()))
    return llvm::ConstantInt::get(RHSTy, RHSMax);
  return llvm::ConstantInt::get(RHSTy, Ty->getBitWidth() - 1);
}

Value *ScalarExprEmitter::ConstrainShiftValue(Value *LHS, Value *RHS,
                                              const Twine &Name) {
  llvm::IntegerType *Ty;
  if (auto *VT = dyn_cast<llvm::VectorType>(LHS->getType()))
    Ty = cast<llvm::IntegerType>(VT->getElementType());
  else
    Ty = cast<llvm::IntegerType>(LHS->getType());

  if (llvm::isPowerOf2_64(Ty->getBitWidth()))
    return Builder.CreateAnd(RHS, GetMaximumShiftAmount(LHS, RHS, false), Name);

  return Builder.CreateURem(
      RHS, llvm::ConstantInt::get(RHS->getType(), Ty->getBitWidth()), Name);
}

Value *ScalarExprEmitter::EmitShl(const BinOpInfo &Ops) {
  // TODO: This misses out on the sanitizer check below.
  if (Ops.isFixedPointOp())
    return EmitFixedPointBinOp(Ops);

  // LLVM requires the LHS and RHS to be the same type: promote or truncate the
  // RHS to the same size as the LHS.
  Value *RHS = Ops.RHS;
  if (Ops.LHS->getType() != RHS->getType())
    RHS = Builder.CreateIntCast(RHS, Ops.LHS->getType(), false, "sh_prom");

  bool SanitizeSignedBase = CGF.SanOpts.has(SanitizerKind::ShiftBase) &&
                            Ops.Ty->hasSignedIntegerRepresentation() &&
                            !CGF.getLangOpts().isSignedOverflowDefined() &&
                            !CGF.getLangOpts().CPlusPlus20;
  bool SanitizeUnsignedBase =
      CGF.SanOpts.has(SanitizerKind::UnsignedShiftBase) &&
      Ops.Ty->hasUnsignedIntegerRepresentation();
  bool SanitizeBase = SanitizeSignedBase || SanitizeUnsignedBase;
  bool SanitizeExponent = CGF.SanOpts.has(SanitizerKind::ShiftExponent);
  // OpenCL 6.3j: shift values are effectively % word size of LHS.
  if (CGF.getLangOpts().OpenCL || CGF.getLangOpts().HLSL)
    RHS = ConstrainShiftValue(Ops.LHS, RHS, "shl.mask");
  else if ((SanitizeBase || SanitizeExponent) &&
           isa<llvm::IntegerType>(Ops.LHS->getType())) {
    CodeGenFunction::SanitizerScope SanScope(&CGF);
    SmallVector<std::pair<Value *, SanitizerMask>, 2> Checks;
    bool RHSIsSigned = Ops.rhsHasSignedIntegerRepresentation();
    llvm::Value *WidthMinusOne =
        GetMaximumShiftAmount(Ops.LHS, Ops.RHS, RHSIsSigned);
    llvm::Value *ValidExponent = Builder.CreateICmpULE(Ops.RHS, WidthMinusOne);

    if (SanitizeExponent) {
      Checks.push_back(
          std::make_pair(ValidExponent, SanitizerKind::ShiftExponent));
    }

    if (SanitizeBase) {
      // Check whether we are shifting any non-zero bits off the top of the
      // integer. We only emit this check if exponent is valid - otherwise
      // instructions below will have undefined behavior themselves.
      llvm::BasicBlock *Orig = Builder.GetInsertBlock();
      llvm::BasicBlock *Cont = CGF.createBasicBlock("cont");
      llvm::BasicBlock *CheckShiftBase = CGF.createBasicBlock("check");
      Builder.CreateCondBr(ValidExponent, CheckShiftBase, Cont);
      llvm::Value *PromotedWidthMinusOne =
          (RHS == Ops.RHS) ? WidthMinusOne
                           : GetMaximumShiftAmount(Ops.LHS, RHS, RHSIsSigned);
      CGF.EmitBlock(CheckShiftBase);
      llvm::Value *BitsShiftedOff = Builder.CreateLShr(
          Ops.LHS, Builder.CreateSub(PromotedWidthMinusOne, RHS, "shl.zeros",
                                     /*NUW*/ true, /*NSW*/ true),
          "shl.check");
      if (SanitizeUnsignedBase || CGF.getLangOpts().CPlusPlus) {
        // In C99, we are not permitted to shift a 1 bit into the sign bit.
        // Under C++11's rules, shifting a 1 bit into the sign bit is
        // OK, but shifting a 1 bit out of it is not. (C89 and C++03 don't
        // define signed left shifts, so we use the C99 and C++11 rules there).
        // Unsigned shifts can always shift into the top bit.
        llvm::Value *One = llvm::ConstantInt::get(BitsShiftedOff->getType(), 1);
        BitsShiftedOff = Builder.CreateLShr(BitsShiftedOff, One);
      }
      llvm::Value *Zero = llvm::ConstantInt::get(BitsShiftedOff->getType(), 0);
      llvm::Value *ValidBase = Builder.CreateICmpEQ(BitsShiftedOff, Zero);
      CGF.EmitBlock(Cont);
      llvm::PHINode *BaseCheck = Builder.CreatePHI(ValidBase->getType(), 2);
      BaseCheck->addIncoming(Builder.getTrue(), Orig);
      BaseCheck->addIncoming(ValidBase, CheckShiftBase);
      Checks.push_back(std::make_pair(
          BaseCheck, SanitizeSignedBase ? SanitizerKind::ShiftBase
                                        : SanitizerKind::UnsignedShiftBase));
    }

    assert(!Checks.empty());
    EmitBinOpCheck(Checks, Ops);
  }

  return Builder.CreateShl(Ops.LHS, RHS, "shl");
}

Value *ScalarExprEmitter::EmitShr(const BinOpInfo &Ops) {
  // TODO: This misses out on the sanitizer check below.
  if (Ops.isFixedPointOp())
    return EmitFixedPointBinOp(Ops);

  // LLVM requires the LHS and RHS to be the same type: promote or truncate the
  // RHS to the same size as the LHS.
  Value *RHS = Ops.RHS;
  if (Ops.LHS->getType() != RHS->getType())
    RHS = Builder.CreateIntCast(RHS, Ops.LHS->getType(), false, "sh_prom");

  // OpenCL 6.3j: shift values are effectively % word size of LHS.
  if (CGF.getLangOpts().OpenCL || CGF.getLangOpts().HLSL)
    RHS = ConstrainShiftValue(Ops.LHS, RHS, "shr.mask");
  else if (CGF.SanOpts.has(SanitizerKind::ShiftExponent) &&
           isa<llvm::IntegerType>(Ops.LHS->getType())) {
    CodeGenFunction::SanitizerScope SanScope(&CGF);
    bool RHSIsSigned = Ops.rhsHasSignedIntegerRepresentation();
    llvm::Value *Valid = Builder.CreateICmpULE(
        Ops.RHS, GetMaximumShiftAmount(Ops.LHS, Ops.RHS, RHSIsSigned));
    EmitBinOpCheck(std::make_pair(Valid, SanitizerKind::ShiftExponent), Ops);
  }

  if (Ops.Ty->hasUnsignedIntegerRepresentation())
    return Builder.CreateLShr(Ops.LHS, RHS, "shr");
  return Builder.CreateAShr(Ops.LHS, RHS, "shr");
}

enum IntrinsicType { VCMPEQ, VCMPGT };
// return corresponding comparison intrinsic for given vector type
static llvm::Intrinsic::ID GetIntrinsic(IntrinsicType IT,
                                        BuiltinType::Kind ElemKind) {
  switch (ElemKind) {
  default: llvm_unreachable("unexpected element type");
  case BuiltinType::Char_U:
  case BuiltinType::UChar:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequb_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtub_p;
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequb_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtsb_p;
  case BuiltinType::UShort:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequh_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtuh_p;
  case BuiltinType::Short:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequh_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtsh_p;
  case BuiltinType::UInt:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequw_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtuw_p;
  case BuiltinType::Int:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequw_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtsw_p;
  case BuiltinType::ULong:
  case BuiltinType::ULongLong:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequd_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtud_p;
  case BuiltinType::Long:
  case BuiltinType::LongLong:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequd_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtsd_p;
  case BuiltinType::Float:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpeqfp_p :
                            llvm::Intrinsic::ppc_altivec_vcmpgtfp_p;
  case BuiltinType::Double:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_vsx_xvcmpeqdp_p :
                            llvm::Intrinsic::ppc_vsx_xvcmpgtdp_p;
  case BuiltinType::UInt128:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequq_p
                          : llvm::Intrinsic::ppc_altivec_vcmpgtuq_p;
  case BuiltinType::Int128:
    return (IT == VCMPEQ) ? llvm::Intrinsic::ppc_altivec_vcmpequq_p
                          : llvm::Intrinsic::ppc_altivec_vcmpgtsq_p;
  }
}

Value *ScalarExprEmitter::EmitCompare(const BinaryOperator *E,
                                      llvm::CmpInst::Predicate UICmpOpc,
                                      llvm::CmpInst::Predicate SICmpOpc,
                                      llvm::CmpInst::Predicate FCmpOpc,
                                      bool IsSignaling) {
  TestAndClearIgnoreResultAssign();
  Value *Result;
  QualType LHSTy = E->getLHS()->getType();
  QualType RHSTy = E->getRHS()->getType();
  if (const MemberPointerType *MPT = LHSTy->getAs<MemberPointerType>()) {
    assert(E->getOpcode() == BO_EQ ||
           E->getOpcode() == BO_NE);
    Value *LHS = CGF.EmitScalarExpr(E->getLHS());
    Value *RHS = CGF.EmitScalarExpr(E->getRHS());
    Result = CGF.CGM.getCXXABI().EmitMemberPointerComparison(
                   CGF, LHS, RHS, MPT, E->getOpcode() == BO_NE);
  } else if (!LHSTy->isAnyComplexType() && !RHSTy->isAnyComplexType()) {
    BinOpInfo BOInfo = EmitBinOps(E);
    Value *LHS = BOInfo.LHS;
    Value *RHS = BOInfo.RHS;

    // If AltiVec, the comparison results in a numeric type, so we use
    // intrinsics comparing vectors and giving 0 or 1 as a result
    if (LHSTy->isVectorType() && !E->getType()->isVectorType()) {
      // constants for mapping CR6 register bits to predicate result
      enum { CR6_EQ=0, CR6_EQ_REV, CR6_LT, CR6_LT_REV } CR6;

      llvm::Intrinsic::ID ID = llvm::Intrinsic::not_intrinsic;

      // in several cases vector arguments order will be reversed
      Value *FirstVecArg = LHS,
            *SecondVecArg = RHS;

      QualType ElTy = LHSTy->castAs<VectorType>()->getElementType();
      BuiltinType::Kind ElementKind = ElTy->castAs<BuiltinType>()->getKind();

      switch(E->getOpcode()) {
      default: llvm_unreachable("is not a comparison operation");
      case BO_EQ:
        CR6 = CR6_LT;
        ID = GetIntrinsic(VCMPEQ, ElementKind);
        break;
      case BO_NE:
        CR6 = CR6_EQ;
        ID = GetIntrinsic(VCMPEQ, ElementKind);
        break;
      case BO_LT:
        CR6 = CR6_LT;
        ID = GetIntrinsic(VCMPGT, ElementKind);
        std::swap(FirstVecArg, SecondVecArg);
        break;
      case BO_GT:
        CR6 = CR6_LT;
        ID = GetIntrinsic(VCMPGT, ElementKind);
        break;
      case BO_LE:
        if (ElementKind == BuiltinType::Float) {
          CR6 = CR6_LT;
          ID = llvm::Intrinsic::ppc_altivec_vcmpgefp_p;
          std::swap(FirstVecArg, SecondVecArg);
        }
        else {
          CR6 = CR6_EQ;
          ID = GetIntrinsic(VCMPGT, ElementKind);
        }
        break;
      case BO_GE:
        if (ElementKind == BuiltinType::Float) {
          CR6 = CR6_LT;
          ID = llvm::Intrinsic::ppc_altivec_vcmpgefp_p;
        }
        else {
          CR6 = CR6_EQ;
          ID = GetIntrinsic(VCMPGT, ElementKind);
          std::swap(FirstVecArg, SecondVecArg);
        }
        break;
      }

      Value *CR6Param = Builder.getInt32(CR6);
      llvm::Function *F = CGF.CGM.getIntrinsic(ID);
      Result = Builder.CreateCall(F, {CR6Param, FirstVecArg, SecondVecArg});

      // The result type of intrinsic may not be same as E->getType().
      // If E->getType() is not BoolTy, EmitScalarConversion will do the
      // conversion work. If E->getType() is BoolTy, EmitScalarConversion will
      // do nothing, if ResultTy is not i1 at the same time, it will cause
      // crash later.
      llvm::IntegerType *ResultTy = cast<llvm::IntegerType>(Result->getType());
      if (ResultTy->getBitWidth() > 1 &&
          E->getType() == CGF.getContext().BoolTy)
        Result = Builder.CreateTrunc(Result, Builder.getInt1Ty());
      return EmitScalarConversion(Result, CGF.getContext().BoolTy, E->getType(),
                                  E->getExprLoc());
    }

    if (BOInfo.isFixedPointOp()) {
      Result = EmitFixedPointBinOp(BOInfo);
    } else if (LHS->getType()->isFPOrFPVectorTy()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, BOInfo.FPFeatures);
      if (!IsSignaling)
        Result = Builder.CreateFCmp(FCmpOpc, LHS, RHS, "cmp");
      else
        Result = Builder.CreateFCmpS(FCmpOpc, LHS, RHS, "cmp");
    } else if (LHSTy->hasSignedIntegerRepresentation()) {
      Result = Builder.CreateICmp(SICmpOpc, LHS, RHS, "cmp");
    } else {
      // Unsigned integers and pointers.

      if (CGF.CGM.getCodeGenOpts().StrictVTablePointers &&
          !isa<llvm::ConstantPointerNull>(LHS) &&
          !isa<llvm::ConstantPointerNull>(RHS)) {

        // Dynamic information is required to be stripped for comparisons,
        // because it could leak the dynamic information.  Based on comparisons
        // of pointers to dynamic objects, the optimizer can replace one pointer
        // with another, which might be incorrect in presence of invariant
        // groups. Comparison with null is safe because null does not carry any
        // dynamic information.
        if (LHSTy.mayBeDynamicClass())
          LHS = Builder.CreateStripInvariantGroup(LHS);
        if (RHSTy.mayBeDynamicClass())
          RHS = Builder.CreateStripInvariantGroup(RHS);
      }

      Result = Builder.CreateICmp(UICmpOpc, LHS, RHS, "cmp");
    }

    // If this is a vector comparison, sign extend the result to the appropriate
    // vector integer type and return it (don't convert to bool).
    if (LHSTy->isVectorType())
      return Builder.CreateSExt(Result, ConvertType(E->getType()), "sext");

  } else {
    // Complex Comparison: can only be an equality comparison.
    CodeGenFunction::ComplexPairTy LHS, RHS;
    QualType CETy;
    if (auto *CTy = LHSTy->getAs<ComplexType>()) {
      LHS = CGF.EmitComplexExpr(E->getLHS());
      CETy = CTy->getElementType();
    } else {
      LHS.first = Visit(E->getLHS());
      LHS.second = llvm::Constant::getNullValue(LHS.first->getType());
      CETy = LHSTy;
    }
    if (auto *CTy = RHSTy->getAs<ComplexType>()) {
      RHS = CGF.EmitComplexExpr(E->getRHS());
      assert(CGF.getContext().hasSameUnqualifiedType(CETy,
                                                     CTy->getElementType()) &&
             "The element types must always match.");
      (void)CTy;
    } else {
      RHS.first = Visit(E->getRHS());
      RHS.second = llvm::Constant::getNullValue(RHS.first->getType());
      assert(CGF.getContext().hasSameUnqualifiedType(CETy, RHSTy) &&
             "The element types must always match.");
    }

    Value *ResultR, *ResultI;
    if (CETy->isRealFloatingType()) {
      // As complex comparisons can only be equality comparisons, they
      // are never signaling comparisons.
      ResultR = Builder.CreateFCmp(FCmpOpc, LHS.first, RHS.first, "cmp.r");
      ResultI = Builder.CreateFCmp(FCmpOpc, LHS.second, RHS.second, "cmp.i");
    } else {
      // Complex comparisons can only be equality comparisons.  As such, signed
      // and unsigned opcodes are the same.
      ResultR = Builder.CreateICmp(UICmpOpc, LHS.first, RHS.first, "cmp.r");
      ResultI = Builder.CreateICmp(UICmpOpc, LHS.second, RHS.second, "cmp.i");
    }

    if (E->getOpcode() == BO_EQ) {
      Result = Builder.CreateAnd(ResultR, ResultI, "and.ri");
    } else {
      assert(E->getOpcode() == BO_NE &&
             "Complex comparison other than == or != ?");
      Result = Builder.CreateOr(ResultR, ResultI, "or.ri");
    }
  }

  return EmitScalarConversion(Result, CGF.getContext().BoolTy, E->getType(),
                              E->getExprLoc());
}

llvm::Value *CodeGenFunction::EmitWithOriginalRHSBitfieldAssignment(
    const BinaryOperator *E, Value **Previous, QualType *SrcType) {
  // In case we have the integer or bitfield sanitizer checks enabled
  // we want to get the expression before scalar conversion.
  if (auto *ICE = dyn_cast<ImplicitCastExpr>(E->getRHS())) {
    CastKind Kind = ICE->getCastKind();
    if (Kind == CK_IntegralCast || Kind == CK_LValueToRValue) {
      *SrcType = ICE->getSubExpr()->getType();
      *Previous = EmitScalarExpr(ICE->getSubExpr());
      // Pass default ScalarConversionOpts to avoid emitting
      // integer sanitizer checks as E refers to bitfield.
      return EmitScalarConversion(*Previous, *SrcType, ICE->getType(),
                                  ICE->getExprLoc());
    }
  }
  return EmitScalarExpr(E->getRHS());
}

Value *ScalarExprEmitter::VisitBinAssign(const BinaryOperator *E) {
  bool Ignore = TestAndClearIgnoreResultAssign();

  Value *RHS;
  LValue LHS;

  switch (E->getLHS()->getType().getObjCLifetime()) {
  case Qualifiers::OCL_Strong:
    std::tie(LHS, RHS) = CGF.EmitARCStoreStrong(E, Ignore);
    break;

  case Qualifiers::OCL_Autoreleasing:
    std::tie(LHS, RHS) = CGF.EmitARCStoreAutoreleasing(E);
    break;

  case Qualifiers::OCL_ExplicitNone:
    std::tie(LHS, RHS) = CGF.EmitARCStoreUnsafeUnretained(E, Ignore);
    break;

  case Qualifiers::OCL_Weak:
    RHS = Visit(E->getRHS());
    LHS = EmitCheckedLValue(E->getLHS(), CodeGenFunction::TCK_Store);
    RHS = CGF.EmitARCStoreWeak(LHS.getAddress(), RHS, Ignore);
    break;

  case Qualifiers::OCL_None:
    // __block variables need to have the rhs evaluated first, plus
    // this should improve codegen just a little.
    Value *Previous = nullptr;
    QualType SrcType = E->getRHS()->getType();
    // Check if LHS is a bitfield, if RHS contains an implicit cast expression
    // we want to extract that value and potentially (if the bitfield sanitizer
    // is enabled) use it to check for an implicit conversion.
    if (E->getLHS()->refersToBitField())
      RHS = CGF.EmitWithOriginalRHSBitfieldAssignment(E, &Previous, &SrcType);
    else
      RHS = Visit(E->getRHS());

    LHS = EmitCheckedLValue(E->getLHS(), CodeGenFunction::TCK_Store);

    // Store the value into the LHS.  Bit-fields are handled specially
    // because the result is altered by the store, i.e., [C99 6.5.16p1]
    // 'An assignment expression has the value of the left operand after
    // the assignment...'.
    if (LHS.isBitField()) {
      CGF.EmitStoreThroughBitfieldLValue(RValue::get(RHS), LHS, &RHS);
      // If the expression contained an implicit conversion, make sure
      // to use the value before the scalar conversion.
      Value *Src = Previous ? Previous : RHS;
      QualType DstType = E->getLHS()->getType();
      CGF.EmitBitfieldConversionCheck(Src, SrcType, RHS, DstType,
                                      LHS.getBitFieldInfo(), E->getExprLoc());
    } else {
      CGF.EmitNullabilityCheck(LHS, RHS, E->getExprLoc());
      CGF.EmitStoreThroughLValue(RValue::get(RHS), LHS);
    }
  }

  // If the result is clearly ignored, return now.
  if (Ignore)
    return nullptr;

  // The result of an assignment in C is the assigned r-value.
  if (!CGF.getLangOpts().CPlusPlus)
    return RHS;

  // If the lvalue is non-volatile, return the computed value of the assignment.
  if (!LHS.isVolatileQualified())
    return RHS;

  // Otherwise, reload the value.
  return EmitLoadOfLValue(LHS, E->getExprLoc());
}

Value *ScalarExprEmitter::VisitBinLAnd(const BinaryOperator *E) {
  // Perform vector logical and on comparisons with zero vectors.
  if (E->getType()->isVectorType()) {
    CGF.incrementProfileCounter(E);

    Value *LHS = Visit(E->getLHS());
    Value *RHS = Visit(E->getRHS());
    Value *Zero = llvm::ConstantAggregateZero::get(LHS->getType());
    if (LHS->getType()->isFPOrFPVectorTy()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(
          CGF, E->getFPFeaturesInEffect(CGF.getLangOpts()));
      LHS = Builder.CreateFCmp(llvm::CmpInst::FCMP_UNE, LHS, Zero, "cmp");
      RHS = Builder.CreateFCmp(llvm::CmpInst::FCMP_UNE, RHS, Zero, "cmp");
    } else {
      LHS = Builder.CreateICmp(llvm::CmpInst::ICMP_NE, LHS, Zero, "cmp");
      RHS = Builder.CreateICmp(llvm::CmpInst::ICMP_NE, RHS, Zero, "cmp");
    }
    Value *And = Builder.CreateAnd(LHS, RHS);
    return Builder.CreateSExt(And, ConvertType(E->getType()), "sext");
  }

  bool InstrumentRegions = CGF.CGM.getCodeGenOpts().hasProfileClangInstr();
  llvm::Type *ResTy = ConvertType(E->getType());

  // If we have 0 && RHS, see if we can elide RHS, if so, just return 0.
  // If we have 1 && X, just emit X without inserting the control flow.
  bool LHSCondVal;
  if (CGF.ConstantFoldsToSimpleInteger(E->getLHS(), LHSCondVal)) {
    if (LHSCondVal) { // If we have 1 && X, just emit X.
      CGF.incrementProfileCounter(E);

      // If the top of the logical operator nest, reset the MCDC temp to 0.
      if (CGF.MCDCLogOpStack.empty())
        CGF.maybeResetMCDCCondBitmap(E);

      CGF.MCDCLogOpStack.push_back(E);

      Value *RHSCond = CGF.EvaluateExprAsBool(E->getRHS());

      // If we're generating for profiling or coverage, generate a branch to a
      // block that increments the RHS counter needed to track branch condition
      // coverage. In this case, use "FBlock" as both the final "TrueBlock" and
      // "FalseBlock" after the increment is done.
      if (InstrumentRegions &&
          CodeGenFunction::isInstrumentedCondition(E->getRHS())) {
        CGF.maybeUpdateMCDCCondBitmap(E->getRHS(), RHSCond);
        llvm::BasicBlock *FBlock = CGF.createBasicBlock("land.end");
        llvm::BasicBlock *RHSBlockCnt = CGF.createBasicBlock("land.rhscnt");
        Builder.CreateCondBr(RHSCond, RHSBlockCnt, FBlock);
        CGF.EmitBlock(RHSBlockCnt);
        CGF.incrementProfileCounter(E->getRHS());
        CGF.EmitBranch(FBlock);
        CGF.EmitBlock(FBlock);
      }

      CGF.MCDCLogOpStack.pop_back();
      // If the top of the logical operator nest, update the MCDC bitmap.
      if (CGF.MCDCLogOpStack.empty())
        CGF.maybeUpdateMCDCTestVectorBitmap(E);

      // ZExt result to int or bool.
      return Builder.CreateZExtOrBitCast(RHSCond, ResTy, "land.ext");
    }

    // 0 && RHS: If it is safe, just elide the RHS, and return 0/false.
    if (!CGF.ContainsLabel(E->getRHS()))
      return llvm::Constant::getNullValue(ResTy);
  }

  // If the top of the logical operator nest, reset the MCDC temp to 0.
  if (CGF.MCDCLogOpStack.empty())
    CGF.maybeResetMCDCCondBitmap(E);

  CGF.MCDCLogOpStack.push_back(E);

  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("land.end");
  llvm::BasicBlock *RHSBlock  = CGF.createBasicBlock("land.rhs");

  CodeGenFunction::ConditionalEvaluation eval(CGF);

  // Branch on the LHS first.  If it is false, go to the failure (cont) block.
  CGF.EmitBranchOnBoolExpr(E->getLHS(), RHSBlock, ContBlock,
                           CGF.getProfileCount(E->getRHS()));

  // Any edges into the ContBlock are now from an (indeterminate number of)
  // edges from this first condition.  All of these values will be false.  Start
  // setting up the PHI node in the Cont Block for this.
  llvm::PHINode *PN = llvm::PHINode::Create(llvm::Type::getInt1Ty(VMContext), 2,
                                            "", ContBlock);
  for (llvm::pred_iterator PI = pred_begin(ContBlock), PE = pred_end(ContBlock);
       PI != PE; ++PI)
    PN->addIncoming(llvm::ConstantInt::getFalse(VMContext), *PI);

  eval.begin(CGF);
  CGF.EmitBlock(RHSBlock);
  CGF.incrementProfileCounter(E);
  Value *RHSCond = CGF.EvaluateExprAsBool(E->getRHS());
  eval.end(CGF);

  // Reaquire the RHS block, as there may be subblocks inserted.
  RHSBlock = Builder.GetInsertBlock();

  // If we're generating for profiling or coverage, generate a branch on the
  // RHS to a block that increments the RHS true counter needed to track branch
  // condition coverage.
  if (InstrumentRegions &&
      CodeGenFunction::isInstrumentedCondition(E->getRHS())) {
    CGF.maybeUpdateMCDCCondBitmap(E->getRHS(), RHSCond);
    llvm::BasicBlock *RHSBlockCnt = CGF.createBasicBlock("land.rhscnt");
    Builder.CreateCondBr(RHSCond, RHSBlockCnt, ContBlock);
    CGF.EmitBlock(RHSBlockCnt);
    CGF.incrementProfileCounter(E->getRHS());
    CGF.EmitBranch(ContBlock);
    PN->addIncoming(RHSCond, RHSBlockCnt);
  }

  // Emit an unconditional branch from this block to ContBlock.
  {
    // There is no need to emit line number for unconditional branch.
    auto NL = ApplyDebugLocation::CreateEmpty(CGF);
    CGF.EmitBlock(ContBlock);
  }
  // Insert an entry into the phi node for the edge with the value of RHSCond.
  PN->addIncoming(RHSCond, RHSBlock);

  CGF.MCDCLogOpStack.pop_back();
  // If the top of the logical operator nest, update the MCDC bitmap.
  if (CGF.MCDCLogOpStack.empty())
    CGF.maybeUpdateMCDCTestVectorBitmap(E);

  // Artificial location to preserve the scope information
  {
    auto NL = ApplyDebugLocation::CreateArtificial(CGF);
    PN->setDebugLoc(Builder.getCurrentDebugLocation());
  }

  // ZExt result to int.
  return Builder.CreateZExtOrBitCast(PN, ResTy, "land.ext");
}

Value *ScalarExprEmitter::VisitBinLOr(const BinaryOperator *E) {
  // Perform vector logical or on comparisons with zero vectors.
  if (E->getType()->isVectorType()) {
    CGF.incrementProfileCounter(E);

    Value *LHS = Visit(E->getLHS());
    Value *RHS = Visit(E->getRHS());
    Value *Zero = llvm::ConstantAggregateZero::get(LHS->getType());
    if (LHS->getType()->isFPOrFPVectorTy()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(
          CGF, E->getFPFeaturesInEffect(CGF.getLangOpts()));
      LHS = Builder.CreateFCmp(llvm::CmpInst::FCMP_UNE, LHS, Zero, "cmp");
      RHS = Builder.CreateFCmp(llvm::CmpInst::FCMP_UNE, RHS, Zero, "cmp");
    } else {
      LHS = Builder.CreateICmp(llvm::CmpInst::ICMP_NE, LHS, Zero, "cmp");
      RHS = Builder.CreateICmp(llvm::CmpInst::ICMP_NE, RHS, Zero, "cmp");
    }
    Value *Or = Builder.CreateOr(LHS, RHS);
    return Builder.CreateSExt(Or, ConvertType(E->getType()), "sext");
  }

  bool InstrumentRegions = CGF.CGM.getCodeGenOpts().hasProfileClangInstr();
  llvm::Type *ResTy = ConvertType(E->getType());

  // If we have 1 || RHS, see if we can elide RHS, if so, just return 1.
  // If we have 0 || X, just emit X without inserting the control flow.
  bool LHSCondVal;
  if (CGF.ConstantFoldsToSimpleInteger(E->getLHS(), LHSCondVal)) {
    if (!LHSCondVal) { // If we have 0 || X, just emit X.
      CGF.incrementProfileCounter(E);

      // If the top of the logical operator nest, reset the MCDC temp to 0.
      if (CGF.MCDCLogOpStack.empty())
        CGF.maybeResetMCDCCondBitmap(E);

      CGF.MCDCLogOpStack.push_back(E);

      Value *RHSCond = CGF.EvaluateExprAsBool(E->getRHS());

      // If we're generating for profiling or coverage, generate a branch to a
      // block that increments the RHS counter need to track branch condition
      // coverage. In this case, use "FBlock" as both the final "TrueBlock" and
      // "FalseBlock" after the increment is done.
      if (InstrumentRegions &&
          CodeGenFunction::isInstrumentedCondition(E->getRHS())) {
        CGF.maybeUpdateMCDCCondBitmap(E->getRHS(), RHSCond);
        llvm::BasicBlock *FBlock = CGF.createBasicBlock("lor.end");
        llvm::BasicBlock *RHSBlockCnt = CGF.createBasicBlock("lor.rhscnt");
        Builder.CreateCondBr(RHSCond, FBlock, RHSBlockCnt);
        CGF.EmitBlock(RHSBlockCnt);
        CGF.incrementProfileCounter(E->getRHS());
        CGF.EmitBranch(FBlock);
        CGF.EmitBlock(FBlock);
      }

      CGF.MCDCLogOpStack.pop_back();
      // If the top of the logical operator nest, update the MCDC bitmap.
      if (CGF.MCDCLogOpStack.empty())
        CGF.maybeUpdateMCDCTestVectorBitmap(E);

      // ZExt result to int or bool.
      return Builder.CreateZExtOrBitCast(RHSCond, ResTy, "lor.ext");
    }

    // 1 || RHS: If it is safe, just elide the RHS, and return 1/true.
    if (!CGF.ContainsLabel(E->getRHS()))
      return llvm::ConstantInt::get(ResTy, 1);
  }

  // If the top of the logical operator nest, reset the MCDC temp to 0.
  if (CGF.MCDCLogOpStack.empty())
    CGF.maybeResetMCDCCondBitmap(E);

  CGF.MCDCLogOpStack.push_back(E);

  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("lor.end");
  llvm::BasicBlock *RHSBlock = CGF.createBasicBlock("lor.rhs");

  CodeGenFunction::ConditionalEvaluation eval(CGF);

  // Branch on the LHS first.  If it is true, go to the success (cont) block.
  CGF.EmitBranchOnBoolExpr(E->getLHS(), ContBlock, RHSBlock,
                           CGF.getCurrentProfileCount() -
                               CGF.getProfileCount(E->getRHS()));

  // Any edges into the ContBlock are now from an (indeterminate number of)
  // edges from this first condition.  All of these values will be true.  Start
  // setting up the PHI node in the Cont Block for this.
  llvm::PHINode *PN = llvm::PHINode::Create(llvm::Type::getInt1Ty(VMContext), 2,
                                            "", ContBlock);
  for (llvm::pred_iterator PI = pred_begin(ContBlock), PE = pred_end(ContBlock);
       PI != PE; ++PI)
    PN->addIncoming(llvm::ConstantInt::getTrue(VMContext), *PI);

  eval.begin(CGF);

  // Emit the RHS condition as a bool value.
  CGF.EmitBlock(RHSBlock);
  CGF.incrementProfileCounter(E);
  Value *RHSCond = CGF.EvaluateExprAsBool(E->getRHS());

  eval.end(CGF);

  // Reaquire the RHS block, as there may be subblocks inserted.
  RHSBlock = Builder.GetInsertBlock();

  // If we're generating for profiling or coverage, generate a branch on the
  // RHS to a block that increments the RHS true counter needed to track branch
  // condition coverage.
  if (InstrumentRegions &&
      CodeGenFunction::isInstrumentedCondition(E->getRHS())) {
    CGF.maybeUpdateMCDCCondBitmap(E->getRHS(), RHSCond);
    llvm::BasicBlock *RHSBlockCnt = CGF.createBasicBlock("lor.rhscnt");
    Builder.CreateCondBr(RHSCond, ContBlock, RHSBlockCnt);
    CGF.EmitBlock(RHSBlockCnt);
    CGF.incrementProfileCounter(E->getRHS());
    CGF.EmitBranch(ContBlock);
    PN->addIncoming(RHSCond, RHSBlockCnt);
  }

  // Emit an unconditional branch from this block to ContBlock.  Insert an entry
  // into the phi node for the edge with the value of RHSCond.
  CGF.EmitBlock(ContBlock);
  PN->addIncoming(RHSCond, RHSBlock);

  CGF.MCDCLogOpStack.pop_back();
  // If the top of the logical operator nest, update the MCDC bitmap.
  if (CGF.MCDCLogOpStack.empty())
    CGF.maybeUpdateMCDCTestVectorBitmap(E);

  // ZExt result to int.
  return Builder.CreateZExtOrBitCast(PN, ResTy, "lor.ext");
}

Value *ScalarExprEmitter::VisitBinComma(const BinaryOperator *E) {
  CGF.EmitIgnoredExpr(E->getLHS());
  CGF.EnsureInsertPoint();
  return Visit(E->getRHS());
}

//===----------------------------------------------------------------------===//
//                             Other Operators
//===----------------------------------------------------------------------===//

/// isCheapEnoughToEvaluateUnconditionally - Return true if the specified
/// expression is cheap enough and side-effect-free enough to evaluate
/// unconditionally instead of conditionally.  This is used to convert control
/// flow into selects in some cases.
static bool isCheapEnoughToEvaluateUnconditionally(const Expr *E,
                                                   CodeGenFunction &CGF) {
  // Anything that is an integer or floating point constant is fine.
  return E->IgnoreParens()->isEvaluatable(CGF.getContext());

  // Even non-volatile automatic variables can't be evaluated unconditionally.
  // Referencing a thread_local may cause non-trivial initialization work to
  // occur. If we're inside a lambda and one of the variables is from the scope
  // outside the lambda, that function may have returned already. Reading its
  // locals is a bad idea. Also, these reads may introduce races there didn't
  // exist in the source-level program.
}


Value *ScalarExprEmitter::
VisitAbstractConditionalOperator(const AbstractConditionalOperator *E) {
  TestAndClearIgnoreResultAssign();

  // Bind the common expression if necessary.
  CodeGenFunction::OpaqueValueMapping binding(CGF, E);

  Expr *condExpr = E->getCond();
  Expr *lhsExpr = E->getTrueExpr();
  Expr *rhsExpr = E->getFalseExpr();

  // If the condition constant folds and can be elided, try to avoid emitting
  // the condition and the dead arm.
  bool CondExprBool;
  if (CGF.ConstantFoldsToSimpleInteger(condExpr, CondExprBool)) {
    Expr *live = lhsExpr, *dead = rhsExpr;
    if (!CondExprBool) std::swap(live, dead);

    // If the dead side doesn't have labels we need, just emit the Live part.
    if (!CGF.ContainsLabel(dead)) {
      if (CondExprBool) {
        if (llvm::EnableSingleByteCoverage) {
          CGF.incrementProfileCounter(lhsExpr);
          CGF.incrementProfileCounter(rhsExpr);
        }
        CGF.incrementProfileCounter(E);
      }
      Value *Result = Visit(live);

      // If the live part is a throw expression, it acts like it has a void
      // type, so evaluating it returns a null Value*.  However, a conditional
      // with non-void type must return a non-null Value*.
      if (!Result && !E->getType()->isVoidType())
        Result = llvm::UndefValue::get(CGF.ConvertType(E->getType()));

      return Result;
    }
  }

  // OpenCL: If the condition is a vector, we can treat this condition like
  // the select function.
  if ((CGF.getLangOpts().OpenCL && condExpr->getType()->isVectorType()) ||
      condExpr->getType()->isExtVectorType()) {
    CGF.incrementProfileCounter(E);

    llvm::Value *CondV = CGF.EmitScalarExpr(condExpr);
    llvm::Value *LHS = Visit(lhsExpr);
    llvm::Value *RHS = Visit(rhsExpr);

    llvm::Type *condType = ConvertType(condExpr->getType());
    auto *vecTy = cast<llvm::FixedVectorType>(condType);

    unsigned numElem = vecTy->getNumElements();
    llvm::Type *elemType = vecTy->getElementType();

    llvm::Value *zeroVec = llvm::Constant::getNullValue(vecTy);
    llvm::Value *TestMSB = Builder.CreateICmpSLT(CondV, zeroVec);
    llvm::Value *tmp = Builder.CreateSExt(
        TestMSB, llvm::FixedVectorType::get(elemType, numElem), "sext");
    llvm::Value *tmp2 = Builder.CreateNot(tmp);

    // Cast float to int to perform ANDs if necessary.
    llvm::Value *RHSTmp = RHS;
    llvm::Value *LHSTmp = LHS;
    bool wasCast = false;
    llvm::VectorType *rhsVTy = cast<llvm::VectorType>(RHS->getType());
    if (rhsVTy->getElementType()->isFloatingPointTy()) {
      RHSTmp = Builder.CreateBitCast(RHS, tmp2->getType());
      LHSTmp = Builder.CreateBitCast(LHS, tmp->getType());
      wasCast = true;
    }

    llvm::Value *tmp3 = Builder.CreateAnd(RHSTmp, tmp2);
    llvm::Value *tmp4 = Builder.CreateAnd(LHSTmp, tmp);
    llvm::Value *tmp5 = Builder.CreateOr(tmp3, tmp4, "cond");
    if (wasCast)
      tmp5 = Builder.CreateBitCast(tmp5, RHS->getType());

    return tmp5;
  }

  if (condExpr->getType()->isVectorType() ||
      condExpr->getType()->isSveVLSBuiltinType()) {
    CGF.incrementProfileCounter(E);

    llvm::Value *CondV = CGF.EmitScalarExpr(condExpr);
    llvm::Value *LHS = Visit(lhsExpr);
    llvm::Value *RHS = Visit(rhsExpr);

    llvm::Type *CondType = ConvertType(condExpr->getType());
    auto *VecTy = cast<llvm::VectorType>(CondType);
    llvm::Value *ZeroVec = llvm::Constant::getNullValue(VecTy);

    CondV = Builder.CreateICmpNE(CondV, ZeroVec, "vector_cond");
    return Builder.CreateSelect(CondV, LHS, RHS, "vector_select");
  }

  // If this is a really simple expression (like x ? 4 : 5), emit this as a
  // select instead of as control flow.  We can only do this if it is cheap and
  // safe to evaluate the LHS and RHS unconditionally.
  if (isCheapEnoughToEvaluateUnconditionally(lhsExpr, CGF) &&
      isCheapEnoughToEvaluateUnconditionally(rhsExpr, CGF)) {
    llvm::Value *CondV = CGF.EvaluateExprAsBool(condExpr);
    llvm::Value *StepV = Builder.CreateZExtOrBitCast(CondV, CGF.Int64Ty);

    if (llvm::EnableSingleByteCoverage) {
      CGF.incrementProfileCounter(lhsExpr);
      CGF.incrementProfileCounter(rhsExpr);
      CGF.incrementProfileCounter(E);
    } else
      CGF.incrementProfileCounter(E, StepV);

    llvm::Value *LHS = Visit(lhsExpr);
    llvm::Value *RHS = Visit(rhsExpr);
    if (!LHS) {
      // If the conditional has void type, make sure we return a null Value*.
      assert(!RHS && "LHS and RHS types must match");
      return nullptr;
    }
    return Builder.CreateSelect(CondV, LHS, RHS, "cond");
  }

  // If the top of the logical operator nest, reset the MCDC temp to 0.
  if (CGF.MCDCLogOpStack.empty())
    CGF.maybeResetMCDCCondBitmap(condExpr);

  llvm::BasicBlock *LHSBlock = CGF.createBasicBlock("cond.true");
  llvm::BasicBlock *RHSBlock = CGF.createBasicBlock("cond.false");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("cond.end");

  CodeGenFunction::ConditionalEvaluation eval(CGF);
  CGF.EmitBranchOnBoolExpr(condExpr, LHSBlock, RHSBlock,
                           CGF.getProfileCount(lhsExpr));

  CGF.EmitBlock(LHSBlock);

  // If the top of the logical operator nest, update the MCDC bitmap for the
  // ConditionalOperator prior to visiting its LHS and RHS blocks, since they
  // may also contain a boolean expression.
  if (CGF.MCDCLogOpStack.empty())
    CGF.maybeUpdateMCDCTestVectorBitmap(condExpr);

  if (llvm::EnableSingleByteCoverage)
    CGF.incrementProfileCounter(lhsExpr);
  else
    CGF.incrementProfileCounter(E);

  eval.begin(CGF);
  Value *LHS = Visit(lhsExpr);
  eval.end(CGF);

  LHSBlock = Builder.GetInsertBlock();
  Builder.CreateBr(ContBlock);

  CGF.EmitBlock(RHSBlock);

  // If the top of the logical operator nest, update the MCDC bitmap for the
  // ConditionalOperator prior to visiting its LHS and RHS blocks, since they
  // may also contain a boolean expression.
  if (CGF.MCDCLogOpStack.empty())
    CGF.maybeUpdateMCDCTestVectorBitmap(condExpr);

  if (llvm::EnableSingleByteCoverage)
    CGF.incrementProfileCounter(rhsExpr);

  eval.begin(CGF);
  Value *RHS = Visit(rhsExpr);
  eval.end(CGF);

  RHSBlock = Builder.GetInsertBlock();
  CGF.EmitBlock(ContBlock);

  // If the LHS or RHS is a throw expression, it will be legitimately null.
  if (!LHS)
    return RHS;
  if (!RHS)
    return LHS;

  // Create a PHI node for the real part.
  llvm::PHINode *PN = Builder.CreatePHI(LHS->getType(), 2, "cond");
  PN->addIncoming(LHS, LHSBlock);
  PN->addIncoming(RHS, RHSBlock);

  // When single byte coverage mode is enabled, add a counter to continuation
  // block.
  if (llvm::EnableSingleByteCoverage)
    CGF.incrementProfileCounter(E);

  return PN;
}

Value *ScalarExprEmitter::VisitChooseExpr(ChooseExpr *E) {
  return Visit(E->getChosenSubExpr());
}

Value *ScalarExprEmitter::VisitVAArgExpr(VAArgExpr *VE) {
  QualType Ty = VE->getType();

  if (Ty->isVariablyModifiedType())
    CGF.EmitVariablyModifiedType(Ty);

  Address ArgValue = Address::invalid();
  RValue ArgPtr = CGF.EmitVAArg(VE, ArgValue);

  return ArgPtr.getScalarVal();
}

Value *ScalarExprEmitter::VisitBlockExpr(const BlockExpr *block) {
  return CGF.EmitBlockLiteral(block);
}

// Convert a vec3 to vec4, or vice versa.
static Value *ConvertVec3AndVec4(CGBuilderTy &Builder, CodeGenFunction &CGF,
                                 Value *Src, unsigned NumElementsDst) {
  static constexpr int Mask[] = {0, 1, 2, -1};
  return Builder.CreateShuffleVector(Src, llvm::ArrayRef(Mask, NumElementsDst));
}

// Create cast instructions for converting LLVM value \p Src to LLVM type \p
// DstTy. \p Src has the same size as \p DstTy. Both are single value types
// but could be scalar or vectors of different lengths, and either can be
// pointer.
// There are 4 cases:
// 1. non-pointer -> non-pointer  : needs 1 bitcast
// 2. pointer -> pointer          : needs 1 bitcast or addrspacecast
// 3. pointer -> non-pointer
//   a) pointer -> intptr_t       : needs 1 ptrtoint
//   b) pointer -> non-intptr_t   : needs 1 ptrtoint then 1 bitcast
// 4. non-pointer -> pointer
//   a) intptr_t -> pointer       : needs 1 inttoptr
//   b) non-intptr_t -> pointer   : needs 1 bitcast then 1 inttoptr
// Note: for cases 3b and 4b two casts are required since LLVM casts do not
// allow casting directly between pointer types and non-integer non-pointer
// types.
static Value *createCastsForTypeOfSameSize(CGBuilderTy &Builder,
                                           const llvm::DataLayout &DL,
                                           Value *Src, llvm::Type *DstTy,
                                           StringRef Name = "") {
  auto SrcTy = Src->getType();

  // Case 1.
  if (!SrcTy->isPointerTy() && !DstTy->isPointerTy())
    return Builder.CreateBitCast(Src, DstTy, Name);

  // Case 2.
  if (SrcTy->isPointerTy() && DstTy->isPointerTy())
    return Builder.CreatePointerBitCastOrAddrSpaceCast(Src, DstTy, Name);

  // Case 3.
  if (SrcTy->isPointerTy() && !DstTy->isPointerTy()) {
    // Case 3b.
    if (!DstTy->isIntegerTy())
      Src = Builder.CreatePtrToInt(Src, DL.getIntPtrType(SrcTy));
    // Cases 3a and 3b.
    return Builder.CreateBitOrPointerCast(Src, DstTy, Name);
  }

  // Case 4b.
  if (!SrcTy->isIntegerTy())
    Src = Builder.CreateBitCast(Src, DL.getIntPtrType(DstTy));
  // Cases 4a and 4b.
  return Builder.CreateIntToPtr(Src, DstTy, Name);
}

Value *ScalarExprEmitter::VisitAsTypeExpr(AsTypeExpr *E) {
  Value *Src  = CGF.EmitScalarExpr(E->getSrcExpr());
  llvm::Type *DstTy = ConvertType(E->getType());

  llvm::Type *SrcTy = Src->getType();
  unsigned NumElementsSrc =
      isa<llvm::VectorType>(SrcTy)
          ? cast<llvm::FixedVectorType>(SrcTy)->getNumElements()
          : 0;
  unsigned NumElementsDst =
      isa<llvm::VectorType>(DstTy)
          ? cast<llvm::FixedVectorType>(DstTy)->getNumElements()
          : 0;

  // Use bit vector expansion for ext_vector_type boolean vectors.
  if (E->getType()->isExtVectorBoolType())
    return CGF.emitBoolVecConversion(Src, NumElementsDst, "astype");

  // Going from vec3 to non-vec3 is a special case and requires a shuffle
  // vector to get a vec4, then a bitcast if the target type is different.
  if (NumElementsSrc == 3 && NumElementsDst != 3) {
    Src = ConvertVec3AndVec4(Builder, CGF, Src, 4);
    Src = createCastsForTypeOfSameSize(Builder, CGF.CGM.getDataLayout(), Src,
                                       DstTy);

    Src->setName("astype");
    return Src;
  }

  // Going from non-vec3 to vec3 is a special case and requires a bitcast
  // to vec4 if the original type is not vec4, then a shuffle vector to
  // get a vec3.
  if (NumElementsSrc != 3 && NumElementsDst == 3) {
    auto *Vec4Ty = llvm::FixedVectorType::get(
        cast<llvm::VectorType>(DstTy)->getElementType(), 4);
    Src = createCastsForTypeOfSameSize(Builder, CGF.CGM.getDataLayout(), Src,
                                       Vec4Ty);

    Src = ConvertVec3AndVec4(Builder, CGF, Src, 3);
    Src->setName("astype");
    return Src;
  }

  return createCastsForTypeOfSameSize(Builder, CGF.CGM.getDataLayout(),
                                      Src, DstTy, "astype");
}

Value *ScalarExprEmitter::VisitAtomicExpr(AtomicExpr *E) {
  return CGF.EmitAtomicExpr(E).getScalarVal();
}

//===----------------------------------------------------------------------===//
//                         Entry Point into this File
//===----------------------------------------------------------------------===//

/// Emit the computation of the specified expression of scalar type, ignoring
/// the result.
Value *CodeGenFunction::EmitScalarExpr(const Expr *E, bool IgnoreResultAssign) {
  assert(E && hasScalarEvaluationKind(E->getType()) &&
         "Invalid scalar expression to emit");

  return ScalarExprEmitter(*this, IgnoreResultAssign)
      .Visit(const_cast<Expr *>(E));
}

/// Emit a conversion from the specified type to the specified destination type,
/// both of which are LLVM scalar types.
Value *CodeGenFunction::EmitScalarConversion(Value *Src, QualType SrcTy,
                                             QualType DstTy,
                                             SourceLocation Loc) {
  assert(hasScalarEvaluationKind(SrcTy) && hasScalarEvaluationKind(DstTy) &&
         "Invalid scalar expression to emit");
  return ScalarExprEmitter(*this).EmitScalarConversion(Src, SrcTy, DstTy, Loc);
}

/// Emit a conversion from the specified complex type to the specified
/// destination type, where the destination type is an LLVM scalar type.
Value *CodeGenFunction::EmitComplexToScalarConversion(ComplexPairTy Src,
                                                      QualType SrcTy,
                                                      QualType DstTy,
                                                      SourceLocation Loc) {
  assert(SrcTy->isAnyComplexType() && hasScalarEvaluationKind(DstTy) &&
         "Invalid complex -> scalar conversion");
  return ScalarExprEmitter(*this)
      .EmitComplexToScalarConversion(Src, SrcTy, DstTy, Loc);
}


Value *
CodeGenFunction::EmitPromotedScalarExpr(const Expr *E,
                                        QualType PromotionType) {
  if (!PromotionType.isNull())
    return ScalarExprEmitter(*this).EmitPromoted(E, PromotionType);
  else
    return ScalarExprEmitter(*this).Visit(const_cast<Expr *>(E));
}


llvm::Value *CodeGenFunction::
EmitScalarPrePostIncDec(const UnaryOperator *E, LValue LV,
                        bool isInc, bool isPre) {
  return ScalarExprEmitter(*this).EmitScalarPrePostIncDec(E, LV, isInc, isPre);
}

LValue CodeGenFunction::EmitObjCIsaExpr(const ObjCIsaExpr *E) {
  // object->isa or (*object).isa
  // Generate code as for: *(Class*)object

  Expr *BaseExpr = E->getBase();
  Address Addr = Address::invalid();
  if (BaseExpr->isPRValue()) {
    llvm::Type *BaseTy =
        ConvertTypeForMem(BaseExpr->getType()->getPointeeType());
    Addr = Address(EmitScalarExpr(BaseExpr), BaseTy, getPointerAlign());
  } else {
    Addr = EmitLValue(BaseExpr).getAddress();
  }

  // Cast the address to Class*.
  Addr = Addr.withElementType(ConvertType(E->getType()));
  return MakeAddrLValue(Addr, E->getType());
}


LValue CodeGenFunction::EmitCompoundAssignmentLValue(
                                            const CompoundAssignOperator *E) {
  ScalarExprEmitter Scalar(*this);
  Value *Result = nullptr;
  switch (E->getOpcode()) {
#define COMPOUND_OP(Op)                                                       \
    case BO_##Op##Assign:                                                     \
      return Scalar.EmitCompoundAssignLValue(E, &ScalarExprEmitter::Emit##Op, \
                                             Result)
  COMPOUND_OP(Mul);
  COMPOUND_OP(Div);
  COMPOUND_OP(Rem);
  COMPOUND_OP(Add);
  COMPOUND_OP(Sub);
  COMPOUND_OP(Shl);
  COMPOUND_OP(Shr);
  COMPOUND_OP(And);
  COMPOUND_OP(Xor);
  COMPOUND_OP(Or);
#undef COMPOUND_OP

  case BO_PtrMemD:
  case BO_PtrMemI:
  case BO_Mul:
  case BO_Div:
  case BO_Rem:
  case BO_Add:
  case BO_Sub:
  case BO_Shl:
  case BO_Shr:
  case BO_LT:
  case BO_GT:
  case BO_LE:
  case BO_GE:
  case BO_EQ:
  case BO_NE:
  case BO_Cmp:
  case BO_And:
  case BO_Xor:
  case BO_Or:
  case BO_LAnd:
  case BO_LOr:
  case BO_Assign:
  case BO_Comma:
    llvm_unreachable("Not valid compound assignment operators");
  }

  llvm_unreachable("Unhandled compound assignment operator");
}

struct GEPOffsetAndOverflow {
  // The total (signed) byte offset for the GEP.
  llvm::Value *TotalOffset;
  // The offset overflow flag - true if the total offset overflows.
  llvm::Value *OffsetOverflows;
};

/// Evaluate given GEPVal, which is either an inbounds GEP, or a constant,
/// and compute the total offset it applies from it's base pointer BasePtr.
/// Returns offset in bytes and a boolean flag whether an overflow happened
/// during evaluation.
static GEPOffsetAndOverflow EmitGEPOffsetInBytes(Value *BasePtr, Value *GEPVal,
                                                 llvm::LLVMContext &VMContext,
                                                 CodeGenModule &CGM,
                                                 CGBuilderTy &Builder) {
  const auto &DL = CGM.getDataLayout();

  // The total (signed) byte offset for the GEP.
  llvm::Value *TotalOffset = nullptr;

  // Was the GEP already reduced to a constant?
  if (isa<llvm::Constant>(GEPVal)) {
    // Compute the offset by casting both pointers to integers and subtracting:
    // GEPVal = BasePtr + ptr(Offset) <--> Offset = int(GEPVal) - int(BasePtr)
    Value *BasePtr_int =
        Builder.CreatePtrToInt(BasePtr, DL.getIntPtrType(BasePtr->getType()));
    Value *GEPVal_int =
        Builder.CreatePtrToInt(GEPVal, DL.getIntPtrType(GEPVal->getType()));
    TotalOffset = Builder.CreateSub(GEPVal_int, BasePtr_int);
    return {TotalOffset, /*OffsetOverflows=*/Builder.getFalse()};
  }

  auto *GEP = cast<llvm::GEPOperator>(GEPVal);
  assert(GEP->getPointerOperand() == BasePtr &&
         "BasePtr must be the base of the GEP.");
  assert(GEP->isInBounds() && "Expected inbounds GEP");

  auto *IntPtrTy = DL.getIntPtrType(GEP->getPointerOperandType());

  // Grab references to the signed add/mul overflow intrinsics for intptr_t.
  auto *Zero = llvm::ConstantInt::getNullValue(IntPtrTy);
  auto *SAddIntrinsic =
      CGM.getIntrinsic(llvm::Intrinsic::sadd_with_overflow, IntPtrTy);
  auto *SMulIntrinsic =
      CGM.getIntrinsic(llvm::Intrinsic::smul_with_overflow, IntPtrTy);

  // The offset overflow flag - true if the total offset overflows.
  llvm::Value *OffsetOverflows = Builder.getFalse();

  /// Return the result of the given binary operation.
  auto eval = [&](BinaryOperator::Opcode Opcode, llvm::Value *LHS,
                  llvm::Value *RHS) -> llvm::Value * {
    assert((Opcode == BO_Add || Opcode == BO_Mul) && "Can't eval binop");

    // If the operands are constants, return a constant result.
    if (auto *LHSCI = dyn_cast<llvm::ConstantInt>(LHS)) {
      if (auto *RHSCI = dyn_cast<llvm::ConstantInt>(RHS)) {
        llvm::APInt N;
        bool HasOverflow = mayHaveIntegerOverflow(LHSCI, RHSCI, Opcode,
                                                  /*Signed=*/true, N);
        if (HasOverflow)
          OffsetOverflows = Builder.getTrue();
        return llvm::ConstantInt::get(VMContext, N);
      }
    }

    // Otherwise, compute the result with checked arithmetic.
    auto *ResultAndOverflow = Builder.CreateCall(
        (Opcode == BO_Add) ? SAddIntrinsic : SMulIntrinsic, {LHS, RHS});
    OffsetOverflows = Builder.CreateOr(
        Builder.CreateExtractValue(ResultAndOverflow, 1), OffsetOverflows);
    return Builder.CreateExtractValue(ResultAndOverflow, 0);
  };

  // Determine the total byte offset by looking at each GEP operand.
  for (auto GTI = llvm::gep_type_begin(GEP), GTE = llvm::gep_type_end(GEP);
       GTI != GTE; ++GTI) {
    llvm::Value *LocalOffset;
    auto *Index = GTI.getOperand();
    // Compute the local offset contributed by this indexing step:
    if (auto *STy = GTI.getStructTypeOrNull()) {
      // For struct indexing, the local offset is the byte position of the
      // specified field.
      unsigned FieldNo = cast<llvm::ConstantInt>(Index)->getZExtValue();
      LocalOffset = llvm::ConstantInt::get(
          IntPtrTy, DL.getStructLayout(STy)->getElementOffset(FieldNo));
    } else {
      // Otherwise this is array-like indexing. The local offset is the index
      // multiplied by the element size.
      auto *ElementSize =
          llvm::ConstantInt::get(IntPtrTy, GTI.getSequentialElementStride(DL));
      auto *IndexS = Builder.CreateIntCast(Index, IntPtrTy, /*isSigned=*/true);
      LocalOffset = eval(BO_Mul, ElementSize, IndexS);
    }

    // If this is the first offset, set it as the total offset. Otherwise, add
    // the local offset into the running total.
    if (!TotalOffset || TotalOffset == Zero)
      TotalOffset = LocalOffset;
    else
      TotalOffset = eval(BO_Add, TotalOffset, LocalOffset);
  }

  return {TotalOffset, OffsetOverflows};
}

Value *
CodeGenFunction::EmitCheckedInBoundsGEP(llvm::Type *ElemTy, Value *Ptr,
                                        ArrayRef<Value *> IdxList,
                                        bool SignedIndices, bool IsSubtraction,
                                        SourceLocation Loc, const Twine &Name) {
  llvm::Type *PtrTy = Ptr->getType();
  Value *GEPVal = Builder.CreateInBoundsGEP(ElemTy, Ptr, IdxList, Name);

  // If the pointer overflow sanitizer isn't enabled, do nothing.
  if (!SanOpts.has(SanitizerKind::PointerOverflow))
    return GEPVal;

  // Perform nullptr-and-offset check unless the nullptr is defined.
  bool PerformNullCheck = !NullPointerIsDefined(
      Builder.GetInsertBlock()->getParent(), PtrTy->getPointerAddressSpace());
  // Check for overflows unless the GEP got constant-folded,
  // and only in the default address space
  bool PerformOverflowCheck =
      !isa<llvm::Constant>(GEPVal) && PtrTy->getPointerAddressSpace() == 0;

  if (!(PerformNullCheck || PerformOverflowCheck))
    return GEPVal;

  const auto &DL = CGM.getDataLayout();

  SanitizerScope SanScope(this);
  llvm::Type *IntPtrTy = DL.getIntPtrType(PtrTy);

  GEPOffsetAndOverflow EvaluatedGEP =
      EmitGEPOffsetInBytes(Ptr, GEPVal, getLLVMContext(), CGM, Builder);

  assert((!isa<llvm::Constant>(EvaluatedGEP.TotalOffset) ||
          EvaluatedGEP.OffsetOverflows == Builder.getFalse()) &&
         "If the offset got constant-folded, we don't expect that there was an "
         "overflow.");

  auto *Zero = llvm::ConstantInt::getNullValue(IntPtrTy);

  // Common case: if the total offset is zero, and we are using C++ semantics,
  // where nullptr+0 is defined, don't emit a check.
  if (EvaluatedGEP.TotalOffset == Zero && CGM.getLangOpts().CPlusPlus)
    return GEPVal;

  // Now that we've computed the total offset, add it to the base pointer (with
  // wrapping semantics).
  auto *IntPtr = Builder.CreatePtrToInt(Ptr, IntPtrTy);
  auto *ComputedGEP = Builder.CreateAdd(IntPtr, EvaluatedGEP.TotalOffset);

  llvm::SmallVector<std::pair<llvm::Value *, SanitizerMask>, 2> Checks;

  if (PerformNullCheck) {
    // In C++, if the base pointer evaluates to a null pointer value,
    // the only valid  pointer this inbounds GEP can produce is also
    // a null pointer, so the offset must also evaluate to zero.
    // Likewise, if we have non-zero base pointer, we can not get null pointer
    // as a result, so the offset can not be -intptr_t(BasePtr).
    // In other words, both pointers are either null, or both are non-null,
    // or the behaviour is undefined.
    //
    // C, however, is more strict in this regard, and gives more
    // optimization opportunities: in C, additionally, nullptr+0 is undefined.
    // So both the input to the 'gep inbounds' AND the output must not be null.
    auto *BaseIsNotNullptr = Builder.CreateIsNotNull(Ptr);
    auto *ResultIsNotNullptr = Builder.CreateIsNotNull(ComputedGEP);
    auto *Valid =
        CGM.getLangOpts().CPlusPlus
            ? Builder.CreateICmpEQ(BaseIsNotNullptr, ResultIsNotNullptr)
            : Builder.CreateAnd(BaseIsNotNullptr, ResultIsNotNullptr);
    Checks.emplace_back(Valid, SanitizerKind::PointerOverflow);
  }

  if (PerformOverflowCheck) {
    // The GEP is valid if:
    // 1) The total offset doesn't overflow, and
    // 2) The sign of the difference between the computed address and the base
    // pointer matches the sign of the total offset.
    llvm::Value *ValidGEP;
    auto *NoOffsetOverflow = Builder.CreateNot(EvaluatedGEP.OffsetOverflows);
    if (SignedIndices) {
      // GEP is computed as `unsigned base + signed offset`, therefore:
      // * If offset was positive, then the computed pointer can not be
      //   [unsigned] less than the base pointer, unless it overflowed.
      // * If offset was negative, then the computed pointer can not be
      //   [unsigned] greater than the bas pointere, unless it overflowed.
      auto *PosOrZeroValid = Builder.CreateICmpUGE(ComputedGEP, IntPtr);
      auto *PosOrZeroOffset =
          Builder.CreateICmpSGE(EvaluatedGEP.TotalOffset, Zero);
      llvm::Value *NegValid = Builder.CreateICmpULT(ComputedGEP, IntPtr);
      ValidGEP =
          Builder.CreateSelect(PosOrZeroOffset, PosOrZeroValid, NegValid);
    } else if (!IsSubtraction) {
      // GEP is computed as `unsigned base + unsigned offset`,  therefore the
      // computed pointer can not be [unsigned] less than base pointer,
      // unless there was an overflow.
      // Equivalent to `@llvm.uadd.with.overflow(%base, %offset)`.
      ValidGEP = Builder.CreateICmpUGE(ComputedGEP, IntPtr);
    } else {
      // GEP is computed as `unsigned base - unsigned offset`, therefore the
      // computed pointer can not be [unsigned] greater than base pointer,
      // unless there was an overflow.
      // Equivalent to `@llvm.usub.with.overflow(%base, sub(0, %offset))`.
      ValidGEP = Builder.CreateICmpULE(ComputedGEP, IntPtr);
    }
    ValidGEP = Builder.CreateAnd(ValidGEP, NoOffsetOverflow);
    Checks.emplace_back(ValidGEP, SanitizerKind::PointerOverflow);
  }

  assert(!Checks.empty() && "Should have produced some checks.");

  llvm::Constant *StaticArgs[] = {EmitCheckSourceLocation(Loc)};
  // Pass the computed GEP to the runtime to avoid emitting poisoned arguments.
  llvm::Value *DynamicArgs[] = {IntPtr, ComputedGEP};
  EmitCheck(Checks, SanitizerHandler::PointerOverflow, StaticArgs, DynamicArgs);

  return GEPVal;
}

Address CodeGenFunction::EmitCheckedInBoundsGEP(
    Address Addr, ArrayRef<Value *> IdxList, llvm::Type *elementType,
    bool SignedIndices, bool IsSubtraction, SourceLocation Loc, CharUnits Align,
    const Twine &Name) {
  if (!SanOpts.has(SanitizerKind::PointerOverflow))
    return Builder.CreateInBoundsGEP(Addr, IdxList, elementType, Align, Name);

  return RawAddress(
      EmitCheckedInBoundsGEP(Addr.getElementType(), Addr.emitRawPointer(*this),
                             IdxList, SignedIndices, IsSubtraction, Loc, Name),
      elementType, Align);
}
