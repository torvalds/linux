//===---- CSKYAttributes.h - CSKY Attributes --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains enumerations for CSKY attributes.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_CSKYATTRIBUTES_H
#define LLVM_SUPPORT_CSKYATTRIBUTES_H

#include "llvm/Support/ELFAttributes.h"

namespace llvm {
namespace CSKYAttrs {

const TagNameMap &getCSKYAttributeTags();

enum AttrType {
  CSKY_ARCH_NAME = 4,
  CSKY_CPU_NAME = 5,
  CSKY_ISA_FLAGS = 6,
  CSKY_ISA_EXT_FLAGS = 7,
  CSKY_DSP_VERSION = 8,
  CSKY_VDSP_VERSION = 9,
  CSKY_FPU_VERSION = 16,
  CSKY_FPU_ABI = 17,
  CSKY_FPU_ROUNDING = 18,
  CSKY_FPU_DENORMAL = 19,
  CSKY_FPU_EXCEPTION = 20,
  CSKY_FPU_NUMBER_MODULE = 21,
  CSKY_FPU_HARDFP = 22
};

enum ISA_FLAGS {
  V2_ISA_E1 = 1 << 1,
  V2_ISA_1E2 = 1 << 2,
  V2_ISA_2E3 = 1 << 3,
  V2_ISA_3E7 = 1 << 4,
  V2_ISA_7E10 = 1 << 5,
  V2_ISA_3E3R1 = 1 << 6,
  V2_ISA_3E3R2 = 1 << 7,
  V2_ISA_10E60 = 1 << 8,
  V2_ISA_3E3R3 = 1 << 9,
  ISA_TRUST = 1 << 11,
  ISA_CACHE = 1 << 12,
  ISA_NVIC = 1 << 13,
  ISA_CP = 1 << 14,
  ISA_MP = 1 << 15,
  ISA_MP_1E2 = 1 << 16,
  ISA_JAVA = 1 << 17,
  ISA_MAC = 1 << 18,
  ISA_MAC_DSP = 1 << 19,
  ISA_DSP = 1 << 20,
  ISA_DSP_1E2 = 1 << 21,
  ISA_DSP_ENHANCE = 1 << 22,
  ISA_DSP_SILAN = 1 << 23,
  ISA_VDSP = 1 << 24,
  ISA_VDSP_2 = 1 << 25,
  ISA_VDSP_2E3 = 1 << 26,
  V2_ISA_DSPE60 = 1 << 27,
  ISA_VDSP_2E60F = 1 << 28
};

enum ISA_EXT_FLAGS {
  ISA_FLOAT_E1 = 1 << 0,
  ISA_FLOAT_1E2 = 1 << 1,
  ISA_FLOAT_1E3 = 1 << 2,
  ISA_FLOAT_3E4 = 1 << 3,
  ISA_FLOAT_7E60 = 1 << 4
};

enum { NONE = 0, NEEDED = 1 };

enum DSP_VERSION { DSP_VERSION_EXTENSION = 1, DSP_VERSION_2 = 2 };

enum VDSP_VERSION { VDSP_VERSION_1 = 1, VDSP_VERSION_2 = 2 };

enum FPU_VERSION { FPU_VERSION_1 = 1, FPU_VERSION_2 = 2, FPU_VERSION_3 = 3 };

enum FPU_ABI { FPU_ABI_SOFT = 1, FPU_ABI_SOFTFP = 2, FPU_ABI_HARD = 3 };

enum FPU_HARDFP {
  FPU_HARDFP_HALF = 1,
  FPU_HARDFP_SINGLE = 2,
  FPU_HARDFP_DOUBLE = 4
};

} // namespace CSKYAttrs
} // namespace llvm

#endif
