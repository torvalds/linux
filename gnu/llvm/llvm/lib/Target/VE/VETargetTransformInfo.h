//===- VETargetTransformInfo.h - VE specific TTI ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific to the
/// VE target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VE_VETARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_VE_VETARGETTRANSFORMINFO_H

#include "VE.h"
#include "VETargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

static llvm::Type *getVectorElementType(llvm::Type *Ty) {
  return llvm::cast<llvm::FixedVectorType>(Ty)->getElementType();
}

static llvm::Type *getLaneType(llvm::Type *Ty) {
  using namespace llvm;
  if (!isa<VectorType>(Ty))
    return Ty;
  return getVectorElementType(Ty);
}

static bool isVectorLaneType(llvm::Type &ElemTy) {
  // check element sizes for vregs
  if (ElemTy.isIntegerTy()) {
    unsigned ScaBits = ElemTy.getScalarSizeInBits();
    return ScaBits == 1 || ScaBits == 32 || ScaBits == 64;
  }
  if (ElemTy.isPointerTy()) {
    return true;
  }
  if (ElemTy.isFloatTy() || ElemTy.isDoubleTy()) {
    return true;
  }
  return false;
}

namespace llvm {

class VETTIImpl : public BasicTTIImplBase<VETTIImpl> {
  using BaseT = BasicTTIImplBase<VETTIImpl>;
  friend BaseT;

  const VESubtarget *ST;
  const VETargetLowering *TLI;

  const VESubtarget *getST() const { return ST; }
  const VETargetLowering *getTLI() const { return TLI; }

  bool enableVPU() const { return getST()->enableVPU(); }

  static bool isSupportedReduction(Intrinsic::ID ReductionID) {
#define VEC_VP_CASE(SUFFIX)                                                    \
  case Intrinsic::vp_reduce_##SUFFIX:                                          \
  case Intrinsic::vector_reduce_##SUFFIX:

    switch (ReductionID) {
      VEC_VP_CASE(add)
      VEC_VP_CASE(and)
      VEC_VP_CASE(or)
      VEC_VP_CASE(xor)
      VEC_VP_CASE(smax)
      return true;

    default:
      return false;
    }
#undef VEC_VP_CASE
  }

public:
  explicit VETTIImpl(const VETargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  unsigned getNumberOfRegisters(unsigned ClassID) const {
    bool VectorRegs = (ClassID == 1);
    if (VectorRegs) {
      // TODO report vregs once vector isel is stable.
      return 0;
    }

    return 64;
  }

  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
    switch (K) {
    case TargetTransformInfo::RGK_Scalar:
      return TypeSize::getFixed(64);
    case TargetTransformInfo::RGK_FixedWidthVector:
      // TODO report vregs once vector isel is stable.
      return TypeSize::getFixed(0);
    case TargetTransformInfo::RGK_ScalableVector:
      return TypeSize::getScalable(0);
    }

    llvm_unreachable("Unsupported register kind");
  }

  /// \returns How the target needs this vector-predicated operation to be
  /// transformed.
  TargetTransformInfo::VPLegalization
  getVPLegalizationStrategy(const VPIntrinsic &PI) const {
    using VPLegalization = TargetTransformInfo::VPLegalization;
    return VPLegalization(VPLegalization::Legal, VPLegalization::Legal);
  }

  unsigned getMinVectorRegisterBitWidth() const {
    // TODO report vregs once vector isel is stable.
    return 0;
  }

  bool shouldBuildRelLookupTables() const {
    // NEC nld doesn't support relative lookup tables.  It shows following
    // errors.  So, we disable it at the moment.
    //   /opt/nec/ve/bin/nld: src/CMakeFiles/cxxabi_shared.dir/cxa_demangle.cpp
    //   .o(.rodata+0x17b4): reloc against `.L.str.376': error 2
    //   /opt/nec/ve/bin/nld: final link failed: Nonrepresentable section on
    //   output
    return false;
  }

  // Load & Store {
  bool isLegalMaskedLoad(Type *DataType, MaybeAlign Alignment) {
    return isVectorLaneType(*getLaneType(DataType));
  }
  bool isLegalMaskedStore(Type *DataType, MaybeAlign Alignment) {
    return isVectorLaneType(*getLaneType(DataType));
  }
  bool isLegalMaskedGather(Type *DataType, MaybeAlign Alignment) {
    return isVectorLaneType(*getLaneType(DataType));
  };
  bool isLegalMaskedScatter(Type *DataType, MaybeAlign Alignment) {
    return isVectorLaneType(*getLaneType(DataType));
  }
  // } Load & Store

  bool shouldExpandReduction(const IntrinsicInst *II) const {
    if (!enableVPU())
      return true;
    return !isSupportedReduction(II->getIntrinsicID());
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_VE_VETARGETTRANSFORMINFO_H
