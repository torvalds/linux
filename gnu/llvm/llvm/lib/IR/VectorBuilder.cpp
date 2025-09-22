//===- VectorBuilder.cpp - Builder for VP Intrinsics ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the VectorBuilder class, which is used as a convenient
// way to create VP intrinsics as if they were LLVM instructions with a
// consistent and simplified interface.
//
//===----------------------------------------------------------------------===//

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/FPEnv.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/VectorBuilder.h>

namespace llvm {

void VectorBuilder::handleError(const char *ErrorMsg) const {
  if (ErrorHandling == Behavior::SilentlyReturnNone)
    return;
  report_fatal_error(ErrorMsg);
}

Module &VectorBuilder::getModule() const {
  return *Builder.GetInsertBlock()->getModule();
}

Value *VectorBuilder::getAllTrueMask() {
  return Builder.getAllOnesMask(StaticVectorLength);
}

Value &VectorBuilder::requestMask() {
  if (Mask)
    return *Mask;

  return *getAllTrueMask();
}

Value &VectorBuilder::requestEVL() {
  if (ExplicitVectorLength)
    return *ExplicitVectorLength;

  assert(!StaticVectorLength.isScalable() && "TODO vscale lowering");
  auto *IntTy = Builder.getInt32Ty();
  return *ConstantInt::get(IntTy, StaticVectorLength.getFixedValue());
}

Value *VectorBuilder::createVectorInstruction(unsigned Opcode, Type *ReturnTy,
                                              ArrayRef<Value *> InstOpArray,
                                              const Twine &Name) {
  auto VPID = VPIntrinsic::getForOpcode(Opcode);
  if (VPID == Intrinsic::not_intrinsic)
    return returnWithError<Value *>("No VPIntrinsic for this opcode");
  return createVectorInstructionImpl(VPID, ReturnTy, InstOpArray, Name);
}

Value *VectorBuilder::createSimpleTargetReduction(Intrinsic::ID RdxID,
                                                  Type *ValTy,
                                                  ArrayRef<Value *> InstOpArray,
                                                  const Twine &Name) {
  auto VPID = VPIntrinsic::getForIntrinsic(RdxID);
  assert(VPReductionIntrinsic::isVPReduction(VPID) &&
         "No VPIntrinsic for this reduction");
  return createVectorInstructionImpl(VPID, ValTy, InstOpArray, Name);
}

Value *VectorBuilder::createVectorInstructionImpl(Intrinsic::ID VPID,
                                                  Type *ReturnTy,
                                                  ArrayRef<Value *> InstOpArray,
                                                  const Twine &Name) {
  auto MaskPosOpt = VPIntrinsic::getMaskParamPos(VPID);
  auto VLenPosOpt = VPIntrinsic::getVectorLengthParamPos(VPID);
  size_t NumInstParams = InstOpArray.size();
  size_t NumVPParams =
      NumInstParams + MaskPosOpt.has_value() + VLenPosOpt.has_value();

  SmallVector<Value *, 6> IntrinParams;

  // Whether the mask and vlen parameter are at the end of the parameter list.
  bool TrailingMaskAndVLen =
      std::min<size_t>(MaskPosOpt.value_or(NumInstParams),
                       VLenPosOpt.value_or(NumInstParams)) >= NumInstParams;

  if (TrailingMaskAndVLen) {
    // Fast path for trailing mask, vector length.
    IntrinParams.append(InstOpArray.begin(), InstOpArray.end());
    IntrinParams.resize(NumVPParams);
  } else {
    IntrinParams.resize(NumVPParams);
    // Insert mask and evl operands in between the instruction operands.
    for (size_t VPParamIdx = 0, ParamIdx = 0; VPParamIdx < NumVPParams;
         ++VPParamIdx) {
      if ((MaskPosOpt && MaskPosOpt.value_or(NumVPParams) == VPParamIdx) ||
          (VLenPosOpt && VLenPosOpt.value_or(NumVPParams) == VPParamIdx))
        continue;
      assert(ParamIdx < NumInstParams);
      IntrinParams[VPParamIdx] = InstOpArray[ParamIdx++];
    }
  }

  if (MaskPosOpt)
    IntrinParams[*MaskPosOpt] = &requestMask();
  if (VLenPosOpt)
    IntrinParams[*VLenPosOpt] = &requestEVL();

  auto *VPDecl = VPIntrinsic::getDeclarationForParams(&getModule(), VPID,
                                                      ReturnTy, IntrinParams);
  return Builder.CreateCall(VPDecl, IntrinParams, Name);
}

} // namespace llvm
