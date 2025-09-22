//===- llvm/FixedPointBuilder.h - Builder for fixed-point ops ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FixedPointBuilder class, which is used as a convenient
// way to lower fixed-point arithmetic operations to LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_FIXEDPOINTBUILDER_H
#define LLVM_IR_FIXEDPOINTBUILDER_H

#include "llvm/ADT/APFixedPoint.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

#include <cmath>

namespace llvm {

template <class IRBuilderTy> class FixedPointBuilder {
  IRBuilderTy &B;

  Value *Convert(Value *Src, const FixedPointSemantics &SrcSema,
                 const FixedPointSemantics &DstSema, bool DstIsInteger) {
    unsigned SrcWidth = SrcSema.getWidth();
    unsigned DstWidth = DstSema.getWidth();
    unsigned SrcScale = SrcSema.getScale();
    unsigned DstScale = DstSema.getScale();
    bool SrcIsSigned = SrcSema.isSigned();
    bool DstIsSigned = DstSema.isSigned();

    Type *DstIntTy = B.getIntNTy(DstWidth);

    Value *Result = Src;
    unsigned ResultWidth = SrcWidth;

    // Downscale.
    if (DstScale < SrcScale) {
      // When converting to integers, we round towards zero. For negative
      // numbers, right shifting rounds towards negative infinity. In this case,
      // we can just round up before shifting.
      if (DstIsInteger && SrcIsSigned) {
        Value *Zero = Constant::getNullValue(Result->getType());
        Value *IsNegative = B.CreateICmpSLT(Result, Zero);
        Value *LowBits = ConstantInt::get(
            B.getContext(), APInt::getLowBitsSet(ResultWidth, SrcScale));
        Value *Rounded = B.CreateAdd(Result, LowBits);
        Result = B.CreateSelect(IsNegative, Rounded, Result);
      }

      Result = SrcIsSigned
                   ? B.CreateAShr(Result, SrcScale - DstScale, "downscale")
                   : B.CreateLShr(Result, SrcScale - DstScale, "downscale");
    }

    if (!DstSema.isSaturated()) {
      // Resize.
      Result = B.CreateIntCast(Result, DstIntTy, SrcIsSigned, "resize");

      // Upscale.
      if (DstScale > SrcScale)
        Result = B.CreateShl(Result, DstScale - SrcScale, "upscale");
    } else {
      // Adjust the number of fractional bits.
      if (DstScale > SrcScale) {
        // Compare to DstWidth to prevent resizing twice.
        ResultWidth = std::max(SrcWidth + DstScale - SrcScale, DstWidth);
        Type *UpscaledTy = B.getIntNTy(ResultWidth);
        Result = B.CreateIntCast(Result, UpscaledTy, SrcIsSigned, "resize");
        Result = B.CreateShl(Result, DstScale - SrcScale, "upscale");
      }

      // Handle saturation.
      bool LessIntBits = DstSema.getIntegralBits() < SrcSema.getIntegralBits();
      if (LessIntBits) {
        Value *Max = ConstantInt::get(
            B.getContext(),
            APFixedPoint::getMax(DstSema).getValue().extOrTrunc(ResultWidth));
        Value *TooHigh = SrcIsSigned ? B.CreateICmpSGT(Result, Max)
                                     : B.CreateICmpUGT(Result, Max);
        Result = B.CreateSelect(TooHigh, Max, Result, "satmax");
      }
      // Cannot overflow min to dest type if src is unsigned since all fixed
      // point types can cover the unsigned min of 0.
      if (SrcIsSigned && (LessIntBits || !DstIsSigned)) {
        Value *Min = ConstantInt::get(
            B.getContext(),
            APFixedPoint::getMin(DstSema).getValue().extOrTrunc(ResultWidth));
        Value *TooLow = B.CreateICmpSLT(Result, Min);
        Result = B.CreateSelect(TooLow, Min, Result, "satmin");
      }

      // Resize the integer part to get the final destination size.
      if (ResultWidth != DstWidth)
        Result = B.CreateIntCast(Result, DstIntTy, SrcIsSigned, "resize");
    }
    return Result;
  }

  /// Get the common semantic for two semantics, with the added imposition that
  /// saturated padded types retain the padding bit.
  FixedPointSemantics
  getCommonBinopSemantic(const FixedPointSemantics &LHSSema,
                         const FixedPointSemantics &RHSSema) {
    auto C = LHSSema.getCommonSemantics(RHSSema);
    bool BothPadded =
        LHSSema.hasUnsignedPadding() && RHSSema.hasUnsignedPadding();
    return FixedPointSemantics(
        C.getWidth() + (unsigned)(BothPadded && C.isSaturated()), C.getScale(),
        C.isSigned(), C.isSaturated(), BothPadded);
  }

  /// Given a floating point type and a fixed-point semantic, return a floating
  /// point type which can accommodate the fixed-point semantic. This is either
  /// \p Ty, or a floating point type with a larger exponent than Ty.
  Type *getAccommodatingFloatType(Type *Ty, const FixedPointSemantics &Sema) {
    const fltSemantics *FloatSema = &Ty->getFltSemantics();
    while (!Sema.fitsInFloatSemantics(*FloatSema))
      FloatSema = APFixedPoint::promoteFloatSemantics(FloatSema);
    return Type::getFloatingPointTy(Ty->getContext(), *FloatSema);
  }

public:
  FixedPointBuilder(IRBuilderTy &Builder) : B(Builder) {}

  /// Convert an integer value representing a fixed-point number from one
  /// fixed-point semantic to another fixed-point semantic.
  /// \p Src     - The source value
  /// \p SrcSema - The fixed-point semantic of the source value
  /// \p DstSema - The resulting fixed-point semantic
  Value *CreateFixedToFixed(Value *Src, const FixedPointSemantics &SrcSema,
                            const FixedPointSemantics &DstSema) {
    return Convert(Src, SrcSema, DstSema, false);
  }

  /// Convert an integer value representing a fixed-point number to an integer
  /// with the given bit width and signedness.
  /// \p Src         - The source value
  /// \p SrcSema     - The fixed-point semantic of the source value
  /// \p DstWidth    - The bit width of the result value
  /// \p DstIsSigned - The signedness of the result value
  Value *CreateFixedToInteger(Value *Src, const FixedPointSemantics &SrcSema,
                              unsigned DstWidth, bool DstIsSigned) {
    return Convert(
        Src, SrcSema,
        FixedPointSemantics::GetIntegerSemantics(DstWidth, DstIsSigned), true);
  }

  /// Convert an integer value with the given signedness to an integer value
  /// representing the given fixed-point semantic.
  /// \p Src         - The source value
  /// \p SrcIsSigned - The signedness of the source value
  /// \p DstSema     - The resulting fixed-point semantic
  Value *CreateIntegerToFixed(Value *Src, unsigned SrcIsSigned,
                              const FixedPointSemantics &DstSema) {
    return Convert(Src,
                   FixedPointSemantics::GetIntegerSemantics(
                       Src->getType()->getScalarSizeInBits(), SrcIsSigned),
                   DstSema, false);
  }

  Value *CreateFixedToFloating(Value *Src, const FixedPointSemantics &SrcSema,
                               Type *DstTy) {
    Value *Result;
    Type *OpTy = getAccommodatingFloatType(DstTy, SrcSema);
    // Convert the raw fixed-point value directly to floating point. If the
    // value is too large to fit, it will be rounded, not truncated.
    Result = SrcSema.isSigned() ? B.CreateSIToFP(Src, OpTy)
                                : B.CreateUIToFP(Src, OpTy);
    // Rescale the integral-in-floating point by the scaling factor. This is
    // lossless, except for overflow to infinity which is unlikely.
    Result = B.CreateFMul(Result,
        ConstantFP::get(OpTy, std::pow(2, -(int)SrcSema.getScale())));
    if (OpTy != DstTy)
      Result = B.CreateFPTrunc(Result, DstTy);
    return Result;
  }

  Value *CreateFloatingToFixed(Value *Src, const FixedPointSemantics &DstSema) {
    bool UseSigned = DstSema.isSigned() || DstSema.hasUnsignedPadding();
    Value *Result = Src;
    Type *OpTy = getAccommodatingFloatType(Src->getType(), DstSema);
    if (OpTy != Src->getType())
      Result = B.CreateFPExt(Result, OpTy);
    // Rescale the floating point value so that its significant bits (for the
    // purposes of the conversion) are in the integral range.
    Result = B.CreateFMul(Result,
        ConstantFP::get(OpTy, std::pow(2, DstSema.getScale())));

    Type *ResultTy = B.getIntNTy(DstSema.getWidth());
    if (DstSema.isSaturated()) {
      Intrinsic::ID IID =
          UseSigned ? Intrinsic::fptosi_sat : Intrinsic::fptoui_sat;
      Result = B.CreateIntrinsic(IID, {ResultTy, OpTy}, {Result});
    } else {
      Result = UseSigned ? B.CreateFPToSI(Result, ResultTy)
                         : B.CreateFPToUI(Result, ResultTy);
    }

    // When saturating unsigned-with-padding using signed operations, we may
    // get negative values. Emit an extra clamp to zero.
    if (DstSema.isSaturated() && DstSema.hasUnsignedPadding()) {
      Constant *Zero = Constant::getNullValue(Result->getType());
      Result =
          B.CreateSelect(B.CreateICmpSLT(Result, Zero), Zero, Result, "satmin");
    }

    return Result;
  }

  /// Add two fixed-point values and return the result in their common semantic.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateAdd(Value *LHS, const FixedPointSemantics &LHSSema,
                   Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);
    bool UseSigned = CommonSema.isSigned() || CommonSema.hasUnsignedPadding();

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    Value *Result;
    if (CommonSema.isSaturated()) {
      Intrinsic::ID IID = UseSigned ? Intrinsic::sadd_sat : Intrinsic::uadd_sat;
      Result = B.CreateBinaryIntrinsic(IID, WideLHS, WideRHS);
    } else {
      Result = B.CreateAdd(WideLHS, WideRHS);
    }

    return CreateFixedToFixed(Result, CommonSema,
                              LHSSema.getCommonSemantics(RHSSema));
  }

  /// Subtract two fixed-point values and return the result in their common
  /// semantic.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateSub(Value *LHS, const FixedPointSemantics &LHSSema,
                   Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);
    bool UseSigned = CommonSema.isSigned() || CommonSema.hasUnsignedPadding();

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    Value *Result;
    if (CommonSema.isSaturated()) {
      Intrinsic::ID IID = UseSigned ? Intrinsic::ssub_sat : Intrinsic::usub_sat;
      Result = B.CreateBinaryIntrinsic(IID, WideLHS, WideRHS);
    } else {
      Result = B.CreateSub(WideLHS, WideRHS);
    }

    // Subtraction can end up below 0 for padded unsigned operations, so emit
    // an extra clamp in that case.
    if (CommonSema.isSaturated() && CommonSema.hasUnsignedPadding()) {
      Constant *Zero = Constant::getNullValue(Result->getType());
      Result =
          B.CreateSelect(B.CreateICmpSLT(Result, Zero), Zero, Result, "satmin");
    }

    return CreateFixedToFixed(Result, CommonSema,
                              LHSSema.getCommonSemantics(RHSSema));
  }

  /// Multiply two fixed-point values and return the result in their common
  /// semantic.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateMul(Value *LHS, const FixedPointSemantics &LHSSema,
                   Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);
    bool UseSigned = CommonSema.isSigned() || CommonSema.hasUnsignedPadding();

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    Intrinsic::ID IID;
    if (CommonSema.isSaturated()) {
      IID = UseSigned ? Intrinsic::smul_fix_sat : Intrinsic::umul_fix_sat;
    } else {
      IID = UseSigned ? Intrinsic::smul_fix : Intrinsic::umul_fix;
    }
    Value *Result = B.CreateIntrinsic(
        IID, {WideLHS->getType()},
        {WideLHS, WideRHS, B.getInt32(CommonSema.getScale())});

    return CreateFixedToFixed(Result, CommonSema,
                              LHSSema.getCommonSemantics(RHSSema));
  }

  /// Divide two fixed-point values and return the result in their common
  /// semantic.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateDiv(Value *LHS, const FixedPointSemantics &LHSSema,
                   Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);
    bool UseSigned = CommonSema.isSigned() || CommonSema.hasUnsignedPadding();

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    Intrinsic::ID IID;
    if (CommonSema.isSaturated()) {
      IID = UseSigned ? Intrinsic::sdiv_fix_sat : Intrinsic::udiv_fix_sat;
    } else {
      IID = UseSigned ? Intrinsic::sdiv_fix : Intrinsic::udiv_fix;
    }
    Value *Result = B.CreateIntrinsic(
        IID, {WideLHS->getType()},
        {WideLHS, WideRHS, B.getInt32(CommonSema.getScale())});

    return CreateFixedToFixed(Result, CommonSema,
                              LHSSema.getCommonSemantics(RHSSema));
  }

  /// Left shift a fixed-point value by an unsigned integer value. The integer
  /// value can be any bit width.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  Value *CreateShl(Value *LHS, const FixedPointSemantics &LHSSema, Value *RHS) {
    bool UseSigned = LHSSema.isSigned() || LHSSema.hasUnsignedPadding();

    RHS = B.CreateIntCast(RHS, LHS->getType(), /*IsSigned=*/false);

    Value *Result;
    if (LHSSema.isSaturated()) {
      Intrinsic::ID IID = UseSigned ? Intrinsic::sshl_sat : Intrinsic::ushl_sat;
      Result = B.CreateBinaryIntrinsic(IID, LHS, RHS);
    } else {
      Result = B.CreateShl(LHS, RHS);
    }

    return Result;
  }

  /// Right shift a fixed-point value by an unsigned integer value. The integer
  /// value can be any bit width.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  Value *CreateShr(Value *LHS, const FixedPointSemantics &LHSSema, Value *RHS) {
    RHS = B.CreateIntCast(RHS, LHS->getType(), false);

    return LHSSema.isSigned() ? B.CreateAShr(LHS, RHS) : B.CreateLShr(LHS, RHS);
  }

  /// Compare two fixed-point values for equality.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateEQ(Value *LHS, const FixedPointSemantics &LHSSema,
                  Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    return B.CreateICmpEQ(WideLHS, WideRHS);
  }

  /// Compare two fixed-point values for inequality.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateNE(Value *LHS, const FixedPointSemantics &LHSSema,
                  Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    return B.CreateICmpNE(WideLHS, WideRHS);
  }

  /// Compare two fixed-point values as LHS < RHS.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateLT(Value *LHS, const FixedPointSemantics &LHSSema,
                  Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    return CommonSema.isSigned() ? B.CreateICmpSLT(WideLHS, WideRHS)
                                 : B.CreateICmpULT(WideLHS, WideRHS);
  }

  /// Compare two fixed-point values as LHS <= RHS.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateLE(Value *LHS, const FixedPointSemantics &LHSSema,
                  Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    return CommonSema.isSigned() ? B.CreateICmpSLE(WideLHS, WideRHS)
                                 : B.CreateICmpULE(WideLHS, WideRHS);
  }

  /// Compare two fixed-point values as LHS > RHS.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateGT(Value *LHS, const FixedPointSemantics &LHSSema,
                  Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    return CommonSema.isSigned() ? B.CreateICmpSGT(WideLHS, WideRHS)
                                 : B.CreateICmpUGT(WideLHS, WideRHS);
  }

  /// Compare two fixed-point values as LHS >= RHS.
  /// \p LHS     - The left hand side
  /// \p LHSSema - The semantic of the left hand side
  /// \p RHS     - The right hand side
  /// \p RHSSema - The semantic of the right hand side
  Value *CreateGE(Value *LHS, const FixedPointSemantics &LHSSema,
                  Value *RHS, const FixedPointSemantics &RHSSema) {
    auto CommonSema = getCommonBinopSemantic(LHSSema, RHSSema);

    Value *WideLHS = CreateFixedToFixed(LHS, LHSSema, CommonSema);
    Value *WideRHS = CreateFixedToFixed(RHS, RHSSema, CommonSema);

    return CommonSema.isSigned() ? B.CreateICmpSGE(WideLHS, WideRHS)
                                 : B.CreateICmpUGE(WideLHS, WideRHS);
  }
};

} // end namespace llvm

#endif // LLVM_IR_FIXEDPOINTBUILDER_H
