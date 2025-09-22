//===-- SIProgramInfo.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// The SIProgramInfo tracks resource usage and hardware flags for kernels and
/// entry functions.
//
//===----------------------------------------------------------------------===//
//

#include "SIProgramInfo.h"
#include "GCNSubtarget.h"
#include "SIDefines.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/MC/MCExpr.h"

using namespace llvm;

void SIProgramInfo::reset(const MachineFunction &MF) {
  MCContext &Ctx = MF.getContext();

  const MCExpr *ZeroExpr = MCConstantExpr::create(0, Ctx);

  VGPRBlocks = ZeroExpr;
  SGPRBlocks = ZeroExpr;
  Priority = 0;
  FloatMode = 0;
  Priv = 0;
  DX10Clamp = 0;
  DebugMode = 0;
  IEEEMode = 0;
  WgpMode = 0;
  MemOrdered = 0;
  RrWgMode = 0;
  ScratchSize = ZeroExpr;

  LDSBlocks = 0;
  ScratchBlocks = ZeroExpr;

  ScratchEnable = ZeroExpr;
  UserSGPR = 0;
  TrapHandlerEnable = 0;
  TGIdXEnable = 0;
  TGIdYEnable = 0;
  TGIdZEnable = 0;
  TGSizeEnable = 0;
  TIdIGCompCount = 0;
  EXCPEnMSB = 0;
  LdsSize = 0;
  EXCPEnable = 0;

  ComputePGMRSrc3GFX90A = ZeroExpr;

  NumVGPR = ZeroExpr;
  NumArchVGPR = ZeroExpr;
  NumAccVGPR = ZeroExpr;
  AccumOffset = ZeroExpr;
  TgSplit = 0;
  NumSGPR = ZeroExpr;
  SGPRSpill = 0;
  VGPRSpill = 0;
  LDSSize = 0;
  FlatUsed = ZeroExpr;

  NumSGPRsForWavesPerEU = ZeroExpr;
  NumVGPRsForWavesPerEU = ZeroExpr;
  Occupancy = ZeroExpr;
  DynamicCallStack = ZeroExpr;
  VCCUsed = ZeroExpr;
}

static uint64_t getComputePGMRSrc1Reg(const SIProgramInfo &ProgInfo,
                                      const GCNSubtarget &ST) {
  uint64_t Reg = S_00B848_PRIORITY(ProgInfo.Priority) |
                 S_00B848_FLOAT_MODE(ProgInfo.FloatMode) |
                 S_00B848_PRIV(ProgInfo.Priv) |
                 S_00B848_DEBUG_MODE(ProgInfo.DebugMode) |
                 S_00B848_WGP_MODE(ProgInfo.WgpMode) |
                 S_00B848_MEM_ORDERED(ProgInfo.MemOrdered);

  if (ST.hasDX10ClampMode())
    Reg |= S_00B848_DX10_CLAMP(ProgInfo.DX10Clamp);

  if (ST.hasIEEEMode())
    Reg |= S_00B848_IEEE_MODE(ProgInfo.IEEEMode);

  if (ST.hasRrWGMode())
    Reg |= S_00B848_RR_WG_MODE(ProgInfo.RrWgMode);

  return Reg;
}

static uint64_t getPGMRSrc1Reg(const SIProgramInfo &ProgInfo,
                               CallingConv::ID CC, const GCNSubtarget &ST) {
  uint64_t Reg = S_00B848_PRIORITY(ProgInfo.Priority) |
                 S_00B848_FLOAT_MODE(ProgInfo.FloatMode) |
                 S_00B848_PRIV(ProgInfo.Priv) |
                 S_00B848_DEBUG_MODE(ProgInfo.DebugMode);

  if (ST.hasDX10ClampMode())
    Reg |= S_00B848_DX10_CLAMP(ProgInfo.DX10Clamp);

  if (ST.hasIEEEMode())
    Reg |= S_00B848_IEEE_MODE(ProgInfo.IEEEMode);

  if (ST.hasRrWGMode())
    Reg |= S_00B848_RR_WG_MODE(ProgInfo.RrWgMode);

  switch (CC) {
  case CallingConv::AMDGPU_PS:
    Reg |= S_00B028_MEM_ORDERED(ProgInfo.MemOrdered);
    break;
  case CallingConv::AMDGPU_VS:
    Reg |= S_00B128_MEM_ORDERED(ProgInfo.MemOrdered);
    break;
  case CallingConv::AMDGPU_GS:
    Reg |= S_00B228_WGP_MODE(ProgInfo.WgpMode) |
           S_00B228_MEM_ORDERED(ProgInfo.MemOrdered);
    break;
  case CallingConv::AMDGPU_HS:
    Reg |= S_00B428_WGP_MODE(ProgInfo.WgpMode) |
           S_00B428_MEM_ORDERED(ProgInfo.MemOrdered);
    break;
  default:
    break;
  }
  return Reg;
}

static uint64_t getComputePGMRSrc2Reg(const SIProgramInfo &ProgInfo) {
  uint64_t Reg = S_00B84C_USER_SGPR(ProgInfo.UserSGPR) |
                 S_00B84C_TRAP_HANDLER(ProgInfo.TrapHandlerEnable) |
                 S_00B84C_TGID_X_EN(ProgInfo.TGIdXEnable) |
                 S_00B84C_TGID_Y_EN(ProgInfo.TGIdYEnable) |
                 S_00B84C_TGID_Z_EN(ProgInfo.TGIdZEnable) |
                 S_00B84C_TG_SIZE_EN(ProgInfo.TGSizeEnable) |
                 S_00B84C_TIDIG_COMP_CNT(ProgInfo.TIdIGCompCount) |
                 S_00B84C_EXCP_EN_MSB(ProgInfo.EXCPEnMSB) |
                 S_00B84C_LDS_SIZE(ProgInfo.LdsSize) |
                 S_00B84C_EXCP_EN(ProgInfo.EXCPEnable);

  return Reg;
}

static const MCExpr *MaskShift(const MCExpr *Val, uint32_t Mask, uint32_t Shift,
                               MCContext &Ctx) {
  if (Mask) {
    const MCExpr *MaskExpr = MCConstantExpr::create(Mask, Ctx);
    Val = MCBinaryExpr::createAnd(Val, MaskExpr, Ctx);
  }
  if (Shift) {
    const MCExpr *ShiftExpr = MCConstantExpr::create(Shift, Ctx);
    Val = MCBinaryExpr::createShl(Val, ShiftExpr, Ctx);
  }
  return Val;
}

const MCExpr *SIProgramInfo::getComputePGMRSrc1(const GCNSubtarget &ST,
                                                MCContext &Ctx) const {
  uint64_t Reg = getComputePGMRSrc1Reg(*this, ST);
  const MCExpr *RegExpr = MCConstantExpr::create(Reg, Ctx);
  const MCExpr *Res = MCBinaryExpr::createOr(
      MaskShift(VGPRBlocks, /*Mask=*/0x3F, /*Shift=*/0, Ctx),
      MaskShift(SGPRBlocks, /*Mask=*/0xF, /*Shift=*/6, Ctx), Ctx);
  return MCBinaryExpr::createOr(RegExpr, Res, Ctx);
}

const MCExpr *SIProgramInfo::getPGMRSrc1(CallingConv::ID CC,
                                         const GCNSubtarget &ST,
                                         MCContext &Ctx) const {
  if (AMDGPU::isCompute(CC)) {
    return getComputePGMRSrc1(ST, Ctx);
  }

  uint64_t Reg = getPGMRSrc1Reg(*this, CC, ST);
  const MCExpr *RegExpr = MCConstantExpr::create(Reg, Ctx);
  const MCExpr *Res = MCBinaryExpr::createOr(
      MaskShift(VGPRBlocks, /*Mask=*/0x3F, /*Shift=*/0, Ctx),
      MaskShift(SGPRBlocks, /*Mask=*/0xF, /*Shift=*/6, Ctx), Ctx);
  return MCBinaryExpr::createOr(RegExpr, Res, Ctx);
}

const MCExpr *SIProgramInfo::getComputePGMRSrc2(MCContext &Ctx) const {
  uint64_t Reg = getComputePGMRSrc2Reg(*this);
  const MCExpr *RegExpr = MCConstantExpr::create(Reg, Ctx);
  return MCBinaryExpr::createOr(ScratchEnable, RegExpr, Ctx);
}

const MCExpr *SIProgramInfo::getPGMRSrc2(CallingConv::ID CC,
                                         MCContext &Ctx) const {
  if (AMDGPU::isCompute(CC))
    return getComputePGMRSrc2(Ctx);

  return MCConstantExpr::create(0, Ctx);
}
