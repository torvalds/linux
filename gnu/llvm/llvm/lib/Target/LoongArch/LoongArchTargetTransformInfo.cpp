//===-- LoongArchTargetTransformInfo.cpp - LoongArch specific TTI ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements a TargetTransformInfo analysis pass specific to the
/// LoongArch target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#include "LoongArchTargetTransformInfo.h"

using namespace llvm;

#define DEBUG_TYPE "loongarchtti"

TypeSize LoongArchTTIImpl::getRegisterBitWidth(
    TargetTransformInfo::RegisterKind K) const {
  TypeSize DefSize = TargetTransformInfoImplBase::getRegisterBitWidth(K);
  switch (K) {
  case TargetTransformInfo::RGK_Scalar:
    return TypeSize::getFixed(ST->is64Bit() ? 64 : 32);
  case TargetTransformInfo::RGK_FixedWidthVector:
    if (ST->hasExtLASX())
      return TypeSize::getFixed(256);
    if (ST->hasExtLSX())
      return TypeSize::getFixed(128);
    [[fallthrough]];
  case TargetTransformInfo::RGK_ScalableVector:
    return DefSize;
  }

  llvm_unreachable("Unsupported register kind");
}

unsigned LoongArchTTIImpl::getNumberOfRegisters(unsigned ClassID) const {
  switch (ClassID) {
  case LoongArchRegisterClass::GPRRC:
    // 30 = 32 GPRs - r0 (zero register) - r21 (non-allocatable)
    return 30;
  case LoongArchRegisterClass::FPRRC:
    return ST->hasBasicF() ? 32 : 0;
  case LoongArchRegisterClass::VRRC:
    return ST->hasExtLSX() ? 32 : 0;
  }
  llvm_unreachable("unknown register class");
}

unsigned LoongArchTTIImpl::getRegisterClassForType(bool Vector,
                                                   Type *Ty) const {
  if (Vector)
    return LoongArchRegisterClass::VRRC;
  if (!Ty)
    return LoongArchRegisterClass::GPRRC;

  Type *ScalarTy = Ty->getScalarType();
  if ((ScalarTy->isFloatTy() && ST->hasBasicF()) ||
      (ScalarTy->isDoubleTy() && ST->hasBasicD())) {
    return LoongArchRegisterClass::FPRRC;
  }

  return LoongArchRegisterClass::GPRRC;
}

unsigned LoongArchTTIImpl::getMaxInterleaveFactor(ElementCount VF) {
  return ST->getMaxInterleaveFactor();
}

const char *LoongArchTTIImpl::getRegisterClassName(unsigned ClassID) const {
  switch (ClassID) {
  case LoongArchRegisterClass::GPRRC:
    return "LoongArch::GPRRC";
  case LoongArchRegisterClass::FPRRC:
    return "LoongArch::FPRRC";
  case LoongArchRegisterClass::VRRC:
    return "LoongArch::VRRC";
  }
  llvm_unreachable("unknown register class");
}

// TODO: Implement more hooks to provide TTI machinery for LoongArch.
