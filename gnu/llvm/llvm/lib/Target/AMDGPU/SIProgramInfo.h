//===--- SIProgramInfo.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines struct to track resource usage and hardware flags for kernels and
/// entry functions.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIPROGRAMINFO_H
#define LLVM_LIB_TARGET_AMDGPU_SIPROGRAMINFO_H

#include "llvm/IR/CallingConv.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class GCNSubtarget;
class MCContext;
class MCExpr;
class MachineFunction;

/// Track resource usage for kernels / entry functions.
struct LLVM_EXTERNAL_VISIBILITY SIProgramInfo {
    // Fields set in PGM_RSRC1 pm4 packet.
    const MCExpr *VGPRBlocks = nullptr;
    const MCExpr *SGPRBlocks = nullptr;
    uint32_t Priority = 0;
    uint32_t FloatMode = 0;
    uint32_t Priv = 0;
    uint32_t DX10Clamp = 0;
    uint32_t DebugMode = 0;
    uint32_t IEEEMode = 0;
    uint32_t WgpMode = 0; // GFX10+
    uint32_t MemOrdered = 0; // GFX10+
    uint32_t RrWgMode = 0;   // GFX12+
    const MCExpr *ScratchSize = nullptr;

    // State used to calculate fields set in PGM_RSRC2 pm4 packet.
    uint32_t LDSBlocks = 0;
    const MCExpr *ScratchBlocks = nullptr;

    // Fields set in PGM_RSRC2 pm4 packet
    const MCExpr *ScratchEnable = nullptr;
    uint32_t UserSGPR = 0;
    uint32_t TrapHandlerEnable = 0;
    uint32_t TGIdXEnable = 0;
    uint32_t TGIdYEnable = 0;
    uint32_t TGIdZEnable = 0;
    uint32_t TGSizeEnable = 0;
    uint32_t TIdIGCompCount = 0;
    uint32_t EXCPEnMSB = 0;
    uint32_t LdsSize = 0;
    uint32_t EXCPEnable = 0;

    const MCExpr *ComputePGMRSrc3GFX90A = nullptr;

    const MCExpr *NumVGPR = nullptr;
    const MCExpr *NumArchVGPR = nullptr;
    const MCExpr *NumAccVGPR = nullptr;
    const MCExpr *AccumOffset = nullptr;
    uint32_t TgSplit = 0;
    const MCExpr *NumSGPR = nullptr;
    unsigned SGPRSpill = 0;
    unsigned VGPRSpill = 0;
    uint32_t LDSSize = 0;
    const MCExpr *FlatUsed = nullptr;

    // Number of SGPRs that meets number of waves per execution unit request.
    const MCExpr *NumSGPRsForWavesPerEU = nullptr;

    // Number of VGPRs that meets number of waves per execution unit request.
    const MCExpr *NumVGPRsForWavesPerEU = nullptr;

    // Final occupancy.
    const MCExpr *Occupancy = nullptr;

    // Whether there is recursion, dynamic allocas, indirect calls or some other
    // reason there may be statically unknown stack usage.
    const MCExpr *DynamicCallStack = nullptr;

    // Bonus information for debugging.
    const MCExpr *VCCUsed = nullptr;

    SIProgramInfo() = default;

    // The constructor sets the values for each member as shown in the struct.
    // However, setting the MCExpr members to their zero value equivalent
    // happens in reset together with (duplicated) value re-set for the
    // non-MCExpr members.
    void reset(const MachineFunction &MF);

    /// Compute the value of the ComputePGMRsrc1 register.
    const MCExpr *getComputePGMRSrc1(const GCNSubtarget &ST,
                                     MCContext &Ctx) const;
    const MCExpr *getPGMRSrc1(CallingConv::ID CC, const GCNSubtarget &ST,
                              MCContext &Ctx) const;

    /// Compute the value of the ComputePGMRsrc2 register.
    const MCExpr *getComputePGMRSrc2(MCContext &Ctx) const;
    const MCExpr *getPGMRSrc2(CallingConv::ID CC, MCContext &Ctx) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_SIPROGRAMINFO_H
