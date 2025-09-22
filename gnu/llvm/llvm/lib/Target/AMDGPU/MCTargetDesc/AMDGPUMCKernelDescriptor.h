//===--- AMDGPUMCKernelDescriptor.h ---------------------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDHSA kernel descriptor MCExpr struct for use in MC layer. Uses
/// AMDHSAKernelDescriptor.h for sizes and constants.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCKERNELDESCRIPTOR_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCKERNELDESCRIPTOR_H

#include "llvm/Support/AMDHSAKernelDescriptor.h"

namespace llvm {
class MCExpr;
class MCContext;
class MCSubtargetInfo;
namespace AMDGPU {

struct MCKernelDescriptor {
  const MCExpr *group_segment_fixed_size = nullptr;
  const MCExpr *private_segment_fixed_size = nullptr;
  const MCExpr *kernarg_size = nullptr;
  const MCExpr *compute_pgm_rsrc3 = nullptr;
  const MCExpr *compute_pgm_rsrc1 = nullptr;
  const MCExpr *compute_pgm_rsrc2 = nullptr;
  const MCExpr *kernel_code_properties = nullptr;
  const MCExpr *kernarg_preload = nullptr;

  static MCKernelDescriptor
  getDefaultAmdhsaKernelDescriptor(const MCSubtargetInfo *STI, MCContext &Ctx);
  // MCExpr for:
  // Dst = Dst & ~Mask
  // Dst = Dst | (Value << Shift)
  static void bits_set(const MCExpr *&Dst, const MCExpr *Value, uint32_t Shift,
                       uint32_t Mask, MCContext &Ctx);

  // MCExpr for:
  // return (Src & Mask) >> Shift
  static const MCExpr *bits_get(const MCExpr *Src, uint32_t Shift,
                                uint32_t Mask, MCContext &Ctx);
};

} // end namespace AMDGPU
} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUMCKERNELDESCRIPTOR_H
