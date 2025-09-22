//===--- AMDHSAKernelDescriptor.h -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMCKernelDescriptor.h"
#include "AMDGPUMCTargetDesc.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/TargetParser/TargetParser.h"

using namespace llvm;
using namespace llvm::AMDGPU;

MCKernelDescriptor
MCKernelDescriptor::getDefaultAmdhsaKernelDescriptor(const MCSubtargetInfo *STI,
                                                     MCContext &Ctx) {
  IsaVersion Version = getIsaVersion(STI->getCPU());

  MCKernelDescriptor KD;
  const MCExpr *ZeroMCExpr = MCConstantExpr::create(0, Ctx);
  const MCExpr *OneMCExpr = MCConstantExpr::create(1, Ctx);

  KD.group_segment_fixed_size = ZeroMCExpr;
  KD.private_segment_fixed_size = ZeroMCExpr;
  KD.compute_pgm_rsrc1 = ZeroMCExpr;
  KD.compute_pgm_rsrc2 = ZeroMCExpr;
  KD.compute_pgm_rsrc3 = ZeroMCExpr;
  KD.kernarg_size = ZeroMCExpr;
  KD.kernel_code_properties = ZeroMCExpr;
  KD.kernarg_preload = ZeroMCExpr;

  MCKernelDescriptor::bits_set(
      KD.compute_pgm_rsrc1,
      MCConstantExpr::create(amdhsa::FLOAT_DENORM_MODE_FLUSH_NONE, Ctx),
      amdhsa::COMPUTE_PGM_RSRC1_FLOAT_DENORM_MODE_16_64_SHIFT,
      amdhsa::COMPUTE_PGM_RSRC1_FLOAT_DENORM_MODE_16_64, Ctx);
  if (Version.Major < 12) {
    MCKernelDescriptor::bits_set(
        KD.compute_pgm_rsrc1, OneMCExpr,
        amdhsa::COMPUTE_PGM_RSRC1_GFX6_GFX11_ENABLE_DX10_CLAMP_SHIFT,
        amdhsa::COMPUTE_PGM_RSRC1_GFX6_GFX11_ENABLE_DX10_CLAMP, Ctx);
    MCKernelDescriptor::bits_set(
        KD.compute_pgm_rsrc1, OneMCExpr,
        amdhsa::COMPUTE_PGM_RSRC1_GFX6_GFX11_ENABLE_IEEE_MODE_SHIFT,
        amdhsa::COMPUTE_PGM_RSRC1_GFX6_GFX11_ENABLE_IEEE_MODE, Ctx);
  }
  MCKernelDescriptor::bits_set(
      KD.compute_pgm_rsrc2, OneMCExpr,
      amdhsa::COMPUTE_PGM_RSRC2_ENABLE_SGPR_WORKGROUP_ID_X_SHIFT,
      amdhsa::COMPUTE_PGM_RSRC2_ENABLE_SGPR_WORKGROUP_ID_X, Ctx);
  if (Version.Major >= 10) {
    if (STI->getFeatureBits().test(FeatureWavefrontSize32))
      MCKernelDescriptor::bits_set(
          KD.kernel_code_properties, OneMCExpr,
          amdhsa::KERNEL_CODE_PROPERTY_ENABLE_WAVEFRONT_SIZE32_SHIFT,
          amdhsa::KERNEL_CODE_PROPERTY_ENABLE_WAVEFRONT_SIZE32, Ctx);
    if (!STI->getFeatureBits().test(FeatureCuMode))
      MCKernelDescriptor::bits_set(
          KD.compute_pgm_rsrc1, OneMCExpr,
          amdhsa::COMPUTE_PGM_RSRC1_GFX10_PLUS_WGP_MODE_SHIFT,
          amdhsa::COMPUTE_PGM_RSRC1_GFX10_PLUS_WGP_MODE, Ctx);

    MCKernelDescriptor::bits_set(
        KD.compute_pgm_rsrc1, OneMCExpr,
        amdhsa::COMPUTE_PGM_RSRC1_GFX10_PLUS_MEM_ORDERED_SHIFT,
        amdhsa::COMPUTE_PGM_RSRC1_GFX10_PLUS_MEM_ORDERED, Ctx);
  }
  if (AMDGPU::isGFX90A(*STI) && STI->getFeatureBits().test(FeatureTgSplit))
    MCKernelDescriptor::bits_set(
        KD.compute_pgm_rsrc3, OneMCExpr,
        amdhsa::COMPUTE_PGM_RSRC3_GFX90A_TG_SPLIT_SHIFT,
        amdhsa::COMPUTE_PGM_RSRC3_GFX90A_TG_SPLIT, Ctx);
  return KD;
}

void MCKernelDescriptor::bits_set(const MCExpr *&Dst, const MCExpr *Value,
                                  uint32_t Shift, uint32_t Mask,
                                  MCContext &Ctx) {
  auto Sft = MCConstantExpr::create(Shift, Ctx);
  auto Msk = MCConstantExpr::create(Mask, Ctx);
  Dst = MCBinaryExpr::createAnd(Dst, MCUnaryExpr::createNot(Msk, Ctx), Ctx);
  Dst = MCBinaryExpr::createOr(Dst, MCBinaryExpr::createShl(Value, Sft, Ctx),
                               Ctx);
}

const MCExpr *MCKernelDescriptor::bits_get(const MCExpr *Src, uint32_t Shift,
                                           uint32_t Mask, MCContext &Ctx) {
  auto Sft = MCConstantExpr::create(Shift, Ctx);
  auto Msk = MCConstantExpr::create(Mask, Ctx);
  return MCBinaryExpr::createLShr(MCBinaryExpr::createAnd(Src, Msk, Ctx), Sft,
                                  Ctx);
}
