//===- BPFLegalizerInfo.h ----------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares the targeting of the Machinelegalizer class for BPF
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_GISEL_BPFMACHINELEGALIZER_H
#define LLVM_LIB_TARGET_BPF_GISEL_BPFMACHINELEGALIZER_H

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

namespace llvm {

class BPFSubtarget;

/// This class provides the information for the BPF target legalizer for
/// GlobalISel.
class BPFLegalizerInfo : public LegalizerInfo {
public:
  BPFLegalizerInfo(const BPFSubtarget &ST);
};
} // namespace llvm
#endif
