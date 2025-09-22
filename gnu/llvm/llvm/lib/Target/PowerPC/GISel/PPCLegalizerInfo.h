//===- PPCLegalizerInfo.h ----------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares the targeting of the Machinelegalizer class for PowerPC
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_GISEL_PPCMACHINELEGALIZER_H
#define LLVM_LIB_TARGET_POWERPC_GISEL_PPCMACHINELEGALIZER_H

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

namespace llvm {

class PPCSubtarget;

/// This class provides the information for the PowerPC target legalizer for
/// GlobalISel.
class PPCLegalizerInfo : public LegalizerInfo {
public:
  PPCLegalizerInfo(const PPCSubtarget &ST);
};
} // namespace llvm
#endif
