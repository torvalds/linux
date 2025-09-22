//===--- SemaOverload.cpp - C++ Overloading -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Sema routines for C++ overloading.
//
//===----------------------------------------------------------------------===//

#include "CheckExprLifetime.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateDeduction.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <optional>

using namespace clang;
using namespace sema;

using AllowedExplicit = Sema::AllowedExplicit;

static bool functionHasPassObjectSizeParams(const FunctionDecl *FD) {
  return llvm::any_of(FD->parameters(), [](const ParmVarDecl *P) {
    return P->hasAttr<PassObjectSizeAttr>();
  });
}

/// A convenience routine for creating a decayed reference to a function.
static ExprResult CreateFunctionRefExpr(
    Sema &S, FunctionDecl *Fn, NamedDecl *FoundDecl, const Expr *Base,
    bool HadMultipleCandidates, SourceLocation Loc = SourceLocation(),
    const DeclarationNameLoc &LocInfo = DeclarationNameLoc()) {
  if (S.DiagnoseUseOfDecl(FoundDecl, Loc))
    return ExprError();
  // If FoundDecl is different from Fn (such as if one is a template
  // and the other a specialization), make sure DiagnoseUseOfDecl is
  // called on both.
  // FIXME: This would be more comprehensively addressed by modifying
  // DiagnoseUseOfDecl to accept both the FoundDecl and the decl
  // being used.
  if (FoundDecl != Fn && S.DiagnoseUseOfDecl(Fn, Loc))
    return ExprError();
  DeclRefExpr *DRE = new (S.Context)
      DeclRefExpr(S.Context, Fn, false, Fn->getType(), VK_LValue, Loc, LocInfo);
  if (HadMultipleCandidates)
    DRE->setHadMultipleCandidates(true);

  S.MarkDeclRefReferenced(DRE, Base);
  if (auto *FPT = DRE->getType()->getAs<FunctionProtoType>()) {
    if (isUnresolvedExceptionSpec(FPT->getExceptionSpecType())) {
      S.ResolveExceptionSpec(Loc, FPT);
      DRE->setType(Fn->getType());
    }
  }
  return S.ImpCastExprToType(DRE, S.Context.getPointerType(DRE->getType()),
                             CK_FunctionToPointerDecay);
}

static bool IsStandardConversion(Sema &S, Expr* From, QualType ToType,
                                 bool InOverloadResolution,
                                 StandardConversionSequence &SCS,
                                 bool CStyle,
                                 bool AllowObjCWritebackConversion);

static bool IsTransparentUnionStandardConversion(Sema &S, Expr* From,
                                                 QualType &ToType,
                                                 bool InOverloadResolution,
                                                 StandardConversionSequence &SCS,
                                                 bool CStyle);
static OverloadingResult
IsUserDefinedConversion(Sema &S, Expr *From, QualType ToType,
                        UserDefinedConversionSequence& User,
                        OverloadCandidateSet& Conversions,
                        AllowedExplicit AllowExplicit,
                        bool AllowObjCConversionOnExplicit);

static ImplicitConversionSequence::CompareKind
CompareStandardConversionSequences(Sema &S, SourceLocation Loc,
                                   const StandardConversionSequence& SCS1,
                                   const StandardConversionSequence& SCS2);

static ImplicitConversionSequence::CompareKind
CompareQualificationConversions(Sema &S,
                                const StandardConversionSequence& SCS1,
                                const StandardConversionSequence& SCS2);

static ImplicitConversionSequence::CompareKind
CompareDerivedToBaseConversions(Sema &S, SourceLocation Loc,
                                const StandardConversionSequence& SCS1,
                                const StandardConversionSequence& SCS2);

/// GetConversionRank - Retrieve the implicit conversion rank
/// corresponding to the given implicit conversion kind.
ImplicitConversionRank clang::GetConversionRank(ImplicitConversionKind Kind) {
  static const ImplicitConversionRank Rank[] = {
      ICR_Exact_Match,
      ICR_Exact_Match,
      ICR_Exact_Match,
      ICR_Exact_Match,
      ICR_Exact_Match,
      ICR_Exact_Match,
      ICR_Promotion,
      ICR_Promotion,
      ICR_Promotion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_OCL_Scalar_Widening,
      ICR_Complex_Real_Conversion,
      ICR_Conversion,
      ICR_Conversion,
      ICR_Writeback_Conversion,
      ICR_Exact_Match, // NOTE(gbiv): This may not be completely right --
                       // it was omitted by the patch that added
                       // ICK_Zero_Event_Conversion
      ICR_Exact_Match, // NOTE(ctopper): This may not be completely right --
                       // it was omitted by the patch that added
                       // ICK_Zero_Queue_Conversion
      ICR_C_Conversion,
      ICR_C_Conversion_Extension,
      ICR_Conversion,
      ICR_HLSL_Dimension_Reduction,
      ICR_Conversion,
      ICR_HLSL_Scalar_Widening,
  };
  static_assert(std::size(Rank) == (int)ICK_Num_Conversion_Kinds);
  return Rank[(int)Kind];
}

ImplicitConversionRank
clang::GetDimensionConversionRank(ImplicitConversionRank Base,
                                  ImplicitConversionKind Dimension) {
  ImplicitConversionRank Rank = GetConversionRank(Dimension);
  if (Rank == ICR_HLSL_Scalar_Widening) {
    if (Base == ICR_Promotion)
      return ICR_HLSL_Scalar_Widening_Promotion;
    if (Base == ICR_Conversion)
      return ICR_HLSL_Scalar_Widening_Conversion;
  }
  if (Rank == ICR_HLSL_Dimension_Reduction) {
    if (Base == ICR_Promotion)
      return ICR_HLSL_Dimension_Reduction_Promotion;
    if (Base == ICR_Conversion)
      return ICR_HLSL_Dimension_Reduction_Conversion;
  }
  return Rank;
}

/// GetImplicitConversionName - Return the name of this kind of
/// implicit conversion.
static const char *GetImplicitConversionName(ImplicitConversionKind Kind) {
  static const char *const Name[] = {
      "No conversion",
      "Lvalue-to-rvalue",
      "Array-to-pointer",
      "Function-to-pointer",
      "Function pointer conversion",
      "Qualification",
      "Integral promotion",
      "Floating point promotion",
      "Complex promotion",
      "Integral conversion",
      "Floating conversion",
      "Complex conversion",
      "Floating-integral conversion",
      "Pointer conversion",
      "Pointer-to-member conversion",
      "Boolean conversion",
      "Compatible-types conversion",
      "Derived-to-base conversion",
      "Vector conversion",
      "SVE Vector conversion",
      "RVV Vector conversion",
      "Vector splat",
      "Complex-real conversion",
      "Block Pointer conversion",
      "Transparent Union Conversion",
      "Writeback conversion",
      "OpenCL Zero Event Conversion",
      "OpenCL Zero Queue Conversion",
      "C specific type conversion",
      "Incompatible pointer conversion",
      "Fixed point conversion",
      "HLSL vector truncation",
      "Non-decaying array conversion",
      "HLSL vector splat",
  };
  static_assert(std::size(Name) == (int)ICK_Num_Conversion_Kinds);
  return Name[Kind];
}

/// StandardConversionSequence - Set the standard conversion
/// sequence to the identity conversion.
void StandardConversionSequence::setAsIdentityConversion() {
  First = ICK_Identity;
  Second = ICK_Identity;
  Dimension = ICK_Identity;
  Third = ICK_Identity;
  DeprecatedStringLiteralToCharPtr = false;
  QualificationIncludesObjCLifetime = false;
  ReferenceBinding = false;
  DirectBinding = false;
  IsLvalueReference = true;
  BindsToFunctionLvalue = false;
  BindsToRvalue = false;
  BindsImplicitObjectArgumentWithoutRefQualifier = false;
  ObjCLifetimeConversionBinding = false;
  CopyConstructor = nullptr;
}

/// getRank - Retrieve the rank of this standard conversion sequence
/// (C++ 13.3.3.1.1p3). The rank is the largest rank of each of the
/// implicit conversions.
ImplicitConversionRank StandardConversionSequence::getRank() const {
  ImplicitConversionRank Rank = ICR_Exact_Match;
  if (GetConversionRank(First) > Rank)
    Rank = GetConversionRank(First);
  if (GetConversionRank(Second) > Rank)
    Rank = GetConversionRank(Second);
  if (GetDimensionConversionRank(Rank, Dimension) > Rank)
    Rank = GetDimensionConversionRank(Rank, Dimension);
  if (GetConversionRank(Third) > Rank)
    Rank = GetConversionRank(Third);
  return Rank;
}

/// isPointerConversionToBool - Determines whether this conversion is
/// a conversion of a pointer or pointer-to-member to bool. This is
/// used as part of the ranking of standard conversion sequences
/// (C++ 13.3.3.2p4).
bool StandardConversionSequence::isPointerConversionToBool() const {
  // Note that FromType has not necessarily been transformed by the
  // array-to-pointer or function-to-pointer implicit conversions, so
  // check for their presence as well as checking whether FromType is
  // a pointer.
  if (getToType(1)->isBooleanType() &&
      (getFromType()->isPointerType() ||
       getFromType()->isMemberPointerType() ||
       getFromType()->isObjCObjectPointerType() ||
       getFromType()->isBlockPointerType() ||
       First == ICK_Array_To_Pointer || First == ICK_Function_To_Pointer))
    return true;

  return false;
}

/// isPointerConversionToVoidPointer - Determines whether this
/// conversion is a conversion of a pointer to a void pointer. This is
/// used as part of the ranking of standard conversion sequences (C++
/// 13.3.3.2p4).
bool
StandardConversionSequence::
isPointerConversionToVoidPointer(ASTContext& Context) const {
  QualType FromType = getFromType();
  QualType ToType = getToType(1);

  // Note that FromType has not necessarily been transformed by the
  // array-to-pointer implicit conversion, so check for its presence
  // and redo the conversion to get a pointer.
  if (First == ICK_Array_To_Pointer)
    FromType = Context.getArrayDecayedType(FromType);

  if (Second == ICK_Pointer_Conversion && FromType->isAnyPointerType())
    if (const PointerType* ToPtrType = ToType->getAs<PointerType>())
      return ToPtrType->getPointeeType()->isVoidType();

  return false;
}

/// Skip any implicit casts which could be either part of a narrowing conversion
/// or after one in an implicit conversion.
static const Expr *IgnoreNarrowingConversion(ASTContext &Ctx,
                                             const Expr *Converted) {
  // We can have cleanups wrapping the converted expression; these need to be
  // preserved so that destructors run if necessary.
  if (auto *EWC = dyn_cast<ExprWithCleanups>(Converted)) {
    Expr *Inner =
        const_cast<Expr *>(IgnoreNarrowingConversion(Ctx, EWC->getSubExpr()));
    return ExprWithCleanups::Create(Ctx, Inner, EWC->cleanupsHaveSideEffects(),
                                    EWC->getObjects());
  }

  while (auto *ICE = dyn_cast<ImplicitCastExpr>(Converted)) {
    switch (ICE->getCastKind()) {
    case CK_NoOp:
    case CK_IntegralCast:
    case CK_IntegralToBoolean:
    case CK_IntegralToFloating:
    case CK_BooleanToSignedIntegral:
    case CK_FloatingToIntegral:
    case CK_FloatingToBoolean:
    case CK_FloatingCast:
      Converted = ICE->getSubExpr();
      continue;

    default:
      return Converted;
    }
  }

  return Converted;
}

/// Check if this standard conversion sequence represents a narrowing
/// conversion, according to C++11 [dcl.init.list]p7.
///
/// \param Ctx  The AST context.
/// \param Converted  The result of applying this standard conversion sequence.
/// \param ConstantValue  If this is an NK_Constant_Narrowing conversion, the
///        value of the expression prior to the narrowing conversion.
/// \param ConstantType  If this is an NK_Constant_Narrowing conversion, the
///        type of the expression prior to the narrowing conversion.
/// \param IgnoreFloatToIntegralConversion If true type-narrowing conversions
///        from floating point types to integral types should be ignored.
NarrowingKind StandardConversionSequence::getNarrowingKind(
    ASTContext &Ctx, const Expr *Converted, APValue &ConstantValue,
    QualType &ConstantType, bool IgnoreFloatToIntegralConversion) const {
  assert((Ctx.getLangOpts().CPlusPlus || Ctx.getLangOpts().C23) &&
         "narrowing check outside C++");

  // C++11 [dcl.init.list]p7:
  //   A narrowing conversion is an implicit conversion ...
  QualType FromType = getToType(0);
  QualType ToType = getToType(1);

  // A conversion to an enumeration type is narrowing if the conversion to
  // the underlying type is narrowing. This only arises for expressions of
  // the form 'Enum{init}'.
  if (auto *ET = ToType->getAs<EnumType>())
    ToType = ET->getDecl()->getIntegerType();

  switch (Second) {
  // 'bool' is an integral type; dispatch to the right place to handle it.
  case ICK_Boolean_Conversion:
    if (FromType->isRealFloatingType())
      goto FloatingIntegralConversion;
    if (FromType->isIntegralOrUnscopedEnumerationType())
      goto IntegralConversion;
    // -- from a pointer type or pointer-to-member type to bool, or
    return NK_Type_Narrowing;

  // -- from a floating-point type to an integer type, or
  //
  // -- from an integer type or unscoped enumeration type to a floating-point
  //    type, except where the source is a constant expression and the actual
  //    value after conversion will fit into the target type and will produce
  //    the original value when converted back to the original type, or
  case ICK_Floating_Integral:
  FloatingIntegralConversion:
    if (FromType->isRealFloatingType() && ToType->isIntegralType(Ctx)) {
      return NK_Type_Narrowing;
    } else if (FromType->isIntegralOrUnscopedEnumerationType() &&
               ToType->isRealFloatingType()) {
      if (IgnoreFloatToIntegralConversion)
        return NK_Not_Narrowing;
      const Expr *Initializer = IgnoreNarrowingConversion(Ctx, Converted);
      assert(Initializer && "Unknown conversion expression");

      // If it's value-dependent, we can't tell whether it's narrowing.
      if (Initializer->isValueDependent())
        return NK_Dependent_Narrowing;

      if (std::optional<llvm::APSInt> IntConstantValue =
              Initializer->getIntegerConstantExpr(Ctx)) {
        // Convert the integer to the floating type.
        llvm::APFloat Result(Ctx.getFloatTypeSemantics(ToType));
        Result.convertFromAPInt(*IntConstantValue, IntConstantValue->isSigned(),
                                llvm::APFloat::rmNearestTiesToEven);
        // And back.
        llvm::APSInt ConvertedValue = *IntConstantValue;
        bool ignored;
        Result.convertToInteger(ConvertedValue,
                                llvm::APFloat::rmTowardZero, &ignored);
        // If the resulting value is different, this was a narrowing conversion.
        if (*IntConstantValue != ConvertedValue) {
          ConstantValue = APValue(*IntConstantValue);
          ConstantType = Initializer->getType();
          return NK_Constant_Narrowing;
        }
      } else {
        // Variables are always narrowings.
        return NK_Variable_Narrowing;
      }
    }
    return NK_Not_Narrowing;

  // -- from long double to double or float, or from double to float, except
  //    where the source is a constant expression and the actual value after
  //    conversion is within the range of values that can be represented (even
  //    if it cannot be represented exactly), or
  case ICK_Floating_Conversion:
    if (FromType->isRealFloatingType() && ToType->isRealFloatingType() &&
        Ctx.getFloatingTypeOrder(FromType, ToType) == 1) {
      // FromType is larger than ToType.
      const Expr *Initializer = IgnoreNarrowingConversion(Ctx, Converted);

      // If it's value-dependent, we can't tell whether it's narrowing.
      if (Initializer->isValueDependent())
        return NK_Dependent_Narrowing;

      Expr::EvalResult R;
      if ((Ctx.getLangOpts().C23 && Initializer->EvaluateAsRValue(R, Ctx)) ||
          Initializer->isCXX11ConstantExpr(Ctx, &ConstantValue)) {
        // Constant!
        if (Ctx.getLangOpts().C23)
          ConstantValue = R.Val;
        assert(ConstantValue.isFloat());
        llvm::APFloat FloatVal = ConstantValue.getFloat();
        // Convert the source value into the target type.
        bool ignored;
        llvm::APFloat Converted = FloatVal;
        llvm::APFloat::opStatus ConvertStatus =
            Converted.convert(Ctx.getFloatTypeSemantics(ToType),
                              llvm::APFloat::rmNearestTiesToEven, &ignored);
        Converted.convert(Ctx.getFloatTypeSemantics(FromType),
                          llvm::APFloat::rmNearestTiesToEven, &ignored);
        if (Ctx.getLangOpts().C23) {
          if (FloatVal.isNaN() && Converted.isNaN() &&
              !FloatVal.isSignaling() && !Converted.isSignaling()) {
            // Quiet NaNs are considered the same value, regardless of
            // payloads.
            return NK_Not_Narrowing;
          }
          // For normal values, check exact equality.
          if (!Converted.bitwiseIsEqual(FloatVal)) {
            ConstantType = Initializer->getType();
            return NK_Constant_Narrowing;
          }
        } else {
          // If there was no overflow, the source value is within the range of
          // values that can be represented.
          if (ConvertStatus & llvm::APFloat::opOverflow) {
            ConstantType = Initializer->getType();
            return NK_Constant_Narrowing;
          }
        }
      } else {
        return NK_Variable_Narrowing;
      }
    }
    return NK_Not_Narrowing;

  // -- from an integer type or unscoped enumeration type to an integer type
  //    that cannot represent all the values of the original type, except where
  //    the source is a constant expression and the actual value after
  //    conversion will fit into the target type and will produce the original
  //    value when converted back to the original type.
  case ICK_Integral_Conversion:
  IntegralConversion: {
    assert(FromType->isIntegralOrUnscopedEnumerationType());
    assert(ToType->isIntegralOrUnscopedEnumerationType());
    const bool FromSigned = FromType->isSignedIntegerOrEnumerationType();
    const unsigned FromWidth = Ctx.getIntWidth(FromType);
    const bool ToSigned = ToType->isSignedIntegerOrEnumerationType();
    const unsigned ToWidth = Ctx.getIntWidth(ToType);

    if (FromWidth > ToWidth ||
        (FromWidth == ToWidth && FromSigned != ToSigned) ||
        (FromSigned && !ToSigned)) {
      // Not all values of FromType can be represented in ToType.
      const Expr *Initializer = IgnoreNarrowingConversion(Ctx, Converted);

      // If it's value-dependent, we can't tell whether it's narrowing.
      if (Initializer->isValueDependent())
        return NK_Dependent_Narrowing;

      std::optional<llvm::APSInt> OptInitializerValue;
      if (!(OptInitializerValue = Initializer->getIntegerConstantExpr(Ctx))) {
        // Such conversions on variables are always narrowing.
        return NK_Variable_Narrowing;
      }
      llvm::APSInt &InitializerValue = *OptInitializerValue;
      bool Narrowing = false;
      if (FromWidth < ToWidth) {
        // Negative -> unsigned is narrowing. Otherwise, more bits is never
        // narrowing.
        if (InitializerValue.isSigned() && InitializerValue.isNegative())
          Narrowing = true;
      } else {
        // Add a bit to the InitializerValue so we don't have to worry about
        // signed vs. unsigned comparisons.
        InitializerValue = InitializerValue.extend(
          InitializerValue.getBitWidth() + 1);
        // Convert the initializer to and from the target width and signed-ness.
        llvm::APSInt ConvertedValue = InitializerValue;
        ConvertedValue = ConvertedValue.trunc(ToWidth);
        ConvertedValue.setIsSigned(ToSigned);
        ConvertedValue = ConvertedValue.extend(InitializerValue.getBitWidth());
        ConvertedValue.setIsSigned(InitializerValue.isSigned());
        // If the result is different, this was a narrowing conversion.
        if (ConvertedValue != InitializerValue)
          Narrowing = true;
      }
      if (Narrowing) {
        ConstantType = Initializer->getType();
        ConstantValue = APValue(InitializerValue);
        return NK_Constant_Narrowing;
      }
    }
    return NK_Not_Narrowing;
  }
  case ICK_Complex_Real:
    if (FromType->isComplexType() && !ToType->isComplexType())
      return NK_Type_Narrowing;
    return NK_Not_Narrowing;

  case ICK_Floating_Promotion:
    if (Ctx.getLangOpts().C23) {
      const Expr *Initializer = IgnoreNarrowingConversion(Ctx, Converted);
      Expr::EvalResult R;
      if (Initializer->EvaluateAsRValue(R, Ctx)) {
        ConstantValue = R.Val;
        assert(ConstantValue.isFloat());
        llvm::APFloat FloatVal = ConstantValue.getFloat();
        // C23 6.7.3p6 If the initializer has real type and a signaling NaN
        // value, the unqualified versions of the type of the initializer and
        // the corresponding real type of the object declared shall be
        // compatible.
        if (FloatVal.isNaN() && FloatVal.isSignaling()) {
          ConstantType = Initializer->getType();
          return NK_Constant_Narrowing;
        }
      }
    }
    return NK_Not_Narrowing;
  default:
    // Other kinds of conversions are not narrowings.
    return NK_Not_Narrowing;
  }
}

/// dump - Print this standard conversion sequence to standard
/// error. Useful for debugging overloading issues.
LLVM_DUMP_METHOD void StandardConversionSequence::dump() const {
  raw_ostream &OS = llvm::errs();
  bool PrintedSomething = false;
  if (First != ICK_Identity) {
    OS << GetImplicitConversionName(First);
    PrintedSomething = true;
  }

  if (Second != ICK_Identity) {
    if (PrintedSomething) {
      OS << " -> ";
    }
    OS << GetImplicitConversionName(Second);

    if (CopyConstructor) {
      OS << " (by copy constructor)";
    } else if (DirectBinding) {
      OS << " (direct reference binding)";
    } else if (ReferenceBinding) {
      OS << " (reference binding)";
    }
    PrintedSomething = true;
  }

  if (Third != ICK_Identity) {
    if (PrintedSomething) {
      OS << " -> ";
    }
    OS << GetImplicitConversionName(Third);
    PrintedSomething = true;
  }

  if (!PrintedSomething) {
    OS << "No conversions required";
  }
}

/// dump - Print this user-defined conversion sequence to standard
/// error. Useful for debugging overloading issues.
void UserDefinedConversionSequence::dump() const {
  raw_ostream &OS = llvm::errs();
  if (Before.First || Before.Second || Before.Third) {
    Before.dump();
    OS << " -> ";
  }
  if (ConversionFunction)
    OS << '\'' << *ConversionFunction << '\'';
  else
    OS << "aggregate initialization";
  if (After.First || After.Second || After.Third) {
    OS << " -> ";
    After.dump();
  }
}

/// dump - Print this implicit conversion sequence to standard
/// error. Useful for debugging overloading issues.
void ImplicitConversionSequence::dump() const {
  raw_ostream &OS = llvm::errs();
  if (hasInitializerListContainerType())
    OS << "Worst list element conversion: ";
  switch (ConversionKind) {
  case StandardConversion:
    OS << "Standard conversion: ";
    Standard.dump();
    break;
  case UserDefinedConversion:
    OS << "User-defined conversion: ";
    UserDefined.dump();
    break;
  case EllipsisConversion:
    OS << "Ellipsis conversion";
    break;
  case AmbiguousConversion:
    OS << "Ambiguous conversion";
    break;
  case BadConversion:
    OS << "Bad conversion";
    break;
  }

  OS << "\n";
}

void AmbiguousConversionSequence::construct() {
  new (&conversions()) ConversionSet();
}

void AmbiguousConversionSequence::destruct() {
  conversions().~ConversionSet();
}

void
AmbiguousConversionSequence::copyFrom(const AmbiguousConversionSequence &O) {
  FromTypePtr = O.FromTypePtr;
  ToTypePtr = O.ToTypePtr;
  new (&conversions()) ConversionSet(O.conversions());
}

namespace {
  // Structure used by DeductionFailureInfo to store
  // template argument information.
  struct DFIArguments {
    TemplateArgument FirstArg;
    TemplateArgument SecondArg;
  };
  // Structure used by DeductionFailureInfo to store
  // template parameter and template argument information.
  struct DFIParamWithArguments : DFIArguments {
    TemplateParameter Param;
  };
  // Structure used by DeductionFailureInfo to store template argument
  // information and the index of the problematic call argument.
  struct DFIDeducedMismatchArgs : DFIArguments {
    TemplateArgumentList *TemplateArgs;
    unsigned CallArgIndex;
  };
  // Structure used by DeductionFailureInfo to store information about
  // unsatisfied constraints.
  struct CNSInfo {
    TemplateArgumentList *TemplateArgs;
    ConstraintSatisfaction Satisfaction;
  };
}

/// Convert from Sema's representation of template deduction information
/// to the form used in overload-candidate information.
DeductionFailureInfo
clang::MakeDeductionFailureInfo(ASTContext &Context,
                                TemplateDeductionResult TDK,
                                TemplateDeductionInfo &Info) {
  DeductionFailureInfo Result;
  Result.Result = static_cast<unsigned>(TDK);
  Result.HasDiagnostic = false;
  switch (TDK) {
  case TemplateDeductionResult::Invalid:
  case TemplateDeductionResult::InstantiationDepth:
  case TemplateDeductionResult::TooManyArguments:
  case TemplateDeductionResult::TooFewArguments:
  case TemplateDeductionResult::MiscellaneousDeductionFailure:
  case TemplateDeductionResult::CUDATargetMismatch:
    Result.Data = nullptr;
    break;

  case TemplateDeductionResult::Incomplete:
  case TemplateDeductionResult::InvalidExplicitArguments:
    Result.Data = Info.Param.getOpaqueValue();
    break;

  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::DeducedMismatchNested: {
    // FIXME: Should allocate from normal heap so that we can free this later.
    auto *Saved = new (Context) DFIDeducedMismatchArgs;
    Saved->FirstArg = Info.FirstArg;
    Saved->SecondArg = Info.SecondArg;
    Saved->TemplateArgs = Info.takeSugared();
    Saved->CallArgIndex = Info.CallArgIndex;
    Result.Data = Saved;
    break;
  }

  case TemplateDeductionResult::NonDeducedMismatch: {
    // FIXME: Should allocate from normal heap so that we can free this later.
    DFIArguments *Saved = new (Context) DFIArguments;
    Saved->FirstArg = Info.FirstArg;
    Saved->SecondArg = Info.SecondArg;
    Result.Data = Saved;
    break;
  }

  case TemplateDeductionResult::IncompletePack:
    // FIXME: It's slightly wasteful to allocate two TemplateArguments for this.
  case TemplateDeductionResult::Inconsistent:
  case TemplateDeductionResult::Underqualified: {
    // FIXME: Should allocate from normal heap so that we can free this later.
    DFIParamWithArguments *Saved = new (Context) DFIParamWithArguments;
    Saved->Param = Info.Param;
    Saved->FirstArg = Info.FirstArg;
    Saved->SecondArg = Info.SecondArg;
    Result.Data = Saved;
    break;
  }

  case TemplateDeductionResult::SubstitutionFailure:
    Result.Data = Info.takeSugared();
    if (Info.hasSFINAEDiagnostic()) {
      PartialDiagnosticAt *Diag = new (Result.Diagnostic) PartialDiagnosticAt(
          SourceLocation(), PartialDiagnostic::NullDiagnostic());
      Info.takeSFINAEDiagnostic(*Diag);
      Result.HasDiagnostic = true;
    }
    break;

  case TemplateDeductionResult::ConstraintsNotSatisfied: {
    CNSInfo *Saved = new (Context) CNSInfo;
    Saved->TemplateArgs = Info.takeSugared();
    Saved->Satisfaction = Info.AssociatedConstraintsSatisfaction;
    Result.Data = Saved;
    break;
  }

  case TemplateDeductionResult::Success:
  case TemplateDeductionResult::NonDependentConversionFailure:
  case TemplateDeductionResult::AlreadyDiagnosed:
    llvm_unreachable("not a deduction failure");
  }

  return Result;
}

void DeductionFailureInfo::Destroy() {
  switch (static_cast<TemplateDeductionResult>(Result)) {
  case TemplateDeductionResult::Success:
  case TemplateDeductionResult::Invalid:
  case TemplateDeductionResult::InstantiationDepth:
  case TemplateDeductionResult::Incomplete:
  case TemplateDeductionResult::TooManyArguments:
  case TemplateDeductionResult::TooFewArguments:
  case TemplateDeductionResult::InvalidExplicitArguments:
  case TemplateDeductionResult::CUDATargetMismatch:
  case TemplateDeductionResult::NonDependentConversionFailure:
    break;

  case TemplateDeductionResult::IncompletePack:
  case TemplateDeductionResult::Inconsistent:
  case TemplateDeductionResult::Underqualified:
  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::DeducedMismatchNested:
  case TemplateDeductionResult::NonDeducedMismatch:
    // FIXME: Destroy the data?
    Data = nullptr;
    break;

  case TemplateDeductionResult::SubstitutionFailure:
    // FIXME: Destroy the template argument list?
    Data = nullptr;
    if (PartialDiagnosticAt *Diag = getSFINAEDiagnostic()) {
      Diag->~PartialDiagnosticAt();
      HasDiagnostic = false;
    }
    break;

  case TemplateDeductionResult::ConstraintsNotSatisfied:
    // FIXME: Destroy the template argument list?
    Data = nullptr;
    if (PartialDiagnosticAt *Diag = getSFINAEDiagnostic()) {
      Diag->~PartialDiagnosticAt();
      HasDiagnostic = false;
    }
    break;

  // Unhandled
  case TemplateDeductionResult::MiscellaneousDeductionFailure:
  case TemplateDeductionResult::AlreadyDiagnosed:
    break;
  }
}

PartialDiagnosticAt *DeductionFailureInfo::getSFINAEDiagnostic() {
  if (HasDiagnostic)
    return static_cast<PartialDiagnosticAt*>(static_cast<void*>(Diagnostic));
  return nullptr;
}

TemplateParameter DeductionFailureInfo::getTemplateParameter() {
  switch (static_cast<TemplateDeductionResult>(Result)) {
  case TemplateDeductionResult::Success:
  case TemplateDeductionResult::Invalid:
  case TemplateDeductionResult::InstantiationDepth:
  case TemplateDeductionResult::TooManyArguments:
  case TemplateDeductionResult::TooFewArguments:
  case TemplateDeductionResult::SubstitutionFailure:
  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::DeducedMismatchNested:
  case TemplateDeductionResult::NonDeducedMismatch:
  case TemplateDeductionResult::CUDATargetMismatch:
  case TemplateDeductionResult::NonDependentConversionFailure:
  case TemplateDeductionResult::ConstraintsNotSatisfied:
    return TemplateParameter();

  case TemplateDeductionResult::Incomplete:
  case TemplateDeductionResult::InvalidExplicitArguments:
    return TemplateParameter::getFromOpaqueValue(Data);

  case TemplateDeductionResult::IncompletePack:
  case TemplateDeductionResult::Inconsistent:
  case TemplateDeductionResult::Underqualified:
    return static_cast<DFIParamWithArguments*>(Data)->Param;

  // Unhandled
  case TemplateDeductionResult::MiscellaneousDeductionFailure:
  case TemplateDeductionResult::AlreadyDiagnosed:
    break;
  }

  return TemplateParameter();
}

TemplateArgumentList *DeductionFailureInfo::getTemplateArgumentList() {
  switch (static_cast<TemplateDeductionResult>(Result)) {
  case TemplateDeductionResult::Success:
  case TemplateDeductionResult::Invalid:
  case TemplateDeductionResult::InstantiationDepth:
  case TemplateDeductionResult::TooManyArguments:
  case TemplateDeductionResult::TooFewArguments:
  case TemplateDeductionResult::Incomplete:
  case TemplateDeductionResult::IncompletePack:
  case TemplateDeductionResult::InvalidExplicitArguments:
  case TemplateDeductionResult::Inconsistent:
  case TemplateDeductionResult::Underqualified:
  case TemplateDeductionResult::NonDeducedMismatch:
  case TemplateDeductionResult::CUDATargetMismatch:
  case TemplateDeductionResult::NonDependentConversionFailure:
    return nullptr;

  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::DeducedMismatchNested:
    return static_cast<DFIDeducedMismatchArgs*>(Data)->TemplateArgs;

  case TemplateDeductionResult::SubstitutionFailure:
    return static_cast<TemplateArgumentList*>(Data);

  case TemplateDeductionResult::ConstraintsNotSatisfied:
    return static_cast<CNSInfo*>(Data)->TemplateArgs;

  // Unhandled
  case TemplateDeductionResult::MiscellaneousDeductionFailure:
  case TemplateDeductionResult::AlreadyDiagnosed:
    break;
  }

  return nullptr;
}

const TemplateArgument *DeductionFailureInfo::getFirstArg() {
  switch (static_cast<TemplateDeductionResult>(Result)) {
  case TemplateDeductionResult::Success:
  case TemplateDeductionResult::Invalid:
  case TemplateDeductionResult::InstantiationDepth:
  case TemplateDeductionResult::Incomplete:
  case TemplateDeductionResult::TooManyArguments:
  case TemplateDeductionResult::TooFewArguments:
  case TemplateDeductionResult::InvalidExplicitArguments:
  case TemplateDeductionResult::SubstitutionFailure:
  case TemplateDeductionResult::CUDATargetMismatch:
  case TemplateDeductionResult::NonDependentConversionFailure:
  case TemplateDeductionResult::ConstraintsNotSatisfied:
    return nullptr;

  case TemplateDeductionResult::IncompletePack:
  case TemplateDeductionResult::Inconsistent:
  case TemplateDeductionResult::Underqualified:
  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::DeducedMismatchNested:
  case TemplateDeductionResult::NonDeducedMismatch:
    return &static_cast<DFIArguments*>(Data)->FirstArg;

  // Unhandled
  case TemplateDeductionResult::MiscellaneousDeductionFailure:
  case TemplateDeductionResult::AlreadyDiagnosed:
    break;
  }

  return nullptr;
}

const TemplateArgument *DeductionFailureInfo::getSecondArg() {
  switch (static_cast<TemplateDeductionResult>(Result)) {
  case TemplateDeductionResult::Success:
  case TemplateDeductionResult::Invalid:
  case TemplateDeductionResult::InstantiationDepth:
  case TemplateDeductionResult::Incomplete:
  case TemplateDeductionResult::IncompletePack:
  case TemplateDeductionResult::TooManyArguments:
  case TemplateDeductionResult::TooFewArguments:
  case TemplateDeductionResult::InvalidExplicitArguments:
  case TemplateDeductionResult::SubstitutionFailure:
  case TemplateDeductionResult::CUDATargetMismatch:
  case TemplateDeductionResult::NonDependentConversionFailure:
  case TemplateDeductionResult::ConstraintsNotSatisfied:
    return nullptr;

  case TemplateDeductionResult::Inconsistent:
  case TemplateDeductionResult::Underqualified:
  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::DeducedMismatchNested:
  case TemplateDeductionResult::NonDeducedMismatch:
    return &static_cast<DFIArguments*>(Data)->SecondArg;

  // Unhandled
  case TemplateDeductionResult::MiscellaneousDeductionFailure:
  case TemplateDeductionResult::AlreadyDiagnosed:
    break;
  }

  return nullptr;
}

std::optional<unsigned> DeductionFailureInfo::getCallArgIndex() {
  switch (static_cast<TemplateDeductionResult>(Result)) {
  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::DeducedMismatchNested:
    return static_cast<DFIDeducedMismatchArgs*>(Data)->CallArgIndex;

  default:
    return std::nullopt;
  }
}

static bool FunctionsCorrespond(ASTContext &Ctx, const FunctionDecl *X,
                                const FunctionDecl *Y) {
  if (!X || !Y)
    return false;
  if (X->getNumParams() != Y->getNumParams())
    return false;
  // FIXME: when do rewritten comparison operators
  // with explicit object parameters correspond?
  // https://cplusplus.github.io/CWG/issues/2797.html
  for (unsigned I = 0; I < X->getNumParams(); ++I)
    if (!Ctx.hasSameUnqualifiedType(X->getParamDecl(I)->getType(),
                                    Y->getParamDecl(I)->getType()))
      return false;
  if (auto *FTX = X->getDescribedFunctionTemplate()) {
    auto *FTY = Y->getDescribedFunctionTemplate();
    if (!FTY)
      return false;
    if (!Ctx.isSameTemplateParameterList(FTX->getTemplateParameters(),
                                         FTY->getTemplateParameters()))
      return false;
  }
  return true;
}

static bool shouldAddReversedEqEq(Sema &S, SourceLocation OpLoc,
                                  Expr *FirstOperand, FunctionDecl *EqFD) {
  assert(EqFD->getOverloadedOperator() ==
         OverloadedOperatorKind::OO_EqualEqual);
  // C++2a [over.match.oper]p4:
  // A non-template function or function template F named operator== is a
  // rewrite target with first operand o unless a search for the name operator!=
  // in the scope S from the instantiation context of the operator expression
  // finds a function or function template that would correspond
  // ([basic.scope.scope]) to F if its name were operator==, where S is the
  // scope of the class type of o if F is a class member, and the namespace
  // scope of which F is a member otherwise. A function template specialization
  // named operator== is a rewrite target if its function template is a rewrite
  // target.
  DeclarationName NotEqOp = S.Context.DeclarationNames.getCXXOperatorName(
      OverloadedOperatorKind::OO_ExclaimEqual);
  if (isa<CXXMethodDecl>(EqFD)) {
    // If F is a class member, search scope is class type of first operand.
    QualType RHS = FirstOperand->getType();
    auto *RHSRec = RHS->getAs<RecordType>();
    if (!RHSRec)
      return true;
    LookupResult Members(S, NotEqOp, OpLoc,
                         Sema::LookupNameKind::LookupMemberName);
    S.LookupQualifiedName(Members, RHSRec->getDecl());
    Members.suppressAccessDiagnostics();
    for (NamedDecl *Op : Members)
      if (FunctionsCorrespond(S.Context, EqFD, Op->getAsFunction()))
        return false;
    return true;
  }
  // Otherwise the search scope is the namespace scope of which F is a member.
  for (NamedDecl *Op : EqFD->getEnclosingNamespaceContext()->lookup(NotEqOp)) {
    auto *NotEqFD = Op->getAsFunction();
    if (auto *UD = dyn_cast<UsingShadowDecl>(Op))
      NotEqFD = UD->getUnderlyingDecl()->getAsFunction();
    if (FunctionsCorrespond(S.Context, EqFD, NotEqFD) && S.isVisible(NotEqFD) &&
        declaresSameEntity(cast<Decl>(EqFD->getEnclosingNamespaceContext()),
                           cast<Decl>(Op->getLexicalDeclContext())))
      return false;
  }
  return true;
}

bool OverloadCandidateSet::OperatorRewriteInfo::allowsReversed(
    OverloadedOperatorKind Op) {
  if (!AllowRewrittenCandidates)
    return false;
  return Op == OO_EqualEqual || Op == OO_Spaceship;
}

bool OverloadCandidateSet::OperatorRewriteInfo::shouldAddReversed(
    Sema &S, ArrayRef<Expr *> OriginalArgs, FunctionDecl *FD) {
  auto Op = FD->getOverloadedOperator();
  if (!allowsReversed(Op))
    return false;
  if (Op == OverloadedOperatorKind::OO_EqualEqual) {
    assert(OriginalArgs.size() == 2);
    if (!shouldAddReversedEqEq(
            S, OpLoc, /*FirstOperand in reversed args*/ OriginalArgs[1], FD))
      return false;
  }
  // Don't bother adding a reversed candidate that can never be a better
  // match than the non-reversed version.
  return FD->getNumNonObjectParams() != 2 ||
         !S.Context.hasSameUnqualifiedType(FD->getParamDecl(0)->getType(),
                                           FD->getParamDecl(1)->getType()) ||
         FD->hasAttr<EnableIfAttr>();
}

void OverloadCandidateSet::destroyCandidates() {
  for (iterator i = begin(), e = end(); i != e; ++i) {
    for (auto &C : i->Conversions)
      C.~ImplicitConversionSequence();
    if (!i->Viable && i->FailureKind == ovl_fail_bad_deduction)
      i->DeductionFailure.Destroy();
  }
}

void OverloadCandidateSet::clear(CandidateSetKind CSK) {
  destroyCandidates();
  SlabAllocator.Reset();
  NumInlineBytesUsed = 0;
  Candidates.clear();
  Functions.clear();
  Kind = CSK;
}

namespace {
  class UnbridgedCastsSet {
    struct Entry {
      Expr **Addr;
      Expr *Saved;
    };
    SmallVector<Entry, 2> Entries;

  public:
    void save(Sema &S, Expr *&E) {
      assert(E->hasPlaceholderType(BuiltinType::ARCUnbridgedCast));
      Entry entry = { &E, E };
      Entries.push_back(entry);
      E = S.ObjC().stripARCUnbridgedCast(E);
    }

    void restore() {
      for (SmallVectorImpl<Entry>::iterator
             i = Entries.begin(), e = Entries.end(); i != e; ++i)
        *i->Addr = i->Saved;
    }
  };
}

/// checkPlaceholderForOverload - Do any interesting placeholder-like
/// preprocessing on the given expression.
///
/// \param unbridgedCasts a collection to which to add unbridged casts;
///   without this, they will be immediately diagnosed as errors
///
/// Return true on unrecoverable error.
static bool
checkPlaceholderForOverload(Sema &S, Expr *&E,
                            UnbridgedCastsSet *unbridgedCasts = nullptr) {
  if (const BuiltinType *placeholder =  E->getType()->getAsPlaceholderType()) {
    // We can't handle overloaded expressions here because overload
    // resolution might reasonably tweak them.
    if (placeholder->getKind() == BuiltinType::Overload) return false;

    // If the context potentially accepts unbridged ARC casts, strip
    // the unbridged cast and add it to the collection for later restoration.
    if (placeholder->getKind() == BuiltinType::ARCUnbridgedCast &&
        unbridgedCasts) {
      unbridgedCasts->save(S, E);
      return false;
    }

    // Go ahead and check everything else.
    ExprResult result = S.CheckPlaceholderExpr(E);
    if (result.isInvalid())
      return true;

    E = result.get();
    return false;
  }

  // Nothing to do.
  return false;
}

/// checkArgPlaceholdersForOverload - Check a set of call operands for
/// placeholders.
static bool checkArgPlaceholdersForOverload(Sema &S, MultiExprArg Args,
                                            UnbridgedCastsSet &unbridged) {
  for (unsigned i = 0, e = Args.size(); i != e; ++i)
    if (checkPlaceholderForOverload(S, Args[i], &unbridged))
      return true;

  return false;
}

Sema::OverloadKind
Sema::CheckOverload(Scope *S, FunctionDecl *New, const LookupResult &Old,
                    NamedDecl *&Match, bool NewIsUsingDecl) {
  for (LookupResult::iterator I = Old.begin(), E = Old.end();
         I != E; ++I) {
    NamedDecl *OldD = *I;

    bool OldIsUsingDecl = false;
    if (isa<UsingShadowDecl>(OldD)) {
      OldIsUsingDecl = true;

      // We can always introduce two using declarations into the same
      // context, even if they have identical signatures.
      if (NewIsUsingDecl) continue;

      OldD = cast<UsingShadowDecl>(OldD)->getTargetDecl();
    }

    // A using-declaration does not conflict with another declaration
    // if one of them is hidden.
    if ((OldIsUsingDecl || NewIsUsingDecl) && !isVisible(*I))
      continue;

    // If either declaration was introduced by a using declaration,
    // we'll need to use slightly different rules for matching.
    // Essentially, these rules are the normal rules, except that
    // function templates hide function templates with different
    // return types or template parameter lists.
    bool UseMemberUsingDeclRules =
      (OldIsUsingDecl || NewIsUsingDecl) && CurContext->isRecord() &&
      !New->getFriendObjectKind();

    if (FunctionDecl *OldF = OldD->getAsFunction()) {
      if (!IsOverload(New, OldF, UseMemberUsingDeclRules)) {
        if (UseMemberUsingDeclRules && OldIsUsingDecl) {
          HideUsingShadowDecl(S, cast<UsingShadowDecl>(*I));
          continue;
        }

        if (!isa<FunctionTemplateDecl>(OldD) &&
            !shouldLinkPossiblyHiddenDecl(*I, New))
          continue;

        Match = *I;
        return Ovl_Match;
      }

      // Builtins that have custom typechecking or have a reference should
      // not be overloadable or redeclarable.
      if (!getASTContext().canBuiltinBeRedeclared(OldF)) {
        Match = *I;
        return Ovl_NonFunction;
      }
    } else if (isa<UsingDecl>(OldD) || isa<UsingPackDecl>(OldD)) {
      // We can overload with these, which can show up when doing
      // redeclaration checks for UsingDecls.
      assert(Old.getLookupKind() == LookupUsingDeclName);
    } else if (isa<TagDecl>(OldD)) {
      // We can always overload with tags by hiding them.
    } else if (auto *UUD = dyn_cast<UnresolvedUsingValueDecl>(OldD)) {
      // Optimistically assume that an unresolved using decl will
      // overload; if it doesn't, we'll have to diagnose during
      // template instantiation.
      //
      // Exception: if the scope is dependent and this is not a class
      // member, the using declaration can only introduce an enumerator.
      if (UUD->getQualifier()->isDependent() && !UUD->isCXXClassMember()) {
        Match = *I;
        return Ovl_NonFunction;
      }
    } else {
      // (C++ 13p1):
      //   Only function declarations can be overloaded; object and type
      //   declarations cannot be overloaded.
      Match = *I;
      return Ovl_NonFunction;
    }
  }

  // C++ [temp.friend]p1:
  //   For a friend function declaration that is not a template declaration:
  //    -- if the name of the friend is a qualified or unqualified template-id,
  //       [...], otherwise
  //    -- if the name of the friend is a qualified-id and a matching
  //       non-template function is found in the specified class or namespace,
  //       the friend declaration refers to that function, otherwise,
  //    -- if the name of the friend is a qualified-id and a matching function
  //       template is found in the specified class or namespace, the friend
  //       declaration refers to the deduced specialization of that function
  //       template, otherwise
  //    -- the name shall be an unqualified-id [...]
  // If we get here for a qualified friend declaration, we've just reached the
  // third bullet. If the type of the friend is dependent, skip this lookup
  // until instantiation.
  if (New->getFriendObjectKind() && New->getQualifier() &&
      !New->getDescribedFunctionTemplate() &&
      !New->getDependentSpecializationInfo() &&
      !New->getType()->isDependentType()) {
    LookupResult TemplateSpecResult(LookupResult::Temporary, Old);
    TemplateSpecResult.addAllDecls(Old);
    if (CheckFunctionTemplateSpecialization(New, nullptr, TemplateSpecResult,
                                            /*QualifiedFriend*/true)) {
      New->setInvalidDecl();
      return Ovl_Overload;
    }

    Match = TemplateSpecResult.getAsSingle<FunctionDecl>();
    return Ovl_Match;
  }

  return Ovl_Overload;
}

static bool IsOverloadOrOverrideImpl(Sema &SemaRef, FunctionDecl *New,
                                     FunctionDecl *Old,
                                     bool UseMemberUsingDeclRules,
                                     bool ConsiderCudaAttrs,
                                     bool UseOverrideRules = false) {
  // C++ [basic.start.main]p2: This function shall not be overloaded.
  if (New->isMain())
    return false;

  // MSVCRT user defined entry points cannot be overloaded.
  if (New->isMSVCRTEntryPoint())
    return false;

  NamedDecl *OldDecl = Old;
  NamedDecl *NewDecl = New;
  FunctionTemplateDecl *OldTemplate = Old->getDescribedFunctionTemplate();
  FunctionTemplateDecl *NewTemplate = New->getDescribedFunctionTemplate();

  // C++ [temp.fct]p2:
  //   A function template can be overloaded with other function templates
  //   and with normal (non-template) functions.
  if ((OldTemplate == nullptr) != (NewTemplate == nullptr))
    return true;

  // Is the function New an overload of the function Old?
  QualType OldQType = SemaRef.Context.getCanonicalType(Old->getType());
  QualType NewQType = SemaRef.Context.getCanonicalType(New->getType());

  // Compare the signatures (C++ 1.3.10) of the two functions to
  // determine whether they are overloads. If we find any mismatch
  // in the signature, they are overloads.

  // If either of these functions is a K&R-style function (no
  // prototype), then we consider them to have matching signatures.
  if (isa<FunctionNoProtoType>(OldQType.getTypePtr()) ||
      isa<FunctionNoProtoType>(NewQType.getTypePtr()))
    return false;

  const auto *OldType = cast<FunctionProtoType>(OldQType);
  const auto *NewType = cast<FunctionProtoType>(NewQType);

  // The signature of a function includes the types of its
  // parameters (C++ 1.3.10), which includes the presence or absence
  // of the ellipsis; see C++ DR 357).
  if (OldQType != NewQType && OldType->isVariadic() != NewType->isVariadic())
    return true;

  // For member-like friends, the enclosing class is part of the signature.
  if ((New->isMemberLikeConstrainedFriend() ||
       Old->isMemberLikeConstrainedFriend()) &&
      !New->getLexicalDeclContext()->Equals(Old->getLexicalDeclContext()))
    return true;

  // Compare the parameter lists.
  // This can only be done once we have establish that friend functions
  // inhabit the same context, otherwise we might tried to instantiate
  // references to non-instantiated entities during constraint substitution.
  // GH78101.
  if (NewTemplate) {
    OldDecl = OldTemplate;
    NewDecl = NewTemplate;
    // C++ [temp.over.link]p4:
    //   The signature of a function template consists of its function
    //   signature, its return type and its template parameter list. The names
    //   of the template parameters are significant only for establishing the
    //   relationship between the template parameters and the rest of the
    //   signature.
    //
    // We check the return type and template parameter lists for function
    // templates first; the remaining checks follow.
    bool SameTemplateParameterList = SemaRef.TemplateParameterListsAreEqual(
        NewTemplate, NewTemplate->getTemplateParameters(), OldTemplate,
        OldTemplate->getTemplateParameters(), false, Sema::TPL_TemplateMatch);
    bool SameReturnType = SemaRef.Context.hasSameType(
        Old->getDeclaredReturnType(), New->getDeclaredReturnType());
    // FIXME(GH58571): Match template parameter list even for non-constrained
    // template heads. This currently ensures that the code prior to C++20 is
    // not newly broken.
    bool ConstraintsInTemplateHead =
        NewTemplate->getTemplateParameters()->hasAssociatedConstraints() ||
        OldTemplate->getTemplateParameters()->hasAssociatedConstraints();
    // C++ [namespace.udecl]p11:
    //   The set of declarations named by a using-declarator that inhabits a
    //   class C does not include member functions and member function
    //   templates of a base class that "correspond" to (and thus would
    //   conflict with) a declaration of a function or function template in
    //   C.
    // Comparing return types is not required for the "correspond" check to
    // decide whether a member introduced by a shadow declaration is hidden.
    if (UseMemberUsingDeclRules && ConstraintsInTemplateHead &&
        !SameTemplateParameterList)
      return true;
    if (!UseMemberUsingDeclRules &&
        (!SameTemplateParameterList || !SameReturnType))
      return true;
  }

  const auto *OldMethod = dyn_cast<CXXMethodDecl>(Old);
  const auto *NewMethod = dyn_cast<CXXMethodDecl>(New);

  int OldParamsOffset = 0;
  int NewParamsOffset = 0;

  // When determining if a method is an overload from a base class, act as if
  // the implicit object parameter are of the same type.

  auto NormalizeQualifiers = [&](const CXXMethodDecl *M, Qualifiers Q) {
    if (M->isExplicitObjectMemberFunction())
      return Q;

    // We do not allow overloading based off of '__restrict'.
    Q.removeRestrict();

    // We may not have applied the implicit const for a constexpr member
    // function yet (because we haven't yet resolved whether this is a static
    // or non-static member function). Add it now, on the assumption that this
    // is a redeclaration of OldMethod.
    if (!SemaRef.getLangOpts().CPlusPlus14 &&
        (M->isConstexpr() || M->isConsteval()) &&
        !isa<CXXConstructorDecl>(NewMethod))
      Q.addConst();
    return Q;
  };

  auto CompareType = [&](QualType Base, QualType D) {
    auto BS = Base.getNonReferenceType().getCanonicalType().split();
    BS.Quals = NormalizeQualifiers(OldMethod, BS.Quals);

    auto DS = D.getNonReferenceType().getCanonicalType().split();
    DS.Quals = NormalizeQualifiers(NewMethod, DS.Quals);

    if (BS.Quals != DS.Quals)
      return false;

    if (OldMethod->isImplicitObjectMemberFunction() &&
        OldMethod->getParent() != NewMethod->getParent()) {
      QualType ParentType =
          SemaRef.Context.getTypeDeclType(OldMethod->getParent())
              .getCanonicalType();
      if (ParentType.getTypePtr() != BS.Ty)
        return false;
      BS.Ty = DS.Ty;
    }

    // FIXME: should we ignore some type attributes here?
    if (BS.Ty != DS.Ty)
      return false;

    if (Base->isLValueReferenceType())
      return D->isLValueReferenceType();
    return Base->isRValueReferenceType() == D->isRValueReferenceType();
  };

  // If the function is a class member, its signature includes the
  // cv-qualifiers (if any) and ref-qualifier (if any) on the function itself.
  auto DiagnoseInconsistentRefQualifiers = [&]() {
    if (SemaRef.LangOpts.CPlusPlus23)
      return false;
    if (OldMethod->getRefQualifier() == NewMethod->getRefQualifier())
      return false;
    if (OldMethod->isExplicitObjectMemberFunction() ||
        NewMethod->isExplicitObjectMemberFunction())
      return false;
    if (!UseMemberUsingDeclRules && (OldMethod->getRefQualifier() == RQ_None ||
                                     NewMethod->getRefQualifier() == RQ_None)) {
      SemaRef.Diag(NewMethod->getLocation(), diag::err_ref_qualifier_overload)
          << NewMethod->getRefQualifier() << OldMethod->getRefQualifier();
      SemaRef.Diag(OldMethod->getLocation(), diag::note_previous_declaration);
      return true;
    }
    return false;
  };

  if (OldMethod && OldMethod->isExplicitObjectMemberFunction())
    OldParamsOffset++;
  if (NewMethod && NewMethod->isExplicitObjectMemberFunction())
    NewParamsOffset++;

  if (OldType->getNumParams() - OldParamsOffset !=
          NewType->getNumParams() - NewParamsOffset ||
      !SemaRef.FunctionParamTypesAreEqual(
          {OldType->param_type_begin() + OldParamsOffset,
           OldType->param_type_end()},
          {NewType->param_type_begin() + NewParamsOffset,
           NewType->param_type_end()},
          nullptr)) {
    return true;
  }

  if (OldMethod && NewMethod && !OldMethod->isStatic() &&
      !NewMethod->isStatic()) {
    bool HaveCorrespondingObjectParameters = [&](const CXXMethodDecl *Old,
                                                 const CXXMethodDecl *New) {
      auto NewObjectType = New->getFunctionObjectParameterReferenceType();
      auto OldObjectType = Old->getFunctionObjectParameterReferenceType();

      auto IsImplicitWithNoRefQual = [](const CXXMethodDecl *F) {
        return F->getRefQualifier() == RQ_None &&
               !F->isExplicitObjectMemberFunction();
      };

      if (IsImplicitWithNoRefQual(Old) != IsImplicitWithNoRefQual(New) &&
          CompareType(OldObjectType.getNonReferenceType(),
                      NewObjectType.getNonReferenceType()))
        return true;
      return CompareType(OldObjectType, NewObjectType);
    }(OldMethod, NewMethod);

    if (!HaveCorrespondingObjectParameters) {
      if (DiagnoseInconsistentRefQualifiers())
        return true;
      // CWG2554
      // and, if at least one is an explicit object member function, ignoring
      // object parameters
      if (!UseOverrideRules || (!NewMethod->isExplicitObjectMemberFunction() &&
                                !OldMethod->isExplicitObjectMemberFunction()))
        return true;
    }
  }

  if (!UseOverrideRules &&
      New->getTemplateSpecializationKind() != TSK_ExplicitSpecialization) {
    Expr *NewRC = New->getTrailingRequiresClause(),
         *OldRC = Old->getTrailingRequiresClause();
    if ((NewRC != nullptr) != (OldRC != nullptr))
      return true;
    if (NewRC &&
        !SemaRef.AreConstraintExpressionsEqual(OldDecl, OldRC, NewDecl, NewRC))
      return true;
  }

  if (NewMethod && OldMethod && OldMethod->isImplicitObjectMemberFunction() &&
      NewMethod->isImplicitObjectMemberFunction()) {
    if (DiagnoseInconsistentRefQualifiers())
      return true;
  }

  // Though pass_object_size is placed on parameters and takes an argument, we
  // consider it to be a function-level modifier for the sake of function
  // identity. Either the function has one or more parameters with
  // pass_object_size or it doesn't.
  if (functionHasPassObjectSizeParams(New) !=
      functionHasPassObjectSizeParams(Old))
    return true;

  // enable_if attributes are an order-sensitive part of the signature.
  for (specific_attr_iterator<EnableIfAttr>
         NewI = New->specific_attr_begin<EnableIfAttr>(),
         NewE = New->specific_attr_end<EnableIfAttr>(),
         OldI = Old->specific_attr_begin<EnableIfAttr>(),
         OldE = Old->specific_attr_end<EnableIfAttr>();
       NewI != NewE || OldI != OldE; ++NewI, ++OldI) {
    if (NewI == NewE || OldI == OldE)
      return true;
    llvm::FoldingSetNodeID NewID, OldID;
    NewI->getCond()->Profile(NewID, SemaRef.Context, true);
    OldI->getCond()->Profile(OldID, SemaRef.Context, true);
    if (NewID != OldID)
      return true;
  }

  if (SemaRef.getLangOpts().CUDA && ConsiderCudaAttrs) {
    // Don't allow overloading of destructors.  (In theory we could, but it
    // would be a giant change to clang.)
    if (!isa<CXXDestructorDecl>(New)) {
      CUDAFunctionTarget NewTarget = SemaRef.CUDA().IdentifyTarget(New),
                         OldTarget = SemaRef.CUDA().IdentifyTarget(Old);
      if (NewTarget != CUDAFunctionTarget::InvalidTarget) {
        assert((OldTarget != CUDAFunctionTarget::InvalidTarget) &&
               "Unexpected invalid target.");

        // Allow overloading of functions with same signature and different CUDA
        // target attributes.
        if (NewTarget != OldTarget)
          return true;
      }
    }
  }

  // The signatures match; this is not an overload.
  return false;
}

bool Sema::IsOverload(FunctionDecl *New, FunctionDecl *Old,
                      bool UseMemberUsingDeclRules, bool ConsiderCudaAttrs) {
  return IsOverloadOrOverrideImpl(*this, New, Old, UseMemberUsingDeclRules,
                                  ConsiderCudaAttrs);
}

bool Sema::IsOverride(FunctionDecl *MD, FunctionDecl *BaseMD,
                      bool UseMemberUsingDeclRules, bool ConsiderCudaAttrs) {
  return IsOverloadOrOverrideImpl(*this, MD, BaseMD,
                                  /*UseMemberUsingDeclRules=*/false,
                                  /*ConsiderCudaAttrs=*/true,
                                  /*UseOverrideRules=*/true);
}

/// Tries a user-defined conversion from From to ToType.
///
/// Produces an implicit conversion sequence for when a standard conversion
/// is not an option. See TryImplicitConversion for more information.
static ImplicitConversionSequence
TryUserDefinedConversion(Sema &S, Expr *From, QualType ToType,
                         bool SuppressUserConversions,
                         AllowedExplicit AllowExplicit,
                         bool InOverloadResolution,
                         bool CStyle,
                         bool AllowObjCWritebackConversion,
                         bool AllowObjCConversionOnExplicit) {
  ImplicitConversionSequence ICS;

  if (SuppressUserConversions) {
    // We're not in the case above, so there is no conversion that
    // we can perform.
    ICS.setBad(BadConversionSequence::no_conversion, From, ToType);
    return ICS;
  }

  // Attempt user-defined conversion.
  OverloadCandidateSet Conversions(From->getExprLoc(),
                                   OverloadCandidateSet::CSK_Normal);
  switch (IsUserDefinedConversion(S, From, ToType, ICS.UserDefined,
                                  Conversions, AllowExplicit,
                                  AllowObjCConversionOnExplicit)) {
  case OR_Success:
  case OR_Deleted:
    ICS.setUserDefined();
    // C++ [over.ics.user]p4:
    //   A conversion of an expression of class type to the same class
    //   type is given Exact Match rank, and a conversion of an
    //   expression of class type to a base class of that type is
    //   given Conversion rank, in spite of the fact that a copy
    //   constructor (i.e., a user-defined conversion function) is
    //   called for those cases.
    if (CXXConstructorDecl *Constructor
          = dyn_cast<CXXConstructorDecl>(ICS.UserDefined.ConversionFunction)) {
      QualType FromCanon
        = S.Context.getCanonicalType(From->getType().getUnqualifiedType());
      QualType ToCanon
        = S.Context.getCanonicalType(ToType).getUnqualifiedType();
      if (Constructor->isCopyConstructor() &&
          (FromCanon == ToCanon ||
           S.IsDerivedFrom(From->getBeginLoc(), FromCanon, ToCanon))) {
        // Turn this into a "standard" conversion sequence, so that it
        // gets ranked with standard conversion sequences.
        DeclAccessPair Found = ICS.UserDefined.FoundConversionFunction;
        ICS.setStandard();
        ICS.Standard.setAsIdentityConversion();
        ICS.Standard.setFromType(From->getType());
        ICS.Standard.setAllToTypes(ToType);
        ICS.Standard.CopyConstructor = Constructor;
        ICS.Standard.FoundCopyConstructor = Found;
        if (ToCanon != FromCanon)
          ICS.Standard.Second = ICK_Derived_To_Base;
      }
    }
    break;

  case OR_Ambiguous:
    ICS.setAmbiguous();
    ICS.Ambiguous.setFromType(From->getType());
    ICS.Ambiguous.setToType(ToType);
    for (OverloadCandidateSet::iterator Cand = Conversions.begin();
         Cand != Conversions.end(); ++Cand)
      if (Cand->Best)
        ICS.Ambiguous.addConversion(Cand->FoundDecl, Cand->Function);
    break;

    // Fall through.
  case OR_No_Viable_Function:
    ICS.setBad(BadConversionSequence::no_conversion, From, ToType);
    break;
  }

  return ICS;
}

/// TryImplicitConversion - Attempt to perform an implicit conversion
/// from the given expression (Expr) to the given type (ToType). This
/// function returns an implicit conversion sequence that can be used
/// to perform the initialization. Given
///
///   void f(float f);
///   void g(int i) { f(i); }
///
/// this routine would produce an implicit conversion sequence to
/// describe the initialization of f from i, which will be a standard
/// conversion sequence containing an lvalue-to-rvalue conversion (C++
/// 4.1) followed by a floating-integral conversion (C++ 4.9).
//
/// Note that this routine only determines how the conversion can be
/// performed; it does not actually perform the conversion. As such,
/// it will not produce any diagnostics if no conversion is available,
/// but will instead return an implicit conversion sequence of kind
/// "BadConversion".
///
/// If @p SuppressUserConversions, then user-defined conversions are
/// not permitted.
/// If @p AllowExplicit, then explicit user-defined conversions are
/// permitted.
///
/// \param AllowObjCWritebackConversion Whether we allow the Objective-C
/// writeback conversion, which allows __autoreleasing id* parameters to
/// be initialized with __strong id* or __weak id* arguments.
static ImplicitConversionSequence
TryImplicitConversion(Sema &S, Expr *From, QualType ToType,
                      bool SuppressUserConversions,
                      AllowedExplicit AllowExplicit,
                      bool InOverloadResolution,
                      bool CStyle,
                      bool AllowObjCWritebackConversion,
                      bool AllowObjCConversionOnExplicit) {
  ImplicitConversionSequence ICS;
  if (IsStandardConversion(S, From, ToType, InOverloadResolution,
                           ICS.Standard, CStyle, AllowObjCWritebackConversion)){
    ICS.setStandard();
    return ICS;
  }

  if (!S.getLangOpts().CPlusPlus) {
    ICS.setBad(BadConversionSequence::no_conversion, From, ToType);
    return ICS;
  }

  // C++ [over.ics.user]p4:
  //   A conversion of an expression of class type to the same class
  //   type is given Exact Match rank, and a conversion of an
  //   expression of class type to a base class of that type is
  //   given Conversion rank, in spite of the fact that a copy/move
  //   constructor (i.e., a user-defined conversion function) is
  //   called for those cases.
  QualType FromType = From->getType();
  if (ToType->getAs<RecordType>() && FromType->getAs<RecordType>() &&
      (S.Context.hasSameUnqualifiedType(FromType, ToType) ||
       S.IsDerivedFrom(From->getBeginLoc(), FromType, ToType))) {
    ICS.setStandard();
    ICS.Standard.setAsIdentityConversion();
    ICS.Standard.setFromType(FromType);
    ICS.Standard.setAllToTypes(ToType);

    // We don't actually check at this point whether there is a valid
    // copy/move constructor, since overloading just assumes that it
    // exists. When we actually perform initialization, we'll find the
    // appropriate constructor to copy the returned object, if needed.
    ICS.Standard.CopyConstructor = nullptr;

    // Determine whether this is considered a derived-to-base conversion.
    if (!S.Context.hasSameUnqualifiedType(FromType, ToType))
      ICS.Standard.Second = ICK_Derived_To_Base;

    return ICS;
  }

  return TryUserDefinedConversion(S, From, ToType, SuppressUserConversions,
                                  AllowExplicit, InOverloadResolution, CStyle,
                                  AllowObjCWritebackConversion,
                                  AllowObjCConversionOnExplicit);
}

ImplicitConversionSequence
Sema::TryImplicitConversion(Expr *From, QualType ToType,
                            bool SuppressUserConversions,
                            AllowedExplicit AllowExplicit,
                            bool InOverloadResolution,
                            bool CStyle,
                            bool AllowObjCWritebackConversion) {
  return ::TryImplicitConversion(*this, From, ToType, SuppressUserConversions,
                                 AllowExplicit, InOverloadResolution, CStyle,
                                 AllowObjCWritebackConversion,
                                 /*AllowObjCConversionOnExplicit=*/false);
}

ExprResult Sema::PerformImplicitConversion(Expr *From, QualType ToType,
                                           AssignmentAction Action,
                                           bool AllowExplicit) {
  if (checkPlaceholderForOverload(*this, From))
    return ExprError();

  // Objective-C ARC: Determine whether we will allow the writeback conversion.
  bool AllowObjCWritebackConversion
    = getLangOpts().ObjCAutoRefCount &&
      (Action == AA_Passing || Action == AA_Sending);
  if (getLangOpts().ObjC)
    ObjC().CheckObjCBridgeRelatedConversions(From->getBeginLoc(), ToType,
                                             From->getType(), From);
  ImplicitConversionSequence ICS = ::TryImplicitConversion(
      *this, From, ToType,
      /*SuppressUserConversions=*/false,
      AllowExplicit ? AllowedExplicit::All : AllowedExplicit::None,
      /*InOverloadResolution=*/false,
      /*CStyle=*/false, AllowObjCWritebackConversion,
      /*AllowObjCConversionOnExplicit=*/false);
  return PerformImplicitConversion(From, ToType, ICS, Action);
}

bool Sema::IsFunctionConversion(QualType FromType, QualType ToType,
                                QualType &ResultTy) {
  if (Context.hasSameUnqualifiedType(FromType, ToType))
    return false;

  // Permit the conversion F(t __attribute__((noreturn))) -> F(t)
  //                    or F(t noexcept) -> F(t)
  // where F adds one of the following at most once:
  //   - a pointer
  //   - a member pointer
  //   - a block pointer
  // Changes here need matching changes in FindCompositePointerType.
  CanQualType CanTo = Context.getCanonicalType(ToType);
  CanQualType CanFrom = Context.getCanonicalType(FromType);
  Type::TypeClass TyClass = CanTo->getTypeClass();
  if (TyClass != CanFrom->getTypeClass()) return false;
  if (TyClass != Type::FunctionProto && TyClass != Type::FunctionNoProto) {
    if (TyClass == Type::Pointer) {
      CanTo = CanTo.castAs<PointerType>()->getPointeeType();
      CanFrom = CanFrom.castAs<PointerType>()->getPointeeType();
    } else if (TyClass == Type::BlockPointer) {
      CanTo = CanTo.castAs<BlockPointerType>()->getPointeeType();
      CanFrom = CanFrom.castAs<BlockPointerType>()->getPointeeType();
    } else if (TyClass == Type::MemberPointer) {
      auto ToMPT = CanTo.castAs<MemberPointerType>();
      auto FromMPT = CanFrom.castAs<MemberPointerType>();
      // A function pointer conversion cannot change the class of the function.
      if (ToMPT->getClass() != FromMPT->getClass())
        return false;
      CanTo = ToMPT->getPointeeType();
      CanFrom = FromMPT->getPointeeType();
    } else {
      return false;
    }

    TyClass = CanTo->getTypeClass();
    if (TyClass != CanFrom->getTypeClass()) return false;
    if (TyClass != Type::FunctionProto && TyClass != Type::FunctionNoProto)
      return false;
  }

  const auto *FromFn = cast<FunctionType>(CanFrom);
  FunctionType::ExtInfo FromEInfo = FromFn->getExtInfo();

  const auto *ToFn = cast<FunctionType>(CanTo);
  FunctionType::ExtInfo ToEInfo = ToFn->getExtInfo();

  bool Changed = false;

  // Drop 'noreturn' if not present in target type.
  if (FromEInfo.getNoReturn() && !ToEInfo.getNoReturn()) {
    FromFn = Context.adjustFunctionType(FromFn, FromEInfo.withNoReturn(false));
    Changed = true;
  }

  // Drop 'noexcept' if not present in target type.
  if (const auto *FromFPT = dyn_cast<FunctionProtoType>(FromFn)) {
    const auto *ToFPT = cast<FunctionProtoType>(ToFn);
    if (FromFPT->isNothrow() && !ToFPT->isNothrow()) {
      FromFn = cast<FunctionType>(
          Context.getFunctionTypeWithExceptionSpec(QualType(FromFPT, 0),
                                                   EST_None)
                 .getTypePtr());
      Changed = true;
    }

    // Convert FromFPT's ExtParameterInfo if necessary. The conversion is valid
    // only if the ExtParameterInfo lists of the two function prototypes can be
    // merged and the merged list is identical to ToFPT's ExtParameterInfo list.
    SmallVector<FunctionProtoType::ExtParameterInfo, 4> NewParamInfos;
    bool CanUseToFPT, CanUseFromFPT;
    if (Context.mergeExtParameterInfo(ToFPT, FromFPT, CanUseToFPT,
                                      CanUseFromFPT, NewParamInfos) &&
        CanUseToFPT && !CanUseFromFPT) {
      FunctionProtoType::ExtProtoInfo ExtInfo = FromFPT->getExtProtoInfo();
      ExtInfo.ExtParameterInfos =
          NewParamInfos.empty() ? nullptr : NewParamInfos.data();
      QualType QT = Context.getFunctionType(FromFPT->getReturnType(),
                                            FromFPT->getParamTypes(), ExtInfo);
      FromFn = QT->getAs<FunctionType>();
      Changed = true;
    }

    // For C, when called from checkPointerTypesForAssignment,
    // we need to not alter FromFn, or else even an innocuous cast
    // like dropping effects will fail. In C++ however we do want to
    // alter FromFn (because of the way PerformImplicitConversion works).
    if (Context.hasAnyFunctionEffects() && getLangOpts().CPlusPlus) {
      FromFPT = cast<FunctionProtoType>(FromFn); // in case FromFn changed above

      // Transparently add/drop effects; here we are concerned with
      // language rules/canonicalization. Adding/dropping effects is a warning.
      const auto FromFX = FromFPT->getFunctionEffects();
      const auto ToFX = ToFPT->getFunctionEffects();
      if (FromFX != ToFX) {
        FunctionProtoType::ExtProtoInfo ExtInfo = FromFPT->getExtProtoInfo();
        ExtInfo.FunctionEffects = ToFX;
        QualType QT = Context.getFunctionType(
            FromFPT->getReturnType(), FromFPT->getParamTypes(), ExtInfo);
        FromFn = QT->getAs<FunctionType>();
        Changed = true;
      }
    }
  }

  if (!Changed)
    return false;

  assert(QualType(FromFn, 0).isCanonical());
  if (QualType(FromFn, 0) != CanTo) return false;

  ResultTy = ToType;
  return true;
}

/// Determine whether the conversion from FromType to ToType is a valid
/// floating point conversion.
///
static bool IsFloatingPointConversion(Sema &S, QualType FromType,
                                      QualType ToType) {
  if (!FromType->isRealFloatingType() || !ToType->isRealFloatingType())
    return false;
  // FIXME: disable conversions between long double, __ibm128 and __float128
  // if their representation is different until there is back end support
  // We of course allow this conversion if long double is really double.

  // Conversions between bfloat16 and float16 are currently not supported.
  if ((FromType->isBFloat16Type() &&
       (ToType->isFloat16Type() || ToType->isHalfType())) ||
      (ToType->isBFloat16Type() &&
       (FromType->isFloat16Type() || FromType->isHalfType())))
    return false;

  // Conversions between IEEE-quad and IBM-extended semantics are not
  // permitted.
  const llvm::fltSemantics &FromSem = S.Context.getFloatTypeSemantics(FromType);
  const llvm::fltSemantics &ToSem = S.Context.getFloatTypeSemantics(ToType);
  if ((&FromSem == &llvm::APFloat::PPCDoubleDouble() &&
       &ToSem == &llvm::APFloat::IEEEquad()) ||
      (&FromSem == &llvm::APFloat::IEEEquad() &&
       &ToSem == &llvm::APFloat::PPCDoubleDouble()))
    return false;
  return true;
}

static bool IsVectorElementConversion(Sema &S, QualType FromType,
                                      QualType ToType,
                                      ImplicitConversionKind &ICK, Expr *From) {
  if (S.Context.hasSameUnqualifiedType(FromType, ToType))
    return true;

  if (S.IsFloatingPointPromotion(FromType, ToType)) {
    ICK = ICK_Floating_Promotion;
    return true;
  }

  if (IsFloatingPointConversion(S, FromType, ToType)) {
    ICK = ICK_Floating_Conversion;
    return true;
  }

  if (ToType->isBooleanType() && FromType->isArithmeticType()) {
    ICK = ICK_Boolean_Conversion;
    return true;
  }

  if ((FromType->isRealFloatingType() && ToType->isIntegralType(S.Context)) ||
      (FromType->isIntegralOrUnscopedEnumerationType() &&
       ToType->isRealFloatingType())) {
    ICK = ICK_Floating_Integral;
    return true;
  }

  if (S.IsIntegralPromotion(From, FromType, ToType)) {
    ICK = ICK_Integral_Promotion;
    return true;
  }

  if (FromType->isIntegralOrUnscopedEnumerationType() &&
      ToType->isIntegralType(S.Context)) {
    ICK = ICK_Integral_Conversion;
    return true;
  }

  return false;
}

/// Determine whether the conversion from FromType to ToType is a valid
/// vector conversion.
///
/// \param ICK Will be set to the vector conversion kind, if this is a vector
/// conversion.
static bool IsVectorConversion(Sema &S, QualType FromType, QualType ToType,
                               ImplicitConversionKind &ICK,
                               ImplicitConversionKind &ElConv, Expr *From,
                               bool InOverloadResolution, bool CStyle) {
  // We need at least one of these types to be a vector type to have a vector
  // conversion.
  if (!ToType->isVectorType() && !FromType->isVectorType())
    return false;

  // Identical types require no conversions.
  if (S.Context.hasSameUnqualifiedType(FromType, ToType))
    return false;

  // There are no conversions between extended vector types, only identity.
  if (auto *ToExtType = ToType->getAs<ExtVectorType>()) {
    if (auto *FromExtType = FromType->getAs<ExtVectorType>()) {
      // HLSL allows implicit truncation of vector types.
      if (S.getLangOpts().HLSL) {
        unsigned FromElts = FromExtType->getNumElements();
        unsigned ToElts = ToExtType->getNumElements();
        if (FromElts < ToElts)
          return false;
        if (FromElts == ToElts)
          ElConv = ICK_Identity;
        else
          ElConv = ICK_HLSL_Vector_Truncation;

        QualType FromElTy = FromExtType->getElementType();
        QualType ToElTy = ToExtType->getElementType();
        if (S.Context.hasSameUnqualifiedType(FromElTy, ToElTy))
          return true;
        return IsVectorElementConversion(S, FromElTy, ToElTy, ICK, From);
      }
      // There are no conversions between extended vector types other than the
      // identity conversion.
      return false;
    }

    // Vector splat from any arithmetic type to a vector.
    if (FromType->isArithmeticType()) {
      if (S.getLangOpts().HLSL) {
        ElConv = ICK_HLSL_Vector_Splat;
        QualType ToElTy = ToExtType->getElementType();
        return IsVectorElementConversion(S, FromType, ToElTy, ICK, From);
      }
      ICK = ICK_Vector_Splat;
      return true;
    }
  }

  if (ToType->isSVESizelessBuiltinType() ||
      FromType->isSVESizelessBuiltinType())
    if (S.Context.areCompatibleSveTypes(FromType, ToType) ||
        S.Context.areLaxCompatibleSveTypes(FromType, ToType)) {
      ICK = ICK_SVE_Vector_Conversion;
      return true;
    }

  if (ToType->isRVVSizelessBuiltinType() ||
      FromType->isRVVSizelessBuiltinType())
    if (S.Context.areCompatibleRVVTypes(FromType, ToType) ||
        S.Context.areLaxCompatibleRVVTypes(FromType, ToType)) {
      ICK = ICK_RVV_Vector_Conversion;
      return true;
    }

  // We can perform the conversion between vector types in the following cases:
  // 1)vector types are equivalent AltiVec and GCC vector types
  // 2)lax vector conversions are permitted and the vector types are of the
  //   same size
  // 3)the destination type does not have the ARM MVE strict-polymorphism
  //   attribute, which inhibits lax vector conversion for overload resolution
  //   only
  if (ToType->isVectorType() && FromType->isVectorType()) {
    if (S.Context.areCompatibleVectorTypes(FromType, ToType) ||
        (S.isLaxVectorConversion(FromType, ToType) &&
         !ToType->hasAttr(attr::ArmMveStrictPolymorphism))) {
      if (S.getASTContext().getTargetInfo().getTriple().isPPC() &&
          S.isLaxVectorConversion(FromType, ToType) &&
          S.anyAltivecTypes(FromType, ToType) &&
          !S.Context.areCompatibleVectorTypes(FromType, ToType) &&
          !InOverloadResolution && !CStyle) {
        S.Diag(From->getBeginLoc(), diag::warn_deprecated_lax_vec_conv_all)
            << FromType << ToType;
      }
      ICK = ICK_Vector_Conversion;
      return true;
    }
  }

  return false;
}

static bool tryAtomicConversion(Sema &S, Expr *From, QualType ToType,
                                bool InOverloadResolution,
                                StandardConversionSequence &SCS,
                                bool CStyle);

/// IsStandardConversion - Determines whether there is a standard
/// conversion sequence (C++ [conv], C++ [over.ics.scs]) from the
/// expression From to the type ToType. Standard conversion sequences
/// only consider non-class types; for conversions that involve class
/// types, use TryImplicitConversion. If a conversion exists, SCS will
/// contain the standard conversion sequence required to perform this
/// conversion and this routine will return true. Otherwise, this
/// routine will return false and the value of SCS is unspecified.
static bool IsStandardConversion(Sema &S, Expr* From, QualType ToType,
                                 bool InOverloadResolution,
                                 StandardConversionSequence &SCS,
                                 bool CStyle,
                                 bool AllowObjCWritebackConversion) {
  QualType FromType = From->getType();

  // Standard conversions (C++ [conv])
  SCS.setAsIdentityConversion();
  SCS.IncompatibleObjC = false;
  SCS.setFromType(FromType);
  SCS.CopyConstructor = nullptr;

  // There are no standard conversions for class types in C++, so
  // abort early. When overloading in C, however, we do permit them.
  if (S.getLangOpts().CPlusPlus &&
      (FromType->isRecordType() || ToType->isRecordType()))
    return false;

  // The first conversion can be an lvalue-to-rvalue conversion,
  // array-to-pointer conversion, or function-to-pointer conversion
  // (C++ 4p1).

  if (FromType == S.Context.OverloadTy) {
    DeclAccessPair AccessPair;
    if (FunctionDecl *Fn
          = S.ResolveAddressOfOverloadedFunction(From, ToType, false,
                                                 AccessPair)) {
      // We were able to resolve the address of the overloaded function,
      // so we can convert to the type of that function.
      FromType = Fn->getType();
      SCS.setFromType(FromType);

      // we can sometimes resolve &foo<int> regardless of ToType, so check
      // if the type matches (identity) or we are converting to bool
      if (!S.Context.hasSameUnqualifiedType(
                      S.ExtractUnqualifiedFunctionType(ToType), FromType)) {
        QualType resultTy;
        // if the function type matches except for [[noreturn]], it's ok
        if (!S.IsFunctionConversion(FromType,
              S.ExtractUnqualifiedFunctionType(ToType), resultTy))
          // otherwise, only a boolean conversion is standard
          if (!ToType->isBooleanType())
            return false;
      }

      // Check if the "from" expression is taking the address of an overloaded
      // function and recompute the FromType accordingly. Take advantage of the
      // fact that non-static member functions *must* have such an address-of
      // expression.
      CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Fn);
      if (Method && !Method->isStatic() &&
          !Method->isExplicitObjectMemberFunction()) {
        assert(isa<UnaryOperator>(From->IgnoreParens()) &&
               "Non-unary operator on non-static member address");
        assert(cast<UnaryOperator>(From->IgnoreParens())->getOpcode()
               == UO_AddrOf &&
               "Non-address-of operator on non-static member address");
        const Type *ClassType
          = S.Context.getTypeDeclType(Method->getParent()).getTypePtr();
        FromType = S.Context.getMemberPointerType(FromType, ClassType);
      } else if (isa<UnaryOperator>(From->IgnoreParens())) {
        assert(cast<UnaryOperator>(From->IgnoreParens())->getOpcode() ==
               UO_AddrOf &&
               "Non-address-of operator for overloaded function expression");
        FromType = S.Context.getPointerType(FromType);
      }
    } else {
      return false;
    }
  }
  // Lvalue-to-rvalue conversion (C++11 4.1):
  //   A glvalue (3.10) of a non-function, non-array type T can
  //   be converted to a prvalue.
  bool argIsLValue = From->isGLValue();
  if (argIsLValue && !FromType->canDecayToPointerType() &&
      S.Context.getCanonicalType(FromType) != S.Context.OverloadTy) {
    SCS.First = ICK_Lvalue_To_Rvalue;

    // C11 6.3.2.1p2:
    //   ... if the lvalue has atomic type, the value has the non-atomic version
    //   of the type of the lvalue ...
    if (const AtomicType *Atomic = FromType->getAs<AtomicType>())
      FromType = Atomic->getValueType();

    // If T is a non-class type, the type of the rvalue is the
    // cv-unqualified version of T. Otherwise, the type of the rvalue
    // is T (C++ 4.1p1). C++ can't get here with class types; in C, we
    // just strip the qualifiers because they don't matter.
    FromType = FromType.getUnqualifiedType();
  } else if (S.getLangOpts().HLSL && FromType->isConstantArrayType() &&
             ToType->isArrayParameterType()) {
    // HLSL constant array parameters do not decay, so if the argument is a
    // constant array and the parameter is an ArrayParameterType we have special
    // handling here.
    FromType = S.Context.getArrayParameterType(FromType);
    if (S.Context.getCanonicalType(FromType) !=
        S.Context.getCanonicalType(ToType))
      return false;

    SCS.First = ICK_HLSL_Array_RValue;
    SCS.setAllToTypes(ToType);
    return true;
  } else if (FromType->isArrayType()) {
    // Array-to-pointer conversion (C++ 4.2)
    SCS.First = ICK_Array_To_Pointer;

    // An lvalue or rvalue of type "array of N T" or "array of unknown
    // bound of T" can be converted to an rvalue of type "pointer to
    // T" (C++ 4.2p1).
    FromType = S.Context.getArrayDecayedType(FromType);

    if (S.IsStringLiteralToNonConstPointerConversion(From, ToType)) {
      // This conversion is deprecated in C++03 (D.4)
      SCS.DeprecatedStringLiteralToCharPtr = true;

      // For the purpose of ranking in overload resolution
      // (13.3.3.1.1), this conversion is considered an
      // array-to-pointer conversion followed by a qualification
      // conversion (4.4). (C++ 4.2p2)
      SCS.Second = ICK_Identity;
      SCS.Third = ICK_Qualification;
      SCS.QualificationIncludesObjCLifetime = false;
      SCS.setAllToTypes(FromType);
      return true;
    }
  } else if (FromType->isFunctionType() && argIsLValue) {
    // Function-to-pointer conversion (C++ 4.3).
    SCS.First = ICK_Function_To_Pointer;

    if (auto *DRE = dyn_cast<DeclRefExpr>(From->IgnoreParenCasts()))
      if (auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl()))
        if (!S.checkAddressOfFunctionIsAvailable(FD))
          return false;

    // An lvalue of function type T can be converted to an rvalue of
    // type "pointer to T." The result is a pointer to the
    // function. (C++ 4.3p1).
    FromType = S.Context.getPointerType(FromType);
  } else {
    // We don't require any conversions for the first step.
    SCS.First = ICK_Identity;
  }
  SCS.setToType(0, FromType);

  // The second conversion can be an integral promotion, floating
  // point promotion, integral conversion, floating point conversion,
  // floating-integral conversion, pointer conversion,
  // pointer-to-member conversion, or boolean conversion (C++ 4p1).
  // For overloading in C, this can also be a "compatible-type"
  // conversion.
  bool IncompatibleObjC = false;
  ImplicitConversionKind SecondICK = ICK_Identity;
  ImplicitConversionKind DimensionICK = ICK_Identity;
  if (S.Context.hasSameUnqualifiedType(FromType, ToType)) {
    // The unqualified versions of the types are the same: there's no
    // conversion to do.
    SCS.Second = ICK_Identity;
  } else if (S.IsIntegralPromotion(From, FromType, ToType)) {
    // Integral promotion (C++ 4.5).
    SCS.Second = ICK_Integral_Promotion;
    FromType = ToType.getUnqualifiedType();
  } else if (S.IsFloatingPointPromotion(FromType, ToType)) {
    // Floating point promotion (C++ 4.6).
    SCS.Second = ICK_Floating_Promotion;
    FromType = ToType.getUnqualifiedType();
  } else if (S.IsComplexPromotion(FromType, ToType)) {
    // Complex promotion (Clang extension)
    SCS.Second = ICK_Complex_Promotion;
    FromType = ToType.getUnqualifiedType();
  } else if (ToType->isBooleanType() &&
             (FromType->isArithmeticType() ||
              FromType->isAnyPointerType() ||
              FromType->isBlockPointerType() ||
              FromType->isMemberPointerType())) {
    // Boolean conversions (C++ 4.12).
    SCS.Second = ICK_Boolean_Conversion;
    FromType = S.Context.BoolTy;
  } else if (FromType->isIntegralOrUnscopedEnumerationType() &&
             ToType->isIntegralType(S.Context)) {
    // Integral conversions (C++ 4.7).
    SCS.Second = ICK_Integral_Conversion;
    FromType = ToType.getUnqualifiedType();
  } else if (FromType->isAnyComplexType() && ToType->isAnyComplexType()) {
    // Complex conversions (C99 6.3.1.6)
    SCS.Second = ICK_Complex_Conversion;
    FromType = ToType.getUnqualifiedType();
  } else if ((FromType->isAnyComplexType() && ToType->isArithmeticType()) ||
             (ToType->isAnyComplexType() && FromType->isArithmeticType())) {
    // Complex-real conversions (C99 6.3.1.7)
    SCS.Second = ICK_Complex_Real;
    FromType = ToType.getUnqualifiedType();
  } else if (IsFloatingPointConversion(S, FromType, ToType)) {
    // Floating point conversions (C++ 4.8).
    SCS.Second = ICK_Floating_Conversion;
    FromType = ToType.getUnqualifiedType();
  } else if ((FromType->isRealFloatingType() &&
              ToType->isIntegralType(S.Context)) ||
             (FromType->isIntegralOrUnscopedEnumerationType() &&
              ToType->isRealFloatingType())) {

    // Floating-integral conversions (C++ 4.9).
    SCS.Second = ICK_Floating_Integral;
    FromType = ToType.getUnqualifiedType();
  } else if (S.IsBlockPointerConversion(FromType, ToType, FromType)) {
    SCS.Second = ICK_Block_Pointer_Conversion;
  } else if (AllowObjCWritebackConversion &&
             S.ObjC().isObjCWritebackConversion(FromType, ToType, FromType)) {
    SCS.Second = ICK_Writeback_Conversion;
  } else if (S.IsPointerConversion(From, FromType, ToType, InOverloadResolution,
                                   FromType, IncompatibleObjC)) {
    // Pointer conversions (C++ 4.10).
    SCS.Second = ICK_Pointer_Conversion;
    SCS.IncompatibleObjC = IncompatibleObjC;
    FromType = FromType.getUnqualifiedType();
  } else if (S.IsMemberPointerConversion(From, FromType, ToType,
                                         InOverloadResolution, FromType)) {
    // Pointer to member conversions (4.11).
    SCS.Second = ICK_Pointer_Member;
  } else if (IsVectorConversion(S, FromType, ToType, SecondICK, DimensionICK,
                                From, InOverloadResolution, CStyle)) {
    SCS.Second = SecondICK;
    SCS.Dimension = DimensionICK;
    FromType = ToType.getUnqualifiedType();
  } else if (!S.getLangOpts().CPlusPlus &&
             S.Context.typesAreCompatible(ToType, FromType)) {
    // Compatible conversions (Clang extension for C function overloading)
    SCS.Second = ICK_Compatible_Conversion;
    FromType = ToType.getUnqualifiedType();
  } else if (IsTransparentUnionStandardConversion(
                 S, From, ToType, InOverloadResolution, SCS, CStyle)) {
    SCS.Second = ICK_TransparentUnionConversion;
    FromType = ToType;
  } else if (tryAtomicConversion(S, From, ToType, InOverloadResolution, SCS,
                                 CStyle)) {
    // tryAtomicConversion has updated the standard conversion sequence
    // appropriately.
    return true;
  } else if (ToType->isEventT() &&
             From->isIntegerConstantExpr(S.getASTContext()) &&
             From->EvaluateKnownConstInt(S.getASTContext()) == 0) {
    SCS.Second = ICK_Zero_Event_Conversion;
    FromType = ToType;
  } else if (ToType->isQueueT() &&
             From->isIntegerConstantExpr(S.getASTContext()) &&
             (From->EvaluateKnownConstInt(S.getASTContext()) == 0)) {
    SCS.Second = ICK_Zero_Queue_Conversion;
    FromType = ToType;
  } else if (ToType->isSamplerT() &&
             From->isIntegerConstantExpr(S.getASTContext())) {
    SCS.Second = ICK_Compatible_Conversion;
    FromType = ToType;
  } else if ((ToType->isFixedPointType() &&
              FromType->isConvertibleToFixedPointType()) ||
             (FromType->isFixedPointType() &&
              ToType->isConvertibleToFixedPointType())) {
    SCS.Second = ICK_Fixed_Point_Conversion;
    FromType = ToType;
  } else {
    // No second conversion required.
    SCS.Second = ICK_Identity;
  }
  SCS.setToType(1, FromType);

  // The third conversion can be a function pointer conversion or a
  // qualification conversion (C++ [conv.fctptr], [conv.qual]).
  bool ObjCLifetimeConversion;
  if (S.IsFunctionConversion(FromType, ToType, FromType)) {
    // Function pointer conversions (removing 'noexcept') including removal of
    // 'noreturn' (Clang extension).
    SCS.Third = ICK_Function_Conversion;
  } else if (S.IsQualificationConversion(FromType, ToType, CStyle,
                                         ObjCLifetimeConversion)) {
    SCS.Third = ICK_Qualification;
    SCS.QualificationIncludesObjCLifetime = ObjCLifetimeConversion;
    FromType = ToType;
  } else {
    // No conversion required
    SCS.Third = ICK_Identity;
  }

  // C++ [over.best.ics]p6:
  //   [...] Any difference in top-level cv-qualification is
  //   subsumed by the initialization itself and does not constitute
  //   a conversion. [...]
  QualType CanonFrom = S.Context.getCanonicalType(FromType);
  QualType CanonTo = S.Context.getCanonicalType(ToType);
  if (CanonFrom.getLocalUnqualifiedType()
                                     == CanonTo.getLocalUnqualifiedType() &&
      CanonFrom.getLocalQualifiers() != CanonTo.getLocalQualifiers()) {
    FromType = ToType;
    CanonFrom = CanonTo;
  }

  SCS.setToType(2, FromType);

  if (CanonFrom == CanonTo)
    return true;

  // If we have not converted the argument type to the parameter type,
  // this is a bad conversion sequence, unless we're resolving an overload in C.
  if (S.getLangOpts().CPlusPlus || !InOverloadResolution)
    return false;

  ExprResult ER = ExprResult{From};
  Sema::AssignConvertType Conv =
      S.CheckSingleAssignmentConstraints(ToType, ER,
                                         /*Diagnose=*/false,
                                         /*DiagnoseCFAudited=*/false,
                                         /*ConvertRHS=*/false);
  ImplicitConversionKind SecondConv;
  switch (Conv) {
  case Sema::Compatible:
    SecondConv = ICK_C_Only_Conversion;
    break;
  // For our purposes, discarding qualifiers is just as bad as using an
  // incompatible pointer. Note that an IncompatiblePointer conversion can drop
  // qualifiers, as well.
  case Sema::CompatiblePointerDiscardsQualifiers:
  case Sema::IncompatiblePointer:
  case Sema::IncompatiblePointerSign:
    SecondConv = ICK_Incompatible_Pointer_Conversion;
    break;
  default:
    return false;
  }

  // First can only be an lvalue conversion, so we pretend that this was the
  // second conversion. First should already be valid from earlier in the
  // function.
  SCS.Second = SecondConv;
  SCS.setToType(1, ToType);

  // Third is Identity, because Second should rank us worse than any other
  // conversion. This could also be ICK_Qualification, but it's simpler to just
  // lump everything in with the second conversion, and we don't gain anything
  // from making this ICK_Qualification.
  SCS.Third = ICK_Identity;
  SCS.setToType(2, ToType);
  return true;
}

static bool
IsTransparentUnionStandardConversion(Sema &S, Expr* From,
                                     QualType &ToType,
                                     bool InOverloadResolution,
                                     StandardConversionSequence &SCS,
                                     bool CStyle) {

  const RecordType *UT = ToType->getAsUnionType();
  if (!UT || !UT->getDecl()->hasAttr<TransparentUnionAttr>())
    return false;
  // The field to initialize within the transparent union.
  RecordDecl *UD = UT->getDecl();
  // It's compatible if the expression matches any of the fields.
  for (const auto *it : UD->fields()) {
    if (IsStandardConversion(S, From, it->getType(), InOverloadResolution, SCS,
                             CStyle, /*AllowObjCWritebackConversion=*/false)) {
      ToType = it->getType();
      return true;
    }
  }
  return false;
}

bool Sema::IsIntegralPromotion(Expr *From, QualType FromType, QualType ToType) {
  const BuiltinType *To = ToType->getAs<BuiltinType>();
  // All integers are built-in.
  if (!To) {
    return false;
  }

  // An rvalue of type char, signed char, unsigned char, short int, or
  // unsigned short int can be converted to an rvalue of type int if
  // int can represent all the values of the source type; otherwise,
  // the source rvalue can be converted to an rvalue of type unsigned
  // int (C++ 4.5p1).
  if (Context.isPromotableIntegerType(FromType) && !FromType->isBooleanType() &&
      !FromType->isEnumeralType()) {
    if ( // We can promote any signed, promotable integer type to an int
        (FromType->isSignedIntegerType() ||
         // We can promote any unsigned integer type whose size is
         // less than int to an int.
         Context.getTypeSize(FromType) < Context.getTypeSize(ToType))) {
      return To->getKind() == BuiltinType::Int;
    }

    return To->getKind() == BuiltinType::UInt;
  }

  // C++11 [conv.prom]p3:
  //   A prvalue of an unscoped enumeration type whose underlying type is not
  //   fixed (7.2) can be converted to an rvalue a prvalue of the first of the
  //   following types that can represent all the values of the enumeration
  //   (i.e., the values in the range bmin to bmax as described in 7.2): int,
  //   unsigned int, long int, unsigned long int, long long int, or unsigned
  //   long long int. If none of the types in that list can represent all the
  //   values of the enumeration, an rvalue a prvalue of an unscoped enumeration
  //   type can be converted to an rvalue a prvalue of the extended integer type
  //   with lowest integer conversion rank (4.13) greater than the rank of long
  //   long in which all the values of the enumeration can be represented. If
  //   there are two such extended types, the signed one is chosen.
  // C++11 [conv.prom]p4:
  //   A prvalue of an unscoped enumeration type whose underlying type is fixed
  //   can be converted to a prvalue of its underlying type. Moreover, if
  //   integral promotion can be applied to its underlying type, a prvalue of an
  //   unscoped enumeration type whose underlying type is fixed can also be
  //   converted to a prvalue of the promoted underlying type.
  if (const EnumType *FromEnumType = FromType->getAs<EnumType>()) {
    // C++0x 7.2p9: Note that this implicit enum to int conversion is not
    // provided for a scoped enumeration.
    if (FromEnumType->getDecl()->isScoped())
      return false;

    // We can perform an integral promotion to the underlying type of the enum,
    // even if that's not the promoted type. Note that the check for promoting
    // the underlying type is based on the type alone, and does not consider
    // the bitfield-ness of the actual source expression.
    if (FromEnumType->getDecl()->isFixed()) {
      QualType Underlying = FromEnumType->getDecl()->getIntegerType();
      return Context.hasSameUnqualifiedType(Underlying, ToType) ||
             IsIntegralPromotion(nullptr, Underlying, ToType);
    }

    // We have already pre-calculated the promotion type, so this is trivial.
    if (ToType->isIntegerType() &&
        isCompleteType(From->getBeginLoc(), FromType))
      return Context.hasSameUnqualifiedType(
          ToType, FromEnumType->getDecl()->getPromotionType());

    // C++ [conv.prom]p5:
    //   If the bit-field has an enumerated type, it is treated as any other
    //   value of that type for promotion purposes.
    //
    // ... so do not fall through into the bit-field checks below in C++.
    if (getLangOpts().CPlusPlus)
      return false;
  }

  // C++0x [conv.prom]p2:
  //   A prvalue of type char16_t, char32_t, or wchar_t (3.9.1) can be converted
  //   to an rvalue a prvalue of the first of the following types that can
  //   represent all the values of its underlying type: int, unsigned int,
  //   long int, unsigned long int, long long int, or unsigned long long int.
  //   If none of the types in that list can represent all the values of its
  //   underlying type, an rvalue a prvalue of type char16_t, char32_t,
  //   or wchar_t can be converted to an rvalue a prvalue of its underlying
  //   type.
  if (FromType->isAnyCharacterType() && !FromType->isCharType() &&
      ToType->isIntegerType()) {
    // Determine whether the type we're converting from is signed or
    // unsigned.
    bool FromIsSigned = FromType->isSignedIntegerType();
    uint64_t FromSize = Context.getTypeSize(FromType);

    // The types we'll try to promote to, in the appropriate
    // order. Try each of these types.
    QualType PromoteTypes[6] = {
      Context.IntTy, Context.UnsignedIntTy,
      Context.LongTy, Context.UnsignedLongTy ,
      Context.LongLongTy, Context.UnsignedLongLongTy
    };
    for (int Idx = 0; Idx < 6; ++Idx) {
      uint64_t ToSize = Context.getTypeSize(PromoteTypes[Idx]);
      if (FromSize < ToSize ||
          (FromSize == ToSize &&
           FromIsSigned == PromoteTypes[Idx]->isSignedIntegerType())) {
        // We found the type that we can promote to. If this is the
        // type we wanted, we have a promotion. Otherwise, no
        // promotion.
        return Context.hasSameUnqualifiedType(ToType, PromoteTypes[Idx]);
      }
    }
  }

  // An rvalue for an integral bit-field (9.6) can be converted to an
  // rvalue of type int if int can represent all the values of the
  // bit-field; otherwise, it can be converted to unsigned int if
  // unsigned int can represent all the values of the bit-field. If
  // the bit-field is larger yet, no integral promotion applies to
  // it. If the bit-field has an enumerated type, it is treated as any
  // other value of that type for promotion purposes (C++ 4.5p3).
  // FIXME: We should delay checking of bit-fields until we actually perform the
  // conversion.
  //
  // FIXME: In C, only bit-fields of types _Bool, int, or unsigned int may be
  // promoted, per C11 6.3.1.1/2. We promote all bit-fields (including enum
  // bit-fields and those whose underlying type is larger than int) for GCC
  // compatibility.
  if (From) {
    if (FieldDecl *MemberDecl = From->getSourceBitField()) {
      std::optional<llvm::APSInt> BitWidth;
      if (FromType->isIntegralType(Context) &&
          (BitWidth =
               MemberDecl->getBitWidth()->getIntegerConstantExpr(Context))) {
        llvm::APSInt ToSize(BitWidth->getBitWidth(), BitWidth->isUnsigned());
        ToSize = Context.getTypeSize(ToType);

        // Are we promoting to an int from a bitfield that fits in an int?
        if (*BitWidth < ToSize ||
            (FromType->isSignedIntegerType() && *BitWidth <= ToSize)) {
          return To->getKind() == BuiltinType::Int;
        }

        // Are we promoting to an unsigned int from an unsigned bitfield
        // that fits into an unsigned int?
        if (FromType->isUnsignedIntegerType() && *BitWidth <= ToSize) {
          return To->getKind() == BuiltinType::UInt;
        }

        return false;
      }
    }
  }

  // An rvalue of type bool can be converted to an rvalue of type int,
  // with false becoming zero and true becoming one (C++ 4.5p4).
  if (FromType->isBooleanType() && To->getKind() == BuiltinType::Int) {
    return true;
  }

  // In HLSL an rvalue of integral type can be promoted to an rvalue of a larger
  // integral type.
  if (Context.getLangOpts().HLSL && FromType->isIntegerType() &&
      ToType->isIntegerType())
    return Context.getTypeSize(FromType) < Context.getTypeSize(ToType);

  return false;
}

bool Sema::IsFloatingPointPromotion(QualType FromType, QualType ToType) {
  if (const BuiltinType *FromBuiltin = FromType->getAs<BuiltinType>())
    if (const BuiltinType *ToBuiltin = ToType->getAs<BuiltinType>()) {
      /// An rvalue of type float can be converted to an rvalue of type
      /// double. (C++ 4.6p1).
      if (FromBuiltin->getKind() == BuiltinType::Float &&
          ToBuiltin->getKind() == BuiltinType::Double)
        return true;

      // C99 6.3.1.5p1:
      //   When a float is promoted to double or long double, or a
      //   double is promoted to long double [...].
      if (!getLangOpts().CPlusPlus &&
          (FromBuiltin->getKind() == BuiltinType::Float ||
           FromBuiltin->getKind() == BuiltinType::Double) &&
          (ToBuiltin->getKind() == BuiltinType::LongDouble ||
           ToBuiltin->getKind() == BuiltinType::Float128 ||
           ToBuiltin->getKind() == BuiltinType::Ibm128))
        return true;

      // In HLSL, `half` promotes to `float` or `double`, regardless of whether
      // or not native half types are enabled.
      if (getLangOpts().HLSL && FromBuiltin->getKind() == BuiltinType::Half &&
          (ToBuiltin->getKind() == BuiltinType::Float ||
           ToBuiltin->getKind() == BuiltinType::Double))
        return true;

      // Half can be promoted to float.
      if (!getLangOpts().NativeHalfType &&
           FromBuiltin->getKind() == BuiltinType::Half &&
          ToBuiltin->getKind() == BuiltinType::Float)
        return true;
    }

  return false;
}

bool Sema::IsComplexPromotion(QualType FromType, QualType ToType) {
  const ComplexType *FromComplex = FromType->getAs<ComplexType>();
  if (!FromComplex)
    return false;

  const ComplexType *ToComplex = ToType->getAs<ComplexType>();
  if (!ToComplex)
    return false;

  return IsFloatingPointPromotion(FromComplex->getElementType(),
                                  ToComplex->getElementType()) ||
    IsIntegralPromotion(nullptr, FromComplex->getElementType(),
                        ToComplex->getElementType());
}

/// BuildSimilarlyQualifiedPointerType - In a pointer conversion from
/// the pointer type FromPtr to a pointer to type ToPointee, with the
/// same type qualifiers as FromPtr has on its pointee type. ToType,
/// if non-empty, will be a pointer to ToType that may or may not have
/// the right set of qualifiers on its pointee.
///
static QualType
BuildSimilarlyQualifiedPointerType(const Type *FromPtr,
                                   QualType ToPointee, QualType ToType,
                                   ASTContext &Context,
                                   bool StripObjCLifetime = false) {
  assert((FromPtr->getTypeClass() == Type::Pointer ||
          FromPtr->getTypeClass() == Type::ObjCObjectPointer) &&
         "Invalid similarly-qualified pointer type");

  /// Conversions to 'id' subsume cv-qualifier conversions.
  if (ToType->isObjCIdType() || ToType->isObjCQualifiedIdType())
    return ToType.getUnqualifiedType();

  QualType CanonFromPointee
    = Context.getCanonicalType(FromPtr->getPointeeType());
  QualType CanonToPointee = Context.getCanonicalType(ToPointee);
  Qualifiers Quals = CanonFromPointee.getQualifiers();

  if (StripObjCLifetime)
    Quals.removeObjCLifetime();

  // Exact qualifier match -> return the pointer type we're converting to.
  if (CanonToPointee.getLocalQualifiers() == Quals) {
    // ToType is exactly what we need. Return it.
    if (!ToType.isNull())
      return ToType.getUnqualifiedType();

    // Build a pointer to ToPointee. It has the right qualifiers
    // already.
    if (isa<ObjCObjectPointerType>(ToType))
      return Context.getObjCObjectPointerType(ToPointee);
    return Context.getPointerType(ToPointee);
  }

  // Just build a canonical type that has the right qualifiers.
  QualType QualifiedCanonToPointee
    = Context.getQualifiedType(CanonToPointee.getLocalUnqualifiedType(), Quals);

  if (isa<ObjCObjectPointerType>(ToType))
    return Context.getObjCObjectPointerType(QualifiedCanonToPointee);
  return Context.getPointerType(QualifiedCanonToPointee);
}

static bool isNullPointerConstantForConversion(Expr *Expr,
                                               bool InOverloadResolution,
                                               ASTContext &Context) {
  // Handle value-dependent integral null pointer constants correctly.
  // http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_active.html#903
  if (Expr->isValueDependent() && !Expr->isTypeDependent() &&
      Expr->getType()->isIntegerType() && !Expr->getType()->isEnumeralType())
    return !InOverloadResolution;

  return Expr->isNullPointerConstant(Context,
                    InOverloadResolution? Expr::NPC_ValueDependentIsNotNull
                                        : Expr::NPC_ValueDependentIsNull);
}

bool Sema::IsPointerConversion(Expr *From, QualType FromType, QualType ToType,
                               bool InOverloadResolution,
                               QualType& ConvertedType,
                               bool &IncompatibleObjC) {
  IncompatibleObjC = false;
  if (isObjCPointerConversion(FromType, ToType, ConvertedType,
                              IncompatibleObjC))
    return true;

  // Conversion from a null pointer constant to any Objective-C pointer type.
  if (ToType->isObjCObjectPointerType() &&
      isNullPointerConstantForConversion(From, InOverloadResolution, Context)) {
    ConvertedType = ToType;
    return true;
  }

  // Blocks: Block pointers can be converted to void*.
  if (FromType->isBlockPointerType() && ToType->isPointerType() &&
      ToType->castAs<PointerType>()->getPointeeType()->isVoidType()) {
    ConvertedType = ToType;
    return true;
  }
  // Blocks: A null pointer constant can be converted to a block
  // pointer type.
  if (ToType->isBlockPointerType() &&
      isNullPointerConstantForConversion(From, InOverloadResolution, Context)) {
    ConvertedType = ToType;
    return true;
  }

  // If the left-hand-side is nullptr_t, the right side can be a null
  // pointer constant.
  if (ToType->isNullPtrType() &&
      isNullPointerConstantForConversion(From, InOverloadResolution, Context)) {
    ConvertedType = ToType;
    return true;
  }

  const PointerType* ToTypePtr = ToType->getAs<PointerType>();
  if (!ToTypePtr)
    return false;

  // A null pointer constant can be converted to a pointer type (C++ 4.10p1).
  if (isNullPointerConstantForConversion(From, InOverloadResolution, Context)) {
    ConvertedType = ToType;
    return true;
  }

  // Beyond this point, both types need to be pointers
  // , including objective-c pointers.
  QualType ToPointeeType = ToTypePtr->getPointeeType();
  if (FromType->isObjCObjectPointerType() && ToPointeeType->isVoidType() &&
      !getLangOpts().ObjCAutoRefCount) {
    ConvertedType = BuildSimilarlyQualifiedPointerType(
        FromType->castAs<ObjCObjectPointerType>(), ToPointeeType, ToType,
        Context);
    return true;
  }
  const PointerType *FromTypePtr = FromType->getAs<PointerType>();
  if (!FromTypePtr)
    return false;

  QualType FromPointeeType = FromTypePtr->getPointeeType();

  // If the unqualified pointee types are the same, this can't be a
  // pointer conversion, so don't do all of the work below.
  if (Context.hasSameUnqualifiedType(FromPointeeType, ToPointeeType))
    return false;

  // An rvalue of type "pointer to cv T," where T is an object type,
  // can be converted to an rvalue of type "pointer to cv void" (C++
  // 4.10p2).
  if (FromPointeeType->isIncompleteOrObjectType() &&
      ToPointeeType->isVoidType()) {
    ConvertedType = BuildSimilarlyQualifiedPointerType(FromTypePtr,
                                                       ToPointeeType,
                                                       ToType, Context,
                                                   /*StripObjCLifetime=*/true);
    return true;
  }

  // MSVC allows implicit function to void* type conversion.
  if (getLangOpts().MSVCCompat && FromPointeeType->isFunctionType() &&
      ToPointeeType->isVoidType()) {
    ConvertedType = BuildSimilarlyQualifiedPointerType(FromTypePtr,
                                                       ToPointeeType,
                                                       ToType, Context);
    return true;
  }

  // When we're overloading in C, we allow a special kind of pointer
  // conversion for compatible-but-not-identical pointee types.
  if (!getLangOpts().CPlusPlus &&
      Context.typesAreCompatible(FromPointeeType, ToPointeeType)) {
    ConvertedType = BuildSimilarlyQualifiedPointerType(FromTypePtr,
                                                       ToPointeeType,
                                                       ToType, Context);
    return true;
  }

  // C++ [conv.ptr]p3:
  //
  //   An rvalue of type "pointer to cv D," where D is a class type,
  //   can be converted to an rvalue of type "pointer to cv B," where
  //   B is a base class (clause 10) of D. If B is an inaccessible
  //   (clause 11) or ambiguous (10.2) base class of D, a program that
  //   necessitates this conversion is ill-formed. The result of the
  //   conversion is a pointer to the base class sub-object of the
  //   derived class object. The null pointer value is converted to
  //   the null pointer value of the destination type.
  //
  // Note that we do not check for ambiguity or inaccessibility
  // here. That is handled by CheckPointerConversion.
  if (getLangOpts().CPlusPlus && FromPointeeType->isRecordType() &&
      ToPointeeType->isRecordType() &&
      !Context.hasSameUnqualifiedType(FromPointeeType, ToPointeeType) &&
      IsDerivedFrom(From->getBeginLoc(), FromPointeeType, ToPointeeType)) {
    ConvertedType = BuildSimilarlyQualifiedPointerType(FromTypePtr,
                                                       ToPointeeType,
                                                       ToType, Context);
    return true;
  }

  if (FromPointeeType->isVectorType() && ToPointeeType->isVectorType() &&
      Context.areCompatibleVectorTypes(FromPointeeType, ToPointeeType)) {
    ConvertedType = BuildSimilarlyQualifiedPointerType(FromTypePtr,
                                                       ToPointeeType,
                                                       ToType, Context);
    return true;
  }

  return false;
}

/// Adopt the given qualifiers for the given type.
static QualType AdoptQualifiers(ASTContext &Context, QualType T, Qualifiers Qs){
  Qualifiers TQs = T.getQualifiers();

  // Check whether qualifiers already match.
  if (TQs == Qs)
    return T;

  if (Qs.compatiblyIncludes(TQs))
    return Context.getQualifiedType(T, Qs);

  return Context.getQualifiedType(T.getUnqualifiedType(), Qs);
}

bool Sema::isObjCPointerConversion(QualType FromType, QualType ToType,
                                   QualType& ConvertedType,
                                   bool &IncompatibleObjC) {
  if (!getLangOpts().ObjC)
    return false;

  // The set of qualifiers on the type we're converting from.
  Qualifiers FromQualifiers = FromType.getQualifiers();

  // First, we handle all conversions on ObjC object pointer types.
  const ObjCObjectPointerType* ToObjCPtr =
    ToType->getAs<ObjCObjectPointerType>();
  const ObjCObjectPointerType *FromObjCPtr =
    FromType->getAs<ObjCObjectPointerType>();

  if (ToObjCPtr && FromObjCPtr) {
    // If the pointee types are the same (ignoring qualifications),
    // then this is not a pointer conversion.
    if (Context.hasSameUnqualifiedType(ToObjCPtr->getPointeeType(),
                                       FromObjCPtr->getPointeeType()))
      return false;

    // Conversion between Objective-C pointers.
    if (Context.canAssignObjCInterfaces(ToObjCPtr, FromObjCPtr)) {
      const ObjCInterfaceType* LHS = ToObjCPtr->getInterfaceType();
      const ObjCInterfaceType* RHS = FromObjCPtr->getInterfaceType();
      if (getLangOpts().CPlusPlus && LHS && RHS &&
          !ToObjCPtr->getPointeeType().isAtLeastAsQualifiedAs(
                                                FromObjCPtr->getPointeeType()))
        return false;
      ConvertedType = BuildSimilarlyQualifiedPointerType(FromObjCPtr,
                                                   ToObjCPtr->getPointeeType(),
                                                         ToType, Context);
      ConvertedType = AdoptQualifiers(Context, ConvertedType, FromQualifiers);
      return true;
    }

    if (Context.canAssignObjCInterfaces(FromObjCPtr, ToObjCPtr)) {
      // Okay: this is some kind of implicit downcast of Objective-C
      // interfaces, which is permitted. However, we're going to
      // complain about it.
      IncompatibleObjC = true;
      ConvertedType = BuildSimilarlyQualifiedPointerType(FromObjCPtr,
                                                   ToObjCPtr->getPointeeType(),
                                                         ToType, Context);
      ConvertedType = AdoptQualifiers(Context, ConvertedType, FromQualifiers);
      return true;
    }
  }
  // Beyond this point, both types need to be C pointers or block pointers.
  QualType ToPointeeType;
  if (const PointerType *ToCPtr = ToType->getAs<PointerType>())
    ToPointeeType = ToCPtr->getPointeeType();
  else if (const BlockPointerType *ToBlockPtr =
            ToType->getAs<BlockPointerType>()) {
    // Objective C++: We're able to convert from a pointer to any object
    // to a block pointer type.
    if (FromObjCPtr && FromObjCPtr->isObjCBuiltinType()) {
      ConvertedType = AdoptQualifiers(Context, ToType, FromQualifiers);
      return true;
    }
    ToPointeeType = ToBlockPtr->getPointeeType();
  }
  else if (FromType->getAs<BlockPointerType>() &&
           ToObjCPtr && ToObjCPtr->isObjCBuiltinType()) {
    // Objective C++: We're able to convert from a block pointer type to a
    // pointer to any object.
    ConvertedType = AdoptQualifiers(Context, ToType, FromQualifiers);
    return true;
  }
  else
    return false;

  QualType FromPointeeType;
  if (const PointerType *FromCPtr = FromType->getAs<PointerType>())
    FromPointeeType = FromCPtr->getPointeeType();
  else if (const BlockPointerType *FromBlockPtr =
           FromType->getAs<BlockPointerType>())
    FromPointeeType = FromBlockPtr->getPointeeType();
  else
    return false;

  // If we have pointers to pointers, recursively check whether this
  // is an Objective-C conversion.
  if (FromPointeeType->isPointerType() && ToPointeeType->isPointerType() &&
      isObjCPointerConversion(FromPointeeType, ToPointeeType, ConvertedType,
                              IncompatibleObjC)) {
    // We always complain about this conversion.
    IncompatibleObjC = true;
    ConvertedType = Context.getPointerType(ConvertedType);
    ConvertedType = AdoptQualifiers(Context, ConvertedType, FromQualifiers);
    return true;
  }
  // Allow conversion of pointee being objective-c pointer to another one;
  // as in I* to id.
  if (FromPointeeType->getAs<ObjCObjectPointerType>() &&
      ToPointeeType->getAs<ObjCObjectPointerType>() &&
      isObjCPointerConversion(FromPointeeType, ToPointeeType, ConvertedType,
                              IncompatibleObjC)) {

    ConvertedType = Context.getPointerType(ConvertedType);
    ConvertedType = AdoptQualifiers(Context, ConvertedType, FromQualifiers);
    return true;
  }

  // If we have pointers to functions or blocks, check whether the only
  // differences in the argument and result types are in Objective-C
  // pointer conversions. If so, we permit the conversion (but
  // complain about it).
  const FunctionProtoType *FromFunctionType
    = FromPointeeType->getAs<FunctionProtoType>();
  const FunctionProtoType *ToFunctionType
    = ToPointeeType->getAs<FunctionProtoType>();
  if (FromFunctionType && ToFunctionType) {
    // If the function types are exactly the same, this isn't an
    // Objective-C pointer conversion.
    if (Context.getCanonicalType(FromPointeeType)
          == Context.getCanonicalType(ToPointeeType))
      return false;

    // Perform the quick checks that will tell us whether these
    // function types are obviously different.
    if (FromFunctionType->getNumParams() != ToFunctionType->getNumParams() ||
        FromFunctionType->isVariadic() != ToFunctionType->isVariadic() ||
        FromFunctionType->getMethodQuals() != ToFunctionType->getMethodQuals())
      return false;

    bool HasObjCConversion = false;
    if (Context.getCanonicalType(FromFunctionType->getReturnType()) ==
        Context.getCanonicalType(ToFunctionType->getReturnType())) {
      // Okay, the types match exactly. Nothing to do.
    } else if (isObjCPointerConversion(FromFunctionType->getReturnType(),
                                       ToFunctionType->getReturnType(),
                                       ConvertedType, IncompatibleObjC)) {
      // Okay, we have an Objective-C pointer conversion.
      HasObjCConversion = true;
    } else {
      // Function types are too different. Abort.
      return false;
    }

    // Check argument types.
    for (unsigned ArgIdx = 0, NumArgs = FromFunctionType->getNumParams();
         ArgIdx != NumArgs; ++ArgIdx) {
      QualType FromArgType = FromFunctionType->getParamType(ArgIdx);
      QualType ToArgType = ToFunctionType->getParamType(ArgIdx);
      if (Context.getCanonicalType(FromArgType)
            == Context.getCanonicalType(ToArgType)) {
        // Okay, the types match exactly. Nothing to do.
      } else if (isObjCPointerConversion(FromArgType, ToArgType,
                                         ConvertedType, IncompatibleObjC)) {
        // Okay, we have an Objective-C pointer conversion.
        HasObjCConversion = true;
      } else {
        // Argument types are too different. Abort.
        return false;
      }
    }

    if (HasObjCConversion) {
      // We had an Objective-C conversion. Allow this pointer
      // conversion, but complain about it.
      ConvertedType = AdoptQualifiers(Context, ToType, FromQualifiers);
      IncompatibleObjC = true;
      return true;
    }
  }

  return false;
}

bool Sema::IsBlockPointerConversion(QualType FromType, QualType ToType,
                                    QualType& ConvertedType) {
  QualType ToPointeeType;
  if (const BlockPointerType *ToBlockPtr =
        ToType->getAs<BlockPointerType>())
    ToPointeeType = ToBlockPtr->getPointeeType();
  else
    return false;

  QualType FromPointeeType;
  if (const BlockPointerType *FromBlockPtr =
      FromType->getAs<BlockPointerType>())
    FromPointeeType = FromBlockPtr->getPointeeType();
  else
    return false;
  // We have pointer to blocks, check whether the only
  // differences in the argument and result types are in Objective-C
  // pointer conversions. If so, we permit the conversion.

  const FunctionProtoType *FromFunctionType
    = FromPointeeType->getAs<FunctionProtoType>();
  const FunctionProtoType *ToFunctionType
    = ToPointeeType->getAs<FunctionProtoType>();

  if (!FromFunctionType || !ToFunctionType)
    return false;

  if (Context.hasSameType(FromPointeeType, ToPointeeType))
    return true;

  // Perform the quick checks that will tell us whether these
  // function types are obviously different.
  if (FromFunctionType->getNumParams() != ToFunctionType->getNumParams() ||
      FromFunctionType->isVariadic() != ToFunctionType->isVariadic())
    return false;

  FunctionType::ExtInfo FromEInfo = FromFunctionType->getExtInfo();
  FunctionType::ExtInfo ToEInfo = ToFunctionType->getExtInfo();
  if (FromEInfo != ToEInfo)
    return false;

  bool IncompatibleObjC = false;
  if (Context.hasSameType(FromFunctionType->getReturnType(),
                          ToFunctionType->getReturnType())) {
    // Okay, the types match exactly. Nothing to do.
  } else {
    QualType RHS = FromFunctionType->getReturnType();
    QualType LHS = ToFunctionType->getReturnType();
    if ((!getLangOpts().CPlusPlus || !RHS->isRecordType()) &&
        !RHS.hasQualifiers() && LHS.hasQualifiers())
       LHS = LHS.getUnqualifiedType();

     if (Context.hasSameType(RHS,LHS)) {
       // OK exact match.
     } else if (isObjCPointerConversion(RHS, LHS,
                                        ConvertedType, IncompatibleObjC)) {
     if (IncompatibleObjC)
       return false;
     // Okay, we have an Objective-C pointer conversion.
     }
     else
       return false;
   }

   // Check argument types.
   for (unsigned ArgIdx = 0, NumArgs = FromFunctionType->getNumParams();
        ArgIdx != NumArgs; ++ArgIdx) {
     IncompatibleObjC = false;
     QualType FromArgType = FromFunctionType->getParamType(ArgIdx);
     QualType ToArgType = ToFunctionType->getParamType(ArgIdx);
     if (Context.hasSameType(FromArgType, ToArgType)) {
       // Okay, the types match exactly. Nothing to do.
     } else if (isObjCPointerConversion(ToArgType, FromArgType,
                                        ConvertedType, IncompatibleObjC)) {
       if (IncompatibleObjC)
         return false;
       // Okay, we have an Objective-C pointer conversion.
     } else
       // Argument types are too different. Abort.
       return false;
   }

   SmallVector<FunctionProtoType::ExtParameterInfo, 4> NewParamInfos;
   bool CanUseToFPT, CanUseFromFPT;
   if (!Context.mergeExtParameterInfo(ToFunctionType, FromFunctionType,
                                      CanUseToFPT, CanUseFromFPT,
                                      NewParamInfos))
     return false;

   ConvertedType = ToType;
   return true;
}

enum {
  ft_default,
  ft_different_class,
  ft_parameter_arity,
  ft_parameter_mismatch,
  ft_return_type,
  ft_qualifer_mismatch,
  ft_noexcept
};

/// Attempts to get the FunctionProtoType from a Type. Handles
/// MemberFunctionPointers properly.
static const FunctionProtoType *tryGetFunctionProtoType(QualType FromType) {
  if (auto *FPT = FromType->getAs<FunctionProtoType>())
    return FPT;

  if (auto *MPT = FromType->getAs<MemberPointerType>())
    return MPT->getPointeeType()->getAs<FunctionProtoType>();

  return nullptr;
}

void Sema::HandleFunctionTypeMismatch(PartialDiagnostic &PDiag,
                                      QualType FromType, QualType ToType) {
  // If either type is not valid, include no extra info.
  if (FromType.isNull() || ToType.isNull()) {
    PDiag << ft_default;
    return;
  }

  // Get the function type from the pointers.
  if (FromType->isMemberPointerType() && ToType->isMemberPointerType()) {
    const auto *FromMember = FromType->castAs<MemberPointerType>(),
               *ToMember = ToType->castAs<MemberPointerType>();
    if (!Context.hasSameType(FromMember->getClass(), ToMember->getClass())) {
      PDiag << ft_different_class << QualType(ToMember->getClass(), 0)
            << QualType(FromMember->getClass(), 0);
      return;
    }
    FromType = FromMember->getPointeeType();
    ToType = ToMember->getPointeeType();
  }

  if (FromType->isPointerType())
    FromType = FromType->getPointeeType();
  if (ToType->isPointerType())
    ToType = ToType->getPointeeType();

  // Remove references.
  FromType = FromType.getNonReferenceType();
  ToType = ToType.getNonReferenceType();

  // Don't print extra info for non-specialized template functions.
  if (FromType->isInstantiationDependentType() &&
      !FromType->getAs<TemplateSpecializationType>()) {
    PDiag << ft_default;
    return;
  }

  // No extra info for same types.
  if (Context.hasSameType(FromType, ToType)) {
    PDiag << ft_default;
    return;
  }

  const FunctionProtoType *FromFunction = tryGetFunctionProtoType(FromType),
                          *ToFunction = tryGetFunctionProtoType(ToType);

  // Both types need to be function types.
  if (!FromFunction || !ToFunction) {
    PDiag << ft_default;
    return;
  }

  if (FromFunction->getNumParams() != ToFunction->getNumParams()) {
    PDiag << ft_parameter_arity << ToFunction->getNumParams()
          << FromFunction->getNumParams();
    return;
  }

  // Handle different parameter types.
  unsigned ArgPos;
  if (!FunctionParamTypesAreEqual(FromFunction, ToFunction, &ArgPos)) {
    PDiag << ft_parameter_mismatch << ArgPos + 1
          << ToFunction->getParamType(ArgPos)
          << FromFunction->getParamType(ArgPos);
    return;
  }

  // Handle different return type.
  if (!Context.hasSameType(FromFunction->getReturnType(),
                           ToFunction->getReturnType())) {
    PDiag << ft_return_type << ToFunction->getReturnType()
          << FromFunction->getReturnType();
    return;
  }

  if (FromFunction->getMethodQuals() != ToFunction->getMethodQuals()) {
    PDiag << ft_qualifer_mismatch << ToFunction->getMethodQuals()
          << FromFunction->getMethodQuals();
    return;
  }

  // Handle exception specification differences on canonical type (in C++17
  // onwards).
  if (cast<FunctionProtoType>(FromFunction->getCanonicalTypeUnqualified())
          ->isNothrow() !=
      cast<FunctionProtoType>(ToFunction->getCanonicalTypeUnqualified())
          ->isNothrow()) {
    PDiag << ft_noexcept;
    return;
  }

  // Unable to find a difference, so add no extra info.
  PDiag << ft_default;
}

bool Sema::FunctionParamTypesAreEqual(ArrayRef<QualType> Old,
                                      ArrayRef<QualType> New, unsigned *ArgPos,
                                      bool Reversed) {
  assert(llvm::size(Old) == llvm::size(New) &&
         "Can't compare parameters of functions with different number of "
         "parameters!");

  for (auto &&[Idx, Type] : llvm::enumerate(Old)) {
    // Reverse iterate over the parameters of `OldType` if `Reversed` is true.
    size_t J = Reversed ? (llvm::size(New) - Idx - 1) : Idx;

    // Ignore address spaces in pointee type. This is to disallow overloading
    // on __ptr32/__ptr64 address spaces.
    QualType OldType =
        Context.removePtrSizeAddrSpace(Type.getUnqualifiedType());
    QualType NewType =
        Context.removePtrSizeAddrSpace((New.begin() + J)->getUnqualifiedType());

    if (!Context.hasSameType(OldType, NewType)) {
      if (ArgPos)
        *ArgPos = Idx;
      return false;
    }
  }
  return true;
}

bool Sema::FunctionParamTypesAreEqual(const FunctionProtoType *OldType,
                                      const FunctionProtoType *NewType,
                                      unsigned *ArgPos, bool Reversed) {
  return FunctionParamTypesAreEqual(OldType->param_types(),
                                    NewType->param_types(), ArgPos, Reversed);
}

bool Sema::FunctionNonObjectParamTypesAreEqual(const FunctionDecl *OldFunction,
                                               const FunctionDecl *NewFunction,
                                               unsigned *ArgPos,
                                               bool Reversed) {

  if (OldFunction->getNumNonObjectParams() !=
      NewFunction->getNumNonObjectParams())
    return false;

  unsigned OldIgnore =
      unsigned(OldFunction->hasCXXExplicitFunctionObjectParameter());
  unsigned NewIgnore =
      unsigned(NewFunction->hasCXXExplicitFunctionObjectParameter());

  auto *OldPT = cast<FunctionProtoType>(OldFunction->getFunctionType());
  auto *NewPT = cast<FunctionProtoType>(NewFunction->getFunctionType());

  return FunctionParamTypesAreEqual(OldPT->param_types().slice(OldIgnore),
                                    NewPT->param_types().slice(NewIgnore),
                                    ArgPos, Reversed);
}

bool Sema::CheckPointerConversion(Expr *From, QualType ToType,
                                  CastKind &Kind,
                                  CXXCastPath& BasePath,
                                  bool IgnoreBaseAccess,
                                  bool Diagnose) {
  QualType FromType = From->getType();
  bool IsCStyleOrFunctionalCast = IgnoreBaseAccess;

  Kind = CK_BitCast;

  if (Diagnose && !IsCStyleOrFunctionalCast && !FromType->isAnyPointerType() &&
      From->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNotNull) ==
          Expr::NPCK_ZeroExpression) {
    if (Context.hasSameUnqualifiedType(From->getType(), Context.BoolTy))
      DiagRuntimeBehavior(From->getExprLoc(), From,
                          PDiag(diag::warn_impcast_bool_to_null_pointer)
                            << ToType << From->getSourceRange());
    else if (!isUnevaluatedContext())
      Diag(From->getExprLoc(), diag::warn_non_literal_null_pointer)
        << ToType << From->getSourceRange();
  }
  if (const PointerType *ToPtrType = ToType->getAs<PointerType>()) {
    if (const PointerType *FromPtrType = FromType->getAs<PointerType>()) {
      QualType FromPointeeType = FromPtrType->getPointeeType(),
               ToPointeeType   = ToPtrType->getPointeeType();

      if (FromPointeeType->isRecordType() && ToPointeeType->isRecordType() &&
          !Context.hasSameUnqualifiedType(FromPointeeType, ToPointeeType)) {
        // We must have a derived-to-base conversion. Check an
        // ambiguous or inaccessible conversion.
        unsigned InaccessibleID = 0;
        unsigned AmbiguousID = 0;
        if (Diagnose) {
          InaccessibleID = diag::err_upcast_to_inaccessible_base;
          AmbiguousID = diag::err_ambiguous_derived_to_base_conv;
        }
        if (CheckDerivedToBaseConversion(
                FromPointeeType, ToPointeeType, InaccessibleID, AmbiguousID,
                From->getExprLoc(), From->getSourceRange(), DeclarationName(),
                &BasePath, IgnoreBaseAccess))
          return true;

        // The conversion was successful.
        Kind = CK_DerivedToBase;
      }

      if (Diagnose && !IsCStyleOrFunctionalCast &&
          FromPointeeType->isFunctionType() && ToPointeeType->isVoidType()) {
        assert(getLangOpts().MSVCCompat &&
               "this should only be possible with MSVCCompat!");
        Diag(From->getExprLoc(), diag::ext_ms_impcast_fn_obj)
            << From->getSourceRange();
      }
    }
  } else if (const ObjCObjectPointerType *ToPtrType =
               ToType->getAs<ObjCObjectPointerType>()) {
    if (const ObjCObjectPointerType *FromPtrType =
          FromType->getAs<ObjCObjectPointerType>()) {
      // Objective-C++ conversions are always okay.
      // FIXME: We should have a different class of conversions for the
      // Objective-C++ implicit conversions.
      if (FromPtrType->isObjCBuiltinType() || ToPtrType->isObjCBuiltinType())
        return false;
    } else if (FromType->isBlockPointerType()) {
      Kind = CK_BlockPointerToObjCPointerCast;
    } else {
      Kind = CK_CPointerToObjCPointerCast;
    }
  } else if (ToType->isBlockPointerType()) {
    if (!FromType->isBlockPointerType())
      Kind = CK_AnyPointerToBlockPointerCast;
  }

  // We shouldn't fall into this case unless it's valid for other
  // reasons.
  if (From->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull))
    Kind = CK_NullToPointer;

  return false;
}

bool Sema::IsMemberPointerConversion(Expr *From, QualType FromType,
                                     QualType ToType,
                                     bool InOverloadResolution,
                                     QualType &ConvertedType) {
  const MemberPointerType *ToTypePtr = ToType->getAs<MemberPointerType>();
  if (!ToTypePtr)
    return false;

  // A null pointer constant can be converted to a member pointer (C++ 4.11p1)
  if (From->isNullPointerConstant(Context,
                    InOverloadResolution? Expr::NPC_ValueDependentIsNotNull
                                        : Expr::NPC_ValueDependentIsNull)) {
    ConvertedType = ToType;
    return true;
  }

  // Otherwise, both types have to be member pointers.
  const MemberPointerType *FromTypePtr = FromType->getAs<MemberPointerType>();
  if (!FromTypePtr)
    return false;

  // A pointer to member of B can be converted to a pointer to member of D,
  // where D is derived from B (C++ 4.11p2).
  QualType FromClass(FromTypePtr->getClass(), 0);
  QualType ToClass(ToTypePtr->getClass(), 0);

  if (!Context.hasSameUnqualifiedType(FromClass, ToClass) &&
      IsDerivedFrom(From->getBeginLoc(), ToClass, FromClass)) {
    ConvertedType = Context.getMemberPointerType(FromTypePtr->getPointeeType(),
                                                 ToClass.getTypePtr());
    return true;
  }

  return false;
}

bool Sema::CheckMemberPointerConversion(Expr *From, QualType ToType,
                                        CastKind &Kind,
                                        CXXCastPath &BasePath,
                                        bool IgnoreBaseAccess) {
  QualType FromType = From->getType();
  const MemberPointerType *FromPtrType = FromType->getAs<MemberPointerType>();
  if (!FromPtrType) {
    // This must be a null pointer to member pointer conversion
    assert(From->isNullPointerConstant(Context,
                                       Expr::NPC_ValueDependentIsNull) &&
           "Expr must be null pointer constant!");
    Kind = CK_NullToMemberPointer;
    return false;
  }

  const MemberPointerType *ToPtrType = ToType->getAs<MemberPointerType>();
  assert(ToPtrType && "No member pointer cast has a target type "
                      "that is not a member pointer.");

  QualType FromClass = QualType(FromPtrType->getClass(), 0);
  QualType ToClass   = QualType(ToPtrType->getClass(), 0);

  // FIXME: What about dependent types?
  assert(FromClass->isRecordType() && "Pointer into non-class.");
  assert(ToClass->isRecordType() && "Pointer into non-class.");

  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/true);
  bool DerivationOkay =
      IsDerivedFrom(From->getBeginLoc(), ToClass, FromClass, Paths);
  assert(DerivationOkay &&
         "Should not have been called if derivation isn't OK.");
  (void)DerivationOkay;

  if (Paths.isAmbiguous(Context.getCanonicalType(FromClass).
                                  getUnqualifiedType())) {
    std::string PathDisplayStr = getAmbiguousPathsDisplayString(Paths);
    Diag(From->getExprLoc(), diag::err_ambiguous_memptr_conv)
      << 0 << FromClass << ToClass << PathDisplayStr << From->getSourceRange();
    return true;
  }

  if (const RecordType *VBase = Paths.getDetectedVirtual()) {
    Diag(From->getExprLoc(), diag::err_memptr_conv_via_virtual)
      << FromClass << ToClass << QualType(VBase, 0)
      << From->getSourceRange();
    return true;
  }

  if (!IgnoreBaseAccess)
    CheckBaseClassAccess(From->getExprLoc(), FromClass, ToClass,
                         Paths.front(),
                         diag::err_downcast_from_inaccessible_base);

  // Must be a base to derived member conversion.
  BuildBasePathArray(Paths, BasePath);
  Kind = CK_BaseToDerivedMemberPointer;
  return false;
}

/// Determine whether the lifetime conversion between the two given
/// qualifiers sets is nontrivial.
static bool isNonTrivialObjCLifetimeConversion(Qualifiers FromQuals,
                                               Qualifiers ToQuals) {
  // Converting anything to const __unsafe_unretained is trivial.
  if (ToQuals.hasConst() &&
      ToQuals.getObjCLifetime() == Qualifiers::OCL_ExplicitNone)
    return false;

  return true;
}

/// Perform a single iteration of the loop for checking if a qualification
/// conversion is valid.
///
/// Specifically, check whether any change between the qualifiers of \p
/// FromType and \p ToType is permissible, given knowledge about whether every
/// outer layer is const-qualified.
static bool isQualificationConversionStep(QualType FromType, QualType ToType,
                                          bool CStyle, bool IsTopLevel,
                                          bool &PreviousToQualsIncludeConst,
                                          bool &ObjCLifetimeConversion) {
  Qualifiers FromQuals = FromType.getQualifiers();
  Qualifiers ToQuals = ToType.getQualifiers();

  // Ignore __unaligned qualifier.
  FromQuals.removeUnaligned();

  // Objective-C ARC:
  //   Check Objective-C lifetime conversions.
  if (FromQuals.getObjCLifetime() != ToQuals.getObjCLifetime()) {
    if (ToQuals.compatiblyIncludesObjCLifetime(FromQuals)) {
      if (isNonTrivialObjCLifetimeConversion(FromQuals, ToQuals))
        ObjCLifetimeConversion = true;
      FromQuals.removeObjCLifetime();
      ToQuals.removeObjCLifetime();
    } else {
      // Qualification conversions cannot cast between different
      // Objective-C lifetime qualifiers.
      return false;
    }
  }

  // Allow addition/removal of GC attributes but not changing GC attributes.
  if (FromQuals.getObjCGCAttr() != ToQuals.getObjCGCAttr() &&
      (!FromQuals.hasObjCGCAttr() || !ToQuals.hasObjCGCAttr())) {
    FromQuals.removeObjCGCAttr();
    ToQuals.removeObjCGCAttr();
  }

  //   -- for every j > 0, if const is in cv 1,j then const is in cv
  //      2,j, and similarly for volatile.
  if (!CStyle && !ToQuals.compatiblyIncludes(FromQuals))
    return false;

  // If address spaces mismatch:
  //  - in top level it is only valid to convert to addr space that is a
  //    superset in all cases apart from C-style casts where we allow
  //    conversions between overlapping address spaces.
  //  - in non-top levels it is not a valid conversion.
  if (ToQuals.getAddressSpace() != FromQuals.getAddressSpace() &&
      (!IsTopLevel ||
       !(ToQuals.isAddressSpaceSupersetOf(FromQuals) ||
         (CStyle && FromQuals.isAddressSpaceSupersetOf(ToQuals)))))
    return false;

  //   -- if the cv 1,j and cv 2,j are different, then const is in
  //      every cv for 0 < k < j.
  if (!CStyle && FromQuals.getCVRQualifiers() != ToQuals.getCVRQualifiers() &&
      !PreviousToQualsIncludeConst)
    return false;

  // The following wording is from C++20, where the result of the conversion
  // is T3, not T2.
  //   -- if [...] P1,i [...] is "array of unknown bound of", P3,i is
  //      "array of unknown bound of"
  if (FromType->isIncompleteArrayType() && !ToType->isIncompleteArrayType())
    return false;

  //   -- if the resulting P3,i is different from P1,i [...], then const is
  //      added to every cv 3_k for 0 < k < i.
  if (!CStyle && FromType->isConstantArrayType() &&
      ToType->isIncompleteArrayType() && !PreviousToQualsIncludeConst)
    return false;

  // Keep track of whether all prior cv-qualifiers in the "to" type
  // include const.
  PreviousToQualsIncludeConst =
      PreviousToQualsIncludeConst && ToQuals.hasConst();
  return true;
}

bool
Sema::IsQualificationConversion(QualType FromType, QualType ToType,
                                bool CStyle, bool &ObjCLifetimeConversion) {
  FromType = Context.getCanonicalType(FromType);
  ToType = Context.getCanonicalType(ToType);
  ObjCLifetimeConversion = false;

  // If FromType and ToType are the same type, this is not a
  // qualification conversion.
  if (FromType.getUnqualifiedType() == ToType.getUnqualifiedType())
    return false;

  // (C++ 4.4p4):
  //   A conversion can add cv-qualifiers at levels other than the first
  //   in multi-level pointers, subject to the following rules: [...]
  bool PreviousToQualsIncludeConst = true;
  bool UnwrappedAnyPointer = false;
  while (Context.UnwrapSimilarTypes(FromType, ToType)) {
    if (!isQualificationConversionStep(
            FromType, ToType, CStyle, !UnwrappedAnyPointer,
            PreviousToQualsIncludeConst, ObjCLifetimeConversion))
      return false;
    UnwrappedAnyPointer = true;
  }

  // We are left with FromType and ToType being the pointee types
  // after unwrapping the original FromType and ToType the same number
  // of times. If we unwrapped any pointers, and if FromType and
  // ToType have the same unqualified type (since we checked
  // qualifiers above), then this is a qualification conversion.
  return UnwrappedAnyPointer && Context.hasSameUnqualifiedType(FromType,ToType);
}

/// - Determine whether this is a conversion from a scalar type to an
/// atomic type.
///
/// If successful, updates \c SCS's second and third steps in the conversion
/// sequence to finish the conversion.
static bool tryAtomicConversion(Sema &S, Expr *From, QualType ToType,
                                bool InOverloadResolution,
                                StandardConversionSequence &SCS,
                                bool CStyle) {
  const AtomicType *ToAtomic = ToType->getAs<AtomicType>();
  if (!ToAtomic)
    return false;

  StandardConversionSequence InnerSCS;
  if (!IsStandardConversion(S, From, ToAtomic->getValueType(),
                            InOverloadResolution, InnerSCS,
                            CStyle, /*AllowObjCWritebackConversion=*/false))
    return false;

  SCS.Second = InnerSCS.Second;
  SCS.setToType(1, InnerSCS.getToType(1));
  SCS.Third = InnerSCS.Third;
  SCS.QualificationIncludesObjCLifetime
    = InnerSCS.QualificationIncludesObjCLifetime;
  SCS.setToType(2, InnerSCS.getToType(2));
  return true;
}

static bool isFirstArgumentCompatibleWithType(ASTContext &Context,
                                              CXXConstructorDecl *Constructor,
                                              QualType Type) {
  const auto *CtorType = Constructor->getType()->castAs<FunctionProtoType>();
  if (CtorType->getNumParams() > 0) {
    QualType FirstArg = CtorType->getParamType(0);
    if (Context.hasSameUnqualifiedType(Type, FirstArg.getNonReferenceType()))
      return true;
  }
  return false;
}

static OverloadingResult
IsInitializerListConstructorConversion(Sema &S, Expr *From, QualType ToType,
                                       CXXRecordDecl *To,
                                       UserDefinedConversionSequence &User,
                                       OverloadCandidateSet &CandidateSet,
                                       bool AllowExplicit) {
  CandidateSet.clear(OverloadCandidateSet::CSK_InitByUserDefinedConversion);
  for (auto *D : S.LookupConstructors(To)) {
    auto Info = getConstructorInfo(D);
    if (!Info)
      continue;

    bool Usable = !Info.Constructor->isInvalidDecl() &&
                  S.isInitListConstructor(Info.Constructor);
    if (Usable) {
      bool SuppressUserConversions = false;
      if (Info.ConstructorTmpl)
        S.AddTemplateOverloadCandidate(Info.ConstructorTmpl, Info.FoundDecl,
                                       /*ExplicitArgs*/ nullptr, From,
                                       CandidateSet, SuppressUserConversions,
                                       /*PartialOverloading*/ false,
                                       AllowExplicit);
      else
        S.AddOverloadCandidate(Info.Constructor, Info.FoundDecl, From,
                               CandidateSet, SuppressUserConversions,
                               /*PartialOverloading*/ false, AllowExplicit);
    }
  }

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  OverloadCandidateSet::iterator Best;
  switch (auto Result =
              CandidateSet.BestViableFunction(S, From->getBeginLoc(), Best)) {
  case OR_Deleted:
  case OR_Success: {
    // Record the standard conversion we used and the conversion function.
    CXXConstructorDecl *Constructor = cast<CXXConstructorDecl>(Best->Function);
    QualType ThisType = Constructor->getFunctionObjectParameterType();
    // Initializer lists don't have conversions as such.
    User.Before.setAsIdentityConversion();
    User.HadMultipleCandidates = HadMultipleCandidates;
    User.ConversionFunction = Constructor;
    User.FoundConversionFunction = Best->FoundDecl;
    User.After.setAsIdentityConversion();
    User.After.setFromType(ThisType);
    User.After.setAllToTypes(ToType);
    return Result;
  }

  case OR_No_Viable_Function:
    return OR_No_Viable_Function;
  case OR_Ambiguous:
    return OR_Ambiguous;
  }

  llvm_unreachable("Invalid OverloadResult!");
}

/// Determines whether there is a user-defined conversion sequence
/// (C++ [over.ics.user]) that converts expression From to the type
/// ToType. If such a conversion exists, User will contain the
/// user-defined conversion sequence that performs such a conversion
/// and this routine will return true. Otherwise, this routine returns
/// false and User is unspecified.
///
/// \param AllowExplicit  true if the conversion should consider C++0x
/// "explicit" conversion functions as well as non-explicit conversion
/// functions (C++0x [class.conv.fct]p2).
///
/// \param AllowObjCConversionOnExplicit true if the conversion should
/// allow an extra Objective-C pointer conversion on uses of explicit
/// constructors. Requires \c AllowExplicit to also be set.
static OverloadingResult
IsUserDefinedConversion(Sema &S, Expr *From, QualType ToType,
                        UserDefinedConversionSequence &User,
                        OverloadCandidateSet &CandidateSet,
                        AllowedExplicit AllowExplicit,
                        bool AllowObjCConversionOnExplicit) {
  assert(AllowExplicit != AllowedExplicit::None ||
         !AllowObjCConversionOnExplicit);
  CandidateSet.clear(OverloadCandidateSet::CSK_InitByUserDefinedConversion);

  // Whether we will only visit constructors.
  bool ConstructorsOnly = false;

  // If the type we are conversion to is a class type, enumerate its
  // constructors.
  if (const RecordType *ToRecordType = ToType->getAs<RecordType>()) {
    // C++ [over.match.ctor]p1:
    //   When objects of class type are direct-initialized (8.5), or
    //   copy-initialized from an expression of the same or a
    //   derived class type (8.5), overload resolution selects the
    //   constructor. [...] For copy-initialization, the candidate
    //   functions are all the converting constructors (12.3.1) of
    //   that class. The argument list is the expression-list within
    //   the parentheses of the initializer.
    if (S.Context.hasSameUnqualifiedType(ToType, From->getType()) ||
        (From->getType()->getAs<RecordType>() &&
         S.IsDerivedFrom(From->getBeginLoc(), From->getType(), ToType)))
      ConstructorsOnly = true;

    if (!S.isCompleteType(From->getExprLoc(), ToType)) {
      // We're not going to find any constructors.
    } else if (CXXRecordDecl *ToRecordDecl
                 = dyn_cast<CXXRecordDecl>(ToRecordType->getDecl())) {

      Expr **Args = &From;
      unsigned NumArgs = 1;
      bool ListInitializing = false;
      if (InitListExpr *InitList = dyn_cast<InitListExpr>(From)) {
        // But first, see if there is an init-list-constructor that will work.
        OverloadingResult Result = IsInitializerListConstructorConversion(
            S, From, ToType, ToRecordDecl, User, CandidateSet,
            AllowExplicit == AllowedExplicit::All);
        if (Result != OR_No_Viable_Function)
          return Result;
        // Never mind.
        CandidateSet.clear(
            OverloadCandidateSet::CSK_InitByUserDefinedConversion);

        // If we're list-initializing, we pass the individual elements as
        // arguments, not the entire list.
        Args = InitList->getInits();
        NumArgs = InitList->getNumInits();
        ListInitializing = true;
      }

      for (auto *D : S.LookupConstructors(ToRecordDecl)) {
        auto Info = getConstructorInfo(D);
        if (!Info)
          continue;

        bool Usable = !Info.Constructor->isInvalidDecl();
        if (!ListInitializing)
          Usable = Usable && Info.Constructor->isConvertingConstructor(
                                 /*AllowExplicit*/ true);
        if (Usable) {
          bool SuppressUserConversions = !ConstructorsOnly;
          // C++20 [over.best.ics.general]/4.5:
          //   if the target is the first parameter of a constructor [of class
          //   X] and the constructor [...] is a candidate by [...] the second
          //   phase of [over.match.list] when the initializer list has exactly
          //   one element that is itself an initializer list, [...] and the
          //   conversion is to X or reference to cv X, user-defined conversion
          //   sequences are not cnosidered.
          if (SuppressUserConversions && ListInitializing) {
            SuppressUserConversions =
                NumArgs == 1 && isa<InitListExpr>(Args[0]) &&
                isFirstArgumentCompatibleWithType(S.Context, Info.Constructor,
                                                  ToType);
          }
          if (Info.ConstructorTmpl)
            S.AddTemplateOverloadCandidate(
                Info.ConstructorTmpl, Info.FoundDecl,
                /*ExplicitArgs*/ nullptr, llvm::ArrayRef(Args, NumArgs),
                CandidateSet, SuppressUserConversions,
                /*PartialOverloading*/ false,
                AllowExplicit == AllowedExplicit::All);
          else
            // Allow one user-defined conversion when user specifies a
            // From->ToType conversion via an static cast (c-style, etc).
            S.AddOverloadCandidate(Info.Constructor, Info.FoundDecl,
                                   llvm::ArrayRef(Args, NumArgs), CandidateSet,
                                   SuppressUserConversions,
                                   /*PartialOverloading*/ false,
                                   AllowExplicit == AllowedExplicit::All);
        }
      }
    }
  }

  // Enumerate conversion functions, if we're allowed to.
  if (ConstructorsOnly || isa<InitListExpr>(From)) {
  } else if (!S.isCompleteType(From->getBeginLoc(), From->getType())) {
    // No conversion functions from incomplete types.
  } else if (const RecordType *FromRecordType =
                 From->getType()->getAs<RecordType>()) {
    if (CXXRecordDecl *FromRecordDecl
         = dyn_cast<CXXRecordDecl>(FromRecordType->getDecl())) {
      // Add all of the conversion functions as candidates.
      const auto &Conversions = FromRecordDecl->getVisibleConversionFunctions();
      for (auto I = Conversions.begin(), E = Conversions.end(); I != E; ++I) {
        DeclAccessPair FoundDecl = I.getPair();
        NamedDecl *D = FoundDecl.getDecl();
        CXXRecordDecl *ActingContext = cast<CXXRecordDecl>(D->getDeclContext());
        if (isa<UsingShadowDecl>(D))
          D = cast<UsingShadowDecl>(D)->getTargetDecl();

        CXXConversionDecl *Conv;
        FunctionTemplateDecl *ConvTemplate;
        if ((ConvTemplate = dyn_cast<FunctionTemplateDecl>(D)))
          Conv = cast<CXXConversionDecl>(ConvTemplate->getTemplatedDecl());
        else
          Conv = cast<CXXConversionDecl>(D);

        if (ConvTemplate)
          S.AddTemplateConversionCandidate(
              ConvTemplate, FoundDecl, ActingContext, From, ToType,
              CandidateSet, AllowObjCConversionOnExplicit,
              AllowExplicit != AllowedExplicit::None);
        else
          S.AddConversionCandidate(Conv, FoundDecl, ActingContext, From, ToType,
                                   CandidateSet, AllowObjCConversionOnExplicit,
                                   AllowExplicit != AllowedExplicit::None);
      }
    }
  }

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  OverloadCandidateSet::iterator Best;
  switch (auto Result =
              CandidateSet.BestViableFunction(S, From->getBeginLoc(), Best)) {
  case OR_Success:
  case OR_Deleted:
    // Record the standard conversion we used and the conversion function.
    if (CXXConstructorDecl *Constructor
          = dyn_cast<CXXConstructorDecl>(Best->Function)) {
      // C++ [over.ics.user]p1:
      //   If the user-defined conversion is specified by a
      //   constructor (12.3.1), the initial standard conversion
      //   sequence converts the source type to the type required by
      //   the argument of the constructor.
      //
      if (isa<InitListExpr>(From)) {
        // Initializer lists don't have conversions as such.
        User.Before.setAsIdentityConversion();
      } else {
        if (Best->Conversions[0].isEllipsis())
          User.EllipsisConversion = true;
        else {
          User.Before = Best->Conversions[0].Standard;
          User.EllipsisConversion = false;
        }
      }
      User.HadMultipleCandidates = HadMultipleCandidates;
      User.ConversionFunction = Constructor;
      User.FoundConversionFunction = Best->FoundDecl;
      User.After.setAsIdentityConversion();
      User.After.setFromType(Constructor->getFunctionObjectParameterType());
      User.After.setAllToTypes(ToType);
      return Result;
    }
    if (CXXConversionDecl *Conversion
                 = dyn_cast<CXXConversionDecl>(Best->Function)) {
      // C++ [over.ics.user]p1:
      //
      //   [...] If the user-defined conversion is specified by a
      //   conversion function (12.3.2), the initial standard
      //   conversion sequence converts the source type to the
      //   implicit object parameter of the conversion function.
      User.Before = Best->Conversions[0].Standard;
      User.HadMultipleCandidates = HadMultipleCandidates;
      User.ConversionFunction = Conversion;
      User.FoundConversionFunction = Best->FoundDecl;
      User.EllipsisConversion = false;

      // C++ [over.ics.user]p2:
      //   The second standard conversion sequence converts the
      //   result of the user-defined conversion to the target type
      //   for the sequence. Since an implicit conversion sequence
      //   is an initialization, the special rules for
      //   initialization by user-defined conversion apply when
      //   selecting the best user-defined conversion for a
      //   user-defined conversion sequence (see 13.3.3 and
      //   13.3.3.1).
      User.After = Best->FinalConversion;
      return Result;
    }
    llvm_unreachable("Not a constructor or conversion function?");

  case OR_No_Viable_Function:
    return OR_No_Viable_Function;

  case OR_Ambiguous:
    return OR_Ambiguous;
  }

  llvm_unreachable("Invalid OverloadResult!");
}

bool
Sema::DiagnoseMultipleUserDefinedConversion(Expr *From, QualType ToType) {
  ImplicitConversionSequence ICS;
  OverloadCandidateSet CandidateSet(From->getExprLoc(),
                                    OverloadCandidateSet::CSK_Normal);
  OverloadingResult OvResult =
    IsUserDefinedConversion(*this, From, ToType, ICS.UserDefined,
                            CandidateSet, AllowedExplicit::None, false);

  if (!(OvResult == OR_Ambiguous ||
        (OvResult == OR_No_Viable_Function && !CandidateSet.empty())))
    return false;

  auto Cands = CandidateSet.CompleteCandidates(
      *this,
      OvResult == OR_Ambiguous ? OCD_AmbiguousCandidates : OCD_AllCandidates,
      From);
  if (OvResult == OR_Ambiguous)
    Diag(From->getBeginLoc(), diag::err_typecheck_ambiguous_condition)
        << From->getType() << ToType << From->getSourceRange();
  else { // OR_No_Viable_Function && !CandidateSet.empty()
    if (!RequireCompleteType(From->getBeginLoc(), ToType,
                             diag::err_typecheck_nonviable_condition_incomplete,
                             From->getType(), From->getSourceRange()))
      Diag(From->getBeginLoc(), diag::err_typecheck_nonviable_condition)
          << false << From->getType() << From->getSourceRange() << ToType;
  }

  CandidateSet.NoteCandidates(
                              *this, From, Cands);
  return true;
}

// Helper for compareConversionFunctions that gets the FunctionType that the
// conversion-operator return  value 'points' to, or nullptr.
static const FunctionType *
getConversionOpReturnTyAsFunction(CXXConversionDecl *Conv) {
  const FunctionType *ConvFuncTy = Conv->getType()->castAs<FunctionType>();
  const PointerType *RetPtrTy =
      ConvFuncTy->getReturnType()->getAs<PointerType>();

  if (!RetPtrTy)
    return nullptr;

  return RetPtrTy->getPointeeType()->getAs<FunctionType>();
}

/// Compare the user-defined conversion functions or constructors
/// of two user-defined conversion sequences to determine whether any ordering
/// is possible.
static ImplicitConversionSequence::CompareKind
compareConversionFunctions(Sema &S, FunctionDecl *Function1,
                           FunctionDecl *Function2) {
  CXXConversionDecl *Conv1 = dyn_cast_or_null<CXXConversionDecl>(Function1);
  CXXConversionDecl *Conv2 = dyn_cast_or_null<CXXConversionDecl>(Function2);
  if (!Conv1 || !Conv2)
    return ImplicitConversionSequence::Indistinguishable;

  if (!Conv1->getParent()->isLambda() || !Conv2->getParent()->isLambda())
    return ImplicitConversionSequence::Indistinguishable;

  // Objective-C++:
  //   If both conversion functions are implicitly-declared conversions from
  //   a lambda closure type to a function pointer and a block pointer,
  //   respectively, always prefer the conversion to a function pointer,
  //   because the function pointer is more lightweight and is more likely
  //   to keep code working.
  if (S.getLangOpts().ObjC && S.getLangOpts().CPlusPlus11) {
    bool Block1 = Conv1->getConversionType()->isBlockPointerType();
    bool Block2 = Conv2->getConversionType()->isBlockPointerType();
    if (Block1 != Block2)
      return Block1 ? ImplicitConversionSequence::Worse
                    : ImplicitConversionSequence::Better;
  }

  // In order to support multiple calling conventions for the lambda conversion
  // operator (such as when the free and member function calling convention is
  // different), prefer the 'free' mechanism, followed by the calling-convention
  // of operator(). The latter is in place to support the MSVC-like solution of
  // defining ALL of the possible conversions in regards to calling-convention.
  const FunctionType *Conv1FuncRet = getConversionOpReturnTyAsFunction(Conv1);
  const FunctionType *Conv2FuncRet = getConversionOpReturnTyAsFunction(Conv2);

  if (Conv1FuncRet && Conv2FuncRet &&
      Conv1FuncRet->getCallConv() != Conv2FuncRet->getCallConv()) {
    CallingConv Conv1CC = Conv1FuncRet->getCallConv();
    CallingConv Conv2CC = Conv2FuncRet->getCallConv();

    CXXMethodDecl *CallOp = Conv2->getParent()->getLambdaCallOperator();
    const auto *CallOpProto = CallOp->getType()->castAs<FunctionProtoType>();

    CallingConv CallOpCC =
        CallOp->getType()->castAs<FunctionType>()->getCallConv();
    CallingConv DefaultFree = S.Context.getDefaultCallingConvention(
        CallOpProto->isVariadic(), /*IsCXXMethod=*/false);
    CallingConv DefaultMember = S.Context.getDefaultCallingConvention(
        CallOpProto->isVariadic(), /*IsCXXMethod=*/true);

    CallingConv PrefOrder[] = {DefaultFree, DefaultMember, CallOpCC};
    for (CallingConv CC : PrefOrder) {
      if (Conv1CC == CC)
        return ImplicitConversionSequence::Better;
      if (Conv2CC == CC)
        return ImplicitConversionSequence::Worse;
    }
  }

  return ImplicitConversionSequence::Indistinguishable;
}

static bool hasDeprecatedStringLiteralToCharPtrConversion(
    const ImplicitConversionSequence &ICS) {
  return (ICS.isStandard() && ICS.Standard.DeprecatedStringLiteralToCharPtr) ||
         (ICS.isUserDefined() &&
          ICS.UserDefined.Before.DeprecatedStringLiteralToCharPtr);
}

/// CompareImplicitConversionSequences - Compare two implicit
/// conversion sequences to determine whether one is better than the
/// other or if they are indistinguishable (C++ 13.3.3.2).
static ImplicitConversionSequence::CompareKind
CompareImplicitConversionSequences(Sema &S, SourceLocation Loc,
                                   const ImplicitConversionSequence& ICS1,
                                   const ImplicitConversionSequence& ICS2)
{
  // (C++ 13.3.3.2p2): When comparing the basic forms of implicit
  // conversion sequences (as defined in 13.3.3.1)
  //   -- a standard conversion sequence (13.3.3.1.1) is a better
  //      conversion sequence than a user-defined conversion sequence or
  //      an ellipsis conversion sequence, and
  //   -- a user-defined conversion sequence (13.3.3.1.2) is a better
  //      conversion sequence than an ellipsis conversion sequence
  //      (13.3.3.1.3).
  //
  // C++0x [over.best.ics]p10:
  //   For the purpose of ranking implicit conversion sequences as
  //   described in 13.3.3.2, the ambiguous conversion sequence is
  //   treated as a user-defined sequence that is indistinguishable
  //   from any other user-defined conversion sequence.

  // String literal to 'char *' conversion has been deprecated in C++03. It has
  // been removed from C++11. We still accept this conversion, if it happens at
  // the best viable function. Otherwise, this conversion is considered worse
  // than ellipsis conversion. Consider this as an extension; this is not in the
  // standard. For example:
  //
  // int &f(...);    // #1
  // void f(char*);  // #2
  // void g() { int &r = f("foo"); }
  //
  // In C++03, we pick #2 as the best viable function.
  // In C++11, we pick #1 as the best viable function, because ellipsis
  // conversion is better than string-literal to char* conversion (since there
  // is no such conversion in C++11). If there was no #1 at all or #1 couldn't
  // convert arguments, #2 would be the best viable function in C++11.
  // If the best viable function has this conversion, a warning will be issued
  // in C++03, or an ExtWarn (+SFINAE failure) will be issued in C++11.

  if (S.getLangOpts().CPlusPlus11 && !S.getLangOpts().WritableStrings &&
      hasDeprecatedStringLiteralToCharPtrConversion(ICS1) !=
          hasDeprecatedStringLiteralToCharPtrConversion(ICS2) &&
      // Ill-formedness must not differ
      ICS1.isBad() == ICS2.isBad())
    return hasDeprecatedStringLiteralToCharPtrConversion(ICS1)
               ? ImplicitConversionSequence::Worse
               : ImplicitConversionSequence::Better;

  if (ICS1.getKindRank() < ICS2.getKindRank())
    return ImplicitConversionSequence::Better;
  if (ICS2.getKindRank() < ICS1.getKindRank())
    return ImplicitConversionSequence::Worse;

  // The following checks require both conversion sequences to be of
  // the same kind.
  if (ICS1.getKind() != ICS2.getKind())
    return ImplicitConversionSequence::Indistinguishable;

  ImplicitConversionSequence::CompareKind Result =
      ImplicitConversionSequence::Indistinguishable;

  // Two implicit conversion sequences of the same form are
  // indistinguishable conversion sequences unless one of the
  // following rules apply: (C++ 13.3.3.2p3):

  // List-initialization sequence L1 is a better conversion sequence than
  // list-initialization sequence L2 if:
  // - L1 converts to std::initializer_list<X> for some X and L2 does not, or,
  //   if not that,
  // — L1 and L2 convert to arrays of the same element type, and either the
  //   number of elements n_1 initialized by L1 is less than the number of
  //   elements n_2 initialized by L2, or (C++20) n_1 = n_2 and L2 converts to
  //   an array of unknown bound and L1 does not,
  // even if one of the other rules in this paragraph would otherwise apply.
  if (!ICS1.isBad()) {
    bool StdInit1 = false, StdInit2 = false;
    if (ICS1.hasInitializerListContainerType())
      StdInit1 = S.isStdInitializerList(ICS1.getInitializerListContainerType(),
                                        nullptr);
    if (ICS2.hasInitializerListContainerType())
      StdInit2 = S.isStdInitializerList(ICS2.getInitializerListContainerType(),
                                        nullptr);
    if (StdInit1 != StdInit2)
      return StdInit1 ? ImplicitConversionSequence::Better
                      : ImplicitConversionSequence::Worse;

    if (ICS1.hasInitializerListContainerType() &&
        ICS2.hasInitializerListContainerType())
      if (auto *CAT1 = S.Context.getAsConstantArrayType(
              ICS1.getInitializerListContainerType()))
        if (auto *CAT2 = S.Context.getAsConstantArrayType(
                ICS2.getInitializerListContainerType())) {
          if (S.Context.hasSameUnqualifiedType(CAT1->getElementType(),
                                               CAT2->getElementType())) {
            // Both to arrays of the same element type
            if (CAT1->getSize() != CAT2->getSize())
              // Different sized, the smaller wins
              return CAT1->getSize().ult(CAT2->getSize())
                         ? ImplicitConversionSequence::Better
                         : ImplicitConversionSequence::Worse;
            if (ICS1.isInitializerListOfIncompleteArray() !=
                ICS2.isInitializerListOfIncompleteArray())
              // One is incomplete, it loses
              return ICS2.isInitializerListOfIncompleteArray()
                         ? ImplicitConversionSequence::Better
                         : ImplicitConversionSequence::Worse;
          }
        }
  }

  if (ICS1.isStandard())
    // Standard conversion sequence S1 is a better conversion sequence than
    // standard conversion sequence S2 if [...]
    Result = CompareStandardConversionSequences(S, Loc,
                                                ICS1.Standard, ICS2.Standard);
  else if (ICS1.isUserDefined()) {
    // User-defined conversion sequence U1 is a better conversion
    // sequence than another user-defined conversion sequence U2 if
    // they contain the same user-defined conversion function or
    // constructor and if the second standard conversion sequence of
    // U1 is better than the second standard conversion sequence of
    // U2 (C++ 13.3.3.2p3).
    if (ICS1.UserDefined.ConversionFunction ==
          ICS2.UserDefined.ConversionFunction)
      Result = CompareStandardConversionSequences(S, Loc,
                                                  ICS1.UserDefined.After,
                                                  ICS2.UserDefined.After);
    else
      Result = compareConversionFunctions(S,
                                          ICS1.UserDefined.ConversionFunction,
                                          ICS2.UserDefined.ConversionFunction);
  }

  return Result;
}

// Per 13.3.3.2p3, compare the given standard conversion sequences to
// determine if one is a proper subset of the other.
static ImplicitConversionSequence::CompareKind
compareStandardConversionSubsets(ASTContext &Context,
                                 const StandardConversionSequence& SCS1,
                                 const StandardConversionSequence& SCS2) {
  ImplicitConversionSequence::CompareKind Result
    = ImplicitConversionSequence::Indistinguishable;

  // the identity conversion sequence is considered to be a subsequence of
  // any non-identity conversion sequence
  if (SCS1.isIdentityConversion() && !SCS2.isIdentityConversion())
    return ImplicitConversionSequence::Better;
  else if (!SCS1.isIdentityConversion() && SCS2.isIdentityConversion())
    return ImplicitConversionSequence::Worse;

  if (SCS1.Second != SCS2.Second) {
    if (SCS1.Second == ICK_Identity)
      Result = ImplicitConversionSequence::Better;
    else if (SCS2.Second == ICK_Identity)
      Result = ImplicitConversionSequence::Worse;
    else
      return ImplicitConversionSequence::Indistinguishable;
  } else if (!Context.hasSimilarType(SCS1.getToType(1), SCS2.getToType(1)))
    return ImplicitConversionSequence::Indistinguishable;

  if (SCS1.Third == SCS2.Third) {
    return Context.hasSameType(SCS1.getToType(2), SCS2.getToType(2))? Result
                             : ImplicitConversionSequence::Indistinguishable;
  }

  if (SCS1.Third == ICK_Identity)
    return Result == ImplicitConversionSequence::Worse
             ? ImplicitConversionSequence::Indistinguishable
             : ImplicitConversionSequence::Better;

  if (SCS2.Third == ICK_Identity)
    return Result == ImplicitConversionSequence::Better
             ? ImplicitConversionSequence::Indistinguishable
             : ImplicitConversionSequence::Worse;

  return ImplicitConversionSequence::Indistinguishable;
}

/// Determine whether one of the given reference bindings is better
/// than the other based on what kind of bindings they are.
static bool
isBetterReferenceBindingKind(const StandardConversionSequence &SCS1,
                             const StandardConversionSequence &SCS2) {
  // C++0x [over.ics.rank]p3b4:
  //   -- S1 and S2 are reference bindings (8.5.3) and neither refers to an
  //      implicit object parameter of a non-static member function declared
  //      without a ref-qualifier, and *either* S1 binds an rvalue reference
  //      to an rvalue and S2 binds an lvalue reference *or S1 binds an
  //      lvalue reference to a function lvalue and S2 binds an rvalue
  //      reference*.
  //
  // FIXME: Rvalue references. We're going rogue with the above edits,
  // because the semantics in the current C++0x working paper (N3225 at the
  // time of this writing) break the standard definition of std::forward
  // and std::reference_wrapper when dealing with references to functions.
  // Proposed wording changes submitted to CWG for consideration.
  if (SCS1.BindsImplicitObjectArgumentWithoutRefQualifier ||
      SCS2.BindsImplicitObjectArgumentWithoutRefQualifier)
    return false;

  return (!SCS1.IsLvalueReference && SCS1.BindsToRvalue &&
          SCS2.IsLvalueReference) ||
         (SCS1.IsLvalueReference && SCS1.BindsToFunctionLvalue &&
          !SCS2.IsLvalueReference && SCS2.BindsToFunctionLvalue);
}

enum class FixedEnumPromotion {
  None,
  ToUnderlyingType,
  ToPromotedUnderlyingType
};

/// Returns kind of fixed enum promotion the \a SCS uses.
static FixedEnumPromotion
getFixedEnumPromtion(Sema &S, const StandardConversionSequence &SCS) {

  if (SCS.Second != ICK_Integral_Promotion)
    return FixedEnumPromotion::None;

  QualType FromType = SCS.getFromType();
  if (!FromType->isEnumeralType())
    return FixedEnumPromotion::None;

  EnumDecl *Enum = FromType->castAs<EnumType>()->getDecl();
  if (!Enum->isFixed())
    return FixedEnumPromotion::None;

  QualType UnderlyingType = Enum->getIntegerType();
  if (S.Context.hasSameType(SCS.getToType(1), UnderlyingType))
    return FixedEnumPromotion::ToUnderlyingType;

  return FixedEnumPromotion::ToPromotedUnderlyingType;
}

/// CompareStandardConversionSequences - Compare two standard
/// conversion sequences to determine whether one is better than the
/// other or if they are indistinguishable (C++ 13.3.3.2p3).
static ImplicitConversionSequence::CompareKind
CompareStandardConversionSequences(Sema &S, SourceLocation Loc,
                                   const StandardConversionSequence& SCS1,
                                   const StandardConversionSequence& SCS2)
{
  // Standard conversion sequence S1 is a better conversion sequence
  // than standard conversion sequence S2 if (C++ 13.3.3.2p3):

  //  -- S1 is a proper subsequence of S2 (comparing the conversion
  //     sequences in the canonical form defined by 13.3.3.1.1,
  //     excluding any Lvalue Transformation; the identity conversion
  //     sequence is considered to be a subsequence of any
  //     non-identity conversion sequence) or, if not that,
  if (ImplicitConversionSequence::CompareKind CK
        = compareStandardConversionSubsets(S.Context, SCS1, SCS2))
    return CK;

  //  -- the rank of S1 is better than the rank of S2 (by the rules
  //     defined below), or, if not that,
  ImplicitConversionRank Rank1 = SCS1.getRank();
  ImplicitConversionRank Rank2 = SCS2.getRank();
  if (Rank1 < Rank2)
    return ImplicitConversionSequence::Better;
  else if (Rank2 < Rank1)
    return ImplicitConversionSequence::Worse;

  // (C++ 13.3.3.2p4): Two conversion sequences with the same rank
  // are indistinguishable unless one of the following rules
  // applies:

  //   A conversion that is not a conversion of a pointer, or
  //   pointer to member, to bool is better than another conversion
  //   that is such a conversion.
  if (SCS1.isPointerConversionToBool() != SCS2.isPointerConversionToBool())
    return SCS2.isPointerConversionToBool()
             ? ImplicitConversionSequence::Better
             : ImplicitConversionSequence::Worse;

  // C++14 [over.ics.rank]p4b2:
  // This is retroactively applied to C++11 by CWG 1601.
  //
  //   A conversion that promotes an enumeration whose underlying type is fixed
  //   to its underlying type is better than one that promotes to the promoted
  //   underlying type, if the two are different.
  FixedEnumPromotion FEP1 = getFixedEnumPromtion(S, SCS1);
  FixedEnumPromotion FEP2 = getFixedEnumPromtion(S, SCS2);
  if (FEP1 != FixedEnumPromotion::None && FEP2 != FixedEnumPromotion::None &&
      FEP1 != FEP2)
    return FEP1 == FixedEnumPromotion::ToUnderlyingType
               ? ImplicitConversionSequence::Better
               : ImplicitConversionSequence::Worse;

  // C++ [over.ics.rank]p4b2:
  //
  //   If class B is derived directly or indirectly from class A,
  //   conversion of B* to A* is better than conversion of B* to
  //   void*, and conversion of A* to void* is better than conversion
  //   of B* to void*.
  bool SCS1ConvertsToVoid
    = SCS1.isPointerConversionToVoidPointer(S.Context);
  bool SCS2ConvertsToVoid
    = SCS2.isPointerConversionToVoidPointer(S.Context);
  if (SCS1ConvertsToVoid != SCS2ConvertsToVoid) {
    // Exactly one of the conversion sequences is a conversion to
    // a void pointer; it's the worse conversion.
    return SCS2ConvertsToVoid ? ImplicitConversionSequence::Better
                              : ImplicitConversionSequence::Worse;
  } else if (!SCS1ConvertsToVoid && !SCS2ConvertsToVoid) {
    // Neither conversion sequence converts to a void pointer; compare
    // their derived-to-base conversions.
    if (ImplicitConversionSequence::CompareKind DerivedCK
          = CompareDerivedToBaseConversions(S, Loc, SCS1, SCS2))
      return DerivedCK;
  } else if (SCS1ConvertsToVoid && SCS2ConvertsToVoid &&
             !S.Context.hasSameType(SCS1.getFromType(), SCS2.getFromType())) {
    // Both conversion sequences are conversions to void
    // pointers. Compare the source types to determine if there's an
    // inheritance relationship in their sources.
    QualType FromType1 = SCS1.getFromType();
    QualType FromType2 = SCS2.getFromType();

    // Adjust the types we're converting from via the array-to-pointer
    // conversion, if we need to.
    if (SCS1.First == ICK_Array_To_Pointer)
      FromType1 = S.Context.getArrayDecayedType(FromType1);
    if (SCS2.First == ICK_Array_To_Pointer)
      FromType2 = S.Context.getArrayDecayedType(FromType2);

    QualType FromPointee1 = FromType1->getPointeeType().getUnqualifiedType();
    QualType FromPointee2 = FromType2->getPointeeType().getUnqualifiedType();

    if (S.IsDerivedFrom(Loc, FromPointee2, FromPointee1))
      return ImplicitConversionSequence::Better;
    else if (S.IsDerivedFrom(Loc, FromPointee1, FromPointee2))
      return ImplicitConversionSequence::Worse;

    // Objective-C++: If one interface is more specific than the
    // other, it is the better one.
    const ObjCObjectPointerType* FromObjCPtr1
      = FromType1->getAs<ObjCObjectPointerType>();
    const ObjCObjectPointerType* FromObjCPtr2
      = FromType2->getAs<ObjCObjectPointerType>();
    if (FromObjCPtr1 && FromObjCPtr2) {
      bool AssignLeft = S.Context.canAssignObjCInterfaces(FromObjCPtr1,
                                                          FromObjCPtr2);
      bool AssignRight = S.Context.canAssignObjCInterfaces(FromObjCPtr2,
                                                           FromObjCPtr1);
      if (AssignLeft != AssignRight) {
        return AssignLeft? ImplicitConversionSequence::Better
                         : ImplicitConversionSequence::Worse;
      }
    }
  }

  if (SCS1.ReferenceBinding && SCS2.ReferenceBinding) {
    // Check for a better reference binding based on the kind of bindings.
    if (isBetterReferenceBindingKind(SCS1, SCS2))
      return ImplicitConversionSequence::Better;
    else if (isBetterReferenceBindingKind(SCS2, SCS1))
      return ImplicitConversionSequence::Worse;
  }

  // Compare based on qualification conversions (C++ 13.3.3.2p3,
  // bullet 3).
  if (ImplicitConversionSequence::CompareKind QualCK
        = CompareQualificationConversions(S, SCS1, SCS2))
    return QualCK;

  if (SCS1.ReferenceBinding && SCS2.ReferenceBinding) {
    // C++ [over.ics.rank]p3b4:
    //   -- S1 and S2 are reference bindings (8.5.3), and the types to
    //      which the references refer are the same type except for
    //      top-level cv-qualifiers, and the type to which the reference
    //      initialized by S2 refers is more cv-qualified than the type
    //      to which the reference initialized by S1 refers.
    QualType T1 = SCS1.getToType(2);
    QualType T2 = SCS2.getToType(2);
    T1 = S.Context.getCanonicalType(T1);
    T2 = S.Context.getCanonicalType(T2);
    Qualifiers T1Quals, T2Quals;
    QualType UnqualT1 = S.Context.getUnqualifiedArrayType(T1, T1Quals);
    QualType UnqualT2 = S.Context.getUnqualifiedArrayType(T2, T2Quals);
    if (UnqualT1 == UnqualT2) {
      // Objective-C++ ARC: If the references refer to objects with different
      // lifetimes, prefer bindings that don't change lifetime.
      if (SCS1.ObjCLifetimeConversionBinding !=
                                          SCS2.ObjCLifetimeConversionBinding) {
        return SCS1.ObjCLifetimeConversionBinding
                                           ? ImplicitConversionSequence::Worse
                                           : ImplicitConversionSequence::Better;
      }

      // If the type is an array type, promote the element qualifiers to the
      // type for comparison.
      if (isa<ArrayType>(T1) && T1Quals)
        T1 = S.Context.getQualifiedType(UnqualT1, T1Quals);
      if (isa<ArrayType>(T2) && T2Quals)
        T2 = S.Context.getQualifiedType(UnqualT2, T2Quals);
      if (T2.isMoreQualifiedThan(T1))
        return ImplicitConversionSequence::Better;
      if (T1.isMoreQualifiedThan(T2))
        return ImplicitConversionSequence::Worse;
    }
  }

  // In Microsoft mode (below 19.28), prefer an integral conversion to a
  // floating-to-integral conversion if the integral conversion
  // is between types of the same size.
  // For example:
  // void f(float);
  // void f(int);
  // int main {
  //    long a;
  //    f(a);
  // }
  // Here, MSVC will call f(int) instead of generating a compile error
  // as clang will do in standard mode.
  if (S.getLangOpts().MSVCCompat &&
      !S.getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2019_8) &&
      SCS1.Second == ICK_Integral_Conversion &&
      SCS2.Second == ICK_Floating_Integral &&
      S.Context.getTypeSize(SCS1.getFromType()) ==
          S.Context.getTypeSize(SCS1.getToType(2)))
    return ImplicitConversionSequence::Better;

  // Prefer a compatible vector conversion over a lax vector conversion
  // For example:
  //
  // typedef float __v4sf __attribute__((__vector_size__(16)));
  // void f(vector float);
  // void f(vector signed int);
  // int main() {
  //   __v4sf a;
  //   f(a);
  // }
  // Here, we'd like to choose f(vector float) and not
  // report an ambiguous call error
  if (SCS1.Second == ICK_Vector_Conversion &&
      SCS2.Second == ICK_Vector_Conversion) {
    bool SCS1IsCompatibleVectorConversion = S.Context.areCompatibleVectorTypes(
        SCS1.getFromType(), SCS1.getToType(2));
    bool SCS2IsCompatibleVectorConversion = S.Context.areCompatibleVectorTypes(
        SCS2.getFromType(), SCS2.getToType(2));

    if (SCS1IsCompatibleVectorConversion != SCS2IsCompatibleVectorConversion)
      return SCS1IsCompatibleVectorConversion
                 ? ImplicitConversionSequence::Better
                 : ImplicitConversionSequence::Worse;
  }

  if (SCS1.Second == ICK_SVE_Vector_Conversion &&
      SCS2.Second == ICK_SVE_Vector_Conversion) {
    bool SCS1IsCompatibleSVEVectorConversion =
        S.Context.areCompatibleSveTypes(SCS1.getFromType(), SCS1.getToType(2));
    bool SCS2IsCompatibleSVEVectorConversion =
        S.Context.areCompatibleSveTypes(SCS2.getFromType(), SCS2.getToType(2));

    if (SCS1IsCompatibleSVEVectorConversion !=
        SCS2IsCompatibleSVEVectorConversion)
      return SCS1IsCompatibleSVEVectorConversion
                 ? ImplicitConversionSequence::Better
                 : ImplicitConversionSequence::Worse;
  }

  if (SCS1.Second == ICK_RVV_Vector_Conversion &&
      SCS2.Second == ICK_RVV_Vector_Conversion) {
    bool SCS1IsCompatibleRVVVectorConversion =
        S.Context.areCompatibleRVVTypes(SCS1.getFromType(), SCS1.getToType(2));
    bool SCS2IsCompatibleRVVVectorConversion =
        S.Context.areCompatibleRVVTypes(SCS2.getFromType(), SCS2.getToType(2));

    if (SCS1IsCompatibleRVVVectorConversion !=
        SCS2IsCompatibleRVVVectorConversion)
      return SCS1IsCompatibleRVVVectorConversion
                 ? ImplicitConversionSequence::Better
                 : ImplicitConversionSequence::Worse;
  }
  return ImplicitConversionSequence::Indistinguishable;
}

/// CompareQualificationConversions - Compares two standard conversion
/// sequences to determine whether they can be ranked based on their
/// qualification conversions (C++ 13.3.3.2p3 bullet 3).
static ImplicitConversionSequence::CompareKind
CompareQualificationConversions(Sema &S,
                                const StandardConversionSequence& SCS1,
                                const StandardConversionSequence& SCS2) {
  // C++ [over.ics.rank]p3:
  //  -- S1 and S2 differ only in their qualification conversion and
  //     yield similar types T1 and T2 (C++ 4.4), respectively, [...]
  // [C++98]
  //     [...] and the cv-qualification signature of type T1 is a proper subset
  //     of the cv-qualification signature of type T2, and S1 is not the
  //     deprecated string literal array-to-pointer conversion (4.2).
  // [C++2a]
  //     [...] where T1 can be converted to T2 by a qualification conversion.
  if (SCS1.First != SCS2.First || SCS1.Second != SCS2.Second ||
      SCS1.Third != SCS2.Third || SCS1.Third != ICK_Qualification)
    return ImplicitConversionSequence::Indistinguishable;

  // FIXME: the example in the standard doesn't use a qualification
  // conversion (!)
  QualType T1 = SCS1.getToType(2);
  QualType T2 = SCS2.getToType(2);
  T1 = S.Context.getCanonicalType(T1);
  T2 = S.Context.getCanonicalType(T2);
  assert(!T1->isReferenceType() && !T2->isReferenceType());
  Qualifiers T1Quals, T2Quals;
  QualType UnqualT1 = S.Context.getUnqualifiedArrayType(T1, T1Quals);
  QualType UnqualT2 = S.Context.getUnqualifiedArrayType(T2, T2Quals);

  // If the types are the same, we won't learn anything by unwrapping
  // them.
  if (UnqualT1 == UnqualT2)
    return ImplicitConversionSequence::Indistinguishable;

  // Don't ever prefer a standard conversion sequence that uses the deprecated
  // string literal array to pointer conversion.
  bool CanPick1 = !SCS1.DeprecatedStringLiteralToCharPtr;
  bool CanPick2 = !SCS2.DeprecatedStringLiteralToCharPtr;

  // Objective-C++ ARC:
  //   Prefer qualification conversions not involving a change in lifetime
  //   to qualification conversions that do change lifetime.
  if (SCS1.QualificationIncludesObjCLifetime &&
      !SCS2.QualificationIncludesObjCLifetime)
    CanPick1 = false;
  if (SCS2.QualificationIncludesObjCLifetime &&
      !SCS1.QualificationIncludesObjCLifetime)
    CanPick2 = false;

  bool ObjCLifetimeConversion;
  if (CanPick1 &&
      !S.IsQualificationConversion(T1, T2, false, ObjCLifetimeConversion))
    CanPick1 = false;
  // FIXME: In Objective-C ARC, we can have qualification conversions in both
  // directions, so we can't short-cut this second check in general.
  if (CanPick2 &&
      !S.IsQualificationConversion(T2, T1, false, ObjCLifetimeConversion))
    CanPick2 = false;

  if (CanPick1 != CanPick2)
    return CanPick1 ? ImplicitConversionSequence::Better
                    : ImplicitConversionSequence::Worse;
  return ImplicitConversionSequence::Indistinguishable;
}

/// CompareDerivedToBaseConversions - Compares two standard conversion
/// sequences to determine whether they can be ranked based on their
/// various kinds of derived-to-base conversions (C++
/// [over.ics.rank]p4b3).  As part of these checks, we also look at
/// conversions between Objective-C interface types.
static ImplicitConversionSequence::CompareKind
CompareDerivedToBaseConversions(Sema &S, SourceLocation Loc,
                                const StandardConversionSequence& SCS1,
                                const StandardConversionSequence& SCS2) {
  QualType FromType1 = SCS1.getFromType();
  QualType ToType1 = SCS1.getToType(1);
  QualType FromType2 = SCS2.getFromType();
  QualType ToType2 = SCS2.getToType(1);

  // Adjust the types we're converting from via the array-to-pointer
  // conversion, if we need to.
  if (SCS1.First == ICK_Array_To_Pointer)
    FromType1 = S.Context.getArrayDecayedType(FromType1);
  if (SCS2.First == ICK_Array_To_Pointer)
    FromType2 = S.Context.getArrayDecayedType(FromType2);

  // Canonicalize all of the types.
  FromType1 = S.Context.getCanonicalType(FromType1);
  ToType1 = S.Context.getCanonicalType(ToType1);
  FromType2 = S.Context.getCanonicalType(FromType2);
  ToType2 = S.Context.getCanonicalType(ToType2);

  // C++ [over.ics.rank]p4b3:
  //
  //   If class B is derived directly or indirectly from class A and
  //   class C is derived directly or indirectly from B,
  //
  // Compare based on pointer conversions.
  if (SCS1.Second == ICK_Pointer_Conversion &&
      SCS2.Second == ICK_Pointer_Conversion &&
      /*FIXME: Remove if Objective-C id conversions get their own rank*/
      FromType1->isPointerType() && FromType2->isPointerType() &&
      ToType1->isPointerType() && ToType2->isPointerType()) {
    QualType FromPointee1 =
        FromType1->castAs<PointerType>()->getPointeeType().getUnqualifiedType();
    QualType ToPointee1 =
        ToType1->castAs<PointerType>()->getPointeeType().getUnqualifiedType();
    QualType FromPointee2 =
        FromType2->castAs<PointerType>()->getPointeeType().getUnqualifiedType();
    QualType ToPointee2 =
        ToType2->castAs<PointerType>()->getPointeeType().getUnqualifiedType();

    //   -- conversion of C* to B* is better than conversion of C* to A*,
    if (FromPointee1 == FromPointee2 && ToPointee1 != ToPointee2) {
      if (S.IsDerivedFrom(Loc, ToPointee1, ToPointee2))
        return ImplicitConversionSequence::Better;
      else if (S.IsDerivedFrom(Loc, ToPointee2, ToPointee1))
        return ImplicitConversionSequence::Worse;
    }

    //   -- conversion of B* to A* is better than conversion of C* to A*,
    if (FromPointee1 != FromPointee2 && ToPointee1 == ToPointee2) {
      if (S.IsDerivedFrom(Loc, FromPointee2, FromPointee1))
        return ImplicitConversionSequence::Better;
      else if (S.IsDerivedFrom(Loc, FromPointee1, FromPointee2))
        return ImplicitConversionSequence::Worse;
    }
  } else if (SCS1.Second == ICK_Pointer_Conversion &&
             SCS2.Second == ICK_Pointer_Conversion) {
    const ObjCObjectPointerType *FromPtr1
      = FromType1->getAs<ObjCObjectPointerType>();
    const ObjCObjectPointerType *FromPtr2
      = FromType2->getAs<ObjCObjectPointerType>();
    const ObjCObjectPointerType *ToPtr1
      = ToType1->getAs<ObjCObjectPointerType>();
    const ObjCObjectPointerType *ToPtr2
      = ToType2->getAs<ObjCObjectPointerType>();

    if (FromPtr1 && FromPtr2 && ToPtr1 && ToPtr2) {
      // Apply the same conversion ranking rules for Objective-C pointer types
      // that we do for C++ pointers to class types. However, we employ the
      // Objective-C pseudo-subtyping relationship used for assignment of
      // Objective-C pointer types.
      bool FromAssignLeft
        = S.Context.canAssignObjCInterfaces(FromPtr1, FromPtr2);
      bool FromAssignRight
        = S.Context.canAssignObjCInterfaces(FromPtr2, FromPtr1);
      bool ToAssignLeft
        = S.Context.canAssignObjCInterfaces(ToPtr1, ToPtr2);
      bool ToAssignRight
        = S.Context.canAssignObjCInterfaces(ToPtr2, ToPtr1);

      // A conversion to an a non-id object pointer type or qualified 'id'
      // type is better than a conversion to 'id'.
      if (ToPtr1->isObjCIdType() &&
          (ToPtr2->isObjCQualifiedIdType() || ToPtr2->getInterfaceDecl()))
        return ImplicitConversionSequence::Worse;
      if (ToPtr2->isObjCIdType() &&
          (ToPtr1->isObjCQualifiedIdType() || ToPtr1->getInterfaceDecl()))
        return ImplicitConversionSequence::Better;

      // A conversion to a non-id object pointer type is better than a
      // conversion to a qualified 'id' type
      if (ToPtr1->isObjCQualifiedIdType() && ToPtr2->getInterfaceDecl())
        return ImplicitConversionSequence::Worse;
      if (ToPtr2->isObjCQualifiedIdType() && ToPtr1->getInterfaceDecl())
        return ImplicitConversionSequence::Better;

      // A conversion to an a non-Class object pointer type or qualified 'Class'
      // type is better than a conversion to 'Class'.
      if (ToPtr1->isObjCClassType() &&
          (ToPtr2->isObjCQualifiedClassType() || ToPtr2->getInterfaceDecl()))
        return ImplicitConversionSequence::Worse;
      if (ToPtr2->isObjCClassType() &&
          (ToPtr1->isObjCQualifiedClassType() || ToPtr1->getInterfaceDecl()))
        return ImplicitConversionSequence::Better;

      // A conversion to a non-Class object pointer type is better than a
      // conversion to a qualified 'Class' type.
      if (ToPtr1->isObjCQualifiedClassType() && ToPtr2->getInterfaceDecl())
        return ImplicitConversionSequence::Worse;
      if (ToPtr2->isObjCQualifiedClassType() && ToPtr1->getInterfaceDecl())
        return ImplicitConversionSequence::Better;

      //   -- "conversion of C* to B* is better than conversion of C* to A*,"
      if (S.Context.hasSameType(FromType1, FromType2) &&
          !FromPtr1->isObjCIdType() && !FromPtr1->isObjCClassType() &&
          (ToAssignLeft != ToAssignRight)) {
        if (FromPtr1->isSpecialized()) {
          // "conversion of B<A> * to B * is better than conversion of B * to
          // C *.
          bool IsFirstSame =
              FromPtr1->getInterfaceDecl() == ToPtr1->getInterfaceDecl();
          bool IsSecondSame =
              FromPtr1->getInterfaceDecl() == ToPtr2->getInterfaceDecl();
          if (IsFirstSame) {
            if (!IsSecondSame)
              return ImplicitConversionSequence::Better;
          } else if (IsSecondSame)
            return ImplicitConversionSequence::Worse;
        }
        return ToAssignLeft? ImplicitConversionSequence::Worse
                           : ImplicitConversionSequence::Better;
      }

      //   -- "conversion of B* to A* is better than conversion of C* to A*,"
      if (S.Context.hasSameUnqualifiedType(ToType1, ToType2) &&
          (FromAssignLeft != FromAssignRight))
        return FromAssignLeft? ImplicitConversionSequence::Better
        : ImplicitConversionSequence::Worse;
    }
  }

  // Ranking of member-pointer types.
  if (SCS1.Second == ICK_Pointer_Member && SCS2.Second == ICK_Pointer_Member &&
      FromType1->isMemberPointerType() && FromType2->isMemberPointerType() &&
      ToType1->isMemberPointerType() && ToType2->isMemberPointerType()) {
    const auto *FromMemPointer1 = FromType1->castAs<MemberPointerType>();
    const auto *ToMemPointer1 = ToType1->castAs<MemberPointerType>();
    const auto *FromMemPointer2 = FromType2->castAs<MemberPointerType>();
    const auto *ToMemPointer2 = ToType2->castAs<MemberPointerType>();
    const Type *FromPointeeType1 = FromMemPointer1->getClass();
    const Type *ToPointeeType1 = ToMemPointer1->getClass();
    const Type *FromPointeeType2 = FromMemPointer2->getClass();
    const Type *ToPointeeType2 = ToMemPointer2->getClass();
    QualType FromPointee1 = QualType(FromPointeeType1, 0).getUnqualifiedType();
    QualType ToPointee1 = QualType(ToPointeeType1, 0).getUnqualifiedType();
    QualType FromPointee2 = QualType(FromPointeeType2, 0).getUnqualifiedType();
    QualType ToPointee2 = QualType(ToPointeeType2, 0).getUnqualifiedType();
    // conversion of A::* to B::* is better than conversion of A::* to C::*,
    if (FromPointee1 == FromPointee2 && ToPointee1 != ToPointee2) {
      if (S.IsDerivedFrom(Loc, ToPointee1, ToPointee2))
        return ImplicitConversionSequence::Worse;
      else if (S.IsDerivedFrom(Loc, ToPointee2, ToPointee1))
        return ImplicitConversionSequence::Better;
    }
    // conversion of B::* to C::* is better than conversion of A::* to C::*
    if (ToPointee1 == ToPointee2 && FromPointee1 != FromPointee2) {
      if (S.IsDerivedFrom(Loc, FromPointee1, FromPointee2))
        return ImplicitConversionSequence::Better;
      else if (S.IsDerivedFrom(Loc, FromPointee2, FromPointee1))
        return ImplicitConversionSequence::Worse;
    }
  }

  if (SCS1.Second == ICK_Derived_To_Base) {
    //   -- conversion of C to B is better than conversion of C to A,
    //   -- binding of an expression of type C to a reference of type
    //      B& is better than binding an expression of type C to a
    //      reference of type A&,
    if (S.Context.hasSameUnqualifiedType(FromType1, FromType2) &&
        !S.Context.hasSameUnqualifiedType(ToType1, ToType2)) {
      if (S.IsDerivedFrom(Loc, ToType1, ToType2))
        return ImplicitConversionSequence::Better;
      else if (S.IsDerivedFrom(Loc, ToType2, ToType1))
        return ImplicitConversionSequence::Worse;
    }

    //   -- conversion of B to A is better than conversion of C to A.
    //   -- binding of an expression of type B to a reference of type
    //      A& is better than binding an expression of type C to a
    //      reference of type A&,
    if (!S.Context.hasSameUnqualifiedType(FromType1, FromType2) &&
        S.Context.hasSameUnqualifiedType(ToType1, ToType2)) {
      if (S.IsDerivedFrom(Loc, FromType2, FromType1))
        return ImplicitConversionSequence::Better;
      else if (S.IsDerivedFrom(Loc, FromType1, FromType2))
        return ImplicitConversionSequence::Worse;
    }
  }

  return ImplicitConversionSequence::Indistinguishable;
}

static QualType withoutUnaligned(ASTContext &Ctx, QualType T) {
  if (!T.getQualifiers().hasUnaligned())
    return T;

  Qualifiers Q;
  T = Ctx.getUnqualifiedArrayType(T, Q);
  Q.removeUnaligned();
  return Ctx.getQualifiedType(T, Q);
}

Sema::ReferenceCompareResult
Sema::CompareReferenceRelationship(SourceLocation Loc,
                                   QualType OrigT1, QualType OrigT2,
                                   ReferenceConversions *ConvOut) {
  assert(!OrigT1->isReferenceType() &&
    "T1 must be the pointee type of the reference type");
  assert(!OrigT2->isReferenceType() && "T2 cannot be a reference type");

  QualType T1 = Context.getCanonicalType(OrigT1);
  QualType T2 = Context.getCanonicalType(OrigT2);
  Qualifiers T1Quals, T2Quals;
  QualType UnqualT1 = Context.getUnqualifiedArrayType(T1, T1Quals);
  QualType UnqualT2 = Context.getUnqualifiedArrayType(T2, T2Quals);

  ReferenceConversions ConvTmp;
  ReferenceConversions &Conv = ConvOut ? *ConvOut : ConvTmp;
  Conv = ReferenceConversions();

  // C++2a [dcl.init.ref]p4:
  //   Given types "cv1 T1" and "cv2 T2," "cv1 T1" is
  //   reference-related to "cv2 T2" if T1 is similar to T2, or
  //   T1 is a base class of T2.
  //   "cv1 T1" is reference-compatible with "cv2 T2" if
  //   a prvalue of type "pointer to cv2 T2" can be converted to the type
  //   "pointer to cv1 T1" via a standard conversion sequence.

  // Check for standard conversions we can apply to pointers: derived-to-base
  // conversions, ObjC pointer conversions, and function pointer conversions.
  // (Qualification conversions are checked last.)
  QualType ConvertedT2;
  if (UnqualT1 == UnqualT2) {
    // Nothing to do.
  } else if (isCompleteType(Loc, OrigT2) &&
             IsDerivedFrom(Loc, UnqualT2, UnqualT1))
    Conv |= ReferenceConversions::DerivedToBase;
  else if (UnqualT1->isObjCObjectOrInterfaceType() &&
           UnqualT2->isObjCObjectOrInterfaceType() &&
           Context.canBindObjCObjectType(UnqualT1, UnqualT2))
    Conv |= ReferenceConversions::ObjC;
  else if (UnqualT2->isFunctionType() &&
           IsFunctionConversion(UnqualT2, UnqualT1, ConvertedT2)) {
    Conv |= ReferenceConversions::Function;
    // No need to check qualifiers; function types don't have them.
    return Ref_Compatible;
  }
  bool ConvertedReferent = Conv != 0;

  // We can have a qualification conversion. Compute whether the types are
  // similar at the same time.
  bool PreviousToQualsIncludeConst = true;
  bool TopLevel = true;
  do {
    if (T1 == T2)
      break;

    // We will need a qualification conversion.
    Conv |= ReferenceConversions::Qualification;

    // Track whether we performed a qualification conversion anywhere other
    // than the top level. This matters for ranking reference bindings in
    // overload resolution.
    if (!TopLevel)
      Conv |= ReferenceConversions::NestedQualification;

    // MS compiler ignores __unaligned qualifier for references; do the same.
    T1 = withoutUnaligned(Context, T1);
    T2 = withoutUnaligned(Context, T2);

    // If we find a qualifier mismatch, the types are not reference-compatible,
    // but are still be reference-related if they're similar.
    bool ObjCLifetimeConversion = false;
    if (!isQualificationConversionStep(T2, T1, /*CStyle=*/false, TopLevel,
                                       PreviousToQualsIncludeConst,
                                       ObjCLifetimeConversion))
      return (ConvertedReferent || Context.hasSimilarType(T1, T2))
                 ? Ref_Related
                 : Ref_Incompatible;

    // FIXME: Should we track this for any level other than the first?
    if (ObjCLifetimeConversion)
      Conv |= ReferenceConversions::ObjCLifetime;

    TopLevel = false;
  } while (Context.UnwrapSimilarTypes(T1, T2));

  // At this point, if the types are reference-related, we must either have the
  // same inner type (ignoring qualifiers), or must have already worked out how
  // to convert the referent.
  return (ConvertedReferent || Context.hasSameUnqualifiedType(T1, T2))
             ? Ref_Compatible
             : Ref_Incompatible;
}

/// Look for a user-defined conversion to a value reference-compatible
///        with DeclType. Return true if something definite is found.
static bool
FindConversionForRefInit(Sema &S, ImplicitConversionSequence &ICS,
                         QualType DeclType, SourceLocation DeclLoc,
                         Expr *Init, QualType T2, bool AllowRvalues,
                         bool AllowExplicit) {
  assert(T2->isRecordType() && "Can only find conversions of record types.");
  auto *T2RecordDecl = cast<CXXRecordDecl>(T2->castAs<RecordType>()->getDecl());

  OverloadCandidateSet CandidateSet(
      DeclLoc, OverloadCandidateSet::CSK_InitByUserDefinedConversion);
  const auto &Conversions = T2RecordDecl->getVisibleConversionFunctions();
  for (auto I = Conversions.begin(), E = Conversions.end(); I != E; ++I) {
    NamedDecl *D = *I;
    CXXRecordDecl *ActingDC = cast<CXXRecordDecl>(D->getDeclContext());
    if (isa<UsingShadowDecl>(D))
      D = cast<UsingShadowDecl>(D)->getTargetDecl();

    FunctionTemplateDecl *ConvTemplate
      = dyn_cast<FunctionTemplateDecl>(D);
    CXXConversionDecl *Conv;
    if (ConvTemplate)
      Conv = cast<CXXConversionDecl>(ConvTemplate->getTemplatedDecl());
    else
      Conv = cast<CXXConversionDecl>(D);

    if (AllowRvalues) {
      // If we are initializing an rvalue reference, don't permit conversion
      // functions that return lvalues.
      if (!ConvTemplate && DeclType->isRValueReferenceType()) {
        const ReferenceType *RefType
          = Conv->getConversionType()->getAs<LValueReferenceType>();
        if (RefType && !RefType->getPointeeType()->isFunctionType())
          continue;
      }

      if (!ConvTemplate &&
          S.CompareReferenceRelationship(
              DeclLoc,
              Conv->getConversionType()
                  .getNonReferenceType()
                  .getUnqualifiedType(),
              DeclType.getNonReferenceType().getUnqualifiedType()) ==
              Sema::Ref_Incompatible)
        continue;
    } else {
      // If the conversion function doesn't return a reference type,
      // it can't be considered for this conversion. An rvalue reference
      // is only acceptable if its referencee is a function type.

      const ReferenceType *RefType =
        Conv->getConversionType()->getAs<ReferenceType>();
      if (!RefType ||
          (!RefType->isLValueReferenceType() &&
           !RefType->getPointeeType()->isFunctionType()))
        continue;
    }

    if (ConvTemplate)
      S.AddTemplateConversionCandidate(
          ConvTemplate, I.getPair(), ActingDC, Init, DeclType, CandidateSet,
          /*AllowObjCConversionOnExplicit=*/false, AllowExplicit);
    else
      S.AddConversionCandidate(
          Conv, I.getPair(), ActingDC, Init, DeclType, CandidateSet,
          /*AllowObjCConversionOnExplicit=*/false, AllowExplicit);
  }

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(S, DeclLoc, Best)) {
  case OR_Success:
    // C++ [over.ics.ref]p1:
    //
    //   [...] If the parameter binds directly to the result of
    //   applying a conversion function to the argument
    //   expression, the implicit conversion sequence is a
    //   user-defined conversion sequence (13.3.3.1.2), with the
    //   second standard conversion sequence either an identity
    //   conversion or, if the conversion function returns an
    //   entity of a type that is a derived class of the parameter
    //   type, a derived-to-base Conversion.
    if (!Best->FinalConversion.DirectBinding)
      return false;

    ICS.setUserDefined();
    ICS.UserDefined.Before = Best->Conversions[0].Standard;
    ICS.UserDefined.After = Best->FinalConversion;
    ICS.UserDefined.HadMultipleCandidates = HadMultipleCandidates;
    ICS.UserDefined.ConversionFunction = Best->Function;
    ICS.UserDefined.FoundConversionFunction = Best->FoundDecl;
    ICS.UserDefined.EllipsisConversion = false;
    assert(ICS.UserDefined.After.ReferenceBinding &&
           ICS.UserDefined.After.DirectBinding &&
           "Expected a direct reference binding!");
    return true;

  case OR_Ambiguous:
    ICS.setAmbiguous();
    for (OverloadCandidateSet::iterator Cand = CandidateSet.begin();
         Cand != CandidateSet.end(); ++Cand)
      if (Cand->Best)
        ICS.Ambiguous.addConversion(Cand->FoundDecl, Cand->Function);
    return true;

  case OR_No_Viable_Function:
  case OR_Deleted:
    // There was no suitable conversion, or we found a deleted
    // conversion; continue with other checks.
    return false;
  }

  llvm_unreachable("Invalid OverloadResult!");
}

/// Compute an implicit conversion sequence for reference
/// initialization.
static ImplicitConversionSequence
TryReferenceInit(Sema &S, Expr *Init, QualType DeclType,
                 SourceLocation DeclLoc,
                 bool SuppressUserConversions,
                 bool AllowExplicit) {
  assert(DeclType->isReferenceType() && "Reference init needs a reference");

  // Most paths end in a failed conversion.
  ImplicitConversionSequence ICS;
  ICS.setBad(BadConversionSequence::no_conversion, Init, DeclType);

  QualType T1 = DeclType->castAs<ReferenceType>()->getPointeeType();
  QualType T2 = Init->getType();

  // If the initializer is the address of an overloaded function, try
  // to resolve the overloaded function. If all goes well, T2 is the
  // type of the resulting function.
  if (S.Context.getCanonicalType(T2) == S.Context.OverloadTy) {
    DeclAccessPair Found;
    if (FunctionDecl *Fn = S.ResolveAddressOfOverloadedFunction(Init, DeclType,
                                                                false, Found))
      T2 = Fn->getType();
  }

  // Compute some basic properties of the types and the initializer.
  bool isRValRef = DeclType->isRValueReferenceType();
  Expr::Classification InitCategory = Init->Classify(S.Context);

  Sema::ReferenceConversions RefConv;
  Sema::ReferenceCompareResult RefRelationship =
      S.CompareReferenceRelationship(DeclLoc, T1, T2, &RefConv);

  auto SetAsReferenceBinding = [&](bool BindsDirectly) {
    ICS.setStandard();
    ICS.Standard.First = ICK_Identity;
    // FIXME: A reference binding can be a function conversion too. We should
    // consider that when ordering reference-to-function bindings.
    ICS.Standard.Second = (RefConv & Sema::ReferenceConversions::DerivedToBase)
                              ? ICK_Derived_To_Base
                              : (RefConv & Sema::ReferenceConversions::ObjC)
                                    ? ICK_Compatible_Conversion
                                    : ICK_Identity;
    ICS.Standard.Dimension = ICK_Identity;
    // FIXME: As a speculative fix to a defect introduced by CWG2352, we rank
    // a reference binding that performs a non-top-level qualification
    // conversion as a qualification conversion, not as an identity conversion.
    ICS.Standard.Third = (RefConv &
                              Sema::ReferenceConversions::NestedQualification)
                             ? ICK_Qualification
                             : ICK_Identity;
    ICS.Standard.setFromType(T2);
    ICS.Standard.setToType(0, T2);
    ICS.Standard.setToType(1, T1);
    ICS.Standard.setToType(2, T1);
    ICS.Standard.ReferenceBinding = true;
    ICS.Standard.DirectBinding = BindsDirectly;
    ICS.Standard.IsLvalueReference = !isRValRef;
    ICS.Standard.BindsToFunctionLvalue = T2->isFunctionType();
    ICS.Standard.BindsToRvalue = InitCategory.isRValue();
    ICS.Standard.BindsImplicitObjectArgumentWithoutRefQualifier = false;
    ICS.Standard.ObjCLifetimeConversionBinding =
        (RefConv & Sema::ReferenceConversions::ObjCLifetime) != 0;
    ICS.Standard.CopyConstructor = nullptr;
    ICS.Standard.DeprecatedStringLiteralToCharPtr = false;
  };

  // C++0x [dcl.init.ref]p5:
  //   A reference to type "cv1 T1" is initialized by an expression
  //   of type "cv2 T2" as follows:

  //     -- If reference is an lvalue reference and the initializer expression
  if (!isRValRef) {
    //     -- is an lvalue (but is not a bit-field), and "cv1 T1" is
    //        reference-compatible with "cv2 T2," or
    //
    // Per C++ [over.ics.ref]p4, we don't check the bit-field property here.
    if (InitCategory.isLValue() && RefRelationship == Sema::Ref_Compatible) {
      // C++ [over.ics.ref]p1:
      //   When a parameter of reference type binds directly (8.5.3)
      //   to an argument expression, the implicit conversion sequence
      //   is the identity conversion, unless the argument expression
      //   has a type that is a derived class of the parameter type,
      //   in which case the implicit conversion sequence is a
      //   derived-to-base Conversion (13.3.3.1).
      SetAsReferenceBinding(/*BindsDirectly=*/true);

      // Nothing more to do: the inaccessibility/ambiguity check for
      // derived-to-base conversions is suppressed when we're
      // computing the implicit conversion sequence (C++
      // [over.best.ics]p2).
      return ICS;
    }

    //       -- has a class type (i.e., T2 is a class type), where T1 is
    //          not reference-related to T2, and can be implicitly
    //          converted to an lvalue of type "cv3 T3," where "cv1 T1"
    //          is reference-compatible with "cv3 T3" 92) (this
    //          conversion is selected by enumerating the applicable
    //          conversion functions (13.3.1.6) and choosing the best
    //          one through overload resolution (13.3)),
    if (!SuppressUserConversions && T2->isRecordType() &&
        S.isCompleteType(DeclLoc, T2) &&
        RefRelationship == Sema::Ref_Incompatible) {
      if (FindConversionForRefInit(S, ICS, DeclType, DeclLoc,
                                   Init, T2, /*AllowRvalues=*/false,
                                   AllowExplicit))
        return ICS;
    }
  }

  //     -- Otherwise, the reference shall be an lvalue reference to a
  //        non-volatile const type (i.e., cv1 shall be const), or the reference
  //        shall be an rvalue reference.
  if (!isRValRef && (!T1.isConstQualified() || T1.isVolatileQualified())) {
    if (InitCategory.isRValue() && RefRelationship != Sema::Ref_Incompatible)
      ICS.setBad(BadConversionSequence::lvalue_ref_to_rvalue, Init, DeclType);
    return ICS;
  }

  //       -- If the initializer expression
  //
  //            -- is an xvalue, class prvalue, array prvalue or function
  //               lvalue and "cv1 T1" is reference-compatible with "cv2 T2", or
  if (RefRelationship == Sema::Ref_Compatible &&
      (InitCategory.isXValue() ||
       (InitCategory.isPRValue() &&
          (T2->isRecordType() || T2->isArrayType())) ||
       (InitCategory.isLValue() && T2->isFunctionType()))) {
    // In C++11, this is always a direct binding. In C++98/03, it's a direct
    // binding unless we're binding to a class prvalue.
    // Note: Although xvalues wouldn't normally show up in C++98/03 code, we
    // allow the use of rvalue references in C++98/03 for the benefit of
    // standard library implementors; therefore, we need the xvalue check here.
    SetAsReferenceBinding(/*BindsDirectly=*/S.getLangOpts().CPlusPlus11 ||
                          !(InitCategory.isPRValue() || T2->isRecordType()));
    return ICS;
  }

  //            -- has a class type (i.e., T2 is a class type), where T1 is not
  //               reference-related to T2, and can be implicitly converted to
  //               an xvalue, class prvalue, or function lvalue of type
  //               "cv3 T3", where "cv1 T1" is reference-compatible with
  //               "cv3 T3",
  //
  //          then the reference is bound to the value of the initializer
  //          expression in the first case and to the result of the conversion
  //          in the second case (or, in either case, to an appropriate base
  //          class subobject).
  if (!SuppressUserConversions && RefRelationship == Sema::Ref_Incompatible &&
      T2->isRecordType() && S.isCompleteType(DeclLoc, T2) &&
      FindConversionForRefInit(S, ICS, DeclType, DeclLoc,
                               Init, T2, /*AllowRvalues=*/true,
                               AllowExplicit)) {
    // In the second case, if the reference is an rvalue reference
    // and the second standard conversion sequence of the
    // user-defined conversion sequence includes an lvalue-to-rvalue
    // conversion, the program is ill-formed.
    if (ICS.isUserDefined() && isRValRef &&
        ICS.UserDefined.After.First == ICK_Lvalue_To_Rvalue)
      ICS.setBad(BadConversionSequence::no_conversion, Init, DeclType);

    return ICS;
  }

  // A temporary of function type cannot be created; don't even try.
  if (T1->isFunctionType())
    return ICS;

  //       -- Otherwise, a temporary of type "cv1 T1" is created and
  //          initialized from the initializer expression using the
  //          rules for a non-reference copy initialization (8.5). The
  //          reference is then bound to the temporary. If T1 is
  //          reference-related to T2, cv1 must be the same
  //          cv-qualification as, or greater cv-qualification than,
  //          cv2; otherwise, the program is ill-formed.
  if (RefRelationship == Sema::Ref_Related) {
    // If cv1 == cv2 or cv1 is a greater cv-qualified than cv2, then
    // we would be reference-compatible or reference-compatible with
    // added qualification. But that wasn't the case, so the reference
    // initialization fails.
    //
    // Note that we only want to check address spaces and cvr-qualifiers here.
    // ObjC GC, lifetime and unaligned qualifiers aren't important.
    Qualifiers T1Quals = T1.getQualifiers();
    Qualifiers T2Quals = T2.getQualifiers();
    T1Quals.removeObjCGCAttr();
    T1Quals.removeObjCLifetime();
    T2Quals.removeObjCGCAttr();
    T2Quals.removeObjCLifetime();
    // MS compiler ignores __unaligned qualifier for references; do the same.
    T1Quals.removeUnaligned();
    T2Quals.removeUnaligned();
    if (!T1Quals.compatiblyIncludes(T2Quals))
      return ICS;
  }

  // If at least one of the types is a class type, the types are not
  // related, and we aren't allowed any user conversions, the
  // reference binding fails. This case is important for breaking
  // recursion, since TryImplicitConversion below will attempt to
  // create a temporary through the use of a copy constructor.
  if (SuppressUserConversions && RefRelationship == Sema::Ref_Incompatible &&
      (T1->isRecordType() || T2->isRecordType()))
    return ICS;

  // If T1 is reference-related to T2 and the reference is an rvalue
  // reference, the initializer expression shall not be an lvalue.
  if (RefRelationship >= Sema::Ref_Related && isRValRef &&
      Init->Classify(S.Context).isLValue()) {
    ICS.setBad(BadConversionSequence::rvalue_ref_to_lvalue, Init, DeclType);
    return ICS;
  }

  // C++ [over.ics.ref]p2:
  //   When a parameter of reference type is not bound directly to
  //   an argument expression, the conversion sequence is the one
  //   required to convert the argument expression to the
  //   underlying type of the reference according to
  //   13.3.3.1. Conceptually, this conversion sequence corresponds
  //   to copy-initializing a temporary of the underlying type with
  //   the argument expression. Any difference in top-level
  //   cv-qualification is subsumed by the initialization itself
  //   and does not constitute a conversion.
  ICS = TryImplicitConversion(S, Init, T1, SuppressUserConversions,
                              AllowedExplicit::None,
                              /*InOverloadResolution=*/false,
                              /*CStyle=*/false,
                              /*AllowObjCWritebackConversion=*/false,
                              /*AllowObjCConversionOnExplicit=*/false);

  // Of course, that's still a reference binding.
  if (ICS.isStandard()) {
    ICS.Standard.ReferenceBinding = true;
    ICS.Standard.IsLvalueReference = !isRValRef;
    ICS.Standard.BindsToFunctionLvalue = false;
    ICS.Standard.BindsToRvalue = true;
    ICS.Standard.BindsImplicitObjectArgumentWithoutRefQualifier = false;
    ICS.Standard.ObjCLifetimeConversionBinding = false;
  } else if (ICS.isUserDefined()) {
    const ReferenceType *LValRefType =
        ICS.UserDefined.ConversionFunction->getReturnType()
            ->getAs<LValueReferenceType>();

    // C++ [over.ics.ref]p3:
    //   Except for an implicit object parameter, for which see 13.3.1, a
    //   standard conversion sequence cannot be formed if it requires [...]
    //   binding an rvalue reference to an lvalue other than a function
    //   lvalue.
    // Note that the function case is not possible here.
    if (isRValRef && LValRefType) {
      ICS.setBad(BadConversionSequence::no_conversion, Init, DeclType);
      return ICS;
    }

    ICS.UserDefined.After.ReferenceBinding = true;
    ICS.UserDefined.After.IsLvalueReference = !isRValRef;
    ICS.UserDefined.After.BindsToFunctionLvalue = false;
    ICS.UserDefined.After.BindsToRvalue = !LValRefType;
    ICS.UserDefined.After.BindsImplicitObjectArgumentWithoutRefQualifier = false;
    ICS.UserDefined.After.ObjCLifetimeConversionBinding = false;
  }

  return ICS;
}

static ImplicitConversionSequence
TryCopyInitialization(Sema &S, Expr *From, QualType ToType,
                      bool SuppressUserConversions,
                      bool InOverloadResolution,
                      bool AllowObjCWritebackConversion,
                      bool AllowExplicit = false);

/// TryListConversion - Try to copy-initialize a value of type ToType from the
/// initializer list From.
static ImplicitConversionSequence
TryListConversion(Sema &S, InitListExpr *From, QualType ToType,
                  bool SuppressUserConversions,
                  bool InOverloadResolution,
                  bool AllowObjCWritebackConversion) {
  // C++11 [over.ics.list]p1:
  //   When an argument is an initializer list, it is not an expression and
  //   special rules apply for converting it to a parameter type.

  ImplicitConversionSequence Result;
  Result.setBad(BadConversionSequence::no_conversion, From, ToType);

  // We need a complete type for what follows.  With one C++20 exception,
  // incomplete types can never be initialized from init lists.
  QualType InitTy = ToType;
  const ArrayType *AT = S.Context.getAsArrayType(ToType);
  if (AT && S.getLangOpts().CPlusPlus20)
    if (const auto *IAT = dyn_cast<IncompleteArrayType>(AT))
      // C++20 allows list initialization of an incomplete array type.
      InitTy = IAT->getElementType();
  if (!S.isCompleteType(From->getBeginLoc(), InitTy))
    return Result;

  // C++20 [over.ics.list]/2:
  //   If the initializer list is a designated-initializer-list, a conversion
  //   is only possible if the parameter has an aggregate type
  //
  // FIXME: The exception for reference initialization here is not part of the
  // language rules, but follow other compilers in adding it as a tentative DR
  // resolution.
  bool IsDesignatedInit = From->hasDesignatedInit();
  if (!ToType->isAggregateType() && !ToType->isReferenceType() &&
      IsDesignatedInit)
    return Result;

  // Per DR1467:
  //   If the parameter type is a class X and the initializer list has a single
  //   element of type cv U, where U is X or a class derived from X, the
  //   implicit conversion sequence is the one required to convert the element
  //   to the parameter type.
  //
  //   Otherwise, if the parameter type is a character array [... ]
  //   and the initializer list has a single element that is an
  //   appropriately-typed string literal (8.5.2 [dcl.init.string]), the
  //   implicit conversion sequence is the identity conversion.
  if (From->getNumInits() == 1 && !IsDesignatedInit) {
    if (ToType->isRecordType()) {
      QualType InitType = From->getInit(0)->getType();
      if (S.Context.hasSameUnqualifiedType(InitType, ToType) ||
          S.IsDerivedFrom(From->getBeginLoc(), InitType, ToType))
        return TryCopyInitialization(S, From->getInit(0), ToType,
                                     SuppressUserConversions,
                                     InOverloadResolution,
                                     AllowObjCWritebackConversion);
    }

    if (AT && S.IsStringInit(From->getInit(0), AT)) {
      InitializedEntity Entity =
          InitializedEntity::InitializeParameter(S.Context, ToType,
                                                 /*Consumed=*/false);
      if (S.CanPerformCopyInitialization(Entity, From)) {
        Result.setStandard();
        Result.Standard.setAsIdentityConversion();
        Result.Standard.setFromType(ToType);
        Result.Standard.setAllToTypes(ToType);
        return Result;
      }
    }
  }

  // C++14 [over.ics.list]p2: Otherwise, if the parameter type [...] (below).
  // C++11 [over.ics.list]p2:
  //   If the parameter type is std::initializer_list<X> or "array of X" and
  //   all the elements can be implicitly converted to X, the implicit
  //   conversion sequence is the worst conversion necessary to convert an
  //   element of the list to X.
  //
  // C++14 [over.ics.list]p3:
  //   Otherwise, if the parameter type is "array of N X", if the initializer
  //   list has exactly N elements or if it has fewer than N elements and X is
  //   default-constructible, and if all the elements of the initializer list
  //   can be implicitly converted to X, the implicit conversion sequence is
  //   the worst conversion necessary to convert an element of the list to X.
  if ((AT || S.isStdInitializerList(ToType, &InitTy)) && !IsDesignatedInit) {
    unsigned e = From->getNumInits();
    ImplicitConversionSequence DfltElt;
    DfltElt.setBad(BadConversionSequence::no_conversion, QualType(),
                   QualType());
    QualType ContTy = ToType;
    bool IsUnbounded = false;
    if (AT) {
      InitTy = AT->getElementType();
      if (ConstantArrayType const *CT = dyn_cast<ConstantArrayType>(AT)) {
        if (CT->getSize().ult(e)) {
          // Too many inits, fatally bad
          Result.setBad(BadConversionSequence::too_many_initializers, From,
                        ToType);
          Result.setInitializerListContainerType(ContTy, IsUnbounded);
          return Result;
        }
        if (CT->getSize().ugt(e)) {
          // Need an init from empty {}, is there one?
          InitListExpr EmptyList(S.Context, From->getEndLoc(), std::nullopt,
                                 From->getEndLoc());
          EmptyList.setType(S.Context.VoidTy);
          DfltElt = TryListConversion(
              S, &EmptyList, InitTy, SuppressUserConversions,
              InOverloadResolution, AllowObjCWritebackConversion);
          if (DfltElt.isBad()) {
            // No {} init, fatally bad
            Result.setBad(BadConversionSequence::too_few_initializers, From,
                          ToType);
            Result.setInitializerListContainerType(ContTy, IsUnbounded);
            return Result;
          }
        }
      } else {
        assert(isa<IncompleteArrayType>(AT) && "Expected incomplete array");
        IsUnbounded = true;
        if (!e) {
          // Cannot convert to zero-sized.
          Result.setBad(BadConversionSequence::too_few_initializers, From,
                        ToType);
          Result.setInitializerListContainerType(ContTy, IsUnbounded);
          return Result;
        }
        llvm::APInt Size(S.Context.getTypeSize(S.Context.getSizeType()), e);
        ContTy = S.Context.getConstantArrayType(InitTy, Size, nullptr,
                                                ArraySizeModifier::Normal, 0);
      }
    }

    Result.setStandard();
    Result.Standard.setAsIdentityConversion();
    Result.Standard.setFromType(InitTy);
    Result.Standard.setAllToTypes(InitTy);
    for (unsigned i = 0; i < e; ++i) {
      Expr *Init = From->getInit(i);
      ImplicitConversionSequence ICS = TryCopyInitialization(
          S, Init, InitTy, SuppressUserConversions, InOverloadResolution,
          AllowObjCWritebackConversion);

      // Keep the worse conversion seen so far.
      // FIXME: Sequences are not totally ordered, so 'worse' can be
      // ambiguous. CWG has been informed.
      if (CompareImplicitConversionSequences(S, From->getBeginLoc(), ICS,
                                             Result) ==
          ImplicitConversionSequence::Worse) {
        Result = ICS;
        // Bail as soon as we find something unconvertible.
        if (Result.isBad()) {
          Result.setInitializerListContainerType(ContTy, IsUnbounded);
          return Result;
        }
      }
    }

    // If we needed any implicit {} initialization, compare that now.
    // over.ics.list/6 indicates we should compare that conversion.  Again CWG
    // has been informed that this might not be the best thing.
    if (!DfltElt.isBad() && CompareImplicitConversionSequences(
                                S, From->getEndLoc(), DfltElt, Result) ==
                                ImplicitConversionSequence::Worse)
      Result = DfltElt;
    // Record the type being initialized so that we may compare sequences
    Result.setInitializerListContainerType(ContTy, IsUnbounded);
    return Result;
  }

  // C++14 [over.ics.list]p4:
  // C++11 [over.ics.list]p3:
  //   Otherwise, if the parameter is a non-aggregate class X and overload
  //   resolution chooses a single best constructor [...] the implicit
  //   conversion sequence is a user-defined conversion sequence. If multiple
  //   constructors are viable but none is better than the others, the
  //   implicit conversion sequence is a user-defined conversion sequence.
  if (ToType->isRecordType() && !ToType->isAggregateType()) {
    // This function can deal with initializer lists.
    return TryUserDefinedConversion(S, From, ToType, SuppressUserConversions,
                                    AllowedExplicit::None,
                                    InOverloadResolution, /*CStyle=*/false,
                                    AllowObjCWritebackConversion,
                                    /*AllowObjCConversionOnExplicit=*/false);
  }

  // C++14 [over.ics.list]p5:
  // C++11 [over.ics.list]p4:
  //   Otherwise, if the parameter has an aggregate type which can be
  //   initialized from the initializer list [...] the implicit conversion
  //   sequence is a user-defined conversion sequence.
  if (ToType->isAggregateType()) {
    // Type is an aggregate, argument is an init list. At this point it comes
    // down to checking whether the initialization works.
    // FIXME: Find out whether this parameter is consumed or not.
    InitializedEntity Entity =
        InitializedEntity::InitializeParameter(S.Context, ToType,
                                               /*Consumed=*/false);
    if (S.CanPerformAggregateInitializationForOverloadResolution(Entity,
                                                                 From)) {
      Result.setUserDefined();
      Result.UserDefined.Before.setAsIdentityConversion();
      // Initializer lists don't have a type.
      Result.UserDefined.Before.setFromType(QualType());
      Result.UserDefined.Before.setAllToTypes(QualType());

      Result.UserDefined.After.setAsIdentityConversion();
      Result.UserDefined.After.setFromType(ToType);
      Result.UserDefined.After.setAllToTypes(ToType);
      Result.UserDefined.ConversionFunction = nullptr;
    }
    return Result;
  }

  // C++14 [over.ics.list]p6:
  // C++11 [over.ics.list]p5:
  //   Otherwise, if the parameter is a reference, see 13.3.3.1.4.
  if (ToType->isReferenceType()) {
    // The standard is notoriously unclear here, since 13.3.3.1.4 doesn't
    // mention initializer lists in any way. So we go by what list-
    // initialization would do and try to extrapolate from that.

    QualType T1 = ToType->castAs<ReferenceType>()->getPointeeType();

    // If the initializer list has a single element that is reference-related
    // to the parameter type, we initialize the reference from that.
    if (From->getNumInits() == 1 && !IsDesignatedInit) {
      Expr *Init = From->getInit(0);

      QualType T2 = Init->getType();

      // If the initializer is the address of an overloaded function, try
      // to resolve the overloaded function. If all goes well, T2 is the
      // type of the resulting function.
      if (S.Context.getCanonicalType(T2) == S.Context.OverloadTy) {
        DeclAccessPair Found;
        if (FunctionDecl *Fn = S.ResolveAddressOfOverloadedFunction(
                                   Init, ToType, false, Found))
          T2 = Fn->getType();
      }

      // Compute some basic properties of the types and the initializer.
      Sema::ReferenceCompareResult RefRelationship =
          S.CompareReferenceRelationship(From->getBeginLoc(), T1, T2);

      if (RefRelationship >= Sema::Ref_Related) {
        return TryReferenceInit(S, Init, ToType, /*FIXME*/ From->getBeginLoc(),
                                SuppressUserConversions,
                                /*AllowExplicit=*/false);
      }
    }

    // Otherwise, we bind the reference to a temporary created from the
    // initializer list.
    Result = TryListConversion(S, From, T1, SuppressUserConversions,
                               InOverloadResolution,
                               AllowObjCWritebackConversion);
    if (Result.isFailure())
      return Result;
    assert(!Result.isEllipsis() &&
           "Sub-initialization cannot result in ellipsis conversion.");

    // Can we even bind to a temporary?
    if (ToType->isRValueReferenceType() ||
        (T1.isConstQualified() && !T1.isVolatileQualified())) {
      StandardConversionSequence &SCS = Result.isStandard() ? Result.Standard :
                                            Result.UserDefined.After;
      SCS.ReferenceBinding = true;
      SCS.IsLvalueReference = ToType->isLValueReferenceType();
      SCS.BindsToRvalue = true;
      SCS.BindsToFunctionLvalue = false;
      SCS.BindsImplicitObjectArgumentWithoutRefQualifier = false;
      SCS.ObjCLifetimeConversionBinding = false;
    } else
      Result.setBad(BadConversionSequence::lvalue_ref_to_rvalue,
                    From, ToType);
    return Result;
  }

  // C++14 [over.ics.list]p7:
  // C++11 [over.ics.list]p6:
  //   Otherwise, if the parameter type is not a class:
  if (!ToType->isRecordType()) {
    //    - if the initializer list has one element that is not itself an
    //      initializer list, the implicit conversion sequence is the one
    //      required to convert the element to the parameter type.
    unsigned NumInits = From->getNumInits();
    if (NumInits == 1 && !isa<InitListExpr>(From->getInit(0)))
      Result = TryCopyInitialization(S, From->getInit(0), ToType,
                                     SuppressUserConversions,
                                     InOverloadResolution,
                                     AllowObjCWritebackConversion);
    //    - if the initializer list has no elements, the implicit conversion
    //      sequence is the identity conversion.
    else if (NumInits == 0) {
      Result.setStandard();
      Result.Standard.setAsIdentityConversion();
      Result.Standard.setFromType(ToType);
      Result.Standard.setAllToTypes(ToType);
    }
    return Result;
  }

  // C++14 [over.ics.list]p8:
  // C++11 [over.ics.list]p7:
  //   In all cases other than those enumerated above, no conversion is possible
  return Result;
}

/// TryCopyInitialization - Try to copy-initialize a value of type
/// ToType from the expression From. Return the implicit conversion
/// sequence required to pass this argument, which may be a bad
/// conversion sequence (meaning that the argument cannot be passed to
/// a parameter of this type). If @p SuppressUserConversions, then we
/// do not permit any user-defined conversion sequences.
static ImplicitConversionSequence
TryCopyInitialization(Sema &S, Expr *From, QualType ToType,
                      bool SuppressUserConversions,
                      bool InOverloadResolution,
                      bool AllowObjCWritebackConversion,
                      bool AllowExplicit) {
  if (InitListExpr *FromInitList = dyn_cast<InitListExpr>(From))
    return TryListConversion(S, FromInitList, ToType, SuppressUserConversions,
                             InOverloadResolution,AllowObjCWritebackConversion);

  if (ToType->isReferenceType())
    return TryReferenceInit(S, From, ToType,
                            /*FIXME:*/ From->getBeginLoc(),
                            SuppressUserConversions, AllowExplicit);

  return TryImplicitConversion(S, From, ToType,
                               SuppressUserConversions,
                               AllowedExplicit::None,
                               InOverloadResolution,
                               /*CStyle=*/false,
                               AllowObjCWritebackConversion,
                               /*AllowObjCConversionOnExplicit=*/false);
}

static bool TryCopyInitialization(const CanQualType FromQTy,
                                  const CanQualType ToQTy,
                                  Sema &S,
                                  SourceLocation Loc,
                                  ExprValueKind FromVK) {
  OpaqueValueExpr TmpExpr(Loc, FromQTy, FromVK);
  ImplicitConversionSequence ICS =
    TryCopyInitialization(S, &TmpExpr, ToQTy, true, true, false);

  return !ICS.isBad();
}

/// TryObjectArgumentInitialization - Try to initialize the object
/// parameter of the given member function (@c Method) from the
/// expression @p From.
static ImplicitConversionSequence TryObjectArgumentInitialization(
    Sema &S, SourceLocation Loc, QualType FromType,
    Expr::Classification FromClassification, CXXMethodDecl *Method,
    const CXXRecordDecl *ActingContext, bool InOverloadResolution = false,
    QualType ExplicitParameterType = QualType(),
    bool SuppressUserConversion = false) {

  // We need to have an object of class type.
  if (const auto *PT = FromType->getAs<PointerType>()) {
    FromType = PT->getPointeeType();

    // When we had a pointer, it's implicitly dereferenced, so we
    // better have an lvalue.
    assert(FromClassification.isLValue());
  }

  auto ValueKindFromClassification = [](Expr::Classification C) {
    if (C.isPRValue())
      return clang::VK_PRValue;
    if (C.isXValue())
      return VK_XValue;
    return clang::VK_LValue;
  };

  if (Method->isExplicitObjectMemberFunction()) {
    if (ExplicitParameterType.isNull())
      ExplicitParameterType = Method->getFunctionObjectParameterReferenceType();
    OpaqueValueExpr TmpExpr(Loc, FromType.getNonReferenceType(),
                            ValueKindFromClassification(FromClassification));
    ImplicitConversionSequence ICS = TryCopyInitialization(
        S, &TmpExpr, ExplicitParameterType, SuppressUserConversion,
        /*InOverloadResolution=*/true, false);
    if (ICS.isBad())
      ICS.Bad.FromExpr = nullptr;
    return ICS;
  }

  assert(FromType->isRecordType());

  QualType ClassType = S.Context.getTypeDeclType(ActingContext);
  // C++98 [class.dtor]p2:
  //   A destructor can be invoked for a const, volatile or const volatile
  //   object.
  // C++98 [over.match.funcs]p4:
  //   For static member functions, the implicit object parameter is considered
  //   to match any object (since if the function is selected, the object is
  //   discarded).
  Qualifiers Quals = Method->getMethodQualifiers();
  if (isa<CXXDestructorDecl>(Method) || Method->isStatic()) {
    Quals.addConst();
    Quals.addVolatile();
  }

  QualType ImplicitParamType = S.Context.getQualifiedType(ClassType, Quals);

  // Set up the conversion sequence as a "bad" conversion, to allow us
  // to exit early.
  ImplicitConversionSequence ICS;

  // C++0x [over.match.funcs]p4:
  //   For non-static member functions, the type of the implicit object
  //   parameter is
  //
  //     - "lvalue reference to cv X" for functions declared without a
  //        ref-qualifier or with the & ref-qualifier
  //     - "rvalue reference to cv X" for functions declared with the &&
  //        ref-qualifier
  //
  // where X is the class of which the function is a member and cv is the
  // cv-qualification on the member function declaration.
  //
  // However, when finding an implicit conversion sequence for the argument, we
  // are not allowed to perform user-defined conversions
  // (C++ [over.match.funcs]p5). We perform a simplified version of
  // reference binding here, that allows class rvalues to bind to
  // non-constant references.

  // First check the qualifiers.
  QualType FromTypeCanon = S.Context.getCanonicalType(FromType);
  // MSVC ignores __unaligned qualifier for overload candidates; do the same.
  if (ImplicitParamType.getCVRQualifiers() !=
          FromTypeCanon.getLocalCVRQualifiers() &&
      !ImplicitParamType.isAtLeastAsQualifiedAs(
          withoutUnaligned(S.Context, FromTypeCanon))) {
    ICS.setBad(BadConversionSequence::bad_qualifiers,
               FromType, ImplicitParamType);
    return ICS;
  }

  if (FromTypeCanon.hasAddressSpace()) {
    Qualifiers QualsImplicitParamType = ImplicitParamType.getQualifiers();
    Qualifiers QualsFromType = FromTypeCanon.getQualifiers();
    if (!QualsImplicitParamType.isAddressSpaceSupersetOf(QualsFromType)) {
      ICS.setBad(BadConversionSequence::bad_qualifiers,
                 FromType, ImplicitParamType);
      return ICS;
    }
  }

  // Check that we have either the same type or a derived type. It
  // affects the conversion rank.
  QualType ClassTypeCanon = S.Context.getCanonicalType(ClassType);
  ImplicitConversionKind SecondKind;
  if (ClassTypeCanon == FromTypeCanon.getLocalUnqualifiedType()) {
    SecondKind = ICK_Identity;
  } else if (S.IsDerivedFrom(Loc, FromType, ClassType)) {
    SecondKind = ICK_Derived_To_Base;
  } else if (!Method->isExplicitObjectMemberFunction()) {
    ICS.setBad(BadConversionSequence::unrelated_class,
               FromType, ImplicitParamType);
    return ICS;
  }

  // Check the ref-qualifier.
  switch (Method->getRefQualifier()) {
  case RQ_None:
    // Do nothing; we don't care about lvalueness or rvalueness.
    break;

  case RQ_LValue:
    if (!FromClassification.isLValue() && !Quals.hasOnlyConst()) {
      // non-const lvalue reference cannot bind to an rvalue
      ICS.setBad(BadConversionSequence::lvalue_ref_to_rvalue, FromType,
                 ImplicitParamType);
      return ICS;
    }
    break;

  case RQ_RValue:
    if (!FromClassification.isRValue()) {
      // rvalue reference cannot bind to an lvalue
      ICS.setBad(BadConversionSequence::rvalue_ref_to_lvalue, FromType,
                 ImplicitParamType);
      return ICS;
    }
    break;
  }

  // Success. Mark this as a reference binding.
  ICS.setStandard();
  ICS.Standard.setAsIdentityConversion();
  ICS.Standard.Second = SecondKind;
  ICS.Standard.setFromType(FromType);
  ICS.Standard.setAllToTypes(ImplicitParamType);
  ICS.Standard.ReferenceBinding = true;
  ICS.Standard.DirectBinding = true;
  ICS.Standard.IsLvalueReference = Method->getRefQualifier() != RQ_RValue;
  ICS.Standard.BindsToFunctionLvalue = false;
  ICS.Standard.BindsToRvalue = FromClassification.isRValue();
  ICS.Standard.BindsImplicitObjectArgumentWithoutRefQualifier
    = (Method->getRefQualifier() == RQ_None);
  return ICS;
}

/// PerformObjectArgumentInitialization - Perform initialization of
/// the implicit object parameter for the given Method with the given
/// expression.
ExprResult Sema::PerformImplicitObjectArgumentInitialization(
    Expr *From, NestedNameSpecifier *Qualifier, NamedDecl *FoundDecl,
    CXXMethodDecl *Method) {
  QualType FromRecordType, DestType;
  QualType ImplicitParamRecordType = Method->getFunctionObjectParameterType();

  Expr::Classification FromClassification;
  if (const PointerType *PT = From->getType()->getAs<PointerType>()) {
    FromRecordType = PT->getPointeeType();
    DestType = Method->getThisType();
    FromClassification = Expr::Classification::makeSimpleLValue();
  } else {
    FromRecordType = From->getType();
    DestType = ImplicitParamRecordType;
    FromClassification = From->Classify(Context);

    // When performing member access on a prvalue, materialize a temporary.
    if (From->isPRValue()) {
      From = CreateMaterializeTemporaryExpr(FromRecordType, From,
                                            Method->getRefQualifier() !=
                                                RefQualifierKind::RQ_RValue);
    }
  }

  // Note that we always use the true parent context when performing
  // the actual argument initialization.
  ImplicitConversionSequence ICS = TryObjectArgumentInitialization(
      *this, From->getBeginLoc(), From->getType(), FromClassification, Method,
      Method->getParent());
  if (ICS.isBad()) {
    switch (ICS.Bad.Kind) {
    case BadConversionSequence::bad_qualifiers: {
      Qualifiers FromQs = FromRecordType.getQualifiers();
      Qualifiers ToQs = DestType.getQualifiers();
      unsigned CVR = FromQs.getCVRQualifiers() & ~ToQs.getCVRQualifiers();
      if (CVR) {
        Diag(From->getBeginLoc(), diag::err_member_function_call_bad_cvr)
            << Method->getDeclName() << FromRecordType << (CVR - 1)
            << From->getSourceRange();
        Diag(Method->getLocation(), diag::note_previous_decl)
          << Method->getDeclName();
        return ExprError();
      }
      break;
    }

    case BadConversionSequence::lvalue_ref_to_rvalue:
    case BadConversionSequence::rvalue_ref_to_lvalue: {
      bool IsRValueQualified =
        Method->getRefQualifier() == RefQualifierKind::RQ_RValue;
      Diag(From->getBeginLoc(), diag::err_member_function_call_bad_ref)
          << Method->getDeclName() << FromClassification.isRValue()
          << IsRValueQualified;
      Diag(Method->getLocation(), diag::note_previous_decl)
        << Method->getDeclName();
      return ExprError();
    }

    case BadConversionSequence::no_conversion:
    case BadConversionSequence::unrelated_class:
      break;

    case BadConversionSequence::too_few_initializers:
    case BadConversionSequence::too_many_initializers:
      llvm_unreachable("Lists are not objects");
    }

    return Diag(From->getBeginLoc(), diag::err_member_function_call_bad_type)
           << ImplicitParamRecordType << FromRecordType
           << From->getSourceRange();
  }

  if (ICS.Standard.Second == ICK_Derived_To_Base) {
    ExprResult FromRes =
      PerformObjectMemberConversion(From, Qualifier, FoundDecl, Method);
    if (FromRes.isInvalid())
      return ExprError();
    From = FromRes.get();
  }

  if (!Context.hasSameType(From->getType(), DestType)) {
    CastKind CK;
    QualType PteeTy = DestType->getPointeeType();
    LangAS DestAS =
        PteeTy.isNull() ? DestType.getAddressSpace() : PteeTy.getAddressSpace();
    if (FromRecordType.getAddressSpace() != DestAS)
      CK = CK_AddressSpaceConversion;
    else
      CK = CK_NoOp;
    From = ImpCastExprToType(From, DestType, CK, From->getValueKind()).get();
  }
  return From;
}

/// TryContextuallyConvertToBool - Attempt to contextually convert the
/// expression From to bool (C++0x [conv]p3).
static ImplicitConversionSequence
TryContextuallyConvertToBool(Sema &S, Expr *From) {
  // C++ [dcl.init]/17.8:
  //   - Otherwise, if the initialization is direct-initialization, the source
  //     type is std::nullptr_t, and the destination type is bool, the initial
  //     value of the object being initialized is false.
  if (From->getType()->isNullPtrType())
    return ImplicitConversionSequence::getNullptrToBool(From->getType(),
                                                        S.Context.BoolTy,
                                                        From->isGLValue());

  // All other direct-initialization of bool is equivalent to an implicit
  // conversion to bool in which explicit conversions are permitted.
  return TryImplicitConversion(S, From, S.Context.BoolTy,
                               /*SuppressUserConversions=*/false,
                               AllowedExplicit::Conversions,
                               /*InOverloadResolution=*/false,
                               /*CStyle=*/false,
                               /*AllowObjCWritebackConversion=*/false,
                               /*AllowObjCConversionOnExplicit=*/false);
}

ExprResult Sema::PerformContextuallyConvertToBool(Expr *From) {
  if (checkPlaceholderForOverload(*this, From))
    return ExprError();

  ImplicitConversionSequence ICS = TryContextuallyConvertToBool(*this, From);
  if (!ICS.isBad())
    return PerformImplicitConversion(From, Context.BoolTy, ICS, AA_Converting);

  if (!DiagnoseMultipleUserDefinedConversion(From, Context.BoolTy))
    return Diag(From->getBeginLoc(), diag::err_typecheck_bool_condition)
           << From->getType() << From->getSourceRange();
  return ExprError();
}

/// Check that the specified conversion is permitted in a converted constant
/// expression, according to C++11 [expr.const]p3. Return true if the conversion
/// is acceptable.
static bool CheckConvertedConstantConversions(Sema &S,
                                              StandardConversionSequence &SCS) {
  // Since we know that the target type is an integral or unscoped enumeration
  // type, most conversion kinds are impossible. All possible First and Third
  // conversions are fine.
  switch (SCS.Second) {
  case ICK_Identity:
  case ICK_Integral_Promotion:
  case ICK_Integral_Conversion: // Narrowing conversions are checked elsewhere.
  case ICK_Zero_Queue_Conversion:
    return true;

  case ICK_Boolean_Conversion:
    // Conversion from an integral or unscoped enumeration type to bool is
    // classified as ICK_Boolean_Conversion, but it's also arguably an integral
    // conversion, so we allow it in a converted constant expression.
    //
    // FIXME: Per core issue 1407, we should not allow this, but that breaks
    // a lot of popular code. We should at least add a warning for this
    // (non-conforming) extension.
    return SCS.getFromType()->isIntegralOrUnscopedEnumerationType() &&
           SCS.getToType(2)->isBooleanType();

  case ICK_Pointer_Conversion:
  case ICK_Pointer_Member:
    // C++1z: null pointer conversions and null member pointer conversions are
    // only permitted if the source type is std::nullptr_t.
    return SCS.getFromType()->isNullPtrType();

  case ICK_Floating_Promotion:
  case ICK_Complex_Promotion:
  case ICK_Floating_Conversion:
  case ICK_Complex_Conversion:
  case ICK_Floating_Integral:
  case ICK_Compatible_Conversion:
  case ICK_Derived_To_Base:
  case ICK_Vector_Conversion:
  case ICK_SVE_Vector_Conversion:
  case ICK_RVV_Vector_Conversion:
  case ICK_HLSL_Vector_Splat:
  case ICK_Vector_Splat:
  case ICK_Complex_Real:
  case ICK_Block_Pointer_Conversion:
  case ICK_TransparentUnionConversion:
  case ICK_Writeback_Conversion:
  case ICK_Zero_Event_Conversion:
  case ICK_C_Only_Conversion:
  case ICK_Incompatible_Pointer_Conversion:
  case ICK_Fixed_Point_Conversion:
  case ICK_HLSL_Vector_Truncation:
    return false;

  case ICK_Lvalue_To_Rvalue:
  case ICK_Array_To_Pointer:
  case ICK_Function_To_Pointer:
  case ICK_HLSL_Array_RValue:
    llvm_unreachable("found a first conversion kind in Second");

  case ICK_Function_Conversion:
  case ICK_Qualification:
    llvm_unreachable("found a third conversion kind in Second");

  case ICK_Num_Conversion_Kinds:
    break;
  }

  llvm_unreachable("unknown conversion kind");
}

/// BuildConvertedConstantExpression - Check that the expression From is a
/// converted constant expression of type T, perform the conversion but
/// does not evaluate the expression
static ExprResult BuildConvertedConstantExpression(Sema &S, Expr *From,
                                                   QualType T,
                                                   Sema::CCEKind CCE,
                                                   NamedDecl *Dest,
                                                   APValue &PreNarrowingValue) {
  assert(S.getLangOpts().CPlusPlus11 &&
         "converted constant expression outside C++11");

  if (checkPlaceholderForOverload(S, From))
    return ExprError();

  // C++1z [expr.const]p3:
  //  A converted constant expression of type T is an expression,
  //  implicitly converted to type T, where the converted
  //  expression is a constant expression and the implicit conversion
  //  sequence contains only [... list of conversions ...].
  ImplicitConversionSequence ICS =
      (CCE == Sema::CCEK_ExplicitBool || CCE == Sema::CCEK_Noexcept)
          ? TryContextuallyConvertToBool(S, From)
          : TryCopyInitialization(S, From, T,
                                  /*SuppressUserConversions=*/false,
                                  /*InOverloadResolution=*/false,
                                  /*AllowObjCWritebackConversion=*/false,
                                  /*AllowExplicit=*/false);
  StandardConversionSequence *SCS = nullptr;
  switch (ICS.getKind()) {
  case ImplicitConversionSequence::StandardConversion:
    SCS = &ICS.Standard;
    break;
  case ImplicitConversionSequence::UserDefinedConversion:
    if (T->isRecordType())
      SCS = &ICS.UserDefined.Before;
    else
      SCS = &ICS.UserDefined.After;
    break;
  case ImplicitConversionSequence::AmbiguousConversion:
  case ImplicitConversionSequence::BadConversion:
    if (!S.DiagnoseMultipleUserDefinedConversion(From, T))
      return S.Diag(From->getBeginLoc(),
                    diag::err_typecheck_converted_constant_expression)
             << From->getType() << From->getSourceRange() << T;
    return ExprError();

  case ImplicitConversionSequence::EllipsisConversion:
  case ImplicitConversionSequence::StaticObjectArgumentConversion:
    llvm_unreachable("bad conversion in converted constant expression");
  }

  // Check that we would only use permitted conversions.
  if (!CheckConvertedConstantConversions(S, *SCS)) {
    return S.Diag(From->getBeginLoc(),
                  diag::err_typecheck_converted_constant_expression_disallowed)
           << From->getType() << From->getSourceRange() << T;
  }
  // [...] and where the reference binding (if any) binds directly.
  if (SCS->ReferenceBinding && !SCS->DirectBinding) {
    return S.Diag(From->getBeginLoc(),
                  diag::err_typecheck_converted_constant_expression_indirect)
           << From->getType() << From->getSourceRange() << T;
  }
  // 'TryCopyInitialization' returns incorrect info for attempts to bind
  // a reference to a bit-field due to C++ [over.ics.ref]p4. Namely,
  // 'SCS->DirectBinding' occurs to be set to 'true' despite it is not
  // the direct binding according to C++ [dcl.init.ref]p5. Hence, check this
  // case explicitly.
  if (From->refersToBitField() && T.getTypePtr()->isReferenceType()) {
    return S.Diag(From->getBeginLoc(),
                  diag::err_reference_bind_to_bitfield_in_cce)
           << From->getSourceRange();
  }

  // Usually we can simply apply the ImplicitConversionSequence we formed
  // earlier, but that's not guaranteed to work when initializing an object of
  // class type.
  ExprResult Result;
  if (T->isRecordType()) {
    assert(CCE == Sema::CCEK_TemplateArg &&
           "unexpected class type converted constant expr");
    Result = S.PerformCopyInitialization(
        InitializedEntity::InitializeTemplateParameter(
            T, cast<NonTypeTemplateParmDecl>(Dest)),
        SourceLocation(), From);
  } else {
    Result = S.PerformImplicitConversion(From, T, ICS, Sema::AA_Converting);
  }
  if (Result.isInvalid())
    return Result;

  // C++2a [intro.execution]p5:
  //   A full-expression is [...] a constant-expression [...]
  Result = S.ActOnFinishFullExpr(Result.get(), From->getExprLoc(),
                                 /*DiscardedValue=*/false, /*IsConstexpr=*/true,
                                 CCE == Sema::CCEKind::CCEK_TemplateArg);
  if (Result.isInvalid())
    return Result;

  // Check for a narrowing implicit conversion.
  bool ReturnPreNarrowingValue = false;
  QualType PreNarrowingType;
  switch (SCS->getNarrowingKind(S.Context, Result.get(), PreNarrowingValue,
                                PreNarrowingType)) {
  case NK_Dependent_Narrowing:
    // Implicit conversion to a narrower type, but the expression is
    // value-dependent so we can't tell whether it's actually narrowing.
  case NK_Variable_Narrowing:
    // Implicit conversion to a narrower type, and the value is not a constant
    // expression. We'll diagnose this in a moment.
  case NK_Not_Narrowing:
    break;

  case NK_Constant_Narrowing:
    if (CCE == Sema::CCEK_ArrayBound &&
        PreNarrowingType->isIntegralOrEnumerationType() &&
        PreNarrowingValue.isInt()) {
      // Don't diagnose array bound narrowing here; we produce more precise
      // errors by allowing the un-narrowed value through.
      ReturnPreNarrowingValue = true;
      break;
    }
    S.Diag(From->getBeginLoc(), diag::ext_cce_narrowing)
        << CCE << /*Constant*/ 1
        << PreNarrowingValue.getAsString(S.Context, PreNarrowingType) << T;
    break;

  case NK_Type_Narrowing:
    // FIXME: It would be better to diagnose that the expression is not a
    // constant expression.
    S.Diag(From->getBeginLoc(), diag::ext_cce_narrowing)
        << CCE << /*Constant*/ 0 << From->getType() << T;
    break;
  }
  if (!ReturnPreNarrowingValue)
    PreNarrowingValue = {};

  return Result;
}

/// CheckConvertedConstantExpression - Check that the expression From is a
/// converted constant expression of type T, perform the conversion and produce
/// the converted expression, per C++11 [expr.const]p3.
static ExprResult CheckConvertedConstantExpression(Sema &S, Expr *From,
                                                   QualType T, APValue &Value,
                                                   Sema::CCEKind CCE,
                                                   bool RequireInt,
                                                   NamedDecl *Dest) {

  APValue PreNarrowingValue;
  ExprResult Result = BuildConvertedConstantExpression(S, From, T, CCE, Dest,
                                                       PreNarrowingValue);
  if (Result.isInvalid() || Result.get()->isValueDependent()) {
    Value = APValue();
    return Result;
  }
  return S.EvaluateConvertedConstantExpression(Result.get(), T, Value, CCE,
                                               RequireInt, PreNarrowingValue);
}

ExprResult Sema::BuildConvertedConstantExpression(Expr *From, QualType T,
                                                  CCEKind CCE,
                                                  NamedDecl *Dest) {
  APValue PreNarrowingValue;
  return ::BuildConvertedConstantExpression(*this, From, T, CCE, Dest,
                                            PreNarrowingValue);
}

ExprResult Sema::CheckConvertedConstantExpression(Expr *From, QualType T,
                                                  APValue &Value, CCEKind CCE,
                                                  NamedDecl *Dest) {
  return ::CheckConvertedConstantExpression(*this, From, T, Value, CCE, false,
                                            Dest);
}

ExprResult Sema::CheckConvertedConstantExpression(Expr *From, QualType T,
                                                  llvm::APSInt &Value,
                                                  CCEKind CCE) {
  assert(T->isIntegralOrEnumerationType() && "unexpected converted const type");

  APValue V;
  auto R = ::CheckConvertedConstantExpression(*this, From, T, V, CCE, true,
                                              /*Dest=*/nullptr);
  if (!R.isInvalid() && !R.get()->isValueDependent())
    Value = V.getInt();
  return R;
}

ExprResult
Sema::EvaluateConvertedConstantExpression(Expr *E, QualType T, APValue &Value,
                                          Sema::CCEKind CCE, bool RequireInt,
                                          const APValue &PreNarrowingValue) {

  ExprResult Result = E;
  // Check the expression is a constant expression.
  SmallVector<PartialDiagnosticAt, 8> Notes;
  Expr::EvalResult Eval;
  Eval.Diag = &Notes;

  ConstantExprKind Kind;
  if (CCE == Sema::CCEK_TemplateArg && T->isRecordType())
    Kind = ConstantExprKind::ClassTemplateArgument;
  else if (CCE == Sema::CCEK_TemplateArg)
    Kind = ConstantExprKind::NonClassTemplateArgument;
  else
    Kind = ConstantExprKind::Normal;

  if (!E->EvaluateAsConstantExpr(Eval, Context, Kind) ||
      (RequireInt && !Eval.Val.isInt())) {
    // The expression can't be folded, so we can't keep it at this position in
    // the AST.
    Result = ExprError();
  } else {
    Value = Eval.Val;

    if (Notes.empty()) {
      // It's a constant expression.
      Expr *E = Result.get();
      if (const auto *CE = dyn_cast<ConstantExpr>(E)) {
        // We expect a ConstantExpr to have a value associated with it
        // by this point.
        assert(CE->getResultStorageKind() != ConstantResultStorageKind::None &&
               "ConstantExpr has no value associated with it");
        (void)CE;
      } else {
        E = ConstantExpr::Create(Context, Result.get(), Value);
      }
      if (!PreNarrowingValue.isAbsent())
        Value = std::move(PreNarrowingValue);
      return E;
    }
  }

  // It's not a constant expression. Produce an appropriate diagnostic.
  if (Notes.size() == 1 &&
      Notes[0].second.getDiagID() == diag::note_invalid_subexpr_in_const_expr) {
    Diag(Notes[0].first, diag::err_expr_not_cce) << CCE;
  } else if (!Notes.empty() && Notes[0].second.getDiagID() ==
                                   diag::note_constexpr_invalid_template_arg) {
    Notes[0].second.setDiagID(diag::err_constexpr_invalid_template_arg);
    for (unsigned I = 0; I < Notes.size(); ++I)
      Diag(Notes[I].first, Notes[I].second);
  } else {
    Diag(E->getBeginLoc(), diag::err_expr_not_cce)
        << CCE << E->getSourceRange();
    for (unsigned I = 0; I < Notes.size(); ++I)
      Diag(Notes[I].first, Notes[I].second);
  }
  return ExprError();
}

/// dropPointerConversions - If the given standard conversion sequence
/// involves any pointer conversions, remove them.  This may change
/// the result type of the conversion sequence.
static void dropPointerConversion(StandardConversionSequence &SCS) {
  if (SCS.Second == ICK_Pointer_Conversion) {
    SCS.Second = ICK_Identity;
    SCS.Dimension = ICK_Identity;
    SCS.Third = ICK_Identity;
    SCS.ToTypePtrs[2] = SCS.ToTypePtrs[1] = SCS.ToTypePtrs[0];
  }
}

/// TryContextuallyConvertToObjCPointer - Attempt to contextually
/// convert the expression From to an Objective-C pointer type.
static ImplicitConversionSequence
TryContextuallyConvertToObjCPointer(Sema &S, Expr *From) {
  // Do an implicit conversion to 'id'.
  QualType Ty = S.Context.getObjCIdType();
  ImplicitConversionSequence ICS
    = TryImplicitConversion(S, From, Ty,
                            // FIXME: Are these flags correct?
                            /*SuppressUserConversions=*/false,
                            AllowedExplicit::Conversions,
                            /*InOverloadResolution=*/false,
                            /*CStyle=*/false,
                            /*AllowObjCWritebackConversion=*/false,
                            /*AllowObjCConversionOnExplicit=*/true);

  // Strip off any final conversions to 'id'.
  switch (ICS.getKind()) {
  case ImplicitConversionSequence::BadConversion:
  case ImplicitConversionSequence::AmbiguousConversion:
  case ImplicitConversionSequence::EllipsisConversion:
  case ImplicitConversionSequence::StaticObjectArgumentConversion:
    break;

  case ImplicitConversionSequence::UserDefinedConversion:
    dropPointerConversion(ICS.UserDefined.After);
    break;

  case ImplicitConversionSequence::StandardConversion:
    dropPointerConversion(ICS.Standard);
    break;
  }

  return ICS;
}

ExprResult Sema::PerformContextuallyConvertToObjCPointer(Expr *From) {
  if (checkPlaceholderForOverload(*this, From))
    return ExprError();

  QualType Ty = Context.getObjCIdType();
  ImplicitConversionSequence ICS =
    TryContextuallyConvertToObjCPointer(*this, From);
  if (!ICS.isBad())
    return PerformImplicitConversion(From, Ty, ICS, AA_Converting);
  return ExprResult();
}

static QualType GetExplicitObjectType(Sema &S, const Expr *MemExprE) {
  const Expr *Base = nullptr;
  assert((isa<UnresolvedMemberExpr, MemberExpr>(MemExprE)) &&
         "expected a member expression");

  if (const auto M = dyn_cast<UnresolvedMemberExpr>(MemExprE);
      M && !M->isImplicitAccess())
    Base = M->getBase();
  else if (const auto M = dyn_cast<MemberExpr>(MemExprE);
           M && !M->isImplicitAccess())
    Base = M->getBase();

  QualType T = Base ? Base->getType() : S.getCurrentThisType();

  if (T->isPointerType())
    T = T->getPointeeType();

  return T;
}

static Expr *GetExplicitObjectExpr(Sema &S, Expr *Obj,
                                   const FunctionDecl *Fun) {
  QualType ObjType = Obj->getType();
  if (ObjType->isPointerType()) {
    ObjType = ObjType->getPointeeType();
    Obj = UnaryOperator::Create(S.getASTContext(), Obj, UO_Deref, ObjType,
                                VK_LValue, OK_Ordinary, SourceLocation(),
                                /*CanOverflow=*/false, FPOptionsOverride());
  }
  if (Obj->Classify(S.getASTContext()).isPRValue()) {
    Obj = S.CreateMaterializeTemporaryExpr(
        ObjType, Obj,
        !Fun->getParamDecl(0)->getType()->isRValueReferenceType());
  }
  return Obj;
}

ExprResult Sema::InitializeExplicitObjectArgument(Sema &S, Expr *Obj,
                                                  FunctionDecl *Fun) {
  Obj = GetExplicitObjectExpr(S, Obj, Fun);
  return S.PerformCopyInitialization(
      InitializedEntity::InitializeParameter(S.Context, Fun->getParamDecl(0)),
      Obj->getExprLoc(), Obj);
}

static bool PrepareExplicitObjectArgument(Sema &S, CXXMethodDecl *Method,
                                          Expr *Object, MultiExprArg &Args,
                                          SmallVectorImpl<Expr *> &NewArgs) {
  assert(Method->isExplicitObjectMemberFunction() &&
         "Method is not an explicit member function");
  assert(NewArgs.empty() && "NewArgs should be empty");

  NewArgs.reserve(Args.size() + 1);
  Expr *This = GetExplicitObjectExpr(S, Object, Method);
  NewArgs.push_back(This);
  NewArgs.append(Args.begin(), Args.end());
  Args = NewArgs;
  return S.DiagnoseInvalidExplicitObjectParameterInLambda(
      Method, Object->getBeginLoc());
}

/// Determine whether the provided type is an integral type, or an enumeration
/// type of a permitted flavor.
bool Sema::ICEConvertDiagnoser::match(QualType T) {
  return AllowScopedEnumerations ? T->isIntegralOrEnumerationType()
                                 : T->isIntegralOrUnscopedEnumerationType();
}

static ExprResult
diagnoseAmbiguousConversion(Sema &SemaRef, SourceLocation Loc, Expr *From,
                            Sema::ContextualImplicitConverter &Converter,
                            QualType T, UnresolvedSetImpl &ViableConversions) {

  if (Converter.Suppress)
    return ExprError();

  Converter.diagnoseAmbiguous(SemaRef, Loc, T) << From->getSourceRange();
  for (unsigned I = 0, N = ViableConversions.size(); I != N; ++I) {
    CXXConversionDecl *Conv =
        cast<CXXConversionDecl>(ViableConversions[I]->getUnderlyingDecl());
    QualType ConvTy = Conv->getConversionType().getNonReferenceType();
    Converter.noteAmbiguous(SemaRef, Conv, ConvTy);
  }
  return From;
}

static bool
diagnoseNoViableConversion(Sema &SemaRef, SourceLocation Loc, Expr *&From,
                           Sema::ContextualImplicitConverter &Converter,
                           QualType T, bool HadMultipleCandidates,
                           UnresolvedSetImpl &ExplicitConversions) {
  if (ExplicitConversions.size() == 1 && !Converter.Suppress) {
    DeclAccessPair Found = ExplicitConversions[0];
    CXXConversionDecl *Conversion =
        cast<CXXConversionDecl>(Found->getUnderlyingDecl());

    // The user probably meant to invoke the given explicit
    // conversion; use it.
    QualType ConvTy = Conversion->getConversionType().getNonReferenceType();
    std::string TypeStr;
    ConvTy.getAsStringInternal(TypeStr, SemaRef.getPrintingPolicy());

    Converter.diagnoseExplicitConv(SemaRef, Loc, T, ConvTy)
        << FixItHint::CreateInsertion(From->getBeginLoc(),
                                      "static_cast<" + TypeStr + ">(")
        << FixItHint::CreateInsertion(
               SemaRef.getLocForEndOfToken(From->getEndLoc()), ")");
    Converter.noteExplicitConv(SemaRef, Conversion, ConvTy);

    // If we aren't in a SFINAE context, build a call to the
    // explicit conversion function.
    if (SemaRef.isSFINAEContext())
      return true;

    SemaRef.CheckMemberOperatorAccess(From->getExprLoc(), From, nullptr, Found);
    ExprResult Result = SemaRef.BuildCXXMemberCallExpr(From, Found, Conversion,
                                                       HadMultipleCandidates);
    if (Result.isInvalid())
      return true;

    // Replace the conversion with a RecoveryExpr, so we don't try to
    // instantiate it later, but can further diagnose here.
    Result = SemaRef.CreateRecoveryExpr(From->getBeginLoc(), From->getEndLoc(),
                                        From, Result.get()->getType());
    if (Result.isInvalid())
      return true;
    From = Result.get();
  }
  return false;
}

static bool recordConversion(Sema &SemaRef, SourceLocation Loc, Expr *&From,
                             Sema::ContextualImplicitConverter &Converter,
                             QualType T, bool HadMultipleCandidates,
                             DeclAccessPair &Found) {
  CXXConversionDecl *Conversion =
      cast<CXXConversionDecl>(Found->getUnderlyingDecl());
  SemaRef.CheckMemberOperatorAccess(From->getExprLoc(), From, nullptr, Found);

  QualType ToType = Conversion->getConversionType().getNonReferenceType();
  if (!Converter.SuppressConversion) {
    if (SemaRef.isSFINAEContext())
      return true;

    Converter.diagnoseConversion(SemaRef, Loc, T, ToType)
        << From->getSourceRange();
  }

  ExprResult Result = SemaRef.BuildCXXMemberCallExpr(From, Found, Conversion,
                                                     HadMultipleCandidates);
  if (Result.isInvalid())
    return true;
  // Record usage of conversion in an implicit cast.
  From = ImplicitCastExpr::Create(SemaRef.Context, Result.get()->getType(),
                                  CK_UserDefinedConversion, Result.get(),
                                  nullptr, Result.get()->getValueKind(),
                                  SemaRef.CurFPFeatureOverrides());
  return false;
}

static ExprResult finishContextualImplicitConversion(
    Sema &SemaRef, SourceLocation Loc, Expr *From,
    Sema::ContextualImplicitConverter &Converter) {
  if (!Converter.match(From->getType()) && !Converter.Suppress)
    Converter.diagnoseNoMatch(SemaRef, Loc, From->getType())
        << From->getSourceRange();

  return SemaRef.DefaultLvalueConversion(From);
}

static void
collectViableConversionCandidates(Sema &SemaRef, Expr *From, QualType ToType,
                                  UnresolvedSetImpl &ViableConversions,
                                  OverloadCandidateSet &CandidateSet) {
  for (unsigned I = 0, N = ViableConversions.size(); I != N; ++I) {
    DeclAccessPair FoundDecl = ViableConversions[I];
    NamedDecl *D = FoundDecl.getDecl();
    CXXRecordDecl *ActingContext = cast<CXXRecordDecl>(D->getDeclContext());
    if (isa<UsingShadowDecl>(D))
      D = cast<UsingShadowDecl>(D)->getTargetDecl();

    CXXConversionDecl *Conv;
    FunctionTemplateDecl *ConvTemplate;
    if ((ConvTemplate = dyn_cast<FunctionTemplateDecl>(D)))
      Conv = cast<CXXConversionDecl>(ConvTemplate->getTemplatedDecl());
    else
      Conv = cast<CXXConversionDecl>(D);

    if (ConvTemplate)
      SemaRef.AddTemplateConversionCandidate(
          ConvTemplate, FoundDecl, ActingContext, From, ToType, CandidateSet,
          /*AllowObjCConversionOnExplicit=*/false, /*AllowExplicit*/ true);
    else
      SemaRef.AddConversionCandidate(Conv, FoundDecl, ActingContext, From,
                                     ToType, CandidateSet,
                                     /*AllowObjCConversionOnExplicit=*/false,
                                     /*AllowExplicit*/ true);
  }
}

/// Attempt to convert the given expression to a type which is accepted
/// by the given converter.
///
/// This routine will attempt to convert an expression of class type to a
/// type accepted by the specified converter. In C++11 and before, the class
/// must have a single non-explicit conversion function converting to a matching
/// type. In C++1y, there can be multiple such conversion functions, but only
/// one target type.
///
/// \param Loc The source location of the construct that requires the
/// conversion.
///
/// \param From The expression we're converting from.
///
/// \param Converter Used to control and diagnose the conversion process.
///
/// \returns The expression, converted to an integral or enumeration type if
/// successful.
ExprResult Sema::PerformContextualImplicitConversion(
    SourceLocation Loc, Expr *From, ContextualImplicitConverter &Converter) {
  // We can't perform any more checking for type-dependent expressions.
  if (From->isTypeDependent())
    return From;

  // Process placeholders immediately.
  if (From->hasPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(From);
    if (result.isInvalid())
      return result;
    From = result.get();
  }

  // Try converting the expression to an Lvalue first, to get rid of qualifiers.
  ExprResult Converted = DefaultLvalueConversion(From);
  QualType T = Converted.isUsable() ? Converted.get()->getType() : QualType();
  // If the expression already has a matching type, we're golden.
  if (Converter.match(T))
    return Converted;

  // FIXME: Check for missing '()' if T is a function type?

  // We can only perform contextual implicit conversions on objects of class
  // type.
  const RecordType *RecordTy = T->getAs<RecordType>();
  if (!RecordTy || !getLangOpts().CPlusPlus) {
    if (!Converter.Suppress)
      Converter.diagnoseNoMatch(*this, Loc, T) << From->getSourceRange();
    return From;
  }

  // We must have a complete class type.
  struct TypeDiagnoserPartialDiag : TypeDiagnoser {
    ContextualImplicitConverter &Converter;
    Expr *From;

    TypeDiagnoserPartialDiag(ContextualImplicitConverter &Converter, Expr *From)
        : Converter(Converter), From(From) {}

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      Converter.diagnoseIncomplete(S, Loc, T) << From->getSourceRange();
    }
  } IncompleteDiagnoser(Converter, From);

  if (Converter.Suppress ? !isCompleteType(Loc, T)
                         : RequireCompleteType(Loc, T, IncompleteDiagnoser))
    return From;

  // Look for a conversion to an integral or enumeration type.
  UnresolvedSet<4>
      ViableConversions; // These are *potentially* viable in C++1y.
  UnresolvedSet<4> ExplicitConversions;
  const auto &Conversions =
      cast<CXXRecordDecl>(RecordTy->getDecl())->getVisibleConversionFunctions();

  bool HadMultipleCandidates =
      (std::distance(Conversions.begin(), Conversions.end()) > 1);

  // To check that there is only one target type, in C++1y:
  QualType ToType;
  bool HasUniqueTargetType = true;

  // Collect explicit or viable (potentially in C++1y) conversions.
  for (auto I = Conversions.begin(), E = Conversions.end(); I != E; ++I) {
    NamedDecl *D = (*I)->getUnderlyingDecl();
    CXXConversionDecl *Conversion;
    FunctionTemplateDecl *ConvTemplate = dyn_cast<FunctionTemplateDecl>(D);
    if (ConvTemplate) {
      if (getLangOpts().CPlusPlus14)
        Conversion = cast<CXXConversionDecl>(ConvTemplate->getTemplatedDecl());
      else
        continue; // C++11 does not consider conversion operator templates(?).
    } else
      Conversion = cast<CXXConversionDecl>(D);

    assert((!ConvTemplate || getLangOpts().CPlusPlus14) &&
           "Conversion operator templates are considered potentially "
           "viable in C++1y");

    QualType CurToType = Conversion->getConversionType().getNonReferenceType();
    if (Converter.match(CurToType) || ConvTemplate) {

      if (Conversion->isExplicit()) {
        // FIXME: For C++1y, do we need this restriction?
        // cf. diagnoseNoViableConversion()
        if (!ConvTemplate)
          ExplicitConversions.addDecl(I.getDecl(), I.getAccess());
      } else {
        if (!ConvTemplate && getLangOpts().CPlusPlus14) {
          if (ToType.isNull())
            ToType = CurToType.getUnqualifiedType();
          else if (HasUniqueTargetType &&
                   (CurToType.getUnqualifiedType() != ToType))
            HasUniqueTargetType = false;
        }
        ViableConversions.addDecl(I.getDecl(), I.getAccess());
      }
    }
  }

  if (getLangOpts().CPlusPlus14) {
    // C++1y [conv]p6:
    // ... An expression e of class type E appearing in such a context
    // is said to be contextually implicitly converted to a specified
    // type T and is well-formed if and only if e can be implicitly
    // converted to a type T that is determined as follows: E is searched
    // for conversion functions whose return type is cv T or reference to
    // cv T such that T is allowed by the context. There shall be
    // exactly one such T.

    // If no unique T is found:
    if (ToType.isNull()) {
      if (diagnoseNoViableConversion(*this, Loc, From, Converter, T,
                                     HadMultipleCandidates,
                                     ExplicitConversions))
        return ExprError();
      return finishContextualImplicitConversion(*this, Loc, From, Converter);
    }

    // If more than one unique Ts are found:
    if (!HasUniqueTargetType)
      return diagnoseAmbiguousConversion(*this, Loc, From, Converter, T,
                                         ViableConversions);

    // If one unique T is found:
    // First, build a candidate set from the previously recorded
    // potentially viable conversions.
    OverloadCandidateSet CandidateSet(Loc, OverloadCandidateSet::CSK_Normal);
    collectViableConversionCandidates(*this, From, ToType, ViableConversions,
                                      CandidateSet);

    // Then, perform overload resolution over the candidate set.
    OverloadCandidateSet::iterator Best;
    switch (CandidateSet.BestViableFunction(*this, Loc, Best)) {
    case OR_Success: {
      // Apply this conversion.
      DeclAccessPair Found =
          DeclAccessPair::make(Best->Function, Best->FoundDecl.getAccess());
      if (recordConversion(*this, Loc, From, Converter, T,
                           HadMultipleCandidates, Found))
        return ExprError();
      break;
    }
    case OR_Ambiguous:
      return diagnoseAmbiguousConversion(*this, Loc, From, Converter, T,
                                         ViableConversions);
    case OR_No_Viable_Function:
      if (diagnoseNoViableConversion(*this, Loc, From, Converter, T,
                                     HadMultipleCandidates,
                                     ExplicitConversions))
        return ExprError();
      [[fallthrough]];
    case OR_Deleted:
      // We'll complain below about a non-integral condition type.
      break;
    }
  } else {
    switch (ViableConversions.size()) {
    case 0: {
      if (diagnoseNoViableConversion(*this, Loc, From, Converter, T,
                                     HadMultipleCandidates,
                                     ExplicitConversions))
        return ExprError();

      // We'll complain below about a non-integral condition type.
      break;
    }
    case 1: {
      // Apply this conversion.
      DeclAccessPair Found = ViableConversions[0];
      if (recordConversion(*this, Loc, From, Converter, T,
                           HadMultipleCandidates, Found))
        return ExprError();
      break;
    }
    default:
      return diagnoseAmbiguousConversion(*this, Loc, From, Converter, T,
                                         ViableConversions);
    }
  }

  return finishContextualImplicitConversion(*this, Loc, From, Converter);
}

/// IsAcceptableNonMemberOperatorCandidate - Determine whether Fn is
/// an acceptable non-member overloaded operator for a call whose
/// arguments have types T1 (and, if non-empty, T2). This routine
/// implements the check in C++ [over.match.oper]p3b2 concerning
/// enumeration types.
static bool IsAcceptableNonMemberOperatorCandidate(ASTContext &Context,
                                                   FunctionDecl *Fn,
                                                   ArrayRef<Expr *> Args) {
  QualType T1 = Args[0]->getType();
  QualType T2 = Args.size() > 1 ? Args[1]->getType() : QualType();

  if (T1->isDependentType() || (!T2.isNull() && T2->isDependentType()))
    return true;

  if (T1->isRecordType() || (!T2.isNull() && T2->isRecordType()))
    return true;

  const auto *Proto = Fn->getType()->castAs<FunctionProtoType>();
  if (Proto->getNumParams() < 1)
    return false;

  if (T1->isEnumeralType()) {
    QualType ArgType = Proto->getParamType(0).getNonReferenceType();
    if (Context.hasSameUnqualifiedType(T1, ArgType))
      return true;
  }

  if (Proto->getNumParams() < 2)
    return false;

  if (!T2.isNull() && T2->isEnumeralType()) {
    QualType ArgType = Proto->getParamType(1).getNonReferenceType();
    if (Context.hasSameUnqualifiedType(T2, ArgType))
      return true;
  }

  return false;
}

static bool isNonViableMultiVersionOverload(FunctionDecl *FD) {
  if (FD->isTargetMultiVersionDefault())
    return false;

  if (!FD->getASTContext().getTargetInfo().getTriple().isAArch64())
    return FD->isTargetMultiVersion();

  if (!FD->isMultiVersion())
    return false;

  // Among multiple target versions consider either the default,
  // or the first non-default in the absence of default version.
  unsigned SeenAt = 0;
  unsigned I = 0;
  bool HasDefault = false;
  FD->getASTContext().forEachMultiversionedFunctionVersion(
      FD, [&](const FunctionDecl *CurFD) {
        if (FD == CurFD)
          SeenAt = I;
        else if (CurFD->isTargetMultiVersionDefault())
          HasDefault = true;
        ++I;
      });
  return HasDefault || SeenAt != 0;
}

void Sema::AddOverloadCandidate(
    FunctionDecl *Function, DeclAccessPair FoundDecl, ArrayRef<Expr *> Args,
    OverloadCandidateSet &CandidateSet, bool SuppressUserConversions,
    bool PartialOverloading, bool AllowExplicit, bool AllowExplicitConversions,
    ADLCallKind IsADLCandidate, ConversionSequenceList EarlyConversions,
    OverloadCandidateParamOrder PO, bool AggregateCandidateDeduction) {
  const FunctionProtoType *Proto
    = dyn_cast<FunctionProtoType>(Function->getType()->getAs<FunctionType>());
  assert(Proto && "Functions without a prototype cannot be overloaded");
  assert(!Function->getDescribedFunctionTemplate() &&
         "Use AddTemplateOverloadCandidate for function templates");

  if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Function)) {
    if (!isa<CXXConstructorDecl>(Method)) {
      // If we get here, it's because we're calling a member function
      // that is named without a member access expression (e.g.,
      // "this->f") that was either written explicitly or created
      // implicitly. This can happen with a qualified call to a member
      // function, e.g., X::f(). We use an empty type for the implied
      // object argument (C++ [over.call.func]p3), and the acting context
      // is irrelevant.
      AddMethodCandidate(Method, FoundDecl, Method->getParent(), QualType(),
                         Expr::Classification::makeSimpleLValue(), Args,
                         CandidateSet, SuppressUserConversions,
                         PartialOverloading, EarlyConversions, PO);
      return;
    }
    // We treat a constructor like a non-member function, since its object
    // argument doesn't participate in overload resolution.
  }

  if (!CandidateSet.isNewCandidate(Function, PO))
    return;

  // C++11 [class.copy]p11: [DR1402]
  //   A defaulted move constructor that is defined as deleted is ignored by
  //   overload resolution.
  CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(Function);
  if (Constructor && Constructor->isDefaulted() && Constructor->isDeleted() &&
      Constructor->isMoveConstructor())
    return;

  // Overload resolution is always an unevaluated context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);

  // C++ [over.match.oper]p3:
  //   if no operand has a class type, only those non-member functions in the
  //   lookup set that have a first parameter of type T1 or "reference to
  //   (possibly cv-qualified) T1", when T1 is an enumeration type, or (if there
  //   is a right operand) a second parameter of type T2 or "reference to
  //   (possibly cv-qualified) T2", when T2 is an enumeration type, are
  //   candidate functions.
  if (CandidateSet.getKind() == OverloadCandidateSet::CSK_Operator &&
      !IsAcceptableNonMemberOperatorCandidate(Context, Function, Args))
    return;

  // Add this candidate
  OverloadCandidate &Candidate =
      CandidateSet.addCandidate(Args.size(), EarlyConversions);
  Candidate.FoundDecl = FoundDecl;
  Candidate.Function = Function;
  Candidate.Viable = true;
  Candidate.RewriteKind =
      CandidateSet.getRewriteInfo().getRewriteKind(Function, PO);
  Candidate.IsADLCandidate = IsADLCandidate;
  Candidate.ExplicitCallArguments = Args.size();

  // Explicit functions are not actually candidates at all if we're not
  // allowing them in this context, but keep them around so we can point
  // to them in diagnostics.
  if (!AllowExplicit && ExplicitSpecifier::getFromDecl(Function).isExplicit()) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_explicit;
    return;
  }

  // Functions with internal linkage are only viable in the same module unit.
  if (getLangOpts().CPlusPlusModules && Function->isInAnotherModuleUnit()) {
    /// FIXME: Currently, the semantics of linkage in clang is slightly
    /// different from the semantics in C++ spec. In C++ spec, only names
    /// have linkage. So that all entities of the same should share one
    /// linkage. But in clang, different entities of the same could have
    /// different linkage.
    NamedDecl *ND = Function;
    if (auto *SpecInfo = Function->getTemplateSpecializationInfo())
      ND = SpecInfo->getTemplate();

    if (ND->getFormalLinkage() == Linkage::Internal) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_module_mismatched;
      return;
    }
  }

  if (isNonViableMultiVersionOverload(Function)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_non_default_multiversion_function;
    return;
  }

  if (Constructor) {
    // C++ [class.copy]p3:
    //   A member function template is never instantiated to perform the copy
    //   of a class object to an object of its class type.
    QualType ClassType = Context.getTypeDeclType(Constructor->getParent());
    if (Args.size() == 1 && Constructor->isSpecializationCopyingObject() &&
        (Context.hasSameUnqualifiedType(ClassType, Args[0]->getType()) ||
         IsDerivedFrom(Args[0]->getBeginLoc(), Args[0]->getType(),
                       ClassType))) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_illegal_constructor;
      return;
    }

    // C++ [over.match.funcs]p8: (proposed DR resolution)
    //   A constructor inherited from class type C that has a first parameter
    //   of type "reference to P" (including such a constructor instantiated
    //   from a template) is excluded from the set of candidate functions when
    //   constructing an object of type cv D if the argument list has exactly
    //   one argument and D is reference-related to P and P is reference-related
    //   to C.
    auto *Shadow = dyn_cast<ConstructorUsingShadowDecl>(FoundDecl.getDecl());
    if (Shadow && Args.size() == 1 && Constructor->getNumParams() >= 1 &&
        Constructor->getParamDecl(0)->getType()->isReferenceType()) {
      QualType P = Constructor->getParamDecl(0)->getType()->getPointeeType();
      QualType C = Context.getRecordType(Constructor->getParent());
      QualType D = Context.getRecordType(Shadow->getParent());
      SourceLocation Loc = Args.front()->getExprLoc();
      if ((Context.hasSameUnqualifiedType(P, C) || IsDerivedFrom(Loc, P, C)) &&
          (Context.hasSameUnqualifiedType(D, P) || IsDerivedFrom(Loc, D, P))) {
        Candidate.Viable = false;
        Candidate.FailureKind = ovl_fail_inhctor_slice;
        return;
      }
    }

    // Check that the constructor is capable of constructing an object in the
    // destination address space.
    if (!Qualifiers::isAddressSpaceSupersetOf(
            Constructor->getMethodQualifiers().getAddressSpace(),
            CandidateSet.getDestAS())) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_object_addrspace_mismatch;
    }
  }

  unsigned NumParams = Proto->getNumParams();

  // (C++ 13.3.2p2): A candidate function having fewer than m
  // parameters is viable only if it has an ellipsis in its parameter
  // list (8.3.5).
  if (TooManyArguments(NumParams, Args.size(), PartialOverloading) &&
      !Proto->isVariadic() &&
      shouldEnforceArgLimit(PartialOverloading, Function)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_too_many_arguments;
    return;
  }

  // (C++ 13.3.2p2): A candidate function having more than m parameters
  // is viable only if the (m+1)st parameter has a default argument
  // (8.3.6). For the purposes of overload resolution, the
  // parameter list is truncated on the right, so that there are
  // exactly m parameters.
  unsigned MinRequiredArgs = Function->getMinRequiredArguments();
  if (!AggregateCandidateDeduction && Args.size() < MinRequiredArgs &&
      !PartialOverloading) {
    // Not enough arguments.
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_too_few_arguments;
    return;
  }

  // (CUDA B.1): Check for invalid calls between targets.
  if (getLangOpts().CUDA) {
    const FunctionDecl *Caller = getCurFunctionDecl(/*AllowLambda=*/true);
    // Skip the check for callers that are implicit members, because in this
    // case we may not yet know what the member's target is; the target is
    // inferred for the member automatically, based on the bases and fields of
    // the class.
    if (!(Caller && Caller->isImplicit()) &&
        !CUDA().IsAllowedCall(Caller, Function)) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_bad_target;
      return;
    }
  }

  if (Function->getTrailingRequiresClause()) {
    ConstraintSatisfaction Satisfaction;
    if (CheckFunctionConstraints(Function, Satisfaction, /*Loc*/ {},
                                 /*ForOverloadResolution*/ true) ||
        !Satisfaction.IsSatisfied) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_constraints_not_satisfied;
      return;
    }
  }

  // Determine the implicit conversion sequences for each of the
  // arguments.
  for (unsigned ArgIdx = 0; ArgIdx < Args.size(); ++ArgIdx) {
    unsigned ConvIdx =
        PO == OverloadCandidateParamOrder::Reversed ? 1 - ArgIdx : ArgIdx;
    if (Candidate.Conversions[ConvIdx].isInitialized()) {
      // We already formed a conversion sequence for this parameter during
      // template argument deduction.
    } else if (ArgIdx < NumParams) {
      // (C++ 13.3.2p3): for F to be a viable function, there shall
      // exist for each argument an implicit conversion sequence
      // (13.3.3.1) that converts that argument to the corresponding
      // parameter of F.
      QualType ParamType = Proto->getParamType(ArgIdx);
      Candidate.Conversions[ConvIdx] = TryCopyInitialization(
          *this, Args[ArgIdx], ParamType, SuppressUserConversions,
          /*InOverloadResolution=*/true,
          /*AllowObjCWritebackConversion=*/
          getLangOpts().ObjCAutoRefCount, AllowExplicitConversions);
      if (Candidate.Conversions[ConvIdx].isBad()) {
        Candidate.Viable = false;
        Candidate.FailureKind = ovl_fail_bad_conversion;
        return;
      }
    } else {
      // (C++ 13.3.2p2): For the purposes of overload resolution, any
      // argument for which there is no corresponding parameter is
      // considered to ""match the ellipsis" (C+ 13.3.3.1.3).
      Candidate.Conversions[ConvIdx].setEllipsis();
    }
  }

  if (EnableIfAttr *FailedAttr =
          CheckEnableIf(Function, CandidateSet.getLocation(), Args)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_enable_if;
    Candidate.DeductionFailure.Data = FailedAttr;
    return;
  }
}

ObjCMethodDecl *
Sema::SelectBestMethod(Selector Sel, MultiExprArg Args, bool IsInstance,
                       SmallVectorImpl<ObjCMethodDecl *> &Methods) {
  if (Methods.size() <= 1)
    return nullptr;

  for (unsigned b = 0, e = Methods.size(); b < e; b++) {
    bool Match = true;
    ObjCMethodDecl *Method = Methods[b];
    unsigned NumNamedArgs = Sel.getNumArgs();
    // Method might have more arguments than selector indicates. This is due
    // to addition of c-style arguments in method.
    if (Method->param_size() > NumNamedArgs)
      NumNamedArgs = Method->param_size();
    if (Args.size() < NumNamedArgs)
      continue;

    for (unsigned i = 0; i < NumNamedArgs; i++) {
      // We can't do any type-checking on a type-dependent argument.
      if (Args[i]->isTypeDependent()) {
        Match = false;
        break;
      }

      ParmVarDecl *param = Method->parameters()[i];
      Expr *argExpr = Args[i];
      assert(argExpr && "SelectBestMethod(): missing expression");

      // Strip the unbridged-cast placeholder expression off unless it's
      // a consumed argument.
      if (argExpr->hasPlaceholderType(BuiltinType::ARCUnbridgedCast) &&
          !param->hasAttr<CFConsumedAttr>())
        argExpr = ObjC().stripARCUnbridgedCast(argExpr);

      // If the parameter is __unknown_anytype, move on to the next method.
      if (param->getType() == Context.UnknownAnyTy) {
        Match = false;
        break;
      }

      ImplicitConversionSequence ConversionState
        = TryCopyInitialization(*this, argExpr, param->getType(),
                                /*SuppressUserConversions*/false,
                                /*InOverloadResolution=*/true,
                                /*AllowObjCWritebackConversion=*/
                                getLangOpts().ObjCAutoRefCount,
                                /*AllowExplicit*/false);
      // This function looks for a reasonably-exact match, so we consider
      // incompatible pointer conversions to be a failure here.
      if (ConversionState.isBad() ||
          (ConversionState.isStandard() &&
           ConversionState.Standard.Second ==
               ICK_Incompatible_Pointer_Conversion)) {
        Match = false;
        break;
      }
    }
    // Promote additional arguments to variadic methods.
    if (Match && Method->isVariadic()) {
      for (unsigned i = NumNamedArgs, e = Args.size(); i < e; ++i) {
        if (Args[i]->isTypeDependent()) {
          Match = false;
          break;
        }
        ExprResult Arg = DefaultVariadicArgumentPromotion(Args[i], VariadicMethod,
                                                          nullptr);
        if (Arg.isInvalid()) {
          Match = false;
          break;
        }
      }
    } else {
      // Check for extra arguments to non-variadic methods.
      if (Args.size() != NumNamedArgs)
        Match = false;
      else if (Match && NumNamedArgs == 0 && Methods.size() > 1) {
        // Special case when selectors have no argument. In this case, select
        // one with the most general result type of 'id'.
        for (unsigned b = 0, e = Methods.size(); b < e; b++) {
          QualType ReturnT = Methods[b]->getReturnType();
          if (ReturnT->isObjCIdType())
            return Methods[b];
        }
      }
    }

    if (Match)
      return Method;
  }
  return nullptr;
}

static bool convertArgsForAvailabilityChecks(
    Sema &S, FunctionDecl *Function, Expr *ThisArg, SourceLocation CallLoc,
    ArrayRef<Expr *> Args, Sema::SFINAETrap &Trap, bool MissingImplicitThis,
    Expr *&ConvertedThis, SmallVectorImpl<Expr *> &ConvertedArgs) {
  if (ThisArg) {
    CXXMethodDecl *Method = cast<CXXMethodDecl>(Function);
    assert(!isa<CXXConstructorDecl>(Method) &&
           "Shouldn't have `this` for ctors!");
    assert(!Method->isStatic() && "Shouldn't have `this` for static methods!");
    ExprResult R = S.PerformImplicitObjectArgumentInitialization(
        ThisArg, /*Qualifier=*/nullptr, Method, Method);
    if (R.isInvalid())
      return false;
    ConvertedThis = R.get();
  } else {
    if (auto *MD = dyn_cast<CXXMethodDecl>(Function)) {
      (void)MD;
      assert((MissingImplicitThis || MD->isStatic() ||
              isa<CXXConstructorDecl>(MD)) &&
             "Expected `this` for non-ctor instance methods");
    }
    ConvertedThis = nullptr;
  }

  // Ignore any variadic arguments. Converting them is pointless, since the
  // user can't refer to them in the function condition.
  unsigned ArgSizeNoVarargs = std::min(Function->param_size(), Args.size());

  // Convert the arguments.
  for (unsigned I = 0; I != ArgSizeNoVarargs; ++I) {
    ExprResult R;
    R = S.PerformCopyInitialization(InitializedEntity::InitializeParameter(
                                        S.Context, Function->getParamDecl(I)),
                                    SourceLocation(), Args[I]);

    if (R.isInvalid())
      return false;

    ConvertedArgs.push_back(R.get());
  }

  if (Trap.hasErrorOccurred())
    return false;

  // Push default arguments if needed.
  if (!Function->isVariadic() && Args.size() < Function->getNumParams()) {
    for (unsigned i = Args.size(), e = Function->getNumParams(); i != e; ++i) {
      ParmVarDecl *P = Function->getParamDecl(i);
      if (!P->hasDefaultArg())
        return false;
      ExprResult R = S.BuildCXXDefaultArgExpr(CallLoc, Function, P);
      if (R.isInvalid())
        return false;
      ConvertedArgs.push_back(R.get());
    }

    if (Trap.hasErrorOccurred())
      return false;
  }
  return true;
}

EnableIfAttr *Sema::CheckEnableIf(FunctionDecl *Function,
                                  SourceLocation CallLoc,
                                  ArrayRef<Expr *> Args,
                                  bool MissingImplicitThis) {
  auto EnableIfAttrs = Function->specific_attrs<EnableIfAttr>();
  if (EnableIfAttrs.begin() == EnableIfAttrs.end())
    return nullptr;

  SFINAETrap Trap(*this);
  SmallVector<Expr *, 16> ConvertedArgs;
  // FIXME: We should look into making enable_if late-parsed.
  Expr *DiscardedThis;
  if (!convertArgsForAvailabilityChecks(
          *this, Function, /*ThisArg=*/nullptr, CallLoc, Args, Trap,
          /*MissingImplicitThis=*/true, DiscardedThis, ConvertedArgs))
    return *EnableIfAttrs.begin();

  for (auto *EIA : EnableIfAttrs) {
    APValue Result;
    // FIXME: This doesn't consider value-dependent cases, because doing so is
    // very difficult. Ideally, we should handle them more gracefully.
    if (EIA->getCond()->isValueDependent() ||
        !EIA->getCond()->EvaluateWithSubstitution(
            Result, Context, Function, llvm::ArrayRef(ConvertedArgs)))
      return EIA;

    if (!Result.isInt() || !Result.getInt().getBoolValue())
      return EIA;
  }
  return nullptr;
}

template <typename CheckFn>
static bool diagnoseDiagnoseIfAttrsWith(Sema &S, const NamedDecl *ND,
                                        bool ArgDependent, SourceLocation Loc,
                                        CheckFn &&IsSuccessful) {
  SmallVector<const DiagnoseIfAttr *, 8> Attrs;
  for (const auto *DIA : ND->specific_attrs<DiagnoseIfAttr>()) {
    if (ArgDependent == DIA->getArgDependent())
      Attrs.push_back(DIA);
  }

  // Common case: No diagnose_if attributes, so we can quit early.
  if (Attrs.empty())
    return false;

  auto WarningBegin = std::stable_partition(
      Attrs.begin(), Attrs.end(),
      [](const DiagnoseIfAttr *DIA) { return DIA->isError(); });

  // Note that diagnose_if attributes are late-parsed, so they appear in the
  // correct order (unlike enable_if attributes).
  auto ErrAttr = llvm::find_if(llvm::make_range(Attrs.begin(), WarningBegin),
                               IsSuccessful);
  if (ErrAttr != WarningBegin) {
    const DiagnoseIfAttr *DIA = *ErrAttr;
    S.Diag(Loc, diag::err_diagnose_if_succeeded) << DIA->getMessage();
    S.Diag(DIA->getLocation(), diag::note_from_diagnose_if)
        << DIA->getParent() << DIA->getCond()->getSourceRange();
    return true;
  }

  for (const auto *DIA : llvm::make_range(WarningBegin, Attrs.end()))
    if (IsSuccessful(DIA)) {
      S.Diag(Loc, diag::warn_diagnose_if_succeeded) << DIA->getMessage();
      S.Diag(DIA->getLocation(), diag::note_from_diagnose_if)
          << DIA->getParent() << DIA->getCond()->getSourceRange();
    }

  return false;
}

bool Sema::diagnoseArgDependentDiagnoseIfAttrs(const FunctionDecl *Function,
                                               const Expr *ThisArg,
                                               ArrayRef<const Expr *> Args,
                                               SourceLocation Loc) {
  return diagnoseDiagnoseIfAttrsWith(
      *this, Function, /*ArgDependent=*/true, Loc,
      [&](const DiagnoseIfAttr *DIA) {
        APValue Result;
        // It's sane to use the same Args for any redecl of this function, since
        // EvaluateWithSubstitution only cares about the position of each
        // argument in the arg list, not the ParmVarDecl* it maps to.
        if (!DIA->getCond()->EvaluateWithSubstitution(
                Result, Context, cast<FunctionDecl>(DIA->getParent()), Args, ThisArg))
          return false;
        return Result.isInt() && Result.getInt().getBoolValue();
      });
}

bool Sema::diagnoseArgIndependentDiagnoseIfAttrs(const NamedDecl *ND,
                                                 SourceLocation Loc) {
  return diagnoseDiagnoseIfAttrsWith(
      *this, ND, /*ArgDependent=*/false, Loc,
      [&](const DiagnoseIfAttr *DIA) {
        bool Result;
        return DIA->getCond()->EvaluateAsBooleanCondition(Result, Context) &&
               Result;
      });
}

void Sema::AddFunctionCandidates(const UnresolvedSetImpl &Fns,
                                 ArrayRef<Expr *> Args,
                                 OverloadCandidateSet &CandidateSet,
                                 TemplateArgumentListInfo *ExplicitTemplateArgs,
                                 bool SuppressUserConversions,
                                 bool PartialOverloading,
                                 bool FirstArgumentIsBase) {
  for (UnresolvedSetIterator F = Fns.begin(), E = Fns.end(); F != E; ++F) {
    NamedDecl *D = F.getDecl()->getUnderlyingDecl();
    ArrayRef<Expr *> FunctionArgs = Args;

    FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(D);
    FunctionDecl *FD =
        FunTmpl ? FunTmpl->getTemplatedDecl() : cast<FunctionDecl>(D);

    if (isa<CXXMethodDecl>(FD) && !cast<CXXMethodDecl>(FD)->isStatic()) {
      QualType ObjectType;
      Expr::Classification ObjectClassification;
      if (Args.size() > 0) {
        if (Expr *E = Args[0]) {
          // Use the explicit base to restrict the lookup:
          ObjectType = E->getType();
          // Pointers in the object arguments are implicitly dereferenced, so we
          // always classify them as l-values.
          if (!ObjectType.isNull() && ObjectType->isPointerType())
            ObjectClassification = Expr::Classification::makeSimpleLValue();
          else
            ObjectClassification = E->Classify(Context);
        } // .. else there is an implicit base.
        FunctionArgs = Args.slice(1);
      }
      if (FunTmpl) {
        AddMethodTemplateCandidate(
            FunTmpl, F.getPair(),
            cast<CXXRecordDecl>(FunTmpl->getDeclContext()),
            ExplicitTemplateArgs, ObjectType, ObjectClassification,
            FunctionArgs, CandidateSet, SuppressUserConversions,
            PartialOverloading);
      } else {
        AddMethodCandidate(cast<CXXMethodDecl>(FD), F.getPair(),
                           cast<CXXMethodDecl>(FD)->getParent(), ObjectType,
                           ObjectClassification, FunctionArgs, CandidateSet,
                           SuppressUserConversions, PartialOverloading);
      }
    } else {
      // This branch handles both standalone functions and static methods.

      // Slice the first argument (which is the base) when we access
      // static method as non-static.
      if (Args.size() > 0 &&
          (!Args[0] || (FirstArgumentIsBase && isa<CXXMethodDecl>(FD) &&
                        !isa<CXXConstructorDecl>(FD)))) {
        assert(cast<CXXMethodDecl>(FD)->isStatic());
        FunctionArgs = Args.slice(1);
      }
      if (FunTmpl) {
        AddTemplateOverloadCandidate(FunTmpl, F.getPair(),
                                     ExplicitTemplateArgs, FunctionArgs,
                                     CandidateSet, SuppressUserConversions,
                                     PartialOverloading);
      } else {
        AddOverloadCandidate(FD, F.getPair(), FunctionArgs, CandidateSet,
                             SuppressUserConversions, PartialOverloading);
      }
    }
  }
}

void Sema::AddMethodCandidate(DeclAccessPair FoundDecl, QualType ObjectType,
                              Expr::Classification ObjectClassification,
                              ArrayRef<Expr *> Args,
                              OverloadCandidateSet &CandidateSet,
                              bool SuppressUserConversions,
                              OverloadCandidateParamOrder PO) {
  NamedDecl *Decl = FoundDecl.getDecl();
  CXXRecordDecl *ActingContext = cast<CXXRecordDecl>(Decl->getDeclContext());

  if (isa<UsingShadowDecl>(Decl))
    Decl = cast<UsingShadowDecl>(Decl)->getTargetDecl();

  if (FunctionTemplateDecl *TD = dyn_cast<FunctionTemplateDecl>(Decl)) {
    assert(isa<CXXMethodDecl>(TD->getTemplatedDecl()) &&
           "Expected a member function template");
    AddMethodTemplateCandidate(TD, FoundDecl, ActingContext,
                               /*ExplicitArgs*/ nullptr, ObjectType,
                               ObjectClassification, Args, CandidateSet,
                               SuppressUserConversions, false, PO);
  } else {
    AddMethodCandidate(cast<CXXMethodDecl>(Decl), FoundDecl, ActingContext,
                       ObjectType, ObjectClassification, Args, CandidateSet,
                       SuppressUserConversions, false, std::nullopt, PO);
  }
}

void
Sema::AddMethodCandidate(CXXMethodDecl *Method, DeclAccessPair FoundDecl,
                         CXXRecordDecl *ActingContext, QualType ObjectType,
                         Expr::Classification ObjectClassification,
                         ArrayRef<Expr *> Args,
                         OverloadCandidateSet &CandidateSet,
                         bool SuppressUserConversions,
                         bool PartialOverloading,
                         ConversionSequenceList EarlyConversions,
                         OverloadCandidateParamOrder PO) {
  const FunctionProtoType *Proto
    = dyn_cast<FunctionProtoType>(Method->getType()->getAs<FunctionType>());
  assert(Proto && "Methods without a prototype cannot be overloaded");
  assert(!isa<CXXConstructorDecl>(Method) &&
         "Use AddOverloadCandidate for constructors");

  if (!CandidateSet.isNewCandidate(Method, PO))
    return;

  // C++11 [class.copy]p23: [DR1402]
  //   A defaulted move assignment operator that is defined as deleted is
  //   ignored by overload resolution.
  if (Method->isDefaulted() && Method->isDeleted() &&
      Method->isMoveAssignmentOperator())
    return;

  // Overload resolution is always an unevaluated context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);

  // Add this candidate
  OverloadCandidate &Candidate =
      CandidateSet.addCandidate(Args.size() + 1, EarlyConversions);
  Candidate.FoundDecl = FoundDecl;
  Candidate.Function = Method;
  Candidate.RewriteKind =
      CandidateSet.getRewriteInfo().getRewriteKind(Method, PO);
  Candidate.TookAddressOfOverload =
      CandidateSet.getKind() == OverloadCandidateSet::CSK_AddressOfOverloadSet;
  Candidate.ExplicitCallArguments = Args.size();

  bool IgnoreExplicitObject =
      (Method->isExplicitObjectMemberFunction() &&
       CandidateSet.getKind() ==
           OverloadCandidateSet::CSK_AddressOfOverloadSet);
  bool ImplicitObjectMethodTreatedAsStatic =
      CandidateSet.getKind() ==
          OverloadCandidateSet::CSK_AddressOfOverloadSet &&
      Method->isImplicitObjectMemberFunction();

  unsigned ExplicitOffset =
      !IgnoreExplicitObject && Method->isExplicitObjectMemberFunction() ? 1 : 0;

  unsigned NumParams = Method->getNumParams() - ExplicitOffset +
                       int(ImplicitObjectMethodTreatedAsStatic);

  // (C++ 13.3.2p2): A candidate function having fewer than m
  // parameters is viable only if it has an ellipsis in its parameter
  // list (8.3.5).
  if (TooManyArguments(NumParams, Args.size(), PartialOverloading) &&
      !Proto->isVariadic() &&
      shouldEnforceArgLimit(PartialOverloading, Method)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_too_many_arguments;
    return;
  }

  // (C++ 13.3.2p2): A candidate function having more than m parameters
  // is viable only if the (m+1)st parameter has a default argument
  // (8.3.6). For the purposes of overload resolution, the
  // parameter list is truncated on the right, so that there are
  // exactly m parameters.
  unsigned MinRequiredArgs = Method->getMinRequiredArguments() -
                             ExplicitOffset +
                             int(ImplicitObjectMethodTreatedAsStatic);

  if (Args.size() < MinRequiredArgs && !PartialOverloading) {
    // Not enough arguments.
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_too_few_arguments;
    return;
  }

  Candidate.Viable = true;

  unsigned FirstConvIdx = PO == OverloadCandidateParamOrder::Reversed ? 1 : 0;
  if (ObjectType.isNull())
    Candidate.IgnoreObjectArgument = true;
  else if (Method->isStatic()) {
    // [over.best.ics.general]p8
    // When the parameter is the implicit object parameter of a static member
    // function, the implicit conversion sequence is a standard conversion
    // sequence that is neither better nor worse than any other standard
    // conversion sequence.
    //
    // This is a rule that was introduced in C++23 to support static lambdas. We
    // apply it retroactively because we want to support static lambdas as an
    // extension and it doesn't hurt previous code.
    Candidate.Conversions[FirstConvIdx].setStaticObjectArgument();
  } else {
    // Determine the implicit conversion sequence for the object
    // parameter.
    Candidate.Conversions[FirstConvIdx] = TryObjectArgumentInitialization(
        *this, CandidateSet.getLocation(), ObjectType, ObjectClassification,
        Method, ActingContext, /*InOverloadResolution=*/true);
    if (Candidate.Conversions[FirstConvIdx].isBad()) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_bad_conversion;
      return;
    }
  }

  // (CUDA B.1): Check for invalid calls between targets.
  if (getLangOpts().CUDA)
    if (!CUDA().IsAllowedCall(getCurFunctionDecl(/*AllowLambda=*/true),
                              Method)) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_bad_target;
      return;
    }

  if (Method->getTrailingRequiresClause()) {
    ConstraintSatisfaction Satisfaction;
    if (CheckFunctionConstraints(Method, Satisfaction, /*Loc*/ {},
                                 /*ForOverloadResolution*/ true) ||
        !Satisfaction.IsSatisfied) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_constraints_not_satisfied;
      return;
    }
  }

  // Determine the implicit conversion sequences for each of the
  // arguments.
  for (unsigned ArgIdx = 0; ArgIdx < Args.size(); ++ArgIdx) {
    unsigned ConvIdx =
        PO == OverloadCandidateParamOrder::Reversed ? 0 : (ArgIdx + 1);
    if (Candidate.Conversions[ConvIdx].isInitialized()) {
      // We already formed a conversion sequence for this parameter during
      // template argument deduction.
    } else if (ArgIdx < NumParams) {
      // (C++ 13.3.2p3): for F to be a viable function, there shall
      // exist for each argument an implicit conversion sequence
      // (13.3.3.1) that converts that argument to the corresponding
      // parameter of F.
      QualType ParamType;
      if (ImplicitObjectMethodTreatedAsStatic) {
        ParamType = ArgIdx == 0
                        ? Method->getFunctionObjectParameterReferenceType()
                        : Proto->getParamType(ArgIdx - 1);
      } else {
        ParamType = Proto->getParamType(ArgIdx + ExplicitOffset);
      }
      Candidate.Conversions[ConvIdx]
        = TryCopyInitialization(*this, Args[ArgIdx], ParamType,
                                SuppressUserConversions,
                                /*InOverloadResolution=*/true,
                                /*AllowObjCWritebackConversion=*/
                                  getLangOpts().ObjCAutoRefCount);
      if (Candidate.Conversions[ConvIdx].isBad()) {
        Candidate.Viable = false;
        Candidate.FailureKind = ovl_fail_bad_conversion;
        return;
      }
    } else {
      // (C++ 13.3.2p2): For the purposes of overload resolution, any
      // argument for which there is no corresponding parameter is
      // considered to "match the ellipsis" (C+ 13.3.3.1.3).
      Candidate.Conversions[ConvIdx].setEllipsis();
    }
  }

  if (EnableIfAttr *FailedAttr =
          CheckEnableIf(Method, CandidateSet.getLocation(), Args, true)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_enable_if;
    Candidate.DeductionFailure.Data = FailedAttr;
    return;
  }

  if (isNonViableMultiVersionOverload(Method)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_non_default_multiversion_function;
  }
}

void Sema::AddMethodTemplateCandidate(
    FunctionTemplateDecl *MethodTmpl, DeclAccessPair FoundDecl,
    CXXRecordDecl *ActingContext,
    TemplateArgumentListInfo *ExplicitTemplateArgs, QualType ObjectType,
    Expr::Classification ObjectClassification, ArrayRef<Expr *> Args,
    OverloadCandidateSet &CandidateSet, bool SuppressUserConversions,
    bool PartialOverloading, OverloadCandidateParamOrder PO) {
  if (!CandidateSet.isNewCandidate(MethodTmpl, PO))
    return;

  // C++ [over.match.funcs]p7:
  //   In each case where a candidate is a function template, candidate
  //   function template specializations are generated using template argument
  //   deduction (14.8.3, 14.8.2). Those candidates are then handled as
  //   candidate functions in the usual way.113) A given name can refer to one
  //   or more function templates and also to a set of overloaded non-template
  //   functions. In such a case, the candidate functions generated from each
  //   function template are combined with the set of non-template candidate
  //   functions.
  TemplateDeductionInfo Info(CandidateSet.getLocation());
  FunctionDecl *Specialization = nullptr;
  ConversionSequenceList Conversions;
  if (TemplateDeductionResult Result = DeduceTemplateArguments(
          MethodTmpl, ExplicitTemplateArgs, Args, Specialization, Info,
          PartialOverloading, /*AggregateDeductionCandidate=*/false, ObjectType,
          ObjectClassification,
          [&](ArrayRef<QualType> ParamTypes) {
            return CheckNonDependentConversions(
                MethodTmpl, ParamTypes, Args, CandidateSet, Conversions,
                SuppressUserConversions, ActingContext, ObjectType,
                ObjectClassification, PO);
          });
      Result != TemplateDeductionResult::Success) {
    OverloadCandidate &Candidate =
        CandidateSet.addCandidate(Conversions.size(), Conversions);
    Candidate.FoundDecl = FoundDecl;
    Candidate.Function = MethodTmpl->getTemplatedDecl();
    Candidate.Viable = false;
    Candidate.RewriteKind =
      CandidateSet.getRewriteInfo().getRewriteKind(Candidate.Function, PO);
    Candidate.IsSurrogate = false;
    Candidate.IgnoreObjectArgument =
        cast<CXXMethodDecl>(Candidate.Function)->isStatic() ||
        ObjectType.isNull();
    Candidate.ExplicitCallArguments = Args.size();
    if (Result == TemplateDeductionResult::NonDependentConversionFailure)
      Candidate.FailureKind = ovl_fail_bad_conversion;
    else {
      Candidate.FailureKind = ovl_fail_bad_deduction;
      Candidate.DeductionFailure = MakeDeductionFailureInfo(Context, Result,
                                                            Info);
    }
    return;
  }

  // Add the function template specialization produced by template argument
  // deduction as a candidate.
  assert(Specialization && "Missing member function template specialization?");
  assert(isa<CXXMethodDecl>(Specialization) &&
         "Specialization is not a member function?");
  AddMethodCandidate(cast<CXXMethodDecl>(Specialization), FoundDecl,
                     ActingContext, ObjectType, ObjectClassification, Args,
                     CandidateSet, SuppressUserConversions, PartialOverloading,
                     Conversions, PO);
}

/// Determine whether a given function template has a simple explicit specifier
/// or a non-value-dependent explicit-specification that evaluates to true.
static bool isNonDependentlyExplicit(FunctionTemplateDecl *FTD) {
  return ExplicitSpecifier::getFromDecl(FTD->getTemplatedDecl()).isExplicit();
}

void Sema::AddTemplateOverloadCandidate(
    FunctionTemplateDecl *FunctionTemplate, DeclAccessPair FoundDecl,
    TemplateArgumentListInfo *ExplicitTemplateArgs, ArrayRef<Expr *> Args,
    OverloadCandidateSet &CandidateSet, bool SuppressUserConversions,
    bool PartialOverloading, bool AllowExplicit, ADLCallKind IsADLCandidate,
    OverloadCandidateParamOrder PO, bool AggregateCandidateDeduction) {
  if (!CandidateSet.isNewCandidate(FunctionTemplate, PO))
    return;

  // If the function template has a non-dependent explicit specification,
  // exclude it now if appropriate; we are not permitted to perform deduction
  // and substitution in this case.
  if (!AllowExplicit && isNonDependentlyExplicit(FunctionTemplate)) {
    OverloadCandidate &Candidate = CandidateSet.addCandidate();
    Candidate.FoundDecl = FoundDecl;
    Candidate.Function = FunctionTemplate->getTemplatedDecl();
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_explicit;
    return;
  }

  // C++ [over.match.funcs]p7:
  //   In each case where a candidate is a function template, candidate
  //   function template specializations are generated using template argument
  //   deduction (14.8.3, 14.8.2). Those candidates are then handled as
  //   candidate functions in the usual way.113) A given name can refer to one
  //   or more function templates and also to a set of overloaded non-template
  //   functions. In such a case, the candidate functions generated from each
  //   function template are combined with the set of non-template candidate
  //   functions.
  TemplateDeductionInfo Info(CandidateSet.getLocation(),
                             FunctionTemplate->getTemplateDepth());
  FunctionDecl *Specialization = nullptr;
  ConversionSequenceList Conversions;
  if (TemplateDeductionResult Result = DeduceTemplateArguments(
          FunctionTemplate, ExplicitTemplateArgs, Args, Specialization, Info,
          PartialOverloading, AggregateCandidateDeduction,
          /*ObjectType=*/QualType(),
          /*ObjectClassification=*/Expr::Classification(),
          [&](ArrayRef<QualType> ParamTypes) {
            return CheckNonDependentConversions(
                FunctionTemplate, ParamTypes, Args, CandidateSet, Conversions,
                SuppressUserConversions, nullptr, QualType(), {}, PO);
          });
      Result != TemplateDeductionResult::Success) {
    OverloadCandidate &Candidate =
        CandidateSet.addCandidate(Conversions.size(), Conversions);
    Candidate.FoundDecl = FoundDecl;
    Candidate.Function = FunctionTemplate->getTemplatedDecl();
    Candidate.Viable = false;
    Candidate.RewriteKind =
      CandidateSet.getRewriteInfo().getRewriteKind(Candidate.Function, PO);
    Candidate.IsSurrogate = false;
    Candidate.IsADLCandidate = IsADLCandidate;
    // Ignore the object argument if there is one, since we don't have an object
    // type.
    Candidate.IgnoreObjectArgument =
        isa<CXXMethodDecl>(Candidate.Function) &&
        !isa<CXXConstructorDecl>(Candidate.Function);
    Candidate.ExplicitCallArguments = Args.size();
    if (Result == TemplateDeductionResult::NonDependentConversionFailure)
      Candidate.FailureKind = ovl_fail_bad_conversion;
    else {
      Candidate.FailureKind = ovl_fail_bad_deduction;
      Candidate.DeductionFailure = MakeDeductionFailureInfo(Context, Result,
                                                            Info);
    }
    return;
  }

  // Add the function template specialization produced by template argument
  // deduction as a candidate.
  assert(Specialization && "Missing function template specialization?");
  AddOverloadCandidate(
      Specialization, FoundDecl, Args, CandidateSet, SuppressUserConversions,
      PartialOverloading, AllowExplicit,
      /*AllowExplicitConversions=*/false, IsADLCandidate, Conversions, PO,
      Info.AggregateDeductionCandidateHasMismatchedArity);
}

bool Sema::CheckNonDependentConversions(
    FunctionTemplateDecl *FunctionTemplate, ArrayRef<QualType> ParamTypes,
    ArrayRef<Expr *> Args, OverloadCandidateSet &CandidateSet,
    ConversionSequenceList &Conversions, bool SuppressUserConversions,
    CXXRecordDecl *ActingContext, QualType ObjectType,
    Expr::Classification ObjectClassification, OverloadCandidateParamOrder PO) {
  // FIXME: The cases in which we allow explicit conversions for constructor
  // arguments never consider calling a constructor template. It's not clear
  // that is correct.
  const bool AllowExplicit = false;

  auto *FD = FunctionTemplate->getTemplatedDecl();
  auto *Method = dyn_cast<CXXMethodDecl>(FD);
  bool HasThisConversion = Method && !isa<CXXConstructorDecl>(Method);
  unsigned ThisConversions = HasThisConversion ? 1 : 0;

  Conversions =
      CandidateSet.allocateConversionSequences(ThisConversions + Args.size());

  // Overload resolution is always an unevaluated context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);

  // For a method call, check the 'this' conversion here too. DR1391 doesn't
  // require that, but this check should never result in a hard error, and
  // overload resolution is permitted to sidestep instantiations.
  if (HasThisConversion && !cast<CXXMethodDecl>(FD)->isStatic() &&
      !ObjectType.isNull()) {
    unsigned ConvIdx = PO == OverloadCandidateParamOrder::Reversed ? 1 : 0;
    if (!FD->hasCXXExplicitFunctionObjectParameter() ||
        !ParamTypes[0]->isDependentType()) {
      Conversions[ConvIdx] = TryObjectArgumentInitialization(
          *this, CandidateSet.getLocation(), ObjectType, ObjectClassification,
          Method, ActingContext, /*InOverloadResolution=*/true,
          FD->hasCXXExplicitFunctionObjectParameter() ? ParamTypes[0]
                                                      : QualType());
      if (Conversions[ConvIdx].isBad())
        return true;
    }
  }

  unsigned Offset =
      Method && Method->hasCXXExplicitFunctionObjectParameter() ? 1 : 0;

  for (unsigned I = 0, N = std::min(ParamTypes.size() - Offset, Args.size());
       I != N; ++I) {
    QualType ParamType = ParamTypes[I + Offset];
    if (!ParamType->isDependentType()) {
      unsigned ConvIdx;
      if (PO == OverloadCandidateParamOrder::Reversed) {
        ConvIdx = Args.size() - 1 - I;
        assert(Args.size() + ThisConversions == 2 &&
               "number of args (including 'this') must be exactly 2 for "
               "reversed order");
        // For members, there would be only one arg 'Args[0]' whose ConvIdx
        // would also be 0. 'this' got ConvIdx = 1 previously.
        assert(!HasThisConversion || (ConvIdx == 0 && I == 0));
      } else {
        // For members, 'this' got ConvIdx = 0 previously.
        ConvIdx = ThisConversions + I;
      }
      Conversions[ConvIdx]
        = TryCopyInitialization(*this, Args[I], ParamType,
                                SuppressUserConversions,
                                /*InOverloadResolution=*/true,
                                /*AllowObjCWritebackConversion=*/
                                  getLangOpts().ObjCAutoRefCount,
                                AllowExplicit);
      if (Conversions[ConvIdx].isBad())
        return true;
    }
  }

  return false;
}

/// Determine whether this is an allowable conversion from the result
/// of an explicit conversion operator to the expected type, per C++
/// [over.match.conv]p1 and [over.match.ref]p1.
///
/// \param ConvType The return type of the conversion function.
///
/// \param ToType The type we are converting to.
///
/// \param AllowObjCPointerConversion Allow a conversion from one
/// Objective-C pointer to another.
///
/// \returns true if the conversion is allowable, false otherwise.
static bool isAllowableExplicitConversion(Sema &S,
                                          QualType ConvType, QualType ToType,
                                          bool AllowObjCPointerConversion) {
  QualType ToNonRefType = ToType.getNonReferenceType();

  // Easy case: the types are the same.
  if (S.Context.hasSameUnqualifiedType(ConvType, ToNonRefType))
    return true;

  // Allow qualification conversions.
  bool ObjCLifetimeConversion;
  if (S.IsQualificationConversion(ConvType, ToNonRefType, /*CStyle*/false,
                                  ObjCLifetimeConversion))
    return true;

  // If we're not allowed to consider Objective-C pointer conversions,
  // we're done.
  if (!AllowObjCPointerConversion)
    return false;

  // Is this an Objective-C pointer conversion?
  bool IncompatibleObjC = false;
  QualType ConvertedType;
  return S.isObjCPointerConversion(ConvType, ToNonRefType, ConvertedType,
                                   IncompatibleObjC);
}

void Sema::AddConversionCandidate(
    CXXConversionDecl *Conversion, DeclAccessPair FoundDecl,
    CXXRecordDecl *ActingContext, Expr *From, QualType ToType,
    OverloadCandidateSet &CandidateSet, bool AllowObjCConversionOnExplicit,
    bool AllowExplicit, bool AllowResultConversion) {
  assert(!Conversion->getDescribedFunctionTemplate() &&
         "Conversion function templates use AddTemplateConversionCandidate");
  QualType ConvType = Conversion->getConversionType().getNonReferenceType();
  if (!CandidateSet.isNewCandidate(Conversion))
    return;

  // If the conversion function has an undeduced return type, trigger its
  // deduction now.
  if (getLangOpts().CPlusPlus14 && ConvType->isUndeducedType()) {
    if (DeduceReturnType(Conversion, From->getExprLoc()))
      return;
    ConvType = Conversion->getConversionType().getNonReferenceType();
  }

  // If we don't allow any conversion of the result type, ignore conversion
  // functions that don't convert to exactly (possibly cv-qualified) T.
  if (!AllowResultConversion &&
      !Context.hasSameUnqualifiedType(Conversion->getConversionType(), ToType))
    return;

  // Per C++ [over.match.conv]p1, [over.match.ref]p1, an explicit conversion
  // operator is only a candidate if its return type is the target type or
  // can be converted to the target type with a qualification conversion.
  //
  // FIXME: Include such functions in the candidate list and explain why we
  // can't select them.
  if (Conversion->isExplicit() &&
      !isAllowableExplicitConversion(*this, ConvType, ToType,
                                     AllowObjCConversionOnExplicit))
    return;

  // Overload resolution is always an unevaluated context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);

  // Add this candidate
  OverloadCandidate &Candidate = CandidateSet.addCandidate(1);
  Candidate.FoundDecl = FoundDecl;
  Candidate.Function = Conversion;
  Candidate.FinalConversion.setAsIdentityConversion();
  Candidate.FinalConversion.setFromType(ConvType);
  Candidate.FinalConversion.setAllToTypes(ToType);
  Candidate.Viable = true;
  Candidate.ExplicitCallArguments = 1;

  // Explicit functions are not actually candidates at all if we're not
  // allowing them in this context, but keep them around so we can point
  // to them in diagnostics.
  if (!AllowExplicit && Conversion->isExplicit()) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_explicit;
    return;
  }

  // C++ [over.match.funcs]p4:
  //   For conversion functions, the function is considered to be a member of
  //   the class of the implicit implied object argument for the purpose of
  //   defining the type of the implicit object parameter.
  //
  // Determine the implicit conversion sequence for the implicit
  // object parameter.
  QualType ObjectType = From->getType();
  if (const auto *FromPtrType = ObjectType->getAs<PointerType>())
    ObjectType = FromPtrType->getPointeeType();
  const auto *ConversionContext =
      cast<CXXRecordDecl>(ObjectType->castAs<RecordType>()->getDecl());

  // C++23 [over.best.ics.general]
  // However, if the target is [...]
  // - the object parameter of a user-defined conversion function
  // [...] user-defined conversion sequences are not considered.
  Candidate.Conversions[0] = TryObjectArgumentInitialization(
      *this, CandidateSet.getLocation(), From->getType(),
      From->Classify(Context), Conversion, ConversionContext,
      /*InOverloadResolution*/ false, /*ExplicitParameterType=*/QualType(),
      /*SuppressUserConversion*/ true);

  if (Candidate.Conversions[0].isBad()) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_bad_conversion;
    return;
  }

  if (Conversion->getTrailingRequiresClause()) {
    ConstraintSatisfaction Satisfaction;
    if (CheckFunctionConstraints(Conversion, Satisfaction) ||
        !Satisfaction.IsSatisfied) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_constraints_not_satisfied;
      return;
    }
  }

  // We won't go through a user-defined type conversion function to convert a
  // derived to base as such conversions are given Conversion Rank. They only
  // go through a copy constructor. 13.3.3.1.2-p4 [over.ics.user]
  QualType FromCanon
    = Context.getCanonicalType(From->getType().getUnqualifiedType());
  QualType ToCanon = Context.getCanonicalType(ToType).getUnqualifiedType();
  if (FromCanon == ToCanon ||
      IsDerivedFrom(CandidateSet.getLocation(), FromCanon, ToCanon)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_trivial_conversion;
    return;
  }

  // To determine what the conversion from the result of calling the
  // conversion function to the type we're eventually trying to
  // convert to (ToType), we need to synthesize a call to the
  // conversion function and attempt copy initialization from it. This
  // makes sure that we get the right semantics with respect to
  // lvalues/rvalues and the type. Fortunately, we can allocate this
  // call on the stack and we don't need its arguments to be
  // well-formed.
  DeclRefExpr ConversionRef(Context, Conversion, false, Conversion->getType(),
                            VK_LValue, From->getBeginLoc());
  ImplicitCastExpr ConversionFn(ImplicitCastExpr::OnStack,
                                Context.getPointerType(Conversion->getType()),
                                CK_FunctionToPointerDecay, &ConversionRef,
                                VK_PRValue, FPOptionsOverride());

  QualType ConversionType = Conversion->getConversionType();
  if (!isCompleteType(From->getBeginLoc(), ConversionType)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_bad_final_conversion;
    return;
  }

  ExprValueKind VK = Expr::getValueKindForType(ConversionType);

  // Note that it is safe to allocate CallExpr on the stack here because
  // there are 0 arguments (i.e., nothing is allocated using ASTContext's
  // allocator).
  QualType CallResultType = ConversionType.getNonLValueExprType(Context);

  alignas(CallExpr) char Buffer[sizeof(CallExpr) + sizeof(Stmt *)];
  CallExpr *TheTemporaryCall = CallExpr::CreateTemporary(
      Buffer, &ConversionFn, CallResultType, VK, From->getBeginLoc());

  ImplicitConversionSequence ICS =
      TryCopyInitialization(*this, TheTemporaryCall, ToType,
                            /*SuppressUserConversions=*/true,
                            /*InOverloadResolution=*/false,
                            /*AllowObjCWritebackConversion=*/false);

  switch (ICS.getKind()) {
  case ImplicitConversionSequence::StandardConversion:
    Candidate.FinalConversion = ICS.Standard;

    // C++ [over.ics.user]p3:
    //   If the user-defined conversion is specified by a specialization of a
    //   conversion function template, the second standard conversion sequence
    //   shall have exact match rank.
    if (Conversion->getPrimaryTemplate() &&
        GetConversionRank(ICS.Standard.Second) != ICR_Exact_Match) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_final_conversion_not_exact;
      return;
    }

    // C++0x [dcl.init.ref]p5:
    //    In the second case, if the reference is an rvalue reference and
    //    the second standard conversion sequence of the user-defined
    //    conversion sequence includes an lvalue-to-rvalue conversion, the
    //    program is ill-formed.
    if (ToType->isRValueReferenceType() &&
        ICS.Standard.First == ICK_Lvalue_To_Rvalue) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_bad_final_conversion;
      return;
    }
    break;

  case ImplicitConversionSequence::BadConversion:
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_bad_final_conversion;
    return;

  default:
    llvm_unreachable(
           "Can only end up with a standard conversion sequence or failure");
  }

  if (EnableIfAttr *FailedAttr =
          CheckEnableIf(Conversion, CandidateSet.getLocation(), std::nullopt)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_enable_if;
    Candidate.DeductionFailure.Data = FailedAttr;
    return;
  }

  if (isNonViableMultiVersionOverload(Conversion)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_non_default_multiversion_function;
  }
}

void Sema::AddTemplateConversionCandidate(
    FunctionTemplateDecl *FunctionTemplate, DeclAccessPair FoundDecl,
    CXXRecordDecl *ActingDC, Expr *From, QualType ToType,
    OverloadCandidateSet &CandidateSet, bool AllowObjCConversionOnExplicit,
    bool AllowExplicit, bool AllowResultConversion) {
  assert(isa<CXXConversionDecl>(FunctionTemplate->getTemplatedDecl()) &&
         "Only conversion function templates permitted here");

  if (!CandidateSet.isNewCandidate(FunctionTemplate))
    return;

  // If the function template has a non-dependent explicit specification,
  // exclude it now if appropriate; we are not permitted to perform deduction
  // and substitution in this case.
  if (!AllowExplicit && isNonDependentlyExplicit(FunctionTemplate)) {
    OverloadCandidate &Candidate = CandidateSet.addCandidate();
    Candidate.FoundDecl = FoundDecl;
    Candidate.Function = FunctionTemplate->getTemplatedDecl();
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_explicit;
    return;
  }

  QualType ObjectType = From->getType();
  Expr::Classification ObjectClassification = From->Classify(getASTContext());

  TemplateDeductionInfo Info(CandidateSet.getLocation());
  CXXConversionDecl *Specialization = nullptr;
  if (TemplateDeductionResult Result = DeduceTemplateArguments(
          FunctionTemplate, ObjectType, ObjectClassification, ToType,
          Specialization, Info);
      Result != TemplateDeductionResult::Success) {
    OverloadCandidate &Candidate = CandidateSet.addCandidate();
    Candidate.FoundDecl = FoundDecl;
    Candidate.Function = FunctionTemplate->getTemplatedDecl();
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_bad_deduction;
    Candidate.ExplicitCallArguments = 1;
    Candidate.DeductionFailure = MakeDeductionFailureInfo(Context, Result,
                                                          Info);
    return;
  }

  // Add the conversion function template specialization produced by
  // template argument deduction as a candidate.
  assert(Specialization && "Missing function template specialization?");
  AddConversionCandidate(Specialization, FoundDecl, ActingDC, From, ToType,
                         CandidateSet, AllowObjCConversionOnExplicit,
                         AllowExplicit, AllowResultConversion);
}

void Sema::AddSurrogateCandidate(CXXConversionDecl *Conversion,
                                 DeclAccessPair FoundDecl,
                                 CXXRecordDecl *ActingContext,
                                 const FunctionProtoType *Proto,
                                 Expr *Object,
                                 ArrayRef<Expr *> Args,
                                 OverloadCandidateSet& CandidateSet) {
  if (!CandidateSet.isNewCandidate(Conversion))
    return;

  // Overload resolution is always an unevaluated context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);

  OverloadCandidate &Candidate = CandidateSet.addCandidate(Args.size() + 1);
  Candidate.FoundDecl = FoundDecl;
  Candidate.Function = nullptr;
  Candidate.Surrogate = Conversion;
  Candidate.IsSurrogate = true;
  Candidate.Viable = true;
  Candidate.ExplicitCallArguments = Args.size();

  // Determine the implicit conversion sequence for the implicit
  // object parameter.
  ImplicitConversionSequence ObjectInit;
  if (Conversion->hasCXXExplicitFunctionObjectParameter()) {
    ObjectInit = TryCopyInitialization(*this, Object,
                                       Conversion->getParamDecl(0)->getType(),
                                       /*SuppressUserConversions=*/false,
                                       /*InOverloadResolution=*/true, false);
  } else {
    ObjectInit = TryObjectArgumentInitialization(
        *this, CandidateSet.getLocation(), Object->getType(),
        Object->Classify(Context), Conversion, ActingContext);
  }

  if (ObjectInit.isBad()) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_bad_conversion;
    Candidate.Conversions[0] = ObjectInit;
    return;
  }

  // The first conversion is actually a user-defined conversion whose
  // first conversion is ObjectInit's standard conversion (which is
  // effectively a reference binding). Record it as such.
  Candidate.Conversions[0].setUserDefined();
  Candidate.Conversions[0].UserDefined.Before = ObjectInit.Standard;
  Candidate.Conversions[0].UserDefined.EllipsisConversion = false;
  Candidate.Conversions[0].UserDefined.HadMultipleCandidates = false;
  Candidate.Conversions[0].UserDefined.ConversionFunction = Conversion;
  Candidate.Conversions[0].UserDefined.FoundConversionFunction = FoundDecl;
  Candidate.Conversions[0].UserDefined.After
    = Candidate.Conversions[0].UserDefined.Before;
  Candidate.Conversions[0].UserDefined.After.setAsIdentityConversion();

  // Find the
  unsigned NumParams = Proto->getNumParams();

  // (C++ 13.3.2p2): A candidate function having fewer than m
  // parameters is viable only if it has an ellipsis in its parameter
  // list (8.3.5).
  if (Args.size() > NumParams && !Proto->isVariadic()) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_too_many_arguments;
    return;
  }

  // Function types don't have any default arguments, so just check if
  // we have enough arguments.
  if (Args.size() < NumParams) {
    // Not enough arguments.
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_too_few_arguments;
    return;
  }

  // Determine the implicit conversion sequences for each of the
  // arguments.
  for (unsigned ArgIdx = 0, N = Args.size(); ArgIdx != N; ++ArgIdx) {
    if (ArgIdx < NumParams) {
      // (C++ 13.3.2p3): for F to be a viable function, there shall
      // exist for each argument an implicit conversion sequence
      // (13.3.3.1) that converts that argument to the corresponding
      // parameter of F.
      QualType ParamType = Proto->getParamType(ArgIdx);
      Candidate.Conversions[ArgIdx + 1]
        = TryCopyInitialization(*this, Args[ArgIdx], ParamType,
                                /*SuppressUserConversions=*/false,
                                /*InOverloadResolution=*/false,
                                /*AllowObjCWritebackConversion=*/
                                  getLangOpts().ObjCAutoRefCount);
      if (Candidate.Conversions[ArgIdx + 1].isBad()) {
        Candidate.Viable = false;
        Candidate.FailureKind = ovl_fail_bad_conversion;
        return;
      }
    } else {
      // (C++ 13.3.2p2): For the purposes of overload resolution, any
      // argument for which there is no corresponding parameter is
      // considered to ""match the ellipsis" (C+ 13.3.3.1.3).
      Candidate.Conversions[ArgIdx + 1].setEllipsis();
    }
  }

  if (Conversion->getTrailingRequiresClause()) {
    ConstraintSatisfaction Satisfaction;
    if (CheckFunctionConstraints(Conversion, Satisfaction, /*Loc*/ {},
                                 /*ForOverloadResolution*/ true) ||
        !Satisfaction.IsSatisfied) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_constraints_not_satisfied;
      return;
    }
  }

  if (EnableIfAttr *FailedAttr =
          CheckEnableIf(Conversion, CandidateSet.getLocation(), std::nullopt)) {
    Candidate.Viable = false;
    Candidate.FailureKind = ovl_fail_enable_if;
    Candidate.DeductionFailure.Data = FailedAttr;
    return;
  }
}

void Sema::AddNonMemberOperatorCandidates(
    const UnresolvedSetImpl &Fns, ArrayRef<Expr *> Args,
    OverloadCandidateSet &CandidateSet,
    TemplateArgumentListInfo *ExplicitTemplateArgs) {
  for (UnresolvedSetIterator F = Fns.begin(), E = Fns.end(); F != E; ++F) {
    NamedDecl *D = F.getDecl()->getUnderlyingDecl();
    ArrayRef<Expr *> FunctionArgs = Args;

    FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(D);
    FunctionDecl *FD =
        FunTmpl ? FunTmpl->getTemplatedDecl() : cast<FunctionDecl>(D);

    // Don't consider rewritten functions if we're not rewriting.
    if (!CandidateSet.getRewriteInfo().isAcceptableCandidate(FD))
      continue;

    assert(!isa<CXXMethodDecl>(FD) &&
           "unqualified operator lookup found a member function");

    if (FunTmpl) {
      AddTemplateOverloadCandidate(FunTmpl, F.getPair(), ExplicitTemplateArgs,
                                   FunctionArgs, CandidateSet);
      if (CandidateSet.getRewriteInfo().shouldAddReversed(*this, Args, FD))
        AddTemplateOverloadCandidate(
            FunTmpl, F.getPair(), ExplicitTemplateArgs,
            {FunctionArgs[1], FunctionArgs[0]}, CandidateSet, false, false,
            true, ADLCallKind::NotADL, OverloadCandidateParamOrder::Reversed);
    } else {
      if (ExplicitTemplateArgs)
        continue;
      AddOverloadCandidate(FD, F.getPair(), FunctionArgs, CandidateSet);
      if (CandidateSet.getRewriteInfo().shouldAddReversed(*this, Args, FD))
        AddOverloadCandidate(
            FD, F.getPair(), {FunctionArgs[1], FunctionArgs[0]}, CandidateSet,
            false, false, true, false, ADLCallKind::NotADL, std::nullopt,
            OverloadCandidateParamOrder::Reversed);
    }
  }
}

void Sema::AddMemberOperatorCandidates(OverloadedOperatorKind Op,
                                       SourceLocation OpLoc,
                                       ArrayRef<Expr *> Args,
                                       OverloadCandidateSet &CandidateSet,
                                       OverloadCandidateParamOrder PO) {
  DeclarationName OpName = Context.DeclarationNames.getCXXOperatorName(Op);

  // C++ [over.match.oper]p3:
  //   For a unary operator @ with an operand of a type whose
  //   cv-unqualified version is T1, and for a binary operator @ with
  //   a left operand of a type whose cv-unqualified version is T1 and
  //   a right operand of a type whose cv-unqualified version is T2,
  //   three sets of candidate functions, designated member
  //   candidates, non-member candidates and built-in candidates, are
  //   constructed as follows:
  QualType T1 = Args[0]->getType();

  //     -- If T1 is a complete class type or a class currently being
  //        defined, the set of member candidates is the result of the
  //        qualified lookup of T1::operator@ (13.3.1.1.1); otherwise,
  //        the set of member candidates is empty.
  if (const RecordType *T1Rec = T1->getAs<RecordType>()) {
    // Complete the type if it can be completed.
    if (!isCompleteType(OpLoc, T1) && !T1Rec->isBeingDefined())
      return;
    // If the type is neither complete nor being defined, bail out now.
    if (!T1Rec->getDecl()->getDefinition())
      return;

    LookupResult Operators(*this, OpName, OpLoc, LookupOrdinaryName);
    LookupQualifiedName(Operators, T1Rec->getDecl());
    Operators.suppressAccessDiagnostics();

    for (LookupResult::iterator Oper = Operators.begin(),
                                OperEnd = Operators.end();
         Oper != OperEnd; ++Oper) {
      if (Oper->getAsFunction() &&
          PO == OverloadCandidateParamOrder::Reversed &&
          !CandidateSet.getRewriteInfo().shouldAddReversed(
              *this, {Args[1], Args[0]}, Oper->getAsFunction()))
        continue;
      AddMethodCandidate(Oper.getPair(), Args[0]->getType(),
                         Args[0]->Classify(Context), Args.slice(1),
                         CandidateSet, /*SuppressUserConversion=*/false, PO);
    }
  }
}

void Sema::AddBuiltinCandidate(QualType *ParamTys, ArrayRef<Expr *> Args,
                               OverloadCandidateSet& CandidateSet,
                               bool IsAssignmentOperator,
                               unsigned NumContextualBoolArguments) {
  // Overload resolution is always an unevaluated context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);

  // Add this candidate
  OverloadCandidate &Candidate = CandidateSet.addCandidate(Args.size());
  Candidate.FoundDecl = DeclAccessPair::make(nullptr, AS_none);
  Candidate.Function = nullptr;
  std::copy(ParamTys, ParamTys + Args.size(), Candidate.BuiltinParamTypes);

  // Determine the implicit conversion sequences for each of the
  // arguments.
  Candidate.Viable = true;
  Candidate.ExplicitCallArguments = Args.size();
  for (unsigned ArgIdx = 0, N = Args.size(); ArgIdx != N; ++ArgIdx) {
    // C++ [over.match.oper]p4:
    //   For the built-in assignment operators, conversions of the
    //   left operand are restricted as follows:
    //     -- no temporaries are introduced to hold the left operand, and
    //     -- no user-defined conversions are applied to the left
    //        operand to achieve a type match with the left-most
    //        parameter of a built-in candidate.
    //
    // We block these conversions by turning off user-defined
    // conversions, since that is the only way that initialization of
    // a reference to a non-class type can occur from something that
    // is not of the same type.
    if (ArgIdx < NumContextualBoolArguments) {
      assert(ParamTys[ArgIdx] == Context.BoolTy &&
             "Contextual conversion to bool requires bool type");
      Candidate.Conversions[ArgIdx]
        = TryContextuallyConvertToBool(*this, Args[ArgIdx]);
    } else {
      Candidate.Conversions[ArgIdx]
        = TryCopyInitialization(*this, Args[ArgIdx], ParamTys[ArgIdx],
                                ArgIdx == 0 && IsAssignmentOperator,
                                /*InOverloadResolution=*/false,
                                /*AllowObjCWritebackConversion=*/
                                  getLangOpts().ObjCAutoRefCount);
    }
    if (Candidate.Conversions[ArgIdx].isBad()) {
      Candidate.Viable = false;
      Candidate.FailureKind = ovl_fail_bad_conversion;
      break;
    }
  }
}

namespace {

/// BuiltinCandidateTypeSet - A set of types that will be used for the
/// candidate operator functions for built-in operators (C++
/// [over.built]). The types are separated into pointer types and
/// enumeration types.
class BuiltinCandidateTypeSet  {
  /// TypeSet - A set of types.
  typedef llvm::SmallSetVector<QualType, 8> TypeSet;

  /// PointerTypes - The set of pointer types that will be used in the
  /// built-in candidates.
  TypeSet PointerTypes;

  /// MemberPointerTypes - The set of member pointer types that will be
  /// used in the built-in candidates.
  TypeSet MemberPointerTypes;

  /// EnumerationTypes - The set of enumeration types that will be
  /// used in the built-in candidates.
  TypeSet EnumerationTypes;

  /// The set of vector types that will be used in the built-in
  /// candidates.
  TypeSet VectorTypes;

  /// The set of matrix types that will be used in the built-in
  /// candidates.
  TypeSet MatrixTypes;

  /// The set of _BitInt types that will be used in the built-in candidates.
  TypeSet BitIntTypes;

  /// A flag indicating non-record types are viable candidates
  bool HasNonRecordTypes;

  /// A flag indicating whether either arithmetic or enumeration types
  /// were present in the candidate set.
  bool HasArithmeticOrEnumeralTypes;

  /// A flag indicating whether the nullptr type was present in the
  /// candidate set.
  bool HasNullPtrType;

  /// Sema - The semantic analysis instance where we are building the
  /// candidate type set.
  Sema &SemaRef;

  /// Context - The AST context in which we will build the type sets.
  ASTContext &Context;

  bool AddPointerWithMoreQualifiedTypeVariants(QualType Ty,
                                               const Qualifiers &VisibleQuals);
  bool AddMemberPointerWithMoreQualifiedTypeVariants(QualType Ty);

public:
  /// iterator - Iterates through the types that are part of the set.
  typedef TypeSet::iterator iterator;

  BuiltinCandidateTypeSet(Sema &SemaRef)
    : HasNonRecordTypes(false),
      HasArithmeticOrEnumeralTypes(false),
      HasNullPtrType(false),
      SemaRef(SemaRef),
      Context(SemaRef.Context) { }

  void AddTypesConvertedFrom(QualType Ty,
                             SourceLocation Loc,
                             bool AllowUserConversions,
                             bool AllowExplicitConversions,
                             const Qualifiers &VisibleTypeConversionsQuals);

  llvm::iterator_range<iterator> pointer_types() { return PointerTypes; }
  llvm::iterator_range<iterator> member_pointer_types() {
    return MemberPointerTypes;
  }
  llvm::iterator_range<iterator> enumeration_types() {
    return EnumerationTypes;
  }
  llvm::iterator_range<iterator> vector_types() { return VectorTypes; }
  llvm::iterator_range<iterator> matrix_types() { return MatrixTypes; }
  llvm::iterator_range<iterator> bitint_types() { return BitIntTypes; }

  bool containsMatrixType(QualType Ty) const { return MatrixTypes.count(Ty); }
  bool hasNonRecordTypes() { return HasNonRecordTypes; }
  bool hasArithmeticOrEnumeralTypes() { return HasArithmeticOrEnumeralTypes; }
  bool hasNullPtrType() const { return HasNullPtrType; }
};

} // end anonymous namespace

/// AddPointerWithMoreQualifiedTypeVariants - Add the pointer type @p Ty to
/// the set of pointer types along with any more-qualified variants of
/// that type. For example, if @p Ty is "int const *", this routine
/// will add "int const *", "int const volatile *", "int const
/// restrict *", and "int const volatile restrict *" to the set of
/// pointer types. Returns true if the add of @p Ty itself succeeded,
/// false otherwise.
///
/// FIXME: what to do about extended qualifiers?
bool
BuiltinCandidateTypeSet::AddPointerWithMoreQualifiedTypeVariants(QualType Ty,
                                             const Qualifiers &VisibleQuals) {

  // Insert this type.
  if (!PointerTypes.insert(Ty))
    return false;

  QualType PointeeTy;
  const PointerType *PointerTy = Ty->getAs<PointerType>();
  bool buildObjCPtr = false;
  if (!PointerTy) {
    const ObjCObjectPointerType *PTy = Ty->castAs<ObjCObjectPointerType>();
    PointeeTy = PTy->getPointeeType();
    buildObjCPtr = true;
  } else {
    PointeeTy = PointerTy->getPointeeType();
  }

  // Don't add qualified variants of arrays. For one, they're not allowed
  // (the qualifier would sink to the element type), and for another, the
  // only overload situation where it matters is subscript or pointer +- int,
  // and those shouldn't have qualifier variants anyway.
  if (PointeeTy->isArrayType())
    return true;

  unsigned BaseCVR = PointeeTy.getCVRQualifiers();
  bool hasVolatile = VisibleQuals.hasVolatile();
  bool hasRestrict = VisibleQuals.hasRestrict();

  // Iterate through all strict supersets of BaseCVR.
  for (unsigned CVR = BaseCVR+1; CVR <= Qualifiers::CVRMask; ++CVR) {
    if ((CVR | BaseCVR) != CVR) continue;
    // Skip over volatile if no volatile found anywhere in the types.
    if ((CVR & Qualifiers::Volatile) && !hasVolatile) continue;

    // Skip over restrict if no restrict found anywhere in the types, or if
    // the type cannot be restrict-qualified.
    if ((CVR & Qualifiers::Restrict) &&
        (!hasRestrict ||
         (!(PointeeTy->isAnyPointerType() || PointeeTy->isReferenceType()))))
      continue;

    // Build qualified pointee type.
    QualType QPointeeTy = Context.getCVRQualifiedType(PointeeTy, CVR);

    // Build qualified pointer type.
    QualType QPointerTy;
    if (!buildObjCPtr)
      QPointerTy = Context.getPointerType(QPointeeTy);
    else
      QPointerTy = Context.getObjCObjectPointerType(QPointeeTy);

    // Insert qualified pointer type.
    PointerTypes.insert(QPointerTy);
  }

  return true;
}

/// AddMemberPointerWithMoreQualifiedTypeVariants - Add the pointer type @p Ty
/// to the set of pointer types along with any more-qualified variants of
/// that type. For example, if @p Ty is "int const *", this routine
/// will add "int const *", "int const volatile *", "int const
/// restrict *", and "int const volatile restrict *" to the set of
/// pointer types. Returns true if the add of @p Ty itself succeeded,
/// false otherwise.
///
/// FIXME: what to do about extended qualifiers?
bool
BuiltinCandidateTypeSet::AddMemberPointerWithMoreQualifiedTypeVariants(
    QualType Ty) {
  // Insert this type.
  if (!MemberPointerTypes.insert(Ty))
    return false;

  const MemberPointerType *PointerTy = Ty->getAs<MemberPointerType>();
  assert(PointerTy && "type was not a member pointer type!");

  QualType PointeeTy = PointerTy->getPointeeType();
  // Don't add qualified variants of arrays. For one, they're not allowed
  // (the qualifier would sink to the element type), and for another, the
  // only overload situation where it matters is subscript or pointer +- int,
  // and those shouldn't have qualifier variants anyway.
  if (PointeeTy->isArrayType())
    return true;
  const Type *ClassTy = PointerTy->getClass();

  // Iterate through all strict supersets of the pointee type's CVR
  // qualifiers.
  unsigned BaseCVR = PointeeTy.getCVRQualifiers();
  for (unsigned CVR = BaseCVR+1; CVR <= Qualifiers::CVRMask; ++CVR) {
    if ((CVR | BaseCVR) != CVR) continue;

    QualType QPointeeTy = Context.getCVRQualifiedType(PointeeTy, CVR);
    MemberPointerTypes.insert(
      Context.getMemberPointerType(QPointeeTy, ClassTy));
  }

  return true;
}

/// AddTypesConvertedFrom - Add each of the types to which the type @p
/// Ty can be implicit converted to the given set of @p Types. We're
/// primarily interested in pointer types and enumeration types. We also
/// take member pointer types, for the conditional operator.
/// AllowUserConversions is true if we should look at the conversion
/// functions of a class type, and AllowExplicitConversions if we
/// should also include the explicit conversion functions of a class
/// type.
void
BuiltinCandidateTypeSet::AddTypesConvertedFrom(QualType Ty,
                                               SourceLocation Loc,
                                               bool AllowUserConversions,
                                               bool AllowExplicitConversions,
                                               const Qualifiers &VisibleQuals) {
  // Only deal with canonical types.
  Ty = Context.getCanonicalType(Ty);

  // Look through reference types; they aren't part of the type of an
  // expression for the purposes of conversions.
  if (const ReferenceType *RefTy = Ty->getAs<ReferenceType>())
    Ty = RefTy->getPointeeType();

  // If we're dealing with an array type, decay to the pointer.
  if (Ty->isArrayType())
    Ty = SemaRef.Context.getArrayDecayedType(Ty);

  // Otherwise, we don't care about qualifiers on the type.
  Ty = Ty.getLocalUnqualifiedType();

  // Flag if we ever add a non-record type.
  const RecordType *TyRec = Ty->getAs<RecordType>();
  HasNonRecordTypes = HasNonRecordTypes || !TyRec;

  // Flag if we encounter an arithmetic type.
  HasArithmeticOrEnumeralTypes =
    HasArithmeticOrEnumeralTypes || Ty->isArithmeticType();

  if (Ty->isObjCIdType() || Ty->isObjCClassType())
    PointerTypes.insert(Ty);
  else if (Ty->getAs<PointerType>() || Ty->getAs<ObjCObjectPointerType>()) {
    // Insert our type, and its more-qualified variants, into the set
    // of types.
    if (!AddPointerWithMoreQualifiedTypeVariants(Ty, VisibleQuals))
      return;
  } else if (Ty->isMemberPointerType()) {
    // Member pointers are far easier, since the pointee can't be converted.
    if (!AddMemberPointerWithMoreQualifiedTypeVariants(Ty))
      return;
  } else if (Ty->isEnumeralType()) {
    HasArithmeticOrEnumeralTypes = true;
    EnumerationTypes.insert(Ty);
  } else if (Ty->isBitIntType()) {
    HasArithmeticOrEnumeralTypes = true;
    BitIntTypes.insert(Ty);
  } else if (Ty->isVectorType()) {
    // We treat vector types as arithmetic types in many contexts as an
    // extension.
    HasArithmeticOrEnumeralTypes = true;
    VectorTypes.insert(Ty);
  } else if (Ty->isMatrixType()) {
    // Similar to vector types, we treat vector types as arithmetic types in
    // many contexts as an extension.
    HasArithmeticOrEnumeralTypes = true;
    MatrixTypes.insert(Ty);
  } else if (Ty->isNullPtrType()) {
    HasNullPtrType = true;
  } else if (AllowUserConversions && TyRec) {
    // No conversion functions in incomplete types.
    if (!SemaRef.isCompleteType(Loc, Ty))
      return;

    CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(TyRec->getDecl());
    for (NamedDecl *D : ClassDecl->getVisibleConversionFunctions()) {
      if (isa<UsingShadowDecl>(D))
        D = cast<UsingShadowDecl>(D)->getTargetDecl();

      // Skip conversion function templates; they don't tell us anything
      // about which builtin types we can convert to.
      if (isa<FunctionTemplateDecl>(D))
        continue;

      CXXConversionDecl *Conv = cast<CXXConversionDecl>(D);
      if (AllowExplicitConversions || !Conv->isExplicit()) {
        AddTypesConvertedFrom(Conv->getConversionType(), Loc, false, false,
                              VisibleQuals);
      }
    }
  }
}
/// Helper function for adjusting address spaces for the pointer or reference
/// operands of builtin operators depending on the argument.
static QualType AdjustAddressSpaceForBuiltinOperandType(Sema &S, QualType T,
                                                        Expr *Arg) {
  return S.Context.getAddrSpaceQualType(T, Arg->getType().getAddressSpace());
}

/// Helper function for AddBuiltinOperatorCandidates() that adds
/// the volatile- and non-volatile-qualified assignment operators for the
/// given type to the candidate set.
static void AddBuiltinAssignmentOperatorCandidates(Sema &S,
                                                   QualType T,
                                                   ArrayRef<Expr *> Args,
                                    OverloadCandidateSet &CandidateSet) {
  QualType ParamTypes[2];

  // T& operator=(T&, T)
  ParamTypes[0] = S.Context.getLValueReferenceType(
      AdjustAddressSpaceForBuiltinOperandType(S, T, Args[0]));
  ParamTypes[1] = T;
  S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                        /*IsAssignmentOperator=*/true);

  if (!S.Context.getCanonicalType(T).isVolatileQualified()) {
    // volatile T& operator=(volatile T&, T)
    ParamTypes[0] = S.Context.getLValueReferenceType(
        AdjustAddressSpaceForBuiltinOperandType(S, S.Context.getVolatileType(T),
                                                Args[0]));
    ParamTypes[1] = T;
    S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                          /*IsAssignmentOperator=*/true);
  }
}

/// CollectVRQualifiers - This routine returns Volatile/Restrict qualifiers,
/// if any, found in visible type conversion functions found in ArgExpr's type.
static  Qualifiers CollectVRQualifiers(ASTContext &Context, Expr* ArgExpr) {
    Qualifiers VRQuals;
    const RecordType *TyRec;
    if (const MemberPointerType *RHSMPType =
        ArgExpr->getType()->getAs<MemberPointerType>())
      TyRec = RHSMPType->getClass()->getAs<RecordType>();
    else
      TyRec = ArgExpr->getType()->getAs<RecordType>();
    if (!TyRec) {
      // Just to be safe, assume the worst case.
      VRQuals.addVolatile();
      VRQuals.addRestrict();
      return VRQuals;
    }

    CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(TyRec->getDecl());
    if (!ClassDecl->hasDefinition())
      return VRQuals;

    for (NamedDecl *D : ClassDecl->getVisibleConversionFunctions()) {
      if (isa<UsingShadowDecl>(D))
        D = cast<UsingShadowDecl>(D)->getTargetDecl();
      if (CXXConversionDecl *Conv = dyn_cast<CXXConversionDecl>(D)) {
        QualType CanTy = Context.getCanonicalType(Conv->getConversionType());
        if (const ReferenceType *ResTypeRef = CanTy->getAs<ReferenceType>())
          CanTy = ResTypeRef->getPointeeType();
        // Need to go down the pointer/mempointer chain and add qualifiers
        // as see them.
        bool done = false;
        while (!done) {
          if (CanTy.isRestrictQualified())
            VRQuals.addRestrict();
          if (const PointerType *ResTypePtr = CanTy->getAs<PointerType>())
            CanTy = ResTypePtr->getPointeeType();
          else if (const MemberPointerType *ResTypeMPtr =
                CanTy->getAs<MemberPointerType>())
            CanTy = ResTypeMPtr->getPointeeType();
          else
            done = true;
          if (CanTy.isVolatileQualified())
            VRQuals.addVolatile();
          if (VRQuals.hasRestrict() && VRQuals.hasVolatile())
            return VRQuals;
        }
      }
    }
    return VRQuals;
}

// Note: We're currently only handling qualifiers that are meaningful for the
// LHS of compound assignment overloading.
static void forAllQualifierCombinationsImpl(
    QualifiersAndAtomic Available, QualifiersAndAtomic Applied,
    llvm::function_ref<void(QualifiersAndAtomic)> Callback) {
  // _Atomic
  if (Available.hasAtomic()) {
    Available.removeAtomic();
    forAllQualifierCombinationsImpl(Available, Applied.withAtomic(), Callback);
    forAllQualifierCombinationsImpl(Available, Applied, Callback);
    return;
  }

  // volatile
  if (Available.hasVolatile()) {
    Available.removeVolatile();
    assert(!Applied.hasVolatile());
    forAllQualifierCombinationsImpl(Available, Applied.withVolatile(),
                                    Callback);
    forAllQualifierCombinationsImpl(Available, Applied, Callback);
    return;
  }

  Callback(Applied);
}

static void forAllQualifierCombinations(
    QualifiersAndAtomic Quals,
    llvm::function_ref<void(QualifiersAndAtomic)> Callback) {
  return forAllQualifierCombinationsImpl(Quals, QualifiersAndAtomic(),
                                         Callback);
}

static QualType makeQualifiedLValueReferenceType(QualType Base,
                                                 QualifiersAndAtomic Quals,
                                                 Sema &S) {
  if (Quals.hasAtomic())
    Base = S.Context.getAtomicType(Base);
  if (Quals.hasVolatile())
    Base = S.Context.getVolatileType(Base);
  return S.Context.getLValueReferenceType(Base);
}

namespace {

/// Helper class to manage the addition of builtin operator overload
/// candidates. It provides shared state and utility methods used throughout
/// the process, as well as a helper method to add each group of builtin
/// operator overloads from the standard to a candidate set.
class BuiltinOperatorOverloadBuilder {
  // Common instance state available to all overload candidate addition methods.
  Sema &S;
  ArrayRef<Expr *> Args;
  QualifiersAndAtomic VisibleTypeConversionsQuals;
  bool HasArithmeticOrEnumeralCandidateType;
  SmallVectorImpl<BuiltinCandidateTypeSet> &CandidateTypes;
  OverloadCandidateSet &CandidateSet;

  static constexpr int ArithmeticTypesCap = 26;
  SmallVector<CanQualType, ArithmeticTypesCap> ArithmeticTypes;

  // Define some indices used to iterate over the arithmetic types in
  // ArithmeticTypes.  The "promoted arithmetic types" are the arithmetic
  // types are that preserved by promotion (C++ [over.built]p2).
  unsigned FirstIntegralType,
           LastIntegralType;
  unsigned FirstPromotedIntegralType,
           LastPromotedIntegralType;
  unsigned FirstPromotedArithmeticType,
           LastPromotedArithmeticType;
  unsigned NumArithmeticTypes;

  void InitArithmeticTypes() {
    // Start of promoted types.
    FirstPromotedArithmeticType = 0;
    ArithmeticTypes.push_back(S.Context.FloatTy);
    ArithmeticTypes.push_back(S.Context.DoubleTy);
    ArithmeticTypes.push_back(S.Context.LongDoubleTy);
    if (S.Context.getTargetInfo().hasFloat128Type())
      ArithmeticTypes.push_back(S.Context.Float128Ty);
    if (S.Context.getTargetInfo().hasIbm128Type())
      ArithmeticTypes.push_back(S.Context.Ibm128Ty);

    // Start of integral types.
    FirstIntegralType = ArithmeticTypes.size();
    FirstPromotedIntegralType = ArithmeticTypes.size();
    ArithmeticTypes.push_back(S.Context.IntTy);
    ArithmeticTypes.push_back(S.Context.LongTy);
    ArithmeticTypes.push_back(S.Context.LongLongTy);
    if (S.Context.getTargetInfo().hasInt128Type() ||
        (S.Context.getAuxTargetInfo() &&
         S.Context.getAuxTargetInfo()->hasInt128Type()))
      ArithmeticTypes.push_back(S.Context.Int128Ty);
    ArithmeticTypes.push_back(S.Context.UnsignedIntTy);
    ArithmeticTypes.push_back(S.Context.UnsignedLongTy);
    ArithmeticTypes.push_back(S.Context.UnsignedLongLongTy);
    if (S.Context.getTargetInfo().hasInt128Type() ||
        (S.Context.getAuxTargetInfo() &&
         S.Context.getAuxTargetInfo()->hasInt128Type()))
      ArithmeticTypes.push_back(S.Context.UnsignedInt128Ty);

    /// We add candidates for the unique, unqualified _BitInt types present in
    /// the candidate type set. The candidate set already handled ensuring the
    /// type is unqualified and canonical, but because we're adding from N
    /// different sets, we need to do some extra work to unique things. Insert
    /// the candidates into a unique set, then move from that set into the list
    /// of arithmetic types.
    llvm::SmallSetVector<CanQualType, 2> BitIntCandidates;
    llvm::for_each(CandidateTypes, [&BitIntCandidates](
                                       BuiltinCandidateTypeSet &Candidate) {
      for (QualType BitTy : Candidate.bitint_types())
        BitIntCandidates.insert(CanQualType::CreateUnsafe(BitTy));
    });
    llvm::move(BitIntCandidates, std::back_inserter(ArithmeticTypes));
    LastPromotedIntegralType = ArithmeticTypes.size();
    LastPromotedArithmeticType = ArithmeticTypes.size();
    // End of promoted types.

    ArithmeticTypes.push_back(S.Context.BoolTy);
    ArithmeticTypes.push_back(S.Context.CharTy);
    ArithmeticTypes.push_back(S.Context.WCharTy);
    if (S.Context.getLangOpts().Char8)
      ArithmeticTypes.push_back(S.Context.Char8Ty);
    ArithmeticTypes.push_back(S.Context.Char16Ty);
    ArithmeticTypes.push_back(S.Context.Char32Ty);
    ArithmeticTypes.push_back(S.Context.SignedCharTy);
    ArithmeticTypes.push_back(S.Context.ShortTy);
    ArithmeticTypes.push_back(S.Context.UnsignedCharTy);
    ArithmeticTypes.push_back(S.Context.UnsignedShortTy);
    LastIntegralType = ArithmeticTypes.size();
    NumArithmeticTypes = ArithmeticTypes.size();
    // End of integral types.
    // FIXME: What about complex? What about half?

    // We don't know for sure how many bit-precise candidates were involved, so
    // we subtract those from the total when testing whether we're under the
    // cap or not.
    assert(ArithmeticTypes.size() - BitIntCandidates.size() <=
               ArithmeticTypesCap &&
           "Enough inline storage for all arithmetic types.");
  }

  /// Helper method to factor out the common pattern of adding overloads
  /// for '++' and '--' builtin operators.
  void addPlusPlusMinusMinusStyleOverloads(QualType CandidateTy,
                                           bool HasVolatile,
                                           bool HasRestrict) {
    QualType ParamTypes[2] = {
      S.Context.getLValueReferenceType(CandidateTy),
      S.Context.IntTy
    };

    // Non-volatile version.
    S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);

    // Use a heuristic to reduce number of builtin candidates in the set:
    // add volatile version only if there are conversions to a volatile type.
    if (HasVolatile) {
      ParamTypes[0] =
        S.Context.getLValueReferenceType(
          S.Context.getVolatileType(CandidateTy));
      S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
    }

    // Add restrict version only if there are conversions to a restrict type
    // and our candidate type is a non-restrict-qualified pointer.
    if (HasRestrict && CandidateTy->isAnyPointerType() &&
        !CandidateTy.isRestrictQualified()) {
      ParamTypes[0]
        = S.Context.getLValueReferenceType(
            S.Context.getCVRQualifiedType(CandidateTy, Qualifiers::Restrict));
      S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);

      if (HasVolatile) {
        ParamTypes[0]
          = S.Context.getLValueReferenceType(
              S.Context.getCVRQualifiedType(CandidateTy,
                                            (Qualifiers::Volatile |
                                             Qualifiers::Restrict)));
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
      }
    }

  }

  /// Helper to add an overload candidate for a binary builtin with types \p L
  /// and \p R.
  void AddCandidate(QualType L, QualType R) {
    QualType LandR[2] = {L, R};
    S.AddBuiltinCandidate(LandR, Args, CandidateSet);
  }

public:
  BuiltinOperatorOverloadBuilder(
    Sema &S, ArrayRef<Expr *> Args,
    QualifiersAndAtomic VisibleTypeConversionsQuals,
    bool HasArithmeticOrEnumeralCandidateType,
    SmallVectorImpl<BuiltinCandidateTypeSet> &CandidateTypes,
    OverloadCandidateSet &CandidateSet)
    : S(S), Args(Args),
      VisibleTypeConversionsQuals(VisibleTypeConversionsQuals),
      HasArithmeticOrEnumeralCandidateType(
        HasArithmeticOrEnumeralCandidateType),
      CandidateTypes(CandidateTypes),
      CandidateSet(CandidateSet) {

    InitArithmeticTypes();
  }

  // Increment is deprecated for bool since C++17.
  //
  // C++ [over.built]p3:
  //
  //   For every pair (T, VQ), where T is an arithmetic type other
  //   than bool, and VQ is either volatile or empty, there exist
  //   candidate operator functions of the form
  //
  //       VQ T&      operator++(VQ T&);
  //       T          operator++(VQ T&, int);
  //
  // C++ [over.built]p4:
  //
  //   For every pair (T, VQ), where T is an arithmetic type other
  //   than bool, and VQ is either volatile or empty, there exist
  //   candidate operator functions of the form
  //
  //       VQ T&      operator--(VQ T&);
  //       T          operator--(VQ T&, int);
  void addPlusPlusMinusMinusArithmeticOverloads(OverloadedOperatorKind Op) {
    if (!HasArithmeticOrEnumeralCandidateType)
      return;

    for (unsigned Arith = 0; Arith < NumArithmeticTypes; ++Arith) {
      const auto TypeOfT = ArithmeticTypes[Arith];
      if (TypeOfT == S.Context.BoolTy) {
        if (Op == OO_MinusMinus)
          continue;
        if (Op == OO_PlusPlus && S.getLangOpts().CPlusPlus17)
          continue;
      }
      addPlusPlusMinusMinusStyleOverloads(
        TypeOfT,
        VisibleTypeConversionsQuals.hasVolatile(),
        VisibleTypeConversionsQuals.hasRestrict());
    }
  }

  // C++ [over.built]p5:
  //
  //   For every pair (T, VQ), where T is a cv-qualified or
  //   cv-unqualified object type, and VQ is either volatile or
  //   empty, there exist candidate operator functions of the form
  //
  //       T*VQ&      operator++(T*VQ&);
  //       T*VQ&      operator--(T*VQ&);
  //       T*         operator++(T*VQ&, int);
  //       T*         operator--(T*VQ&, int);
  void addPlusPlusMinusMinusPointerOverloads() {
    for (QualType PtrTy : CandidateTypes[0].pointer_types()) {
      // Skip pointer types that aren't pointers to object types.
      if (!PtrTy->getPointeeType()->isObjectType())
        continue;

      addPlusPlusMinusMinusStyleOverloads(
          PtrTy,
          (!PtrTy.isVolatileQualified() &&
           VisibleTypeConversionsQuals.hasVolatile()),
          (!PtrTy.isRestrictQualified() &&
           VisibleTypeConversionsQuals.hasRestrict()));
    }
  }

  // C++ [over.built]p6:
  //   For every cv-qualified or cv-unqualified object type T, there
  //   exist candidate operator functions of the form
  //
  //       T&         operator*(T*);
  //
  // C++ [over.built]p7:
  //   For every function type T that does not have cv-qualifiers or a
  //   ref-qualifier, there exist candidate operator functions of the form
  //       T&         operator*(T*);
  void addUnaryStarPointerOverloads() {
    for (QualType ParamTy : CandidateTypes[0].pointer_types()) {
      QualType PointeeTy = ParamTy->getPointeeType();
      if (!PointeeTy->isObjectType() && !PointeeTy->isFunctionType())
        continue;

      if (const FunctionProtoType *Proto =PointeeTy->getAs<FunctionProtoType>())
        if (Proto->getMethodQuals() || Proto->getRefQualifier())
          continue;

      S.AddBuiltinCandidate(&ParamTy, Args, CandidateSet);
    }
  }

  // C++ [over.built]p9:
  //  For every promoted arithmetic type T, there exist candidate
  //  operator functions of the form
  //
  //       T         operator+(T);
  //       T         operator-(T);
  void addUnaryPlusOrMinusArithmeticOverloads() {
    if (!HasArithmeticOrEnumeralCandidateType)
      return;

    for (unsigned Arith = FirstPromotedArithmeticType;
         Arith < LastPromotedArithmeticType; ++Arith) {
      QualType ArithTy = ArithmeticTypes[Arith];
      S.AddBuiltinCandidate(&ArithTy, Args, CandidateSet);
    }

    // Extension: We also add these operators for vector types.
    for (QualType VecTy : CandidateTypes[0].vector_types())
      S.AddBuiltinCandidate(&VecTy, Args, CandidateSet);
  }

  // C++ [over.built]p8:
  //   For every type T, there exist candidate operator functions of
  //   the form
  //
  //       T*         operator+(T*);
  void addUnaryPlusPointerOverloads() {
    for (QualType ParamTy : CandidateTypes[0].pointer_types())
      S.AddBuiltinCandidate(&ParamTy, Args, CandidateSet);
  }

  // C++ [over.built]p10:
  //   For every promoted integral type T, there exist candidate
  //   operator functions of the form
  //
  //        T         operator~(T);
  void addUnaryTildePromotedIntegralOverloads() {
    if (!HasArithmeticOrEnumeralCandidateType)
      return;

    for (unsigned Int = FirstPromotedIntegralType;
         Int < LastPromotedIntegralType; ++Int) {
      QualType IntTy = ArithmeticTypes[Int];
      S.AddBuiltinCandidate(&IntTy, Args, CandidateSet);
    }

    // Extension: We also add this operator for vector types.
    for (QualType VecTy : CandidateTypes[0].vector_types())
      S.AddBuiltinCandidate(&VecTy, Args, CandidateSet);
  }

  // C++ [over.match.oper]p16:
  //   For every pointer to member type T or type std::nullptr_t, there
  //   exist candidate operator functions of the form
  //
  //        bool operator==(T,T);
  //        bool operator!=(T,T);
  void addEqualEqualOrNotEqualMemberPointerOrNullptrOverloads() {
    /// Set of (canonical) types that we've already handled.
    llvm::SmallPtrSet<QualType, 8> AddedTypes;

    for (unsigned ArgIdx = 0, N = Args.size(); ArgIdx != N; ++ArgIdx) {
      for (QualType MemPtrTy : CandidateTypes[ArgIdx].member_pointer_types()) {
        // Don't add the same builtin candidate twice.
        if (!AddedTypes.insert(S.Context.getCanonicalType(MemPtrTy)).second)
          continue;

        QualType ParamTypes[2] = {MemPtrTy, MemPtrTy};
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
      }

      if (CandidateTypes[ArgIdx].hasNullPtrType()) {
        CanQualType NullPtrTy = S.Context.getCanonicalType(S.Context.NullPtrTy);
        if (AddedTypes.insert(NullPtrTy).second) {
          QualType ParamTypes[2] = { NullPtrTy, NullPtrTy };
          S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
        }
      }
    }
  }

  // C++ [over.built]p15:
  //
  //   For every T, where T is an enumeration type or a pointer type,
  //   there exist candidate operator functions of the form
  //
  //        bool       operator<(T, T);
  //        bool       operator>(T, T);
  //        bool       operator<=(T, T);
  //        bool       operator>=(T, T);
  //        bool       operator==(T, T);
  //        bool       operator!=(T, T);
  //           R       operator<=>(T, T)
  void addGenericBinaryPointerOrEnumeralOverloads(bool IsSpaceship) {
    // C++ [over.match.oper]p3:
    //   [...]the built-in candidates include all of the candidate operator
    //   functions defined in 13.6 that, compared to the given operator, [...]
    //   do not have the same parameter-type-list as any non-template non-member
    //   candidate.
    //
    // Note that in practice, this only affects enumeration types because there
    // aren't any built-in candidates of record type, and a user-defined operator
    // must have an operand of record or enumeration type. Also, the only other
    // overloaded operator with enumeration arguments, operator=,
    // cannot be overloaded for enumeration types, so this is the only place
    // where we must suppress candidates like this.
    llvm::DenseSet<std::pair<CanQualType, CanQualType> >
      UserDefinedBinaryOperators;

    for (unsigned ArgIdx = 0, N = Args.size(); ArgIdx != N; ++ArgIdx) {
      if (!CandidateTypes[ArgIdx].enumeration_types().empty()) {
        for (OverloadCandidateSet::iterator C = CandidateSet.begin(),
                                         CEnd = CandidateSet.end();
             C != CEnd; ++C) {
          if (!C->Viable || !C->Function || C->Function->getNumParams() != 2)
            continue;

          if (C->Function->isFunctionTemplateSpecialization())
            continue;

          // We interpret "same parameter-type-list" as applying to the
          // "synthesized candidate, with the order of the two parameters
          // reversed", not to the original function.
          bool Reversed = C->isReversed();
          QualType FirstParamType = C->Function->getParamDecl(Reversed ? 1 : 0)
                                        ->getType()
                                        .getUnqualifiedType();
          QualType SecondParamType = C->Function->getParamDecl(Reversed ? 0 : 1)
                                         ->getType()
                                         .getUnqualifiedType();

          // Skip if either parameter isn't of enumeral type.
          if (!FirstParamType->isEnumeralType() ||
              !SecondParamType->isEnumeralType())
            continue;

          // Add this operator to the set of known user-defined operators.
          UserDefinedBinaryOperators.insert(
            std::make_pair(S.Context.getCanonicalType(FirstParamType),
                           S.Context.getCanonicalType(SecondParamType)));
        }
      }
    }

    /// Set of (canonical) types that we've already handled.
    llvm::SmallPtrSet<QualType, 8> AddedTypes;

    for (unsigned ArgIdx = 0, N = Args.size(); ArgIdx != N; ++ArgIdx) {
      for (QualType PtrTy : CandidateTypes[ArgIdx].pointer_types()) {
        // Don't add the same builtin candidate twice.
        if (!AddedTypes.insert(S.Context.getCanonicalType(PtrTy)).second)
          continue;
        if (IsSpaceship && PtrTy->isFunctionPointerType())
          continue;

        QualType ParamTypes[2] = {PtrTy, PtrTy};
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
      }
      for (QualType EnumTy : CandidateTypes[ArgIdx].enumeration_types()) {
        CanQualType CanonType = S.Context.getCanonicalType(EnumTy);

        // Don't add the same builtin candidate twice, or if a user defined
        // candidate exists.
        if (!AddedTypes.insert(CanonType).second ||
            UserDefinedBinaryOperators.count(std::make_pair(CanonType,
                                                            CanonType)))
          continue;
        QualType ParamTypes[2] = {EnumTy, EnumTy};
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
      }
    }
  }

  // C++ [over.built]p13:
  //
  //   For every cv-qualified or cv-unqualified object type T
  //   there exist candidate operator functions of the form
  //
  //      T*         operator+(T*, ptrdiff_t);
  //      T&         operator[](T*, ptrdiff_t);    [BELOW]
  //      T*         operator-(T*, ptrdiff_t);
  //      T*         operator+(ptrdiff_t, T*);
  //      T&         operator[](ptrdiff_t, T*);    [BELOW]
  //
  // C++ [over.built]p14:
  //
  //   For every T, where T is a pointer to object type, there
  //   exist candidate operator functions of the form
  //
  //      ptrdiff_t  operator-(T, T);
  void addBinaryPlusOrMinusPointerOverloads(OverloadedOperatorKind Op) {
    /// Set of (canonical) types that we've already handled.
    llvm::SmallPtrSet<QualType, 8> AddedTypes;

    for (int Arg = 0; Arg < 2; ++Arg) {
      QualType AsymmetricParamTypes[2] = {
        S.Context.getPointerDiffType(),
        S.Context.getPointerDiffType(),
      };
      for (QualType PtrTy : CandidateTypes[Arg].pointer_types()) {
        QualType PointeeTy = PtrTy->getPointeeType();
        if (!PointeeTy->isObjectType())
          continue;

        AsymmetricParamTypes[Arg] = PtrTy;
        if (Arg == 0 || Op == OO_Plus) {
          // operator+(T*, ptrdiff_t) or operator-(T*, ptrdiff_t)
          // T* operator+(ptrdiff_t, T*);
          S.AddBuiltinCandidate(AsymmetricParamTypes, Args, CandidateSet);
        }
        if (Op == OO_Minus) {
          // ptrdiff_t operator-(T, T);
          if (!AddedTypes.insert(S.Context.getCanonicalType(PtrTy)).second)
            continue;

          QualType ParamTypes[2] = {PtrTy, PtrTy};
          S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
        }
      }
    }
  }

  // C++ [over.built]p12:
  //
  //   For every pair of promoted arithmetic types L and R, there
  //   exist candidate operator functions of the form
  //
  //        LR         operator*(L, R);
  //        LR         operator/(L, R);
  //        LR         operator+(L, R);
  //        LR         operator-(L, R);
  //        bool       operator<(L, R);
  //        bool       operator>(L, R);
  //        bool       operator<=(L, R);
  //        bool       operator>=(L, R);
  //        bool       operator==(L, R);
  //        bool       operator!=(L, R);
  //
  //   where LR is the result of the usual arithmetic conversions
  //   between types L and R.
  //
  // C++ [over.built]p24:
  //
  //   For every pair of promoted arithmetic types L and R, there exist
  //   candidate operator functions of the form
  //
  //        LR       operator?(bool, L, R);
  //
  //   where LR is the result of the usual arithmetic conversions
  //   between types L and R.
  // Our candidates ignore the first parameter.
  void addGenericBinaryArithmeticOverloads() {
    if (!HasArithmeticOrEnumeralCandidateType)
      return;

    for (unsigned Left = FirstPromotedArithmeticType;
         Left < LastPromotedArithmeticType; ++Left) {
      for (unsigned Right = FirstPromotedArithmeticType;
           Right < LastPromotedArithmeticType; ++Right) {
        QualType LandR[2] = { ArithmeticTypes[Left],
                              ArithmeticTypes[Right] };
        S.AddBuiltinCandidate(LandR, Args, CandidateSet);
      }
    }

    // Extension: Add the binary operators ==, !=, <, <=, >=, >, *, /, and the
    // conditional operator for vector types.
    for (QualType Vec1Ty : CandidateTypes[0].vector_types())
      for (QualType Vec2Ty : CandidateTypes[1].vector_types()) {
        QualType LandR[2] = {Vec1Ty, Vec2Ty};
        S.AddBuiltinCandidate(LandR, Args, CandidateSet);
      }
  }

  /// Add binary operator overloads for each candidate matrix type M1, M2:
  ///  * (M1, M1) -> M1
  ///  * (M1, M1.getElementType()) -> M1
  ///  * (M2.getElementType(), M2) -> M2
  ///  * (M2, M2) -> M2 // Only if M2 is not part of CandidateTypes[0].
  void addMatrixBinaryArithmeticOverloads() {
    if (!HasArithmeticOrEnumeralCandidateType)
      return;

    for (QualType M1 : CandidateTypes[0].matrix_types()) {
      AddCandidate(M1, cast<MatrixType>(M1)->getElementType());
      AddCandidate(M1, M1);
    }

    for (QualType M2 : CandidateTypes[1].matrix_types()) {
      AddCandidate(cast<MatrixType>(M2)->getElementType(), M2);
      if (!CandidateTypes[0].containsMatrixType(M2))
        AddCandidate(M2, M2);
    }
  }

  // C++2a [over.built]p14:
  //
  //   For every integral type T there exists a candidate operator function
  //   of the form
  //
  //        std::strong_ordering operator<=>(T, T)
  //
  // C++2a [over.built]p15:
  //
  //   For every pair of floating-point types L and R, there exists a candidate
  //   operator function of the form
  //
  //       std::partial_ordering operator<=>(L, R);
  //
  // FIXME: The current specification for integral types doesn't play nice with
  // the direction of p0946r0, which allows mixed integral and unscoped-enum
  // comparisons. Under the current spec this can lead to ambiguity during
  // overload resolution. For example:
  //
  //   enum A : int {a};
  //   auto x = (a <=> (long)42);
  //
  //   error: call is ambiguous for arguments 'A' and 'long'.
  //   note: candidate operator<=>(int, int)
  //   note: candidate operator<=>(long, long)
  //
  // To avoid this error, this function deviates from the specification and adds
  // the mixed overloads `operator<=>(L, R)` where L and R are promoted
  // arithmetic types (the same as the generic relational overloads).
  //
  // For now this function acts as a placeholder.
  void addThreeWayArithmeticOverloads() {
    addGenericBinaryArithmeticOverloads();
  }

  // C++ [over.built]p17:
  //
  //   For every pair of promoted integral types L and R, there
  //   exist candidate operator functions of the form
  //
  //      LR         operator%(L, R);
  //      LR         operator&(L, R);
  //      LR         operator^(L, R);
  //      LR         operator|(L, R);
  //      L          operator<<(L, R);
  //      L          operator>>(L, R);
  //
  //   where LR is the result of the usual arithmetic conversions
  //   between types L and R.
  void addBinaryBitwiseArithmeticOverloads() {
    if (!HasArithmeticOrEnumeralCandidateType)
      return;

    for (unsigned Left = FirstPromotedIntegralType;
         Left < LastPromotedIntegralType; ++Left) {
      for (unsigned Right = FirstPromotedIntegralType;
           Right < LastPromotedIntegralType; ++Right) {
        QualType LandR[2] = { ArithmeticTypes[Left],
                              ArithmeticTypes[Right] };
        S.AddBuiltinCandidate(LandR, Args, CandidateSet);
      }
    }
  }

  // C++ [over.built]p20:
  //
  //   For every pair (T, VQ), where T is an enumeration or
  //   pointer to member type and VQ is either volatile or
  //   empty, there exist candidate operator functions of the form
  //
  //        VQ T&      operator=(VQ T&, T);
  void addAssignmentMemberPointerOrEnumeralOverloads() {
    /// Set of (canonical) types that we've already handled.
    llvm::SmallPtrSet<QualType, 8> AddedTypes;

    for (unsigned ArgIdx = 0; ArgIdx < 2; ++ArgIdx) {
      for (QualType EnumTy : CandidateTypes[ArgIdx].enumeration_types()) {
        if (!AddedTypes.insert(S.Context.getCanonicalType(EnumTy)).second)
          continue;

        AddBuiltinAssignmentOperatorCandidates(S, EnumTy, Args, CandidateSet);
      }

      for (QualType MemPtrTy : CandidateTypes[ArgIdx].member_pointer_types()) {
        if (!AddedTypes.insert(S.Context.getCanonicalType(MemPtrTy)).second)
          continue;

        AddBuiltinAssignmentOperatorCandidates(S, MemPtrTy, Args, CandidateSet);
      }
    }
  }

  // C++ [over.built]p19:
  //
  //   For every pair (T, VQ), where T is any type and VQ is either
  //   volatile or empty, there exist candidate operator functions
  //   of the form
  //
  //        T*VQ&      operator=(T*VQ&, T*);
  //
  // C++ [over.built]p21:
  //
  //   For every pair (T, VQ), where T is a cv-qualified or
  //   cv-unqualified object type and VQ is either volatile or
  //   empty, there exist candidate operator functions of the form
  //
  //        T*VQ&      operator+=(T*VQ&, ptrdiff_t);
  //        T*VQ&      operator-=(T*VQ&, ptrdiff_t);
  void addAssignmentPointerOverloads(bool isEqualOp) {
    /// Set of (canonical) types that we've already handled.
    llvm::SmallPtrSet<QualType, 8> AddedTypes;

    for (QualType PtrTy : CandidateTypes[0].pointer_types()) {
      // If this is operator=, keep track of the builtin candidates we added.
      if (isEqualOp)
        AddedTypes.insert(S.Context.getCanonicalType(PtrTy));
      else if (!PtrTy->getPointeeType()->isObjectType())
        continue;

      // non-volatile version
      QualType ParamTypes[2] = {
          S.Context.getLValueReferenceType(PtrTy),
          isEqualOp ? PtrTy : S.Context.getPointerDiffType(),
      };
      S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                            /*IsAssignmentOperator=*/ isEqualOp);

      bool NeedVolatile = !PtrTy.isVolatileQualified() &&
                          VisibleTypeConversionsQuals.hasVolatile();
      if (NeedVolatile) {
        // volatile version
        ParamTypes[0] =
            S.Context.getLValueReferenceType(S.Context.getVolatileType(PtrTy));
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                              /*IsAssignmentOperator=*/isEqualOp);
      }

      if (!PtrTy.isRestrictQualified() &&
          VisibleTypeConversionsQuals.hasRestrict()) {
        // restrict version
        ParamTypes[0] =
            S.Context.getLValueReferenceType(S.Context.getRestrictType(PtrTy));
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                              /*IsAssignmentOperator=*/isEqualOp);

        if (NeedVolatile) {
          // volatile restrict version
          ParamTypes[0] =
              S.Context.getLValueReferenceType(S.Context.getCVRQualifiedType(
                  PtrTy, (Qualifiers::Volatile | Qualifiers::Restrict)));
          S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                                /*IsAssignmentOperator=*/isEqualOp);
        }
      }
    }

    if (isEqualOp) {
      for (QualType PtrTy : CandidateTypes[1].pointer_types()) {
        // Make sure we don't add the same candidate twice.
        if (!AddedTypes.insert(S.Context.getCanonicalType(PtrTy)).second)
          continue;

        QualType ParamTypes[2] = {
            S.Context.getLValueReferenceType(PtrTy),
            PtrTy,
        };

        // non-volatile version
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                              /*IsAssignmentOperator=*/true);

        bool NeedVolatile = !PtrTy.isVolatileQualified() &&
                            VisibleTypeConversionsQuals.hasVolatile();
        if (NeedVolatile) {
          // volatile version
          ParamTypes[0] = S.Context.getLValueReferenceType(
              S.Context.getVolatileType(PtrTy));
          S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                                /*IsAssignmentOperator=*/true);
        }

        if (!PtrTy.isRestrictQualified() &&
            VisibleTypeConversionsQuals.hasRestrict()) {
          // restrict version
          ParamTypes[0] = S.Context.getLValueReferenceType(
              S.Context.getRestrictType(PtrTy));
          S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                                /*IsAssignmentOperator=*/true);

          if (NeedVolatile) {
            // volatile restrict version
            ParamTypes[0] =
                S.Context.getLValueReferenceType(S.Context.getCVRQualifiedType(
                    PtrTy, (Qualifiers::Volatile | Qualifiers::Restrict)));
            S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                                  /*IsAssignmentOperator=*/true);
          }
        }
      }
    }
  }

  // C++ [over.built]p18:
  //
  //   For every triple (L, VQ, R), where L is an arithmetic type,
  //   VQ is either volatile or empty, and R is a promoted
  //   arithmetic type, there exist candidate operator functions of
  //   the form
  //
  //        VQ L&      operator=(VQ L&, R);
  //        VQ L&      operator*=(VQ L&, R);
  //        VQ L&      operator/=(VQ L&, R);
  //        VQ L&      operator+=(VQ L&, R);
  //        VQ L&      operator-=(VQ L&, R);
  void addAssignmentArithmeticOverloads(bool isEqualOp) {
    if (!HasArithmeticOrEnumeralCandidateType)
      return;

    for (unsigned Left = 0; Left < NumArithmeticTypes; ++Left) {
      for (unsigned Right = FirstPromotedArithmeticType;
           Right < LastPromotedArithmeticType; ++Right) {
        QualType ParamTypes[2];
        ParamTypes[1] = ArithmeticTypes[Right];
        auto LeftBaseTy = AdjustAddressSpaceForBuiltinOperandType(
            S, ArithmeticTypes[Left], Args[0]);

        forAllQualifierCombinations(
            VisibleTypeConversionsQuals, [&](QualifiersAndAtomic Quals) {
              ParamTypes[0] =
                  makeQualifiedLValueReferenceType(LeftBaseTy, Quals, S);
              S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                                    /*IsAssignmentOperator=*/isEqualOp);
            });
      }
    }

    // Extension: Add the binary operators =, +=, -=, *=, /= for vector types.
    for (QualType Vec1Ty : CandidateTypes[0].vector_types())
      for (QualType Vec2Ty : CandidateTypes[0].vector_types()) {
        QualType ParamTypes[2];
        ParamTypes[1] = Vec2Ty;
        // Add this built-in operator as a candidate (VQ is empty).
        ParamTypes[0] = S.Context.getLValueReferenceType(Vec1Ty);
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                              /*IsAssignmentOperator=*/isEqualOp);

        // Add this built-in operator as a candidate (VQ is 'volatile').
        if (VisibleTypeConversionsQuals.hasVolatile()) {
          ParamTypes[0] = S.Context.getVolatileType(Vec1Ty);
          ParamTypes[0] = S.Context.getLValueReferenceType(ParamTypes[0]);
          S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                                /*IsAssignmentOperator=*/isEqualOp);
        }
      }
  }

  // C++ [over.built]p22:
  //
  //   For every triple (L, VQ, R), where L is an integral type, VQ
  //   is either volatile or empty, and R is a promoted integral
  //   type, there exist candidate operator functions of the form
  //
  //        VQ L&       operator%=(VQ L&, R);
  //        VQ L&       operator<<=(VQ L&, R);
  //        VQ L&       operator>>=(VQ L&, R);
  //        VQ L&       operator&=(VQ L&, R);
  //        VQ L&       operator^=(VQ L&, R);
  //        VQ L&       operator|=(VQ L&, R);
  void addAssignmentIntegralOverloads() {
    if (!HasArithmeticOrEnumeralCandidateType)
      return;

    for (unsigned Left = FirstIntegralType; Left < LastIntegralType; ++Left) {
      for (unsigned Right = FirstPromotedIntegralType;
           Right < LastPromotedIntegralType; ++Right) {
        QualType ParamTypes[2];
        ParamTypes[1] = ArithmeticTypes[Right];
        auto LeftBaseTy = AdjustAddressSpaceForBuiltinOperandType(
            S, ArithmeticTypes[Left], Args[0]);

        forAllQualifierCombinations(
            VisibleTypeConversionsQuals, [&](QualifiersAndAtomic Quals) {
              ParamTypes[0] =
                  makeQualifiedLValueReferenceType(LeftBaseTy, Quals, S);
              S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
            });
      }
    }
  }

  // C++ [over.operator]p23:
  //
  //   There also exist candidate operator functions of the form
  //
  //        bool        operator!(bool);
  //        bool        operator&&(bool, bool);
  //        bool        operator||(bool, bool);
  void addExclaimOverload() {
    QualType ParamTy = S.Context.BoolTy;
    S.AddBuiltinCandidate(&ParamTy, Args, CandidateSet,
                          /*IsAssignmentOperator=*/false,
                          /*NumContextualBoolArguments=*/1);
  }
  void addAmpAmpOrPipePipeOverload() {
    QualType ParamTypes[2] = { S.Context.BoolTy, S.Context.BoolTy };
    S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet,
                          /*IsAssignmentOperator=*/false,
                          /*NumContextualBoolArguments=*/2);
  }

  // C++ [over.built]p13:
  //
  //   For every cv-qualified or cv-unqualified object type T there
  //   exist candidate operator functions of the form
  //
  //        T*         operator+(T*, ptrdiff_t);     [ABOVE]
  //        T&         operator[](T*, ptrdiff_t);
  //        T*         operator-(T*, ptrdiff_t);     [ABOVE]
  //        T*         operator+(ptrdiff_t, T*);     [ABOVE]
  //        T&         operator[](ptrdiff_t, T*);
  void addSubscriptOverloads() {
    for (QualType PtrTy : CandidateTypes[0].pointer_types()) {
      QualType ParamTypes[2] = {PtrTy, S.Context.getPointerDiffType()};
      QualType PointeeType = PtrTy->getPointeeType();
      if (!PointeeType->isObjectType())
        continue;

      // T& operator[](T*, ptrdiff_t)
      S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
    }

    for (QualType PtrTy : CandidateTypes[1].pointer_types()) {
      QualType ParamTypes[2] = {S.Context.getPointerDiffType(), PtrTy};
      QualType PointeeType = PtrTy->getPointeeType();
      if (!PointeeType->isObjectType())
        continue;

      // T& operator[](ptrdiff_t, T*)
      S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
    }
  }

  // C++ [over.built]p11:
  //    For every quintuple (C1, C2, T, CV1, CV2), where C2 is a class type,
  //    C1 is the same type as C2 or is a derived class of C2, T is an object
  //    type or a function type, and CV1 and CV2 are cv-qualifier-seqs,
  //    there exist candidate operator functions of the form
  //
  //      CV12 T& operator->*(CV1 C1*, CV2 T C2::*);
  //
  //    where CV12 is the union of CV1 and CV2.
  void addArrowStarOverloads() {
    for (QualType PtrTy : CandidateTypes[0].pointer_types()) {
      QualType C1Ty = PtrTy;
      QualType C1;
      QualifierCollector Q1;
      C1 = QualType(Q1.strip(C1Ty->getPointeeType()), 0);
      if (!isa<RecordType>(C1))
        continue;
      // heuristic to reduce number of builtin candidates in the set.
      // Add volatile/restrict version only if there are conversions to a
      // volatile/restrict type.
      if (!VisibleTypeConversionsQuals.hasVolatile() && Q1.hasVolatile())
        continue;
      if (!VisibleTypeConversionsQuals.hasRestrict() && Q1.hasRestrict())
        continue;
      for (QualType MemPtrTy : CandidateTypes[1].member_pointer_types()) {
        const MemberPointerType *mptr = cast<MemberPointerType>(MemPtrTy);
        QualType C2 = QualType(mptr->getClass(), 0);
        C2 = C2.getUnqualifiedType();
        if (C1 != C2 && !S.IsDerivedFrom(CandidateSet.getLocation(), C1, C2))
          break;
        QualType ParamTypes[2] = {PtrTy, MemPtrTy};
        // build CV12 T&
        QualType T = mptr->getPointeeType();
        if (!VisibleTypeConversionsQuals.hasVolatile() &&
            T.isVolatileQualified())
          continue;
        if (!VisibleTypeConversionsQuals.hasRestrict() &&
            T.isRestrictQualified())
          continue;
        T = Q1.apply(S.Context, T);
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
      }
    }
  }

  // Note that we don't consider the first argument, since it has been
  // contextually converted to bool long ago. The candidates below are
  // therefore added as binary.
  //
  // C++ [over.built]p25:
  //   For every type T, where T is a pointer, pointer-to-member, or scoped
  //   enumeration type, there exist candidate operator functions of the form
  //
  //        T        operator?(bool, T, T);
  //
  void addConditionalOperatorOverloads() {
    /// Set of (canonical) types that we've already handled.
    llvm::SmallPtrSet<QualType, 8> AddedTypes;

    for (unsigned ArgIdx = 0; ArgIdx < 2; ++ArgIdx) {
      for (QualType PtrTy : CandidateTypes[ArgIdx].pointer_types()) {
        if (!AddedTypes.insert(S.Context.getCanonicalType(PtrTy)).second)
          continue;

        QualType ParamTypes[2] = {PtrTy, PtrTy};
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
      }

      for (QualType MemPtrTy : CandidateTypes[ArgIdx].member_pointer_types()) {
        if (!AddedTypes.insert(S.Context.getCanonicalType(MemPtrTy)).second)
          continue;

        QualType ParamTypes[2] = {MemPtrTy, MemPtrTy};
        S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
      }

      if (S.getLangOpts().CPlusPlus11) {
        for (QualType EnumTy : CandidateTypes[ArgIdx].enumeration_types()) {
          if (!EnumTy->castAs<EnumType>()->getDecl()->isScoped())
            continue;

          if (!AddedTypes.insert(S.Context.getCanonicalType(EnumTy)).second)
            continue;

          QualType ParamTypes[2] = {EnumTy, EnumTy};
          S.AddBuiltinCandidate(ParamTypes, Args, CandidateSet);
        }
      }
    }
  }
};

} // end anonymous namespace

void Sema::AddBuiltinOperatorCandidates(OverloadedOperatorKind Op,
                                        SourceLocation OpLoc,
                                        ArrayRef<Expr *> Args,
                                        OverloadCandidateSet &CandidateSet) {
  // Find all of the types that the arguments can convert to, but only
  // if the operator we're looking at has built-in operator candidates
  // that make use of these types. Also record whether we encounter non-record
  // candidate types or either arithmetic or enumeral candidate types.
  QualifiersAndAtomic VisibleTypeConversionsQuals;
  VisibleTypeConversionsQuals.addConst();
  for (unsigned ArgIdx = 0, N = Args.size(); ArgIdx != N; ++ArgIdx) {
    VisibleTypeConversionsQuals += CollectVRQualifiers(Context, Args[ArgIdx]);
    if (Args[ArgIdx]->getType()->isAtomicType())
      VisibleTypeConversionsQuals.addAtomic();
  }

  bool HasNonRecordCandidateType = false;
  bool HasArithmeticOrEnumeralCandidateType = false;
  SmallVector<BuiltinCandidateTypeSet, 2> CandidateTypes;
  for (unsigned ArgIdx = 0, N = Args.size(); ArgIdx != N; ++ArgIdx) {
    CandidateTypes.emplace_back(*this);
    CandidateTypes[ArgIdx].AddTypesConvertedFrom(Args[ArgIdx]->getType(),
                                                 OpLoc,
                                                 true,
                                                 (Op == OO_Exclaim ||
                                                  Op == OO_AmpAmp ||
                                                  Op == OO_PipePipe),
                                                 VisibleTypeConversionsQuals);
    HasNonRecordCandidateType = HasNonRecordCandidateType ||
        CandidateTypes[ArgIdx].hasNonRecordTypes();
    HasArithmeticOrEnumeralCandidateType =
        HasArithmeticOrEnumeralCandidateType ||
        CandidateTypes[ArgIdx].hasArithmeticOrEnumeralTypes();
  }

  // Exit early when no non-record types have been added to the candidate set
  // for any of the arguments to the operator.
  //
  // We can't exit early for !, ||, or &&, since there we have always have
  // 'bool' overloads.
  if (!HasNonRecordCandidateType &&
      !(Op == OO_Exclaim || Op == OO_AmpAmp || Op == OO_PipePipe))
    return;

  // Setup an object to manage the common state for building overloads.
  BuiltinOperatorOverloadBuilder OpBuilder(*this, Args,
                                           VisibleTypeConversionsQuals,
                                           HasArithmeticOrEnumeralCandidateType,
                                           CandidateTypes, CandidateSet);

  // Dispatch over the operation to add in only those overloads which apply.
  switch (Op) {
  case OO_None:
  case NUM_OVERLOADED_OPERATORS:
    llvm_unreachable("Expected an overloaded operator");

  case OO_New:
  case OO_Delete:
  case OO_Array_New:
  case OO_Array_Delete:
  case OO_Call:
    llvm_unreachable(
                    "Special operators don't use AddBuiltinOperatorCandidates");

  case OO_Comma:
  case OO_Arrow:
  case OO_Coawait:
    // C++ [over.match.oper]p3:
    //   -- For the operator ',', the unary operator '&', the
    //      operator '->', or the operator 'co_await', the
    //      built-in candidates set is empty.
    break;

  case OO_Plus: // '+' is either unary or binary
    if (Args.size() == 1)
      OpBuilder.addUnaryPlusPointerOverloads();
    [[fallthrough]];

  case OO_Minus: // '-' is either unary or binary
    if (Args.size() == 1) {
      OpBuilder.addUnaryPlusOrMinusArithmeticOverloads();
    } else {
      OpBuilder.addBinaryPlusOrMinusPointerOverloads(Op);
      OpBuilder.addGenericBinaryArithmeticOverloads();
      OpBuilder.addMatrixBinaryArithmeticOverloads();
    }
    break;

  case OO_Star: // '*' is either unary or binary
    if (Args.size() == 1)
      OpBuilder.addUnaryStarPointerOverloads();
    else {
      OpBuilder.addGenericBinaryArithmeticOverloads();
      OpBuilder.addMatrixBinaryArithmeticOverloads();
    }
    break;

  case OO_Slash:
    OpBuilder.addGenericBinaryArithmeticOverloads();
    break;

  case OO_PlusPlus:
  case OO_MinusMinus:
    OpBuilder.addPlusPlusMinusMinusArithmeticOverloads(Op);
    OpBuilder.addPlusPlusMinusMinusPointerOverloads();
    break;

  case OO_EqualEqual:
  case OO_ExclaimEqual:
    OpBuilder.addEqualEqualOrNotEqualMemberPointerOrNullptrOverloads();
    OpBuilder.addGenericBinaryPointerOrEnumeralOverloads(/*IsSpaceship=*/false);
    OpBuilder.addGenericBinaryArithmeticOverloads();
    break;

  case OO_Less:
  case OO_Greater:
  case OO_LessEqual:
  case OO_GreaterEqual:
    OpBuilder.addGenericBinaryPointerOrEnumeralOverloads(/*IsSpaceship=*/false);
    OpBuilder.addGenericBinaryArithmeticOverloads();
    break;

  case OO_Spaceship:
    OpBuilder.addGenericBinaryPointerOrEnumeralOverloads(/*IsSpaceship=*/true);
    OpBuilder.addThreeWayArithmeticOverloads();
    break;

  case OO_Percent:
  case OO_Caret:
  case OO_Pipe:
  case OO_LessLess:
  case OO_GreaterGreater:
    OpBuilder.addBinaryBitwiseArithmeticOverloads();
    break;

  case OO_Amp: // '&' is either unary or binary
    if (Args.size() == 1)
      // C++ [over.match.oper]p3:
      //   -- For the operator ',', the unary operator '&', or the
      //      operator '->', the built-in candidates set is empty.
      break;

    OpBuilder.addBinaryBitwiseArithmeticOverloads();
    break;

  case OO_Tilde:
    OpBuilder.addUnaryTildePromotedIntegralOverloads();
    break;

  case OO_Equal:
    OpBuilder.addAssignmentMemberPointerOrEnumeralOverloads();
    [[fallthrough]];

  case OO_PlusEqual:
  case OO_MinusEqual:
    OpBuilder.addAssignmentPointerOverloads(Op == OO_Equal);
    [[fallthrough]];

  case OO_StarEqual:
  case OO_SlashEqual:
    OpBuilder.addAssignmentArithmeticOverloads(Op == OO_Equal);
    break;

  case OO_PercentEqual:
  case OO_LessLessEqual:
  case OO_GreaterGreaterEqual:
  case OO_AmpEqual:
  case OO_CaretEqual:
  case OO_PipeEqual:
    OpBuilder.addAssignmentIntegralOverloads();
    break;

  case OO_Exclaim:
    OpBuilder.addExclaimOverload();
    break;

  case OO_AmpAmp:
  case OO_PipePipe:
    OpBuilder.addAmpAmpOrPipePipeOverload();
    break;

  case OO_Subscript:
    if (Args.size() == 2)
      OpBuilder.addSubscriptOverloads();
    break;

  case OO_ArrowStar:
    OpBuilder.addArrowStarOverloads();
    break;

  case OO_Conditional:
    OpBuilder.addConditionalOperatorOverloads();
    OpBuilder.addGenericBinaryArithmeticOverloads();
    break;
  }
}

void
Sema::AddArgumentDependentLookupCandidates(DeclarationName Name,
                                           SourceLocation Loc,
                                           ArrayRef<Expr *> Args,
                                 TemplateArgumentListInfo *ExplicitTemplateArgs,
                                           OverloadCandidateSet& CandidateSet,
                                           bool PartialOverloading) {
  ADLResult Fns;

  // FIXME: This approach for uniquing ADL results (and removing
  // redundant candidates from the set) relies on pointer-equality,
  // which means we need to key off the canonical decl.  However,
  // always going back to the canonical decl might not get us the
  // right set of default arguments.  What default arguments are
  // we supposed to consider on ADL candidates, anyway?

  // FIXME: Pass in the explicit template arguments?
  ArgumentDependentLookup(Name, Loc, Args, Fns);

  // Erase all of the candidates we already knew about.
  for (OverloadCandidateSet::iterator Cand = CandidateSet.begin(),
                                   CandEnd = CandidateSet.end();
       Cand != CandEnd; ++Cand)
    if (Cand->Function) {
      Fns.erase(Cand->Function);
      if (FunctionTemplateDecl *FunTmpl = Cand->Function->getPrimaryTemplate())
        Fns.erase(FunTmpl);
    }

  // For each of the ADL candidates we found, add it to the overload
  // set.
  for (ADLResult::iterator I = Fns.begin(), E = Fns.end(); I != E; ++I) {
    DeclAccessPair FoundDecl = DeclAccessPair::make(*I, AS_none);

    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(*I)) {
      if (ExplicitTemplateArgs)
        continue;

      AddOverloadCandidate(
          FD, FoundDecl, Args, CandidateSet, /*SuppressUserConversions=*/false,
          PartialOverloading, /*AllowExplicit=*/true,
          /*AllowExplicitConversion=*/false, ADLCallKind::UsesADL);
      if (CandidateSet.getRewriteInfo().shouldAddReversed(*this, Args, FD)) {
        AddOverloadCandidate(
            FD, FoundDecl, {Args[1], Args[0]}, CandidateSet,
            /*SuppressUserConversions=*/false, PartialOverloading,
            /*AllowExplicit=*/true, /*AllowExplicitConversion=*/false,
            ADLCallKind::UsesADL, std::nullopt,
            OverloadCandidateParamOrder::Reversed);
      }
    } else {
      auto *FTD = cast<FunctionTemplateDecl>(*I);
      AddTemplateOverloadCandidate(
          FTD, FoundDecl, ExplicitTemplateArgs, Args, CandidateSet,
          /*SuppressUserConversions=*/false, PartialOverloading,
          /*AllowExplicit=*/true, ADLCallKind::UsesADL);
      if (CandidateSet.getRewriteInfo().shouldAddReversed(
              *this, Args, FTD->getTemplatedDecl())) {
        AddTemplateOverloadCandidate(
            FTD, FoundDecl, ExplicitTemplateArgs, {Args[1], Args[0]},
            CandidateSet, /*SuppressUserConversions=*/false, PartialOverloading,
            /*AllowExplicit=*/true, ADLCallKind::UsesADL,
            OverloadCandidateParamOrder::Reversed);
      }
    }
  }
}

namespace {
enum class Comparison { Equal, Better, Worse };
}

/// Compares the enable_if attributes of two FunctionDecls, for the purposes of
/// overload resolution.
///
/// Cand1's set of enable_if attributes are said to be "better" than Cand2's iff
/// Cand1's first N enable_if attributes have precisely the same conditions as
/// Cand2's first N enable_if attributes (where N = the number of enable_if
/// attributes on Cand2), and Cand1 has more than N enable_if attributes.
///
/// Note that you can have a pair of candidates such that Cand1's enable_if
/// attributes are worse than Cand2's, and Cand2's enable_if attributes are
/// worse than Cand1's.
static Comparison compareEnableIfAttrs(const Sema &S, const FunctionDecl *Cand1,
                                       const FunctionDecl *Cand2) {
  // Common case: One (or both) decls don't have enable_if attrs.
  bool Cand1Attr = Cand1->hasAttr<EnableIfAttr>();
  bool Cand2Attr = Cand2->hasAttr<EnableIfAttr>();
  if (!Cand1Attr || !Cand2Attr) {
    if (Cand1Attr == Cand2Attr)
      return Comparison::Equal;
    return Cand1Attr ? Comparison::Better : Comparison::Worse;
  }

  auto Cand1Attrs = Cand1->specific_attrs<EnableIfAttr>();
  auto Cand2Attrs = Cand2->specific_attrs<EnableIfAttr>();

  llvm::FoldingSetNodeID Cand1ID, Cand2ID;
  for (auto Pair : zip_longest(Cand1Attrs, Cand2Attrs)) {
    std::optional<EnableIfAttr *> Cand1A = std::get<0>(Pair);
    std::optional<EnableIfAttr *> Cand2A = std::get<1>(Pair);

    // It's impossible for Cand1 to be better than (or equal to) Cand2 if Cand1
    // has fewer enable_if attributes than Cand2, and vice versa.
    if (!Cand1A)
      return Comparison::Worse;
    if (!Cand2A)
      return Comparison::Better;

    Cand1ID.clear();
    Cand2ID.clear();

    (*Cand1A)->getCond()->Profile(Cand1ID, S.getASTContext(), true);
    (*Cand2A)->getCond()->Profile(Cand2ID, S.getASTContext(), true);
    if (Cand1ID != Cand2ID)
      return Comparison::Worse;
  }

  return Comparison::Equal;
}

static Comparison
isBetterMultiversionCandidate(const OverloadCandidate &Cand1,
                              const OverloadCandidate &Cand2) {
  if (!Cand1.Function || !Cand1.Function->isMultiVersion() || !Cand2.Function ||
      !Cand2.Function->isMultiVersion())
    return Comparison::Equal;

  // If both are invalid, they are equal. If one of them is invalid, the other
  // is better.
  if (Cand1.Function->isInvalidDecl()) {
    if (Cand2.Function->isInvalidDecl())
      return Comparison::Equal;
    return Comparison::Worse;
  }
  if (Cand2.Function->isInvalidDecl())
    return Comparison::Better;

  // If this is a cpu_dispatch/cpu_specific multiversion situation, prefer
  // cpu_dispatch, else arbitrarily based on the identifiers.
  bool Cand1CPUDisp = Cand1.Function->hasAttr<CPUDispatchAttr>();
  bool Cand2CPUDisp = Cand2.Function->hasAttr<CPUDispatchAttr>();
  const auto *Cand1CPUSpec = Cand1.Function->getAttr<CPUSpecificAttr>();
  const auto *Cand2CPUSpec = Cand2.Function->getAttr<CPUSpecificAttr>();

  if (!Cand1CPUDisp && !Cand2CPUDisp && !Cand1CPUSpec && !Cand2CPUSpec)
    return Comparison::Equal;

  if (Cand1CPUDisp && !Cand2CPUDisp)
    return Comparison::Better;
  if (Cand2CPUDisp && !Cand1CPUDisp)
    return Comparison::Worse;

  if (Cand1CPUSpec && Cand2CPUSpec) {
    if (Cand1CPUSpec->cpus_size() != Cand2CPUSpec->cpus_size())
      return Cand1CPUSpec->cpus_size() < Cand2CPUSpec->cpus_size()
                 ? Comparison::Better
                 : Comparison::Worse;

    std::pair<CPUSpecificAttr::cpus_iterator, CPUSpecificAttr::cpus_iterator>
        FirstDiff = std::mismatch(
            Cand1CPUSpec->cpus_begin(), Cand1CPUSpec->cpus_end(),
            Cand2CPUSpec->cpus_begin(),
            [](const IdentifierInfo *LHS, const IdentifierInfo *RHS) {
              return LHS->getName() == RHS->getName();
            });

    assert(FirstDiff.first != Cand1CPUSpec->cpus_end() &&
           "Two different cpu-specific versions should not have the same "
           "identifier list, otherwise they'd be the same decl!");
    return (*FirstDiff.first)->getName() < (*FirstDiff.second)->getName()
               ? Comparison::Better
               : Comparison::Worse;
  }
  llvm_unreachable("No way to get here unless both had cpu_dispatch");
}

/// Compute the type of the implicit object parameter for the given function,
/// if any. Returns std::nullopt if there is no implicit object parameter, and a
/// null QualType if there is a 'matches anything' implicit object parameter.
static std::optional<QualType>
getImplicitObjectParamType(ASTContext &Context, const FunctionDecl *F) {
  if (!isa<CXXMethodDecl>(F) || isa<CXXConstructorDecl>(F))
    return std::nullopt;

  auto *M = cast<CXXMethodDecl>(F);
  // Static member functions' object parameters match all types.
  if (M->isStatic())
    return QualType();
  return M->getFunctionObjectParameterReferenceType();
}

// As a Clang extension, allow ambiguity among F1 and F2 if they represent
// represent the same entity.
static bool allowAmbiguity(ASTContext &Context, const FunctionDecl *F1,
                           const FunctionDecl *F2) {
  if (declaresSameEntity(F1, F2))
    return true;
  auto PT1 = F1->getPrimaryTemplate();
  auto PT2 = F2->getPrimaryTemplate();
  if (PT1 && PT2) {
    if (declaresSameEntity(PT1, PT2) ||
        declaresSameEntity(PT1->getInstantiatedFromMemberTemplate(),
                           PT2->getInstantiatedFromMemberTemplate()))
      return true;
  }
  // TODO: It is not clear whether comparing parameters is necessary (i.e.
  // different functions with same params). Consider removing this (as no test
  // fail w/o it).
  auto NextParam = [&](const FunctionDecl *F, unsigned &I, bool First) {
    if (First) {
      if (std::optional<QualType> T = getImplicitObjectParamType(Context, F))
        return *T;
    }
    assert(I < F->getNumParams());
    return F->getParamDecl(I++)->getType();
  };

  unsigned F1NumParams = F1->getNumParams() + isa<CXXMethodDecl>(F1);
  unsigned F2NumParams = F2->getNumParams() + isa<CXXMethodDecl>(F2);

  if (F1NumParams != F2NumParams)
    return false;

  unsigned I1 = 0, I2 = 0;
  for (unsigned I = 0; I != F1NumParams; ++I) {
    QualType T1 = NextParam(F1, I1, I == 0);
    QualType T2 = NextParam(F2, I2, I == 0);
    assert(!T1.isNull() && !T2.isNull() && "Unexpected null param types");
    if (!Context.hasSameUnqualifiedType(T1, T2))
      return false;
  }
  return true;
}

/// We're allowed to use constraints partial ordering only if the candidates
/// have the same parameter types:
/// [over.match.best.general]p2.6
/// F1 and F2 are non-template functions with the same
/// non-object-parameter-type-lists, and F1 is more constrained than F2 [...]
static bool sameFunctionParameterTypeLists(Sema &S,
                                           const OverloadCandidate &Cand1,
                                           const OverloadCandidate &Cand2) {
  if (!Cand1.Function || !Cand2.Function)
    return false;

  FunctionDecl *Fn1 = Cand1.Function;
  FunctionDecl *Fn2 = Cand2.Function;

  if (Fn1->isVariadic() != Fn2->isVariadic())
    return false;

  if (!S.FunctionNonObjectParamTypesAreEqual(
          Fn1, Fn2, nullptr, Cand1.isReversed() ^ Cand2.isReversed()))
    return false;

  auto *Mem1 = dyn_cast<CXXMethodDecl>(Fn1);
  auto *Mem2 = dyn_cast<CXXMethodDecl>(Fn2);
  if (Mem1 && Mem2) {
    // if they are member functions, both are direct members of the same class,
    // and
    if (Mem1->getParent() != Mem2->getParent())
      return false;
    // if both are non-static member functions, they have the same types for
    // their object parameters
    if (Mem1->isInstance() && Mem2->isInstance() &&
        !S.getASTContext().hasSameType(
            Mem1->getFunctionObjectParameterReferenceType(),
            Mem1->getFunctionObjectParameterReferenceType()))
      return false;
  }
  return true;
}

/// isBetterOverloadCandidate - Determines whether the first overload
/// candidate is a better candidate than the second (C++ 13.3.3p1).
bool clang::isBetterOverloadCandidate(
    Sema &S, const OverloadCandidate &Cand1, const OverloadCandidate &Cand2,
    SourceLocation Loc, OverloadCandidateSet::CandidateSetKind Kind) {
  // Define viable functions to be better candidates than non-viable
  // functions.
  if (!Cand2.Viable)
    return Cand1.Viable;
  else if (!Cand1.Viable)
    return false;

  // [CUDA] A function with 'never' preference is marked not viable, therefore
  // is never shown up here. The worst preference shown up here is 'wrong side',
  // e.g. an H function called by a HD function in device compilation. This is
  // valid AST as long as the HD function is not emitted, e.g. it is an inline
  // function which is called only by an H function. A deferred diagnostic will
  // be triggered if it is emitted. However a wrong-sided function is still
  // a viable candidate here.
  //
  // If Cand1 can be emitted and Cand2 cannot be emitted in the current
  // context, Cand1 is better than Cand2. If Cand1 can not be emitted and Cand2
  // can be emitted, Cand1 is not better than Cand2. This rule should have
  // precedence over other rules.
  //
  // If both Cand1 and Cand2 can be emitted, or neither can be emitted, then
  // other rules should be used to determine which is better. This is because
  // host/device based overloading resolution is mostly for determining
  // viability of a function. If two functions are both viable, other factors
  // should take precedence in preference, e.g. the standard-defined preferences
  // like argument conversion ranks or enable_if partial-ordering. The
  // preference for pass-object-size parameters is probably most similar to a
  // type-based-overloading decision and so should take priority.
  //
  // If other rules cannot determine which is better, CUDA preference will be
  // used again to determine which is better.
  //
  // TODO: Currently IdentifyPreference does not return correct values
  // for functions called in global variable initializers due to missing
  // correct context about device/host. Therefore we can only enforce this
  // rule when there is a caller. We should enforce this rule for functions
  // in global variable initializers once proper context is added.
  //
  // TODO: We can only enable the hostness based overloading resolution when
  // -fgpu-exclude-wrong-side-overloads is on since this requires deferring
  // overloading resolution diagnostics.
  if (S.getLangOpts().CUDA && Cand1.Function && Cand2.Function &&
      S.getLangOpts().GPUExcludeWrongSideOverloads) {
    if (FunctionDecl *Caller = S.getCurFunctionDecl(/*AllowLambda=*/true)) {
      bool IsCallerImplicitHD = SemaCUDA::isImplicitHostDeviceFunction(Caller);
      bool IsCand1ImplicitHD =
          SemaCUDA::isImplicitHostDeviceFunction(Cand1.Function);
      bool IsCand2ImplicitHD =
          SemaCUDA::isImplicitHostDeviceFunction(Cand2.Function);
      auto P1 = S.CUDA().IdentifyPreference(Caller, Cand1.Function);
      auto P2 = S.CUDA().IdentifyPreference(Caller, Cand2.Function);
      assert(P1 != SemaCUDA::CFP_Never && P2 != SemaCUDA::CFP_Never);
      // The implicit HD function may be a function in a system header which
      // is forced by pragma. In device compilation, if we prefer HD candidates
      // over wrong-sided candidates, overloading resolution may change, which
      // may result in non-deferrable diagnostics. As a workaround, we let
      // implicit HD candidates take equal preference as wrong-sided candidates.
      // This will preserve the overloading resolution.
      // TODO: We still need special handling of implicit HD functions since
      // they may incur other diagnostics to be deferred. We should make all
      // host/device related diagnostics deferrable and remove special handling
      // of implicit HD functions.
      auto EmitThreshold =
          (S.getLangOpts().CUDAIsDevice && IsCallerImplicitHD &&
           (IsCand1ImplicitHD || IsCand2ImplicitHD))
              ? SemaCUDA::CFP_Never
              : SemaCUDA::CFP_WrongSide;
      auto Cand1Emittable = P1 > EmitThreshold;
      auto Cand2Emittable = P2 > EmitThreshold;
      if (Cand1Emittable && !Cand2Emittable)
        return true;
      if (!Cand1Emittable && Cand2Emittable)
        return false;
    }
  }

  // C++ [over.match.best]p1: (Changed in C++23)
  //
  //   -- if F is a static member function, ICS1(F) is defined such
  //      that ICS1(F) is neither better nor worse than ICS1(G) for
  //      any function G, and, symmetrically, ICS1(G) is neither
  //      better nor worse than ICS1(F).
  unsigned StartArg = 0;
  if (Cand1.IgnoreObjectArgument || Cand2.IgnoreObjectArgument)
    StartArg = 1;

  auto IsIllFormedConversion = [&](const ImplicitConversionSequence &ICS) {
    // We don't allow incompatible pointer conversions in C++.
    if (!S.getLangOpts().CPlusPlus)
      return ICS.isStandard() &&
             ICS.Standard.Second == ICK_Incompatible_Pointer_Conversion;

    // The only ill-formed conversion we allow in C++ is the string literal to
    // char* conversion, which is only considered ill-formed after C++11.
    return S.getLangOpts().CPlusPlus11 && !S.getLangOpts().WritableStrings &&
           hasDeprecatedStringLiteralToCharPtrConversion(ICS);
  };

  // Define functions that don't require ill-formed conversions for a given
  // argument to be better candidates than functions that do.
  unsigned NumArgs = Cand1.Conversions.size();
  assert(Cand2.Conversions.size() == NumArgs && "Overload candidate mismatch");
  bool HasBetterConversion = false;
  for (unsigned ArgIdx = StartArg; ArgIdx < NumArgs; ++ArgIdx) {
    bool Cand1Bad = IsIllFormedConversion(Cand1.Conversions[ArgIdx]);
    bool Cand2Bad = IsIllFormedConversion(Cand2.Conversions[ArgIdx]);
    if (Cand1Bad != Cand2Bad) {
      if (Cand1Bad)
        return false;
      HasBetterConversion = true;
    }
  }

  if (HasBetterConversion)
    return true;

  // C++ [over.match.best]p1:
  //   A viable function F1 is defined to be a better function than another
  //   viable function F2 if for all arguments i, ICSi(F1) is not a worse
  //   conversion sequence than ICSi(F2), and then...
  bool HasWorseConversion = false;
  for (unsigned ArgIdx = StartArg; ArgIdx < NumArgs; ++ArgIdx) {
    switch (CompareImplicitConversionSequences(S, Loc,
                                               Cand1.Conversions[ArgIdx],
                                               Cand2.Conversions[ArgIdx])) {
    case ImplicitConversionSequence::Better:
      // Cand1 has a better conversion sequence.
      HasBetterConversion = true;
      break;

    case ImplicitConversionSequence::Worse:
      if (Cand1.Function && Cand2.Function &&
          Cand1.isReversed() != Cand2.isReversed() &&
          allowAmbiguity(S.Context, Cand1.Function, Cand2.Function)) {
        // Work around large-scale breakage caused by considering reversed
        // forms of operator== in C++20:
        //
        // When comparing a function against a reversed function, if we have a
        // better conversion for one argument and a worse conversion for the
        // other, the implicit conversion sequences are treated as being equally
        // good.
        //
        // This prevents a comparison function from being considered ambiguous
        // with a reversed form that is written in the same way.
        //
        // We diagnose this as an extension from CreateOverloadedBinOp.
        HasWorseConversion = true;
        break;
      }

      // Cand1 can't be better than Cand2.
      return false;

    case ImplicitConversionSequence::Indistinguishable:
      // Do nothing.
      break;
    }
  }

  //    -- for some argument j, ICSj(F1) is a better conversion sequence than
  //       ICSj(F2), or, if not that,
  if (HasBetterConversion && !HasWorseConversion)
    return true;

  //   -- the context is an initialization by user-defined conversion
  //      (see 8.5, 13.3.1.5) and the standard conversion sequence
  //      from the return type of F1 to the destination type (i.e.,
  //      the type of the entity being initialized) is a better
  //      conversion sequence than the standard conversion sequence
  //      from the return type of F2 to the destination type.
  if (Kind == OverloadCandidateSet::CSK_InitByUserDefinedConversion &&
      Cand1.Function && Cand2.Function &&
      isa<CXXConversionDecl>(Cand1.Function) &&
      isa<CXXConversionDecl>(Cand2.Function)) {
    // First check whether we prefer one of the conversion functions over the
    // other. This only distinguishes the results in non-standard, extension
    // cases such as the conversion from a lambda closure type to a function
    // pointer or block.
    ImplicitConversionSequence::CompareKind Result =
        compareConversionFunctions(S, Cand1.Function, Cand2.Function);
    if (Result == ImplicitConversionSequence::Indistinguishable)
      Result = CompareStandardConversionSequences(S, Loc,
                                                  Cand1.FinalConversion,
                                                  Cand2.FinalConversion);

    if (Result != ImplicitConversionSequence::Indistinguishable)
      return Result == ImplicitConversionSequence::Better;

    // FIXME: Compare kind of reference binding if conversion functions
    // convert to a reference type used in direct reference binding, per
    // C++14 [over.match.best]p1 section 2 bullet 3.
  }

  // FIXME: Work around a defect in the C++17 guaranteed copy elision wording,
  // as combined with the resolution to CWG issue 243.
  //
  // When the context is initialization by constructor ([over.match.ctor] or
  // either phase of [over.match.list]), a constructor is preferred over
  // a conversion function.
  if (Kind == OverloadCandidateSet::CSK_InitByConstructor && NumArgs == 1 &&
      Cand1.Function && Cand2.Function &&
      isa<CXXConstructorDecl>(Cand1.Function) !=
          isa<CXXConstructorDecl>(Cand2.Function))
    return isa<CXXConstructorDecl>(Cand1.Function);

  //    -- F1 is a non-template function and F2 is a function template
  //       specialization, or, if not that,
  bool Cand1IsSpecialization = Cand1.Function &&
                               Cand1.Function->getPrimaryTemplate();
  bool Cand2IsSpecialization = Cand2.Function &&
                               Cand2.Function->getPrimaryTemplate();
  if (Cand1IsSpecialization != Cand2IsSpecialization)
    return Cand2IsSpecialization;

  //   -- F1 and F2 are function template specializations, and the function
  //      template for F1 is more specialized than the template for F2
  //      according to the partial ordering rules described in 14.5.5.2, or,
  //      if not that,
  if (Cand1IsSpecialization && Cand2IsSpecialization) {
    const auto *Obj1Context =
        dyn_cast<CXXRecordDecl>(Cand1.FoundDecl->getDeclContext());
    const auto *Obj2Context =
        dyn_cast<CXXRecordDecl>(Cand2.FoundDecl->getDeclContext());
    if (FunctionTemplateDecl *BetterTemplate = S.getMoreSpecializedTemplate(
            Cand1.Function->getPrimaryTemplate(),
            Cand2.Function->getPrimaryTemplate(), Loc,
            isa<CXXConversionDecl>(Cand1.Function) ? TPOC_Conversion
                                                   : TPOC_Call,
            Cand1.ExplicitCallArguments,
            Obj1Context ? QualType(Obj1Context->getTypeForDecl(), 0)
                        : QualType{},
            Obj2Context ? QualType(Obj2Context->getTypeForDecl(), 0)
                        : QualType{},
            Cand1.isReversed() ^ Cand2.isReversed())) {
      return BetterTemplate == Cand1.Function->getPrimaryTemplate();
    }
  }

  //   -— F1 and F2 are non-template functions with the same
  //      parameter-type-lists, and F1 is more constrained than F2 [...],
  if (!Cand1IsSpecialization && !Cand2IsSpecialization &&
      sameFunctionParameterTypeLists(S, Cand1, Cand2) &&
      S.getMoreConstrainedFunction(Cand1.Function, Cand2.Function) ==
          Cand1.Function)
    return true;

  //   -- F1 is a constructor for a class D, F2 is a constructor for a base
  //      class B of D, and for all arguments the corresponding parameters of
  //      F1 and F2 have the same type.
  // FIXME: Implement the "all parameters have the same type" check.
  bool Cand1IsInherited =
      isa_and_nonnull<ConstructorUsingShadowDecl>(Cand1.FoundDecl.getDecl());
  bool Cand2IsInherited =
      isa_and_nonnull<ConstructorUsingShadowDecl>(Cand2.FoundDecl.getDecl());
  if (Cand1IsInherited != Cand2IsInherited)
    return Cand2IsInherited;
  else if (Cand1IsInherited) {
    assert(Cand2IsInherited);
    auto *Cand1Class = cast<CXXRecordDecl>(Cand1.Function->getDeclContext());
    auto *Cand2Class = cast<CXXRecordDecl>(Cand2.Function->getDeclContext());
    if (Cand1Class->isDerivedFrom(Cand2Class))
      return true;
    if (Cand2Class->isDerivedFrom(Cand1Class))
      return false;
    // Inherited from sibling base classes: still ambiguous.
  }

  //   -- F2 is a rewritten candidate (12.4.1.2) and F1 is not
  //   -- F1 and F2 are rewritten candidates, and F2 is a synthesized candidate
  //      with reversed order of parameters and F1 is not
  //
  // We rank reversed + different operator as worse than just reversed, but
  // that comparison can never happen, because we only consider reversing for
  // the maximally-rewritten operator (== or <=>).
  if (Cand1.RewriteKind != Cand2.RewriteKind)
    return Cand1.RewriteKind < Cand2.RewriteKind;

  // Check C++17 tie-breakers for deduction guides.
  {
    auto *Guide1 = dyn_cast_or_null<CXXDeductionGuideDecl>(Cand1.Function);
    auto *Guide2 = dyn_cast_or_null<CXXDeductionGuideDecl>(Cand2.Function);
    if (Guide1 && Guide2) {
      //  -- F1 is generated from a deduction-guide and F2 is not
      if (Guide1->isImplicit() != Guide2->isImplicit())
        return Guide2->isImplicit();

      //  -- F1 is the copy deduction candidate(16.3.1.8) and F2 is not
      if (Guide1->getDeductionCandidateKind() == DeductionCandidate::Copy)
        return true;
      if (Guide2->getDeductionCandidateKind() == DeductionCandidate::Copy)
        return false;

      //  --F1 is generated from a non-template constructor and F2 is generated
      //  from a constructor template
      const auto *Constructor1 = Guide1->getCorrespondingConstructor();
      const auto *Constructor2 = Guide2->getCorrespondingConstructor();
      if (Constructor1 && Constructor2) {
        bool isC1Templated = Constructor1->getTemplatedKind() !=
                             FunctionDecl::TemplatedKind::TK_NonTemplate;
        bool isC2Templated = Constructor2->getTemplatedKind() !=
                             FunctionDecl::TemplatedKind::TK_NonTemplate;
        if (isC1Templated != isC2Templated)
          return isC2Templated;
      }
    }
  }

  // Check for enable_if value-based overload resolution.
  if (Cand1.Function && Cand2.Function) {
    Comparison Cmp = compareEnableIfAttrs(S, Cand1.Function, Cand2.Function);
    if (Cmp != Comparison::Equal)
      return Cmp == Comparison::Better;
  }

  bool HasPS1 = Cand1.Function != nullptr &&
                functionHasPassObjectSizeParams(Cand1.Function);
  bool HasPS2 = Cand2.Function != nullptr &&
                functionHasPassObjectSizeParams(Cand2.Function);
  if (HasPS1 != HasPS2 && HasPS1)
    return true;

  auto MV = isBetterMultiversionCandidate(Cand1, Cand2);
  if (MV == Comparison::Better)
    return true;
  if (MV == Comparison::Worse)
    return false;

  // If other rules cannot determine which is better, CUDA preference is used
  // to determine which is better.
  if (S.getLangOpts().CUDA && Cand1.Function && Cand2.Function) {
    FunctionDecl *Caller = S.getCurFunctionDecl(/*AllowLambda=*/true);
    return S.CUDA().IdentifyPreference(Caller, Cand1.Function) >
           S.CUDA().IdentifyPreference(Caller, Cand2.Function);
  }

  // General member function overloading is handled above, so this only handles
  // constructors with address spaces.
  // This only handles address spaces since C++ has no other
  // qualifier that can be used with constructors.
  const auto *CD1 = dyn_cast_or_null<CXXConstructorDecl>(Cand1.Function);
  const auto *CD2 = dyn_cast_or_null<CXXConstructorDecl>(Cand2.Function);
  if (CD1 && CD2) {
    LangAS AS1 = CD1->getMethodQualifiers().getAddressSpace();
    LangAS AS2 = CD2->getMethodQualifiers().getAddressSpace();
    if (AS1 != AS2) {
      if (Qualifiers::isAddressSpaceSupersetOf(AS2, AS1))
        return true;
      if (Qualifiers::isAddressSpaceSupersetOf(AS1, AS2))
        return false;
    }
  }

  return false;
}

/// Determine whether two declarations are "equivalent" for the purposes of
/// name lookup and overload resolution. This applies when the same internal/no
/// linkage entity is defined by two modules (probably by textually including
/// the same header). In such a case, we don't consider the declarations to
/// declare the same entity, but we also don't want lookups with both
/// declarations visible to be ambiguous in some cases (this happens when using
/// a modularized libstdc++).
bool Sema::isEquivalentInternalLinkageDeclaration(const NamedDecl *A,
                                                  const NamedDecl *B) {
  auto *VA = dyn_cast_or_null<ValueDecl>(A);
  auto *VB = dyn_cast_or_null<ValueDecl>(B);
  if (!VA || !VB)
    return false;

  // The declarations must be declaring the same name as an internal linkage
  // entity in different modules.
  if (!VA->getDeclContext()->getRedeclContext()->Equals(
          VB->getDeclContext()->getRedeclContext()) ||
      getOwningModule(VA) == getOwningModule(VB) ||
      VA->isExternallyVisible() || VB->isExternallyVisible())
    return false;

  // Check that the declarations appear to be equivalent.
  //
  // FIXME: Checking the type isn't really enough to resolve the ambiguity.
  // For constants and functions, we should check the initializer or body is
  // the same. For non-constant variables, we shouldn't allow it at all.
  if (Context.hasSameType(VA->getType(), VB->getType()))
    return true;

  // Enum constants within unnamed enumerations will have different types, but
  // may still be similar enough to be interchangeable for our purposes.
  if (auto *EA = dyn_cast<EnumConstantDecl>(VA)) {
    if (auto *EB = dyn_cast<EnumConstantDecl>(VB)) {
      // Only handle anonymous enums. If the enumerations were named and
      // equivalent, they would have been merged to the same type.
      auto *EnumA = cast<EnumDecl>(EA->getDeclContext());
      auto *EnumB = cast<EnumDecl>(EB->getDeclContext());
      if (EnumA->hasNameForLinkage() || EnumB->hasNameForLinkage() ||
          !Context.hasSameType(EnumA->getIntegerType(),
                               EnumB->getIntegerType()))
        return false;
      // Allow this only if the value is the same for both enumerators.
      return llvm::APSInt::isSameValue(EA->getInitVal(), EB->getInitVal());
    }
  }

  // Nothing else is sufficiently similar.
  return false;
}

void Sema::diagnoseEquivalentInternalLinkageDeclarations(
    SourceLocation Loc, const NamedDecl *D, ArrayRef<const NamedDecl *> Equiv) {
  assert(D && "Unknown declaration");
  Diag(Loc, diag::ext_equivalent_internal_linkage_decl_in_modules) << D;

  Module *M = getOwningModule(D);
  Diag(D->getLocation(), diag::note_equivalent_internal_linkage_decl)
      << !M << (M ? M->getFullModuleName() : "");

  for (auto *E : Equiv) {
    Module *M = getOwningModule(E);
    Diag(E->getLocation(), diag::note_equivalent_internal_linkage_decl)
        << !M << (M ? M->getFullModuleName() : "");
  }
}

bool OverloadCandidate::NotValidBecauseConstraintExprHasError() const {
  return FailureKind == ovl_fail_bad_deduction &&
         static_cast<TemplateDeductionResult>(DeductionFailure.Result) ==
             TemplateDeductionResult::ConstraintsNotSatisfied &&
         static_cast<CNSInfo *>(DeductionFailure.Data)
             ->Satisfaction.ContainsErrors;
}

/// Computes the best viable function (C++ 13.3.3)
/// within an overload candidate set.
///
/// \param Loc The location of the function name (or operator symbol) for
/// which overload resolution occurs.
///
/// \param Best If overload resolution was successful or found a deleted
/// function, \p Best points to the candidate function found.
///
/// \returns The result of overload resolution.
OverloadingResult
OverloadCandidateSet::BestViableFunction(Sema &S, SourceLocation Loc,
                                         iterator &Best) {
  llvm::SmallVector<OverloadCandidate *, 16> Candidates;
  std::transform(begin(), end(), std::back_inserter(Candidates),
                 [](OverloadCandidate &Cand) { return &Cand; });

  // [CUDA] HD->H or HD->D calls are technically not allowed by CUDA but
  // are accepted by both clang and NVCC. However, during a particular
  // compilation mode only one call variant is viable. We need to
  // exclude non-viable overload candidates from consideration based
  // only on their host/device attributes. Specifically, if one
  // candidate call is WrongSide and the other is SameSide, we ignore
  // the WrongSide candidate.
  // We only need to remove wrong-sided candidates here if
  // -fgpu-exclude-wrong-side-overloads is off. When
  // -fgpu-exclude-wrong-side-overloads is on, all candidates are compared
  // uniformly in isBetterOverloadCandidate.
  if (S.getLangOpts().CUDA && !S.getLangOpts().GPUExcludeWrongSideOverloads) {
    const FunctionDecl *Caller = S.getCurFunctionDecl(/*AllowLambda=*/true);
    bool ContainsSameSideCandidate =
        llvm::any_of(Candidates, [&](OverloadCandidate *Cand) {
          // Check viable function only.
          return Cand->Viable && Cand->Function &&
                 S.CUDA().IdentifyPreference(Caller, Cand->Function) ==
                     SemaCUDA::CFP_SameSide;
        });
    if (ContainsSameSideCandidate) {
      auto IsWrongSideCandidate = [&](OverloadCandidate *Cand) {
        // Check viable function only to avoid unnecessary data copying/moving.
        return Cand->Viable && Cand->Function &&
               S.CUDA().IdentifyPreference(Caller, Cand->Function) ==
                   SemaCUDA::CFP_WrongSide;
      };
      llvm::erase_if(Candidates, IsWrongSideCandidate);
    }
  }

  // Find the best viable function.
  Best = end();
  for (auto *Cand : Candidates) {
    Cand->Best = false;
    if (Cand->Viable) {
      if (Best == end() ||
          isBetterOverloadCandidate(S, *Cand, *Best, Loc, Kind))
        Best = Cand;
    } else if (Cand->NotValidBecauseConstraintExprHasError()) {
      // This candidate has constraint that we were unable to evaluate because
      // it referenced an expression that contained an error. Rather than fall
      // back onto a potentially unintended candidate (made worse by
      // subsuming constraints), treat this as 'no viable candidate'.
      Best = end();
      return OR_No_Viable_Function;
    }
  }

  // If we didn't find any viable functions, abort.
  if (Best == end())
    return OR_No_Viable_Function;

  llvm::SmallVector<const NamedDecl *, 4> EquivalentCands;

  llvm::SmallVector<OverloadCandidate*, 4> PendingBest;
  PendingBest.push_back(&*Best);
  Best->Best = true;

  // Make sure that this function is better than every other viable
  // function. If not, we have an ambiguity.
  while (!PendingBest.empty()) {
    auto *Curr = PendingBest.pop_back_val();
    for (auto *Cand : Candidates) {
      if (Cand->Viable && !Cand->Best &&
          !isBetterOverloadCandidate(S, *Curr, *Cand, Loc, Kind)) {
        PendingBest.push_back(Cand);
        Cand->Best = true;

        if (S.isEquivalentInternalLinkageDeclaration(Cand->Function,
                                                     Curr->Function))
          EquivalentCands.push_back(Cand->Function);
        else
          Best = end();
      }
    }
  }

  // If we found more than one best candidate, this is ambiguous.
  if (Best == end())
    return OR_Ambiguous;

  // Best is the best viable function.
  if (Best->Function && Best->Function->isDeleted())
    return OR_Deleted;

  if (auto *M = dyn_cast_or_null<CXXMethodDecl>(Best->Function);
      Kind == CSK_AddressOfOverloadSet && M &&
      M->isImplicitObjectMemberFunction()) {
    return OR_No_Viable_Function;
  }

  if (!EquivalentCands.empty())
    S.diagnoseEquivalentInternalLinkageDeclarations(Loc, Best->Function,
                                                    EquivalentCands);

  return OR_Success;
}

namespace {

enum OverloadCandidateKind {
  oc_function,
  oc_method,
  oc_reversed_binary_operator,
  oc_constructor,
  oc_implicit_default_constructor,
  oc_implicit_copy_constructor,
  oc_implicit_move_constructor,
  oc_implicit_copy_assignment,
  oc_implicit_move_assignment,
  oc_implicit_equality_comparison,
  oc_inherited_constructor
};

enum OverloadCandidateSelect {
  ocs_non_template,
  ocs_template,
  ocs_described_template,
};

static std::pair<OverloadCandidateKind, OverloadCandidateSelect>
ClassifyOverloadCandidate(Sema &S, const NamedDecl *Found,
                          const FunctionDecl *Fn,
                          OverloadCandidateRewriteKind CRK,
                          std::string &Description) {

  bool isTemplate = Fn->isTemplateDecl() || Found->isTemplateDecl();
  if (FunctionTemplateDecl *FunTmpl = Fn->getPrimaryTemplate()) {
    isTemplate = true;
    Description = S.getTemplateArgumentBindingsText(
        FunTmpl->getTemplateParameters(), *Fn->getTemplateSpecializationArgs());
  }

  OverloadCandidateSelect Select = [&]() {
    if (!Description.empty())
      return ocs_described_template;
    return isTemplate ? ocs_template : ocs_non_template;
  }();

  OverloadCandidateKind Kind = [&]() {
    if (Fn->isImplicit() && Fn->getOverloadedOperator() == OO_EqualEqual)
      return oc_implicit_equality_comparison;

    if (CRK & CRK_Reversed)
      return oc_reversed_binary_operator;

    if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(Fn)) {
      if (!Ctor->isImplicit()) {
        if (isa<ConstructorUsingShadowDecl>(Found))
          return oc_inherited_constructor;
        else
          return oc_constructor;
      }

      if (Ctor->isDefaultConstructor())
        return oc_implicit_default_constructor;

      if (Ctor->isMoveConstructor())
        return oc_implicit_move_constructor;

      assert(Ctor->isCopyConstructor() &&
             "unexpected sort of implicit constructor");
      return oc_implicit_copy_constructor;
    }

    if (const auto *Meth = dyn_cast<CXXMethodDecl>(Fn)) {
      // This actually gets spelled 'candidate function' for now, but
      // it doesn't hurt to split it out.
      if (!Meth->isImplicit())
        return oc_method;

      if (Meth->isMoveAssignmentOperator())
        return oc_implicit_move_assignment;

      if (Meth->isCopyAssignmentOperator())
        return oc_implicit_copy_assignment;

      assert(isa<CXXConversionDecl>(Meth) && "expected conversion");
      return oc_method;
    }

    return oc_function;
  }();

  return std::make_pair(Kind, Select);
}

void MaybeEmitInheritedConstructorNote(Sema &S, const Decl *FoundDecl) {
  // FIXME: It'd be nice to only emit a note once per using-decl per overload
  // set.
  if (const auto *Shadow = dyn_cast<ConstructorUsingShadowDecl>(FoundDecl))
    S.Diag(FoundDecl->getLocation(),
           diag::note_ovl_candidate_inherited_constructor)
      << Shadow->getNominatedBaseClass();
}

} // end anonymous namespace

static bool isFunctionAlwaysEnabled(const ASTContext &Ctx,
                                    const FunctionDecl *FD) {
  for (auto *EnableIf : FD->specific_attrs<EnableIfAttr>()) {
    bool AlwaysTrue;
    if (EnableIf->getCond()->isValueDependent() ||
        !EnableIf->getCond()->EvaluateAsBooleanCondition(AlwaysTrue, Ctx))
      return false;
    if (!AlwaysTrue)
      return false;
  }
  return true;
}

/// Returns true if we can take the address of the function.
///
/// \param Complain - If true, we'll emit a diagnostic
/// \param InOverloadResolution - For the purposes of emitting a diagnostic, are
///   we in overload resolution?
/// \param Loc - The location of the statement we're complaining about. Ignored
///   if we're not complaining, or if we're in overload resolution.
static bool checkAddressOfFunctionIsAvailable(Sema &S, const FunctionDecl *FD,
                                              bool Complain,
                                              bool InOverloadResolution,
                                              SourceLocation Loc) {
  if (!isFunctionAlwaysEnabled(S.Context, FD)) {
    if (Complain) {
      if (InOverloadResolution)
        S.Diag(FD->getBeginLoc(),
               diag::note_addrof_ovl_candidate_disabled_by_enable_if_attr);
      else
        S.Diag(Loc, diag::err_addrof_function_disabled_by_enable_if_attr) << FD;
    }
    return false;
  }

  if (FD->getTrailingRequiresClause()) {
    ConstraintSatisfaction Satisfaction;
    if (S.CheckFunctionConstraints(FD, Satisfaction, Loc))
      return false;
    if (!Satisfaction.IsSatisfied) {
      if (Complain) {
        if (InOverloadResolution) {
          SmallString<128> TemplateArgString;
          if (FunctionTemplateDecl *FunTmpl = FD->getPrimaryTemplate()) {
            TemplateArgString += " ";
            TemplateArgString += S.getTemplateArgumentBindingsText(
                FunTmpl->getTemplateParameters(),
                *FD->getTemplateSpecializationArgs());
          }

          S.Diag(FD->getBeginLoc(),
                 diag::note_ovl_candidate_unsatisfied_constraints)
              << TemplateArgString;
        } else
          S.Diag(Loc, diag::err_addrof_function_constraints_not_satisfied)
              << FD;
        S.DiagnoseUnsatisfiedConstraint(Satisfaction);
      }
      return false;
    }
  }

  auto I = llvm::find_if(FD->parameters(), [](const ParmVarDecl *P) {
    return P->hasAttr<PassObjectSizeAttr>();
  });
  if (I == FD->param_end())
    return true;

  if (Complain) {
    // Add one to ParamNo because it's user-facing
    unsigned ParamNo = std::distance(FD->param_begin(), I) + 1;
    if (InOverloadResolution)
      S.Diag(FD->getLocation(),
             diag::note_ovl_candidate_has_pass_object_size_params)
          << ParamNo;
    else
      S.Diag(Loc, diag::err_address_of_function_with_pass_object_size_params)
          << FD << ParamNo;
  }
  return false;
}

static bool checkAddressOfCandidateIsAvailable(Sema &S,
                                               const FunctionDecl *FD) {
  return checkAddressOfFunctionIsAvailable(S, FD, /*Complain=*/true,
                                           /*InOverloadResolution=*/true,
                                           /*Loc=*/SourceLocation());
}

bool Sema::checkAddressOfFunctionIsAvailable(const FunctionDecl *Function,
                                             bool Complain,
                                             SourceLocation Loc) {
  return ::checkAddressOfFunctionIsAvailable(*this, Function, Complain,
                                             /*InOverloadResolution=*/false,
                                             Loc);
}

// Don't print candidates other than the one that matches the calling
// convention of the call operator, since that is guaranteed to exist.
static bool shouldSkipNotingLambdaConversionDecl(const FunctionDecl *Fn) {
  const auto *ConvD = dyn_cast<CXXConversionDecl>(Fn);

  if (!ConvD)
    return false;
  const auto *RD = cast<CXXRecordDecl>(Fn->getParent());
  if (!RD->isLambda())
    return false;

  CXXMethodDecl *CallOp = RD->getLambdaCallOperator();
  CallingConv CallOpCC =
      CallOp->getType()->castAs<FunctionType>()->getCallConv();
  QualType ConvRTy = ConvD->getType()->castAs<FunctionType>()->getReturnType();
  CallingConv ConvToCC =
      ConvRTy->getPointeeType()->castAs<FunctionType>()->getCallConv();

  return ConvToCC != CallOpCC;
}

// Notes the location of an overload candidate.
void Sema::NoteOverloadCandidate(const NamedDecl *Found, const FunctionDecl *Fn,
                                 OverloadCandidateRewriteKind RewriteKind,
                                 QualType DestType, bool TakingAddress) {
  if (TakingAddress && !checkAddressOfCandidateIsAvailable(*this, Fn))
    return;
  if (Fn->isMultiVersion() && Fn->hasAttr<TargetAttr>() &&
      !Fn->getAttr<TargetAttr>()->isDefaultVersion())
    return;
  if (Fn->isMultiVersion() && Fn->hasAttr<TargetVersionAttr>() &&
      !Fn->getAttr<TargetVersionAttr>()->isDefaultVersion())
    return;
  if (shouldSkipNotingLambdaConversionDecl(Fn))
    return;

  std::string FnDesc;
  std::pair<OverloadCandidateKind, OverloadCandidateSelect> KSPair =
      ClassifyOverloadCandidate(*this, Found, Fn, RewriteKind, FnDesc);
  PartialDiagnostic PD = PDiag(diag::note_ovl_candidate)
                         << (unsigned)KSPair.first << (unsigned)KSPair.second
                         << Fn << FnDesc;

  HandleFunctionTypeMismatch(PD, Fn->getType(), DestType);
  Diag(Fn->getLocation(), PD);
  MaybeEmitInheritedConstructorNote(*this, Found);
}

static void
MaybeDiagnoseAmbiguousConstraints(Sema &S, ArrayRef<OverloadCandidate> Cands) {
  // Perhaps the ambiguity was caused by two atomic constraints that are
  // 'identical' but not equivalent:
  //
  // void foo() requires (sizeof(T) > 4) { } // #1
  // void foo() requires (sizeof(T) > 4) && T::value { } // #2
  //
  // The 'sizeof(T) > 4' constraints are seemingly equivalent and should cause
  // #2 to subsume #1, but these constraint are not considered equivalent
  // according to the subsumption rules because they are not the same
  // source-level construct. This behavior is quite confusing and we should try
  // to help the user figure out what happened.

  SmallVector<const Expr *, 3> FirstAC, SecondAC;
  FunctionDecl *FirstCand = nullptr, *SecondCand = nullptr;
  for (auto I = Cands.begin(), E = Cands.end(); I != E; ++I) {
    if (!I->Function)
      continue;
    SmallVector<const Expr *, 3> AC;
    if (auto *Template = I->Function->getPrimaryTemplate())
      Template->getAssociatedConstraints(AC);
    else
      I->Function->getAssociatedConstraints(AC);
    if (AC.empty())
      continue;
    if (FirstCand == nullptr) {
      FirstCand = I->Function;
      FirstAC = AC;
    } else if (SecondCand == nullptr) {
      SecondCand = I->Function;
      SecondAC = AC;
    } else {
      // We have more than one pair of constrained functions - this check is
      // expensive and we'd rather not try to diagnose it.
      return;
    }
  }
  if (!SecondCand)
    return;
  // The diagnostic can only happen if there are associated constraints on
  // both sides (there needs to be some identical atomic constraint).
  if (S.MaybeEmitAmbiguousAtomicConstraintsDiagnostic(FirstCand, FirstAC,
                                                      SecondCand, SecondAC))
    // Just show the user one diagnostic, they'll probably figure it out
    // from here.
    return;
}

// Notes the location of all overload candidates designated through
// OverloadedExpr
void Sema::NoteAllOverloadCandidates(Expr *OverloadedExpr, QualType DestType,
                                     bool TakingAddress) {
  assert(OverloadedExpr->getType() == Context.OverloadTy);

  OverloadExpr::FindResult Ovl = OverloadExpr::find(OverloadedExpr);
  OverloadExpr *OvlExpr = Ovl.Expression;

  for (UnresolvedSetIterator I = OvlExpr->decls_begin(),
                            IEnd = OvlExpr->decls_end();
       I != IEnd; ++I) {
    if (FunctionTemplateDecl *FunTmpl =
                dyn_cast<FunctionTemplateDecl>((*I)->getUnderlyingDecl()) ) {
      NoteOverloadCandidate(*I, FunTmpl->getTemplatedDecl(), CRK_None, DestType,
                            TakingAddress);
    } else if (FunctionDecl *Fun
                      = dyn_cast<FunctionDecl>((*I)->getUnderlyingDecl()) ) {
      NoteOverloadCandidate(*I, Fun, CRK_None, DestType, TakingAddress);
    }
  }
}

/// Diagnoses an ambiguous conversion.  The partial diagnostic is the
/// "lead" diagnostic; it will be given two arguments, the source and
/// target types of the conversion.
void ImplicitConversionSequence::DiagnoseAmbiguousConversion(
                                 Sema &S,
                                 SourceLocation CaretLoc,
                                 const PartialDiagnostic &PDiag) const {
  S.Diag(CaretLoc, PDiag)
    << Ambiguous.getFromType() << Ambiguous.getToType();
  unsigned CandsShown = 0;
  AmbiguousConversionSequence::const_iterator I, E;
  for (I = Ambiguous.begin(), E = Ambiguous.end(); I != E; ++I) {
    if (CandsShown >= S.Diags.getNumOverloadCandidatesToShow())
      break;
    ++CandsShown;
    S.NoteOverloadCandidate(I->first, I->second);
  }
  S.Diags.overloadCandidatesShown(CandsShown);
  if (I != E)
    S.Diag(SourceLocation(), diag::note_ovl_too_many_candidates) << int(E - I);
}

static void DiagnoseBadConversion(Sema &S, OverloadCandidate *Cand,
                                  unsigned I, bool TakingCandidateAddress) {
  const ImplicitConversionSequence &Conv = Cand->Conversions[I];
  assert(Conv.isBad());
  assert(Cand->Function && "for now, candidate must be a function");
  FunctionDecl *Fn = Cand->Function;

  // There's a conversion slot for the object argument if this is a
  // non-constructor method.  Note that 'I' corresponds the
  // conversion-slot index.
  bool isObjectArgument = false;
  if (isa<CXXMethodDecl>(Fn) && !isa<CXXConstructorDecl>(Fn)) {
    if (I == 0)
      isObjectArgument = true;
    else
      I--;
  }

  std::string FnDesc;
  std::pair<OverloadCandidateKind, OverloadCandidateSelect> FnKindPair =
      ClassifyOverloadCandidate(S, Cand->FoundDecl, Fn, Cand->getRewriteKind(),
                                FnDesc);

  Expr *FromExpr = Conv.Bad.FromExpr;
  QualType FromTy = Conv.Bad.getFromType();
  QualType ToTy = Conv.Bad.getToType();
  SourceRange ToParamRange;

  // FIXME: In presence of parameter packs we can't determine parameter range
  // reliably, as we don't have access to instantiation.
  bool HasParamPack =
      llvm::any_of(Fn->parameters().take_front(I), [](const ParmVarDecl *Parm) {
        return Parm->isParameterPack();
      });
  if (!isObjectArgument && !HasParamPack)
    ToParamRange = Fn->getParamDecl(I)->getSourceRange();

  if (FromTy == S.Context.OverloadTy) {
    assert(FromExpr && "overload set argument came from implicit argument?");
    Expr *E = FromExpr->IgnoreParens();
    if (isa<UnaryOperator>(E))
      E = cast<UnaryOperator>(E)->getSubExpr()->IgnoreParens();
    DeclarationName Name = cast<OverloadExpr>(E)->getName();

    S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_overload)
        << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
        << ToParamRange << ToTy << Name << I + 1;
    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;
  }

  // Do some hand-waving analysis to see if the non-viability is due
  // to a qualifier mismatch.
  CanQualType CFromTy = S.Context.getCanonicalType(FromTy);
  CanQualType CToTy = S.Context.getCanonicalType(ToTy);
  if (CanQual<ReferenceType> RT = CToTy->getAs<ReferenceType>())
    CToTy = RT->getPointeeType();
  else {
    // TODO: detect and diagnose the full richness of const mismatches.
    if (CanQual<PointerType> FromPT = CFromTy->getAs<PointerType>())
      if (CanQual<PointerType> ToPT = CToTy->getAs<PointerType>()) {
        CFromTy = FromPT->getPointeeType();
        CToTy = ToPT->getPointeeType();
      }
  }

  if (CToTy.getUnqualifiedType() == CFromTy.getUnqualifiedType() &&
      !CToTy.isAtLeastAsQualifiedAs(CFromTy)) {
    Qualifiers FromQs = CFromTy.getQualifiers();
    Qualifiers ToQs = CToTy.getQualifiers();

    if (FromQs.getAddressSpace() != ToQs.getAddressSpace()) {
      if (isObjectArgument)
        S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_addrspace_this)
            << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second
            << FnDesc << FromQs.getAddressSpace() << ToQs.getAddressSpace();
      else
        S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_addrspace)
            << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second
            << FnDesc << ToParamRange << FromQs.getAddressSpace()
            << ToQs.getAddressSpace() << ToTy->isReferenceType() << I + 1;
      MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
      return;
    }

    if (FromQs.getObjCLifetime() != ToQs.getObjCLifetime()) {
      S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_ownership)
          << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
          << ToParamRange << FromTy << FromQs.getObjCLifetime()
          << ToQs.getObjCLifetime() << (unsigned)isObjectArgument << I + 1;
      MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
      return;
    }

    if (FromQs.getObjCGCAttr() != ToQs.getObjCGCAttr()) {
      S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_gc)
          << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
          << ToParamRange << FromTy << FromQs.getObjCGCAttr()
          << ToQs.getObjCGCAttr() << (unsigned)isObjectArgument << I + 1;
      MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
      return;
    }

    unsigned CVR = FromQs.getCVRQualifiers() & ~ToQs.getCVRQualifiers();
    assert(CVR && "expected qualifiers mismatch");

    if (isObjectArgument) {
      S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_cvr_this)
          << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
          << FromTy << (CVR - 1);
    } else {
      S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_cvr)
          << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
          << ToParamRange << FromTy << (CVR - 1) << I + 1;
    }
    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;
  }

  if (Conv.Bad.Kind == BadConversionSequence::lvalue_ref_to_rvalue ||
      Conv.Bad.Kind == BadConversionSequence::rvalue_ref_to_lvalue) {
    S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_value_category)
        << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
        << (unsigned)isObjectArgument << I + 1
        << (Conv.Bad.Kind == BadConversionSequence::rvalue_ref_to_lvalue)
        << ToParamRange;
    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;
  }

  // Special diagnostic for failure to convert an initializer list, since
  // telling the user that it has type void is not useful.
  if (FromExpr && isa<InitListExpr>(FromExpr)) {
    S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_list_argument)
        << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
        << ToParamRange << FromTy << ToTy << (unsigned)isObjectArgument << I + 1
        << (Conv.Bad.Kind == BadConversionSequence::too_few_initializers ? 1
            : Conv.Bad.Kind == BadConversionSequence::too_many_initializers
                ? 2
                : 0);
    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;
  }

  // Diagnose references or pointers to incomplete types differently,
  // since it's far from impossible that the incompleteness triggered
  // the failure.
  QualType TempFromTy = FromTy.getNonReferenceType();
  if (const PointerType *PTy = TempFromTy->getAs<PointerType>())
    TempFromTy = PTy->getPointeeType();
  if (TempFromTy->isIncompleteType()) {
    // Emit the generic diagnostic and, optionally, add the hints to it.
    S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_conv_incomplete)
        << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
        << ToParamRange << FromTy << ToTy << (unsigned)isObjectArgument << I + 1
        << (unsigned)(Cand->Fix.Kind);

    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;
  }

  // Diagnose base -> derived pointer conversions.
  unsigned BaseToDerivedConversion = 0;
  if (const PointerType *FromPtrTy = FromTy->getAs<PointerType>()) {
    if (const PointerType *ToPtrTy = ToTy->getAs<PointerType>()) {
      if (ToPtrTy->getPointeeType().isAtLeastAsQualifiedAs(
                                               FromPtrTy->getPointeeType()) &&
          !FromPtrTy->getPointeeType()->isIncompleteType() &&
          !ToPtrTy->getPointeeType()->isIncompleteType() &&
          S.IsDerivedFrom(SourceLocation(), ToPtrTy->getPointeeType(),
                          FromPtrTy->getPointeeType()))
        BaseToDerivedConversion = 1;
    }
  } else if (const ObjCObjectPointerType *FromPtrTy
                                    = FromTy->getAs<ObjCObjectPointerType>()) {
    if (const ObjCObjectPointerType *ToPtrTy
                                        = ToTy->getAs<ObjCObjectPointerType>())
      if (const ObjCInterfaceDecl *FromIface = FromPtrTy->getInterfaceDecl())
        if (const ObjCInterfaceDecl *ToIface = ToPtrTy->getInterfaceDecl())
          if (ToPtrTy->getPointeeType().isAtLeastAsQualifiedAs(
                                                FromPtrTy->getPointeeType()) &&
              FromIface->isSuperClassOf(ToIface))
            BaseToDerivedConversion = 2;
  } else if (const ReferenceType *ToRefTy = ToTy->getAs<ReferenceType>()) {
    if (ToRefTy->getPointeeType().isAtLeastAsQualifiedAs(FromTy) &&
        !FromTy->isIncompleteType() &&
        !ToRefTy->getPointeeType()->isIncompleteType() &&
        S.IsDerivedFrom(SourceLocation(), ToRefTy->getPointeeType(), FromTy)) {
      BaseToDerivedConversion = 3;
    }
  }

  if (BaseToDerivedConversion) {
    S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_base_to_derived_conv)
        << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
        << ToParamRange << (BaseToDerivedConversion - 1) << FromTy << ToTy
        << I + 1;
    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;
  }

  if (isa<ObjCObjectPointerType>(CFromTy) &&
      isa<PointerType>(CToTy)) {
    Qualifiers FromQs = CFromTy.getQualifiers();
    Qualifiers ToQs = CToTy.getQualifiers();
    if (FromQs.getObjCLifetime() != ToQs.getObjCLifetime()) {
      S.Diag(Fn->getLocation(), diag::note_ovl_candidate_bad_arc_conv)
          << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
          << ToParamRange << FromTy << ToTy << (unsigned)isObjectArgument
          << I + 1;
      MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
      return;
    }
  }

  if (TakingCandidateAddress &&
      !checkAddressOfCandidateIsAvailable(S, Cand->Function))
    return;

  // Emit the generic diagnostic and, optionally, add the hints to it.
  PartialDiagnostic FDiag = S.PDiag(diag::note_ovl_candidate_bad_conv);
  FDiag << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
        << ToParamRange << FromTy << ToTy << (unsigned)isObjectArgument << I + 1
        << (unsigned)(Cand->Fix.Kind);

  // Check that location of Fn is not in system header.
  if (!S.SourceMgr.isInSystemHeader(Fn->getLocation())) {
    // If we can fix the conversion, suggest the FixIts.
    for (const FixItHint &HI : Cand->Fix.Hints)
        FDiag << HI;
  }

  S.Diag(Fn->getLocation(), FDiag);

  MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
}

/// Additional arity mismatch diagnosis specific to a function overload
/// candidates. This is not covered by the more general DiagnoseArityMismatch()
/// over a candidate in any candidate set.
static bool CheckArityMismatch(Sema &S, OverloadCandidate *Cand,
                               unsigned NumArgs, bool IsAddressOf = false) {
  FunctionDecl *Fn = Cand->Function;
  unsigned MinParams = Fn->getMinRequiredExplicitArguments() +
                       ((IsAddressOf && !Fn->isStatic()) ? 1 : 0);

  // With invalid overloaded operators, it's possible that we think we
  // have an arity mismatch when in fact it looks like we have the
  // right number of arguments, because only overloaded operators have
  // the weird behavior of overloading member and non-member functions.
  // Just don't report anything.
  if (Fn->isInvalidDecl() &&
      Fn->getDeclName().getNameKind() == DeclarationName::CXXOperatorName)
    return true;

  if (NumArgs < MinParams) {
    assert((Cand->FailureKind == ovl_fail_too_few_arguments) ||
           (Cand->FailureKind == ovl_fail_bad_deduction &&
            Cand->DeductionFailure.getResult() ==
                TemplateDeductionResult::TooFewArguments));
  } else {
    assert((Cand->FailureKind == ovl_fail_too_many_arguments) ||
           (Cand->FailureKind == ovl_fail_bad_deduction &&
            Cand->DeductionFailure.getResult() ==
                TemplateDeductionResult::TooManyArguments));
  }

  return false;
}

/// General arity mismatch diagnosis over a candidate in a candidate set.
static void DiagnoseArityMismatch(Sema &S, NamedDecl *Found, Decl *D,
                                  unsigned NumFormalArgs,
                                  bool IsAddressOf = false) {
  assert(isa<FunctionDecl>(D) &&
      "The templated declaration should at least be a function"
      " when diagnosing bad template argument deduction due to too many"
      " or too few arguments");

  FunctionDecl *Fn = cast<FunctionDecl>(D);

  // TODO: treat calls to a missing default constructor as a special case
  const auto *FnTy = Fn->getType()->castAs<FunctionProtoType>();
  unsigned MinParams = Fn->getMinRequiredExplicitArguments() +
                       ((IsAddressOf && !Fn->isStatic()) ? 1 : 0);

  // at least / at most / exactly
  bool HasExplicitObjectParam =
      !IsAddressOf && Fn->hasCXXExplicitFunctionObjectParameter();

  unsigned ParamCount =
      Fn->getNumNonObjectParams() + ((IsAddressOf && !Fn->isStatic()) ? 1 : 0);
  unsigned mode, modeCount;

  if (NumFormalArgs < MinParams) {
    if (MinParams != ParamCount || FnTy->isVariadic() ||
        FnTy->isTemplateVariadic())
      mode = 0; // "at least"
    else
      mode = 2; // "exactly"
    modeCount = MinParams;
  } else {
    if (MinParams != ParamCount)
      mode = 1; // "at most"
    else
      mode = 2; // "exactly"
    modeCount = ParamCount;
  }

  std::string Description;
  std::pair<OverloadCandidateKind, OverloadCandidateSelect> FnKindPair =
      ClassifyOverloadCandidate(S, Found, Fn, CRK_None, Description);

  if (modeCount == 1 && !IsAddressOf &&
      Fn->getParamDecl(HasExplicitObjectParam ? 1 : 0)->getDeclName())
    S.Diag(Fn->getLocation(), diag::note_ovl_candidate_arity_one)
        << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second
        << Description << mode
        << Fn->getParamDecl(HasExplicitObjectParam ? 1 : 0) << NumFormalArgs
        << HasExplicitObjectParam << Fn->getParametersSourceRange();
  else
    S.Diag(Fn->getLocation(), diag::note_ovl_candidate_arity)
        << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second
        << Description << mode << modeCount << NumFormalArgs
        << HasExplicitObjectParam << Fn->getParametersSourceRange();

  MaybeEmitInheritedConstructorNote(S, Found);
}

/// Arity mismatch diagnosis specific to a function overload candidate.
static void DiagnoseArityMismatch(Sema &S, OverloadCandidate *Cand,
                                  unsigned NumFormalArgs) {
  if (!CheckArityMismatch(S, Cand, NumFormalArgs, Cand->TookAddressOfOverload))
    DiagnoseArityMismatch(S, Cand->FoundDecl, Cand->Function, NumFormalArgs,
                          Cand->TookAddressOfOverload);
}

static TemplateDecl *getDescribedTemplate(Decl *Templated) {
  if (TemplateDecl *TD = Templated->getDescribedTemplate())
    return TD;
  llvm_unreachable("Unsupported: Getting the described template declaration"
                   " for bad deduction diagnosis");
}

/// Diagnose a failed template-argument deduction.
static void DiagnoseBadDeduction(Sema &S, NamedDecl *Found, Decl *Templated,
                                 DeductionFailureInfo &DeductionFailure,
                                 unsigned NumArgs,
                                 bool TakingCandidateAddress) {
  TemplateParameter Param = DeductionFailure.getTemplateParameter();
  NamedDecl *ParamD;
  (ParamD = Param.dyn_cast<TemplateTypeParmDecl*>()) ||
  (ParamD = Param.dyn_cast<NonTypeTemplateParmDecl*>()) ||
  (ParamD = Param.dyn_cast<TemplateTemplateParmDecl*>());
  switch (DeductionFailure.getResult()) {
  case TemplateDeductionResult::Success:
    llvm_unreachable(
        "TemplateDeductionResult::Success while diagnosing bad deduction");
  case TemplateDeductionResult::NonDependentConversionFailure:
    llvm_unreachable("TemplateDeductionResult::NonDependentConversionFailure "
                     "while diagnosing bad deduction");
  case TemplateDeductionResult::Invalid:
  case TemplateDeductionResult::AlreadyDiagnosed:
    return;

  case TemplateDeductionResult::Incomplete: {
    assert(ParamD && "no parameter found for incomplete deduction result");
    S.Diag(Templated->getLocation(),
           diag::note_ovl_candidate_incomplete_deduction)
        << ParamD->getDeclName();
    MaybeEmitInheritedConstructorNote(S, Found);
    return;
  }

  case TemplateDeductionResult::IncompletePack: {
    assert(ParamD && "no parameter found for incomplete deduction result");
    S.Diag(Templated->getLocation(),
           diag::note_ovl_candidate_incomplete_deduction_pack)
        << ParamD->getDeclName()
        << (DeductionFailure.getFirstArg()->pack_size() + 1)
        << *DeductionFailure.getFirstArg();
    MaybeEmitInheritedConstructorNote(S, Found);
    return;
  }

  case TemplateDeductionResult::Underqualified: {
    assert(ParamD && "no parameter found for bad qualifiers deduction result");
    TemplateTypeParmDecl *TParam = cast<TemplateTypeParmDecl>(ParamD);

    QualType Param = DeductionFailure.getFirstArg()->getAsType();

    // Param will have been canonicalized, but it should just be a
    // qualified version of ParamD, so move the qualifiers to that.
    QualifierCollector Qs;
    Qs.strip(Param);
    QualType NonCanonParam = Qs.apply(S.Context, TParam->getTypeForDecl());
    assert(S.Context.hasSameType(Param, NonCanonParam));

    // Arg has also been canonicalized, but there's nothing we can do
    // about that.  It also doesn't matter as much, because it won't
    // have any template parameters in it (because deduction isn't
    // done on dependent types).
    QualType Arg = DeductionFailure.getSecondArg()->getAsType();

    S.Diag(Templated->getLocation(), diag::note_ovl_candidate_underqualified)
        << ParamD->getDeclName() << Arg << NonCanonParam;
    MaybeEmitInheritedConstructorNote(S, Found);
    return;
  }

  case TemplateDeductionResult::Inconsistent: {
    assert(ParamD && "no parameter found for inconsistent deduction result");
    int which = 0;
    if (isa<TemplateTypeParmDecl>(ParamD))
      which = 0;
    else if (isa<NonTypeTemplateParmDecl>(ParamD)) {
      // Deduction might have failed because we deduced arguments of two
      // different types for a non-type template parameter.
      // FIXME: Use a different TDK value for this.
      QualType T1 =
          DeductionFailure.getFirstArg()->getNonTypeTemplateArgumentType();
      QualType T2 =
          DeductionFailure.getSecondArg()->getNonTypeTemplateArgumentType();
      if (!T1.isNull() && !T2.isNull() && !S.Context.hasSameType(T1, T2)) {
        S.Diag(Templated->getLocation(),
               diag::note_ovl_candidate_inconsistent_deduction_types)
          << ParamD->getDeclName() << *DeductionFailure.getFirstArg() << T1
          << *DeductionFailure.getSecondArg() << T2;
        MaybeEmitInheritedConstructorNote(S, Found);
        return;
      }

      which = 1;
    } else {
      which = 2;
    }

    // Tweak the diagnostic if the problem is that we deduced packs of
    // different arities. We'll print the actual packs anyway in case that
    // includes additional useful information.
    if (DeductionFailure.getFirstArg()->getKind() == TemplateArgument::Pack &&
        DeductionFailure.getSecondArg()->getKind() == TemplateArgument::Pack &&
        DeductionFailure.getFirstArg()->pack_size() !=
            DeductionFailure.getSecondArg()->pack_size()) {
      which = 3;
    }

    S.Diag(Templated->getLocation(),
           diag::note_ovl_candidate_inconsistent_deduction)
        << which << ParamD->getDeclName() << *DeductionFailure.getFirstArg()
        << *DeductionFailure.getSecondArg();
    MaybeEmitInheritedConstructorNote(S, Found);
    return;
  }

  case TemplateDeductionResult::InvalidExplicitArguments:
    assert(ParamD && "no parameter found for invalid explicit arguments");
    if (ParamD->getDeclName())
      S.Diag(Templated->getLocation(),
             diag::note_ovl_candidate_explicit_arg_mismatch_named)
          << ParamD->getDeclName();
    else {
      int index = 0;
      if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(ParamD))
        index = TTP->getIndex();
      else if (NonTypeTemplateParmDecl *NTTP
                                  = dyn_cast<NonTypeTemplateParmDecl>(ParamD))
        index = NTTP->getIndex();
      else
        index = cast<TemplateTemplateParmDecl>(ParamD)->getIndex();
      S.Diag(Templated->getLocation(),
             diag::note_ovl_candidate_explicit_arg_mismatch_unnamed)
          << (index + 1);
    }
    MaybeEmitInheritedConstructorNote(S, Found);
    return;

  case TemplateDeductionResult::ConstraintsNotSatisfied: {
    // Format the template argument list into the argument string.
    SmallString<128> TemplateArgString;
    TemplateArgumentList *Args = DeductionFailure.getTemplateArgumentList();
    TemplateArgString = " ";
    TemplateArgString += S.getTemplateArgumentBindingsText(
        getDescribedTemplate(Templated)->getTemplateParameters(), *Args);
    if (TemplateArgString.size() == 1)
      TemplateArgString.clear();
    S.Diag(Templated->getLocation(),
           diag::note_ovl_candidate_unsatisfied_constraints)
        << TemplateArgString;

    S.DiagnoseUnsatisfiedConstraint(
        static_cast<CNSInfo*>(DeductionFailure.Data)->Satisfaction);
    return;
  }
  case TemplateDeductionResult::TooManyArguments:
  case TemplateDeductionResult::TooFewArguments:
    DiagnoseArityMismatch(S, Found, Templated, NumArgs);
    return;

  case TemplateDeductionResult::InstantiationDepth:
    S.Diag(Templated->getLocation(),
           diag::note_ovl_candidate_instantiation_depth);
    MaybeEmitInheritedConstructorNote(S, Found);
    return;

  case TemplateDeductionResult::SubstitutionFailure: {
    // Format the template argument list into the argument string.
    SmallString<128> TemplateArgString;
    if (TemplateArgumentList *Args =
            DeductionFailure.getTemplateArgumentList()) {
      TemplateArgString = " ";
      TemplateArgString += S.getTemplateArgumentBindingsText(
          getDescribedTemplate(Templated)->getTemplateParameters(), *Args);
      if (TemplateArgString.size() == 1)
        TemplateArgString.clear();
    }

    // If this candidate was disabled by enable_if, say so.
    PartialDiagnosticAt *PDiag = DeductionFailure.getSFINAEDiagnostic();
    if (PDiag && PDiag->second.getDiagID() ==
          diag::err_typename_nested_not_found_enable_if) {
      // FIXME: Use the source range of the condition, and the fully-qualified
      //        name of the enable_if template. These are both present in PDiag.
      S.Diag(PDiag->first, diag::note_ovl_candidate_disabled_by_enable_if)
        << "'enable_if'" << TemplateArgString;
      return;
    }

    // We found a specific requirement that disabled the enable_if.
    if (PDiag && PDiag->second.getDiagID() ==
        diag::err_typename_nested_not_found_requirement) {
      S.Diag(Templated->getLocation(),
             diag::note_ovl_candidate_disabled_by_requirement)
        << PDiag->second.getStringArg(0) << TemplateArgString;
      return;
    }

    // Format the SFINAE diagnostic into the argument string.
    // FIXME: Add a general mechanism to include a PartialDiagnostic *'s
    //        formatted message in another diagnostic.
    SmallString<128> SFINAEArgString;
    SourceRange R;
    if (PDiag) {
      SFINAEArgString = ": ";
      R = SourceRange(PDiag->first, PDiag->first);
      PDiag->second.EmitToString(S.getDiagnostics(), SFINAEArgString);
    }

    S.Diag(Templated->getLocation(),
           diag::note_ovl_candidate_substitution_failure)
        << TemplateArgString << SFINAEArgString << R;
    MaybeEmitInheritedConstructorNote(S, Found);
    return;
  }

  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::DeducedMismatchNested: {
    // Format the template argument list into the argument string.
    SmallString<128> TemplateArgString;
    if (TemplateArgumentList *Args =
            DeductionFailure.getTemplateArgumentList()) {
      TemplateArgString = " ";
      TemplateArgString += S.getTemplateArgumentBindingsText(
          getDescribedTemplate(Templated)->getTemplateParameters(), *Args);
      if (TemplateArgString.size() == 1)
        TemplateArgString.clear();
    }

    S.Diag(Templated->getLocation(), diag::note_ovl_candidate_deduced_mismatch)
        << (*DeductionFailure.getCallArgIndex() + 1)
        << *DeductionFailure.getFirstArg() << *DeductionFailure.getSecondArg()
        << TemplateArgString
        << (DeductionFailure.getResult() ==
            TemplateDeductionResult::DeducedMismatchNested);
    break;
  }

  case TemplateDeductionResult::NonDeducedMismatch: {
    // FIXME: Provide a source location to indicate what we couldn't match.
    TemplateArgument FirstTA = *DeductionFailure.getFirstArg();
    TemplateArgument SecondTA = *DeductionFailure.getSecondArg();
    if (FirstTA.getKind() == TemplateArgument::Template &&
        SecondTA.getKind() == TemplateArgument::Template) {
      TemplateName FirstTN = FirstTA.getAsTemplate();
      TemplateName SecondTN = SecondTA.getAsTemplate();
      if (FirstTN.getKind() == TemplateName::Template &&
          SecondTN.getKind() == TemplateName::Template) {
        if (FirstTN.getAsTemplateDecl()->getName() ==
            SecondTN.getAsTemplateDecl()->getName()) {
          // FIXME: This fixes a bad diagnostic where both templates are named
          // the same.  This particular case is a bit difficult since:
          // 1) It is passed as a string to the diagnostic printer.
          // 2) The diagnostic printer only attempts to find a better
          //    name for types, not decls.
          // Ideally, this should folded into the diagnostic printer.
          S.Diag(Templated->getLocation(),
                 diag::note_ovl_candidate_non_deduced_mismatch_qualified)
              << FirstTN.getAsTemplateDecl() << SecondTN.getAsTemplateDecl();
          return;
        }
      }
    }

    if (TakingCandidateAddress && isa<FunctionDecl>(Templated) &&
        !checkAddressOfCandidateIsAvailable(S, cast<FunctionDecl>(Templated)))
      return;

    // FIXME: For generic lambda parameters, check if the function is a lambda
    // call operator, and if so, emit a prettier and more informative
    // diagnostic that mentions 'auto' and lambda in addition to
    // (or instead of?) the canonical template type parameters.
    S.Diag(Templated->getLocation(),
           diag::note_ovl_candidate_non_deduced_mismatch)
        << FirstTA << SecondTA;
    return;
  }
  // TODO: diagnose these individually, then kill off
  // note_ovl_candidate_bad_deduction, which is uselessly vague.
  case TemplateDeductionResult::MiscellaneousDeductionFailure:
    S.Diag(Templated->getLocation(), diag::note_ovl_candidate_bad_deduction);
    MaybeEmitInheritedConstructorNote(S, Found);
    return;
  case TemplateDeductionResult::CUDATargetMismatch:
    S.Diag(Templated->getLocation(),
           diag::note_cuda_ovl_candidate_target_mismatch);
    return;
  }
}

/// Diagnose a failed template-argument deduction, for function calls.
static void DiagnoseBadDeduction(Sema &S, OverloadCandidate *Cand,
                                 unsigned NumArgs,
                                 bool TakingCandidateAddress) {
  TemplateDeductionResult TDK = Cand->DeductionFailure.getResult();
  if (TDK == TemplateDeductionResult::TooFewArguments ||
      TDK == TemplateDeductionResult::TooManyArguments) {
    if (CheckArityMismatch(S, Cand, NumArgs))
      return;
  }
  DiagnoseBadDeduction(S, Cand->FoundDecl, Cand->Function, // pattern
                       Cand->DeductionFailure, NumArgs, TakingCandidateAddress);
}

/// CUDA: diagnose an invalid call across targets.
static void DiagnoseBadTarget(Sema &S, OverloadCandidate *Cand) {
  FunctionDecl *Caller = S.getCurFunctionDecl(/*AllowLambda=*/true);
  FunctionDecl *Callee = Cand->Function;

  CUDAFunctionTarget CallerTarget = S.CUDA().IdentifyTarget(Caller),
                     CalleeTarget = S.CUDA().IdentifyTarget(Callee);

  std::string FnDesc;
  std::pair<OverloadCandidateKind, OverloadCandidateSelect> FnKindPair =
      ClassifyOverloadCandidate(S, Cand->FoundDecl, Callee,
                                Cand->getRewriteKind(), FnDesc);

  S.Diag(Callee->getLocation(), diag::note_ovl_candidate_bad_target)
      << (unsigned)FnKindPair.first << (unsigned)ocs_non_template
      << FnDesc /* Ignored */
      << llvm::to_underlying(CalleeTarget) << llvm::to_underlying(CallerTarget);

  // This could be an implicit constructor for which we could not infer the
  // target due to a collsion. Diagnose that case.
  CXXMethodDecl *Meth = dyn_cast<CXXMethodDecl>(Callee);
  if (Meth != nullptr && Meth->isImplicit()) {
    CXXRecordDecl *ParentClass = Meth->getParent();
    CXXSpecialMemberKind CSM;

    switch (FnKindPair.first) {
    default:
      return;
    case oc_implicit_default_constructor:
      CSM = CXXSpecialMemberKind::DefaultConstructor;
      break;
    case oc_implicit_copy_constructor:
      CSM = CXXSpecialMemberKind::CopyConstructor;
      break;
    case oc_implicit_move_constructor:
      CSM = CXXSpecialMemberKind::MoveConstructor;
      break;
    case oc_implicit_copy_assignment:
      CSM = CXXSpecialMemberKind::CopyAssignment;
      break;
    case oc_implicit_move_assignment:
      CSM = CXXSpecialMemberKind::MoveAssignment;
      break;
    };

    bool ConstRHS = false;
    if (Meth->getNumParams()) {
      if (const ReferenceType *RT =
              Meth->getParamDecl(0)->getType()->getAs<ReferenceType>()) {
        ConstRHS = RT->getPointeeType().isConstQualified();
      }
    }

    S.CUDA().inferTargetForImplicitSpecialMember(ParentClass, CSM, Meth,
                                                 /* ConstRHS */ ConstRHS,
                                                 /* Diagnose */ true);
  }
}

static void DiagnoseFailedEnableIfAttr(Sema &S, OverloadCandidate *Cand) {
  FunctionDecl *Callee = Cand->Function;
  EnableIfAttr *Attr = static_cast<EnableIfAttr*>(Cand->DeductionFailure.Data);

  S.Diag(Callee->getLocation(),
         diag::note_ovl_candidate_disabled_by_function_cond_attr)
      << Attr->getCond()->getSourceRange() << Attr->getMessage();
}

static void DiagnoseFailedExplicitSpec(Sema &S, OverloadCandidate *Cand) {
  ExplicitSpecifier ES = ExplicitSpecifier::getFromDecl(Cand->Function);
  assert(ES.isExplicit() && "not an explicit candidate");

  unsigned Kind;
  switch (Cand->Function->getDeclKind()) {
  case Decl::Kind::CXXConstructor:
    Kind = 0;
    break;
  case Decl::Kind::CXXConversion:
    Kind = 1;
    break;
  case Decl::Kind::CXXDeductionGuide:
    Kind = Cand->Function->isImplicit() ? 0 : 2;
    break;
  default:
    llvm_unreachable("invalid Decl");
  }

  // Note the location of the first (in-class) declaration; a redeclaration
  // (particularly an out-of-class definition) will typically lack the
  // 'explicit' specifier.
  // FIXME: This is probably a good thing to do for all 'candidate' notes.
  FunctionDecl *First = Cand->Function->getFirstDecl();
  if (FunctionDecl *Pattern = First->getTemplateInstantiationPattern())
    First = Pattern->getFirstDecl();

  S.Diag(First->getLocation(),
         diag::note_ovl_candidate_explicit)
      << Kind << (ES.getExpr() ? 1 : 0)
      << (ES.getExpr() ? ES.getExpr()->getSourceRange() : SourceRange());
}

static void NoteImplicitDeductionGuide(Sema &S, FunctionDecl *Fn) {
  auto *DG = dyn_cast<CXXDeductionGuideDecl>(Fn);
  if (!DG)
    return;
  TemplateDecl *OriginTemplate =
      DG->getDeclName().getCXXDeductionGuideTemplate();
  // We want to always print synthesized deduction guides for type aliases.
  // They would retain the explicit bit of the corresponding constructor.
  if (!(DG->isImplicit() || (OriginTemplate && OriginTemplate->isTypeAlias())))
    return;
  std::string FunctionProto;
  llvm::raw_string_ostream OS(FunctionProto);
  FunctionTemplateDecl *Template = DG->getDescribedFunctionTemplate();
  if (!Template) {
    // This also could be an instantiation. Find out the primary template.
    FunctionDecl *Pattern =
        DG->getTemplateInstantiationPattern(/*ForDefinition=*/false);
    if (!Pattern) {
      // The implicit deduction guide is built on an explicit non-template
      // deduction guide. Currently, this might be the case only for type
      // aliases.
      // FIXME: Add a test once https://github.com/llvm/llvm-project/pull/96686
      // gets merged.
      assert(OriginTemplate->isTypeAlias() &&
             "Non-template implicit deduction guides are only possible for "
             "type aliases");
      DG->print(OS);
      S.Diag(DG->getLocation(), diag::note_implicit_deduction_guide)
          << FunctionProto;
      return;
    }
    Template = Pattern->getDescribedFunctionTemplate();
    assert(Template && "Cannot find the associated function template of "
                       "CXXDeductionGuideDecl?");
  }
  Template->print(OS);
  S.Diag(DG->getLocation(), diag::note_implicit_deduction_guide)
      << FunctionProto;
}

/// Generates a 'note' diagnostic for an overload candidate.  We've
/// already generated a primary error at the call site.
///
/// It really does need to be a single diagnostic with its caret
/// pointed at the candidate declaration.  Yes, this creates some
/// major challenges of technical writing.  Yes, this makes pointing
/// out problems with specific arguments quite awkward.  It's still
/// better than generating twenty screens of text for every failed
/// overload.
///
/// It would be great to be able to express per-candidate problems
/// more richly for those diagnostic clients that cared, but we'd
/// still have to be just as careful with the default diagnostics.
/// \param CtorDestAS Addr space of object being constructed (for ctor
/// candidates only).
static void NoteFunctionCandidate(Sema &S, OverloadCandidate *Cand,
                                  unsigned NumArgs,
                                  bool TakingCandidateAddress,
                                  LangAS CtorDestAS = LangAS::Default) {
  FunctionDecl *Fn = Cand->Function;
  if (shouldSkipNotingLambdaConversionDecl(Fn))
    return;

  // There is no physical candidate declaration to point to for OpenCL builtins.
  // Except for failed conversions, the notes are identical for each candidate,
  // so do not generate such notes.
  if (S.getLangOpts().OpenCL && Fn->isImplicit() &&
      Cand->FailureKind != ovl_fail_bad_conversion)
    return;

  // Skip implicit member functions when trying to resolve
  // the address of a an overload set for a function pointer.
  if (Cand->TookAddressOfOverload &&
      !Cand->Function->hasCXXExplicitFunctionObjectParameter() &&
      !Cand->Function->isStatic())
    return;

  // Note deleted candidates, but only if they're viable.
  if (Cand->Viable) {
    if (Fn->isDeleted()) {
      std::string FnDesc;
      std::pair<OverloadCandidateKind, OverloadCandidateSelect> FnKindPair =
          ClassifyOverloadCandidate(S, Cand->FoundDecl, Fn,
                                    Cand->getRewriteKind(), FnDesc);

      S.Diag(Fn->getLocation(), diag::note_ovl_candidate_deleted)
          << (unsigned)FnKindPair.first << (unsigned)FnKindPair.second << FnDesc
          << (Fn->isDeleted() ? (Fn->isDeletedAsWritten() ? 1 : 2) : 0);
      MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
      return;
    }

    // We don't really have anything else to say about viable candidates.
    S.NoteOverloadCandidate(Cand->FoundDecl, Fn, Cand->getRewriteKind());
    return;
  }

  // If this is a synthesized deduction guide we're deducing against, add a note
  // for it. These deduction guides are not explicitly spelled in the source
  // code, so simply printing a deduction failure note mentioning synthesized
  // template parameters or pointing to the header of the surrounding RecordDecl
  // would be confusing.
  //
  // We prefer adding such notes at the end of the deduction failure because
  // duplicate code snippets appearing in the diagnostic would likely become
  // noisy.
  auto _ = llvm::make_scope_exit([&] { NoteImplicitDeductionGuide(S, Fn); });

  switch (Cand->FailureKind) {
  case ovl_fail_too_many_arguments:
  case ovl_fail_too_few_arguments:
    return DiagnoseArityMismatch(S, Cand, NumArgs);

  case ovl_fail_bad_deduction:
    return DiagnoseBadDeduction(S, Cand, NumArgs,
                                TakingCandidateAddress);

  case ovl_fail_illegal_constructor: {
    S.Diag(Fn->getLocation(), diag::note_ovl_candidate_illegal_constructor)
      << (Fn->getPrimaryTemplate() ? 1 : 0);
    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;
  }

  case ovl_fail_object_addrspace_mismatch: {
    Qualifiers QualsForPrinting;
    QualsForPrinting.setAddressSpace(CtorDestAS);
    S.Diag(Fn->getLocation(),
           diag::note_ovl_candidate_illegal_constructor_adrspace_mismatch)
        << QualsForPrinting;
    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;
  }

  case ovl_fail_trivial_conversion:
  case ovl_fail_bad_final_conversion:
  case ovl_fail_final_conversion_not_exact:
    return S.NoteOverloadCandidate(Cand->FoundDecl, Fn, Cand->getRewriteKind());

  case ovl_fail_bad_conversion: {
    unsigned I = (Cand->IgnoreObjectArgument ? 1 : 0);
    for (unsigned N = Cand->Conversions.size(); I != N; ++I)
      if (Cand->Conversions[I].isInitialized() && Cand->Conversions[I].isBad())
        return DiagnoseBadConversion(S, Cand, I, TakingCandidateAddress);

    // FIXME: this currently happens when we're called from SemaInit
    // when user-conversion overload fails.  Figure out how to handle
    // those conditions and diagnose them well.
    return S.NoteOverloadCandidate(Cand->FoundDecl, Fn, Cand->getRewriteKind());
  }

  case ovl_fail_bad_target:
    return DiagnoseBadTarget(S, Cand);

  case ovl_fail_enable_if:
    return DiagnoseFailedEnableIfAttr(S, Cand);

  case ovl_fail_explicit:
    return DiagnoseFailedExplicitSpec(S, Cand);

  case ovl_fail_inhctor_slice:
    // It's generally not interesting to note copy/move constructors here.
    if (cast<CXXConstructorDecl>(Fn)->isCopyOrMoveConstructor())
      return;
    S.Diag(Fn->getLocation(),
           diag::note_ovl_candidate_inherited_constructor_slice)
      << (Fn->getPrimaryTemplate() ? 1 : 0)
      << Fn->getParamDecl(0)->getType()->isRValueReferenceType();
    MaybeEmitInheritedConstructorNote(S, Cand->FoundDecl);
    return;

  case ovl_fail_addr_not_available: {
    bool Available = checkAddressOfCandidateIsAvailable(S, Cand->Function);
    (void)Available;
    assert(!Available);
    break;
  }
  case ovl_non_default_multiversion_function:
    // Do nothing, these should simply be ignored.
    break;

  case ovl_fail_constraints_not_satisfied: {
    std::string FnDesc;
    std::pair<OverloadCandidateKind, OverloadCandidateSelect> FnKindPair =
        ClassifyOverloadCandidate(S, Cand->FoundDecl, Fn,
                                  Cand->getRewriteKind(), FnDesc);

    S.Diag(Fn->getLocation(),
           diag::note_ovl_candidate_constraints_not_satisfied)
        << (unsigned)FnKindPair.first << (unsigned)ocs_non_template
        << FnDesc /* Ignored */;
    ConstraintSatisfaction Satisfaction;
    if (S.CheckFunctionConstraints(Fn, Satisfaction))
      break;
    S.DiagnoseUnsatisfiedConstraint(Satisfaction);
  }
  }
}

static void NoteSurrogateCandidate(Sema &S, OverloadCandidate *Cand) {
  if (shouldSkipNotingLambdaConversionDecl(Cand->Surrogate))
    return;

  // Desugar the type of the surrogate down to a function type,
  // retaining as many typedefs as possible while still showing
  // the function type (and, therefore, its parameter types).
  QualType FnType = Cand->Surrogate->getConversionType();
  bool isLValueReference = false;
  bool isRValueReference = false;
  bool isPointer = false;
  if (const LValueReferenceType *FnTypeRef =
        FnType->getAs<LValueReferenceType>()) {
    FnType = FnTypeRef->getPointeeType();
    isLValueReference = true;
  } else if (const RValueReferenceType *FnTypeRef =
               FnType->getAs<RValueReferenceType>()) {
    FnType = FnTypeRef->getPointeeType();
    isRValueReference = true;
  }
  if (const PointerType *FnTypePtr = FnType->getAs<PointerType>()) {
    FnType = FnTypePtr->getPointeeType();
    isPointer = true;
  }
  // Desugar down to a function type.
  FnType = QualType(FnType->getAs<FunctionType>(), 0);
  // Reconstruct the pointer/reference as appropriate.
  if (isPointer) FnType = S.Context.getPointerType(FnType);
  if (isRValueReference) FnType = S.Context.getRValueReferenceType(FnType);
  if (isLValueReference) FnType = S.Context.getLValueReferenceType(FnType);

  if (!Cand->Viable &&
      Cand->FailureKind == ovl_fail_constraints_not_satisfied) {
    S.Diag(Cand->Surrogate->getLocation(),
           diag::note_ovl_surrogate_constraints_not_satisfied)
        << Cand->Surrogate;
    ConstraintSatisfaction Satisfaction;
    if (S.CheckFunctionConstraints(Cand->Surrogate, Satisfaction))
      S.DiagnoseUnsatisfiedConstraint(Satisfaction);
  } else {
    S.Diag(Cand->Surrogate->getLocation(), diag::note_ovl_surrogate_cand)
        << FnType;
  }
}

static void NoteBuiltinOperatorCandidate(Sema &S, StringRef Opc,
                                         SourceLocation OpLoc,
                                         OverloadCandidate *Cand) {
  assert(Cand->Conversions.size() <= 2 && "builtin operator is not binary");
  std::string TypeStr("operator");
  TypeStr += Opc;
  TypeStr += "(";
  TypeStr += Cand->BuiltinParamTypes[0].getAsString();
  if (Cand->Conversions.size() == 1) {
    TypeStr += ")";
    S.Diag(OpLoc, diag::note_ovl_builtin_candidate) << TypeStr;
  } else {
    TypeStr += ", ";
    TypeStr += Cand->BuiltinParamTypes[1].getAsString();
    TypeStr += ")";
    S.Diag(OpLoc, diag::note_ovl_builtin_candidate) << TypeStr;
  }
}

static void NoteAmbiguousUserConversions(Sema &S, SourceLocation OpLoc,
                                         OverloadCandidate *Cand) {
  for (const ImplicitConversionSequence &ICS : Cand->Conversions) {
    if (ICS.isBad()) break; // all meaningless after first invalid
    if (!ICS.isAmbiguous()) continue;

    ICS.DiagnoseAmbiguousConversion(
        S, OpLoc, S.PDiag(diag::note_ambiguous_type_conversion));
  }
}

static SourceLocation GetLocationForCandidate(const OverloadCandidate *Cand) {
  if (Cand->Function)
    return Cand->Function->getLocation();
  if (Cand->IsSurrogate)
    return Cand->Surrogate->getLocation();
  return SourceLocation();
}

static unsigned RankDeductionFailure(const DeductionFailureInfo &DFI) {
  switch (static_cast<TemplateDeductionResult>(DFI.Result)) {
  case TemplateDeductionResult::Success:
  case TemplateDeductionResult::NonDependentConversionFailure:
  case TemplateDeductionResult::AlreadyDiagnosed:
    llvm_unreachable("non-deduction failure while diagnosing bad deduction");

  case TemplateDeductionResult::Invalid:
  case TemplateDeductionResult::Incomplete:
  case TemplateDeductionResult::IncompletePack:
    return 1;

  case TemplateDeductionResult::Underqualified:
  case TemplateDeductionResult::Inconsistent:
    return 2;

  case TemplateDeductionResult::SubstitutionFailure:
  case TemplateDeductionResult::DeducedMismatch:
  case TemplateDeductionResult::ConstraintsNotSatisfied:
  case TemplateDeductionResult::DeducedMismatchNested:
  case TemplateDeductionResult::NonDeducedMismatch:
  case TemplateDeductionResult::MiscellaneousDeductionFailure:
  case TemplateDeductionResult::CUDATargetMismatch:
    return 3;

  case TemplateDeductionResult::InstantiationDepth:
    return 4;

  case TemplateDeductionResult::InvalidExplicitArguments:
    return 5;

  case TemplateDeductionResult::TooManyArguments:
  case TemplateDeductionResult::TooFewArguments:
    return 6;
  }
  llvm_unreachable("Unhandled deduction result");
}

namespace {

struct CompareOverloadCandidatesForDisplay {
  Sema &S;
  SourceLocation Loc;
  size_t NumArgs;
  OverloadCandidateSet::CandidateSetKind CSK;

  CompareOverloadCandidatesForDisplay(
      Sema &S, SourceLocation Loc, size_t NArgs,
      OverloadCandidateSet::CandidateSetKind CSK)
      : S(S), NumArgs(NArgs), CSK(CSK) {}

  OverloadFailureKind EffectiveFailureKind(const OverloadCandidate *C) const {
    // If there are too many or too few arguments, that's the high-order bit we
    // want to sort by, even if the immediate failure kind was something else.
    if (C->FailureKind == ovl_fail_too_many_arguments ||
        C->FailureKind == ovl_fail_too_few_arguments)
      return static_cast<OverloadFailureKind>(C->FailureKind);

    if (C->Function) {
      if (NumArgs > C->Function->getNumParams() && !C->Function->isVariadic())
        return ovl_fail_too_many_arguments;
      if (NumArgs < C->Function->getMinRequiredArguments())
        return ovl_fail_too_few_arguments;
    }

    return static_cast<OverloadFailureKind>(C->FailureKind);
  }

  bool operator()(const OverloadCandidate *L,
                  const OverloadCandidate *R) {
    // Fast-path this check.
    if (L == R) return false;

    // Order first by viability.
    if (L->Viable) {
      if (!R->Viable) return true;

      if (int Ord = CompareConversions(*L, *R))
        return Ord < 0;
      // Use other tie breakers.
    } else if (R->Viable)
      return false;

    assert(L->Viable == R->Viable);

    // Criteria by which we can sort non-viable candidates:
    if (!L->Viable) {
      OverloadFailureKind LFailureKind = EffectiveFailureKind(L);
      OverloadFailureKind RFailureKind = EffectiveFailureKind(R);

      // 1. Arity mismatches come after other candidates.
      if (LFailureKind == ovl_fail_too_many_arguments ||
          LFailureKind == ovl_fail_too_few_arguments) {
        if (RFailureKind == ovl_fail_too_many_arguments ||
            RFailureKind == ovl_fail_too_few_arguments) {
          int LDist = std::abs((int)L->getNumParams() - (int)NumArgs);
          int RDist = std::abs((int)R->getNumParams() - (int)NumArgs);
          if (LDist == RDist) {
            if (LFailureKind == RFailureKind)
              // Sort non-surrogates before surrogates.
              return !L->IsSurrogate && R->IsSurrogate;
            // Sort candidates requiring fewer parameters than there were
            // arguments given after candidates requiring more parameters
            // than there were arguments given.
            return LFailureKind == ovl_fail_too_many_arguments;
          }
          return LDist < RDist;
        }
        return false;
      }
      if (RFailureKind == ovl_fail_too_many_arguments ||
          RFailureKind == ovl_fail_too_few_arguments)
        return true;

      // 2. Bad conversions come first and are ordered by the number
      // of bad conversions and quality of good conversions.
      if (LFailureKind == ovl_fail_bad_conversion) {
        if (RFailureKind != ovl_fail_bad_conversion)
          return true;

        // The conversion that can be fixed with a smaller number of changes,
        // comes first.
        unsigned numLFixes = L->Fix.NumConversionsFixed;
        unsigned numRFixes = R->Fix.NumConversionsFixed;
        numLFixes = (numLFixes == 0) ? UINT_MAX : numLFixes;
        numRFixes = (numRFixes == 0) ? UINT_MAX : numRFixes;
        if (numLFixes != numRFixes) {
          return numLFixes < numRFixes;
        }

        // If there's any ordering between the defined conversions...
        if (int Ord = CompareConversions(*L, *R))
          return Ord < 0;
      } else if (RFailureKind == ovl_fail_bad_conversion)
        return false;

      if (LFailureKind == ovl_fail_bad_deduction) {
        if (RFailureKind != ovl_fail_bad_deduction)
          return true;

        if (L->DeductionFailure.Result != R->DeductionFailure.Result) {
          unsigned LRank = RankDeductionFailure(L->DeductionFailure);
          unsigned RRank = RankDeductionFailure(R->DeductionFailure);
          if (LRank != RRank)
            return LRank < RRank;
        }
      } else if (RFailureKind == ovl_fail_bad_deduction)
        return false;

      // TODO: others?
    }

    // Sort everything else by location.
    SourceLocation LLoc = GetLocationForCandidate(L);
    SourceLocation RLoc = GetLocationForCandidate(R);

    // Put candidates without locations (e.g. builtins) at the end.
    if (LLoc.isValid() && RLoc.isValid())
      return S.SourceMgr.isBeforeInTranslationUnit(LLoc, RLoc);
    if (LLoc.isValid() && !RLoc.isValid())
      return true;
    if (RLoc.isValid() && !LLoc.isValid())
      return false;
    assert(!LLoc.isValid() && !RLoc.isValid());
    // For builtins and other functions without locations, fallback to the order
    // in which they were added into the candidate set.
    return L < R;
  }

private:
  struct ConversionSignals {
    unsigned KindRank = 0;
    ImplicitConversionRank Rank = ICR_Exact_Match;

    static ConversionSignals ForSequence(ImplicitConversionSequence &Seq) {
      ConversionSignals Sig;
      Sig.KindRank = Seq.getKindRank();
      if (Seq.isStandard())
        Sig.Rank = Seq.Standard.getRank();
      else if (Seq.isUserDefined())
        Sig.Rank = Seq.UserDefined.After.getRank();
      // We intend StaticObjectArgumentConversion to compare the same as
      // StandardConversion with ICR_ExactMatch rank.
      return Sig;
    }

    static ConversionSignals ForObjectArgument() {
      // We intend StaticObjectArgumentConversion to compare the same as
      // StandardConversion with ICR_ExactMatch rank. Default give us that.
      return {};
    }
  };

  // Returns -1 if conversions in L are considered better.
  //          0 if they are considered indistinguishable.
  //          1 if conversions in R are better.
  int CompareConversions(const OverloadCandidate &L,
                         const OverloadCandidate &R) {
    // We cannot use `isBetterOverloadCandidate` because it is defined
    // according to the C++ standard and provides a partial order, but we need
    // a total order as this function is used in sort.
    assert(L.Conversions.size() == R.Conversions.size());
    for (unsigned I = 0, N = L.Conversions.size(); I != N; ++I) {
      auto LS = L.IgnoreObjectArgument && I == 0
                    ? ConversionSignals::ForObjectArgument()
                    : ConversionSignals::ForSequence(L.Conversions[I]);
      auto RS = R.IgnoreObjectArgument
                    ? ConversionSignals::ForObjectArgument()
                    : ConversionSignals::ForSequence(R.Conversions[I]);
      if (std::tie(LS.KindRank, LS.Rank) != std::tie(RS.KindRank, RS.Rank))
        return std::tie(LS.KindRank, LS.Rank) < std::tie(RS.KindRank, RS.Rank)
                   ? -1
                   : 1;
    }
    // FIXME: find a way to compare templates for being more or less
    // specialized that provides a strict weak ordering.
    return 0;
  }
};
}

/// CompleteNonViableCandidate - Normally, overload resolution only
/// computes up to the first bad conversion. Produces the FixIt set if
/// possible.
static void
CompleteNonViableCandidate(Sema &S, OverloadCandidate *Cand,
                           ArrayRef<Expr *> Args,
                           OverloadCandidateSet::CandidateSetKind CSK) {
  assert(!Cand->Viable);

  // Don't do anything on failures other than bad conversion.
  if (Cand->FailureKind != ovl_fail_bad_conversion)
    return;

  // We only want the FixIts if all the arguments can be corrected.
  bool Unfixable = false;
  // Use a implicit copy initialization to check conversion fixes.
  Cand->Fix.setConversionChecker(TryCopyInitialization);

  // Attempt to fix the bad conversion.
  unsigned ConvCount = Cand->Conversions.size();
  for (unsigned ConvIdx = (Cand->IgnoreObjectArgument ? 1 : 0); /**/;
       ++ConvIdx) {
    assert(ConvIdx != ConvCount && "no bad conversion in candidate");
    if (Cand->Conversions[ConvIdx].isInitialized() &&
        Cand->Conversions[ConvIdx].isBad()) {
      Unfixable = !Cand->TryToFixBadConversion(ConvIdx, S);
      break;
    }
  }

  // FIXME: this should probably be preserved from the overload
  // operation somehow.
  bool SuppressUserConversions = false;

  unsigned ConvIdx = 0;
  unsigned ArgIdx = 0;
  ArrayRef<QualType> ParamTypes;
  bool Reversed = Cand->isReversed();

  if (Cand->IsSurrogate) {
    QualType ConvType
      = Cand->Surrogate->getConversionType().getNonReferenceType();
    if (const PointerType *ConvPtrType = ConvType->getAs<PointerType>())
      ConvType = ConvPtrType->getPointeeType();
    ParamTypes = ConvType->castAs<FunctionProtoType>()->getParamTypes();
    // Conversion 0 is 'this', which doesn't have a corresponding parameter.
    ConvIdx = 1;
  } else if (Cand->Function) {
    ParamTypes =
        Cand->Function->getType()->castAs<FunctionProtoType>()->getParamTypes();
    if (isa<CXXMethodDecl>(Cand->Function) &&
        !isa<CXXConstructorDecl>(Cand->Function) && !Reversed) {
      // Conversion 0 is 'this', which doesn't have a corresponding parameter.
      ConvIdx = 1;
      if (CSK == OverloadCandidateSet::CSK_Operator &&
          Cand->Function->getDeclName().getCXXOverloadedOperator() != OO_Call &&
          Cand->Function->getDeclName().getCXXOverloadedOperator() !=
              OO_Subscript)
        // Argument 0 is 'this', which doesn't have a corresponding parameter.
        ArgIdx = 1;
    }
  } else {
    // Builtin operator.
    assert(ConvCount <= 3);
    ParamTypes = Cand->BuiltinParamTypes;
  }

  // Fill in the rest of the conversions.
  for (unsigned ParamIdx = Reversed ? ParamTypes.size() - 1 : 0;
       ConvIdx != ConvCount;
       ++ConvIdx, ++ArgIdx, ParamIdx += (Reversed ? -1 : 1)) {
    assert(ArgIdx < Args.size() && "no argument for this arg conversion");
    if (Cand->Conversions[ConvIdx].isInitialized()) {
      // We've already checked this conversion.
    } else if (ParamIdx < ParamTypes.size()) {
      if (ParamTypes[ParamIdx]->isDependentType())
        Cand->Conversions[ConvIdx].setAsIdentityConversion(
            Args[ArgIdx]->getType());
      else {
        Cand->Conversions[ConvIdx] =
            TryCopyInitialization(S, Args[ArgIdx], ParamTypes[ParamIdx],
                                  SuppressUserConversions,
                                  /*InOverloadResolution=*/true,
                                  /*AllowObjCWritebackConversion=*/
                                  S.getLangOpts().ObjCAutoRefCount);
        // Store the FixIt in the candidate if it exists.
        if (!Unfixable && Cand->Conversions[ConvIdx].isBad())
          Unfixable = !Cand->TryToFixBadConversion(ConvIdx, S);
      }
    } else
      Cand->Conversions[ConvIdx].setEllipsis();
  }
}

SmallVector<OverloadCandidate *, 32> OverloadCandidateSet::CompleteCandidates(
    Sema &S, OverloadCandidateDisplayKind OCD, ArrayRef<Expr *> Args,
    SourceLocation OpLoc,
    llvm::function_ref<bool(OverloadCandidate &)> Filter) {
  // Sort the candidates by viability and position.  Sorting directly would
  // be prohibitive, so we make a set of pointers and sort those.
  SmallVector<OverloadCandidate*, 32> Cands;
  if (OCD == OCD_AllCandidates) Cands.reserve(size());
  for (iterator Cand = begin(), LastCand = end(); Cand != LastCand; ++Cand) {
    if (!Filter(*Cand))
      continue;
    switch (OCD) {
    case OCD_AllCandidates:
      if (!Cand->Viable) {
        if (!Cand->Function && !Cand->IsSurrogate) {
          // This a non-viable builtin candidate.  We do not, in general,
          // want to list every possible builtin candidate.
          continue;
        }
        CompleteNonViableCandidate(S, Cand, Args, Kind);
      }
      break;

    case OCD_ViableCandidates:
      if (!Cand->Viable)
        continue;
      break;

    case OCD_AmbiguousCandidates:
      if (!Cand->Best)
        continue;
      break;
    }

    Cands.push_back(Cand);
  }

  llvm::stable_sort(
      Cands, CompareOverloadCandidatesForDisplay(S, OpLoc, Args.size(), Kind));

  return Cands;
}

bool OverloadCandidateSet::shouldDeferDiags(Sema &S, ArrayRef<Expr *> Args,
                                            SourceLocation OpLoc) {
  bool DeferHint = false;
  if (S.getLangOpts().CUDA && S.getLangOpts().GPUDeferDiag) {
    // Defer diagnostic for CUDA/HIP if there are wrong-sided candidates or
    // host device candidates.
    auto WrongSidedCands =
        CompleteCandidates(S, OCD_AllCandidates, Args, OpLoc, [](auto &Cand) {
          return (Cand.Viable == false &&
                  Cand.FailureKind == ovl_fail_bad_target) ||
                 (Cand.Function &&
                  Cand.Function->template hasAttr<CUDAHostAttr>() &&
                  Cand.Function->template hasAttr<CUDADeviceAttr>());
        });
    DeferHint = !WrongSidedCands.empty();
  }
  return DeferHint;
}

/// When overload resolution fails, prints diagnostic messages containing the
/// candidates in the candidate set.
void OverloadCandidateSet::NoteCandidates(
    PartialDiagnosticAt PD, Sema &S, OverloadCandidateDisplayKind OCD,
    ArrayRef<Expr *> Args, StringRef Opc, SourceLocation OpLoc,
    llvm::function_ref<bool(OverloadCandidate &)> Filter) {

  auto Cands = CompleteCandidates(S, OCD, Args, OpLoc, Filter);

  S.Diag(PD.first, PD.second, shouldDeferDiags(S, Args, OpLoc));

  // In WebAssembly we don't want to emit further diagnostics if a table is
  // passed as an argument to a function.
  bool NoteCands = true;
  for (const Expr *Arg : Args) {
    if (Arg->getType()->isWebAssemblyTableType())
      NoteCands = false;
  }

  if (NoteCands)
    NoteCandidates(S, Args, Cands, Opc, OpLoc);

  if (OCD == OCD_AmbiguousCandidates)
    MaybeDiagnoseAmbiguousConstraints(S, {begin(), end()});
}

void OverloadCandidateSet::NoteCandidates(Sema &S, ArrayRef<Expr *> Args,
                                          ArrayRef<OverloadCandidate *> Cands,
                                          StringRef Opc, SourceLocation OpLoc) {
  bool ReportedAmbiguousConversions = false;

  const OverloadsShown ShowOverloads = S.Diags.getShowOverloads();
  unsigned CandsShown = 0;
  auto I = Cands.begin(), E = Cands.end();
  for (; I != E; ++I) {
    OverloadCandidate *Cand = *I;

    if (CandsShown >= S.Diags.getNumOverloadCandidatesToShow() &&
        ShowOverloads == Ovl_Best) {
      break;
    }
    ++CandsShown;

    if (Cand->Function)
      NoteFunctionCandidate(S, Cand, Args.size(),
                            /*TakingCandidateAddress=*/false, DestAS);
    else if (Cand->IsSurrogate)
      NoteSurrogateCandidate(S, Cand);
    else {
      assert(Cand->Viable &&
             "Non-viable built-in candidates are not added to Cands.");
      // Generally we only see ambiguities including viable builtin
      // operators if overload resolution got screwed up by an
      // ambiguous user-defined conversion.
      //
      // FIXME: It's quite possible for different conversions to see
      // different ambiguities, though.
      if (!ReportedAmbiguousConversions) {
        NoteAmbiguousUserConversions(S, OpLoc, Cand);
        ReportedAmbiguousConversions = true;
      }

      // If this is a viable builtin, print it.
      NoteBuiltinOperatorCandidate(S, Opc, OpLoc, Cand);
    }
  }

  // Inform S.Diags that we've shown an overload set with N elements.  This may
  // inform the future value of S.Diags.getNumOverloadCandidatesToShow().
  S.Diags.overloadCandidatesShown(CandsShown);

  if (I != E)
    S.Diag(OpLoc, diag::note_ovl_too_many_candidates,
           shouldDeferDiags(S, Args, OpLoc))
        << int(E - I);
}

static SourceLocation
GetLocationForCandidate(const TemplateSpecCandidate *Cand) {
  return Cand->Specialization ? Cand->Specialization->getLocation()
                              : SourceLocation();
}

namespace {
struct CompareTemplateSpecCandidatesForDisplay {
  Sema &S;
  CompareTemplateSpecCandidatesForDisplay(Sema &S) : S(S) {}

  bool operator()(const TemplateSpecCandidate *L,
                  const TemplateSpecCandidate *R) {
    // Fast-path this check.
    if (L == R)
      return false;

    // Assuming that both candidates are not matches...

    // Sort by the ranking of deduction failures.
    if (L->DeductionFailure.Result != R->DeductionFailure.Result)
      return RankDeductionFailure(L->DeductionFailure) <
             RankDeductionFailure(R->DeductionFailure);

    // Sort everything else by location.
    SourceLocation LLoc = GetLocationForCandidate(L);
    SourceLocation RLoc = GetLocationForCandidate(R);

    // Put candidates without locations (e.g. builtins) at the end.
    if (LLoc.isInvalid())
      return false;
    if (RLoc.isInvalid())
      return true;

    return S.SourceMgr.isBeforeInTranslationUnit(LLoc, RLoc);
  }
};
}

/// Diagnose a template argument deduction failure.
/// We are treating these failures as overload failures due to bad
/// deductions.
void TemplateSpecCandidate::NoteDeductionFailure(Sema &S,
                                                 bool ForTakingAddress) {
  DiagnoseBadDeduction(S, FoundDecl, Specialization, // pattern
                       DeductionFailure, /*NumArgs=*/0, ForTakingAddress);
}

void TemplateSpecCandidateSet::destroyCandidates() {
  for (iterator i = begin(), e = end(); i != e; ++i) {
    i->DeductionFailure.Destroy();
  }
}

void TemplateSpecCandidateSet::clear() {
  destroyCandidates();
  Candidates.clear();
}

/// NoteCandidates - When no template specialization match is found, prints
/// diagnostic messages containing the non-matching specializations that form
/// the candidate set.
/// This is analoguous to OverloadCandidateSet::NoteCandidates() with
/// OCD == OCD_AllCandidates and Cand->Viable == false.
void TemplateSpecCandidateSet::NoteCandidates(Sema &S, SourceLocation Loc) {
  // Sort the candidates by position (assuming no candidate is a match).
  // Sorting directly would be prohibitive, so we make a set of pointers
  // and sort those.
  SmallVector<TemplateSpecCandidate *, 32> Cands;
  Cands.reserve(size());
  for (iterator Cand = begin(), LastCand = end(); Cand != LastCand; ++Cand) {
    if (Cand->Specialization)
      Cands.push_back(Cand);
    // Otherwise, this is a non-matching builtin candidate.  We do not,
    // in general, want to list every possible builtin candidate.
  }

  llvm::sort(Cands, CompareTemplateSpecCandidatesForDisplay(S));

  // FIXME: Perhaps rename OverloadsShown and getShowOverloads()
  // for generalization purposes (?).
  const OverloadsShown ShowOverloads = S.Diags.getShowOverloads();

  SmallVectorImpl<TemplateSpecCandidate *>::iterator I, E;
  unsigned CandsShown = 0;
  for (I = Cands.begin(), E = Cands.end(); I != E; ++I) {
    TemplateSpecCandidate *Cand = *I;

    // Set an arbitrary limit on the number of candidates we'll spam
    // the user with.  FIXME: This limit should depend on details of the
    // candidate list.
    if (CandsShown >= 4 && ShowOverloads == Ovl_Best)
      break;
    ++CandsShown;

    assert(Cand->Specialization &&
           "Non-matching built-in candidates are not added to Cands.");
    Cand->NoteDeductionFailure(S, ForTakingAddress);
  }

  if (I != E)
    S.Diag(Loc, diag::note_ovl_too_many_candidates) << int(E - I);
}

// [PossiblyAFunctionType]  -->   [Return]
// NonFunctionType --> NonFunctionType
// R (A) --> R(A)
// R (*)(A) --> R (A)
// R (&)(A) --> R (A)
// R (S::*)(A) --> R (A)
QualType Sema::ExtractUnqualifiedFunctionType(QualType PossiblyAFunctionType) {
  QualType Ret = PossiblyAFunctionType;
  if (const PointerType *ToTypePtr =
    PossiblyAFunctionType->getAs<PointerType>())
    Ret = ToTypePtr->getPointeeType();
  else if (const ReferenceType *ToTypeRef =
    PossiblyAFunctionType->getAs<ReferenceType>())
    Ret = ToTypeRef->getPointeeType();
  else if (const MemberPointerType *MemTypePtr =
    PossiblyAFunctionType->getAs<MemberPointerType>())
    Ret = MemTypePtr->getPointeeType();
  Ret =
    Context.getCanonicalType(Ret).getUnqualifiedType();
  return Ret;
}

static bool completeFunctionType(Sema &S, FunctionDecl *FD, SourceLocation Loc,
                                 bool Complain = true) {
  if (S.getLangOpts().CPlusPlus14 && FD->getReturnType()->isUndeducedType() &&
      S.DeduceReturnType(FD, Loc, Complain))
    return true;

  auto *FPT = FD->getType()->castAs<FunctionProtoType>();
  if (S.getLangOpts().CPlusPlus17 &&
      isUnresolvedExceptionSpec(FPT->getExceptionSpecType()) &&
      !S.ResolveExceptionSpec(Loc, FPT))
    return true;

  return false;
}

namespace {
// A helper class to help with address of function resolution
// - allows us to avoid passing around all those ugly parameters
class AddressOfFunctionResolver {
  Sema& S;
  Expr* SourceExpr;
  const QualType& TargetType;
  QualType TargetFunctionType; // Extracted function type from target type

  bool Complain;
  //DeclAccessPair& ResultFunctionAccessPair;
  ASTContext& Context;

  bool TargetTypeIsNonStaticMemberFunction;
  bool FoundNonTemplateFunction;
  bool StaticMemberFunctionFromBoundPointer;
  bool HasComplained;

  OverloadExpr::FindResult OvlExprInfo;
  OverloadExpr *OvlExpr;
  TemplateArgumentListInfo OvlExplicitTemplateArgs;
  SmallVector<std::pair<DeclAccessPair, FunctionDecl*>, 4> Matches;
  TemplateSpecCandidateSet FailedCandidates;

public:
  AddressOfFunctionResolver(Sema &S, Expr *SourceExpr,
                            const QualType &TargetType, bool Complain)
      : S(S), SourceExpr(SourceExpr), TargetType(TargetType),
        Complain(Complain), Context(S.getASTContext()),
        TargetTypeIsNonStaticMemberFunction(
            !!TargetType->getAs<MemberPointerType>()),
        FoundNonTemplateFunction(false),
        StaticMemberFunctionFromBoundPointer(false),
        HasComplained(false),
        OvlExprInfo(OverloadExpr::find(SourceExpr)),
        OvlExpr(OvlExprInfo.Expression),
        FailedCandidates(OvlExpr->getNameLoc(), /*ForTakingAddress=*/true) {
    ExtractUnqualifiedFunctionTypeFromTargetType();

    if (TargetFunctionType->isFunctionType()) {
      if (UnresolvedMemberExpr *UME = dyn_cast<UnresolvedMemberExpr>(OvlExpr))
        if (!UME->isImplicitAccess() &&
            !S.ResolveSingleFunctionTemplateSpecialization(UME))
          StaticMemberFunctionFromBoundPointer = true;
    } else if (OvlExpr->hasExplicitTemplateArgs()) {
      DeclAccessPair dap;
      if (FunctionDecl *Fn = S.ResolveSingleFunctionTemplateSpecialization(
              OvlExpr, false, &dap)) {
        if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Fn))
          if (!Method->isStatic()) {
            // If the target type is a non-function type and the function found
            // is a non-static member function, pretend as if that was the
            // target, it's the only possible type to end up with.
            TargetTypeIsNonStaticMemberFunction = true;

            // And skip adding the function if its not in the proper form.
            // We'll diagnose this due to an empty set of functions.
            if (!OvlExprInfo.HasFormOfMemberPointer)
              return;
          }

        Matches.push_back(std::make_pair(dap, Fn));
      }
      return;
    }

    if (OvlExpr->hasExplicitTemplateArgs())
      OvlExpr->copyTemplateArgumentsInto(OvlExplicitTemplateArgs);

    if (FindAllFunctionsThatMatchTargetTypeExactly()) {
      // C++ [over.over]p4:
      //   If more than one function is selected, [...]
      if (Matches.size() > 1 && !eliminiateSuboptimalOverloadCandidates()) {
        if (FoundNonTemplateFunction)
          EliminateAllTemplateMatches();
        else
          EliminateAllExceptMostSpecializedTemplate();
      }
    }

    if (S.getLangOpts().CUDA && Matches.size() > 1)
      EliminateSuboptimalCudaMatches();
  }

  bool hasComplained() const { return HasComplained; }

private:
  bool candidateHasExactlyCorrectType(const FunctionDecl *FD) {
    QualType Discard;
    return Context.hasSameUnqualifiedType(TargetFunctionType, FD->getType()) ||
           S.IsFunctionConversion(FD->getType(), TargetFunctionType, Discard);
  }

  /// \return true if A is considered a better overload candidate for the
  /// desired type than B.
  bool isBetterCandidate(const FunctionDecl *A, const FunctionDecl *B) {
    // If A doesn't have exactly the correct type, we don't want to classify it
    // as "better" than anything else. This way, the user is required to
    // disambiguate for us if there are multiple candidates and no exact match.
    return candidateHasExactlyCorrectType(A) &&
           (!candidateHasExactlyCorrectType(B) ||
            compareEnableIfAttrs(S, A, B) == Comparison::Better);
  }

  /// \return true if we were able to eliminate all but one overload candidate,
  /// false otherwise.
  bool eliminiateSuboptimalOverloadCandidates() {
    // Same algorithm as overload resolution -- one pass to pick the "best",
    // another pass to be sure that nothing is better than the best.
    auto Best = Matches.begin();
    for (auto I = Matches.begin()+1, E = Matches.end(); I != E; ++I)
      if (isBetterCandidate(I->second, Best->second))
        Best = I;

    const FunctionDecl *BestFn = Best->second;
    auto IsBestOrInferiorToBest = [this, BestFn](
        const std::pair<DeclAccessPair, FunctionDecl *> &Pair) {
      return BestFn == Pair.second || isBetterCandidate(BestFn, Pair.second);
    };

    // Note: We explicitly leave Matches unmodified if there isn't a clear best
    // option, so we can potentially give the user a better error
    if (!llvm::all_of(Matches, IsBestOrInferiorToBest))
      return false;
    Matches[0] = *Best;
    Matches.resize(1);
    return true;
  }

  bool isTargetTypeAFunction() const {
    return TargetFunctionType->isFunctionType();
  }

  // [ToType]     [Return]

  // R (*)(A) --> R (A), IsNonStaticMemberFunction = false
  // R (&)(A) --> R (A), IsNonStaticMemberFunction = false
  // R (S::*)(A) --> R (A), IsNonStaticMemberFunction = true
  void inline ExtractUnqualifiedFunctionTypeFromTargetType() {
    TargetFunctionType = S.ExtractUnqualifiedFunctionType(TargetType);
  }

  // return true if any matching specializations were found
  bool AddMatchingTemplateFunction(FunctionTemplateDecl* FunctionTemplate,
                                   const DeclAccessPair& CurAccessFunPair) {
    if (CXXMethodDecl *Method
              = dyn_cast<CXXMethodDecl>(FunctionTemplate->getTemplatedDecl())) {
      // Skip non-static function templates when converting to pointer, and
      // static when converting to member pointer.
      bool CanConvertToFunctionPointer =
          Method->isStatic() || Method->isExplicitObjectMemberFunction();
      if (CanConvertToFunctionPointer == TargetTypeIsNonStaticMemberFunction)
        return false;
    }
    else if (TargetTypeIsNonStaticMemberFunction)
      return false;

    // C++ [over.over]p2:
    //   If the name is a function template, template argument deduction is
    //   done (14.8.2.2), and if the argument deduction succeeds, the
    //   resulting template argument list is used to generate a single
    //   function template specialization, which is added to the set of
    //   overloaded functions considered.
    FunctionDecl *Specialization = nullptr;
    TemplateDeductionInfo Info(FailedCandidates.getLocation());
    if (TemplateDeductionResult Result = S.DeduceTemplateArguments(
            FunctionTemplate, &OvlExplicitTemplateArgs, TargetFunctionType,
            Specialization, Info, /*IsAddressOfFunction*/ true);
        Result != TemplateDeductionResult::Success) {
      // Make a note of the failed deduction for diagnostics.
      FailedCandidates.addCandidate()
          .set(CurAccessFunPair, FunctionTemplate->getTemplatedDecl(),
               MakeDeductionFailureInfo(Context, Result, Info));
      return false;
    }

    // Template argument deduction ensures that we have an exact match or
    // compatible pointer-to-function arguments that would be adjusted by ICS.
    // This function template specicalization works.
    assert(S.isSameOrCompatibleFunctionType(
              Context.getCanonicalType(Specialization->getType()),
              Context.getCanonicalType(TargetFunctionType)));

    if (!S.checkAddressOfFunctionIsAvailable(Specialization))
      return false;

    Matches.push_back(std::make_pair(CurAccessFunPair, Specialization));
    return true;
  }

  bool AddMatchingNonTemplateFunction(NamedDecl* Fn,
                                      const DeclAccessPair& CurAccessFunPair) {
    if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Fn)) {
      // Skip non-static functions when converting to pointer, and static
      // when converting to member pointer.
      bool CanConvertToFunctionPointer =
          Method->isStatic() || Method->isExplicitObjectMemberFunction();
      if (CanConvertToFunctionPointer == TargetTypeIsNonStaticMemberFunction)
        return false;
    }
    else if (TargetTypeIsNonStaticMemberFunction)
      return false;

    if (FunctionDecl *FunDecl = dyn_cast<FunctionDecl>(Fn)) {
      if (S.getLangOpts().CUDA) {
        FunctionDecl *Caller = S.getCurFunctionDecl(/*AllowLambda=*/true);
        if (!(Caller && Caller->isImplicit()) &&
            !S.CUDA().IsAllowedCall(Caller, FunDecl))
          return false;
      }
      if (FunDecl->isMultiVersion()) {
        const auto *TA = FunDecl->getAttr<TargetAttr>();
        if (TA && !TA->isDefaultVersion())
          return false;
        const auto *TVA = FunDecl->getAttr<TargetVersionAttr>();
        if (TVA && !TVA->isDefaultVersion())
          return false;
      }

      // If any candidate has a placeholder return type, trigger its deduction
      // now.
      if (completeFunctionType(S, FunDecl, SourceExpr->getBeginLoc(),
                               Complain)) {
        HasComplained |= Complain;
        return false;
      }

      if (!S.checkAddressOfFunctionIsAvailable(FunDecl))
        return false;

      // If we're in C, we need to support types that aren't exactly identical.
      if (!S.getLangOpts().CPlusPlus ||
          candidateHasExactlyCorrectType(FunDecl)) {
        Matches.push_back(std::make_pair(
            CurAccessFunPair, cast<FunctionDecl>(FunDecl->getCanonicalDecl())));
        FoundNonTemplateFunction = true;
        return true;
      }
    }

    return false;
  }

  bool FindAllFunctionsThatMatchTargetTypeExactly() {
    bool Ret = false;

    // If the overload expression doesn't have the form of a pointer to
    // member, don't try to convert it to a pointer-to-member type.
    if (IsInvalidFormOfPointerToMemberFunction())
      return false;

    for (UnresolvedSetIterator I = OvlExpr->decls_begin(),
                               E = OvlExpr->decls_end();
         I != E; ++I) {
      // Look through any using declarations to find the underlying function.
      NamedDecl *Fn = (*I)->getUnderlyingDecl();

      // C++ [over.over]p3:
      //   Non-member functions and static member functions match
      //   targets of type "pointer-to-function" or "reference-to-function."
      //   Nonstatic member functions match targets of
      //   type "pointer-to-member-function."
      // Note that according to DR 247, the containing class does not matter.
      if (FunctionTemplateDecl *FunctionTemplate
                                        = dyn_cast<FunctionTemplateDecl>(Fn)) {
        if (AddMatchingTemplateFunction(FunctionTemplate, I.getPair()))
          Ret = true;
      }
      // If we have explicit template arguments supplied, skip non-templates.
      else if (!OvlExpr->hasExplicitTemplateArgs() &&
               AddMatchingNonTemplateFunction(Fn, I.getPair()))
        Ret = true;
    }
    assert(Ret || Matches.empty());
    return Ret;
  }

  void EliminateAllExceptMostSpecializedTemplate() {
    //   [...] and any given function template specialization F1 is
    //   eliminated if the set contains a second function template
    //   specialization whose function template is more specialized
    //   than the function template of F1 according to the partial
    //   ordering rules of 14.5.5.2.

    // The algorithm specified above is quadratic. We instead use a
    // two-pass algorithm (similar to the one used to identify the
    // best viable function in an overload set) that identifies the
    // best function template (if it exists).

    UnresolvedSet<4> MatchesCopy; // TODO: avoid!
    for (unsigned I = 0, E = Matches.size(); I != E; ++I)
      MatchesCopy.addDecl(Matches[I].second, Matches[I].first.getAccess());

    // TODO: It looks like FailedCandidates does not serve much purpose
    // here, since the no_viable diagnostic has index 0.
    UnresolvedSetIterator Result = S.getMostSpecialized(
        MatchesCopy.begin(), MatchesCopy.end(), FailedCandidates,
        SourceExpr->getBeginLoc(), S.PDiag(),
        S.PDiag(diag::err_addr_ovl_ambiguous)
            << Matches[0].second->getDeclName(),
        S.PDiag(diag::note_ovl_candidate)
            << (unsigned)oc_function << (unsigned)ocs_described_template,
        Complain, TargetFunctionType);

    if (Result != MatchesCopy.end()) {
      // Make it the first and only element
      Matches[0].first = Matches[Result - MatchesCopy.begin()].first;
      Matches[0].second = cast<FunctionDecl>(*Result);
      Matches.resize(1);
    } else
      HasComplained |= Complain;
  }

  void EliminateAllTemplateMatches() {
    //   [...] any function template specializations in the set are
    //   eliminated if the set also contains a non-template function, [...]
    for (unsigned I = 0, N = Matches.size(); I != N; ) {
      if (Matches[I].second->getPrimaryTemplate() == nullptr)
        ++I;
      else {
        Matches[I] = Matches[--N];
        Matches.resize(N);
      }
    }
  }

  void EliminateSuboptimalCudaMatches() {
    S.CUDA().EraseUnwantedMatches(S.getCurFunctionDecl(/*AllowLambda=*/true),
                                  Matches);
  }

public:
  void ComplainNoMatchesFound() const {
    assert(Matches.empty());
    S.Diag(OvlExpr->getBeginLoc(), diag::err_addr_ovl_no_viable)
        << OvlExpr->getName() << TargetFunctionType
        << OvlExpr->getSourceRange();
    if (FailedCandidates.empty())
      S.NoteAllOverloadCandidates(OvlExpr, TargetFunctionType,
                                  /*TakingAddress=*/true);
    else {
      // We have some deduction failure messages. Use them to diagnose
      // the function templates, and diagnose the non-template candidates
      // normally.
      for (UnresolvedSetIterator I = OvlExpr->decls_begin(),
                                 IEnd = OvlExpr->decls_end();
           I != IEnd; ++I)
        if (FunctionDecl *Fun =
                dyn_cast<FunctionDecl>((*I)->getUnderlyingDecl()))
          if (!functionHasPassObjectSizeParams(Fun))
            S.NoteOverloadCandidate(*I, Fun, CRK_None, TargetFunctionType,
                                    /*TakingAddress=*/true);
      FailedCandidates.NoteCandidates(S, OvlExpr->getBeginLoc());
    }
  }

  bool IsInvalidFormOfPointerToMemberFunction() const {
    return TargetTypeIsNonStaticMemberFunction &&
      !OvlExprInfo.HasFormOfMemberPointer;
  }

  void ComplainIsInvalidFormOfPointerToMemberFunction() const {
      // TODO: Should we condition this on whether any functions might
      // have matched, or is it more appropriate to do that in callers?
      // TODO: a fixit wouldn't hurt.
      S.Diag(OvlExpr->getNameLoc(), diag::err_addr_ovl_no_qualifier)
        << TargetType << OvlExpr->getSourceRange();
  }

  bool IsStaticMemberFunctionFromBoundPointer() const {
    return StaticMemberFunctionFromBoundPointer;
  }

  void ComplainIsStaticMemberFunctionFromBoundPointer() const {
    S.Diag(OvlExpr->getBeginLoc(),
           diag::err_invalid_form_pointer_member_function)
        << OvlExpr->getSourceRange();
  }

  void ComplainOfInvalidConversion() const {
    S.Diag(OvlExpr->getBeginLoc(), diag::err_addr_ovl_not_func_ptrref)
        << OvlExpr->getName() << TargetType;
  }

  void ComplainMultipleMatchesFound() const {
    assert(Matches.size() > 1);
    S.Diag(OvlExpr->getBeginLoc(), diag::err_addr_ovl_ambiguous)
        << OvlExpr->getName() << OvlExpr->getSourceRange();
    S.NoteAllOverloadCandidates(OvlExpr, TargetFunctionType,
                                /*TakingAddress=*/true);
  }

  bool hadMultipleCandidates() const { return (OvlExpr->getNumDecls() > 1); }

  int getNumMatches() const { return Matches.size(); }

  FunctionDecl* getMatchingFunctionDecl() const {
    if (Matches.size() != 1) return nullptr;
    return Matches[0].second;
  }

  const DeclAccessPair* getMatchingFunctionAccessPair() const {
    if (Matches.size() != 1) return nullptr;
    return &Matches[0].first;
  }
};
}

FunctionDecl *
Sema::ResolveAddressOfOverloadedFunction(Expr *AddressOfExpr,
                                         QualType TargetType,
                                         bool Complain,
                                         DeclAccessPair &FoundResult,
                                         bool *pHadMultipleCandidates) {
  assert(AddressOfExpr->getType() == Context.OverloadTy);

  AddressOfFunctionResolver Resolver(*this, AddressOfExpr, TargetType,
                                     Complain);
  int NumMatches = Resolver.getNumMatches();
  FunctionDecl *Fn = nullptr;
  bool ShouldComplain = Complain && !Resolver.hasComplained();
  if (NumMatches == 0 && ShouldComplain) {
    if (Resolver.IsInvalidFormOfPointerToMemberFunction())
      Resolver.ComplainIsInvalidFormOfPointerToMemberFunction();
    else
      Resolver.ComplainNoMatchesFound();
  }
  else if (NumMatches > 1 && ShouldComplain)
    Resolver.ComplainMultipleMatchesFound();
  else if (NumMatches == 1) {
    Fn = Resolver.getMatchingFunctionDecl();
    assert(Fn);
    if (auto *FPT = Fn->getType()->getAs<FunctionProtoType>())
      ResolveExceptionSpec(AddressOfExpr->getExprLoc(), FPT);
    FoundResult = *Resolver.getMatchingFunctionAccessPair();
    if (Complain) {
      if (Resolver.IsStaticMemberFunctionFromBoundPointer())
        Resolver.ComplainIsStaticMemberFunctionFromBoundPointer();
      else
        CheckAddressOfMemberAccess(AddressOfExpr, FoundResult);
    }
  }

  if (pHadMultipleCandidates)
    *pHadMultipleCandidates = Resolver.hadMultipleCandidates();
  return Fn;
}

FunctionDecl *
Sema::resolveAddressOfSingleOverloadCandidate(Expr *E, DeclAccessPair &Pair) {
  OverloadExpr::FindResult R = OverloadExpr::find(E);
  OverloadExpr *Ovl = R.Expression;
  bool IsResultAmbiguous = false;
  FunctionDecl *Result = nullptr;
  DeclAccessPair DAP;
  SmallVector<FunctionDecl *, 2> AmbiguousDecls;

  // Return positive for better, negative for worse, 0 for equal preference.
  auto CheckCUDAPreference = [&](FunctionDecl *FD1, FunctionDecl *FD2) {
    FunctionDecl *Caller = getCurFunctionDecl(/*AllowLambda=*/true);
    return static_cast<int>(CUDA().IdentifyPreference(Caller, FD1)) -
           static_cast<int>(CUDA().IdentifyPreference(Caller, FD2));
  };

  // Don't use the AddressOfResolver because we're specifically looking for
  // cases where we have one overload candidate that lacks
  // enable_if/pass_object_size/...
  for (auto I = Ovl->decls_begin(), E = Ovl->decls_end(); I != E; ++I) {
    auto *FD = dyn_cast<FunctionDecl>(I->getUnderlyingDecl());
    if (!FD)
      return nullptr;

    if (!checkAddressOfFunctionIsAvailable(FD))
      continue;

    // If we found a better result, update Result.
    auto FoundBetter = [&]() {
      IsResultAmbiguous = false;
      DAP = I.getPair();
      Result = FD;
    };

    // We have more than one result - see if it is more constrained than the
    // previous one.
    if (Result) {
      // Check CUDA preference first. If the candidates have differennt CUDA
      // preference, choose the one with higher CUDA preference. Otherwise,
      // choose the one with more constraints.
      if (getLangOpts().CUDA) {
        int PreferenceByCUDA = CheckCUDAPreference(FD, Result);
        // FD has different preference than Result.
        if (PreferenceByCUDA != 0) {
          // FD is more preferable than Result.
          if (PreferenceByCUDA > 0)
            FoundBetter();
          continue;
        }
      }
      // FD has the same CUDA prefernece than Result. Continue check
      // constraints.
      FunctionDecl *MoreConstrained = getMoreConstrainedFunction(FD, Result);
      if (MoreConstrained != FD) {
        if (!MoreConstrained) {
          IsResultAmbiguous = true;
          AmbiguousDecls.push_back(FD);
        }
        continue;
      }
      // FD is more constrained - replace Result with it.
    }
    FoundBetter();
  }

  if (IsResultAmbiguous)
    return nullptr;

  if (Result) {
    SmallVector<const Expr *, 1> ResultAC;
    // We skipped over some ambiguous declarations which might be ambiguous with
    // the selected result.
    for (FunctionDecl *Skipped : AmbiguousDecls) {
      // If skipped candidate has different CUDA preference than the result,
      // there is no ambiguity. Otherwise check whether they have different
      // constraints.
      if (getLangOpts().CUDA && CheckCUDAPreference(Skipped, Result) != 0)
        continue;
      if (!getMoreConstrainedFunction(Skipped, Result))
        return nullptr;
    }
    Pair = DAP;
  }
  return Result;
}

bool Sema::resolveAndFixAddressOfSingleOverloadCandidate(
    ExprResult &SrcExpr, bool DoFunctionPointerConversion) {
  Expr *E = SrcExpr.get();
  assert(E->getType() == Context.OverloadTy && "SrcExpr must be an overload");

  DeclAccessPair DAP;
  FunctionDecl *Found = resolveAddressOfSingleOverloadCandidate(E, DAP);
  if (!Found || Found->isCPUDispatchMultiVersion() ||
      Found->isCPUSpecificMultiVersion())
    return false;

  // Emitting multiple diagnostics for a function that is both inaccessible and
  // unavailable is consistent with our behavior elsewhere. So, always check
  // for both.
  DiagnoseUseOfDecl(Found, E->getExprLoc());
  CheckAddressOfMemberAccess(E, DAP);
  ExprResult Res = FixOverloadedFunctionReference(E, DAP, Found);
  if (Res.isInvalid())
    return false;
  Expr *Fixed = Res.get();
  if (DoFunctionPointerConversion && Fixed->getType()->isFunctionType())
    SrcExpr = DefaultFunctionArrayConversion(Fixed, /*Diagnose=*/false);
  else
    SrcExpr = Fixed;
  return true;
}

FunctionDecl *Sema::ResolveSingleFunctionTemplateSpecialization(
    OverloadExpr *ovl, bool Complain, DeclAccessPair *FoundResult,
    TemplateSpecCandidateSet *FailedTSC) {
  // C++ [over.over]p1:
  //   [...] [Note: any redundant set of parentheses surrounding the
  //   overloaded function name is ignored (5.1). ]
  // C++ [over.over]p1:
  //   [...] The overloaded function name can be preceded by the &
  //   operator.

  // If we didn't actually find any template-ids, we're done.
  if (!ovl->hasExplicitTemplateArgs())
    return nullptr;

  TemplateArgumentListInfo ExplicitTemplateArgs;
  ovl->copyTemplateArgumentsInto(ExplicitTemplateArgs);

  // Look through all of the overloaded functions, searching for one
  // whose type matches exactly.
  FunctionDecl *Matched = nullptr;
  for (UnresolvedSetIterator I = ovl->decls_begin(),
         E = ovl->decls_end(); I != E; ++I) {
    // C++0x [temp.arg.explicit]p3:
    //   [...] In contexts where deduction is done and fails, or in contexts
    //   where deduction is not done, if a template argument list is
    //   specified and it, along with any default template arguments,
    //   identifies a single function template specialization, then the
    //   template-id is an lvalue for the function template specialization.
    FunctionTemplateDecl *FunctionTemplate
      = cast<FunctionTemplateDecl>((*I)->getUnderlyingDecl());

    // C++ [over.over]p2:
    //   If the name is a function template, template argument deduction is
    //   done (14.8.2.2), and if the argument deduction succeeds, the
    //   resulting template argument list is used to generate a single
    //   function template specialization, which is added to the set of
    //   overloaded functions considered.
    FunctionDecl *Specialization = nullptr;
    TemplateDeductionInfo Info(ovl->getNameLoc());
    if (TemplateDeductionResult Result = DeduceTemplateArguments(
            FunctionTemplate, &ExplicitTemplateArgs, Specialization, Info,
            /*IsAddressOfFunction*/ true);
        Result != TemplateDeductionResult::Success) {
      // Make a note of the failed deduction for diagnostics.
      if (FailedTSC)
        FailedTSC->addCandidate().set(
            I.getPair(), FunctionTemplate->getTemplatedDecl(),
            MakeDeductionFailureInfo(Context, Result, Info));
      continue;
    }

    assert(Specialization && "no specialization and no error?");

    // Multiple matches; we can't resolve to a single declaration.
    if (Matched) {
      if (Complain) {
        Diag(ovl->getExprLoc(), diag::err_addr_ovl_ambiguous)
          << ovl->getName();
        NoteAllOverloadCandidates(ovl);
      }
      return nullptr;
    }

    Matched = Specialization;
    if (FoundResult) *FoundResult = I.getPair();
  }

  if (Matched &&
      completeFunctionType(*this, Matched, ovl->getExprLoc(), Complain))
    return nullptr;

  return Matched;
}

bool Sema::ResolveAndFixSingleFunctionTemplateSpecialization(
    ExprResult &SrcExpr, bool doFunctionPointerConversion, bool complain,
    SourceRange OpRangeForComplaining, QualType DestTypeForComplaining,
    unsigned DiagIDForComplaining) {
  assert(SrcExpr.get()->getType() == Context.OverloadTy);

  OverloadExpr::FindResult ovl = OverloadExpr::find(SrcExpr.get());

  DeclAccessPair found;
  ExprResult SingleFunctionExpression;
  if (FunctionDecl *fn = ResolveSingleFunctionTemplateSpecialization(
                           ovl.Expression, /*complain*/ false, &found)) {
    if (DiagnoseUseOfDecl(fn, SrcExpr.get()->getBeginLoc())) {
      SrcExpr = ExprError();
      return true;
    }

    // It is only correct to resolve to an instance method if we're
    // resolving a form that's permitted to be a pointer to member.
    // Otherwise we'll end up making a bound member expression, which
    // is illegal in all the contexts we resolve like this.
    if (!ovl.HasFormOfMemberPointer &&
        isa<CXXMethodDecl>(fn) &&
        cast<CXXMethodDecl>(fn)->isInstance()) {
      if (!complain) return false;

      Diag(ovl.Expression->getExprLoc(),
           diag::err_bound_member_function)
        << 0 << ovl.Expression->getSourceRange();

      // TODO: I believe we only end up here if there's a mix of
      // static and non-static candidates (otherwise the expression
      // would have 'bound member' type, not 'overload' type).
      // Ideally we would note which candidate was chosen and why
      // the static candidates were rejected.
      SrcExpr = ExprError();
      return true;
    }

    // Fix the expression to refer to 'fn'.
    SingleFunctionExpression =
        FixOverloadedFunctionReference(SrcExpr.get(), found, fn);

    // If desired, do function-to-pointer decay.
    if (doFunctionPointerConversion) {
      SingleFunctionExpression =
        DefaultFunctionArrayLvalueConversion(SingleFunctionExpression.get());
      if (SingleFunctionExpression.isInvalid()) {
        SrcExpr = ExprError();
        return true;
      }
    }
  }

  if (!SingleFunctionExpression.isUsable()) {
    if (complain) {
      Diag(OpRangeForComplaining.getBegin(), DiagIDForComplaining)
        << ovl.Expression->getName()
        << DestTypeForComplaining
        << OpRangeForComplaining
        << ovl.Expression->getQualifierLoc().getSourceRange();
      NoteAllOverloadCandidates(SrcExpr.get());

      SrcExpr = ExprError();
      return true;
    }

    return false;
  }

  SrcExpr = SingleFunctionExpression;
  return true;
}

/// Add a single candidate to the overload set.
static void AddOverloadedCallCandidate(Sema &S,
                                       DeclAccessPair FoundDecl,
                                 TemplateArgumentListInfo *ExplicitTemplateArgs,
                                       ArrayRef<Expr *> Args,
                                       OverloadCandidateSet &CandidateSet,
                                       bool PartialOverloading,
                                       bool KnownValid) {
  NamedDecl *Callee = FoundDecl.getDecl();
  if (isa<UsingShadowDecl>(Callee))
    Callee = cast<UsingShadowDecl>(Callee)->getTargetDecl();

  if (FunctionDecl *Func = dyn_cast<FunctionDecl>(Callee)) {
    if (ExplicitTemplateArgs) {
      assert(!KnownValid && "Explicit template arguments?");
      return;
    }
    // Prevent ill-formed function decls to be added as overload candidates.
    if (!isa<FunctionProtoType>(Func->getType()->getAs<FunctionType>()))
      return;

    S.AddOverloadCandidate(Func, FoundDecl, Args, CandidateSet,
                           /*SuppressUserConversions=*/false,
                           PartialOverloading);
    return;
  }

  if (FunctionTemplateDecl *FuncTemplate
      = dyn_cast<FunctionTemplateDecl>(Callee)) {
    S.AddTemplateOverloadCandidate(FuncTemplate, FoundDecl,
                                   ExplicitTemplateArgs, Args, CandidateSet,
                                   /*SuppressUserConversions=*/false,
                                   PartialOverloading);
    return;
  }

  assert(!KnownValid && "unhandled case in overloaded call candidate");
}

void Sema::AddOverloadedCallCandidates(UnresolvedLookupExpr *ULE,
                                       ArrayRef<Expr *> Args,
                                       OverloadCandidateSet &CandidateSet,
                                       bool PartialOverloading) {

#ifndef NDEBUG
  // Verify that ArgumentDependentLookup is consistent with the rules
  // in C++0x [basic.lookup.argdep]p3:
  //
  //   Let X be the lookup set produced by unqualified lookup (3.4.1)
  //   and let Y be the lookup set produced by argument dependent
  //   lookup (defined as follows). If X contains
  //
  //     -- a declaration of a class member, or
  //
  //     -- a block-scope function declaration that is not a
  //        using-declaration, or
  //
  //     -- a declaration that is neither a function or a function
  //        template
  //
  //   then Y is empty.

  if (ULE->requiresADL()) {
    for (UnresolvedLookupExpr::decls_iterator I = ULE->decls_begin(),
           E = ULE->decls_end(); I != E; ++I) {
      assert(!(*I)->getDeclContext()->isRecord());
      assert(isa<UsingShadowDecl>(*I) ||
             !(*I)->getDeclContext()->isFunctionOrMethod());
      assert((*I)->getUnderlyingDecl()->isFunctionOrFunctionTemplate());
    }
  }
#endif

  // It would be nice to avoid this copy.
  TemplateArgumentListInfo TABuffer;
  TemplateArgumentListInfo *ExplicitTemplateArgs = nullptr;
  if (ULE->hasExplicitTemplateArgs()) {
    ULE->copyTemplateArgumentsInto(TABuffer);
    ExplicitTemplateArgs = &TABuffer;
  }

  for (UnresolvedLookupExpr::decls_iterator I = ULE->decls_begin(),
         E = ULE->decls_end(); I != E; ++I)
    AddOverloadedCallCandidate(*this, I.getPair(), ExplicitTemplateArgs, Args,
                               CandidateSet, PartialOverloading,
                               /*KnownValid*/ true);

  if (ULE->requiresADL())
    AddArgumentDependentLookupCandidates(ULE->getName(), ULE->getExprLoc(),
                                         Args, ExplicitTemplateArgs,
                                         CandidateSet, PartialOverloading);
}

void Sema::AddOverloadedCallCandidates(
    LookupResult &R, TemplateArgumentListInfo *ExplicitTemplateArgs,
    ArrayRef<Expr *> Args, OverloadCandidateSet &CandidateSet) {
  for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I)
    AddOverloadedCallCandidate(*this, I.getPair(), ExplicitTemplateArgs, Args,
                               CandidateSet, false, /*KnownValid*/ false);
}

/// Determine whether a declaration with the specified name could be moved into
/// a different namespace.
static bool canBeDeclaredInNamespace(const DeclarationName &Name) {
  switch (Name.getCXXOverloadedOperator()) {
  case OO_New: case OO_Array_New:
  case OO_Delete: case OO_Array_Delete:
    return false;

  default:
    return true;
  }
}

/// Attempt to recover from an ill-formed use of a non-dependent name in a
/// template, where the non-dependent name was declared after the template
/// was defined. This is common in code written for a compilers which do not
/// correctly implement two-stage name lookup.
///
/// Returns true if a viable candidate was found and a diagnostic was issued.
static bool DiagnoseTwoPhaseLookup(
    Sema &SemaRef, SourceLocation FnLoc, const CXXScopeSpec &SS,
    LookupResult &R, OverloadCandidateSet::CandidateSetKind CSK,
    TemplateArgumentListInfo *ExplicitTemplateArgs, ArrayRef<Expr *> Args,
    CXXRecordDecl **FoundInClass = nullptr) {
  if (!SemaRef.inTemplateInstantiation() || !SS.isEmpty())
    return false;

  for (DeclContext *DC = SemaRef.CurContext; DC; DC = DC->getParent()) {
    if (DC->isTransparentContext())
      continue;

    SemaRef.LookupQualifiedName(R, DC);

    if (!R.empty()) {
      R.suppressDiagnostics();

      OverloadCandidateSet Candidates(FnLoc, CSK);
      SemaRef.AddOverloadedCallCandidates(R, ExplicitTemplateArgs, Args,
                                          Candidates);

      OverloadCandidateSet::iterator Best;
      OverloadingResult OR =
          Candidates.BestViableFunction(SemaRef, FnLoc, Best);

      if (auto *RD = dyn_cast<CXXRecordDecl>(DC)) {
        // We either found non-function declarations or a best viable function
        // at class scope. A class-scope lookup result disables ADL. Don't
        // look past this, but let the caller know that we found something that
        // either is, or might be, usable in this class.
        if (FoundInClass) {
          *FoundInClass = RD;
          if (OR == OR_Success) {
            R.clear();
            R.addDecl(Best->FoundDecl.getDecl(), Best->FoundDecl.getAccess());
            R.resolveKind();
          }
        }
        return false;
      }

      if (OR != OR_Success) {
        // There wasn't a unique best function or function template.
        return false;
      }

      // Find the namespaces where ADL would have looked, and suggest
      // declaring the function there instead.
      Sema::AssociatedNamespaceSet AssociatedNamespaces;
      Sema::AssociatedClassSet AssociatedClasses;
      SemaRef.FindAssociatedClassesAndNamespaces(FnLoc, Args,
                                                 AssociatedNamespaces,
                                                 AssociatedClasses);
      Sema::AssociatedNamespaceSet SuggestedNamespaces;
      if (canBeDeclaredInNamespace(R.getLookupName())) {
        DeclContext *Std = SemaRef.getStdNamespace();
        for (Sema::AssociatedNamespaceSet::iterator
               it = AssociatedNamespaces.begin(),
               end = AssociatedNamespaces.end(); it != end; ++it) {
          // Never suggest declaring a function within namespace 'std'.
          if (Std && Std->Encloses(*it))
            continue;

          // Never suggest declaring a function within a namespace with a
          // reserved name, like __gnu_cxx.
          NamespaceDecl *NS = dyn_cast<NamespaceDecl>(*it);
          if (NS &&
              NS->getQualifiedNameAsString().find("__") != std::string::npos)
            continue;

          SuggestedNamespaces.insert(*it);
        }
      }

      SemaRef.Diag(R.getNameLoc(), diag::err_not_found_by_two_phase_lookup)
        << R.getLookupName();
      if (SuggestedNamespaces.empty()) {
        SemaRef.Diag(Best->Function->getLocation(),
                     diag::note_not_found_by_two_phase_lookup)
          << R.getLookupName() << 0;
      } else if (SuggestedNamespaces.size() == 1) {
        SemaRef.Diag(Best->Function->getLocation(),
                     diag::note_not_found_by_two_phase_lookup)
          << R.getLookupName() << 1 << *SuggestedNamespaces.begin();
      } else {
        // FIXME: It would be useful to list the associated namespaces here,
        // but the diagnostics infrastructure doesn't provide a way to produce
        // a localized representation of a list of items.
        SemaRef.Diag(Best->Function->getLocation(),
                     diag::note_not_found_by_two_phase_lookup)
          << R.getLookupName() << 2;
      }

      // Try to recover by calling this function.
      return true;
    }

    R.clear();
  }

  return false;
}

/// Attempt to recover from ill-formed use of a non-dependent operator in a
/// template, where the non-dependent operator was declared after the template
/// was defined.
///
/// Returns true if a viable candidate was found and a diagnostic was issued.
static bool
DiagnoseTwoPhaseOperatorLookup(Sema &SemaRef, OverloadedOperatorKind Op,
                               SourceLocation OpLoc,
                               ArrayRef<Expr *> Args) {
  DeclarationName OpName =
    SemaRef.Context.DeclarationNames.getCXXOperatorName(Op);
  LookupResult R(SemaRef, OpName, OpLoc, Sema::LookupOperatorName);
  return DiagnoseTwoPhaseLookup(SemaRef, OpLoc, CXXScopeSpec(), R,
                                OverloadCandidateSet::CSK_Operator,
                                /*ExplicitTemplateArgs=*/nullptr, Args);
}

namespace {
class BuildRecoveryCallExprRAII {
  Sema &SemaRef;
  Sema::SatisfactionStackResetRAII SatStack;

public:
  BuildRecoveryCallExprRAII(Sema &S) : SemaRef(S), SatStack(S) {
    assert(SemaRef.IsBuildingRecoveryCallExpr == false);
    SemaRef.IsBuildingRecoveryCallExpr = true;
  }

  ~BuildRecoveryCallExprRAII() { SemaRef.IsBuildingRecoveryCallExpr = false; }
};
}

/// Attempts to recover from a call where no functions were found.
///
/// This function will do one of three things:
///  * Diagnose, recover, and return a recovery expression.
///  * Diagnose, fail to recover, and return ExprError().
///  * Do not diagnose, do not recover, and return ExprResult(). The caller is
///    expected to diagnose as appropriate.
static ExprResult
BuildRecoveryCallExpr(Sema &SemaRef, Scope *S, Expr *Fn,
                      UnresolvedLookupExpr *ULE,
                      SourceLocation LParenLoc,
                      MutableArrayRef<Expr *> Args,
                      SourceLocation RParenLoc,
                      bool EmptyLookup, bool AllowTypoCorrection) {
  // Do not try to recover if it is already building a recovery call.
  // This stops infinite loops for template instantiations like
  //
  // template <typename T> auto foo(T t) -> decltype(foo(t)) {}
  // template <typename T> auto foo(T t) -> decltype(foo(&t)) {}
  if (SemaRef.IsBuildingRecoveryCallExpr)
    return ExprResult();
  BuildRecoveryCallExprRAII RCE(SemaRef);

  CXXScopeSpec SS;
  SS.Adopt(ULE->getQualifierLoc());
  SourceLocation TemplateKWLoc = ULE->getTemplateKeywordLoc();

  TemplateArgumentListInfo TABuffer;
  TemplateArgumentListInfo *ExplicitTemplateArgs = nullptr;
  if (ULE->hasExplicitTemplateArgs()) {
    ULE->copyTemplateArgumentsInto(TABuffer);
    ExplicitTemplateArgs = &TABuffer;
  }

  LookupResult R(SemaRef, ULE->getName(), ULE->getNameLoc(),
                 Sema::LookupOrdinaryName);
  CXXRecordDecl *FoundInClass = nullptr;
  if (DiagnoseTwoPhaseLookup(SemaRef, Fn->getExprLoc(), SS, R,
                             OverloadCandidateSet::CSK_Normal,
                             ExplicitTemplateArgs, Args, &FoundInClass)) {
    // OK, diagnosed a two-phase lookup issue.
  } else if (EmptyLookup) {
    // Try to recover from an empty lookup with typo correction.
    R.clear();
    NoTypoCorrectionCCC NoTypoValidator{};
    FunctionCallFilterCCC FunctionCallValidator(SemaRef, Args.size(),
                                                ExplicitTemplateArgs != nullptr,
                                                dyn_cast<MemberExpr>(Fn));
    CorrectionCandidateCallback &Validator =
        AllowTypoCorrection
            ? static_cast<CorrectionCandidateCallback &>(FunctionCallValidator)
            : static_cast<CorrectionCandidateCallback &>(NoTypoValidator);
    if (SemaRef.DiagnoseEmptyLookup(S, SS, R, Validator, ExplicitTemplateArgs,
                                    Args))
      return ExprError();
  } else if (FoundInClass && SemaRef.getLangOpts().MSVCCompat) {
    // We found a usable declaration of the name in a dependent base of some
    // enclosing class.
    // FIXME: We should also explain why the candidates found by name lookup
    // were not viable.
    if (SemaRef.DiagnoseDependentMemberLookup(R))
      return ExprError();
  } else {
    // We had viable candidates and couldn't recover; let the caller diagnose
    // this.
    return ExprResult();
  }

  // If we get here, we should have issued a diagnostic and formed a recovery
  // lookup result.
  assert(!R.empty() && "lookup results empty despite recovery");

  // If recovery created an ambiguity, just bail out.
  if (R.isAmbiguous()) {
    R.suppressDiagnostics();
    return ExprError();
  }

  // Build an implicit member call if appropriate.  Just drop the
  // casts and such from the call, we don't really care.
  ExprResult NewFn = ExprError();
  if ((*R.begin())->isCXXClassMember())
    NewFn = SemaRef.BuildPossibleImplicitMemberExpr(SS, TemplateKWLoc, R,
                                                    ExplicitTemplateArgs, S);
  else if (ExplicitTemplateArgs || TemplateKWLoc.isValid())
    NewFn = SemaRef.BuildTemplateIdExpr(SS, TemplateKWLoc, R, false,
                                        ExplicitTemplateArgs);
  else
    NewFn = SemaRef.BuildDeclarationNameExpr(SS, R, false);

  if (NewFn.isInvalid())
    return ExprError();

  // This shouldn't cause an infinite loop because we're giving it
  // an expression with viable lookup results, which should never
  // end up here.
  return SemaRef.BuildCallExpr(/*Scope*/ nullptr, NewFn.get(), LParenLoc,
                               MultiExprArg(Args.data(), Args.size()),
                               RParenLoc);
}

bool Sema::buildOverloadedCallSet(Scope *S, Expr *Fn,
                                  UnresolvedLookupExpr *ULE,
                                  MultiExprArg Args,
                                  SourceLocation RParenLoc,
                                  OverloadCandidateSet *CandidateSet,
                                  ExprResult *Result) {
#ifndef NDEBUG
  if (ULE->requiresADL()) {
    // To do ADL, we must have found an unqualified name.
    assert(!ULE->getQualifier() && "qualified name with ADL");

    // We don't perform ADL for implicit declarations of builtins.
    // Verify that this was correctly set up.
    FunctionDecl *F;
    if (ULE->decls_begin() != ULE->decls_end() &&
        ULE->decls_begin() + 1 == ULE->decls_end() &&
        (F = dyn_cast<FunctionDecl>(*ULE->decls_begin())) &&
        F->getBuiltinID() && F->isImplicit())
      llvm_unreachable("performing ADL for builtin");

    // We don't perform ADL in C.
    assert(getLangOpts().CPlusPlus && "ADL enabled in C");
  }
#endif

  UnbridgedCastsSet UnbridgedCasts;
  if (checkArgPlaceholdersForOverload(*this, Args, UnbridgedCasts)) {
    *Result = ExprError();
    return true;
  }

  // Add the functions denoted by the callee to the set of candidate
  // functions, including those from argument-dependent lookup.
  AddOverloadedCallCandidates(ULE, Args, *CandidateSet);

  if (getLangOpts().MSVCCompat &&
      CurContext->isDependentContext() && !isSFINAEContext() &&
      (isa<FunctionDecl>(CurContext) || isa<CXXRecordDecl>(CurContext))) {

    OverloadCandidateSet::iterator Best;
    if (CandidateSet->empty() ||
        CandidateSet->BestViableFunction(*this, Fn->getBeginLoc(), Best) ==
            OR_No_Viable_Function) {
      // In Microsoft mode, if we are inside a template class member function
      // then create a type dependent CallExpr. The goal is to postpone name
      // lookup to instantiation time to be able to search into type dependent
      // base classes.
      CallExpr *CE =
          CallExpr::Create(Context, Fn, Args, Context.DependentTy, VK_PRValue,
                           RParenLoc, CurFPFeatureOverrides());
      CE->markDependentForPostponedNameLookup();
      *Result = CE;
      return true;
    }
  }

  if (CandidateSet->empty())
    return false;

  UnbridgedCasts.restore();
  return false;
}

// Guess at what the return type for an unresolvable overload should be.
static QualType chooseRecoveryType(OverloadCandidateSet &CS,
                                   OverloadCandidateSet::iterator *Best) {
  std::optional<QualType> Result;
  // Adjust Type after seeing a candidate.
  auto ConsiderCandidate = [&](const OverloadCandidate &Candidate) {
    if (!Candidate.Function)
      return;
    if (Candidate.Function->isInvalidDecl())
      return;
    QualType T = Candidate.Function->getReturnType();
    if (T.isNull())
      return;
    if (!Result)
      Result = T;
    else if (Result != T)
      Result = QualType();
  };

  // Look for an unambiguous type from a progressively larger subset.
  // e.g. if types disagree, but all *viable* overloads return int, choose int.
  //
  // First, consider only the best candidate.
  if (Best && *Best != CS.end())
    ConsiderCandidate(**Best);
  // Next, consider only viable candidates.
  if (!Result)
    for (const auto &C : CS)
      if (C.Viable)
        ConsiderCandidate(C);
  // Finally, consider all candidates.
  if (!Result)
    for (const auto &C : CS)
      ConsiderCandidate(C);

  if (!Result)
    return QualType();
  auto Value = *Result;
  if (Value.isNull() || Value->isUndeducedType())
    return QualType();
  return Value;
}

/// FinishOverloadedCallExpr - given an OverloadCandidateSet, builds and returns
/// the completed call expression. If overload resolution fails, emits
/// diagnostics and returns ExprError()
static ExprResult FinishOverloadedCallExpr(Sema &SemaRef, Scope *S, Expr *Fn,
                                           UnresolvedLookupExpr *ULE,
                                           SourceLocation LParenLoc,
                                           MultiExprArg Args,
                                           SourceLocation RParenLoc,
                                           Expr *ExecConfig,
                                           OverloadCandidateSet *CandidateSet,
                                           OverloadCandidateSet::iterator *Best,
                                           OverloadingResult OverloadResult,
                                           bool AllowTypoCorrection) {
  switch (OverloadResult) {
  case OR_Success: {
    FunctionDecl *FDecl = (*Best)->Function;
    SemaRef.CheckUnresolvedLookupAccess(ULE, (*Best)->FoundDecl);
    if (SemaRef.DiagnoseUseOfDecl(FDecl, ULE->getNameLoc()))
      return ExprError();
    ExprResult Res =
        SemaRef.FixOverloadedFunctionReference(Fn, (*Best)->FoundDecl, FDecl);
    if (Res.isInvalid())
      return ExprError();
    return SemaRef.BuildResolvedCallExpr(
        Res.get(), FDecl, LParenLoc, Args, RParenLoc, ExecConfig,
        /*IsExecConfig=*/false, (*Best)->IsADLCandidate);
  }

  case OR_No_Viable_Function: {
    if (*Best != CandidateSet->end() &&
        CandidateSet->getKind() ==
            clang::OverloadCandidateSet::CSK_AddressOfOverloadSet) {
      if (CXXMethodDecl *M =
              dyn_cast_if_present<CXXMethodDecl>((*Best)->Function);
          M && M->isImplicitObjectMemberFunction()) {
        CandidateSet->NoteCandidates(
            PartialDiagnosticAt(
                Fn->getBeginLoc(),
                SemaRef.PDiag(diag::err_member_call_without_object) << 0 << M),
            SemaRef, OCD_AmbiguousCandidates, Args);
        return ExprError();
      }
    }

    // Try to recover by looking for viable functions which the user might
    // have meant to call.
    ExprResult Recovery = BuildRecoveryCallExpr(SemaRef, S, Fn, ULE, LParenLoc,
                                                Args, RParenLoc,
                                                CandidateSet->empty(),
                                                AllowTypoCorrection);
    if (Recovery.isInvalid() || Recovery.isUsable())
      return Recovery;

    // If the user passes in a function that we can't take the address of, we
    // generally end up emitting really bad error messages. Here, we attempt to
    // emit better ones.
    for (const Expr *Arg : Args) {
      if (!Arg->getType()->isFunctionType())
        continue;
      if (auto *DRE = dyn_cast<DeclRefExpr>(Arg->IgnoreParenImpCasts())) {
        auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl());
        if (FD &&
            !SemaRef.checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                                       Arg->getExprLoc()))
          return ExprError();
      }
    }

    CandidateSet->NoteCandidates(
        PartialDiagnosticAt(
            Fn->getBeginLoc(),
            SemaRef.PDiag(diag::err_ovl_no_viable_function_in_call)
                << ULE->getName() << Fn->getSourceRange()),
        SemaRef, OCD_AllCandidates, Args);
    break;
  }

  case OR_Ambiguous:
    CandidateSet->NoteCandidates(
        PartialDiagnosticAt(Fn->getBeginLoc(),
                            SemaRef.PDiag(diag::err_ovl_ambiguous_call)
                                << ULE->getName() << Fn->getSourceRange()),
        SemaRef, OCD_AmbiguousCandidates, Args);
    break;

  case OR_Deleted: {
    FunctionDecl *FDecl = (*Best)->Function;
    SemaRef.DiagnoseUseOfDeletedFunction(Fn->getBeginLoc(),
                                         Fn->getSourceRange(), ULE->getName(),
                                         *CandidateSet, FDecl, Args);

    // We emitted an error for the unavailable/deleted function call but keep
    // the call in the AST.
    ExprResult Res =
        SemaRef.FixOverloadedFunctionReference(Fn, (*Best)->FoundDecl, FDecl);
    if (Res.isInvalid())
      return ExprError();
    return SemaRef.BuildResolvedCallExpr(
        Res.get(), FDecl, LParenLoc, Args, RParenLoc, ExecConfig,
        /*IsExecConfig=*/false, (*Best)->IsADLCandidate);
  }
  }

  // Overload resolution failed, try to recover.
  SmallVector<Expr *, 8> SubExprs = {Fn};
  SubExprs.append(Args.begin(), Args.end());
  return SemaRef.CreateRecoveryExpr(Fn->getBeginLoc(), RParenLoc, SubExprs,
                                    chooseRecoveryType(*CandidateSet, Best));
}

static void markUnaddressableCandidatesUnviable(Sema &S,
                                                OverloadCandidateSet &CS) {
  for (auto I = CS.begin(), E = CS.end(); I != E; ++I) {
    if (I->Viable &&
        !S.checkAddressOfFunctionIsAvailable(I->Function, /*Complain=*/false)) {
      I->Viable = false;
      I->FailureKind = ovl_fail_addr_not_available;
    }
  }
}

ExprResult Sema::BuildOverloadedCallExpr(Scope *S, Expr *Fn,
                                         UnresolvedLookupExpr *ULE,
                                         SourceLocation LParenLoc,
                                         MultiExprArg Args,
                                         SourceLocation RParenLoc,
                                         Expr *ExecConfig,
                                         bool AllowTypoCorrection,
                                         bool CalleesAddressIsTaken) {
  OverloadCandidateSet CandidateSet(
      Fn->getExprLoc(), CalleesAddressIsTaken
                            ? OverloadCandidateSet::CSK_AddressOfOverloadSet
                            : OverloadCandidateSet::CSK_Normal);
  ExprResult result;

  if (buildOverloadedCallSet(S, Fn, ULE, Args, LParenLoc, &CandidateSet,
                             &result))
    return result;

  // If the user handed us something like `(&Foo)(Bar)`, we need to ensure that
  // functions that aren't addressible are considered unviable.
  if (CalleesAddressIsTaken)
    markUnaddressableCandidatesUnviable(*this, CandidateSet);

  OverloadCandidateSet::iterator Best;
  OverloadingResult OverloadResult =
      CandidateSet.BestViableFunction(*this, Fn->getBeginLoc(), Best);

  // Model the case with a call to a templated function whose definition
  // encloses the call and whose return type contains a placeholder type as if
  // the UnresolvedLookupExpr was type-dependent.
  if (OverloadResult == OR_Success) {
    const FunctionDecl *FDecl = Best->Function;
    if (FDecl && FDecl->isTemplateInstantiation() &&
        FDecl->getReturnType()->isUndeducedType()) {
      if (const auto *TP =
              FDecl->getTemplateInstantiationPattern(/*ForDefinition=*/false);
          TP && TP->willHaveBody()) {
        return CallExpr::Create(Context, Fn, Args, Context.DependentTy,
                                VK_PRValue, RParenLoc, CurFPFeatureOverrides());
      }
    }
  }

  return FinishOverloadedCallExpr(*this, S, Fn, ULE, LParenLoc, Args, RParenLoc,
                                  ExecConfig, &CandidateSet, &Best,
                                  OverloadResult, AllowTypoCorrection);
}

ExprResult Sema::CreateUnresolvedLookupExpr(CXXRecordDecl *NamingClass,
                                            NestedNameSpecifierLoc NNSLoc,
                                            DeclarationNameInfo DNI,
                                            const UnresolvedSetImpl &Fns,
                                            bool PerformADL) {
  return UnresolvedLookupExpr::Create(
      Context, NamingClass, NNSLoc, DNI, PerformADL, Fns.begin(), Fns.end(),
      /*KnownDependent=*/false, /*KnownInstantiationDependent=*/false);
}

ExprResult Sema::BuildCXXMemberCallExpr(Expr *E, NamedDecl *FoundDecl,
                                        CXXConversionDecl *Method,
                                        bool HadMultipleCandidates) {
  // Convert the expression to match the conversion function's implicit object
  // parameter.
  ExprResult Exp;
  if (Method->isExplicitObjectMemberFunction())
    Exp = InitializeExplicitObjectArgument(*this, E, Method);
  else
    Exp = PerformImplicitObjectArgumentInitialization(E, /*Qualifier=*/nullptr,
                                                      FoundDecl, Method);
  if (Exp.isInvalid())
    return true;

  if (Method->getParent()->isLambda() &&
      Method->getConversionType()->isBlockPointerType()) {
    // This is a lambda conversion to block pointer; check if the argument
    // was a LambdaExpr.
    Expr *SubE = E;
    auto *CE = dyn_cast<CastExpr>(SubE);
    if (CE && CE->getCastKind() == CK_NoOp)
      SubE = CE->getSubExpr();
    SubE = SubE->IgnoreParens();
    if (auto *BE = dyn_cast<CXXBindTemporaryExpr>(SubE))
      SubE = BE->getSubExpr();
    if (isa<LambdaExpr>(SubE)) {
      // For the conversion to block pointer on a lambda expression, we
      // construct a special BlockLiteral instead; this doesn't really make
      // a difference in ARC, but outside of ARC the resulting block literal
      // follows the normal lifetime rules for block literals instead of being
      // autoreleased.
      PushExpressionEvaluationContext(
          ExpressionEvaluationContext::PotentiallyEvaluated);
      ExprResult BlockExp = BuildBlockForLambdaConversion(
          Exp.get()->getExprLoc(), Exp.get()->getExprLoc(), Method, Exp.get());
      PopExpressionEvaluationContext();

      // FIXME: This note should be produced by a CodeSynthesisContext.
      if (BlockExp.isInvalid())
        Diag(Exp.get()->getExprLoc(), diag::note_lambda_to_block_conv);
      return BlockExp;
    }
  }
  CallExpr *CE;
  QualType ResultType = Method->getReturnType();
  ExprValueKind VK = Expr::getValueKindForType(ResultType);
  ResultType = ResultType.getNonLValueExprType(Context);
  if (Method->isExplicitObjectMemberFunction()) {
    ExprResult FnExpr =
        CreateFunctionRefExpr(*this, Method, FoundDecl, Exp.get(),
                              HadMultipleCandidates, E->getBeginLoc());
    if (FnExpr.isInvalid())
      return ExprError();
    Expr *ObjectParam = Exp.get();
    CE = CallExpr::Create(Context, FnExpr.get(), MultiExprArg(&ObjectParam, 1),
                          ResultType, VK, Exp.get()->getEndLoc(),
                          CurFPFeatureOverrides());
  } else {
    MemberExpr *ME =
        BuildMemberExpr(Exp.get(), /*IsArrow=*/false, SourceLocation(),
                        NestedNameSpecifierLoc(), SourceLocation(), Method,
                        DeclAccessPair::make(FoundDecl, FoundDecl->getAccess()),
                        HadMultipleCandidates, DeclarationNameInfo(),
                        Context.BoundMemberTy, VK_PRValue, OK_Ordinary);

    CE = CXXMemberCallExpr::Create(Context, ME, /*Args=*/{}, ResultType, VK,
                                   Exp.get()->getEndLoc(),
                                   CurFPFeatureOverrides());
  }

  if (CheckFunctionCall(Method, CE,
                        Method->getType()->castAs<FunctionProtoType>()))
    return ExprError();

  return CheckForImmediateInvocation(CE, CE->getDirectCallee());
}

ExprResult
Sema::CreateOverloadedUnaryOp(SourceLocation OpLoc, UnaryOperatorKind Opc,
                              const UnresolvedSetImpl &Fns,
                              Expr *Input, bool PerformADL) {
  OverloadedOperatorKind Op = UnaryOperator::getOverloadedOperator(Opc);
  assert(Op != OO_None && "Invalid opcode for overloaded unary operator");
  DeclarationName OpName = Context.DeclarationNames.getCXXOperatorName(Op);
  // TODO: provide better source location info.
  DeclarationNameInfo OpNameInfo(OpName, OpLoc);

  if (checkPlaceholderForOverload(*this, Input))
    return ExprError();

  Expr *Args[2] = { Input, nullptr };
  unsigned NumArgs = 1;

  // For post-increment and post-decrement, add the implicit '0' as
  // the second argument, so that we know this is a post-increment or
  // post-decrement.
  if (Opc == UO_PostInc || Opc == UO_PostDec) {
    llvm::APSInt Zero(Context.getTypeSize(Context.IntTy), false);
    Args[1] = IntegerLiteral::Create(Context, Zero, Context.IntTy,
                                     SourceLocation());
    NumArgs = 2;
  }

  ArrayRef<Expr *> ArgsArray(Args, NumArgs);

  if (Input->isTypeDependent()) {
    ExprValueKind VK = ExprValueKind::VK_PRValue;
    // [C++26][expr.unary.op][expr.pre.incr]
    // The * operator yields an lvalue of type
    // The pre/post increment operators yied an lvalue.
    if (Opc == UO_PreDec || Opc == UO_PreInc || Opc == UO_Deref)
      VK = VK_LValue;

    if (Fns.empty())
      return UnaryOperator::Create(Context, Input, Opc, Context.DependentTy, VK,
                                   OK_Ordinary, OpLoc, false,
                                   CurFPFeatureOverrides());

    CXXRecordDecl *NamingClass = nullptr; // lookup ignores member operators
    ExprResult Fn = CreateUnresolvedLookupExpr(
        NamingClass, NestedNameSpecifierLoc(), OpNameInfo, Fns);
    if (Fn.isInvalid())
      return ExprError();
    return CXXOperatorCallExpr::Create(Context, Op, Fn.get(), ArgsArray,
                                       Context.DependentTy, VK_PRValue, OpLoc,
                                       CurFPFeatureOverrides());
  }

  // Build an empty overload set.
  OverloadCandidateSet CandidateSet(OpLoc, OverloadCandidateSet::CSK_Operator);

  // Add the candidates from the given function set.
  AddNonMemberOperatorCandidates(Fns, ArgsArray, CandidateSet);

  // Add operator candidates that are member functions.
  AddMemberOperatorCandidates(Op, OpLoc, ArgsArray, CandidateSet);

  // Add candidates from ADL.
  if (PerformADL) {
    AddArgumentDependentLookupCandidates(OpName, OpLoc, ArgsArray,
                                         /*ExplicitTemplateArgs*/nullptr,
                                         CandidateSet);
  }

  // Add builtin operator candidates.
  AddBuiltinOperatorCandidates(Op, OpLoc, ArgsArray, CandidateSet);

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  // Perform overload resolution.
  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(*this, OpLoc, Best)) {
  case OR_Success: {
    // We found a built-in operator or an overloaded operator.
    FunctionDecl *FnDecl = Best->Function;

    if (FnDecl) {
      Expr *Base = nullptr;
      // We matched an overloaded operator. Build a call to that
      // operator.

      // Convert the arguments.
      if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(FnDecl)) {
        CheckMemberOperatorAccess(OpLoc, Input, nullptr, Best->FoundDecl);

        ExprResult InputInit;
        if (Method->isExplicitObjectMemberFunction())
          InputInit = InitializeExplicitObjectArgument(*this, Input, Method);
        else
          InputInit = PerformImplicitObjectArgumentInitialization(
              Input, /*Qualifier=*/nullptr, Best->FoundDecl, Method);
        if (InputInit.isInvalid())
          return ExprError();
        Base = Input = InputInit.get();
      } else {
        // Convert the arguments.
        ExprResult InputInit
          = PerformCopyInitialization(InitializedEntity::InitializeParameter(
                                                      Context,
                                                      FnDecl->getParamDecl(0)),
                                      SourceLocation(),
                                      Input);
        if (InputInit.isInvalid())
          return ExprError();
        Input = InputInit.get();
      }

      // Build the actual expression node.
      ExprResult FnExpr = CreateFunctionRefExpr(*this, FnDecl, Best->FoundDecl,
                                                Base, HadMultipleCandidates,
                                                OpLoc);
      if (FnExpr.isInvalid())
        return ExprError();

      // Determine the result type.
      QualType ResultTy = FnDecl->getReturnType();
      ExprValueKind VK = Expr::getValueKindForType(ResultTy);
      ResultTy = ResultTy.getNonLValueExprType(Context);

      Args[0] = Input;
      CallExpr *TheCall = CXXOperatorCallExpr::Create(
          Context, Op, FnExpr.get(), ArgsArray, ResultTy, VK, OpLoc,
          CurFPFeatureOverrides(), Best->IsADLCandidate);

      if (CheckCallReturnType(FnDecl->getReturnType(), OpLoc, TheCall, FnDecl))
        return ExprError();

      if (CheckFunctionCall(FnDecl, TheCall,
                            FnDecl->getType()->castAs<FunctionProtoType>()))
        return ExprError();
      return CheckForImmediateInvocation(MaybeBindToTemporary(TheCall), FnDecl);
    } else {
      // We matched a built-in operator. Convert the arguments, then
      // break out so that we will build the appropriate built-in
      // operator node.
      ExprResult InputRes = PerformImplicitConversion(
          Input, Best->BuiltinParamTypes[0], Best->Conversions[0], AA_Passing,
          CheckedConversionKind::ForBuiltinOverloadedOp);
      if (InputRes.isInvalid())
        return ExprError();
      Input = InputRes.get();
      break;
    }
  }

  case OR_No_Viable_Function:
    // This is an erroneous use of an operator which can be overloaded by
    // a non-member function. Check for non-member operators which were
    // defined too late to be candidates.
    if (DiagnoseTwoPhaseOperatorLookup(*this, Op, OpLoc, ArgsArray))
      // FIXME: Recover by calling the found function.
      return ExprError();

    // No viable function; fall through to handling this as a
    // built-in operator, which will produce an error message for us.
    break;

  case OR_Ambiguous:
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(OpLoc,
                            PDiag(diag::err_ovl_ambiguous_oper_unary)
                                << UnaryOperator::getOpcodeStr(Opc)
                                << Input->getType() << Input->getSourceRange()),
        *this, OCD_AmbiguousCandidates, ArgsArray,
        UnaryOperator::getOpcodeStr(Opc), OpLoc);
    return ExprError();

  case OR_Deleted: {
    // CreateOverloadedUnaryOp fills the first element of ArgsArray with the
    // object whose method was called. Later in NoteCandidates size of ArgsArray
    // is passed further and it eventually ends up compared to number of
    // function candidate parameters which never includes the object parameter,
    // so slice ArgsArray to make sure apples are compared to apples.
    StringLiteral *Msg = Best->Function->getDeletedMessage();
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(OpLoc, PDiag(diag::err_ovl_deleted_oper)
                                       << UnaryOperator::getOpcodeStr(Opc)
                                       << (Msg != nullptr)
                                       << (Msg ? Msg->getString() : StringRef())
                                       << Input->getSourceRange()),
        *this, OCD_AllCandidates, ArgsArray.drop_front(),
        UnaryOperator::getOpcodeStr(Opc), OpLoc);
    return ExprError();
  }
  }

  // Either we found no viable overloaded operator or we matched a
  // built-in operator. In either case, fall through to trying to
  // build a built-in operation.
  return CreateBuiltinUnaryOp(OpLoc, Opc, Input);
}

void Sema::LookupOverloadedBinOp(OverloadCandidateSet &CandidateSet,
                                 OverloadedOperatorKind Op,
                                 const UnresolvedSetImpl &Fns,
                                 ArrayRef<Expr *> Args, bool PerformADL) {
  SourceLocation OpLoc = CandidateSet.getLocation();

  OverloadedOperatorKind ExtraOp =
      CandidateSet.getRewriteInfo().AllowRewrittenCandidates
          ? getRewrittenOverloadedOperator(Op)
          : OO_None;

  // Add the candidates from the given function set. This also adds the
  // rewritten candidates using these functions if necessary.
  AddNonMemberOperatorCandidates(Fns, Args, CandidateSet);

  // Add operator candidates that are member functions.
  AddMemberOperatorCandidates(Op, OpLoc, Args, CandidateSet);
  if (CandidateSet.getRewriteInfo().allowsReversed(Op))
    AddMemberOperatorCandidates(Op, OpLoc, {Args[1], Args[0]}, CandidateSet,
                                OverloadCandidateParamOrder::Reversed);

  // In C++20, also add any rewritten member candidates.
  if (ExtraOp) {
    AddMemberOperatorCandidates(ExtraOp, OpLoc, Args, CandidateSet);
    if (CandidateSet.getRewriteInfo().allowsReversed(ExtraOp))
      AddMemberOperatorCandidates(ExtraOp, OpLoc, {Args[1], Args[0]},
                                  CandidateSet,
                                  OverloadCandidateParamOrder::Reversed);
  }

  // Add candidates from ADL. Per [over.match.oper]p2, this lookup is not
  // performed for an assignment operator (nor for operator[] nor operator->,
  // which don't get here).
  if (Op != OO_Equal && PerformADL) {
    DeclarationName OpName = Context.DeclarationNames.getCXXOperatorName(Op);
    AddArgumentDependentLookupCandidates(OpName, OpLoc, Args,
                                         /*ExplicitTemplateArgs*/ nullptr,
                                         CandidateSet);
    if (ExtraOp) {
      DeclarationName ExtraOpName =
          Context.DeclarationNames.getCXXOperatorName(ExtraOp);
      AddArgumentDependentLookupCandidates(ExtraOpName, OpLoc, Args,
                                           /*ExplicitTemplateArgs*/ nullptr,
                                           CandidateSet);
    }
  }

  // Add builtin operator candidates.
  //
  // FIXME: We don't add any rewritten candidates here. This is strictly
  // incorrect; a builtin candidate could be hidden by a non-viable candidate,
  // resulting in our selecting a rewritten builtin candidate. For example:
  //
  //   enum class E { e };
  //   bool operator!=(E, E) requires false;
  //   bool k = E::e != E::e;
  //
  // ... should select the rewritten builtin candidate 'operator==(E, E)'. But
  // it seems unreasonable to consider rewritten builtin candidates. A core
  // issue has been filed proposing to removed this requirement.
  AddBuiltinOperatorCandidates(Op, OpLoc, Args, CandidateSet);
}

ExprResult Sema::CreateOverloadedBinOp(SourceLocation OpLoc,
                                       BinaryOperatorKind Opc,
                                       const UnresolvedSetImpl &Fns, Expr *LHS,
                                       Expr *RHS, bool PerformADL,
                                       bool AllowRewrittenCandidates,
                                       FunctionDecl *DefaultedFn) {
  Expr *Args[2] = { LHS, RHS };
  LHS=RHS=nullptr; // Please use only Args instead of LHS/RHS couple

  if (!getLangOpts().CPlusPlus20)
    AllowRewrittenCandidates = false;

  OverloadedOperatorKind Op = BinaryOperator::getOverloadedOperator(Opc);

  // If either side is type-dependent, create an appropriate dependent
  // expression.
  if (Args[0]->isTypeDependent() || Args[1]->isTypeDependent()) {
    if (Fns.empty()) {
      // If there are no functions to store, just build a dependent
      // BinaryOperator or CompoundAssignment.
      if (BinaryOperator::isCompoundAssignmentOp(Opc))
        return CompoundAssignOperator::Create(
            Context, Args[0], Args[1], Opc, Context.DependentTy, VK_LValue,
            OK_Ordinary, OpLoc, CurFPFeatureOverrides(), Context.DependentTy,
            Context.DependentTy);
      return BinaryOperator::Create(
          Context, Args[0], Args[1], Opc, Context.DependentTy, VK_PRValue,
          OK_Ordinary, OpLoc, CurFPFeatureOverrides());
    }

    // FIXME: save results of ADL from here?
    CXXRecordDecl *NamingClass = nullptr; // lookup ignores member operators
    // TODO: provide better source location info in DNLoc component.
    DeclarationName OpName = Context.DeclarationNames.getCXXOperatorName(Op);
    DeclarationNameInfo OpNameInfo(OpName, OpLoc);
    ExprResult Fn = CreateUnresolvedLookupExpr(
        NamingClass, NestedNameSpecifierLoc(), OpNameInfo, Fns, PerformADL);
    if (Fn.isInvalid())
      return ExprError();
    return CXXOperatorCallExpr::Create(Context, Op, Fn.get(), Args,
                                       Context.DependentTy, VK_PRValue, OpLoc,
                                       CurFPFeatureOverrides());
  }

  // If this is the .* operator, which is not overloadable, just
  // create a built-in binary operator.
  if (Opc == BO_PtrMemD) {
    auto CheckPlaceholder = [&](Expr *&Arg) {
      ExprResult Res = CheckPlaceholderExpr(Arg);
      if (Res.isUsable())
        Arg = Res.get();
      return !Res.isUsable();
    };

    // CreateBuiltinBinOp() doesn't like it if we tell it to create a '.*'
    // expression that contains placeholders (in either the LHS or RHS).
    if (CheckPlaceholder(Args[0]) || CheckPlaceholder(Args[1]))
      return ExprError();
    return CreateBuiltinBinOp(OpLoc, Opc, Args[0], Args[1]);
  }

  // Always do placeholder-like conversions on the RHS.
  if (checkPlaceholderForOverload(*this, Args[1]))
    return ExprError();

  // Do placeholder-like conversion on the LHS; note that we should
  // not get here with a PseudoObject LHS.
  assert(Args[0]->getObjectKind() != OK_ObjCProperty);
  if (checkPlaceholderForOverload(*this, Args[0]))
    return ExprError();

  // If this is the assignment operator, we only perform overload resolution
  // if the left-hand side is a class or enumeration type. This is actually
  // a hack. The standard requires that we do overload resolution between the
  // various built-in candidates, but as DR507 points out, this can lead to
  // problems. So we do it this way, which pretty much follows what GCC does.
  // Note that we go the traditional code path for compound assignment forms.
  if (Opc == BO_Assign && !Args[0]->getType()->isOverloadableType())
    return CreateBuiltinBinOp(OpLoc, Opc, Args[0], Args[1]);

  // Build the overload set.
  OverloadCandidateSet CandidateSet(OpLoc, OverloadCandidateSet::CSK_Operator,
                                    OverloadCandidateSet::OperatorRewriteInfo(
                                        Op, OpLoc, AllowRewrittenCandidates));
  if (DefaultedFn)
    CandidateSet.exclude(DefaultedFn);
  LookupOverloadedBinOp(CandidateSet, Op, Fns, Args, PerformADL);

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  // Perform overload resolution.
  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(*this, OpLoc, Best)) {
    case OR_Success: {
      // We found a built-in operator or an overloaded operator.
      FunctionDecl *FnDecl = Best->Function;

      bool IsReversed = Best->isReversed();
      if (IsReversed)
        std::swap(Args[0], Args[1]);

      if (FnDecl) {

        if (FnDecl->isInvalidDecl())
          return ExprError();

        Expr *Base = nullptr;
        // We matched an overloaded operator. Build a call to that
        // operator.

        OverloadedOperatorKind ChosenOp =
            FnDecl->getDeclName().getCXXOverloadedOperator();

        // C++2a [over.match.oper]p9:
        //   If a rewritten operator== candidate is selected by overload
        //   resolution for an operator@, its return type shall be cv bool
        if (Best->RewriteKind && ChosenOp == OO_EqualEqual &&
            !FnDecl->getReturnType()->isBooleanType()) {
          bool IsExtension =
              FnDecl->getReturnType()->isIntegralOrUnscopedEnumerationType();
          Diag(OpLoc, IsExtension ? diag::ext_ovl_rewrite_equalequal_not_bool
                                  : diag::err_ovl_rewrite_equalequal_not_bool)
              << FnDecl->getReturnType() << BinaryOperator::getOpcodeStr(Opc)
              << Args[0]->getSourceRange() << Args[1]->getSourceRange();
          Diag(FnDecl->getLocation(), diag::note_declared_at);
          if (!IsExtension)
            return ExprError();
        }

        if (AllowRewrittenCandidates && !IsReversed &&
            CandidateSet.getRewriteInfo().isReversible()) {
          // We could have reversed this operator, but didn't. Check if some
          // reversed form was a viable candidate, and if so, if it had a
          // better conversion for either parameter. If so, this call is
          // formally ambiguous, and allowing it is an extension.
          llvm::SmallVector<FunctionDecl*, 4> AmbiguousWith;
          for (OverloadCandidate &Cand : CandidateSet) {
            if (Cand.Viable && Cand.Function && Cand.isReversed() &&
                allowAmbiguity(Context, Cand.Function, FnDecl)) {
              for (unsigned ArgIdx = 0; ArgIdx < 2; ++ArgIdx) {
                if (CompareImplicitConversionSequences(
                        *this, OpLoc, Cand.Conversions[ArgIdx],
                        Best->Conversions[ArgIdx]) ==
                    ImplicitConversionSequence::Better) {
                  AmbiguousWith.push_back(Cand.Function);
                  break;
                }
              }
            }
          }

          if (!AmbiguousWith.empty()) {
            bool AmbiguousWithSelf =
                AmbiguousWith.size() == 1 &&
                declaresSameEntity(AmbiguousWith.front(), FnDecl);
            Diag(OpLoc, diag::ext_ovl_ambiguous_oper_binary_reversed)
                << BinaryOperator::getOpcodeStr(Opc)
                << Args[0]->getType() << Args[1]->getType() << AmbiguousWithSelf
                << Args[0]->getSourceRange() << Args[1]->getSourceRange();
            if (AmbiguousWithSelf) {
              Diag(FnDecl->getLocation(),
                   diag::note_ovl_ambiguous_oper_binary_reversed_self);
              // Mark member== const or provide matching != to disallow reversed
              // args. Eg.
              // struct S { bool operator==(const S&); };
              // S()==S();
              if (auto *MD = dyn_cast<CXXMethodDecl>(FnDecl))
                if (Op == OverloadedOperatorKind::OO_EqualEqual &&
                    !MD->isConst() &&
                    !MD->hasCXXExplicitFunctionObjectParameter() &&
                    Context.hasSameUnqualifiedType(
                        MD->getFunctionObjectParameterType(),
                        MD->getParamDecl(0)->getType().getNonReferenceType()) &&
                    Context.hasSameUnqualifiedType(
                        MD->getFunctionObjectParameterType(),
                        Args[0]->getType()) &&
                    Context.hasSameUnqualifiedType(
                        MD->getFunctionObjectParameterType(),
                        Args[1]->getType()))
                  Diag(FnDecl->getLocation(),
                       diag::note_ovl_ambiguous_eqeq_reversed_self_non_const);
            } else {
              Diag(FnDecl->getLocation(),
                   diag::note_ovl_ambiguous_oper_binary_selected_candidate);
              for (auto *F : AmbiguousWith)
                Diag(F->getLocation(),
                     diag::note_ovl_ambiguous_oper_binary_reversed_candidate);
            }
          }
        }

        // Check for nonnull = nullable.
        // This won't be caught in the arg's initialization: the parameter to
        // the assignment operator is not marked nonnull.
        if (Op == OO_Equal)
          diagnoseNullableToNonnullConversion(Args[0]->getType(),
                                              Args[1]->getType(), OpLoc);

        // Convert the arguments.
        if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(FnDecl)) {
          // Best->Access is only meaningful for class members.
          CheckMemberOperatorAccess(OpLoc, Args[0], Args[1], Best->FoundDecl);

          ExprResult Arg0, Arg1;
          unsigned ParamIdx = 0;
          if (Method->isExplicitObjectMemberFunction()) {
            Arg0 = InitializeExplicitObjectArgument(*this, Args[0], FnDecl);
            ParamIdx = 1;
          } else {
            Arg0 = PerformImplicitObjectArgumentInitialization(
                Args[0], /*Qualifier=*/nullptr, Best->FoundDecl, Method);
          }
          Arg1 = PerformCopyInitialization(
              InitializedEntity::InitializeParameter(
                  Context, FnDecl->getParamDecl(ParamIdx)),
              SourceLocation(), Args[1]);
          if (Arg0.isInvalid() || Arg1.isInvalid())
            return ExprError();

          Base = Args[0] = Arg0.getAs<Expr>();
          Args[1] = RHS = Arg1.getAs<Expr>();
        } else {
          // Convert the arguments.
          ExprResult Arg0 = PerformCopyInitialization(
            InitializedEntity::InitializeParameter(Context,
                                                   FnDecl->getParamDecl(0)),
            SourceLocation(), Args[0]);
          if (Arg0.isInvalid())
            return ExprError();

          ExprResult Arg1 =
            PerformCopyInitialization(
              InitializedEntity::InitializeParameter(Context,
                                                     FnDecl->getParamDecl(1)),
              SourceLocation(), Args[1]);
          if (Arg1.isInvalid())
            return ExprError();
          Args[0] = LHS = Arg0.getAs<Expr>();
          Args[1] = RHS = Arg1.getAs<Expr>();
        }

        // Build the actual expression node.
        ExprResult FnExpr = CreateFunctionRefExpr(*this, FnDecl,
                                                  Best->FoundDecl, Base,
                                                  HadMultipleCandidates, OpLoc);
        if (FnExpr.isInvalid())
          return ExprError();

        // Determine the result type.
        QualType ResultTy = FnDecl->getReturnType();
        ExprValueKind VK = Expr::getValueKindForType(ResultTy);
        ResultTy = ResultTy.getNonLValueExprType(Context);

        CallExpr *TheCall;
        ArrayRef<const Expr *> ArgsArray(Args, 2);
        const Expr *ImplicitThis = nullptr;

        // We always create a CXXOperatorCallExpr, even for explicit object
        // members; CodeGen should take care not to emit the this pointer.
        TheCall = CXXOperatorCallExpr::Create(
            Context, ChosenOp, FnExpr.get(), Args, ResultTy, VK, OpLoc,
            CurFPFeatureOverrides(), Best->IsADLCandidate);

        if (const auto *Method = dyn_cast<CXXMethodDecl>(FnDecl);
            Method && Method->isImplicitObjectMemberFunction()) {
          // Cut off the implicit 'this'.
          ImplicitThis = ArgsArray[0];
          ArgsArray = ArgsArray.slice(1);
        }

        if (CheckCallReturnType(FnDecl->getReturnType(), OpLoc, TheCall,
                                FnDecl))
          return ExprError();

        if (Op == OO_Equal) {
          // Check for a self move.
          DiagnoseSelfMove(Args[0], Args[1], OpLoc);
          // lifetime check.
          checkExprLifetime(*this, AssignedEntity{Args[0]}, Args[1]);
        }
        if (ImplicitThis) {
          QualType ThisType = Context.getPointerType(ImplicitThis->getType());
          QualType ThisTypeFromDecl = Context.getPointerType(
              cast<CXXMethodDecl>(FnDecl)->getFunctionObjectParameterType());

          CheckArgAlignment(OpLoc, FnDecl, "'this'", ThisType,
                            ThisTypeFromDecl);
        }

        checkCall(FnDecl, nullptr, ImplicitThis, ArgsArray,
                  isa<CXXMethodDecl>(FnDecl), OpLoc, TheCall->getSourceRange(),
                  VariadicDoesNotApply);

        ExprResult R = MaybeBindToTemporary(TheCall);
        if (R.isInvalid())
          return ExprError();

        R = CheckForImmediateInvocation(R, FnDecl);
        if (R.isInvalid())
          return ExprError();

        // For a rewritten candidate, we've already reversed the arguments
        // if needed. Perform the rest of the rewrite now.
        if ((Best->RewriteKind & CRK_DifferentOperator) ||
            (Op == OO_Spaceship && IsReversed)) {
          if (Op == OO_ExclaimEqual) {
            assert(ChosenOp == OO_EqualEqual && "unexpected operator name");
            R = CreateBuiltinUnaryOp(OpLoc, UO_LNot, R.get());
          } else {
            assert(ChosenOp == OO_Spaceship && "unexpected operator name");
            llvm::APSInt Zero(Context.getTypeSize(Context.IntTy), false);
            Expr *ZeroLiteral =
                IntegerLiteral::Create(Context, Zero, Context.IntTy, OpLoc);

            Sema::CodeSynthesisContext Ctx;
            Ctx.Kind = Sema::CodeSynthesisContext::RewritingOperatorAsSpaceship;
            Ctx.Entity = FnDecl;
            pushCodeSynthesisContext(Ctx);

            R = CreateOverloadedBinOp(
                OpLoc, Opc, Fns, IsReversed ? ZeroLiteral : R.get(),
                IsReversed ? R.get() : ZeroLiteral, /*PerformADL=*/true,
                /*AllowRewrittenCandidates=*/false);

            popCodeSynthesisContext();
          }
          if (R.isInvalid())
            return ExprError();
        } else {
          assert(ChosenOp == Op && "unexpected operator name");
        }

        // Make a note in the AST if we did any rewriting.
        if (Best->RewriteKind != CRK_None)
          R = new (Context) CXXRewrittenBinaryOperator(R.get(), IsReversed);

        return R;
      } else {
        // We matched a built-in operator. Convert the arguments, then
        // break out so that we will build the appropriate built-in
        // operator node.
        ExprResult ArgsRes0 = PerformImplicitConversion(
            Args[0], Best->BuiltinParamTypes[0], Best->Conversions[0],
            AA_Passing, CheckedConversionKind::ForBuiltinOverloadedOp);
        if (ArgsRes0.isInvalid())
          return ExprError();
        Args[0] = ArgsRes0.get();

        ExprResult ArgsRes1 = PerformImplicitConversion(
            Args[1], Best->BuiltinParamTypes[1], Best->Conversions[1],
            AA_Passing, CheckedConversionKind::ForBuiltinOverloadedOp);
        if (ArgsRes1.isInvalid())
          return ExprError();
        Args[1] = ArgsRes1.get();
        break;
      }
    }

    case OR_No_Viable_Function: {
      // C++ [over.match.oper]p9:
      //   If the operator is the operator , [...] and there are no
      //   viable functions, then the operator is assumed to be the
      //   built-in operator and interpreted according to clause 5.
      if (Opc == BO_Comma)
        break;

      // When defaulting an 'operator<=>', we can try to synthesize a three-way
      // compare result using '==' and '<'.
      if (DefaultedFn && Opc == BO_Cmp) {
        ExprResult E = BuildSynthesizedThreeWayComparison(OpLoc, Fns, Args[0],
                                                          Args[1], DefaultedFn);
        if (E.isInvalid() || E.isUsable())
          return E;
      }

      // For class as left operand for assignment or compound assignment
      // operator do not fall through to handling in built-in, but report that
      // no overloaded assignment operator found
      ExprResult Result = ExprError();
      StringRef OpcStr = BinaryOperator::getOpcodeStr(Opc);
      auto Cands = CandidateSet.CompleteCandidates(*this, OCD_AllCandidates,
                                                   Args, OpLoc);
      DeferDiagsRAII DDR(*this,
                         CandidateSet.shouldDeferDiags(*this, Args, OpLoc));
      if (Args[0]->getType()->isRecordType() &&
          Opc >= BO_Assign && Opc <= BO_OrAssign) {
        Diag(OpLoc,  diag::err_ovl_no_viable_oper)
             << BinaryOperator::getOpcodeStr(Opc)
             << Args[0]->getSourceRange() << Args[1]->getSourceRange();
        if (Args[0]->getType()->isIncompleteType()) {
          Diag(OpLoc, diag::note_assign_lhs_incomplete)
            << Args[0]->getType()
            << Args[0]->getSourceRange() << Args[1]->getSourceRange();
        }
      } else {
        // This is an erroneous use of an operator which can be overloaded by
        // a non-member function. Check for non-member operators which were
        // defined too late to be candidates.
        if (DiagnoseTwoPhaseOperatorLookup(*this, Op, OpLoc, Args))
          // FIXME: Recover by calling the found function.
          return ExprError();

        // No viable function; try to create a built-in operation, which will
        // produce an error. Then, show the non-viable candidates.
        Result = CreateBuiltinBinOp(OpLoc, Opc, Args[0], Args[1]);
      }
      assert(Result.isInvalid() &&
             "C++ binary operator overloading is missing candidates!");
      CandidateSet.NoteCandidates(*this, Args, Cands, OpcStr, OpLoc);
      return Result;
    }

    case OR_Ambiguous:
      CandidateSet.NoteCandidates(
          PartialDiagnosticAt(OpLoc, PDiag(diag::err_ovl_ambiguous_oper_binary)
                                         << BinaryOperator::getOpcodeStr(Opc)
                                         << Args[0]->getType()
                                         << Args[1]->getType()
                                         << Args[0]->getSourceRange()
                                         << Args[1]->getSourceRange()),
          *this, OCD_AmbiguousCandidates, Args, BinaryOperator::getOpcodeStr(Opc),
          OpLoc);
      return ExprError();

    case OR_Deleted: {
      if (isImplicitlyDeleted(Best->Function)) {
        FunctionDecl *DeletedFD = Best->Function;
        DefaultedFunctionKind DFK = getDefaultedFunctionKind(DeletedFD);
        if (DFK.isSpecialMember()) {
          Diag(OpLoc, diag::err_ovl_deleted_special_oper)
              << Args[0]->getType()
              << llvm::to_underlying(DFK.asSpecialMember());
        } else {
          assert(DFK.isComparison());
          Diag(OpLoc, diag::err_ovl_deleted_comparison)
            << Args[0]->getType() << DeletedFD;
        }

        // The user probably meant to call this special member. Just
        // explain why it's deleted.
        NoteDeletedFunction(DeletedFD);
        return ExprError();
      }

      StringLiteral *Msg = Best->Function->getDeletedMessage();
      CandidateSet.NoteCandidates(
          PartialDiagnosticAt(
              OpLoc,
              PDiag(diag::err_ovl_deleted_oper)
                  << getOperatorSpelling(Best->Function->getDeclName()
                                             .getCXXOverloadedOperator())
                  << (Msg != nullptr) << (Msg ? Msg->getString() : StringRef())
                  << Args[0]->getSourceRange() << Args[1]->getSourceRange()),
          *this, OCD_AllCandidates, Args, BinaryOperator::getOpcodeStr(Opc),
          OpLoc);
      return ExprError();
    }
  }

  // We matched a built-in operator; build it.
  return CreateBuiltinBinOp(OpLoc, Opc, Args[0], Args[1]);
}

ExprResult Sema::BuildSynthesizedThreeWayComparison(
    SourceLocation OpLoc, const UnresolvedSetImpl &Fns, Expr *LHS, Expr *RHS,
    FunctionDecl *DefaultedFn) {
  const ComparisonCategoryInfo *Info =
      Context.CompCategories.lookupInfoForType(DefaultedFn->getReturnType());
  // If we're not producing a known comparison category type, we can't
  // synthesize a three-way comparison. Let the caller diagnose this.
  if (!Info)
    return ExprResult((Expr*)nullptr);

  // If we ever want to perform this synthesis more generally, we will need to
  // apply the temporary materialization conversion to the operands.
  assert(LHS->isGLValue() && RHS->isGLValue() &&
         "cannot use prvalue expressions more than once");
  Expr *OrigLHS = LHS;
  Expr *OrigRHS = RHS;

  // Replace the LHS and RHS with OpaqueValueExprs; we're going to refer to
  // each of them multiple times below.
  LHS = new (Context)
      OpaqueValueExpr(LHS->getExprLoc(), LHS->getType(), LHS->getValueKind(),
                      LHS->getObjectKind(), LHS);
  RHS = new (Context)
      OpaqueValueExpr(RHS->getExprLoc(), RHS->getType(), RHS->getValueKind(),
                      RHS->getObjectKind(), RHS);

  ExprResult Eq = CreateOverloadedBinOp(OpLoc, BO_EQ, Fns, LHS, RHS, true, true,
                                        DefaultedFn);
  if (Eq.isInvalid())
    return ExprError();

  ExprResult Less = CreateOverloadedBinOp(OpLoc, BO_LT, Fns, LHS, RHS, true,
                                          true, DefaultedFn);
  if (Less.isInvalid())
    return ExprError();

  ExprResult Greater;
  if (Info->isPartial()) {
    Greater = CreateOverloadedBinOp(OpLoc, BO_LT, Fns, RHS, LHS, true, true,
                                    DefaultedFn);
    if (Greater.isInvalid())
      return ExprError();
  }

  // Form the list of comparisons we're going to perform.
  struct Comparison {
    ExprResult Cmp;
    ComparisonCategoryResult Result;
  } Comparisons[4] =
  { {Eq, Info->isStrong() ? ComparisonCategoryResult::Equal
                          : ComparisonCategoryResult::Equivalent},
    {Less, ComparisonCategoryResult::Less},
    {Greater, ComparisonCategoryResult::Greater},
    {ExprResult(), ComparisonCategoryResult::Unordered},
  };

  int I = Info->isPartial() ? 3 : 2;

  // Combine the comparisons with suitable conditional expressions.
  ExprResult Result;
  for (; I >= 0; --I) {
    // Build a reference to the comparison category constant.
    auto *VI = Info->lookupValueInfo(Comparisons[I].Result);
    // FIXME: Missing a constant for a comparison category. Diagnose this?
    if (!VI)
      return ExprResult((Expr*)nullptr);
    ExprResult ThisResult =
        BuildDeclarationNameExpr(CXXScopeSpec(), DeclarationNameInfo(), VI->VD);
    if (ThisResult.isInvalid())
      return ExprError();

    // Build a conditional unless this is the final case.
    if (Result.get()) {
      Result = ActOnConditionalOp(OpLoc, OpLoc, Comparisons[I].Cmp.get(),
                                  ThisResult.get(), Result.get());
      if (Result.isInvalid())
        return ExprError();
    } else {
      Result = ThisResult;
    }
  }

  // Build a PseudoObjectExpr to model the rewriting of an <=> operator, and to
  // bind the OpaqueValueExprs before they're (repeatedly) used.
  Expr *SyntacticForm = BinaryOperator::Create(
      Context, OrigLHS, OrigRHS, BO_Cmp, Result.get()->getType(),
      Result.get()->getValueKind(), Result.get()->getObjectKind(), OpLoc,
      CurFPFeatureOverrides());
  Expr *SemanticForm[] = {LHS, RHS, Result.get()};
  return PseudoObjectExpr::Create(Context, SyntacticForm, SemanticForm, 2);
}

static bool PrepareArgumentsForCallToObjectOfClassType(
    Sema &S, SmallVectorImpl<Expr *> &MethodArgs, CXXMethodDecl *Method,
    MultiExprArg Args, SourceLocation LParenLoc) {

  const auto *Proto = Method->getType()->castAs<FunctionProtoType>();
  unsigned NumParams = Proto->getNumParams();
  unsigned NumArgsSlots =
      MethodArgs.size() + std::max<unsigned>(Args.size(), NumParams);
  // Build the full argument list for the method call (the implicit object
  // parameter is placed at the beginning of the list).
  MethodArgs.reserve(MethodArgs.size() + NumArgsSlots);
  bool IsError = false;
  // Initialize the implicit object parameter.
  // Check the argument types.
  for (unsigned i = 0; i != NumParams; i++) {
    Expr *Arg;
    if (i < Args.size()) {
      Arg = Args[i];
      ExprResult InputInit =
          S.PerformCopyInitialization(InitializedEntity::InitializeParameter(
                                          S.Context, Method->getParamDecl(i)),
                                      SourceLocation(), Arg);
      IsError |= InputInit.isInvalid();
      Arg = InputInit.getAs<Expr>();
    } else {
      ExprResult DefArg =
          S.BuildCXXDefaultArgExpr(LParenLoc, Method, Method->getParamDecl(i));
      if (DefArg.isInvalid()) {
        IsError = true;
        break;
      }
      Arg = DefArg.getAs<Expr>();
    }

    MethodArgs.push_back(Arg);
  }
  return IsError;
}

ExprResult Sema::CreateOverloadedArraySubscriptExpr(SourceLocation LLoc,
                                                    SourceLocation RLoc,
                                                    Expr *Base,
                                                    MultiExprArg ArgExpr) {
  SmallVector<Expr *, 2> Args;
  Args.push_back(Base);
  for (auto *e : ArgExpr) {
    Args.push_back(e);
  }
  DeclarationName OpName =
      Context.DeclarationNames.getCXXOperatorName(OO_Subscript);

  SourceRange Range = ArgExpr.empty()
                          ? SourceRange{}
                          : SourceRange(ArgExpr.front()->getBeginLoc(),
                                        ArgExpr.back()->getEndLoc());

  // If either side is type-dependent, create an appropriate dependent
  // expression.
  if (Expr::hasAnyTypeDependentArguments(Args)) {

    CXXRecordDecl *NamingClass = nullptr; // lookup ignores member operators
    // CHECKME: no 'operator' keyword?
    DeclarationNameInfo OpNameInfo(OpName, LLoc);
    OpNameInfo.setCXXOperatorNameRange(SourceRange(LLoc, RLoc));
    ExprResult Fn = CreateUnresolvedLookupExpr(
        NamingClass, NestedNameSpecifierLoc(), OpNameInfo, UnresolvedSet<0>());
    if (Fn.isInvalid())
      return ExprError();
    // Can't add any actual overloads yet

    return CXXOperatorCallExpr::Create(Context, OO_Subscript, Fn.get(), Args,
                                       Context.DependentTy, VK_PRValue, RLoc,
                                       CurFPFeatureOverrides());
  }

  // Handle placeholders
  UnbridgedCastsSet UnbridgedCasts;
  if (checkArgPlaceholdersForOverload(*this, Args, UnbridgedCasts)) {
    return ExprError();
  }
  // Build an empty overload set.
  OverloadCandidateSet CandidateSet(LLoc, OverloadCandidateSet::CSK_Operator);

  // Subscript can only be overloaded as a member function.

  // Add operator candidates that are member functions.
  AddMemberOperatorCandidates(OO_Subscript, LLoc, Args, CandidateSet);

  // Add builtin operator candidates.
  if (Args.size() == 2)
    AddBuiltinOperatorCandidates(OO_Subscript, LLoc, Args, CandidateSet);

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  // Perform overload resolution.
  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(*this, LLoc, Best)) {
    case OR_Success: {
      // We found a built-in operator or an overloaded operator.
      FunctionDecl *FnDecl = Best->Function;

      if (FnDecl) {
        // We matched an overloaded operator. Build a call to that
        // operator.

        CheckMemberOperatorAccess(LLoc, Args[0], ArgExpr, Best->FoundDecl);

        // Convert the arguments.
        CXXMethodDecl *Method = cast<CXXMethodDecl>(FnDecl);
        SmallVector<Expr *, 2> MethodArgs;

        // Initialize the object parameter.
        if (Method->isExplicitObjectMemberFunction()) {
          ExprResult Res =
              InitializeExplicitObjectArgument(*this, Args[0], Method);
          if (Res.isInvalid())
            return ExprError();
          Args[0] = Res.get();
          ArgExpr = Args;
        } else {
          ExprResult Arg0 = PerformImplicitObjectArgumentInitialization(
              Args[0], /*Qualifier=*/nullptr, Best->FoundDecl, Method);
          if (Arg0.isInvalid())
            return ExprError();

          MethodArgs.push_back(Arg0.get());
        }

        bool IsError = PrepareArgumentsForCallToObjectOfClassType(
            *this, MethodArgs, Method, ArgExpr, LLoc);
        if (IsError)
          return ExprError();

        // Build the actual expression node.
        DeclarationNameInfo OpLocInfo(OpName, LLoc);
        OpLocInfo.setCXXOperatorNameRange(SourceRange(LLoc, RLoc));
        ExprResult FnExpr = CreateFunctionRefExpr(
            *this, FnDecl, Best->FoundDecl, Base, HadMultipleCandidates,
            OpLocInfo.getLoc(), OpLocInfo.getInfo());
        if (FnExpr.isInvalid())
          return ExprError();

        // Determine the result type
        QualType ResultTy = FnDecl->getReturnType();
        ExprValueKind VK = Expr::getValueKindForType(ResultTy);
        ResultTy = ResultTy.getNonLValueExprType(Context);

        CallExpr *TheCall = CXXOperatorCallExpr::Create(
            Context, OO_Subscript, FnExpr.get(), MethodArgs, ResultTy, VK, RLoc,
            CurFPFeatureOverrides());

        if (CheckCallReturnType(FnDecl->getReturnType(), LLoc, TheCall, FnDecl))
          return ExprError();

        if (CheckFunctionCall(Method, TheCall,
                              Method->getType()->castAs<FunctionProtoType>()))
          return ExprError();

        return CheckForImmediateInvocation(MaybeBindToTemporary(TheCall),
                                           FnDecl);
      } else {
        // We matched a built-in operator. Convert the arguments, then
        // break out so that we will build the appropriate built-in
        // operator node.
        ExprResult ArgsRes0 = PerformImplicitConversion(
            Args[0], Best->BuiltinParamTypes[0], Best->Conversions[0],
            AA_Passing, CheckedConversionKind::ForBuiltinOverloadedOp);
        if (ArgsRes0.isInvalid())
          return ExprError();
        Args[0] = ArgsRes0.get();

        ExprResult ArgsRes1 = PerformImplicitConversion(
            Args[1], Best->BuiltinParamTypes[1], Best->Conversions[1],
            AA_Passing, CheckedConversionKind::ForBuiltinOverloadedOp);
        if (ArgsRes1.isInvalid())
          return ExprError();
        Args[1] = ArgsRes1.get();

        break;
      }
    }

    case OR_No_Viable_Function: {
      PartialDiagnostic PD =
          CandidateSet.empty()
              ? (PDiag(diag::err_ovl_no_oper)
                 << Args[0]->getType() << /*subscript*/ 0
                 << Args[0]->getSourceRange() << Range)
              : (PDiag(diag::err_ovl_no_viable_subscript)
                 << Args[0]->getType() << Args[0]->getSourceRange() << Range);
      CandidateSet.NoteCandidates(PartialDiagnosticAt(LLoc, PD), *this,
                                  OCD_AllCandidates, ArgExpr, "[]", LLoc);
      return ExprError();
    }

    case OR_Ambiguous:
      if (Args.size() == 2) {
        CandidateSet.NoteCandidates(
            PartialDiagnosticAt(
                LLoc, PDiag(diag::err_ovl_ambiguous_oper_binary)
                          << "[]" << Args[0]->getType() << Args[1]->getType()
                          << Args[0]->getSourceRange() << Range),
            *this, OCD_AmbiguousCandidates, Args, "[]", LLoc);
      } else {
        CandidateSet.NoteCandidates(
            PartialDiagnosticAt(LLoc,
                                PDiag(diag::err_ovl_ambiguous_subscript_call)
                                    << Args[0]->getType()
                                    << Args[0]->getSourceRange() << Range),
            *this, OCD_AmbiguousCandidates, Args, "[]", LLoc);
      }
      return ExprError();

    case OR_Deleted: {
      StringLiteral *Msg = Best->Function->getDeletedMessage();
      CandidateSet.NoteCandidates(
          PartialDiagnosticAt(LLoc,
                              PDiag(diag::err_ovl_deleted_oper)
                                  << "[]" << (Msg != nullptr)
                                  << (Msg ? Msg->getString() : StringRef())
                                  << Args[0]->getSourceRange() << Range),
          *this, OCD_AllCandidates, Args, "[]", LLoc);
      return ExprError();
    }
    }

  // We matched a built-in operator; build it.
  return CreateBuiltinArraySubscriptExpr(Args[0], LLoc, Args[1], RLoc);
}

ExprResult Sema::BuildCallToMemberFunction(Scope *S, Expr *MemExprE,
                                           SourceLocation LParenLoc,
                                           MultiExprArg Args,
                                           SourceLocation RParenLoc,
                                           Expr *ExecConfig, bool IsExecConfig,
                                           bool AllowRecovery) {
  assert(MemExprE->getType() == Context.BoundMemberTy ||
         MemExprE->getType() == Context.OverloadTy);

  // Dig out the member expression. This holds both the object
  // argument and the member function we're referring to.
  Expr *NakedMemExpr = MemExprE->IgnoreParens();

  // Determine whether this is a call to a pointer-to-member function.
  if (BinaryOperator *op = dyn_cast<BinaryOperator>(NakedMemExpr)) {
    assert(op->getType() == Context.BoundMemberTy);
    assert(op->getOpcode() == BO_PtrMemD || op->getOpcode() == BO_PtrMemI);

    QualType fnType =
      op->getRHS()->getType()->castAs<MemberPointerType>()->getPointeeType();

    const FunctionProtoType *proto = fnType->castAs<FunctionProtoType>();
    QualType resultType = proto->getCallResultType(Context);
    ExprValueKind valueKind = Expr::getValueKindForType(proto->getReturnType());

    // Check that the object type isn't more qualified than the
    // member function we're calling.
    Qualifiers funcQuals = proto->getMethodQuals();

    QualType objectType = op->getLHS()->getType();
    if (op->getOpcode() == BO_PtrMemI)
      objectType = objectType->castAs<PointerType>()->getPointeeType();
    Qualifiers objectQuals = objectType.getQualifiers();

    Qualifiers difference = objectQuals - funcQuals;
    difference.removeObjCGCAttr();
    difference.removeAddressSpace();
    if (difference) {
      std::string qualsString = difference.getAsString();
      Diag(LParenLoc, diag::err_pointer_to_member_call_drops_quals)
        << fnType.getUnqualifiedType()
        << qualsString
        << (qualsString.find(' ') == std::string::npos ? 1 : 2);
    }

    CXXMemberCallExpr *call = CXXMemberCallExpr::Create(
        Context, MemExprE, Args, resultType, valueKind, RParenLoc,
        CurFPFeatureOverrides(), proto->getNumParams());

    if (CheckCallReturnType(proto->getReturnType(), op->getRHS()->getBeginLoc(),
                            call, nullptr))
      return ExprError();

    if (ConvertArgumentsForCall(call, op, nullptr, proto, Args, RParenLoc))
      return ExprError();

    if (CheckOtherCall(call, proto))
      return ExprError();

    return MaybeBindToTemporary(call);
  }

  // We only try to build a recovery expr at this level if we can preserve
  // the return type, otherwise we return ExprError() and let the caller
  // recover.
  auto BuildRecoveryExpr = [&](QualType Type) {
    if (!AllowRecovery)
      return ExprError();
    std::vector<Expr *> SubExprs = {MemExprE};
    llvm::append_range(SubExprs, Args);
    return CreateRecoveryExpr(MemExprE->getBeginLoc(), RParenLoc, SubExprs,
                              Type);
  };
  if (isa<CXXPseudoDestructorExpr>(NakedMemExpr))
    return CallExpr::Create(Context, MemExprE, Args, Context.VoidTy, VK_PRValue,
                            RParenLoc, CurFPFeatureOverrides());

  UnbridgedCastsSet UnbridgedCasts;
  if (checkArgPlaceholdersForOverload(*this, Args, UnbridgedCasts))
    return ExprError();

  MemberExpr *MemExpr;
  CXXMethodDecl *Method = nullptr;
  bool HadMultipleCandidates = false;
  DeclAccessPair FoundDecl = DeclAccessPair::make(nullptr, AS_public);
  NestedNameSpecifier *Qualifier = nullptr;
  if (isa<MemberExpr>(NakedMemExpr)) {
    MemExpr = cast<MemberExpr>(NakedMemExpr);
    Method = cast<CXXMethodDecl>(MemExpr->getMemberDecl());
    FoundDecl = MemExpr->getFoundDecl();
    Qualifier = MemExpr->getQualifier();
    UnbridgedCasts.restore();
  } else {
    UnresolvedMemberExpr *UnresExpr = cast<UnresolvedMemberExpr>(NakedMemExpr);
    Qualifier = UnresExpr->getQualifier();

    QualType ObjectType = UnresExpr->getBaseType();
    Expr::Classification ObjectClassification
      = UnresExpr->isArrow()? Expr::Classification::makeSimpleLValue()
                            : UnresExpr->getBase()->Classify(Context);

    // Add overload candidates
    OverloadCandidateSet CandidateSet(UnresExpr->getMemberLoc(),
                                      OverloadCandidateSet::CSK_Normal);

    // FIXME: avoid copy.
    TemplateArgumentListInfo TemplateArgsBuffer, *TemplateArgs = nullptr;
    if (UnresExpr->hasExplicitTemplateArgs()) {
      UnresExpr->copyTemplateArgumentsInto(TemplateArgsBuffer);
      TemplateArgs = &TemplateArgsBuffer;
    }

    for (UnresolvedMemberExpr::decls_iterator I = UnresExpr->decls_begin(),
           E = UnresExpr->decls_end(); I != E; ++I) {

      QualType ExplicitObjectType = ObjectType;

      NamedDecl *Func = *I;
      CXXRecordDecl *ActingDC = cast<CXXRecordDecl>(Func->getDeclContext());
      if (isa<UsingShadowDecl>(Func))
        Func = cast<UsingShadowDecl>(Func)->getTargetDecl();

      bool HasExplicitParameter = false;
      if (const auto *M = dyn_cast<FunctionDecl>(Func);
          M && M->hasCXXExplicitFunctionObjectParameter())
        HasExplicitParameter = true;
      else if (const auto *M = dyn_cast<FunctionTemplateDecl>(Func);
               M &&
               M->getTemplatedDecl()->hasCXXExplicitFunctionObjectParameter())
        HasExplicitParameter = true;

      if (HasExplicitParameter)
        ExplicitObjectType = GetExplicitObjectType(*this, UnresExpr);

      // Microsoft supports direct constructor calls.
      if (getLangOpts().MicrosoftExt && isa<CXXConstructorDecl>(Func)) {
        AddOverloadCandidate(cast<CXXConstructorDecl>(Func), I.getPair(), Args,
                             CandidateSet,
                             /*SuppressUserConversions*/ false);
      } else if ((Method = dyn_cast<CXXMethodDecl>(Func))) {
        // If explicit template arguments were provided, we can't call a
        // non-template member function.
        if (TemplateArgs)
          continue;

        AddMethodCandidate(Method, I.getPair(), ActingDC, ExplicitObjectType,
                           ObjectClassification, Args, CandidateSet,
                           /*SuppressUserConversions=*/false);
      } else {
        AddMethodTemplateCandidate(cast<FunctionTemplateDecl>(Func),
                                   I.getPair(), ActingDC, TemplateArgs,
                                   ExplicitObjectType, ObjectClassification,
                                   Args, CandidateSet,
                                   /*SuppressUserConversions=*/false);
      }
    }

    HadMultipleCandidates = (CandidateSet.size() > 1);

    DeclarationName DeclName = UnresExpr->getMemberName();

    UnbridgedCasts.restore();

    OverloadCandidateSet::iterator Best;
    bool Succeeded = false;
    switch (CandidateSet.BestViableFunction(*this, UnresExpr->getBeginLoc(),
                                            Best)) {
    case OR_Success:
      Method = cast<CXXMethodDecl>(Best->Function);
      FoundDecl = Best->FoundDecl;
      CheckUnresolvedMemberAccess(UnresExpr, Best->FoundDecl);
      if (DiagnoseUseOfOverloadedDecl(Best->FoundDecl, UnresExpr->getNameLoc()))
        break;
      // If FoundDecl is different from Method (such as if one is a template
      // and the other a specialization), make sure DiagnoseUseOfDecl is
      // called on both.
      // FIXME: This would be more comprehensively addressed by modifying
      // DiagnoseUseOfDecl to accept both the FoundDecl and the decl
      // being used.
      if (Method != FoundDecl.getDecl() &&
          DiagnoseUseOfOverloadedDecl(Method, UnresExpr->getNameLoc()))
        break;
      Succeeded = true;
      break;

    case OR_No_Viable_Function:
      CandidateSet.NoteCandidates(
          PartialDiagnosticAt(
              UnresExpr->getMemberLoc(),
              PDiag(diag::err_ovl_no_viable_member_function_in_call)
                  << DeclName << MemExprE->getSourceRange()),
          *this, OCD_AllCandidates, Args);
      break;
    case OR_Ambiguous:
      CandidateSet.NoteCandidates(
          PartialDiagnosticAt(UnresExpr->getMemberLoc(),
                              PDiag(diag::err_ovl_ambiguous_member_call)
                                  << DeclName << MemExprE->getSourceRange()),
          *this, OCD_AmbiguousCandidates, Args);
      break;
    case OR_Deleted:
      DiagnoseUseOfDeletedFunction(
          UnresExpr->getMemberLoc(), MemExprE->getSourceRange(), DeclName,
          CandidateSet, Best->Function, Args, /*IsMember=*/true);
      break;
    }
    // Overload resolution fails, try to recover.
    if (!Succeeded)
      return BuildRecoveryExpr(chooseRecoveryType(CandidateSet, &Best));

    ExprResult Res =
        FixOverloadedFunctionReference(MemExprE, FoundDecl, Method);
    if (Res.isInvalid())
      return ExprError();
    MemExprE = Res.get();

    // If overload resolution picked a static member
    // build a non-member call based on that function.
    if (Method->isStatic()) {
      return BuildResolvedCallExpr(MemExprE, Method, LParenLoc, Args, RParenLoc,
                                   ExecConfig, IsExecConfig);
    }

    MemExpr = cast<MemberExpr>(MemExprE->IgnoreParens());
  }

  QualType ResultType = Method->getReturnType();
  ExprValueKind VK = Expr::getValueKindForType(ResultType);
  ResultType = ResultType.getNonLValueExprType(Context);

  assert(Method && "Member call to something that isn't a method?");
  const auto *Proto = Method->getType()->castAs<FunctionProtoType>();

  CallExpr *TheCall = nullptr;
  llvm::SmallVector<Expr *, 8> NewArgs;
  if (Method->isExplicitObjectMemberFunction()) {
    if (PrepareExplicitObjectArgument(*this, Method, MemExpr->getBase(), Args,
                                      NewArgs))
      return ExprError();

    // Build the actual expression node.
    ExprResult FnExpr =
        CreateFunctionRefExpr(*this, Method, FoundDecl, MemExpr,
                              HadMultipleCandidates, MemExpr->getExprLoc());
    if (FnExpr.isInvalid())
      return ExprError();

    TheCall =
        CallExpr::Create(Context, FnExpr.get(), Args, ResultType, VK, RParenLoc,
                         CurFPFeatureOverrides(), Proto->getNumParams());
  } else {
    // Convert the object argument (for a non-static member function call).
    // We only need to do this if there was actually an overload; otherwise
    // it was done at lookup.
    ExprResult ObjectArg = PerformImplicitObjectArgumentInitialization(
        MemExpr->getBase(), Qualifier, FoundDecl, Method);
    if (ObjectArg.isInvalid())
      return ExprError();
    MemExpr->setBase(ObjectArg.get());
    TheCall = CXXMemberCallExpr::Create(Context, MemExprE, Args, ResultType, VK,
                                        RParenLoc, CurFPFeatureOverrides(),
                                        Proto->getNumParams());
  }

  // Check for a valid return type.
  if (CheckCallReturnType(Method->getReturnType(), MemExpr->getMemberLoc(),
                          TheCall, Method))
    return BuildRecoveryExpr(ResultType);

  // Convert the rest of the arguments
  if (ConvertArgumentsForCall(TheCall, MemExpr, Method, Proto, Args,
                              RParenLoc))
    return BuildRecoveryExpr(ResultType);

  DiagnoseSentinelCalls(Method, LParenLoc, Args);

  if (CheckFunctionCall(Method, TheCall, Proto))
    return ExprError();

  // In the case the method to call was not selected by the overloading
  // resolution process, we still need to handle the enable_if attribute. Do
  // that here, so it will not hide previous -- and more relevant -- errors.
  if (auto *MemE = dyn_cast<MemberExpr>(NakedMemExpr)) {
    if (const EnableIfAttr *Attr =
            CheckEnableIf(Method, LParenLoc, Args, true)) {
      Diag(MemE->getMemberLoc(),
           diag::err_ovl_no_viable_member_function_in_call)
          << Method << Method->getSourceRange();
      Diag(Method->getLocation(),
           diag::note_ovl_candidate_disabled_by_function_cond_attr)
          << Attr->getCond()->getSourceRange() << Attr->getMessage();
      return ExprError();
    }
  }

  if (isa<CXXConstructorDecl, CXXDestructorDecl>(CurContext) &&
      TheCall->getDirectCallee()->isPureVirtual()) {
    const FunctionDecl *MD = TheCall->getDirectCallee();

    if (isa<CXXThisExpr>(MemExpr->getBase()->IgnoreParenCasts()) &&
        MemExpr->performsVirtualDispatch(getLangOpts())) {
      Diag(MemExpr->getBeginLoc(),
           diag::warn_call_to_pure_virtual_member_function_from_ctor_dtor)
          << MD->getDeclName() << isa<CXXDestructorDecl>(CurContext)
          << MD->getParent();

      Diag(MD->getBeginLoc(), diag::note_previous_decl) << MD->getDeclName();
      if (getLangOpts().AppleKext)
        Diag(MemExpr->getBeginLoc(), diag::note_pure_qualified_call_kext)
            << MD->getParent() << MD->getDeclName();
    }
  }

  if (auto *DD = dyn_cast<CXXDestructorDecl>(TheCall->getDirectCallee())) {
    // a->A::f() doesn't go through the vtable, except in AppleKext mode.
    bool CallCanBeVirtual = !MemExpr->hasQualifier() || getLangOpts().AppleKext;
    CheckVirtualDtorCall(DD, MemExpr->getBeginLoc(), /*IsDelete=*/false,
                         CallCanBeVirtual, /*WarnOnNonAbstractTypes=*/true,
                         MemExpr->getMemberLoc());
  }

  return CheckForImmediateInvocation(MaybeBindToTemporary(TheCall),
                                     TheCall->getDirectCallee());
}

ExprResult
Sema::BuildCallToObjectOfClassType(Scope *S, Expr *Obj,
                                   SourceLocation LParenLoc,
                                   MultiExprArg Args,
                                   SourceLocation RParenLoc) {
  if (checkPlaceholderForOverload(*this, Obj))
    return ExprError();
  ExprResult Object = Obj;

  UnbridgedCastsSet UnbridgedCasts;
  if (checkArgPlaceholdersForOverload(*this, Args, UnbridgedCasts))
    return ExprError();

  assert(Object.get()->getType()->isRecordType() &&
         "Requires object type argument");

  // C++ [over.call.object]p1:
  //  If the primary-expression E in the function call syntax
  //  evaluates to a class object of type "cv T", then the set of
  //  candidate functions includes at least the function call
  //  operators of T. The function call operators of T are obtained by
  //  ordinary lookup of the name operator() in the context of
  //  (E).operator().
  OverloadCandidateSet CandidateSet(LParenLoc,
                                    OverloadCandidateSet::CSK_Operator);
  DeclarationName OpName = Context.DeclarationNames.getCXXOperatorName(OO_Call);

  if (RequireCompleteType(LParenLoc, Object.get()->getType(),
                          diag::err_incomplete_object_call, Object.get()))
    return true;

  const auto *Record = Object.get()->getType()->castAs<RecordType>();
  LookupResult R(*this, OpName, LParenLoc, LookupOrdinaryName);
  LookupQualifiedName(R, Record->getDecl());
  R.suppressAccessDiagnostics();

  for (LookupResult::iterator Oper = R.begin(), OperEnd = R.end();
       Oper != OperEnd; ++Oper) {
    AddMethodCandidate(Oper.getPair(), Object.get()->getType(),
                       Object.get()->Classify(Context), Args, CandidateSet,
                       /*SuppressUserConversion=*/false);
  }

  // When calling a lambda, both the call operator, and
  // the conversion operator to function pointer
  // are considered. But when constraint checking
  // on the call operator fails, it will also fail on the
  // conversion operator as the constraints are always the same.
  // As the user probably does not intend to perform a surrogate call,
  // we filter them out to produce better error diagnostics, ie to avoid
  // showing 2 failed overloads instead of one.
  bool IgnoreSurrogateFunctions = false;
  if (CandidateSet.size() == 1 && Record->getAsCXXRecordDecl()->isLambda()) {
    const OverloadCandidate &Candidate = *CandidateSet.begin();
    if (!Candidate.Viable &&
        Candidate.FailureKind == ovl_fail_constraints_not_satisfied)
      IgnoreSurrogateFunctions = true;
  }

  // C++ [over.call.object]p2:
  //   In addition, for each (non-explicit in C++0x) conversion function
  //   declared in T of the form
  //
  //        operator conversion-type-id () cv-qualifier;
  //
  //   where cv-qualifier is the same cv-qualification as, or a
  //   greater cv-qualification than, cv, and where conversion-type-id
  //   denotes the type "pointer to function of (P1,...,Pn) returning
  //   R", or the type "reference to pointer to function of
  //   (P1,...,Pn) returning R", or the type "reference to function
  //   of (P1,...,Pn) returning R", a surrogate call function [...]
  //   is also considered as a candidate function. Similarly,
  //   surrogate call functions are added to the set of candidate
  //   functions for each conversion function declared in an
  //   accessible base class provided the function is not hidden
  //   within T by another intervening declaration.
  const auto &Conversions =
      cast<CXXRecordDecl>(Record->getDecl())->getVisibleConversionFunctions();
  for (auto I = Conversions.begin(), E = Conversions.end();
       !IgnoreSurrogateFunctions && I != E; ++I) {
    NamedDecl *D = *I;
    CXXRecordDecl *ActingContext = cast<CXXRecordDecl>(D->getDeclContext());
    if (isa<UsingShadowDecl>(D))
      D = cast<UsingShadowDecl>(D)->getTargetDecl();

    // Skip over templated conversion functions; they aren't
    // surrogates.
    if (isa<FunctionTemplateDecl>(D))
      continue;

    CXXConversionDecl *Conv = cast<CXXConversionDecl>(D);
    if (!Conv->isExplicit()) {
      // Strip the reference type (if any) and then the pointer type (if
      // any) to get down to what might be a function type.
      QualType ConvType = Conv->getConversionType().getNonReferenceType();
      if (const PointerType *ConvPtrType = ConvType->getAs<PointerType>())
        ConvType = ConvPtrType->getPointeeType();

      if (const FunctionProtoType *Proto = ConvType->getAs<FunctionProtoType>())
      {
        AddSurrogateCandidate(Conv, I.getPair(), ActingContext, Proto,
                              Object.get(), Args, CandidateSet);
      }
    }
  }

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  // Perform overload resolution.
  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(*this, Object.get()->getBeginLoc(),
                                          Best)) {
  case OR_Success:
    // Overload resolution succeeded; we'll build the appropriate call
    // below.
    break;

  case OR_No_Viable_Function: {
    PartialDiagnostic PD =
        CandidateSet.empty()
            ? (PDiag(diag::err_ovl_no_oper)
               << Object.get()->getType() << /*call*/ 1
               << Object.get()->getSourceRange())
            : (PDiag(diag::err_ovl_no_viable_object_call)
               << Object.get()->getType() << Object.get()->getSourceRange());
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(Object.get()->getBeginLoc(), PD), *this,
        OCD_AllCandidates, Args);
    break;
  }
  case OR_Ambiguous:
    if (!R.isAmbiguous())
      CandidateSet.NoteCandidates(
          PartialDiagnosticAt(Object.get()->getBeginLoc(),
                              PDiag(diag::err_ovl_ambiguous_object_call)
                                  << Object.get()->getType()
                                  << Object.get()->getSourceRange()),
          *this, OCD_AmbiguousCandidates, Args);
    break;

  case OR_Deleted: {
    // FIXME: Is this diagnostic here really necessary? It seems that
    //   1. we don't have any tests for this diagnostic, and
    //   2. we already issue err_deleted_function_use for this later on anyway.
    StringLiteral *Msg = Best->Function->getDeletedMessage();
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(Object.get()->getBeginLoc(),
                            PDiag(diag::err_ovl_deleted_object_call)
                                << Object.get()->getType() << (Msg != nullptr)
                                << (Msg ? Msg->getString() : StringRef())
                                << Object.get()->getSourceRange()),
        *this, OCD_AllCandidates, Args);
    break;
  }
  }

  if (Best == CandidateSet.end())
    return true;

  UnbridgedCasts.restore();

  if (Best->Function == nullptr) {
    // Since there is no function declaration, this is one of the
    // surrogate candidates. Dig out the conversion function.
    CXXConversionDecl *Conv
      = cast<CXXConversionDecl>(
                         Best->Conversions[0].UserDefined.ConversionFunction);

    CheckMemberOperatorAccess(LParenLoc, Object.get(), nullptr,
                              Best->FoundDecl);
    if (DiagnoseUseOfDecl(Best->FoundDecl, LParenLoc))
      return ExprError();
    assert(Conv == Best->FoundDecl.getDecl() &&
             "Found Decl & conversion-to-functionptr should be same, right?!");
    // We selected one of the surrogate functions that converts the
    // object parameter to a function pointer. Perform the conversion
    // on the object argument, then let BuildCallExpr finish the job.

    // Create an implicit member expr to refer to the conversion operator.
    // and then call it.
    ExprResult Call = BuildCXXMemberCallExpr(Object.get(), Best->FoundDecl,
                                             Conv, HadMultipleCandidates);
    if (Call.isInvalid())
      return ExprError();
    // Record usage of conversion in an implicit cast.
    Call = ImplicitCastExpr::Create(
        Context, Call.get()->getType(), CK_UserDefinedConversion, Call.get(),
        nullptr, VK_PRValue, CurFPFeatureOverrides());

    return BuildCallExpr(S, Call.get(), LParenLoc, Args, RParenLoc);
  }

  CheckMemberOperatorAccess(LParenLoc, Object.get(), nullptr, Best->FoundDecl);

  // We found an overloaded operator(). Build a CXXOperatorCallExpr
  // that calls this method, using Object for the implicit object
  // parameter and passing along the remaining arguments.
  CXXMethodDecl *Method = cast<CXXMethodDecl>(Best->Function);

  // An error diagnostic has already been printed when parsing the declaration.
  if (Method->isInvalidDecl())
    return ExprError();

  const auto *Proto = Method->getType()->castAs<FunctionProtoType>();
  unsigned NumParams = Proto->getNumParams();

  DeclarationNameInfo OpLocInfo(
               Context.DeclarationNames.getCXXOperatorName(OO_Call), LParenLoc);
  OpLocInfo.setCXXOperatorNameRange(SourceRange(LParenLoc, RParenLoc));
  ExprResult NewFn = CreateFunctionRefExpr(*this, Method, Best->FoundDecl,
                                           Obj, HadMultipleCandidates,
                                           OpLocInfo.getLoc(),
                                           OpLocInfo.getInfo());
  if (NewFn.isInvalid())
    return true;

  SmallVector<Expr *, 8> MethodArgs;
  MethodArgs.reserve(NumParams + 1);

  bool IsError = false;

  // Initialize the object parameter.
  llvm::SmallVector<Expr *, 8> NewArgs;
  if (Method->isExplicitObjectMemberFunction()) {
    IsError |= PrepareExplicitObjectArgument(*this, Method, Obj, Args, NewArgs);
  } else {
    ExprResult ObjRes = PerformImplicitObjectArgumentInitialization(
        Object.get(), /*Qualifier=*/nullptr, Best->FoundDecl, Method);
    if (ObjRes.isInvalid())
      IsError = true;
    else
      Object = ObjRes;
    MethodArgs.push_back(Object.get());
  }

  IsError |= PrepareArgumentsForCallToObjectOfClassType(
      *this, MethodArgs, Method, Args, LParenLoc);

  // If this is a variadic call, handle args passed through "...".
  if (Proto->isVariadic()) {
    // Promote the arguments (C99 6.5.2.2p7).
    for (unsigned i = NumParams, e = Args.size(); i < e; i++) {
      ExprResult Arg = DefaultVariadicArgumentPromotion(Args[i], VariadicMethod,
                                                        nullptr);
      IsError |= Arg.isInvalid();
      MethodArgs.push_back(Arg.get());
    }
  }

  if (IsError)
    return true;

  DiagnoseSentinelCalls(Method, LParenLoc, Args);

  // Once we've built TheCall, all of the expressions are properly owned.
  QualType ResultTy = Method->getReturnType();
  ExprValueKind VK = Expr::getValueKindForType(ResultTy);
  ResultTy = ResultTy.getNonLValueExprType(Context);

  CallExpr *TheCall = CXXOperatorCallExpr::Create(
      Context, OO_Call, NewFn.get(), MethodArgs, ResultTy, VK, RParenLoc,
      CurFPFeatureOverrides());

  if (CheckCallReturnType(Method->getReturnType(), LParenLoc, TheCall, Method))
    return true;

  if (CheckFunctionCall(Method, TheCall, Proto))
    return true;

  return CheckForImmediateInvocation(MaybeBindToTemporary(TheCall), Method);
}

ExprResult
Sema::BuildOverloadedArrowExpr(Scope *S, Expr *Base, SourceLocation OpLoc,
                               bool *NoArrowOperatorFound) {
  assert(Base->getType()->isRecordType() &&
         "left-hand side must have class type");

  if (checkPlaceholderForOverload(*this, Base))
    return ExprError();

  SourceLocation Loc = Base->getExprLoc();

  // C++ [over.ref]p1:
  //
  //   [...] An expression x->m is interpreted as (x.operator->())->m
  //   for a class object x of type T if T::operator->() exists and if
  //   the operator is selected as the best match function by the
  //   overload resolution mechanism (13.3).
  DeclarationName OpName =
    Context.DeclarationNames.getCXXOperatorName(OO_Arrow);
  OverloadCandidateSet CandidateSet(Loc, OverloadCandidateSet::CSK_Operator);

  if (RequireCompleteType(Loc, Base->getType(),
                          diag::err_typecheck_incomplete_tag, Base))
    return ExprError();

  LookupResult R(*this, OpName, OpLoc, LookupOrdinaryName);
  LookupQualifiedName(R, Base->getType()->castAs<RecordType>()->getDecl());
  R.suppressAccessDiagnostics();

  for (LookupResult::iterator Oper = R.begin(), OperEnd = R.end();
       Oper != OperEnd; ++Oper) {
    AddMethodCandidate(Oper.getPair(), Base->getType(), Base->Classify(Context),
                       std::nullopt, CandidateSet,
                       /*SuppressUserConversion=*/false);
  }

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  // Perform overload resolution.
  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(*this, OpLoc, Best)) {
  case OR_Success:
    // Overload resolution succeeded; we'll build the call below.
    break;

  case OR_No_Viable_Function: {
    auto Cands = CandidateSet.CompleteCandidates(*this, OCD_AllCandidates, Base);
    if (CandidateSet.empty()) {
      QualType BaseType = Base->getType();
      if (NoArrowOperatorFound) {
        // Report this specific error to the caller instead of emitting a
        // diagnostic, as requested.
        *NoArrowOperatorFound = true;
        return ExprError();
      }
      Diag(OpLoc, diag::err_typecheck_member_reference_arrow)
        << BaseType << Base->getSourceRange();
      if (BaseType->isRecordType() && !BaseType->isPointerType()) {
        Diag(OpLoc, diag::note_typecheck_member_reference_suggestion)
          << FixItHint::CreateReplacement(OpLoc, ".");
      }
    } else
      Diag(OpLoc, diag::err_ovl_no_viable_oper)
        << "operator->" << Base->getSourceRange();
    CandidateSet.NoteCandidates(*this, Base, Cands);
    return ExprError();
  }
  case OR_Ambiguous:
    if (!R.isAmbiguous())
      CandidateSet.NoteCandidates(
          PartialDiagnosticAt(OpLoc, PDiag(diag::err_ovl_ambiguous_oper_unary)
                                         << "->" << Base->getType()
                                         << Base->getSourceRange()),
          *this, OCD_AmbiguousCandidates, Base);
    return ExprError();

  case OR_Deleted: {
    StringLiteral *Msg = Best->Function->getDeletedMessage();
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(OpLoc, PDiag(diag::err_ovl_deleted_oper)
                                       << "->" << (Msg != nullptr)
                                       << (Msg ? Msg->getString() : StringRef())
                                       << Base->getSourceRange()),
        *this, OCD_AllCandidates, Base);
    return ExprError();
  }
  }

  CheckMemberOperatorAccess(OpLoc, Base, nullptr, Best->FoundDecl);

  // Convert the object parameter.
  CXXMethodDecl *Method = cast<CXXMethodDecl>(Best->Function);

  if (Method->isExplicitObjectMemberFunction()) {
    ExprResult R = InitializeExplicitObjectArgument(*this, Base, Method);
    if (R.isInvalid())
      return ExprError();
    Base = R.get();
  } else {
    ExprResult BaseResult = PerformImplicitObjectArgumentInitialization(
        Base, /*Qualifier=*/nullptr, Best->FoundDecl, Method);
    if (BaseResult.isInvalid())
      return ExprError();
    Base = BaseResult.get();
  }

  // Build the operator call.
  ExprResult FnExpr = CreateFunctionRefExpr(*this, Method, Best->FoundDecl,
                                            Base, HadMultipleCandidates, OpLoc);
  if (FnExpr.isInvalid())
    return ExprError();

  QualType ResultTy = Method->getReturnType();
  ExprValueKind VK = Expr::getValueKindForType(ResultTy);
  ResultTy = ResultTy.getNonLValueExprType(Context);

  CallExpr *TheCall =
      CXXOperatorCallExpr::Create(Context, OO_Arrow, FnExpr.get(), Base,
                                  ResultTy, VK, OpLoc, CurFPFeatureOverrides());

  if (CheckCallReturnType(Method->getReturnType(), OpLoc, TheCall, Method))
    return ExprError();

  if (CheckFunctionCall(Method, TheCall,
                        Method->getType()->castAs<FunctionProtoType>()))
    return ExprError();

  return CheckForImmediateInvocation(MaybeBindToTemporary(TheCall), Method);
}

ExprResult Sema::BuildLiteralOperatorCall(LookupResult &R,
                                          DeclarationNameInfo &SuffixInfo,
                                          ArrayRef<Expr*> Args,
                                          SourceLocation LitEndLoc,
                                       TemplateArgumentListInfo *TemplateArgs) {
  SourceLocation UDSuffixLoc = SuffixInfo.getCXXLiteralOperatorNameLoc();

  OverloadCandidateSet CandidateSet(UDSuffixLoc,
                                    OverloadCandidateSet::CSK_Normal);
  AddNonMemberOperatorCandidates(R.asUnresolvedSet(), Args, CandidateSet,
                                 TemplateArgs);

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  // Perform overload resolution. This will usually be trivial, but might need
  // to perform substitutions for a literal operator template.
  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(*this, UDSuffixLoc, Best)) {
  case OR_Success:
  case OR_Deleted:
    break;

  case OR_No_Viable_Function:
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(UDSuffixLoc,
                            PDiag(diag::err_ovl_no_viable_function_in_call)
                                << R.getLookupName()),
        *this, OCD_AllCandidates, Args);
    return ExprError();

  case OR_Ambiguous:
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(R.getNameLoc(), PDiag(diag::err_ovl_ambiguous_call)
                                                << R.getLookupName()),
        *this, OCD_AmbiguousCandidates, Args);
    return ExprError();
  }

  FunctionDecl *FD = Best->Function;
  ExprResult Fn = CreateFunctionRefExpr(*this, FD, Best->FoundDecl,
                                        nullptr, HadMultipleCandidates,
                                        SuffixInfo.getLoc(),
                                        SuffixInfo.getInfo());
  if (Fn.isInvalid())
    return true;

  // Check the argument types. This should almost always be a no-op, except
  // that array-to-pointer decay is applied to string literals.
  Expr *ConvArgs[2];
  for (unsigned ArgIdx = 0, N = Args.size(); ArgIdx != N; ++ArgIdx) {
    ExprResult InputInit = PerformCopyInitialization(
      InitializedEntity::InitializeParameter(Context, FD->getParamDecl(ArgIdx)),
      SourceLocation(), Args[ArgIdx]);
    if (InputInit.isInvalid())
      return true;
    ConvArgs[ArgIdx] = InputInit.get();
  }

  QualType ResultTy = FD->getReturnType();
  ExprValueKind VK = Expr::getValueKindForType(ResultTy);
  ResultTy = ResultTy.getNonLValueExprType(Context);

  UserDefinedLiteral *UDL = UserDefinedLiteral::Create(
      Context, Fn.get(), llvm::ArrayRef(ConvArgs, Args.size()), ResultTy, VK,
      LitEndLoc, UDSuffixLoc, CurFPFeatureOverrides());

  if (CheckCallReturnType(FD->getReturnType(), UDSuffixLoc, UDL, FD))
    return ExprError();

  if (CheckFunctionCall(FD, UDL, nullptr))
    return ExprError();

  return CheckForImmediateInvocation(MaybeBindToTemporary(UDL), FD);
}

Sema::ForRangeStatus
Sema::BuildForRangeBeginEndCall(SourceLocation Loc,
                                SourceLocation RangeLoc,
                                const DeclarationNameInfo &NameInfo,
                                LookupResult &MemberLookup,
                                OverloadCandidateSet *CandidateSet,
                                Expr *Range, ExprResult *CallExpr) {
  Scope *S = nullptr;

  CandidateSet->clear(OverloadCandidateSet::CSK_Normal);
  if (!MemberLookup.empty()) {
    ExprResult MemberRef =
        BuildMemberReferenceExpr(Range, Range->getType(), Loc,
                                 /*IsPtr=*/false, CXXScopeSpec(),
                                 /*TemplateKWLoc=*/SourceLocation(),
                                 /*FirstQualifierInScope=*/nullptr,
                                 MemberLookup,
                                 /*TemplateArgs=*/nullptr, S);
    if (MemberRef.isInvalid()) {
      *CallExpr = ExprError();
      return FRS_DiagnosticIssued;
    }
    *CallExpr =
        BuildCallExpr(S, MemberRef.get(), Loc, std::nullopt, Loc, nullptr);
    if (CallExpr->isInvalid()) {
      *CallExpr = ExprError();
      return FRS_DiagnosticIssued;
    }
  } else {
    ExprResult FnR = CreateUnresolvedLookupExpr(/*NamingClass=*/nullptr,
                                                NestedNameSpecifierLoc(),
                                                NameInfo, UnresolvedSet<0>());
    if (FnR.isInvalid())
      return FRS_DiagnosticIssued;
    UnresolvedLookupExpr *Fn = cast<UnresolvedLookupExpr>(FnR.get());

    bool CandidateSetError = buildOverloadedCallSet(S, Fn, Fn, Range, Loc,
                                                    CandidateSet, CallExpr);
    if (CandidateSet->empty() || CandidateSetError) {
      *CallExpr = ExprError();
      return FRS_NoViableFunction;
    }
    OverloadCandidateSet::iterator Best;
    OverloadingResult OverloadResult =
        CandidateSet->BestViableFunction(*this, Fn->getBeginLoc(), Best);

    if (OverloadResult == OR_No_Viable_Function) {
      *CallExpr = ExprError();
      return FRS_NoViableFunction;
    }
    *CallExpr = FinishOverloadedCallExpr(*this, S, Fn, Fn, Loc, Range,
                                         Loc, nullptr, CandidateSet, &Best,
                                         OverloadResult,
                                         /*AllowTypoCorrection=*/false);
    if (CallExpr->isInvalid() || OverloadResult != OR_Success) {
      *CallExpr = ExprError();
      return FRS_DiagnosticIssued;
    }
  }
  return FRS_Success;
}

ExprResult Sema::FixOverloadedFunctionReference(Expr *E, DeclAccessPair Found,
                                                FunctionDecl *Fn) {
  if (ParenExpr *PE = dyn_cast<ParenExpr>(E)) {
    ExprResult SubExpr =
        FixOverloadedFunctionReference(PE->getSubExpr(), Found, Fn);
    if (SubExpr.isInvalid())
      return ExprError();
    if (SubExpr.get() == PE->getSubExpr())
      return PE;

    return new (Context)
        ParenExpr(PE->getLParen(), PE->getRParen(), SubExpr.get());
  }

  if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    ExprResult SubExpr =
        FixOverloadedFunctionReference(ICE->getSubExpr(), Found, Fn);
    if (SubExpr.isInvalid())
      return ExprError();
    assert(Context.hasSameType(ICE->getSubExpr()->getType(),
                               SubExpr.get()->getType()) &&
           "Implicit cast type cannot be determined from overload");
    assert(ICE->path_empty() && "fixing up hierarchy conversion?");
    if (SubExpr.get() == ICE->getSubExpr())
      return ICE;

    return ImplicitCastExpr::Create(Context, ICE->getType(), ICE->getCastKind(),
                                    SubExpr.get(), nullptr, ICE->getValueKind(),
                                    CurFPFeatureOverrides());
  }

  if (auto *GSE = dyn_cast<GenericSelectionExpr>(E)) {
    if (!GSE->isResultDependent()) {
      ExprResult SubExpr =
          FixOverloadedFunctionReference(GSE->getResultExpr(), Found, Fn);
      if (SubExpr.isInvalid())
        return ExprError();
      if (SubExpr.get() == GSE->getResultExpr())
        return GSE;

      // Replace the resulting type information before rebuilding the generic
      // selection expression.
      ArrayRef<Expr *> A = GSE->getAssocExprs();
      SmallVector<Expr *, 4> AssocExprs(A.begin(), A.end());
      unsigned ResultIdx = GSE->getResultIndex();
      AssocExprs[ResultIdx] = SubExpr.get();

      if (GSE->isExprPredicate())
        return GenericSelectionExpr::Create(
            Context, GSE->getGenericLoc(), GSE->getControllingExpr(),
            GSE->getAssocTypeSourceInfos(), AssocExprs, GSE->getDefaultLoc(),
            GSE->getRParenLoc(), GSE->containsUnexpandedParameterPack(),
            ResultIdx);
      return GenericSelectionExpr::Create(
          Context, GSE->getGenericLoc(), GSE->getControllingType(),
          GSE->getAssocTypeSourceInfos(), AssocExprs, GSE->getDefaultLoc(),
          GSE->getRParenLoc(), GSE->containsUnexpandedParameterPack(),
          ResultIdx);
    }
    // Rather than fall through to the unreachable, return the original generic
    // selection expression.
    return GSE;
  }

  if (UnaryOperator *UnOp = dyn_cast<UnaryOperator>(E)) {
    assert(UnOp->getOpcode() == UO_AddrOf &&
           "Can only take the address of an overloaded function");
    if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Fn)) {
      if (!Method->isImplicitObjectMemberFunction()) {
        // Do nothing: the address of static and
        // explicit object member functions is a (non-member) function pointer.
      } else {
        // Fix the subexpression, which really has to be an
        // UnresolvedLookupExpr holding an overloaded member function
        // or template.
        ExprResult SubExpr =
            FixOverloadedFunctionReference(UnOp->getSubExpr(), Found, Fn);
        if (SubExpr.isInvalid())
          return ExprError();
        if (SubExpr.get() == UnOp->getSubExpr())
          return UnOp;

        if (CheckUseOfCXXMethodAsAddressOfOperand(UnOp->getBeginLoc(),
                                                  SubExpr.get(), Method))
          return ExprError();

        assert(isa<DeclRefExpr>(SubExpr.get()) &&
               "fixed to something other than a decl ref");
        assert(cast<DeclRefExpr>(SubExpr.get())->getQualifier() &&
               "fixed to a member ref with no nested name qualifier");

        // We have taken the address of a pointer to member
        // function. Perform the computation here so that we get the
        // appropriate pointer to member type.
        QualType ClassType
          = Context.getTypeDeclType(cast<RecordDecl>(Method->getDeclContext()));
        QualType MemPtrType
          = Context.getMemberPointerType(Fn->getType(), ClassType.getTypePtr());
        // Under the MS ABI, lock down the inheritance model now.
        if (Context.getTargetInfo().getCXXABI().isMicrosoft())
          (void)isCompleteType(UnOp->getOperatorLoc(), MemPtrType);

        return UnaryOperator::Create(Context, SubExpr.get(), UO_AddrOf,
                                     MemPtrType, VK_PRValue, OK_Ordinary,
                                     UnOp->getOperatorLoc(), false,
                                     CurFPFeatureOverrides());
      }
    }
    ExprResult SubExpr =
        FixOverloadedFunctionReference(UnOp->getSubExpr(), Found, Fn);
    if (SubExpr.isInvalid())
      return ExprError();
    if (SubExpr.get() == UnOp->getSubExpr())
      return UnOp;

    return CreateBuiltinUnaryOp(UnOp->getOperatorLoc(), UO_AddrOf,
                                SubExpr.get());
  }

  if (UnresolvedLookupExpr *ULE = dyn_cast<UnresolvedLookupExpr>(E)) {
    // FIXME: avoid copy.
    TemplateArgumentListInfo TemplateArgsBuffer, *TemplateArgs = nullptr;
    if (ULE->hasExplicitTemplateArgs()) {
      ULE->copyTemplateArgumentsInto(TemplateArgsBuffer);
      TemplateArgs = &TemplateArgsBuffer;
    }

    QualType Type = Fn->getType();
    ExprValueKind ValueKind =
        getLangOpts().CPlusPlus && !Fn->hasCXXExplicitFunctionObjectParameter()
            ? VK_LValue
            : VK_PRValue;

    // FIXME: Duplicated from BuildDeclarationNameExpr.
    if (unsigned BID = Fn->getBuiltinID()) {
      if (!Context.BuiltinInfo.isDirectlyAddressable(BID)) {
        Type = Context.BuiltinFnTy;
        ValueKind = VK_PRValue;
      }
    }

    DeclRefExpr *DRE = BuildDeclRefExpr(
        Fn, Type, ValueKind, ULE->getNameInfo(), ULE->getQualifierLoc(),
        Found.getDecl(), ULE->getTemplateKeywordLoc(), TemplateArgs);
    DRE->setHadMultipleCandidates(ULE->getNumDecls() > 1);
    return DRE;
  }

  if (UnresolvedMemberExpr *MemExpr = dyn_cast<UnresolvedMemberExpr>(E)) {
    // FIXME: avoid copy.
    TemplateArgumentListInfo TemplateArgsBuffer, *TemplateArgs = nullptr;
    if (MemExpr->hasExplicitTemplateArgs()) {
      MemExpr->copyTemplateArgumentsInto(TemplateArgsBuffer);
      TemplateArgs = &TemplateArgsBuffer;
    }

    Expr *Base;

    // If we're filling in a static method where we used to have an
    // implicit member access, rewrite to a simple decl ref.
    if (MemExpr->isImplicitAccess()) {
      if (cast<CXXMethodDecl>(Fn)->isStatic()) {
        DeclRefExpr *DRE = BuildDeclRefExpr(
            Fn, Fn->getType(), VK_LValue, MemExpr->getNameInfo(),
            MemExpr->getQualifierLoc(), Found.getDecl(),
            MemExpr->getTemplateKeywordLoc(), TemplateArgs);
        DRE->setHadMultipleCandidates(MemExpr->getNumDecls() > 1);
        return DRE;
      } else {
        SourceLocation Loc = MemExpr->getMemberLoc();
        if (MemExpr->getQualifier())
          Loc = MemExpr->getQualifierLoc().getBeginLoc();
        Base =
            BuildCXXThisExpr(Loc, MemExpr->getBaseType(), /*IsImplicit=*/true);
      }
    } else
      Base = MemExpr->getBase();

    ExprValueKind valueKind;
    QualType type;
    if (cast<CXXMethodDecl>(Fn)->isStatic()) {
      valueKind = VK_LValue;
      type = Fn->getType();
    } else {
      valueKind = VK_PRValue;
      type = Context.BoundMemberTy;
    }

    return BuildMemberExpr(
        Base, MemExpr->isArrow(), MemExpr->getOperatorLoc(),
        MemExpr->getQualifierLoc(), MemExpr->getTemplateKeywordLoc(), Fn, Found,
        /*HadMultipleCandidates=*/true, MemExpr->getMemberNameInfo(),
        type, valueKind, OK_Ordinary, TemplateArgs);
  }

  llvm_unreachable("Invalid reference to overloaded function");
}

ExprResult Sema::FixOverloadedFunctionReference(ExprResult E,
                                                DeclAccessPair Found,
                                                FunctionDecl *Fn) {
  return FixOverloadedFunctionReference(E.get(), Found, Fn);
}

bool clang::shouldEnforceArgLimit(bool PartialOverloading,
                                  FunctionDecl *Function) {
  if (!PartialOverloading || !Function)
    return true;
  if (Function->isVariadic())
    return false;
  if (const auto *Proto =
          dyn_cast<FunctionProtoType>(Function->getFunctionType()))
    if (Proto->isTemplateVariadic())
      return false;
  if (auto *Pattern = Function->getTemplateInstantiationPattern())
    if (const auto *Proto =
            dyn_cast<FunctionProtoType>(Pattern->getFunctionType()))
      if (Proto->isTemplateVariadic())
        return false;
  return true;
}

void Sema::DiagnoseUseOfDeletedFunction(SourceLocation Loc, SourceRange Range,
                                        DeclarationName Name,
                                        OverloadCandidateSet &CandidateSet,
                                        FunctionDecl *Fn, MultiExprArg Args,
                                        bool IsMember) {
  StringLiteral *Msg = Fn->getDeletedMessage();
  CandidateSet.NoteCandidates(
      PartialDiagnosticAt(Loc, PDiag(diag::err_ovl_deleted_call)
                                   << IsMember << Name << (Msg != nullptr)
                                   << (Msg ? Msg->getString() : StringRef())
                                   << Range),
      *this, OCD_AllCandidates, Args);
}
