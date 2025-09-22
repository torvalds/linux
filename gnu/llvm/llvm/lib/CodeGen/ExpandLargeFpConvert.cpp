//===--- ExpandLargeFpConvert.cpp - Expand large fp convert----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//

// This pass expands ‘fptoui .. to’, ‘fptosi .. to’, ‘uitofp .. to’,
// ‘sitofp .. to’ instructions with a bitwidth above a threshold into
// auto-generated functions. This is useful for targets like x86_64 that cannot
// lower fp convertions with more than 128 bits.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ExpandLargeFpConvert.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

static cl::opt<unsigned>
    ExpandFpConvertBits("expand-fp-convert-bits", cl::Hidden,
                     cl::init(llvm::IntegerType::MAX_INT_BITS),
                     cl::desc("fp convert instructions on integers with "
                              "more than <N> bits are expanded."));

/// Generate code to convert a fp number to integer, replacing FPToS(U)I with
/// the generated code. This currently generates code similarly to compiler-rt's
/// implementations.
///
/// An example IR generated from compiler-rt/fixsfdi.c looks like below:
/// define dso_local i64 @foo(float noundef %a) local_unnamed_addr #0 {
/// entry:
///   %0 = bitcast float %a to i32
///   %conv.i = zext i32 %0 to i64
///   %tobool.not = icmp sgt i32 %0, -1
///   %conv = select i1 %tobool.not, i64 1, i64 -1
///   %and = lshr i64 %conv.i, 23
///   %shr = and i64 %and, 255
///   %and2 = and i64 %conv.i, 8388607
///   %or = or i64 %and2, 8388608
///   %cmp = icmp ult i64 %shr, 127
///   br i1 %cmp, label %cleanup, label %if.end
///
/// if.end:                                           ; preds = %entry
///   %sub = add nuw nsw i64 %shr, 4294967169
///   %conv5 = and i64 %sub, 4294967232
///   %cmp6.not = icmp eq i64 %conv5, 0
///   br i1 %cmp6.not, label %if.end12, label %if.then8
///
/// if.then8:                                         ; preds = %if.end
///   %cond11 = select i1 %tobool.not, i64 9223372036854775807, i64 -9223372036854775808
///   br label %cleanup
///
/// if.end12:                                         ; preds = %if.end
///   %cmp13 = icmp ult i64 %shr, 150
///   br i1 %cmp13, label %if.then15, label %if.else
///
/// if.then15:                                        ; preds = %if.end12
///   %sub16 = sub nuw nsw i64 150, %shr
///   %shr17 = lshr i64 %or, %sub16
///   %mul = mul nsw i64 %shr17, %conv
///   br label %cleanup
///
/// if.else:                                          ; preds = %if.end12
///   %sub18 = add nsw i64 %shr, -150
///   %shl = shl i64 %or, %sub18
///   %mul19 = mul nsw i64 %shl, %conv
///   br label %cleanup
///
/// cleanup:                                          ; preds = %entry, %if.else, %if.then15, %if.then8
///   %retval.0 = phi i64 [ %cond11, %if.then8 ], [ %mul, %if.then15 ], [ %mul19, %if.else ], [ 0, %entry ]
///   ret i64 %retval.0
/// }
///
/// Replace fp to integer with generated code.
static void expandFPToI(Instruction *FPToI) {
  IRBuilder<> Builder(FPToI);
  auto *FloatVal = FPToI->getOperand(0);
  IntegerType *IntTy = cast<IntegerType>(FPToI->getType());

  unsigned BitWidth = FPToI->getType()->getIntegerBitWidth();
  unsigned FPMantissaWidth = FloatVal->getType()->getFPMantissaWidth() - 1;

  // FIXME: fp16's range is covered by i32. So `fptoi half` can convert
  // to i32 first following a sext/zext to target integer type.
  Value *A1 = nullptr;
  if (FloatVal->getType()->isHalfTy()) {
    if (FPToI->getOpcode() == Instruction::FPToUI) {
      Value *A0 = Builder.CreateFPToUI(FloatVal, Builder.getIntNTy(32));
      A1 = Builder.CreateZExt(A0, IntTy);
    } else { // FPToSI
      Value *A0 = Builder.CreateFPToSI(FloatVal, Builder.getIntNTy(32));
      A1 = Builder.CreateSExt(A0, IntTy);
    }
    FPToI->replaceAllUsesWith(A1);
    FPToI->dropAllReferences();
    FPToI->eraseFromParent();
    return;
  }

  // fp80 conversion is implemented by fpext to fp128 first then do the
  // conversion.
  FPMantissaWidth = FPMantissaWidth == 63 ? 112 : FPMantissaWidth;
  unsigned FloatWidth =
      PowerOf2Ceil(FloatVal->getType()->getScalarSizeInBits());
  unsigned ExponentWidth = FloatWidth - FPMantissaWidth - 1;
  unsigned ExponentBias = (1 << (ExponentWidth - 1)) - 1;
  Value *ImplicitBit = Builder.CreateShl(
      Builder.getIntN(BitWidth, 1), Builder.getIntN(BitWidth, FPMantissaWidth));
  Value *SignificandMask =
      Builder.CreateSub(ImplicitBit, Builder.getIntN(BitWidth, 1));
  Value *NegOne = Builder.CreateSExt(
      ConstantInt::getSigned(Builder.getInt32Ty(), -1), IntTy);
  Value *NegInf =
      Builder.CreateShl(ConstantInt::getSigned(IntTy, 1),
                        ConstantInt::getSigned(IntTy, BitWidth - 1));

  BasicBlock *Entry = Builder.GetInsertBlock();
  Function *F = Entry->getParent();
  Entry->setName(Twine(Entry->getName(), "fp-to-i-entry"));
  BasicBlock *End =
      Entry->splitBasicBlock(Builder.GetInsertPoint(), "fp-to-i-cleanup");
  BasicBlock *IfEnd =
      BasicBlock::Create(Builder.getContext(), "fp-to-i-if-end", F, End);
  BasicBlock *IfThen5 =
      BasicBlock::Create(Builder.getContext(), "fp-to-i-if-then5", F, End);
  BasicBlock *IfEnd9 =
      BasicBlock::Create(Builder.getContext(), "fp-to-i-if-end9", F, End);
  BasicBlock *IfThen12 =
      BasicBlock::Create(Builder.getContext(), "fp-to-i-if-then12", F, End);
  BasicBlock *IfElse =
      BasicBlock::Create(Builder.getContext(), "fp-to-i-if-else", F, End);

  Entry->getTerminator()->eraseFromParent();

  // entry:
  Builder.SetInsertPoint(Entry);
  Value *FloatVal0 = FloatVal;
  // fp80 conversion is implemented by fpext to fp128 first then do the
  // conversion.
  if (FloatVal->getType()->isX86_FP80Ty())
    FloatVal0 =
        Builder.CreateFPExt(FloatVal, Type::getFP128Ty(Builder.getContext()));
  Value *ARep0 =
      Builder.CreateBitCast(FloatVal0, Builder.getIntNTy(FloatWidth));
  Value *ARep = Builder.CreateZExt(ARep0, FPToI->getType());
  Value *PosOrNeg = Builder.CreateICmpSGT(
      ARep0, ConstantInt::getSigned(Builder.getIntNTy(FloatWidth), -1));
  Value *Sign = Builder.CreateSelect(PosOrNeg, ConstantInt::getSigned(IntTy, 1),
                                     ConstantInt::getSigned(IntTy, -1));
  Value *And =
      Builder.CreateLShr(ARep, Builder.getIntN(BitWidth, FPMantissaWidth));
  Value *And2 = Builder.CreateAnd(
      And, Builder.getIntN(BitWidth, (1 << ExponentWidth) - 1));
  Value *Abs = Builder.CreateAnd(ARep, SignificandMask);
  Value *Or = Builder.CreateOr(Abs, ImplicitBit);
  Value *Cmp =
      Builder.CreateICmpULT(And2, Builder.getIntN(BitWidth, ExponentBias));
  Builder.CreateCondBr(Cmp, End, IfEnd);

  // if.end:
  Builder.SetInsertPoint(IfEnd);
  Value *Add1 = Builder.CreateAdd(
      And2, ConstantInt::getSigned(
                IntTy, -static_cast<int64_t>(ExponentBias + BitWidth)));
  Value *Cmp3 = Builder.CreateICmpULT(
      Add1, ConstantInt::getSigned(IntTy, -static_cast<int64_t>(BitWidth)));
  Builder.CreateCondBr(Cmp3, IfThen5, IfEnd9);

  // if.then5:
  Builder.SetInsertPoint(IfThen5);
  Value *PosInf = Builder.CreateXor(NegOne, NegInf);
  Value *Cond8 = Builder.CreateSelect(PosOrNeg, PosInf, NegInf);
  Builder.CreateBr(End);

  // if.end9:
  Builder.SetInsertPoint(IfEnd9);
  Value *Cmp10 = Builder.CreateICmpULT(
      And2, Builder.getIntN(BitWidth, ExponentBias + FPMantissaWidth));
  Builder.CreateCondBr(Cmp10, IfThen12, IfElse);

  // if.then12:
  Builder.SetInsertPoint(IfThen12);
  Value *Sub13 = Builder.CreateSub(
      Builder.getIntN(BitWidth, ExponentBias + FPMantissaWidth), And2);
  Value *Shr14 = Builder.CreateLShr(Or, Sub13);
  Value *Mul = Builder.CreateMul(Shr14, Sign);
  Builder.CreateBr(End);

  // if.else:
  Builder.SetInsertPoint(IfElse);
  Value *Sub15 = Builder.CreateAdd(
      And2, ConstantInt::getSigned(
                IntTy, -static_cast<int64_t>(ExponentBias + FPMantissaWidth)));
  Value *Shl = Builder.CreateShl(Or, Sub15);
  Value *Mul16 = Builder.CreateMul(Shl, Sign);
  Builder.CreateBr(End);

  // cleanup:
  Builder.SetInsertPoint(End, End->begin());
  PHINode *Retval0 = Builder.CreatePHI(FPToI->getType(), 4);

  Retval0->addIncoming(Cond8, IfThen5);
  Retval0->addIncoming(Mul, IfThen12);
  Retval0->addIncoming(Mul16, IfElse);
  Retval0->addIncoming(Builder.getIntN(BitWidth, 0), Entry);

  FPToI->replaceAllUsesWith(Retval0);
  FPToI->dropAllReferences();
  FPToI->eraseFromParent();
}

/// Generate code to convert a fp number to integer, replacing S(U)IToFP with
/// the generated code. This currently generates code similarly to compiler-rt's
/// implementations. This implementation has an implicit assumption that integer
/// width is larger than fp.
///
/// An example IR generated from compiler-rt/floatdisf.c looks like below:
/// define dso_local float @__floatdisf(i64 noundef %a) local_unnamed_addr #0 {
/// entry:
///   %cmp = icmp eq i64 %a, 0
///   br i1 %cmp, label %return, label %if.end
///
/// if.end:                                           ; preds = %entry
///   %shr = ashr i64 %a, 63
///   %xor = xor i64 %shr, %a
///   %sub = sub nsw i64 %xor, %shr
///   %0 = tail call i64 @llvm.ctlz.i64(i64 %sub, i1 true), !range !5
///   %cast = trunc i64 %0 to i32
///   %sub1 = sub nuw nsw i32 64, %cast
///   %sub2 = xor i32 %cast, 63
///   %cmp3 = icmp ult i32 %cast, 40
///   br i1 %cmp3, label %if.then4, label %if.else
///
/// if.then4:                                         ; preds = %if.end
///   switch i32 %sub1, label %sw.default [
///     i32 25, label %sw.bb
///     i32 26, label %sw.epilog
///   ]
///
/// sw.bb:                                            ; preds = %if.then4
///   %shl = shl i64 %sub, 1
///   br label %sw.epilog
///
/// sw.default:                                       ; preds = %if.then4
///   %sub5 = sub nsw i64 38, %0
///   %sh_prom = and i64 %sub5, 4294967295
///   %shr6 = lshr i64 %sub, %sh_prom
///   %shr9 = lshr i64 274877906943, %0
///   %and = and i64 %shr9, %sub
///   %cmp10 = icmp ne i64 %and, 0
///   %conv11 = zext i1 %cmp10 to i64
///   %or = or i64 %shr6, %conv11
///   br label %sw.epilog
///
/// sw.epilog:                                        ; preds = %sw.default, %if.then4, %sw.bb
///   %a.addr.0 = phi i64 [ %or, %sw.default ], [ %sub, %if.then4 ], [ %shl, %sw.bb ]
///   %1 = lshr i64 %a.addr.0, 2
///   %2 = and i64 %1, 1
///   %or16 = or i64 %2, %a.addr.0
///   %inc = add nsw i64 %or16, 1
///   %3 = and i64 %inc, 67108864
///   %tobool.not = icmp eq i64 %3, 0
///   %spec.select.v = select i1 %tobool.not, i64 2, i64 3
///   %spec.select = ashr i64 %inc, %spec.select.v
///   %spec.select56 = select i1 %tobool.not, i32 %sub2, i32 %sub1
///   br label %if.end26
///
/// if.else:                                          ; preds = %if.end
///   %sub23 = add nuw nsw i64 %0, 4294967256
///   %sh_prom24 = and i64 %sub23, 4294967295
///   %shl25 = shl i64 %sub, %sh_prom24
///   br label %if.end26
///
/// if.end26:                                         ; preds = %sw.epilog, %if.else
///   %a.addr.1 = phi i64 [ %shl25, %if.else ], [ %spec.select, %sw.epilog ]
///   %e.0 = phi i32 [ %sub2, %if.else ], [ %spec.select56, %sw.epilog ]
///   %conv27 = trunc i64 %shr to i32
///   %and28 = and i32 %conv27, -2147483648
///   %add = shl nuw nsw i32 %e.0, 23
///   %shl29 = add nuw nsw i32 %add, 1065353216
///   %conv31 = trunc i64 %a.addr.1 to i32
///   %and32 = and i32 %conv31, 8388607
///   %or30 = or i32 %and32, %and28
///   %or33 = or i32 %or30, %shl29
///   %4 = bitcast i32 %or33 to float
///   br label %return
///
/// return:                                           ; preds = %entry, %if.end26
///   %retval.0 = phi float [ %4, %if.end26 ], [ 0.000000e+00, %entry ]
///   ret float %retval.0
/// }
///
/// Replace integer to fp with generated code.
static void expandIToFP(Instruction *IToFP) {
  IRBuilder<> Builder(IToFP);
  auto *IntVal = IToFP->getOperand(0);
  IntegerType *IntTy = cast<IntegerType>(IntVal->getType());

  unsigned BitWidth = IntVal->getType()->getIntegerBitWidth();
  unsigned FPMantissaWidth = IToFP->getType()->getFPMantissaWidth() - 1;
  // fp80 conversion is implemented by conversion tp fp128 first following
  // a fptrunc to fp80.
  FPMantissaWidth = FPMantissaWidth == 63 ? 112 : FPMantissaWidth;
  // FIXME: As there is no related builtins added in compliler-rt,
  // here currently utilized the fp32 <-> fp16 lib calls to implement.
  FPMantissaWidth = FPMantissaWidth == 10 ? 23 : FPMantissaWidth;
  FPMantissaWidth = FPMantissaWidth == 7 ? 23 : FPMantissaWidth;
  unsigned FloatWidth = PowerOf2Ceil(FPMantissaWidth);
  bool IsSigned = IToFP->getOpcode() == Instruction::SIToFP;

  assert(BitWidth > FloatWidth && "Unexpected conversion. expandIToFP() "
                                  "assumes integer width is larger than fp.");

  Value *Temp1 =
      Builder.CreateShl(Builder.getIntN(BitWidth, 1),
                        Builder.getIntN(BitWidth, FPMantissaWidth + 3));

  BasicBlock *Entry = Builder.GetInsertBlock();
  Function *F = Entry->getParent();
  Entry->setName(Twine(Entry->getName(), "itofp-entry"));
  BasicBlock *End =
      Entry->splitBasicBlock(Builder.GetInsertPoint(), "itofp-return");
  BasicBlock *IfEnd =
      BasicBlock::Create(Builder.getContext(), "itofp-if-end", F, End);
  BasicBlock *IfThen4 =
      BasicBlock::Create(Builder.getContext(), "itofp-if-then4", F, End);
  BasicBlock *SwBB =
      BasicBlock::Create(Builder.getContext(), "itofp-sw-bb", F, End);
  BasicBlock *SwDefault =
      BasicBlock::Create(Builder.getContext(), "itofp-sw-default", F, End);
  BasicBlock *SwEpilog =
      BasicBlock::Create(Builder.getContext(), "itofp-sw-epilog", F, End);
  BasicBlock *IfThen20 =
      BasicBlock::Create(Builder.getContext(), "itofp-if-then20", F, End);
  BasicBlock *IfElse =
      BasicBlock::Create(Builder.getContext(), "itofp-if-else", F, End);
  BasicBlock *IfEnd26 =
      BasicBlock::Create(Builder.getContext(), "itofp-if-end26", F, End);

  Entry->getTerminator()->eraseFromParent();

  Function *CTLZ =
      Intrinsic::getDeclaration(F->getParent(), Intrinsic::ctlz, IntTy);
  ConstantInt *True = Builder.getTrue();

  // entry:
  Builder.SetInsertPoint(Entry);
  Value *Cmp = Builder.CreateICmpEQ(IntVal, ConstantInt::getSigned(IntTy, 0));
  Builder.CreateCondBr(Cmp, End, IfEnd);

  // if.end:
  Builder.SetInsertPoint(IfEnd);
  Value *Shr =
      Builder.CreateAShr(IntVal, Builder.getIntN(BitWidth, BitWidth - 1));
  Value *Xor = Builder.CreateXor(Shr, IntVal);
  Value *Sub = Builder.CreateSub(Xor, Shr);
  Value *Call = Builder.CreateCall(CTLZ, {IsSigned ? Sub : IntVal, True});
  Value *Cast = Builder.CreateTrunc(Call, Builder.getInt32Ty());
  int BitWidthNew = FloatWidth == 128 ? BitWidth : 32;
  Value *Sub1 = Builder.CreateSub(Builder.getIntN(BitWidthNew, BitWidth),
                                  FloatWidth == 128 ? Call : Cast);
  Value *Sub2 = Builder.CreateSub(Builder.getIntN(BitWidthNew, BitWidth - 1),
                                  FloatWidth == 128 ? Call : Cast);
  Value *Cmp3 = Builder.CreateICmpSGT(
      Sub1, Builder.getIntN(BitWidthNew, FPMantissaWidth + 1));
  Builder.CreateCondBr(Cmp3, IfThen4, IfElse);

  // if.then4:
  Builder.SetInsertPoint(IfThen4);
  llvm::SwitchInst *SI = Builder.CreateSwitch(Sub1, SwDefault);
  SI->addCase(Builder.getIntN(BitWidthNew, FPMantissaWidth + 2), SwBB);
  SI->addCase(Builder.getIntN(BitWidthNew, FPMantissaWidth + 3), SwEpilog);

  // sw.bb:
  Builder.SetInsertPoint(SwBB);
  Value *Shl =
      Builder.CreateShl(IsSigned ? Sub : IntVal, Builder.getIntN(BitWidth, 1));
  Builder.CreateBr(SwEpilog);

  // sw.default:
  Builder.SetInsertPoint(SwDefault);
  Value *Sub5 = Builder.CreateSub(
      Builder.getIntN(BitWidthNew, BitWidth - FPMantissaWidth - 3),
      FloatWidth == 128 ? Call : Cast);
  Value *ShProm = Builder.CreateZExt(Sub5, IntTy);
  Value *Shr6 = Builder.CreateLShr(IsSigned ? Sub : IntVal,
                                   FloatWidth == 128 ? Sub5 : ShProm);
  Value *Sub8 =
      Builder.CreateAdd(FloatWidth == 128 ? Call : Cast,
                        Builder.getIntN(BitWidthNew, FPMantissaWidth + 3));
  Value *ShProm9 = Builder.CreateZExt(Sub8, IntTy);
  Value *Shr9 = Builder.CreateLShr(ConstantInt::getSigned(IntTy, -1),
                                   FloatWidth == 128 ? Sub8 : ShProm9);
  Value *And = Builder.CreateAnd(Shr9, IsSigned ? Sub : IntVal);
  Value *Cmp10 = Builder.CreateICmpNE(And, Builder.getIntN(BitWidth, 0));
  Value *Conv11 = Builder.CreateZExt(Cmp10, IntTy);
  Value *Or = Builder.CreateOr(Shr6, Conv11);
  Builder.CreateBr(SwEpilog);

  // sw.epilog:
  Builder.SetInsertPoint(SwEpilog);
  PHINode *AAddr0 = Builder.CreatePHI(IntTy, 3);
  AAddr0->addIncoming(Or, SwDefault);
  AAddr0->addIncoming(IsSigned ? Sub : IntVal, IfThen4);
  AAddr0->addIncoming(Shl, SwBB);
  Value *A0 = Builder.CreateTrunc(AAddr0, Builder.getInt32Ty());
  Value *A1 = Builder.CreateLShr(A0, Builder.getIntN(32, 2));
  Value *A2 = Builder.CreateAnd(A1, Builder.getIntN(32, 1));
  Value *Conv16 = Builder.CreateZExt(A2, IntTy);
  Value *Or17 = Builder.CreateOr(AAddr0, Conv16);
  Value *Inc = Builder.CreateAdd(Or17, Builder.getIntN(BitWidth, 1));
  Value *Shr18 = nullptr;
  if (IsSigned)
    Shr18 = Builder.CreateAShr(Inc, Builder.getIntN(BitWidth, 2));
  else
    Shr18 = Builder.CreateLShr(Inc, Builder.getIntN(BitWidth, 2));
  Value *A3 = Builder.CreateAnd(Inc, Temp1, "a3");
  Value *PosOrNeg = Builder.CreateICmpEQ(A3, Builder.getIntN(BitWidth, 0));
  Value *ExtractT60 = Builder.CreateTrunc(Shr18, Builder.getIntNTy(FloatWidth));
  Value *Extract63 = Builder.CreateLShr(Shr18, Builder.getIntN(BitWidth, 32));
  Value *ExtractT64 = nullptr;
  if (FloatWidth > 80)
    ExtractT64 = Builder.CreateTrunc(Sub2, Builder.getInt64Ty());
  else
    ExtractT64 = Builder.CreateTrunc(Extract63, Builder.getInt32Ty());
  Builder.CreateCondBr(PosOrNeg, IfEnd26, IfThen20);

  // if.then20
  Builder.SetInsertPoint(IfThen20);
  Value *Shr21 = nullptr;
  if (IsSigned)
    Shr21 = Builder.CreateAShr(Inc, Builder.getIntN(BitWidth, 3));
  else
    Shr21 = Builder.CreateLShr(Inc, Builder.getIntN(BitWidth, 3));
  Value *ExtractT = Builder.CreateTrunc(Shr21, Builder.getIntNTy(FloatWidth));
  Value *Extract = Builder.CreateLShr(Shr21, Builder.getIntN(BitWidth, 32));
  Value *ExtractT62 = nullptr;
  if (FloatWidth > 80)
    ExtractT62 = Builder.CreateTrunc(Sub1, Builder.getIntNTy(64));
  else
    ExtractT62 = Builder.CreateTrunc(Extract, Builder.getIntNTy(32));
  Builder.CreateBr(IfEnd26);

  // if.else:
  Builder.SetInsertPoint(IfElse);
  Value *Sub24 = Builder.CreateAdd(
      FloatWidth == 128 ? Call : Cast,
      ConstantInt::getSigned(Builder.getIntNTy(BitWidthNew),
                             -(BitWidth - FPMantissaWidth - 1)));
  Value *ShProm25 = Builder.CreateZExt(Sub24, IntTy);
  Value *Shl26 = Builder.CreateShl(IsSigned ? Sub : IntVal,
                                   FloatWidth == 128 ? Sub24 : ShProm25);
  Value *ExtractT61 = Builder.CreateTrunc(Shl26, Builder.getIntNTy(FloatWidth));
  Value *Extract65 = Builder.CreateLShr(Shl26, Builder.getIntN(BitWidth, 32));
  Value *ExtractT66 = nullptr;
  if (FloatWidth > 80)
    ExtractT66 = Builder.CreateTrunc(Sub2, Builder.getIntNTy(64));
  else
    ExtractT66 = Builder.CreateTrunc(Extract65, Builder.getInt32Ty());
  Builder.CreateBr(IfEnd26);

  // if.end26:
  Builder.SetInsertPoint(IfEnd26);
  PHINode *AAddr1Off0 = Builder.CreatePHI(Builder.getIntNTy(FloatWidth), 3);
  AAddr1Off0->addIncoming(ExtractT, IfThen20);
  AAddr1Off0->addIncoming(ExtractT60, SwEpilog);
  AAddr1Off0->addIncoming(ExtractT61, IfElse);
  PHINode *AAddr1Off32 = nullptr;
  if (FloatWidth > 32) {
    AAddr1Off32 =
        Builder.CreatePHI(Builder.getIntNTy(FloatWidth > 80 ? 64 : 32), 3);
    AAddr1Off32->addIncoming(ExtractT62, IfThen20);
    AAddr1Off32->addIncoming(ExtractT64, SwEpilog);
    AAddr1Off32->addIncoming(ExtractT66, IfElse);
  }
  PHINode *E0 = nullptr;
  if (FloatWidth <= 80) {
    E0 = Builder.CreatePHI(Builder.getIntNTy(BitWidthNew), 3);
    E0->addIncoming(Sub1, IfThen20);
    E0->addIncoming(Sub2, SwEpilog);
    E0->addIncoming(Sub2, IfElse);
  }
  Value *And29 = nullptr;
  if (FloatWidth > 80) {
    Value *Temp2 = Builder.CreateShl(Builder.getIntN(BitWidth, 1),
                                     Builder.getIntN(BitWidth, 63));
    And29 = Builder.CreateAnd(Shr, Temp2, "and29");
  } else {
    Value *Conv28 = Builder.CreateTrunc(Shr, Builder.getIntNTy(32));
    And29 = Builder.CreateAnd(
        Conv28, ConstantInt::getSigned(Builder.getIntNTy(32), 0x80000000));
  }
  unsigned TempMod = FPMantissaWidth % 32;
  Value *And34 = nullptr;
  Value *Shl30 = nullptr;
  if (FloatWidth > 80) {
    TempMod += 32;
    Value *Add = Builder.CreateShl(AAddr1Off32, Builder.getIntN(64, TempMod));
    Shl30 = Builder.CreateAdd(
        Add,
        Builder.getIntN(64, ((1ull << (62ull - TempMod)) - 1ull) << TempMod));
    And34 = Builder.CreateZExt(Shl30, Builder.getIntNTy(128));
  } else {
    Value *Add = Builder.CreateShl(E0, Builder.getIntN(32, TempMod));
    Shl30 = Builder.CreateAdd(
        Add, Builder.getIntN(32, ((1 << (30 - TempMod)) - 1) << TempMod));
    And34 = Builder.CreateAnd(FloatWidth > 32 ? AAddr1Off32 : AAddr1Off0,
                              Builder.getIntN(32, (1 << TempMod) - 1));
  }
  Value *Or35 = nullptr;
  if (FloatWidth > 80) {
    Value *And29Trunc = Builder.CreateTrunc(And29, Builder.getIntNTy(128));
    Value *Or31 = Builder.CreateOr(And29Trunc, And34);
    Value *Or34 = Builder.CreateShl(Or31, Builder.getIntN(128, 64));
    Value *Temp3 = Builder.CreateShl(Builder.getIntN(128, 1),
                                     Builder.getIntN(128, FPMantissaWidth));
    Value *Temp4 = Builder.CreateSub(Temp3, Builder.getIntN(128, 1));
    Value *A6 = Builder.CreateAnd(AAddr1Off0, Temp4);
    Or35 = Builder.CreateOr(Or34, A6);
  } else {
    Value *Or31 = Builder.CreateOr(And34, And29);
    Or35 = Builder.CreateOr(IsSigned ? Or31 : And34, Shl30);
  }
  Value *A4 = nullptr;
  if (IToFP->getType()->isDoubleTy()) {
    Value *ZExt1 = Builder.CreateZExt(Or35, Builder.getIntNTy(FloatWidth));
    Value *Shl1 = Builder.CreateShl(ZExt1, Builder.getIntN(FloatWidth, 32));
    Value *And1 =
        Builder.CreateAnd(AAddr1Off0, Builder.getIntN(FloatWidth, 0xFFFFFFFF));
    Value *Or1 = Builder.CreateOr(Shl1, And1);
    A4 = Builder.CreateBitCast(Or1, IToFP->getType());
  } else if (IToFP->getType()->isX86_FP80Ty()) {
    Value *A40 =
        Builder.CreateBitCast(Or35, Type::getFP128Ty(Builder.getContext()));
    A4 = Builder.CreateFPTrunc(A40, IToFP->getType());
  } else if (IToFP->getType()->isHalfTy() || IToFP->getType()->isBFloatTy()) {
    // Deal with "half" situation. This is a workaround since we don't have
    // floattihf.c currently as referring.
    Value *A40 =
        Builder.CreateBitCast(Or35, Type::getFloatTy(Builder.getContext()));
    A4 = Builder.CreateFPTrunc(A40, IToFP->getType());
  } else // float type
    A4 = Builder.CreateBitCast(Or35, IToFP->getType());
  Builder.CreateBr(End);

  // return:
  Builder.SetInsertPoint(End, End->begin());
  PHINode *Retval0 = Builder.CreatePHI(IToFP->getType(), 2);
  Retval0->addIncoming(A4, IfEnd26);
  Retval0->addIncoming(ConstantFP::getZero(IToFP->getType(), false), Entry);

  IToFP->replaceAllUsesWith(Retval0);
  IToFP->dropAllReferences();
  IToFP->eraseFromParent();
}

static void scalarize(Instruction *I, SmallVectorImpl<Instruction *> &Replace) {
  VectorType *VTy = cast<FixedVectorType>(I->getType());

  IRBuilder<> Builder(I);

  unsigned NumElements = VTy->getElementCount().getFixedValue();
  Value *Result = PoisonValue::get(VTy);
  for (unsigned Idx = 0; Idx < NumElements; ++Idx) {
    Value *Ext = Builder.CreateExtractElement(I->getOperand(0), Idx);
    Value *Cast = Builder.CreateCast(cast<CastInst>(I)->getOpcode(), Ext,
                                     I->getType()->getScalarType());
    Result = Builder.CreateInsertElement(Result, Cast, Idx);
    if (isa<Instruction>(Cast))
      Replace.push_back(cast<Instruction>(Cast));
  }
  I->replaceAllUsesWith(Result);
  I->dropAllReferences();
  I->eraseFromParent();
}

static bool runImpl(Function &F, const TargetLowering &TLI) {
  SmallVector<Instruction *, 4> Replace;
  SmallVector<Instruction *, 4> ReplaceVector;
  bool Modified = false;

  unsigned MaxLegalFpConvertBitWidth =
      TLI.getMaxLargeFPConvertBitWidthSupported();
  if (ExpandFpConvertBits != llvm::IntegerType::MAX_INT_BITS)
    MaxLegalFpConvertBitWidth = ExpandFpConvertBits;

  if (MaxLegalFpConvertBitWidth >= llvm::IntegerType::MAX_INT_BITS)
    return false;

  for (auto &I : instructions(F)) {
    switch (I.getOpcode()) {
    case Instruction::FPToUI:
    case Instruction::FPToSI: {
      // TODO: This pass doesn't handle scalable vectors.
      if (I.getOperand(0)->getType()->isScalableTy())
        continue;

      auto *IntTy = cast<IntegerType>(I.getType()->getScalarType());
      if (IntTy->getIntegerBitWidth() <= MaxLegalFpConvertBitWidth)
        continue;

      if (I.getOperand(0)->getType()->isVectorTy())
        ReplaceVector.push_back(&I);
      else
        Replace.push_back(&I);
      Modified = true;
      break;
    }
    case Instruction::UIToFP:
    case Instruction::SIToFP: {
      // TODO: This pass doesn't handle scalable vectors.
      if (I.getOperand(0)->getType()->isScalableTy())
        continue;

      auto *IntTy =
          cast<IntegerType>(I.getOperand(0)->getType()->getScalarType());
      if (IntTy->getIntegerBitWidth() <= MaxLegalFpConvertBitWidth)
        continue;

      if (I.getOperand(0)->getType()->isVectorTy())
        ReplaceVector.push_back(&I);
      else
        Replace.push_back(&I);
      Modified = true;
      break;
    }
    default:
      break;
    }
  }

  while (!ReplaceVector.empty()) {
    Instruction *I = ReplaceVector.pop_back_val();
    scalarize(I, Replace);
  }

  if (Replace.empty())
    return false;

  while (!Replace.empty()) {
    Instruction *I = Replace.pop_back_val();
    if (I->getOpcode() == Instruction::FPToUI ||
        I->getOpcode() == Instruction::FPToSI) {
      expandFPToI(I);
    } else {
      expandIToFP(I);
    }
  }

  return Modified;
}

namespace {
class ExpandLargeFpConvertLegacyPass : public FunctionPass {
public:
  static char ID;

  ExpandLargeFpConvertLegacyPass() : FunctionPass(ID) {
    initializeExpandLargeFpConvertLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    auto *TM = &getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
    auto *TLI = TM->getSubtargetImpl(F)->getTargetLowering();
    return runImpl(F, *TLI);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
    AU.addPreserved<AAResultsWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }
};
} // namespace

PreservedAnalyses ExpandLargeFpConvertPass::run(Function &F,
                                                FunctionAnalysisManager &FAM) {
  const TargetSubtargetInfo *STI = TM->getSubtargetImpl(F);
  return runImpl(F, *STI->getTargetLowering()) ? PreservedAnalyses::none()
                                               : PreservedAnalyses::all();
}

char ExpandLargeFpConvertLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(ExpandLargeFpConvertLegacyPass, "expand-large-fp-convert",
                      "Expand large fp convert", false, false)
INITIALIZE_PASS_END(ExpandLargeFpConvertLegacyPass, "expand-large-fp-convert",
                    "Expand large fp convert", false, false)

FunctionPass *llvm::createExpandLargeFpConvertPass() {
  return new ExpandLargeFpConvertLegacyPass();
}
